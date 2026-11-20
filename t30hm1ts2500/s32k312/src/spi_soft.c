/**
 * @file  spi_soft.c
 * @brief S32K312 GPIO 位操作 SPI (模式0, MSB first) —— 编译期 fallback
 *
 * 仅在 -DS2500_SPI_BACKEND=0 时启用。线序 (用户纠正后):
 *   CS_N : PTB5  (pin index 37)  -> 输出
 *   SCLK : PTD15 (pin index 111) -> 输出
 *   MOSI : PTB4  (pin index 36)  -> 输出  (LPSPI0_SOUT 所在焊盘)
 *   MISO : PTD16 (pin index 112) -> 输入  (LPSPI0_SIN 所在焊盘)
 *
 * SIUL2 GPIO 寄存器是 8bit, 且每 4 个 pin 的字节序是反的:
 *   offset = (pin & ~3) | (~(pin) & 3)
 */
#include "spi_soft.h"
#include "S32K312_SIUL2.h"
#include <stddef.h>

#define PIN_CS     37u
#define PIN_SCK    111u
#define PIN_MOSI   36u
#define PIN_MISO   112u

#define SIUL2_ADDR   ((uintptr_t)IP_SIUL2)

static volatile uint8_t *s_cs;
static volatile uint8_t *s_sck;
static volatile uint8_t *s_mosi;
static volatile uint8_t *s_miso;

static volatile uint8_t *siul2_gpdo(uint32_t pin)
{
    uint32_t off = (pin & ~3u) | ((~pin) & 3u);
    return (volatile uint8_t *)(SIUL2_ADDR + 0x1300u + off);
}

static volatile uint8_t *siul2_gpdi(uint32_t pin)
{
    uint32_t off = (pin & ~3u) | ((~pin) & 3u);
    return (volatile uint8_t *)(SIUL2_ADDR + 0x1500u + off);
}

static void siul2_mscr(uint32_t pin, uint32_t val)
{
    *(volatile uint32_t *)(SIUL2_ADDR + 0x240u + 4u * pin) = val;
}

void spi_soft_init(void)
{
    /* 引脚方向 (覆盖 board/Siul2_Port_Ip_Cfg.c 里按旧线序生成的配置):
       CS/SCK/MOSI 输出, MISO 输入 */
    siul2_mscr(PIN_CS,   0x00200000u);   /* OBE */
    siul2_mscr(PIN_SCK,  0x00200000u);
    siul2_mscr(PIN_MOSI, 0x00200000u);
    siul2_mscr(PIN_MISO, 0x00080000u);   /* IBE */

    s_cs   = siul2_gpdo(PIN_CS);
    s_sck  = siul2_gpdo(PIN_SCK);
    s_mosi = siul2_gpdo(PIN_MOSI);
    s_miso = siul2_gpdi(PIN_MISO);
    *s_cs   = 1u;
    *s_sck  = 0u;
    *s_mosi = 0u;
}

int spi_soft_xfer(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    uint32_t i;
    int b;

    if (len == 0u || len > 68u) {
        return -1;
    }

    *s_cs = 0u;                       /* CS_N 拉低, 覆盖整包 */

    for (i = 0u; i < len; i++) {
        uint8_t out = (tx != NULL) ? tx[i] : 0xFFu;
        uint8_t in = 0u;

        for (b = 0; b < 8; b++) {
            *s_mosi = (uint8_t)((out & 0x80u) ? 1u : 0u);
            *s_sck = 1u;              /* 上升沿: 从机采样 MOSI, 主机采样 MISO */
            in = (uint8_t)((in << 1) | (*s_miso & 1u));
            *s_sck = 0u;
            out = (uint8_t)(out << 1);
        }
        if (rx != NULL) {
            rx[i] = in;
        }
    }

    *s_cs = 1u;                       /* 释放 CS_N */
    return 0;
}

uint32_t spi_soft_sck_hz(void)
{
    /* 每次 GPIO 写约 3~4 个 AIPS 周期, 8bit 约 350ns -> ~2.8MHz (实测) */
    return 3000000u;
}
