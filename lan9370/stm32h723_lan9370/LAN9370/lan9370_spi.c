/**
  ******************************************************************************
  * @file    lan9370_spi.c
  * @brief   LAN9370 SPI Interface Driver Implementation
  * @details STM32H723 SPI1
  *          - PG10: nCS  (software GPIO, active low)
  *          - PG11: SCK  (SPI1 AF)
  *          - PG9:  MISO (SPI1 AF)
  *          - PD7:  MOSI (SPI1 AF)
  *          Mode: Master, CPOL=0, CPHA=0 (Mode 0), MSB First
  ******************************************************************************
  */

#include "lan9370_spi.h"
#include "lan9370_reg.h"
#include <string.h>
#include <stdlib.h>

/* =============================================================================
 * Private Definitions
 * ===========================================================================*/
#define LAN9370_SPI_TIMEOUT_MS      100
#define LAN9370_CS_PORT             GPIOG
#define LAN9370_CS_PIN              GPIO_PIN_10
#define LAN9370_SPI_CMD_LEN         4U
#define LAN9370_SPI_ADDR_MASK       0x00FFFFFFUL
#define LAN9370_SPI_ADDR_SHIFT      24U
#define LAN9370_SPI_TA_SHIFT        5U

static SPI_HandleTypeDef *pSpiHandle = NULL;

/* =============================================================================
 * Private Functions
 * ===========================================================================*/
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

static inline void LAN9370_SPI_CS_Low(void)
{
    HAL_GPIO_WritePin(LAN9370_CS_PORT, LAN9370_CS_PIN, GPIO_PIN_RESET);
}

static inline void LAN9370_SPI_CS_High(void)
{
    HAL_GPIO_WritePin(LAN9370_CS_PORT, LAN9370_CS_PIN, GPIO_PIN_SET);
}

static inline void LAN9370_SPI_Delay(void)
{
    /* ~5µs CS setup/hold delay (for SPI up to 25MHz) */
    for (volatile int i = 0; i < 1000; i++) { __NOP(); }
}

/* =============================================================================
 * Public Functions
 * ===========================================================================*/

LAN9370_SPI_Status_t LAN9370_SPI_Init(SPI_HandleTypeDef *hspi)
{
    if (hspi == NULL) {
        return LAN9370_SPI_ERROR;
    }
    pSpiHandle = hspi;

    /* Override PG10 as GPIO output for software CS control.
     * HAL_SPI_MspInit may have configured it as AF; reconfigure as output here. */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitStruct.Pin   = LAN9370_CS_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(LAN9370_CS_PORT, &GPIO_InitStruct);

    LAN9370_SPI_CS_High();
    return LAN9370_SPI_OK;
}

LAN9370_SPI_Status_t LAN9370_SPI_ReadReg8(uint16_t addr, uint8_t *data)
{
    if (pSpiHandle == NULL || data == NULL) return LAN9370_SPI_ERROR;

    uint8_t txBuf[LAN9370_SPI_CMD_LEN + 1];
    uint8_t rxBuf[LAN9370_SPI_CMD_LEN + 1];

    LAN9370_SPI_BuildHeader(addr, true, txBuf);
    txBuf[LAN9370_SPI_CMD_LEN] = 0x00;

    LAN9370_SPI_CS_Low();
    LAN9370_SPI_Delay();
    HAL_StatusTypeDef s = HAL_SPI_TransmitReceive(pSpiHandle, txBuf, rxBuf,
                                                   (uint16_t)sizeof(txBuf), LAN9370_SPI_TIMEOUT_MS);
    LAN9370_SPI_Delay();
    LAN9370_SPI_CS_High();

    if (s != HAL_OK) return LAN9370_SPI_ERROR;
    *data = rxBuf[LAN9370_SPI_CMD_LEN];
    return LAN9370_SPI_OK;
}

