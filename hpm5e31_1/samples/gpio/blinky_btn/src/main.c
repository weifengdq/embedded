/*
 * USR LED PA02 (active high) + USR BTN PA03 (pressed = high)
 */
#include "board.h"
#include "hpm_gpio_drv.h"
#include "hpm_debug_console.h"

int main(void)
{
    board_init();
    board_init_led_pins();
    board_init_butn_pins();
    gpio_set_pin_output(BOARD_LED_GPIO_CTRL, BOARD_LED_GPIO_INDEX, BOARD_LED_GPIO_PIN);
    gpio_set_pin_input(BOARD_APP_GPIO_CTRL, BOARD_APP_GPIO_INDEX, BOARD_APP_GPIO_PIN);

    printf("blinky_btn: press USR BTN (PA03) to change blink rate\r\n");
    uint32_t delay = 500;
    uint8_t on = 1, last_btn = 0;
    while (1) {
        uint8_t btn = gpio_read_pin(BOARD_APP_GPIO_CTRL, BOARD_APP_GPIO_INDEX, BOARD_APP_GPIO_PIN);
        if (btn && !last_btn) {
            delay = (delay == 500) ? 100 : ((delay == 100) ? 50 : 500);
            printf("btn pressed -> blink delay %lu ms\r\n", (unsigned long)delay);
        }
        last_btn = btn;
        on = !on;
        gpio_write_pin(BOARD_LED_GPIO_CTRL, BOARD_LED_GPIO_INDEX, BOARD_LED_GPIO_PIN, on);
        board_delay_ms(delay);
    }
    return 0;
}
