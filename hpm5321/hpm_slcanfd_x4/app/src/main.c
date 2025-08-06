/*
 * Copyright (c) 2024 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <stdio.h>

#include "hpm_debug_console.h"
#include "hpm_gpio_drv.h"

#include "board.h"
#include "bsp_gpio.h"

int main(void)
{
    board_init();
    init_pins();
    bsp_led_init();

    int print_cnt = 0;
    while (1) {
        // toggle all LEDs
        for (led_index_t i = 0; i < LED_COUNT; i++) {
            bsp_led_toggle(i);
        }
        printf("hi hpm %d\n", print_cnt++);
        board_delay_ms(2000);
    }
    return 0;
}
