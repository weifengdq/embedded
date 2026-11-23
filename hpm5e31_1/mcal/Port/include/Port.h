/*
 * Port.h —— AUTOSAR CP Port Driver 公共接口（HPM5E31 / RV32）
 *
 * 参考：AUTOSAR_CP_SWS_PortDriver (R25-11) §7.2 开发错误 / §8.2 类型 / §8.3 函数
 *       HPM5E31 RM：IOC PADx（FUNC_CTL/PAD_CTL）、GPIO OE/DO/DI
 * 例程：samples/mcal/port_pin_config（SPI1<->SPI3 杜邦线 GPIO 回环）
 *
 * 本文件只依赖 MCAL 公共头，不含 HPM SDK 头；板级映射全部在 Port_PBcfg.c。
 */
#ifndef PORT_H
#define PORT_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Mcal_ModuleId.h"
#include "Compiler.h"
#include "Port_Cfg.h"

#define PORT_AR_RELEASE_MAJOR_VERSION    (4u)
#define PORT_AR_RELEASE_MINOR_VERSION    (4u)
#define PORT_AR_RELEASE_REVISION_VERSION (0u)
#define PORT_SW_MAJOR_VERSION            (1u)
#define PORT_SW_MINOR_VERSION            (0u)
#define PORT_SW_PATCH_VERSION            (0u)
#define PORT_VENDOR_ID                   (0xFFFFu)
/* PORT_MODULE_ID（124）由 Mcal_ModuleId.h 提供 */

/* API ID（= SWS §8.3 Service ID） */
#define PORT_API_ID_INIT                 (0x00u)
#define PORT_API_ID_SET_PIN_DIRECTION    (0x01u)
#define PORT_API_ID_REFRESH_PORT_DIRECTION (0x02u)
#define PORT_API_ID_GET_VERSION_INFO     (0x03u)
#define PORT_API_ID_SET_PIN_MODE         (0x04u)

/* 开发错误码（SWS_Port_00051 §7.2） */
#define PORT_E_PARAM_PIN                 (0x0Au)
#define PORT_E_DIRECTION_UNCHANGEABLE    (0x0Bu)
#define PORT_E_INIT_FAILED               (0x0Cu)
#define PORT_E_PARAM_INVALID_MODE        (0x0Du)
#define PORT_E_MODE_UNCHANGEABLE         (0x0Eu)
#define PORT_E_UNINIT                    (0x0Fu)
#define PORT_E_PARAM_POINTER             (0x10u)
#define PORT_E_PARAM_INVALID_DIRECTION   (0x11u)

/* ---------------- 类型（SWS §8.2） ---------------- */
typedef uint16 Port_PinType;              /* SWS_Port_00229 */
typedef uint16 Port_PinModeType;          /* SWS_Port_00231 */

typedef enum
{
    PORT_PIN_IN  = 0x00u,                 /* SWS_Port_00230 */
    PORT_PIN_OUT = 0x01u
} Port_PinDirectionType;

/* 引脚模式（Port_PinModeType 实现相关取值） */
#define PORT_PIN_MODE_GPIO    ((Port_PinModeType)0u)   /* IOC ALT=0，GPIO 功能 */
#define PORT_PIN_MODE_ALT1    ((Port_PinModeType)1u)
#define PORT_PIN_MODE_ALT2    ((Port_PinModeType)2u)
#define PORT_PIN_MODE_ALT3    ((Port_PinModeType)3u)
#define PORT_PIN_MODE_ALT4    ((Port_PinModeType)4u)
#define PORT_PIN_MODE_ANALOG  ((Port_PinModeType)0x1FFu) /* IOC FUNC_CTL.ANALOG=1 */

/* 内部上下拉配置 */
#define PORT_PIN_PULL_NONE    (0u)
#define PORT_PIN_PULL_UP      (1u)
#define PORT_PIN_PULL_DOWN    (2u)

/* post-build 单引脚配置（板级映射表的一行） */
typedef struct
{
    uint32 iocBase;          /* IOC 基地址（寄存器 PAD[padIndex]） */
    uint32 gpioBase;         /* GPIO 控制器基地址 */
    uint16 padIndex;         /* IOC PAD 索引：PA00=0, PB00=32, PC00=64, PD00=96, PE00=128, PF00=160 */
    uint8  gpioPort;         /* GPIO 端口索引：A=0 ... F=5 */
    uint8  gpioPin;          /* 0..31 */
    uint8  direction;        /* Port_PinDirectionType */
    uint8  mode;             /* Port_PinModeType */
    uint8  pull;             /* PORT_PIN_PULL_xxx */
    uint8  openDrain;        /* 0/1 */
    uint8  initialLevel;     /* 输出初值 0/1（SWS_Port_00055：先定电平再切方向） */
    uint8  directionChangeable; /* 1 = 允许 Port_SetPinDirection 运行时改 */
    uint8  modeChangeable;   /* 1 = 允许 Port_SetPinMode 运行时改 */
    uint8  driveStrength;    /* IOC PAD_CTL.DS 0..7 */
    uint8  slewRate;         /* IOC PAD_CTL.SR 0=slow 1=fast */
    uint8  speed;            /* IOC PAD_CTL.SPD 0..3 */
    uint8  hysteresis;       /* IOC PAD_CTL.HYS 0/1 */
} Port_PinConfigType;

typedef struct
{
    const Port_PinConfigType *pins;
    uint16 pinCount;
} Port_ConfigType;

/* post-build 配置实例（定义在 Port_PBcfg.c，const） */
extern const Port_ConfigType Port_Config;

/* ---------------- API（SWS §8.3） ---------------- */
void Port_Init(const Port_ConfigType *ConfigPtr);
void Port_SetPinDirection(Port_PinType Pin, Port_PinDirectionType Direction);
void Port_RefreshPortDirection(void);
void Port_GetVersionInfo(Std_VersionInfoType *versioninfo);
void Port_SetPinMode(Port_PinType Pin, Port_PinModeType Mode);

/* ---------------- 本实现扩展（非 AUTOSAR API） ---------------- */
uint16 Port_GetPinCount(void);
const Port_PinConfigType *Port_GetPinConfig(Port_PinType Pin);
boolean Port_IsInitialized(void);
/* 运行时改内部上下拉（Port 例程做上拉/下拉验证用；AUTOSAR 里属于配置项） */
void Port_SetPinPull(Port_PinType Pin, uint8 Pull);

#endif /* PORT_H */
