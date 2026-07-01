/**
  ******************************************************************************
  * @file    mdio_bitbang.h
  * @brief   Direct Clause-22 SMI/MDIO bit-bang driver header
  * @details GPIO bit-banging implementation for external PHY management.
  *          - PB5: MDIO (bidirectional data)
  *          - PB6: MDC (clock)
  ******************************************************************************
  */

#ifndef __MDIO_BITBANG_H
#define __MDIO_BITBANG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "stm32h5xx_hal.h"

typedef enum {
    MDIO_BITBANG_OK = 0,
    MDIO_BITBANG_ERROR,
    MDIO_BITBANG_TIMEOUT,
    MDIO_BITBANG_BUSY
} MDIO_BitBang_Status_t;

typedef enum {
    MDIO_BITBANG_OP_WRITE = 0x01,
    MDIO_BITBANG_OP_READ  = 0x02
} MDIO_BitBang_Op_t;

MDIO_BitBang_Status_t MDIO_BitBang_Init(void);
MDIO_BitBang_Status_t MDIO_BitBang_Read(uint8_t phyAddr, uint8_t regAddr, uint16_t *data);
MDIO_BitBang_Status_t MDIO_BitBang_Write(uint8_t phyAddr, uint8_t regAddr, uint16_t data);
MDIO_BitBang_Status_t MDIO_BitBang_ModifyReg(uint8_t phyAddr, uint8_t regAddr, uint16_t mask, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* __MDIO_BITBANG_H */