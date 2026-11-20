/**
 * @file  s2500_port.h
 * @brief 每个 MCU 平台必须实现的极小移植层
 */
#ifndef S2500_PORT_H
#define S2500_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 调试串口: 返回 -1 表示当前无数据 */
int  s2500_port_uart_getc(void);
/* 调试串口: 阻塞发送 */
void s2500_port_uart_write(const char *data, unsigned int len);

/* 板级初始化完成后由 main 调用 */
void s2500_board_init(void);

/* 软件复位 (reboot 命令) */
void s2500_port_reboot(void);

#ifdef __cplusplus
}
#endif
#endif
