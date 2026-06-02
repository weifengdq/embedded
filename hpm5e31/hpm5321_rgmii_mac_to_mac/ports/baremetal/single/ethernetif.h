/*
 * Copyright (c) 2021-2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef ETHERNETIF_H
#define ETHERNETIF_H

#include "lwip/pbuf.h"
#include "lwip/netif.h"

typedef struct my_custom_pbuf {
   struct pbuf_custom p;
   void *dma_descriptor;
} my_custom_pbuf_t;

typedef struct ethernetif_debug_counters {
   u32_t tx_frames;
   u32_t tx_bytes;
   u32_t tx_busy;
   u32_t tx_errors;
   u32_t rx_frames;
   u32_t rx_bytes;
   u32_t input_ok;
   u32_t input_err;
   u32_t last_tx_ms;
   u32_t last_rx_ms;
} ethernetif_debug_counters_t;

#ifdef __cplusplus
extern "C" {
#endif
/* Exported functions---------------------------------------------------------*/
err_t ethernetif_init(struct netif *netif);
void ethernetif_reset_debug_counters(void);
void ethernetif_get_debug_counters(ethernetif_debug_counters_t *counters);
#if defined(NO_SYS) && !NO_SYS
void ethernetif_input(void *pvParameters);
#else
err_t ethernetif_input(struct netif *netif);
#endif
#ifdef __cplusplus /* __cplusplus */
}
#endif

#endif /* ETHERNETIF_H */
