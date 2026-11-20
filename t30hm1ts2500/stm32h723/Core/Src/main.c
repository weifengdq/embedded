/**
 * @file  main.c
 * @brief STM32H723ZGT6 + T30HM1TS2500 (S2500) 10BASE-T1S 移植示例
 */
#include "s2500_app.h"
#include "s2500_port.h"
#include "board.h"
#include <string.h>

static s2500_app_cfg_t s_cfg = {
    .hal        = NULL,                 /* main 里绑定 s2500_platform_hal() */
    .mac        = NULL,                 /* NULL: 使用芯片出厂 MAC */
    .plca_id    = 1,                    /* 本板 PLCA ID = 1, 唯一; 主机=0 HPM=2 S32K=3 TC387=4 RK3506=5 */
    .plca_ncnt  = 8,
    .plca_totmr = 32,
    .plca_en    = 1,                    /* 默认 PLCA follower (主机是协调者 0) */
    .promiscuous = 0,
    .ip         = 0xC0A80A0Bu,          /* 192.168.10.11 */
    .mask       = 0xFFFFFF00u,          /* 255.255.255.0 */
    .gw         = 0xC0A80A0Au,          /* 192.168.10.10 */
    .board_name = "STM32H723 + T30HM1TS2500",
};

static void boot_msg(const char *s)
{
    board_uart_write(s, (unsigned int)strlen(s));
}

static void boot_banner(void)
{
    boot_msg("\r\n");
    boot_msg("[BOOT] STM32H723ZGT6 + T30HM1TS2500 (S2500) 10BASE-T1S\r\n");

    boot_msg("[BOOT] SYSCLK ");
    {
        uint32_t mhz = g_board_sysclk_hz / 1000000u;
        char t[12];
        int i = 0;
        if (mhz == 0u) { t[i++] = '0'; }
        while (mhz > 0u) { t[i++] = (char)('0' + (mhz % 10u)); mhz /= 10u; }
        while (i > 0) { char c = t[--i]; board_uart_write(&c, 1); }
    }
    boot_msg(" MHz / HCLK ");
    {
        uint32_t mhz = g_board_hclk_hz / 1000000u;
        char t[12];
        int i = 0;
        if (mhz == 0u) { t[i++] = '0'; }
        while (mhz > 0u) { t[i++] = (char)('0' + (mhz % 10u)); mhz /= 10u; }
        while (i > 0) { char c = t[--i]; board_uart_write(&c, 1); }
    }
    boot_msg(" MHz / SPI1 ");
    {
        uint32_t khz = board_spi_clock_hz() / 1000u;
        char t[12];
        int i = 0;
        if (khz == 0u) { t[i++] = '0'; }
        while (khz > 0u) { t[i++] = (char)('0' + (khz % 10u)); khz /= 10u; }
        while (i > 0) { char c = t[--i]; board_uart_write(&c, 1); }
    }
    boot_msg(" kHz\r\n");
    boot_msg(g_board_hse_ok ? "[BOOT] HSE 25MHz OK, VOS0, D-Cache ON\r\n"
                            : "[BOOT] HSE FAIL -> HSI fallback, D-Cache ON\r\n");
    boot_msg("[BOOT] USART1 115200 8N1 / PLCA id=1\r\n");
}

int main(void)
{
    s2500_board_init();                 /* MPU/D-Cache/时钟/GPIO/SPI1/USART1/DMA/DWT */

    boot_banner();

    s_cfg.hal = (const s2500_hal_t *)s2500_platform_hal();

    /* 初始化失败时每 2 s 重试一次, 便于现场排查 SPI / 供电 / 复位问题 */
    while (s2500_app_init(&s_cfg) != S2500_OK) {
        boot_msg("[BOOT] s2500_app_init failed (SPI1 wiring / 3V3 / IDVER?), retry in 2s\r\n");
        HAL_Delay(2000);
        boot_msg("[BOOT] retry ...\r\n");
    }

    for (;;) {
        s2500_app_poll();
    }
}
