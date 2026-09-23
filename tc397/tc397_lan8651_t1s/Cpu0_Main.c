/**********************************************************************************************************************
 * \file Cpu0_Main.c
 * \brief TC397 + LAN8651 (10BASE-T1S, OA-TC6 over QSPI4) + LwIP + Letter-Shell.
 * Ported from lan8651/tc387_lan8651_lwip_iperf_gcc (QSPI2 P15.x -> QSPI4 P22.0/22.2/22.3 + P33.13,
 * nRST P15.4 -> P23.4, nINT P15.5 -> P33.7) merged with tc397_lwip_iperf shell/LED/DTS/UART0 base.
 * UART0 P14.0/P14.1 921600, LED P13.0 (low=on), STM 100MHz 1ms tick.
 * \copyright Copyright (C) Infineon Technologies AG 2019
 *********************************************************************************************************************/
#include "Ifx_Types.h"
#include "IfxStm.h"
#include "IfxScuWdt.h"
#include "IfxScuRcu.h"
#include "IfxScuCcu.h"
#include "IfxPort.h"
#include "IfxCpu.h"
#include "IfxCpu_Irq.h"
#include "Ifx_reg.h"
#include "Configuration.h"
#include "ConfigurationIsr.h"
#include "Ifx_Lwip.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/etharp.h"
#include "lwip/ip_addr.h"
#include "lwip/udp.h"
#include "lan8651.h"
#include "Bsp.h"
#include "UART_Logging.h"
#include "shell_port.h"
#include "Dts/Dts/IfxDts_Dts.h"
#include "Dts/Std/IfxDts.h"
#include "IfxScu_reg.h"
#include "IfxPms_reg.h"
#include <stdio.h>
#include <string.h>

IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0;
/* g_TickCount_1ms is defined in Ifx_Lwip.c (shared with lwIP sys_now + shell tick) */
extern volatile uint32 g_TickCount_1ms;

#define UDP_ECHO_PORT 9U

static struct udp_pcb *g_udpEchoPcb = NULL_PTR;
static uint8 g_arp_sent = 0U;

static void udp_echo_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
    const ip_addr_t *addr, u16_t port)
{
    LWIP_UNUSED_ARG(arg);

    if (p == NULL)
    {
        return;
    }

    udp_sendto(pcb, p, addr, port);
    pbuf_free(p);
}

static void udp_echo_init(void)
{
    err_t err;

    g_udpEchoPcb = udp_new_ip_type(IPADDR_TYPE_ANY);

    if (g_udpEchoPcb == NULL_PTR)
    {
        Ifx_Lwip_printf("UDP echo pcb alloc failed");
        return;
    }

    err = udp_bind(g_udpEchoPcb, IP_ANY_TYPE, UDP_ECHO_PORT);

    if (err != ERR_OK)
    {
        Ifx_Lwip_printf("UDP echo bind failed: %d", (int)err);
        udp_remove(g_udpEchoPcb);
        g_udpEchoPcb = NULL_PTR;
        return;
    }

    udp_recv(g_udpEchoPcb, udp_echo_recv, NULL_PTR);
    Ifx_Lwip_printf("UDP echo listening on port %u", (unsigned)UDP_ECHO_PORT);
}

#if LWIP_TCP
static void
lwiperf_report(void *arg, enum lwiperf_report_type report_type,
  const ip_addr_t* local_addr, u16_t local_port, const ip_addr_t* remote_addr, u16_t remote_port,
  u32_t bytes_transferred, u32_t ms_duration, u32_t bandwidth_kbitpsec)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(local_addr);
  LWIP_UNUSED_ARG(local_port);

  Ifx_Lwip_printf("IPERF report: type=%d, remote: %s:%d, total bytes: %"U32_F", duration in ms: %"U32_F", kbits/s: %"U32_F"\n",
    (int)report_type, ipaddr_ntoa(remote_addr), (int)remote_port, bytes_transferred, ms_duration, bandwidth_kbitpsec);
}
#endif /* LWIP_TCP */

