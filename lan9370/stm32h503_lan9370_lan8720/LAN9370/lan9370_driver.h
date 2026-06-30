/**
  ******************************************************************************
  * @file    lan9370_driver.h
  * @brief   LAN9370 5-Port 100BASE-T1 Ethernet Switch Driver
  * @details High-level API for LAN9370 configuration and management
  ******************************************************************************
  */

#ifndef __LAN9370_DRIVER_H
#define __LAN9370_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "lan9370_reg.h"
#include "lan9370_spi.h"
#include "lan9370_smi.h"

/* =============================================================================
 * Type Definitions
 * ===========================================================================*/
typedef enum {
    LAN9370_OK = 0,
    LAN9370_ERROR,
    LAN9370_TIMEOUT,
    LAN9370_NOT_INITIALIZED,
    LAN9370_INVALID_PARAM
} LAN9370_Status_t;

typedef enum {
    LAN9370_PORT_1 = 1,
    LAN9370_PORT_2 = 2,
    LAN9370_PORT_3 = 3,
    LAN9370_PORT_4 = 4,
    LAN9370_PORT_5 = 5  /* Host port (RGMII/RMII) */
} LAN9370_Port_t;

typedef enum {
    LAN9370_LINK_SPEED_10M = 0,
    LAN9370_LINK_SPEED_100M,
    LAN9370_LINK_SPEED_1G
} LAN9370_LinkSpeed_t;

typedef enum {
    LAN9370_DUPLEX_HALF = 0,
    LAN9370_DUPLEX_FULL
} LAN9370_Duplex_t;

typedef enum {
    LAN9370_T1_SLAVE = 0,
    LAN9370_T1_MASTER = 1
} LAN9370_T1_Mode_t;

typedef struct {
    bool linkUp;
    LAN9370_LinkSpeed_t speed;
    LAN9370_Duplex_t duplex;
    bool flowControlTx;
    bool flowControlRx;
} LAN9370_PortStatus_t;

typedef struct {
    uint32_t chipId;
    uint8_t revisionId;
    char chipName[32];
} LAN9370_ChipInfo_t;

typedef struct {
    bool enabled;
    LAN9370_T1_Mode_t masterSlaveMode;
    bool autoNegEnable;
    bool loopbackEnable;
} LAN9370_T1_Config_t;

typedef struct {
    uint8_t mac[6];
    uint16_t vlanId;
    uint8_t destPorts;
} LAN9370_StaticMacEntry_t;

/* =============================================================================
 * Function Prototypes - Initialization
 * ===========================================================================*/

/**
 * @brief Initialize LAN9370 driver and hardware
 * @param hspi Pointer to SPI handle
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_Init(SPI_HandleTypeDef *hspi);

/**
 * @brief Hardware reset LAN9370 via nRESET pin (PB7)
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_HardwareReset(void);

/**
 * @brief Software reset LAN9370
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_SoftwareReset(void);

/**
 * @brief Get chip information
 * @param info Pointer to store chip information
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_GetChipInfo(LAN9370_ChipInfo_t *info);

/* =============================================================================
 * Function Prototypes - Port Configuration
 * ===========================================================================*/

/**
 * @brief Enable or disable a port
 * @param port Port number (1-5)
 * @param enable true to enable, false to disable
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_SetPortEnable(LAN9370_Port_t port, bool enable);

/**
 * @brief Configure 100BASE-T1 port as Master or Slave
 * @param port Port number (1-4)
 * @param mode Master or Slave mode
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_SetT1MasterSlave(LAN9370_Port_t port, LAN9370_T1_Mode_t mode);

/**
 * @brief Read configured 100BASE-T1 Master/Slave mode
 * @param port Port number (1-4)
 * @param mode Pointer to store mode
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_GetT1MasterSlave(LAN9370_Port_t port, LAN9370_T1_Mode_t *mode);

/**
 * @brief Get port link status
 * @param port Port number (1-5)
 * @param status Pointer to store port status
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_GetPortStatus(LAN9370_Port_t port, LAN9370_PortStatus_t *status);

/**
 * @brief Configure T1 port settings
 * @param port Port number (1-4)
 * @param config T1 configuration
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_ConfigureT1Port(LAN9370_Port_t port, const LAN9370_T1_Config_t *config);

/* =============================================================================
 * Function Prototypes - PHY Operations (via SPI)
 * ===========================================================================*/

/**
 * @brief Read PHY register via SPI
 * @param port Port number (1-4)
 * @param regAddr PHY register address
 * @param data Pointer to store read data
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_PHY_ReadReg(LAN9370_Port_t port, uint8_t regAddr, uint16_t *data);

/**
 * @brief Write PHY register via SPI
 * @param port Port number (1-4)
 * @param regAddr PHY register address
 * @param data Data to write
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_PHY_WriteReg(LAN9370_Port_t port, uint8_t regAddr, uint16_t data);

/* =============================================================================
 * Function Prototypes - Switch Operations
 * ===========================================================================*/

