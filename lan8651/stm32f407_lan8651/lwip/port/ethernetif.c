/**
 * @file  ethernetif.c
 * @brief lwIP network interface driver for LAN8651 10BASE-T1S
 *        Bare-metal (NO_SYS=1)
 */
#include "ethernetif.h"
#include "lan8651.h"
#include "app_config.h"

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#include <string.h>

/* MTU for 10BASE-T1S */
#define NETIF_MTU   1500
#define ETH_MIN_FRAME_LEN 60
#define ETHERNETIF_INPUT_MAX_FRAMES 32

extern int debug_printf(const char *fmt, ...);
#define ETHDBG(...) debug_printf(__VA_ARGS__)

#define ETHDBG_IF(enabled, ...) do { if ((enabled) != 0) ETHDBG(__VA_ARGS__); } while (0)

#define ETHTYPE_IPV4 0x0800U
#define IP_PROTO_TCP 6U
#define TCP_FLAG_FIN 0x01U
#define TCP_FLAG_SYN 0x02U
#define TCP_FLAG_RST 0x04U
#define TCP_FLAG_ACK 0x10U

/* Private receive buffer */
static uint8_t rx_frame_buf[LAN8651_MAX_FRAME_SIZE];
static uint8_t tx_frame_buf[LAN8651_MAX_FRAME_SIZE];
static uint32_t tx_trace_count;
static uint32_t rx_trace_count;
volatile ethernetif_tcp_diag_t g_ethernetif_tcp_diag __attribute__((used));

