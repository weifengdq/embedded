#include "shell_port.h"
#include "shell.h"
#include "IfxAsclin_Asc.h"
#include "IfxCpu_Irq.h"
#include "IfxStm.h"
#include "IfxPort.h"
#include "IfxScuRcu.h"
#include "IfxScuCcu.h"
#include "IfxScuWdt.h"
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
#include "lan8651.h"
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
#include "lwip/udp.h"
#include "UART_Logging.h"
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

/* LED P13.0: low = on. Heartbeat enabled by default (1 Hz toggle). */
static volatile uint8_t gLedHeartbeat = 1;
static uint32_t gLedLastTick = 0;

static inline void Led_On(void)  { IfxPort_setPinLow(&MODULE_P13, 0); }
static inline void Led_Off(void) { IfxPort_setPinHigh(&MODULE_P13, 0); }
static inline void Led_Toggle(void) { IfxPort_togglePin(&MODULE_P13, 0); }

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

/* ---------- MCU / System commands ---------- */
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0

static int cmd_version(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    shellPrint(shell, "\r\nTC397 Letter-Shell Firmware\r\n");
    shellPrint(shell, "Version : %d.%d.%d\r\n", FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    shellPrint(shell, "Build   : %s %s\r\n", __DATE__, __TIME__);
    shellPrint(shell, "Board   : TC397XX 292pin (ASCLIN0 P14.0 TX / P14.1 RX, 921600)\r\n");
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

/* ---------- Network / LwIP commands ---------- */
extern struct netif *netif_list;

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

/* ----- ping via RAW ICMP (Ctrl+C aborts) ----- */
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
    if (p->tot_len < sizeof(struct ip_hdr) + sizeof(struct icmp_echo_hdr)) {
        return 0;
    }
    struct ip_hdr *iph = (struct ip_hdr*)p->payload;
    u16_t iph_len = IPH_HL(iph) * 4;
    if (p->len < iph_len + sizeof(struct icmp_echo_hdr)) {
        return 0;
    }
    struct icmp_echo_hdr *iecho = (struct icmp_echo_hdr*)((u8_t*)p->payload + iph_len);
    if (ICMPH_TYPE(iecho) == ICMP_ER && iecho->id == PP_HTONS(ping_id) && iecho->seqno == PP_HTONS((uint16_t)ping_seq)) {
        ping_got_reply = 1;
        ping_recv_cnt++;
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
        shellPrint(shell, "  ex: ping 192.168.1.1 4 32   (Ctrl+C to abort)\r\n");
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

        uint32_t wait_start = g_TickCount_1ms;
        int got = 0;
        while ((g_TickCount_1ms - wait_start) < 1000) {
            Ifx_Lwip_pollReceiveFlags();
            uint16_t h = gRxHead, t = gRxTail;
            int abort = 0;
            while (t != h) {
                if (gRxRing[t] == 0x03) { abort = 1; break; }
                t = (uint16_t)((t+1) % SHELL_RX_RING_SIZE);
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
        }
        if (!got && !ping_got_reply) {
            shellPrint(shell, "Request timeout for icmp_seq %lu\r\n", (unsigned long)i);
        }
        uint32_t inter_start = g_TickCount_1ms;
        while ((g_TickCount_1ms - inter_start) < 500 && i+1 < count) {
            Ifx_Lwip_pollReceiveFlags();
            UART_Poll();
            uint16_t h2 = gRxHead, t2 = gRxTail;
            while (t2 != h2) { if (gRxRing[t2]==0x03) { shellPrint(shell, "\r\nAborted\r\n"); goto ping_done; } t2=(uint16_t)((t2+1) % SHELL_RX_RING_SIZE); }
            if ((g_TickCount_1ms - inter_start) >= 500) break;
        }
    }
ping_done:
    shellPrint(shell, "PING statistics: %lu sent, %lu received, %lu%% loss\r\n",
        (unsigned long)ping_sent_cnt, (unsigned long)ping_recv_cnt,
        ping_sent_cnt ? (unsigned long)((ping_sent_cnt - ping_recv_cnt)*100/ping_sent_cnt) : 0);
    return 0;
}

/* ---------- LAN8651 diagnostic commands (cf. lan8671 shell) ---------- */
static int cmd_t1stat(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32_t v = 0;
    uint32_t devid = 0, oas = 0, oac = 0, buf = 0;
    uint32_t c0 = 0, c1 = 0, sts = 0, tot = 0, bst = 0;
    uint32_t bmcr = 0, bmsr = 0, id1 = 0, id2 = 0;
    uint32_t ncr = 0, ncfgr = 0, nsr = 0;
    lan8651_read_reg(&g_lan8651, LAN8651_MISC_DEVID, &devid);
    lan8651_read_reg(&g_lan8651, LAN8651_OA_STATUS0, &oas);
    lan8651_read_reg(&g_lan8651, LAN8651_OA_CONFIG0, &oac);
    lan8651_read_reg(&g_lan8651, LAN8651_OA_BUFSTS, &buf);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL0, &c0);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL1, &c1);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_STS, &sts);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_TOTMR, &tot);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_BURST, &bst);
    lan8651_read_reg(&g_lan8651, LAN8651_PHY_BMCR, &bmcr);
    lan8651_read_reg(&g_lan8651, LAN8651_PHY_BMSR, &bmsr);
    lan8651_read_reg(&g_lan8651, LAN8651_PHY_ID1, &id1);
    lan8651_read_reg(&g_lan8651, LAN8651_PHY_ID2, &id2);
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_NCR, &ncr);
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_NCFGR, &ncfgr);
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_NSR, &nsr);
    (void)v;
    shellPrint(shell, "DEVID=0x%08lX SYNC=%u RESETC=%u oa_cfg=0x%04lX\r\n",
        (unsigned long)devid,
        (unsigned)((oas & LAN8651_OA_STATUS0_SYNC) != 0U),
        (unsigned)((oas & LAN8651_OA_STATUS0_RESETC) != 0U),
        (unsigned long)(oac & 0xFFFFU));
    shellPrint(shell, "PLCA en=%u id=%lu ncnt=%lu pst=%u tot=0x%04lX burst=0x%04lX\r\n",
        (unsigned)((c0 & LAN8651_PLCA_CTRL0_EN) != 0U),
        (unsigned long)(c1 & 0xFFU), (unsigned long)((c1 >> 8) & 0xFFU),
        (unsigned)((sts & LAN8651_PLCA_STS_PST) != 0U),
        (unsigned long)(tot & 0xFFFFU), (unsigned long)(bst & 0xFFFFU));
    shellPrint(shell, "PHY bmcr=0x%04lX bmsr=0x%04lX(link=%u) id=0x%04lX/0x%04lX\r\n",
        (unsigned long)(bmcr & 0xFFFFU), (unsigned long)(bmsr & 0xFFFFU),
        (unsigned)((bmsr & LAN8651_PHY_BMSR_LINK_STATUS) != 0U),
        (unsigned long)(id1 & 0xFFFFU), (unsigned long)(id2 & 0xFFFFU));
    shellPrint(shell, "MAC ncr=0x%02lX(TXEN=%u RXEN=%u) ncfgr=0x%08lX nsr=0x%02lX\r\n",
        (unsigned long)(ncr & 0xFFU),
        (unsigned)((ncr & LAN8651_MAC_NCR_TXEN) != 0U),
        (unsigned)((ncr & LAN8651_MAC_NCR_RXEN) != 0U),
        (unsigned long)ncfgr, (unsigned long)(nsr & 0xFFU));
    shellPrint(shell, "BUF rba=%lu txc=%lu irq=%s(%u)\r\n",
        (unsigned long)(buf & LAN8651_OA_BUFSTS_RBA_MASK),
        (unsigned long)((buf & LAN8651_OA_BUFSTS_TXC_MASK) >> LAN8651_OA_BUFSTS_TXC_SHIFT),
        lan8651_irq_asserted(&g_lan8651) ? "ASSERTED" : "idle",
        (unsigned)IfxPort_getPinState(LAN8651_INT_PORT, LAN8651_INT_PIN));
    return 0;
}

