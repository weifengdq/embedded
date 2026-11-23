/*
 * lan8651_lwip_iperf - LAN8651 (10BASE-T1S MAC-PHY, TC6 over SPI2) + lwIP + iperf2
 *
 * 板端固定 IP 192.168.1.100/24，网关 192.168.1.1（主机 USB 10BASE-T1S 适配器
 * enx001ec0d1c337 的地址）。数据通道 = TC6（68 B chunk + TX credits），
 * lwIP 适配层见 src/oa_tc6_netif.c。
 *
 * 命令：
 *   ip                       打印 MAC/IP/掩码/网关/netif 状态
 *   link                     打印 LAN8651 PHY 链路 + PLCA 状态
 *   iserver [tcp|udp]        板端做 iperf2 服务端（主机 iperf -c）
 *   iclient <ip> [tcp|udp] [secs] [kbps]  板端做 iperf2 客户端（主机 iperf -s）
 *   istop                    停止当前 iperf 会话
 *   stats                    TC6 + netif 统计
 *   speed <hz>               改 SPI2 SCLK（默认 20 MHz）
 */
#include <string.h>
#include "bsp.h"
#include "oa_tc6.h"
#include "lan865x.h"
#include "oa_tc6_netif.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/ip4_addr.h"
#include "lwip/apps/lwiperf.h"
#include "netif/ethernet.h"

#ifndef BOARD_NET1_IP
#define BOARD_NET1_IP      "192.168.1.100"
#define BOARD_NET1_NETMASK "255.255.255.0"
#define BOARD_NET1_HOST_IP "192.168.1.1"
#endif

#define LAN8651_SPI_SCLK_DEFAULT 20000000UL

static oa_tc6_t s_tc6;
static struct netif gnetif;
static uint8_t s_mac[6] = {BOARD_LAN8651_MAC0, BOARD_LAN8651_MAC1, BOARD_LAN8651_MAC2,
                           BOARD_LAN8651_MAC3, BOARD_LAN8651_MAC4, BOARD_LAN8651_MAC5};
static void *s_iperf_session;
static uint32_t s_last_link_poll;

static uint32_t now_ms(void) { return bsp_uptime_ms(); }

static void iperf_report(void *arg, enum lwiperf_report_type type,
                         const ip_addr_t *local_addr, u16_t local_port,
                         const ip_addr_t *remote_addr, u16_t remote_port,
                         u32_t bytes, u32_t ms_duration, u32_t bandwidth_kbitpsec)
{
    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(local_addr);
    LWIP_UNUSED_ARG(local_port);
    printf("[iperf] type=%d remote=%s:%u bytes=%lu ms=%lu kbit/s=%lu\r\n",
           (int) type, ipaddr_ntoa(remote_addr), (unsigned) remote_port,
           (unsigned long) bytes, (unsigned long) ms_duration,
           (unsigned long) bandwidth_kbitpsec);
}

