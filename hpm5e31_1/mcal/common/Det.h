/*
 * Det.h —— Default Error Tracer（AUTOSAR 模块 ID 15）
 *
 * 说明：Det 是 BSW 模块，不属于 MCAL；MCAL 只按 SWS 要求调用 Det_ReportError。
 * 本仓库给一个"记录 + 可查询"的最小实现，方便上板测试断言错误路径。
 */
#ifndef DET_H
#define DET_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Mcal_ModuleId.h"

#define DET_AR_RELEASE_MAJOR_VERSION    (4u)
#define DET_AR_RELEASE_MINOR_VERSION    (4u)
#define DET_SW_MAJOR_VERSION            (1u)
#define DET_SW_MINOR_VERSION            (0u)
#define DET_SW_PATCH_VERSION            (0u)

/* AUTOSAR 标准错误码（SWS_BSWGeneral / 各模块 SWS 附录） */
#define DET_E_PARAM_POINTER   (0x01u)
#define DET_E_PARAM_VALUE     (0x02u)
#define DET_E_PARAM_CONFIG    (0x03u)
#define DET_E_UNINIT          (0x04u)
#define DET_E_INIT_FAILED     (0x05u)
#define DET_E_NOT_INITIALIZED (0x06u)
#define DET_E_BUSY            (0x07u)
#define DET_E_INVALID_REQUEST (0x08u)
#define DET_E_NULL_POINTER    DET_E_PARAM_POINTER

/* 每个模块 API ID 由各模块头文件按 SWS 定义（<MOD>_API_ID_xxx） */

#if (MCAL_DEV_ERROR_DETECT == STD_ON)
#define MCAL_DET_REPORT(mod, inst, api, err)  ((void)Det_ReportError((uint16)(mod), (uint8)(inst), (uint8)(api), (uint8)(err)))
#else
#define MCAL_DET_REPORT(mod, inst, api, err)  ((void)0)
#endif

/* Det 自身 API（AUTOSAR SWS_DefaultErrorTracer） */
void Det_Init(void);
Std_ReturnType Det_ReportError(uint16 ModuleId, uint8 InstanceId, uint8 ApiId, uint8 ErrorId);
Std_ReturnType Det_ReportRuntimeError(uint16 ModuleId, uint8 InstanceId, uint8 ApiId, uint8 ErrorId);

/* 本仓库扩展：给上板测试用的查询接口（非 AUTOSAR API） */
uint32 Det_GetErrorCount(void);
void   Det_ClearErrorCount(void);
Std_ReturnType Det_GetLastError(uint16 *ModuleId, uint8 *InstanceId, uint8 *ApiId, uint8 *ErrorId);

#endif /* DET_H */