static int cmd_t1r(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32_t addr, val = 0;
    if (argc < 2 || parse_u32(argv[1], &addr)) {
        shellPrint(shell, "usage: t1r <reg_hex>  (ex: t1r 0x0004CA01)\r\n");
        return -1;
    }
    if (lan8651_read_reg(&g_lan8651, addr, &val) != kLan8651Status_Ok) {
        shellPrint(shell, "t1r 0x%08lX failed\r\n", (unsigned long)addr);
        return -1;
    }
    shellPrint(shell, "0x%08lX = 0x%08lX\r\n", (unsigned long)addr, (unsigned long)val);
    return 0;
}

static int cmd_t1w(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32_t addr, val, back = 0;
    if (argc < 3 || parse_u32(argv[1], &addr) || parse_u32(argv[2], &val)) {
        shellPrint(shell, "usage: t1w <reg_hex> <val_hex>\r\n");
        return -1;
    }
    if (lan8651_write_reg(&g_lan8651, addr, val) != kLan8651Status_Ok) {
        shellPrint(shell, "t1w write failed\r\n");
        return -1;
    }
    lan8651_read_reg(&g_lan8651, addr, &back);
    shellPrint(shell, "0x%08lX <= 0x%08lX (readback 0x%08lX)\r\n",
        (unsigned long)addr, (unsigned long)val, (unsigned long)back);
    return 0;
}

