// SPDX-License-Identifier: GPL-2.0
/*
 * onsemi T30HM1TS2500 (S2500) 10BASE-T1S MAC-PHY driver
 *
 * The S2500 is an IEEE 802.3cg 10BASE-T1S MAC-PHY with an OPEN Alliance
 * 10BASE-T1x MAC-PHY Serial Interface (OA-SPI, TC6).  It is register
 * compatible with the standard OA memory maps 0..4 but, unlike the
 * Microchip LAN865x, its MAC lives in MMS1 and follows the standard OA
 * MAC register layout (MCONTROL0 at 0x0000 with TXEN/RXEN, address
 * filters, statistics counters).
 *
 * OA-SPI transactions implemented here:
 *   - control: 4B header + 4B value(s) + 4B ignored, CS_N per transaction
 *   - data:    4B header + 64B payload, returns 64B payload + 4B footer
 *   - 32-bit words, MSB first, odd parity in bit 0 of header/footer
 *
 * The driver deliberately keeps the PLCA / SQI / HDD control plane in
 * sysfs instead of phylib (the S2500 is not an MDIO PHY; its PHY
 * registers are reached through OA "direct PHY register access" using
 * the MMS selector of the control transaction).
 *
 * Copyright (c) 2026 S2500 project
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/bitfield.h>
#include <linux/ethtool.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/sysfs.h>

#include "s2500_regs.h"

#define DRV_NAME		"s2500"
#define DRV_VERSION		"1.0"

#define S2500_CHUNK		64
#define S2500_MAX_FRAME		1536
#define S2500_CTRL_LEN		12
#define S2500_DATA_LEN		(4 + S2500_CHUNK)
/* Max chunks moved by one SPI burst (same 48-chunk limit as OA-TC6) */
#define S2500_MAX_BURST		48
#define S2500_BURST_SIZE	(S2500_MAX_BURST * S2500_DATA_LEN)
#define S2500_MAX_REGS		128
#define S2500_CTRL_BUF_SIZE	(4 + 4 * S2500_MAX_REGS + 4)
#define S2500_TX_TIMEOUT	(4 * HZ)
#define S2500_TXQ_HIGH		48
#define S2500_TXQ_LOW		16
#define S2500_LINK_POLL_MS	500

#define S2500_FLAG_IRQ		0
#define S2500_FLAG_RESET	1
#define S2500_FLAG_RXMODE	2

struct s2500_priv {
	struct spi_device *spi;
	struct net_device *ndev;
	struct gpio_desc *reset_gpio;

	int irq;
	struct task_struct *thread;
	wait_queue_head_t wq;
	unsigned long flags;
	spinlock_t irq_lock;
	bool irq_masked;

	/* OA-SPI transaction serialization + buffers */
	struct mutex lock;
	u8 *ctrl_tx;
	u8 *ctrl_rx;
	u8 *txbuf;
	u8 *rxbuf;
	u32 tx_credits;

	/* burst data path: one SPI transfer carries many 68-byte chunks */
	u8 *burst_tx;
	u8 *burst_rx;
	struct sk_buff *tx_cur;		/* frame being packed into the burst */
	u32 tx_off;
	unsigned long tx_stall;

	struct sk_buff_head txq;
	struct sk_buff *rx_skb;

	/* configuration */
	u8 plca_id;
	u8 plca_ncnt;
	u8 plca_totmr;
	bool plca_en;
	bool plca_cr;
	bool led_on;

	/* stats (driver internal) */
	u64 spi_errors;
	u64 ctrl_retries;
	u64 rx_fcs_err;
	u64 rx_runt;
	u64 rx_dropped;
	u64 rx_err;
	u64 tx_dropped;
	u64 tx_err;

	/* cached hardware counters */
	u64 hw_rx_frames;
	u64 hw_tx_frames;
	u64 hw_rx_bytes;
	u64 hw_tx_bytes;
	u64 hw_rx_fcs_err;
	u64 hw_rx_dropped;
	u64 hw_tx_underflow;

	bool link_up;
	bool promisc;
	unsigned long link_poll;
	unsigned long err_poll;

	/* diagnostics */
	unsigned int loopback;
	bool hdd_valid;
	u8 hdd_code;
	bool hdd_1wire;
	u32 hdd_raw;
};

/* Module parameters (defaults follow the S2500 bus plan: PLCA follower 5) */
static int plca_en;
static int plca_id = 5;
static int plca_ncnt = 8;
static int plca_totmr = 32;
static int plca_cr;
static int led = 1;
static int rx_fcs_strip = 1;
static int spi_hz;

module_param(plca_en, int, 0644);
MODULE_PARM_DESC(plca_en, "Enable PLCA at load time (default 0)");
module_param(plca_id, int, 0644);
MODULE_PARM_DESC(plca_id, "PLCA node id, 0 = coordinator (default 5)");
module_param(plca_ncnt, int, 0644);
MODULE_PARM_DESC(plca_ncnt, "PLCA node count (default 8)");
module_param(plca_totmr, int, 0644);
MODULE_PARM_DESC(plca_totmr, "PLCA TO timer in bit times (default 32)");
module_param(plca_cr, int, 0644);
MODULE_PARM_DESC(plca_cr, "Enable PLCA coordinator redundancy (default 0)");
module_param(led, int, 0644);
MODULE_PARM_DESC(led, "Program DIO0/DIO1 as activity/PLCA LEDs (default 1)");
module_param(rx_fcs_strip, int, 0644);
MODULE_PARM_DESC(rx_fcs_strip, "Strip the 4-byte FCS from received frames (default 1)");
module_param(spi_hz, int, 0644);
MODULE_PARM_DESC(spi_hz, "Override SPI clock in Hz (0 = use DTS spi-max-frequency, default 0)");

/* --------------------------------------------------------------------- */
/* OA-SPI primitives                                                     */
/* --------------------------------------------------------------------- */

/* Odd parity of a 32-bit header/footer word lives in bit 0. */
static u32 s2500_oaspi_parity(u32 v)
{
	v >>= 1;
	v ^= v >> 16;
	v ^= v >> 8;
	v ^= v >> 4;
	v ^= v >> 2;
	v ^= v >> 1;
	return (v & 1u) ? 0u : 1u;
}

static u32 s2500_ctrl_header(bool write, u8 mms, u16 addr, u8 len_m1, bool aid)
{
	u32 h = 0;

	if (write)
		h |= BIT(29);		/* WNR */
	if (aid)
		h |= BIT(28);		/* AID: single register access */
	h |= FIELD_PREP(GENMASK(27, 24), mms);
	h |= FIELD_PREP(GENMASK(23, 8), addr);
	h |= FIELD_PREP(GENMASK(7, 1), len_m1);
	h |= s2500_oaspi_parity(h);
	return h;
}

static int s2500_spi_xfer(struct s2500_priv *priv, const void *tx, void *rx, u32 len)
{
	struct spi_transfer xfer = {
		.tx_buf = tx,
		.rx_buf = rx,
		.len = len,
	};
	struct spi_message msg;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	return spi_sync(priv->spi, &msg);
}

/*
 * Read/write @n consecutive 32-bit registers starting at (mms, addr).
 * Caller must hold priv->lock.  Returns 0 or a negative errno.
 */
static int __s2500_ctrl(struct s2500_priv *priv, bool write, u8 mms, u16 addr,
			u32 *val, int n)
{
	int len = 8 + 4 * n;
	int attempt;
	int ret = -EIO;
	__be32 *tx = (__be32 *)priv->ctrl_tx;
	__be32 *rx = (__be32 *)priv->ctrl_rx;
	u32 hdr;

	if (!n || n > S2500_MAX_REGS)
		return -EINVAL;

	for (attempt = 0; attempt < 3; attempt++) {
		hdr = s2500_ctrl_header(write, mms, addr, n - 1, n == 1);
		memset(priv->ctrl_tx, 0, len);
		memset(priv->ctrl_rx, 0, len);
		tx[0] = cpu_to_be32(hdr);
		if (write) {
			int i;

			for (i = 0; i < n; i++)
				tx[1 + i] = cpu_to_be32(val[i]);
		}
		ret = s2500_spi_xfer(priv, priv->ctrl_tx, priv->ctrl_rx, len);
		if (ret)
			continue;
		if (be32_to_cpu(rx[1]) != hdr) {
			/* header echo corrupted: parity error on the SPI link */
			priv->ctrl_retries++;
			ret = -EPROTO;
			continue;
		}
		if (!write) {
			int i;

			for (i = 0; i < n; i++)
				val[i] = be32_to_cpu(rx[2 + i]);
		}
		return 0;
	}
	priv->spi_errors++;
	return ret;
}

