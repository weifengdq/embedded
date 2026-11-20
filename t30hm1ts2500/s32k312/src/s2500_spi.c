/**
 * @file  s2500_spi.c
 * @brief S2500 主机 SPI 后端: 硬件 LPSPI0 (默认) / 软件 GPIO, 可运行时切换
 *
 * 两个后端都编译进来, 默认走硬件; 现场用 "s32k hw off|on" 切换,
 * 切换时会重新配置引脚并初始化对应后端, 不需要重新烧写。
 */
#include "s2500_spi.h"
#include "spi_soft.h"
#include "lpspi_hw.h"

#include <stdio.h>
#include <string.h>

static int s_backend = S2500_SPI_BACKEND;

static void backend_start(int hw)
{
    if (hw != 0) {
        lpspi_hw_init(0u);
    } else {
        lpspi_hw_stop();
        spi_soft_init();
    }
}

void s2500_spi_init(void)
{
    backend_start(s_backend);
}

void s2500_spi_select(int hw)
{
    s_backend = (hw != 0) ? S2500_SPI_BACKEND_HW : S2500_SPI_BACKEND_SOFT;
    backend_start(s_backend);
}

int s2500_spi_backend(void)
{
    return s_backend;
}

int s2500_spi_xfer(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    if (s_backend == S2500_SPI_BACKEND_HW) {
        return lpspi_hw_xfer(tx, rx, len);
    }
    return spi_soft_xfer(tx, rx, len);
}

uint32_t s2500_spi_sck_hz(void)
{
    if (s_backend == S2500_SPI_BACKEND_HW) {
        return lpspi_hw_sck_hz();
    }
    return spi_soft_sck_hz();
}

uint32_t s2500_spi_set_speed(uint32_t hz)
{
    if (s_backend == S2500_SPI_BACKEND_HW) {
        return lpspi_hw_set_speed(hz);
    }
    (void)hz;
    return spi_soft_sck_hz();
}

const char *s2500_spi_name(void)
{
    return (s_backend == S2500_SPI_BACKEND_HW) ? "LPSPI0 HW" : "GPIO soft";
}

void s2500_spi_info_str(char *buf, unsigned int len)
{
    if (buf == NULL || len == 0u) {
        return;
    }
    if (s_backend == S2500_SPI_BACKEND_HW) {
        lpspi_hw_info_t info;
        lpspi_hw_info(&info);
        (void)snprintf(buf, len, "LPSPI0 %lukHz 32bit f=%luMHz half=%lu fifo=%lu/%lu",
                       (unsigned long)(info.sck_hz / 1000u),
                       (unsigned long)(info.func_hz / 1000000u),
                       (unsigned long)info.half,
                       (unsigned long)info.tx_depth, (unsigned long)info.rx_depth);
    } else {
        (void)snprintf(buf, len, "GPIO soft %lukHz",
                       (unsigned long)(spi_soft_sck_hz() / 1000u));
    }
}
