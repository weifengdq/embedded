/*
 * app_lwip.c - 应用层 lwIP 组件实现（ENET0 RGMII + YT8011AN + baremetal lwIP 2.1.2）
 *
 * 初始化序列与 samples/enet/enet_lwip_iperf 完全一致：
 *   enet_init(ENET) -> lwip_init() -> netif_config(&gnetif) -> enet_services(&gnetif)
 *   -> board_timer_create(1 ms, sys_timer_callback)
 * 主循环：app_lwip_poll() = enet_common_handler(&gnetif)
 *   （内部做 ethernetif_input + sys_check_timeouts，等价于"收包驱动 + lwIP 定时器"）
 *
 * 本文件同时提供 YT8011AN 的 3 个板级钩子（替换 SDK 的 CONFIG_ENET_PHY 路径）：
 *   board_init_enet_phy() / board_get_enet_phy_status() / lwip_enet_phy_adaptive_poll()
 * 因此**不要**再编译 enet_lwip_iperf/src/phy_glue.c，也不要定义 CONFIG_ENET_PHY。
 *
 * 作者：tp-infra
 */
#include <string.h>
#include <stdio.h>

#include "app_lwip.h"

#include "board.h"
#include "ext/board_net2.h"
#include "common.h"
#include "netconf.h"
#include "netinfo.h"
#include "ethernetif.h"

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"

#include "hpm_yt8011.h"
#include "hpm_enet_phy.h"
#include "enet_phy_adaptive_lwip.h"

static bool s_inited;

/* ---------------- YT8011AN 板级钩子（被 SDK common.c enet_init 调用） ---------------- */

void board_get_enet_phy_status(uint8_t idx, void *status)
{
    enet_phy_status_t *st = (enet_phy_status_t *) status;
    yt8011_status_t ys;

    if ((idx != 0U) || (st == NULL)) {
        return;
    }
    memset(&ys, 0, sizeof(ys));
    if (yt8011_get_status(BOARD_NET2_ENET_BASE, BOARD_NET2_PHY_ADDR, &ys) != status_success) {
        st->enet_phy_link = enet_phy_link_unknown;
        st->enet_phy_speed_valid = 0U;
        return;
    }
    st->enet_phy_link = ys.link_up ? enet_phy_link_up : enet_phy_link_down;
    st->enet_phy_speed = (ys.speed == yt8011_speed_1000m) ? enet_phy_port_speed_1000mbps
                                                          : enet_phy_port_speed_100mbps;
    st->enet_phy_duplex = enet_phy_duplex_full;
    st->enet_phy_speed_valid = ys.link_up ? 1U : 0U;
}

hpm_stat_t board_init_enet_phy(ENET_Type *ptr)
{
    yt8011_config_t cfg;
    yt8011_status_t st;

    if (ptr != BOARD_NET2_ENET_BASE) {
        return status_invalid_argument;
    }
    if (!yt8011_check_id(ptr, BOARD_NET2_PHY_ADDR)) {
        printf("[lwip] YT8011AN ID check failed (expect 0x4F51)\r\n");
        return status_fail;
    }
    yt8011_basic_mode_default_config(ptr, &cfg);
    cfg.speed = yt8011_speed_1000m;   /* 1000BASE-T1 + Slave（与板上 strap 一致，第 12 章实测） */
    cfg.master = false;
    if (yt8011_basic_mode_init(ptr, BOARD_NET2_PHY_ADDR, &cfg) != status_success) {
        printf("[lwip] YT8011AN basic mode init failed\r\n");
        return status_fail;
    }
    if (yt8011_wait_link_up(ptr, BOARD_NET2_PHY_ADDR, 2000U, &st) != status_success) {
        printf("[lwip] YT8011AN link not up yet (由 2 s 自适应定时器继续轮询)\r\n");
    }
    return status_success;
}

void lwip_enet_phy_adaptive_poll(const lwip_enet_phy_adaptive_binding_t *binding)
{
    enet_phy_status_t status;

    if (binding == NULL) {
        return;
    }
    memset(&status, 0, sizeof(status));
    board_get_enet_phy_status(binding->phy_port, &status);
    if (binding->last != NULL) {
        *binding->last = status;
    }
    if (binding->notify_netif) {
        struct netif *netif = netif_get_by_index(binding->netif_idx);
        if (netif != NULL) {
            if (status.enet_phy_link == enet_phy_link_up) {
                netif_set_link_up(netif);
            } else if (status.enet_phy_link == enet_phy_link_down) {
                netif_set_link_down(netif);
            }
        }
    }
}

/* ---------------- 状态打印 ---------------- */

void app_lwip_print(void)
{
    yt8011_status_t st;

    printf("[lwip] netif=%c%c idx=%u MAC=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
           gnetif.name[0], gnetif.name[1], (unsigned) netif_get_index(&gnetif),
           gnetif.hwaddr[0], gnetif.hwaddr[1], gnetif.hwaddr[2],
           gnetif.hwaddr[3], gnetif.hwaddr[4], gnetif.hwaddr[5]);
    /* 注意：ipaddr_ntoa() 返回静态缓冲区，一行 printf 里调 3 次会全部打印成最后一个，
       所以这里拆成 3 次 printf（第 29 章"易错点"里有记录） */
    printf("[lwip] IP=%s link=%s\r\n", ipaddr_ntoa(netif_ip_addr4(&gnetif)),
           netif_is_link_up(&gnetif) ? "UP" : "DOWN");
    printf("[lwip] MASK=%s\r\n", ipaddr_ntoa(netif_ip_netmask4(&gnetif)));
    printf("[lwip] GW=%s\r\n", ipaddr_ntoa(netif_ip_gw4(&gnetif)));
    if (yt8011_get_status(BOARD_NET2_ENET_BASE, BOARD_NET2_PHY_ADDR, &st) == status_success) {
        printf("[lwip] PHY link=%s speed=%s role=%s PHYSR=%04X GBSR=%04X BMSR=%04X\r\n",
               st.link_up ? "UP" : "DOWN", yt8011_speed_str(st.speed),
               st.master ? "MASTER" : "SLAVE", st.physr, st.gbsr, st.bmsr);
    }
}