static int s2500_reg_read(struct s2500_priv *priv, u8 mms, u16 addr, u32 *val)
{
	int ret;

	mutex_lock(&priv->lock);
	ret = __s2500_ctrl(priv, false, mms, addr, val, 1);
	mutex_unlock(&priv->lock);
	return ret;
}

static int s2500_reg_write(struct s2500_priv *priv, u8 mms, u16 addr, u32 val)
{
	int ret;

	mutex_lock(&priv->lock);
	ret = __s2500_ctrl(priv, true, mms, addr, &val, 1);
	mutex_unlock(&priv->lock);
	return ret;
}

static int s2500_reg_readn(struct s2500_priv *priv, u8 mms, u16 addr, u32 *val, int n)
{
	int ret;

	mutex_lock(&priv->lock);
	ret = __s2500_ctrl(priv, false, mms, addr, val, n);
	mutex_unlock(&priv->lock);
	return ret;
}

static int s2500_reg_update(struct s2500_priv *priv, u8 mms, u16 addr,
			    u32 clr, u32 set)
{
	u32 v;
	int ret;

	mutex_lock(&priv->lock);
	ret = __s2500_ctrl(priv, false, mms, addr, &v, 1);
	if (!ret) {
		v = (v & ~clr) | set;
		ret = __s2500_ctrl(priv, true, mms, addr, &v, 1);
	}
	mutex_unlock(&priv->lock);
	return ret;
}

/*
 * One OA data chunk transaction.  @hdr is the data header without parity,
 * @payload may be NULL (transmit side), @rpayload receives the 64 data
 * bytes returned by the MAC-PHY and @footer the 4-byte data footer.
 */
static int s2500_data_xfer(struct s2500_priv *priv, u32 hdr,
			   const u8 *payload, u8 *rpayload, u32 *footer)
{
	__be32 *tx = (__be32 *)priv->txbuf;
	__be32 *rx = (__be32 *)priv->rxbuf;
	int ret;

	hdr |= s2500_oaspi_parity(hdr);
	tx[0] = cpu_to_be32(hdr);
	if (payload)
		memcpy(priv->txbuf + 4, payload, S2500_CHUNK);
	else
		memset(priv->txbuf + 4, 0, S2500_CHUNK);

	mutex_lock(&priv->lock);
	ret = s2500_spi_xfer(priv, priv->txbuf, priv->rxbuf, S2500_DATA_LEN);
	mutex_unlock(&priv->lock);
	if (ret) {
		priv->spi_errors++;
		return ret;
	}
	*footer = be32_to_cpu(rx[S2500_CHUNK / 4]);
	if (rpayload)
		memcpy(rpayload, priv->rxbuf, S2500_CHUNK);
	return 0;
}

/* --------------------------------------------------------------------- */
/* Hardware configuration                                                */
/* --------------------------------------------------------------------- */

static int s2500_wait_resetc(struct s2500_priv *priv)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(200);
	u32 v;

	do {
		if (s2500_reg_read(priv, S2500_MMS_STD, S2500_REG_STATUS0, &v))
			return -EIO;
		if (v & S2500_STATUS0_RESETC)
			return 0;
		usleep_range(1000, 2000);
	} while (time_before(jiffies, timeout));

	netdev_err(priv->ndev, "timeout waiting for RESETC (STATUS0=%08x)\n", v);
	return -ETIMEDOUT;
}

static int s2500_hw_reset(struct s2500_priv *priv)
{
	u32 v;

	if (priv->reset_gpio) {
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
		usleep_range(500, 1000);
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
		msleep(15);
	} else {
		if (s2500_reg_write(priv, S2500_MMS_STD, S2500_REG_RESET, 1))
			return -EIO;
		msleep(5);
	}

	if (s2500_wait_resetc(priv))
		return -ETIMEDOUT;

	/* Clear the reset-complete latch and any stale error bits. */
	v = S2500_STATUS0_RESETC | S2500_STATUS0_ERR_MASK;
	return s2500_reg_write(priv, S2500_MMS_STD, S2500_REG_STATUS0, v);
}

static int s2500_set_mac(struct s2500_priv *priv, const u8 *mac)
{
	u32 lo = ((u32)mac[2] << 24) | ((u32)mac[3] << 16) |
		 ((u32)mac[4] << 8) | mac[5];
	u32 hi = ((u32)mac[0] << 8) | mac[1];
	int ret;

	ret = s2500_reg_write(priv, S2500_MMS_MAC, S2500_REG_MADDRFILT0L, lo);
	if (ret)
		return ret;
	ret = s2500_reg_write(priv, S2500_MMS_MAC, S2500_REG_MADDRMASK0L,
			      0xffffffff);
	if (ret)
		return ret;
	ret = s2500_reg_write(priv, S2500_MMS_MAC, S2500_REG_MADDRMASK0H,
			      0x0000ffff);
	if (ret)
		return ret;
	return s2500_reg_write(priv, S2500_MMS_MAC, S2500_REG_MADDRFILT0H,
			       hi | S2500_FILT_EN);
}

static int s2500_hw_enable(struct s2500_priv *priv)
{
	return s2500_reg_update(priv, S2500_MMS_MAC, S2500_REG_MCONTROL0, 0,
				S2500_MC_TXEN | S2500_MC_RXEN);
}

static int s2500_hw_disable(struct s2500_priv *priv)
{
	return s2500_reg_update(priv, S2500_MMS_MAC, S2500_REG_MCONTROL0,
				S2500_MC_TXEN | S2500_MC_RXEN, 0);
}

/* PLCA: CTRL0.EN / CTRL1.NCNT:ID / TOTMR, plus the onsemi PLCAEXT bits. */
static int s2500_plca_apply(struct s2500_priv *priv)
{
	u32 ctl1 = FIELD_PREP(GENMASK(15, 8), priv->plca_ncnt) |
		   FIELD_PREP(GENMASK(7, 0), priv->plca_id);
	int ret;

	ret = s2500_reg_write(priv, S2500_MMS_PLCA, S2500_REG_PLCACTL0, 0);
	if (ret)
		return ret;
	ret = s2500_reg_write(priv, S2500_MMS_PLCA, S2500_REG_PLCACTL0,
			      S2500_PLCA_RST);
	if (ret)
		return ret;
	usleep_range(50, 100);
	ret = s2500_reg_write(priv, S2500_MMS_PLCA, S2500_REG_PLCATOTMR,
			      priv->plca_totmr);
	if (ret)
		return ret;
	ret = s2500_reg_write(priv, S2500_MMS_PLCA, S2500_REG_PLCACTL1, ctl1);
	if (ret)
		return ret;
	/* Coordinator redundancy is an onsemi extension (MMS4 0x8002). */
	ret = s2500_reg_update(priv, S2500_MMS_PLCA, S2500_REG_PLCAEXT,
			       0, priv->plca_cr ? S2500_PLCAEXT_CREN : 0);
	if (ret)
		return ret;
	return s2500_reg_write(priv, S2500_MMS_PLCA, S2500_REG_PLCACTL0,
			       priv->plca_en ? S2500_PLCA_EN : 0);
}

