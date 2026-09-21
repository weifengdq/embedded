/**********************************************************************************************************************
 * \file Cpu0_Main.c
 * \brief tc397_selftest - combined peripheral self-test firmware for the TC397XX AppKit (292pin).
 *
 * One firmware, one serial console, every bench peripheral:
 *
 *   ASCLIN0  P14.0/P14.1   921600 8N1             letter-shell console (COM165)
 *   EVADC    48 channels   AN0..AN47              supply rail / divider monitor
 *   MCMCAN   12 nodes      CAN0..CAN11            pairs wired CAN0-CAN1 ... CAN10-CAN11
 *   ERAY0/1  P02.x / P14.x  FR0A + FR1A            channel A of both nodes looped
 *   QSPI2    P15.6/15.7/15.8 + nCS P14.2   TLF35584 safety SBC (MPS high = TestMode)
 *   SDMMC0   P15.1/15.3 + P20.7/8/10/11    SD card + FatFS (32GB FAT32)
 *   QSPI4    P22.0/22.2/22.3 + P33.13      LAN8651 10BASE-T1S   -> lwIP netif t10
 *   GETH     RGMII P11.x + P12.x           YT8011AN 1000BASE-T1 -> lwIP netif en0
 *
 * lwIP runs one stack with two netifs (GETH stays the default route):
 *   en0  192.168.0.100/24  gw 192.168.0.1   (PC "Ethernet 2"  = 192.168.0.2)
 *   t10  192.168.1.100/24  gw 192.168.1.1   (PC "Ethernet 16" = 192.168.1.1)
 * Servers: iperf2 TCP 5001, UDP echo 9, GETH diag UDP 5002/5003.
 *
 * See README.md for the procedures, the raw logs and the measured performance
 * numbers; `selftest` runs the automatic factory check, `bench` the throughput
 * measurements.
 *
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
#include "shell_can.h"
#include "Dts/Dts/IfxDts_Dts.h"
#include "Dts/Std/IfxDts.h"
#include "IfxScu_reg.h"
#include "IfxPms_reg.h"
#include "IfxGeth_reg.h"
#include "adc.h"
#include "can12.h"
#include "tlf35584.h"
#include "flexray_dual.h"
#include "selftest.h"
#include "shell_sd.h"
#include <stdio.h>
#include <string.h>

IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0;
/* g_TickCount_1ms is defined in Ifx_Lwip.c (shared with lwIP sys_now + shell tick) */
extern volatile uint32 g_TickCount_1ms;

#define UDP_ECHO_PORT 9U

static struct udp_pcb *g_udpEchoPcb = NULL_PTR;

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
    char ipAddrText[16];
    char netMaskText[16];
    char gatewayText[16];
    uint32 lastShellTick = 0;

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
        const char *msg = "\r\nTC397 COMBINED PERIPHERAL SELF-TEST\r\n";
        Ifx_SizeT cnt = strlen(msg);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg, &cnt, TIME_INFINITE);
        const char *msg2 = "ADC | CAN x12 | FlexRay | TLF35584 | SD | 10BASE-T1S | 1000BASE-T1 | UART shell\r\n";
        cnt = strlen(msg2);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg2, &cnt, TIME_INFINITE);
        const char *msg3 = "Console: ASCLIN0 P14.0/P14.1 921600 8N1.  Try 'selftest' or 'help'\r\n";
        cnt = strlen(msg3);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg3, &cnt, TIME_INFINITE);
    }
    {
        uint32_t chipId = SCU_CHIPID.U;
        char buf[160];
        int len = snprintf(buf, sizeof(buf), "ChipID: 0x%08lX CHREV=0x%02lX  SCU_ID: 0x%08lX RSTSTAT: 0x%08lX\r\n",
                           (unsigned long)chipId, (unsigned long)SCU_CHIPID.B.CHREV,
                           (unsigned long)SCU_ID.U, (unsigned long)SCU_RSTSTAT.U);
        Ifx_SizeT cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
        len = snprintf(buf, sizeof(buf), "STM: %lu Hz  CPU: %lu Hz  uptime: %lu ms\r\n",
                       (unsigned long)IfxStm_getFrequency(&MODULE_STM0),
                       (unsigned long)IfxScuCcu_getCpuFrequency(IfxCpu_getCoreIndex()),
                       (unsigned long)g_TickCount_1ms);
        cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
        uint16_t raw = IfxDts_getTemperatureValue();
        float c = IfxDts_Dts_convertToCelsius(raw);
        len = snprintf(buf, sizeof(buf), "DTS raw=0x%04X -> %.2f C\r\n", (unsigned)raw, (double)c);
        cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
    }

    /* LED on briefly to show boot */
    IfxPort_setPinLow(&MODULE_P13, 0);

    /* ---------------------------------------------------------------- */
    /* Peripheral bring-up                                               */
    /* ---------------------------------------------------------------- */

    /* --- TLF35584 safety SBC on QSPI2 (MPS high = TestMode, so the INIT
     *     watchdog is stopped and the boot sequence can run at leisure). --- */
    Tlf_Init();
