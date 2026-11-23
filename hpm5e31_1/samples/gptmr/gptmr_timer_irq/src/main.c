/*
 * gptmr_timer_irq —— GPTMR 周期中断 + 中断抖动统计   (书中第 15 章)
 *
 * 结构(全部片内, 不需要任何外部接线):
 *   GPTMR0 CH0 : 周期中断通道, reload = f_gptmr / rate
 *   GPTMR0 CH3 : 32 位自由运行时基(不产生比较事件), 中断里读它的 CNT 当时间戳
 *
 * 统计量 = 相邻两次中断读到的时基差(单位: GPTMR 时钟 tick), 换算成 ns。
 * 这是"软件可见的中断到达时刻抖动", 包含 NVIC 响应延迟 + ISR 入口开销,
 * 与示波器上量到的"定时器硬件输出抖动"不是同一个量, 详见第 15 章坑 15-2。
 *
 * 串口命令:
 *   gstat        打印统计
 *   grate <hz>   修改中断频率(100~200000)
 *   gclr         清零统计
 *   ginfo        打印时钟与配置
 */
#include "bsp.h"
#include "board_timer.h"
#include "clock.h"
#include "hpm_gptmr_drv.h"
#include "hpm_interrupt.h"

#define APP_GPTMR     BOARD_TIMER_GPTMR0
#define APP_GPTMR_IRQ BOARD_TIMER_GPTMR0_IRQ
#define APP_GPTMR_CLK BOARD_TIMER_GPTMR0_CLK
#define APP_IRQ_CH    (0U)   /* 周期中断通道 */
#define APP_FREE_CH   (3U)   /* 自由运行时基 */

static uint32_t s_clk_hz;
static volatile uint32_t s_rate_hz = 1000U;
static volatile uint32_t s_cnt;              /* 已采样中断次数 */
static volatile uint32_t s_last_tick;
static volatile uint32_t s_min_dt = 0xFFFFFFFFUL;
static volatile uint32_t s_max_dt;
static volatile uint64_t s_sum_dt;           /* 单位: tick */
static volatile uint64_t s_sum_sq;           /* 单位: tick^2 */
static volatile uint32_t s_ovf;              /* 时基回绕次数(近似) */

/* 一次中断里只做最少的读 + 累加, 避免把 ISR 自身开销算进抖动 */
SDK_DECLARE_EXT_ISR_M(APP_GPTMR_IRQ, gptmr_isr)
void gptmr_isr(void)
{
    if (gptmr_check_status(APP_GPTMR, GPTMR_CH_RLD_STAT_MASK(APP_IRQ_CH))) {
        uint32_t now, dt;

        gptmr_clear_status(APP_GPTMR, GPTMR_CH_RLD_STAT_MASK(APP_IRQ_CH));
        now = gptmr_channel_get_counter(APP_GPTMR, APP_FREE_CH, gptmr_counter_type_normal);
        dt = now - s_last_tick;
        s_last_tick = now;
        if (s_cnt != 0U) {
            if (dt < s_min_dt) {
                s_min_dt = dt;
            }
            if (dt > s_max_dt) {
                s_max_dt = dt;
            }
            s_sum_dt += dt;
            s_sum_sq += (uint64_t) dt * (uint64_t) dt;
        }
        s_cnt++;
    }
}

static void tmr_setup(uint32_t rate_hz)
{
    gptmr_channel_config_t cfg;
    uint32_t irq_key;

    init_gptmr0_clock();
    s_clk_hz = clock_get_frequency(APP_GPTMR_CLK);
    if (s_clk_hz == 0U) {
        printf("!! clock_gptmr0 = 0, board clock init failed\r\n");
        return;
    }
    if (rate_hz < 100U) {
        rate_hz = 100U;
    }
    if ((s_clk_hz / rate_hz) < 32U) {
        rate_hz = s_clk_hz / 32U;
    }

    irq_key = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    s_cnt = 0U;
    s_last_tick = 0U;
    s_min_dt = 0xFFFFFFFFUL;
    s_max_dt = 0U;
    s_sum_dt = 0U;
    s_sum_sq = 0U;
    restore_global_irq(irq_key);

    /* CH3: 自由运行时基 */
    gptmr_stop_counter(APP_GPTMR, APP_FREE_CH);
    gptmr_channel_get_default_config(APP_GPTMR, &cfg);
    cfg.mode = gptmr_work_mode_no_capture;
    cfg.reload = 0xFFFFFFFFUL;
    cfg.cmp[0] = 0xFFFFFFFFUL;
    cfg.cmp[1] = 0xFFFFFFFFUL;
    cfg.enable_cmp_output = false;
    gptmr_channel_config(APP_GPTMR, APP_FREE_CH, &cfg, false);
    gptmr_channel_reset_count(APP_GPTMR, APP_FREE_CH);
    gptmr_start_counter(APP_GPTMR, APP_FREE_CH);

    /* CH0: 周期中断 */
    gptmr_stop_counter(APP_GPTMR, APP_IRQ_CH);
    gptmr_disable_irq(APP_GPTMR, GPTMR_CH_RLD_IRQ_MASK(APP_IRQ_CH));
    gptmr_channel_get_default_config(APP_GPTMR, &cfg);
    cfg.mode = gptmr_work_mode_no_capture;
    cfg.reload = s_clk_hz / rate_hz;
    cfg.cmp[0] = 0xFFFFFFFFUL;
    cfg.cmp[1] = 0xFFFFFFFFUL;
    cfg.enable_cmp_output = false;
    gptmr_channel_config(APP_GPTMR, APP_IRQ_CH, &cfg, false);
    gptmr_channel_reset_count(APP_GPTMR, APP_IRQ_CH);
    s_rate_hz = rate_hz;
    gptmr_enable_irq(APP_GPTMR, GPTMR_CH_RLD_IRQ_MASK(APP_IRQ_CH));
    gptmr_start_counter(APP_GPTMR, APP_IRQ_CH);
    intc_m_enable_irq_with_priority(APP_GPTMR_IRQ, 1);
}