static int s2500_led_apply(struct s2500_priv *priv)
{
	u32 v;

	if (!priv->led_on)
		return s2500_reg_write(priv, S2500_MMS_VENDOR, S2500_REG_DIOCFG0, 0);

	/* S2500_1 board: LEDs are active high.  DIO0 = TX/RX activity,
	 * DIO1 = PLCA status (mapped to "link/activity" in CSMA mode).
	 */
	v = S2500_DIO_SLW(0) | S2500_DIO_PEN(0) |
	    S2500_DIO_FN(0, S2500_DIOFN_LED_TXRX) | S2500_DIO_VAL(0, 1);
	v |= S2500_DIO_SLW(1) | S2500_DIO_PEN(1) |
	     S2500_DIO_FN(1, S2500_DIOFN_LED_PLCASTS) | S2500_DIO_VAL(1, 1);
	return s2500_reg_write(priv, S2500_MMS_VENDOR, S2500_REG_DIOCFG0, v);
}

static int s2500_hw_init(struct s2500_priv *priv)
{
	u32 id = 0, v;
	int ret;

	ret = s2500_reg_read(priv, S2500_MMS_STD, S2500_REG_IDVER, &id);
	if (ret) {
		netdev_err(priv->ndev, "cannot read IDVER: %d\n", ret);
		return ret;
	}
	if (id == 0 || id == 0xffffffff) {
		netdev_err(priv->ndev, "no MAC-PHY response (IDVER=%08x)\n", id);
		return -ENODEV;
	}
	if ((id & 0xff) != S2500_IDVER_S2500) {
		netdev_err(priv->ndev, "not a T30HM1TS2500 (IDVER=%08x)\n", id);
		return -ENODEV;
	}
	netdev_info(priv->ndev, "S2500 IDVER=%08x\n", id);

	ret = s2500_hw_reset(priv);
	if (ret)
		return ret;

	/* 64-byte chunks, RX frames aligned to chunk start (ZARFE), 8-credit
	 * TX threshold, configuration synchronized.
	 */
	v = S2500_CONFIG0_SYNC | S2500_CONFIG0_ZARFE |
	    S2500_CONFIG0_TXCTHRESH_8 | S2500_CONFIG0_CPS_64;
	ret = s2500_reg_write(priv, S2500_MMS_STD, S2500_REG_CONFIG0, v);
	if (ret)
		return ret;

	/* Mask all STATUS0 events: the data/credit interrupts are not
	 * maskable and remain the wake-up source, errors are polled.
	 */
	ret = s2500_reg_write(priv, S2500_MMS_STD, S2500_REG_IMSK0,
			      S2500_STATUS0_ERR_MASK);
	if (ret)
		return ret;
	ret = s2500_reg_write(priv, S2500_MMS_STD, S2500_REG_IMSK1, 0x3);
	if (ret)
		return ret;

	/* PHY transmit/receive enable: LCTL defaults to 0 (both directions
	 * off) and is cleared by every chip reset, so it must be set here.
	 */
	ret = s2500_reg_update(priv, S2500_MMS_STD, S2500_REG_PHY_CTRL,
			       0, S2500_PHYCTRL_LCTL);
	if (ret)
		return ret;

	/* Standard OA MAC: FCS append + address filtering, broadcast and
	 * multicast accepted (needed for ARP/IPv6).
	 */
	ret = s2500_reg_write(priv, S2500_MMS_MAC, S2500_REG_MCONTROL0,
			      S2500_MC_FCSA | S2500_MC_ADRF);
	if (ret)
		return ret;

	ret = s2500_set_mac(priv, priv->ndev->dev_addr);
	if (ret)
		return ret;

	ret = s2500_plca_apply(priv);
	if (ret)
		return ret;

	ret = s2500_led_apply(priv);
	if (ret)
		return ret;

	/* TX_CLK output off (EMI), recommended slew. */
	if (!s2500_reg_read(priv, S2500_MMS_VENDOR, S2500_REG_TWEAKS2, &v)) {
		v &= ~S2500_TWEAKS2_CLKO_ENA;
		s2500_reg_write(priv, S2500_MMS_VENDOR, S2500_REG_TWEAKS2, v);
	}

	priv->tx_credits = 0;
	return 0;
}

static void s2500_hw_start(struct s2500_priv *priv)
{
	if (s2500_hw_enable(priv))
		netdev_err(priv->ndev, "failed to enable MAC\n");
}

static void s2500_hw_stop(struct s2500_priv *priv)
{
	if (s2500_hw_disable(priv))
		netdev_err(priv->ndev, "failed to disable MAC\n");
}

static int s2500_link_poll(struct s2500_priv *priv)
{
	u32 st = 0;
	bool up;

	if (s2500_reg_read(priv, S2500_MMS_STD, S2500_REG_PHY_STATUS, &st))
		return -EIO;

	up = !!(st & S2500_PHYSTS_LKST);
	if (up != priv->link_up) {
		priv->link_up = up;
		if (up) {
			netdev_info(priv->ndev, "link up (10BASE-T1S, PHY status %08x)\n", st);
			netif_carrier_on(priv->ndev);
		} else {
			netdev_info(priv->ndev, "link down (PHY status %08x)\n", st);
			netif_carrier_off(priv->ndev);
		}
	}
	return 0;
}

static void s2500_status_check(struct s2500_priv *priv)
{
	u32 st = 0;

	if (s2500_reg_read(priv, S2500_MMS_STD, S2500_REG_STATUS0, &st))
		return;
	if (!st)
		return;
	if (st & S2500_STATUS0_ERR_MASK) {
		priv->rx_err++;
		netdev_dbg(priv->ndev, "STATUS0 error %08x\n", st);
	}
	/* W1C; RESETC is a sticky reset-complete flag. */
	if (st & S2500_STATUS0_RESETC) {
		netdev_warn(priv->ndev, "unexpected reset complete\n");
		set_bit(S2500_FLAG_RESET, &priv->flags);
		wake_up_interruptible(&priv->wq);
		st &= ~S2500_STATUS0_RESETC;
	}
	if (st & ~S2500_STATUS0_PHYINT)
		s2500_reg_write(priv, S2500_MMS_STD, S2500_REG_STATUS0,
				st & ~S2500_STATUS0_PHYINT);
}

static void s2500_hw_counters(struct s2500_priv *priv)
{
	u32 c[S2500_MST_COUNT];
	u64 rx_bytes, tx_bytes;

	if (s2500_reg_readn(priv, S2500_MMS_MAC, S2500_REG_MST_BASE, c,
			    S2500_MST_COUNT))
		return;

	rx_bytes = ((u64)c[S2500_MST_RX_OCTETS_H - S2500_REG_MST_BASE] << 32) |
		   c[S2500_MST_RX_OCTETS_L - S2500_REG_MST_BASE];
	tx_bytes = ((u64)c[S2500_MST_TX_OCTETS_H - S2500_REG_MST_BASE] << 32) |
		   c[S2500_MST_TX_OCTETS_L - S2500_REG_MST_BASE];

	priv->hw_rx_bytes = rx_bytes;
	priv->hw_tx_bytes = tx_bytes;
	priv->hw_rx_frames = c[S2500_MST_RX_FRAMES - S2500_REG_MST_BASE];
	priv->hw_tx_frames = c[S2500_MST_TX_FRAMES - S2500_REG_MST_BASE];
	priv->hw_rx_fcs_err = c[S2500_MST_RX_FCSERR - S2500_REG_MST_BASE];
	priv->hw_rx_dropped = c[S2500_MST_RX_DROPPED - S2500_REG_MST_BASE];
	priv->hw_tx_underflow = c[S2500_MST_TX_UNDERFLOW - S2500_REG_MST_BASE];
}

static int s2500_read_sqi(struct s2500_priv *priv, u8 *sqi, u8 *sqip)
{
	u32 v;

	if (s2500_reg_read(priv, S2500_MMS_PLCA, S2500_REG_DCQ_SQI, &v))
		return -EIO;
	*sqi = v & S2500_SQI_MASK;
	if (s2500_reg_read(priv, S2500_MMS_PLCA, S2500_REG_DCQ_SQIP, &v))
		return -EIO;
	*sqip = v & S2500_SQIP_MASK;
	return 0;
}

