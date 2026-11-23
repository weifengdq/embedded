/*
 * stop_wake - STOP(系统低功耗) 模式 + GPTMR 定时唤醒, 100 次统计
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1), 无外部接线
 *   GPTMR1 CH0 每 10 ms 产生一次 reload 中断作唤醒源; 主循环连续执行
 *   sysctl_set_cpu0_lp_retention + cpu_lp_mode_trigger_system_lp + WFI 进入 STOP,
 *   唤醒延迟由 ISR 里读 GPTMR 计数器得到(事件->ISR 的 tick 数)。
 *
 * 同时验证 Errata E00042: "停止模式 CPU0 复位/掉电且系统总线时钟跟随关闭时可能唤醒异常"。
 * 三组配置:
 *   c1 retention=0x1F (CPU0 periph 保留, 正常用法)
 *   c2 retention=0x17 (CPU0 periph 关闭 => 勘误条件), RESOURCE[AXIS].MODE 默认 auto
 *   c3 retention=0x17 + RESOURCE[AXIS].MODE=force_on (勘误给的规避办法)
 * 若某组卡死不唤醒, 预先武装的 EWDG(3 s) 会复位; 复位后启动打印 RESET_FLAG WDG0 位。
 *
 * 命令: stopinfo | stopwake [iters]
 */
#include <string.h>
#include "bsp.h"
#include "hpm_gptmr_drv.h"
#include "hpm_mchtmr_drv.h"
#include "hpm_sysctl_drv.h"
#include "hpm_ppor_drv.h"
#include "hpm_ewdg_drv.h"
#include "hpm_pcfg_drv.h"
#include "hpm_csr_drv.h"

#define APP_TMR          HPM_GPTMR1
#define APP_TMR_IRQ      IRQn_GPTMR1
#define APP_TMR_CLK      clock_gptmr1
#define APP_TMR_CH       (0U)
#define APP_PERIOD_MS    (10U)
#define APP_EWDG         HPM_EWDG0
#define APP_EWDG_SRC_HZ  (32768UL)
#define RESET_SOURCE_WDG0 (1UL << 16)

#define MAX_SAMP 200

static volatile uint32_t s_wake_cnt;
static volatile uint32_t s_lat[MAX_SAMP];
static uint32_t s_tmr_hz;
static uint32_t s_boot_flag;

SDK_DECLARE_EXT_ISR_M(APP_TMR_IRQ, isr_stop_wake)
void isr_stop_wake(void)
{
    if (gptmr_check_status(APP_TMR, GPTMR_CH_RLD_STAT_MASK(APP_TMR_CH))) {
        gptmr_clear_status(APP_TMR, GPTMR_CH_RLD_STAT_MASK(APP_TMR_CH));
        uint32_t idx = s_wake_cnt;
        if (idx < MAX_SAMP) {
            /* reload 后经过的 GPTMR tick 数 = 定时事件到 ISR 入口的唤醒延迟 */
            s_lat[idx] = APP_TMR->CHANNEL[APP_TMR_CH].CNT;
        }
        s_wake_cnt = idx + 1;
    }
}

static void wake_timer_setup(void)
{
    gptmr_channel_config_t cfg;
    clock_add_to_group(APP_TMR_CLK, 0);
    s_tmr_hz = clock_get_frequency(APP_TMR_CLK);
    gptmr_channel_get_default_config(APP_TMR, &cfg);
    cfg.reload = s_tmr_hz / 1000U * APP_PERIOD_MS;
    (void) gptmr_channel_config(APP_TMR, APP_TMR_CH, &cfg, false);
    gptmr_enable_irq(APP_TMR, GPTMR_CH_RLD_IRQ_MASK(APP_TMR_CH));
    gptmr_start_counter(APP_TMR, APP_TMR_CH);
    intc_m_enable_irq_with_priority(APP_TMR_IRQ, 1);
}

static void ewdg_guard_setup(void)
{
    ewdg_config_t cfg;
    ewdg_get_default_config(APP_EWDG, &cfg);
    cfg.enable_watchdog = true;
    cfg.int_rst_config.enable_timeout_reset = true;
    cfg.ctrl_config.use_lowlevel_timeout = false;
    cfg.ctrl_config.cnt_clk_sel = ewdg_cnt_src_clk_32k;
    cfg.cnt_src_freq = APP_EWDG_SRC_HZ;
    cfg.ctrl_config.timeout_reset_us = 3000000UL;   /* 3 s: 卡死时复位并留证据 */
    (void) ewdg_init(APP_EWDG, &cfg);
}

