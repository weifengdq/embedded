/*
 * Pwm.h —— AUTOSAR PWM Driver 公共接口（HPM5E31 / PWM0 PWMv2）
 *
 * 依据：AUTOSAR_CP_SWS_PWMDriver（CP R25-11）§8 "API specification"、
 *       §8.3 Service ID、§7.3.1 开发错误表；
 *       HPM5E31 RM 第 37 章 PWMv2（影子寄存器/计数器/比较器）；
 *       本书 docs/chapters/14-pwmv2-hrpwm.md（PWMv2 寄存器级实测与 E00038 勘误）。
 *
 * 例程：samples/mcal/pwm_basic/、samples/mcal/pwm_notify/。
 * 本头文件不含任何 HPM SDK 头文件。
 */
#ifndef PWM_H
#define PWM_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Pwm_Cfg.h"
#include "Mcal_ModuleId.h"

/* ---- 版本 ---- */
#define PWM_AR_RELEASE_MAJOR_VERSION    (4u)
#define PWM_AR_RELEASE_MINOR_VERSION    (4u)
#define PWM_AR_RELEASE_REVISION_VERSION (0u)
#define PWM_SW_MAJOR_VERSION            (1u)
#define PWM_SW_MINOR_VERSION            (0u)
#define PWM_SW_PATCH_VERSION            (0u)

#define PWM_VENDOR_ID                   ((uint16)0xFFFFu)

/* ---- API Service ID（SWS_Pwm §8.3） ---- */
#define PWM_API_ID_INIT                 (0x00u)
#define PWM_API_ID_DEINIT               (0x01u)
#define PWM_API_ID_SET_DUTY_CYCLE       (0x02u)
#define PWM_API_ID_SET_PERIOD_AND_DUTY  (0x03u)
#define PWM_API_ID_SET_OUTPUT_TO_IDLE   (0x04u)
#define PWM_API_ID_GET_OUTPUT_STATE     (0x05u)
#define PWM_API_ID_DISABLE_NOTIFICATION (0x06u)
#define PWM_API_ID_ENABLE_NOTIFICATION  (0x07u)
#define PWM_API_ID_GET_VERSION_INFO     (0x08u)
#define PWM_API_ID_SET_CHANNEL_CLASS    (0x1Fu)  /* 非 SWS 标准服务，见第 45 章说明 */

/* ---- 开发错误码（SWS_Pwm §7.3.1） ---- */
#define PWM_E_INIT_FAILED               (0x10u)
#define PWM_E_UNINIT                    (0x11u)
#define PWM_E_PARAM_CHANNEL             (0x12u)
#define PWM_E_PERIOD_UNCHANGEABLE       (0x13u)
#define PWM_E_ALREADY_INITIALIZED       (0x14u)
#define PWM_E_PARAM_POINTER             (0x15u)
#define PWM_E_NOT_DISENGAGED            (0x16u)
#define PWM_E_POWER_STATE_NOT_SUPPORTED (0x17u)
#define PWM_E_TRANSITION_NOT_POSSIBLE   (0x18u)
#define PWM_E_PERIPHERAL_NOT_PREPARED   (0x19u)
/* 占空比超出 0..0x8000：SWS 未单列，复用通用 DET_E_PARAM_VALUE(0x02) */

/* ---- 数据类型（SWS_Pwm §8.2） ---- */
typedef uint8  Pwm_ChannelType;    /* SWS_Pwm_00106 */
typedef uint32 Pwm_PeriodType;     /* SWS_Pwm_00107，单位 = PWM 计数器 tick        */

typedef uint8 Pwm_OutputStateType; /* SWS_Pwm_00108 */
#define PWM_HIGH (0x00u)
#define PWM_LOW  (0x01u)

typedef uint8 Pwm_EdgeNotificationType; /* SWS_Pwm_00109 */
#define PWM_RISING_EDGE  (0x00u)
#define PWM_FALLING_EDGE (0x01u)
#define PWM_BOTH_EDGES   (0x02u)

