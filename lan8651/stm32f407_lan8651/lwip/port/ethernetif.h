/**
 * @file  ethernetif.h
 * @brief lwIP network interface for LAN8651 over SPI
 */
#ifndef ETHERNETIF_H
#define ETHERNETIF_H

#include "lwip/netif.h"
#include "lan8651.h"

typedef struct {
	uint32_t magic;
	uint32_t session_seq;
	uint32_t tracked_port;
	uint32_t rx_input_frames;
	uint32_t tcp_data_frames;
	uint32_t tcp_payload_bytes;
	uint32_t retransmit_frames;
	uint32_t gap_frames;
	uint32_t last_seq;
	uint32_t last_payload_len;
	uint32_t highest_seq_end;
	uint32_t tx_ack_frames;
	uint32_t tx_pure_ack_frames;
	uint32_t tx_dup_ack_frames;
	uint32_t last_ack;
	uint32_t last_ack_window;
	uint32_t max_ack_window;
	uint32_t rx_pbuf_alloc_failures;
	uint32_t rx_pbuf_take_errors;
	uint32_t rx_input_errors;
	uint32_t last_input_err;
	uint32_t tx_status_samples;
	uint32_t tx_status_read_failures;
	uint32_t tx_tsr_col_samples;
	uint32_t tx_tsr_tfc_samples;
	uint32_t tx_tsr_und_samples;
	uint32_t tx_last_tsr;
	uint32_t rx_status_samples;
	uint32_t rx_status_read_failures;
	uint32_t rx_rsr_bna_samples;
	uint32_t rx_last_rsr;
} ethernetif_tcp_diag_t;

/**
 * @brief  Initialise the LAN8651 network interface (called by netif_add)
 */
err_t ethernetif_init(struct netif *netif);

/**
 * @brief  Poll for incoming frames and feed them to lwIP.
 *         Must be called frequently from the main loop.
 */
void ethernetif_input(struct netif *netif);

void ethernetif_reset_tcp_diag(uint16_t tracked_port);

extern volatile ethernetif_tcp_diag_t g_ethernetif_tcp_diag;

#endif /* ETHERNETIF_H */
