/**********************************************************************************************************************
 * \file Cpu0_Main.c
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
#include "IfxGeth_Eth.h"
#include "Ifx_Console.h"
#include "Configuration.h"
#include "ConfigurationIsr.h"
#include "Ifx_Lwip.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/udp.h"
#include "Bsp.h"
#include "IfxGeth_reg.h"
#include "UART_Logging.h"
#include "shell_port.h"
#include "Dts/Dts/IfxDts_Dts.h"
#include "Dts/Std/IfxDts.h"
#include "IfxScu_reg.h"
#include "IfxPms_reg.h"
#include <stdio.h>
#include <string.h>

/*********************************************************************************************************************/
/*-------------------------------------------------Global variables--------------------------------------------------*/
/*********************************************************************************************************************/
IfxCpu_syncEvent cpuSyncEvent = 0;

#if LWIP_TCP
static void
lwiperf_report(void *arg, enum lwiperf_report_type report_type,
  const ip_addr_t* local_addr, u16_t local_port, const ip_addr_t* remote_addr, u16_t remote_port,
  u64_t bytes_transferred, u32_t ms_duration, u32_t bandwidth_kbitpsec)
{
  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(local_addr);
  LWIP_UNUSED_ARG(local_port);

  Ifx_Lwip_printf("IPERF report: type=%d, remote: %s:%d, total bytes: %llu, duration in ms: %"U32_F", kbits/s: %"U32_F"",
    (int)report_type, ipaddr_ntoa(remote_addr), (int)remote_port, (unsigned long long)bytes_transferred, ms_duration, bandwidth_kbitpsec);
}
#endif /* LWIP_TCP */

/*********************************************************************************************************************/
/*------------------------------------------UDP diagnostic service (port 5002)---------------------------------------*/
/*********************************************************************************************************************/
extern volatile uint32 g_diag_rx_ok, g_diag_rx_err, g_diag_rx_nobuf, g_diag_tx_pkts;
extern volatile uint32 g_prof_copy_ticks, g_prof_copy_cnt;
extern volatile uint32 g_prof_input_ticks, g_prof_input_cnt;
extern volatile uint32 g_prof_tx_ticks, g_prof_tx_cnt;
extern volatile uint32 g_prof_poll_gap_max, g_prof_poll_gap_1ms, g_diag_rbu;
extern volatile uint32 g_prof_busy_ticks, g_prof_idle_polls, g_prof_alloc_ticks;
extern uint32 isrRxCount, isrTxCount;
static volatile unsigned long g_diag_rdval;

