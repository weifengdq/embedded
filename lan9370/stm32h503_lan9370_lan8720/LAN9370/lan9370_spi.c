/**
  ******************************************************************************
  * @file    lan9370_spi.c
  * @brief   LAN9370 SPI Interface Driver Implementation
  * @details SPI communication using STM32 HAL
  *          - SPI1: PA1(NSS), PA2(SCK), PA3(MISO), PA4(MOSI)
  *          - Mode: Master, CPOL=0, CPHA=0, MSB First
  *          - Max Speed: Up to 50MHz (adjust prescaler as needed)
  ******************************************************************************
  */

#include "lan9370_spi.h"
#include "lan9370_reg.h"
#include "main.h"
#include <string.h>
#include <stdlib.h>

/* =============================================================================
 * Private Definitions
 * ===========================================================================*/
#define LAN9370_SPI_TIMEOUT_MS      100
#define LAN9370_CS_PORT             GPIOA
#define LAN9370_CS_PIN              GPIO_PIN_1
#define LAN9370_SPI_MAX_BURST       512
#define LAN9370_SPI_CMD_LEN         4U
#define LAN9370_SPI_ADDR_MASK       0x00FFFFFFUL
#define LAN9370_SPI_ADDR_SHIFT      24U
#define LAN9370_SPI_TA_SHIFT        5U

/* =============================================================================
 * Private Variables
 * ===========================================================================*/
static SPI_HandleTypeDef *pSpiHandle = NULL;

static LAN9370_SPI_Status_t LAN9370_SPI_WaitReady(uint32_t timeoutMs)
{
    uint32_t start = HAL_GetTick();
    while (HAL_SPI_GetState(pSpiHandle) != HAL_SPI_STATE_READY) {
        if ((HAL_GetTick() - start) > timeoutMs) {
            return LAN9370_SPI_TIMEOUT;
        }
    }
    return LAN9370_SPI_OK;
}

static void LAN9370_SPI_BuildHeader(uint16_t addr, bool isRead, uint8_t *header)
{
    uint32_t cmd;

    cmd = ((uint32_t)addr) & LAN9370_SPI_ADDR_MASK;
    cmd |= (uint32_t)(isRead ? LAN9370_SPI_CMD_READ : LAN9370_SPI_CMD_WRITE) << LAN9370_SPI_ADDR_SHIFT;
    cmd <<= LAN9370_SPI_TA_SHIFT;

    header[0] = (uint8_t)(cmd >> 24);
    header[1] = (uint8_t)(cmd >> 16);
    header[2] = (uint8_t)(cmd >> 8);
    header[3] = (uint8_t)(cmd);
}

/* =============================================================================
 * Private Functions
 * ===========================================================================*/

/**
 * @brief Assert SPI chip select (active low)
 */
static inline void LAN9370_SPI_CS_Low(void)
{
    HAL_GPIO_WritePin(LAN9370_CS_PORT, LAN9370_CS_PIN, GPIO_PIN_RESET);
}

/**
 * @brief Deassert SPI chip select
 */
static inline void LAN9370_SPI_CS_High(void)
{
    HAL_GPIO_WritePin(LAN9370_CS_PORT, LAN9370_CS_PIN, GPIO_PIN_SET);
}

/**
 * @brief Small delay for CS setup/hold timing
 */
static inline void LAN9370_SPI_Delay(void)
{
    /* Short delay for CS timing (adjust if needed) */
    for (volatile int i = 0; i < 10; i++);
}

/* =============================================================================
 * Public Functions
 * ===========================================================================*/

/**
 * @brief Initialize SPI interface
 */
LAN9370_SPI_Status_t LAN9370_SPI_Init(SPI_HandleTypeDef *hspi)
{
    if (hspi == NULL) {
        return LAN9370_SPI_ERROR;
    }

    pSpiHandle = hspi;

    /* Initialize CS pin as output, initially high (deasserted) */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    
    GPIO_InitStruct.Pin = LAN9370_CS_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(LAN9370_CS_PORT, &GPIO_InitStruct);

    LAN9370_SPI_CS_High();

    return LAN9370_SPI_OK;
}

/**
 * @brief Read 8-bit register
 */
LAN9370_SPI_Status_t LAN9370_SPI_ReadReg8(uint16_t addr, uint8_t *data)
{
    if (pSpiHandle == NULL || data == NULL) {
        return LAN9370_SPI_ERROR;
    }

    uint8_t txBuf[LAN9370_SPI_CMD_LEN + 1];
    uint8_t rxBuf[LAN9370_SPI_CMD_LEN + 1];

    LAN9370_SPI_BuildHeader(addr, true, txBuf);
    txBuf[LAN9370_SPI_CMD_LEN] = 0x00;

    LAN9370_SPI_CS_Low();
    LAN9370_SPI_Delay();

    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(pSpiHandle, txBuf, rxBuf, (uint16_t)sizeof(txBuf), LAN9370_SPI_TIMEOUT_MS);

    LAN9370_SPI_Delay();
    LAN9370_SPI_CS_High();

    if (status != HAL_OK) {
        return LAN9370_SPI_ERROR;
    }

    *data = rxBuf[LAN9370_SPI_CMD_LEN];
    return LAN9370_SPI_OK;
}

