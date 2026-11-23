/*
 * ewdg_window - EWDG 窗口看门狗: 窗口外喂狗复位 / 窗口内喂狗存活
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1), 无需外部接线
 *   EWDG0 时钟源 OSC32 (32768 Hz), 超时 1000 ms
 *   窗口模式: WIN_LOWER = 8/16, WIN_UPPER = 7/16 (见 hpm_ewdg_drv.h 的枚举注释)
 *   复位后 PPOR->RESET_FLAG 的 WDG0 位(bit16) 置 1, 启动时打印并清除
 *
 * 用法:
 *   wdinfo                 看 EWDG 配置寄存器与换算出的超时
 *   wdw <delay_ms> [T_ms]  窗口模式下在 delay_ms 喂一次狗, 然后等 2T 看是否存活
 *   wdn <delay_ms> [T_ms]  关窗口的对照组
 *
 * 判读: 串口日志里看到 "SURVIVED" 表示没复位; 若复位, 板子重启后会打印
 *       "*** last reset was caused by EWDG0 timeout ***" 和 BOOT_RESET 行。
 */
#include <string.h>
#include "bsp.h"
#include "hpm_ewdg_drv.h"
#include "hpm_sysctl_drv.h"
#include "hpm_ppor_drv.h"

#define APP_EWDG            HPM_EWDG0
#define APP_EWDG_SRC_HZ     (32768UL)
#define RESET_SOURCE_WDG0   (1UL << 16)
#define RESET_SOURCE_WDG1   (1UL << 17)

static uint32_t s_boot_reset_flag;

static int ewdg_arm(uint32_t timeout_ms, bool window,
                    ewdg_window_low_limit_t lower, ewdg_window_upper_limit_t upper)
{
    ewdg_config_t cfg;
    clock_add_to_group(clock_watchdog0, 0);
    ewdg_get_default_config(APP_EWDG, &cfg);
    cfg.enable_watchdog = true;
    cfg.int_rst_config.enable_timeout_reset = true;
    cfg.ctrl_config.use_lowlevel_timeout = false;
    cfg.ctrl_config.cnt_clk_sel = ewdg_cnt_src_clk_32k;
    cfg.cnt_src_freq = APP_EWDG_SRC_HZ;
    cfg.ctrl_config.timeout_reset_us = timeout_ms * 1000UL;
    cfg.ctrl_config.enable_window_mode = window;
    cfg.ctrl_config.window_lower_limit = lower;
    cfg.ctrl_config.window_upper_limit = upper;
    hpm_stat_t st = ewdg_init(APP_EWDG, &cfg);
    if (st != status_success) {
        printf("ewdg_init failed: %d\r\n", (int) st);
        return -1;
    }
    uint32_t ticks = ewdg_get_timeout_reset_ticks(APP_EWDG);
    uint32_t us = ewdg_convert_timeout_ticks_to_timeout_us(APP_EWDG, APP_EWDG_SRC_HZ, ticks);
    printf("EWDG0 armed: req=%lu ms actual=%lu us ticks=%lu window=%d ctrl0=0x%08lx\r\n",
           (unsigned long) timeout_ms, (unsigned long) us, (unsigned long) ticks,
           window ? 1 : 0, (unsigned long) APP_EWDG->CTRL0);
    return 0;
}

static void cmd_wdinfo(void)
{
    printf("BOOT_RESET flag=0x%08lx WDG0=%d WDG1=%d\r\n",
           (unsigned long) s_boot_reset_flag,
           (s_boot_reset_flag & RESET_SOURCE_WDG0) ? 1 : 0,
           (s_boot_reset_flag & RESET_SOURCE_WDG1) ? 1 : 0);
    printf("EWDG0 CTRL0=0x%08lx (WIN_EN bit, WIN_LOWER/WIN_UPPER 见 hpm_ewdg_drv.h)\r\n",
           (unsigned long) APP_EWDG->CTRL0);
    printf("ewdg clock = %lu Hz (OSC32)\r\n", (unsigned long) APP_EWDG_SRC_HZ);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 wdinfo, cmd_wdinfo, EWDG window-mode register/state info);

