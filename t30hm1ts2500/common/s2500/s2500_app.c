/**
 * @file  s2500_app.c
 * @brief 通用应用层: S2500 + lwIP(NO_SYS) + letter-shell + 测试服务
 *
 * 提供的 shell 命令:
 *   s2500 ...     驱动/芯片命令 (见 s2500_shell.c)
 *   netif         显示 IP/MAC/链路
 *   ping <ip> [n] ICMP echo (本机发起)
 *   iperf -s | iperf -c <ip>   TCP 吞吐测试 (lwiperf)
 *   udpecho on|off             UDP 回显 (端口 7)
 *   tcpstats                   lwIP 统计
 */
#include "s2500_app.h"
#include "s2500_netif.h"
#include "s2500_shell.h"
#include "s2500_port.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/ip4_addr.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/stats.h"
#include "shell.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static s2500_dev_t   s_dev;
static const s2500_app_cfg_t *s_cfg;
static int           s_lwip_inited;
static int           s_shell_inited;
static uint8_t       s_udp_echo_on = 1;
static struct udp_pcb *s_udp_echo;
static struct tcp_pcb *s_tcp_echo;
static void         *s_iperf;

/* ------------------------------------------------------------------ */
/* lwIP 时间基准                                                      */
/* ------------------------------------------------------------------ */
u32_t sys_now(void)
{
    if (s_cfg && s_cfg->hal && s_cfg->hal->millis) {
        return s_cfg->hal->millis(s_cfg->hal->ctx);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* UDP 回显 (端口 7)                                                  */
/* ------------------------------------------------------------------ */
static void udp_echo_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                          const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    if (p) {
        udp_sendto(pcb, p, addr, port);
        pbuf_free(p);
    }
}

static void udp_echo_start(void)
{
    if (s_udp_echo) return;
    s_udp_echo = udp_new();
    if (!s_udp_echo) return;
    if (udp_bind(s_udp_echo, IP_ADDR_ANY, 7) == ERR_OK) {
        udp_recv(s_udp_echo, udp_echo_recv, NULL);
    }
}

static void udp_echo_stop(void)
{
    if (s_udp_echo) {
        udp_remove(s_udp_echo);
        s_udp_echo = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* TCP 回显 (端口 7)                                                  */
/* ------------------------------------------------------------------ */
static err_t tcp_echo_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    if (err != ERR_OK) {
        if (p) pbuf_free(p);
        return err;
    }
    if (!p) {
        tcp_close(pcb);
        return ERR_OK;
    }
    tcp_write(pcb, p->payload, p->len, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
    pbuf_free(p);
    return ERR_OK;
}

static err_t tcp_echo_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    tcp_recv(newpcb, tcp_echo_recv);
    return ERR_OK;
}

static void tcp_echo_start(void)
{
    if (s_tcp_echo) return;
    s_tcp_echo = tcp_new();
    if (!s_tcp_echo) return;
    if (tcp_bind(s_tcp_echo, IP_ADDR_ANY, 7) == ERR_OK) {
        s_tcp_echo = tcp_listen(s_tcp_echo);
        tcp_accept(s_tcp_echo, tcp_echo_accept);
    }
}

/* ------------------------------------------------------------------ */
/* ICMP ping (raw API, NO_SYS 阻塞式)                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    uint16_t seq;
    uint16_t replies;
    uint32_t sent_ms;
    uint32_t rtt_ms;
    int      done;
    ip_addr_t addr;
} ping_state_t;

static ping_state_t s_ping;

static u8_t ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p,
                      const ip_addr_t *addr)
{
    ping_state_t *st = (ping_state_t *)arg;
    struct icmp_echo_hdr *iecho;
    (void)pcb;
    if (pbuf_header(p, -PBUF_IP_HLEN) != 0) {
        pbuf_free(p);
        return 1;
    }
    iecho = (struct icmp_echo_hdr *)p->payload;
    if (iecho->type == ICMP_ER && ntohs(iecho->id) == 0x5A5A &&
        ntohs(iecho->seqno) == st->seq) {
        st->replies++;
        st->rtt_ms = s_cfg->hal->millis(s_cfg->hal->ctx) - st->sent_ms;
    }
    pbuf_free(p);
    (void)addr;
    return 1;
}

static int cmd_ping(int argc, char **argv)
{
    ip_addr_t target;
    struct raw_pcb *pcb;
    struct pbuf *p;
    struct icmp_echo_hdr *iecho;
    uint32_t count = 4, i;

    if (argc < 2) {
        s2500_shell_printf("usage: ping <ip> [count]\r\n");
        return 0;
    }
    if (!ipaddr_aton(argv[1], &target)) {
        s2500_shell_printf("bad ip\r\n");
        return 0;
    }
    if (argc >= 3) count = (uint32_t)strtoul(argv[2], NULL, 0);

    memset(&s_ping, 0, sizeof(s_ping));
    s_ping.addr = target;
    pcb = raw_new(IP_PROTO_ICMP);
    if (!pcb) { s2500_shell_printf("raw_new failed\r\n"); return 0; }
    raw_recv(pcb, ping_recv, &s_ping);
    raw_bind(pcb, IP_ADDR_ANY);

    s2500_shell_printf("PING %s (%lu bytes)\r\n", ipaddr_ntoa(&target), (unsigned long)56);

    for (i = 0; i < count; i++) {
        s_ping.seq = (uint16_t)i;
        p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr) + 56, PBUF_RAM);
        if (!p) break;
        iecho = (struct icmp_echo_hdr *)p->payload;
        ICMPH_TYPE_SET(iecho, ICMP_ECHO);
        ICMPH_CODE_SET(iecho, 0);
        iecho->chksum = 0;
        iecho->id = htons(0x5A5A);
        iecho->seqno = htons((uint16_t)i);
        memset((u8_t *)iecho + sizeof(struct icmp_echo_hdr), 0xAB, 56);
        iecho->chksum = inet_chksum(iecho, p->len);
        s_ping.replies = 0;
        s_ping.sent_ms = s_cfg->hal->millis(s_cfg->hal->ctx);
        raw_sendto(pcb, p, &target);
        pbuf_free(p);

        {
            uint32_t t0 = s_cfg->hal->millis(s_cfg->hal->ctx);
            while (!s_ping.replies &&
                   (s_cfg->hal->millis(s_cfg->hal->ctx) - t0) < 1000) {
                s2500_app_poll();
            }
        }
        if (s_ping.replies) {
            s2500_shell_printf("  reply seq=%lu time=%lu ms\r\n",
                               (unsigned long)i, (unsigned long)s_ping.rtt_ms);
        } else {
            s2500_shell_printf("  timeout seq=%lu\r\n", (unsigned long)i);
        }
    }
    raw_remove(pcb);
    return 0;
}

/* ------------------------------------------------------------------ */
/* shell 命令                                                         */
/* ------------------------------------------------------------------ */
static int cmd_netif(int argc, char **argv)
{
    (void)argc; (void)argv;
    s2500_shell_printf("board : %s\r\n", s2500_app_board());
    s2500_shell_printf("mac   : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                       s_dev.mac[0], s_dev.mac[1], s_dev.mac[2],
                       s_dev.mac[3], s_dev.mac[4], s_dev.mac[5]);
    {
        /* ip4addr_ntoa 使用静态缓冲区, 同一 printf 里调用两次会互相覆盖 */
        char ipbuf[24], gwbuf[24];
        snprintf(ipbuf, sizeof(ipbuf), "%s", ip4addr_ntoa(netif_ip4_addr(netif_default)));
        snprintf(gwbuf, sizeof(gwbuf), "%s", ip4addr_ntoa(netif_ip4_gw(netif_default)));
        s2500_shell_printf("ip    : %s/24  gw %s\r\n", ipbuf, gwbuf);
    }
    s2500_shell_printf("link  : %s   plca id=%u\r\n",
                       netif_is_link_up(netif_default) ? "UP" : "DOWN",
                       s_dev.plca_id);
    return 0;
}

static int cmd_iperf(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "-s")) {
        if (!s_iperf) {
            s_iperf = lwiperf_start_tcp_server_default(NULL, NULL);
        }
        s2500_shell_printf("iperf server on port %d : %s\r\n", 5001,
                           s_iperf ? "started" : "failed");
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "-c")) {
        ip_addr_t dst;
        if (!ipaddr_aton(argv[2], &dst)) {
            s2500_shell_printf("bad ip\r\n");
            return 0;
        }
        s_iperf = lwiperf_start_tcp_client_default(&dst, NULL, NULL);
        s2500_shell_printf("iperf client -> %s : %s\r\n", argv[2],
                           s_iperf ? "started" : "failed");
        return 0;
    }
    s2500_shell_printf("usage: iperf -s | iperf -c <ip> [time]\r\n");
    return 0;
}