void core0_main(void)
{
    boolean lastLinkUp = FALSE;
    char ipAddrText[16];
    char netMaskText[16];
    char gatewayText[16];

    IfxCpu_enableInterrupts();
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());
    IfxCpu_emitEvent(&cpuSyncEvent);
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    /* STM 1ms tick (also drives lwIP timers via updateLwIPStackISR) */
    IfxStm_CompareConfig stmCompareConfig;
    IfxStm_initCompareConfig(&stmCompareConfig);
    stmCompareConfig.triggerPriority     = ISR_PRIORITY_OS_TICK;
    stmCompareConfig.comparatorInterrupt = IfxStm_ComparatorInterrupt_ir0;
    stmCompareConfig.ticks               = IFX_CFG_STM_TICKS_PER_MS * 10;
    stmCompareConfig.typeOfService       = IfxSrc_Tos_cpu0;
    IfxStm_initCompare(&MODULE_STM0, &stmCompareConfig);

    /* P13.0 LED: output, high = off (low = on). Heartbeat in Shell_Process. */
    IfxPort_setPinMode(&MODULE_P13, 0, IfxPort_Mode_outputPushPullGeneral);
    IfxPort_setPinState(&MODULE_P13, 0, IfxPort_State_high);

    initUART();
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    /* DTS init */
    {
        IfxDts_Dts_Config dtsConf;
        IfxDts_Dts_initModuleConfig(&dtsConf);
        dtsConf.lowerTemperatureLimit = -40;
        dtsConf.upperTemperatureLimit = 170;
        dtsConf.isrPriority = 0;
        IfxDts_Dts_initModule(&dtsConf);
    }

    Shell_Init();
    Shell_PrintBanner();
    {
        const char *msg = "\r\nTC397 QSPI4 + LAN8651 10BASE-T1S + LwIP + Letter-Shell\r\n";
        Ifx_SizeT cnt = strlen(msg);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg, &cnt, TIME_INFINITE);
        const char *msg2 = "Board: TC397XX 292pin (ASCLIN0 P14.0 TX / P14.1 RX, 921600; QSPI4 P22.0/22.2/22.3+P33.13, nRST P23.4, nINT P33.7)\r\n";
        cnt = strlen(msg2);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg2, &cnt, TIME_INFINITE);
    }
    {
        uint32_t chipId = SCU_CHIPID.U;
        char buf[128];
        int len = snprintf(buf, sizeof(buf), "ChipID: 0x%08lX CHREV=0x%02lX\r\n", (unsigned long)chipId, (unsigned long)SCU_CHIPID.B.CHREV);
        Ifx_SizeT cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
        uint16_t raw = IfxDts_getTemperatureValue();
        float c = IfxDts_Dts_convertToCelsius(raw);
        len = snprintf(buf, sizeof(buf), "DTS raw=0x%04X -> %.2f C\r\n", (unsigned)raw, (double)c);
        cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
    }

    /* LAN8651 + lwIP init */
    {
        uint8_t mac[6] = {
            LAN8651_MAC0,
            LAN8651_MAC1,
            LAN8651_MAC2,
            LAN8651_MAC3,
            LAN8651_MAC4,
            LAN8651_MAC5,
        };
        eth_addr_t ethAddr;
        ip_addr_t ipAddr  = IPADDR4_INIT_BYTES(LAN8651_IP_ADDR0, LAN8651_IP_ADDR1, LAN8651_IP_ADDR2, LAN8651_IP_ADDR3);
        ip_addr_t netMask = IPADDR4_INIT_BYTES(LAN8651_NETMASK0, LAN8651_NETMASK1, LAN8651_NETMASK2, LAN8651_NETMASK3);
        ip_addr_t gateway = IPADDR4_INIT_BYTES(LAN8651_GATEWAY0, LAN8651_GATEWAY1, LAN8651_GATEWAY2, LAN8651_GATEWAY3);
        uint32_t oaConfig0 = 0U;
        uint32_t plcaCtrl0 = 0U;
        uint32_t plcaCtrl1 = 0U;
        uint32_t plcaTotmr = 0U;
        uint32_t plcaBurst = 0U;
        uint32_t plcaSts = 0U;
        uint32_t phyBmcr = 0U;
        uint32_t phyBmsr = 0U;
        uint32_t macNcr = 0U;
        uint32_t macNcfgr = 0U;

        ethAddr.addr[0] = mac[0];
        ethAddr.addr[1] = mac[1];
        ethAddr.addr[2] = mac[2];
        ethAddr.addr[3] = mac[3];
        ethAddr.addr[4] = mac[4];
        ethAddr.addr[5] = mac[5];

        Ifx_Lwip_printf("Booting TC397 LAN8651 firmware (QSPI4 20MHz, PLCA ID=%u CNT=%u)",
            (unsigned)LAN8651_PLCA_NODE_ID, (unsigned)LAN8651_PLCA_NODE_COUNT);

        if (lan8651_init(&g_lan8651,
                &MODULE_QSPI4,
                LAN8651_RST_PORT,
                LAN8651_RST_PIN,
                LAN8651_INT_PORT,
                LAN8651_INT_PIN,
                mac) != kLan8651Status_Ok)
        {
            Ifx_Lwip_printf("LAN8651 init failed");
            while (1)
            {
            }
        }

        if (lan8651_start(&g_lan8651) != kLan8651Status_Ok)
        {
            Ifx_Lwip_printf("LAN8651 start failed");
            while (1)
            {
            }
        }

        if ((lan8651_read_reg(&g_lan8651, LAN8651_OA_CONFIG0, &oaConfig0) == kLan8651Status_Ok) &&
            (lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL0, &plcaCtrl0) == kLan8651Status_Ok) &&
            (lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL1, &plcaCtrl1) == kLan8651Status_Ok) &&
            (lan8651_read_reg(&g_lan8651, LAN8651_PLCA_TOTMR, &plcaTotmr) == kLan8651Status_Ok) &&
            (lan8651_read_reg(&g_lan8651, LAN8651_PLCA_BURST, &plcaBurst) == kLan8651Status_Ok) &&
            (lan8651_read_reg(&g_lan8651, LAN8651_PLCA_STS, &plcaSts) == kLan8651Status_Ok) &&
            (lan8651_read_reg(&g_lan8651, LAN8651_PHY_BMCR, &phyBmcr) == kLan8651Status_Ok) &&
            (lan8651_read_reg(&g_lan8651, LAN8651_PHY_BMSR, &phyBmsr) == kLan8651Status_Ok) &&
            (lan8651_read_reg(&g_lan8651, LAN8651_MAC_NCR, &macNcr) == kLan8651Status_Ok) &&
            (lan8651_read_reg(&g_lan8651, LAN8651_MAC_NCFGR, &macNcfgr) == kLan8651Status_Ok))
        {
            Ifx_Lwip_printf("cfg readback ok (see t1stat)");
        }
        else
        {
            Ifx_Lwip_printf("cfg: register readback failed");
        }

        Ifx_Lwip_init_with_ip(ethAddr, ipAddr, netMask, gateway);
        ipaddr_ntoa_r(&ipAddr, ipAddrText, sizeof(ipAddrText));
        ipaddr_ntoa_r(&netMask, netMaskText, sizeof(netMaskText));
        ipaddr_ntoa_r(&gateway, gatewayText, sizeof(gatewayText));
        Ifx_Lwip_printf("LAN8651 started, MAC=%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        Ifx_Lwip_printf("Static IP=%s MASK=%s GW=%s",
            ipAddrText, netMaskText, gatewayText);
        udp_echo_init();
    }

