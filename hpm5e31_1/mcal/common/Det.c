/*
 * Det.c —— Default Error Tracer 最小实现（记录最近 8 条错误 + 计数）
 */
#include "Det.h"

#define DET_NUM_RECORDS (8u)

typedef struct
{
    uint16 moduleId;
    uint8  instanceId;
    uint8  apiId;
    uint8  errorId;
} Det_RecordType;

static Det_RecordType s_det_records[DET_NUM_RECORDS];
static uint8  s_det_wr;
static uint32 s_det_count;
static boolean s_det_initialized = FALSE;

void Det_Init(void)
{
    s_det_wr = 0u;
    s_det_count = 0u;
    s_det_initialized = TRUE;
}

Std_ReturnType Det_ReportError(uint16 ModuleId, uint8 InstanceId, uint8 ApiId, uint8 ErrorId)
{
    s_det_records[s_det_wr].moduleId   = ModuleId;
    s_det_records[s_det_wr].instanceId = InstanceId;
    s_det_records[s_det_wr].apiId      = ApiId;
    s_det_records[s_det_wr].errorId    = ErrorId;
    s_det_wr = (uint8)((s_det_wr + 1u) % DET_NUM_RECORDS);
    s_det_count++;
#if (MCAL_DET_PRINTF == STD_ON)
    {
        extern int printf(const char *, ...);
        (void)printf("[DET] mod=%u inst=%u api=%u err=%u\r\n",
                     (unsigned)ModuleId, (unsigned)InstanceId,
                     (unsigned)ApiId, (unsigned)ErrorId);
    }
#endif
    return E_OK;
}

Std_ReturnType Det_ReportRuntimeError(uint16 ModuleId, uint8 InstanceId, uint8 ApiId, uint8 ErrorId)
{
    return Det_ReportError(ModuleId, InstanceId, ApiId, ErrorId);
}

uint32 Det_GetErrorCount(void)
{
    return s_det_count;
}

void Det_ClearErrorCount(void)
{
    s_det_count = 0u;
    s_det_wr = 0u;
}

Std_ReturnType Det_GetLastError(uint16 *ModuleId, uint8 *InstanceId, uint8 *ApiId, uint8 *ErrorId)
{
    uint8 idx;
    if ((ModuleId == NULL_PTR) || (InstanceId == NULL_PTR) ||
        (ApiId == NULL_PTR) || (ErrorId == NULL_PTR))
    {
        return E_NOT_OK;
    }
    if (s_det_count == 0u)
    {
        return E_NOT_OK;
    }
    idx = (uint8)((s_det_wr + DET_NUM_RECORDS - 1u) % DET_NUM_RECORDS);
    *ModuleId   = s_det_records[idx].moduleId;
    *InstanceId = s_det_records[idx].instanceId;
    *ApiId      = s_det_records[idx].apiId;
    *ErrorId    = s_det_records[idx].errorId;
    return E_OK;
}
