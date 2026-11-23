/*
 * Crc_PBcfg.c —— Crc 板级 post-build 配置（HPM5E31 / hpm5e31_1）
 *
 * 依据：AUTOSAR_CP_SWS_CRCLibrary §10 配置容器 + docs/MCAL开发约定.md §5。
 * 改板只改这里：CRC 外设基址、使用的通道号、外设时钟。
 * 资源归属：HPM CRC 外设整块归 Crc 使用（约定 §2 "HPM CRC 外设"）。
 */
#include "Crc.h"
#include "Crc_Cfg.h"
#include "Crc_PBcfg.h"
#include "hpm_soc.h"          /* HPM_CRC（hpm_soc_ip.h:188/190） */
#include "hpm_clock_drv.h"    /* clock_crc0（hpm_clock_drv.h:144） */

#define CRC_CFG_HW_CHANNEL      (0u)   /* 通道 0，其余通道留给将来多上下文 */

const Crc_ConfigType Crc_Config = {
    /* hwAccelEnabled */ (uint8)CRC_USE_HW_ACCEL,
    /* hwChannel      */ (uint8)CRC_CFG_HW_CHANNEL,
    /* hwBaseAddress  */ (uint32)(uintptr_t)HPM_CRC,
    /* hwClockId      */ (uint32)clock_crc0
};
