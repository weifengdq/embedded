/*
 * gpio_irq_btn - USR 按键 PA03: GPIO 双边沿中断 + 软件消抖 + 事件计数 + 内部回环自测
 *
 * 硬件: hpm5e31_1   USR BTN = PA03 (按下为高, 内部下拉)   USR LED = PA02 (高亮)
 * 自证: 上电先做一次 IOC LOOP_BACK 回环自测(PA03 当输出, DO 回送到 DI, 软件造边沿),
 *       打印 ISR/press/release 计数, 证明中断链路与消抖逻辑; 之后恢复为输入供人工按键.
 * 命令: btn_info / btn_stat / btn_reset / btn_deb / btn_edge / btn_irq / btn_poll / btn_selftest
 */
#include "bsp.h"
#include "hpm_gpio_drv.h"
#include "hpm_mchtmr_drv.h"
#include "hpm_iomux.h"

#define BTN_CTRL  BOARD_APP_GPIO_CTRL
#define BTN_PORT  BOARD_APP_GPIO_INDEX
#define BTN_PIN   BOARD_APP_GPIO_PIN
#define BTN_PAD   IOC_PAD_PA03
#define LED_CTRL  BOARD_LED_GPIO_CTRL
#define LED_PORT  BOARD_LED_GPIO_INDEX
#define LED_PIN   BOARD_LED_GPIO_PIN

static volatile uint32_t s_isr_cnt;      /* 进 ISR 次数(未消抖) */
static volatile uint32_t s_press_cnt;    /* 消抖后按下次数 */
static volatile uint32_t s_release_cnt;  /* 消抖后释放次数 */
static volatile uint32_t s_bounce_cnt;   /* 被判为抖动的边沿数 */
static volatile uint32_t s_level;        /* 最近一次确认的电平 */
static volatile uint64_t s_last_tick;    /* 最近一次确认边沿的 mchtmr 计数 */
static volatile uint32_t s_evt;          /* bit0 press, bit1 release */
static volatile uint32_t s_debounce_ms = 20U;
static volatile uint32_t s_tick_per_ms = 1U;
static volatile uint32_t s_trigger = 4U; /* gpio_interrupt_trigger_edge_both */
static volatile uint32_t s_irq_on = 1U;
static uint32_t s_loopback_ok;

static uint32_t btn_now_ms(void)
{
    return (uint32_t) (mchtmr_get_count(HPM_MCHTMR) / s_tick_per_ms);
}

/* ---- 中断服务程序: 时间窗消抖 + 电平变化判定 ---- */
SDK_DECLARE_EXT_ISR_M(BOARD_APP_GPIO_IRQ, isr_btn)
void isr_btn(void)
{
    uint64_t now = mchtmr_get_count(HPM_MCHTMR);
    uint32_t level = gpio_read_pin(BTN_CTRL, BTN_PORT, BTN_PIN);
    gpio_clear_pin_interrupt_flag(BTN_CTRL, BTN_PORT, BTN_PIN);
    s_isr_cnt++;
    if ((now - s_last_tick) < (uint64_t) s_debounce_ms * s_tick_per_ms) {
        s_bounce_cnt++;
        return;
    }
    if (level == s_level) {
        s_bounce_cnt++;
        return;
    }
    s_last_tick = now;
    s_level = level;
    if (level) {
        s_press_cnt++;
        s_evt |= 1U;
    } else {
        s_release_cnt++;
        s_evt |= 2U;
    }
}

static void btn_pin_as_input(void)
{
    HPM_IOC->PAD[BTN_PAD].FUNC_CTL = IOC_PA03_FUNC_CTL_GPIO_A_03;
    HPM_IOC->PAD[BTN_PAD].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(0);
    gpio_set_pin_input(BTN_CTRL, BTN_PORT, BTN_PIN);
}

