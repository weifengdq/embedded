/*
 * Wdg.c —— AUTOSAR Watchdog Driver 实现（HPM5E31 EWDG0）
 *
 * 参考：
 *   AUTOSAR_CP_SWS_WatchdogDriver §8.3（Init/SetMode/SetTriggerCondition 语义）；
 *   HPM5E31 RM EWDG；hpm_sdk/drivers/src/hpm_ewdg_drv.c（ewdg_init/refresh 时序）；
 *   本书 docs/chapters/22-wdg-power-temp.md（EWDG 实测，含窗口模式坑）。
 *
 * 语义映射：
 *   Wdg_SetMode(OFF/SLOW/FAST)  -> 关狗 / 用 slowTimeoutMs / fastTimeoutMs 重新使能；
 *   Wdg_SetTriggerCondition(ms) -> 按 ms 重算超时 tick 并重新装载（SWS 要求可动态改触发周期），
 *                                  然后刷新一次；timeout=0 表示"尽快复位"；
 *   窗口模式：窗口外刷新会触发 EWDG 复位（硬件行为，driver 不屏蔽）。
 */
#include "Wdg.h"
#include "Wdg_PBcfg.h"
#include "Det.h"
#include "hpm_soc.h"
#include "hpm_clock_drv.h"
#include "hpm_ewdg_drv.h"

static uint8                s_wdg_state = WDG_UNINIT;
static uint8                s_wdg_mode = WDGIF_OFF_MODE;
static const Wdg_ConfigType *s_wdg_cfg = NULL_PTR;

#if (WDG_DEV_ERROR_DETECT == STD_ON)
#define WDG_DET(api, err)  MCAL_DET_REPORT(WDG_MODULE_ID, 0u, (api), (err))
#else
#define WDG_DET(api, err)  ((void)0)
#endif

#define WDG_BASE(pc) ((EWDG_Type *)(uintptr_t)(pc)->base)

/* 按模式选择超时（ms） */
static uint32 wdg_timeout_of_mode(const Wdg_ConfigType *cfg, uint8 mode)
{
    uint32 t;
    switch (mode)
    {
    case WDGIF_FAST_MODE:
        t = (uint32)cfg->fastTimeoutMs;
        break;
    case WDGIF_SLOW_MODE:
    default:
        t = (uint32)cfg->slowTimeoutMs;
        break;
    }
    if (t == 0u)
    {
        t = 1u;
    }
    if (t > (uint32)cfg->maxTimeoutMs)
    {
        t = (uint32)cfg->maxTimeoutMs;
    }
    return t;
}

/* 以 timeout_ms 重新装载 EWDG（窗口模式/复位使能来自 PBcfg） */
static Std_ReturnType wdg_arm(uint32 timeout_ms)
{
    const Wdg_ConfigType *cfg = s_wdg_cfg;
    ewdg_config_t ecfg;
    hpm_stat_t st;

    if (cfg == NULL_PTR)
    {
        return E_NOT_OK;
    }
    if (timeout_ms == 0u)
    {
        timeout_ms = 1u;   /* "0" = 尽快复位：按 1 ms 装载，调用方不再喂狗 */
    }
    if (timeout_ms > (uint32)cfg->maxTimeoutMs)
    {
        timeout_ms = (uint32)cfg->maxTimeoutMs;
    }

    clock_add_to_group((clock_name_t)cfg->clockName, 0);
    ewdg_get_default_config(WDG_BASE(cfg), &ecfg);
    ecfg.enable_watchdog = true;
    ecfg.int_rst_config.enable_timeout_reset = true;
    ecfg.int_rst_config.enable_timeout_interrupt = false;
    ecfg.ctrl_config.use_lowlevel_timeout = false;
    ecfg.ctrl_config.cnt_clk_sel = ewdg_cnt_src_clk_32k;
    ecfg.cnt_src_freq = cfg->srcClkHz;
    ecfg.ctrl_config.timeout_reset_us = timeout_ms * 1000uL;
    ecfg.ctrl_config.enable_window_mode = (cfg->windowMode != 0u) ? true : false;
    ecfg.ctrl_config.window_lower_limit = (ewdg_window_low_limit_t)cfg->windowLower;
    ecfg.ctrl_config.window_upper_limit = (ewdg_window_upper_limit_t)cfg->windowUpper;

    st = ewdg_init(WDG_BASE(cfg), &ecfg);
    if (st != status_success)
    {
        return E_NOT_OK;
    }
    return E_OK;
}

/* ---------- SWS_Wdg §8.3.1 Wdg_Init（Service ID 0x00） ---------- */
void Wdg_Init(const Wdg_ConfigType *ConfigPtr)
{
    const Wdg_ConfigType *cfg;

    cfg = (ConfigPtr != NULL_PTR) ? ConfigPtr : (&Wdg_Config);
    if ((cfg->maxTimeoutMs == 0u) || (cfg->srcClkHz == 0u) ||
        (cfg->slowTimeoutMs > cfg->maxTimeoutMs) || (cfg->fastTimeoutMs > cfg->maxTimeoutMs) ||
        (cfg->defaultMode > WDGIF_FAST_MODE))
    {
        WDG_DET(WDG_API_ID_INIT, WDG_E_PARAM_CONFIG);   /* SWS_Wdg_00090 */
        return;
    }
    if ((cfg->defaultMode == WDGIF_OFF_MODE) && (cfg->disableAllowed == 0u))
    {
        /* SWS_Wdg_00025：不允许关狗时默认模式却要关狗 -> 不初始化 */
        WDG_DET(WDG_API_ID_INIT, WDG_E_PARAM_CONFIG);
        return;
    }
    s_wdg_cfg = cfg;
    s_wdg_state = WDG_BUSY;
    if (wdg_arm(wdg_timeout_of_mode(cfg, cfg->defaultMode)) != E_OK)
    {
        s_wdg_cfg = NULL_PTR;
        s_wdg_state = WDG_UNINIT;
        WDG_DET(WDG_API_ID_INIT, WDG_E_INIT_FAILED);    /* SWS_Wdg_00019 */
        return;
    }
    s_wdg_mode = cfg->defaultMode;
    s_wdg_state = WDG_IDLE;
    (void)ewdg_refresh(WDG_BASE(cfg));
}