LAN9370_SPI_Status_t LAN9370_SPI_WriteReg8(uint16_t addr, uint8_t data)
{
    if (pSpiHandle == NULL) return LAN9370_SPI_ERROR;

    uint8_t txBuf[LAN9370_SPI_CMD_LEN + 1];
    uint8_t rxBuf[LAN9370_SPI_CMD_LEN + 1];

    LAN9370_SPI_BuildHeader(addr, false, txBuf);
    txBuf[LAN9370_SPI_CMD_LEN] = data;

    LAN9370_SPI_CS_Low();
    LAN9370_SPI_Delay();
    HAL_StatusTypeDef s = HAL_SPI_TransmitReceive(pSpiHandle, txBuf, rxBuf,
                                                   (uint16_t)sizeof(txBuf), LAN9370_SPI_TIMEOUT_MS);
    LAN9370_SPI_Delay();
    LAN9370_SPI_CS_High();

    return (s == HAL_OK) ? LAN9370_SPI_OK : LAN9370_SPI_ERROR;
}

LAN9370_SPI_Status_t LAN9370_SPI_ReadReg16(uint16_t addr, uint16_t *data)
{
    if (pSpiHandle == NULL || data == NULL) return LAN9370_SPI_ERROR;

    uint8_t txBuf[LAN9370_SPI_CMD_LEN + 2];
    uint8_t rxBuf[LAN9370_SPI_CMD_LEN + 2];

    LAN9370_SPI_BuildHeader(addr, true, txBuf);
    txBuf[LAN9370_SPI_CMD_LEN]     = 0x00;
    txBuf[LAN9370_SPI_CMD_LEN + 1] = 0x00;

    LAN9370_SPI_CS_Low();
    LAN9370_SPI_Delay();
    HAL_StatusTypeDef s = HAL_SPI_TransmitReceive(pSpiHandle, txBuf, rxBuf,
                                                   (uint16_t)sizeof(txBuf), LAN9370_SPI_TIMEOUT_MS);
    LAN9370_SPI_Delay();
    LAN9370_SPI_CS_High();

    if (s != HAL_OK) return LAN9370_SPI_ERROR;
    /* LAN9370 returns data MSB first */
    *data = ((uint16_t)rxBuf[LAN9370_SPI_CMD_LEN] << 8) | rxBuf[LAN9370_SPI_CMD_LEN + 1];
    return LAN9370_SPI_OK;
}

LAN9370_SPI_Status_t LAN9370_SPI_WriteReg16(uint16_t addr, uint16_t data)
{
    if (pSpiHandle == NULL) return LAN9370_SPI_ERROR;

    uint8_t txBuf[LAN9370_SPI_CMD_LEN + 2];
    uint8_t rxBuf[LAN9370_SPI_CMD_LEN + 2];

    LAN9370_SPI_BuildHeader(addr, false, txBuf);
    txBuf[LAN9370_SPI_CMD_LEN]     = (uint8_t)(data >> 8);
    txBuf[LAN9370_SPI_CMD_LEN + 1] = (uint8_t)(data);

    LAN9370_SPI_CS_Low();
    LAN9370_SPI_Delay();
    HAL_StatusTypeDef s = HAL_SPI_TransmitReceive(pSpiHandle, txBuf, rxBuf,
                                                   (uint16_t)sizeof(txBuf), LAN9370_SPI_TIMEOUT_MS);
    LAN9370_SPI_Delay();
    LAN9370_SPI_CS_High();

    return (s == HAL_OK) ? LAN9370_SPI_OK : LAN9370_SPI_ERROR;
}

LAN9370_SPI_Status_t LAN9370_SPI_ReadReg32(uint16_t addr, uint32_t *data)
{
    if (pSpiHandle == NULL || data == NULL) return LAN9370_SPI_ERROR;

    uint8_t txBuf[LAN9370_SPI_CMD_LEN + 4];
    uint8_t rxBuf[LAN9370_SPI_CMD_LEN + 4];

    LAN9370_SPI_BuildHeader(addr, true, txBuf);
    txBuf[LAN9370_SPI_CMD_LEN]     = 0x00;
    txBuf[LAN9370_SPI_CMD_LEN + 1] = 0x00;
    txBuf[LAN9370_SPI_CMD_LEN + 2] = 0x00;
    txBuf[LAN9370_SPI_CMD_LEN + 3] = 0x00;

    LAN9370_SPI_CS_Low();
    LAN9370_SPI_Delay();
    HAL_StatusTypeDef s = HAL_SPI_TransmitReceive(pSpiHandle, txBuf, rxBuf,
                                                   (uint16_t)sizeof(txBuf), LAN9370_SPI_TIMEOUT_MS);
    LAN9370_SPI_Delay();
    LAN9370_SPI_CS_High();

    if (s != HAL_OK) return LAN9370_SPI_ERROR;
    *data = ((uint32_t)rxBuf[LAN9370_SPI_CMD_LEN]     << 24) |
            ((uint32_t)rxBuf[LAN9370_SPI_CMD_LEN + 1] << 16) |
            ((uint32_t)rxBuf[LAN9370_SPI_CMD_LEN + 2] <<  8) |
             (uint32_t)rxBuf[LAN9370_SPI_CMD_LEN + 3];
    return LAN9370_SPI_OK;
}

