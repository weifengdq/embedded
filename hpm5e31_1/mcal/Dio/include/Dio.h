/*
 * Dio.h —— AUTOSAR CP Dio Driver 公共接口（HPM5E31 / RV32）
 *
 * 参考：AUTOSAR_CP_SWS_DIODriver (R25-11) §7.6 开发错误 / §8.2 类型 / §8.3 函数
 *       HPM5E31 RM：GPIO DI/DO/OE 寄存器
 * 例程：samples/mcal/dio_led_button
 *
 * AUTOSAR 的 Dio 没有 Dio_Init/Dio_DeInit：引脚方向/模式由 Port 模块负责，
 * Dio 只做电平读写。本实现把"通道 -> GPIO 端口/位"的映射放在 Dio_PBcfg.c，
 * 采用 VARIANT-PRE-COMPILE 语义（通道号在编译期固定），因此也不需要 Init。
 *
 * 本文件只依赖 MCAL 公共头，不含 HPM SDK 头。
 */
#ifndef DIO_H
#define DIO_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Mcal_ModuleId.h"
#include "Compiler.h"
#include "Dio_Cfg.h"

#define DIO_AR_RELEASE_MAJOR_VERSION    (4u)
#define DIO_AR_RELEASE_MINOR_VERSION    (4u)
#define DIO_AR_RELEASE_REVISION_VERSION (0u)
#define DIO_SW_MAJOR_VERSION            (1u)
#define DIO_SW_MINOR_VERSION            (0u)
#define DIO_SW_PATCH_VERSION            (0u)
#define DIO_VENDOR_ID                   (0xFFFFu)
/* DIO_MODULE_ID（120）由 Mcal_ModuleId.h 提供 */

/* API ID（= SWS §8.3 Service ID） */
#define DIO_API_ID_READ_CHANNEL        (0x00u)
#define DIO_API_ID_WRITE_CHANNEL       (0x01u)
#define DIO_API_ID_READ_PORT           (0x02u)
#define DIO_API_ID_WRITE_PORT          (0x03u)
#define DIO_API_ID_READ_CHANNEL_GROUP  (0x04u)
#define DIO_API_ID_WRITE_CHANNEL_GROUP (0x05u)
#define DIO_API_ID_FLIP_CHANNEL        (0x11u)
#define DIO_API_ID_GET_VERSION_INFO    (0x12u)
#define DIO_API_ID_MASKED_WRITE_PORT   (0x13u)

/* 开发错误码（SWS_Dio_91001 §7.6.1） */
#define DIO_E_PARAM_INVALID_CHANNEL_ID (0x0Au)
#define DIO_E_PARAM_INVALID_PORT_ID    (0x14u)
#define DIO_E_PARAM_INVALID_GROUP      (0x1Fu)
#define DIO_E_PARAM_POINTER            (0x20u)

/* ---------------- 类型（SWS §8.2） ---------------- */
typedef uint16 Dio_ChannelType;        /* SWS_Dio_00182 */
typedef uint16 Dio_PortType;           /* SWS_Dio_00183 */
typedef uint8  Dio_LevelType;          /* SWS_Dio_00185（STD_LOW/STD_HIGH） */
typedef uint32 Dio_PortLevelType;      /* SWS_Dio_00186（本 MCU 端口 32 位） */

typedef struct
{
    Dio_PortType port;                 /* GPIO 端口索引 A=0..F=5 */
    uint8        offset;               /* 组在端口内的起始位 */
    uint32       mask;                 /* 组内有效位（相对 offset 之前的原始掩码） */
} Dio_ChannelGroupType;                /* SWS_Dio_00184 */

/* post-build 通道表的一行 */
typedef struct
{
    uint8 gpioPort;                    /* GPIO 端口索引 */
    uint8 gpioPin;                     /* 位号 0..31 */
} Dio_ChannelConfigType;

typedef struct
{
    uint32 gpioBase;                   /* GPIO 控制器基地址（Dio_PBcfg.c 给出） */
    const Dio_ChannelConfigType *channels;
    uint16 channelCount;
    const Dio_ChannelGroupType *groups;
    uint16 groupCount;
} Dio_ConfigType;

/* post-build 配置实例（定义在 Dio_PBcfg.c，const） */
extern const Dio_ConfigType Dio_Config;

/* ---------------- API（SWS §8.3） ---------------- */
Dio_LevelType      Dio_ReadChannel(Dio_ChannelType ChannelId);
void               Dio_WriteChannel(Dio_ChannelType ChannelId, Dio_LevelType Level);
Dio_PortLevelType  Dio_ReadPort(Dio_PortType PortId);
void               Dio_WritePort(Dio_PortType PortId, Dio_PortLevelType Level);
Dio_PortLevelType  Dio_ReadChannelGroup(const Dio_ChannelGroupType *ChannelGroupIdPtr);
void               Dio_WriteChannelGroup(const Dio_ChannelGroupType *ChannelGroupIdPtr, Dio_PortLevelType Level);
void               Dio_GetVersionInfo(Std_VersionInfoType *versioninfo);
Dio_LevelType      Dio_FlipChannel(Dio_ChannelType ChannelId);
void               Dio_MaskedWritePort(Dio_PortType PortId, Dio_PortLevelType Level, Dio_PortLevelType Mask);

/* ---------------- 本实现扩展（非 AUTOSAR API） ---------------- */
uint16 Dio_GetChannelCount(void);
uint16 Dio_GetGroupCount(void);
const Dio_ChannelGroupType *Dio_GetChannelGroup(uint16 index);

#endif /* DIO_H */
