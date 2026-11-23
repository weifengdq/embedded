/*
 * Mcu.h —— AUTOSAR CP MCU Driver 公共接口（HPM5E31 / RV32）
 *
 * 参考：AUTOSAR_CP_SWS_MCUDriver (R25-11) §8.2 类型 / §8.3 函数 / §7.2.1 开发错误
 *       HPM5E31 RM：PLLCTLV2（PLL 倍频/分频）、SYSCTL（时钟节点 mux/div）、PPOR（复位源/软复位）
 * 例程：samples/mcal/mcu_clock_init、samples/mcal/mcu_reset_mode
 *
 * 本文件只依赖 MCAL 公共头（Std_Types/Mcal_Cfg/Mcal_ModuleId），不含任何 HPM SDK 头。
 * 板级实例/时钟参数全部来自 Mcu_PBcfg.c，改板只改那里。
 */
#ifndef MCU_H
#define MCU_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Mcal_ModuleId.h"
#include "Compiler.h"
#include "Mcu_Cfg.h"

/* ---------------- 版本号（AUTOSAR 模块版本 + 软件版本） ---------------- */
#define MCU_AR_RELEASE_MAJOR_VERSION        (4u)
#define MCU_AR_RELEASE_MINOR_VERSION        (4u)
#define MCU_AR_RELEASE_REVISION_VERSION     (0u)
#define MCU_SW_MAJOR_VERSION                (1u)
#define MCU_SW_MINOR_VERSION                (0u)
#define MCU_SW_PATCH_VERSION                (0u)
#define MCU_VENDOR_ID                       (0xFFFFu)
/* MCU_MODULE_ID（101）由 Mcal_ModuleId.h 提供，Det 上报必须用它 */

/* ---------------- API ID（= SWS §8.3 的 Service ID） ---------------- */
#define MCU_API_ID_MCU_INIT                 (0x00u)
#define MCU_API_ID_MCU_INITRAM_SECTION      (0x01u)
#define MCU_API_ID_MCU_INIT_CLOCK           (0x02u)
#define MCU_API_ID_MCU_DISTRIBUTE_PLL_CLOCK (0x03u)
#define MCU_API_ID_MCU_GET_PLL_STATUS       (0x04u)
#define MCU_API_ID_MCU_GET_RESET_REASON     (0x05u)
#define MCU_API_ID_MCU_GET_RESET_RAW_VALUE  (0x06u)
#define MCU_API_ID_MCU_PERFORM_RESET        (0x07u)
#define MCU_API_ID_MCU_SET_MODE             (0x08u)
#define MCU_API_ID_MCU_GET_VERSION_INFO     (0x09u)
#define MCU_API_ID_MCU_GET_RAM_STATE        (0x0Au)

/* 兼容 NXP/Infineon 命名习惯的别名（评审/工具链常用） */
#define MCU_INIT_API_ID                     MCU_API_ID_MCU_INIT
#define MCU_INIT_CLOCK_API_ID               MCU_API_ID_MCU_INIT_CLOCK
#define MCU_DISTRIBUTE_PLL_CLOCK_API_ID     MCU_API_ID_MCU_DISTRIBUTE_PLL_CLOCK
#define MCU_GET_PLL_STATUS_API_ID           MCU_API_ID_MCU_GET_PLL_STATUS
#define MCU_GET_RESET_REASON_API_ID         MCU_API_ID_MCU_GET_RESET_REASON
#define MCU_GET_RESET_RAW_VALUE_API_ID      MCU_API_ID_MCU_GET_RESET_RAW_VALUE
#define MCU_PERFORM_RESET_API_ID            MCU_API_ID_MCU_PERFORM_RESET
#define MCU_SET_MODE_API_ID                 MCU_API_ID_MCU_SET_MODE
#define MCU_GET_VERSION_INFO_API_ID         MCU_API_ID_MCU_GET_VERSION_INFO

/* ---------------- 开发错误码（SWS_Mcu_00012 §7.2.1） ---------------- */
#define MCU_E_PARAM_CONFIG                  (0x0Au)
#define MCU_E_PARAM_CLOCK                   (0x0Bu)
#define MCU_E_PARAM_MODE                    (0x0Cu)
#define MCU_E_PARAM_RAMSECTION              (0x0Du)
#define MCU_E_PLL_NOT_LOCKED                (0x0Eu)
#define MCU_E_UNINIT                        (0x0Fu)
#define MCU_E_PARAM_POINTER                 (0x10u)
#define MCU_E_INIT_FAILED                   (0x11u)

/* ---------------- 类型（SWS §8.2） ---------------- */
typedef uint8 Mcu_ClockType;         /* SWS_Mcu_00251/00233：uint8 */
typedef uint8 Mcu_ModeType;          /* SWS_Mcu_00254/00238 */
typedef uint32 Mcu_RawResetType;     /* SWS_Mcu_00253/00236 */

typedef enum
{
    MCU_PLL_LOCKED          = 0x00u,  /* SWS_Mcu_00250 */
    MCU_PLL_UNLOCKED        = 0x01u,
    MCU_PLL_STATUS_UNDEFINED = 0x02u
} Mcu_PllStatusType;

typedef enum
{
    MCU_POWER_ON_RESET = 0x00u,       /* SWS_Mcu_00252 */
    MCU_WATCHDOG_RESET = 0x01u,
    MCU_SW_RESET       = 0x02u,
    MCU_RESET_UNDEFINED = 0x03u
} Mcu_ResetType;