LAN9370_SPI_Status_t LAN9370_SPI_WriteReg32(uint16_t addr, uint32_t data)
{
    if (pSpiHandle == NULL) return LAN9370_SPI_ERROR;

    uint8_t txBuf[LAN9370_SPI_CMD_LEN + 4];
    uint8_t rxBuf[LAN9370_SPI_CMD_LEN + 4];

    LAN9370_SPI_BuildHeader(addr, false, txBuf);
    txBuf[LAN9370_SPI_CMD_LEN]     = (uint8_t)(data >> 24);
    txBuf[LAN9370_SPI_CMD_LEN + 1] = (uint8_t)(data >> 16);
    txBuf[LAN9370_SPI_CMD_LEN + 2] = (uint8_t)(data >>  8);
    txBuf[LAN9370_SPI_CMD_LEN + 3] = (uint8_t)(data);

    LAN9370_SPI_CS_Low();
    LAN9370_SPI_Delay();
    HAL_StatusTypeDef s = HAL_SPI_TransmitReceive(pSpiHandle, txBuf, rxBuf,
                                                   (uint16_t)sizeof(txBuf), LAN9370_SPI_TIMEOUT_MS);
    LAN9370_SPI_Delay();
    LAN9370_SPI_CS_High();

    return (s == HAL_OK) ? LAN9370_SPI_OK : LAN9370_SPI_ERROR;
}

LAN9370_SPI_Status_t LAN9370_SPI_ReadBurst(uint16_t addr, uint8_t *buffer, uint16_t length)
{
    if (pSpiHandle == NULL || buffer == NULL || length == 0) return LAN9370_SPI_ERROR;

    uint8_t header[LAN9370_SPI_CMD_LEN];
    uint8_t dummy[LAN9370_SPI_CMD_LEN];

    LAN9370_SPI_BuildHeader(addr, true, header);

    LAN9370_SPI_CS_Low();
    LAN9370_SPI_Delay();
    HAL_SPI_TransmitReceive(pSpiHandle, header, dummy, LAN9370_SPI_CMD_LEN, LAN9370_SPI_TIMEOUT_MS);

    uint8_t *txDummy = (uint8_t *)alloca(length);
    memset(txDummy, 0, length);
    HAL_StatusTypeDef s = HAL_SPI_TransmitReceive(pSpiHandle, txDummy, buffer, length, LAN9370_SPI_TIMEOUT_MS);

    LAN9370_SPI_Delay();
    LAN9370_SPI_CS_High();

    return (s == HAL_OK) ? LAN9370_SPI_OK : LAN9370_SPI_ERROR;
}

LAN9370_SPI_Status_t LAN9370_SPI_WriteBurst(uint16_t addr, const uint8_t *buffer, uint16_t length)
{
    if (pSpiHandle == NULL || buffer == NULL || length == 0) return LAN9370_SPI_ERROR;

    uint8_t header[LAN9370_SPI_CMD_LEN];
    uint8_t dummy[LAN9370_SPI_CMD_LEN];

    LAN9370_SPI_BuildHeader(addr, false, header);

    LAN9370_SPI_CS_Low();
    LAN9370_SPI_Delay();
    HAL_SPI_TransmitReceive(pSpiHandle, header, dummy, LAN9370_SPI_CMD_LEN, LAN9370_SPI_TIMEOUT_MS);

    uint8_t *rxDummy = (uint8_t *)alloca(length);
    HAL_StatusTypeDef s = HAL_SPI_TransmitReceive(pSpiHandle, (uint8_t *)(uintptr_t)buffer,
                                                   rxDummy, length, LAN9370_SPI_TIMEOUT_MS);

    LAN9370_SPI_Delay();
    LAN9370_SPI_CS_High();

    return (s == HAL_OK) ? LAN9370_SPI_OK : LAN9370_SPI_ERROR;
}

