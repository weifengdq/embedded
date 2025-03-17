#include "bsp_uart.h"
#include "usart.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>


void print(const char *fmt, ...) {
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  HAL_UART_Transmit(&hlpuart1, (uint8_t *)buffer, strlen(buffer), 1000);
  va_end(args);
}