static void cmd_stopinfo(void)
{
    uint32_t ret = sysctl_get_cpu_lp_retention(HPM_SYSCTL, 0);
    printf("BOOT_RESET flag=0x%08lx WDG0=%d\r\n", (unsigned long) s_boot_flag,
           (s_boot_flag & RESET_SOURCE_WDG0) ? 1 : 0);
    printf("STOP_INFO tmr_hz=%lu period=%lu ms retention_now=0x%lx axis_mode_settable=1\r\n",
           (unsigned long) s_tmr_hz, (unsigned long) APP_PERIOD_MS, (unsigned long) ret);
    printf("  PLL0/PLL1/XTAL retained? retention bits: b0=soc_pwr b1=soc_periph b2=cpu0_pwr b3=cpu0_periph b4=xtal b5=pll0 b6=pll1\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stopinfo, cmd_stopinfo, STOP-mode wake-up test info);

struct cfg_case {
    const char *name;
    uint32_t retention;
    bool axis_force;
};

static void run_case(const struct cfg_case *c, uint32_t iters)
{
    if (iters > MAX_SAMP) {
        iters = MAX_SAMP;
    }
    printf("CASE_START name=%s retention=0x%02lx axis_force=%d\r\n",
           c->name, (unsigned long) c->retention, c->axis_force ? 1 : 0);
    ewdg_guard_setup();
    sysctl_resource_target_set_mode(HPM_SYSCTL, sysctl_resource_axis,
                                    c->axis_force ? sysctl_resource_mode_force_on : sysctl_resource_mode_auto);
    s_wake_cnt = 0;
    memset((void *) s_lat, 0, sizeof(s_lat));
    uint32_t ok = 0, fail = 0;

    for (uint32_t i = 0; i < iters; i++) {
        ewdg_refresh(APP_EWDG);
        uint32_t c0 = s_wake_cnt;
        sysctl_enable_cpu0_wakeup_source_with_irq(HPM_SYSCTL, APP_TMR_IRQ);
        sysctl_set_cpu0_lp_retention(HPM_SYSCTL, c->retention);
        sysctl_set_cpu0_lp_mode(HPM_SYSCTL, cpu_lp_mode_trigger_system_lp);
        WFI();
        sysctl_set_cpu0_lp_mode(HPM_SYSCTL, cpu_lp_mode_gate_cpu_clock);
        if (s_wake_cnt == c0) {
            fail++;
            printf("STOP_WAKE_FAIL cfg=%s iter=%lu (WFI returned without wake IRQ)\r\n",
                   c->name, (unsigned long) i);
        } else {
            ok++;
        }
    }

    ewdg_disable(APP_EWDG);
    uint32_t n = (s_wake_cnt < iters) ? s_wake_cnt : iters;
    uint32_t mn = 0xFFFFFFFFUL, mx = 0;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = s_lat[i];
        if (v < mn) { mn = v; }
        if (v > mx) { mx = v; }
        sum += v;
    }
    if (n == 0) { mn = 0; }
    printf("STOP_WAKE cfg=%-10s retention=0x%02lx axis_force=%d iters=%lu ok=%lu fail=%lu lat_n=%lu min=%lu avg=%lu max=%lu tick\r\n",
           c->name, (unsigned long) c->retention, c->axis_force ? 1 : 0,
           (unsigned long) iters, (unsigned long) ok, (unsigned long) fail,
           (unsigned long) n, (unsigned long) mn,
           (unsigned long) ((n > 0) ? (uint32_t) (sum / n) : 0), (unsigned long) mx);
    if (n > 0 && s_tmr_hz > 0) {
        printf("STOP_WAKE_US cfg=%s min_us=%.2f avg_us=%.2f max_us=%.2f\r\n", c->name,
               (double) mn * 1e6 / (double) s_tmr_hz,
               (double) (sum / (n > 0 ? n : 1)) * 1e6 / (double) s_tmr_hz,
               (double) mx * 1e6 / (double) s_tmr_hz);
    }
}

static void cmd_stopwake(int argc, char *argv[])
{
    uint32_t iters = 100;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &iters);
    }
    static const struct cfg_case cases[] = {
        { "c1_ret3F", 0x3FUL, false },
        { "c2_ret37", 0x37UL, false },
        { "c3_ret37ax", 0x37UL, true },
    };
    printf("STOP_WAKE_START iters=%lu\r\n", (unsigned long) iters);
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        run_case(&cases[i], iters);
    }
    sysctl_resource_target_set_mode(HPM_SYSCTL, sysctl_resource_axis, sysctl_resource_mode_auto);
    sysctl_set_cpu0_lp_retention(HPM_SYSCTL, 0x7FUL);
    sysctl_set_cpu0_lp_mode(HPM_SYSCTL, cpu_lp_mode_gate_cpu_clock);
    printf("STOP_WAKE_DONE\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stopwake, cmd_stopwake, STOP-mode wake loop for 3 retention/axis configs: stopwake [iters]);

int main(void)
{
    bsp_init();
    s_boot_flag = ppor_reset_get_flags(HPM_PPOR);
    ppor_reset_clear_flags(HPM_PPOR, 0xFFFFFFFFUL);

    printf("\r\n=== stop_wake: STOP mode + Errata E00042 ===\r\n");
    printf("BOOT_RESET flag=0x%08lx WDG0=%d\r\n", (unsigned long) s_boot_flag,
           (s_boot_flag & RESET_SOURCE_WDG0) ? 1 : 0);
    if (s_boot_flag & RESET_SOURCE_WDG0) {
        printf("*** last reset was caused by EWDG0 timeout (previous stop-wake test hung?) ***\r\n");
    }
    clock_add_to_group(clock_watchdog0, 0);
    /* SDK 的 enter_stop_mode 前置：DCDC 低功耗电流限/迟滞，不做这步第一次 WFI 就醒不来(实测) */
    pcfg_dcdc_set_lp_current_limit(HPM_PCFG, pcfg_dcdc_lp_current_limit_250ma, false);
    pcfg_dcdc_set_current_hys_range(HPM_PCFG, pcfg_dcdc_current_hys_25mv);
    wake_timer_setup();
    sysctl_set_cpu0_lp_retention(HPM_SYSCTL, 0x7FUL);
    printf("shell: stopinfo | stopwake [iters]\r\n");

    /* R5 自动化: 单次上板跑三组配置; 若某组卡死, EWDG 3 s 复位, 下次启动跳过以免复位死循环 */
    board_delay_ms(300);
    if (s_boot_flag & RESET_SOURCE_WDG0) {
        printf("AUTO: 上一次 stopwake 卡死并被 EWDG 复位, 跳过自动重跑(避免复位死循环)\r\n");
    } else {
        cmd_stopwake(0, NULL);
    }

    uint32_t last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if (bsp_uptime_ms() - last >= 3000U) {
            last = bsp_uptime_ms();
            printf("alive uptime=%lu ms\r\n", (unsigned long) last);
        }
    }
    return 0;
}
