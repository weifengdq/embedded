#include "ethernetif_lan8651.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "lan8651.h"
#include "lwip/etharp.h"
#include "lwip/ip4.h"
#include "lwip/pbuf.h"

#define ETH_MIN_FRAME_LEN 60U

static uint32_t s_rx_log_count;
static uint32_t s_tx_log_count;

static void log_frame_summary(const char *tag, uint32_t index, const uint8_t *frame, uint16_t frame_len)
{
    uint16_t eth_type;

    if (frame_len < 14U) {
        printf("%s[%lu]: short=%u\n", tag, (unsigned long)index, frame_len);
        return;
    }

    eth_type = ((uint16_t)frame[12] << 8) | frame[13];
    printf("%s[%lu]: len=%u type=0x%04X\n",
        tag,
        (unsigned long)index,
        frame_len,
        eth_type);
    printf("%s[%lu]: dst=%02X:%02X:%02X:%02X:%02X:%02X src=%02X:%02X:%02X:%02X:%02X:%02X\n",
        tag,
        (unsigned long)index,
        frame[0], frame[1], frame[2], frame[3], frame[4], frame[5],
        frame[6], frame[7], frame[8], frame[9], frame[10], frame[11]);

    if (eth_type == 0x0806U && frame_len >= 42U) {
        uint16_t arp_opcode = ((uint16_t)frame[20] << 8) | frame[21];
        printf("%s[%lu]: arp op=%u sip=%u.%u.%u.%u tip=%u.%u.%u.%u sha=%02X:%02X:%02X:%02X:%02X:%02X tha=%02X:%02X:%02X:%02X:%02X:%02X\n",
            tag,
            (unsigned long)index,
            arp_opcode,
            frame[28], frame[29], frame[30], frame[31],
            frame[38], frame[39], frame[40], frame[41],
            frame[22], frame[23], frame[24], frame[25], frame[26], frame[27],
            frame[32], frame[33], frame[34], frame[35], frame[36], frame[37]);
    } else if (eth_type == 0x0800U && frame_len >= 34U) {
        uint8_t ihl = (uint8_t)((frame[14] & 0x0FU) * 4U);
        uint8_t proto = frame[23];
        printf("%s[%lu]: ip proto=%u sip=%u.%u.%u.%u dip=%u.%u.%u.%u ihl=%u\n",
            tag,
            (unsigned long)index,
            proto,
            frame[26], frame[27], frame[28], frame[29],
            frame[30], frame[31], frame[32], frame[33],
            ihl);
        if (proto == 1U && frame_len >= (uint16_t)(14U + ihl + 2U)) {
            printf("%s[%lu]: icmp type=%u code=%u\n",
                tag,
                (unsigned long)index,
                frame[14U + ihl],
                frame[14U + ihl + 1U]);
        }
    }
}

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    lan8651_t *dev = (lan8651_t *)netif->state;
    uint16_t frame_len = 0;
    uint16_t wire_len;
    struct pbuf *q;

    for (q = p; q != NULL; q = q->next) {
        if ((frame_len + q->len) > sizeof(dev->tx_frame)) {
            return ERR_BUF;
        }
        memcpy(&dev->tx_frame[frame_len], q->payload, q->len);
        frame_len += (uint16_t)q->len;
    }

    wire_len = frame_len;
    if (wire_len < ETH_MIN_FRAME_LEN) {
        memset(&dev->tx_frame[wire_len], 0, ETH_MIN_FRAME_LEN - wire_len);
        wire_len = ETH_MIN_FRAME_LEN;
    }

    if (s_tx_log_count < APP_LAN8651_FRAME_LOG_LIMIT) {
        log_frame_summary("tx_frame", s_tx_log_count, dev->tx_frame, wire_len);
        s_tx_log_count++;
    }

    if (lan8651_transmit(dev, dev->tx_frame, wire_len) != kLan8651Status_Ok) {
        printf("tx_error\n");
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
    uint16_t copied = 0;

    if (lan8651_receive(dev, &frame, &frame_len) != kLan8651Status_Ok) {
        return NULL;
    }

    if (frame_len == 0U) {
        return NULL;
    }

    if (s_rx_log_count < APP_LAN8651_FRAME_LOG_LIMIT) {
        log_frame_summary("rx_frame", s_rx_log_count, frame, frame_len);
        s_rx_log_count++;
    }

    p = pbuf_alloc(PBUF_RAW, frame_len, PBUF_POOL);
    if (p == NULL) {
        return NULL;
    }

    for (q = p; q != NULL; q = q->next) {
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
    netif->hostname = APP_PROJECT_NAME;
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
    while (budget-- > 0U) {
        struct pbuf *p = low_level_input(netif);
        if (p == NULL) {
            break;
        }

        if (netif->input(p, netif) != ERR_OK) {
            printf("netif_input_drop\n");
            pbuf_free(p);
        } else if (APP_LAN8651_ENABLE_RX_OK_LOG != 0U && s_rx_log_count <= APP_LAN8651_FRAME_LOG_LIMIT) {
            printf("netif_input_ok\n");
        }
    }
}