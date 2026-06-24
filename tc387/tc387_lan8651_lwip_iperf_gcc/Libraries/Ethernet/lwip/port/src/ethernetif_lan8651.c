#include "ethernetif_lan8651.h"

#include <string.h>

#include "Ifx_Lwip.h"
#include "lan8651.h"
#include "lwip/etharp.h"
#include "lwip/ip4.h"
#include "lwip/pbuf.h"

#define ETH_MIN_FRAME_LEN 60U

#define LAN8651_RX_LOG 0
#define LAN8651_TX_LOG 0

#if LAN8651_RX_LOG
static uint32_t s_rx_log_count;
#endif
#if LAN8651_TX_LOG
static uint32_t s_tx_log_count;
#endif

static void log_frame_summary(const char *tag, uint32_t index, const uint8_t *frame, uint16_t frame_len)
{
    uint16_t eth_type;

    if (frame_len < 14U)
    {
        Ifx_Lwip_printf("%s[%lu]: short=%u", tag, (unsigned long)index, (unsigned)frame_len);
        return;
    }

    eth_type = ((uint16_t)frame[12] << 8) | frame[13];
    Ifx_Lwip_printf("%s[%lu]: len=%u type=0x%04X dst=%02X:%02X:%02X:%02X:%02X:%02X src=%02X:%02X:%02X:%02X:%02X:%02X",
        tag,
        (unsigned long)index,
        (unsigned)frame_len,
        (unsigned)eth_type,
        frame[0], frame[1], frame[2], frame[3], frame[4], frame[5],
        frame[6], frame[7], frame[8], frame[9], frame[10], frame[11]);

    if ((eth_type == 0x0806U) && (frame_len >= 42U))
    {
        uint16_t arp_opcode = ((uint16_t)frame[20] << 8) | frame[21];
        Ifx_Lwip_printf("%s[%lu]: arp op=%u sip=%u.%u.%u.%u tip=%u.%u.%u.%u",
            tag,
            (unsigned long)index,
            (unsigned)arp_opcode,
            frame[28], frame[29], frame[30], frame[31],
            frame[38], frame[39], frame[40], frame[41]);
    }
    else if ((eth_type == 0x0800U) && (frame_len >= 34U))
    {
        uint8_t ihl = (uint8_t)((frame[14] & 0x0FU) * 4U);
        uint8_t proto = frame[23];
        Ifx_Lwip_printf("%s[%lu]: ip proto=%u sip=%u.%u.%u.%u dip=%u.%u.%u.%u ihl=%u",
            tag,
            (unsigned long)index,
            (unsigned)proto,
            frame[26], frame[27], frame[28], frame[29],
            frame[30], frame[31], frame[32], frame[33],
            (unsigned)ihl);
    }
}

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    lan8651_t *dev = (lan8651_t *)netif->state;
    uint16_t frame_len = 0U;
    uint16_t wire_len;
    struct pbuf *q;

    for (q = p; q != NULL; q = q->next)
    {
        uint16_t skip = (q == p) ? (uint16_t)ETH_PAD_SIZE : 0U;
        uint16_t copy_len = (uint16_t)q->len - skip;
        if ((frame_len + copy_len) > sizeof(dev->tx_frame))
        {
            return ERR_BUF;
        }
        if (copy_len > 0U)
        {
            memcpy(&dev->tx_frame[frame_len], (uint8_t *)q->payload + skip, copy_len);
            frame_len += copy_len;
        }
    }

    wire_len = frame_len;
    if (wire_len < ETH_MIN_FRAME_LEN)
    {
        memset(&dev->tx_frame[wire_len], 0, ETH_MIN_FRAME_LEN - wire_len);
        wire_len = ETH_MIN_FRAME_LEN;
    }

#if LAN8651_TX_LOG
    if (s_tx_log_count < 8U)
    {
        Ifx_Lwip_printf("%s[%lu]: raw=%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
            "tx_raw",
            (unsigned long)s_tx_log_count,
            dev->tx_frame[0], dev->tx_frame[1], dev->tx_frame[2], dev->tx_frame[3],
            dev->tx_frame[4], dev->tx_frame[5], dev->tx_frame[6], dev->tx_frame[7],
            dev->tx_frame[8], dev->tx_frame[9], dev->tx_frame[10], dev->tx_frame[11],
            dev->tx_frame[12], dev->tx_frame[13], dev->tx_frame[14], dev->tx_frame[15]);
        log_frame_summary("tx_frame", s_tx_log_count, dev->tx_frame, wire_len);
        s_tx_log_count++;
    }
#endif

    if (lan8651_transmit(dev, dev->tx_frame, wire_len) != kLan8651Status_Ok)
    {
        Ifx_Lwip_printf("tx_error len=%u", (unsigned)wire_len);
        return ERR_IF;
    }

    return ERR_OK;
}

static struct pbuf *low_level_input(struct netif *netif)
{
    lan8651_t *dev = (lan8651_t *)netif->state;
    uint8_t *frame;
    uint16_t frame_len;
    lan8651_status_t status;
    struct pbuf *p;
    struct pbuf *q;
    uint16_t copied = 0U;

    status = lan8651_receive(dev, &frame, &frame_len);
    if (status != kLan8651Status_Ok)
    {
        return NULL;
    }

    if (frame_len == 0U)
    {
        return NULL;
    }

    p = pbuf_alloc(PBUF_RAW, (uint16_t)((uint16_t)ETH_PAD_SIZE + frame_len), PBUF_POOL);
    if (p == NULL)
    {
        return NULL;
    }

    memset(p->payload, 0, ETH_PAD_SIZE);
    for (q = p; q != NULL; q = q->next)
    {
        uint16_t skip = (q == p) ? (uint16_t)ETH_PAD_SIZE : 0U;
        uint16_t copy_len = (uint16_t)q->len - skip;
        if (copy_len > 0U)
        {
            memcpy((uint8_t *)q->payload + skip, &frame[copied], copy_len);
            copied += copy_len;
        }
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
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;
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
            Ifx_Lwip_printf("netif_input_drop");
            pbuf_free(p);
        }
    }
}
