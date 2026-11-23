/*
 * Wdg.h —— AUTOSAR Watchdog Driver 公共接口（HPM5E31 / EWDG0）
 *
 * 依据：AUTOSAR_CP_SWS_WatchdogDriver（CP R25-11）§8.3、§7.3.1；
 *       HPM5E31 RM EWDG 章节；本书 docs/chapters/22-wdg-power-temp.md（EWDG 实测）。
 * 例程：samples/mcal/wdg_trigger/、samples/mcal/wdg_reset/（本轮 wdg_timeout_reset/）。
 * 本头文件不包含任何 HPM SDK 头文件。
 */
#ifndef WDG_H
#define WDG_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Wdg_Cfg.h"
#include "Mcal_ModuleId.h"

/* ---- 版本 ---- */
#define WDG_AR_RELEASE_MAJOR_VERSION    (4u)
#define WDG_AR_RELEASE_MINOR_VERSION    (4u)
#define WDG_AR_RELEASE_REVISION_VERSION (0u)
#define WDG_SW_MAJOR_VERSION            (1u)
#define WDG_SW_MINOR_VERSION            (0u)
#define WDG_SW_PATCH_VERSION            (0u)

#define WDG_VENDOR_ID                   ((uint16)0xFFFFu)

/* ---- API Service ID（SWS_Wdg §8.3） ---- */
#define WDG_API_ID_INIT              (0x00u)
#define WDG_API_ID_SET_MODE          (0x01u)
#define WDG_API_ID_SET_TRIGGER_CONDITION (0x03u)
#define WDG_API_ID_GET_VERSION_INFO  (0x04u)

/* ---- 开发错误码（SWS_Wdg §7.3.1） ---- */
#define WDG_E_DRIVER_STATE   (0x10u)
#define WDG_E_PARAM_MODE     (0x11u)
#define WDG_E_PARAM_CONFIG   (0x12u)
#define WDG_E_PARAM_TIMEOUT  (0x13u)
#define WDG_E_PARAM_POINTER  (0x14u)
#define WDG_E_INIT_FAILED    (0x15u)

/* ---- 扩展生产错误（SWS_Wdg §7.3.4，正常应上报 Dem；本子集只有 Dem 桩） ---- */
#define WDG_E_MODE_FAILED      (0x1Au)
#define WDG_E_DISABLE_REJECTED (0x1Bu)

/* ---- 数据类型 ----
 * WdgIf_ModeType 在 AUTOSAR 里来自 WdgIf.h（BSW 接口）。本 MCAL 子集不包含 WdgIf，
 * 因此在 Wdg.h 提供同值定义，保证与 COMAS/上层 WdgIf 头文件共存时可用
 * （若上层已定义 WDGIF_OFF_MODE 则不重复定义）。
 */
#ifndef WDGIF_OFF_MODE
typedef uint8 WdgIf_ModeType;
#define WDGIF_OFF_MODE  (0x00u)
#define WDGIF_SLOW_MODE (0x01u)
#define WDGIF_FAST_MODE (0x02u)
#endif

/* 驱动内部状态（SWS_Wdg §8.3，外部可见便于调试） */
#define WDG_UNINIT (0u)
#define WDG_IDLE   (1u)
#define WDG_BUSY   (2u)

/* ---- Post-build 配置（板级映射只放这里） ---- */
typedef struct
{
    uint32 base;           /* EWDG 实例基地址                              */
    uint32 clockName;      /* 时钟节点（clock_watchdog0）                  */
    uint32 srcClkHz;       /* 计数器时钟源频率（OSC32 = 32768 Hz）         */
    uint16 maxTimeoutMs;   /* WdgMaxTimeout（SWS 配置参数）                */
    uint16 slowTimeoutMs;  /* WDGIF_SLOW_MODE 的超时                      */
    uint16 fastTimeoutMs;  /* WDGIF_FAST_MODE 的超时                      */
    uint8  windowMode;     /* 1 = 窗口看门狗                              */
    uint8  windowLower;    /* ewdg_window_low_limit_t 枚举值              */
    uint8  windowUpper;    /* ewdg_window_upper_limit_t 枚举值            */
    uint8  disableAllowed; /* WdgDisableAllowed：0 = 不许关狗             */
    uint8  defaultMode;    /* Wdg_Init 后进入的模式（WdgIf_ModeType）      */
} Wdg_ConfigType;

/* ---- API（SWS_Wdg §8.3） ---- */
void           Wdg_Init(const Wdg_ConfigType *ConfigPtr);
Std_ReturnType Wdg_SetMode(WdgIf_ModeType Mode);
void           Wdg_SetTriggerCondition(uint16 timeout);
void           Wdg_GetVersionInfo(Std_VersionInfoType *versioninfo);

/* ---- MCAL 内部调试接口（非 AUTOSAR） ---- */
uint8 Wdg_GetState(void);
uint8 Wdg_GetMode(void);

#endif /* WDG_H */