#if TLF_AUTO_INIT
    {
        uint8 pre  = Tlf_Read(TLF_DEVSTAT);
        boolean ok = Tlf_AutoInit();
        uint8 post = Tlf_Read(TLF_DEVSTAT);
        Ifx_Lwip_printf("TLF35584: auto-init %s (DEVSTAT 0x%02X -> 0x%02X %s, SS1=%u)",
                        ok ? "OK" : "FAILED",
                        (unsigned)pre, (unsigned)post,
                        Tlf_StateName(post & 0x07u), (unsigned)Tlf_Ss1Level());
    }
#endif

    /* --- ADC: EVADC groups 0/1/2/3/8 queued scan over AN0..AN47 --- */
    Adc_Init();

    /* --- CAN: 12 MCMCAN nodes, 1 Mbit/s arbitration + 5 Mbit/s data --- */
    xcvr_init();
    can12_init_all_1M_5M();

    /* --- SD card detect pin (card init stays lazy: 'sd init' / selftest) --- */
    SdShell_InitPins();

    /* --- ERAY: nothing to do at boot; 'fr init' or the selftest runs the
     *     coldstart sequence (it re-configures the POC, so it is not part of
     *     the normal boot path). --- */

    /* ---------------------------------------------------------------- */
    /* Ethernet: GETH (1000BASE-T1) = netif 0, LAN8651 (10BASE-T1S) = 1  */
    /* ---------------------------------------------------------------- */
    IfxGeth_enableModule(&MODULE_GETH);

    eth_addr_t ethAddr;
    ethAddr.addr[0] = 0xDE;
    ethAddr.addr[1] = 0xAD;
    ethAddr.addr[2] = 0xBE;
    ethAddr.addr[3] = 0xEF;
    ethAddr.addr[4] = 0xFE;
    ethAddr.addr[5] = 0xED;

    ip_addr_t ipAddr    = IPADDR4_INIT_BYTES(192, 168,   0, 100);
    ip_addr_t netMask   = IPADDR4_INIT_BYTES(255, 255, 255,   0);
    ip_addr_t gateway   = IPADDR4_INIT_BYTES(192, 168,   0,   1);

    Ifx_Lwip_init_with_ip(ethAddr, ipAddr, netMask, gateway);

    /* LAN8651 10BASE-T1S as the second netif (192.168.1.100/24). */
    {
        uint8_t mac[6] = {
            LAN8651_MAC0, LAN8651_MAC1, LAN8651_MAC2,
            LAN8651_MAC3, LAN8651_MAC4, LAN8651_MAC5,
        };
        ip_addr_t t1Ip  = IPADDR4_INIT_BYTES(LAN8651_IP_ADDR0, LAN8651_IP_ADDR1,
                                             LAN8651_IP_ADDR2, LAN8651_IP_ADDR3);
        ip_addr_t t1Nm  = IPADDR4_INIT_BYTES(LAN8651_NETMASK0, LAN8651_NETMASK1,
                                             LAN8651_NETMASK2, LAN8651_NETMASK3);
        ip_addr_t t1Gw  = IPADDR4_INIT_BYTES(LAN8651_GATEWAY0, LAN8651_GATEWAY1,
                                             LAN8651_GATEWAY2, LAN8651_GATEWAY3);

        if (IfxLan8651_initHw(mac) != FALSE)
        {
            (void)IfxLan8651_netifAdd(t1Ip, t1Nm, t1Gw);
            ipaddr_ntoa_r(&t1Ip, ipAddrText, sizeof(ipAddrText));
            ipaddr_ntoa_r(&t1Nm, netMaskText, sizeof(netMaskText));
            ipaddr_ntoa_r(&t1Gw, gatewayText, sizeof(gatewayText));
            Ifx_Lwip_printf("LAN8651: IP=%s MASK=%s GW=%s",
                            ipAddrText, netMaskText, gatewayText);
        }
    }

#if LWIP_TCP
    lwiperf_start_tcp_server_default(lwiperf_report, NULL);
    Ifx_Lwip_printf("lwIP iperf server ready (TCP 5001)");
#endif
    udp_echo_init();
    Selftest_DiagUdpInit();

    while (1)
    {
        Ifx_Lwip_pollTimerFlags();
        /* Drains GETH and, internally, polls/drains the LAN8651 netif. */
        Ifx_Lwip_pollReceiveFlags();

        /* Shell is non-critical: poll at most 1kHz or when RX is pending, so
         * that the GETH path keeps its line rate. */
        if (Shell_HasPending() || (g_TickCount_1ms != lastShellTick))
        {
            UART_Poll();
            Shell_Process();
            lastShellTick = g_TickCount_1ms;
        }

        /* CAN: drain all 12 RX FIFOs into the shell rings */
        (void)can12_poll_all();
        if (shell_can_live_enabled())
        {
            shell_can_live_dump();
        }

        /* FlexRay: harvest received frames when the cluster is running */
        (void)frd_poll();

        /* TLF35584: watchdog trigger / deferred work */
        Tlf_Background(g_TickCount_1ms);
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
IFX_INTERRUPT (updateLwIPStackISR, 0, ISR_PRIORITY_OS_TICK) __attribute__((used));

/* ISR to update LwIP stack */
void updateLwIPStackISR(void)
{
    IfxStm_increaseCompare(&MODULE_STM0, IfxStm_Comparator_0, IFX_CFG_STM_TICKS_PER_MS);
    g_TickCount_1ms++;
    Ifx_Lwip_onTimerTick();
}