/* ---------- SWS_Wdg §8.3.2 Wdg_SetMode（Service ID 0x01） ---------- */
Std_ReturnType Wdg_SetMode(WdgIf_ModeType Mode)
{
    uint32 timeout_ms;

    if (s_wdg_state == WDG_UNINIT)
    {
        WDG_DET(WDG_API_ID_SET_MODE, WDG_E_DRIVER_STATE);  /* SWS_Wdg_00017 */
        return E_NOT_OK;
    }
    if (Mode > WDGIF_FAST_MODE)
    {
        WDG_DET(WDG_API_ID_SET_MODE, WDG_E_PARAM_MODE);    /* SWS_Wdg_00091 */
        return E_NOT_OK;
    }
    if (Mode == WDGIF_OFF_MODE)
    {
        if (s_wdg_cfg->disableAllowed == 0u)
        {
            /* SWS_Wdg_00026：不允许关狗 -> 拒绝并返回 E_NOT_OK（生产错误 WDG_E_DISABLE_REJECTED） */
            WDG_DET(WDG_API_ID_SET_MODE, WDG_E_DISABLE_REJECTED);
            return E_NOT_OK;
        }
        ewdg_disable(WDG_BASE(s_wdg_cfg));
        s_wdg_mode = WDGIF_OFF_MODE;
        return E_OK;
    }

    s_wdg_state = WDG_BUSY;                                /* SWS_Wdg_00018 */
    timeout_ms = wdg_timeout_of_mode(s_wdg_cfg, Mode);
    if (wdg_arm(timeout_ms) != E_OK)
    {
        s_wdg_state = WDG_IDLE;
        WDG_DET(WDG_API_ID_SET_MODE, WDG_E_MODE_FAILED);   /* SWS_Wdg_00016 */
        return E_NOT_OK;
    }
    (void)ewdg_refresh(WDG_BASE(s_wdg_cfg));               /* SWS_Wdg_00145 */
    s_wdg_mode = Mode;
    s_wdg_state = WDG_IDLE;
    return E_OK;                                           /* SWS_Wdg_00103 */
}

/* ---------- SWS_Wdg §8.3.3 Wdg_SetTriggerCondition（Service ID 0x03） ---------- */
void Wdg_SetTriggerCondition(uint16 timeout)
{
    if (s_wdg_state == WDG_UNINIT)
    {
        WDG_DET(WDG_API_ID_SET_TRIGGER_CONDITION, WDG_E_DRIVER_STATE);
        return;
    }
    if ((uint32)timeout > (uint32)s_wdg_cfg->maxTimeoutMs)
    {
        WDG_DET(WDG_API_ID_SET_TRIGGER_CONDITION, WDG_E_PARAM_TIMEOUT); /* SWS_Wdg_00146 */
        return;
    }
    if (timeout == 0u)
    {
        /* SWS_Wdg_00140：0 = 立即停止喂狗倒计时 -> 尽快复位；装载 1 ms 后不再刷新 */
        s_wdg_state = WDG_BUSY;
        (void)wdg_arm(1u);
        s_wdg_state = WDG_IDLE;
        return;
    }
    s_wdg_state = WDG_BUSY;
    if (wdg_arm((uint32)timeout) == E_OK)
    {
        (void)ewdg_refresh(WDG_BASE(s_wdg_cfg));
    }
    s_wdg_state = WDG_IDLE;
}

/* ---------- SWS_Wdg §8.3.4 Wdg_GetVersionInfo（Service ID 0x04） ---------- */
void Wdg_GetVersionInfo(Std_VersionInfoType *versioninfo)
{
#if (WDG_VERSION_INFO_API == STD_ON)
    if (versioninfo == NULL_PTR)
    {
        WDG_DET(WDG_API_ID_GET_VERSION_INFO, WDG_E_PARAM_POINTER);
        return;
    }
    versioninfo->vendorID = WDG_VENDOR_ID;
    versioninfo->moduleID = (uint16)WDG_MODULE_ID;
    versioninfo->sw_major_version = (uint8)WDG_SW_MAJOR_VERSION;
    versioninfo->sw_minor_version = (uint8)WDG_SW_MINOR_VERSION;
    versioninfo->sw_patch_version = (uint8)WDG_SW_PATCH_VERSION;
#else
    (void)versioninfo;
    WDG_DET(WDG_API_ID_GET_VERSION_INFO, WDG_E_PARAM_POINTER);
#endif
}

/* ---------- 内部调试接口 ---------- */
uint8 Wdg_GetState(void)
{
    return s_wdg_state;
}

uint8 Wdg_GetMode(void)
{
    return s_wdg_mode;
}
