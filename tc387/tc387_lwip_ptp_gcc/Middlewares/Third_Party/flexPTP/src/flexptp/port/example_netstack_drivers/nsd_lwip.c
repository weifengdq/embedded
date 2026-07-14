#include "../../network_stack_driver.h"

#include "../../ptp_defs.h"
#include "../../task_ptp.h"

#include "lwip/err.h"
#include "lwip/igmp.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/udp.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"

#include <string.h>

/* ── PTP diagnostics counters (readable from outside) ── */
volatile uint32_t g_ptp_rx_hook_called;      /* times hook_unknown_ethertype was called for any ethertype */
volatile uint32_t g_ptp_rx_ethertype_match;  /* times ethertype matched 0x88F7 */
volatile uint32_t g_ptp_rx_mac_match;        /* times dest MAC matched PTP multicast */
volatile uint32_t g_ptp_rx_enqueued;         /* times ptp_receive_enqueue was called */
volatile uint32_t g_ptp_tx_attempts;         /* times ptp_nsd_transmit_msg was called */
volatile uint32_t g_ptp_tx_sent;             /* times ethernet_output returned OK */
volatile uint32_t g_ptp_operating;           /* cached is_flexPTP_operating() snapshot */

// initialize connection blocks to invalid states
static struct udp_pcb *PTP_L4_EVENT = NULL;
static struct udp_pcb *PTP_L4_GENERAL = NULL;

// store current settings
static PtpTransportType TP = -1;
static PtpDelayMechanism DM = -1;

static void ptp_transmit_cb(uint32_t ts_s, uint32_t ts_ns, void * tag);
static void ptp_receive_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);

void ptp_nsd_igmp_join_leave(bool join) {
#ifdef FLEXPTP_LWIP_BIND_ANY_FOR_MULTICAST
    LWIP_UNUSED_ARG(join);
    return;
#else
    // only join IGMP if Transport Type is IP
    if (TP == PTP_TP_IPv4) {
        err_t (*igmp_fn)(const ip_addr_t *, const ip_addr_t *) = join ? igmp_joingroup : igmp_leavegroup; // join or leave

        if (DM == PTP_DM_E2E) {
            igmp_fn(&netif_default->ip_addr, &PTP_IGMP_PRIMARY); // join E2E DM message group
        } else if (DM == PTP_DM_P2P) {
            igmp_fn(&netif_default->ip_addr, &PTP_IGMP_PEER_DELAY); // join P2P DM message group
        }
    }
#endif
}

void ptp_nsd_init(PtpTransportType tp, PtpDelayMechanism dm) {
    // lock LWIP core
    LOCK_TCPIP_CORE();

    // leave current IGMP group if applicable
    ptp_nsd_igmp_join_leave(false);

    // first, close all open connection blocks (zero CBDs won't cause trouble)
    if (PTP_L4_EVENT != NULL) {
        udp_disconnect(PTP_L4_EVENT);
        udp_remove(PTP_L4_EVENT);
        PTP_L4_EVENT = NULL;
    }
    if (PTP_L4_GENERAL != NULL) {
        udp_disconnect(PTP_L4_GENERAL);
        udp_remove(PTP_L4_GENERAL);
        PTP_L4_GENERAL = NULL;
    }

    // calling either parameter with -1 just closes connections
    if ((tp == -1) || (dm == -1)) {
        // message transmission and reception is turned off
        TP = -1;
        DM = -1;
        return;
    }

    // open only the necessary ones
    if (tp == PTP_TP_IPv4) {
        // NO_SYS=1 bare-metal: UDP pcbs corrupt lwIP state.
        // PTP over Ethernet (IEEE 1588 Annex F) uses the same ethertype 0x88F7
        // and is already handled by hook_unknown_ethertype + ethernet_output.
        // Route all IPv4 PTP through the L2 path instead.
        MSG("ptp_nsd_init: IPv4→L2 fallback (NO_SYS=1 bare-metal)\r\n");
        tp = PTP_TP_802_3;
    }

    // store configuration
    TP = tp;
    DM = dm;

    // join new IGMP group
    ptp_nsd_igmp_join_leave(true);

    // unlock LWIP core
    UNLOCK_TCPIP_CORE();
}

