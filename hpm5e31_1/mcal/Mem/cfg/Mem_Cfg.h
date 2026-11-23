/*
 * Mem_Cfg.h —— Mem 预编译配置（AUTOSAR Pre-compile 配置阶段）
 *
 * 依据：AUTOSAR_CP_SWS_MemoryDriver §10（MemDevErrorDetect / MemVersionInfoApi 等开关）。
 * 只放"编译期决定"的开关；实例地址/几何在 Mem_PBcfg.[ch]（post-build）。
 */
#ifndef MEM_CFG_H
#define MEM_CFG_H

#include "Mcal_Cfg.h"

/* ---- 开发错误检测（SWS MemDevErrorDetect） ---- */
#ifndef MEM_DEV_ERROR_DETECT
#define MEM_DEV_ERROR_DETECT      MCAL_DEV_ERROR_DETECT
#endif

/* ---- 版本信息 API（SWS MemVersionInfoApi） ---- */
#ifndef MEM_VERSION_INFO_API
#define MEM_VERSION_INFO_API      MCAL_VERSION_INFO_API
#endif

/* ---- 支持的实例个数上限（SWS_Mem_00060 多实例支持） ---- */
#ifndef MEM_MAX_INSTANCES
#define MEM_MAX_INSTANCES         (2u)
#endif

/* ---- 空白值：NOR Flash 擦除后为 0xFF ---- */
#ifndef MEM_ERASED_VALUE
#define MEM_ERASED_VALUE          (0xFFu)
#endif

/* ---- Mem_MainFunction 单次调用最多推进的"硬件单元"个数 ----
 * 1 = 每次只擦 1 个扇区 / 编 1 页 / 读 1 个 chunk，单次 MainFunction 阻塞上限最小，
 * 便于在 1ms 任务里安全调用；调大可减少调用次数但会拉长单次阻塞。 */
#ifndef MEM_UNITS_PER_MAIN_FUNCTION
#define MEM_UNITS_PER_MAIN_FUNCTION  (1u)
#endif

/* ---- 作业被受理后是否允许再次受理（SWS_Mem_00057 只允许一个作业） ---- */
#define MEM_ALLOW_ONE_JOB         STD_ON

#endif /* MEM_CFG_H */
