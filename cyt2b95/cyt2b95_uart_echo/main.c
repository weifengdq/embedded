#include "cy_pdl.h"
#include "cy_scb_uart.h"
#include "cybsp.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

cy_stc_scb_uart_context_t uart0;
uint8_t g_uart_out_data[256];

void uart0_callback(uint32_t event) {}

void print(void *fmt, ...) {
  va_list arg;
  va_start(arg, fmt);
  vsprintf((char *)&g_uart_out_data[0], (char *)fmt, arg);
  while (Cy_SCB_UART_IsTxComplete(SCB0) != true) {
  };
  Cy_SCB_UART_PutArray(SCB0, g_uart_out_data, strlen((char *)g_uart_out_data));
  va_end(arg);
}

int main(void) {
  cy_rslt_t result = cybsp_init();
  if (result != CY_RSLT_SUCCESS) {
    CY_ASSERT(0);
  }

  Cy_SCB_UART_DeInit(SCB0);
  Cy_SCB_UART_Init(SCB0, &scb_0_config, &uart0);
  Cy_SCB_UART_Enable(SCB0);

  __enable_irq();

  print("CYT2B95 UART Echo Example\r\n");
  // 测试整数浮点数uint64_t打印
  print("int: %d, float: %.2f, uint64_t: %llu\r\n", -12345, 3.14159,
        12345678901234ULL);

  while (1) {
    // Check if there is at least one character in the RX FIFO
    if (Cy_SCB_UART_GetNumInRxFifo(SCB0) > 0) {
      // Read the received character
      uint8_t rx_data = Cy_SCB_UART_Get(SCB0);

      // Send the same character back to the terminal (echo)
      Cy_SCB_UART_Put(SCB0, rx_data);
    }
  }

  return 0;
}