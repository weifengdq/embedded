/**
  ******************************************************************************
  * @file    lan8720_driver.c
  * @brief   LAN8720A 10/100 Ethernet PHY Driver Implementation
  * @details Accesses external PHY through LAN9370's internal MIIM (MDIO) master
  *          via SPI. STM32 GPIO MDIO pins (PB5/PB6) are NOT used — the
  *          LAN9370's MDC/MDIO pins connect directly to LAN8720A.
  *
  * Hardware configuration:
  *   - RMII interface to LAN9370 Port 5
  *   - 50MHz reference clock from external oscillator
  *   - MDC/MDIO: LAN9370 MDIO master ↔ LAN8720A (STM32 PB5/PB6 disconnected)
  *   - PHY address: auto-detected (tries addr 1, then addr 0)
  *
  * Reference:
  *   - LAN8720A datasheet (Microchip/SMSC)
  *   - libopencm3: include/libopencm3/ethernet/phy_lan87xx.h
  *   - Linux kernel: drivers/net/phy/smsc.c
  ******************************************************************************
  */

#include "lan8720_driver.h"
#include "lan9370_driver.h"
#include "main.h"
#include <stdio.h>

#define LAN8720_P5_STATUS_ADDR     ((uint16_t)(0x5000U + 0x0030U))

/* =============================================================================
 * Private Variables
 * ===========================================================================*/
static bool s_initialized = false;
static bool s_registerAccess = false;
static uint8_t s_phyAddr = 0;
static uint16_t s_phyId1 = 0;
static uint16_t s_phyId2 = 0;

/* =============================================================================
 * Private Helper Functions
 * ===========================================================================*/

/**
 * @brief Read a PHY register via LAN9370 MIIM master (SPI -> MDIO)
 */
static LAN8720_Ret_t phy_read(uint8_t regAddr, uint16_t *data)
{
    (void)regAddr;
    (void)data;

    if (!s_registerAccess) {
        return LAN8720_ERROR;
    }

    if (LAN9370_MIIM_Read(s_phyAddr, regAddr, data) != LAN9370_OK) {
        return LAN8720_ERROR;
    }
    return LAN8720_OK;
}

/**
 * @brief Write a PHY register via LAN9370 MIIM master (SPI -> MDIO)
 */
static LAN8720_Ret_t phy_write(uint8_t regAddr, uint16_t data)
{
    (void)regAddr;
    (void)data;

    if (!s_registerAccess) {
        return LAN8720_ERROR;
    }

    if (LAN9370_MIIM_Write(s_phyAddr, regAddr, data) != LAN9370_OK) {
        return LAN8720_ERROR;
    }
    return LAN8720_OK;
}

static bool lan8720_get_port5_status(uint8_t *statusReg)
{
    if (statusReg == NULL) {
        return false;
    }

    return LAN9370_SPI_ReadReg8(LAN8720_P5_STATUS_ADDR, statusReg) == LAN9370_SPI_OK;
}

static void lan8720_fill_status_from_port5(LAN8720_Status_t *status, uint8_t p5status)
{
    if (status == NULL) {
        return;
    }

    status->linkUp = ((p5status & PORT_INTF_SPEED_MASK) == PORT_INTF_SPEED_10) ||
                     ((p5status & PORT_INTF_SPEED_MASK) == PORT_INTF_SPEED_100) ||
                     ((p5status & PORT_INTF_SPEED_MASK) == PORT_INTF_SPEED_1000);
    status->speed100M = ((p5status & PORT_INTF_SPEED_MASK) == PORT_INTF_SPEED_100);
    status->fullDuplex = (p5status & PORT_INTF_FULL_DUPLEX) != 0U;
    status->anComplete = status->linkUp;
    status->registerAccess = false;
    status->phyId1 = s_phyId1;
    status->phyId2 = s_phyId2;
    status->phyAddr = s_phyAddr;
}

/**
 * @brief Try to detect LAN8720 at a given PHY address via LAN9370 MIIM master
 */