typedef enum
{
    MCU_RAMSTATE_INVALID = 0x00u,     /* SWS_Mcu_00256（本轮不实现，见 Mcu_Cfg.h） */
    MCU_RAMSTATE_VALID   = 0x01u
} Mcu_RamStateType;

/* Mcu_SetMode 的模式 ID（SWS_Mcu_00254：由配置决定；本板 3 个） */
#define MCU_MODE_RUN        ((Mcu_ModeType)0u)
#define MCU_MODE_SLEEP      ((Mcu_ModeType)1u)
#define MCU_MODE_RESET      ((Mcu_ModeType)2u)
#define MCU_MODE_COUNT      (3u)

/* 时钟设置 ID（Mcu_InitClock 的入参，SWS_Mcu_00251） */
#define MCU_CLOCK_SETTING_0 ((Mcu_ClockType)0u)   /* 480 MHz：PLL0 960 MHz / 2 */
#define MCU_CLOCK_SETTING_1 ((Mcu_ClockType)1u)   /* 240 MHz：CPU 节点 /2 */
#define MCU_CLOCK_SETTING_COUNT (2u)

/* ---------------- 硬件相关配置类型（post-build，实例在 Mcu_PBcfg.c） ----------------
 * 说明：公共头不能 include HPM SDK，所以基地址用 uint32 地址值保存，
 *       .c 内再 cast 成 PPOR/SYSCTL/PLLCTLV2 的寄存器结构体指针。
 */
typedef struct
{
    uint32 iocBase;      /* 保留：时钟门控需要时使用，本轮未用 */
    uint32 pllBase;      /* PLLCTLV2 基地址 */
    uint16 pllIndex;     /* 0=PLL0 1=PLL1 2=PLL2 */
    uint16 mfi;          /* 倍频整数部分（MFI，16..42） */
    uint32 mfn;          /* 倍频小数分子（MFN） */
    uint32 mfd;          /* 倍频小数分母（MFD，默认 240000000） */
    uint8  clk0PostDiv;  /* DIV[0] 原始值：分频 = DIV/5 + 1（pllctlv2_div_2p0 = 5） */
    uint8  clk1PostDiv;  /* DIV[1] 原始值 */
    uint8  reserved0;
    uint16 lockTimeoutUs;/* 等 PLL 锁定的超时（us，按 mchtmr 24 MHz 计时） */
} Mcu_PllConfigType;

typedef struct
{
    uint32 sysctlBase;   /* SYSCTL 基地址 */
    uint8  node;         /* SYSCTL 时钟节点号（cpu0=0, mchtmr0=1, ahb0=2 ...） */
    uint8  src;          /* 0=osc24m, 1=pll0_clk0, 2=pll0_clk1, 3=pll1_clk0 ... 7=pll2_clk1 */
    uint8  divMinusOne;  /* 分频寄存器原值：实际分频 = 值 + 1 */
    uint8  reserved0;
} Mcu_ClockNodeConfigType;

typedef struct
{
    const Mcu_PllConfigType        *pll;        /* 该设置下的 PLL 参数 */
    const Mcu_ClockNodeConfigType  *clockNodes; /* 该设置下要写入的时钟节点表 */
    uint8                           clockNodeCount;
    uint8                           reserved0;
} Mcu_ClockSettingConfigType;

typedef struct
{
    uint32 pporBase;     /* PPOR 基地址（复位源/软复位） */
    uint32 sysctlBase;   /* SYSCTL 基地址 */
    uint32 mchtmrBase;   /* MCHTMR 基地址（24 MHz，用于超时计时） */
    uint32 oscFreqHz;    /* 晶振频率（24 MHz） */
    uint8  coreClockNode;/* CPU0 所在 SYSCTL 时钟节点（Mcu_GetCoreClockHz 用） */
    uint8  resetDelayTicks; /* Mcu_PerformReset 的 PPOR SOFTWARE_RESET 计数（24 MHz 拍） */
    uint8  modeCount;    /* Mcu_SetMode 支持的合法模式个数 */
    uint8  reserved0;
    const Mcu_ClockSettingConfigType *clockSettings;
    uint8  clockSettingCount;
    uint8  reserved1[3];
} Mcu_ConfigType;

/* post-build 配置实例（定义在 Mcu_PBcfg.c，const） */
extern const Mcu_ConfigType Mcu_Config;

/* ---------------- API（SWS §8.3） ---------------- */
void            Mcu_Init(const Mcu_ConfigType *ConfigPtr);
Std_ReturnType  Mcu_InitRamSection(uint16 RamSection);           /* 可选，本板不实现 */
Std_ReturnType  Mcu_InitClock(Mcu_ClockType ClockSetting);
Std_ReturnType  Mcu_DistributePllClock(void);
Mcu_PllStatusType Mcu_GetPllStatus(void);
Mcu_ResetType   Mcu_GetResetReason(void);
Mcu_RawResetType Mcu_GetResetRawValue(void);
void            Mcu_PerformReset(void);
void            Mcu_SetMode(Mcu_ModeType McuMode);
void            Mcu_GetVersionInfo(Std_VersionInfoType *versioninfo);
Mcu_RamStateType Mcu_GetRamState(void);                          /* 可选，本板不实现 */

/* ---------------- 本实现扩展（非 AUTOSAR API） ---------------- */
uint32          Mcu_GetCoreClockHz(void);    /* 由寄存器实时换算 CPU0 频率 */
uint32          Mcu_GetClockNodeHz(uint8 node); /* 任意 SYSCTL 时钟节点频率 */
void            Mcu_ClearResetFlags(void);   /* 写 1 清除 PPOR RESET_FLAG */
boolean         Mcu_IsInitialized(void);

#endif /* MCU_H */