static int s2500_hdd_run(struct s2500_priv *priv, u32 *result)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(2000);
	u32 v = 0;

	if (s2500_reg_write(priv, S2500_MMS_PLCA, S2500_REG_HDD,
			    S2500_HDD_CTRL | S2500_HDD_START))
		return -EIO;
	do {
		if (s2500_reg_read(priv, S2500_MMS_PLCA, S2500_REG_HDD, &v))
			return -EIO;
		if (v & S2500_HDD_VALID) {
			s2500_reg_write(priv, S2500_MMS_PLCA, S2500_REG_HDD, 0);
			*result = v;
			return 0;
		}
		usleep_range(1000, 2000);
	} while (time_before(jiffies, timeout));

	s2500_reg_write(priv, S2500_MMS_PLCA, S2500_REG_HDD, 0);
	return -ETIMEDOUT;
}

/* --------------------------------------------------------------------- */
/* TX path                                                               */
/* --------------------------------------------------------------------- */

static void s2500_rx_chunk(struct s2500_priv *priv, const u8 *payload,
			   u32 footer);
static void s2500_irq_ack(struct s2500_priv *priv);

/*
 * Burst data path
 *
 * One SPI transfer carries several 68-byte chunks (4-byte header + 64-byte
 * payload), which is the normal OA-SPI way of moving data.  Feeding the
 * MAC-PHY one chunk per spi_sync() costs a whole SPI transaction (~75 us at
 * 25 MHz, measured on RK3506) for every 68 bytes, so a 1514-byte frame needs
 * 24 transactions and the link tops out at roughly two thirds of the
 * 10BASE-T1S line rate.  Batching the chunks amortises that overhead over up
 * to 48 chunks per transfer.
 *
 * Within one burst:
 *   - the leading chunks carry the frames waiting in the TX queue, as many as
 *     the current TX credit count allows,
 *   - the rest are empty chunks, used to collect the frames the MAC-PHY has
 *     queued for us.
 */

/* Prepare chunk @idx of the outgoing burst, returns the payload pointer. */
static u8 *s2500_burst_chunk(struct s2500_priv *priv, u32 idx, u32 hdr)
{
	u8 *p = priv->burst_tx + idx * S2500_DATA_LEN;

	hdr |= s2500_oaspi_parity(hdr);
	*(__be32 *)p = cpu_to_be32(hdr);
	memset(p + 4, 0, S2500_CHUNK);
	return p + 4;
}

static int s2500_burst_xfer(struct s2500_priv *priv, u32 chunks)
{
	struct spi_transfer xfer = {
		.tx_buf = priv->burst_tx,
		.rx_buf = priv->burst_rx,
		.len = chunks * S2500_DATA_LEN,
	};
	struct spi_message msg;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	return spi_sync(priv->spi, &msg);
}

/* Pack queued frames into the burst buffer, at most @limit chunks. */
static u32 s2500_burst_fill(struct s2500_priv *priv, u32 limit)
{
	struct net_device *ndev = priv->ndev;
	u32 idx = 0;

	while (idx < limit) {
		struct sk_buff *skb = priv->tx_cur;
		u32 len, off, n, hdr, copy;
		bool first, last;
		u8 *p;

		if (!skb) {
			skb = skb_dequeue(&priv->txq);
			if (!skb)
				break;
			priv->tx_cur = skb;
			priv->tx_off = 0;
		}

		len = max_t(u32, skb->len, ETH_ZLEN);
		off = priv->tx_off;
		n = min_t(u32, len - off, S2500_CHUNK);
		first = !off;
		last = (off + n >= len);

		hdr = BIT(31) | BIT(29) | BIT(21);	/* DNC | NORX | DV */
		if (first)
			hdr |= BIT(20);			/* SV */
		if (last)
			hdr |= BIT(14) | ((n - 1) << 8);/* EV | EBO */

		p = s2500_burst_chunk(priv, idx, hdr);
		copy = min_t(u32, S2500_CHUNK, skb->len - min(off, skb->len));
		if (off < skb->len)
			memcpy(p, skb->data + off, copy);

		idx++;
		priv->tx_off = off + n;
		priv->tx_stall = jiffies + msecs_to_jiffies(500);
		if (last) {
			ndev->stats.tx_packets++;
			ndev->stats.tx_bytes += skb->len;
			dev_kfree_skb(skb);
			priv->tx_cur = NULL;
		}
	}
	return idx;
}

static void s2500_tx_cur_drop(struct s2500_priv *priv)
{
	if (priv->tx_cur) {
		priv->ndev->stats.tx_dropped++;
		dev_kfree_skb_any(priv->tx_cur);
		priv->tx_cur = NULL;
	}
	priv->tx_off = 0;
}

/*
 * Run bursts until there is nothing left to move: no queued frame, no data
 * waiting in the MAC-PHY and no pending interrupt.
 */
static void s2500_burst_run(struct s2500_priv *priv)
{
	struct net_device *ndev = priv->ndev;
	int guard = 0;

	while (guard++ < 128) {
		u32 v, credits, rca, n, tx_chunks, i;

		clear_bit(S2500_FLAG_IRQ, &priv->flags);

		if (s2500_reg_read(priv, S2500_MMS_STD, S2500_REG_BUFSTS, &v)) {
			priv->spi_errors++;
			break;
		}
		credits = FIELD_GET(S2500_BUFSTS_TXC_MASK, v);
		rca = FIELD_GET(S2500_BUFSTS_RCA_MASK, v);

		tx_chunks = 0;
		if (credits && (priv->tx_cur || !skb_queue_empty(&priv->txq)))
			tx_chunks = s2500_burst_fill(priv,
						     min_t(u32, credits,
							   S2500_MAX_BURST));

		n = tx_chunks;
		while (n < S2500_MAX_BURST && (n - tx_chunks) < rca) {
			s2500_burst_chunk(priv, n, BIT(31));
			n++;
		}

		if (!n) {
			if (test_and_clear_bit(S2500_FLAG_IRQ, &priv->flags))
				s2500_irq_ack(priv);
			break;
		}

		if (s2500_burst_xfer(priv, n)) {
			priv->spi_errors++;
			s2500_tx_cur_drop(priv);
			break;
		}

		for (i = 0; i < n; i++) {
			const u8 *p = priv->burst_rx + i * S2500_DATA_LEN;
			u32 footer = get_unaligned_be32(p + S2500_CHUNK);

			if (footer & BIT(30)) {		/* header bad */
				priv->tx_err++;
				continue;
			}
			if (i < tx_chunks && (footer & BIT(15))) {
				priv->tx_dropped++;
				continue;
			}
			if (i >= tx_chunks)
				s2500_rx_chunk(priv, p, footer);
		}

		/* The last footer always carries the live credit counters. */
		priv->tx_credits = FIELD_GET(GENMASK(5, 1),
					     get_unaligned_be32(priv->burst_rx +
						(n - 1) * S2500_DATA_LEN +
						S2500_CHUNK));

		if (priv->tx_cur && time_after(jiffies, priv->tx_stall)) {
			netdev_warn(ndev, "TX stalled, dropping partial frame\n");
			s2500_tx_cur_drop(priv);
		}

		if (test_bit(S2500_FLAG_IRQ, &priv->flags))
			continue;
		if (!rca && !tx_chunks)
			break;
	}

	if (netif_queue_stopped(ndev) && skb_queue_len(&priv->txq) < S2500_TXQ_LOW)
		netif_wake_queue(ndev);
}

/* --------------------------------------------------------------------- */
/* RX path                                                               */
/* --------------------------------------------------------------------- */

static void s2500_rx_drop(struct s2500_priv *priv)
{
	if (priv->rx_skb) {
		dev_kfree_skb_any(priv->rx_skb);
		priv->rx_skb = NULL;
	}
}

