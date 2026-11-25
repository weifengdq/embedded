/*
 * pwm_basic —— PWMv2 频率/占空比/互补输出 + 无示波器时序实测   (书中第 14 章)
 *
 * 波形: PWM0 子模块 3(计数器 3) -> 输出 6(PD06) / 输出 7(PD07) 互补
 *   输出 6/7 固定对应计数器 3 与比较值 12/13/14/15  (RM §37.4.8, p.613)
 *   a = CMP12(影子 12) = 上升沿位置,  b = CMP13(影子 13) = 下降沿位置
 *   高电平区间 [a, b)  (RM §37.4.5, p.610)
 *   周期 T = 计数器 3 重载值 = 影子 0 = f_pwm_clk / f_pwm
 *   PAIR_MODE=1: 输出 7 是输出 6 的互补
 *
 * 实测方法(不需要示波器/飞线):
 *   GPTMR0 通道 3 做 32 位自由运行时基(f_gptmr = 100 MHz, 10 ns 分辨率),
 *   PWM0 的"计数器 3 重载中断"与"CMP13 比较中断"里各读一次时基,
 *   于是: 周期 = 相邻两次重载时间戳之差, 高电平 = CMP13 时刻 - 重载时刻。
 *   平均值准确(量化误差 10 ns), 单次值含中断响应抖动, 这正是抖动统计的来源。
 *
 *   ⚠ 注意: 曾尝试用 PWM 的 TRGO -> TRGM -> GPTMR CAP2/CAP3 做纯硬件捕获,
 *   但 TRGO 是 1 个 PWM 时钟周期宽(200 MHz 下 5 ns)的窄脉冲, 跨到 100 MHz 的
 *   GPTMR 采样会大量漏采(实测 50 kHz 时 2 s 只捕到 3533 个周期, 应为 10 万个),
 *   详见第 14 章易错点 14-3 与 logs/20261030-113* 系列日志。
 *
 * 串口命令:
 *   pwm <freq_hz> <duty_permille>   设置频率与占空比(千分比), 例: pwm 10000 500
 *   pinfo                            打印当前配置
 *   pmeas                            打印实测结果并清零统计
 */
#include "bsp.h"
#include "board_timer.h"
#include "hpm_pwmv2_drv.h"
#include "hpm_trgm_drv.h"
#include "hpm_gptmr_drv.h"
#include "hpm_interrupt.h"
#include "clock.h"

#define APP_PWM       BOARD_TIMER_PWM0
#define APP_PWM_CLK   BOARD_TIMER_PWM0_CLK
#define APP_CNT       pwm_counter_3
#define APP_CH_X      pwm_channel_6
#define APP_CH_Y      pwm_channel_7
#define CMP_A_IDX     (12U)
#define CMP_B_IDX     (13U)
#define SHADOW_RLD    PWMV2_SHADOW_INDEX(0)
#define SHADOW_A      PWMV2_SHADOW_INDEX(12)
#define SHADOW_B      PWMV2_SHADOW_INDEX(13)

#define APP_GPTMR     BOARD_TIMER_GPTMR0
#define TB_CH         (3U)     /* 自由运行时基通道 */

static uint32_t s_pwm_clk_hz;
static uint32_t s_tb_clk_hz;
static uint32_t s_freq_hz = 10000U;
static uint32_t s_duty_pm = 500U;
static uint32_t s_reload;
static uint32_t s_cmp_a;
static uint32_t s_cmp_b;

/* 实测统计(单位: 10 ns 时基 tick) */
static volatile uint32_t s_rld_prev, s_rld_n, s_t_rise;
static volatile uint32_t s_cmp_n;
static volatile uint64_t s_sum_period, s_sum_high;
static volatile uint32_t s_min_period = 0xFFFFFFFFUL, s_max_period;
/* E00038 试验: 在 CMP0/CMP4 中断里读计数器, 判断比较值是否真的更新 */
static volatile uint32_t s_e_val0, s_e_val4, s_e_n0, s_e_n4;
static volatile uint32_t s_min_high = 0xFFFFFFFFUL, s_max_high;

static double dsqrt_local(double x)
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

