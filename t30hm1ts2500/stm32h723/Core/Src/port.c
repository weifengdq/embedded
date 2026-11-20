/**
 * @file  port.c
 * @brief common/s2500 移植层: SPI 事务 / 延时 / 串口 / 复位
 */
#include "board.h"
#include "s2500_port.h"
#include "s2500.h"

/* ------------------------------------------------------------------ */
/* SPI 全双工事务: CS_N 覆盖整个 len (12 或 68 字节)                   */
/* ------------------------------------------------------------------ */
static int st_spi_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    int rc;
    uint32_t t0;
    uint32_t dt;

    (void)ctx;

    t0 = DWT->CYCCNT;
    HAL_GPIO_WritePin(S2500_CS_PORT, S2500_CS_PIN, GPIO_PIN_RESET);

    rc = board_spi_transfer(tx, rx, (uint16_t)len);

    /* 注意 (H7 坑): 这里不要再等 SPI_SR_EOT/TXC。
     * 新 SPI IP 在 DMA 路径由 board_spi_transfer 内部等 EOT+TXC 后关 SPE;
     * 轮询回退路径由 HAL_SPI_TransmitReceive 内部等 EOT 并 SPI_CloseTransfer()
     * 清掉标志, 出来时 EOT/TXC 一定已经是 0, 在这里等只会白等 1ms/次。 */
    HAL_GPIO_WritePin(S2500_CS_PORT, S2500_CS_PIN, GPIO_PIN_SET);

    /* 统计: 一次事务从 CS 拉低到拉高的总周期 (含 DMA 等待) */
    dt = DWT->CYCCNT - t0;
    g_spi_stats.xfers++;
    g_spi_stats.bytes += len;
    g_spi_stats.cycles_last = dt;
    g_spi_stats.cycles_total += dt;
    if (dt > g_spi_stats.cycles_max) {
        g_spi_stats.cycles_max = dt;
    }

    return rc;
}

static void st_delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    board_delay_us(us);
}

static uint32_t st_millis(void *ctx)
{
    (void)ctx;
    return board_millis();
}

static int st_irq_level(void *ctx)
{
    (void)ctx;
    /* IRQ_N 低有效 -> 返回 1 表示有中断 */
    return (HAL_GPIO_ReadPin(S2500_IRQ_PORT, S2500_IRQ_PIN) == GPIO_PIN_RESET) ? 1 : 0;
}

static const s2500_hal_t s_hal = {
    .ctx       = NULL,
    .spi_xfer  = st_spi_xfer,
    .delay_us  = st_delay_us,
    .millis    = st_millis,
    .irq_level = st_irq_level,
    .log       = NULL,
};

const void *s2500_platform_hal(void)
{
    return (const void *)&s_hal;
}

/* ------------------------------------------------------------------ */
/* 调试串口                                                            */
/* ------------------------------------------------------------------ */
int s2500_port_uart_getc(void)
{
    return board_uart_getc();
}

void s2500_port_uart_write(const char *data, unsigned int len)
{
    board_uart_write(data, len);
}

void s2500_port_reboot(void)
{
    __disable_irq();
    NVIC_SystemReset();
}

/* 板级初始化 (s2500_port.h 约定名) */
void s2500_board_init(void)
{
    board_init();
}
