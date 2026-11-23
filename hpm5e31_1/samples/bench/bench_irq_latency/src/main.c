/*
 * bench_irq_latency - 中断延迟基准（hpm5e31_1 / HPM5E31 @480MHz）
 *
 * 测什么:
 *   "中断延迟" = 定时器比较匹配事件发生  ->  用户 ISR 第 1 条指令执行
 *   用 GPTMR 自己的计数器做时间基准（自计时，不需要示波器/GPIO 翻转）：
 *     - GPTMR 通道工作在"比较匹配中断"模式，reload 设成 0xFFFFFFFF（不回绕）
 *     - ISR 第一条语句读出 CNT，latency_ticks = CNT - CMP寄存器值
 *     - ISR 末尾把 CNT 清零，开始下一轮，因此采样频率 = CMP ticks
 *   同时用 mcycle 记录 ISR 之间的周期数，做时基交叉校验（换算 GPTMR 实际频率）。
 *
 * 对比项（同一个固件内切换，无需重复烧写）:
 *   mode=xip    : ISR 主体代码在 XIP flash（默认）
 *   mode=ilm    : ISR 主体代码放在 ILM (.fast 段，0x00000000，128KB)
 *   mode=nocache: 关闭 I-Cache 后再测（放大取指代价）
 *
 * 另外测 GPIO 中断延迟：PA02 输出翻转 -> 同一 pad 的 DI 回读 -> GPIO 边沿中断，
 * 用 mcycle 测"写 DO 寄存器 -> ISR 执行"的周期数（自环回，无需外部飞线）。
 *
 * 硬件: hpm5e31_1，UART4 921600 8N1；USR LED PA02
 * 命令: irq / irqilm / irqnc / irqgpio / irqinfo
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "bsp.h"
#include "hpm_gptmr_drv.h"
#include "hpm_gpio_drv.h"
#include "hpm_l1c_drv.h"
#include "hpm_csr_drv.h"
#include "hpm_clock_drv.h"

#define N_SAMPLES   10000
#define N_SAMPLES_NC 2000

/* GPTMR 时钟目标 100MHz：PLL0 clk1 = 400MHz, 4 分频 */
#define GPTMR_SRC_HZ   400000000UL
#define GPTMR_DIV      4
#define CMP_TICKS      2000       /* 20us @100MHz */

#define LED_PORT  GPIO_DI_GPIOA
#define LED_PIN   2

typedef struct {
    volatile uint16_t *buf;
    volatile uint32_t n;
    uint32_t nmax;
    uint32_t cmp_reg;       /* 比较寄存器实际值（SDK 说明：写 cmp，硬件为 cmp-1） */
    volatile uint32_t prev_cyc;
    volatile uint64_t period_cyc_sum;   /* ISR 之间的 mcycle 增量之和 */
    volatile uint32_t isr_last_cyc;
    volatile uint64_t active_cyc_sum;  /* 中断入口->出口 的周期累计 */
} msctx_t;

static msctx_t g_ctx;
static uint16_t g_samp[N_SAMPLES];

/* GPIO 中断测量 */
static volatile uint32_t g_gpio_n;
static volatile uint32_t g_gpio_stamp_cyc;
static volatile uint16_t g_gpio_samp[N_SAMPLES / 5];
static uint32_t g_gpio_cpu_hz;

static uint32_t g_gptmr_hz;
static uint32_t g_cpu_hz;

/* ---------- GPTMR 中断 ---------- */
SDK_DECLARE_EXT_ISR_M(IRQn_GPTMR0, isr_gptmr0)

static void isr_measure_body(GPTMR_Type *tmr, uint8_t ch)
{
    /* 第 1 条语句：读计数器（这之后的时间不属于"中断延迟"） */
    uint32_t cnt = tmr->CHANNEL[ch].CNT;
    uint32_t cyc = (uint32_t) hpm_csr_get_core_mcycle();
    msctx_t *c = &g_ctx;

    if (c->n < c->nmax) {
        uint32_t lt = cnt - c->cmp_reg;
        c->buf[c->n] = (lt > 0xFFFFu) ? 0xFFFFu : (uint16_t) lt;
        c->n++;
        if (c->prev_cyc != 0u) {
            c->period_cyc_sum += (uint32_t) (cyc - c->prev_cyc);
        }
        c->prev_cyc = cyc;
    }
    c->isr_last_cyc = cyc;
    gptmr_clear_status(tmr, GPTMR_CH_CMP_STAT_MASK(ch, 0));
    gptmr_channel_reset_count(tmr, ch);
    c->active_cyc_sum += (uint32_t) hpm_csr_get_core_mcycle() - cyc;
}

