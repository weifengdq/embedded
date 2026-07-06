/**
  ******************************************************************************
  * @file    shell_port.h
  * @brief   Shell Port Layer for UART Communication
  ******************************************************************************
  */

#ifndef __SHELL_PORT_H
#define __SHELL_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32h7xx_hal.h"

int  Shell_Init(UART_HandleTypeDef *huart);
void Shell_Process(void);
void Shell_PrintBanner(void);

#ifdef __cplusplus
}
#endif

#endif /* __SHELL_PORT_H */