static int cmd_plca(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32_t c0 = 0, c1 = 0, sts = 0, tot = 0, bst = 0;
    if (argc == 1) {
        lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL0, &c0);
        lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL1, &c1);
        lan8651_read_reg(&g_lan8651, LAN8651_PLCA_STS, &sts);
        lan8651_read_reg(&g_lan8651, LAN8651_PLCA_TOTMR, &tot);
        lan8651_read_reg(&g_lan8651, LAN8651_PLCA_BURST, &bst);
        shellPrint(shell, "PLCA en=%u id=%lu ncnt=%lu pst=%u tot=0x%04lX burst=0x%04lX\r\n",
            (unsigned)((c0 & LAN8651_PLCA_CTRL0_EN) != 0U),
            (unsigned long)(c1 & 0xFFU), (unsigned long)((c1 >> 8) & 0xFFU),
            (unsigned)((sts & LAN8651_PLCA_STS_PST) != 0U),
            (unsigned long)(tot & 0xFFFFU), (unsigned long)(bst & 0xFFFFU));
        shellPrint(shell, "usage: plca <node_id 0..255> [node_count]  (0=coordinator/master)\r\n");
        return 0;
    }
    {
        uint32_t id, ncnt = 8;
        uint32_t cur = 0;
        lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL1, &cur);
        if (parse_u32(argv[1], &id) || id > 255) {
            shellPrint(shell, "bad node_id (0..255)\r\n");
            return -1;
        }
        if (argc >= 3) {
            if (parse_u32(argv[2], &ncnt) || ncnt == 0 || ncnt > 255) {
                shellPrint(shell, "bad node_count (1..255)\r\n");
                return -1;
            }
        } else {
            ncnt = (cur >> 8) & 0xFFU;
            if (ncnt == 0) ncnt = 8;
        }
        {
            uint32_t v = ((ncnt & 0xFFU) << 8) | (id & 0xFFU);
            if (lan8651_write_reg(&g_lan8651, LAN8651_PLCA_CTRL1, v) != kLan8651Status_Ok) {
                shellPrint(shell, "plca write failed\r\n");
                return -1;
            }
            /* ensure PLCA enabled */
            lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL0, &c0);
            if ((c0 & LAN8651_PLCA_CTRL0_EN) == 0U) {
                lan8651_write_reg(&g_lan8651, LAN8651_PLCA_CTRL0, LAN8651_PLCA_CTRL0_EN);
            }
            lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL1, &c1);
            shellPrint(shell, "PLCA set id=%lu ncnt=%lu (readback id=%lu ncnt=%lu)%s\r\n",
                (unsigned long)id, (unsigned long)ncnt,
                (unsigned long)(c1 & 0xFFU), (unsigned long)((c1 >> 8) & 0xFFU),
                (id == 0) ? " [coordinator/master]" : " [follower/slave]");
        }
    }
    return 0;
}

