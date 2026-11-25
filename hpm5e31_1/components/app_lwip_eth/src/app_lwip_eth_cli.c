/*
 * app_lwip_eth_cli.c - app_lwip_eth 的 letter-shell 命令
 *
 * 命令集（与第 50 章"例程"节的串口自证格式一一对应）：
 *   eth                     打印 netif/PHY/统计
 *   eth stat                只打印统计
 *   eth ping <ip> [n] [ms]  ICMP echo，输出 RTT 与抖动
 *   eth iserver [tcp|udp]   启动 iperf2 服务端（端口 5001）
 *   eth iclient <ip> [tcp|udp]  启动 iperf2 客户端（-1000 = 10 s）
 *   eth istop               中止 iperf
 *
 * 注意：letter-shell 的 SHELL_TYPE_CMD_MAIN 才会把 (argc, argv) 原样传进来，
 * 用 SHELL_TYPE_CMD_FUNC 会把参数当整数解析（docs/HANDOVER §3.3 易错点 4）。
 *
 * 作者：eth-stack（task-2）
 */
#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include "app_lwip_eth.h"
#if LWIP_STATS
#include "lwip/stats.h"
#endif
#include "lwip/tcp.h"
#include "lwip/priv/tcp_priv.h"
#include "Eth.h"

/* ---- PHY 诊断（R10）：经 Eth MII 间接读写 YT8011AN 的 EXT 寄存器 ----------
 * 现场排"板->主机线缆 CRC 误码"时用来扫 RGMII TXC 延迟档位（EXT 0x9001[7:4]），
 * 不需要为此重烧固件。只做运行时诊断：复位后仍回到 EthTrcv_PBcfg.c 的配置。
 */
#define ETH_CLI_EXT_ADDR     (0x1EU)
#define ETH_CLI_EXT_DATA     (0x1FU)
#define ETH_CLI_RGMII_CFG1   (0x9001U)
#define ETH_CLI_TXDLY_MASK   (0x00F0U)
#define ETH_CLI_TXDLY_SHIFT  (4U)

static Std_ReturnType eth_cli_ext_read(uint16 ExtAddr, uint16 *Val)
{
    if (Eth_WriteMii(0U, 0U, ETH_CLI_EXT_ADDR, ExtAddr) != E_OK) {
        return E_NOT_OK;
    }
    return Eth_ReadMii(0U, 0U, ETH_CLI_EXT_DATA, Val);
}

static Std_ReturnType eth_cli_ext_write(uint16 ExtAddr, uint16 Val)
{
    if (Eth_WriteMii(0U, 0U, ETH_CLI_EXT_ADDR, ExtAddr) != E_OK) {
        return E_NOT_OK;
    }
    return Eth_WriteMii(0U, 0U, ETH_CLI_EXT_DATA, Val);
}

