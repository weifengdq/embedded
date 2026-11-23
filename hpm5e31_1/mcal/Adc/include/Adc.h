/*
 * Adc.h —— AUTOSAR CP ADC Driver 公共接口（HPM5E31 / RV32，ADC16）
 *
 * 参考：AUTOSAR_CP_SWS_ADCDriver R21-11 §7.5 错误 / §8.2 类型 / §8.3 函数
 *       HPM5E31 RM：ADC16（CONV_CFG1/ADC_CFG0/BUF_CFG0/ANA_CTRL0/SAMPLE_CFG/BUS_RESULT/ADC16_PARAMS）
 * 例程：samples/mcal/adc_single、samples/mcal/adc_bench
 *
 * 本文件只依赖 MCAL 公共头，不含 HPM SDK 头；ADC 实例/通道/引脚/时钟参数全部在 Adc_PBcfg.c。
 */
#ifndef ADC_H
#define ADC_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Mcal_ModuleId.h"
#include "Compiler.h"
#include "Adc_Cfg.h"

#define ADC_AR_RELEASE_MAJOR_VERSION    (4u)
#define ADC_AR_RELEASE_MINOR_VERSION    (4u)
#define ADC_AR_RELEASE_REVISION_VERSION (0u)
#define ADC_SW_MAJOR_VERSION            (1u)
#define ADC_SW_MINOR_VERSION            (0u)
#define ADC_SW_PATCH_VERSION            (0u)
#define ADC_VENDOR_ID                   (0xFFFFu)
/* ADC_MODULE_ID（123）由 Mcal_ModuleId.h 提供 */

/* API ID（= SWS §8.3 Service ID） */
#define ADC_API_ID_INIT                    (0x00u)
#define ADC_API_ID_DEINIT                  (0x01u)
#define ADC_API_ID_START_GROUP_CONVERSION  (0x02u)
#define ADC_API_ID_STOP_GROUP_CONVERSION   (0x03u)
#define ADC_API_ID_READ_GROUP              (0x04u)
#define ADC_API_ID_ENABLE_HW_TRIGGER       (0x05u)
#define ADC_API_ID_DISABLE_HW_TRIGGER      (0x06u)
#define ADC_API_ID_ENABLE_GROUP_NOTIFICATION  (0x07u)
#define ADC_API_ID_DISABLE_GROUP_NOTIFICATION (0x08u)
#define ADC_API_ID_GET_GROUP_STATUS        (0x09u)
#define ADC_API_ID_GET_VERSION_INFO        (0x0Au)
#define ADC_API_ID_GET_STREAM_LAST_POINTER (0x0Bu)
#define ADC_API_ID_SETUP_RESULT_BUFFER     (0x0Cu)

/* 开发/运行时错误码（SWS_Adc_91005 / SWS_Adc_91006） */
#define ADC_E_UNINIT                  (0x0Au)
#define ADC_E_BUSY                    (0x0Bu)
#define ADC_E_IDLE                    (0x0Cu)
#define ADC_E_ALREADY_INITIALIZED     (0x0Du)
#define ADC_E_PARAM_POINTER           (0x14u)
#define ADC_E_PARAM_GROUP             (0x15u)
#define ADC_E_WRONG_CONV_MODE         (0x16u)
#define ADC_E_WRONG_TRIGG_SRC         (0x17u)
#define ADC_E_NOTIF_CAPABILITY        (0x18u)
#define ADC_E_BUFFER_UNINIT           (0x19u)   /* SWS 原文拼写 ADE_E_BUFFER_UNINIT，这里用 ADC_ 前缀 */
#define ADC_E_NOT_DISENGAGED          (0x1Au)

/* ---------------- 类型（SWS §8.2） ---------------- */
typedef uint8  Adc_ChannelType;        /* SWS_Adc_00506 */
typedef uint8  Adc_GroupType;          /* SWS_Adc_00507 */
typedef uint16 Adc_ValueGroupType;     /* SWS_Adc_00508：16 bit 右对齐 */
typedef uint8  Adc_PrescaleType;       /* SWS_Adc_00509 */
typedef uint16 Adc_ConversionTimeType; /* SWS_Adc_00510 */
typedef uint16 Adc_SamplingTimeType;   /* SWS_Adc_00511 */
typedef uint8  Adc_ResolutionType;     /* SWS_Adc_00512 */
typedef uint8  Adc_GroupPriorityType;  /* SWS_Adc_00516 */
typedef uint16 Adc_GroupDefType;       /* SWS_Adc_00517 */
typedef uint8  Adc_StreamNumSampleType;/* SWS_Adc_00518 */