static int cmd_link(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32_t oas = 0, sts = 0, bmsr = 0;
    lan8651_read_reg(&g_lan8651, LAN8651_OA_STATUS0, &oas);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_STS, &sts);
    lan8651_read_reg(&g_lan8651, LAN8651_PHY_BMSR, &bmsr);
    shellPrint(shell, "link: %s (sync=%u pst=%u phy_link=%u irq=%s)\r\n",
        lan8651_link_up(&g_lan8651) ? "UP" : "DOWN",
        (unsigned)((oas & LAN8651_OA_STATUS0_SYNC) != 0U),
        (unsigned)((sts & LAN8651_PLCA_STS_PST) != 0U),
        (unsigned)((bmsr & LAN8651_PHY_BMSR_LINK_STATUS) != 0U),
        lan8651_irq_asserted(&g_lan8651) ? "ASSERTED" : "idle");
    return 0;
}

/* ---------- LAN8651 diagnostics: SQI / PLCA / PCS / cable ---------- */

static const char *sqi_text(uint32_t v)
{
    switch (v & 0x7U) {
        case 0: return "SNR<=~5dB BER>=~3.8E-02 (worst)";
        case 1: return "~5-10dB BER~3.8E-02~7.8E-4";
        case 2: return "~10-12dB BER~7.8E-4~3.4E-5";
        case 3: return "~12-14dB BER~3.4E-5~2.7E-7";
        case 4: return "~14-16dB BER~2.7E-7~1.4E-10";
        case 5: return "~16-17dB BER~1.4E-10~7.2E-13";
        case 6: return "~17-18dB BER~7.2E-13~9.9E-16";
        default: return "SNR>=~18dB BER<=~9.9E-16 (best)";
    }
}

/* Run one SQI polling-mode measurement. Returns 0 + *sqival on success,
 * -1 on timeout, -2 on SQI error. Keeps lwIP polling so links survive. */