static int board_bring_up(void)
{
    uint32_t src;
    int ret;

    init_lan8651_pins();
    src = board_init_spi_clock(BOARD_LAN8651_SPI);
    if (src == 0U) {
        src = clock_get_frequency(clock_spi2);
    }
    if (status_success != oa_tc6_spi_init(&s_tc6, BOARD_LAN8651_SPI, src, LAN8651_SPI_SCLK_DEFAULT)) {
        printf("[init] SPI2 init failed\r\n");
        return -1;
    }
    {
        uint32_t v = 0;
        if ((oa_tc6_read_reg(&s_tc6, OA_TC6_REG_STDCAP, &v) != 0) ||
            (memcmp(&s_tc6.ctrl_rx[4], s_tc6.ctrl_tx, 4) != 0)) {
            printf("[init] SPI2 controller gives no echo -> bit-banged transport\r\n");
            oa_tc6_use_bitbang(&s_tc6, true);
        } else {
            printf("[init] SPI2 controller echo OK, SCLK %lu Hz\r\n",
                   (unsigned long) oa_tc6_spi_get_sclk(&s_tc6, src));
        }
    }
    ret = lan865x_full_init(&s_tc6, s_mac);
    if (ret != 0) {
        printf("[init] LAN8651 bring-up failed: %s (%d)\r\n", lan865x_strerror(ret), ret);
        return ret;
    }
    (void) lan865x_mac_set_promisc(&s_tc6, true);
    {
        lan865x_plca_cfg_t plca = {
            .enabled = false, .node_id = 0, .node_cnt = BOARD_PLCA_NODE_CNT_DEFAULT,
            .to_tmr = BOARD_PLCA_TO_TMR_DEFAULT, .burst_cnt = BOARD_PLCA_BURST_CNT_DEFAULT,
            .burst_tmr = BOARD_PLCA_BURST_TMR_DEFAULT,
        };
        (void) lan865x_plca_configure(&s_tc6, &plca);
    }
    printf("[init] LAN8651 up: MAC %02x:%02x:%02x:%02x:%02x:%02x, transport=%s\r\n",
           s_mac[0], s_mac[1], s_mac[2], s_mac[3], s_mac[4], s_mac[5],
           s_tc6.use_bb ? "bit-bang" : "SPI2");
    return 0;
}

static void cmd_ip(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("[net] netif %c%c%u  MAC %02x:%02x:%02x:%02x:%02x:%02x\r\n",
           gnetif.name[0], gnetif.name[1], (unsigned) gnetif.num,
           gnetif.hwaddr[0], gnetif.hwaddr[1], gnetif.hwaddr[2],
           gnetif.hwaddr[3], gnetif.hwaddr[4], gnetif.hwaddr[5]);
    /* ip4addr_ntoa() returns one static buffer: print one address per call */
    printf("[net] IP   %s\r\n", ip4addr_ntoa(netif_ip4_addr(&gnetif)));
    printf("[net] mask %s\r\n", ip4addr_ntoa(netif_ip4_netmask(&gnetif)));
    printf("[net] gw   %s\r\n", ip4addr_ntoa(netif_ip4_gw(&gnetif)));
    printf("[net] MTU  %u\r\n", (unsigned) gnetif.mtu);
    printf("[net] up=%u link=%u\r\n", (unsigned) netif_is_up(&gnetif),
           (unsigned) netif_is_link_up(&gnetif));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ip, cmd_ip, show MAC/IP/mask/gw/netif state);

static void cmd_link(int argc, char *argv[])
{
    uint16_t bmsr1 = 0, bmsr2 = 0, phyidr1 = 0, phyidr2 = 0;
    lan865x_plca_cfg_t plca;
    (void) argc; (void) argv;
    (void) oa_tc6_c22_read(&s_tc6, 0x01, &bmsr1);
    (void) oa_tc6_c22_read(&s_tc6, 0x01, &bmsr2);
    (void) oa_tc6_c22_read(&s_tc6, 0x02, &phyidr1);
    (void) oa_tc6_c22_read(&s_tc6, 0x03, &phyidr2);
    memset(&plca, 0, sizeof(plca));
    (void) lan865x_plca_read(&s_tc6, &plca);
    printf("[link] PHYID=0x%04x%04x BMSR=0x%04x/0x%04x link=%u netif_link=%u\r\n",
           phyidr1, phyidr2, bmsr1, bmsr2, (unsigned) (((bmsr1 & bmsr2) & 0x0004U) != 0U),
           (unsigned) netif_is_link_up(&gnetif));
    printf("[link] PLCA en=%u node=%u/%u to=%u burst=%u/%u status=0x%04x PST=%u\r\n",
           (unsigned) plca.enabled, (unsigned) plca.node_id, (unsigned) plca.node_cnt,
           (unsigned) plca.to_tmr, (unsigned) plca.burst_cnt, (unsigned) plca.burst_tmr,
           (unsigned) plca.status, (unsigned) ((plca.status >> 15) & 1U));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 link, cmd_link, show LAN8651 PHY link and PLCA status);

