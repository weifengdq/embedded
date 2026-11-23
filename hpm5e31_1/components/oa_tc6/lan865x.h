/*
 * Microchip LAN8650/LAN8651 10BASE-T1S MAC-PHY (with internal PHY) driver
 * for HPM5E31, on top of the oa_tc6 TC6 serial interface.
 *
 * Ported from the Linux drivers (lan865x.c + microchip_t1s.c) to bare metal.
 * PHY analog configuration follows Microchip AN1760 rev F (DS60001760G) for
 * LAN865x Rev.B0/B1.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef COMPONENTS_OA_TC6_LAN865X_H
#define COMPONENTS_OA_TC6_LAN865X_H

#include <stdint.h>
#include <stdbool.h>
#include "oa_tc6.h"

#ifdef __cplusplus
extern "C" {
#endif

/* expected clause 22 PHY identifier (LAN865x Rev.B0/B1) */
#define LAN865X_PHY_ID          0x0007C1B3U

/* ---------------- MAC registers (memory map selector 1) ------------------- */
#define LAN865X_REG_MAC_NET_CTL     0x00010000U
#define MAC_NET_CTL_TXEN            (1U << 3)
#define MAC_NET_CTL_RXEN            (1U << 2)

#define LAN865X_REG_MAC_NET_CFG     0x00010001U
#define MAC_NET_CFG_PROMISCUOUS     (1U << 4)
#define MAC_NET_CFG_MULTICAST       (1U << 6)
#define MAC_NET_CFG_UNICAST         (1U << 7)

#define LAN865X_REG_MAC_L_HASH      0x00010020U
#define LAN865X_REG_MAC_H_HASH      0x00010021U
#define LAN865X_REG_MAC_L_SADDR1    0x00010022U
#define LAN865X_REG_MAC_H_SADDR1    0x00010023U

/* ---------------- PHY registers (MMD 31 / MMS 4) -------------------------- */
#define LAN86XX_REG_STS1            0x0018U  /* PHY interrupt status, read clear */
#define LAN86XX_STS1_LINK_STS_CHANGED (1U << 13)
#define LAN86XX_STS1_PLCA_STS_CHANGED (1U << 11)
#define LAN86XX_REG_IMSK1           0x001CU  /* 0 = interrupt enabled */
#define LAN86XX_REG_COL_DET_CTRL0   0x0087U
#define COL_DET_ENABLE              (1U << 15)
#define COL_DET_DISABLE             0x0000U

/* LAN8650/1 configuration parameter indirect access (AN1760) */
#define LAN865X_REG_CFGPARAM_ADDR   0x00D8U
#define LAN865X_REG_CFGPARAM_DATA   0x00D9U
#define LAN865X_REG_CFGPARAM_CTRL   0x00DAU
#define LAN865X_CFGPARAM_READ_EN    (1U << 1)

/* ---------------- OPEN Alliance TC14 PLCA registers (MMD 31) -------------- */
#define MDIO_OATC14_PLCA_IDVER      0xCA00U
#define MDIO_OATC14_PLCA_CTRL0      0xCA01U
#define MDIO_OATC14_PLCA_CTRL1      0xCA02U
#define MDIO_OATC14_PLCA_STATUS     0xCA03U
#define MDIO_OATC14_PLCA_TOTMR      0xCA04U
#define MDIO_OATC14_PLCA_BURST      0xCA05U

#define PLCA_CTRL0_EN               (1U << 15)
#define PLCA_CTRL0_RST              (1U << 14)
#define PLCA_STATUS_PST             (1U << 15)
#define PLCA_IDVER_IDM_MASK         0xFF00U
#define PLCA_IDVER_IDM_OATC14       0x0A00U

/** PLCA configuration tuple (also used as read-back result) */
typedef struct {
    bool     enabled;
    uint8_t  node_id;
    uint8_t  node_cnt;
    uint8_t  to_tmr;
    uint8_t  burst_cnt;
    uint8_t  burst_tmr;
    uint16_t status;        /* PLCA STATUS register read-back */
} lan865x_plca_cfg_t;

/* ---------------- bring-up ------------------------------------------------ */
/** hard reset through PA19 (nRST), then wait for STATUS0.RESETC */
int lan865x_hw_reset(oa_tc6_t *tc6);
/** read clause 22 PHY id (reg 2/3) */
int lan865x_phy_id(oa_tc6_t *tc6, uint32_t *id);
/** AN1760 rev F configuration parameters for LAN865x Rev.B0/B1 */
int lan865x_phy_config_init(oa_tc6_t *tc6);
/** write the MAC address registers */
int lan865x_mac_set_addr(oa_tc6_t *tc6, const uint8_t mac[6]);
/** read back the MAC address registers */
int lan865x_mac_get_addr(oa_tc6_t *tc6, uint8_t mac[6]);
/** enable/disable the MAC transmit and receive paths */
int lan865x_mac_enable(oa_tc6_t *tc6, bool tx_enable, bool rx_enable);
/** promiscuous (true) or unicast-only (false) receive filtering */
int lan865x_mac_set_promisc(oa_tc6_t *tc6, bool enable);
/** write a complete PLCA configuration and read it back */
int lan865x_plca_configure(oa_tc6_t *tc6, lan865x_plca_cfg_t *cfg);
/** read the current PLCA configuration/status */
int lan865x_plca_read(oa_tc6_t *tc6, lan865x_plca_cfg_t *cfg);
/** PLCA status register only (bit 15 = PST = coordinator/beacon seen) */
int lan865x_plca_status(oa_tc6_t *tc6, uint16_t *status);
/** read MMD31 interrupt status register STS1 (read to clear) */
int lan865x_phy_sts1(oa_tc6_t *tc6, uint16_t *sts1);
/** mute/unmute the PHY interrupt sources used by this driver */
int lan865x_phy_irq_enable(oa_tc6_t *tc6, bool enable);
/**
 * @brief complete bring-up: SPI already initialised, does reset + TC6 init +
 *        AN1760 PHY config + ZARFE + MAC address + MAC enable.
 */
int lan865x_full_init(oa_tc6_t *tc6, const uint8_t mac[6]);

/** human readable name of the last error code returned by this driver */
const char *lan865x_strerror(int err);

#ifdef __cplusplus
}
#endif
#endif /* COMPONENTS_OA_TC6_LAN865X_H */
