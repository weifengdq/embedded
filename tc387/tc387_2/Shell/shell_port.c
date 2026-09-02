#include "shell_port.h"
#include "shell.h"
#include "IfxAsclin_Asc.h"
#include "IfxCpu_Irq.h"
#include "IfxStm.h"
#include "IfxScuRcu.h"
#include "IfxScuCcu.h"
#include "IfxScuWdt.h"
#include "Dts/Dts/IfxDts_Dts.h"
#include "Dts/Std/IfxDts.h"
#include "IfxScu_reg.h"
#include "IfxPms_reg.h"
#include "Configuration.h"
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
        __asm__ volatile("dsync":::"memory");
        gRxHead = next;
        __asm__ volatile("dsync":::"memory");
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
    __asm__ volatile("dsync":::"memory");
    return (short)i;
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

/* ---------- MCU / System commands ---------- */
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0

static int cmd_version(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    shellPrint(shell, "\r\nTC387 Letter-Shell Firmware\r\n");
    shellPrint(shell, "Version : %d.%d.%d\r\n", FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    shellPrint(shell, "Build   : %s %s\r\n", __DATE__, __TIME__);
    shellPrint(shell, "Board   : TC387 TriBoard TC2XX V2.0 (ASCLIN4 921600)\r\n");
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
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ifconfig, cmd_ifconfig, netif info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ethstat, cmd_ethstat, eth counters);
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
    MODULE_P00.OMR.U = (1<<5) | (1<<8);
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
        shellPrint(shell, "\r\nTC387 Letter-Shell v%d.%d.%d\r\n", FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
        shellPrint(shell, "Type 'help' for commands, 'mcu' for chip info\r\n");
        shellPrint(shell, "UART4: P00.9(TX) P00.12(RX) 921600bps (oversampling 16)\r\n");
        shellPrint(shell, "\r\n");
    } else {
        printf("\r\nTC387 Letter-Shell v%d.%d.%d (no shell)\r\n", FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    }
}

int Shell_Exec(const char *cmd)
{
    if (cmd == NULL) return -1;
    return shellRun(&gShell, cmd);
}
