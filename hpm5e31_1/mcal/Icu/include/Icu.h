/*
 * Icu.h —— AUTOSAR ICU Driver 公共接口（HPM5E31 / GPTMR2 输入捕获 + TRGM）
 *
 * 依据：AUTOSAR_CP_SWS_ICUDriver（CP R25-11）§8；
 *       HPM5E31 RM GPTMR 捕获（CAPPRD/CAPDTY/CAPVALn）与 TRGM 交叉开关；
 *       本书 docs/chapters/15-gptmr.md（GPTMR 捕获实测）、17-sdm-plb-trgm.md（TRGM）。
 *
 * 例程：samples/mcal/icu_capture/。
 * 本头文件不包含任何 HPM SDK 头文件。
 */
#ifndef ICU_H
#define ICU_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Icu_Cfg.h"
#include "Mcal_ModuleId.h"

/* ---- 版本 ---- */
#define ICU_AR_RELEASE_MAJOR_VERSION    (4u)
#define ICU_AR_RELEASE_MINOR_VERSION    (4u)
#define ICU_AR_RELEASE_REVISION_VERSION (0u)
#define ICU_SW_MAJOR_VERSION            (1u)
#define ICU_SW_MINOR_VERSION            (0u)
#define ICU_SW_PATCH_VERSION            (0u)

#define ICU_VENDOR_ID                   ((uint16)0xFFFFu)

/* ---- API Service ID（SWS_Icu §8.3） ---- */
#define ICU_API_ID_INIT                 (0x00u)
#define ICU_API_ID_DEINIT               (0x01u)
#define ICU_API_ID_SET_MODE             (0x02u)
#define ICU_API_ID_DISABLE_WAKEUP       (0x03u)
#define ICU_API_ID_ENABLE_WAKEUP        (0x04u)
#define ICU_API_ID_SET_ACTIVATION       (0x05u)
#define ICU_API_ID_DISABLE_NOTIFICATION (0x06u)
#define ICU_API_ID_ENABLE_NOTIFICATION  (0x07u)
#define ICU_API_ID_GET_INPUT_STATE      (0x08u)
#define ICU_API_ID_START_TIMESTAMP      (0x09u)
#define ICU_API_ID_STOP_TIMESTAMP       (0x0Au)
#define ICU_API_ID_GET_TIMESTAMP_INDEX  (0x0Bu)
#define ICU_API_ID_GET_TIME_ELAPSED     (0x10u)
#define ICU_API_ID_GET_DUTY_CYCLE_VALUES (0x11u)
#define ICU_API_ID_GET_VERSION_INFO     (0x12u)
#define ICU_API_ID_START_SIGNAL_MEASUREMENT (0x13u)
#define ICU_API_ID_STOP_SIGNAL_MEASUREMENT  (0x14u)
#define ICU_API_ID_CHECK_WAKEUP         (0x15u)
#define ICU_API_ID_ENABLE_EDGE_DETECTION  (0x16u)
#define ICU_API_ID_DISABLE_EDGE_DETECTION (0x17u)

/* ---- 开发错误码（SWS_Icu §7.3.1） ---- */
#define ICU_E_PARAM_POINTER        (0x0Au)
#define ICU_E_PARAM_CHANNEL        (0x0Bu)
#define ICU_E_PARAM_ACTIVATION     (0x0Cu)
#define ICU_E_INIT_FAILED          (0x0Du)
#define ICU_E_PARAM_BUFFER_SIZE    (0x0Eu)
#define ICU_E_PARAM_MODE           (0x0Fu)
#define ICU_E_UNINIT               (0x14u)
#define ICU_E_NOT_STARTED          (0x15u)
#define ICU_E_BUSY_OPERATION       (0x16u)
#define ICU_E_ALREADY_INITIALIZED  (0x17u)
#define ICU_E_PARAM_NOTIFY_INTERVAL (0x18u)
#define ICU_E_PARAM_VINFO          (0x19u)

/* ---- 数据类型（SWS_Icu §8.2） ---- */
typedef uint8  Icu_ChannelType;   /* SWS_Icu_00278 */
typedef uint32 Icu_ValueType;     /* SWS_Icu_00290：捕获/计时 tick 数（GPTMR 32 位） */
typedef uint16 Icu_IndexType;     /* SWS_Icu_00292 */
typedef uint32 Icu_EdgeNumberType;/* SWS_Icu_00293 */

typedef uint8 Icu_ModeType;       /* SWS_Icu_00277 */
#define ICU_MODE_NORMAL (0x00u)
#define ICU_MODE_SLEEP  (0x01u)

typedef uint8 Icu_InputStateType; /* SWS_Icu_00279 */
#define ICU_ACTIVE (0x00u)
#define ICU_IDLE   (0x01u)

typedef uint8 Icu_ActivationType; /* SWS_Icu_00289 */
#define ICU_RISING_EDGE  (0x00u)
#define ICU_FALLING_EDGE (0x01u)
#define ICU_BOTH_EDGES   (0x02u)

