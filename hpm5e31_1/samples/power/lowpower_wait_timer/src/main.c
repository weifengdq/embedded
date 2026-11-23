/*
 * lowpower_wait_timer - CPU WAIT(WFI) 模式 + GPTMR 定时唤醒, 实测唤醒延迟
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1), 无外部接线
 *   GPTMR1 通道 0 每 100 ms 中断一次, 主循环执行 __WFI() 进入 WAIT 模式,
 *   用 mchtmr(24MHz) 记录 "进 WFI -> 唤醒返回" 的延迟, 统计 min/avg/max;
 *   同时用相邻两次唤醒的时间戳验证 GPTMR 在 WAIT 模式下仍在计数(周期准确性)。
 *
 * 现象: 见 README.md
 */
#include "bsp.h"
#include "hpm_gptmr_drv.h"
#include "hpm_csr_drv.h"
#include "hpm_mchtmr_drv.h"
#include "hpm_sysctl_drv.h"

#define APP_WAKE_TIMER      HPM_GPTMR1
#define APP_WAKE_TIMER_IRQ  IRQn_GPTMR1
#define APP_WAKE_TIMER_CLK  clock_gptmr1
#define APP_WAKE_TIMER_CH   (0U)
#define APP_WAKE_PERIOD_MS  (100U)

static volatile uint32_t s_wake_cnt;
static volatile uint32_t s_last_wake_tick;
static uint32_t s_tmr_hz;
static uint32_t s_cpu_hz;

SDK_DECLARE_EXT_ISR_M(APP_WAKE_TIMER_IRQ, isr_wake_timer)
void isr_wake_timer(void)
{
    if (gptmr_check_status(APP_WAKE_TIMER, GPTMR_CH_RLD_STAT_MASK(APP_WAKE_TIMER_CH))) {
        gptmr_clear_status(APP_WAKE_TIMER, GPTMR_CH_RLD_STAT_MASK(APP_WAKE_TIMER_CH));
        s_last_wake_tick = mchtmr_get_count(HPM_MCHTMR);
        s_wake_cnt++;
    }
}

static void wake_timer_setup(void)
{
    gptmr_channel_config_t cfg;
    clock_add_to_group(APP_WAKE_TIMER_CLK, 0);
    uint32_t freq = clock_get_frequency(APP_WAKE_TIMER_CLK);

    gptmr_channel_get_default_config(APP_WAKE_TIMER, &cfg);
    cfg.reload = freq / 1000U * APP_WAKE_PERIOD_MS;
    gptmr_channel_config(APP_WAKE_TIMER, APP_WAKE_TIMER_CH, &cfg, false);
    gptmr_enable_irq(APP_WAKE_TIMER, GPTMR_CH_RLD_IRQ_MASK(APP_WAKE_TIMER_CH));
    gptmr_start_counter(APP_WAKE_TIMER, APP_WAKE_TIMER_CH);
    intc_m_enable_irq_with_priority(APP_WAKE_TIMER_IRQ, 1);
}

static void cmd_lpinfo(void)
{
    printf("WAIT(WFI) + GPTMR%u CH%u wakeup, period=%u ms\r\n",
           (unsigned) (APP_WAKE_TIMER == HPM_GPTMR0 ? 0 : (APP_WAKE_TIMER == HPM_GPTMR1 ? 1 :
                     (APP_WAKE_TIMER == HPM_GPTMR2 ? 2 : 3))),
           (unsigned) APP_WAKE_TIMER_CH, (unsigned) APP_WAKE_PERIOD_MS);
    printf("  gptmr clk = %lu Hz, cpu = %lu Hz, mchtmr = %lu Hz\r\n",
           (unsigned long) clock_get_frequency(APP_WAKE_TIMER_CLK),
           (unsigned long) s_cpu_hz, (unsigned long) s_tmr_hz);
    printf("  wake count=%lu\r\n", (unsigned long) s_wake_cnt);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 lpinfo, cmd_lpinfo, WAIT mode / wake timer summary);