/**
 * @brief Enable or disable MAC learning
 * @param enable true to enable, false to disable
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_SetMACLearning(bool enable);

/**
 * @brief Flush dynamic MAC address table
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_FlushDynamicMAC(void);

/**
 * @brief Enable or disable fast aging
 * @param enable true to enable, false to disable
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_SetFastAging(bool enable);

/**
 * @brief Enable or disable VLAN lookup
 * @param enable true to enable VLAN, false to disable
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_SetVlanEnable(bool enable);

/**
 * @brief Read VLAN lookup enable state
 * @param enabled Pointer to store VLAN enable state
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_GetVlanEnable(bool *enabled);

/**
 * @brief Set default VLAN ID of one port
 * @param port Port number (1-5)
 * @param vlanId VLAN ID (1-4094)
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_SetPortDefaultVlan(LAN9370_Port_t port, uint16_t vlanId);

/**
 * @brief Configure egress membership bitmap for one port
 * @param port Port number (1-5)
 * @param memberMask Forwarding member bitmap (bit0..bit4 = port1..port5)
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_SetPortMembership(LAN9370_Port_t port, uint8_t memberMask);

/**
 * @brief Read egress membership bitmap for one port
 * @param port Port number (1-5)
 * @param memberMask Pointer to store member bitmap
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_GetPortMembership(LAN9370_Port_t port, uint8_t *memberMask);

/**
 * @brief Read one port MIB counter by index
 * @param port Port number (1-5)
 * @param mibIndex MIB counter index
 * @param value Pointer to store counter value (32-bit low part)
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_ReadPortMibCounter(LAN9370_Port_t port, uint16_t mibIndex, uint32_t *value);

/**
 * @brief Print brief MIB counters for one port
 * @param port Port number (1-5)
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_PrintPortMib(LAN9370_Port_t port);

/**
 * @brief Flush static MAC table in switch and software shadow
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_FlushStaticMAC(void);

/**
 * @brief Add one static MAC entry to software shadow table
 * @param entry Static MAC entry
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_AddStaticMac(const LAN9370_StaticMacEntry_t *entry);

/**
 * @brief Print software shadow static MAC table
 * @return 0 on success
 */
int LAN9370_PrintStaticMacTable(void);

/* =============================================================================
 * Function Prototypes - Diagnostics
 * ===========================================================================*/

/**
 * @brief Dump all switch registers for debugging
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_DumpRegisters(void);

/**
 * @brief Print detailed port status
 * @param port Port number (1-5)
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_PrintPortStatus(LAN9370_Port_t port);

/**
 * @brief Check if LAN9370 is responding
 * @return LAN9370_OK if responsive
 */
LAN9370_Status_t LAN9370_CheckCommunication(void);

/**
 * @brief Print status of all ports in aligned format
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_PrintAllPortsStatus(void);

/**
 * @brief Set port mirror (source->destination)
 * @param srcPort Source port to mirror (1-5)
 * @param dstPort Destination port (1-5), 0 to disable mirror
 * @param ingress true to mirror ingress, false for egress
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_SetPortMirror(LAN9370_Port_t srcPort, LAN9370_Port_t dstPort, bool ingress);

/**
 * @brief Get current mirror configuration
 * @param srcPort Source port (1-5)
 * @param dstPort Pointer to store destination port
 * @param ingress Pointer to store mirror direction
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_GetPortMirror(LAN9370_Port_t srcPort, LAN9370_Port_t *dstPort, bool *ingress);

/**
 * @brief Read port default VLAN ID
 * @param port Port number (1-5)
 * @param vlanId Pointer to store VLAN ID
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_GetPortDefaultVlan(LAN9370_Port_t port, uint16_t *vlanId);

/**
 * @brief Print current VLAN configuration for all ports
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_PrintVlanConfig(void);

/* =============================================================================
 * Function Prototypes - MIIM (MDIO Master) for External PHY Access
 * ===========================================================================*/

/**
 * @brief Read external PHY register via LAN9370 MIIM master over SPI
 * @param phyAddr PHY address (0-31) on the MDIO bus
 * @param regAddr PHY register address (0-31)
 * @param data Pointer to store 16-bit register data
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_MIIM_Read(uint8_t phyAddr, uint8_t regAddr, uint16_t *data);

/**
 * @brief Write external PHY register via LAN9370 MIIM master over SPI
 * @param phyAddr PHY address (0-31) on the MDIO bus
 * @param regAddr PHY register address (0-31)
 * @param data 16-bit data to write
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_MIIM_Write(uint8_t phyAddr, uint8_t regAddr, uint16_t data);

/**
 * @brief Enable or disable PTP
 * @param enable true to enable PTP
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_SetPtpEnable(bool enable);

/**
 * @brief Get PTP status
 * @param enabled Pointer to store PTP enable state
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_GetPtpEnable(bool *enabled);

/**
 * @brief Enable or disable gPTP
 * @param enable true to enable gPTP
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_SetGptpEnable(bool enable);

/**
 * @brief Get gPTP status
 * @param enabled Pointer to store gPTP enable state
 * @return LAN9370_OK on success
 */
LAN9370_Status_t LAN9370_GetGptpEnable(bool *enabled);

#ifdef __cplusplus
}
#endif

#endif /* __LAN9370_DRIVER_H */
