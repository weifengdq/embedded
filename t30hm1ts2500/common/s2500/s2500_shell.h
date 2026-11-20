/**
 * @file  s2500_shell.h
 * @brief S2500 letter-shell 命令集
 */
#ifndef S2500_SHELL_H
#define S2500_SHELL_H

#include "s2500.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 shell (使用 s2500_port_uart_read/write) */
void s2500_shell_init(s2500_dev_t *dev);

/** 主循环调用: 从串口取字符喂给 shell */
void s2500_shell_poll(void);

/** 打印一行 (同时输出到 shell 与日志) */
void s2500_shell_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
#endif