/**
 * @brief Write 8-bit register
 */
LAN9370_SPI_Status_t LAN9370_SPI_WriteReg8(uint16_t addr, uint8_t data)
{
    if (pSpiHandle == NULL) {
        return LAN9370_SPI_ERROR;
    }

    uint8_t txBuf[LAN9370_SPI_CMD_LEN + 1];

    LAN9370_SPI_BuildHeader(addr, false, txBuf);
    txBuf[LAN9370_SPI_CMD_LEN] = data;

    LAN9370_SPI_CS_Low();
    LAN9370_SPI_Delay();

    HAL_StatusTypeDef status = HAL_SPI_Transmit(pSpiHandle, txBuf, (uint16_t)sizeof(txBuf), LAN9370_SPI_TIMEOUT_MS);

    LAN9370_SPI_Delay();
    LAN9370_SPI_CS_High();

    return (status == HAL_OK) ? LAN9370_SPI_OK : LAN9370_SPI_ERROR;
}

/**
 * @brief Read 16-bit register
 */
LAN9370_SPI_Status_t LAN9370_SPI_ReadReg16(uint16_t addr, uint16_t *data)
{
    if (data == NULL) {
        return LAN9370_SPI_ERROR;
    }

    uint8_t buf[2];
    LAN9370_SPI_Status_t status;

    /* Read as big-endian (MSB first) */
    status = LAN9370_SPI_ReadReg8(addr, &buf[0]);
    if (status != LAN9370_SPI_OK) return status;

    status = LAN9370_SPI_ReadReg8(addr + 1, &buf[1]);
    if (status != LAN9370_SPI_OK) return status;

    *data = ((uint16_t)buf[0] << 8) | buf[1];
    return LAN9370_SPI_OK;
}

/**
 * @brief Write 16-bit register
 */
LAN9370_SPI_Status_t LAN9370_SPI_WriteReg16(uint16_t addr, uint16_t data)
{
    LAN9370_SPI_Status_t status;

    /* Write as big-endian (MSB first) */
    status = LAN9370_SPI_WriteReg8(addr, (data >> 8) & 0xFF);
    if (status != LAN9370_SPI_OK) return status;

    status = LAN9370_SPI_WriteReg8(addr + 1, data & 0xFF);
    return status;
}

/**
 * @brief Read 32-bit register
 */
LAN9370_SPI_Status_t LAN9370_SPI_ReadReg32(uint16_t addr, uint32_t *data)
{
    if (data == NULL) {
        return LAN9370_SPI_ERROR;
    }

    uint8_t buf[4];
    LAN9370_SPI_Status_t status;

    /* Read as big-endian (MSB first) */
    for (int i = 0; i < 4; i++) {
        status = LAN9370_SPI_ReadReg8(addr + i, &buf[i]);
        if (status != LAN9370_SPI_OK) return status;
    }

    *data = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | 
            ((uint32_t)buf[2] << 8) | buf[3];
    return LAN9370_SPI_OK;
}

/**
 * @brief Write 32-bit register
 */
LAN9370_SPI_Status_t LAN9370_SPI_WriteReg32(uint16_t addr, uint32_t data)
{
    LAN9370_SPI_Status_t status;

    /* Write as big-endian (MSB first) */
    status = LAN9370_SPI_WriteReg8(addr, (data >> 24) & 0xFF);
    if (status != LAN9370_SPI_OK) return status;

    status = LAN9370_SPI_WriteReg8(addr + 1, (data >> 16) & 0xFF);
    if (status != LAN9370_SPI_OK) return status;

    status = LAN9370_SPI_WriteReg8(addr + 2, (data >> 8) & 0xFF);
    if (status != LAN9370_SPI_OK) return status;

    status = LAN9370_SPI_WriteReg8(addr + 3, data & 0xFF);
    return status;
}

/**
 * @brief Read multiple bytes (burst read)
 */