static int sqi_measure(uint32_t toid, uint32_t timeout_ms, uint32_t *sqival, uint32_t *errc)
{
    uint32_t v = 0;
    uint32_t start;
    int restarts = 0;

    /* TOID lives in SQICFG0 bits 11:4 */
    if (lan8651_read_reg(&g_lan8651, LAN8651_SQICFG0, &v) != kLan8651Status_Ok) return -3;
    v = (v & ~(uint32_t)LAN8651_SQICFG0_TOID_MASK) |
        (((toid << LAN8651_SQICFG0_TOID_SHIFT) & LAN8651_SQICFG0_TOID_MASK));
    if (lan8651_write_reg(&g_lan8651, LAN8651_SQICFG0, v) != kLan8651Status_Ok) return -3;

    /* polling mode: interrupt threshold disabled (0x1F) */
    if (lan8651_read_reg(&g_lan8651, LAN8651_SQICFG2, &v) != kLan8651Status_Ok) return -3;
    v = (v & ~(uint32_t)LAN8651_SQICFG2_INTTHR_MASK) | LAN8651_SQICFG2_INTTHR_OFF;
    if (lan8651_write_reg(&g_lan8651, LAN8651_SQICFG2, v) != kLan8651Status_Ok) return -3;

    /* reset SQI block, wait self-clear */
    if (lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v) != kLan8651Status_Ok) return -3;
    if (lan8651_write_reg(&g_lan8651, LAN8651_SQICTL, v | LAN8651_SQICTL_SQIRST) != kLan8651Status_Ok) return -3;
    start = g_TickCount_1ms;
    do {
        if (lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v) != kLan8651Status_Ok) return -3;
        if ((v & LAN8651_SQICTL_SQIRST) == 0U) break;
        Ifx_Lwip_pollTimerFlags();
        Ifx_Lwip_pollReceiveFlags();
    } while ((g_TickCount_1ms - start) < 500U);

    /* enable measurement */
    if (lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v) != kLan8651Status_Ok) return -3;
    if (lan8651_write_reg(&g_lan8651, LAN8651_SQICTL, v | LAN8651_SQICTL_SQIEN) != kLan8651Status_Ok) return -3;

    start = g_TickCount_1ms;
    while ((g_TickCount_1ms - start) < timeout_ms) {
        uint32_t s0;
        Ifx_Lwip_pollTimerFlags();
        Ifx_Lwip_pollReceiveFlags();
        if (lan8651_read_reg(&g_lan8651, LAN8651_SQISTS0, &s0) != kLan8651Status_Ok) return -3;
        if ((s0 & LAN8651_SQISTS0_SQIVLD) != 0U) {
            *sqival = (s0 & LAN8651_SQISTS0_SQIVAL_MASK) >> LAN8651_SQISTS0_SQIVAL_SHIFT;
            *errc = 0U;
            lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v);
            lan8651_write_reg(&g_lan8651, LAN8651_SQICTL, v & ~(uint32_t)LAN8651_SQICTL_SQIEN);
            return 0;
        }
        if ((s0 & LAN8651_SQISTS0_SQIERR) != 0U) {
            *errc = s0 & LAN8651_SQISTS0_SQIERRC_MASK;
            if (restarts == 0) {
                /* datasheet: clear SQIEN then re-enable and retry once */
                restarts++;
                lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v);
                lan8651_write_reg(&g_lan8651, LAN8651_SQICTL, v & ~(uint32_t)LAN8651_SQICTL_SQIEN);
                lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v);
                lan8651_write_reg(&g_lan8651, LAN8651_SQICTL, v | LAN8651_SQICTL_SQIEN);
                start = g_TickCount_1ms;
                continue;
            }
            lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v);
            lan8651_write_reg(&g_lan8651, LAN8651_SQICTL, v & ~(uint32_t)LAN8651_SQICTL_SQIEN);
            return -2;
        }
    }
    lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v);
    lan8651_write_reg(&g_lan8651, LAN8651_SQICTL, v & ~(uint32_t)LAN8651_SQICTL_SQIEN);
    return -1;
}

static int cmd_sqi(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32_t toid = 0xFFU;
    uint32_t sqival = 0U, errc = 0U;
    int rc;
    if (argc >= 2 && parse_u32(argv[1], &toid)) {
        shellPrint(shell, "usage: sqi [toid 0..255, default 0xFF=all]\r\n");
        return -1;
    }
    shellPrint(shell, "SQI measuring (toid=0x%02lX, up to 6s, needs RX traffic)...\r\n", (unsigned long)toid);
    rc = sqi_measure(toid, 6000U, &sqival, &errc);
    if (rc == 0) {
        shellPrint(shell, "SQI=%lu (%s)\r\n", (unsigned long)sqival, sqi_text(sqival));
    } else if (rc == -2) {
        shellPrint(shell, "SQI error, code=0x%lX (see datasheet SQIERRC)\r\n", (unsigned long)errc);
    } else if (rc == -1) {
        shellPrint(shell, "SQI timeout (no valid estimation in 6s; generate RX traffic e.g. ping/iperf and retry)\r\n");
    } else {
        shellPrint(shell, "SQI register access failed\r\n");
    }
    return rc == 0 ? 0 : -1;
}

