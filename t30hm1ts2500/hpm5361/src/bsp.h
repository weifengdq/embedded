/**
 * @file  bsp.h
 * @brief HPM5361 板级支持: SPI1(25MHz, DMA) / GPIO / UART0+UART2 / 时间基准
 */
#ifndef HPM5361_BSP_H
#define HPM5361_BSP_H

#include <stdint.h>
#include "s2500.h"

#ifdef __cplusplus
extern "C" {
#endif

/** SPI1 事务统计 (用于 CPU 占用/吞吐评估) */
typedef struct {
    uint32_t sclk_hz;     /**< SPI1 实际 SCLK */
    uint32_t src_hz;      /**< SPI1 时钟源频率 */
    uint8_t  dma;         /**< 1 = 数据事务使用 DMA */
    uint8_t  dma_ok;      /**< DMA 自检是否通过 */
    uint32_t xfers;       /**< SPI 事务次数 */
    uint32_t bytes;       /**< 累计字节数 */
    uint64_t cyc;         /**< SPI 路径累计 CPU 周期 (含 CS/等待), 64 位防溢出 */
    uint64_t wait_cyc;    /**< 其中 DMA 纯等待周期 */
} bsp_spi_stats_t;

/** 时钟/SPI1/GPIO/串口初始化, 并复位 S2500 */
void s2500_board_init(void);

/** 返回 S2500 驱动使用的 HAL */
const s2500_hal_t *s2500_board_hal(void);

void     bsp_delay_us(uint32_t us);
uint32_t bsp_millis(void);
int      bsp_uart_getc(void);
void     bsp_uart_write(const char *data, unsigned int len);
void     bsp_puts(const char *s);
uint32_t bsp_cpu_hz(void);

/** 运行时修改 SPI1 SCLK (0 表示恢复默认 25MHz) */
int      bsp_spi_set_hz(uint32_t hz);
/** SPI1 时钟源频率 */
uint32_t bsp_spi_src_hz(void);
/** 开关数据事务 DMA (0 = 阻塞式 FIFO 收发) */
void     bsp_spi_set_dma(int on);
int      bsp_spi_dma(void);
/** 对比 DMA / 阻塞两种路径的寄存器读结果, 1 = DMA 可用 (失败时自动回退到阻塞) */
int      bsp_spi_dma_selftest(void);

void     bsp_spi_get_stats(bsp_spi_stats_t *st);
void     bsp_spi_reset_stats(void);

#ifdef __cplusplus
}
#endif
#endif
