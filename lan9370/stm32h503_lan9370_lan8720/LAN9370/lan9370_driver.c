/**
  ******************************************************************************
  * @file    lan9370_driver.c
  * @brief   LAN9370 5-Port 100BASE-T1 Ethernet Switch Driver Implementation
  ******************************************************************************
  */

#include "lan9370_driver.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

/* =============================================================================
 * Private Definitions
 * ===========================================================================*/
#define LAN9370_RESET_PORT          GPIOB
#define LAN9370_RESET_PIN           GPIO_PIN_7

#define LAN9370_RESET_PULSE_MS      10
#define LAN9370_RESET_DELAY_MS      100
#define LAN9370_VPHY_BUSY_TIMEOUT_MS 100
#define LAN9370_MIB_READ_TIMEOUT_MS 100

/* Placeholder PTP control bits in a shared global register (board-validated experimentally). */
#define LAN9370_PTP_CTRL_REG_ADDR      0x0200U
#define LAN9370_PTP_ENABLE_BIT         (1U << 0)
#define LAN9370_GPTP_ENABLE_BIT        (1U << 1)

/* =============================================================================
 * Private Variables
 * ===========================================================================*/
static bool isInitialized = false;
#define LAN9370_STATIC_MAC_SHADOW_SIZE 32
static LAN9370_StaticMacEntry_t s_staticMacShadow[LAN9370_STATIC_MAC_SHADOW_SIZE];
static uint8_t s_staticMacCount = 0;

static LAN9370_Status_t LAN9370_VphyWaitReady(void)
{
    uint16_t ctrl;
    uint32_t start = HAL_GetTick();

    do {
        if (LAN9370_SPI_ReadReg16(REG_VPHY_IND_CTRL__2, &ctrl) != LAN9370_SPI_OK) {
            return LAN9370_ERROR;
        }

        if ((ctrl & VPHY_IND_BUSY) == 0U) {
            return LAN9370_OK;
        }
    } while ((HAL_GetTick() - start) < LAN9370_VPHY_BUSY_TIMEOUT_MS);

    return LAN9370_TIMEOUT;
}