typedef enum
{
    ADC_IDLE              = 0x00u,     /* SWS_Adc_00513 */
    ADC_BUSY              = 0x01u,
    ADC_COMPLETED         = 0x02u,
    ADC_STREAM_COMPLETED  = 0x03u
} Adc_StatusType;

typedef enum
{
    ADC_TRIGG_SRC_SW = 0x00u,          /* SWS_Adc_00514 */
    ADC_TRIGG_SRC_HW = 0x01u
} Adc_TriggerSourceType;

typedef enum
{
    ADC_CONV_MODE_ONESHOT    = 0x00u,  /* SWS_Adc_00515 */
    ADC_CONV_MODE_CONTINUOUS = 0x01u
} Adc_GroupConvModeType;

typedef enum
{
    ADC_STREAM_BUFFER_LINEAR   = 0x00u,/* SWS_Adc_00519 */
    ADC_STREAM_BUFFER_CIRCULAR = 0x01u
} Adc_StreamBufferModeType;

typedef enum
{
    ADC_ACCESS_MODE_SINGLE    = 0x00u, /* SWS_Adc_00528 */
    ADC_ACCESS_MODE_STREAMING = 0x01u
} Adc_GroupAccessModeType;

/* ---------------- 硬件相关配置（post-build，实例在 Adc_PBcfg.c） ---------------- */
typedef struct
{
    uint8  group;              /* Adc_GroupType */
    uint8  channel;            /* ADC16 通道号（PF26 = ADC0.IN01） */
    uint8  triggerSource;      /* Adc_TriggerSourceType */
    uint8  convMode;           /* Adc_GroupConvModeType */
    uint8  accessMode;         /* Adc_GroupAccessModeType */
    uint8  resolution;         /* Adc_ResolutionType（本板 16） */
    uint8  streamBufferMode;   /* Adc_StreamBufferModeType */
    uint8  channelCount;       /* 组内通道数（本板 1） */
    uint16 samplingTime;       /* 采样保持周期数（ADC 时钟） */
    uint16 streamSamples;      /* 每组每通道采样数（单次访问=1） */
    uint16 padIndex;           /* IOC PAD 索引（模拟模式由 Adc_Init 配置） */
    void (*notification)(void);/* 组转换完成通知（NULL = 无通知能力） */
} Adc_GroupConfigType;

typedef struct
{
    uint32 adcBase;            /* ADC16 实例基地址 */
    uint32 sysctlBase;         /* SYSCTL 基地址（ADC 时钟门控/时钟源） */
    uint32 iocBase;            /* IOC 基地址（模拟引脚 FUNC_CTL.ANALOG） */
    uint16 adcClockNode;       /* SYSCTL.ADCCLK 索引 */
    uint16 adcClockSrc;        /* 0 = ahb0 */
    uint32 adcBusHz;           /* ADC 总线时钟（ahb0） */
    uint8  clockDivider;       /* 转换时钟分频：conv_clk = adcBusHz / divider（<= 50 MHz） */
    uint8  groupCount;
    uint16 reserved0;
    const Adc_GroupConfigType *groups;
} Adc_ConfigType;

/* post-build 配置实例（定义在 Adc_PBcfg.c，const） */
extern const Adc_ConfigType Adc_Config;

/* ---------------- API（SWS §8.3） ---------------- */
void Adc_Init(const Adc_ConfigType *ConfigPtr);
void Adc_DeInit(void);
Std_ReturnType Adc_SetupResultBuffer(Adc_GroupType Group, Adc_ValueGroupType *DataBufferPtr);
void Adc_StartGroupConversion(Adc_GroupType Group);
void Adc_StopGroupConversion(Adc_GroupType Group);
Std_ReturnType Adc_ReadGroup(Adc_GroupType Group, Adc_ValueGroupType *DataBufferPtr);
void Adc_EnableHardwareTrigger(Adc_GroupType Group);
void Adc_DisableHardwareTrigger(Adc_GroupType Group);
void Adc_EnableGroupNotification(Adc_GroupType Group);
void Adc_DisableGroupNotification(Adc_GroupType Group);
Adc_StatusType Adc_GetGroupStatus(Adc_GroupType Group);
Adc_StreamNumSampleType Adc_GetStreamLastPointer(Adc_GroupType Group, Adc_ValueGroupType **PtrToSamplePtr);
void Adc_GetVersionInfo(Std_VersionInfoType *versioninfo);

/* ---------------- 本实现扩展（非 AUTOSAR API） ---------------- */
uint32 Adc_GetConvClockHz(void);          /* 实际转换时钟（adcBusHz/divider） */
uint32 Adc_GetGroupSampleCount(Adc_GroupType Group); /* 连续组累计完成采样数 */
boolean Adc_IsInitialized(void);

#endif /* ADC_H */