/* 内部回环自测: PA03 切输出 + IOC LOOP_BACK, DO 回送 DI, 由软件造出真实边沿 */
static uint32_t btn_selftest(uint32_t cycles)
{
    uint32_t isr0 = s_isr_cnt, press0 = s_press_cnt, rel0 = s_release_cnt, bounce0 = s_bounce_cnt;
    uint32_t gap = s_debounce_ms + 5U;

    if (cycles == 0U) { cycles = 5U; }
    if (cycles > 50U) { cycles = 50U; }

    gpio_disable_pin_interrupt(BTN_CTRL, BTN_PORT, BTN_PIN);
    HPM_IOC->PAD[BTN_PAD].FUNC_CTL = IOC_PA03_FUNC_CTL_GPIO_A_03 | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
    HPM_IOC->PAD[BTN_PAD].PAD_CTL = 0;
    gpio_set_pin_output_with_initial(BTN_CTRL, BTN_PORT, BTN_PIN, 0U);
    gpio_clear_pin_interrupt_flag(BTN_CTRL, BTN_PORT, BTN_PIN);
    s_level = 0U;
    s_last_tick = mchtmr_get_count(HPM_MCHTMR);
    gpio_enable_pin_interrupt(BTN_CTRL, BTN_PORT, BTN_PIN);
    board_delay_ms(gap); /* 先跨过消抖窗口, 否则第一个边沿会被当成抖动丢掉 */

    for (uint32_t i = 0; i < cycles; i++) {
        gpio_write_pin(BTN_CTRL, BTN_PORT, BTN_PIN, 1U);
        board_delay_ms(gap);
        gpio_write_pin(BTN_CTRL, BTN_PORT, BTN_PIN, 0U);
        board_delay_ms(gap);
    }

    gpio_disable_pin_interrupt(BTN_CTRL, BTN_PORT, BTN_PIN);
    btn_pin_as_input();
    gpio_clear_pin_interrupt_flag(BTN_CTRL, BTN_PORT, BTN_PIN);
    gpio_enable_pin_interrupt(BTN_CTRL, BTN_PORT, BTN_PIN);

    s_loopback_ok = (s_isr_cnt != isr0) ? 1U : 0U;
    printf("[selftest] loop_back=%s cycles=%lu isr_delta=%lu press_delta=%lu release_delta=%lu bounce_delta=%lu\r\n",
           s_loopback_ok ? "OK" : "NO-EDGE",
           (unsigned long) cycles,
           (unsigned long) (s_isr_cnt - isr0),
           (unsigned long) (s_press_cnt - press0),
           (unsigned long) (s_release_cnt - rel0),
           (unsigned long) (s_bounce_cnt - bounce0));
    printf("[selftest] expect press=%lu release=%lu when debounce=%lu ms < gap=%lu ms\r\n",
           (unsigned long) cycles, (unsigned long) cycles,
           (unsigned long) s_debounce_ms, (unsigned long) gap);
    return s_loopback_ok;
}

static void btn_apply_trigger(uint32_t t)
{
    gpio_interrupt_trigger_t trig;
    switch (t) {
    case 0:  trig = gpio_interrupt_trigger_edge_rising;  break;
    case 1:  trig = gpio_interrupt_trigger_edge_falling; break;
    case 2:  trig = gpio_interrupt_trigger_level_high;   break;
    case 3:  trig = gpio_interrupt_trigger_level_low;    break;
    default: trig = gpio_interrupt_trigger_edge_both; t = 4U; break;
    }
    s_trigger = t;
    gpio_config_pin_interrupt(BTN_CTRL, BTN_PORT, BTN_PIN, trig);
}

static const char *trigger_name(uint32_t t)
{
    switch (t) {
    case 0: return "rising";
    case 1: return "falling";
    case 2: return "level_high";
    case 3: return "level_low";
    default: return "both_edges";
    }
}

static void print_stat(void)
{
    printf("isr=%lu  press=%lu  release=%lu  bounce_rejected=%lu  level=%lu\r\n",
           (unsigned long) s_isr_cnt, (unsigned long) s_press_cnt,
           (unsigned long) s_release_cnt, (unsigned long) s_bounce_cnt,
           (unsigned long) s_level);
}