static uint32_t ctr32(uint32_t hi_reg, uint32_t lo_reg)
{
    uint32_t hi = 0U, lo = 0U;
    lan8651_read_reg(&g_lan8651, hi_reg, &hi);
    lan8651_read_reg(&g_lan8651, lo_reg, &lo);
    return ((hi & 0xFFFFU) << 16) | (lo & 0xFFFFU);
}

static int cmd_plcadiag(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32_t c0 = 0, c1 = 0, sts = 0, tot = 0, bst = 0, s1 = 0, prs = 0, ctr = 0;
    uint32_t bcn0, to0, bcn1, to1;
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL0, &c0);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL1, &c1);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_STS, &sts);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_TOTMR, &tot);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_BURST, &bst);
    lan8651_read_reg(&g_lan8651, LAN8651_PRSSTS, &prs);
    /* enable TO/BCN counters if needed (sticky once enabled) */
    lan8651_read_reg(&g_lan8651, LAN8651_CTRCTRL, &ctr);
    if ((ctr & (LAN8651_CTRCTRL_TOCTRE | LAN8651_CTRCTRL_BCNCTRE)) !=
        (LAN8651_CTRCTRL_TOCTRE | LAN8651_CTRCTRL_BCNCTRE)) {
        lan8651_write_reg(&g_lan8651, LAN8651_CTRCTRL,
            ctr | LAN8651_CTRCTRL_TOCTRE | LAN8651_CTRCTRL_BCNCTRE);
        shellPrint(shell, "(CTRCTRL counters enabled)\r\n");
    }
    bcn0 = ctr32(LAN8651_BCNCNTH, LAN8651_BCNCNTL);
    to0 = ctr32(LAN8651_TOCNTH, LAN8651_TOCNTL);
    { uint32_t s = g_TickCount_1ms; while ((g_TickCount_1ms - s) < 1000U) {
        Ifx_Lwip_pollTimerFlags(); Ifx_Lwip_pollReceiveFlags(); } }
    bcn1 = ctr32(LAN8651_BCNCNTH, LAN8651_BCNCNTL);
    to1 = ctr32(LAN8651_TOCNTH, LAN8651_TOCNTL);
    s1 = 0U;
    lan8651_read_reg(&g_lan8651, LAN8651_STS1, &s1); /* RC: read-clear */
    shellPrint(shell, "PLCA en=%u id=%lu ncnt=%lu pst=%u tot=0x%04lX burst=0x%04lX maxid=%lu\r\n",
        (unsigned)((c0 & LAN8651_PLCA_CTRL0_EN) != 0U),
        (unsigned long)(c1 & 0xFFU), (unsigned long)((c1 >> 8) & 0xFFU),
        (unsigned)((sts & LAN8651_PLCA_STS_PST) != 0U),
        (unsigned long)(tot & 0xFFFFU), (unsigned long)(bst & 0xFFFFU),
        (unsigned long)((prs >> LAN8651_PRSSTS_MAXID_SHIFT) & 0xFFU));
    shellPrint(shell, "BEACON %lu/s, TO %lu/s (1s window, 32-bit wrap-aware diff)\r\n",
        (unsigned long)(bcn1 - bcn0), (unsigned long)(to1 - to0));
    shellPrint(shell, "STS1(RC,read-clear)=0x%04lX: EMPCYC=%u RXINTO=%u UNEXPB=%u BCNBFTO=%u PSTC=%u\r\n",
        (unsigned long)(s1 & 0xFFFFU),
        (unsigned)((s1 & LAN8651_STS1_EMPCYC) != 0U),
        (unsigned)((s1 & LAN8651_STS1_RXINTO) != 0U),
        (unsigned)((s1 & LAN8651_STS1_UNEXPB) != 0U),
        (unsigned)((s1 & LAN8651_STS1_BCNBFTO) != 0U),
        (unsigned)((s1 & LAN8651_STS1_PSTC) != 0U));
    return 0;
}