static uint16_t read_be16(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static void sample_tx_mac_status(lan8651_t *dev)
{
    uint32_t mac_tsr = 0;

    g_ethernetif_tcp_diag.tx_status_samples++;
    if (LAN8651_ReadReg(dev, LAN8651_MMS_MAC, MAC_TSR_REG, &mac_tsr) != HAL_OK) {
        g_ethernetif_tcp_diag.tx_status_read_failures++;
        return;
    }

    g_ethernetif_tcp_diag.tx_last_tsr = mac_tsr;
    if ((mac_tsr & MAC_TSR_COL) != 0U) {
        g_ethernetif_tcp_diag.tx_tsr_col_samples++;
    }
    if ((mac_tsr & MAC_TSR_TFC) != 0U) {
        g_ethernetif_tcp_diag.tx_tsr_tfc_samples++;
    }
    if ((mac_tsr & MAC_TSR_UND) != 0U) {
        g_ethernetif_tcp_diag.tx_tsr_und_samples++;
    }
}

static void sample_rx_mac_status(lan8651_t *dev)
{
    uint32_t mac_rsr = 0;

    g_ethernetif_tcp_diag.rx_status_samples++;
    if (LAN8651_ReadReg(dev, LAN8651_MMS_MAC, MAC_RSR_REG, &mac_rsr) != HAL_OK) {
        g_ethernetif_tcp_diag.rx_status_read_failures++;
        return;
    }

    g_ethernetif_tcp_diag.rx_last_rsr = mac_rsr;
    if ((mac_rsr & MAC_RSR_BNA) != 0U) {
        g_ethernetif_tcp_diag.rx_rsr_bna_samples++;
    }
}

void ethernetif_reset_tcp_diag(uint16_t tracked_port)
{
    uint32_t next_session_seq = g_ethernetif_tcp_diag.session_seq + 1U;
    memset((void *)&g_ethernetif_tcp_diag, 0, sizeof(g_ethernetif_tcp_diag));
    g_ethernetif_tcp_diag.magic = 0x45544854u;
    g_ethernetif_tcp_diag.session_seq = next_session_seq;
    g_ethernetif_tcp_diag.tracked_port = tracked_port;
}

static int trace_tcp_session_frame(const uint8_t *frame, uint16_t len)
{
    uint16_t eth_type;
    uint16_t ip_total_len;
    uint16_t dst_port;
    uint16_t ip_header_len;
    uint16_t tcp_header_len;
    uint16_t tcp_payload_len;
    uint32_t seq;

    if (len < 14U + 20U + 20U) {
        return 0;
    }

    eth_type = read_be16(&frame[12]);
    if (eth_type != ETHTYPE_IPV4) {
        return 0;
    }

    ip_header_len = (uint16_t)((frame[14] & 0x0FU) * 4U);
    if (ip_header_len < 20U || len < (uint16_t)(14U + ip_header_len + 20U)) {
        return 0;
    }

    if (frame[23] != IP_PROTO_TCP) {
        return 0;
    }

    ip_total_len = read_be16(&frame[16]);
    if (ip_total_len < (uint16_t)(ip_header_len + 20U)) {
        return 0;
    }

    dst_port = read_be16(&frame[14U + ip_header_len + 2U]);
    if (dst_port != (uint16_t)g_ethernetif_tcp_diag.tracked_port) {
        return 0;
    }

    tcp_header_len = (uint16_t)(((frame[14U + ip_header_len + 12U] >> 4) & 0x0FU) * 4U);
    if (tcp_header_len < 20U || ip_total_len < (uint16_t)(ip_header_len + tcp_header_len)) {
        return 0;
    }

    tcp_payload_len = (uint16_t)(ip_total_len - ip_header_len - tcp_header_len);
    if (tcp_payload_len == 0U) {
        return 0;
    }

    seq = read_be32(&frame[14U + ip_header_len + 4U]);

    g_ethernetif_tcp_diag.magic = 0x45544854u;
    g_ethernetif_tcp_diag.tcp_data_frames++;
    g_ethernetif_tcp_diag.tcp_payload_bytes += tcp_payload_len;
    g_ethernetif_tcp_diag.last_seq = seq;
    g_ethernetif_tcp_diag.last_payload_len = tcp_payload_len;

    if (g_ethernetif_tcp_diag.highest_seq_end != 0U) {
        if (seq < g_ethernetif_tcp_diag.highest_seq_end) {
            g_ethernetif_tcp_diag.retransmit_frames++;
        } else if (seq > g_ethernetif_tcp_diag.highest_seq_end) {
            g_ethernetif_tcp_diag.gap_frames++;
        }
    }

    if ((seq + tcp_payload_len) > g_ethernetif_tcp_diag.highest_seq_end) {
        g_ethernetif_tcp_diag.highest_seq_end = seq + tcp_payload_len;
    }

    return 1;
}

static int trace_tcp_session_ack(const uint8_t *frame, uint16_t len)
{
    uint16_t eth_type;
    uint16_t ip_total_len;
    uint16_t src_port;
    uint16_t ip_header_len;
    uint16_t tcp_header_len;
    uint16_t tcp_payload_len;
    uint16_t ack_window;
    uint8_t tcp_flags;
    uint32_t ack;

    if (len < 14U + 20U + 20U) {
        return 0;
    }

    eth_type = read_be16(&frame[12]);
    if (eth_type != ETHTYPE_IPV4) {
        return 0;
    }

    ip_header_len = (uint16_t)((frame[14] & 0x0FU) * 4U);
    if (ip_header_len < 20U || len < (uint16_t)(14U + ip_header_len + 20U)) {
        return 0;
    }

    if (frame[23] != IP_PROTO_TCP) {
        return 0;
    }

    ip_total_len = read_be16(&frame[16]);
    if (ip_total_len < (uint16_t)(ip_header_len + 20U)) {
        return 0;
    }

    src_port = read_be16(&frame[14U + ip_header_len]);
    if (src_port != (uint16_t)g_ethernetif_tcp_diag.tracked_port) {
        return 0;
    }

    tcp_header_len = (uint16_t)(((frame[14U + ip_header_len + 12U] >> 4) & 0x0FU) * 4U);
    if (tcp_header_len < 20U || ip_total_len < (uint16_t)(ip_header_len + tcp_header_len)) {
        return 0;
    }

    tcp_payload_len = (uint16_t)(ip_total_len - ip_header_len - tcp_header_len);
    tcp_flags = frame[14U + ip_header_len + 13U];
    if ((tcp_flags & TCP_FLAG_ACK) == 0U) {
        return 0;
    }

    ack = read_be32(&frame[14U + ip_header_len + 8U]);
    ack_window = read_be16(&frame[14U + ip_header_len + 14U]);

    g_ethernetif_tcp_diag.tx_ack_frames++;
    if (tcp_payload_len == 0U && (tcp_flags & (TCP_FLAG_SYN | TCP_FLAG_FIN | TCP_FLAG_RST)) == 0U) {
        g_ethernetif_tcp_diag.tx_pure_ack_frames++;
        if (ack == g_ethernetif_tcp_diag.last_ack && ack_window == g_ethernetif_tcp_diag.last_ack_window) {
            g_ethernetif_tcp_diag.tx_dup_ack_frames++;
        }
    }

    g_ethernetif_tcp_diag.last_ack = ack;
    g_ethernetif_tcp_diag.last_ack_window = ack_window;
    if (ack_window > g_ethernetif_tcp_diag.max_ack_window) {
        g_ethernetif_tcp_diag.max_ack_window = ack_window;
    }

    return 1;
}

static void trace_arp_frame(const char *dir, const uint8_t *frame, uint16_t len)
{
    if (len < 42) {
        return;
    }

    uint16_t oper = ((uint16_t)frame[20] << 8) | frame[21];
    ETHDBG_IF(ETHERNETIF_LOG_FRAMES,
              "%s ARP: dst=%02X:%02X:%02X:%02X:%02X:%02X src=%02X:%02X:%02X:%02X:%02X:%02X op=%u sip=%u.%u.%u.%u tip=%u.%u.%u.%u\r\n",
              dir,
              frame[0], frame[1], frame[2], frame[3], frame[4], frame[5],
              frame[6], frame[7], frame[8], frame[9], frame[10], frame[11],
              oper,
              frame[28], frame[29], frame[30], frame[31],
              frame[38], frame[39], frame[40], frame[41]);
}

/* ------------------------------------------------------------------ */
/*  Low-level output — send an Ethernet frame via LAN8651             */
/* ------------------------------------------------------------------ */
static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    lan8651_t *dev = (lan8651_t *)netif->state;

    /* Flatten pbuf chain into a contiguous buffer on the stack */
    uint16_t len = 0;

    for (struct pbuf *q = p; q != NULL; q = q->next) {
        if (len + q->len > sizeof(tx_frame_buf)) return ERR_BUF;
        memcpy(tx_frame_buf + len, q->payload, q->len);
        len += q->len;
    }

    uint16_t wire_len = len;
    if (wire_len < ETH_MIN_FRAME_LEN) {
        memset(tx_frame_buf + wire_len, 0, ETH_MIN_FRAME_LEN - wire_len);
        wire_len = ETH_MIN_FRAME_LEN;
    }

    if (ETHERNETIF_LOG_FRAMES && tx_trace_count < 8 && len >= 14) {
        uint16_t eth_type = ((uint16_t)tx_frame_buf[12] << 8) | tx_frame_buf[13];
        ETHDBG("lwIP TX[%lu]: len=%u wire=%u type=0x%04X\r\n",
               (unsigned long)tx_trace_count,
               len,
               wire_len,
               eth_type);
        if (eth_type == ETHTYPE_ARP) {
            trace_arp_frame("TX", tx_frame_buf, len);
        }
        tx_trace_count++;
    }

    int tracked_ack = trace_tcp_session_ack(tx_frame_buf, len);

    if (LAN8651_Transmit(dev, tx_frame_buf, wire_len) != HAL_OK) {
        ETHDBG("lwIP TX failed: len=%u wire=%u\r\n", len, wire_len);
        return ERR_IF;
    }

    if (tracked_ack && ((g_ethernetif_tcp_diag.tx_ack_frames & 0x0FU) == 0U)) {
        sample_tx_mac_status(dev);
    }

    return ERR_OK;
}