/* 32 位自由运行时基的当前值(GPTMR0 CH3) */
static inline uint32_t tb_read(void)
{
    return gptmr_channel_get_counter(APP_GPTMR, TB_CH, gptmr_counter_type_normal);
}

/* PWM0 中断: 重载事件给周期, CMP13 事件给下降沿 */
/* PWMv2 的 CNT_VAL 是 24.8 定点(整数在 bit31:8), 读回来先右移 8 位才是 tick */
static uint32_t cnt_ticks(uint32_t raw)
{
    return raw >> 8;
}

SDK_DECLARE_EXT_ISR_M(BOARD_TIMER_PWM0_IRQ, pwm_isr)
void pwm_isr(void)
{
    uint32_t st_r = pwmv2_get_reload_irq_status(APP_PWM);
    uint32_t st_c = pwmv2_get_cmp_irq_status(APP_PWM);
    uint32_t tb;

    if ((st_r & (1UL << (uint32_t) APP_CNT)) != 0U) {
        uint32_t period;

        pwmv2_clear_reload_irq_status(APP_PWM, 1UL << (uint32_t) APP_CNT);
        tb = tb_read();
        if (s_rld_n != 0U) {
            period = tb - s_rld_prev;
            if (period < s_min_period) {
                s_min_period = period;
            }
            if (period > s_max_period) {
                s_max_period = period;
            }
            s_sum_period += period;
        }
        s_rld_prev = tb;
        s_t_rise = tb;          /* a = 0, 上升沿与重载同刻 */
        s_rld_n++;
    }
    if ((st_c & 1UL) != 0U) {           /* CMP0: 记录匹配时的计数器值 */
        pwmv2_clear_cmp_irq_status(APP_PWM, 1UL);
        s_e_val0 = cnt_ticks(pwmv2_get_counter_value(APP_PWM, pwm_counter_0));
        s_e_n0++;
    }
    if ((st_c & (1UL << 4)) != 0U) {     /* CMP4 */
        pwmv2_clear_cmp_irq_status(APP_PWM, 1UL << 4);
        s_e_val4 = cnt_ticks(pwmv2_get_counter_value(APP_PWM, pwm_counter_1));
        s_e_n4++;
    }
    if ((st_c & (1UL << CMP_B_IDX)) != 0U) {
        uint32_t high;

        pwmv2_clear_cmp_irq_status(APP_PWM, 1UL << CMP_B_IDX);
        tb = tb_read();
        high = tb - s_t_rise;
        if (high < s_min_high) {
            s_min_high = high;
        }
        if (high > s_max_high) {
            s_max_high = high;
        }
        s_sum_high += high;
        s_cmp_n++;
    }
}

static void tb_init(void)
{
    gptmr_channel_config_t cfg;

    init_gptmr0_clock();
    s_tb_clk_hz = clock_get_frequency(BOARD_TIMER_GPTMR0_CLK);
    gptmr_stop_counter(APP_GPTMR, TB_CH);
    gptmr_channel_get_default_config(APP_GPTMR, &cfg);
    cfg.mode = gptmr_work_mode_no_capture;
    cfg.reload = 0xFFFFFFFFUL;
    cfg.cmp[0] = 0xFFFFFFFFUL;
    cfg.cmp[1] = 0xFFFFFFFFUL;
    cfg.enable_cmp_output = false;
    (void) gptmr_channel_config(APP_GPTMR, TB_CH, &cfg, false);
    gptmr_channel_reset_count(APP_GPTMR, TB_CH);
    gptmr_start_counter(APP_GPTMR, TB_CH);
}