static void cmd_eth(int argc, char *argv[])
{
    if (argc < 2) {
        app_lwip_eth_print();
        app_lwip_eth_print_stats();
        return;
    }
    if (strcmp(argv[1], "stat") == 0) {
        app_lwip_eth_print_stats();
    } else if (strcmp(argv[1], "ping") == 0) {
        uint32_t n = 10U;
        uint32_t interval = 100U;
        app_lwip_eth_ping_result_t r;
        if (argc < 3) {
            printf("usage: eth ping <ip> [count] [interval_ms]\r\n");
            return;
        }
        if (argc >= 4) {
            (void) bsp_parse_u32(argv[3], &n);
        }
        if (argc >= 5) {
            (void) bsp_parse_u32(argv[4], &interval);
        }
        (void) app_lwip_eth_ping(argv[2], n, interval, &r);
    } else if (strcmp(argv[1], "iserver") == 0) {
        bool udp = (argc >= 3) && (strcmp(argv[2], "udp") == 0);
        if (app_lwip_eth_iperf_server(udp) == 0) {
            printf("[iperf] %s server started on port %u\r\n", udp ? "UDP" : "TCP",
                   (unsigned) (udp ? 5001 : 5001));
        } else {
            printf("[iperf] server start FAILED (已有会话在跑?)\r\n");
        }
    } else if (strcmp(argv[1], "iclient") == 0) {
        bool udp;
        if (argc < 3) {
            printf("usage: eth iclient <ip> [tcp|udp]\r\n");
            return;
        }
        udp = (argc >= 4) && (strcmp(argv[3], "udp") == 0);
        if (app_lwip_eth_iperf_client(argv[2], udp) == 0) {
            printf("[iperf] %s client -> %s started\r\n", udp ? "UDP" : "TCP", argv[2]);
        } else {
            printf("[iperf] client start FAILED\r\n");
        }
    } else if (strcmp(argv[1], "istop") == 0) {
        app_lwip_eth_iperf_stop();
        printf("[iperf] aborted\r\n");
    } else if (strcmp(argv[1], "phy") == 0) {
        uint16 ext = 0U;
        uint16 bmcr = 0U;
        uint16 bmsr = 0U;
        uint16 physr = 0U;
        if (eth_cli_ext_read(ETH_CLI_RGMII_CFG1, &ext) == E_OK) {
            printf("[phy] EXT 0x9001=0x%04X txdly=%u rx_dly_en=%u txclk_sel=%u\r\n",
                   (unsigned) ext,
                   (unsigned) ((ext & ETH_CLI_TXDLY_MASK) >> ETH_CLI_TXDLY_SHIFT),
                   (unsigned) ((ext >> 8) & 1U), (unsigned) ((ext >> 9) & 1U));
        } else {
            printf("[phy] EXT 0x9001 read FAILED\r\n");
        }
        if ((Eth_ReadMii(0U, 0U, 0x00U, &bmcr) == E_OK) &&
            (Eth_ReadMii(0U, 0U, 0x01U, &bmsr) == E_OK) &&
            (Eth_ReadMii(0U, 0U, 0x11U, &physr) == E_OK)) {
            printf("[phy] BMCR=0x%04X BMSR=0x%04X PHYSR=0x%04X\r\n",
                   (unsigned) bmcr, (unsigned) bmsr, (unsigned) physr);
        }
    } else if (strcmp(argv[1], "txdly") == 0) {
        uint32 n = 0U;
        uint16 ext = 0U;
        uint16 rb = 0U;
        if (argc < 3) {
            printf("usage: eth txdly <0..15>   (sweep RGMII TXC delay, EXT 0x9001[7:4])\r\n");
            return;
        }
        if ((bsp_parse_u32(argv[2], &n) != 0) || (n > 15U)) {
            printf("[phy] bad txdly\r\n");
            return;
        }
        if (eth_cli_ext_read(ETH_CLI_RGMII_CFG1, &ext) != E_OK) {
            printf("[phy] EXT read FAILED\r\n");
            return;
        }
        ext = (uint16) ((ext & (uint16) ~ETH_CLI_TXDLY_MASK) |
                        (uint16) ((n << ETH_CLI_TXDLY_SHIFT) & ETH_CLI_TXDLY_MASK));
        if (eth_cli_ext_write(ETH_CLI_RGMII_CFG1, ext) != E_OK) {
            printf("[phy] EXT write FAILED\r\n");
            return;
        }
        (void) eth_cli_ext_read(ETH_CLI_RGMII_CFG1, &rb);
        printf("[phy] txdly=%u EXT 0x9001=0x%04X (readback)\r\n", (unsigned) n, (unsigned) rb);
    } else {
        printf("usage: eth [stat|ping <ip> [n] [ms]|iserver [tcp|udp]|iclient <ip> [tcp|udp]|istop|phy|txdly <n>]\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 eth, cmd_eth, MCAL Eth + lwIP: eth [stat|ping|iserver|iclient|istop]);

/* lwipstat：打印 lwIP 的 mem(堆)/memp(pcb、seg、pbuf 池) 用量。
 * 只在 lwipopts_app.h 打开 LWIP_STATS/MEM_STATS/MEMP_STATS/LWIP_STATS_DISPLAY 时有效，
 * 用于长跑定位资源泄漏（mem_err/内存池 used 是否单调增长）。 */
static void cmd_lwipstat(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
#if LWIP_STATS && LWIP_STATS_DISPLAY
    stats_display();
#else
    printf("[lwipstat] LWIP_STATS/LWIP_STATS_DISPLAY 未开启\r\n");
#endif
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 lwipstat, cmd_lwipstat, show lwIP mem/memp stats (needs LWIP_STATS=1));
/* tcppcb：dump lwIP TCP pcb 链表（listener 的 accepts_pending/backlog、active/tw 的
 * 状态与端口）。长跑里"监听还在但新连接静默超时"时用它看池是否被卡死的 pcb 占满。 */
static const char *tcp_state_name(u8_t st)
{
    switch (st) {
    case CLOSED:      return "CLOSED";
    case LISTEN:      return "LISTEN";
    case SYN_SENT:    return "SYN_SENT";
    case SYN_RCVD:    return "SYN_RCVD";
    case ESTABLISHED: return "ESTAB";
    case FIN_WAIT_1:  return "FIN_WAIT_1";
    case FIN_WAIT_2:  return "FIN_WAIT_2";
    case CLOSE_WAIT:  return "CLOSE_WAIT";
    case CLOSING:     return "CLOSING";
    case LAST_ACK:    return "LAST_ACK";
    case TIME_WAIT:   return "TIME_WAIT";
    default:          return "?";
    }
}

static void cmd_tcppcb(int argc, char *argv[])
{
    struct tcp_pcb *p;
    struct tcp_pcb_listen *l;
    int n;
    (void) argc;
    (void) argv;

    printf("[pcb] LISTEN:");
    for (l = tcp_listen_pcbs.listen_pcbs; l != NULL; l = l->next) {
        printf(" port=%u", (unsigned) l->local_port);
    }
    printf("\r\n");
    n = 0;
    for (p = tcp_active_pcbs; p != NULL; p = p->next) {
        printf("[pcb] A%02d %-10s %u<-%u\r\n", n++, tcp_state_name(p->state),
               (unsigned) p->local_port, (unsigned) p->remote_port);
    }
    n = 0;
    for (p = tcp_tw_pcbs; p != NULL; p = p->next) {
        printf("[pcb] T%02d %-10s %u<-%u\r\n", n++, tcp_state_name(p->state),
               (unsigned) p->local_port, (unsigned) p->remote_port);
    }
    n = 0;
    for (p = tcp_bound_pcbs; p != NULL; p = p->next) {
        n++;
    }
    printf("[pcb] bound=%d\r\n", n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 tcppcb, cmd_tcppcb, dump lwip TCP pcb lists (listen/active/timewait));