static void cmd_lpwait(int argc, char *argv[])
{
    uint32_t n = 10;
    if (argc >= 2 && (bsp_parse_u32(argv[1], &n) || n < 1 || n > 200)) {
        printf("usage: lpwait [1..200]\r\n");
        return;
    }
    printf("lpwait: %lu 次 WFI, GPTMR%u 每 %u ms 唤醒\r\n", (unsigned long) n,
           (unsigned) (APP_WAKE_TIMER == HPM_GPTMR1 ? 1 : 0), (unsigned) APP_WAKE_PERIOD_MS);

    uint32_t min_ns = 0xFFFFFFFFUL, max_ns = 0;
    uint64_t sum_ns = 0, sum_period_us = 0;
    uint32_t period_cnt = 0;
    uint32_t prev_wake = s_last_wake_tick;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t before = s_wake_cnt;
        uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
        __asm volatile ("wfi");
        uint32_t t1 = mchtmr_get_count(HPM_MCHTMR);
        /* WFI 返回时按定义是在中断被采样的指令之后, 这里测的是 "唤醒->继续执行" 的延迟 */
        if (s_wake_cnt == before) {
            /* 被其它中断唤醒, 不计入统计 */
            continue;
        }
        /* 睡眠时长 = 进 WFI 到硬件中断触发(ISR 里打的时间戳), 单位 ns */
        uint32_t sleep_ns = (uint32_t) (((uint64_t) (s_last_wake_tick - t0) * 1000000000ULL) / s_tmr_hz);
        /* 唤醒延迟 = 中断触发到 CPU 恢复执行(含 ISR 退出 + WFI 返回), 单位 ns */
        uint32_t wake_ns = (uint32_t) (((uint64_t) (t1 - s_last_wake_tick) * 1000000000ULL) / s_tmr_hz);
        (void) sleep_ns;
        if (wake_ns < min_ns) min_ns = wake_ns;
        if (wake_ns > max_ns) max_ns = wake_ns;
        sum_ns += wake_ns;
        if (i > 0) {
            uint32_t period_us = (uint32_t) (((uint64_t) (s_last_wake_tick - prev_wake) * 1000000ULL) / s_tmr_hz);
            sum_period_us += period_us;
            period_cnt++;
        }
        prev_wake = s_last_wake_tick;
    }
    if (n == 0) {
        printf("no wake event\r\n");
        return;
    }
    printf("  唤醒延迟(中断触发->CPU 恢复): min=%lu ns, avg=%lu ns, max=%lu ns (mchtmr %lu Hz, 1 tick=%.1f ns)\r\n",
           (unsigned long) min_ns, (unsigned long) (sum_ns / n), (unsigned long) max_ns,
           (unsigned long) s_tmr_hz, 1e9 / (double) s_tmr_hz);
    if (period_cnt) {
        printf("  唤醒周期: avg=%lu us (配置 %u ms, n=%lu)\r\n",
               (unsigned long) (sum_period_us / period_cnt), (unsigned) APP_WAKE_PERIOD_MS,
               (unsigned long) period_cnt);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 lpwait, cmd_lpwait, enter WFI N times and measure wake latency: lpwait [n]);

int main(void)
{
    bsp_init();
    s_tmr_hz = clock_get_frequency(clock_mchtmr0);
    s_cpu_hz = clock_get_frequency(clock_cpu0);

    printf("\r\n=== lowpower_wait_timer: WFI + GPTMR wakeup ===\r\n");

    /* 让 mchtmr 在 WFI 期间继续计数(否则唤醒后计时不准) */
    board_ungate_mchtmr_at_lp_mode();
    wake_timer_setup();

    cmd_lpinfo();
    printf("shell: lpinfo | lpwait [n]\r\n");

    uint32_t last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if (bsp_uptime_ms() - last >= 3000U) {
            last = bsp_uptime_ms();
            printf("uptime=%lu ms, wakes=%lu\r\n", (unsigned long) last, (unsigned long) s_wake_cnt);
        }
    }
    return 0;
}
