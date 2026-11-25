/*
 * gptmr_capture_measure —— GPTMR 输入捕获测量频率/占空比/抖动   (书中第 15 章)
 *
 * 被测信号源(片内, 无需飞线):
 *   GPTMR0 CH3 输出比较产生方波, 默认 1 kHz / 25%(高 250 us, 低 750 us)
 *     -> TRGM: GPTMR0_OUT2 -> GPTMR0_CAPT_3
 *     -> GPTMR0 CH3 输入捕获
 *   CH3 用 measure_width 模式: 硬件自动填 CAPPRD(周期) 与 CAPDTY(高电平时间),
 *   主循环读取并做统计, 因此可以得到 频率/占空比 的均值、极值、抖动。
 *
 * 为什么被测源用 GPTMR 输出而不是 PWM 的 TRGO: TRGO 是 5 ns 窄脉冲,
 * GPTMR 100 MHz 采样会漏采(第 14 章易错点 14-3)。GPTMR 输出是 500 us 宽脉冲, 100% 可靠。
 *
 * 想测外部信号: 把信号接到 PC00(TRGM_P_00), 调用 board_timer_init_trgm_gptmr_p00_to_cap(),
 * 再把 capture_src_select(1) 切到外部引脚。
 *
 * 串口命令:
 *   cstat                  打印实测频率/占空比/抖动
 *   crate <hz> <permille>  改被测方波频率与占空比
 *   csrc <0|1>             0=内部 GPTMR 输出(默认), 1=外部 PC00 引脚
 */
#include "bsp.h"
#include "board_timer.h"
#include "hpm_gptmr_drv.h"
#include "hpm_interrupt.h"
#include "clock.h"

#define GPTMR      BOARD_TIMER_GPTMR0
#define SRC_CH     (3U)    /* 信号源: 输出比较(OUT3, 本板 CAPT_3 路由异常, 见易错点) */
#define CAP_CH     (2U)    /* 测量: 输入捕获(CAPT_2, 实测可用) */

static uint32_t s_clk_hz;
static uint32_t s_src_hz = 1000U;
static uint32_t s_src_pm = 250U;
static uint8_t  s_src_ext;

static volatile uint32_t s_cap_n;
static volatile uint32_t s_min_p = 0xFFFFFFFFUL, s_max_p;
static volatile uint64_t s_sum_p, s_sum_high;
static volatile uint32_t s_min_h = 0xFFFFFFFFUL, s_max_h;

SDK_DECLARE_EXT_ISR_M(BOARD_TIMER_GPTMR0_IRQ, cap_isr)
void cap_isr(void)
{
    if (gptmr_check_status(GPTMR, GPTMR_CH_CAP_STAT_MASK(CAP_CH))) {
        uint32_t prd, hgh;

        gptmr_clear_status(GPTMR, GPTMR_CH_CAP_STAT_MASK(CAP_CH));
        prd = gptmr_channel_get_counter(GPTMR, CAP_CH, gptmr_counter_type_measured_period);
        hgh = gptmr_channel_get_counter(GPTMR, CAP_CH, gptmr_counter_type_measured_duty_cycle);
        if (prd != 0U) {
            if (prd < s_min_p) { s_min_p = prd; }
            if (prd > s_max_p) { s_max_p = prd; }
            if (hgh < s_min_h) { s_min_h = hgh; }
            if (hgh > s_max_h) { s_max_h = hgh; }
            s_sum_p += prd;
            s_sum_high += hgh;
            s_cap_n++;
        }
    }
}

/* 配置被测方波: 周期 T = f/rate, 高电平 = T * permille/1000 */
static void src_config(uint32_t hz, uint32_t pm)
{
    gptmr_channel_config_t cfg;

    if (hz == 0U) { hz = 1U; }
    if (pm > 999U) { pm = 999U; }

    gptmr_channel_get_default_config(GPTMR, &cfg);
    cfg.mode = gptmr_work_mode_no_capture;
    cfg.reload = s_clk_hz / hz;
    cfg.cmp[0] = (uint32_t) ((uint64_t) cfg.reload * pm / 1000ULL);
    cfg.cmp[1] = 0xFFFFFFFFUL;
    cfg.enable_cmp_output = true;   /* 关键: 使能比较输出, TRGM 才能看到 OUT2 */
    cfg.cmp_initial_polarity_high = false;
    (void) gptmr_channel_config(GPTMR, SRC_CH, &cfg, false);
    gptmr_channel_enable(GPTMR, SRC_CH, true);
    gptmr_channel_reset_count(GPTMR, SRC_CH);
    gptmr_start_counter(GPTMR, SRC_CH);
    s_src_hz = hz;
    s_src_pm = pm;
}

static void cap_config(void)
{
    gptmr_channel_config_t cfg;

    gptmr_channel_get_default_config(GPTMR, &cfg);
    cfg.mode = gptmr_work_mode_measure_width;      /* 硬件测周期与高电平 */
    cfg.reload = 0xFFFFFFFFUL;
    cfg.cmp[0] = 0xFFFFFFFFUL;
    cfg.cmp[1] = 0xFFFFFFFFUL;
    cfg.enable_cmp_output = false;
    (void) gptmr_channel_config(GPTMR, CAP_CH, &cfg, false);
    gptmr_channel_reset_count(GPTMR, CAP_CH);
    gptmr_start_counter(GPTMR, CAP_CH);
    gptmr_enable_irq(GPTMR, GPTMR_CH_CAP_IRQ_MASK(CAP_CH));
    intc_m_enable_irq_with_priority(BOARD_TIMER_GPTMR0_IRQ, 1);
}