/* ------------------------------------------------------------------ */
/*  Low-level init                                                    */
/* ------------------------------------------------------------------ */
static void low_level_init(struct netif *netif)
{
    lan8651_t *dev = (lan8651_t *)netif->state;

    /* Set MAC hardware address */
    netif->hwaddr_len = 6;
    memcpy(netif->hwaddr, dev->mac_addr, 6);

    /* Set MTU */
    netif->mtu = NETIF_MTU;

    /* Accept broadcast, ARP, link up */
    netif->flags = NETIF_FLAG_BROADCAST |
                   NETIF_FLAG_ETHARP |
                   NETIF_FLAG_ETHERNET |
                   NETIF_FLAG_LINK_UP;

#if LWIP_IGMP
    netif->flags |= NETIF_FLAG_IGMP;
#endif
}

/* ------------------------------------------------------------------ */
/*  ethernetif_init — called by netif_add()                           */
/* ------------------------------------------------------------------ */
err_t ethernetif_init(struct netif *netif)
{
    /* Set interface name */
    netif->name[0] = 'e';
    netif->name[1] = 'n';

    netif->output     = etharp_output;
    netif->linkoutput = low_level_output;

#if LWIP_NETIF_HOSTNAME
    netif->hostname = "stm32f407";
#endif

    low_level_init(netif);
    return ERR_OK;
}

