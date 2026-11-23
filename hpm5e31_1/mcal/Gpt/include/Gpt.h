/*
 * Gpt.h —— AUTOSAR GPT Driver 公共接口（HPM5E31 / GPTMR0 + GPTMR1）
 *
 * 依据：AUTOSAR_CP_SWS_GPTDriver（CP R25-11）§8 "API specification"；
 *       寄存器/时钟/引脚细节见 HPM5E31 RM 第 15 章 GPTMR 与
 *       本书 docs/chapters/15-gptmr.md（既有寄存器级实测）。
 * 例程：samples/mcal/gpt_periodic/、samples/mcal/gpt_oneShot/。
 *
 * 本头文件只依赖 Std_Types.h / Mcal_Cfg.h / Gpt_Cfg.h / Mcal_ModuleId.h，
 * 不含任何 HPM SDK 头文件（AUTOSAR MCAL 公共头可移植性要求）。
 */
#ifndef GPT_H
#define GPT_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Gpt_Cfg.h"
#include "Mcal_ModuleId.h"

/* ---- 版本号（SWS_Gpt §7.2 Version checking） ---- */
#define GPT_AR_RELEASE_MAJOR_VERSION    (4u)
#define GPT_AR_RELEASE_MINOR_VERSION    (4u)
#define GPT_AR_RELEASE_REVISION_VERSION (0u)
#define GPT_SW_MAJOR_VERSION            (1u)
#define GPT_SW_MINOR_VERSION            (0u)
#define GPT_SW_PATCH_VERSION            (0u)

#define GPT_VENDOR_ID                   ((uint16)0xFFFFu)

/* ---- API Service ID（SWS_Gpt §8.3，与 Service ID [hex] 一一对应） ---- */
#define GPT_API_ID_GET_VERSION_INFO  (0x00u)
#define GPT_API_ID_INIT              (0x01u)
#define GPT_API_ID_DEINIT            (0x02u)
#define GPT_API_ID_GET_TIME_ELAPSED  (0x03u)
#define GPT_API_ID_GET_TIME_REMAINING (0x04u)
#define GPT_API_ID_START_TIMER       (0x05u)
#define GPT_API_ID_STOP_TIMER        (0x06u)
#define GPT_API_ID_ENABLE_NOTIFICATION  (0x07u)
#define GPT_API_ID_DISABLE_NOTIFICATION (0x08u)
#define GPT_API_ID_SET_MODE          (0x09u)
#define GPT_API_ID_DISABLE_WAKEUP    (0x0Au)   /* 可选：本子集未实现 */
#define GPT_API_ID_ENABLE_WAKEUP     (0x0Bu)   /* 可选：本子集未实现 */
#define GPT_API_ID_CHECK_WAKEUP      (0x0Cu)   /* 可选：本子集未实现 */
#define GPT_API_ID_GET_PREDEF_TIMER_VALUE (0x0Du)

/* ---- 开发错误码（SWS_Gpt §7.3.1 Development Errors 表） ---- */
#define GPT_E_UNINIT              (0x0Au)
#define GPT_E_BUSY                (0x0Bu)   /* runtime error */
#define GPT_E_MODE                (0x0Cu)   /* runtime error */
#define GPT_E_ALREADY_INITIALIZED (0x0Du)
#define GPT_E_INIT_FAILED         (0x0Eu)
#define GPT_E_PARAM_CHANNEL       (0x14u)
#define GPT_E_PARAM_VALUE         (0x15u)
#define GPT_E_PARAM_POINTER       (0x16u)
#define GPT_E_PARAM_PREDEF_TIMER  (0x17u)
#define GPT_E_PARAM_MODE          (0x1Fu)

/* ---- 数据类型（SWS_Gpt §8.2） ---- */
/** Gpt_ChannelType：逻辑通道号，0..GPT_NUM_CHANNELS-1（SWS_Gpt_00358） */
typedef uint8 Gpt_ChannelType;

/** Gpt_ValueType：定时器 tick 数，宽度 = GPTMR 32 位计数器（SWS_Gpt_00359） */
typedef uint32 Gpt_ValueType;

