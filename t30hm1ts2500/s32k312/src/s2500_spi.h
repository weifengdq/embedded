/**
 * @file  s2500_spi.h
 * @brief S2500 主机 SPI 后端选择 (硬件 LPSPI0 / 软件 GPIO), 支持运行时切换
 */
#ifndef S2500_SPI_H
#define S2500_SPI_H

#include <stdint.h>

#define S2500_SPI_BACKEND_SOFT  0   /* spi_soft.c   GPIO 位操作 */
#define S2500_SPI_BACKEND_HW    1   /* lpspi_hw.c   LPSPI0 */

/* 默认后端: 硬件 LPSPI0 (2026-11-20 打通, 32bit 帧, IDVER 1000 次无误码)。
 * 现场可用 "s32k hw off" 运行时退回软件 SPI, 不需要重新烧写。 */
#ifndef S2500_SPI_BACKEND
#define S2500_SPI_BACKEND       S2500_SPI_BACKEND_HW
#endif

#ifdef __cplusplus
extern "C" {
#endif

void        s2500_spi_init(void);
/** 运行时切换后端: 1 = 硬件 LPSPI0, 0 = 软件 GPIO */
void        s2500_spi_select(int hw);
/** 当前后端 (0/1) */
int         s2500_spi_backend(void);
int         s2500_spi_xfer(const uint8_t *tx, uint8_t *rx, uint32_t len);
uint32_t    s2500_spi_sck_hz(void);
uint32_t    s2500_spi_set_speed(uint32_t hz);
const char *s2500_spi_name(void);
void        s2500_spi_info_str(char *buf, unsigned int len);

#ifdef __cplusplus
}
#endif
#endif /* S2500_SPI_H */
