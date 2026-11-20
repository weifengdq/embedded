/**
 * @file  main.c
 * @brief HPM5361 + T30HM1TS2500 (S2500) 10BASE-T1S 移植示例
 *
 * 总线约定 (本工程默认值):
 *   - 主机 (K2L / enx001ec0d1c337) 为 PLCA 协调者 node-id 0, node count 8, TO 32BT
 *   - 本板默认 PLCA follower, node-id 2, ncnt 8, totmr 32, 上电即参与 PLCA
 *   - 需要本板做协调者时: s2500 plca id 0 ncnt 8 en 1
 */
#include <string.h>
#include "board.h"                 /* HPM SDK */
#include "s2500_app.h"
#include "s2500_port.h"
#include "bsp.h"

static s2500_app_cfg_t s_cfg = {
    .hal        = NULL,                 /* main 中绑定 s2500_board_hal() */
    .mac        = NULL,                 /* NULL: 使用 S2500 出厂 MAC */
    .plca_id    = 2,                    /* 本板 PLCA ID = 2 (主机为协调者 0) */
    .plca_ncnt  = 8,
    .plca_totmr = 32,
    .plca_en    = 1,                    /* 默认跟随者并参与 PLCA, shell 可改 */
    .promiscuous = 0,
    .ip         = 0xC0A80A0Cu,          /* 192.168.10.12 */
    .mask       = 0xFFFFFF00u,          /* 255.255.255.0 */
    .gw         = 0xC0A80A0Au,          /* 192.168.10.10 */
    .board_name = "HPM5361 + T30HM1TS2500",
};

int main(void)
{
    s2500_board_init();

    bsp_puts("\r\n[BOOT] HPM5361 + T30HM1TS2500 (S2500) 10BASE-T1S\r\n");
    bsp_puts("[BOOT] SPI1 25MHz  CS=PA26 SCLK=PA27 MISO=PA28 MOSI=PA29  HDMA ch0/1\r\n");
    bsp_puts("[BOOT] RST=PA10 IRQ=PA12  UART0(PA00/01)+UART2(PB08/09) 115200\r\n");

    s_cfg.hal = s2500_board_hal();

    /* 初始化失败时每 2 s 重试, 便于现场排查 SPI / 供电 / 复位问题 */
    while (s2500_app_init(&s_cfg) != S2500_OK) {
        bsp_puts("[BOOT] s2500_app_init failed (SPI1 wiring / 3V3 / IDVER?), retry in 2s\r\n");
        bsp_delay_us(2000000);
        bsp_puts("[BOOT] retry ...\r\n");
    }

    /* SPI 通道自检: 阻塞读 vs DMA 读 IDVER, 不一致就自动退回阻塞式收发 */
    (void)bsp_spi_dma_selftest();

    for (;;) {
        s2500_app_poll();
    }
}
