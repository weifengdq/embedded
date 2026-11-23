/*
 * pinmux_net2.c - tp-net2 负责的以太网引脚复用与 YT8011AN 硬件控制
 *
 * 说明:
 *  - RGMII / MDIO 的 IOC 配置已在 boards/hpm5e31_1/pinmux.c 的
 *    init_enet0_pins_rgmii() / init_yt8011an_ctrl_pins() 中实现（板级公共代码），
 *    本文件只做本 domain 需要的组合封装与 PHY 复位时序，避免重复定义符号。
 *  - 由 boards/hpm5e31_1/CMakeLists.txt 的 file(GLOB pins/pinmux_*.c) 自动编译。
 */
#include "board.h"
#include "pinmux.h"
#include "clock.h"
#include "hpm_gpio_drv.h"

void net2_enet_rgmii_pins_init(void)
{
    /* PF02~PF13 RGMII + PA30/PA31 MDIO */
    init_enet0_pins_rgmii();
}

void net2_yt8011_ctrl_pins_init(void)
{
    /* PB29 nRST 输出、PB28 nINT 输入（内部上拉） */
    init_yt8011an_ctrl_pins();
    gpio_set_pin_output_with_initial(BOARD_YT8011_RST_GPIO_CTRL,
                                     BOARD_YT8011_RST_GPIO_INDEX,
                                     BOARD_YT8011_RST_GPIO_PIN, 1);
    gpio_set_pin_input(BOARD_YT8011_INT_GPIO_CTRL,
                       BOARD_YT8011_INT_GPIO_INDEX,
                       BOARD_YT8011_INT_GPIO_PIN);
}

void net2_yt8011_hw_reset(void)
{
    /* nRST 低有效：拉低 2ms -> 释放 -> 等待 30ms 内部初始化 */
    gpio_write_pin(BOARD_YT8011_RST_GPIO_CTRL, BOARD_YT8011_RST_GPIO_INDEX,
                   BOARD_YT8011_RST_GPIO_PIN, 0);
    board_delay_ms(BOARD_NET2_PHY_RST_LOW_MS);
    gpio_write_pin(BOARD_YT8011_RST_GPIO_CTRL, BOARD_YT8011_RST_GPIO_INDEX,
                   BOARD_YT8011_RST_GPIO_PIN, 1);
    board_delay_ms(BOARD_NET2_PHY_RST_HIGH_MS);
}

void net2_enet_board_init(void)
{
    net2_enet_rgmii_pins_init();
    net2_yt8011_ctrl_pins_init();
    init_eth0_clock();          /* ENET0 CSR / MDIO 时钟 */
    net2_yt8011_hw_reset();
}