static bool probe_phy(uint8_t addr, uint16_t *id1, uint16_t *id2)
{
    uint16_t tmp1, tmp2;
    int retry;

    /* Retry up to 3 times - MDIO bus may need settling after LAN9370 reset */
    for (retry = 0; retry < 3; retry++) {
        if (retry > 0) {
            HAL_Delay(10);
        }

        if (LAN9370_MIIM_Read(addr, MII_PHYSID1, &tmp1) != LAN9370_OK) {
            continue;
        }
        if (LAN9370_MIIM_Read(addr, MII_PHYSID2, &tmp2) != LAN9370_OK) {
            continue;
        }

        /* Check for valid PHY ID (not all zeros or all ones) */
        if (tmp1 == 0x0000 || tmp1 == 0xFFFF ||
            tmp2 == 0x0000 || tmp2 == 0xFFFF) {
            continue;
        }

        /* Check OUI matches LAN8720A: 0x0007C0 */
        if (tmp1 != LAN8720_PHY_ID1) {
            printf("[LAN8720] probe addr %d: ID1=0x%04X (expected 0x%04X) - skip\r\n",
                   addr, tmp1, LAN8720_PHY_ID1);
            return false;
        }

        /* Check OUI bits in PHYID2 (bits 15:10 should be 0x30 for SMSC OUI) */
        if ((tmp2 & 0xFC00) != 0xC000) {
            printf("[LAN8720] probe addr %d: ID2=0x%04X OUI mismatch\r\n", addr, tmp2);
            return false;
        }

        printf("[LAN8720] probe addr %d: ID=0x%04X%04X - MATCH!\r\n",
               addr, tmp1, tmp2);
        *id1 = tmp1;
        *id2 = tmp2;
        return true;
    }

    return false;
}

/* =============================================================================
 * Public Functions
 * ===========================================================================*/

/**
 * @brief Auto-detect and initialize LAN8720A PHY
 *
 * NOTE: LAN9370 does not expose a SPI-to-MDIO bridge. The MIIM registers
 * at 0x006C-0x006E were assumed but do not exist in this chip family.
 * GPIO bitbang MDIO (PB5/PB6) is also not connected to LAN8720 MDIO on
 * this hardware.  As a fallback, PHY presence is inferred from LAN9370
 * Port 5 xMII status (register PORT_CTRL_ADDR(4, REG_PORTn_STATUS)).
 * Port 5 showing 100M/Full confirms LAN8720 is powered and linked via RMII.
 */