/* ISR 主体放 ILM(.fast)：单独一份代码，供 mode=ilm 使用 */
__attribute__((section(".fast"), noinline))
static void isr_measure_body_ilm(GPTMR_Type *tmr, uint8_t ch)
{
    uint32_t cnt = tmr->CHANNEL[ch].CNT;
    uint32_t cyc = (uint32_t) hpm_csr_get_core_mcycle();
    msctx_t *c = &g_ctx;

    if (c->n < c->nmax) {
        uint32_t lt = cnt - c->cmp_reg;
        c->buf[c->n] = (lt > 0xFFFFu) ? 0xFFFFu : (uint16_t) lt;
        c->n++;
        if (c->prev_cyc != 0u) {
            c->period_cyc_sum += (uint32_t) (cyc - c->prev_cyc);
        }
        c->prev_cyc = cyc;
    }
    c->isr_last_cyc = cyc;
    gptmr_clear_status(tmr, GPTMR_CH_CMP_STAT_MASK(ch, 0));
    gptmr_channel_reset_count(tmr, ch);
    c->active_cyc_sum += (uint32_t) hpm_csr_get_core_mcycle() - cyc;
}

static volatile uint8_t g_isr_mode;   /* 0=xip 1=ilm */

void isr_gptmr0(void)
{
    if (g_isr_mode == 0u) {
        isr_measure_body(HPM_GPTMR0, 0);
    } else {
        isr_measure_body_ilm(HPM_GPTMR0, 0);
    }
}

static void gptmr_setup(void)
{
    gptmr_channel_config_t cfg;

    board_init_gptmr_clock(HPM_GPTMR0);
    clock_set_source_divider(clock_gptmr0, clk_src_pll0_clk1, GPTMR_DIV);
    g_gptmr_hz = clock_get_frequency(clock_gptmr0);

    gptmr_channel_get_default_config(HPM_GPTMR0, &cfg);
    cfg.reload = 0xFFFFFFFFUL;          /* 不自动回绕，由 ISR 手动清零 */
    cfg.cmp[0] = CMP_TICKS;             /* 到达 CMP_TICKS 时产生比较中断 */
    if (gptmr_channel_config(HPM_GPTMR0, 0, &cfg, false) != status_success) {
        printf("ERROR: gptmr config failed\r\n");
        return;
    }
    g_ctx.cmp_reg = HPM_GPTMR0->CHANNEL[0].CMP[0];   /* 读回硬件实际比较值 */
    gptmr_enable_irq(HPM_GPTMR0, GPTMR_CH_CMP_IRQ_MASK(0, 0));
    gptmr_start_counter(HPM_GPTMR0, 0);
    intc_m_enable_irq_with_priority(IRQn_GPTMR0, 1);
}

/* ---------- 统计 ---------- */
static int cmp_u16(const void *a, const void *b)
{
    return (int) (*(const uint16_t *) a) - (int) (*(const uint16_t *) b);
}

static void report_stats(const char *mode, volatile uint16_t *buf, uint32_t n, uint64_t period_sum, uint64_t active_sum)
{
    static uint16_t sorted[N_SAMPLES];
    for (uint32_t i = 0; i < n; i++) {
        sorted[i] = buf[i];
    }
    /* 简单插入排序太慢，直接冒泡/库函数都行，n=10000 用 qsort */
    qsort((void *) sorted, n, sizeof(uint16_t), cmp_u16);

    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        sum += sorted[i];
    }
    double mean = (double) sum / (double) n;
    double var = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        double d = (double) sorted[i] - mean;
        var += d * d;
    }
    var /= (n > 1 ? (n - 1) : 1);
    double sd = 0.0;
    {
        double g = var;
        for (int k = 0; k < 40; k++) {
            g = 0.5 * (g + var / g);
        }
        sd = g;
    }
    double tick_ns = 1e9 / (double) g_gptmr_hz;
    uint32_t p50 = sorted[n / 2];
    uint32_t p90 = sorted[(uint32_t) (n * 0.90)];
    uint32_t p99 = sorted[(uint32_t) (n * 0.99)];
    /* 周期 = 入口到入口；其中包含 ISR 自身执行时间（CNT 在 ISR 末尾清零），
       扣掉后才能反推 GPTMR 实际时钟 */
    double period_cyc = (n > 1) ? (double) period_sum / (double) (n - 1) : 0.0;
    double active_cyc = (n > 0) ? (double) active_sum / (double) n : 0.0;
    double derived_hz = (period_cyc > active_cyc) ? ((double) g_cpu_hz * (double) CMP_TICKS / (period_cyc - active_cyc)) : 0.0;

    printf("IRQ_STAT mode=%-8s n=%lu min=%u avg=%.2f max=%u sd=%.2f p50=%u p90=%u p99=%u tick_ns=%.2f\r\n",
           mode, (unsigned long) n, sorted[0], mean, sorted[n - 1], sd, p50, p90, p99, tick_ns);
    printf("IRQ_TIME mode=%-8s min_ns=%.0f avg_ns=%.0f max_ns=%.0f p99_ns=%.0f\r\n",
           mode, (double) sorted[0] * tick_ns, mean * tick_ns, (double) sorted[n - 1] * tick_ns,
           (double) p99 * tick_ns);
    printf("IRQ_CLK  gptmr_hz_cfg=%lu gptmr_hz_derived=%.0f period_cycles=%.1f (CMP=%lu ticks)\r\n",
           (unsigned long) g_gptmr_hz, derived_hz, period_cyc, (unsigned long) CMP_TICKS);
    printf("IRQ_ISR  mode=%-8s isr_active_cycles=%.1f isr_active_ns=%.0f\r\n",
           mode, active_cyc, active_cyc * 1e9 / (double) g_cpu_hz);

    /* 直方图：min..max 分 16 档 */
    uint32_t lo = sorted[0];
    uint32_t hi = sorted[n - 1];
    uint32_t nb = 16;
    uint32_t step = (hi > lo) ? ((hi - lo + nb - 1) / nb) : 1;
    uint32_t hist[16] = { 0 };
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (sorted[i] - lo) / step;
        if (idx >= nb) {
            idx = nb - 1;
        }
        hist[idx]++;
    }
    printf("IRQ_HIST mode=%s lo=%lu step=%lu\r\n", mode, (unsigned long) lo, (unsigned long) step);
    for (uint32_t i = 0; i < nb; i++) {
        uint32_t bar = (uint32_t) ((uint64_t) hist[i] * 60u / n);
        printf("IRQ_BIN mode=%s bin=%lu from=%lu to=%lu count=%lu |",
               mode, (unsigned long) i, (unsigned long) (lo + i * step),
               (unsigned long) (lo + (i + 1) * step - 1), (unsigned long) hist[i]);
        for (uint32_t k = 0; k < bar; k++) {
            printf("#");
        }
        printf("\r\n");
    }
}

