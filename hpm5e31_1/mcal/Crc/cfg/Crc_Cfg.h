/*
 * Crc_Cfg.h —— Crc 预编译配置（AUTOSAR Pre-compile 配置阶段）
 *
 * SWS_Crc 的配置项：Crc8Mode/Crc16Mode/... 只决定"查表还是逐位"（本工程统一查表），
 * CrcVersionInfoApi 决定是否编译 Crc_GetVersionInfo。这里的 HW 加速开关是本实现
 * 的工程扩展（AUTOSAR 不关心实现用不用硬件单元）。
 */
#ifndef CRC_CFG_H
#define CRC_CFG_H

#include "Mcal_Cfg.h"

/* ---- Crc_GetVersionInfo 编译开关（SWS CrcVersionInfoApi） ---- */
#ifndef CRC_VERSION_INFO_API
#define CRC_VERSION_INFO_API      MCAL_VERSION_INFO_API
#endif

/* ---- 是否允许用 HPM CRC 外设加速（运行时还会做一次自校准，失败自动回落软件） ---- */
#ifndef CRC_USE_HW_ACCEL
#define CRC_USE_HW_ACCEL          STD_ON
#endif

/* ---- 小于该长度的数据直接用软件表，避免外设配置开销（实测阈值见第 48 章） ---- */
#ifndef CRC_HW_MIN_LENGTH
#define CRC_HW_MIN_LENGTH         (16u)
#endif

/* ---- 表驱动 / 逐位：SWS 允许 Crc<X>Mode 选 TABLE 或 BITWISE。本工程默认 TABLE。 ---- */
#ifndef CRC_USE_TABLE
#define CRC_USE_TABLE             STD_ON
#endif

/* ---- 开发错误检测：SWS_Crc §7.6.1 "There are no development errors"，
 *      且 [SWS_Crc_00072] 禁止 CRC 库函数调用 BSW（含 Det），因此默认关闭。 ---- */
#ifndef CRC_DEV_ERROR_DETECT
#define CRC_DEV_ERROR_DETECT      STD_OFF
#endif

#endif /* CRC_CFG_H */