static void ptp_receive_cb(void *pArg, struct udp_pcb *pPCB, struct pbuf *pP, const ip_addr_t *pAddr, uint16_t port) {
    // put msg into the queue
    ptp_receive_enqueue(pP->payload, pP->len, pP->time_s, pP->time_ns, PTP_TP_IPv4);

    // release pbuf resources
    pbuf_free(pP);
}

static void ptp_transmit_cb(uint32_t ts_s, uint32_t ts_ns, void * tag) {
    ptp_transmit_timestamp_cb((uint32_t) tag, ts_s, ts_ns);
}

void ptp_nsd_transmit_msg(RawPtpMessage *pMsg, uint32_t uid) {
    g_ptp_tx_attempts++;
    if (pMsg == NULL) {
        MSG("NULL!!!\n");
        return;
    }

    PtpMessageClass mc = pMsg->tx_mc;

    // allocate buffer
    struct pbuf *p = NULL;
    p = pbuf_alloc((TP == PTP_TP_IPv4) ? PBUF_TRANSPORT : PBUF_LINK, pMsg->size, PBUF_RAM);

    // fill buffer
    memcpy(p->payload, pMsg->data, pMsg->size);

    // set transmit callback
    p->tag = (void *)uid;
    p->tx_cb = ptp_transmit_cb;

    // lock LWIP core
    LOCK_TCPIP_CORE();

    // narrow down by transport type
    if (TP == PTP_TP_IPv4) {
        struct udp_pcb *conn = (mc == PTP_MC_EVENT) ? PTP_L4_EVENT : PTP_L4_GENERAL;    // select connection by message type
        if (conn == NULL) {
            MSG("ptp_nsd_transmit: no UDP PCB (conn=NULL)\r\n");
            UNLOCK_TCPIP_CORE();
            pbuf_free(p);
            return;
        }
        uint16_t port = (mc == PTP_MC_EVENT) ? PTP_PORT_EVENT : PTP_PORT_GENERAL;       // select port by message class
        ip_addr_t ipaddr = (DM == PTP_DM_E2E) ? PTP_IGMP_PRIMARY : PTP_IGMP_PEER_DELAY; // select destination IP-address by delmech.
        udp_sendto(conn, p, &ipaddr, port);                                             // send packet
    } else if (TP == PTP_TP_802_3) {
        const uint8_t *ethaddr = (DM == PTP_DM_E2E) ? PTP_ETHERNET_PRIMARY : PTP_ETHERNET_PEER_DELAY; // select destination address by delmech.
        ethernet_output(netif_default, p, (struct eth_addr *)netif_default->hwaddr, (struct eth_addr *)ethaddr, ETHERTYPE_PTP);
        g_ptp_tx_sent++;
    }

    // unlock LWIP core
    UNLOCK_TCPIP_CORE();

    /* The AURIX port completes TX timestamp handling from the superloop,
       so no ARM-specific interrupt masking is needed here. */
    pbuf_free(p);
}

void ptp_transmit_free(struct pbuf *pPBuf) {
    pbuf_free(pPBuf);
}

void ptp_nsd_get_interface_address(uint8_t *hwa) {
    memcpy(hwa, netif_default->hwaddr, netif_default->hwaddr_len);
}

// hook for L2 PTP messages
err_t hook_unknown_ethertype(struct pbuf *pbuf, struct netif *netif) {
    LWIP_UNUSED_ARG(netif);

    g_ptp_rx_hook_called++;

    uint16_t etherType = 0;
    memcpy(&etherType, ((uint8_t *)pbuf->payload) + 12 + ETH_PAD_SIZE, 2);
    etherType = FLEXPTP_ntohs(etherType);

    if (etherType == ETHERTYPE_PTP) {
        g_ptp_rx_ethertype_match++;

        if (!memcmp(PTP_ETHERNET_PRIMARY, (uint8_t*)pbuf->payload + ETH_PAD_SIZE, 6) ||
            !memcmp(PTP_ETHERNET_PEER_DELAY, (uint8_t*)pbuf->payload + ETH_PAD_SIZE, 6)) {
            g_ptp_rx_mac_match++;
            g_ptp_rx_enqueued++;
            ptp_receive_enqueue(((uint8_t *)pbuf->payload) + ETH_PAD_SIZE + 14,
                                pbuf->len - ETH_PAD_SIZE - 14,
                                pbuf->time_s,
                                pbuf->time_ns,
                                PTP_TP_802_3);
        }
    }

    pbuf_free(pbuf);
    return ERR_OK;
}
