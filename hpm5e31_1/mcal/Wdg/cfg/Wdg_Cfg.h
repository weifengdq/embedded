/*
 * Wdg_Cfg.h —— WDG 预编译配置（SWS_Wdg §10.2）
 */
#ifndef WDG_CFG_H
#define WDG_CFG_H

#include "Mcal_Cfg.h"

#ifndef WDG_DEV_ERROR_DETECT
#define WDG_DEV_ERROR_DETECT        MCAL_DEV_ERROR_DETECT
#endif

#define WDG_VERSION_INFO_API        MCAL_VERSION_INFO_API
#define WDG_DISABLE_ALLOWED         STD_OFF   /* WdgDisableAllowed：安全件不允许关狗 */
#define WDG_TRIGGER_CONDITION_API   STD_ON

/* EWDG0 计数器源：OSC32 32768 Hz（见 hpm_ewdg_drv.h ewdg_cnt_src_clk_32k） */
#define WDG_SRC_CLK_HZ              (32768uL)

#endif /* WDG_CFG_H */