typedef uint8 Pwm_ChannelClassType;     /* SWS_Pwm_00110 */
#define PWM_VARIABLE_PERIOD       (0x00u)
#define PWM_FIXED_PERIOD          (0x01u)
#define PWM_FIXED_PERIOD_SHIFTED  (0x02u)

/* 占空比标度：0x0000 = 0%，0x8000 = 100%（SWS_Pwm §8.3.3） */
#define PWM_DUTY_0_PERCENT   (0x0000u)
#define PWM_DUTY_100_PERCENT (0x8000u)

typedef void (*Pwm_NotificationFnType)(void);

/* ---- Post-build 配置（板级映射只放这里，驱动 .c 不硬编码） ---- */
typedef struct
{
    uint32 base;          /* PWMv2 实例基地址                                  */
    uint32 clockName;     /* 时钟节点                                          */
    uint32 iocBase;       /* IOC 实例基地址（HPM_IOC，板级参数只放 PBcfg）     */
    uint32 padIndex;      /* IOC 内部 pad 索引（IOC->PAD[] 的下标）            */
    uint32 padFunc;       /* IOC pad 复用功能（PWM0_P_n）                      */
    uint32 padCtl;        /* IOC pad 电气属性（PAD_CTL）                       */
    uint8  hwChannel;     /* PWMv2 输出通道（pwm_channel_t）                   */
    uint8  counter;       /* 该输出所属计数器（pwm_counter_t）                 */
    uint8  cmpA;          /* 上升沿比较器索引                                  */
    uint8  cmpB;          /* 下降沿比较器索引                                  */
    uint8  shadowA;       /* 上升沿影子寄存器索引                              */
    uint8  shadowB;       /* 下降沿影子寄存器索引                              */
    uint8  idleState;     /* Pwm_SetOutputToIdle 后的电平：PWM_HIGH / PWM_LOW   */
    uint8  channelClass;  /* Pwm_ChannelClassType                              */
    uint16 defaultDuty;   /* 初始占空比（0..0x8000）                            */
    uint16 phasePermille; /* 相位偏移（千分比，0..1000），SWS 配置参数 Phase   */
    uint32 defaultPeriod; /* 初始周期（计数器 tick）                            */
    Pwm_NotificationFnType notification; /* 通知回调                         */
} Pwm_ChannelConfigType;

typedef struct
{
    uint32 irq;                              /* 实例中断号                    */
    uint8  numChannels;                      /* 配置通道数                    */
    const Pwm_ChannelConfigType *channels;   /* 通道配置表                    */
} Pwm_ConfigType;

/* ---- API（SWS_Pwm §8.3） ---- */
void              Pwm_Init(const Pwm_ConfigType *ConfigPtr);
void              Pwm_DeInit(void);
void              Pwm_SetDutyCycle(Pwm_ChannelType ChannelNumber, uint16 DutyCycle);
void              Pwm_SetPeriodAndDuty(Pwm_ChannelType ChannelNumber, Pwm_PeriodType Period, uint16 DutyCycle);
void              Pwm_SetOutputToIdle(Pwm_ChannelType ChannelNumber);
Pwm_OutputStateType Pwm_GetOutputState(Pwm_ChannelType ChannelNumber);
void              Pwm_DisableNotification(Pwm_ChannelType ChannelNumber);
void              Pwm_EnableNotification(Pwm_ChannelType ChannelNumber, Pwm_EdgeNotificationType Notification);
void              Pwm_GetVersionInfo(Std_VersionInfoType *versioninfo);

/* 任务书要求的扩展服务（SWS_PWM R25-11 §8.3 无此 API，见第 45 章"待确认项与风险"） */
Std_ReturnType    Pwm_SetChannelClass(Pwm_ChannelType ChannelNumber, Pwm_ChannelClassType ChannelClass);

/* ---- MCAL 内部：ISR 分发（由 Pwm_PBcfg.c 的向量桩调用） ---- */
void Pwm_IsrHandler(void);

#endif /* PWM_H */