typedef uint8 Icu_MeasurementModeType; /* SWS_Icu_00294 */
#define ICU_MODE_SIGNAL_EDGE_DETECT  (0x00u)
#define ICU_MODE_SIGNAL_MEASUREMENT  (0x01u)
#define ICU_MODE_TIMESTAMP           (0x02u)
#define ICU_MODE_EDGE_COUNTER        (0x03u)

typedef uint8 Icu_SignalMeasurementPropertyType; /* SWS_Icu_00295 */
#define ICU_LOW_TIME   (0x00u)
#define ICU_HIGH_TIME  (0x01u)
#define ICU_PERIOD_TIME (0x02u)
#define ICU_DUTY_CYCLE  (0x03u)

typedef uint8 Icu_TimestampBufferType; /* SWS_Icu_00296 */
#define ICU_LINEAR_BUFFER   (0x00u)
#define ICU_CIRCULAR_BUFFER (0x01u)

typedef struct
{
    Icu_ValueType ActiveTime;   /* 高电平时间（tick） */
    Icu_ValueType PeriodTime;   /* 周期（tick）       */
} Icu_DutyCycleType;            /* SWS_Icu_00291 */

typedef void (*Icu_NotificationFnType)(void);

/* ---- Post-build 配置（板级映射只放这里） ---- */
typedef struct
{
    uint32 base;            /* GPTMR 实例基地址                                  */
    uint32 irq;             /* 实例中断号                                        */
    uint32 clockName;       /* 时钟节点                                          */
    uint32 trgmBase;        /* TRGM 实例基地址                                   */
    uint32 trgmInputSrc;    /* TRGM 输入源（TRGM0_Pxx，来自捕获引脚）            */
    uint32 trgmOutputSrc;   /* TRGM 输出源（GPTMRx_CAPT_n）                      */
    uint32 iocBase;         /* IOC 实例基地址（板级参数只放 PBcfg）              */
    uint32 padIndex;        /* 捕获引脚 IOC pad 索引                             */
    uint32 padFunc;         /* 捕获引脚复用功能（TRGM_P_xx）                     */
    uint8  hwChannel;       /* GPTMR 捕获通道号                                  */
    uint8  measurementMode; /* Icu_MeasurementModeType（配置期）                 */
    uint8  signalProperty;  /* Icu_SignalMeasurementPropertyType                 */
    uint8  activation;      /* Icu_ActivationType（默认边沿）                    */
    uint8  timestampBufferType;
    Icu_NotificationFnType signalNotification;    /* Icu_SignalNotification_<ch>    */
    Icu_NotificationFnType timestampNotification; /* Icu_TimestampNotification_<ch> */
} Icu_ChannelConfigType;

typedef struct
{
    uint8 numChannels;
    const Icu_ChannelConfigType *channels;
} Icu_ConfigType;

/* ---- API（SWS_Icu §8.3） ---- */
void            Icu_Init(const Icu_ConfigType *ConfigPtr);
void            Icu_DeInit(void);
void            Icu_SetMode(Icu_ModeType Mode);
void            Icu_SetActivationCondition(Icu_ChannelType Channel, Icu_ActivationType Activation);
void            Icu_DisableNotification(Icu_ChannelType Channel);
void            Icu_EnableNotification(Icu_ChannelType Channel);
Icu_InputStateType Icu_GetInputState(Icu_ChannelType Channel);
void            Icu_StartTimestamp(Icu_ChannelType Channel, Icu_ValueType *BufferPtr, uint16 BufferSize, uint16 NotifyInterval);
void            Icu_StopTimestamp(Icu_ChannelType Channel);
Icu_IndexType   Icu_GetTimestampIndex(Icu_ChannelType Channel);
void            Icu_EnableEdgeDetection(Icu_ChannelType Channel);
void            Icu_DisableEdgeDetection(Icu_ChannelType Channel);
void            Icu_StartSignalMeasurement(Icu_ChannelType Channel);
void            Icu_StopSignalMeasurement(Icu_ChannelType Channel);
Icu_ValueType   Icu_GetTimeElapsed(Icu_ChannelType Channel);
void            Icu_GetDutyCycleValues(Icu_ChannelType Channel, Icu_DutyCycleType *DutyCycleValues);
void            Icu_GetVersionInfo(Std_VersionInfoType *versioninfo);

/* ---- 非 AUTOSAR 扩展：读最近一次捕获的原始边沿时间戳 ----
 * 用途：同一 GPTMR 实例的多个捕获通道共享同一个自由运行计数器，所以两个通道的
 * 边沿时间戳可以直接相减得到相位差（第 45 章 pwm_basic 相位测量用）。
 */
Std_ReturnType Icu_GetLastEdgeTimestamp(Icu_ChannelType Channel, Icu_ValueType *TimestampPtr);

/* ---- MCAL 内部：ISR 分发（由 Icu_PBcfg.c 的向量桩调用） ---- */
void Icu_IsrHandler(void);

#endif /* ICU_H */