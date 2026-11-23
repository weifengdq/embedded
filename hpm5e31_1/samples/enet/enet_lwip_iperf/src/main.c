/*
 * enet_lwip_iperf - YT8011AN + lwIP(iperf) 千兆以太网吞吐测试
 *
 * 硬件: hpm5e31_1 (ENET0 RGMII + YT8011AN 1000BASE-T1) <-> 千兆车载以太网转换盒 <-> 主机 enp6s0
 *
 * 网络: 板端 192.168.2.100/24, 主机 192.168.2.1/24, 网关 192.168.2.1
 * 协议: lwIP 自带 lwiperf 实现的是 **iperf2** 协议（默认端口 5001），主机请用 'iperf'（v2）而不是 iperf3
 *
 * shell 命令: ip / link / iserver [tcp|udp] / iclient <ip> [tcp|udp] / istop / mode <1000|100> / rst
 * 现象与实测数据: 见 README.md 与 logs/
 */
#include <string.h>
#include "bsp.h"
#include "common.h"
#include "netconf.h"
#include "netinfo.h"
#include "ethernetif.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/pbuf.h"
#include "hpm_yt8011.h"
#include "hpm_csr_drv.h"
#include "hpm_csr_regs.h"

#ifndef IPERF_UDP_CLIENT_RATE
#define IPERF_UDP_CLIENT_RATE   (1000U * 1024U * 1024U)   /* 1 Gbit/s 需求速率 */
#endif
#ifndef IPERF_CLIENT_AMOUNT
#define IPERF_CLIENT_AMOUNT     (-1000)                    /* -1000 = 10 秒 */
#endif

static void *s_iperf_session;
static uint32_t s_last_print_ms;
static uint32_t s_last_cpu_ms;
static uint64_t s_busy_cyc;
static uint64_t s_total_cyc;

static void iperf_report(void *arg, enum lwiperf_report_type report_type,
                         const ip_addr_t *local_addr, u16_t local_port,
                         const ip_addr_t *remote_addr, u16_t remote_port,
                         u32_t bytes_transferred, u32_t ms_duration, u32_t bandwidth_kbitpsec)
{
    (void) arg; (void) local_addr; (void) local_port;
    printf("[iperf] type=%d remote=%s:%u bytes=%lu ms=%lu => %lu kbit/s (%.2f Mbit/s)\r\n",
           (int) report_type, ipaddr_ntoa(remote_addr), (unsigned) remote_port,
           (unsigned long) bytes_transferred, (unsigned long) ms_duration,
           (unsigned long) bandwidth_kbitpsec, (double) bandwidth_kbitpsec / 1000.0);
    if ((report_type == LWIPERF_TCP_DONE_CLIENT) || (report_type == LWIPERF_TCP_DONE_SERVER) ||
        (report_type == LWIPERF_UDP_DONE_CLIENT) || (report_type == LWIPERF_UDP_DONE_SERVER)) {
        s_iperf_session = NULL;
    }
}

static void print_netinfo(void)
{
    printf("[net] netif  : %c%c (index=%u)\r\n", gnetif.name[0], gnetif.name[1],
           (unsigned) netif_get_index(&gnetif));
    printf("[net] MAC    : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           gnetif.hwaddr[0], gnetif.hwaddr[1], gnetif.hwaddr[2],
           gnetif.hwaddr[3], gnetif.hwaddr[4], gnetif.hwaddr[5]);
    printf("[net] IP     : %s\r\n", ipaddr_ntoa(netif_ip_addr4(&gnetif)));
    printf("[net] NETMASK: %s\r\n", ipaddr_ntoa(netif_ip_netmask4(&gnetif)));
    printf("[net] GW     : %s\r\n", ipaddr_ntoa(netif_ip_gw4(&gnetif)));
    printf("[net] link   : %s\r\n", netif_is_link_up(&gnetif) ? "UP" : "DOWN");
}

static void print_phy(void)
{
    yt8011_status_t st;
    if (yt8011_get_status(BOARD_NET2_ENET_BASE, BOARD_NET2_PHY_ADDR, &st) != status_success) {
        printf("[phy] status read failed\r\n");
        return;
    }
    printf("[phy] link=%s speed=%s role=%s PHYSR=%04X GBSR=%04X BMSR=%04X thread=%u\r\n",
           st.link_up ? "UP" : "DOWN", yt8011_speed_str(st.speed),
           st.master ? "MASTER" : "SLAVE", st.physr, st.gbsr, st.bmsr,
           (unsigned) netif_is_link_up(&gnetif));
}

/* ---------------- shell 命令 ---------------- */
static void cmd_ip(int argc, char *argv[])
{
    (void) argc; (void) argv;
    print_netinfo();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ip, cmd_ip, show lwIP netif info);

static void cmd_link(int argc, char *argv[])
{
    (void) argc; (void) argv;
    print_phy();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 link, cmd_link, show PHY + netif link status);