static void cmd_iserver(int argc, char *argv[])
{
    bool tcp = true;
    if (argc >= 2 && argv[1][0] == 'u') {
        tcp = false;
    }
    if (!netif_is_link_up(&gnetif)) {
        printf("[iperf] link is down, wait for LAN8651 link up\r\n");
        return;
    }
    if (tcp) {
        s_iperf_session = lwiperf_start_tcp_server_default(iperf_report, NULL);
    } else {
        s_iperf_session = lwiperf_start_udp_server(netif_ip_addr4(&gnetif),
                                                   LWIPERF_UDP_PORT_DEFAULT, iperf_report, NULL);
    }
    printf("[iperf] %s server on port %u : %s\r\n", tcp ? "TCP" : "UDP",
           (unsigned) (tcp ? LWIPERF_TCP_PORT_DEFAULT : LWIPERF_UDP_PORT_DEFAULT),
           s_iperf_session != NULL ? "started" : "failed");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 iserver, cmd_iserver, start iperf2 server: iserver [tcp|udp]);

static void cmd_iclient(int argc, char *argv[])
{
    ip_addr_t remote;
    bool tcp = true;
    uint32_t amount = 1000;
    uint32_t kbps = 10000;
    if (argc < 2) {
        printf("usage: iclient <host ip> [tcp|udp] [seconds] [kbps]\r\n");
        return;
    }
    if (!ipaddr_aton(argv[1], &remote)) {
        printf("[iperf] bad ip %s\r\n", argv[1]);
        return;
    }
    if (argc >= 3 && argv[2][0] == 'u') {
        tcp = false;
    }
    if (argc >= 4) {
        (void) bsp_parse_u32(argv[3], &amount);
    }
    if (argc >= 5) {
        (void) bsp_parse_u32(argv[4], &kbps);
    }
    for (uint32_t wait = 0; wait < 10000U && !netif_is_link_up(&gnetif); wait += 100U) {
        oa_tc6_netif_poll(&gnetif);
        sys_check_timeouts();
        (void) oa_tc6_netif_update_link(&gnetif);
        board_delay_ms(100);
    }
    if (!netif_is_link_up(&gnetif)) {
        printf("[iperf] link is down after 10 s, abort\r\n");
        return;
    }
    if (tcp) {
        s_iperf_session = lwiperf_start_tcp_client_default(&remote, iperf_report, NULL);
    } else {
        /* amount < 0 means "run for |amount| ms" in lwiperf's UDP client */
        s_iperf_session = lwiperf_start_udp_client(netif_ip_addr4(&gnetif),
                                                   LWIPERF_UDP_PORT_DEFAULT, &remote,
                                                   LWIPERF_UDP_PORT_DEFAULT, LWIPERF_CLIENT,
                                                   -(int32_t) amount, kbps * 1024U, 0,
                                                   iperf_report, NULL);
    }
    printf("[iperf] %s client -> %s (%lu s, %lu kbit/s): %s\r\n", tcp ? "TCP" : "UDP",
           ipaddr_ntoa(&remote), (unsigned long) amount, (unsigned long) kbps,
           s_iperf_session != NULL ? "started" : "failed");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 iclient, cmd_iclient, start iperf2 client: iclient <ip> [tcp|udp] [secs] [kbps]);