static void run_feed_trial(const char *name, uint32_t delay_ms, uint32_t timeout_ms, bool window)
{
    printf("TRIAL %s delay=%lu ms timeout=%lu ms window=%d\r\n",
           name, (unsigned long) delay_ms, (unsigned long) timeout_ms, window ? 1 : 0);
    if (ewdg_arm(timeout_ms, window,
                 ewdg_window_lower_timeout_period_8_div_16,
                 ewdg_window_upper_timeout_period_7_div_16) != 0) {
        return;
    }
    board_delay_ms(delay_ms);
    printf("  feed #1 @%lu ms\r\n", (unsigned long) delay_ms);
    ewdg_refresh(APP_EWDG);
    /* 喂完之后继续等 2T, 如果配置无效/窗口内允许, 则会存活; 否则复位/超时复位 */
    board_delay_ms(2U * timeout_ms + 200U);
    ewdg_disable(APP_EWDG);
    printf("TRIAL %s RESULT=SURVIVED (waited %lu ms after one feed)\r\n",
           name, (unsigned long) (2U * timeout_ms + 200U));
}

static void run_hold_trial(uint32_t timeout_ms, uint32_t feed_ms, uint32_t hold_ms)
{
    printf("TRIAL hold timeout=%lu ms feed_every=%lu ms hold=%lu ms window=1\r\n",
           (unsigned long) timeout_ms, (unsigned long) feed_ms, (unsigned long) hold_ms);
    if (ewdg_arm(timeout_ms, true,
                 ewdg_window_lower_timeout_period_8_div_16,
                 ewdg_window_upper_timeout_period_7_div_16) != 0) {
        return;
    }
    uint32_t t0 = bsp_uptime_ms();
    uint32_t feeds = 0;
    while (bsp_uptime_ms() - t0 < hold_ms) {
        board_delay_ms(feed_ms);
        if (bsp_uptime_ms() - t0 < timeout_ms / 2U) {
            continue;   /* 窗口下限之前不喂，否则是窗口外喂狗 */
        }
        ewdg_refresh(APP_EWDG);
        feeds++;
    }
    ewdg_disable(APP_EWDG);
    printf("TRIAL hold RESULT=SURVIVED feeds=%lu hold=%lu ms (窗口内持续喂狗)\r\n",
           (unsigned long) feeds, (unsigned long) hold_ms);
}

static void cmd_wdw(int argc, char *argv[])
{
    uint32_t delay = 50, tmo = 1000;
    if (argc < 2 || bsp_parse_u32(argv[1], &delay)) {
        printf("usage: wdw <delay_ms> [timeout_ms]\r\n");
        return;
    }
    if (argc >= 3) {
        bsp_parse_u32(argv[2], &tmo);
    }
    run_feed_trial("wdw", delay, tmo, true);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 wdw, cmd_wdw, window mode: feed once at delay -> wdw <delay_ms> [timeout_ms]);

static void cmd_wdn(int argc, char *argv[])
{
    uint32_t delay = 50, tmo = 1000;
    if (argc < 2 || bsp_parse_u32(argv[1], &delay)) {
        printf("usage: wdn <delay_ms> [timeout_ms]\r\n");
        return;
    }
    if (argc >= 3) {
        bsp_parse_u32(argv[2], &tmo);
    }
    run_feed_trial("wdn", delay, tmo, false);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 wdn, cmd_wdn, no window (control): feed once at delay -> wdn <delay_ms> [timeout_ms]);

int main(void)
{
    bsp_init();

    s_boot_reset_flag = ppor_reset_get_flags(HPM_PPOR);
    ppor_reset_clear_flags(HPM_PPOR, 0xFFFFFFFFUL);

    printf("\r\n=== ewdg_window: EWDG window watchdog ===\r\n");
    printf("BOOT_RESET flag=0x%08lx WDG0=%d WDG1=%d\r\n",
           (unsigned long) s_boot_reset_flag,
           (s_boot_reset_flag & RESET_SOURCE_WDG0) ? 1 : 0,
           (s_boot_reset_flag & RESET_SOURCE_WDG1) ? 1 : 0);
    if (s_boot_reset_flag & RESET_SOURCE_WDG0) {
        printf("*** last reset was caused by EWDG0 timeout/window violation ***\r\n");
    }

    /* R5 自动化: 第一次启动做"窗口外喂狗"(预期复位); 复位后的启动做"窗口内喂狗"(预期存活) */
    board_delay_ms(300);
    if (s_boot_reset_flag & RESET_SOURCE_WDG0) {
        printf("AUTO phase=hold : 上一次是 WDG0 复位(窗口外喂狗被复位, 符合预期)\r\n");
        run_hold_trial(1000, 50, 2500);
    } else {
        printf("AUTO phase=early : 立刻在窗口外喂一次狗, 预期 ~50 ms 后复位\r\n");
        run_feed_trial("auto_early", 50, 1000, true);
    }
    clock_add_to_group(clock_watchdog0, 0);
    printf("shell: wdinfo | wdw <delay_ms> [T_ms] | wdn <delay_ms> [T_ms]\r\n");

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
