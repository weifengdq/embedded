#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#include "s2500.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 供 s2500_app 使用的 HAL (SPI / 延时 / 毫秒) */
extern const s2500_hal_t g_s2500_hal;

/* 板级初始化: 串口 -> GPIO -> S2500 复位 -> QSPI2 */
void board_init(void);

/* 调试串口 (ASCLIN0, P14.0/P14.1, 115200) */
void board_uart_write(const char *data, unsigned int len);
int  board_uart_getc(void);

/* 时基 (STM0) */
uint32_t board_millis(void);
void     board_delay_us(uint32_t us);

/* 软件复位 */
void board_reboot(void);

#ifdef __cplusplus
}
#endif
#endif
