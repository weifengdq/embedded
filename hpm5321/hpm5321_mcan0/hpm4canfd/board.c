/*
 * Copyright (c) 2023-2025 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "board.h"

#include "hpm_clock_drv.h"
#include "hpm_gpio_drv.h"
#include "hpm_pcfg_drv.h" /* 添加缺少的头文件 */
#include "hpm_pllctlv2_drv.h"
#include "hpm_sdk_version.h"
#include "hpm_uart_drv.h"


/**
 * @brief FLASH configuration option definitions
 */
#if defined(FLASH_XIP) && FLASH_XIP
__attribute__((section(".nor_cfg_option"), used))
const uint32_t option[4] = {0xfcf90002, 0x00000005, 0x1000, 0x0};
#endif

#if defined(FLASH_UF2) && FLASH_UF2
ATTR_PLACE_AT(".uf2_signature")
__attribute__((used)) const uint32_t uf2_signature = BOARD_UF2_SIGNATURE;
#endif

void board_init_console(void)
{
#if !defined(CONFIG_NDEBUG_CONSOLE) || !CONFIG_NDEBUG_CONSOLE
#if BOARD_CONSOLE_TYPE == CONSOLE_TYPE_UART
    console_config_t cfg;

    clock_add_to_group(BOARD_CONSOLE_UART_CLK_NAME, 0);

    cfg.type = BOARD_CONSOLE_TYPE;
    cfg.base = (uint32_t) BOARD_CONSOLE_UART_BASE;
    cfg.src_freq_in_hz = clock_get_frequency(BOARD_CONSOLE_UART_CLK_NAME);
    cfg.baudrate = BOARD_CONSOLE_UART_BAUDRATE;

    if (status_success != console_init(&cfg)) {
        /* failed to initialize debug console */
        while (1) {
        }
    }
#else
    while (1)
        ;
#endif
#endif
}

void board_print_banner(void)
{
    const uint8_t banner[] =
        "\n"
        "----------------------------------------------------------------------\n"
        "----------------------------------------------------------------------\n";
#ifdef SDK_VERSION_STRING
    printf("hpm_sdk: %s\n", SDK_VERSION_STRING);
#endif
    printf("%s", banner);
}

void board_print_clock_freq(void)
{
    printf("==============================\n");
    printf(" %s clock summary\n", BOARD_NAME);
    printf("==============================\n");
    printf("cpu0:\t %luHz\n", clock_get_frequency(clock_cpu0));
    printf("ahb:\t %luHz\n", clock_get_frequency(clock_ahb));
    printf("mchtmr0:\t %luHz\n", clock_get_frequency(clock_mchtmr0));
    printf("xpi0:\t %luHz\n", clock_get_frequency(clock_xpi0));
    printf("==============================\n");
}

void board_init(void)
{
    board_init_clock();
    board_init_console();
#if BOARD_SHOW_CLOCK
    board_print_clock_freq();
#endif
#if BOARD_SHOW_BANNER
    board_print_banner();
#endif
}

void board_init_clock(void)
{
    uint32_t cpu0_freq = clock_get_frequency(clock_cpu0);

    if (cpu0_freq == PLLCTL_SOC_PLL_REFCLK_FREQ) {
        /* Configure the External OSC ramp-up time: ~9ms */
        pllctlv2_xtal_set_rampup_time(HPM_PLLCTLV2, 32UL * 1000UL * 9U);

        /* Select clock setting preset1 */
        sysctl_clock_set_preset(HPM_SYSCTL, 2);
    }

    /* group0[0] */
    clock_add_to_group(clock_cpu0, 0);
    clock_add_to_group(clock_ahb, 0);
    clock_add_to_group(clock_lmm0, 0);
    clock_add_to_group(clock_mchtmr0, 0);
    clock_add_to_group(clock_rom, 0);
    clock_add_to_group(clock_mot0, 0);
    clock_add_to_group(clock_gpio, 0);
    clock_add_to_group(clock_hdma, 0);
    clock_add_to_group(clock_xpi0, 0);
    clock_add_to_group(clock_ptpc, 0);

    /* Connect Group0 to CPU0 */
    clock_connect_group_to_cpu(0, 0);

    /* Bump up DCDC voltage to 1275mv */
    pcfg_dcdc_set_voltage(HPM_PCFG, 1275);

    /* Configure CPU to 480MHz, AXI/AHB to 160MHz */
    sysctl_config_cpu0_domain_clock(HPM_SYSCTL, clock_source_pll0_clk0, 2, 3);
    /* Configure PLL0 Post Divider */
    pllctlv2_set_postdiv(
        HPM_PLLCTLV2, pllctlv2_pll0, pllctlv2_clk0, pllctlv2_div_1p0); /* PLL0CLK0: 960MHz */
    pllctlv2_set_postdiv(
        HPM_PLLCTLV2, pllctlv2_pll0, pllctlv2_clk1, pllctlv2_div_1p6); /* PLL0CLK1: 600MHz */
    pllctlv2_set_postdiv(
        HPM_PLLCTLV2, pllctlv2_pll0, pllctlv2_clk2, pllctlv2_div_2p4); /* PLL0CLK2: 400MHz */
    /* Configure PLL0 Frequency to 960MHz */
    pllctlv2_init_pll_with_freq(HPM_PLLCTLV2, pllctlv2_pll0, 960000000);

    clock_update_core_clock();

    /* Configure mchtmr to 24MHz */
    clock_set_source_divider(clock_mchtmr0, clk_src_osc24m, 1);
}

void board_delay_us(uint32_t us)
{
    clock_cpu_delay_us(us);
}

void board_delay_ms(uint32_t ms)
{
    clock_cpu_delay_ms(ms);
}

void board_led_write(uint8_t state)
{
    /* 使用 board.h 中正确定义的 GPIO 控制器 */
    gpio_write_pin(BOARD_LED_GPIO_CTRL, BOARD_LED_GPIO_INDEX, BOARD_LED_GPIO_PIN, state);
}

void board_led_toggle(void)
{
    /* 使用 board.h 中正确定义的 GPIO 控制器 */
    gpio_toggle_pin(BOARD_LED_GPIO_CTRL, BOARD_LED_GPIO_INDEX, BOARD_LED_GPIO_PIN);
}

uint32_t board_init_uart_clock(UART_Type *ptr)
{
    uint32_t freq = 0U;
    if (ptr == HPM_UART0) {
        clock_add_to_group(clock_uart0, 0);
        freq = clock_get_frequency(clock_uart0);
    } else if (ptr == HPM_UART1) {
        clock_add_to_group(clock_uart1, 0);
        freq = clock_get_frequency(clock_uart1);
    } else if (ptr == HPM_UART2) {
        clock_add_to_group(clock_uart2, 0);
        freq = clock_get_frequency(clock_uart2);
    } else if (ptr == HPM_UART3) {
        clock_add_to_group(clock_uart3, 0);
        freq = clock_get_frequency(clock_uart3);
    }

    return freq;
}
