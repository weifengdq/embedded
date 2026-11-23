/*
 * letter-shell 基础示例: UART0 控制台 + 默认命令 + 本文件自定义命令
 */
#include "bsp.h"
#include "hpm_gpio_drv.h"

static uint32_t s_counter;
/* R5: 命令分发/打印耗时用 mcycle 计时 (主循环每次进入 shellTask 前打点) */
static volatile uint64_t s_poll_enter;

static void cmd_hello(int argc, char *argv[])
{
    printf("hello from app-level shell command (argc=%d)\r\n", argc);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN, hello, cmd_hello, demo app command);

static void cmd_led(int argc, char *argv[])
{
    /* led <0|1|toggle> */
    static uint8_t on = 0;
    if (argc >= 2) {
        if (argv[1][0] == 't') { on = !on; }
        else { uint32_t v = 0; bsp_parse_u32(argv[1], &v); on = (uint8_t) (v != 0); }
    } else {
        on = !on;
    }
    gpio_write_pin(BOARD_LED_GPIO_CTRL, BOARD_LED_GPIO_INDEX, BOARD_LED_GPIO_PIN, on);
    printf("USR LED(PA02) = %d\r\n", on);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN, led, cmd_led, control USR LED: led [0|1|toggle]);

static void cmd_sh_time(int argc, char *argv[])
{
    (void) argc; (void) argv;
    uint64_t t0 = hpm_csr_get_core_mcycle();
    uint64_t disp = t0 - s_poll_enter;

    uint64_t a0 = hpm_csr_get_core_mcycle();
    volatile uint32_t acc = 0U;
    for (uint32_t i = 0U; i < 1000U; i++) { acc += i; }
    uint64_t a1 = hpm_csr_get_core_mcycle();
    (void) acc;

    uint64_t p0 = hpm_csr_get_core_mcycle();
    printf("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\r\n");
    uint64_t p1 = hpm_csr_get_core_mcycle();

    uint64_t q0 = hpm_csr_get_core_mcycle();
    printf("\r\n");
    uint64_t q1 = hpm_csr_get_core_mcycle();

    printf("sh_time: dispatch=%lu cyc, busy1000=%lu cyc, printf64=%lu cyc, printf2=%lu cyc\r\n",
           (unsigned long) disp, (unsigned long) (a1 - a0),
           (unsigned long) (p1 - p0), (unsigned long) (q1 - q0));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sh_time, cmd_sh_time, measure dispatch/printf cycles with mcycle);

int main(void)
{
    bsp_init();
    board_init_led_pins();
    gpio_write_pin(BOARD_LED_GPIO_CTRL, BOARD_LED_GPIO_INDEX, BOARD_LED_GPIO_PIN, 1);

    printf("\r\n=== hpm5e31_1 letter-shell demo ===\r\n");
    printf("type 'help' to list commands, 'ver' for version\r\n");

    uint32_t last = 0;
    while (1) {
        s_poll_enter = hpm_csr_get_core_mcycle();
        bsp_shell_poll();
        if (bsp_uptime_ms() - last >= 5000U) {
            last = bsp_uptime_ms();
            s_counter++;
            gpio_write_pin(BOARD_LED_GPIO_CTRL, BOARD_LED_GPIO_INDEX, BOARD_LED_GPIO_PIN,
                           (uint8_t) (s_counter & 1U));
        }
    }
    return 0;
}
