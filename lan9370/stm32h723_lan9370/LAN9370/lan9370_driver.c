/**
  ******************************************************************************
  * @file    lan9370_driver.c
  * @brief   LAN9370 5-Port 100BASE-T1 Ethernet Switch Driver Implementation
  *          Adapted for STM32H723
  *          nRST: PC3 (LAN9370_NRST_Pin / LAN9370_NRST_GPIO_Port from main.h)
  ******************************************************************************
  */

#include "lan9370_driver.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

/* Route all driver debug output through the application-level debug_printf */
extern int debug_printf(const char *format, ...);
#define printf debug_printf

/* =============================================================================
 * Private Definitions
 * ===========================================================================*/
#define LAN9370_RESET_PULSE_MS       10
#define LAN9370_RESET_DELAY_MS       300
#define LAN9370_VPHY_BUSY_TIMEOUT_MS 200
#define LAN9370_MIB_READ_TIMEOUT_MS  100

/* =============================================================================
 * Private Variables
 * ===========================================================================*/
static bool isInitialized = false;
#define LAN9370_STATIC_MAC_SHADOW_SIZE 32
static LAN9370_StaticMacEntry_t s_staticMacShadow[LAN9370_STATIC_MAC_SHADOW_SIZE];
static uint8_t s_staticMacCount = 0;