/* ---------------- 初始化 / 轮询 ---------------- */

int app_lwip_init(const app_lwip_cfg_t *cfg)
{
    static const app_lwip_cfg_t def = {
        APP_LWIP_DEFAULT_IP, APP_LWIP_DEFAULT_NETMASK, APP_LWIP_DEFAULT_GW,
        APP_LWIP_DEFAULT_LINK_TIMEOUT_MS, true
    };
    ip4_addr_t ip;
    ip4_addr_t mask;
    ip4_addr_t gw;
    uint32_t t0;
    uint32_t t_last_poll;
    uint32_t timeout;

    if (cfg == NULL) {
        cfg = &def;
    }
    if (s_inited) {
        return netif_is_link_up(&gnetif) ? 0 : 1;
    }

    if (enet_init(ENET) != status_success) {
        printf("[lwip] enet_init FAILED\r\n");
        return -2;
    }
    lwip_init();
    netif_config(&gnetif);
    enet_services(&gnetif);

    if (!ip4addr_aton((cfg->ip != NULL) ? cfg->ip : APP_LWIP_DEFAULT_IP, &ip) ||
        !ip4addr_aton((cfg->netmask != NULL) ? cfg->netmask : APP_LWIP_DEFAULT_NETMASK, &mask) ||
        !ip4addr_aton((cfg->gw != NULL) ? cfg->gw : APP_LWIP_DEFAULT_GW, &gw)) {
        printf("[lwip] bad static IP config\r\n");
        return -1;
    }
    printf("[lwip] cfg ip=%s mask=%s gw=%s\r\n", (cfg->ip != NULL) ? cfg->ip : APP_LWIP_DEFAULT_IP,
           (cfg->netmask != NULL) ? cfg->netmask : APP_LWIP_DEFAULT_NETMASK,
           (cfg->gw != NULL) ? cfg->gw : APP_LWIP_DEFAULT_GW);
    netif_set_addr(&gnetif, &ip, &mask, &gw);
    printf("[lwip] aton+set: ip=%08lX mask=%08lX gw=%08lX\r\n",
           (unsigned long) ip.addr, (unsigned long) mask.addr, (unsigned long) gw.addr);
    printf("[lwip] netif now: ip=%08lX mask=%08lX gw=%08lX hwlen=%u mac=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
           (unsigned long) netif_ip4_addr(&gnetif)->addr,
           (unsigned long) netif_ip4_netmask(&gnetif)->addr,
           (unsigned long) netif_ip4_gw(&gnetif)->addr, (unsigned) gnetif.hwaddr_len,
           gnetif.hwaddr[0], gnetif.hwaddr[1], gnetif.hwaddr[2],
           gnetif.hwaddr[3], gnetif.hwaddr[4], gnetif.hwaddr[5]);

    /* 1 ms 硬件定时器：lwIP 的 sys_now()/sys_check_timeouts 全靠它 */
    board_timer_create(LWIP_APP_TIMER_INTERVAL, sys_timer_callback);
    s_inited = true;

    if (cfg->verbose) {
        app_lwip_print();
    }

    /* 等 link up：每 10 ms 主动查一次 PHY（板级 2 s 定时器太慢，首次上电要快） */
    timeout = cfg->link_timeout_ms;
    t0 = sys_now();
    t_last_poll = t0;
    while (!netif_is_link_up(&gnetif)) {
        app_lwip_poll();
        if ((sys_now() - t_last_poll) >= 10U) {
            t_last_poll = sys_now();
            enet_self_adaptive_port_speed();
        }
        if ((timeout == 0U) || ((sys_now() - t0) >= timeout)) {
            if (cfg->verbose) {
                printf("[lwip] link DOWN after %lu ms (netif 已起，可继续轮询)\r\n",
                       (unsigned long) (sys_now() - t0));
            }
            return 1;
        }
        board_delay_ms(1);
    }
    if (cfg->verbose) {
        printf("[lwip] link UP after %lu ms\r\n", (unsigned long) (sys_now() - t0));
        app_lwip_print();
        printf("[lwip] ready: IP=%s port 任意 UDP 可直接收\r\n", ipaddr_ntoa(netif_ip_addr4(&gnetif)));
    }
    return 0;
}

struct netif *app_lwip_netif(void)
{
    return &gnetif;
}

uint32_t app_lwip_now_ms(void)
{
    return sys_now();
}

void app_lwip_poll(void)
{
    enet_common_handler(&gnetif);
}

bool app_lwip_link_up(void)
{
    return netif_is_link_up(&gnetif);
}

void app_lwip_link_poll(void)
{
    enet_self_adaptive_port_speed();
}

struct udp_pcb *app_udp_socket(const ip_addr_t *local, uint16_t port)
{
    struct udp_pcb *pcb = udp_new();
    if (pcb == NULL) {
        return NULL;
    }
    if (udp_bind(pcb, local, port) != ERR_OK) {
        udp_remove(pcb);
        return NULL;
    }
    return pcb;
}

bool app_lwip_parse_ip(const char *s, ip_addr_t *out)
{
    ip4_addr_t tmp;
    if ((s == NULL) || (out == NULL)) {
        return false;
    }
    if (!ip4addr_aton(s, &tmp)) {
        return false;
    }
    ip_addr_copy_from_ip4(*out, tmp);
    return true;
}