LAN9370_SPI_Status_t LAN9370_SPI_ReadBurst(uint16_t addr, uint8_t *buffer, uint16_t length)
{
    if (pSpiHandle == NULL || buffer == NULL || length == 0) {
        return LAN9370_SPI_ERROR;
    }

    uint8_t txBuf[LAN9370_SPI_CMD_LEN + LAN9370_SPI_MAX_BURST];
    uint8_t rxBuf[LAN9370_SPI_CMD_LEN + LAN9370_SPI_MAX_BURST];

    if (length > LAN9370_SPI_MAX_BURST) {
        return LAN9370_SPI_ERROR;
    }

    LAN9370_SPI_BuildHeader(addr, true, txBuf);
    memset(&txBuf[LAN9370_SPI_CMD_LEN], 0, length);

    LAN9370_SPI_CS_Low();
    LAN9370_SPI_Delay();

    if (HAL_SPI_TransmitReceive(pSpiHandle,
                                txBuf,
                                rxBuf,
                                (uint16_t)(LAN9370_SPI_CMD_LEN + length),
                                LAN9370_SPI_TIMEOUT_MS) != HAL_OK) {
        LAN9370_SPI_CS_High();
        return LAN9370_SPI_ERROR;
    }

    memcpy(buffer, &rxBuf[LAN9370_SPI_CMD_LEN], length);

    LAN9370_SPI_Delay();
    LAN9370_SPI_CS_High();

    return LAN9370_SPI_OK;
}

/**
 * @brief Write multiple bytes (burst write)
 */
LAN9370_SPI_Status_t LAN9370_SPI_WriteBurst(uint16_t addr, const uint8_t *buffer, uint16_t length)
{
    if (pSpiHandle == NULL || buffer == NULL || length == 0) {
        return LAN9370_SPI_ERROR;
    }

    if (length > LAN9370_SPI_MAX_BURST) {
        return LAN9370_SPI_ERROR;
    }

    uint8_t txBuf[LAN9370_SPI_CMD_LEN + LAN9370_SPI_MAX_BURST];

    LAN9370_SPI_BuildHeader(addr, false, txBuf);
    memcpy(&txBuf[LAN9370_SPI_CMD_LEN], buffer, length);

    LAN9370_SPI_CS_Low();
    LAN9370_SPI_Delay();

    if (HAL_SPI_Transmit(pSpiHandle, txBuf, (uint16_t)(LAN9370_SPI_CMD_LEN + length), LAN9370_SPI_TIMEOUT_MS) != HAL_OK) {
        LAN9370_SPI_CS_High();
        return LAN9370_SPI_ERROR;
    }

    LAN9370_SPI_Delay();
    LAN9370_SPI_CS_High();

    return LAN9370_SPI_OK;
}

LAN9370_SPI_Status_t LAN9370_SPI_ReadBurstDMA(uint16_t addr, uint8_t *buffer, uint16_t length)
{
    uint8_t header[LAN9370_SPI_CMD_LEN];

    if (pSpiHandle == NULL || buffer == NULL || length == 0) {
        return LAN9370_SPI_ERROR;
    }

    if (length > LAN9370_SPI_MAX_BURST) {
        return LAN9370_SPI_ERROR;
    }

    LAN9370_SPI_BuildHeader(addr, true, header);

    LAN9370_SPI_CS_Low();
    LAN9370_SPI_Delay();

    if (HAL_SPI_Transmit(pSpiHandle, header, (uint16_t)sizeof(header), LAN9370_SPI_TIMEOUT_MS) != HAL_OK) {
        LAN9370_SPI_CS_High();
        return LAN9370_SPI_ERROR;
    }

    if (HAL_SPI_Receive_DMA(pSpiHandle, buffer, length) != HAL_OK) {
        LAN9370_SPI_CS_High();
        return LAN9370_SPI_ERROR;
    }

    if (LAN9370_SPI_WaitReady(LAN9370_SPI_TIMEOUT_MS) != LAN9370_SPI_OK) {
        LAN9370_SPI_CS_High();
        return LAN9370_SPI_TIMEOUT;
    }

    LAN9370_SPI_Delay();
    LAN9370_SPI_CS_High();

    return LAN9370_SPI_OK;
}

LAN9370_SPI_Status_t LAN9370_SPI_WriteBurstDMA(uint16_t addr, const uint8_t *buffer, uint16_t length)
{
    static uint8_t txBuf[LAN9370_SPI_CMD_LEN + LAN9370_SPI_MAX_BURST];

    if (pSpiHandle == NULL || buffer == NULL || length == 0 || length > LAN9370_SPI_MAX_BURST) {
        return LAN9370_SPI_ERROR;
    }

    LAN9370_SPI_BuildHeader(addr, false, txBuf);
    memcpy(&txBuf[LAN9370_SPI_CMD_LEN], buffer, length);

    LAN9370_SPI_CS_Low();
    LAN9370_SPI_Delay();

    if (HAL_SPI_Transmit_DMA(pSpiHandle, txBuf, (uint16_t)(LAN9370_SPI_CMD_LEN + length)) != HAL_OK) {
        LAN9370_SPI_CS_High();
        return LAN9370_SPI_ERROR;
    }

    if (LAN9370_SPI_WaitReady(LAN9370_SPI_TIMEOUT_MS) != LAN9370_SPI_OK) {
        LAN9370_SPI_CS_High();
        return LAN9370_SPI_TIMEOUT;
    }

    LAN9370_SPI_Delay();
    LAN9370_SPI_CS_High();

    return LAN9370_SPI_OK;
}