/* ------------------------------------------------------------------ */
/*  ethernetif_input — poll for received frames (call from main loop) */
/* ------------------------------------------------------------------ */
void ethernetif_input(struct netif *netif)
{
    lan8651_t *dev = (lan8651_t *)netif->state;

    for (int attempts = 0; attempts < ETHERNETIF_INPUT_MAX_FRAMES; attempts++) {
#if ETHERNETIF_USE_RBA_GATE
        if (!dev->irq_pending) {
            if (LAN8651_GetRxBlocksAvailable(dev) == 0U) {
                break;
            }
        }
#endif

        uint16_t len = 0;
        if (LAN8651_Receive(dev, rx_frame_buf, &len) != HAL_OK || len == 0) {
            break;
        }

        dev->irq_pending = 0;
        g_ethernetif_tcp_diag.rx_input_frames++;

        if (ETHERNETIF_LOG_FRAMES && rx_trace_count < 8 && len >= 14) {
            uint16_t eth_type = ((uint16_t)rx_frame_buf[12] << 8) | rx_frame_buf[13];
            ETHDBG("lwIP RX[%lu]: len=%u type=0x%04X\r\n",
                   (unsigned long)rx_trace_count,
                   len,
                   eth_type);
            if (eth_type == ETHTYPE_ARP) {
                trace_arp_frame("RX", rx_frame_buf, len);
            }
            rx_trace_count++;
        }

        int tracked_frame = trace_tcp_session_frame(rx_frame_buf, len);
        if (tracked_frame && ((g_ethernetif_tcp_diag.tcp_data_frames & 0x0FU) == 0U)) {
            sample_rx_mac_status(dev);
        }

        /* Allocate a pbuf chain */
        struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
        if (p == NULL) {
            g_ethernetif_tcp_diag.rx_pbuf_alloc_failures++;
            break;
        }

        /* Copy frame data into pbuf */
        if (pbuf_take(p, rx_frame_buf, len) != ERR_OK) {
            g_ethernetif_tcp_diag.rx_pbuf_take_errors++;
            pbuf_free(p);
            break;
        }

        /* Feed into lwIP */
        err_t input_err = netif->input(p, netif);
        if (input_err != ERR_OK) {
            g_ethernetif_tcp_diag.rx_input_errors++;
            g_ethernetif_tcp_diag.last_input_err = (uint32_t)(uint8_t)input_err;
            pbuf_free(p);
        }
    }
}