static int cmd_pcsdiag(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32_t s1 = 0, s2 = 0, s3 = 0, ncr = 0, ncfgr = 0, nsr = 0, tsr = 0, rsr = 0;
    lan8651_read_reg(&g_lan8651, LAN8651_STS1, &s1); /* RC */
    lan8651_read_reg(&g_lan8651, LAN8651_STS2, &s2); /* RC */
    lan8651_read_reg(&g_lan8651, LAN8651_STS3, &s3); /* RC */
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_NCR, &ncr);
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_NCFGR, &ncfgr);
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_NSR, &nsr);
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_TSR, &tsr);
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_RSR, &rsr);
    shellPrint(shell, "STS1(RC)=0x%04lX: DEC5B=%u ESDERR=%u PLCASYM=%u UNCRS=%u SQI=%u TXCOL=%u TXJAB=%u TSSI=%u\r\n",
        (unsigned long)(s1 & 0xFFFFU),
        (unsigned)((s1 & LAN8651_STS1_DEC5B) != 0U),
        (unsigned)((s1 & LAN8651_STS1_ESDERR) != 0U),
        (unsigned)((s1 & LAN8651_STS1_PLCASYM) != 0U),
        (unsigned)((s1 & LAN8651_STS1_UNCRS) != 0U),
        (unsigned)((s1 & LAN8651_STS1_SQI) != 0U),
        (unsigned)((s1 & LAN8651_STS1_TXCOL) != 0U),
        (unsigned)((s1 & LAN8651_STS1_TXJAB) != 0U),
        (unsigned)((s1 & LAN8651_STS1_TSSI) != 0U));
    shellPrint(shell, "STS2(RC)=0x%04lX: UV33=%u OT=%u IWDTO=%u WKEMDI=%u WKEWI=%u; STS3(RC)=0x%04lX ERRTOID=%lu\r\n",
        (unsigned long)(s2 & 0xFFFFU),
        (unsigned)((s2 & LAN8651_STS2_UV33) != 0U),
        (unsigned)((s2 & LAN8651_STS2_OT) != 0U),
        (unsigned)((s2 & LAN8651_STS2_IWDTO) != 0U),
        (unsigned)((s2 & LAN8651_STS2_WKEMDI) != 0U),
        (unsigned)((s2 & LAN8651_STS2_WKEWI) != 0U),
        (unsigned long)(s3 & 0xFFFFU), (unsigned long)(s3 & 0xFFU));
    shellPrint(shell, "MAC ncr=0x%02lX ncfgr=0x%08lX nsr=0x%02lX(IDLE=%u) tsr=0x%02lX(COL=%u TXCOMP=%u) rsr=0x%02lX(REC=%u)\r\n",
        (unsigned long)(ncr & 0xFFU), (unsigned long)ncfgr, (unsigned long)(nsr & 0xFFU),
        (unsigned)((nsr & LAN8651_MAC_NSR_IDLE) != 0U),
        (unsigned long)(tsr & 0xFFU),
        (unsigned)((tsr & LAN8651_MAC_TSR_COL) != 0U),
        (unsigned)((tsr & LAN8651_MAC_TSR_TXCOMP) != 0U),
        (unsigned long)(rsr & 0xFFU),
        (unsigned)((rsr & LAN8651_MAC_RSR_REC) != 0U));
    shellPrint(shell, "(STS1/2/3 are read-clear: bits show events since last pcsdiag/plcadiag)\r\n");
    return 0;
}

static int cmd_evcnt(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32_t ctr = 0;
    lan8651_read_reg(&g_lan8651, LAN8651_CTRCTRL, &ctr);
    if ((ctr & (LAN8651_CTRCTRL_TOCTRE | LAN8651_CTRCTRL_BCNCTRE)) !=
        (LAN8651_CTRCTRL_TOCTRE | LAN8651_CTRCTRL_BCNCTRE)) {
        lan8651_write_reg(&g_lan8651, LAN8651_CTRCTRL,
            ctr | LAN8651_CTRCTRL_TOCTRE | LAN8651_CTRCTRL_BCNCTRE);
    }
    shellPrint(shell, "TO_cnt=%lu BCN_cnt=%lu (cumulative since counter enable)\r\n",
        (unsigned long)ctr32(LAN8651_TOCNTH, LAN8651_TOCNTL),
        (unsigned long)ctr32(LAN8651_BCNCNTH, LAN8651_BCNCNTL));
    return 0;
}