static void pwm_apply(uint32_t freq_hz, uint32_t duty_pm)
{
    uint32_t reload, a, b;
    pwm_counter_t i;

    if (freq_hz == 0U) {
        freq_hz = 1U;
    }
    reload = s_pwm_clk_hz / freq_hz;
    if (reload < 64U) {
        reload = 64U;
    }
    if (reload > 0xFFFFFFUL) {
        reload = 0xFFFFFFUL;
    }
    a = 0U;
    b = (uint32_t) (((uint64_t) reload * (uint64_t) duty_pm) / 1000ULL);
    if (b > reload) {
        b = reload;
    }

    s_reload = reload;
    s_cmp_a = a;
    s_cmp_b = b;
    s_freq_hz = freq_hz;
    s_duty_pm = duty_pm;

    pwmv2_shadow_register_unlock(APP_PWM);
    pwmv2_set_shadow_val(APP_PWM, SHADOW_RLD, reload, 0, false);
    pwmv2_set_shadow_val(APP_PWM, SHADOW_A, a, 0, false);
    pwmv2_set_shadow_val(APP_PWM, SHADOW_B, b, 0, false);
    /* 4 个计数器重载值必须一致, 否则交叉周期的 CMP 事件会被丢弃 (RM §37.4.8, p.613) */
    for (i = pwm_counter_0; i <= pwm_counter_3; i++) {
        pwmv2_counter_select_data_offset_from_shadow_value(APP_PWM, i, SHADOW_RLD);
        pwmv2_set_reload_update_time(APP_PWM, i, pwm_reload_update_on_shlk);
        pwmv2_counter_burst_disable(APP_PWM, i);
    }
    pwmv2_select_cmp_source(APP_PWM, PWMV2_CMP_INDEX(12), cmp_value_from_shadow_val, SHADOW_A);
    pwmv2_select_cmp_source(APP_PWM, PWMV2_CMP_INDEX(13), cmp_value_from_shadow_val, SHADOW_B);
    pwmv2_shadow_register_lock(APP_PWM);
}

static void pwm_start(void)
{
    pwmv2_deinit(APP_PWM);
    pwm_apply(s_freq_hz, s_duty_pm);

    pwmv2_disable_four_cmp(APP_PWM, APP_CH_X);
    pwmv2_enable_pair_mode(APP_PWM, APP_CH_X);
    pwmv2_channel_enable_output(APP_PWM, APP_CH_X);
    pwmv2_channel_enable_output(APP_PWM, APP_CH_Y);
    pwmv2_enable_counter(APP_PWM, APP_CNT);
    pwmv2_start_pwm_output(APP_PWM, APP_CNT);

    pwmv2_enable_reload_irq(APP_PWM, APP_CNT);
    (void) pwmv2_enable_cmp_irq(APP_PWM, CMP_B_IDX);
    intc_m_enable_irq_with_priority(BOARD_TIMER_PWM0_IRQ, 1);
}

static void print_meas(void)
{
    uint32_t n, nh, mnp, mxp, mnh, mxh;
    uint64_t sp, sh;
    uint32_t key;
    double ns_per_tick, period_ns, high_ns, jit_ns;

    key = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    n = s_rld_n;
    nh = s_cmp_n;
    mnp = s_min_period; mxp = s_max_period;
    mnh = s_min_high; mxh = s_max_high;
    sp = s_sum_period; sh = s_sum_high;
    s_rld_n = 0U; s_cmp_n = 0U;
    s_min_period = 0xFFFFFFFFUL; s_max_period = 0U;
    s_min_high = 0xFFFFFFFFUL; s_max_high = 0U;
    s_sum_period = 0U; s_sum_high = 0U;
    restore_global_irq(key);

    if (n < 3U) {
        printf("measure: only %lu reload events, skip\r\n", (unsigned long) n);
        return;
    }
    ns_per_tick = 1e9 / (double) s_tb_clk_hz;
    period_ns = (double) sp / (double) (n - 1U) * ns_per_tick;
    high_ns = (nh > 0U) ? ((double) sh / (double) nh * ns_per_tick) : 0.0;

    printf("[meas] reload=%lu cmp=%lu  period=%.3f us (%.1f Hz)  high=%.3f us  duty=%.2f%%\r\n",
           (unsigned long) (n - 1U), (unsigned long) nh, period_ns / 1000.0, 1e9 / period_ns,
           high_ns / 1000.0, (period_ns > 0.0) ? (high_ns / period_ns * 100.0) : 0.0);
    printf("       period jitter: min=%.3f max=%.3f pp=%.3f us\r\n",
           (double) mnp * ns_per_tick / 1000.0, (double) mxp * ns_per_tick / 1000.0,
           (double) (mxp - mnp) * ns_per_tick / 1000.0);
    jit_ns = (double) (mxh - mnh) * ns_per_tick;
    printf("       high  jitter: pp=%.3f us (=%.1f tick)\r\n", jit_ns / 1000.0, jit_ns / ns_per_tick);
    printf("       target: %lu Hz / %lu permille -> freq err %+.1f ppm, duty err %+.3f%%\r\n",
           (unsigned long) s_freq_hz, (unsigned long) s_duty_pm,
           (1e9 / period_ns - (double) s_freq_hz) / (double) s_freq_hz * 1e6,
           (period_ns > 0.0) ? (high_ns / period_ns * 100.0 - (double) s_duty_pm / 10.0) : 0.0);
}