static void s2500_rx_complete(struct s2500_priv *priv)
{
	struct net_device *ndev = priv->ndev;
	struct sk_buff *skb = priv->rx_skb;

	priv->rx_skb = NULL;
	if (!skb)
		return;

	/* The S2500 forwards RX frames with their FCS appended; validate it
	 * and strip the 4 bytes before handing the frame to the stack.
	 * Set rx_fcs_strip=0 if a future revision strips it in hardware.
	 */
	if (rx_fcs_strip) {
		u32 crc, fcs;

		if (skb->len < ETH_ZLEN + ETH_FCS_LEN) {
			priv->rx_runt++;
			dev_kfree_skb_any(skb);
			return;
		}
		crc = crc32_le(~0, skb->data, skb->len - ETH_FCS_LEN) ^ ~0;
		fcs = get_unaligned_le32(skb->data + skb->len - ETH_FCS_LEN);
		if (crc != fcs) {
			priv->rx_fcs_err++;
			dev_kfree_skb_any(skb);
			return;
		}
		skb_trim(skb, skb->len - ETH_FCS_LEN);
	} else if (skb->len < ETH_ZLEN) {
		priv->rx_runt++;
		dev_kfree_skb_any(skb);
		return;
	}

	skb->protocol = eth_type_trans(skb, ndev);
	skb->ip_summed = CHECKSUM_NONE;
	ndev->stats.rx_packets++;
	ndev->stats.rx_bytes += skb->len;
	netif_rx(skb);
}

static void s2500_rx_chunk(struct s2500_priv *priv, const u8 *payload,
			   u32 footer)
{
	u32 swo, ebo, start, n;
	bool sv, ev;

	if ((footer & 1u) != s2500_oaspi_parity(footer)) {
		priv->rx_err++;
		s2500_rx_drop(priv);
		return;
	}
	if (footer & BIT(30)) {		/* HDRB */
		priv->rx_err++;
		s2500_rx_drop(priv);
		return;
	}
	if (!(footer & BIT(21))) {	/* DV */
		if (priv->rx_skb) {
			priv->rx_dropped++;
			s2500_rx_drop(priv);
		}
		return;
	}
	if (footer & BIT(15)) {		/* FD: frame dropped by the MAC */
		priv->rx_fcs_err++;
		s2500_rx_drop(priv);
		return;
	}

	sv = footer & BIT(20);
	ev = footer & BIT(14);
	swo = FIELD_GET(GENMASK(19, 16), footer);
	ebo = FIELD_GET(GENMASK(13, 8), footer);
	start = swo * 4;

	if (sv && priv->rx_skb) {
		/* A new frame starts while the previous one is incomplete. */
		priv->rx_dropped++;
		s2500_rx_drop(priv);
	}
	if (!priv->rx_skb) {
		if (!sv) {
			priv->rx_dropped++;
			return;
		}
		priv->rx_skb = netdev_alloc_skb_ip_align(priv->ndev,
							 S2500_MAX_FRAME);
		if (!priv->rx_skb) {
			priv->rx_dropped++;
			return;
		}
	}

	n = ev ? (ebo + 1 - start) : (S2500_CHUNK - start);
	if (start >= S2500_CHUNK || start + n > S2500_CHUNK || !n) {
		priv->rx_dropped++;
		s2500_rx_drop(priv);
		return;
	}
	skb_put_data(priv->rx_skb, payload + start, n);
	if (ev)
		s2500_rx_complete(priv);
}

/* Empty data chunk: deasserts IRQ_N and refreshes the TX credit counter. */
static void s2500_irq_ack(struct s2500_priv *priv)
{
	u32 footer = 0;

	if (s2500_data_xfer(priv, BIT(31) | BIT(29), NULL, NULL, &footer))
		return;
	priv->tx_credits = FIELD_GET(GENMASK(5, 1), footer);
}

/* --------------------------------------------------------------------- */
/* Interrupt + SPI thread                                                */
/* --------------------------------------------------------------------- */

static irqreturn_t s2500_isr(int irq, void *data)
{
	struct s2500_priv *priv = data;
	unsigned long flags;

	spin_lock_irqsave(&priv->irq_lock, flags);
	if (!priv->irq_masked) {
		priv->irq_masked = true;
		disable_irq_nosync(irq);
	}
	spin_unlock_irqrestore(&priv->irq_lock, flags);

	set_bit(S2500_FLAG_IRQ, &priv->flags);
	wake_up_interruptible(&priv->wq);
	return IRQ_HANDLED;
}

static void s2500_irq_unmask(struct s2500_priv *priv)
{
	unsigned long flags;
	bool do_enable = false;

	if (!priv->irq)
		return;
	spin_lock_irqsave(&priv->irq_lock, flags);
	if (priv->irq_masked) {
		priv->irq_masked = false;
		do_enable = true;
	}
	spin_unlock_irqrestore(&priv->irq_lock, flags);

	/* enable_irq() must not be called with our lock held. */
	if (do_enable)
		enable_irq(priv->irq);
}

static int s2500_thread(void *data)
{
	struct s2500_priv *priv = data;
	struct net_device *ndev = priv->ndev;
	unsigned long timeout;

	while (!kthread_should_stop()) {
		timeout = priv->irq ? msecs_to_jiffies(200) : msecs_to_jiffies(1);

		wait_event_interruptible_timeout(priv->wq,
			test_bit(S2500_FLAG_IRQ, &priv->flags) ||
			test_bit(S2500_FLAG_RESET, &priv->flags) ||
			!skb_queue_empty(&priv->txq), timeout);

		if (kthread_should_stop())
			break;

		if (test_and_clear_bit(S2500_FLAG_RESET, &priv->flags)) {
			skb_queue_purge(&priv->txq);
			s2500_rx_drop(priv);
			priv->tx_credits = 0;
			if (netif_running(ndev)) {
				s2500_hw_stop(priv);
				if (!s2500_hw_init(priv))
					s2500_hw_start(priv);
				netif_wake_queue(ndev);
			}
			s2500_irq_unmask(priv);
			continue;
		}

		if (!netif_running(ndev)) {
			if (test_and_clear_bit(S2500_FLAG_IRQ, &priv->flags))
				s2500_irq_ack(priv);
			s2500_irq_unmask(priv);
			continue;
		}

		clear_bit(S2500_FLAG_IRQ, &priv->flags);

		if (test_and_clear_bit(S2500_FLAG_RXMODE, &priv->flags))
			s2500_reg_update(priv, S2500_MMS_MAC,
					 S2500_REG_MCONTROL0, S2500_MC_ADRF,
					 priv->promisc ? 0 : S2500_MC_ADRF);

		s2500_burst_run(priv);

		if (test_bit(S2500_FLAG_IRQ, &priv->flags))
			s2500_irq_ack(priv);

		if (time_after(jiffies, priv->err_poll)) {
			s2500_status_check(priv);
			priv->err_poll = jiffies + msecs_to_jiffies(S2500_LINK_POLL_MS);
		}
		if (time_after(jiffies, priv->link_poll)) {
			s2500_link_poll(priv);
			s2500_hw_counters(priv);
			priv->link_poll = jiffies +
				msecs_to_jiffies(S2500_LINK_POLL_MS);
		}
		s2500_irq_unmask(priv);
	}
	return 0;
}

/* --------------------------------------------------------------------- */
/* net_device operations                                                 */
/* --------------------------------------------------------------------- */

static int s2500_open(struct net_device *ndev)
{
	struct s2500_priv *priv = netdev_priv(ndev);
	int ret;

	ret = s2500_hw_init(priv);
	if (ret)
		return ret;

	priv->link_up = false;
	netif_carrier_off(ndev);
	skb_queue_purge(&priv->txq);
	s2500_rx_drop(priv);
	priv->tx_credits = 0;
	s2500_hw_start(priv);

	priv->link_poll = jiffies;
	priv->err_poll = jiffies;
	netif_start_queue(ndev);
	wake_up_interruptible(&priv->wq);
	return 0;
}

static int s2500_stop(struct net_device *ndev)
{
	struct s2500_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	netif_carrier_off(ndev);
	s2500_hw_stop(priv);
	skb_queue_purge(&priv->txq);
	s2500_tx_cur_drop(priv);
	s2500_rx_drop(priv);
	priv->link_up = false;
	return 0;
}

