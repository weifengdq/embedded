/*
 * Mcu_Cfg.h —— MCU 模块预编译配置（pre-compile，AUTOSAR ECUC 对应项见各宏注释）
 *
 * 参考：AUTOSAR_CP_SWS_MCUDriver §10（配置参数）；例程 samples/mcal/mcu_clock_init
 */
#ifndef MCU_CFG_H
#define MCU_CFG_H

#include "Mcal_Cfg.h"

/* McuDevErrorDetect（默认跟全局 MCAL_DEV_ERROR_DETECT） */
#ifndef MCU_DEV_ERROR_DETECT
#define MCU_DEV_ERROR_DETECT        MCAL_DEV_ERROR_DETECT
#endif

/* McuInitClock：是否提供 Mcu_InitClock（SWS_Mcu_00210） */
#ifndef MCU_INIT_CLOCK_API
#define MCU_INIT_CLOCK_API          STD_ON
#endif

/* McuNoPll：FALSE 表示有 PLL，需要 Mcu_DistributePllClock（SWS_Mcu_00205） */
#ifndef MCU_NO_PLL
#define MCU_NO_PLL                  STD_OFF
#endif

/* McuPerformResetApi：是否提供 Mcu_PerformReset（SWS_Mcu_00146） */
#ifndef MCU_PERFORM_RESET_API
#define MCU_PERFORM_RESET_API       STD_ON
#endif

/* McuGetRamStateApi：HPM5E31 RAM 无"内容有效"标志，置 FALSE（SWS_Mcu_00209） */
#ifndef MCU_GET_RAM_STATE_API
#define MCU_GET_RAM_STATE_API       STD_OFF
#endif

/* McuInitRamSection：本工程 RAM 由启动代码初始化，不提供该 API */
#ifndef MCU_INIT_RAM_SECTION_API
#define MCU_INIT_RAM_SECTION_API    STD_OFF
#endif

/* MCU_VERSION_INFO_API：GetVersionInfo 开关（SWS_Mcu_00162） */
#ifndef MCU_VERSION_INFO_API
#define MCU_VERSION_INFO_API        MCAL_VERSION_INFO_API
#endif

/* 时钟节点/模式表最大长度（编译期上界，配置表超出会静态断言） */
#define MCU_MAX_CLOCK_NODES         (8u)
#define MCU_MAX_CLOCK_SETTINGS      (4u)

#endif /* MCU_CFG_H */