static int cmd_udpecho(int argc, char **argv)
{
    if (argc < 2) {
        s2500_shell_printf("udp echo: %s\r\n", s_udp_echo_on ? "on" : "off");
        return 0;
    }
    s_udp_echo_on = !strcmp(argv[1], "on");
    if (s_udp_echo_on) {
        udp_echo_start();
        s2500_shell_printf("udp echo on (port 7)\r\n");
    } else {
        udp_echo_stop();
        s2500_shell_printf("udp echo off\r\n");
    }
    return 0;
}

static int cmd_lwipstats(int argc, char **argv)
{
    (void)argc; (void)argv;
#if LWIP_STATS
    s2500_shell_printf("mem used=%lu max=%lu  memp PBUF used=%lu TCP_PCB used=%lu\r\n",
        (unsigned long)lwip_stats.mem.used, (unsigned long)lwip_stats.mem.max,
        (unsigned long)lwip_stats.memp[MEMP_PBUF]->used,
        (unsigned long)lwip_stats.memp[MEMP_TCP_PCB]->used);
    s2500_shell_printf("link recv=%lu xmit=%lu drop=%lu memerr=%lu\r\n",
        (unsigned long)lwip_stats.link.recv, (unsigned long)lwip_stats.link.xmit,
        (unsigned long)lwip_stats.link.drop, (unsigned long)lwip_stats.link.memerr);
#else
    s2500_shell_printf("LWIP_STATS disabled\r\n");
#endif
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    s2500_shell_printf("reboot\r\n");
    s2500_port_reboot();
    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 netif, cmd_netif, 显示网络接口信息);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 ping, cmd_ping, ICMP ping: ping <ip> [count]);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 iperf, cmd_iperf, TCP 吞吐: iperf -s | iperf -c <ip>);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 udpecho, cmd_udpecho, UDP 回显开关: udpecho on|off);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 tcpstats, cmd_lwipstats, lwIP 统计);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 reboot, cmd_reboot, 软件复位);

