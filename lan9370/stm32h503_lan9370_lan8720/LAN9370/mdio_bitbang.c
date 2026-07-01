/**
  ******************************************************************************
  * @file    mdio_bitbang.c
  * @brief   Direct Clause-22 SMI/MDIO bit-bang driver implementation
  * @details GPIO bit-banging implementation based on IEEE 802.3 Clause 22.
  *          - PB5: MDIO (bidirectional data)
  *          - PB6: MDC (clock, max 2.5MHz)
  ******************************************************************************
  */

#include "mdio_bitbang.h"
#include "main.h"

/* =============================================================================
 * Private Definitions
 * ===========================================================================*/
#define MDIO_MDC_PORT               GPIOB
#define MDIO_MDC_PIN                GPIO_PIN_6

#define MDIO_DATA_PORT              GPIOB
#define MDIO_DATA_PIN               GPIO_PIN_5

#define MDIO_PREAMBLE_LEN           32

/* =============================================================================
 * Private Functions
 * ===========================================================================*/

static inline void MDIO_Delay(void)
{
    for (volatile int i = 0; i < 12; i++);
}

static inline void MDIO_MdcHigh(void)
{
    HAL_GPIO_WritePin(MDIO_MDC_PORT, MDIO_MDC_PIN, GPIO_PIN_SET);
}

static inline void MDIO_MdcLow(void)
{
    HAL_GPIO_WritePin(MDIO_MDC_PORT, MDIO_MDC_PIN, GPIO_PIN_RESET);
}

static inline void MDIO_DataHigh(void)
{
    HAL_GPIO_WritePin(MDIO_DATA_PORT, MDIO_DATA_PIN, GPIO_PIN_SET);
}

static inline void MDIO_DataLow(void)
{
    HAL_GPIO_WritePin(MDIO_DATA_PORT, MDIO_DATA_PIN, GPIO_PIN_RESET);
}

static inline bool MDIO_DataRead(void)
{
    return (HAL_GPIO_ReadPin(MDIO_DATA_PORT, MDIO_DATA_PIN) == GPIO_PIN_SET);
}

static void MDIO_DataOutput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = MDIO_DATA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(MDIO_DATA_PORT, &GPIO_InitStruct);
}

static void MDIO_DataInput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = MDIO_DATA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(MDIO_DATA_PORT, &GPIO_InitStruct);
}

static void MDIO_Clock(void)
{
    MDIO_MdcLow();
    MDIO_Delay();
    MDIO_MdcHigh();
    MDIO_Delay();
}

static void MDIO_WriteBit(bool bit)
{
    if (bit) {
        MDIO_DataHigh();
    } else {
        MDIO_DataLow();
    }
    MDIO_Clock();
}

static bool MDIO_ReadBit(void)
{
    bool bit;
    MDIO_MdcLow();
    MDIO_Delay();
    bit = MDIO_DataRead();
    MDIO_MdcHigh();
    MDIO_Delay();
    return bit;
}

static void MDIO_SendPreamble(void)
{
    MDIO_DataOutput();
    MDIO_DataHigh();

    for (int i = 0; i < MDIO_PREAMBLE_LEN; i++) {
        MDIO_Clock();
    }
}

static void MDIO_SendStart(void)
{
    MDIO_DataOutput();
    MDIO_WriteBit(false);
    MDIO_WriteBit(true);
}

static void MDIO_SendOp(MDIO_BitBang_Op_t op)
{
    MDIO_DataOutput();
    if (op == MDIO_BITBANG_OP_READ) {
        MDIO_WriteBit(true);
        MDIO_WriteBit(false);
    } else {
        MDIO_WriteBit(false);
        MDIO_WriteBit(true);
    }
}

static void MDIO_SendAddr(uint8_t addr)
{
    MDIO_DataOutput();
    for (int i = 4; i >= 0; i--) {
        MDIO_WriteBit((addr >> i) & 0x01U);
    }
}

static void MDIO_SendTaWrite(void)
{
    MDIO_DataOutput();
    MDIO_WriteBit(true);
    MDIO_WriteBit(false);
}

static void MDIO_HandleTaRead(void)
{
    MDIO_DataInput();
    MDIO_Clock();
    MDIO_Clock();
}

static void MDIO_SendData(uint16_t data)
{
    MDIO_DataOutput();
    for (int i = 15; i >= 0; i--) {
        MDIO_WriteBit((data >> i) & 0x01U);
    }
}

static uint16_t MDIO_ReadData(void)
{
    uint16_t data = 0;

    MDIO_DataInput();
    for (int i = 15; i >= 0; i--) {
        if (MDIO_ReadBit()) {
            data |= (uint16_t)(1U << i);
        }
    }

    return data;
}

static void MDIO_EndTransaction(void)
{
    MDIO_DataInput();
    MDIO_MdcLow();
}

/* =============================================================================
 * Public Functions
 * ===========================================================================*/

MDIO_BitBang_Status_t MDIO_BitBang_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = MDIO_MDC_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(MDIO_MDC_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = MDIO_DATA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(MDIO_DATA_PORT, &GPIO_InitStruct);

    MDIO_MdcLow();

    return MDIO_BITBANG_OK;
}

MDIO_BitBang_Status_t MDIO_BitBang_Read(uint8_t phyAddr, uint8_t regAddr, uint16_t *data)
{
    if (data == NULL || phyAddr > 31U || regAddr > 31U) {
        return MDIO_BITBANG_ERROR;
    }

    __disable_irq();
    MDIO_SendPreamble();
    MDIO_SendStart();
    MDIO_SendOp(MDIO_BITBANG_OP_READ);
    MDIO_SendAddr(phyAddr);
    MDIO_SendAddr(regAddr);
    MDIO_HandleTaRead();
    *data = MDIO_ReadData();
    MDIO_EndTransaction();
    __enable_irq();

    return MDIO_BITBANG_OK;
}

MDIO_BitBang_Status_t MDIO_BitBang_Write(uint8_t phyAddr, uint8_t regAddr, uint16_t data)
{
    if (phyAddr > 31U || regAddr > 31U) {
        return MDIO_BITBANG_ERROR;
    }

    __disable_irq();
    MDIO_SendPreamble();
    MDIO_SendStart();
    MDIO_SendOp(MDIO_BITBANG_OP_WRITE);
    MDIO_SendAddr(phyAddr);
    MDIO_SendAddr(regAddr);
    MDIO_SendTaWrite();
    MDIO_SendData(data);
    MDIO_EndTransaction();
    __enable_irq();

    return MDIO_BITBANG_OK;
}

MDIO_BitBang_Status_t MDIO_BitBang_ModifyReg(uint8_t phyAddr, uint8_t regAddr, uint16_t mask, uint16_t value)
{
    uint16_t regValue;
    MDIO_BitBang_Status_t status;

    status = MDIO_BitBang_Read(phyAddr, regAddr, &regValue);
    if (status != MDIO_BITBANG_OK) {
        return status;
    }

    regValue = (uint16_t)((regValue & (uint16_t)(~mask)) | (value & mask));
    status = MDIO_BitBang_Write(phyAddr, regAddr, regValue);
    return status;
}