/* ---- shell 命令 ---- */
static void cmd_info(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("gpio_irq_btn @ hpm5e31_1\r\n");
    printf("  button : PA03 (GPIO0 port A pin 3), pressed=1, internal pull-down\r\n");
    printf("  led    : PA02 (GPIO0 port A pin 2), active high\r\n");
    printf("  irq    : %d (GPIO0_A), priority 1\r\n", (int) BOARD_APP_GPIO_IRQ);
    printf("  trigger: %s, debounce: %lu ms, mchtmr: %lu ticks/ms\r\n",
           trigger_name(s_trigger), (unsigned long) s_debounce_ms,
           (unsigned long) s_tick_per_ms);
    printf("  irq_on : %lu, loopback: %s, uptime: %lu ms\r\n", (unsigned long) s_irq_on,
           s_loopback_ok ? "yes" : "no", (unsigned long) btn_now_ms());
}

static void cmd_stat(int argc, char *argv[])
{
    (void) argc; (void) argv;
    print_stat();
}

static void cmd_reset(int argc, char *argv[])
{
    (void) argc; (void) argv;
    s_isr_cnt = 0; s_press_cnt = 0; s_release_cnt = 0; s_bounce_cnt = 0;
    s_evt = 0;
    printf("counters cleared\r\n");
}

static void cmd_deb(int argc, char *argv[])
{
    uint32_t v = 20U;
    if (argc >= 2 && bsp_parse_u32(argv[1], &v) == 0) {
        if (v > 2000U) { v = 2000U; }
        s_debounce_ms = v;
    }
    printf("debounce = %lu ms (0 = 只做电平变化判定)\r\n", (unsigned long) s_debounce_ms);
}

static void cmd_edge(int argc, char *argv[])
{
    uint32_t v = 4U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &v); }
    btn_apply_trigger(v);
    printf("trigger = %s\r\n", trigger_name(s_trigger));
}

static void cmd_irq(int argc, char *argv[])
{
    uint32_t v = 1U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &v); }
    if (v) {
        gpio_clear_pin_interrupt_flag(BTN_CTRL, BTN_PORT, BTN_PIN);
        gpio_enable_pin_interrupt(BTN_CTRL, BTN_PORT, BTN_PIN);
        s_irq_on = 1U;
    } else {
        gpio_disable_pin_interrupt(BTN_CTRL, BTN_PORT, BTN_PIN);
        s_irq_on = 0U;
    }
    printf("pin irq %s\r\n", s_irq_on ? "enabled" : "disabled");
}

/* 轮询方式对比: 关闭引脚中断, 以固定采样间隔读电平, 统计边沿与循环次数 */
static void cmd_poll(int argc, char *argv[])
{
    uint32_t ms = 1000U, sample_us = 1000U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &ms); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &sample_us); }
    if (ms > 10000U) { ms = 10000U; }
    if (sample_us < 10U) { sample_us = 10U; }
    if (sample_us > 20000U) { sample_us = 20000U; }

    gpio_disable_pin_interrupt(BTN_CTRL, BTN_PORT, BTN_PIN);
    uint32_t last = gpio_read_pin(BTN_CTRL, BTN_PORT, BTN_PIN);
    uint32_t edges = 0, presses = 0, loops = 0;
    uint64_t t0 = mchtmr_get_count(HPM_MCHTMR);
    uint64_t ticks = (uint64_t) ms * s_tick_per_ms;
    while ((mchtmr_get_count(HPM_MCHTMR) - t0) < ticks) {
        uint32_t l = gpio_read_pin(BTN_CTRL, BTN_PORT, BTN_PIN);
        loops++;
        if (l != last) {
            edges++;
            if (l) { presses++; }
            last = l;
        }
        board_delay_us(sample_us);
    }
    if (s_irq_on) {
        gpio_enable_pin_interrupt(BTN_CTRL, BTN_PORT, BTN_PIN);
    }
    printf("poll %lu ms, sample %lu us: loops=%lu edges=%lu presses(未消抖)=%lu\r\n",
           (unsigned long) ms, (unsigned long) sample_us,
           (unsigned long) loops, (unsigned long) edges, (unsigned long) presses);
}