static netdev_tx_t s2500_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct s2500_priv *priv = netdev_priv(ndev);

	if (unlikely(skb->len > S2500_MAX_FRAME - ETH_FCS_LEN)) {
		ndev->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	skb_queue_tail(&priv->txq, skb);
	if (skb_queue_len(&priv->txq) >= S2500_TXQ_HIGH)
		netif_stop_queue(ndev);
	wake_up_interruptible(&priv->wq);
	return NETDEV_TX_OK;
}

static void s2500_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct s2500_priv *priv = netdev_priv(ndev);

	netdev_warn(ndev, "TX timeout, reinitializing MAC-PHY\n");
	ndev->stats.tx_errors++;
	set_bit(S2500_FLAG_RESET, &priv->flags);
	wake_up_interruptible(&priv->wq);
}

static void s2500_get_stats64(struct net_device *ndev,
			      struct rtnl_link_stats64 *stats)
{
	struct s2500_priv *priv = netdev_priv(ndev);

	/* dev_get_stats() runs in atomic context: use the counters cached by
	 * the SPI thread instead of issuing SPI transfers here.
	 */
	stats->rx_packets = priv->hw_rx_frames;
	stats->tx_packets = priv->hw_tx_frames;
	stats->rx_bytes = priv->hw_rx_bytes;
	stats->tx_bytes = priv->hw_tx_bytes;
	stats->rx_errors = priv->hw_rx_fcs_err + priv->rx_err;
	stats->tx_errors = priv->hw_tx_underflow + priv->tx_err;
	stats->rx_dropped = priv->hw_rx_dropped + priv->rx_dropped;
	stats->tx_dropped = priv->tx_dropped;
}

/* ndo_set_rx_mode may run in atomic context: only latch the flag and let
 * the SPI thread program ADRF.
 */
static void s2500_set_rx_mode(struct net_device *ndev)
{
	struct s2500_priv *priv = netdev_priv(ndev);

	priv->promisc = !!(ndev->flags & IFF_PROMISC);
	set_bit(S2500_FLAG_RXMODE, &priv->flags);
	wake_up_interruptible(&priv->wq);
}

static int s2500_set_mac_address(struct net_device *ndev, void *addr)
{
	struct s2500_priv *priv = netdev_priv(ndev);
	struct sockaddr *sa = addr;
	int ret;

	ret = eth_prepare_mac_addr_change(ndev, addr);
	if (ret)
		return ret;
	if (ether_addr_equal(sa->sa_data, ndev->dev_addr))
		return 0;

	ret = s2500_set_mac(priv, sa->sa_data);
	if (ret)
		return ret;

	eth_commit_mac_addr_change(ndev, addr);
	return 0;
}

static const struct net_device_ops s2500_netdev_ops = {
	.ndo_open		= s2500_open,
	.ndo_stop		= s2500_stop,
	.ndo_start_xmit		= s2500_start_xmit,
	.ndo_tx_timeout		= s2500_tx_timeout,
	.ndo_get_stats64	= s2500_get_stats64,
	.ndo_set_rx_mode	= s2500_set_rx_mode,
	.ndo_set_mac_address	= s2500_set_mac_address,
};

/* --------------------------------------------------------------------- */
/* ethtool                                                               */
/* --------------------------------------------------------------------- */

static int s2500_get_link_ksettings(struct net_device *ndev,
				    struct ethtool_link_ksettings *cmd)
{
	struct s2500_priv *priv = netdev_priv(ndev);
	u32 st = 0;

	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_zero_link_mode(cmd, advertising);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 10baseT1S_P2MP_Half);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, 10baseT1S_P2MP_Half);

	cmd->base.phy_address = 0;
	cmd->base.autoneg = AUTONEG_DISABLE;
	cmd->base.port = PORT_TP;
	cmd->base.duplex = DUPLEX_HALF;
	cmd->base.speed = SPEED_10;

	if (!s2500_reg_read(priv, S2500_MMS_STD, S2500_REG_PHY_STATUS, &st)) {
		if (!(st & S2500_PHYSTS_LKST)) {
			cmd->base.speed = SPEED_UNKNOWN;
			cmd->base.duplex = DUPLEX_UNKNOWN;
		}
	}
	return 0;
}

static void s2500_get_drvinfo(struct net_device *ndev,
			      struct ethtool_drvinfo *info)
{
	strscpy(info->driver, DRV_NAME, sizeof(info->driver));
	strscpy(info->version, DRV_VERSION, sizeof(info->version));
	strscpy(info->bus_info, dev_name(&ndev->dev), sizeof(info->bus_info));
}

static u32 s2500_get_link(struct net_device *ndev)
{
	return netif_carrier_ok(ndev) ? 1 : 0;
}

static const struct ethtool_ops s2500_ethtool_ops = {
	.get_link_ksettings	= s2500_get_link_ksettings,
	.get_drvinfo		= s2500_get_drvinfo,
	.get_link		= s2500_get_link,
};

/* --------------------------------------------------------------------- */
/* sysfs control plane                                                   */
/* --------------------------------------------------------------------- */

static struct s2500_priv *s2500_priv_from_dev(struct device *dev)
{
	return netdev_priv(to_net_dev(dev));
}

static ssize_t plca_enable_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	return sysfs_emit(buf, "%d\n", s2500_priv_from_dev(dev)->plca_en ? 1 : 0);
}

static ssize_t plca_enable_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len)
{
	struct s2500_priv *priv = s2500_priv_from_dev(dev);
	bool en;
	int ret;

	ret = kstrtobool(buf, &en);
	if (ret)
		return ret;
	priv->plca_en = en;
	ret = s2500_plca_apply(priv);
	return ret ? ret : len;
}
static DEVICE_ATTR_RW(plca_enable);

static ssize_t plca_id_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sysfs_emit(buf, "%u\n", s2500_priv_from_dev(dev)->plca_id);
}

static ssize_t plca_id_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct s2500_priv *priv = s2500_priv_from_dev(dev);
	u8 id;
	int ret;

	ret = kstrtou8(buf, 0, &id);
	if (ret)
		return ret;
	priv->plca_id = id;
	ret = s2500_plca_apply(priv);
	return ret ? ret : len;
}
static DEVICE_ATTR_RW(plca_id);

static ssize_t plca_ncnt_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	return sysfs_emit(buf, "%u\n", s2500_priv_from_dev(dev)->plca_ncnt);
}

static ssize_t plca_ncnt_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t len)
{
	struct s2500_priv *priv = s2500_priv_from_dev(dev);
	u8 n;
	int ret;

	ret = kstrtou8(buf, 0, &n);
	if (ret)
		return ret;
	if (!n)
		return -EINVAL;
	priv->plca_ncnt = n;
	ret = s2500_plca_apply(priv);
	return ret ? ret : len;
}
static DEVICE_ATTR_RW(plca_ncnt);

static ssize_t plca_totmr_show(struct device *dev, struct device_attribute *attr,
			       char *buf)
{
	return sysfs_emit(buf, "%u\n", s2500_priv_from_dev(dev)->plca_totmr);
}

static ssize_t plca_totmr_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct s2500_priv *priv = s2500_priv_from_dev(dev);
	u8 t;
	int ret;

	ret = kstrtou8(buf, 0, &t);
	if (ret)
		return ret;
	priv->plca_totmr = t;
	ret = s2500_plca_apply(priv);
	return ret ? ret : len;
}
static DEVICE_ATTR_RW(plca_totmr);

static ssize_t plca_cr_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct s2500_priv *priv = s2500_priv_from_dev(dev);
	u32 v = 0;

	if (s2500_reg_read(priv, S2500_MMS_PLCA, S2500_REG_PLCAEXT, &v))
		return -EIO;
	return sysfs_emit(buf, "cr_en=%d\n", !!(v & S2500_PLCAEXT_CREN));
}

static ssize_t plca_cr_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct s2500_priv *priv = s2500_priv_from_dev(dev);
	bool en;
	int ret;

	ret = kstrtobool(buf, &en);
	if (ret)
		return ret;
	priv->plca_cr = en;
	ret = s2500_reg_update(priv, S2500_MMS_PLCA, S2500_REG_PLCAEXT,
			       0, en ? S2500_PLCAEXT_CREN : 0);
	return ret ? ret : len;
}
static DEVICE_ATTR_RW(plca_cr);