static void print_stat(void)
{
    uint32_t n, mnp, mxp, mnh, mxh;
    uint64_t sp, sh;
    uint32_t key;
    double ns, prd_us, high_us;

    key = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    n = s_cap_n; mnp = s_min_p; mxp = s_max_p; mnh = s_min_h; mxh = s_max_h;
    sp = s_sum_p; sh = s_sum_high;
    s_cap_n = 0U; s_min_p = 0xFFFFFFFFUL; s_max_p = 0U;
    s_min_h = 0xFFFFFFFFUL; s_max_h = 0U; s_sum_p = 0U; s_sum_high = 0U;
    restore_global_irq(key);

    if (n == 0U) {
        printf("capture: 还没有捕获事件(检查 TRGM 路由 / 信号源是否在跑)\r\n");
        return;
    }
    ns = 1e9 / (double) s_clk_hz;
    prd_us = (double) sp / (double) n * ns / 1000.0;
    high_us = (double) sh / (double) n * ns / 1000.0;
    printf("[cap] n=%lu  period=%.3f us (%.1f Hz)  high=%.3f us  duty=%.2f%%\r\n",
           (unsigned long) n, prd_us, 1e6 / prd_us, high_us,
           (prd_us > 0.0) ? (high_us / prd_us * 100.0) : 0.0);
    printf("      jitter: period pp=%.3f us (min %.3f / max %.3f)  high pp=%.3f us\r\n",
           (double) (mxp - mnp) * ns / 1000.0, (double) mnp * ns / 1000.0,
           (double) mxp * ns / 1000.0, (double) (mxh - mnh) * ns / 1000.0);
    printf("      target: %lu Hz / %lu permille -> freq err %+.1f ppm, duty err %+.3f%%  src=%s\r\n",
           (unsigned long) s_src_hz, (unsigned long) s_src_pm,
           (1e6 / prd_us - (double) s_src_hz) / (double) s_src_hz * 1e6,
           (prd_us > 0.0) ? (high_us / prd_us * 100.0 - (double) s_src_pm / 10.0) : 0.0,
           s_src_ext ? "ext PC00" : "int GPTMR_OUT2");
}

static void cmd_cstat(int argc, char *argv[])
{
    (void) argc; (void) argv;
    print_stat();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 cstat, cmd_cstat, print captured frequency/duty/jitter);

static void cmd_crate(int argc, char *argv[])
{
    uint32_t hz = 0, pm = 0;

    if ((argc < 3) || (bsp_parse_u32(argv[1], &hz) != 0) || (bsp_parse_u32(argv[2], &pm) != 0)) {
        printf("usage: crate <hz> <permille>   e.g. crate 2000 500\r\n");
        return;
    }
    src_config(hz, pm);
    print_stat();
    printf("source now %lu Hz / %lu permille\r\n", (unsigned long) s_src_hz,
           (unsigned long) s_src_pm);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 crate, cmd_crate, set measured signal freq/duty: crate <hz> <permille>);

static void cmd_csrc(int argc, char *argv[])
{
    uint32_t v = 0;

    if ((argc < 2) || (bsp_parse_u32(argv[1], &v) != 0)) {
        printf("usage: csrc <0|1>  0=int GPTMR OUT2, 1=ext PC00\r\n");
        return;
    }
    s_src_ext = (uint8_t) (v != 0U);
    if (s_src_ext) {
        board_timer_init_trgm_gptmr_p00_to_cap();   /* PC00 -> CAP3 */
        printf("capture source = external PC00 (TRGM_P_00)\r\n");
    } else {
        trgm_output_t cfg = {0};
        cfg.invert = false;
        cfg.type = trgm_output_same_as_input;
        cfg.input = HPM_TRGM0_INPUT_SRC_GPTMR0_OUT3;
        trgm_output_config(BOARD_TIMER_TRGM, BOARD_TIMER_GPTMR0_CAPT_2, &cfg);
        printf("capture source = internal GPTMR0_OUT3\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 csrc, cmd_csrc, select capture source: csrc <0|1>);

int main(void)
{
    trgm_output_t cfg = {0};
    uint32_t last;

    bsp_init();
    board_init_led_pins();
    init_gptmr0_clock();
    s_clk_hz = clock_get_frequency(BOARD_TIMER_GPTMR0_CLK);

    printf("\r\n=== gptmr_capture_measure (board %s, ext %s) ===\r\n", BOARD_NAME,
           board_timer_ext_version());
    printf("src: GPTMR0 CH3 output-compare %lu Hz / %lu permille -> TRGM -> CH3 capture(measure_width)\r\n",
           (unsigned long) s_src_hz, (unsigned long) s_src_pm);

    cfg.invert = false;
    cfg.type = trgm_output_same_as_input;
    cfg.input = HPM_TRGM0_INPUT_SRC_GPTMR0_OUT3;
    trgm_output_config(BOARD_TIMER_TRGM, BOARD_TIMER_GPTMR0_CAPT_2, &cfg);

    src_config(s_src_hz, s_src_pm);
    cap_config();
    printf("gptmr clk = %lu Hz (1 tick = %.2f ns)\r\n", (unsigned long) s_clk_hz, 1e9 / (double) s_clk_hz);
    printf("commands: cstat | crate <hz> <permille> | csrc <0|1>\r\n");

    last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if ((bsp_uptime_ms() - last) >= 2000U) {
            last = bsp_uptime_ms();
            print_stat();
            board_led_toggle();
        }
    }
    return 0;
}