/**
  ******************************************************************************
  * @file    shell_port.h
  * @brief   Shell Port Layer for UART Communication
  * @details Simplified command line interface for LAN9370 diagnostics
  *          For full letter-shell integration, see README.md
  ******************************************************************************
  */

#ifndef __SHELL_PORT_H
#define __SHELL_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32h5xx_hal.h"

/* =============================================================================
 * Function Prototypes
 * ===========================================================================*/

/**
 * @brief Initialize shell interface
 * @param huart Pointer to UART handle
 * @return 0 on success
 */
int Shell_Init(UART_HandleTypeDef *huart);

/**
 * @brief Process shell input (call periodically)
 */
void Shell_Process(void);

/**
 * @brief Print welcome banner
 */
void Shell_PrintBanner(void);

#ifdef __cplusplus
}
#endif

#endif /* __SHELL_PORT_H */