/* ------------------------------------------------------------------ */
/* 初始化 / 轮询                                                      */
/* ------------------------------------------------------------------ */
/* 当前生效的芯片配置: 上电初始化与看门狗自愈重初始化共用 */
static s2500_cfg_t s_sc;

/* 芯片初始化序列 (s2500_init -> s2500_configure -> s2500_start):
 * 上电和自愈都走这里, 保证 MAC 过滤 / PLCA / LED / 模拟参数一致 */
static int app_chip_apply(const s2500_cfg_t *sc)
{
    if (s2500_init(&s_dev, s_cfg->hal) != S2500_OK) {
        return S2500_ERR_PROTOCOL;
    }
    if (s2500_configure(&s_dev, sc) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    if (s2500_start(&s_dev) != S2500_OK) {
        return S2500_ERR_SPI;
    }
    return S2500_OK;
}

/* 看门狗检出芯片自发复位后的重初始化 (复用 app_chip_apply) */
static int app_wd_reinit(void *ctx)
{
    s2500_cfg_t sc = s_sc;
    uint8_t mac[6];
    uint8_t id, ncnt;
    uint32_t n, t0, t1;

    (void)ctx;
    n  = s2500_wd_state()->detections;
    t0 = s2500_wd_state()->last_detect_ms;

    /* 保留运行时的 PLCA / MAC 设置 (shell 的 s2500 plca / s2500 mac 可能改过) */
    id   = s_dev.plca_id;
    ncnt = s_dev.plca_ncnt;
    memcpy(mac, s_dev.mac, sizeof(mac));
    if ((mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) != 0 &&
        !(mac[0] & 1u)) {
        sc.mac = mac;
    }
    sc.plca_id = id;
    if (ncnt) {
        sc.plca_ncnt = ncnt;
    }

    s2500_shell_printf("\r\nS2500 已复位, 自动重新初始化 (第 %lu 次)\r\n",
                       (unsigned long)n);

    /* 芯片已回到默认态, 补一次软复位确保寄存器全部回到已知状态 */
    (void)s2500_soft_reset(&s_dev);
    if (app_chip_apply(&sc) != S2500_OK) {
        s2500_shell_printf("S2500 自动重新初始化失败, 下个检查周期重试\r\n");
        return -1;
    }
    t1 = s_cfg->hal->millis ? s_cfg->hal->millis(s_cfg->hal->ctx) : 0;
    s2500_shell_printf("S2500 自愈完成: PLCA id=%u ncnt=%u, 重新初始化耗时 %lu ms\r\n",
                       (unsigned)sc.plca_id, (unsigned)sc.plca_ncnt,
                       (unsigned long)(t1 - t0));
    return 0;
}

int s2500_app_init(const s2500_app_cfg_t *cfg)
{
    ip4_addr_t ip, mask, gw;

    if (!cfg || !cfg->hal) return S2500_ERR_PARAM;
    s_cfg = cfg;

    /* 允许 main 在初始化失败后重试: lwIP 与 shell 只初始化一次 */
    if (!s_lwip_inited) {
        lwip_init();
        s_lwip_inited = 1;
    }
    if (!s_shell_inited) {
        s2500_shell_init(&s_dev);
        s_shell_inited = 1;
    }

    memset(&s_sc, 0, sizeof(s_sc));
    s_sc.mac = cfg->mac;
    s_sc.plca_id = cfg->plca_id;
    s_sc.plca_ncnt = cfg->plca_ncnt ? cfg->plca_ncnt : 8;
    s_sc.plca_totmr = cfg->plca_totmr ? cfg->plca_totmr : 32;
    s_sc.plca_en = cfg->plca_en;
    s_sc.promiscuous = cfg->promiscuous;

    if (app_chip_apply(&s_sc) != S2500_OK) {
        s2500_shell_printf("\r\n[S2500] 初始化失败: SPI/ID 读取错误 (IDVER 应为 0x11)\r\n");
        return S2500_ERR_PROTOCOL;
    }

    /* 芯片初始化成功后启用自愈看门狗 (默认 on) */
    s2500_wd_set_reinit_cb(app_wd_reinit, NULL);
    s2500_wd_reset();

    IP4_ADDR(&ip, (cfg->ip >> 24) & 0xFF, (cfg->ip >> 16) & 0xFF,
             (cfg->ip >> 8) & 0xFF, cfg->ip & 0xFF);
    IP4_ADDR(&mask, (cfg->mask >> 24) & 0xFF, (cfg->mask >> 16) & 0xFF,
             (cfg->mask >> 8) & 0xFF, cfg->mask & 0xFF);
    IP4_ADDR(&gw, (cfg->gw >> 24) & 0xFF, (cfg->gw >> 16) & 0xFF,
             (cfg->gw >> 8) & 0xFF, cfg->gw & 0xFF);

    s2500_netif_add(&s_dev, &ip, &mask, &gw);

    udp_echo_start();
    tcp_echo_start();

    s2500_shell_printf("\r\n==============================================\r\n");
    s2500_shell_printf(" T30HM1TS2500 10BASE-T1S  %s\r\n", cfg->board_name ? cfg->board_name : "board");
    s2500_shell_printf(" MAC  : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                       s_dev.mac[0], s_dev.mac[1], s_dev.mac[2],
                       s_dev.mac[3], s_dev.mac[4], s_dev.mac[5]);
    {
        char ipbuf[24], gwbuf[24];
        snprintf(ipbuf, sizeof(ipbuf), "%s", ip4addr_ntoa(&ip));
        snprintf(gwbuf, sizeof(gwbuf), "%s", ip4addr_ntoa(&gw));
        s2500_shell_printf(" IPv4 : %s/24  gw %s\r\n", ipbuf, gwbuf);
    }
    s2500_shell_printf(" PLCA : id=%u ncnt=%u en=%u\r\n",
                       s_sc.plca_id, s_sc.plca_ncnt, s_sc.plca_en);
    s2500_shell_printf(" shell: 输入 help 查看命令, s2500 info 查看芯片\r\n");
    s2500_shell_printf("==============================================\r\n");
    return S2500_OK;
}

void s2500_app_poll(void)
{
    struct netif *n = s2500_app_netif();
    if (n) {
        s2500_netif_poll(n);
    }
    s2500_shell_poll();
    s2500_wd_poll(&s_dev);      /* 芯片自发复位自愈: 每 S2500_WD_PERIOD_MS 一次 */
    sys_check_timeouts();
}

s2500_dev_t *s2500_app_dev(void) { return &s_dev; }
struct netif *s2500_app_netif(void) { return netif_default; }
const char *s2500_app_board(void) { return s_cfg && s_cfg->board_name ? s_cfg->board_name : "board"; }