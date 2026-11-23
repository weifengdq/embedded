/*
 * Wdg_PBcfg.c —— WDG post-build 配置实例（EWDG0）
 *
 * 板级参数（与 docs/MCAL开发约定.md §2 一致）：
 *   EWDG0，计数器源 OSC32 = 32768 Hz（不随 PLL 配置变化，掉电前也能工作）
 *   超时：SLOW = 1000 ms，FAST = 100 ms；默认 SLOW
 *   窗口模式：WIN_LOWER = 8/16、WIN_UPPER = 7/16（实测见 samples/power/ewdg_window）
 *   不允许关狗（WdgDisableAllowed = OFF），复位源 = EWDG0 timeout（PPOR.RESET_FLAG bit16）
 */
#include "Wdg.h"
#include "Wdg_PBcfg.h"
#include "hpm_soc.h"
#include "hpm_clock_drv.h"

const Wdg_ConfigType Wdg_Config = {
    (uint32)(uintptr_t)HPM_EWDG0,
    (uint32)clock_watchdog0,
    (uint32)WDG_SRC_CLK_HZ,
    60000u,   /* WdgMaxTimeout: 60 s */
    1000u,    /* WDGIF_SLOW_MODE: 1 s   */
    100u,     /* WDGIF_FAST_MODE: 100 ms */
    1u,       /* 窗口模式 */
    0u,       /* ewdg_window_lower_timeout_period_8_div_16  */
    0u,       /* ewdg_window_upper_timeout_period_8_div_16  */
    0u,       /* WdgDisableAllowed = OFF */
    WDGIF_SLOW_MODE
};