LAN8720_Ret_t LAN8720_Init(void)
{
    printf("[LAN8720] Probing PHY via LAN9370 MIIM master...\r\n");
    s_registerAccess = false;

    /* Try to detect LAN8720 at default address (1), then alternate (0) */
    if (probe_phy(LAN8720_PHY_ADDR_DEFAULT, &s_phyId1, &s_phyId2)) {
        s_phyAddr = LAN8720_PHY_ADDR_DEFAULT;
        s_registerAccess = true;
    } else if (probe_phy(LAN8720_PHY_ADDR_ALT, &s_phyId1, &s_phyId2)) {
        s_phyAddr = LAN8720_PHY_ADDR_ALT;
        s_registerAccess = true;
    } else {
        /* Try scanning all addresses 0-31 */
        printf("[LAN8720] MIIM probe failed. Scanning all addresses...\r\n");
        bool found = false;
        for (uint8_t addr = 0; addr <= 31; addr++) {
            if (addr == LAN8720_PHY_ADDR_DEFAULT || addr == LAN8720_PHY_ADDR_ALT) {
                continue; /* Already tried */
            }
            if (probe_phy(addr, &s_phyId1, &s_phyId2)) {
                s_phyAddr = addr;
                found = true;
                s_registerAccess = true;
                break;
            }
        }
        if (!found) {
            /* ----------------------------------------------------------------
             * Fallback: LAN9370 has no SPI→MDIO bridge — MIIM registers
             * 0x006C-0x006E do not exist in KSZ9370/LAN9370.
             * Infer PHY presence from Port 5 xMII status register.
             * Port 5 base = (5 << 12) = 0x5000, status offset = 0x0030.
             * Bits: [3]=100M, [2]=Full-Duplex, [0]=RxFlowCtrl
             * Seeing 0x0D (100M+Full+RxFC) confirms RMII link is active.
             * ----------------------------------------------------------------
             */
            {
                uint8_t p5status = 0;
                bool port5_up = false;

                if (lan8720_get_port5_status(&p5status)) {
                    printf("[LAN8720] Port5 xMII status = 0x%02X\r\n", p5status);
                    if (((p5status & PORT_INTF_SPEED_MASK) == PORT_INTF_SPEED_10) ||
                        ((p5status & PORT_INTF_SPEED_MASK) == PORT_INTF_SPEED_100) ||
                        ((p5status & PORT_INTF_SPEED_MASK) == PORT_INTF_SPEED_1000)) {
                        port5_up = true;
                    }
                }

                if (port5_up) {
                    /* External PHY is reachable only through the switch's
                     * own SMI_OUT state machine. The MCU cannot proxy clause-22
                     * reads over SPI on LAN9370, so fall back to derived Port5
                     * status and keep registerAccess disabled. */
                    s_phyAddr = LAN8720_PHY_ADDR_DEFAULT;
                    s_phyId1  = 0x0000;
                    s_phyId2  = 0x0000;
                    s_registerAccess = false;
                    s_initialized = true;
                    printf("[LAN8720] PHY detected via Port5 xMII status\r\n");
                    printf("[LAN8720] PHY addr = %d (strapped, 0x5302=0x01)\r\n", s_phyAddr);
                    printf("[LAN8720] NOTE: external PHY registers are not SPI-accessible on LAN9370\r\n");
                    return LAN8720_OK;
                }

                printf("[LAN8720] ERROR: No LAN8720 detected (MIIM + Port5 status both fail)\r\n");
                printf("[LAN8720] Port5 status = 0x%02X (expect 0x0C or 0x0D for 100M Full)\r\n",
                       p5status);
            }
            return LAN8720_NO_PHY;
        }
    }

    printf("[LAN8720] Found at PHY address %d, ID: 0x%04X%04X\r\n",
           s_phyAddr, s_phyId1, s_phyId2);

    /* Step 1: Reset PHY */
    printf("[LAN8720] Resetting PHY...\r\n");
    phy_write(MII_BMCR, BMCR_RESET);

    /* Wait for reset complete (max 500ms per datasheet) */
    {
        uint32_t start = HAL_GetTick();
        uint16_t bmcr;
        bool resetDone = false;
        do {
            HAL_Delay(10);
            if (phy_read(MII_BMCR, &bmcr) == LAN8720_OK) {
                if (!(bmcr & BMCR_RESET)) {
                    resetDone = true;
                    break;
                }
            }
        } while ((HAL_GetTick() - start) < 600);

        if (!resetDone) {
            printf("[LAN8720] WARNING: PHY reset timeout\r\n");
        } else {
            printf("[LAN8720] Reset complete after %lu ms\r\n",
                   (unsigned long)(HAL_GetTick() - start));
        }
    }

    /* Step 2: Configure Special Modes register
     * - Bit 14 (MIIMODE): Must be 1 for RMII mode (LAN8720A only supports RMII)
     * - Mode bits[7:5]: 111 = All capable, auto-negotiation enabled
     */
    {
        uint16_t sm;
        if (phy_read(LAN87XX_SM, &sm) == LAN8720_OK) {
            printf("[LAN8720] SM register (before): 0x%04X\r\n", sm);

            /* Ensure RMII mode is set */
            sm |= LAN87XX_SM_MIIMODE;

            /* Set mode to "All capable" for auto-negotiation */
            sm &= ~LAN87XX_SM_MODE_MASK;
            sm |= LAN87XX_SM_MODE_ALL;

            phy_write(LAN87XX_SM, sm);

            if (phy_read(LAN87XX_SM, &sm) == LAN8720_OK) {
                printf("[LAN8720] SM register (after):  0x%04X\r\n", sm);
            }
        }
    }

    /* Step 3: Configure SCSR register
     * - Bit 6 (ENABLE4B5B): Must be 1 for LAN8720A
     */
    {
        uint16_t scsr;
        if (phy_read(LAN87XX_SCSR, &scsr) == LAN8720_OK) {
            printf("[LAN8720] SCSR register (before): 0x%04X\r\n", scsr);
            scsr |= LAN87XX_SCSR_ENABLE4B5B;
            phy_write(LAN87XX_SCSR, scsr);
            if (phy_read(LAN87XX_SCSR, &scsr) == LAN8720_OK) {
                printf("[LAN8720] SCSR register (after):  0x%04X\r\n", scsr);
            }
        }
    }

    /* Step 4: Advertise 10/100 full/half duplex capabilities */
    {
        uint16_t anar = ANAR_100BASE_TX_FD | ANAR_100BASE_TX_HD |
                        ANAR_10BASE_T_FD   | ANAR_10BASE_T_HD |
                        ANAR_SELECTOR_8023;
        phy_write(MII_ANAR, anar);
        printf("[LAN8720] ANAR set to 0x%04X (100FD/100HD/10FD/10HD)\r\n", anar);
    }

    /* Step 5: Enable auto-negotiation and restart */
    {
        /* Read BMCR, clear power-down and isolate, set AN enable and restart */
        uint16_t bmcr;
        if (phy_read(MII_BMCR, &bmcr) == LAN8720_OK) {
            bmcr &= ~(BMCR_POWER_DOWN | BMCR_ISOLATE | BMCR_LOOPBACK);
            bmcr |= BMCR_AN_ENABLE | BMCR_RESTART_AN;
            phy_write(MII_BMCR, bmcr);
            printf("[LAN8720] BMCR set to 0x%04X (AN enabled, restarting)\r\n", bmcr);
        }
    }

    /* Step 6: Wait for auto-negotiation to complete */
    printf("[LAN8720] Waiting for auto-negotiation...\r\n");
    {
        uint32_t start = HAL_GetTick();
        uint16_t bmsr;
        bool anDone = false;
        do {
            HAL_Delay(100);
            if (phy_read(MII_BMSR, &bmsr) == LAN8720_OK) {
                if (bmsr & BMSR_AN_COMPLETE) {
                    anDone = true;
                    break;
                }
            }
        } while ((HAL_GetTick() - start) < 5000);

        if (anDone) {
            printf("[LAN8720] Auto-negotiation complete after %lu ms\r\n",
                   (unsigned long)(HAL_GetTick() - start));
            if (bmsr & BMSR_LINK_STATUS) {
                printf("[LAN8720] Link: UP\r\n");
            } else {
                printf("[LAN8720] Link: DOWN (check cable to PC)\r\n");
            }
        } else {
            printf("[LAN8720] WARNING: Auto-negotiation timeout (check cable)\r\n");
        }
    }

    s_initialized = true;
    return LAN8720_OK;
}

