/**
  ******************************************************************************
  * @file    lan9370_smi.h
  * @brief   LAN9370 SMI stub header (MDC/MDIO not used in this project)
  ******************************************************************************
  */

#ifndef __LAN9370_SMI_H
#define __LAN9370_SMI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32h7xx_hal.h"

typedef enum {
    LAN9370_SMI_OK = 0,
    LAN9370_SMI_ERROR,
    LAN9370_SMI_TIMEOUT,
    LAN9370_SMI_BUSY
} LAN9370_SMI_Status_t;

LAN9370_SMI_Status_t LAN9370_SMI_Init(void);
LAN9370_SMI_Status_t LAN9370_SMI_Read(uint8_t phyAddr, uint8_t regAddr, uint16_t *data);
LAN9370_SMI_Status_t LAN9370_SMI_Write(uint8_t phyAddr, uint8_t regAddr, uint16_t data);

#ifdef __cplusplus
}
#endif

#endif /* __LAN9370_SMI_H */
