/**
 * @file  s2500_port_board.h
 * @brief S32K312 平台移植层额外接口
 */
#ifndef S2500_PORT_BOARD_H
#define S2500_PORT_BOARD_H

#include "s2500.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 板级 HAL (SPI/延时/毫秒/IRQ 电平) */
const s2500_hal_t *s2500_port_hal(void);

/**
 * @brief 串口初始化 (在 Lpuart_Uart_Ip_Init 之后调用)
 *
 * RTD 的 Lpuart_Uart_Ip_Hw_Init() 会把 CTRL 清 0 (TE/RE 都不开),
 * 只在 SyncSend/AsyncReceive 里按需打开。本工程改为寄存器轮询收发,
 * 必须自己开 TE/RE, 并关掉所有串口中断。
 */
void s2500_port_uart_init(void);

/**
 * @brief 串口接收轮询 (R13: 长期压测后 shell 无响应的修复)
 *
 * 主循环每轮调用: 清 OR/NF/FE/PF, 抽干 RX FIFO 进环形缓冲。
 */
void s2500_port_uart_poll(void);

/** 串口诊断计数 */
typedef struct {
    uint32_t or_cnt;      /* 接收溢出 */
    uint32_t fe_cnt;      /* 帧错误 */
    uint32_t nf_cnt;      /* 噪声 */
    uint32_t pf_cnt;      /* 校验 */
    uint32_t rx_bytes;    /* 收到的字节 */
    uint32_t rx_dropped;  /* 环形缓冲满丢弃 */
    uint32_t tx_timeout;  /* 发送等待 TDRE 超时 */
    uint32_t idle_irq;    /* 空闲标志出现次数 */
} s2500_uart_diag_t;

void s2500_port_uart_diag(s2500_uart_diag_t *out);

#ifdef __cplusplus
}
#endif
#endif /* S2500_PORT_BOARD_H */
