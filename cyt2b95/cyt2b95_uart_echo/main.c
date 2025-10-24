#include "cy_pdl.h"
#include "cy_scb_uart.h"
#include "cybsp.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

cy_stc_scb_uart_context_t uart0;
uint8_t g_uart_out_data[256];
uint8_t g_uart_rx_ring[512] = {0};

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
  __enable_irq();

  Cy_SCB_UART_DeInit(SCB0);
  Cy_SCB_UART_Init(SCB0, &scb_0_config, &uart0);
  Cy_SCB_UART_RegisterCallback(
      SCB0, (cy_cb_scb_uart_handle_events_t)uart0_callback, &uart0);
  Cy_SCB_UART_StartRingBuffer(SCB0, g_uart_rx_ring, sizeof(g_uart_rx_ring),
                              &uart0);
  Cy_SCB_UART_Enable(SCB0);
  NVIC_EnableIRQ((IRQn_Type)NvicMux0_IRQn);

  print("CYT2B95 UART Echo Example\r\n");
  // 测试整数浮点数uint64_t打印
  print("int: %d, float: %.2f, uint64_t: %llu\r\n", -12345, 3.14159,
        12345678901234ULL);

  while (1) {
    // Your application code here
  }

  return 0;
}