static void cmd_iserver(int argc, char *argv[])
{
    bool udp = (argc >= 2) && (strncmp(argv[1], "udp", 3) == 0);
    if (s_iperf_session != NULL) {
        printf("[iperf] a session is running, use 'istop' first\r\n");
        return;
    }
    if (udp) {
        s_iperf_session = lwiperf_start_udp_server(netif_ip_addr4(&gnetif), LWIPERF_UDP_PORT_DEFAULT,
                                                   iperf_report, NULL);
    } else {
        s_iperf_session = lwiperf_start_tcp_server_default(iperf_report, NULL);
    }
    printf("[iperf] %s server %s on port %u\r\n", udp ? "UDP" : "TCP",
           (s_iperf_session != NULL) ? "started" : "FAILED",
           (unsigned) (udp ? LWIPERF_UDP_PORT_DEFAULT : LWIPERF_TCP_PORT_DEFAULT));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 iserver, cmd_iserver, start iperf2 server: iserver [tcp|udp]);

static void cmd_iclient(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: iclient <remote_ip> [tcp|udp]\r\n");
        return;
    }
    bool udp = (argc >= 3) && (strncmp(argv[2], "udp", 3) == 0);
    ip4_addr_t remote4;
    ip_addr_t remote;
    if (!ip4addr_aton(argv[1], &remote4)) {
        printf("[iperf] bad ip: %s\r\n", argv[1]);
        return;
    }
    ip_addr_copy_from_ip4(remote, remote4);
    if (s_iperf_session != NULL) {
        printf("[iperf] a session is running, use 'istop' first\r\n");
        return;
    }
    if (udp) {
        s_iperf_session = lwiperf_start_udp_client(netif_ip_addr4(&gnetif), LWIPERF_UDP_PORT_DEFAULT,
                                                   &remote, LWIPERF_UDP_PORT_DEFAULT,
                                                   LWIPERF_CLIENT, IPERF_CLIENT_AMOUNT,
                                                   IPERF_UDP_CLIENT_RATE, 0, iperf_report, NULL);
    } else {
        s_iperf_session = lwiperf_start_tcp_client_default(&remote, iperf_report, NULL);
    }
    printf("[iperf] %s client to %s %s\r\n", udp ? "UDP" : "TCP", argv[1],
           (s_iperf_session != NULL) ? "started" : "FAILED");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 iclient, cmd_iclient, start iperf2 client: iclient <ip> [tcp|udp]);

/* 延时命令：方便 --send "sleep 8000;iclient ..." 这类需要等链路起来的脚本化测试 */
static void cmd_sleep(int argc, char *argv[])
{
    uint32_t ms = 1000;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &ms);
    }
    printf("[sys] sleep %u ms\r\n", (unsigned) ms);
    board_delay_ms(ms);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sleep, cmd_sleep, delay N ms: sleep <ms>);

static void cmd_istop(int argc, char *argv[])
{
    (void) argc; (void) argv;
    if (s_iperf_session != NULL) {
        lwiperf_abort(s_iperf_session);
        s_iperf_session = NULL;
        printf("[iperf] aborted\r\n");
    } else {
        printf("[iperf] no active session\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 istop, cmd_istop, abort iperf session);

static void report_cpu(void)
{
    uint32_t cpu_hz = (uint32_t) clock_get_frequency(clock_cpu0);
    double pct = (s_total_cyc == 0U) ? 0.0 : (double) s_busy_cyc * 100.0 / (double) s_total_cyc;
    printf("[cpu] busy=%.1f%% (cpu=%lu Hz, window=%.2f s)\r\n", pct, (unsigned long) cpu_hz,
           (double) s_total_cyc / (double) cpu_hz);
    s_busy_cyc = 0U;
    s_total_cyc = 0U;
}

static void cmd_cpu(int argc, char *argv[])
{
    (void) argc; (void) argv;
    report_cpu();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 cpu, cmd_cpu, report CPU usage since last call (non-blocking));

/* [tp-net2 诊断] 驱动层直发微基准：绕过 lwIP 协议栈，直接反复把同一个 1460B pbuf
 * 交给 enet_lwip_output()，测 ENET TX 描述符 + DMA 的纯发送上限（含等待描述符完成的自旋）。
 * 用途：区分“瓶颈在 ENET 驱动/DMA”还是“在 lwIP 协议栈/iperf 发送泵”。 */
static void cmd_txbench(int argc, char *argv[])
{
    uint32_t frames = 20000, len = 1460;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &frames);
    }
    if (argc >= 3) {
        bsp_parse_u32(argv[2], &len);
    }
    if (len < 60U) { len = 60U; }
    if (len > 1500U) { len = 1500U; }
    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t) len, PBUF_RAM);
    if (p == NULL) {
        printf("[txbench] pbuf_alloc failed\r\n");
        return;
    }
    memset(p->payload, 0x5A, len);
    uint32_t cpu_hz = (uint32_t) clock_get_frequency(clock_cpu0);
    uint64_t t0 = hpm_csr_get_core_mcycle();
    uint32_t sent = 0, fail = 0;
    while (sent < frames) {
        if (enet_lwip_output(ENET, &desc[0], p) == ERR_OK) {
            sent++;
        } else {
            fail++;
            if (fail > 5000000U) { break; }
        }
    }
    uint64_t t1 = hpm_csr_get_core_mcycle();
    board_delay_ms(100);   /* 等 TX 环排空 */
    pbuf_free(p);
    double sec = (double) (t1 - t0) / (double) cpu_hz;
    double pps = (sec > 0.0) ? (double) sent / sec : 0.0;
    printf("[txbench] len=%lu sent=%lu fail=%lu %.3f s => %.0f pkt/s, %.1f Mbit/s, %.0f cyc/frame\r\n",
           (unsigned long) len, (unsigned long) sent, (unsigned long) fail, sec, pps, pps * (double) len * 8.0 / 1e6,
           (sec > 0.0) ? (double) (t1 - t0) / (double) sent : 0.0);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 txbench, cmd_txbench, driver-level TX microbenchmark: txbench [frames]);

