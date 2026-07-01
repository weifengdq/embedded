/**
  ******************************************************************************
  * @file    lan8720_driver.h
  * @brief   LAN8720A 10/100 Ethernet PHY Driver Header
  * @details Driver for Microchip LAN8720A RMII Ethernet PHY.
  *          Accesses PHY through LAN9370's internal MIIM (MDIO) master via SPI.
  *          STM32 GPIO MDIO pins (PB5/PB6) are NOT used with new wiring.
  *          - PHY Address: 1 (PHYAD0 strapped high, common on many boards)
  *          - Interface: RMII only (no MII support)
  *          - Speed: 10/100 Mbps
  *          - MDIO bus: LAN9370 MDC/MDIO pins → LAN8720A MDC/MDIO pins
  *
  * Reference:
  *   - LAN8720A datasheet (Microchip/SMSC)
  *   - libopencm3: include/libopencm3/ethernet/phy_lan87xx.h
  *   - Linux kernel: drivers/net/phy/smsc.c
  ******************************************************************************
  */

#ifndef __LAN8720_DRIVER_H
#define __LAN8720_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* =============================================================================
 * PHY Constants
 * ===========================================================================*/

/* PHY ID for LAN8720A (OUI=0x0007C0, Model=0x0F or 0x1F, Rev varies) */
#define LAN8720_PHY_ID1             0x0007
#define LAN8720_PHY_ID2_MASK        0xFFF0
#define LAN8720_PHY_ID2             0xC0F0
#define LAN8720_PHY_ID2_ALT         0xC1F0

/* Default PHY address: try addr 1 first (PHYAD0 pulled high), fallback to 0 */
#define LAN8720_PHY_ADDR_DEFAULT    1
#define LAN8720_PHY_ADDR_ALT        0

/* =============================================================================
 * Standard MII Registers (IEEE 802.3 Clause 22)
 * ===========================================================================*/
#define MII_BMCR                    0x00    /* Basic Mode Control Register */
#define MII_BMSR                    0x01    /* Basic Mode Status Register */
#define MII_PHYSID1                 0x02    /* PHY Identifier 1 */
#define MII_PHYSID2                 0x03    /* PHY Identifier 2 */
#define MII_ANAR                    0x04    /* Auto-Negotiation Advertisement */
#define MII_ANLPAR                  0x05    /* Auto-Negotiation Link Partner Ability */
#define MII_ANER                    0x06    /* Auto-Negotiation Expansion */

/* BMCR bit definitions */
#define BMCR_RESET                  (1 << 15)
#define BMCR_LOOPBACK               (1 << 14)
#define BMCR_SPEED_SEL_LSB          (1 << 13)  /* 1=100M */
#define BMCR_AN_ENABLE              (1 << 12)
#define BMCR_POWER_DOWN             (1 << 11)
#define BMCR_ISOLATE                (1 << 10)
#define BMCR_RESTART_AN             (1 << 9)
#define BMCR_DUPLEX_MODE            (1 << 8)

/* BMSR bit definitions */
#define BMSR_100BASE_TX_FD          (1 << 14)
#define BMSR_100BASE_TX_HD          (1 << 13)
#define BMSR_10BASE_T_FD            (1 << 12)
#define BMSR_10BASE_T_HD            (1 << 11)
#define BMSR_MF_PREAMBLE_SUPPR      (1 << 6)
#define BMSR_AN_COMPLETE            (1 << 5)
#define BMSR_REMOTE_FAULT           (1 << 4)
#define BMSR_AN_CAPABLE             (1 << 3)
#define BMSR_LINK_STATUS            (1 << 2)
#define BMSR_JABBER_DETECT          (1 << 1)

/* ANAR bit definitions */
#define ANAR_NEXT_PAGE              (1 << 15)
#define ANAR_REMOTE_FAULT           (1 << 13)
#define ANAR_ASYM_PAUSE             (1 << 11)
#define ANAR_PAUSE                  (1 << 10)
#define ANAR_100BASE_TX_FD          (1 << 8)
#define ANAR_100BASE_TX_HD          (1 << 7)
#define ANAR_10BASE_T_FD            (1 << 6)
#define ANAR_10BASE_T_HD            (1 << 5)
#define ANAR_SELECTOR_8023          0x0001

/* =============================================================================
 * LAN8720A Specific Registers
 * ===========================================================================*/
#define LAN87XX_SM                  0x12    /* Special Modes Register */
#define LAN87XX_SCSR                0x1F    /* Special Control/Status Register */

/* SM register bit definitions */
#define LAN87XX_SM_MIIMODE          (1 << 14)   /* 1=RMII mode (LAN8720A only RMII, must be 1) */
#define LAN87XX_SM_MODE_SHIFT       5
#define LAN87XX_SM_MODE_MASK        (7 << 5)
#define LAN87XX_SM_MODE_10HD        (0 << 5)
#define LAN87XX_SM_MODE_10FD        (1 << 5)
#define LAN87XX_SM_MODE_100HD       (2 << 5)
#define LAN87XX_SM_MODE_100FD       (3 << 5)
#define LAN87XX_SM_MODE_100HD_AN    (4 << 5)
#define LAN87XX_SM_MODE_ALL         (7 << 5)
#define LAN87XX_SM_PHYAD_MASK       0x1F        /* Bits 4:0 = PHY address */

/* SCSR register bit definitions */
#define LAN87XX_SCSR_AUTODONE       (1 << 12)
#define LAN87XX_SCSR_ENABLE4B5B     (1 << 6)    /* Must write 1 for LAN8720A */
#define LAN87XX_SCSR_SPEED_SHIFT    2
#define LAN87XX_SCSR_SPEED_MASK     (7 << 2)
#define LAN87XX_SCSR_SPEED_10HD     (1 << 2)
#define LAN87XX_SCSR_SPEED_100HD    (2 << 2)
#define LAN87XX_SCSR_SPEED_10FD     (5 << 2)
#define LAN87XX_SCSR_SPEED_100FD    (6 << 2)

/* =============================================================================
 * Type Definitions
 * ===========================================================================*/
typedef enum {
    LAN8720_OK = 0,
    LAN8720_ERROR,
    LAN8720_TIMEOUT,
    LAN8720_NO_PHY
} LAN8720_Ret_t;

typedef struct {
    bool linkUp;
    bool fullDuplex;
    bool speed100M;
    bool anComplete;
  bool registerAccess;
    uint16_t phyId1;
    uint16_t phyId2;
    uint8_t phyAddr;
} LAN8720_Status_t;

/* =============================================================================
 * Function Prototypes
 * ===========================================================================*/

/**
 * @brief Auto-detect and initialize LAN8720A PHY on the shared MDIO bus
 * @return LAN8720_OK on success
 */
LAN8720_Ret_t LAN8720_Init(void);

/**
 * @brief Print PHY status
 */
void LAN8720_PrintStatus(void);

/**
 * @brief Get PHY link status
 * @param status Pointer to store status info
 * @return LAN8720_OK on success
 */
LAN8720_Ret_t LAN8720_GetStatus(LAN8720_Status_t *status);

/**
 * @brief Check if link is up
 * @return true if link up
 */
bool LAN8720_IsLinkUp(void);

#ifdef __cplusplus
}
#endif

#endif /* __LAN8720_DRIVER_H */
