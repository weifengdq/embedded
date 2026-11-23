/*
 * Mcal_Cfg.h —— MCAL 全局编译期开关（本工程唯一"全局配置"入口）
 *
 * 各模块的 <Mod>_Cfg.h 必须先 include 本文件，再定义自己的开关，
 * 这样全局开关（例如 Dev Error Detect）可以一处关闭。
 */
#ifndef MCAL_CFG_H
#define MCAL_CFG_H

#include "Std_Types.h"

#define MCAL_AR_RELEASE_MAJOR_VERSION    (4u)
#define MCAL_AR_RELEASE_MINOR_VERSION    (4u)
#define MCAL_AR_RELEASE_REVISION_VERSION (0u)

/* 开发错误检测（AUTOSAR DEV_ERROR_DETECT）。量产可置 STD_OFF 省 Flash/CPU。 */
#ifndef MCAL_DEV_ERROR_DETECT
#define MCAL_DEV_ERROR_DETECT STD_ON
#endif

/* 生产错误上报 Dem（Dem 属于 BSW，本仓库只给桩；默认关闭）。 */
#ifndef MCAL_DEM_ENABLE
#define MCAL_DEM_ENABLE STD_OFF
#endif

/* GetVersionInfo API 开关（AUTOSAR 可选）。 */
#ifndef MCAL_VERSION_INFO_API
#define MCAL_VERSION_INFO_API STD_ON
#endif

/* 是否把 Det 错误同时打印到调试串口（仅 bring-up，量产关闭）。 */
#ifndef MCAL_DET_PRINTF
#define MCAL_DET_PRINTF STD_OFF
#endif

/* 板卡基础时钟（仅作断言/换算参考，真正配置在 Mcu_PBcfg.c）。 */
#define MCAL_BOARD_XTAL_HZ   (24000000u)

#endif /* MCAL_CFG_H */