/** Gpt_ModeType（SWS_Gpt_00360） */
typedef uint8 Gpt_ModeType;
#define GPT_MODE_NORMAL (0x00u)
#define GPT_MODE_SLEEP  (0x01u)

/** Gpt_PredefTimerType（SWS_Gpt_00389） */
typedef uint8 Gpt_PredefTimerType;
#define GPT_PREDEF_TIMER_1US_16BIT   (0x00u)
#define GPT_PREDEF_TIMER_1US_24BIT   (0x01u)
#define GPT_PREDEF_TIMER_1US_32BIT   (0x02u)
#define GPT_PREDEF_TIMER_100US_32BIT (0x03u)

/** 通道工作模式（配置参数 GptChannelMode） */
typedef uint8 Gpt_ChannelModeType;
#define GPT_CHANNEL_MODE_CONTINUOUS (0x00u)  /* GptChannelMode = GPT_CH_MODE_CONTINUOUS */
#define GPT_CHANNEL_MODE_ONESHOT    (0x01u)  /* GptChannelMode = GPT_CH_MODE_ONESHOT    */

/** 通知回调原型：对应 SWS_Gpt §8.6.3.1 的 Gpt_Notification_<channel>() */
typedef void (*Gpt_NotificationFnType)(void);

/* ---- Post-build 配置结构（SWS_Gpt §10，硬件相关） ----
 * 约束：本结构只放板级映射（实例/IRQ/时钟/模式/回调），驱动 .c 不做实例硬编码。
 */
typedef struct
{
    uint32                 base;         /* GPTMR 实例基地址（来自 Gpt_PBcfg.c）      */
    uint32                 irq;          /* 实例中断号（NVIC IRQn）                   */
    uint32                 clockName;    /* 时钟节点（clock_name_t）                  */
    uint8                  hwChannel;    /* 实例内 GPTMR 通道号（0..3）               */
    uint8                  mode;          /* Gpt_ChannelModeType                        */
    Gpt_NotificationFnType notification; /* 通知回调（弱符号，可在应用中重定义）      */
} Gpt_ChannelConfigType;

/* Predef Timer 硬件（SWS_Gpt §7.1：1us/100us 自由运行时基） */
typedef struct
{
    uint32 base;        /* GPTMR 实例基地址                     */
    uint32 clockName;   /* 时钟节点                            */
    uint8  hwChannel;   /* 自由运行通道号                      */
    uint8  ticksPerUs;  /* 每个 1us 对应的 GPTMR tick 数       */
} Gpt_PredefTimerConfigType;

typedef struct
{
    uint8                            numChannels; /* 已配置通道数（= GPT_NUM_CHANNELS） */
    const Gpt_ChannelConfigType     *channels;    /* 通道配置表                        */
    const Gpt_PredefTimerConfigType *predefTimer; /* Predef Timer 配置，NULL=不支持    */
} Gpt_ConfigType;

/* ---- API（SWS_Gpt §8.3） ---- */
void           Gpt_Init(const Gpt_ConfigType *ConfigPtr);
void           Gpt_DeInit(void);
Gpt_ValueType  Gpt_GetTimeElapsed(Gpt_ChannelType Channel);
Gpt_ValueType  Gpt_GetTimeRemaining(Gpt_ChannelType Channel);
void           Gpt_StartTimer(Gpt_ChannelType Channel, Gpt_ValueType Value);
void           Gpt_StopTimer(Gpt_ChannelType Channel);
void           Gpt_EnableNotification(Gpt_ChannelType Channel);
void           Gpt_DisableNotification(Gpt_ChannelType Channel);
void           Gpt_SetMode(Gpt_ModeType Mode);
Std_ReturnType Gpt_GetPredefTimerValue(Gpt_PredefTimerType PredefTimer, uint32 *TimeValuePtr);
void           Gpt_GetVersionInfo(Std_VersionInfoType *VersionInfoPtr);

/* ---- MCAL 内部接口：ISR 分发入口 ----
 * SDK 的向量表要求中断号以字面量形式出现在 SDK_DECLARE_EXT_ISR_M() 里，
 * 因此向量桩放在 Gpt_PBcfg.c（板级 glue），本函数由桩调用。
 */
void Gpt_IsrHandler(Gpt_ChannelType Channel);

#endif /* GPT_H */
