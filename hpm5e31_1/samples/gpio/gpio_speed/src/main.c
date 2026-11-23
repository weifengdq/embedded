/*
 * gpio_speed - GPIO 软件翻转/读写的极限速度测量 (mcycle 计时)
 *
 * 硬件: hpm5e31_1
 *   - USR LED       PA02  单脚翻转测试
 *   - CAN 状态灯x8  PF26/PF18/PF20/PF16/PF22/PF21/PF23/PF19  端口级整字写测试
 * 方法: 用 mcycle 计 CPU 周期, 减去空循环基线, 得到每次 GPIO 操作的周期数/纳秒,
 *       并给出由软件能达到的最高翻转频率(硬件引脚边沿速度受 PAD_CTL 的 SR/SPD 限制).
 * 命令: spd_info / spd [N] / spd_bank / spd_fgpio / spd_dump
 */
#include "bsp.h"
#include "hpm_gpio_drv.h"
#include "hpm_gpiom_drv.h"

#define LED_CTRL HPM_GPIO0
#define LED_PORT BOARD_LED_GPIO_INDEX
#define LED_PIN  BOARD_LED_GPIO_PIN

#define BANK_CTRL HPM_GPIO0
#define BANK_PORT GPIO_DO_GPIOF
#define BANK_MASK 0x07FF0000UL /* PF16..PF26, 见 board.h 的 CANLED 定义 */

static uint32_t s_cpu_hz = 480000000UL;
static uint32_t s_iters = 200000UL;
static volatile uint32_t s_sink;

static inline uint64_t cyc(void)
{
    return hpm_csr_get_core_mcycle();
}

/* cycles/op 用 x100 定点打印, ns/op 与等效频率由 CPU 主频换算 */
static void report(const char *name, uint32_t iters, uint64_t cycles)
{
    if (cycles == 0U) { cycles = 1U; }
    uint64_t cyc100 = cycles * 100ULL / iters;
    uint64_t ns = cycles * 1000000000ULL / ((uint64_t) iters * s_cpu_hz);
    uint64_t ns100 = (cycles * 100000000000ULL / ((uint64_t) iters * s_cpu_hz)) % 100ULL;
    uint64_t khz = ((uint64_t) s_cpu_hz * iters) / (cycles * 1000ULL);
    printf("%-18s N=%-7lu cyc/op=%lu.%02lu  ns/op=%lu.%02lu  f=%lu kHz\r\n",
           name, (unsigned long) iters,
           (unsigned long) (cyc100 / 100ULL), (unsigned long) (cyc100 % 100ULL),
           (unsigned long) ns, (unsigned long) ns100, (unsigned long) khz);
}

static uint64_t bench_empty(uint32_t n)
{
    uint64_t t0 = cyc();
    for (volatile uint32_t i = 0; i < n; i++) {
    }
    return cyc() - t0;
}

static uint64_t bench_api_toggle(uint32_t n)
{
    uint64_t t0 = cyc();
    for (uint32_t i = 0; i < n; i++) {
        gpio_toggle_pin(LED_CTRL, LED_PORT, LED_PIN);
    }
    return cyc() - t0;
}

static uint64_t bench_reg_toggle(uint32_t n)
{
    uint64_t t0 = cyc();
    for (uint32_t i = 0; i < n; i++) {
        LED_CTRL->DO[LED_PORT].TOGGLE = 1UL << LED_PIN;
    }
    return cyc() - t0;
}

static uint64_t bench_set_clear(uint32_t n)
{
    uint64_t t0 = cyc();
    for (uint32_t i = 0; i < n; i++) {
        LED_CTRL->DO[LED_PORT].SET = 1UL << LED_PIN;
        LED_CTRL->DO[LED_PORT].CLEAR = 1UL << LED_PIN;
    }
    return cyc() - t0;
}

static uint64_t bench_bank_word(uint32_t n)
{
    uint64_t t0 = cyc();
    for (uint32_t i = 0; i < n; i++) {
        BANK_CTRL->DO[BANK_PORT].VALUE = (i & 1U) ? BANK_MASK : 0U;
    }
    return cyc() - t0;
}