/* 不链接 libm: 自己用牛顿迭代算平方根, 避免 -lm 依赖 */
static double dsqrt(double x)
{
    double r;
    int i;
    if (x <= 0.0) {
        return 0.0;
    }
    r = (x > 1.0) ? x : 1.0;
    for (i = 0; i < 40; i++) {
        r = 0.5 * (r + x / r);
    }
    return r;
}

static void print_stats(void)
{
    uint32_t cnt, mn, mx;
    uint64_t sum, sq;
    uint32_t irq_key;

    irq_key = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    cnt = s_cnt;
    mn = s_min_dt;
    mx = s_max_dt;
    sum = s_sum_dt;
    sq = s_sum_sq;
    restore_global_irq(irq_key);

    if (cnt < 2U) {
        printf("gptmr irq: %lu Hz, only %lu irq so far (wait a moment)\r\n",
               (unsigned long) s_rate_hz, (unsigned long) cnt);
        return;
    }
    /* 均值/标准差用 tick 计算后再换算到 ns, 1 tick = 1e9/f_clk ns */
    double n = (double) (cnt - 1U);
    double mean_tick = (double) sum / n;
    double var_tick = (double) sq / n - mean_tick * mean_tick;
    double std_tick = (var_tick > 0.0) ? dsqrt(var_tick) : 0.0;
    double ns_per_tick = 1e9 / (double) s_clk_hz;

    printf("[gptmr irq] f_clk=%lu Hz rate=%lu Hz cnt=%lu\r\n",
           (unsigned long) s_clk_hz, (unsigned long) s_rate_hz, (unsigned long) (cnt - 1U));
    printf("  period : mean=%.3f us  min=%.3f us  max=%.3f us  (expect %.3f us)\r\n",
           mean_tick * ns_per_tick / 1000.0, mn * ns_per_tick / 1000.0,
           mx * ns_per_tick / 1000.0, 1e6 / (double) s_rate_hz);
    printf("  jitter : peak-peak=%.3f us  stddev=%.3f us  (=%.1f ticks)\r\n",
           (mx - mn) * ns_per_tick / 1000.0, std_tick * ns_per_tick / 1000.0, std_tick);
    printf("  error  : %+.2f ppm\r\n",
           (mean_tick * ns_per_tick * 1e-3 - 1e6 / (double) s_rate_hz) / (1e6 / (double) s_rate_hz) * 1e6);
}

static void cmd_gstat(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    print_stats();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gstat, cmd_gstat, print gptmr irq period/jitter statistics);

static void cmd_grate(int argc, char *argv[])
{
    uint32_t hz = 0;

    if ((argc < 2) || (bsp_parse_u32(argv[1], &hz) != 0) || (hz == 0U)) {
        printf("usage: grate <hz>   e.g. grate 1000\r\n");
        return;
    }
    tmr_setup(hz);
    printf("rate -> %lu Hz (reload=%lu)\r\n", (unsigned long) s_rate_hz,
           (unsigned long) (s_clk_hz / s_rate_hz));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 grate, cmd_grate, set gptmr irq rate: grate <hz>);

static void cmd_gclr(int argc, char *argv[])
{
    uint32_t irq_key;

    (void) argc;
    (void) argv;
    irq_key = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    s_cnt = 0U;
    s_min_dt = 0xFFFFFFFFUL;
    s_max_dt = 0U;
    s_sum_dt = 0U;
    s_sum_sq = 0U;
    s_last_tick = gptmr_channel_get_counter(APP_GPTMR, APP_FREE_CH, gptmr_counter_type_normal);
    restore_global_irq(irq_key);
    printf("stats cleared\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gclr, cmd_gclr, clear gptmr irq statistics);

static void cmd_ginfo(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("gptmr0: base=0x%08lx clk=%lu Hz\r\n", (unsigned long) HPM_GPTMR0_BASE,
           (unsigned long) s_clk_hz);
    printf("  CH%u rx reload=%lu, CH%u free-run\r\n", APP_IRQ_CH,
           (unsigned long) (s_clk_hz / s_rate_hz), APP_FREE_CH);
    printf("  CMP count per channel = %u, reload/cmp 位宽见第 15 章表 15-2\r\n", GPTMR_CH_CMP_COUNT);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ginfo, cmd_ginfo, print gptmr clock/config info);

int main(void)
{
    uint32_t last;

    bsp_init();
    board_init_led_pins();
    printf("\r\n=== gptmr_timer_irq (board %s, ext %s) ===\r\n", BOARD_NAME,
           board_timer_ext_version());
    tmr_setup(1000U);
    printf("gptmr0 clock = %lu Hz, irq rate = %lu Hz, reload = %lu\r\n",
           (unsigned long) s_clk_hz, (unsigned long) s_rate_hz,
           (unsigned long) (s_clk_hz / s_rate_hz));
    printf("commands: gstat | grate <hz> | gclr | ginfo\r\n");

    last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if ((bsp_uptime_ms() - last) >= 3000U) {
            last = bsp_uptime_ms();
            print_stats();
            board_led_toggle();
        }
    }
    return 0;
}