static LAN9370_Status_t LAN9370_EnableSpiIndirectVphyAccess(void)
{
    uint8_t lsb;

    /* Use byte-write to set bit 12 of VPHY_SPECIAL_CTRL (16-bit register).
     * Register is big-endian: addr 0x077C = MSB, 0x077D = LSB.
     * Bit 12 corresponds to bit 4 of the MSB byte at 0x077C.
     * WriteReg16 may have byte-order issues on some register types;
     * byte-level write is more reliable here. */
    if (LAN9370_SPI_ReadReg8(REG_VPHY_SPECIAL_CTRL__2, &lsb) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    lsb |= 0x10;  /* Set bit 4 of MSB = bit 12 of 16-bit value */

    if (LAN9370_SPI_WriteReg8(REG_VPHY_SPECIAL_CTRL__2, lsb) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_OK;
}

static LAN9370_Status_t LAN9370_SetVphyIndirectAddress(LAN9370_Port_t port, uint8_t regAddr)
{
    uint8_t phyIndex;
    uint16_t phyRegAddr;

    if (port < LAN9370_PORT_1 || port > LAN9370_PORT_4) {
        return LAN9370_INVALID_PARAM;
    }

    phyIndex = (uint8_t)(port - 1U);
    phyRegAddr = PORT_CTRL_ADDR(phyIndex,
                                (uint16_t)(REG_PORT_T1_PHY_CTRL_BASE + ((uint16_t)regAddr << 2)));

    if (LAN9370_SPI_WriteReg16(REG_VPHY_IND_ADDR__2, phyRegAddr) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_OK;
}

/* =============================================================================
 * Private Functions
 * ===========================================================================*/

/**
 * @brief Decode chip ID to name
 */
static void DecodeChipName(uint32_t chipId, char *name, size_t len)
{
    switch (chipId & 0xFFFFFF00) {
        case 0x00937000:
            snprintf(name, len, "LAN9370");
            break;
        case 0x00937100:
            snprintf(name, len, "LAN9371");
            break;
        case 0x00937200:
            snprintf(name, len, "LAN9372");
            break;
        case 0x00937300:
            snprintf(name, len, "LAN9373");
            break;
        case 0x00937400:
            snprintf(name, len, "LAN9374");
            break;
        default:
            snprintf(name, len, "Unknown (0x%08lX)", chipId);
            break;
    }
}

/* =============================================================================
 * Public Functions - Initialization
 * ===========================================================================*/

/**
 * @brief Initialize LAN9370
 */
LAN9370_Status_t LAN9370_Init(SPI_HandleTypeDef *hspi)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Initialize SPI interface */
    if (LAN9370_SPI_Init(hspi) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    /* Initialize SMI interface */
    if (LAN9370_SMI_Init() != LAN9370_SMI_OK) {
        return LAN9370_ERROR;
    }

    /* Configure RESET pin (PB7) as output */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitStruct.Pin = LAN9370_RESET_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LAN9370_RESET_PORT, &GPIO_InitStruct);

    /* Keep reset deasserted initially */
    HAL_GPIO_WritePin(LAN9370_RESET_PORT, LAN9370_RESET_PIN, GPIO_PIN_SET);

    isInitialized = true;

    /* Perform hardware reset */
    if (LAN9370_HardwareReset() != LAN9370_OK) {
        return LAN9370_ERROR;
    }

    /* Extra VPHY enable using byte-write (redundant but more reliable) */
    {
        uint8_t vphyMsb;
        if (LAN9370_SPI_ReadReg8(0x077C, &vphyMsb) == LAN9370_SPI_OK) {
            LAN9370_SPI_WriteReg8(0x077C, vphyMsb | 0x10);
        }
    }

    return LAN9370_OK;
}

/**
 * @brief Hardware reset via nRESET pin
 */
LAN9370_Status_t LAN9370_HardwareReset(void)
{
    uint8_t gctrl0;

    /* Assert reset (active low) */
    HAL_GPIO_WritePin(LAN9370_RESET_PORT, LAN9370_RESET_PIN, GPIO_PIN_RESET);
    HAL_Delay(LAN9370_RESET_PULSE_MS);

    /* Deassert reset */
    HAL_GPIO_WritePin(LAN9370_RESET_PORT, LAN9370_RESET_PIN, GPIO_PIN_SET);
    HAL_Delay(LAN9370_RESET_DELAY_MS);

    /* Enable PHY register access through SPI by clearing SW_PHY_REG_BLOCK. */
    if (LAN9370_SPI_ReadReg8(REG_GLOBAL_CTRL_0, &gctrl0) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }
    gctrl0 &= (uint8_t)(~SW_PHY_REG_BLOCK);
    if (LAN9370_SPI_WriteReg8(REG_GLOBAL_CTRL_0, gctrl0) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    if (LAN9370_EnableSpiIndirectVphyAccess() != LAN9370_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_OK;
}

/**
 * @brief Software reset
 */
LAN9370_Status_t LAN9370_SoftwareReset(void)
{
    if (!isInitialized) {
        return LAN9370_NOT_INITIALIZED;
    }

    /* Write to reset register */
    if (LAN9370_SPI_WriteReg8(REG_SW_RESET, SW_SOFT_RESET) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    /* Wait for reset to complete */
    HAL_Delay(100);

    return LAN9370_OK;
}

/**
 * @brief Get chip information
 */
LAN9370_Status_t LAN9370_GetChipInfo(LAN9370_ChipInfo_t *info)
{
    if (!isInitialized || info == NULL) {
        return LAN9370_INVALID_PARAM;
    }

    uint8_t id0, id1, id2, id3;

    /* Read chip ID (4 bytes) */
    if (LAN9370_SPI_ReadReg8(REG_CHIP_ID0, &id0) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (LAN9370_SPI_ReadReg8(REG_CHIP_ID1, &id1) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (LAN9370_SPI_ReadReg8(REG_CHIP_ID2, &id2) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (LAN9370_SPI_ReadReg8(REG_CHIP_ID3, &id3) != LAN9370_SPI_OK) return LAN9370_ERROR;

    info->chipId = ((uint32_t)id0 << 24) | ((uint32_t)id1 << 16) | 
                   ((uint32_t)id2 << 8) | id3;
    info->revisionId = id3 & 0x0F;

    DecodeChipName(info->chipId, info->chipName, sizeof(info->chipName));

    return LAN9370_OK;
}

/* =============================================================================
 * Public Functions - Port Configuration
 * ===========================================================================*/

/**
 * @brief Enable or disable a port
 */
LAN9370_Status_t LAN9370_SetPortEnable(LAN9370_Port_t port, bool enable)
{
    uint8_t hwPortIndex;
    uint8_t ctrl0Val;
    uint8_t mstpVal;

    if (!isInitialized || port < 1 || port > 5) {
        return LAN9370_INVALID_PARAM;
    }

    hwPortIndex = (uint8_t)port - 1U;

    /* Clear any accidental tail-tag or queue-split state on standalone switch ports. */
    if (LAN9370_SPI_ReadReg8(PORT_CTRL_ADDR(hwPortIndex, REG_PORTn_OP_CTRL0), &ctrl0Val) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }
    ctrl0Val &= (uint8_t)~(PORT_TAIL_TAG_ENABLE | PORT_QUEUE_SPLIT_MASK);
    if (LAN9370_SPI_WriteReg8(PORT_CTRL_ADDR(hwPortIndex, REG_PORTn_OP_CTRL0), ctrl0Val) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    /* Drive actual forwarding state through MSTP control. */
    if (enable) {
        mstpVal = (uint8_t)(PORT_TX_ENABLE | PORT_RX_ENABLE);
    } else {
        mstpVal = 0U;
    }

    if (LAN9370_SPI_WriteReg8(PORT_CTRL_ADDR(hwPortIndex, REG_PORTn_MSTP_STATE), mstpVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_OK;
}

/**
 * @brief Configure T1 Master/Slave mode
 */
LAN9370_Status_t LAN9370_SetT1MasterSlave(LAN9370_Port_t port, LAN9370_T1_Mode_t mode)
{
    if (!isInitialized || port < 1 || port > 4) {
        return LAN9370_INVALID_PARAM;
    }

    /* Read Master-Slave Control register */
    uint16_t regVal;
    if (LAN9370_PHY_ReadReg(port, T1_PHY_MASTER_SLAVE_CTRL, &regVal) != LAN9370_OK) {
        return LAN9370_ERROR;
    }

    /* Enable manual configuration */
    regVal |= T1_PHY_MS_CFG_ENABLE;

    /* Set Master or Slave */
    if (mode == LAN9370_T1_MASTER) {
        regVal |= T1_PHY_MS_CFG_VALUE;
    } else {
        regVal &= ~T1_PHY_MS_CFG_VALUE;
    }

    /* Write back */
    if (LAN9370_PHY_WriteReg(port, T1_PHY_MASTER_SLAVE_CTRL, regVal) != LAN9370_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_GetT1MasterSlave(LAN9370_Port_t port, LAN9370_T1_Mode_t *mode)
{
    uint16_t regVal;

    if (!isInitialized || port < LAN9370_PORT_1 || port > LAN9370_PORT_4 || mode == NULL) {
        return LAN9370_INVALID_PARAM;
    }

    if (LAN9370_PHY_ReadReg(port, T1_PHY_MASTER_SLAVE_CTRL, &regVal) != LAN9370_OK) {
        return LAN9370_ERROR;
    }

    *mode = ((regVal & T1_PHY_MS_CFG_VALUE) != 0U) ? LAN9370_T1_MASTER : LAN9370_T1_SLAVE;
    return LAN9370_OK;
}

/**
 * @brief Get port status
 */
LAN9370_Status_t LAN9370_GetPortStatus(LAN9370_Port_t port, LAN9370_PortStatus_t *status)
{
    uint8_t hwPortIndex;

    if (!isInitialized || port < 1 || port > 5 || status == NULL) {
        return LAN9370_INVALID_PARAM;
    }

    hwPortIndex = (uint8_t)port - 1U;

    memset(status, 0, sizeof(LAN9370_PortStatus_t));

    /* Read port status register */
    uint16_t portStatusAddr = PORT_CTRL_ADDR(hwPortIndex, REG_PORTn_STATUS);
    uint8_t statusReg;

    if (LAN9370_SPI_ReadReg8(portStatusAddr, &statusReg) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    /* Parse speed */
    uint8_t speedBits = (statusReg & PORT_INTF_SPEED_MASK) >> 3;
    switch (speedBits) {
        case 0: status->speed = LAN9370_LINK_SPEED_10M; break;
        case 1: status->speed = LAN9370_LINK_SPEED_100M; break;
        case 2: status->speed = LAN9370_LINK_SPEED_1G; break;
        default: status->speed = LAN9370_LINK_SPEED_10M; break;
    }

    /* Parse duplex */
    status->duplex = (statusReg & PORT_INTF_FULL_DUPLEX) ? 
                     LAN9370_DUPLEX_FULL : LAN9370_DUPLEX_HALF;

    /* Parse flow control */
    status->flowControlTx = (statusReg & PORT_TX_FLOW_CTRL_EN) ? true : false;
    status->flowControlRx = (statusReg & PORT_RX_FLOW_CTRL_EN) ? true : false;

    /* Check link status from PHY (for T1 ports) */
    if (port >= 1 && port <= 4) {
        uint16_t phyStatus;
        if (LAN9370_PHY_ReadReg(port, T1_PHY_BASIC_STATUS, &phyStatus) == LAN9370_OK) {
            status->linkUp = (phyStatus & T1_PHY_LINK_STATUS) ? true : false;
        }
    } else {
        /* For host port, assume link is up if enabled */
        status->linkUp = true;
    }

    return LAN9370_OK;
}

/**
 * @brief Configure T1 port
 */
LAN9370_Status_t LAN9370_ConfigureT1Port(LAN9370_Port_t port, const LAN9370_T1_Config_t *config)
{
    if (!isInitialized || port < 1 || port > 4 || config == NULL) {
        return LAN9370_INVALID_PARAM;
    }

    uint16_t regVal;

    /* Read Basic Control register */
    if (LAN9370_PHY_ReadReg(port, T1_PHY_BASIC_CTRL, &regVal) != LAN9370_OK) {
        return LAN9370_ERROR;
    }

    /* Configure loopback */
    if (config->loopbackEnable) {
        regVal |= T1_PHY_LOOPBACK;
    } else {
        regVal &= ~T1_PHY_LOOPBACK;
    }

    /* Configure auto-negotiation */
    if (config->autoNegEnable) {
        regVal |= T1_PHY_AN_ENABLE;
    } else {
        regVal &= ~T1_PHY_AN_ENABLE;
    }

    /* Enable/disable port */
    if (config->enabled) {
        regVal &= ~T1_PHY_POWER_DOWN;
        regVal &= ~T1_PHY_ISOLATE;
    } else {
        regVal |= T1_PHY_POWER_DOWN;
    }

    /* Write back */
    if (LAN9370_PHY_WriteReg(port, T1_PHY_BASIC_CTRL, regVal) != LAN9370_OK) {
        return LAN9370_ERROR;
    }

    /* Configure Master/Slave mode */
    if (LAN9370_SetT1MasterSlave(port, config->masterSlaveMode) != LAN9370_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_OK;
}

/* =============================================================================
 * Public Functions - PHY Operations
 * ===========================================================================*/

/**
 * @brief Read PHY register via SPI
 */
LAN9370_Status_t LAN9370_PHY_ReadReg(LAN9370_Port_t port, uint8_t regAddr, uint16_t *data)
{
    if (!isInitialized || port < 1 || port > 4 || data == NULL) {
        return LAN9370_INVALID_PARAM;
    }

    if (LAN9370_SetVphyIndirectAddress(port, regAddr) != LAN9370_OK) {
        return LAN9370_ERROR;
    }

    if (LAN9370_SPI_WriteReg16(REG_VPHY_IND_CTRL__2, VPHY_IND_BUSY) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    if (LAN9370_VphyWaitReady() != LAN9370_OK) {
        return LAN9370_TIMEOUT;
    }

    return (LAN9370_SPI_ReadReg16(REG_VPHY_IND_DATA__2, data) == LAN9370_SPI_OK) ?
           LAN9370_OK : LAN9370_ERROR;
}

/**
 * @brief Write PHY register via SPI
 */
LAN9370_Status_t LAN9370_PHY_WriteReg(LAN9370_Port_t port, uint8_t regAddr, uint16_t data)
{
    if (!isInitialized || port < 1 || port > 4) {
        return LAN9370_INVALID_PARAM;
    }

    if (LAN9370_SetVphyIndirectAddress(port, regAddr) != LAN9370_OK) {
        return LAN9370_ERROR;
    }

    if (LAN9370_SPI_WriteReg16(REG_VPHY_IND_DATA__2, data) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    if (LAN9370_SPI_WriteReg16(REG_VPHY_IND_CTRL__2, (uint16_t)(VPHY_IND_WRITE | VPHY_IND_BUSY)) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    if (LAN9370_VphyWaitReady() != LAN9370_OK) {
        return LAN9370_TIMEOUT;
    }

    return LAN9370_OK;
}

/* =============================================================================
 * Public Functions - MIIM (MDIO Master) for External PHY Access
 * ===========================================================================*/

#define LAN9370_MIIM_TIMEOUT_MS 50

/**
 * @brief Read external PHY register via LAN9370 MIIM master over SPI
 */
LAN9370_Status_t LAN9370_MIIM_Read(uint8_t phyAddr, uint8_t regAddr, uint16_t *data)
{
    uint8_t ctrl;
    uint32_t start;

    if (!isInitialized || data == NULL) {
        return LAN9370_INVALID_PARAM;
    }
    if (phyAddr > 31 || regAddr > 31) {
        return LAN9370_INVALID_PARAM;
    }

    /* Set PHY address and register address */
    {
        uint16_t addrVal = (uint16_t)(((uint16_t)phyAddr << MIIM_PHY_ADDR_S) & MIIM_PHY_ADDR_MASK)
                         | (uint16_t)(((uint16_t)regAddr << MIIM_REG_ADDR_S) & MIIM_REG_ADDR_MASK);
        if (LAN9370_SPI_WriteReg16(REG_MIIM_PHY_ADDR, addrVal) != LAN9370_SPI_OK) {
            return LAN9370_ERROR;
        }
    }

    /* Start read operation */
    if (LAN9370_SPI_WriteReg8(REG_MIIM_CTRL, MIIM_CTRL_READ | MIIM_CTRL_BUSY) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    /* Wait for operation complete */
    start = HAL_GetTick();
    do {
        if (LAN9370_SPI_ReadReg8(REG_MIIM_CTRL, &ctrl) != LAN9370_SPI_OK) {
            return LAN9370_ERROR;
        }
        if (!(ctrl & MIIM_CTRL_BUSY)) {
            break;
        }
    } while ((HAL_GetTick() - start) < LAN9370_MIIM_TIMEOUT_MS);

    if (ctrl & MIIM_CTRL_BUSY) {
        return LAN9370_TIMEOUT;
    }

    /* Read data */
    return (LAN9370_SPI_ReadReg16(REG_MIIM_IND_DATA, data) == LAN9370_SPI_OK) ?
           LAN9370_OK : LAN9370_ERROR;
}

/**
 * @brief Write external PHY register via LAN9370 MIIM master over SPI
 */
LAN9370_Status_t LAN9370_MIIM_Write(uint8_t phyAddr, uint8_t regAddr, uint16_t data)
{
    uint8_t ctrl;
    uint32_t start;

    if (!isInitialized) {
        return LAN9370_INVALID_PARAM;
    }
    if (phyAddr > 31 || regAddr > 31) {
        return LAN9370_INVALID_PARAM;
    }

    /* Write data first */
    if (LAN9370_SPI_WriteReg16(REG_MIIM_IND_DATA, data) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    /* Set PHY address and register address */
    {
        uint16_t addrVal = (uint16_t)(((uint16_t)phyAddr << MIIM_PHY_ADDR_S) & MIIM_PHY_ADDR_MASK)
                         | (uint16_t)(((uint16_t)regAddr << MIIM_REG_ADDR_S) & MIIM_REG_ADDR_MASK);
        if (LAN9370_SPI_WriteReg16(REG_MIIM_PHY_ADDR, addrVal) != LAN9370_SPI_OK) {
            return LAN9370_ERROR;
        }
    }

    /* Start write operation */
    if (LAN9370_SPI_WriteReg8(REG_MIIM_CTRL, MIIM_CTRL_WRITE | MIIM_CTRL_BUSY) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    /* Wait for operation complete */
    start = HAL_GetTick();
    do {
        if (LAN9370_SPI_ReadReg8(REG_MIIM_CTRL, &ctrl) != LAN9370_SPI_OK) {
            return LAN9370_ERROR;
        }
        if (!(ctrl & MIIM_CTRL_BUSY)) {
            break;
        }
    } while ((HAL_GetTick() - start) < LAN9370_MIIM_TIMEOUT_MS);

    if (ctrl & MIIM_CTRL_BUSY) {
        return LAN9370_TIMEOUT;
    }

    return LAN9370_OK;
}

/* =============================================================================
 * Public Functions - Switch Operations
 * ===========================================================================*/

/**
 * @brief Set MAC learning
 */
LAN9370_Status_t LAN9370_SetMACLearning(bool enable)
{
    if (!isInitialized) {
        return LAN9370_NOT_INITIALIZED;
    }

    uint8_t regVal;

    /* Read LUE Control 0 register */
    if (LAN9370_SPI_ReadReg8(REG_LUE_CTRL_0, &regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    /* Modify learning bit */
    if (enable) {
        regVal |= LUE_UNICAST_LEARNING;
    } else {
        regVal &= ~LUE_UNICAST_LEARNING;
    }

    /* Write back */
    if (LAN9370_SPI_WriteReg8(REG_LUE_CTRL_0, regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_OK;
}

/**
 * @brief Flush dynamic MAC table
 */
LAN9370_Status_t LAN9370_FlushDynamicMAC(void)
{
    if (!isInitialized) {
        return LAN9370_NOT_INITIALIZED;
    }

    uint8_t regVal;

    /* Read Global Control 0 */
    if (LAN9370_SPI_ReadReg8(REG_GLOBAL_CTRL_0, &regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    /* Set flush bit */
    regVal |= SW_FLUSH_DYN_MAC_TABLE;

    /* Write back */
    if (LAN9370_SPI_WriteReg8(REG_GLOBAL_CTRL_0, regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    /* Wait for flush to complete */
    HAL_Delay(10);

    return LAN9370_OK;
}

/**
 * @brief Set fast aging
 */
LAN9370_Status_t LAN9370_SetFastAging(bool enable)
{
    if (!isInitialized) {
        return LAN9370_NOT_INITIALIZED;
    }

    uint8_t regVal;

    /* Read Global Control 0 */
    if (LAN9370_SPI_ReadReg8(REG_GLOBAL_CTRL_0, &regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    /* Modify fast mode bit */
    if (enable) {
        regVal |= SW_FAST_MODE;
    } else {
        regVal &= ~SW_FAST_MODE;
    }

    /* Write back */
    if (LAN9370_SPI_WriteReg8(REG_GLOBAL_CTRL_0, regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_SetVlanEnable(bool enable)
{
    uint8_t regVal;

    if (!isInitialized) {
        return LAN9370_NOT_INITIALIZED;
    }

    if (LAN9370_SPI_ReadReg8(REG_LUE_CTRL_0, &regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    if (enable) {
        regVal |= LUE_VLAN_ENABLE;
    } else {
        regVal &= (uint8_t)(~LUE_VLAN_ENABLE);
    }

    if (LAN9370_SPI_WriteReg8(REG_LUE_CTRL_0, regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_GetVlanEnable(bool *enabled)
{
    uint8_t regVal;

    if (!isInitialized || enabled == NULL) {
        return LAN9370_INVALID_PARAM;
    }

    if (LAN9370_SPI_ReadReg8(REG_LUE_CTRL_0, &regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    *enabled = ((regVal & LUE_VLAN_ENABLE) != 0U);
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_SetPortDefaultVlan(LAN9370_Port_t port, uint16_t vlanId)
{
    uint8_t hwPortIndex;
    uint16_t regBase;

    if (!isInitialized || port < LAN9370_PORT_1 || port > LAN9370_PORT_5 || vlanId == 0 || vlanId > 4094) {
        return LAN9370_INVALID_PARAM;
    }

    hwPortIndex = (uint8_t)port - 1U;

    regBase = PORT_CTRL_ADDR(hwPortIndex, REG_PORTn_DEFAULT_TAG0);

    if (LAN9370_SPI_WriteReg8(regBase, (uint8_t)(vlanId & 0xFF)) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }
    if (LAN9370_SPI_WriteReg8((uint16_t)(regBase + 1), (uint8_t)((vlanId >> 8) & 0x0F)) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_SetPortMembership(LAN9370_Port_t port, uint8_t memberMask)
{
    uint8_t hwPortIndex;
    uint16_t regAddr;
    uint32_t regVal;

    if (!isInitialized || port < LAN9370_PORT_1 || port > LAN9370_PORT_5 || (memberMask & 0xE0U) != 0U) {
        return LAN9370_INVALID_PARAM;
    }

    hwPortIndex = (uint8_t)port - 1U;
    regAddr = PORT_CTRL_ADDR(hwPortIndex, REG_PORT_VLAN_MEMBERSHIP__4);
    regVal = (uint32_t)memberMask;

    if (LAN9370_SPI_WriteReg32(regAddr, regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_GetPortMembership(LAN9370_Port_t port, uint8_t *memberMask)
{
    uint8_t hwPortIndex;
    uint16_t regAddr;
    uint32_t regVal;

    if (!isInitialized || port < LAN9370_PORT_1 || port > LAN9370_PORT_5 || memberMask == NULL) {
        return LAN9370_INVALID_PARAM;
    }

    hwPortIndex = (uint8_t)port - 1U;
    regAddr = PORT_CTRL_ADDR(hwPortIndex, REG_PORT_VLAN_MEMBERSHIP__4);

    if (LAN9370_SPI_ReadReg32(regAddr, &regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    *memberMask = (uint8_t)(regVal & 0x1FU);
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_ReadPortMibCounter(LAN9370_Port_t port, uint16_t mibIndex, uint32_t *value)
{
    uint8_t hwPortIndex;
    uint16_t ctrlAddr;
    uint16_t dataAddr;
    uint32_t ctrlVal;
    uint32_t start;

    if (!isInitialized || port < LAN9370_PORT_1 || port > LAN9370_PORT_5 || value == NULL) {
        return LAN9370_INVALID_PARAM;
    }

    hwPortIndex = (uint8_t)port - 1U;
    ctrlAddr = PORT_CTRL_ADDR(hwPortIndex, REG_PORT_MIB_CTRL_STAT__4);
    dataAddr = PORT_CTRL_ADDR(hwPortIndex, REG_PORT_MIB_DATA);

    ctrlVal = (uint32_t)MIB_COUNTER_READ | ((uint32_t)mibIndex << MIB_COUNTER_INDEX_S);
    if (LAN9370_SPI_WriteReg32(ctrlAddr, ctrlVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    start = HAL_GetTick();
    do {
        if (LAN9370_SPI_ReadReg32(ctrlAddr, &ctrlVal) != LAN9370_SPI_OK) {
            return LAN9370_ERROR;
        }
        if ((ctrlVal & MIB_COUNTER_READ) == 0U) {
            break;
        }
    } while ((HAL_GetTick() - start) < LAN9370_MIB_READ_TIMEOUT_MS);

    if ((ctrlVal & MIB_COUNTER_READ) != 0U) {
        return LAN9370_TIMEOUT;
    }

    if (LAN9370_SPI_ReadReg32(dataAddr, value) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_PrintPortMib(LAN9370_Port_t port)
{
    uint32_t rxTotal;
    uint32_t txTotal;
    uint32_t rxDrop;
    uint32_t txDrop;

    if (LAN9370_ReadPortMibCounter(port, LAN9370_MIB_RX_TOTAL_IDX, &rxTotal) != LAN9370_OK) {
        return LAN9370_ERROR;
    }
    if (LAN9370_ReadPortMibCounter(port, LAN9370_MIB_TX_TOTAL_IDX, &txTotal) != LAN9370_OK) {
        return LAN9370_ERROR;
    }
    if (LAN9370_ReadPortMibCounter(port, LAN9370_MIB_RX_DROP_IDX, &rxDrop) != LAN9370_OK) {
        return LAN9370_ERROR;
    }
    if (LAN9370_ReadPortMibCounter(port, LAN9370_MIB_TX_DROP_IDX, &txDrop) != LAN9370_OK) {
        return LAN9370_ERROR;
    }

    printf("Port %d MIB: rx_total=%lu tx_total=%lu rx_drop=%lu tx_drop=%lu\n",
           (int)port,
           (unsigned long)rxTotal,
           (unsigned long)txTotal,
           (unsigned long)rxDrop,
           (unsigned long)txDrop);

    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_FlushStaticMAC(void)
{
    uint8_t regVal;

    if (!isInitialized) {
        return LAN9370_NOT_INITIALIZED;
    }

    s_staticMacCount = 0;
    memset(s_staticMacShadow, 0, sizeof(s_staticMacShadow));

    if (LAN9370_SPI_ReadReg8(REG_GLOBAL_CTRL_0, &regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    regVal |= SW_FLUSH_STA_MAC_TABLE;

    if (LAN9370_SPI_WriteReg8(REG_GLOBAL_CTRL_0, regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    HAL_Delay(10);
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_AddStaticMac(const LAN9370_StaticMacEntry_t *entry)
{
    if (entry == NULL || entry->vlanId == 0 || entry->vlanId > 4094 || entry->destPorts == 0) {
        return LAN9370_INVALID_PARAM;
    }

    if (s_staticMacCount >= LAN9370_STATIC_MAC_SHADOW_SIZE) {
        return LAN9370_ERROR;
    }

    memcpy(&s_staticMacShadow[s_staticMacCount], entry, sizeof(LAN9370_StaticMacEntry_t));
    s_staticMacCount++;
    return LAN9370_OK;
}

int LAN9370_PrintStaticMacTable(void)
{
    uint8_t i;

    printf("Static MAC shadow entries: %u\n", s_staticMacCount);
    for (i = 0; i < s_staticMacCount; i++) {
        printf("[%u] %02X:%02X:%02X:%02X:%02X:%02X vid=%u ports=0x%02X\n",
               i,
               s_staticMacShadow[i].mac[0], s_staticMacShadow[i].mac[1],
               s_staticMacShadow[i].mac[2], s_staticMacShadow[i].mac[3],
               s_staticMacShadow[i].mac[4], s_staticMacShadow[i].mac[5],
               s_staticMacShadow[i].vlanId,
               s_staticMacShadow[i].destPorts);
    }

    return 0;
}

/* =============================================================================
 * Public Functions - Diagnostics
 * ===========================================================================*/

/**
 * @brief Check communication
 */
LAN9370_Status_t LAN9370_CheckCommunication(void)
{
    if (!isInitialized) {
        return LAN9370_NOT_INITIALIZED;
    }

    LAN9370_ChipInfo_t info;
    return LAN9370_GetChipInfo(&info);
}

/**
 * @brief Print port status (uses printf, requires retarget or semihosting)
 */
LAN9370_Status_t LAN9370_PrintPortStatus(LAN9370_Port_t port)
{
    LAN9370_PortStatus_t status;
    
    if (LAN9370_GetPortStatus(port, &status) != LAN9370_OK) {
        return LAN9370_ERROR;
    }

    printf("Port %d Status:\n", port);
    printf("  Link: %s\n", status.linkUp ? "UP" : "DOWN");
    printf("  Speed: ");
    switch (status.speed) {
        case LAN9370_LINK_SPEED_10M: printf("10M\n"); break;
        case LAN9370_LINK_SPEED_100M: printf("100M\n"); break;
        case LAN9370_LINK_SPEED_1G: printf("1G\n"); break;
    }
    printf("  Duplex: %s\n", status.duplex == LAN9370_DUPLEX_FULL ? "Full" : "Half");
    printf("  Flow Control TX: %s\n", status.flowControlTx ? "ON" : "OFF");
    printf("  Flow Control RX: %s\n", status.flowControlRx ? "ON" : "OFF");

    return LAN9370_OK;
}

/**
 * @brief Dump registers (basic implementation)
 */
LAN9370_Status_t LAN9370_DumpRegisters(void)
{
    if (!isInitialized) {
        return LAN9370_NOT_INITIALIZED;
    }

    printf("=== LAN9370 Register Dump ===\n");

    /* Chip ID */
    uint32_t chipId;
    LAN9370_SPI_ReadReg32(REG_CHIP_ID0, &chipId);
    printf("Chip ID: 0x%08lX\n", chipId);

    /* Global registers */
    uint8_t regVal;
    LAN9370_SPI_ReadReg8(REG_GLOBAL_CTRL_0, &regVal);
    printf("Global Ctrl 0: 0x%02X\n", regVal);

    /* Port status */
    for (int port = 1; port <= 5; port++) {
        uint16_t addr = PORT_CTRL_ADDR((uint8_t)(port - 1), REG_PORTn_STATUS);
        LAN9370_SPI_ReadReg8(addr, &regVal);
        printf("Port %d Status: 0x%02X\n", port, regVal);
    }

    printf("=============================\n");

    return LAN9370_OK;
}

/**
 * @brief Print status of all ports in aligned columnar format
 */
LAN9370_Status_t LAN9370_PrintAllPortsStatus(void)
{
    if (!isInitialized) {
        return LAN9370_NOT_INITIALIZED;
    }

    LAN9370_PortStatus_t status;
    
    printf("\r\n=== All Ports Status ===\r\n");
    printf("Port | Link   | Speed  | Duplex | FC-RX | FC-TX | Mode\r\n");
    printf("-----+--------+--------+--------+-------+-------+--------\r\n");

    for (int port = 1; port <= 5; port++) {
        if (LAN9370_GetPortStatus((LAN9370_Port_t)port, &status) != LAN9370_OK) {
            printf("  %d  | ERROR  |        |        |       |       |\r\n", port);
            continue;
        }

        char speedStr[8];
        switch (status.speed) {
            case LAN9370_LINK_SPEED_10M:  strcpy(speedStr, "10M"); break;
            case LAN9370_LINK_SPEED_100M: strcpy(speedStr, "100M"); break;
            case LAN9370_LINK_SPEED_1G:   strcpy(speedStr, "1G"); break;
            default: strcpy(speedStr, "?"); break;
        }

        char modeStr[8] = "";
        if (port >= 1 && port <= 4) {
            LAN9370_T1_Mode_t mode;
            if (LAN9370_GetT1MasterSlave((LAN9370_Port_t)port, &mode) == LAN9370_OK) {
                strcpy(modeStr, (mode == LAN9370_T1_MASTER) ? "Master" : "Slave");
            }
        }

        printf("  %d  | %s | %s | %s | %3s | %3s | %s\r\n",
               port,
               status.linkUp ? "UP" : "DOWN",
               speedStr,
               status.duplex == LAN9370_DUPLEX_FULL ? "Full" : "Half",
               status.flowControlRx ? "ON" : "OFF",
               status.flowControlTx ? "ON" : "OFF",
               modeStr);
    }
    printf("========================\r\n\r\n");

    return LAN9370_OK;
}

/**
 * @brief Set port mirror (source port -> destination port)
 * @note Mirror is configured via egress membership - not all LAN9370 chips support
 *       dedicated mirror register. This is a simplified implementation.
 */
LAN9370_Status_t LAN9370_SetPortMirror(LAN9370_Port_t srcPort, LAN9370_Port_t dstPort, bool ingress)
{
    (void)ingress; /* LAN9370 mirror may not support direction selection */
    
    if (!isInitialized || srcPort < 1 || srcPort > 5 || (dstPort != 0 && (dstPort < 1 || dstPort > 5))) {
        return LAN9370_INVALID_PARAM;
    }

    if (dstPort == 0) {
        /* Disable mirror: restore normal membership */
        return LAN9370_SetPortMembership(srcPort, 0x1F);
    }

    /* Enable mirror: redirect to destination port only */
    uint8_t mirrorMask = (1 << (dstPort - 1));
    return LAN9370_SetPortMembership(srcPort, mirrorMask);
}

/**
 * @brief Get current mirror configuration
 */
LAN9370_Status_t LAN9370_GetPortMirror(LAN9370_Port_t srcPort, LAN9370_Port_t *dstPort, bool *ingress)
{
    if (!isInitialized || srcPort < 1 || srcPort > 5 || dstPort == NULL) {
        return LAN9370_INVALID_PARAM;
    }

    uint8_t membership;
    if (LAN9370_GetPortMembership(srcPort, &membership) != LAN9370_OK) {
        return LAN9370_ERROR;
    }

    /* Check if membership is normal (0x1F) or restricted (mirror) */
    if (membership == 0x1F) {
        *dstPort = 0; /* No mirror */
    } else {
        /* Find which port is mirrored to */
        *dstPort = 0;
        for (int p = 1; p <= 5; p++) {
            if (membership & (1 << (p - 1))) {
                *dstPort = p;
                break;
            }
        }
    }

    if (ingress != NULL) {
        *ingress = false; /* LAN9370 default: egress mirror */
    }

    return LAN9370_OK;
}

/**
 * @brief Read port default VLAN ID
 */
LAN9370_Status_t LAN9370_GetPortDefaultVlan(LAN9370_Port_t port, uint16_t *vlanId)
{
    uint8_t hwPortIndex;
    uint16_t regAddr;
    uint16_t regVal;

    if (!isInitialized || port < 1 || port > 5 || vlanId == NULL) {
        return LAN9370_INVALID_PARAM;
    }

    hwPortIndex = (uint8_t)port - 1U;
    regAddr = PORT_CTRL_ADDR(hwPortIndex, REG_PORTn_DEFAULT_VLAN);

    if (LAN9370_SPI_ReadReg16(regAddr, &regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    /* Extract VLAN ID (typically bits 11:0) */
    *vlanId = regVal & 0x0FFF;

    return LAN9370_OK;
}

/**
 * @brief Print current VLAN configuration for all ports
 */
LAN9370_Status_t LAN9370_PrintVlanConfig(void)
{
    if (!isInitialized) {
        return LAN9370_NOT_INITIALIZED;
    }

    printf("\r\n=== VLAN Configuration ===\r\n");
    printf("Port | Default VID | Egress Members\r\n");
    printf("-----+-------------+----------------\r\n");

    for (int port = 1; port <= 5; port++) {
        uint16_t vlanId;
        uint8_t membership;

        if (LAN9370_GetPortDefaultVlan((LAN9370_Port_t)port, &vlanId) == LAN9370_OK &&
            LAN9370_GetPortMembership((LAN9370_Port_t)port, &membership) == LAN9370_OK) {
            
            printf("  %d  |    %4d     | ", port, vlanId);
            for (int p = 1; p <= 5; p++) {
                if (membership & (1 << (p - 1))) {
                    printf("%d ", p);
                }
            }
            printf("\r\n");
        }
    }
    printf("==========================\r\n\r\n");

    return LAN9370_OK;
}

/**
 * @brief Enable or disable PTP
 * @note This is a simplified implementation; full PTP requires more registers
 */
LAN9370_Status_t LAN9370_SetPtpEnable(bool enable)
{
    if (!isInitialized) {
        return LAN9370_NOT_INITIALIZED;
    }

    uint16_t regVal;

    if (LAN9370_SPI_ReadReg16(LAN9370_PTP_CTRL_REG_ADDR, &regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    if (enable) {
        regVal |= LAN9370_PTP_ENABLE_BIT;
    } else {
        regVal &= (uint16_t)(~LAN9370_PTP_ENABLE_BIT);
    }

    if (LAN9370_SPI_WriteReg16(LAN9370_PTP_CTRL_REG_ADDR, regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_OK;
}

/**
 * @brief Get PTP enable status
 */
LAN9370_Status_t LAN9370_GetPtpEnable(bool *enabled)
{
    if (!isInitialized || enabled == NULL) {
        return LAN9370_INVALID_PARAM;
    }

    uint16_t regVal;

    if (LAN9370_SPI_ReadReg16(LAN9370_PTP_CTRL_REG_ADDR, &regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    *enabled = ((regVal & LAN9370_PTP_ENABLE_BIT) != 0U);

    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_SetGptpEnable(bool enable)
{
    if (!isInitialized) {
        return LAN9370_NOT_INITIALIZED;
    }

    uint16_t regVal;

    if (LAN9370_SPI_ReadReg16(LAN9370_PTP_CTRL_REG_ADDR, &regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    if (enable) {
        regVal |= LAN9370_GPTP_ENABLE_BIT;
    } else {
        regVal &= (uint16_t)(~LAN9370_GPTP_ENABLE_BIT);
    }

    if (LAN9370_SPI_WriteReg16(LAN9370_PTP_CTRL_REG_ADDR, regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_GetGptpEnable(bool *enabled)
{
    if (!isInitialized || enabled == NULL) {
        return LAN9370_INVALID_PARAM;
    }

    uint16_t regVal;

    if (LAN9370_SPI_ReadReg16(LAN9370_PTP_CTRL_REG_ADDR, &regVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    *enabled = ((regVal & LAN9370_GPTP_ENABLE_BIT) != 0U);

    return LAN9370_OK;
}
