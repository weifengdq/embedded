/**
  ******************************************************************************
  * @file    lan9370_spi.h
  * @brief   LAN9370 SPI Interface Driver Header
  *          Adapted for STM32H7 (H7 HAL include)
  *          CS: PG10 (software GPIO)
  ******************************************************************************
  */

#ifndef __LAN9370_SPI_H
#define __LAN9370_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx_hal.h"

typedef enum {
    LAN9370_SPI_OK = 0,
    LAN9370_SPI_ERROR,
    LAN9370_SPI_TIMEOUT,
    LAN9370_SPI_BUSY
} LAN9370_SPI_Status_t;

LAN9370_SPI_Status_t LAN9370_SPI_Init(SPI_HandleTypeDef *hspi);
LAN9370_SPI_Status_t LAN9370_SPI_ReadReg8(uint16_t addr, uint8_t *data);
LAN9370_SPI_Status_t LAN9370_SPI_WriteReg8(uint16_t addr, uint8_t data);
LAN9370_SPI_Status_t LAN9370_SPI_ReadReg16(uint16_t addr, uint16_t *data);
LAN9370_SPI_Status_t LAN9370_SPI_WriteReg16(uint16_t addr, uint16_t data);
LAN9370_SPI_Status_t LAN9370_SPI_ReadReg32(uint16_t addr, uint32_t *data);
LAN9370_SPI_Status_t LAN9370_SPI_WriteReg32(uint16_t addr, uint32_t data);
LAN9370_SPI_Status_t LAN9370_SPI_ReadBurst(uint16_t addr, uint8_t *buffer, uint16_t length);
LAN9370_SPI_Status_t LAN9370_SPI_WriteBurst(uint16_t addr, const uint8_t *buffer, uint16_t length);

/* Retry helpers – read up to 3 times, return value that matches >= 2 times */
LAN9370_SPI_Status_t LAN9370_SPI_ReadReg8_Retry(uint16_t addr, uint8_t *data);
LAN9370_SPI_Status_t LAN9370_SPI_ReadReg16_Retry(uint16_t addr, uint16_t *data);
LAN9370_SPI_Status_t LAN9370_SPI_ReadReg32_Retry(uint16_t addr, uint32_t *data);

#ifdef __cplusplus
}
#endif

#endif /* __LAN9370_SPI_H */
