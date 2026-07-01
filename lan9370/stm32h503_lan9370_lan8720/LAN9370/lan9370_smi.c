/**
  ******************************************************************************
  * @file    lan9370_smi.c
    * @brief   Direct Clause-22 SMI/MDIO bit-bang driver implementation
    * @details Historical filename retained to minimize project churn.
    *          The module now drives the external LAN8720 PHY directly from
    *          the MCU over PB6(MDC)/PB5(MDIO), based on IEEE 802.3 Clause 22.
  *          - PB5: MDIO (bidirectional data)
  *          - PB6: MDC (clock, max 2.5MHz)
  *          
  * SMI Frame Format (Clause 22):
  * - Preamble: 32 bits of '1'
  * - Start: 01
  * - Op: 10 (Read) or 01 (Write)
  * - PHY Addr: 5 bits
  * - Reg Addr: 5 bits
  * - TA: 10 (Read) or 10 (Write)
  * - Data: 16 bits
  ******************************************************************************
  */

#include "lan9370_smi.h"
#include "main.h"

/* =============================================================================
 * Private Definitions
 * ===========================================================================*/
#define SMI_MDC_PORT                GPIOB
#define SMI_MDC_PIN                 GPIO_PIN_6

#define SMI_MDIO_PORT               GPIOB
#define SMI_MDIO_PIN                GPIO_PIN_5

#define SMI_PREAMBLE_LEN            32
#define SMI_START_FRAME             0x01
#define SMI_READ_OP                 0x02
#define SMI_WRITE_OP                0x01
#define SMI_TA_READ                 0x02
#define SMI_TA_WRITE                0x02

/* =============================================================================
 * Private Functions
 * ===========================================================================*/

/**
 * @brief Short delay for MDC timing (~400ns for 1.25MHz, ~200ns for 2.5MHz)
 * Adjust based on system clock (250MHz)
 */
static inline void SMI_Delay(void)
{
    /* For 250MHz system clock: ~50 cycles = 200ns, suitable for 2.5MHz MDC */
    for (volatile int i = 0; i < 12; i++);
}

/**
 * @brief Set MDC clock high
 */
static inline void SMI_MDC_High(void)
{
    HAL_GPIO_WritePin(SMI_MDC_PORT, SMI_MDC_PIN, GPIO_PIN_SET);
}

/**
 * @brief Set MDC clock low
 */
static inline void SMI_MDC_Low(void)
{
    HAL_GPIO_WritePin(SMI_MDC_PORT, SMI_MDC_PIN, GPIO_PIN_RESET);
}

/**
 * @brief Set MDIO data line high
 */
static inline void SMI_MDIO_High(void)
{
    HAL_GPIO_WritePin(SMI_MDIO_PORT, SMI_MDIO_PIN, GPIO_PIN_SET);
}

/**
 * @brief Set MDIO data line low
 */
static inline void SMI_MDIO_Low(void)
{
    HAL_GPIO_WritePin(SMI_MDIO_PORT, SMI_MDIO_PIN, GPIO_PIN_RESET);
}

/**
 * @brief Read MDIO data line
 */
static inline bool SMI_MDIO_Read(void)
{
    return (HAL_GPIO_ReadPin(SMI_MDIO_PORT, SMI_MDIO_PIN) == GPIO_PIN_SET);
}

/**
 * @brief Configure MDIO as output (open-drain to avoid bus conflict)
 * MDIO bus requires open-drain with external pull-up per IEEE 802.3 Clause 22.
 * Push-pull output would prevent other devices (LAN9370, LAN8720) from driving the line.
 */
static void SMI_MDIO_Output(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = SMI_MDIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;  /* Open-drain, not push-pull! */
    GPIO_InitStruct.Pull = GPIO_NOPULL;           /* External pull-up on MDIO line */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(SMI_MDIO_PORT, &GPIO_InitStruct);
}

/**
 * @brief Configure MDIO as input
 */
