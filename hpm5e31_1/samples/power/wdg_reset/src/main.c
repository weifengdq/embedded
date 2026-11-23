/*
 * wdg_reset - EWDG 看门狗: 不喂狗复位 / 喂狗存活 / 复位源识别
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1), 无需外部接线
 *   EWDG0 时钟源 OSC32 (32768 Hz), 超时 1s 不喂狗 -> SoC 复位
 *   复位后 PPOR->RESET_FLAG 的 WDG0 位(bits16) 置 1, 本程序启动时打印并清除
 *
 * 现象: 见 README.md
 */
#include "bsp.h"
#include "hpm_ewdg_drv.h"
#include "hpm_sysctl_drv.h"
#include "hpm_ppor_drv.h"
#include "hpm_csr_drv.h"

#define APP_EWDG            HPM_EWDG0
#define APP_EWDG_SRC_HZ     (32768UL)
#define RESET_SOURCE_WDG0   (1UL << 16)
#define RESET_SOURCE_WDG1   (1UL << 17)
#define RESET_SOURCE_WDG2   (1UL << 18)
#define RESET_SOURCE_WDG3   (1UL << 19)
#define RESET_SOURCE_WDG4   (1UL << 20)

static uint32_t s_boot_reset_flag;

static int ewdg_arm(uint32_t timeout_ms)
{
    ewdg_config_t cfg;
    ewdg_get_default_config(APP_EWDG, &cfg);
    cfg.enable_watchdog = true;
    cfg.int_rst_config.enable_timeout_reset = true;
    cfg.ctrl_config.use_lowlevel_timeout = false;
    cfg.ctrl_config.cnt_clk_sel = ewdg_cnt_src_clk_32k;
    cfg.cnt_src_freq = APP_EWDG_SRC_HZ;
    cfg.ctrl_config.timeout_reset_us = timeout_ms * 1000UL;
    hpm_stat_t st = ewdg_init(APP_EWDG, &cfg);
    if (st != status_success) {
        printf("ewdg_init failed: %d\r\n", (int) st);
        return -1;
    }
    uint32_t ticks = ewdg_get_timeout_reset_ticks(APP_EWDG);
    uint32_t us = ewdg_convert_timeout_ticks_to_timeout_us(APP_EWDG, APP_EWDG_SRC_HZ, ticks);
    printf("EWDG0 armed: request %lu ms, actual %lu us (ticks=%lu)\r\n",
           (unsigned long) timeout_ms, (unsigned long) us, (unsigned long) ticks);
    return 0;
}

static void cmd_resetcause(void)
{
    uint32_t f = s_boot_reset_flag;
    printf("PPOR RESET_FLAG at boot = 0x%08lx\r\n", (unsigned long) f);
    if (f & RESET_SOURCE_WDG0) printf("  WDG0 reset detected\r\n");
    if (f & RESET_SOURCE_WDG1) printf("  WDG1 reset detected\r\n");
    if (!(f & (RESET_SOURCE_WDG0 | RESET_SOURCE_WDG1 | RESET_SOURCE_WDG2 | RESET_SOURCE_WDG3 | RESET_SOURCE_WDG4))) {
        printf("  no watchdog reset bit set (power-on / pin / software reset)\r\n");
    }
    printf("  current PPOR RESET_FLAG = 0x%08lx\r\n",
           (unsigned long) ppor_reset_get_flags(HPM_PPOR));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 resetcause, cmd_resetcause, show boot-time PPOR reset flags);

static void cmd_wdgarm(int argc, char *argv[])
{
    uint32_t ms = 1000;
    if (argc >= 2 && (bsp_parse_u32(argv[1], &ms) || ms < 1 || ms > 60000)) {
        printf("usage: wdgarm [1..60000] ms\r\n");
        return;
    }
    if (ewdg_arm(ms) == 0) {
        printf("NOT feeding: SoC should reset in ~%lu ms ...\r\n", (unsigned long) ms);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 wdgarm, cmd_wdgarm, arm EWDG without feeding -> reset: wdgarm [ms]);

static void cmd_wdgfeed(int argc, char *argv[])
{
    uint32_t ms = 2000, period = 200;
    if (argc >= 2 && (bsp_parse_u32(argv[1], &ms) || ms < 100 || ms > 60000)) {
        printf("usage: wdgfeed [timeout_ms] [feed_period_ms]\r\n");
        return;
    }
    if (argc >= 3) {
        bsp_parse_u32(argv[2], &period);
    }
    if (ewdg_arm(ms) != 0) {
        return;
    }
    uint32_t t0 = bsp_uptime_ms();
    uint32_t feeds = 0;
    while (bsp_uptime_ms() - t0 < 5U * ms) {
        board_delay_ms(period);
        ewdg_refresh(APP_EWDG);
        feeds++;
        printf("  feed #%lu @%lu ms\r\n", (unsigned long) feeds, (unsigned long) (bsp_uptime_ms() - t0));
    }
    ewdg_disable(APP_EWDG);
    printf("wdgfeed: survived %lu ms with %lu feeds -> PASS\r\n",
           (unsigned long) (5U * ms), (unsigned long) feeds);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 wdgfeed, cmd_wdgfeed, arm EWDG and keep feeding it: wdgfeed [timeout_ms] [period_ms]);

int main(void)
{
    bsp_init();

    s_boot_reset_flag = ppor_reset_get_flags(HPM_PPOR);
    ppor_reset_clear_flags(HPM_PPOR, 0xFFFFFFFFUL);

    printf("\r\n=== wdg_reset: EWDG watchdog ===\r\n");
    if (s_boot_reset_flag & RESET_SOURCE_WDG0) {
        printf("*** last reset was caused by EWDG0 timeout (RESET_FLAG=0x%08lx) ***\r\n",
               (unsigned long) s_boot_reset_flag);
    }
    clock_add_to_group(clock_watchdog0, 0);
    printf("shell: resetcause | wdgarm [ms] | wdgfeed [timeout_ms] [period_ms]\r\n");

    uint32_t last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if (bsp_uptime_ms() - last >= 3000U) {
            last = bsp_uptime_ms();
            printf("alive, uptime=%lu ms, RESET_FLAG=0x%08lx\r\n",
                   (unsigned long) last, (unsigned long) ppor_reset_get_flags(HPM_PPOR));
        }
    }
    return 0;
}
