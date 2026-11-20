/**
 * @file  lpspi_hw.h
 * @brief S32K312 LPSPI0 硬件 SPI 驱动 (32 位帧 + eDMA 可选) —— 驱动 T30HM1TS2500
 *
 * 板上线序:
 *   CS_N (白) = PTB5  -> 软件 GPIO (MSCR37), 覆盖整笔事务
 *   MISO (灰) = PTD16 -> LPSPI0_SIN  (MSCR112 IBE=1 + SIUL2_IMCR230=3)
 *   MOSI (蓝) = PTB4  -> LPSPI0_SOUT (MSCR36 SSS=3)
 *   SCLK (紫) = PTD15 -> LPSPI0_SCK  (MSCR111 SSS=4)
 *   IRQ_N(绿) = PTE9 (EIRQ[8]), RST_N(黄) = PTD14
 */
#ifndef LPSPI_HW_H
#define LPSPI_HW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 目标 SCK 频率 */
#ifndef LPSPI_HW_TARGET_HZ
#define LPSPI_HW_TARGET_HZ      15000000u
#endif

/** S2500 的 SPI 时钟上限 (datasheet 25MHz) */
#ifndef LPSPI_HW_MAX_HZ
#define LPSPI_HW_MAX_HZ         25000000u
#endif

typedef struct {
    uint32_t xfers;        /* 事务总数 */
    uint32_t bytes;        /* 字节总数 */
    uint32_t timeout;      /* 超时次数 */
    uint32_t dma_tx_err;
    uint32_t dma_rx_err;
    uint32_t sck_hz;       /* 配置/实测 SCK */
    uint32_t func_hz;      /* 实测 LPSPI0 功能时钟 */
    uint32_t half;         /* CCR1 SCKSET=SCKHLD */
    uint32_t tx_depth;     /* PARAM.TXFIFO */
    uint32_t rx_depth;     /* PARAM.RXFIFO */
    uint32_t dma_enabled;
} lpspi_hw_info_t;

/** 初始化 (引脚复用 + 时钟标定 + 分频) */
void        lpspi_hw_init(uint32_t target_hz);

/** 停止: 关 LPSPI0 并把引脚交给软件 SPI */
void        lpspi_hw_stop(void);

/** 运行时改速, 返回实际 SCK */
uint32_t    lpspi_hw_set_speed(uint32_t target_hz);

/** 当前 SCK 频率 */
uint32_t    lpspi_hw_sck_hz(void);

/** 调试: 软/硬 SPI 同一事务对比 */
void        lpspi_hw_cmp2(void);

/** 寄存器/状态转储 */
void        lpspi_hw_dump(void);

void        lpspi_hw_dma(int on);
int         lpspi_hw_dma_enabled(void);
void        lpspi_hw_dma_set_src(uint32_t tx, uint32_t rx);
int         lpspi_hw_dma_probe(void);
void        lpspi_hw_dma_regs(void);
int         lpspi_hw_dma_clock(int fix);
int         lpspi_hw_dma_test(uint32_t txs, uint32_t rxs);
int         lpspi_hw_dma_ram_test(void);
int         lpspi_hw_dma_burst_test(void);
int         lpspi_hw_dma_best_test(void);
int         lpspi_hw_dma_byte_test(void);

/** 诊断信息 */
void        lpspi_hw_info(lpspi_hw_info_t *out);

/**
 * @brief 全双工传输, CS_N 覆盖整个 len (len 建议 4 字节对齐 = 32 位帧)
 * @return 0 成功, <0 失败
 */
int         lpspi_hw_xfer(const uint8_t *tx, uint8_t *rx, uint32_t len);

#ifdef __cplusplus
}
#endif
#endif /* LPSPI_HW_H */
