#include "shell_port.h"
#include "shell.h"
#include "IfxAsclin_Asc.h"
#include "IfxCpu_Irq.h"
#include "IfxStm.h"
#include "IfxScuRcu.h"
#include "IfxScuCcu.h"
#include "IfxScuWdt.h"
#include "IfxPort.h"
#include "IfxGeth_Phy_Yt8011an.h"
#include "Dts/Dts/IfxDts_Dts.h"
#include "Dts/Std/IfxDts.h"
#include "IfxScu_reg.h"
#include "IfxPms_reg.h"
#include "Configuration.h"

/* TASKING has no GCC extended asm; use iLLD __dsync() instead (both toolchains). */
#if defined(__TASKING__)
#include "IfxCpu_Intrinsics.h"
#define SHELL_DSYNC() __dsync()
#else
#define SHELL_DSYNC() __asm__ volatile("dsync":::"memory")
#endif
#define _SSIZE_T_DECLARED
#include "Ifx_Lwip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "UART_Logging.h"
#include "IfxGeth_reg.h"
extern void UART_Poll(void);

/* UART handle is defined in UART_Logging.c as g_asc */
extern IfxAsclin_Asc g_asc;

/* letter-shell objects — enlarged for 921600 to avoid overrun on paste */
static Shell gShell;
static char gShellBuffer[1024];

#define SHELL_RX_RING_SIZE 1024
static volatile uint16_t gRxHead = 0;
static volatile uint16_t gRxTail = 0;
static volatile uint8_t gRxRing[SHELL_RX_RING_SIZE];
/* Overflow counter for diagnostics */
volatile uint32_t gShellRxOverflow = 0;

void Shell_RxPush(uint8_t ch)
{
    uint16_t next = (uint16_t)((gRxHead + 1U) % SHELL_RX_RING_SIZE);
    if (next != gRxTail) {
        gRxRing[gRxHead] = ch;
        SHELL_DSYNC();
        gRxHead = next;
        SHELL_DSYNC();
    } else {
        gShellRxOverflow++;
    }
}

/* Bulk push — used by ISR to reduce per-byte overhead */
void Shell_RxPushBulk(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; ++i) Shell_RxPush(data[i]);
}

int Shell_HasPending(void)
{
    return gRxHead != gRxTail;
}

static int Shell_RxPop(uint8_t *ch)
{
    if (ch == NULL || gRxHead == gRxTail) {
        return 0;
    }
    *ch = gRxRing[gRxTail];
    gRxTail = (uint16_t)((gRxTail + 1U) % SHELL_RX_RING_SIZE);
    return 1;
}

static short userShellWrite(char *data, unsigned short len)
{
    if (data == NULL || len == 0) {
        return 0;
    }
    Ifx_SizeT count = (Ifx_SizeT)len;
    /* Chunked write to keep TX ISR responsive at 921600 */
    Ifx_SizeT offset = 0;
    while (offset < count) {
        Ifx_SizeT chunk = (count - offset) > 128 ? 128 : (count - offset);
        Ifx_SizeT c = chunk;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)(data + offset), &c, TIME_INFINITE);
        if (c == 0) break;
        offset += c;
    }
    return (short)offset;
}

static short userShellRead(char *data, unsigned short len)
{
    if (data == NULL || len == 0) {
        return 0;
    }
    unsigned short i = 0;
    /* Snapshot head to avoid volatile re-read per iteration */
    uint16_t head = gRxHead;
    uint16_t tail = gRxTail;
    for (i = 0; i < len; i++) {
        if (head == tail) break;
        data[i] = (char)gRxRing[tail];
        tail = (uint16_t)((tail + 1U) % SHELL_RX_RING_SIZE);
    }
    gRxTail = tail;
    SHELL_DSYNC();
    return (short)i;
}

/* LED P13.0: low = on. Heartbeat enabled by default (1 Hz toggle). */
static volatile uint8_t gLedHeartbeat = 1;
static uint32_t gLedLastTick = 0;

static inline void Led_On(void)  { IfxPort_setPinLow(&MODULE_P13, 0); }
static inline void Led_Off(void) { IfxPort_setPinHigh(&MODULE_P13, 0); }
static inline void Led_Toggle(void) { IfxPort_togglePin(&MODULE_P13, 0); }