/* =============================================================================
 * Retry helpers – majority-vote across 3 reads to filter SPI glitches
 * ===========================================================================*/

LAN9370_SPI_Status_t LAN9370_SPI_ReadReg8_Retry(uint16_t addr, uint8_t *data)
{
    uint8_t v[3];
    int ok[3] = {0, 0, 0};

    if (data == NULL) return LAN9370_SPI_ERROR;

    for (int i = 0; i < 3; i++) {
        ok[i] = (LAN9370_SPI_ReadReg8(addr, &v[i]) == LAN9370_SPI_OK) ? 1 : 0;
    }

    /* Return the first successful read; if multiple, prefer majority match */
    if (ok[0] && ok[1] && v[0] == v[1]) { *data = v[0]; return LAN9370_SPI_OK; }
    if (ok[0] && ok[2] && v[0] == v[2]) { *data = v[0]; return LAN9370_SPI_OK; }
    if (ok[1] && ok[2] && v[1] == v[2]) { *data = v[1]; return LAN9370_SPI_OK; }
    if (ok[0]) { *data = v[0]; return LAN9370_SPI_OK; }
    if (ok[1]) { *data = v[1]; return LAN9370_SPI_OK; }
    if (ok[2]) { *data = v[2]; return LAN9370_SPI_OK; }

    return LAN9370_SPI_ERROR;
}

LAN9370_SPI_Status_t LAN9370_SPI_ReadReg16_Retry(uint16_t addr, uint16_t *data)
{
    uint16_t v[3];
    int ok[3] = {0, 0, 0};

    if (data == NULL) return LAN9370_SPI_ERROR;

    for (int i = 0; i < 3; i++) {
        ok[i] = (LAN9370_SPI_ReadReg16(addr, &v[i]) == LAN9370_SPI_OK) ? 1 : 0;
    }

    if (ok[0] && ok[1] && v[0] == v[1]) { *data = v[0]; return LAN9370_SPI_OK; }
    if (ok[0] && ok[2] && v[0] == v[2]) { *data = v[0]; return LAN9370_SPI_OK; }
    if (ok[1] && ok[2] && v[1] == v[2]) { *data = v[1]; return LAN9370_SPI_OK; }
    if (ok[0]) { *data = v[0]; return LAN9370_SPI_OK; }
    if (ok[1]) { *data = v[1]; return LAN9370_SPI_OK; }
    if (ok[2]) { *data = v[2]; return LAN9370_SPI_OK; }

    return LAN9370_SPI_ERROR;
}

LAN9370_SPI_Status_t LAN9370_SPI_ReadReg32_Retry(uint16_t addr, uint32_t *data)
{
    uint32_t v[3];
    int ok[3] = {0, 0, 0};

    if (data == NULL) return LAN9370_SPI_ERROR;

    for (int i = 0; i < 3; i++) {
        ok[i] = (LAN9370_SPI_ReadReg32(addr, &v[i]) == LAN9370_SPI_OK) ? 1 : 0;
    }

    if (ok[0] && ok[1] && v[0] == v[1]) { *data = v[0]; return LAN9370_SPI_OK; }
    if (ok[0] && ok[2] && v[0] == v[2]) { *data = v[0]; return LAN9370_SPI_OK; }
    if (ok[1] && ok[2] && v[1] == v[2]) { *data = v[1]; return LAN9370_SPI_OK; }
    if (ok[0]) { *data = v[0]; return LAN9370_SPI_OK; }
    if (ok[1]) { *data = v[1]; return LAN9370_SPI_OK; }
    if (ok[2]) { *data = v[2]; return LAN9370_SPI_OK; }

    return LAN9370_SPI_ERROR;
}