static ssize_t plca_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct s2500_priv *priv = s2500_priv_from_dev(dev);
	u32 v = 0, e = 0;

	if (s2500_reg_read(priv, S2500_MMS_PLCA, S2500_REG_PLCASTATUS, &v))
		return -EIO;
	if (s2500_reg_read(priv, S2500_MMS_PLCA, S2500_REG_PLCAEXT, &e))
		return -EIO;
	return sysfs_emit(buf, "pst=%d crps=%d\n",
			  !!(v & S2500_PLCA_PST),
			  !!(e & S2500_PLCAEXT_CRPS));
}
static DEVICE_ATTR_RO(plca_status);

static ssize_t s2500_sqi_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct s2500_priv *priv = s2500_priv_from_dev(dev);
	u8 sqi = 0, sqip = 0;
	int ret = s2500_read_sqi(priv, &sqi, &sqip);

	if (ret)
		return ret;
	return sysfs_emit(buf, "sqi=%u sqi_plus=%u\n", sqi, sqip);
}
static DEVICE_ATTR_RO(s2500_sqi);

static ssize_t s2500_hdd_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct s2500_priv *priv = s2500_priv_from_dev(dev);

	if (!priv->hdd_valid)
		return sysfs_emit(buf, "not run\n");
	return sysfs_emit(buf, "valid=%d short_open=%u one_wire=%d raw=%08x\n",
			  priv->hdd_valid, priv->hdd_code, priv->hdd_1wire,
			  priv->hdd_raw);
}

static ssize_t s2500_hdd_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t len)
{
	struct s2500_priv *priv = s2500_priv_from_dev(dev);
	u32 raw = 0;
	int ret;

	ret = s2500_hdd_run(priv, &raw);
	if (ret)
		return ret;
	priv->hdd_valid = !!(raw & S2500_HDD_VALID);
	priv->hdd_code = raw & S2500_HDD_SHORT_OPEN_MASK;
	priv->hdd_1wire = !!(raw & S2500_HDD_1W_ST);
	priv->hdd_raw = raw;
	return len;
}
static DEVICE_ATTR_RW(s2500_hdd);

static ssize_t s2500_counters_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct s2500_priv *priv = s2500_priv_from_dev(dev);
	struct net_device *ndev = priv->ndev;

	s2500_hw_counters(priv);
	return sysfs_emit(buf,
		"mac_rx_frames=%llu mac_tx_frames=%llu\n"
		"mac_rx_bytes=%llu mac_tx_bytes=%llu\n"
		"mac_rx_fcs_err=%llu mac_rx_dropped=%llu mac_tx_underflow=%llu\n"
		"drv_rx_fcs_err=%llu drv_rx_runt=%llu drv_rx_dropped=%llu\n"
		"drv_tx_dropped=%llu drv_tx_err=%llu drv_spi_errors=%llu\n"
		"ndev_rx_packets=%lu ndev_tx_packets=%lu\n",
		priv->hw_rx_frames, priv->hw_tx_frames,
		priv->hw_rx_bytes, priv->hw_tx_bytes,
		priv->hw_rx_fcs_err, priv->hw_rx_dropped, priv->hw_tx_underflow,
		priv->rx_fcs_err, priv->rx_runt, priv->rx_dropped,
		priv->tx_dropped, priv->tx_err, priv->spi_errors,
		ndev->stats.rx_packets, ndev->stats.tx_packets);
}
static DEVICE_ATTR_RO(s2500_counters);

static ssize_t s2500_regs_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct s2500_priv *priv = s2500_priv_from_dev(dev);
	u32 id = 0, phy = 0, stdcap = 0, status0 = 0, bufsts = 0, cfg0 = 0;
	u32 mc = 0, ctl0 = 0, ctl1 = 0, totmr = 0, pst = 0, ext = 0;

	s2500_reg_read(priv, S2500_MMS_STD, S2500_REG_IDVER, &id);
	s2500_reg_read(priv, S2500_MMS_STD, S2500_REG_PHYID, &phy);
	s2500_reg_read(priv, S2500_MMS_STD, S2500_REG_STDCAP, &stdcap);
	s2500_reg_read(priv, S2500_MMS_STD, S2500_REG_STATUS0, &status0);
	s2500_reg_read(priv, S2500_MMS_STD, S2500_REG_BUFSTS, &bufsts);
	s2500_reg_read(priv, S2500_MMS_STD, S2500_REG_CONFIG0, &cfg0);
	s2500_reg_read(priv, S2500_MMS_MAC, S2500_REG_MCONTROL0, &mc);
	s2500_reg_read(priv, S2500_MMS_PLCA, S2500_REG_PLCACTL0, &ctl0);
	s2500_reg_read(priv, S2500_MMS_PLCA, S2500_REG_PLCACTL1, &ctl1);
	s2500_reg_read(priv, S2500_MMS_PLCA, S2500_REG_PLCATOTMR, &totmr);
	s2500_reg_read(priv, S2500_MMS_PLCA, S2500_REG_PLCASTATUS, &pst);
	s2500_reg_read(priv, S2500_MMS_PLCA, S2500_REG_PLCAEXT, &ext);

	return sysfs_emit(buf,
		"IDVER=%08x PHYID=%08x STDCAP=%08x\n"
		"STATUS0=%08x BUFSTS=%08x CONFIG0=%08x\n"
		"MCONTROL0=%08x PLCACTL0=%08x PLCACTL1=%08x\n"
		"PLCATOTMR=%08x PLCASTATUS=%08x PLCAEXT=%08x\n",
		id, phy, stdcap, status0, bufsts, cfg0, mc, ctl0, ctl1,
		totmr, pst, ext);
}
static DEVICE_ATTR_RO(s2500_regs);

static ssize_t s2500_loopback_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return sysfs_emit(buf, "%u\n", s2500_priv_from_dev(dev)->loopback);
}

static ssize_t s2500_loopback_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t len)
{
	struct s2500_priv *priv = s2500_priv_from_dev(dev);
	unsigned int mode;
	int ret;

	ret = kstrtouint(buf, 0, &mode);
	if (ret)
		return ret;
	if (mode > 2)
		return -EINVAL;

	/* mode 1: PCS loopback (MMS2 PCSCTL.LOOP), mode 2: PHY loopback */
	ret = s2500_reg_update(priv, S2500_MMS_PCS, S2500_REG_PCSCTL,
			       S2500_PCSCTL_LOOP, mode == 1 ? S2500_PCSCTL_LOOP : 0);
	if (ret)
		return ret;
	ret = s2500_reg_update(priv, S2500_MMS_STD, S2500_REG_PHY_CTRL,
			       S2500_PHYCTRL_LOOPBACK,
			       mode == 2 ? S2500_PHYCTRL_LOOPBACK : 0);
	if (ret)
		return ret;
	priv->loopback = mode;
	return len;
}
static DEVICE_ATTR_RW(s2500_loopback);

static ssize_t s2500_led_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return sysfs_emit(buf, "%d\n", s2500_priv_from_dev(dev)->led_on ? 1 : 0);
}

static ssize_t s2500_led_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t len)
{
	struct s2500_priv *priv = s2500_priv_from_dev(dev);
	bool on;
	int ret;

	ret = kstrtobool(buf, &on);
	if (ret)
		return ret;
	priv->led_on = on;
	ret = s2500_led_apply(priv);
	return ret ? ret : len;
}
static DEVICE_ATTR_RW(s2500_led);

static struct attribute *s2500_attrs[] = {
	&dev_attr_plca_enable.attr,
	&dev_attr_plca_id.attr,
	&dev_attr_plca_ncnt.attr,
	&dev_attr_plca_totmr.attr,
	&dev_attr_plca_status.attr,
	&dev_attr_plca_cr.attr,
	&dev_attr_s2500_sqi.attr,
	&dev_attr_s2500_hdd.attr,
	&dev_attr_s2500_counters.attr,
	&dev_attr_s2500_regs.attr,
	&dev_attr_s2500_loopback.attr,
	&dev_attr_s2500_led.attr,
	NULL,
};
ATTRIBUTE_GROUPS(s2500);