static void diag_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    static char msg[512];
    static char cmd[64];
    int  n;
    LWIP_UNUSED_ARG(arg);
    if (p != NULL)
    {
        u16_t l = (p->tot_len < (u16_t)(sizeof(cmd) - 1)) ? p->tot_len : (u16_t)(sizeof(cmd) - 1);
        pbuf_copy_partial(p, cmd, l, 0);
        cmd[l] = '\0';
        pbuf_free(p);

        if (cmd[0] == 'f' && cmd[1] == 'c')
        {
            unsigned rfa = 1, rfd = 4, pt = 0x1000;
            if (sscanf(&cmd[2], "%u %u %u", &rfa, &rfd, &pt) == 3)
            {
                if (pt == 0u)
                {
                    GETH_MTL_RXQ0_OPERATION_MODE.B.EHFC = 0;
                    GETH_MAC_Q0_TX_FLOW_CTRL.U = 0;
                }
                else
                {
                    GETH_MTL_RXQ0_OPERATION_MODE.B.RFA  = rfa & 0xF;
                    GETH_MTL_RXQ0_OPERATION_MODE.B.RFD  = rfd & 0xF;
                    GETH_MTL_RXQ0_OPERATION_MODE.B.EHFC = 1;
                    GETH_MAC_Q0_TX_FLOW_CTRL.U = ((uint32)(pt & 0xFFFF) << 16) | (4u << 4) | (1u << 1);
                }
            }
        }
        else if (cmd[0] == 'w' && cmd[1] == 'r')
        {
            unsigned long off = 0, val = 0;
            if (sscanf(&cmd[2], "%lx %lx", &off, &val) == 2 && off < 0x2000ul)
            {
                *(volatile unsigned long *)(0xF001D000ul + (off & ~3ul)) = val;
            }
        }
        else if (cmd[0] == 'r' && cmd[1] == 'd')
        {
            unsigned long off = 0;
            if (sscanf(&cmd[2], "%lx", &off) == 1 && off < 0x2000ul)
            {
                g_diag_rdval = *(volatile unsigned long *)(0xF001D000ul + (off & ~3ul));
            }
        }
        else if (cmd[0] == 'z')
        {
            g_prof_copy_ticks = 0; g_prof_copy_cnt = 0;
            g_prof_input_ticks = 0; g_prof_input_cnt = 0;
            g_prof_tx_ticks = 0; g_prof_tx_cnt = 0;
            g_prof_busy_ticks = 0; g_prof_idle_polls = 0;
            g_prof_alloc_ticks = 0; g_diag_rx_ok = 0; g_diag_tx_pkts = 0;
        }
    }
    n = snprintf(msg, sizeof(msg),
        "rdval=0x%08lx tick=%lu isrRx=%lu isrTx=%lu rx_ok=%lu rx_err=%lu rx_nobuf=%lu tx=%lu "
        "hw_rxgb=%lu hw_crc=%lu hw_fifo_ovf=%lu hw_missed=%lu dma_ch0_status=0x%08lx "
        "copy_t=%lu copy_n=%lu input_t=%lu input_n=%lu tx_t=%lu tx_n=%lu "
        "gap_max=%lu gap1ms=%lu phy_cs=0x%08lx rbu=%lu dma_miss=%lu "
        "busy_t=%lu idle_p=%lu alloc_t=%lu txpause=%lu fcpu=%lu fstm=%lu fsri=%lu fgeth=%lu "
        "sysbus=0x%08lx rxctl=0x%08lx txctl=0x%08lx rxqop=0x%08lx rxqdbg=0x%08lx txqop=0x%08lx\n",
        g_diag_rdval,
        (unsigned long)g_TickCount_1ms,
        (unsigned long)isrRxCount, (unsigned long)isrTxCount,
        (unsigned long)g_diag_rx_ok, (unsigned long)g_diag_rx_err,
        (unsigned long)g_diag_rx_nobuf, (unsigned long)g_diag_tx_pkts,
        (unsigned long)GETH_RX_PACKETS_COUNT_GOOD_BAD.U,
        (unsigned long)GETH_RX_CRC_ERROR_PACKETS.U,
        (unsigned long)GETH_RX_FIFO_OVERFLOW_PACKETS.U,
        (unsigned long)GETH_MTL_RXQ0_MISSED_PACKET_OVERFLOW_CNT.U,
        (unsigned long)GETH_DMA_CH0_STATUS.U,
        (unsigned long)g_prof_copy_ticks, (unsigned long)g_prof_copy_cnt,
        (unsigned long)g_prof_input_ticks, (unsigned long)g_prof_input_cnt,
        (unsigned long)g_prof_tx_ticks, (unsigned long)g_prof_tx_cnt,
        (unsigned long)g_prof_poll_gap_max, (unsigned long)g_prof_poll_gap_1ms,
        (unsigned long)GETH_MAC_PHYIF_CONTROL_STATUS.U,
        (unsigned long)g_diag_rbu,
        (unsigned long)GETH_DMA_CH0_MISS_FRAME_CNT.U,
        (unsigned long)g_prof_busy_ticks, (unsigned long)g_prof_idle_polls,
        (unsigned long)g_prof_alloc_ticks,
        (unsigned long)GETH_TX_PAUSE_PACKETS.U,
        (unsigned long)IfxScuCcu_getCpuFrequency(IfxCpu_getCoreIndex()),
        (unsigned long)IfxStm_getFrequency(&MODULE_STM0),
        (unsigned long)IfxScuCcu_getSriFrequency(),
        (unsigned long)(IfxScuCcu_getSriFrequency() / (MODULE_SCU.CCUCON5.B.GETHDIV ? MODULE_SCU.CCUCON5.B.GETHDIV : 1u)),
        (unsigned long)GETH_DMA_SYSBUS_MODE.U,
        (unsigned long)GETH_DMA_CH0_RX_CONTROL.U,
        (unsigned long)GETH_DMA_CH0_TX_CONTROL.U,
        (unsigned long)GETH_MTL_RXQ0_OPERATION_MODE.U,
        (unsigned long)GETH_MTL_RXQ0_DEBUG.U,
        (unsigned long)GETH_MTL_TXQ0_OPERATION_MODE.U);
    if (n > 0)
    {
        struct pbuf *r = pbuf_alloc(PBUF_TRANSPORT, (u16_t)n, PBUF_RAM);
        if (r != NULL)
        {
            memcpy(r->payload, msg, (size_t)n);
            udp_sendto(pcb, r, addr, port);
            pbuf_free(r);
        }
    }
}

static void sink_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    LWIP_UNUSED_ARG(arg); LWIP_UNUSED_ARG(pcb);
    LWIP_UNUSED_ARG(addr); LWIP_UNUSED_ARG(port);
    if (p != NULL)
    {
        pbuf_free(p);
    }
}

static void diag_udp_init(void)
{
    struct udp_pcb *pcb = udp_new();
    if (pcb != NULL)
    {
        udp_bind(pcb, IP_ANY_TYPE, 5002);
        udp_recv(pcb, diag_udp_recv, NULL);
    }

    pcb = udp_new();
    if (pcb != NULL)
    {
        udp_bind(pcb, IP_ANY_TYPE, 5003);
        udp_recv(pcb, sink_udp_recv, NULL);
    }
}