static void run_irq(const char *mode, uint8_t ilm)
{
    g_isr_mode = ilm;
    g_ctx.buf = g_samp;
    g_ctx.n = 0;
    g_ctx.nmax = N_SAMPLES;
    g_ctx.prev_cyc = 0;
    g_ctx.period_cyc_sum = 0;
    g_ctx.active_cyc_sum = 0;
    /* 先重开计数器，保证 CNT 从 0 开始 */
    gptmr_channel_reset_count(HPM_GPTMR0, 0);
    while (g_ctx.n < g_ctx.nmax) {
        /* 等采样完成，期间可以喂 shell */
    }
    report_stats(mode, g_samp, g_ctx.n, g_ctx.period_cyc_sum, g_ctx.active_cyc_sum);
}

/* ---------- GPIO 中断（PA02 自环回） ---------- */
SDK_DECLARE_EXT_ISR_M(IRQn_GPIO0_A, isr_gpio_a)

void isr_gpio_a(void)
{
    uint32_t cyc = (uint32_t) hpm_csr_get_core_mcycle();
    if (g_gpio_n < (N_SAMPLES / 5)) {
        g_gpio_stamp_cyc = cyc;
        g_gpio_n++;
    }
    gpio_clear_pin_interrupt_flag(HPM_GPIO0, LED_PORT, LED_PIN);
}

static void run_gpio(void)
{
    const uint32_t n_want = N_SAMPLES / 5;
    g_gpio_n = 0;

    board_init_led_pins();
    gpio_set_pin_output(HPM_GPIO0, LED_PORT, LED_PIN);
    gpio_config_pin_interrupt(HPM_GPIO0, LED_PORT, LED_PIN, gpio_interrupt_trigger_edge_both);
    gpio_enable_pin_interrupt(HPM_GPIO0, LED_PORT, LED_PIN);
    intc_m_enable_irq_with_priority(IRQn_GPIO0_A, 2);

    for (uint32_t i = 0; i < n_want; i++) {
        uint32_t t0 = (uint32_t) hpm_csr_get_core_mcycle();
        if (i & 1u) {
            HPM_GPIO0->DO[LED_PORT].SET = 1u << LED_PIN;
        } else {
            HPM_GPIO0->DO[LED_PORT].CLEAR = 1u << LED_PIN;
        }
        /* 等中断（超时 ~200us 放弃） */
        uint32_t spins = 0;
        while (g_gpio_n <= i) {
            if (hpm_csr_get_core_mcycle() - t0 > 200000u) {
                spins = 1;
                break;
            }
        }
        if (spins) {
            printf("GPIO_LOOPBACK result=FAIL n=%lu (写 DO 后 DI 无变化：同一 pad 自环回不成立)\r\n",
                   (unsigned long) i);
            printf("GPIO_TODO 需外部飞线（PA02 -> 任一 GPIO 输入）后重测\r\n");
            return;
        }
        g_gpio_samp[i] = (uint16_t) (g_gpio_stamp_cyc - t0);
    }

    uint64_t sum = 0;
    uint32_t mn = g_gpio_samp[0], mx = g_gpio_samp[0];
    for (uint32_t i = 0; i < n_want; i++) {
        sum += g_gpio_samp[i];
        if (g_gpio_samp[i] < mn) mn = g_gpio_samp[i];
        if (g_gpio_samp[i] > mx) mx = g_gpio_samp[i];
    }
    printf("GPIO_STAT n=%lu min=%lu avg=%.1f max=%lu min_ns=%.0f avg_ns=%.0f max_ns=%.0f\r\n",
           (unsigned long) n_want, (unsigned long) mn, (double) sum / (double) n_want, (unsigned long) mx,
           (double) mn * 1e9 / (double) g_gpio_cpu_hz,
           (double) sum / (double) n_want * 1e9 / (double) g_gpio_cpu_hz,
           (double) mx * 1e9 / (double) g_gpio_cpu_hz);
}

