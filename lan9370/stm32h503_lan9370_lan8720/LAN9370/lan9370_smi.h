/**
  ******************************************************************************
  * @file    lan9370_smi.h
  * @brief   LAN9370 SMI/MDIO Interface Driver Header
  * @details GPIO bit-banging implementation of SMI (Serial Management Interface)
  *          - PB5: MDIO (bidirectional data)
  *          - PB6: MDC (clock)
  ******************************************************************************
  */

#ifndef __LAN9370_SMI_H
#define __LAN9370_SMI_H

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
    LAN9370_SMI_OK = 0,
    LAN9370_SMI_ERROR,
    LAN9370_SMI_TIMEOUT,
    LAN9370_SMI_BUSY
} LAN9370_SMI_Status_t;

/* SMI Operation Codes */
typedef enum {
    SMI_OP_WRITE = 0x01,
    SMI_OP_READ  = 0x02
} LAN9370_SMI_Op_t;

/* =============================================================================
 * Function Prototypes
 * ===========================================================================*/

/**
 * @brief Initialize SMI interface
 * @return LAN9370_SMI_OK on success
 */
LAN9370_SMI_Status_t LAN9370_SMI_Init(void);

/**
 * @brief Read PHY register via SMI
 * @param phyAddr PHY address (0-31)
 * @param regAddr Register address (0-31)
 * @param data Pointer to store read data
 * @return LAN9370_SMI_OK on success
 */
LAN9370_SMI_Status_t LAN9370_SMI_Read(uint8_t phyAddr, uint8_t regAddr, uint16_t *data);

/**
 * @brief Write PHY register via SMI
 * @param phyAddr PHY address (0-31)
 * @param regAddr Register address (0-31)
 * @param data Data to write
 * @return LAN9370_SMI_OK on success
 */
LAN9370_SMI_Status_t LAN9370_SMI_Write(uint8_t phyAddr, uint8_t regAddr, uint16_t data);

/**
 * @brief Read-modify-write PHY register
 * @param phyAddr PHY address (0-31)
 * @param regAddr Register address (0-31)
 * @param mask Bit mask
 * @param value Value to set for masked bits
 * @return LAN9370_SMI_OK on success
 */
LAN9370_SMI_Status_t LAN9370_SMI_ModifyReg(uint8_t phyAddr, uint8_t regAddr, uint16_t mask, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* __LAN9370_SMI_H */
