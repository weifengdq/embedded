/**
  ******************************************************************************
  * @file    lan9370_driver.h
  * @brief   LAN9370 5-Port 100BASE-T1 Ethernet Switch Driver
  *          Adapted for STM32H723
  *          nRST: PC3
  ******************************************************************************
  */

#ifndef __LAN9370_DRIVER_H
#define __LAN9370_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx_hal.h"
#include "lan9370_reg.h"
#include "lan9370_spi.h"
#include "lan9370_smi.h"

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
    LAN9370_PORT_5 = 5
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
    uint8_t mac[6];
    uint16_t vlanId;
    uint8_t destPorts;
} LAN9370_StaticMacEntry_t;

/* Initialization */
LAN9370_Status_t LAN9370_Init(SPI_HandleTypeDef *hspi);
LAN9370_Status_t LAN9370_HardwareReset(void);
LAN9370_Status_t LAN9370_SoftwareReset(void);
LAN9370_Status_t LAN9370_GetChipInfo(LAN9370_ChipInfo_t *info);

/* Port Configuration */
LAN9370_Status_t LAN9370_SetPortEnable(LAN9370_Port_t port, bool enable);
LAN9370_Status_t LAN9370_SetT1MasterSlave(LAN9370_Port_t port, LAN9370_T1_Mode_t mode);
LAN9370_Status_t LAN9370_GetT1MasterSlave(LAN9370_Port_t port, LAN9370_T1_Mode_t *mode);
LAN9370_Status_t LAN9370_GetPortStatus(LAN9370_Port_t port, LAN9370_PortStatus_t *status);
LAN9370_Status_t LAN9370_ConfigurePort5Rmii(void);
LAN9370_Status_t LAN9370_TryPort5Xmii1(uint8_t xmii1_val);

/* PHY Operations via SPI */
LAN9370_Status_t LAN9370_PHY_ReadReg(LAN9370_Port_t port, uint8_t regAddr, uint16_t *data);
LAN9370_Status_t LAN9370_PHY_WriteReg(LAN9370_Port_t port, uint8_t regAddr, uint16_t data);

/* Switch Operations */
LAN9370_Status_t LAN9370_SetMACLearning(bool enable);
LAN9370_Status_t LAN9370_SetPortMembership(LAN9370_Port_t port, uint8_t memberMask);
LAN9370_Status_t LAN9370_GetPortMembership(LAN9370_Port_t port, uint8_t *memberMask);
LAN9370_Status_t LAN9370_FlushDynamicMAC(void);
LAN9370_Status_t LAN9370_SetFastAging(bool enable);

/* VLAN */
LAN9370_Status_t LAN9370_SetVlanEnable(bool enable);
LAN9370_Status_t LAN9370_GetVlanEnable(bool *enabled);
LAN9370_Status_t LAN9370_SetPortDefaultVlan(LAN9370_Port_t port, uint16_t vlanId);
LAN9370_Status_t LAN9370_GetPortDefaultVlan(LAN9370_Port_t port, uint16_t *vlanId);
LAN9370_Status_t LAN9370_PrintVlanConfig(void);

/* VLAN Table Entry (program hardware VLAN table so traffic can flow) */
LAN9370_Status_t LAN9370_WriteVlanEntry(uint16_t vid, uint8_t memberMask);

/* Mirror */
LAN9370_Status_t LAN9370_SetPortMirror(LAN9370_Port_t srcPort, LAN9370_Port_t dstPort, bool ingress);
LAN9370_Status_t LAN9370_GetPortMirror(LAN9370_Port_t srcPort, LAN9370_Port_t *dstPort, bool *ingress);

/* PTP / gPTP */
LAN9370_Status_t LAN9370_SetPtpEnable(bool enable);
LAN9370_Status_t LAN9370_GetPtpEnable(bool *enabled);
LAN9370_Status_t LAN9370_SetGptpEnable(bool enable);
LAN9370_Status_t LAN9370_GetGptpEnable(bool *enabled);

/* Static MAC */
LAN9370_Status_t LAN9370_FlushStaticMAC(void);
LAN9370_Status_t LAN9370_AddStaticMac(const LAN9370_StaticMacEntry_t *entry);
int             LAN9370_PrintStaticMacTable(void);

/* Diagnostics */
LAN9370_Status_t LAN9370_PrintPortStatus(LAN9370_Port_t port);
LAN9370_Status_t LAN9370_PrintAllPortsStatus(void);
LAN9370_Status_t LAN9370_DumpRegisters(void);
bool             LAN9370_IsEthLinkUp(void);
LAN9370_Status_t LAN9370_ReadPortMibCounter(LAN9370_Port_t port, uint16_t mibIndex, uint32_t *value);

#ifdef __cplusplus
}
#endif

#endif /* __LAN9370_DRIVER_H */