#if LWIP_TCP
    lwiperf_start_tcp_server_default(lwiperf_report, NULL);
    Ifx_Lwip_printf("lwIP iperf server ready (TCP 5001)");
#endif

    /* LED on briefly to show boot */
    IfxPort_setPinLow(&MODULE_P13, 0);

    while (1)
    {
        Ifx_Lwip_pollTimerFlags();
        Ifx_Lwip_pollReceiveFlags();

        {
            boolean currentLinkUp = lan8651_link_up(&g_lan8651);

            if (currentLinkUp != lastLinkUp)
            {
                lastLinkUp = currentLinkUp;
                Ifx_Lwip_printf("LAN8651 link %s", lastLinkUp != FALSE ? "up" : "down");

                if ((lastLinkUp != FALSE) && (g_arp_sent == 0U))
                {
                    g_arp_sent = 1U;
                    if (etharp_gratuitous(&g_Lwip.netif) == ERR_OK)
                    {
                        Ifx_Lwip_printf("gratuitous_arp=sent");
                    }
                }
            }
        }

        UART_Poll();
        Shell_Process();
    }
}

IFX_INTERRUPT(ISR_Qspi4_Tx, 0, ISR_PRIORITY_QSPI4_TX);
void ISR_Qspi4_Tx(void)
{
    lan8651_qspi_tx_isr();
}

IFX_INTERRUPT(ISR_Qspi4_Rx, 0, ISR_PRIORITY_QSPI4_RX);
void ISR_Qspi4_Rx(void)
{
    lan8651_qspi_rx_isr();
}

IFX_INTERRUPT(ISR_Qspi4_Er, 0, ISR_PRIORITY_QSPI4_ER);
void ISR_Qspi4_Er(void)
{
    lan8651_qspi_er_isr();
}

/* This interrupt is raised by the STM0 */
IFX_INTERRUPT(updateLwIPStackISR, 0, ISR_PRIORITY_OS_TICK);

/* ISR to update LwIP stack */
void updateLwIPStackISR(void)
{
    /* Configure STM to generate an interrupt in 1 ms */
    IfxStm_increaseCompare(&MODULE_STM0, IfxStm_Comparator_0, IFX_CFG_STM_TICKS_PER_MS);

    g_TickCount_1ms++;                                      /* Increase LwIP system time */

    Ifx_Lwip_onTimerTick();
}
