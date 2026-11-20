/**
 * @file  spi_soft.h
 * @brief S32K312 软件 SPI (GPIO 位操作) —— 编译期 fallback
 *
 * 仅在 -DS2500_SPI_BACKEND=0 时启用 (正常情况用 LPSPI0 硬件 SPI)。
 * 线序 (2026-11 用户纠正后):
 *   CS_N = PTB5(37)   MOSI = PTB4(36)   MISO = PTD16(112)   SCLK = PTD15(111)
 */
#ifndef SPI_SOFT_H
#define SPI_SOFT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化: 配置 CS/SCK/MOSI 为输出, MISO 为输入 */
void spi_soft_init(void);

/**
 * @brief 全双工传输, CS_N 覆盖整个 len 字节
 * @param tx 发送缓冲 (NULL 表示发 0xFF)
 * @param rx 接收缓冲 (NULL 表示丢弃)
 * @param len 1..68
 * @return 0 成功
 */
int spi_soft_xfer(const uint8_t *tx, uint8_t *rx, uint32_t len);

/** 估算的 SCK 频率 (用于打印) */
uint32_t spi_soft_sck_hz(void);

#ifdef __cplusplus
}
#endif
#endif /* SPI_SOFT_H */