/*********************************************************************************************************************/
/*---------------------------------------------Function Implementations----------------------------------------------*/
void core0_main (void)
{
    IfxCpu_enableInterrupts();
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());
    IfxCpu_emitEvent(&cpuSyncEvent);
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    // eth PHY RESET
    IfxPort_setPinModeOutput(&MODULE_P11,15, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinLow(&MODULE_P11,15);
    waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, 90));
    IfxPort_setPinHigh(&MODULE_P11,15);
    waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, 10));

    IfxStm_CompareConfig stmCompareConfig;
    IfxStm_initCompareConfig(&stmCompareConfig);
    stmCompareConfig.triggerPriority     = ISR_PRIORITY_OS_TICK;
    stmCompareConfig.comparatorInterrupt = IfxStm_ComparatorInterrupt_ir0;
    stmCompareConfig.ticks               = IFX_CFG_STM_TICKS_PER_MS * 10;
    stmCompareConfig.typeOfService       = IfxSrc_Tos_cpu0;
    IfxStm_initCompare(&MODULE_STM0, &stmCompareConfig);

    IfxPort_setPinMode(&MODULE_P00, 5, IfxPort_Mode_outputPushPullGeneral);
    IfxPort_setPinState(&MODULE_P00, 5, IfxPort_State_high);

    initUART();
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    {
        IfxDts_Dts_Config dtsConf;
        IfxDts_Dts_initModuleConfig(&dtsConf);
        dtsConf.lowerTemperatureLimit = -40;
        dtsConf.upperTemperatureLimit = 170;
        dtsConf.isrPriority = 0;
        IfxDts_Dts_initModule(&dtsConf);
    }

    Shell_Init();
    {
        const char *msg = "After Shell_Init direct\r\n";
        Ifx_SizeT cnt = strlen(msg);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg, &cnt, TIME_INFINITE);
    }
    Shell_PrintBanner();
    {
        const char *msg = "\r\nTC387 LwIP iperf + Letter-Shell\r\n";
        Ifx_SizeT cnt = strlen(msg);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg, &cnt, TIME_INFINITE);
        const char *msg2 = "Board: TC387 TriBoard TC2XX V2.0 (ASCLIN4 P00.9 TX / P00.12 RX, 921600)\r\n";
        cnt = strlen(msg2);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg2, &cnt, TIME_INFINITE);
        const char *msg3 = "Type 'help' for commands, 'ifconfig' for net, 'ping <ip>' to test\r\n";
        cnt = strlen(msg3);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg3, &cnt, TIME_INFINITE);
    }
    {
        uint32_t chipId = SCU_CHIPID.U;
        char buf[128];
        int len = snprintf(buf, sizeof(buf), "ChipID: 0x%08lX CHREV=0x%02lX\r\n", (unsigned long)chipId, (unsigned long)SCU_CHIPID.B.CHREV);
        Ifx_SizeT cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
        len = snprintf(buf, sizeof(buf), "SCU_ID: 0x%08lX RSTSTAT: 0x%08lX\r\n", (unsigned long)SCU_ID.U, (unsigned long)SCU_RSTSTAT.U);
        cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
        len = snprintf(buf, sizeof(buf), "STM Freq: %lu Hz Tick: %lu\r\n", (unsigned long)IfxStm_getFrequency(&MODULE_STM0), (unsigned long)g_TickCount_1ms);
        cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
        uint16_t raw = IfxDts_getTemperatureValue();
        float c = IfxDts_Dts_convertToCelsius(raw);
        len = snprintf(buf, sizeof(buf), "DTS raw=0x%04X -> %.2f C\r\n", (unsigned)raw, (double)c);
        cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
    }

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

#if LWIP_TCP
    lwiperf_start_tcp_server_default(lwiperf_report, NULL);
#endif
    diag_udp_init();

    {
        char buf[96];
        int len = snprintf(buf, sizeof(buf), "LWIP: MAC DE:AD:BE:EF:FE:ED IP 192.168.0.100/24 GW 192.168.0.1\r\n");
        Ifx_SizeT cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
        const char *msg = "iperf TCP server on 5001, diag UDP 5002, sink 5003\r\n";
        cnt = strlen(msg);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg, &cnt, TIME_INFINITE);
    }

    uint32_t lastShellTick = 0;
    while (1)
    {
        Ifx_Lwip_pollTimerFlags();
        Ifx_Lwip_pollReceiveFlags();
        /* Shell is non-critical: poll at most 1kHz or when RX pending, to keep GETH line-rate */
        if (Shell_HasPending() || g_TickCount_1ms != lastShellTick) {
            UART_Poll();
            Shell_Process();
            lastShellTick = g_TickCount_1ms;
        }
    }
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
