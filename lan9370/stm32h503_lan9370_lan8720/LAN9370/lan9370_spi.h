/**
  ******************************************************************************
  * @file    lan9370_spi.h
  * @brief   LAN9370 SPI Interface Driver Header
  ******************************************************************************
  */

#ifndef __LAN9370_SPI_H
#define __LAN9370_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "stm32h5xx_hal.h"

/* =============================================================================
 * Type Definitions
 * ===========================================================================*/
typedef enum {
    LAN9370_SPI_OK = 0,
    LAN9370_SPI_ERROR,
    LAN9370_SPI_TIMEOUT,
    LAN9370_SPI_BUSY
} LAN9370_SPI_Status_t;

/* =============================================================================
 * Function Prototypes
 * ===========================================================================*/

/**
 * @brief Initialize SPI interface for LAN9370
 * @param hspi Pointer to SPI handle
 * @return LAN9370_SPI_OK on success
 */
LAN9370_SPI_Status_t LAN9370_SPI_Init(SPI_HandleTypeDef *hspi);

/**
 * @brief Read 8-bit register
 * @param addr Register address (16-bit)
 * @param data Pointer to store read data
 * @return LAN9370_SPI_OK on success
 */
LAN9370_SPI_Status_t LAN9370_SPI_ReadReg8(uint16_t addr, uint8_t *data);

/**
 * @brief Write 8-bit register
 * @param addr Register address (16-bit)
 * @param data Data to write
 * @return LAN9370_SPI_OK on success
 */
LAN9370_SPI_Status_t LAN9370_SPI_WriteReg8(uint16_t addr, uint8_t data);

/**
 * @brief Read 16-bit register
 * @param addr Register address (16-bit)
 * @param data Pointer to store read data
 * @return LAN9370_SPI_OK on success
 */
LAN9370_SPI_Status_t LAN9370_SPI_ReadReg16(uint16_t addr, uint16_t *data);

/**
 * @brief Write 16-bit register
 * @param addr Register address (16-bit)
 * @param data Data to write
 * @return LAN9370_SPI_OK on success
 */
LAN9370_SPI_Status_t LAN9370_SPI_WriteReg16(uint16_t addr, uint16_t data);

/**
 * @brief Read 32-bit register
 * @param addr Register address (16-bit)
 * @param data Pointer to store read data
 * @return LAN9370_SPI_OK on success
 */
LAN9370_SPI_Status_t LAN9370_SPI_ReadReg32(uint16_t addr, uint32_t *data);

/**
 * @brief Write 32-bit register
 * @param addr Register address (16-bit)
 * @param data Data to write
 * @return LAN9370_SPI_OK on success
 */
LAN9370_SPI_Status_t LAN9370_SPI_WriteReg32(uint16_t addr, uint32_t data);

/**
 * @brief Read multiple bytes
 * @param addr Starting register address
 * @param buffer Buffer to store read data
 * @param length Number of bytes to read
 * @return LAN9370_SPI_OK on success
 */
LAN9370_SPI_Status_t LAN9370_SPI_ReadBurst(uint16_t addr, uint8_t *buffer, uint16_t length);

/**
 * @brief Write multiple bytes
 * @param addr Starting register address
 * @param buffer Buffer containing data to write
 * @param length Number of bytes to write
 * @return LAN9370_SPI_OK on success
 */
LAN9370_SPI_Status_t LAN9370_SPI_WriteBurst(uint16_t addr, const uint8_t *buffer, uint16_t length);

/**
 * @brief Read multiple bytes using DMA if available
 * @param addr Starting register address
 * @param buffer Buffer to store read data
 * @param length Number of bytes to read
 * @return LAN9370_SPI_OK on success
 */
LAN9370_SPI_Status_t LAN9370_SPI_ReadBurstDMA(uint16_t addr, uint8_t *buffer, uint16_t length);

/**
 * @brief Write multiple bytes using DMA if available
 * @param addr Starting register address
 * @param buffer Buffer containing data to write
 * @param length Number of bytes to write
 * @return LAN9370_SPI_OK on success
 */
LAN9370_SPI_Status_t LAN9370_SPI_WriteBurstDMA(uint16_t addr, const uint8_t *buffer, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* __LAN9370_SPI_H */