static void print_info(void)
{
    printf("PWM0: clk=%lu Hz counter3 reload(T)=%lu cmp a(CMP12)=%lu cmp b(CMP13)=%lu\r\n",
           (unsigned long) s_pwm_clk_hz, (unsigned long) s_reload, (unsigned long) s_cmp_a,
           (unsigned long) s_cmp_b);
    printf("  freq = clk/T = %.3f Hz   duty = (b-a)/T = %.2f%%\r\n",
           (double) s_pwm_clk_hz / (double) s_reload,
           (s_reload > 0U) ? ((double) (s_cmp_b - s_cmp_a) / (double) s_reload * 100.0) : 0.0);
    printf("  pins: P6=PD06  P7=PD07(PAIR_MODE=1 互补)   timebase: GPTMR0 CH%u %lu Hz\r\n",
           TB_CH, (unsigned long) s_tb_clk_hz);
}

static void cmd_pwm(int argc, char *argv[])
{
    uint32_t f = 0, d = 0;

    if ((argc < 3) || (bsp_parse_u32(argv[1], &f) != 0) || (bsp_parse_u32(argv[2], &d) != 0) ||
        (f == 0U) || (d > 1000U)) {
        printf("usage: pwm <freq_hz> <duty_permille 0..1000>\r\n");
        return;
    }
    s_freq_hz = f;
    s_duty_pm = d;
    pwm_apply(f, d);
    print_info();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 pwm, cmd_pwm, set pwm freq and duty: pwm <freq_hz> <duty_permille>);

static void cmd_pinfo(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    print_info();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 pinfo, cmd_pinfo, print pwm configuration);