/* ---------- shell ---------- */
static void cmd_irq(int argc, char *argv[])
{
    (void) argc; (void) argv;
    run_irq("xip", 0);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 irq, cmd_irq, measure GPTMR IRQ latency (ISR in XIP flash));

static void cmd_irqilm(int argc, char *argv[])
{
    (void) argc; (void) argv;
    run_irq("ilm", 1);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 irqilm, cmd_irqilm, measure GPTMR IRQ latency (ISR body in ILM));

static void cmd_irqnc(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("disabling I-Cache, ISR in XIP flash ...\r\n");
    g_isr_mode = 0;
    l1c_ic_disable();
    g_ctx.buf = g_samp;
    g_ctx.n = 0;
    g_ctx.nmax = N_SAMPLES_NC;
    g_ctx.prev_cyc = 0;
    g_ctx.period_cyc_sum = 0;
    gptmr_channel_reset_count(HPM_GPTMR0, 0);
    uint32_t guard = 0;
    while (g_ctx.n < g_ctx.nmax && guard++ < 0x40000000u) {
    }
    report_stats("xip_nocache", g_samp, g_ctx.n, g_ctx.period_cyc_sum, g_ctx.active_cyc_sum);
    l1c_ic_enable();
    printf("I-Cache re-enabled\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 irqnc, cmd_irqnc, measure GPTMR IRQ latency with I-Cache disabled);

static void cmd_irqgpio(int argc, char *argv[])
{
    (void) argc; (void) argv;
    run_gpio();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 irqgpio, cmd_irqgpio, measure GPIO IRQ latency via PA02 self-loopback);

static void cmd_irqinfo(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("cpu        : %lu Hz\r\n", (unsigned long) g_cpu_hz);
    printf("gptmr      : %lu Hz (src pll0_clk1=%lu Hz / %lu), CMP=%lu ticks\r\n",
           (unsigned long) g_gptmr_hz, (unsigned long) GPTMR_SRC_HZ, (unsigned long) GPTMR_DIV,
           (unsigned long) CMP_TICKS);
    printf("gptmr cmp reg readback: %lu\r\n", (unsigned long) g_ctx.cmp_reg);
    printf("isr gptmr0 addr: %p  isr_ilm body addr: %p\r\n",
           (void *) isr_gptmr0, (void *) isr_measure_body_ilm);
    printf("vector table  : %p (ILM: 0x0..0x20000)\r\n", (void *) &g_ctx);
    printf("icache on     : %d\r\n", (int) l1c_ic_is_enabled());
    printf("opt           : %s\r\n", BENCH_OPT_STR);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 irqinfo, cmd_irqinfo, show IRQ latency environment);

int main(void)
{
    bsp_init();
    g_cpu_hz = clock_get_frequency(clock_cpu0);
    g_gpio_cpu_hz = g_cpu_hz;
    printf("\r\n=== bench_irq_latency (hpm5e31_1) ===\r\n");
    printf("commands: irq | irqilm | irqnc | irqgpio | irqinfo\r\n");
    printf("method: GPTMR self-timed (latency_ticks = CNT_in_ISR - CMP), mcycle cross-check\r\n");

    /* 上电后等 2s：hwrun 的串口抓取在烧写后 0.3s 才启动，先等它接上 */
    board_delay_ms(2000);

    gptmr_setup();
    printf("gptmr_hz=%lu CMP=%lu ticks  isr_addr=%p ilm_body=%p\r\n",
           (unsigned long) g_gptmr_hz, (unsigned long) CMP_TICKS,
           (void *) isr_gptmr0, (void *) isr_measure_body_ilm);

    run_irq("xip", 0);
    run_irq("ilm", 1);
    run_gpio();
    cmd_irqnc(0, NULL);
    printf("IRQ_SWEEP_DONE\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