/* =============================================================================
 * Private Functions
 * ===========================================================================*/
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
    uint16_t specialCtrl;
    if (LAN9370_SPI_ReadReg16(REG_VPHY_SPECIAL_CTRL__2, &specialCtrl) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }
    specialCtrl |= VPHY_SPI_INDIRECT_ENABLE;
    if (LAN9370_SPI_WriteReg16(REG_VPHY_SPECIAL_CTRL__2, specialCtrl) != LAN9370_SPI_OK) {
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

static void DecodeChipName(uint32_t chipId, char *name, size_t len)
{
    switch (chipId & 0xFFFFFF00U) {
        case 0x00937000U: snprintf(name, len, "LAN9370"); break;
        case 0x00937100U: snprintf(name, len, "LAN9371"); break;
        case 0x00937200U: snprintf(name, len, "LAN9372"); break;
        case 0x00937300U: snprintf(name, len, "LAN9373"); break;
        case 0x00937400U: snprintf(name, len, "LAN9374"); break;
        default: snprintf(name, len, "Unknown(0x%08lX)", (unsigned long)chipId); break;
    }
}

/* =============================================================================
 * Public Functions - Initialization
 * ===========================================================================*/

LAN9370_Status_t LAN9370_Init(SPI_HandleTypeDef *hspi)
{
    if (LAN9370_SPI_Init(hspi) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }
    if (LAN9370_SMI_Init() != LAN9370_SMI_OK) {
        return LAN9370_ERROR;
    }

    /* nRST pin already configured in MX_GPIO_Init() as GPIO output */
    /* Deassert reset initially */
    HAL_GPIO_WritePin(LAN9370_NRST_GPIO_Port, LAN9370_NRST_Pin, GPIO_PIN_SET);

    isInitialized = true;

    return LAN9370_HardwareReset();
}

LAN9370_Status_t LAN9370_HardwareReset(void)
{
    uint8_t gctrl0;

    /* Assert nRST (active low) */
    HAL_GPIO_WritePin(LAN9370_NRST_GPIO_Port, LAN9370_NRST_Pin, GPIO_PIN_RESET);
    HAL_Delay(LAN9370_RESET_PULSE_MS);

    /* Deassert nRST */
    HAL_GPIO_WritePin(LAN9370_NRST_GPIO_Port, LAN9370_NRST_Pin, GPIO_PIN_SET);
    HAL_Delay(LAN9370_RESET_DELAY_MS);

    /* Enable PHY register access through SPI by clearing SW_PHY_REG_BLOCK */
    if (LAN9370_SPI_ReadReg8(REG_GLOBAL_CTRL_0, &gctrl0) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }
    gctrl0 &= (uint8_t)(~SW_PHY_REG_BLOCK);
    if (LAN9370_SPI_WriteReg8(REG_GLOBAL_CTRL_0, gctrl0) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    return LAN9370_EnableSpiIndirectVphyAccess();
}

LAN9370_Status_t LAN9370_SoftwareReset(void)
{
    if (!isInitialized) return LAN9370_NOT_INITIALIZED;
    if (LAN9370_SPI_WriteReg8(REG_SW_RESET, SW_SOFT_RESET) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }
    HAL_Delay(100);
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_GetChipInfo(LAN9370_ChipInfo_t *info)
{
    if (!isInitialized || info == NULL) return LAN9370_INVALID_PARAM;

    /* Read chip ID with retry + consistency check to filter SPI glitches.
     * The chip ID is read-only silicon — any change across reads is a SPI error. */
    uint8_t id0, id1, id2, id3;
    uint32_t chipId1, chipId2;
    int attempts;

    for (attempts = 0; attempts < 5; attempts++) {
        if (LAN9370_SPI_ReadReg8_Retry(REG_CHIP_ID0, &id0) != LAN9370_SPI_OK) continue;
        if (LAN9370_SPI_ReadReg8_Retry(REG_CHIP_ID1, &id1) != LAN9370_SPI_OK) continue;
        if (LAN9370_SPI_ReadReg8_Retry(REG_CHIP_ID2, &id2) != LAN9370_SPI_OK) continue;
        if (LAN9370_SPI_ReadReg8_Retry(REG_CHIP_ID3, &id3) != LAN9370_SPI_OK) continue;

        chipId1 = ((uint32_t)id0 << 24) | ((uint32_t)id1 << 16) |
                  ((uint32_t)id2 << 8) | id3;

        /* Re-read to verify consistency */
        uint8_t v0, v1, v2, v3;
        if (LAN9370_SPI_ReadReg8_Retry(REG_CHIP_ID0, &v0) != LAN9370_SPI_OK) continue;
        if (LAN9370_SPI_ReadReg8_Retry(REG_CHIP_ID1, &v1) != LAN9370_SPI_OK) continue;
        if (LAN9370_SPI_ReadReg8_Retry(REG_CHIP_ID2, &v2) != LAN9370_SPI_OK) continue;
        if (LAN9370_SPI_ReadReg8_Retry(REG_CHIP_ID3, &v3) != LAN9370_SPI_OK) continue;

        chipId2 = ((uint32_t)v0 << 24) | ((uint32_t)v1 << 16) |
                  ((uint32_t)v2 << 8) | v3;

        if (chipId1 == chipId2) {
            info->chipId = chipId1;
            info->revisionId = id3 & 0x0FU;
            DecodeChipName(info->chipId, info->chipName, sizeof(info->chipName));
            return LAN9370_OK;
        }
    }

    /* Last resort: use last read value */
    info->chipId = chipId1;
    info->revisionId = id3 & 0x0FU;
    DecodeChipName(info->chipId, info->chipName, sizeof(info->chipName));
    return LAN9370_OK;
}

/* =============================================================================
 * Public Functions - Port Configuration
 * ===========================================================================*/

LAN9370_Status_t LAN9370_SetPortEnable(LAN9370_Port_t port, bool enable)
{
    uint8_t hwPortIndex;
    uint8_t ctrl0Val;
    uint8_t mstpVal;

    if (!isInitialized || port < 1 || port > 5) return LAN9370_INVALID_PARAM;

    hwPortIndex = (uint8_t)port - 1U;

    if (LAN9370_SPI_ReadReg8(PORT_CTRL_ADDR(hwPortIndex, REG_PORTn_OP_CTRL0), &ctrl0Val) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }
    ctrl0Val &= (uint8_t)~(PORT_TAIL_TAG_ENABLE | PORT_QUEUE_SPLIT_MASK);
    if (LAN9370_SPI_WriteReg8(PORT_CTRL_ADDR(hwPortIndex, REG_PORTn_OP_CTRL0), ctrl0Val) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    mstpVal = enable ? (uint8_t)(PORT_TX_ENABLE | PORT_RX_ENABLE) : 0U;
    if (LAN9370_SPI_WriteReg8(PORT_CTRL_ADDR(hwPortIndex, REG_PORTn_MSTP_STATE), mstpVal) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_SetT1MasterSlave(LAN9370_Port_t port, LAN9370_T1_Mode_t mode)
{
    if (!isInitialized || port < 1 || port > 4) return LAN9370_INVALID_PARAM;

    uint16_t regVal;
    if (LAN9370_PHY_ReadReg(port, T1_PHY_MASTER_SLAVE_CTRL, &regVal) != LAN9370_OK) {
        return LAN9370_ERROR;
    }
    regVal |= T1_PHY_MS_CFG_ENABLE;
    if (mode == LAN9370_T1_MASTER) {
        regVal |= T1_PHY_MS_CFG_VALUE;
    } else {
        regVal &= (uint16_t)~T1_PHY_MS_CFG_VALUE;
    }
    if (LAN9370_PHY_WriteReg(port, T1_PHY_MASTER_SLAVE_CTRL, regVal) != LAN9370_OK) {
        return LAN9370_ERROR;
    }
    /* Re-read to verify the write took effect */
    uint16_t verify;
    if (LAN9370_PHY_ReadReg(port, T1_PHY_MASTER_SLAVE_CTRL, &verify) == LAN9370_OK) {
        if ((verify & T1_PHY_MS_CFG_ENABLE) == 0U) {
            /* Write didn't stick — retry once */
            LAN9370_PHY_WriteReg(port, T1_PHY_MASTER_SLAVE_CTRL, regVal);
        }
    }
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_GetT1MasterSlave(LAN9370_Port_t port, LAN9370_T1_Mode_t *mode)
{
    uint16_t regVal;
    if (!isInitialized || port < LAN9370_PORT_1 || port > LAN9370_PORT_4 || mode == NULL) {
        return LAN9370_INVALID_PARAM;
    }
    /* Read twice and verify consistency to filter SPI glitches */
    uint16_t v1, v2;
    if (LAN9370_PHY_ReadReg(port, T1_PHY_MASTER_SLAVE_CTRL, &v1) != LAN9370_OK) {
        return LAN9370_ERROR;
    }
    if (LAN9370_PHY_ReadReg(port, T1_PHY_MASTER_SLAVE_CTRL, &v2) != LAN9370_OK) {
        /* Second read failed, use first */
        regVal = v1;
    } else if (v1 == v2) {
        regVal = v1;
    } else {
        /* Mismatch — try a tie-breaker read */
        uint16_t v3;
        if (LAN9370_PHY_ReadReg(port, T1_PHY_MASTER_SLAVE_CTRL, &v3) == LAN9370_OK) {
            regVal = (v3 == v1) ? v1 : v2;  /* pick the majority */
        } else {
            regVal = v1;
        }
    }
    *mode = ((regVal & T1_PHY_MS_CFG_VALUE) != 0U) ? LAN9370_T1_MASTER : LAN9370_T1_SLAVE;
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_GetPortStatus(LAN9370_Port_t port, LAN9370_PortStatus_t *status)
{
    uint8_t hwPortIndex;
    if (!isInitialized || port < 1 || port > 5 || status == NULL) return LAN9370_INVALID_PARAM;

    hwPortIndex = (uint8_t)port - 1U;
    memset(status, 0, sizeof(LAN9370_PortStatus_t));

    uint8_t statusReg;
    if (LAN9370_SPI_ReadReg8_Retry(PORT_CTRL_ADDR(hwPortIndex, REG_PORTn_STATUS), &statusReg) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    /* T1 PHY ports (1-4) are always 100M full-duplex.
     * The PORT_STATUS speed bits reflect the MAC interface config, not the PHY
     * line rate; on T1 ports they can report 1G or 10M spuriously. */
    if (port >= 1 && port <= 4) {
        status->speed = LAN9370_LINK_SPEED_100M;
    } else {
        uint8_t speedBits = (statusReg & PORT_INTF_SPEED_MASK) >> 3;
        switch (speedBits) {
            case 1:  status->speed = LAN9370_LINK_SPEED_100M; break;
            case 2:  status->speed = LAN9370_LINK_SPEED_1G;   break;
            default: status->speed = LAN9370_LINK_SPEED_10M;  break;
        }
    }
    status->duplex        = (statusReg & PORT_INTF_FULL_DUPLEX) ? LAN9370_DUPLEX_FULL : LAN9370_DUPLEX_HALF;
    status->flowControlTx = (statusReg & PORT_TX_FLOW_CTRL_EN) ? true : false;
    status->flowControlRx = (statusReg & PORT_RX_FLOW_CTRL_EN) ? true : false;

    if (port >= 1 && port <= 4) {
        uint16_t phyStatus;
        if (LAN9370_PHY_ReadReg(port, T1_PHY_BASIC_STATUS, &phyStatus) == LAN9370_OK) {
            status->linkUp = (phyStatus & T1_PHY_LINK_STATUS) ? true : false;
        }
    } else {
        /* Port 5: host port, link controlled by MSTP state */
        uint8_t mstp;
        if (LAN9370_SPI_ReadReg8_Retry(PORT_CTRL_ADDR(hwPortIndex, REG_PORTn_MSTP_STATE), &mstp) == LAN9370_SPI_OK) {
            status->linkUp = (mstp & PORT_TX_ENABLE) ? true : false;
        }
    }
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_ConfigurePort5Rmii(void)
{
    if (!isInitialized) return LAN9370_NOT_INITIALIZED;

    uint8_t hwPort5 = 4U;  /* Port 5 = index 4 */

    /* XMII_CTRL0: RMII mode (bit[1:0]=01), rising edge sample (bit[5]=1) -> 0x61 */
    if (LAN9370_SPI_WriteReg8(PORT_CTRL_ADDR(hwPort5, REG_PORTn_XMII_CTRL0), 0x61U) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }
    /* XMII_CTRL1: external REFCLK (bit[7]=0), 100Mbps (bit[6]=1) -> 0x4D */
    if (LAN9370_SPI_WriteReg8(PORT_CTRL_ADDR(hwPort5, REG_PORTn_XMII_CTRL1), 0x4DU) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }

    /* Verify immediately */
    {
        uint8_t v0=0, v1=0;
        LAN9370_SPI_ReadReg8(PORT_CTRL_ADDR(hwPort5, REG_PORTn_XMII_CTRL0), &v0);
        LAN9370_SPI_ReadReg8(PORT_CTRL_ADDR(hwPort5, REG_PORTn_XMII_CTRL1), &v1);
        printf("[LAN9370] Port5 RMII: XMII0=0x%02X XMII1=0x%02X\r\n", v0, v1);
    }
    return LAN9370_OK;
}

/**
 * @brief Try alternative XMII_CTRL1 values for RMII link testing
 */
LAN9370_Status_t LAN9370_TryPort5Xmii1(uint8_t xmii1_val)
{
    if (!isInitialized) return LAN9370_NOT_INITIALIZED;
    uint8_t hwPort5 = 4U;

    if (LAN9370_SPI_WriteReg8(PORT_CTRL_ADDR(hwPort5, REG_PORTn_XMII_CTRL1), xmii1_val) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }
    {
        uint8_t v=0;
        LAN9370_SPI_ReadReg8(PORT_CTRL_ADDR(hwPort5, REG_PORTn_XMII_CTRL1), &v);
        printf("[LAN9370] Port5 XMII1 write 0x%02X read 0x%02X\r\n", xmii1_val, v);
    }
    return LAN9370_OK;
}

/* =============================================================================
 * Public Functions - PHY Operations
 * ===========================================================================*/

LAN9370_Status_t LAN9370_PHY_ReadReg(LAN9370_Port_t port, uint8_t regAddr, uint16_t *data)
{
    if (!isInitialized || port < 1 || port > 4 || data == NULL) return LAN9370_INVALID_PARAM;

    if (LAN9370_SetVphyIndirectAddress(port, regAddr) != LAN9370_OK) return LAN9370_ERROR;
    if (LAN9370_SPI_WriteReg16(REG_VPHY_IND_CTRL__2, VPHY_IND_BUSY) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (LAN9370_VphyWaitReady() != LAN9370_OK) return LAN9370_TIMEOUT;
    return (LAN9370_SPI_ReadReg16(REG_VPHY_IND_DATA__2, data) == LAN9370_SPI_OK) ? LAN9370_OK : LAN9370_ERROR;
}

LAN9370_Status_t LAN9370_PHY_WriteReg(LAN9370_Port_t port, uint8_t regAddr, uint16_t data)
{
    if (!isInitialized || port < 1 || port > 4) return LAN9370_INVALID_PARAM;

    if (LAN9370_SetVphyIndirectAddress(port, regAddr) != LAN9370_OK) return LAN9370_ERROR;
    if (LAN9370_SPI_WriteReg16(REG_VPHY_IND_DATA__2, data) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (LAN9370_SPI_WriteReg16(REG_VPHY_IND_CTRL__2, (uint16_t)(VPHY_IND_WRITE | VPHY_IND_BUSY)) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (LAN9370_VphyWaitReady() != LAN9370_OK) return LAN9370_TIMEOUT;
    return LAN9370_OK;
}

/* =============================================================================
 * Public Functions - Switch Operations
 * ===========================================================================*/

LAN9370_Status_t LAN9370_SetMACLearning(bool enable)
{
    if (!isInitialized) return LAN9370_NOT_INITIALIZED;
    uint8_t regVal;
    if (LAN9370_SPI_ReadReg8(REG_LUE_CTRL_0, &regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (enable) {
        regVal |= LUE_UNICAST_LEARNING;
    } else {
        regVal &= (uint8_t)~LUE_UNICAST_LEARNING;
    }
    if (LAN9370_SPI_WriteReg8(REG_LUE_CTRL_0, regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_SetPortMembership(LAN9370_Port_t port, uint8_t memberMask)
{
    uint8_t hwPortIndex;
    if (!isInitialized || port < LAN9370_PORT_1 || port > LAN9370_PORT_5 || (memberMask & 0xE0U) != 0U) {
        return LAN9370_INVALID_PARAM;
    }
    hwPortIndex = (uint8_t)port - 1U;
    if (LAN9370_SPI_WriteReg32(PORT_CTRL_ADDR(hwPortIndex, REG_PORT_VLAN_MEMBERSHIP__4),
                               (uint32_t)memberMask) != LAN9370_SPI_OK) {
        return LAN9370_ERROR;
    }
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_FlushDynamicMAC(void)
{
    if (!isInitialized) return LAN9370_NOT_INITIALIZED;
    uint8_t regVal;
    if (LAN9370_SPI_ReadReg8(REG_GLOBAL_CTRL_0, &regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    regVal |= SW_FLUSH_DYN_MAC_TABLE;
    if (LAN9370_SPI_WriteReg8(REG_GLOBAL_CTRL_0, regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    HAL_Delay(10);
    return LAN9370_OK;
}

/* =============================================================================
 * Public Functions - Diagnostics
 * ===========================================================================*/

LAN9370_Status_t LAN9370_PrintPortStatus(LAN9370_Port_t port)
{
    LAN9370_PortStatus_t status;
    if (LAN9370_GetPortStatus(port, &status) != LAN9370_OK) return LAN9370_ERROR;

    printf("Port%d: %s %s %s\r\n",
           (int)port,
           status.linkUp ? "UP" : "DOWN",
           status.speed == LAN9370_LINK_SPEED_100M ? "100M" :
           status.speed == LAN9370_LINK_SPEED_10M  ? "10M"  : "1G",
           status.duplex == LAN9370_DUPLEX_FULL ? "Full" : "Half");
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_PrintAllPortsStatus(void)
{
    if (!isInitialized) return LAN9370_NOT_INITIALIZED;

    LAN9370_PortStatus_t status;
    printf("\r\n=== LAN9370 Port Status ===\r\n");
    printf("Port | Link | Speed | Duplex | Mode\r\n");
    printf("-----+------+-------+--------+--------\r\n");

    for (int port = 1; port <= 5; port++) {
        if (LAN9370_GetPortStatus((LAN9370_Port_t)port, &status) != LAN9370_OK) {
            printf("  %d  | ERR\r\n", port);
            continue;
        }
        char modeStr[8] = "-";
        if (port >= 1 && port <= 4) {
            LAN9370_T1_Mode_t m;
            if (LAN9370_GetT1MasterSlave((LAN9370_Port_t)port, &m) == LAN9370_OK) {
                strncpy(modeStr, (m == LAN9370_T1_MASTER) ? "Master" : "Slave", sizeof(modeStr) - 1);
                modeStr[sizeof(modeStr) - 1] = '\0';
            }
        } else {
            strncpy(modeStr, "RMII", sizeof(modeStr) - 1);
        }
        printf("  %d  | %s | %s | %s | %s\r\n",
               port,
               status.linkUp ? "UP  " : "DOWN",
               status.speed == LAN9370_LINK_SPEED_100M ? "100M " :
               status.speed == LAN9370_LINK_SPEED_10M  ? "10M  " : "1G   ",
               status.duplex == LAN9370_DUPLEX_FULL ? "Full" : "Half",
               modeStr);
    }
    printf("===========================\r\n\r\n");
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_ReadPortMibCounter(LAN9370_Port_t port, uint16_t mibIndex, uint32_t *value)
{
    uint8_t hwPortIndex;
    uint16_t ctrlAddr;
    uint16_t dataAddr;
    uint32_t ctrlVal;
    uint32_t start;

    if (!isInitialized || port < 1 || port > 5 || value == NULL) return LAN9370_INVALID_PARAM;

    hwPortIndex = (uint8_t)port - 1U;
    ctrlAddr = PORT_CTRL_ADDR(hwPortIndex, REG_PORT_MIB_CTRL_STAT__4);
    dataAddr = PORT_CTRL_ADDR(hwPortIndex, REG_PORT_MIB_DATA);

    ctrlVal = (uint32_t)MIB_COUNTER_READ | ((uint32_t)mibIndex << MIB_COUNTER_INDEX_S);
    if (LAN9370_SPI_WriteReg32(ctrlAddr, ctrlVal) != LAN9370_SPI_OK) return LAN9370_ERROR;

    start = HAL_GetTick();
    do {
        if (LAN9370_SPI_ReadReg32(ctrlAddr, &ctrlVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
        if ((ctrlVal & MIB_COUNTER_READ) == 0U) break;
    } while ((HAL_GetTick() - start) < LAN9370_MIB_READ_TIMEOUT_MS);

    if ((ctrlVal & MIB_COUNTER_READ) != 0U) return LAN9370_TIMEOUT;
    if (LAN9370_SPI_ReadReg32(dataAddr, value) != LAN9370_SPI_OK) return LAN9370_ERROR;
    return LAN9370_OK;
}

bool LAN9370_IsEthLinkUp(void)
{
    if (!isInitialized) return false;
    uint8_t mstp;
    if (LAN9370_SPI_ReadReg8_Retry(PORT_CTRL_ADDR(4U, REG_PORTn_MSTP_STATE), &mstp) == LAN9370_SPI_OK) {
        return (mstp & PORT_TX_ENABLE) ? true : false;
    }
    return false;
}

LAN9370_Status_t LAN9370_DumpRegisters(void)
{
    if (!isInitialized) return LAN9370_NOT_INITIALIZED;
    uint8_t v;
    printf("=== LAN9370 Register Dump ===\r\n");
    /* Read chip ID properly */
    uint8_t id[4];
    LAN9370_SPI_ReadReg8_Retry(REG_CHIP_ID0, &id[0]);
    LAN9370_SPI_ReadReg8_Retry(REG_CHIP_ID1, &id[1]);
    LAN9370_SPI_ReadReg8_Retry(REG_CHIP_ID2, &id[2]);
    LAN9370_SPI_ReadReg8_Retry(REG_CHIP_ID3, &id[3]);
    printf("ChipID: %02X%02X%02X%02X\r\n", id[0], id[1], id[2], id[3]);
    LAN9370_SPI_ReadReg8_Retry(REG_GLOBAL_CTRL_0, &v); printf("GCTRL0:  0x%02X\r\n", v);
    LAN9370_SPI_ReadReg8_Retry(REG_SW_RESET, &v);       printf("SW_RST:  0x%02X\r\n", v);
    LAN9370_SPI_ReadReg8_Retry(REG_LUE_CTRL_0, &v);     printf("LUE_CTL: 0x%02X\r\n", v);
    /* Port5 key regs */
    uint8_t hwP5 = 4U;
    LAN9370_SPI_ReadReg8_Retry(PORT_CTRL_ADDR(hwP5, REG_PORTn_STATUS), &v);    printf("P5_STS:  0x%02X\r\n", v);
    LAN9370_SPI_ReadReg8_Retry(PORT_CTRL_ADDR(hwP5, REG_PORTn_XMII_CTRL0), &v); printf("P5_XMII0:0x%02X\r\n", v);
    LAN9370_SPI_ReadReg8_Retry(PORT_CTRL_ADDR(hwP5, REG_PORTn_XMII_CTRL1), &v); printf("P5_XMII1:0x%02X\r\n", v);
    LAN9370_SPI_ReadReg8_Retry(PORT_CTRL_ADDR(hwP5, REG_PORTn_MSTP_STATE), &v);  printf("P5_MSTP: 0x%02X\r\n", v);
    /* LED registers */
    LAN9370_SPI_ReadReg8_Retry(REG_LED_CTRL0, &v);      printf("LED0:    0x%02X\r\n", v);
    LAN9370_SPI_ReadReg8_Retry(REG_LED_BLINK_CTRL, &v); printf("LED_BLK: 0x%02X\r\n", v);
    printf("============================\r\n");
    return LAN9370_OK;
}

/* =============================================================================
 * Fast Aging
 * ===========================================================================*/
LAN9370_Status_t LAN9370_SetFastAging(bool enable)
{
    if (!isInitialized) return LAN9370_NOT_INITIALIZED;
    uint8_t regVal;
    if (LAN9370_SPI_ReadReg8_Retry(REG_GLOBAL_CTRL_0, &regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (enable) regVal |= SW_FAST_MODE;
    else        regVal &= (uint8_t)(~SW_FAST_MODE);
    return (LAN9370_SPI_WriteReg8(REG_GLOBAL_CTRL_0, regVal) == LAN9370_SPI_OK) ? LAN9370_OK : LAN9370_ERROR;
}

/* =============================================================================
 * VLAN
 * ===========================================================================*/
LAN9370_Status_t LAN9370_SetVlanEnable(bool enable)
{
    if (!isInitialized) return LAN9370_NOT_INITIALIZED;

    /* Global VLAN enable in LUE_CTRL_0 */
    uint8_t regVal;
    if (LAN9370_SPI_ReadReg8_Retry(REG_LUE_CTRL_0, &regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (enable) regVal |= SW_VLAN_ENABLE;
    else        regVal &= (uint8_t)(~SW_VLAN_ENABLE);
    if (LAN9370_SPI_WriteReg8(REG_LUE_CTRL_0, regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;

    /* Per-port VLAN lookup enable (REG_PORT_LUE_CTRL at 0x0B00) */
    for (int p = 1; p <= 5; p++) {
        uint8_t hwPortIndex = (uint8_t)p - 1U;
        uint16_t lueAddr = PORT_CTRL_ADDR(hwPortIndex, REG_PORT_LUE_CTRL);
        uint8_t portLue;
        if (LAN9370_SPI_ReadReg8_Retry(lueAddr, &portLue) != LAN9370_SPI_OK) return LAN9370_ERROR;
        if (enable) portLue |= PORT_VLAN_LOOKUP_VID_0;
        else        portLue &= (uint8_t)(~PORT_VLAN_LOOKUP_VID_0);
        if (LAN9370_SPI_WriteReg8(lueAddr, portLue) != LAN9370_SPI_OK) return LAN9370_ERROR;
    }
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_GetVlanEnable(bool *enabled)
{
    if (!isInitialized || enabled == NULL) return LAN9370_INVALID_PARAM;
    uint8_t regVal;
    if (LAN9370_SPI_ReadReg8_Retry(REG_LUE_CTRL_0, &regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    *enabled = ((regVal & SW_VLAN_ENABLE) != 0U);
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_SetPortDefaultVlan(LAN9370_Port_t port, uint16_t vlanId)
{
    if (!isInitialized || port < 1 || port > 5 || vlanId == 0 || vlanId > 4094) return LAN9370_INVALID_PARAM;
    uint8_t hwPortIndex = (uint8_t)port - 1U;
    /* REG_PORT_DEFAULT_VID is 16-bit at per-port offset 0x0000.
     * Write as two 8-bit registers to avoid byte-order confusion
     * between SPI 16-bit MSB-first and the register's byte layout. */
    uint16_t regBase = PORT_CTRL_ADDR(hwPortIndex, REG_PORT_DEFAULT_VID);
    if (LAN9370_SPI_WriteReg8(regBase, (uint8_t)(vlanId & 0xFF)) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (LAN9370_SPI_WriteReg8((uint16_t)(regBase + 1), (uint8_t)((vlanId >> 8) & 0x0F)) != LAN9370_SPI_OK) return LAN9370_ERROR;
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_GetPortDefaultVlan(LAN9370_Port_t port, uint16_t *vlanId)
{
    if (!isInitialized || port < 1 || port > 5 || vlanId == NULL) return LAN9370_INVALID_PARAM;
    uint8_t hwPortIndex = (uint8_t)port - 1U;
    uint16_t regBase = PORT_CTRL_ADDR(hwPortIndex, REG_PORT_DEFAULT_VID);
    uint8_t lo = 0, hi = 0;
    if (LAN9370_SPI_ReadReg8_Retry(regBase, &lo) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (LAN9370_SPI_ReadReg8_Retry((uint16_t)(regBase + 1), &hi) != LAN9370_SPI_OK) return LAN9370_ERROR;
    *vlanId = (uint16_t)(((uint16_t)(hi & 0x0F) << 8) | lo);
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_PrintVlanConfig(void)
{
    if (!isInitialized) return LAN9370_NOT_INITIALIZED;

    bool vlanEnabled = false;
    LAN9370_GetVlanEnable(&vlanEnabled);

    printf("\r\n=== VLAN Configuration ===\r\n");
    printf("VLAN: %s\r\n", vlanEnabled ? "ENABLED" : "DISABLED");
    printf("Port | Default VID | Egress Members\r\n");
    printf("-----+-------------+----------------\r\n");

    for (int port = 1; port <= 5; port++) {
        uint16_t vid = 0;
        uint8_t member = 0;
        LAN9370_GetPortDefaultVlan((LAN9370_Port_t)port, &vid);
        LAN9370_GetPortMembership((LAN9370_Port_t)port, &member);
        printf("  %d  |     %4u    |", port, (unsigned)vid);
        for (int m = 1; m <= 5; m++) {
            printf(" %c", (member & (1 << (m - 1))) ? (char)('0' + m) : ' ');
        }
        printf("\r\n");
    }
    printf("===========================\r\n\r\n");
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_GetPortMembership(LAN9370_Port_t port, uint8_t *memberMask)
{
    if (!isInitialized || port < 1 || port > 5 || memberMask == NULL) return LAN9370_INVALID_PARAM;
    uint8_t hwPortIndex = (uint8_t)port - 1U;
    uint16_t regAddr = PORT_CTRL_ADDR(hwPortIndex, REG_PORT_VLAN_MEMBERSHIP__4);
    uint32_t regVal = 0;
    if (LAN9370_SPI_ReadReg32_Retry(regAddr, &regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    *memberMask = (uint8_t)(regVal & 0x1FU);
    return LAN9370_OK;
}

/* =============================================================================
 * VLAN Table Entry — program hardware VLAN table so traffic can flow.
 * Without a VLAN table entry for a given VID, the switch drops all frames
 * on that VLAN even if PVIDs are set correctly.
 * ===========================================================================*/
LAN9370_Status_t LAN9370_WriteVlanEntry(uint16_t vid, uint8_t memberMask)
{
    if (!isInitialized || vid == 0 || vid > 4094) return LAN9370_INVALID_PARAM;

    /* Format VLAN table entry:
     * vlan_table[0] = VLAN_VALID | (vid & VLAN_FID_M)    → REG 0x0400
     * vlan_table[1] = untag membership (0 = all tagged)  → REG 0x0404
     * vlan_table[2] = port membership                     → REG 0x0408
     */
    uint32_t entry  = VLAN_VALID | (uint32_t)(vid & 0x7F);
    uint32_t untag  = (uint32_t)(memberMask & 0x1FU);  /* All members untag → no 802.1Q tags on wire */
    uint32_t ports  = (uint32_t)(memberMask & 0x1FU);

    /* Write table data */
    if (LAN9370_SPI_WriteReg32(REG_SW_VLAN_ENTRY__4, entry) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (LAN9370_SPI_WriteReg32(REG_SW_VLAN_ENTRY_UNTAG__4, untag) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (LAN9370_SPI_WriteReg32(REG_SW_VLAN_ENTRY_PORTS__4, ports) != LAN9370_SPI_OK) return LAN9370_ERROR;

    /* Trigger VLAN table write */
    if (LAN9370_SPI_WriteReg16(REG_SW_VLAN_ENTRY_INDEX__2, vid & VLAN_INDEX_M) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (LAN9370_SPI_WriteReg8(REG_SW_VLAN_CTRL, (uint8_t)(VLAN_START | VLAN_WRITE)) != LAN9370_SPI_OK) return LAN9370_ERROR;

    /* Wait for completion (poll VLAN_START bit) */
    uint32_t timeout = 100;
    while (timeout--) {
        uint8_t ctrl;
        if (LAN9370_SPI_ReadReg8_Retry(REG_SW_VLAN_CTRL, &ctrl) != LAN9370_SPI_OK) return LAN9370_ERROR;
        if ((ctrl & VLAN_START) == 0U) break;
        HAL_Delay(1);
    }
    if (timeout == 0) return LAN9370_TIMEOUT;

    /* Clear VLAN_CTRL */
    if (LAN9370_SPI_WriteReg8(REG_SW_VLAN_CTRL, 0) != LAN9370_SPI_OK) return LAN9370_ERROR;

    printf("VLAN entry VID=%u members=0x%02X written\r\n", (unsigned)vid, memberMask);
    return LAN9370_OK;
}

/* =============================================================================
 * Mirror
 * ===========================================================================*/
LAN9370_Status_t LAN9370_SetPortMirror(LAN9370_Port_t srcPort, LAN9370_Port_t dstPort, bool ingress)
{
    /* Use real hardware mirror registers per KSZ9477/LAN937x:
     * - P_MIRROR_CTRL (0x0800): per-port mirror control (RX/TX bits)
     * - S_MIRROR_CTRL (0x0370): global mirror RX/TX mode
     * - SNIFFER port: the port that receives mirrored traffic */
    if (!isInitialized || srcPort < 1 || srcPort > 5 || (dstPort != 0 && (dstPort < 1 || dstPort > 5)))
        return LAN9370_INVALID_PARAM;

    uint8_t srcHw = (uint8_t)srcPort - 1U;
    uint16_t srcMirrorAddr = PORT_CTRL_ADDR(srcHw, REG_PORT_MRI_MIRROR_CTRL);

    if (dstPort == 0) {
        /* Disable mirror on source port */
        uint8_t val = 0;
        if (LAN9370_SPI_ReadReg8_Retry(srcMirrorAddr, &val) != LAN9370_SPI_OK) return LAN9370_ERROR;
        val &= (uint8_t)(~(PORT_MIRROR_RX | PORT_MIRROR_TX));
        if (LAN9370_SPI_WriteReg8(srcMirrorAddr, val) != LAN9370_SPI_OK) return LAN9370_ERROR;
        return LAN9370_OK;
    }

    /* Enable mirror on source port (both RX and TX) */
    uint8_t srcVal = PORT_MIRROR_RX | PORT_MIRROR_TX;
    if (LAN9370_SPI_WriteReg8(srcMirrorAddr, srcVal) != LAN9370_SPI_OK) return LAN9370_ERROR;

    /* Set sniffer mode on destination port */
    uint8_t dstHw = (uint8_t)dstPort - 1U;
    uint16_t dstMirrorAddr = PORT_CTRL_ADDR(dstHw, REG_PORT_MRI_MIRROR_CTRL);
    uint8_t dstVal;
    if (LAN9370_SPI_ReadReg8_Retry(dstMirrorAddr, &dstVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    dstVal |= PORT_MIRROR_SNIFFER;
    if (LAN9370_SPI_WriteReg8(dstMirrorAddr, dstVal) != LAN9370_SPI_OK) return LAN9370_ERROR;

    /* Enable global mirror RX+TX mode */
    uint8_t globalVal;
    if (LAN9370_SPI_ReadReg8_Retry(REG_SW_MRI_CTRL_0, &globalVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    globalVal |= SW_MIRROR_RX_TX;
    if (LAN9370_SPI_WriteReg8(REG_SW_MRI_CTRL_0, globalVal) != LAN9370_SPI_OK) return LAN9370_ERROR;

    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_GetPortMirror(LAN9370_Port_t srcPort, LAN9370_Port_t *dstPort, bool *ingress)
{
    if (!isInitialized || srcPort < 1 || srcPort > 5 || dstPort == NULL) return LAN9370_INVALID_PARAM;

    uint8_t srcHw = (uint8_t)srcPort - 1U;
    uint16_t srcMirrorAddr = PORT_CTRL_ADDR(srcHw, REG_PORT_MRI_MIRROR_CTRL);
    uint8_t val;
    if (LAN9370_SPI_ReadReg8_Retry(srcMirrorAddr, &val) != LAN9370_SPI_OK) return LAN9370_ERROR;

    if ((val & (PORT_MIRROR_RX | PORT_MIRROR_TX)) == 0) {
        *dstPort = 0;
        if (ingress) *ingress = false;
    } else {
        /* Find which port is the sniffer */
        *dstPort = 0;
        for (int p = 1; p <= 5; p++) {
            uint8_t hw = (uint8_t)p - 1U;
            uint16_t addr = PORT_CTRL_ADDR(hw, REG_PORT_MRI_MIRROR_CTRL);
            uint8_t pval;
            if (LAN9370_SPI_ReadReg8_Retry(addr, &pval) == LAN9370_SPI_OK) {
                if (pval & PORT_MIRROR_SNIFFER) { *dstPort = (LAN9370_Port_t)p; break; }
            }
        }
        if (ingress) *ingress = (val & PORT_MIRROR_RX) ? true : false;
    }
    return LAN9370_OK;
}

/* =============================================================================
 * PTP / gPTP
 * ===========================================================================*/
LAN9370_Status_t LAN9370_SetPtpEnable(bool enable)
{
    /* Use real PTP MSG_CONF1 register (0x0514) per KSZ9477/LAN937x */
    if (!isInitialized) return LAN9370_NOT_INITIALIZED;
    uint8_t regVal;
    if (LAN9370_SPI_ReadReg8_Retry(REG_PTP_MSG_CONF1, &regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (enable) regVal |= PTP_ENABLE;
    else        regVal &= (uint8_t)(~PTP_ENABLE);
    return (LAN9370_SPI_WriteReg8(REG_PTP_MSG_CONF1, regVal) == LAN9370_SPI_OK) ? LAN9370_OK : LAN9370_ERROR;
}

LAN9370_Status_t LAN9370_GetPtpEnable(bool *enabled)
{
    if (!isInitialized || enabled == NULL) return LAN9370_INVALID_PARAM;
    uint8_t regVal;
    if (LAN9370_SPI_ReadReg8_Retry(REG_PTP_MSG_CONF1, &regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    *enabled = ((regVal & PTP_ENABLE) != 0U);
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_SetGptpEnable(bool enable)
{
    /* gPTP = 802.1AS mode: set PTP_802_1AS bit alongside PTP_ENABLE */
    if (!isInitialized) return LAN9370_NOT_INITIALIZED;
    uint8_t regVal;
    if (LAN9370_SPI_ReadReg8_Retry(REG_PTP_MSG_CONF1, &regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    if (enable) { regVal |= PTP_ENABLE; regVal |= PTP_802_1AS; }
    else        { regVal &= (uint8_t)(~PTP_802_1AS); }
    return (LAN9370_SPI_WriteReg8(REG_PTP_MSG_CONF1, regVal) == LAN9370_SPI_OK) ? LAN9370_OK : LAN9370_ERROR;
}

LAN9370_Status_t LAN9370_GetGptpEnable(bool *enabled)
{
    if (!isInitialized || enabled == NULL) return LAN9370_INVALID_PARAM;
    uint8_t regVal;
    if (LAN9370_SPI_ReadReg8_Retry(REG_PTP_MSG_CONF1, &regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    *enabled = ((regVal & PTP_802_1AS) != 0U);
    return LAN9370_OK;
}

/* =============================================================================
 * Static MAC Table (software shadow)
 * ===========================================================================*/
LAN9370_Status_t LAN9370_FlushStaticMAC(void)
{
    if (!isInitialized) return LAN9370_NOT_INITIALIZED;
    s_staticMacCount = 0;
    memset(s_staticMacShadow, 0, sizeof(s_staticMacShadow));

    uint8_t regVal;
    if (LAN9370_SPI_ReadReg8_Retry(REG_GLOBAL_CTRL_0, &regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    regVal |= SW_FLUSH_STA_MAC_TABLE;
    if (LAN9370_SPI_WriteReg8(REG_GLOBAL_CTRL_0, regVal) != LAN9370_SPI_OK) return LAN9370_ERROR;
    HAL_Delay(10);
    return LAN9370_OK;
}

LAN9370_Status_t LAN9370_AddStaticMac(const LAN9370_StaticMacEntry_t *entry)
{
    if (entry == NULL || entry->vlanId == 0 || entry->vlanId > 4094 || entry->destPorts == 0)
        return LAN9370_INVALID_PARAM;
    if (s_staticMacCount >= LAN9370_STATIC_MAC_SHADOW_SIZE) return LAN9370_ERROR;
    memcpy(&s_staticMacShadow[s_staticMacCount], entry, sizeof(LAN9370_StaticMacEntry_t));
    s_staticMacCount++;
    return LAN9370_OK;
}

int LAN9370_PrintStaticMacTable(void)
{
    printf("Static MAC shadow entries: %u\r\n", (unsigned)s_staticMacCount);
    for (uint8_t i = 0; i < s_staticMacCount; i++) {
        printf("[%u] %02X:%02X:%02X:%02X:%02X:%02X vid=%u ports=0x%02X\r\n",
               i,
               s_staticMacShadow[i].mac[0], s_staticMacShadow[i].mac[1],
               s_staticMacShadow[i].mac[2], s_staticMacShadow[i].mac[3],
               s_staticMacShadow[i].mac[4], s_staticMacShadow[i].mac[5],
               s_staticMacShadow[i].vlanId,
               s_staticMacShadow[i].destPorts);
    }
    return 0;
}
