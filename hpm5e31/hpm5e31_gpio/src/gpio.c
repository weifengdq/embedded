/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "board.h"
#include "hpm_gpio_drv.h"

#define POLL_INTERVAL_MS            (10U)
#define LED_BLINK_PERIOD_IDLE_MS    (1000U)
#define LED_BLINK_PERIOD_PRESSED_MS (100U)

static bool board_button_is_pressed(void)
{
    return gpio_read_pin(BOARD_APP_GPIO_CTRL, BOARD_APP_GPIO_INDEX, BOARD_APP_GPIO_PIN) == BOARD_BUTTON_PRESSED_VALUE;
}

int main(void)
{
    bool button_pressed;
    bool last_button_pressed;
    bool led_is_on = false;
    uint32_t blink_period_ms = LED_BLINK_PERIOD_IDLE_MS;
    uint32_t elapsed_ms = 0;

    board_init();
    board_init_gpio_pins();
    board_init_led_pins();
    board_led_write(BOARD_LED_OFF_LEVEL);

    last_button_pressed = board_button_is_pressed();
    printf("gpio example on hpm5e31_lite\n");
    printf("LED: PA02 active-high, KEY: PA03 active-high\n");
    printf("blink period: %ums\n", last_button_pressed ? LED_BLINK_PERIOD_PRESSED_MS : LED_BLINK_PERIOD_IDLE_MS);

    while (1) {
        button_pressed = board_button_is_pressed();
        if (button_pressed != last_button_pressed) {
            blink_period_ms = button_pressed ? LED_BLINK_PERIOD_PRESSED_MS : LED_BLINK_PERIOD_IDLE_MS;
            elapsed_ms = 0;
            last_button_pressed = button_pressed;
            printf("button %s, blink period: %ums\n", button_pressed ? "pressed" : "released", blink_period_ms);
        }

        elapsed_ms += POLL_INTERVAL_MS;
        if (elapsed_ms >= blink_period_ms) {
            elapsed_ms = 0;
            led_is_on = !led_is_on;
            board_led_toggle();
            printf("led %s, blink period: %ums\n", led_is_on ? "on" : "off", blink_period_ms);
        }

        board_delay_ms(POLL_INTERVAL_MS);
    }

    return 0;
}