static void SMI_MDIO_Input(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = SMI_MDIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;  /* Pull-up as per IEEE 802.3 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(SMI_MDIO_PORT, &GPIO_InitStruct);
}

/**
 * @brief Generate one MDC clock cycle
 */
static void SMI_Clock(void)
{
    SMI_MDC_Low();
    SMI_Delay();
    SMI_MDC_High();
    SMI_Delay();
}

/**
 * @brief Write one bit to MDIO
 */
static void SMI_WriteBit(bool bit)
{
    if (bit) {
        SMI_MDIO_High();
    } else {
        SMI_MDIO_Low();
    }
    SMI_Clock();
}

/**
 * @brief Read one bit from MDIO
 */
static bool SMI_ReadBit(void)
{
    bool bit;
    SMI_MDC_Low();
    SMI_Delay();
    bit = SMI_MDIO_Read();
    SMI_MDC_High();
    SMI_Delay();
    return bit;
}

/**
 * @brief Send preamble (32 bits of '1')
 */
static void SMI_SendPreamble(void)
{
    SMI_MDIO_Output();
    SMI_MDIO_High();
    
    for (int i = 0; i < SMI_PREAMBLE_LEN; i++) {
        SMI_Clock();
    }
}

/**
 * @brief Send start of frame (01)
 */
static void SMI_SendStart(void)
{
    SMI_MDIO_Output();
    SMI_WriteBit(0);
    SMI_WriteBit(1);
}

/**
 * @brief Send operation code
 */
static void SMI_SendOp(LAN9370_SMI_Op_t op)
{
    SMI_MDIO_Output();
    if (op == SMI_OP_READ) {
        SMI_WriteBit(1);
        SMI_WriteBit(0);
    } else {
        SMI_WriteBit(0);
        SMI_WriteBit(1);
    }
}

/**
 * @brief Send 5-bit address
 */
static void SMI_SendAddr(uint8_t addr)
{
    SMI_MDIO_Output();
    for (int i = 4; i >= 0; i--) {
        SMI_WriteBit((addr >> i) & 0x01);
    }
}

/**
 * @brief Send turnaround for write (10)
 */
static void SMI_SendTA_Write(void)
{
    SMI_MDIO_Output();
    SMI_WriteBit(1);
    SMI_WriteBit(0);
}

/**
 * @brief Handle turnaround for read (Z0)
 */
static void SMI_HandleTA_Read(void)
{
    /* Switch MDIO to input for TA */
    SMI_MDIO_Input();
    
    /* Clock for high-Z bit */
    SMI_Clock();
    
    /* Clock for '0' bit (should be driven by PHY) */
    SMI_Clock();
}

/**
 * @brief Send 16-bit data
 */
static void SMI_SendData(uint16_t data)
{
    SMI_MDIO_Output();
    for (int i = 15; i >= 0; i--) {
        SMI_WriteBit((data >> i) & 0x01);
    }
}

/**
 * @brief Read 16-bit data
 */
static uint16_t SMI_ReadData(void)
{
    uint16_t data = 0;
    
    SMI_MDIO_Input();
    for (int i = 15; i >= 0; i--) {
        if (SMI_ReadBit()) {
            data |= (1 << i);
        }
    }
    
    return data;
}

/**
 * @brief End SMI transaction - release MDIO bus
 */
static void SMI_EndTransaction(void)
{
    /* Return MDIO to input mode (high-impedance) to release the bus.
     * The external pull-up will keep the line high when idle.
     * This allows other devices (LAN9370, LAN8720) to use the bus. */
    SMI_MDIO_Input();
    SMI_MDC_Low();
}

/* =============================================================================
 * Public Functions
 * ===========================================================================*/

/**
 * @brief Initialize SMI interface
 */
LAN9370_SMI_Status_t LAN9370_SMI_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable GPIO clock */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Configure MDC as output (push-pull) */
    GPIO_InitStruct.Pin = SMI_MDC_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(SMI_MDC_PORT, &GPIO_InitStruct);

    /* Configure MDIO as input initially (high-impedance).
     * External pull-up keeps the bus idle-high until the MCU starts a
     * Clause-22 transaction. */
    GPIO_InitStruct.Pin = SMI_MDIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;  /* Pull-up as per IEEE 802.3 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(SMI_MDIO_PORT, &GPIO_InitStruct);

    /* Set idle state: MDC low, MDIO high (via pull-up, not driven) */
    SMI_MDC_Low();

    return LAN9370_SMI_OK;
}

/**
 * @brief Read PHY register via SMI
 */
LAN9370_SMI_Status_t LAN9370_SMI_Read(uint8_t phyAddr, uint8_t regAddr, uint16_t *data)
{
    if (data == NULL || phyAddr > 31 || regAddr > 31) {
        return LAN9370_SMI_ERROR;
    }

    /* Disable interrupts to prevent timing issues */
    __disable_irq();

    /* Send preamble */
    SMI_SendPreamble();

    /* Send start of frame */
    SMI_SendStart();

    /* Send read operation */
    SMI_SendOp(SMI_OP_READ);

    /* Send PHY address */
    SMI_SendAddr(phyAddr);

    /* Send register address */
    SMI_SendAddr(regAddr);

    /* Handle turnaround (Z0) */
    SMI_HandleTA_Read();

    /* Read data */
    *data = SMI_ReadData();

    /* End transaction */
    SMI_EndTransaction();

    /* Re-enable interrupts */
    __enable_irq();

    return LAN9370_SMI_OK;
}

/**
 * @brief Write PHY register via SMI
 */
LAN9370_SMI_Status_t LAN9370_SMI_Write(uint8_t phyAddr, uint8_t regAddr, uint16_t data)
{
    if (phyAddr > 31 || regAddr > 31) {
        return LAN9370_SMI_ERROR;
    }

    /* Disable interrupts to prevent timing issues */
    __disable_irq();

    /* Send preamble */
    SMI_SendPreamble();

    /* Send start of frame */
    SMI_SendStart();

    /* Send write operation */
    SMI_SendOp(SMI_OP_WRITE);

    /* Send PHY address */
    SMI_SendAddr(phyAddr);

    /* Send register address */
    SMI_SendAddr(regAddr);

    /* Send turnaround (10) */
    SMI_SendTA_Write();

    /* Send data */
    SMI_SendData(data);

    /* End transaction */
    SMI_EndTransaction();

    /* Re-enable interrupts */
    __enable_irq();

    return LAN9370_SMI_OK;
}

/**
 * @brief Read-modify-write PHY register
 */
LAN9370_SMI_Status_t LAN9370_SMI_ModifyReg(uint8_t phyAddr, uint8_t regAddr, uint16_t mask, uint16_t value)
{
    uint16_t regValue;
    LAN9370_SMI_Status_t status;

    /* Read current value */
    status = LAN9370_SMI_Read(phyAddr, regAddr, &regValue);
    if (status != LAN9370_SMI_OK) {
        return status;
    }

    /* Modify value */
    regValue = (regValue & ~mask) | (value & mask);

    /* Write back */
    status = LAN9370_SMI_Write(phyAddr, regAddr, regValue);
    return status;
}
