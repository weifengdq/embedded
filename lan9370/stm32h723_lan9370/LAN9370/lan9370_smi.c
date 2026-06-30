/**
  ******************************************************************************
  * @file    lan9370_smi.c
  * @brief   LAN9370 SMI stub - MDC/MDIO are not connected in this project.
  *          All configuration is performed exclusively via SPI.
  ******************************************************************************
  */

#include "lan9370_smi.h"

LAN9370_SMI_Status_t LAN9370_SMI_Init(void)
{
    /* MDC/MDIO not connected - return OK */
    return LAN9370_SMI_OK;
}

LAN9370_SMI_Status_t LAN9370_SMI_Read(uint8_t phyAddr, uint8_t regAddr, uint16_t *data)
{
    (void)phyAddr;
    (void)regAddr;
    if (data != NULL) *data = 0;
    return LAN9370_SMI_OK;
}

LAN9370_SMI_Status_t LAN9370_SMI_Write(uint8_t phyAddr, uint8_t regAddr, uint16_t data)
{
    (void)phyAddr;
    (void)regAddr;
    (void)data;
    return LAN9370_SMI_OK;
}
