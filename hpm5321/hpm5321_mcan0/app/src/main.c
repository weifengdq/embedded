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

int main(void)
{
    board_init();
    init_pins();

    int print_cnt = 0;
    while (1) {
        board_led_toggle();
        printf("hi hpm %d\n", print_cnt++);
        board_delay_ms(1000);
    }
    return 0;
}
