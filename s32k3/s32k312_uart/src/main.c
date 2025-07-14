#include "Clock_Ip.h"
#include "IntCtrl_Ip.h"
#include "Lpuart_Uart_Ip.h"
#include "Lpuart_Uart_Ip_Irq.h"
#include "Mcal.h"
#include "Siul2_Port_Ip.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

const uint8 UART_INSTANCE = 0; // LPUART0
// 环形缓冲区相关定义
#define UART_RX_BUFFER_SIZE 256
uint8_t g_uart0_rx_buffer[UART_RX_BUFFER_SIZE] = {0};
uint8_t g_uart0_cmd_buf[UART_RX_BUFFER_SIZE] = {0};
volatile uint16_t g_uart0_rx_head = 0;
volatile uint16_t g_uart0_rx_tail = 0;
volatile uint8_t g_uart0_cmd_ready = 0;
volatile uint8_t g_uart0_cmd_len = 0;

void UART0_CALLBACK(const uint8 HwInstance,
                    const Lpuart_Uart_Ip_EventType Event,
                    const void *UserData) {
  (void)UserData;
  if (Event == LPUART_UART_IP_EVENT_RX_FULL) {
    // 写入数据到环形缓冲区
    uint8_t data = g_uart0_rx_buffer[g_uart0_rx_head]; // 当前收到的数据
    g_uart0_rx_head = (g_uart0_rx_head + 1) % UART_RX_BUFFER_SIZE;
    // 检查是否收到换行符
    if (data == '\n') {
      // 取出一条命令（从 tail 到 head，遇到 \n 截止）
      while (g_uart0_rx_tail != g_uart0_rx_head) {
        uint8_t ch = g_uart0_rx_buffer[g_uart0_rx_tail];
        g_uart0_rx_tail = (g_uart0_rx_tail + 1) % UART_RX_BUFFER_SIZE;
        g_uart0_cmd_buf[g_uart0_cmd_len++] = ch;
        if (ch == '\n') {
          break;
        }
      }
      g_uart0_cmd_ready = 1;
    }
    // 继续接收下一个字节
    Lpuart_Uart_Ip_SetRxBuffer(HwInstance, &g_uart0_rx_buffer[g_uart0_rx_head],
                               1);
  }
}

int main(void) {
  Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);
  Siul2_Port_Ip_Init(
      NUM_OF_CONFIGURED_PINS_PortContainer_0_BOARD_InitPeripherals,
      g_pin_mux_InitConfigArr_PortContainer_0_BOARD_InitPeripherals);
  IntCtrl_Ip_Init(&IntCtrlConfig_0);
  Lpuart_Uart_Ip_Init(UART_INSTANCE, &Lpuart_Uart_Ip_xHwConfigPB_0);

  const char *message =
      "UART Ping-Pong Buffer Example\r\nType something and press enter...\r\n";
  Lpuart_Uart_Ip_SyncSend(UART_INSTANCE, (const uint8_t *)message,
                          strlen(message), 0xFFFF);
  memset(g_uart0_rx_buffer, 0, sizeof(g_uart0_rx_buffer));
  Lpuart_Uart_Ip_AsyncReceive(UART_INSTANCE, g_uart0_rx_buffer, 1);

  while (1) {
    // 检查是否收到完整命令（遇到换行符）
    if (g_uart0_cmd_ready) {
      // 处理命令, 这里原封不动发回
      if (LPUART_UART_IP_STATUS_BUSY !=
          Lpuart_Uart_Ip_GetTransmitStatus(UART_INSTANCE, NULL)) {
        Lpuart_Uart_Ip_AsyncSend(UART_INSTANCE, g_uart0_cmd_buf,
                                 g_uart0_cmd_len);
      }
      g_uart0_cmd_len = 0; // 重置命令长度
      g_uart0_cmd_ready = 0;
    }
  }

  return 0;
}