/* [tp-net2 诊断] 打印 L1 D-cache 控制寄存器，确认是否开启 write-around */
static void cmd_cacheinfo(int argc, char *argv[])
{
    (void) argc; (void) argv;
    uint32_t ctl = (uint32_t) read_csr(CSR_MCACHE_CTL);
    printf("[cache] MCACHE_CTL=0x%08lx dc_en=%lu ic_en=%lu writearound=%lu\r\n",
           (unsigned long) ctl,
           (unsigned long) ((ctl >> 1) & 1U),
           (unsigned long) ((ctl >> 0) & 1U),
           (unsigned long) ((ctl >> 13) & 3U));   /* MCACHE_CTL[14:13]: 0=writeback, 3=write-around */
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 cacheinfo, cmd_cacheinfo, show L1 D-cache control register);

static void cmd_mode(int argc, char *argv[])
{
    uint32_t spd = 1000;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &spd);
    }
    yt8011_config_t cfg;
    yt8011_basic_mode_default_config(BOARD_NET2_ENET_BASE, &cfg);
    cfg.speed = (spd == 100U) ? yt8011_speed_100m : yt8011_speed_1000m;
    cfg.master = false;
    printf("[phy] reconfigure %s slave ...\r\n", yt8011_speed_str(cfg.speed));
    if (yt8011_basic_mode_init(BOARD_NET2_ENET_BASE, BOARD_NET2_PHY_ADDR, &cfg) != status_success) {
        printf("[phy] reconfigure failed\r\n");
        return;
    }
    yt8011_status_t st;
    if (yt8011_wait_link_up(BOARD_NET2_ENET_BASE, BOARD_NET2_PHY_ADDR, 3000U, &st) == status_success) {
        printf("[phy] link up at %s\r\n", yt8011_speed_str(st.speed));
    } else {
        printf("[phy] link down after reconfigure (对端速率/主从不匹配?)\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mode, cmd_mode, set PHY speed: mode <1000|100>);

int main(void)
{
    bsp_init();
    printf("\r\n=== enet_lwip_iperf: YT8011AN(1000BASE-T1) + lwIP + iperf2 ===\r\n");
    printf("lwIP %s\r\n", LWIP_VERSION_STRING);

    if (enet_init(ENET) != status_success) {
        printf("Enet initialization fails !!!\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }
    printf("[net] Enet controller + PHY init done\r\n");

    lwip_init();
    netif_config(&gnetif);
    enet_services(&gnetif);

    /* 1ms 系统时基：sys_now()/lwIP 超时/链路自适应都依赖它 */
    board_timer_create(LWIP_APP_TIMER_INTERVAL, sys_timer_callback);

    print_netinfo();
    print_phy();
    printf("[net] commands: ip | link | iserver [tcp|udp] | iclient <ip> [tcp|udp] | istop | cpu | sleep | mode <1000|100>\r\n");

    uint64_t t_prev = hpm_csr_get_core_mcycle();
    while (1) {
        uint64_t t0 = hpm_csr_get_core_mcycle();
        enet_common_handler(&gnetif);
        lwiperf_poll_udp_client();  /* UDP client 需要每轮轮询才真正发包（lwIP lwiperf 的要求） */
        bsp_shell_poll();
        uint64_t t1 = hpm_csr_get_core_mcycle();
        s_busy_cyc += (t1 - t0);
        s_total_cyc += (t1 - t_prev);
        t_prev = t1;
        /* iperf 运行期间每 5 s 上报一次 CPU 占用，便于在串口日志里直接取数 */
        if ((s_iperf_session != NULL) && (bsp_uptime_ms() - s_last_cpu_ms >= 5000U)) {
            s_last_cpu_ms = bsp_uptime_ms();
            report_cpu();
        }
        /* 每 10 s 打印一次链路状态，便于长时间观测（不在 iperf 期间刷屏） */
        if (bsp_uptime_ms() - s_last_print_ms >= 10000U) {
            s_last_print_ms = bsp_uptime_ms();
            if (!s_iperf_session) {
                print_phy();
            }
        }
    }
    return 0;
}