static void cmd_selftest(int argc, char *argv[])
{
    uint32_t n = 5U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &n); }
    (void) btn_selftest(n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 btn_info, cmd_info, gpio irq button: pin/irq/debounce config);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 btn_stat, cmd_stat, gpio irq button: counters);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 btn_reset, cmd_reset, gpio irq button: clear counters);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 btn_deb, cmd_deb, gpio irq button: btn_deb <ms> set debounce window);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 btn_edge, cmd_edge, gpio irq button: btn_edge <0:rise 1:fall 2:lvl_hi 3:lvl_lo 4:both>);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 btn_irq, cmd_irq, gpio irq button: btn_irq <0|1> pin interrupt on/off);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 btn_poll, cmd_poll, gpio irq button: btn_poll <ms> [sample_us] polling compare);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 btn_selftest, cmd_selftest, gpio irq button: btn_selftest [cycles] IOC loopback self test);

int main(void)
{
    bsp_init();
    init_butn_pins();
    init_led_pins();
    btn_pin_as_input();
    gpio_set_pin_output_with_initial(LED_CTRL, LED_PORT, LED_PIN, 0U);

    s_tick_per_ms = (uint32_t) (clock_get_frequency(clock_mchtmr0) / 1000U);
    if (s_tick_per_ms == 0U) { s_tick_per_ms = 24U; }
    s_level = gpio_read_pin(BTN_CTRL, BTN_PORT, BTN_PIN);
    s_last_tick = mchtmr_get_count(HPM_MCHTMR);

    gpio_config_pin_interrupt(BTN_CTRL, BTN_PORT, BTN_PIN, gpio_interrupt_trigger_edge_both);
    gpio_clear_pin_interrupt_flag(BTN_CTRL, BTN_PORT, BTN_PIN);
    intc_m_enable_irq_with_priority(BOARD_APP_GPIO_IRQ, 1);

    printf("\r\n=== gpio_irq_btn (PA03 irq + debounce) ===\r\n");
    printf("mchtmr=%lu Hz, irq=%d, debounce=%lu ms, idle level=%lu\r\n",
           (unsigned long) clock_get_frequency(clock_mchtmr0),
           (int) BOARD_APP_GPIO_IRQ, (unsigned long) s_debounce_ms,
           (unsigned long) s_level);

    /* 上电自测: 不需要人工按键即可验证 中断/消抖/LED 链路 */
    (void) btn_selftest(5U);
    gpio_write_pin(LED_CTRL, LED_PORT, LED_PIN, 0U);
    printf("[selftest] led PA02 DO=%lu DI=%lu (无回送时 DI 等于外部电平)\r\n",
           (unsigned long) gpio_get_pin_output_status(LED_CTRL, LED_PORT, LED_PIN),
           (unsigned long) gpio_read_pin(LED_CTRL, LED_PORT, LED_PIN));
    printf("press USR BTN: serial prints press/release events; try 'btn_stat' or 'btn_poll 500'\r\n");

    uint32_t last_hb = 0;
    while (1) {
        bsp_shell_poll();
        if (s_evt) {
            uint32_t evt = s_evt;
            s_evt = 0;
            if (evt & 1U) {
                gpio_write_pin(LED_CTRL, LED_PORT, LED_PIN, 1U);
                printf("[%6lu ms] BTN press   #%lu\r\n", (unsigned long) btn_now_ms(),
                       (unsigned long) s_press_cnt);
            }
            if (evt & 2U) {
                gpio_write_pin(LED_CTRL, LED_PORT, LED_PIN, 0U);
                printf("[%6lu ms] BTN release #%lu\r\n", (unsigned long) btn_now_ms(),
                       (unsigned long) s_release_cnt);
            }
        }
        if (btn_now_ms() - last_hb >= 5000U) {
            last_hb = btn_now_ms();
            printf("[%6lu ms] heartbeat: isr=%lu press=%lu bounce_rejected=%lu\r\n",
                   (unsigned long) last_hb, (unsigned long) s_isr_cnt,
                   (unsigned long) s_press_cnt, (unsigned long) s_bounce_cnt);
        }
    }
    return 0;
}
