/*
 * oa_tc6_netif.c - lwIP netif glue for the LAN8651 (10BASE-T1S MAC-PHY over TC6)
 *
 * The data path is copy based on purpose: at 10 Mbit/s the SPI/TC6 side can
 * move ~9 Mbit/s, so a memcpy per frame is cheap compared with the 68-byte
 * chunk bookkeeping the TC6 driver already does, and it keeps lwIP pbufs out
 * of the SPI transaction lifetime.
 *
 * Ported for HPM5E31 + HPM SDK v1.13.0 bare-metal lwIP (NO_SYS=1).
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <string.h>
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/err.h"
#include "netif/etharp.h"
#include "oa_tc6.h"
#include "oa_tc6_netif.h"
#include "bsp.h"

#define LINK_POLL_MS 2000U

static oa_tc6_t *s_tc6;
static const uint8_t *s_mac;
static uint8_t s_tx_buf[OA_TC6_FRAME_MAX];
static uint8_t s_rx_buf[OA_TC6_FRAME_MAX];
static oa_tc6_netif_stats_t s_stats;
static bool s_link_up;
static uint32_t s_last_rx_service_ms;

void oa_tc6_netif_attach(oa_tc6_t *tc6, const uint8_t mac[6])
{
    s_tc6 = tc6;
    s_mac = mac;
    memset(&s_stats, 0, sizeof(s_stats));
    s_link_up = false;
}

const oa_tc6_netif_stats_t *oa_tc6_netif_stats(void)
{
    return &s_stats;
}

static err_t oa_tc6_netif_linkoutput(struct netif *netif, struct pbuf *p)
{
    uint16_t len = p->tot_len;

    LWIP_UNUSED_ARG(netif);
    if (s_tc6 == NULL) {
        return ERR_IF;
    }
    /* The LAN8651 MAC adds the FCS; lwIP must hand over the frame without it. */
    if ((len < 14U) || (len > 1514U)) {
        s_stats.tx_fail++;
        return ERR_VAL;
    }
    if (p->next != NULL) {
        (void) pbuf_copy_partial(p, s_tx_buf, len, 0);
    } else {
        memcpy(s_tx_buf, p->payload, len);
    }
    if (oa_tc6_send_frame(s_tc6, s_tx_buf, len) != 0) {
        s_stats.tx_fail++;
        return ERR_IF;
    }
    s_stats.tx_frames++;
    return ERR_OK;
}

err_t oa_tc6_netif_init(struct netif *netif)
{
    if ((netif == NULL) || (s_mac == NULL)) {
        return ERR_ARG;
    }
    netif->name[0] = 't';
    netif->name[1] = '1';
    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, s_mac, ETH_HWADDR_LEN);
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
#if LWIP_IGMP
    netif->flags |= NETIF_FLAG_IGMP;
#endif
    netif->output = etharp_output;
    netif->linkoutput = oa_tc6_netif_linkoutput;
    return ERR_OK;
}

void oa_tc6_netif_poll(struct netif *netif)
{
    uint16_t len;

    if (s_tc6 == NULL) {
        return;
    }
    /* Every TC6 data transfer returns the pending RX-chunk count in its footer.
     * For pure receive traffic there is no TX event to refresh it, so issue one
     * empty transfer at least every 1 ms (the MAC-PHY buffers ~31 chunks = 2 KB);
     * once the count is non-zero, drain it as fast as the main loop runs. */
    {
        uint32_t now = bsp_uptime_ms();
        if ((now != s_last_rx_service_ms) || (s_tc6->rx_chunks_available > 0U)) {
            s_last_rx_service_ms = now;
            (void) oa_tc6_poll_rx(s_tc6);
        }
    }
    while ((len = oa_tc6_rx_pop(s_tc6, s_rx_buf, sizeof(s_rx_buf))) > 0U) {
        struct pbuf *p;

        /* LAN8651 hands the received frame over with its 4-byte FCS. */
        if (len <= 4U) {
            s_stats.rx_dropped++;
            continue;
        }
        len = (uint16_t) (len - 4U);
        s_stats.rx_fcs_stripped++;
        if (len < 14U) {
            s_stats.rx_dropped++;
            continue;
        }
        p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
        if (p == NULL) {
            s_stats.rx_dropped++;
            continue;
        }
        if (pbuf_take(p, s_rx_buf, len) != ERR_OK) {
            pbuf_free(p);
            s_stats.rx_dropped++;
            continue;
        }
        if (netif->input(p, netif) != ERR_OK) {
            pbuf_free(p);
            s_stats.rx_dropped++;
            continue;
        }
        s_stats.rx_frames++;
    }
}

bool oa_tc6_netif_update_link(struct netif *netif)
{
    uint16_t bmsr1 = 0, bmsr2 = 0;
    bool up = false;

    if ((s_tc6 == NULL) || (netif == NULL)) {
        return false;
    }
    /* BMSR.LINK is latched: read twice and AND the results (data sheet §4.1.2). */
    if ((oa_tc6_c22_read(s_tc6, 0x01, &bmsr1) == 0) &&
        (oa_tc6_c22_read(s_tc6, 0x01, &bmsr2) == 0)) {
        up = (((bmsr1 & bmsr2) & 0x0004U) != 0U);
    }
    if (up != s_link_up) {
        s_link_up = up;
        s_stats.link_changes++;
        if (up) {
            netif_set_link_up(netif);
        } else {
            netif_set_link_down(netif);
        }
    }
    return up;
}