/* --------------------------------------------------------------------- */
/* probe / remove                                                        */
/* --------------------------------------------------------------------- */

static int s2500_read_factory_mac(struct s2500_priv *priv, u8 *mac)
{
	u32 lo = 0, hi = 0;
	int ret;

	ret = s2500_reg_read(priv, S2500_MMS_VENDOR, S2500_REG_MACID0, &lo);
	if (ret)
		return ret;
	ret = s2500_reg_read(priv, S2500_MMS_VENDOR, S2500_REG_MACID1, &hi);
	if (ret)
		return ret;

	mac[0] = (hi >> 8) & 0xff;
	mac[1] = hi & 0xff;
	mac[2] = (lo >> 24) & 0xff;
	mac[3] = (lo >> 16) & 0xff;
	mac[4] = (lo >> 8) & 0xff;
	mac[5] = lo & 0xff;
	return 0;
}

static int s2500_probe(struct spi_device *spi)
{
	struct net_device *ndev;
	struct s2500_priv *priv;
	u8 mac[ETH_ALEN];
	int ret;

	ndev = alloc_etherdev(sizeof(*priv));
	if (!ndev)
		return -ENOMEM;

	priv = netdev_priv(ndev);
	priv->spi = spi;
	priv->ndev = ndev;
	priv->plca_id = plca_id;
	priv->plca_ncnt = plca_ncnt ? plca_ncnt : 8;
	priv->plca_totmr = plca_totmr;
	priv->plca_en = !!plca_en;
	priv->plca_cr = !!plca_cr;
	priv->led_on = !!led;
	spi_set_drvdata(spi, priv);
	SET_NETDEV_DEV(ndev, &spi->dev);

	mutex_init(&priv->lock);
	spin_lock_init(&priv->irq_lock);
	init_waitqueue_head(&priv->wq);
	skb_queue_head_init(&priv->txq);

	priv->ctrl_tx = devm_kzalloc(&spi->dev, S2500_CTRL_BUF_SIZE, GFP_KERNEL);
	priv->ctrl_rx = devm_kzalloc(&spi->dev, S2500_CTRL_BUF_SIZE, GFP_KERNEL);
	priv->txbuf = devm_kzalloc(&spi->dev, S2500_DATA_LEN + 32, GFP_KERNEL);
	priv->rxbuf = devm_kzalloc(&spi->dev, S2500_DATA_LEN + 32, GFP_KERNEL);
	priv->burst_tx = devm_kzalloc(&spi->dev, S2500_BURST_SIZE, GFP_KERNEL);
	priv->burst_rx = devm_kzalloc(&spi->dev, S2500_BURST_SIZE, GFP_KERNEL);
	if (!priv->ctrl_tx || !priv->ctrl_rx || !priv->txbuf || !priv->rxbuf ||
	    !priv->burst_tx || !priv->burst_rx) {
		ret = -ENOMEM;
		goto free_netdev;
	}

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	if (spi_hz > 0)
		spi->max_speed_hz = spi_hz;
	if (!spi->max_speed_hz)
		spi->max_speed_hz = 15000000;
	ret = spi_setup(spi);
	if (ret) {
		dev_err(&spi->dev, "spi_setup failed: %d\n", ret);
		goto free_netdev;
	}
	dev_info(&spi->dev, "SPI %u Hz, mode 0\n", spi->max_speed_hz);

	priv->reset_gpio = devm_gpiod_get_optional(&spi->dev, "reset",
						   GPIOD_OUT_LOW);
	if (IS_ERR(priv->reset_gpio)) {
		dev_warn(&spi->dev, "reset-gpios: %ld\n",
			 PTR_ERR(priv->reset_gpio));
		priv->reset_gpio = NULL;
	}

	/* MAC address preference: chip factory MAC (unique per module, MMS12
	 * 0x1002/0x1003), then DT local-mac-address, then random.
	 */
	if (!s2500_read_factory_mac(priv, mac) && is_valid_ether_addr(mac)) {
		eth_hw_addr_set(ndev, mac);
	} else if (device_get_ethdev_address(&spi->dev, ndev)) {
		eth_hw_addr_random(ndev);
	}

	ndev->netdev_ops = &s2500_netdev_ops;
	ndev->ethtool_ops = &s2500_ethtool_ops;
	ndev->watchdog_timeo = S2500_TX_TIMEOUT;
	ndev->min_mtu = ETH_MIN_MTU;
	ndev->max_mtu = ETH_DATA_LEN;

	/* Configure and validate the MAC-PHY before registering. */
	ret = s2500_hw_init(priv);
	if (ret) {
		dev_err(&spi->dev, "S2500 init failed: %d\n", ret);
		goto free_netdev;
	}

	priv->irq = spi->irq;
	if (priv->irq > 0) {
		ret = request_irq(priv->irq, s2500_isr, 0,
				  dev_name(&spi->dev), priv);
		if (ret) {
			dev_warn(&spi->dev,
				 "cannot request IRQ %d (%d), polling mode\n",
				 priv->irq, ret);
			priv->irq = 0;
		}
	}

	priv->thread = kthread_run(s2500_thread, priv, "s2500/%s",
				   dev_name(&spi->dev));
	if (IS_ERR(priv->thread)) {
		ret = PTR_ERR(priv->thread);
		priv->thread = NULL;
		goto free_irq;
	}

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(&spi->dev, "register_netdev failed: %d\n", ret);
		goto stop_thread;
	}
	ndev->irq = priv->irq;

	ret = sysfs_create_group(&ndev->dev.kobj, s2500_groups[0]);
	if (ret)
		dev_warn(&spi->dev, "sysfs group failed: %d\n", ret);

	netdev_info(ndev, "T30HM1TS2500 at %s, MAC %pM, PLCA %s id %u ncnt %u totmr %u, irq %d\n",
		    dev_name(&spi->dev), ndev->dev_addr,
		    priv->plca_en ? "on" : "off", priv->plca_id,
		    priv->plca_ncnt, priv->plca_totmr, priv->irq);
	return 0;

stop_thread:
	kthread_stop(priv->thread);
	priv->thread = NULL;
free_irq:
	if (priv->irq)
		free_irq(priv->irq, priv);
free_netdev:
	free_netdev(ndev);
	return ret;
}

static void s2500_remove(struct spi_device *spi)
{
	struct s2500_priv *priv = spi_get_drvdata(spi);
	struct net_device *ndev = priv->ndev;

	unregister_netdev(ndev);
	sysfs_remove_group(&ndev->dev.kobj, s2500_groups[0]);
	if (priv->thread)
		kthread_stop(priv->thread);
	if (priv->irq)
		free_irq(priv->irq, priv);
	skb_queue_purge(&priv->txq);
	s2500_rx_drop(priv);
	free_netdev(ndev);
}

static const struct of_device_id s2500_of_match[] = {
	{ .compatible = "onsemi,t30hm1ts2500" },
	{ .compatible = "onsemi,s2500" },
	/* Bring-up fallback: the RK3506 EVM device tree currently carries a
	 * lan865x node on the same chip select.  The in-tree lan865x driver
	 * probes first, so users must unbind it before binding this one; the
	 * proper long-term solution is the DTS node added by this repository.
	 */
	{ .compatible = "microchip,lan8651" },
	{ }
};
MODULE_DEVICE_TABLE(of, s2500_of_match);

static const struct spi_device_id s2500_id[] = {
	{ .name = "s2500" },
	{ }
};
MODULE_DEVICE_TABLE(spi, s2500_id);

static struct spi_driver s2500_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = s2500_of_match,
	},
	.probe = s2500_probe,
	.remove = s2500_remove,
	.id_table = s2500_id,
};
module_spi_driver(s2500_driver);

MODULE_DESCRIPTION("onsemi T30HM1TS2500 10BASE-T1S MAC-PHY driver");
MODULE_AUTHOR("S2500 project");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
