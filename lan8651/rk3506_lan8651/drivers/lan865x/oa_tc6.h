/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * OPEN Alliance 10BASE‑T1x MAC‑PHY Serial Interface framework
 *
 * Link: https://opensig.org/download/document/OPEN_Alliance_10BASET1x_MAC-PHY_Serial_Interface_V1.1.pdf
 *
 * Author: Parthiban Veerasooran <parthiban.veerasooran@microchip.com>
 */

#include <linux/etherdevice.h>
#include <linux/spi/spi.h>

struct oa_tc6;

enum oa_tc6_quirk_flag {
	OA_TC6_BROKEN_PHY = BIT(0),
	OA_TC6_PHY_INT    = BIT(1),
};

struct oa_tc6_quirks {
	u32 quirk_flags;
};

struct oa_tc6 *oa_tc6_init(struct spi_device *spi, struct net_device *netdev,
			    struct oa_tc6_quirks *quirks);
void oa_tc6_exit(struct oa_tc6 *tc6);
int oa_tc6_write_register(struct oa_tc6 *tc6, u32 address, u32 value);
int oa_tc6_write_registers(struct oa_tc6 *tc6, u32 address, u32 value[],
			   u8 length);
int oa_tc6_read_register(struct oa_tc6 *tc6, u32 address, u32 *value);
int oa_tc6_read_registers(struct oa_tc6 *tc6, u32 address, u32 value[],
			  u8 length);
netdev_tx_t oa_tc6_start_xmit(struct oa_tc6 *tc6, struct sk_buff *skb);
int oa_tc6_zero_align_receive_frame_enable(struct oa_tc6 *tc6);
void oa_tc6_request_reset(struct oa_tc6 *tc6);
void oa_tc6_set_post_reset_callback(struct oa_tc6 *tc6,
				    int (*cb)(void *priv), void *priv);
