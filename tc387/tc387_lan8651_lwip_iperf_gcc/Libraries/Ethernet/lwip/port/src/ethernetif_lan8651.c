#include "ethernetif_lan8651.h"

#include <string.h>

#include "lan8651.h"
#include "lwip/etharp.h"
#include "lwip/ip4.h"
#include "lwip/pbuf.h"

#define ETH_MIN_FRAME_LEN 60U

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    lan8651_t *dev = (lan8651_t *)netif->state;
    uint16_t frame_len = 0U;
    uint16_t wire_len;
    struct pbuf *q;

    for (q = p; q != NULL; q = q->next)
    {
        if ((frame_len + q->len) > sizeof(dev->tx_frame))
        {
            return ERR_BUF;
        }
        memcpy(&dev->tx_frame[frame_len], q->payload, q->len);
        frame_len += (uint16_t)q->len;
    }

    wire_len = frame_len;
    if (wire_len < ETH_MIN_FRAME_LEN)
    {
        memset(&dev->tx_frame[wire_len], 0, ETH_MIN_FRAME_LEN - wire_len);
        wire_len = ETH_MIN_FRAME_LEN;
    }

    if (lan8651_transmit(dev, dev->tx_frame, wire_len) != kLan8651Status_Ok)
    {
        return ERR_IF;
    }

    return ERR_OK;
}

static struct pbuf *low_level_input(struct netif *netif)
{
    lan8651_t *dev = (lan8651_t *)netif->state;
    uint8_t *frame;
    uint16_t frame_len;
    struct pbuf *p;
    struct pbuf *q;
    uint16_t copied = 0U;

    if (lan8651_receive(dev, &frame, &frame_len) != kLan8651Status_Ok)
    {
        return NULL;
    }

    if (frame_len == 0U)
    {
        return NULL;
    }

    p = pbuf_alloc(PBUF_RAW, frame_len, PBUF_POOL);
    if (p == NULL)
    {
        return NULL;
    }

    for (q = p; q != NULL; q = q->next)
    {
        memcpy(q->payload, &frame[copied], q->len);
        copied += (uint16_t)q->len;
    }

    return p;
}

err_t lan8651_ethernetif_init(struct netif *netif)
{
    lan8651_t *dev = (lan8651_t *)netif->state;

    netif->name[0] = 't';
    netif->name[1] = '1';
    netif->output = etharp_output;
    netif->linkoutput = low_level_output;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, dev->mac_addr, ETH_HWADDR_LEN);

    return ERR_OK;
}

void lan8651_ethernetif_input(struct netif *netif, uint32_t budget)
{
    while (budget-- > 0U)
    {
        struct pbuf *p = low_level_input(netif);
        if (p == NULL)
        {
            break;
        }

        if (netif->input(p, netif) != ERR_OK)
        {
            pbuf_free(p);
        }
    }
}
