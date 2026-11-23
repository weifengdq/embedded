/*
 * Copyright (c) 2026, hpm5e31_1 应用层书稿项目
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * phy_glue.c - 把 YT8011AN 驱动接到 HPM SDK 的 lwIP 移植层（PTP 样例共用）
 *
 * 背景:
 *   SDK samples/lwip/common/single/common.c 的 enet_init() 会调用 board_init_enet_phy()；
 *   board.c 中该函数在 CONFIG_ENET_PHY=1 时才编译，且只支持 rtl8211/jl1111。
 *   本板 PHY 是 YT8011AN，因此 ptp 样例 **不打开 CONFIG_ENET_PHY**，由本文件提供同名板级钩子，
 *   直接调用 components/enet_phy_yt8011 的驱动（不改 board.c / boards/CMakeLists.txt）。
 *
 *   同时替换 SDK samples/lwip/common/enet_phy_adaptive_lwip.c 的
 *   lwip_enet_phy_adaptive_poll()：链路状态查询走 YT8011 驱动。
 *
 * 与 samples/enet/enet_lwip_iperf/src/phy_glue.c 同源，区别：本文件给 PTP 三个例程共用。
 */
#include <string.h>
#include "board.h"
#include "hpm_yt8011.h"
#include "hpm_enet_phy.h"
#include "enet_phy_adaptive_lwip.h"
#include "lwip/netif.h"

#define NET2_PHY_ADDR   BOARD_NET2_PHY_ADDR

void board_get_enet_phy_status(uint8_t idx, void *status)
{
    enet_phy_status_t *st = (enet_phy_status_t *) status;
    yt8011_status_t ys;

    if ((idx != 0U) || (st == NULL)) {
        return;
    }
    memset(&ys, 0, sizeof(ys));
    if (yt8011_get_status(BOARD_NET2_ENET_BASE, NET2_PHY_ADDR, &ys) != status_success) {
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
    if (!yt8011_check_id(ptr, NET2_PHY_ADDR)) {
        printf("[phy] YT8011AN ID check failed (expect ID1=0x4F51)\r\n");
        return status_fail;
    }
    yt8011_basic_mode_default_config(ptr, &cfg);
    cfg.speed  = yt8011_speed_1000m;   /* 1000BASE-T1 + Slave，与板上 strap 一致 */
    cfg.master = false;

    if (yt8011_basic_mode_init(ptr, NET2_PHY_ADDR, &cfg) != status_success) {
        printf("[phy] YT8011AN basic mode init failed\r\n");
        return status_fail;
    }
    if (yt8011_wait_link_up(ptr, NET2_PHY_ADDR, 2000U, &st) == status_success) {
        printf("[phy] YT8011AN link up: %s %s\r\n", yt8011_speed_str(st.speed),
               st.master ? "MASTER" : "SLAVE");
    } else {
        printf("[phy] YT8011AN link not up yet (polled by adaptive timer)\r\n");
    }
    return status_success;
}

/* 替换 SDK 的 enet_phy_adaptive_lwip.c（common.c 的 enet_self_adaptive_port_speed 会调用） */
void lwip_enet_phy_adaptive_poll(const lwip_enet_phy_adaptive_binding_t *binding)
{
    enet_phy_status_t status = {0};

    if (binding == NULL) {
        return;
    }
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