/**
 * @brief Print PHY status
 */
void LAN8720_PrintStatus(void)
{
    LAN8720_Status_t status;

    if (!s_initialized) {
        printf("[LAN8720] Not initialized\r\n");
        return;
    }

    if (LAN8720_GetStatus(&status) != LAN8720_OK) {
        printf("[LAN8720] Status unavailable\r\n");
        return;
    }

    printf("=== LAN8720 PHY Status (addr=%d) ===\r\n", s_phyAddr);
    if (status.registerAccess) {
        printf("  PHY ID: 0x%04X%04X\r\n", status.phyId1, status.phyId2);
        printf("  Access: direct PHY register access\r\n");
    } else {
        printf("  PHY ID: unreadable via SPI on LAN9370\r\n");
        printf("  Access: derived from Port5 xMII status\r\n");
    }

    printf("  Link:        %s\r\n", status.linkUp ? "UP" : "DOWN");
    printf("  AN Complete: %s\r\n", status.anComplete ? "YES" : "NO/UNKNOWN");
    printf("  Speed:       %s\r\n",
           status.speed100M ? (status.fullDuplex ? "100M Full" : "100M Half")
                            : (status.fullDuplex ? "10M Full/Unknown" : "10M Half/Unknown"));

    printf("=====================================\r\n");
}

/**
 * @brief Get PHY link status
 */
LAN8720_Ret_t LAN8720_GetStatus(LAN8720_Status_t *status)
{
    uint16_t bmsr, scsr;
    uint8_t p5status;

    if (!s_initialized || status == NULL) {
        return LAN8720_ERROR;
    }

    if (!s_registerAccess) {
        if (!lan8720_get_port5_status(&p5status)) {
            return LAN8720_ERROR;
        }

        lan8720_fill_status_from_port5(status, p5status);
        return LAN8720_OK;
    }

    if (phy_read(MII_BMSR, &bmsr) != LAN8720_OK) {
        return LAN8720_ERROR;
    }

    status->linkUp = (bmsr & BMSR_LINK_STATUS) ? true : false;
    status->anComplete = (bmsr & BMSR_AN_COMPLETE) ? true : false;
    status->registerAccess = true;
    status->phyId1 = s_phyId1;
    status->phyId2 = s_phyId2;
    status->phyAddr = s_phyAddr;

    if (phy_read(LAN87XX_SCSR, &scsr) == LAN8720_OK) {
        uint8_t speed = (scsr & LAN87XX_SCSR_SPEED_MASK) >> LAN87XX_SCSR_SPEED_SHIFT;
        status->speed100M = (speed == 2 || speed == 6);
        status->fullDuplex = (speed >= 5);
    } else {
        status->speed100M = false;
        status->fullDuplex = false;
    }

    return LAN8720_OK;
}

/**
 * @brief Check if link is up
 */
bool LAN8720_IsLinkUp(void)
{
    LAN8720_Status_t status;

    if (!s_initialized) {
        return false;
    }

    if (LAN8720_GetStatus(&status) != LAN8720_OK) {
        return false;
    }

    return status.linkUp;
}