/* 一次命令跑完频率/占空比扫描, 每个配置测量 1.2 s, 便于一次上板收集整张表 */
static void cmd_psweep(int argc, char *argv[])
{
    static const uint32_t freqs[] = {1000U, 10000U, 20000U, 50000U, 100000U};
    static const uint32_t dutys[] = {250U, 500U, 750U, 100U, 500U};
    uint32_t i;

    (void) argc;
    (void) argv;
    print_meas();                       /* 丢弃旧统计 */
    for (i = 0; i < (sizeof(freqs) / sizeof(freqs[0])); i++) {
        pwm_apply(freqs[i], dutys[i]);
        print_meas();                   /* 清零 */
        board_delay_ms(1200);
        printf("--- sweep %lu: target %lu Hz / %lu permille ---\r\n",
               (unsigned long) i, (unsigned long) freqs[i], (unsigned long) dutys[i]);
        print_meas();
    }
    pwm_apply(10000U, 500U);
    printf("sweep done, back to 10 kHz / 50%%\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 psweep, cmd_psweep, sweep freq/duty and print measured results);

/* 高频段扫描: 100k/200k/500k/1M, 看"软件中断测占空比"从哪个频率开始失效 */
static void cmd_psweep2(int argc, char *argv[])
{
    static const uint32_t freqs[] = {100000U, 200000U, 500000U, 1000000U};
    static const uint32_t dutys[] = {500U, 500U, 500U, 500U};
    uint32_t i;

    (void) argc;
    (void) argv;
    for (i = 0; i < (sizeof(freqs) / sizeof(freqs[0])); i++) {
        pwm_apply(freqs[i], dutys[i]);
        print_meas();
        board_delay_ms(1200);
        printf("--- high sweep: target %lu Hz / %lu permille, T=%lu tick ---\r\n",
               (unsigned long) freqs[i], (unsigned long) dutys[i], (unsigned long) s_reload);
        print_meas();
    }
    pwm_apply(10000U, 500U);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 psweep2, cmd_psweep2, high frequency sweep 100k-1M);

static uint32_t cmp_diff(uint32_t a, uint32_t b)
{
    uint32_t d = (a > b) ? (a - b) : (b - a);
    return (d > 0x80000000UL) ? (0U - d) : d;
}

/*
 * 勘误 E00038 试验: CMP0 与 CMP4 同属 CMP_CFG0 的触发选择组, 用同一个触发输入更新。
 *   源: PWM0 自身的 TRGO_6(CMP15 事件) --TRGM--> PWM0 TRIG_IN_0
 *   CMP0 用计数器 0, CMP4 用计数器 1, 重载值都是 T(交叉周期限制已满足)
 * 判据: CMPn 中断里读计数器 = 匹配位置, 应等于新的比较值(±容差); 不等即"更新丢失/异常"
 * mode: 0=触发输入(硬件), 1=影子锁(软件强制), 作为对照组
 */
static void cmd_perr(int argc, char *argv[])
{
    uint32_t iters = (argc > 1) ? (uint32_t) strtoul(argv[1], NULL, 0) : 100U;
    uint32_t mode = (argc > 2) ? (uint32_t) strtoul(argv[2], NULL, 0) : 0U;
    uint32_t t, tol = 3000U;
    uint32_t bad0 = 0U, bad4 = 0U, miss0 = 0U, miss4 = 0U;
    uint32_t a0 = 0U, a4 = 0U;
    uint32_t v0 = 0U, v4 = 0U, n0 = 0U, n4 = 0U;

    pwm_apply(10000U, 500U);
    t = s_reload;

    pwmv2_shadow_register_unlock(APP_PWM);
    pwmv2_set_shadow_val(APP_PWM, PWMV2_SHADOW_INDEX(15), t / 8U, 0, false);
    pwmv2_select_cmp_source(APP_PWM, PWMV2_CMP_INDEX(15), cmp_value_from_shadow_val, PWMV2_SHADOW_INDEX(15));
    pwmv2_set_trigout_cmp_index(APP_PWM, pwm_channel_6, 15);
    pwmv2_select_cmp_source(APP_PWM, PWMV2_CMP_INDEX(0), cmp_value_from_shadow_val, PWMV2_SHADOW_INDEX(20));
    pwmv2_select_cmp_source(APP_PWM, PWMV2_CMP_INDEX(4), cmp_value_from_shadow_val, PWMV2_SHADOW_INDEX(21));
    pwmv2_select_cmp_trigmux(APP_PWM, PWMV2_CMP_INDEX(0), 0U);
    /* mode 2: 故意让 CMP4 选另一个触发源, 验证勘误说的"只有 CMP_CFG0 的选择生效" */
    pwmv2_select_cmp_trigmux(APP_PWM, PWMV2_CMP_INDEX(4), (mode == 2U) ? 1U : 0U);
    if (mode == 1U) {
        /* 规避法 2: 不用触发输入更新, 改影子锁 */
        pwmv2_cmp_update_trig_time(APP_PWM, 0, pwm_shadow_register_update_on_shlk);
        pwmv2_cmp_update_trig_time(APP_PWM, 4, pwm_shadow_register_update_on_shlk);
    } else {
        pwmv2_cmp_update_trig_time(APP_PWM, 0, pwm_shadow_register_update_on_trigmux);
        pwmv2_cmp_update_trig_time(APP_PWM, 4, pwm_shadow_register_update_on_trigmux);
    }
    pwmv2_shadow_register_lock(APP_PWM);

    /* 计数器 0/1 只做比较, 不使能输出引脚(PD00/PD01 是 CAN0) */
    pwmv2_enable_counter(APP_PWM, pwm_counter_0);
    pwmv2_enable_counter(APP_PWM, pwm_counter_1);
    pwmv2_start_pwm_output(APP_PWM, pwm_counter_0);
    pwmv2_start_pwm_output(APP_PWM, pwm_counter_1);
    (void) pwmv2_enable_cmp_irq(APP_PWM, 0);
    (void) pwmv2_enable_cmp_irq(APP_PWM, 4);

    if (mode != 1U) {
        trgm_output_t c = {0};
        c.type = trgm_output_same_as_input;
        c.invert = false;
        c.input = HPM_TRGM0_INPUT_SRC_PWM0_TRGO_6;
        trgm_output_config(BOARD_TIMER_TRGM, HPM_TRGM0_OUTPUT_SRC_PWM0_TRIG_IN_0, &c);
    }

    printf("[perr] 模式=%lu(%s), CMP0/CMP4 交替更新 %lu 次, 期望值 %lu/%lu tick 交替\r\n",
           (unsigned long) mode,
           (mode == 0U) ? "命中勘误: TRIG_SEL 同为0 + UPDATE_TIME=trigmux"
                        : ((mode == 1U) ? "规避: UPDATE_TIME=影子锁(软件强制)"
                                        : "对照: CMP4 的 TRIG_SEL 写 1, CMP0 写 0"),
           (unsigned long) iters,
           (unsigned long) (t / 4U), (unsigned long) (3U * t / 4U));

    for (uint32_t i = 0U; i < iters; i++) {
        a0 = (i & 1U) ? (t / 4U) : (3U * t / 4U);
        a4 = (i & 1U) ? (t / 3U) : (2U * t / 3U);
        pwmv2_shadow_register_unlock(APP_PWM);
        pwmv2_set_shadow_val(APP_PWM, PWMV2_SHADOW_INDEX(20), a0, 0, false);
        pwmv2_set_shadow_val(APP_PWM, PWMV2_SHADOW_INDEX(21), a4, 0, false);
        pwmv2_shadow_register_lock(APP_PWM);
        s_e_n0 = 0U; s_e_n4 = 0U;
        board_delay_us(600U);
        if (s_e_n0 == 0U) { miss0++; }
        if (s_e_n4 == 0U) { miss4++; }
        v0 = s_e_val0; v4 = s_e_val4;   /* ISR 里已换算成 tick */
        n0 = s_e_n0; n4 = s_e_n4;
        if ((n0 != 0U) && (cmp_diff(v0, a0) > tol)) { bad0++; }
        if ((n4 != 0U) && (cmp_diff(v4, a4) > tol)) { bad4++; }
        if (((i & 0x1FU) == 0U) && (i != 0U)) {
            printf("  i=%lu a0=%lu v0=%lu n0=%lu | a4=%lu v4=%lu n4=%lu\r\n",
                   (unsigned long) i, (unsigned long) a0, (unsigned long) v0, (unsigned long) n0,
                   (unsigned long) a4, (unsigned long) v4, (unsigned long) n4);
        }
    }
    printf("[perr] 结果: CMP0 异常 %lu/%lu, CMP4 异常 %lu/%lu, 无中断 %lu/%lu\r\n",
           (unsigned long) bad0, (unsigned long) iters,
           (unsigned long) bad4, (unsigned long) iters,
           (unsigned long) (miss0 + miss4), (unsigned long) (2U * iters));
    printf("       tol=%lu tick (%.2f us @ %lu Hz), 最后 v0=%lu v4=%lu\r\n",
           (unsigned long) tol, (double) tol * 1e6 / (double) s_pwm_clk_hz,
           (unsigned long) s_pwm_clk_hz, (unsigned long) v0, (unsigned long) v4);
    /* 收尾: 关掉试验用中断, 恢复计数器 0/1 配置 */
    pwmv2_disable_cmp_irq(APP_PWM, 0);
    pwmv2_disable_cmp_irq(APP_PWM, 4);
    pwm_start();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 perr, cmd_perr, errata E00038 test: CMP0/CMP4 trigmux update <iters> <mode>);

static void cmd_pmeas(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    print_meas();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 pmeas, cmd_pmeas, print measured period/duty and reset stats);

int main(void)
{
    uint32_t last;

    bsp_init();
    board_init_led_pins();
    board_timer_init_pwm0_pins();

    s_pwm_clk_hz = clock_get_frequency(APP_PWM_CLK);
    printf("\r\n=== pwm_basic (board %s, ext %s) ===\r\n", BOARD_NAME, board_timer_ext_version());
    if (s_pwm_clk_hz == 0U) {
        printf("!! clock_pwm0 = 0\r\n");
    }
    tb_init();
    pwm_start();
    print_info();
    printf("commands: pwm <hz> <permille> | pinfo | pmeas\r\n");

    last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if ((bsp_uptime_ms() - last) >= 2000U) {
            last = bsp_uptime_ms();
            print_meas();
            board_led_toggle();
        }
    }
    return 0;
}