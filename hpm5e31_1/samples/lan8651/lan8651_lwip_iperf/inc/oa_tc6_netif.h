#ifndef OA_TC6_NETIF_H
#define OA_TC6_NETIF_H

#include <stdbool.h>
#include <stdint.h>
#include "lwip/netif.h"
#include "oa_tc6.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t tx_frames;      /* frames handed to oa_tc6_send_frame() */
    uint32_t tx_fail;        /* oa_tc6_send_frame() failures */
    uint32_t rx_frames;      /* frames delivered to lwIP */
    uint32_t rx_dropped;     /* pbuf allocation / short frame drops */
    uint32_t rx_fcs_stripped;/* always == rx_frames, LAN8651 appends the FCS */
    uint32_t link_changes;
} oa_tc6_netif_stats_t;

/* Bind the netif layer to one TC6 instance and its MAC address. */
void oa_tc6_netif_attach(oa_tc6_t *tc6, const uint8_t mac[6]);
/* Set lwIP's netif_add() init callback. */
err_t oa_tc6_netif_init(struct netif *netif);
/* Poll the TC6 RX path and hand frames to lwIP (call in the main loop). */
void oa_tc6_netif_poll(struct netif *netif);
/* Read the LAN8651 PHY link state and drive netif_set_link_up/down(). */
bool oa_tc6_netif_update_link(struct netif *netif);
const oa_tc6_netif_stats_t *oa_tc6_netif_stats(void);

#ifdef __cplusplus
}
#endif
#endif /* OA_TC6_NETIF_H */