static void cmd_istop(int argc, char *argv[])
{
    (void) argc; (void) argv;
    if (s_iperf_session != NULL) {
        lwiperf_abort(s_iperf_session);
        s_iperf_session = NULL;
        printf("[iperf] session aborted\r\n");
    } else {
        printf("[iperf] no session\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 istop, cmd_istop, abort the running iperf2 session);

static void cmd_stats(int argc, char *argv[])
{
    const oa_tc6_netif_stats_t *ns = oa_tc6_netif_stats();
    (void) argc; (void) argv;
    printf("[tc6] ctrl xfers=%lu err=%lu | data xfers=%lu | tx_frames=%lu tx_bytes=%lu\r\n",
           (unsigned long) s_tc6.stat_ctrl_xfer, (unsigned long) s_tc6.stat_ctrl_err,
           (unsigned long) s_tc6.stat_data_xfer,
           (unsigned long) s_tc6.stat_tx_frames, (unsigned long) s_tc6.stat_tx_bytes);
    printf("[tc6] rx_frames=%lu rx_bytes=%lu dropped=%lu status0=0x%08lx\r\n",
           (unsigned long) s_tc6.stat_rx_frames, (unsigned long) s_tc6.stat_rx_bytes,
           (unsigned long) s_tc6.stat_rx_dropped, (unsigned long) s_tc6.stat_status0);
    printf("[netif] tx=%lu tx_fail=%lu rx=%lu rx_drop=%lu link_changes=%lu\r\n",
           (unsigned long) ns->tx_frames, (unsigned long) ns->tx_fail,
           (unsigned long) ns->rx_frames, (unsigned long) ns->rx_dropped,
           (unsigned long) ns->link_changes);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stats, cmd_stats, TC6 and lwIP statistics);

static void cmd_speed(int argc, char *argv[])
{
    uint32_t hz = 0;
    if (argc < 2 || bsp_parse_u32(argv[1], &hz) != 0 || hz < 100000UL || hz > 25000000UL) {
        printf("usage: speed <hz> (current %lu)\r\n", (unsigned long) s_tc6.sclk_hz);
        return;
    }
    if (s_tc6.use_bb) {
        printf("bit-bang transport active\r\n");
        return;
    }
    if (status_success != oa_tc6_spi_init(&s_tc6, BOARD_LAN8651_SPI,
                                          clock_get_frequency(clock_spi2), hz)) {
        printf("spi init failed\r\n");
        return;
    }
    printf("SPI2 SCLK = %lu Hz\r\n", (unsigned long) oa_tc6_spi_get_sclk(&s_tc6, clock_get_frequency(clock_spi2)));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 speed, cmd_speed, set SPI2 SCLK: speed <hz>);

int main(void)
{
    ip4_addr_t ip, mask, gw;

    bsp_init();
    printf("\r\n=== hpm5e31_1 LAN8651 10BASE-T1S + lwIP + iperf2 ===\r\n");
    printf("lwIP %s, NO_SYS=1, polling\r\n", LWIP_VERSION_STRING);
    if (board_bring_up() != 0) {
        printf("[init] bring-up failed, commands still available for debugging\r\n");
    }

    lwip_init();
    IP4_ADDR(&ip, 192, 168, 1, 100);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 1, 1);
    oa_tc6_netif_attach(&s_tc6, s_mac);
    if (netif_add(&gnetif, &ip, &mask, &gw, NULL, oa_tc6_netif_init, ethernet_input) == NULL) {
        printf("[net] netif_add failed\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }
    netif_set_default(&gnetif);
    netif_set_up(&gnetif);
    printf("[net] netif %c%c added: IP %s/24 gw %s, waiting for PHY link\r\n",
           gnetif.name[0], gnetif.name[1], ip4addr_ntoa(&ip), ip4addr_ntoa(&gw));

    s_last_link_poll = now_ms();
    while (1) {
        bsp_shell_poll();
        oa_tc6_netif_poll(&gnetif);
        sys_check_timeouts();
#if LWIP_UDP
        lwiperf_poll_udp_client();
#endif
        if ((now_ms() - s_last_link_poll) >= 2000U) {
            s_last_link_poll = now_ms();
            (void) oa_tc6_netif_update_link(&gnetif);
        }
    }
    return 0;
}