static uint64_t bench_read_pin(uint32_t n)
{
    uint32_t sum = 0;
    uint64_t t0 = cyc();
    for (uint32_t i = 0; i < n; i++) {
        sum += gpio_read_pin(BANK_CTRL, BANK_PORT, 26U);
    }
    s_sink = sum;
    return cyc() - t0;
}

static uint64_t bench_read_port(uint32_t n)
{
    uint32_t sum = 0;
    uint64_t t0 = cyc();
    for (uint32_t i = 0; i < n; i++) {
        sum += BANK_CTRL->DI[BANK_PORT].VALUE;
    }
    s_sink = sum;
    return cyc() - t0;
}

static uint64_t bench_fgpio_toggle(uint32_t n)
{
    uint64_t t0 = cyc();
    for (uint32_t i = 0; i < n; i++) {
        HPM_FGPIO->DO[LED_PORT].TOGGLE = 1UL << LED_PIN;
    }
    return cyc() - t0;
}

static void led_bank_init(void)
{
    init_canled_pins();
    for (uint32_t i = 16U; i <= 26U; i++) {
        if (BANK_MASK & (1UL << i)) {
            gpio_set_pin_output(BANK_CTRL, BANK_PORT, (uint8_t) i);
            gpio_write_pin(BANK_CTRL, BANK_PORT, (uint8_t) i, 0U);
        }
    }
}

static void run_all(uint32_t n)
{
    uint64_t base;
    s_cpu_hz = clock_get_frequency(clock_cpu0);
    uint32_t ps_per_cycle = (uint32_t) (1000000000000ULL / s_cpu_hz);
    printf("\r\ngpio_speed: cpu=%lu Hz, N=%lu, 1 cycle=%lu.%02lu ns\r\n",
           (unsigned long) s_cpu_hz, (unsigned long) n,
           (unsigned long) (ps_per_cycle / 1000U), (unsigned long) ((ps_per_cycle % 1000U) / 10U));
    printf("---- 基准 ----\r\n");
    base = bench_empty(n);
    report("empty_loop", n, base);
    printf("---- 单脚(PA02) 翻转 ----\r\n");
    report("api_toggle", n, bench_api_toggle(n));
    report("reg_toggle", n, bench_reg_toggle(n));
    report("reg_set_clear", n, bench_set_clear(n));
    printf("---- 端口级整字写(PF16..PF26) ----\r\n");
    report("port_word_write", n, bench_bank_word(n));
    printf("---- 读 ----\r\n");
    report("read_pin", n, bench_read_pin(n));
    report("read_port", n, bench_read_port(n));
    printf("---- FGPIO(核心私有快速总线) ----\r\n");
    gpiom_set_pin_controller(HPM_GPIOM, LED_PORT, LED_PIN, gpiom_core0_fast);
    gpiom_enable_pin_visibility(HPM_GPIOM, LED_PORT, LED_PIN, gpiom_core0_fast);
    HPM_FGPIO->DO[LED_PORT].CLEAR = 1UL << LED_PIN;
    HPM_FGPIO->OE[LED_PORT].SET = 1UL << LED_PIN;
    report("fgpio_toggle", n, bench_fgpio_toggle(n));
    /* 还原映射, 避免影响后续命令/其它例程 */
    HPM_FGPIO->OE[LED_PORT].CLEAR = 1UL << LED_PIN;
    gpiom_set_pin_controller(HPM_GPIOM, LED_PORT, LED_PIN, gpiom_soc_gpio0);
    gpiom_enable_pin_visibility(HPM_GPIOM, LED_PORT, LED_PIN, gpiom_soc_gpio0);
    gpio_set_pin_output_with_initial(LED_CTRL, LED_PORT, LED_PIN, 0U);
    printf("---- 结论: 单脚翻转上限约 %lu MHz(软件, 不含 PAD 边沿/示波器限制) ----\r\n",
           (unsigned long) (((uint64_t) s_cpu_hz * n) / (bench_reg_toggle(n) * 2000ULL)));
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("gpio_speed @ hpm5e31_1: cpu=%lu Hz\r\n", (unsigned long) clock_get_frequency(clock_cpu0));
    printf("  single pin: PA02 (USR LED, active high)\r\n");
    printf("  port bank : GPIOF PF16..PF26 (8x CAN 状态灯), mask=0x%08lX\r\n",
           (unsigned long) BANK_MASK);
    printf("  usage     : spd [N]  spd_bank  spd_fgpio  spd_dump  spd_info\r\n");
}

