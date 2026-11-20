/**
 * @file  s2500_netif.c
 * @brief S2500 + lwIP (NO_SYS) 以太网接口实现
 *
 * 说明:
 *  - MAC 硬件自动追加 FCS (MCONTROL0.FCSA=1), 驱动在接收时校验并剥离 FCS
 *  - 10BASE-T1S 半双工, MTU 1500
 *  - 接收在主循环轮询 (s2500_netif_poll), 也可由 IRQ_N 触发
 */
#include "s2500_netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/def.h"
#include "netif/ethernet.h"
#include <string.h>

typedef struct {
    s2500_dev_t *dev;
    uint8_t rxpkt[S2500_MAX_FRAME];
} s2500_netif_priv_t;

static s2500_netif_priv_t s_priv[2];
static struct netif s_netif[2];
static uint8_t s_priv_count;

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    s2500_netif_priv_t *priv = (s2500_netif_priv_t *)netif->state;
    uint8_t frame[S2500_MAX_FRAME];
    uint16_t len = 0;
    struct pbuf *q;

    if (p->tot_len > (S2500_MAX_FRAME - 4)) {
        return ERR_BUF;
    }
    for (q = p; q != NULL; q = q->next) {
        if (len + q->len > sizeof(frame)) {
            return ERR_BUF;
        }
        memcpy(&frame[len], q->payload, q->len);
        len = (uint16_t)(len + q->len);
    }
    if (s2500_send(priv->dev, frame, len, 0) != S2500_OK) {
        return ERR_IF;
    }
    LINK_STATS_INC(link.xmit);
    return ERR_OK;
}

static err_t s2500_netif_init_cb(struct netif *netif)
{
    s2500_netif_priv_t *priv = (s2500_netif_priv_t *)netif->state;

    netif->name[0] = 's';
    netif->name[1] = '0';
    netif->mtu = 1500;
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, priv->dev->mac, 6);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
                   NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;
    netif->output = etharp_output;
    netif->linkoutput = low_level_output;
    return ERR_OK;
}

struct netif *s2500_netif_add(s2500_dev_t *dev, const ip4_addr_t *ip,
                              const ip4_addr_t *mask, const ip4_addr_t *gw)
{
    struct netif *n;
    s2500_netif_priv_t *priv;

    if (!dev || s_priv_count >= 2) {
        return NULL;
    }
    priv = &s_priv[s_priv_count++];
    priv->dev = dev;

    n = netif_add(&s_netif[s_priv_count - 1], ip, mask, gw, priv,
                  s2500_netif_init_cb, ethernet_input);
    if (n == NULL) {
        return NULL;
    }
    netif_set_default(n);
    netif_set_up(n);
    netif_set_link_up(n);
    return n;
}

s2500_dev_t *s2500_netif_dev(struct netif *netif)
{
    s2500_netif_priv_t *priv = (s2500_netif_priv_t *)netif->state;
    return priv ? priv->dev : NULL;
}

void s2500_netif_poll(struct netif *netif)
{
    s2500_netif_priv_t *priv;
    /* 一次进入最多取 S2500_NETIF_RX_BUDGET 帧: 批量收包可以减少主循环
     * 往返次数, 提高 64B 小包连续收发时的效率 (10BASE-T1S 半双工, 突发越小
     * 越容易被 PLCA 轮次切碎)。平台可自行覆盖该宏。 */
#ifndef S2500_NETIF_RX_BUDGET
#define S2500_NETIF_RX_BUDGET 16
#endif
    int budget = S2500_NETIF_RX_BUDGET;

    if (!netif) {
        return;
    }
    priv = (s2500_netif_priv_t *)netif->state;
    while (budget-- > 0) {
        uint16_t len = 0;
        struct pbuf *p;
        if (s2500_recv(priv->dev, priv->rxpkt, &len, NULL) != S2500_OK) {
            break;
        }
        if (len < 14) {
            continue;
        }
        p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
        if (p == NULL) {
            LINK_STATS_INC(link.memerr);
            LINK_STATS_INC(link.drop);
            continue;
        }
#if ETH_PAD_SIZE
        pbuf_header(p, -ETH_PAD_SIZE);
#endif
        if (pbuf_take(p, priv->rxpkt, len) != ERR_OK) {
            pbuf_free(p);
            continue;
        }
        if (netif->input(p, netif) != ERR_OK) {
            pbuf_free(p);
            LINK_STATS_INC(link.drop);
        }
        LINK_STATS_INC(link.recv);
    }
}