/* helpers */
static int parse_u32(const char *arg, uint32_t *value)
{
    char *end = NULL;
    unsigned long v;
    if (arg == NULL || value == NULL || *arg == '\0') return -1;
    v = strtoul(arg, &end, 0);
    if (end == NULL || *end != '\0') return -1;
    *value = (uint32_t)v;
    return 0;
}

/* ---------- MCU / System commands ---------- */
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 1
#define FW_VERSION_PATCH 0

static int cmd_version(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    shellPrint(shell, "\r\nTC397 Letter-Shell Firmware\r\n");
    shellPrint(shell, "Version : %d.%d.%d\r\n", FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    shellPrint(shell, "Build   : %s %s\r\n", __DATE__, __TIME__);
    shellPrint(shell, "Board   : TC397XX 292pin (ASCLIN0 P14.0/14.1 921600, RGMII YT8011AN)\r\n");
    return 0;
}

static int cmd_mcu(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32 chipId = SCU_CHIPID.U;
    shellPrint(shell, "\r\n=== MCU Info ===\r\n");
    shellPrint(shell, "ChipID  : 0x%08lX (CHREV=0x%02lX)\r\n", (unsigned long)chipId, (unsigned long)SCU_CHIPID.B.CHREV);
    shellPrint(shell, "SCU_ID  : 0x%08lX\r\n", (unsigned long)SCU_ID.U);
    shellPrint(shell, "RSTSTAT : 0x%08lX RSTCON: 0x%08lX\r\n", (unsigned long)SCU_RSTSTAT.U, (unsigned long)SCU_RSTCON.U);
    shellPrint(shell, "STM Freq: %lu Hz\r\n", (unsigned long)IfxStm_getFrequency(&MODULE_STM0));
    shellPrint(shell, "CCUCON0 : 0x%08lX CCUCON1: 0x%08lX\r\n", (unsigned long)SCU_CCUCON0.U, (unsigned long)SCU_CCUCON1.U);
    shellPrint(shell, "Build   : %s %s\r\n", __DATE__, __TIME__);
    shellPrint(shell, "================\r\n");
    return 0;
}

static int cmd_uid(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    shellPrint(shell, "UID/ChipID: 0x%08lX\r\n", (unsigned long)SCU_CHIPID.U);
    shellPrint(shell, "SCU_ID  : 0x%08lX\r\n", (unsigned long)SCU_ID.U);
    shellPrint(shell, "PMS DTSSTAT: 0x%04X\r\n", (unsigned)IfxDts_getTemperatureValue());
    return 0;
}

static int cmd_uptime(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32_t tick = g_TickCount_1ms;
    uint32_t sec = tick / 1000U;
    uint32_t ms = tick % 1000U;
    uint32_t min = sec / 60U;
    uint32_t hour = min / 60U;
    uint32_t day = hour / 24U;
    shellPrint(shell, "Uptime: %lu ms (%lu days %02lu:%02lu:%02lu.%03lu)\r\n",
           (unsigned long)tick,
           (unsigned long)day,
           (unsigned long)(hour % 24U),
           (unsigned long)(min % 60U),
           (unsigned long)(sec % 60U),
           (unsigned long)ms);
    return 0;
}

static int cmd_reset(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    shellPrint(shell, "MCU software reset in 500 ms...\r\n");
    uint32_t start = g_TickCount_1ms;
    while ((g_TickCount_1ms - start) < 500U) { }
    IfxScuRcu_performReset(IfxScuRcu_ResetType_system, 0);
    return 0;
}

static int cmd_temp(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint16 raw = IfxDts_getTemperatureValue();
    float32 c = IfxDts_Dts_convertToCelsius(raw);
    /* Avoid printf float in shellPrint if possible, but shellPrint supports float via vsnprintf */
    shellPrint(shell, "DTS raw=0x%04X (%u) -> %.2f C\r\n", (unsigned)raw, (unsigned)raw, (double)c);
    shellPrint(shell, "Limits: LOW=%d C HIGH=%d C\r\n", (int)IFXDTS_DEFAULT_TEMPERATURELIMIT_LOW, (int)IFXDTS_DEFAULT_TEMPERATURELIMIT_UPPER);
    shellPrint(shell, "PMS DTSSTAT RESULT field = %u\r\n", (unsigned)raw);
    return 0;
}

static int cmd_sysinfo(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    cmd_mcu(argc, argv);
    cmd_temp(argc, argv);
    cmd_uptime(argc, argv);
    shellPrint(shell, "sysinfo done\r\n");
    return 0;
}

/* ---------- Network / LwIP commands ---------- */
extern struct netif *netif_list;
extern struct netif *netif_default;

static int cmd_ifconfig(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    struct netif *netif;
    int idx = 0;
    for (netif = netif_list; netif != NULL; netif = netif->next) {
        shellPrint(shell, "netif %d: %c%c%d ", idx++,
            netif->name[0], netif->name[1], (int)netif->num);
        shellPrint(shell, "IP %s ", ipaddr_ntoa(&netif->ip_addr));
        shellPrint(shell, "NM %s ", ipaddr_ntoa(&netif->netmask));
        shellPrint(shell, "GW %s\r\n", ipaddr_ntoa(&netif->gw));
        shellPrint(shell, "  HWaddr %02X:%02X:%02X:%02X:%02X:%02X MTU %d flags 0x%02X\r\n",
            netif->hwaddr[0], netif->hwaddr[1], netif->hwaddr[2],
            netif->hwaddr[3], netif->hwaddr[4], netif->hwaddr[5],
            (int)netif->mtu, (int)netif->flags);
        shellPrint(shell, "  link %s\r\n", netif_is_link_up(netif) ? "UP" : "DOWN");
    }
    if (idx==0) shellPrint(shell, "no netif\r\n");
    return 0;
}

static int cmd_ethstat(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    extern volatile uint32 g_diag_rx_ok, g_diag_rx_err, g_diag_rx_nobuf, g_diag_tx_pkts;
    extern volatile uint32 g_diag_rbu;
    extern uint32 isrRxCount, isrTxCount;
    shellPrint(shell, "ETH rx_ok=%lu rx_err=%lu rx_nobuf=%lu tx=%lu rbu=%lu isrRx=%lu isrTx=%lu\r\n",
        (unsigned long)g_diag_rx_ok, (unsigned long)g_diag_rx_err,
        (unsigned long)g_diag_rx_nobuf, (unsigned long)g_diag_tx_pkts,
        (unsigned long)g_diag_rbu, (unsigned long)isrRxCount, (unsigned long)isrTxCount);
    shellPrint(shell, "sysbus=0x%08lX rxctl=0x%08lX txctl=0x%08lX\r\n",
        (unsigned long)GETH_DMA_SYSBUS_MODE.U,
        (unsigned long)GETH_DMA_CH0_RX_CONTROL.U,
        (unsigned long)GETH_DMA_CH0_TX_CONTROL.U);
    return 0;
}

/* Sample P11.IN 8x: bit5=GREFCLK(125M xtal), bit4=TXCLK(MAC out),
 * bit12=RXCLK(PHY out). A running clock reads back toggling 0/1. */
static int cmd_clks(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    for (int i = 0; i < 8; i++) {
        uint32 v = MODULE_P11.IN.U;
        shellPrint(shell, "P11.IN=0x%08lX GREFCLK=%lu TXCLK=%lu RXCLK=%lu\r\n",
            (unsigned long)v, (unsigned long)((v >> 5) & 1u),
            (unsigned long)((v >> 4) & 1u), (unsigned long)((v >> 12) & 1u));
    }
    /* Burst sample to defeat stroboscopic aliasing: live 125MHz -> ~50% ones */
    {
        int n1_5 = 0, n1_4 = 0, n1_12 = 0;
        for (int i = 0; i < 200; i++) {
            uint32 v = MODULE_P11.IN.U;
            n1_5 += (v >> 5) & 1u; n1_4 += (v >> 4) & 1u; n1_12 += (v >> 12) & 1u;
        }
        shellPrint(shell, "burst200: GREFCLK_ones=%d TXCLK_ones=%d RXCLK_ones=%d\r\n",
            n1_5, n1_4, n1_12);
    }
    return 0;
}

static int cmd_geth(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    shellPrint(shell, "MAC_CFG=0x%08lX PHYIF=0x%08lX SKEW=0x%08lX\r\n",
        (unsigned long)GETH_MAC_CONFIGURATION.U,
        (unsigned long)GETH_MAC_PHYIF_CONTROL_STATUS.U,
        (unsigned long)GETH_SKEWCTL.U);
    shellPrint(shell, "TX_GB=%lu RX_GB=%lu RX_CRC=%lu RX_ALIGN=%lu RX_OVF=%lu\r\n",
        (unsigned long)GETH_TX_PACKET_COUNT_GOOD_BAD.U,
        (unsigned long)GETH_RX_PACKETS_COUNT_GOOD_BAD.U,
        (unsigned long)GETH_RX_CRC_ERROR_PACKETS.U,
        (unsigned long)GETH_RX_ALIGNMENT_ERROR_PACKETS.U,
        (unsigned long)GETH_RX_FIFO_OVERFLOW_PACKETS.U);
    shellPrint(shell, "DMA_ST=0x%08lX MISS=0x%08lX RXQ_MISS_OVF=%lu TX_PAUSE=%lu\r\n",
        (unsigned long)GETH_DMA_CH0_STATUS.U,
        (unsigned long)GETH_DMA_CH0_MISS_FRAME_CNT.U,
        (unsigned long)GETH_MTL_RXQ0_MISSED_PACKET_OVERFLOW_CNT.U,
        (unsigned long)GETH_TX_PAUSE_PACKETS.U);
    shellPrint(shell, "MTL_RXQOP=0x%08lX RXQDBG=0x%08lX TXQOP=0x%08lX\r\n",
        (unsigned long)GETH_MTL_RXQ0_OPERATION_MODE.U,
        (unsigned long)GETH_MTL_RXQ0_DEBUG.U,
        (unsigned long)GETH_MTL_TXQ0_OPERATION_MODE.U);
    shellPrint(shell, "fGETH=%lu Hz SRI=%lu SPB=%lu GETHDIV=%u\r\n",
        (unsigned long)IfxScuCcu_getGethFrequency(),
        (unsigned long)IfxScuCcu_getSriFrequency(),
        (unsigned long)IfxScuCcu_getSpbFrequency(),
        (unsigned)MODULE_SCU.CCUCON5.B.GETHDIV);
    return 0;
}

/* ----- ping via RAW ICMP ----- */
static struct raw_pcb *ping_pcb = NULL;
static ip_addr_t ping_target;
static volatile uint32_t ping_got_reply = 0;
static uint32_t ping_seq = 0;
static uint16_t ping_id = 0x1234;
static uint32_t ping_sent_cnt = 0, ping_recv_cnt = 0;

static u8_t ping_recv_fn(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
    (void)arg; (void)pcb;
    if (!ip_addr_cmp(addr, &ping_target)) {
        return 0;
    }
    /* p->payload = IP header, strip to ICMP */
    if (p->tot_len < sizeof(struct ip_hdr) + sizeof(struct icmp_echo_hdr)) {
        return 0;
    }
    struct ip_hdr *iph = (struct ip_hdr*)p->payload;
    u16_t iph_len = IPH_HL(iph) * 4;
    if (p->len < iph_len + sizeof(struct icmp_echo_hdr)) {
        /* if header split across pbufs, copy */
        return 0;
    }
    struct icmp_echo_hdr *iecho = (struct icmp_echo_hdr*)((u8_t*)p->payload + iph_len);
    if (ICMPH_TYPE(iecho) == ICMP_ER && iecho->id == PP_HTONS(ping_id) && iecho->seqno == PP_HTONS((uint16_t)ping_seq)) {
        ping_got_reply = 1;
        ping_recv_cnt++;
        /* eat packet */
        pbuf_free(p);
        return 1;
    }
    return 0;
}

static int cmd_ping(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    if (argc < 2) {
        shellPrint(shell, "usage: ping <ip> [count] [size]\r\n");
        shellPrint(shell, "  ex: ping 192.168.0.1 4 32   (Ctrl+C to abort)\r\n");
        return -1;
    }
    ip_addr_t target;
    if (!ipaddr_aton(argv[1], &target)) {
        shellPrint(shell, "invalid IP: %s\r\n", argv[1]);
        return -1;
    }
    uint32_t count = 4;
    uint32_t size = 32;
    if (argc >= 3) {
        char *e=NULL; count = strtoul(argv[2], &e, 0); if (count==0) count=4;
    }
    if (argc >= 4) {
        char *e=NULL; size = strtoul(argv[3], &e, 0); if (size>1000) size=1000;
    }

    if (ping_pcb == NULL) {
        ping_pcb = raw_new(IP_PROTO_ICMP);
        if (ping_pcb == NULL) {
            shellPrint(shell, "ping: raw_new failed\r\n");
            return -1;
        }
        raw_recv(ping_pcb, ping_recv_fn, NULL);
        raw_bind(ping_pcb, IP_ADDR_ANY);
    }
    ping_target = target;
    ping_id = (uint16_t)(g_TickCount_1ms & 0xFFFF);
    if (ping_id==0) ping_id=0x1234;
    ping_sent_cnt = 0; ping_recv_cnt = 0;

    shellPrint(shell, "PING %s : %lu bytes count %lu\r\n", ipaddr_ntoa(&target), (unsigned long)size, (unsigned long)count);

    for (uint32_t i=0;i<count;i++) {
        ping_seq = i;
        ping_got_reply = 0;
        uint32_t start = g_TickCount_1ms;

        struct pbuf *p = pbuf_alloc(PBUF_IP, (u16_t)(sizeof(struct icmp_echo_hdr)+size), PBUF_RAM);
        if (p == NULL) {
            shellPrint(shell, "pbuf alloc failed\r\n");
            break;
        }
        struct icmp_echo_hdr *iecho = (struct icmp_echo_hdr*)p->payload;
        ICMPH_TYPE_SET(iecho, ICMP_ECHO);
        ICMPH_CODE_SET(iecho, 0);
        iecho->chksum = 0;
        iecho->id = PP_HTONS(ping_id);
        iecho->seqno = PP_HTONS((uint16_t)i);
        if (size) {
            memset((uint8_t*)p->payload + sizeof(struct icmp_echo_hdr), 0xA5, size);
        }
        iecho->chksum = inet_chksum(iecho, p->len);

        err_t err = raw_sendto(ping_pcb, p, &target);
        pbuf_free(p);
        if (err != ERR_OK) {
            shellPrint(shell, "sendto failed %d\r\n", (int)err);
            break;
        }
        ping_sent_cnt++;

        /* wait up to 1000ms for reply, polling lwIP and checking abort */
        uint32_t wait_start = g_TickCount_1ms;
        int got = 0;
        while ((g_TickCount_1ms - wait_start) < 1000) {
            /* poll lwIP to process incoming */
            Ifx_Lwip_pollReceiveFlags();
            uint16_t h = gRxHead, t = gRxTail;
            int abort = 0;
            while (t != h) {
                if (gRxRing[t] == 0x03) { abort = 1; break; }
                t = (t+1)%1024;
            }
            if (abort) {
                shellPrint(shell, "\r\nAborted by Ctrl+C\r\n");
                goto ping_done;
            }
            if (ping_got_reply) {
                uint32_t rtt = g_TickCount_1ms - start;
                shellPrint(shell, "Reply from %s: bytes=%lu seq=%lu time=%lu ms\r\n",
                    ipaddr_ntoa(&target), (unsigned long)size, (unsigned long)i, (unsigned long)rtt);
                got = 1;
                break;
            }
            /* avoid busy loop starving: small wait */
            // no delay needed, poll is tight
        }
        if (!got && !ping_got_reply) {
            shellPrint(shell, "Request timeout for icmp_seq %lu\r\n", (unsigned long)i);
        }
        /* inter-ping interval 1000ms, but allow abort polling */
        uint32_t inter_start = g_TickCount_1ms;
        while ((g_TickCount_1ms - inter_start) < 500 && i+1 < count) {
            Ifx_Lwip_pollReceiveFlags();
            UART_Poll();
            /* check abort again */
            uint16_t h2 = gRxHead, t2 = gRxTail;
            while (t2 != h2) { if (gRxRing[t2]==0x03) { shellPrint(shell, "\r\nAborted\r\n"); goto ping_done; } t2=(t2+1)%1024; }
            if ((g_TickCount_1ms - inter_start) >= 500) break;
        }
    }
ping_done:
    shellPrint(shell, "PING statistics: %lu sent, %lu received, %lu%% loss\r\n",
        (unsigned long)ping_sent_cnt, (unsigned long)ping_recv_cnt,
        ping_sent_cnt ? (unsigned long)((ping_sent_cnt - ping_recv_cnt)*100/ping_sent_cnt) : 0);
    return 0;
}

/* ---------- LED P13.0 (low = on) ---------- */
static int cmd_led(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    if (argc < 2) {
        shellPrint(shell, "Usage: led <on|off|toggle|blink|hb> [args]\r\n");
        shellPrint(shell, "  led on            - LED on (P13.0 low), heartbeat off\r\n");
        shellPrint(shell, "  led off           - LED off (P13.0 high), heartbeat off\r\n");
        shellPrint(shell, "  led toggle        - toggle once, heartbeat off\r\n");
        shellPrint(shell, "  led blink <n> [ms]- blink n times (default 5 x 200ms), heartbeat off\r\n");
        shellPrint(shell, "  led hb <on|off>   - heartbeat 1Hz toggle on/off (default on)\r\n");
        shellPrint(shell, "LED P13.0 low=on. heartbeat=%s\r\n", gLedHeartbeat ? "on" : "off");
        return 0;
    }
    if (strcmp(argv[1], "on") == 0) {
        gLedHeartbeat = 0;
        Led_On();
        shellPrint(shell, "LED on (P13.0 low)\r\n");
    } else if (strcmp(argv[1], "off") == 0) {
        gLedHeartbeat = 0;
        Led_Off();
        shellPrint(shell, "LED off (P13.0 high)\r\n");
    } else if (strcmp(argv[1], "toggle") == 0) {
        gLedHeartbeat = 0;
        Led_Toggle();
        shellPrint(shell, "LED toggled\r\n");
    } else if (strcmp(argv[1], "blink") == 0) {
        int n = 5, ms = 200;
        if (argc >= 3) n = atoi(argv[2]);
        if (argc >= 4) ms = atoi(argv[3]);
        if (n <= 0) n = 1;
        if (n > 50) n = 50;
        if (ms < 20) ms = 20;
        if (ms > 2000) ms = 2000;
        gLedHeartbeat = 0;
        shellPrint(shell, "LED blink %d x %dms\r\n", n, ms);
        for (int i = 0; i < n; i++) {
            Led_Toggle();
            uint32_t start = g_TickCount_1ms;
            while ((g_TickCount_1ms - start) < (uint32_t)ms) { }
        }
        Led_Off();
        shellPrint(shell, "LED blink done (now off)\r\n");
    } else if (strcmp(argv[1], "hb") == 0) {
        if (argc >= 3 && strcmp(argv[2], "off") == 0) {
            gLedHeartbeat = 0;
            shellPrint(shell, "LED heartbeat off\r\n");
        } else {
            gLedHeartbeat = 1;
            gLedLastTick = g_TickCount_1ms;
            shellPrint(shell, "LED heartbeat on (1Hz)\r\n");
        }
    } else {
        shellPrint(shell, "Unknown led arg '%s'. Try 'led' for usage.\r\n", argv[1]);
        return -1;
    }
    return 0;
}

/* ---------- YT8011AN PHY commands (clause-22 via GETH MDIO, PHYAD=1) ---------- */
static int cmd_phy(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32 v;
    IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(1, 0x02, &v);
    uint32 id1 = v & 0xFFFFu;
    IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(1, 0x03, &v);
    uint32 id2 = v & 0xFFFFu;
    shellPrint(shell, "YT8011AN PHYAD=1 ID: 0x%04lX 0x%04lX (expect 0x4F51 0x3A3x)\r\n",
        (unsigned long)id1, (unsigned long)id2);
    shellPrint(shell, "MII regs:\r\n");
    for (uint32 r = 0; r < 0x20; r++) {
        IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(1, r, &v);
        shellPrint(shell, "  0x%02lX = 0x%04lX%s", (unsigned long)r, (unsigned long)(v & 0xFFFFu),
            (r % 4 == 3) ? "\r\n" : "  ");
    }
    shellPrint(shell, "\r\n");
    return 0;
}

static int cmd_phyr(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32 reg, phy = 1;
    if (argc < 2 || parse_u32(argv[1], &reg) || reg > 0x1F) {
        shellPrint(shell, "usage: phyr <mii_reg 0..0x1F> [phyaddr]\r\n");
        return -1;
    }
    if (argc >= 3 && parse_u32(argv[2], &phy)) {
        shellPrint(shell, "bad phyaddr\r\n");
        return -1;
    }
    uint32 v;
    IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(phy, reg, &v);
    shellPrint(shell, "PHY%lu MII 0x%02lX = 0x%04lX\r\n", (unsigned long)phy, (unsigned long)reg, (unsigned long)(v & 0xFFFFu));
    return 0;
}

static int cmd_phyw(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32 reg, val, phy = 1;
    if (argc < 3 || parse_u32(argv[1], &reg) || parse_u32(argv[2], &val) || reg > 0x1F || val > 0xFFFF) {
        shellPrint(shell, "usage: phyw <mii_reg> <value> [phyaddr]\r\n");
        return -1;
    }
    if (argc >= 4 && parse_u32(argv[3], &phy)) {
        shellPrint(shell, "bad phyaddr\r\n");
        return -1;
    }
    IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(phy, reg, val);
    uint32 back;
    IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(phy, reg, &back);
    shellPrint(shell, "PHY%lu MII 0x%02lX <= 0x%04lX (readback 0x%04lX)\r\n",
        (unsigned long)phy, (unsigned long)reg, (unsigned long)val, (unsigned long)(back & 0xFFFFu));
    return 0;
}

static int cmd_ext(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32 addr;
    if (argc < 2 || parse_u32(argv[1], &addr) || addr > 0xFFFF) {
        shellPrint(shell, "usage: ext <ext_addr> [value]   (via mii 0x1e/0x1f)\r\n");
        return -1;
    }
    if (argc >= 3) {
        uint32 val;
        if (parse_u32(argv[2], &val) || val > 0xFFFF) {
            shellPrint(shell, "bad value\r\n");
            return -1;
        }
        IfxGeth_Eth_Phy_Yt8011an_write_ext(addr, val);
    }
    uint32 back = IfxGeth_Eth_Phy_Yt8011an_read_ext(addr);
    shellPrint(shell, "EXT 0x%04lX = 0x%04lX\r\n", (unsigned long)addr, (unsigned long)back);
    return 0;
}

static int cmd_mmd(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32 dev, addr;
    if (argc < 3 || parse_u32(argv[1], &dev) || parse_u32(argv[2], &addr)) {
        shellPrint(shell, "usage: mmd <device> <reg> [value]   (via mii 0xd/0xe)\r\n");
        shellPrint(shell, "  ex: mmd 1 0x834   (master/slave config)\r\n");
        return -1;
    }
    if (argc >= 4) {
        uint32 val;
        if (parse_u32(argv[3], &val) || val > 0xFFFF) {
            shellPrint(shell, "bad value\r\n");
            return -1;
        }
        IfxGeth_Eth_Phy_Yt8011an_write_mmd(dev, addr, val);
    }
    uint32 back = IfxGeth_Eth_Phy_Yt8011an_read_mmd(dev, addr);
    shellPrint(shell, "MMD%lu 0x%04lX = 0x%04lX\r\n", (unsigned long)dev, (unsigned long)addr, (unsigned long)back);
    return 0;
}

static int cmd_link(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32 bmsr1, bmsr2, spec;
    /* BMSR link bit is latched-low: read twice */
    IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(1, 0x01, &bmsr1);
    IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(1, 0x01, &bmsr2);
    IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(1, 0x11, &spec);
    spec &= 0xFFFFu;
    const char *speed = "???";
    if (((spec >> 14) & 3u) == 2u) speed = "1000M";
    else if (((spec >> 14) & 3u) == 1u) speed = "100M";
    shellPrint(shell, "PHY link: %s (BMSR=0x%04lX) speed=%s mode=%s (SPEC=0x%04lX)\r\n",
        (bmsr2 & 0x4u) ? "UP" : "DOWN", (unsigned long)(bmsr2 & 0xFFFFu),
        speed, (spec & (1u << 13)) ? "Master" : "Slave", (unsigned long)spec);
    shellPrint(shell, "MAC PHYIF: 0x%08lX (LNKSTS bit0, LNKMOD bit1, LNKSPEED bit2)\r\n",
        (unsigned long)GETH_MAC_PHYIF_CONTROL_STATUS.U);
    return 0;
}

static int cmd_ytinit(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    shellPrint(shell, "Re-running YT8011AN init (basic + RGMII 3.3V + soft reset)...\r\n");
    IfxGeth_Eth_Phy_Yt8011an_init();
    shellPrint(shell, "ytinit done. Try 'link' / 'phy'.\r\n");
    return 0;
}

static int cmd_mem(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    shellPrint(shell, "LwIP heap MEM_SIZE %d bytes (LMURAM)\r\n", (int)MEM_SIZE);
    shellPrint(shell, "Use 'ifconfig' for net, 'ethstat' for counters\r\n");
    return 0;
}

static int cmd_reboot(int argc, char *argv[]) { return cmd_reset(argc, argv); }
static int cmd_ver(int argc, char *argv[]) { return cmd_version(argc, argv); }

/* ---------- Shell export ---------- */
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), version, cmd_version, version info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ver, cmd_ver, version alias);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), mcu, cmd_mcu, MCU info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), uid, cmd_uid, chip UID);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), uptime, cmd_uptime, uptime);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), reset, cmd_reset, software reset);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), reboot, cmd_reboot, reboot alias);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), temp, cmd_temp, die temperature);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), sysinfo, cmd_sysinfo, system info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), mem, cmd_mem, memory info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), led, cmd_led, P13.0 LED control);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), phy, cmd_phy, YT8011 MII dump);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), phyr, cmd_phyr, MII read);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), phyw, cmd_phyw, MII write);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ext, cmd_ext, EXT reg);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), mmd, cmd_mmd, MMD reg);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), link, cmd_link, link status);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ytinit, cmd_ytinit, YT8011 re-init);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ifconfig, cmd_ifconfig, netif info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ethstat, cmd_ethstat, eth counters);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), geth, cmd_geth, GETH regs);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), clks, cmd_clks, P11 clocks);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ping, cmd_ping, ping <ip> [count] [size]);