static void cmd_spd(int argc, char *argv[])
{
    uint32_t n = s_iters;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &n); }
    if (n < 1000U) { n = 1000U; }
    if (n > 5000000U) { n = 5000000U; }
    s_iters = n;
    run_all(n);
}

static void cmd_bank(int argc, char *argv[])
{
    uint32_t pattern = (argc >= 2) ? 0U : 0x00FF0000UL;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &pattern); }
    uint32_t p = pattern & 0x07FF0000UL;
    BANK_CTRL->DO[BANK_PORT].VALUE = p;
    printf("GPIOF DO=0x%08lX (PF16..PF26 remap 到 8 颗 CAN 状态灯)\r\n", (unsigned long) p);
}

static void cmd_fgpio(int argc, char *argv[])
{
    uint32_t n = s_iters;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &n); }
    if (n < 1000U) { n = 1000U; }
    s_cpu_hz = clock_get_frequency(clock_cpu0);
    gpiom_set_pin_controller(HPM_GPIOM, LED_PORT, LED_PIN, gpiom_core0_fast);
    gpiom_enable_pin_visibility(HPM_GPIOM, LED_PORT, LED_PIN, gpiom_core0_fast);
    HPM_FGPIO->OE[LED_PORT].SET = 1UL << LED_PIN;
    report("fgpio_toggle", n, bench_fgpio_toggle(n));
    HPM_FGPIO->OE[LED_PORT].CLEAR = 1UL << LED_PIN;
    gpiom_set_pin_controller(HPM_GPIOM, LED_PORT, LED_PIN, gpiom_soc_gpio0);
    gpiom_enable_pin_visibility(HPM_GPIOM, LED_PORT, LED_PIN, gpiom_soc_gpio0);
    gpio_set_pin_output_with_initial(LED_CTRL, LED_PORT, LED_PIN, 0U);
}

static void cmd_dump(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("GPIO0  DO[A]=0x%08lX DI[A]=0x%08lX\r\n",
           (unsigned long) LED_CTRL->DO[GPIO_DO_GPIOA].VALUE,
           (unsigned long) LED_CTRL->DI[GPIO_DO_GPIOA].VALUE);
    printf("GPIO0  DO[F]=0x%08lX DI[F]=0x%08lX\r\n",
           (unsigned long) LED_CTRL->DO[GPIO_DO_GPIOF].VALUE,
           (unsigned long) LED_CTRL->DI[GPIO_DO_GPIOF].VALUE);
    printf("GPIOM  PA02 controller=%lu, PF26 controller=%lu (0=soc_gpio0, 2=core0_fast)\r\n",
           (unsigned long) gpiom_get_pin_controller(HPM_GPIOM, LED_PORT, LED_PIN),
           (unsigned long) gpiom_get_pin_controller(HPM_GPIOM, BANK_PORT, 26U));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spd_info, cmd_info, gpio speed: show config);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spd, cmd_spd, gpio speed: spd [N] run full benchmark);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spd_bank, cmd_bank, gpio speed: spd_bank [value] write GPIOF word);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spd_fgpio, cmd_fgpio, gpio speed: spd_fgpio [N] FGPIO toggle bench);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spd_dump, cmd_dump, gpio speed: dump DO/DI/GPIOM mapping);

int main(void)
{
    bsp_init();
    init_led_pins();
    gpio_set_pin_output_with_initial(LED_CTRL, LED_PORT, LED_PIN, 0U);
    led_bank_init();
    s_cpu_hz = clock_get_frequency(clock_cpu0);

    printf("\r\n=== gpio_speed (GPIO toggle/IO limit, mcycle timed) ===\r\n");
    run_all(s_iters);
    printf("type 'spd 1000000' to rerun, 'spd_dump' for DO/DI, 'spd_info' for help\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