static int cmd_cable(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32_t toid = 0xFFU;
    uint32_t sqival = 0U, errc = 0U;
    uint32_t s1 = 0, s2 = 0, bmsr = 0;
    int rc;
    if (argc >= 2 && parse_u32(argv[1], &toid)) {
        shellPrint(shell, "usage: cable [toid, default 0xFF=all]\r\n");
        shellPrint(shell, "  cable health = SQI + error sticky bits (note: Microchip HDD algorithm is NDA-only)\r\n");
        return -1;
    }
    shellPrint(shell, "cable health check (SQI up to 6s, needs RX traffic)...\r\n");
    rc = sqi_measure(toid, 6000U, &sqival, &errc);
    lan8651_read_reg(&g_lan8651, LAN8651_STS1, &s1); /* RC */
    lan8651_read_reg(&g_lan8651, LAN8651_STS2, &s2); /* RC */
    lan8651_read_reg(&g_lan8651, LAN8651_PHY_BMSR, &bmsr);
    lan8651_read_reg(&g_lan8651, LAN8651_PHY_BMSR, &bmsr);
    if (rc == 0) {
        const char *verdict =
            (sqival >= 6) ? "GOOD" :
            (sqival >= 4) ? "MARGINAL" : "POOR (check wiring/termination/length)";
        shellPrint(shell, "SQI=%lu (%s) -> cable %s\r\n", (unsigned long)sqival, sqi_text(sqival), verdict);
    } else if (rc == -2) {
        shellPrint(shell, "SQI error code=0x%lX -> cable check inconclusive, retry with traffic\r\n", (unsigned long)errc);
    } else {
        shellPrint(shell, "SQI timeout -> cable check inconclusive (generate RX traffic and retry)\r\n");
    }
    shellPrint(shell, "phy_link=%u STS1err(DEC5B/ESDERR/PLCASYM/UNCRS)=%u%u%u%u UV33=%u OT=%u\r\n",
        (unsigned)((bmsr & LAN8651_PHY_BMSR_LINK_STATUS) != 0U),
        (unsigned)((s1 & LAN8651_STS1_DEC5B) != 0U),
        (unsigned)((s1 & LAN8651_STS1_ESDERR) != 0U),
        (unsigned)((s1 & LAN8651_STS1_PLCASYM) != 0U),
        (unsigned)((s1 & LAN8651_STS1_UNCRS) != 0U),
        (unsigned)((s2 & LAN8651_STS2_UV33) != 0U),
        (unsigned)((s2 & LAN8651_STS2_OT) != 0U));
    return 0;
}

/* ---------- System commands (no LAN) ---------- */

static int cmd_mem(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    shellPrint(shell, "mem stats: bare-metal NO_SYS, no heap tracker\r\n");
    shellPrint(shell, "Use 'mcu' for chip info, 'help' for commands\r\n");
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
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ifconfig, cmd_ifconfig, netif info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ping, cmd_ping, ping <ip> [count] [size]);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), t1stat, cmd_t1stat, LAN8651 status dump);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), t1r, cmd_t1r, LAN8651 reg read);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), t1w, cmd_t1w, LAN8651 reg write);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), plca, cmd_plca, show or set PLCA id/count);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), link, cmd_link, T1S link status);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), sqi, cmd_sqi, SQI signal quality 0-7);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), plcadiag, cmd_plcadiag, PLCA diag + beacon rate);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), pcsdiag, cmd_pcsdiag, PCS/MAC error sticky bits);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), evcnt, cmd_evcnt, TO/BEACON cumulative counters);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), cable, cmd_cable, cable health via SQI+errors);

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