/* ---------- Shell init ---------- */
int Shell_Init(void)
{
    gShell.write = userShellWrite;
    gShell.read = userShellRead;
    shellInit(&gShell, gShellBuffer, sizeof(gShellBuffer));
    return 0;
}

void Shell_Process(void)
{
    /* 1 Hz heartbeat on P13.0 when enabled */
    if (gLedHeartbeat) {
        uint32_t now = g_TickCount_1ms;
        if ((now - gLedLastTick) >= 500U) {
            gLedLastTick = now;
            Led_Toggle();
        }
    }
    shellTask(&gShell);
}

void Shell_PrintBanner(void)
{
    Shell *shell = shellGetCurrent();
    if (shell) {
        shellPrint(shell, "\r\n");
        shellPrint(shell, "  _____  _____ _____  ___ ______\r\n");
        shellPrint(shell, " |_   _|/ ____|  __ \\|__  /__  /\r\n");
        shellPrint(shell, "   | | | |    | |__) |  / /  / / \r\n");
        shellPrint(shell, "   | | | |    |  _  /  / /  / /  \r\n");
        shellPrint(shell, "   | | | |____| | \\ \\ / /_ / /_ \r\n");
        shellPrint(shell, "   |_|  \\_____|_|  \\_\\____/____|\r\n");
        shellPrint(shell, "\r\nTC397 Letter-Shell v%d.%d.%d\r\n", FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
        shellPrint(shell, "Type 'help' for commands, 'mcu' for chip info\r\n");
        shellPrint(shell, "UART0: P14.0(TX) P14.1(RX) 921600bps (oversampling 16)\r\n");
        shellPrint(shell, "LED: P13.0 (low=on), try 'led blink 5'\r\n");
        shellPrint(shell, "NET: RGMII YT8011AN PHYAD=1, try 'link', 'ifconfig'\r\n");
        shellPrint(shell, "\r\n");
    } else {
        printf("\r\nTC397 Letter-Shell v%d.%d.%d (no shell)\r\n", FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    }
}

int Shell_Exec(const char *cmd)
{
    if (cmd == NULL) return -1;
    return shellRun(&gShell, cmd);
}
