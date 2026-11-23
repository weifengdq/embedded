/*
 * Mem.c —— AUTOSAR Memory Driver 驱动实现（HPM5E31 片内 XPI NOR）
 *
 * 依据：AUTOSAR_CP_SWS_MemoryDriver R25-11
 *       §7.2.1 作业管理（单作业、MainFunction 执行、作业结果）
 *       §7.2.1.2 Suspend/Resume（无硬件挂起能力 -> E_MEM_SERVICE_NOT_AVAIL）
 *       §7.2.2  Mem_HwSpecificService 分发
 *       §7.5.1  开发错误（MEM_E_*）
 *       §8.3    每个 API 的语法/参数/返回值
 * 硬件：HPM5E31 片内 1MB Flash，经 XPI0 映射到 0x80000000；擦/编由 ROM API
 *       （rom_xpi_nor_erase / rom_xpi_nor_program）完成，读走 XIP 直读 + L1 D-Cache。
 *       ROM API 说明见 hpm_sdk/soc/HPM5E00/HPM5E31/hpm_romapi.h。
 * 例程：samples/mcal/mem_flash_rw（擦-写-读-比较-空白检查）、samples/mcal/mem_bench。
 *
 * 设计要点：
 *  1) 异步作业：API 只做参数检查 + 置 MEM_JOB_PENDING 并返回；硬件动作全部在
 *     Mem_MainFunction 里按"单元"推进（1 扇区擦除 / 1 页编程 / 1 个 chunk 读），
 *     保证单次 MainFunction 阻塞有上限（[SWS_Mem_00066]）；
 *  2) 所有板级参数（XPI 基址、容量、窗口、几何、ROM API 配置头）都来自 Mem_PBcfg.c，
 *     本文件没有硬编码引脚/实例；
 *  3) 擦/写/空白检查被限制在 PBcfg 配的保留窗口内（默认最后 64KB），越界报
 *     MEM_E_PARAM_ADDRESS 并拒绝受理，避免把应用镜像擦掉（见 README 的窗口说明）。
 */
#include "Mem.h"
#include "Mem_Cfg.h"
#include "Mem_PBcfg.h"
#include "Det.h"
#include "MemMap.h"

#include <string.h>

#include "hpm_common.h"
#include "hpm_romapi.h"
#include "hpm_l1c_drv.h"

/* ============================ 内部类型与状态 ============================ */
typedef enum
{
    MEM_JOB_NONE = 0,
    MEM_JOB_READ,
    MEM_JOB_WRITE,
    MEM_JOB_ERASE,
    MEM_JOB_BLANKCHECK,
    MEM_JOB_HWSPEC
} mem_job_type_t;

typedef struct
{
    mem_job_type_t   type;
    uint8            instance;
    Mem_AddressType  address;
    Mem_LengthType   length;
    Mem_DataType    *dataPtr;      /* 读：目的缓冲；写：源缓冲；hw spec：数据区 */
    Mem_HwServiceIdType hwServiceId;
    Mem_LengthType   done;
} mem_job_t;

static boolean                 s_initialized = FALSE;
static const Mem_ConfigType   *s_config = NULL_PTR;
static xpi_nor_config_t        s_norCfg[MEM_MAX_INSTANCES];
static boolean                 s_norReady[MEM_MAX_INSTANCES];
static mem_job_t               s_job;
static volatile boolean        s_jobActive = FALSE;
static volatile Mem_JobResultType s_jobResult = MEM_JOB_OK;
static uint8                   s_instanceCount = 0U;

/* ============================ 内联小工具 ============================ */
#define MEM_REPORT(inst, api, err) \
    MCAL_DET_REPORT(MEM_MODULE_ID, (uint8)(inst), (uint8)(api), (uint8)(err))

static void mem_job_finish(Mem_JobResultType result)
{
    s_jobResult = result;
    s_jobActive = FALSE;
    s_job.type  = MEM_JOB_NONE;
}

static void mem_dcache_invalidate(Mem_AddressType addr, Mem_LengthType len)
{
    if (l1c_dc_is_enabled()) {
        uint32_t a0 = (uint32_t)HPM_L1C_CACHELINE_ALIGN_DOWN((uint32_t)addr);
        uint32_t a1 = (uint32_t)HPM_L1C_CACHELINE_ALIGN_UP((uint32_t)addr + (uint32_t)len);
        l1c_dc_invalidate(a0, a1 - a0);
    }
}

#if (MEM_DEV_ERROR_DETECT == STD_ON)
static boolean mem_check_initialized(uint8 apiId)
{
    if (!s_initialized) {
        MEM_REPORT(0U, apiId, MEM_E_UNINIT);
        return FALSE;
    }
    return TRUE;
}

static boolean mem_check_instance(Mem_InstanceIdType instanceId, uint8 apiId)
{
    if ((uint32)instanceId >= (uint32)s_instanceCount) {
        MEM_REPORT(instanceId, apiId, MEM_E_PARAM_INSTANCE_ID);
        return FALSE;
    }
    return TRUE;
}

static boolean mem_check_idle(uint8 apiId)
{
    if (s_jobActive) {
        MEM_REPORT(0U, apiId, MEM_E_JOB_PENDING);
        return FALSE;
    }
    return TRUE;
}
#else
static boolean mem_check_initialized(uint8 apiId)            { MCAL_UNUSED(apiId); return s_initialized; }
static boolean mem_check_instance(Mem_InstanceIdType id, uint8 apiId)
{ MCAL_UNUSED(apiId); return ((uint32)id < (uint32)s_instanceCount); }
static boolean mem_check_idle(uint8 apiId)                   { MCAL_UNUSED(apiId); return (s_jobActive == FALSE); }
#endif

/* 读范围合法性：落在设备映射区 [base, base+size) 内，长度非 0 且不溢出 */
static boolean mem_range_in_device(uint8 instance, Mem_AddressType addr, Mem_LengthType len)
{
    const Mem_InstanceConfigType *ic = &s_config->instances[instance];
    Mem_AddressType offset;

    if ((len == 0U) || (addr < ic->baseAddress)) {
        return FALSE;
    }
    offset = addr - ic->baseAddress;
    if ((offset >= ic->size) || (len > (ic->size - offset))) {
        return FALSE;
    }
    return TRUE;
}

/* 擦/写范围合法性：必须完全落在 PBcfg 配置的保留窗口内 */
static boolean mem_range_in_window(uint8 instance, Mem_AddressType addr, Mem_LengthType len)
{
    const Mem_InstanceConfigType *ic = &s_config->instances[instance];
    Mem_AddressType windowSize = ic->eraseWindowEnd - ic->eraseWindowStart;
    Mem_AddressType offset;

    if ((len == 0U) || (addr < ic->eraseWindowStart)) {
        return FALSE;
    }
    offset = addr - ic->eraseWindowStart;
    if ((offset >= windowSize) || (len > (windowSize - offset))) {
        return FALSE;
    }
    return TRUE;
}

/* 受理一个作业：置 PENDING，等待 Mem_MainFunction 推进 */
static Std_ReturnType mem_accept_job(mem_job_type_t type, uint8 instance, Mem_AddressType addr,
                                     Mem_LengthType len, Mem_DataType *dataPtr,
                                     Mem_HwServiceIdType hwServiceId)
{
    s_job.type        = type;
    s_job.instance    = instance;
    s_job.address     = addr;
    s_job.length      = len;
    s_job.dataPtr     = dataPtr;
    s_job.hwServiceId = hwServiceId;
    s_job.done        = 0U;
    s_jobResult       = MEM_JOB_PENDING;
    s_jobActive       = TRUE;
    return E_OK;
}

/* ============================ 8.3.1 同步 API ============================ */
#define MEM_START_SEC_CODE
#include "MemMap.h"

void Mem_Init(const Mem_ConfigType *configPtr)
{
    uint8 i;

#if (MEM_DEV_ERROR_DETECT == STD_ON)
    if (configPtr == NULL_PTR) {
        MEM_REPORT(0U, MEM_API_ID_INIT, MEM_E_PARAM_POINTER);
        return;
    }
#endif

    /* 初始化内部状态：只允许一个作业，作业结果为 MEM_JOB_OK（[SWS_Mem_00001]） */
    s_jobActive  = FALSE;
    s_job.type   = MEM_JOB_NONE;
    s_jobResult  = MEM_JOB_OK;
    s_job.done   = 0U;
    s_config     = configPtr;
    s_instanceCount = 0U;

    if ((configPtr->numInstances == 0U) || (configPtr->numInstances > MEM_MAX_INSTANCES) ||
        (configPtr->instances == NULL_PTR)) {
        MEM_REPORT(0U, MEM_API_ID_INIT, MEM_E_INIT_FAILED);
        return;
    }

    /* 逐实例做 XPI NOR 自动配置（[SWS_Mem_00060] 多实例支持）；参数全部来自 PBcfg */
    for (i = 0U; i < configPtr->numInstances; i++) {
        xpi_nor_config_option_t opt;
        hpm_stat_t st;

        opt.header.U  = configPtr->hwCfgOptionHeader;
        opt.option0.U = configPtr->hwCfgOption0;
        opt.option1.U = configPtr->hwCfgOption1;

        st = rom_xpi_nor_auto_config((XPI_Type *)(uintptr_t)configPtr->hwBaseAddress,
                                     &s_norCfg[i], &opt);
        s_norReady[i] = (st == status_success) ? TRUE : FALSE;
        if (!s_norReady[i]) {
            MEM_REPORT(i, MEM_API_ID_INIT, MEM_E_INIT_FAILED);
        }
    }

    s_instanceCount = configPtr->numInstances;
    s_initialized   = TRUE;
}

void Mem_DeInit(void)
{
#if (MEM_DEV_ERROR_DETECT == STD_ON)
    if (!mem_check_initialized(MEM_API_ID_DEINIT)) {
        return;
    }
#endif
    /* 立即中止在途作业（ROM API 的擦/编本身不可中断，这里只丢软件状态）[SWS_Mem_00079] */
    s_jobActive = FALSE;
    s_job.type  = MEM_JOB_NONE;
    s_jobResult = MEM_JOB_FAILED;
    s_initialized = FALSE;
}

void Mem_GetVersionInfo(Std_VersionInfoType *versionInfoPtr)
{
    if (versionInfoPtr == NULL_PTR) {
        MEM_REPORT(0U, MEM_API_ID_GET_VERSION_INFO, MEM_E_PARAM_POINTER);
        return;
    }
    versionInfoPtr->vendorID         = MEM_VENDOR_ID;
    versionInfoPtr->moduleID         = MEM_MODULE_ID;
    versionInfoPtr->sw_major_version = MEM_SW_MAJOR_VERSION;
    versionInfoPtr->sw_minor_version = MEM_SW_MINOR_VERSION;
    versionInfoPtr->sw_patch_version = MEM_SW_PATCH_VERSION;
}

Mem_JobResultType Mem_GetJobResult(Mem_InstanceIdType instanceId)
{
#if (MEM_DEV_ERROR_DETECT == STD_ON)
    if (!mem_check_initialized(MEM_API_ID_GET_JOB_RESULT)) {
        return MEM_JOB_FAILED;
    }
    if (!mem_check_instance(instanceId, MEM_API_ID_GET_JOB_RESULT)) {
        return MEM_JOB_FAILED;
    }
#else
    if ((!s_initialized) || ((uint32)instanceId >= (uint32)s_instanceCount)) {
        return MEM_JOB_FAILED;
    }
#endif
    return s_jobResult;
}

Std_ReturnType Mem_Suspend(Mem_InstanceIdType instanceId)
{
#if (MEM_DEV_ERROR_DETECT == STD_ON)
    if (!mem_check_initialized(MEM_API_ID_SUSPEND)) { return E_NOT_OK; }
    if (!mem_check_instance(instanceId, MEM_API_ID_SUSPEND)) { return E_NOT_OK; }
#else
    if (!s_initialized) { return E_NOT_OK; }
#endif
    /* HPM5E31 的 XPI NOR 擦/编由 ROM API 阻塞完成，无硬件 suspend/resume 机制，
     * 按 [SWS_Mem_00082] 返回 E_MEM_SERVICE_NOT_AVAIL。 */
    return E_MEM_SERVICE_NOT_AVAIL;
}

Std_ReturnType Mem_Resume(Mem_InstanceIdType instanceId)
{
#if (MEM_DEV_ERROR_DETECT == STD_ON)
    if (!mem_check_initialized(MEM_API_ID_RESUME)) { return E_NOT_OK; }
    if (!mem_check_instance(instanceId, MEM_API_ID_RESUME)) { return E_NOT_OK; }
#else
    if (!s_initialized) { return E_NOT_OK; }
#endif
    return E_MEM_SERVICE_NOT_AVAIL;   /* [SWS_Mem_00082] */
}

void Mem_PropagateError(Mem_InstanceIdType instanceId)
{
#if (MEM_DEV_ERROR_DETECT == STD_ON)
    if (!mem_check_initialized(MEM_API_ID_PROPAGATE_ERROR)) { return; }
    if (!mem_check_instance(instanceId, MEM_API_ID_PROPAGATE_ERROR)) { return; }
#else
    if (!s_initialized) { return; }
#endif
    /* ECC 错误上报：作业结果置不可纠正并中止当前作业 [SWS_Mem_00061] */
    mem_job_finish(MEM_ECC_UNCORRECTED);
}

/* ============================ 8.3.2 异步 API ============================ */
Std_ReturnType Mem_Read(Mem_InstanceIdType instanceId, Mem_AddressType sourceAddress,
                        Mem_DataType *destinationDataPtr, Mem_LengthType length)
{
#if (MEM_DEV_ERROR_DETECT == STD_ON)
    if (!mem_check_initialized(MEM_API_ID_READ))            { return E_NOT_OK; }
    if (!mem_check_instance(instanceId, MEM_API_ID_READ))   { return E_NOT_OK; }
    if (destinationDataPtr == NULL_PTR) {
        MEM_REPORT(instanceId, MEM_API_ID_READ, MEM_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    if ((!mem_range_in_device((uint8)instanceId, sourceAddress, length)) ||
        (length == 0U)) {
        MEM_REPORT(instanceId, MEM_API_ID_READ, MEM_E_PARAM_ADDRESS);
        return E_NOT_OK;
    }
    if (!mem_check_idle(MEM_API_ID_READ))                   { return E_NOT_OK; }
#endif
    if (!s_norReady[(uint8)instanceId]) {
        s_jobResult = MEM_JOB_FAILED;
        return E_NOT_OK;
    }
    /* 读之前使 D-Cache 失效，避免读到擦写前缓存的旧行 */
    mem_dcache_invalidate(sourceAddress, length);
    return mem_accept_job(MEM_JOB_READ, (uint8)instanceId, sourceAddress, length,
                          destinationDataPtr, 0U);
}

Std_ReturnType Mem_Write(Mem_InstanceIdType instanceId, Mem_AddressType targetAddress,
                         const Mem_DataType *sourceDataPtr, Mem_LengthType length)
{
#if (MEM_DEV_ERROR_DETECT == STD_ON)
    const Mem_InstanceConfigType *ic;

    if (!mem_check_initialized(MEM_API_ID_WRITE))           { return E_NOT_OK; }
    if (!mem_check_instance(instanceId, MEM_API_ID_WRITE))  { return E_NOT_OK; }
    if (sourceDataPtr == NULL_PTR) {
        MEM_REPORT(instanceId, MEM_API_ID_WRITE, MEM_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    if (!mem_range_in_window((uint8)instanceId, targetAddress, length)) {
        MEM_REPORT(instanceId, MEM_API_ID_WRITE, MEM_E_PARAM_ADDRESS);
        return E_NOT_OK;
    }
    ic = &s_config->instances[(uint8)instanceId];
    /* 编程地址/长度必须按页对齐；源缓冲必须 4 字节对齐（ROM API 按字搬运） */
    if (((targetAddress & (ic->pageSize - 1U)) != 0U) ||
        ((length & (ic->pageSize - 1U)) != 0U)) {
        MEM_REPORT(instanceId, MEM_API_ID_WRITE, MEM_E_PARAM_ADDRESS);
        return E_NOT_OK;
    }
    if (((uint32)(uintptr_t)sourceDataPtr & 0x3U) != 0U) {
        MEM_REPORT(instanceId, MEM_API_ID_WRITE, MEM_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    if (!mem_check_idle(MEM_API_ID_WRITE))                  { return E_NOT_OK; }
#endif
    if (!s_norReady[(uint8)instanceId]) {
        s_jobResult = MEM_JOB_FAILED;
        return E_NOT_OK;
    }
    return mem_accept_job(MEM_JOB_WRITE, (uint8)instanceId, targetAddress, length,
                          (Mem_DataType *)(uintptr_t)sourceDataPtr, 0U);
}

Std_ReturnType Mem_Erase(Mem_InstanceIdType instanceId, Mem_AddressType targetAddress,
                         Mem_LengthType length)
{
#if (MEM_DEV_ERROR_DETECT == STD_ON)
    const Mem_InstanceConfigType *ic;

    if (!mem_check_initialized(MEM_API_ID_ERASE))           { return E_NOT_OK; }
    if (!mem_check_instance(instanceId, MEM_API_ID_ERASE))  { return E_NOT_OK; }
    if (!mem_range_in_window((uint8)instanceId, targetAddress, length)) {
        MEM_REPORT(instanceId, MEM_API_ID_ERASE, MEM_E_PARAM_ADDRESS);
        return E_NOT_OK;
    }
    ic = &s_config->instances[(uint8)instanceId];
    if (((targetAddress & (ic->sectorSize - 1U)) != 0U) ||
        ((length & (ic->sectorSize - 1U)) != 0U)) {
        MEM_REPORT(instanceId, MEM_API_ID_ERASE, MEM_E_PARAM_LENGTH);
        return E_NOT_OK;
    }
    if (!mem_check_idle(MEM_API_ID_ERASE))                  { return E_NOT_OK; }
#endif
    if (!s_norReady[(uint8)instanceId]) {
        s_jobResult = MEM_JOB_FAILED;
        return E_NOT_OK;
    }
    return mem_accept_job(MEM_JOB_ERASE, (uint8)instanceId, targetAddress, length, NULL_PTR, 0U);
}

Std_ReturnType Mem_BlankCheck(Mem_InstanceIdType instanceId, Mem_AddressType targetAddress,
                              Mem_LengthType length)
{
#if (MEM_DEV_ERROR_DETECT == STD_ON)
    if (!mem_check_initialized(MEM_API_ID_BLANK_CHECK))           { return E_NOT_OK; }
    if (!mem_check_instance(instanceId, MEM_API_ID_BLANK_CHECK))  { return E_NOT_OK; }
    if (!mem_range_in_window((uint8)instanceId, targetAddress, length)) {
        MEM_REPORT(instanceId, MEM_API_ID_BLANK_CHECK, MEM_E_PARAM_ADDRESS);
        return E_NOT_OK;
    }
    if (!mem_check_idle(MEM_API_ID_BLANK_CHECK))                  { return E_NOT_OK; }
#endif
    if (!s_norReady[(uint8)instanceId]) {
        s_jobResult = MEM_JOB_FAILED;
        return E_NOT_OK;
    }
    mem_dcache_invalidate(targetAddress, length);
    return mem_accept_job(MEM_JOB_BLANKCHECK, (uint8)instanceId, targetAddress, length,
                          NULL_PTR, 0U);
}

Std_ReturnType Mem_HwSpecificService(Mem_InstanceIdType instanceId, Mem_HwServiceIdType hwServiceId,
                                     Mem_DataType *dataPtr, Mem_LengthType *lengthPtr)
{
#if (MEM_DEV_ERROR_DETECT == STD_ON)
    if (!mem_check_initialized(MEM_API_ID_HW_SPECIFIC_SERVICE))           { return E_NOT_OK; }
    if (!mem_check_instance(instanceId, MEM_API_ID_HW_SPECIFIC_SERVICE))  { return E_NOT_OK; }
    if ((dataPtr == NULL_PTR) || (lengthPtr == NULL_PTR)) {
        MEM_REPORT(instanceId, MEM_API_ID_HW_SPECIFIC_SERVICE, MEM_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    if (!mem_check_idle(MEM_API_ID_HW_SPECIFIC_SERVICE))                  { return E_NOT_OK; }
#endif
    if ((hwServiceId != MEM_HW_SERVICE_ID_GET_GEOMETRY) &&
        (hwServiceId != MEM_HW_SERVICE_ID_GET_PROGRESS)) {
        return E_NOT_OK;    /* 未实现的硬件专有服务，拒绝受理 */
    }
    if (hwServiceId == MEM_HW_SERVICE_ID_GET_GEOMETRY) {
        if ((*lengthPtr < 16U) || (((uint32)(uintptr_t)dataPtr & 0x3U) != 0U)) {
#if (MEM_DEV_ERROR_DETECT == STD_ON)
            MEM_REPORT(instanceId, MEM_API_ID_HW_SPECIFIC_SERVICE, MEM_E_PARAM_LENGTH);
#endif
            return E_NOT_OK;
        }
    } else {
        if (*lengthPtr < 8U) {
#if (MEM_DEV_ERROR_DETECT == STD_ON)
            MEM_REPORT(instanceId, MEM_API_ID_HW_SPECIFIC_SERVICE, MEM_E_PARAM_LENGTH);
#endif
            return E_NOT_OK;
        }
    }
    return mem_accept_job(MEM_JOB_HWSPEC, (uint8)instanceId, 0U, 0U, dataPtr, hwServiceId);
}

/* ============================ 8.5 调度函数 ============================ */
void Mem_MainFunction(void)
{
    const Mem_InstanceConfigType *ic;
    Mem_LengthType chunk;
    uint8 units;

    if (!s_initialized) {
        return;   /* 未初始化时静默返回（MainFunction 允许 Init 前被调用） */
    }
    if (!s_jobActive) {
        return;
    }

    ic = &s_config->instances[s_job.instance];

    for (units = 0U; units < MEM_UNITS_PER_MAIN_FUNCTION; units++) {
        if (!s_jobActive) {
            break;
        }

        switch (s_job.type) {
        case MEM_JOB_READ:
        {
            Mem_LengthType remain = s_job.length - s_job.done;
            chunk = (remain > ic->readChunk) ? ic->readChunk : remain;
            (void)memcpy(&s_job.dataPtr[s_job.done],
                         (const void *)(uintptr_t)(s_job.address + s_job.done),
                         (size_t)chunk);
            s_job.done += chunk;
            if (s_job.done >= s_job.length) {
                mem_job_finish(MEM_JOB_OK);
            }
            break;
        }
        case MEM_JOB_WRITE:
        {
            Mem_LengthType remain = s_job.length - s_job.done;
            hpm_stat_t st;
            chunk = (remain > ic->pageSize) ? ic->pageSize : remain;
            st = rom_xpi_nor_program((XPI_Type *)(uintptr_t)s_config->hwBaseAddress,
                                     xpi_xfer_channel_auto, &s_norCfg[s_job.instance],
                                     (uint32_t *)(void *)&s_job.dataPtr[s_job.done],
                                     (uint32_t)(s_job.address + s_job.done), (uint32_t)chunk);
            if (st != status_success) {
                mem_job_finish(MEM_JOB_FAILED);
                break;
            }
            mem_dcache_invalidate(s_job.address + s_job.done, chunk);
            s_job.done += chunk;
            if (s_job.done >= s_job.length) {
                mem_job_finish(MEM_JOB_OK);
            }
            break;
        }
        case MEM_JOB_ERASE:
        {
            Mem_LengthType remain = s_job.length - s_job.done;
            hpm_stat_t st;
            chunk = (remain > ic->sectorSize) ? ic->sectorSize : remain;
            st = rom_xpi_nor_erase((XPI_Type *)(uintptr_t)s_config->hwBaseAddress,
                                   xpi_xfer_channel_auto, &s_norCfg[s_job.instance],
                                   (uint32_t)(s_job.address + s_job.done), (uint32_t)chunk);
            if (st != status_success) {
                mem_job_finish(MEM_JOB_FAILED);
                break;
            }
            mem_dcache_invalidate(s_job.address + s_job.done, chunk);
            s_job.done += chunk;
            if (s_job.done >= s_job.length) {
                mem_job_finish(MEM_JOB_OK);
            }
            break;
        }
        case MEM_JOB_BLANKCHECK:
        {
            const volatile uint8 *p =
                (const volatile uint8 *)(uintptr_t)(s_job.address + s_job.done);
            Mem_LengthType remain = s_job.length - s_job.done;
            Mem_LengthType i;

            chunk = (remain > ic->readChunk) ? ic->readChunk : remain;
            for (i = 0U; i < chunk; i++) {
                if (p[i] != (uint8)MEM_ERASED_VALUE) {
                    mem_job_finish(MEM_INCONSISTENT);   /* [SWS_Mem_00076] */
                    break;
                }
            }
            if (!s_jobActive) {
                break;
            }
            s_job.done += chunk;
            if (s_job.done >= s_job.length) {
                mem_job_finish(MEM_JOB_OK);
            }
            break;
        }
        case MEM_JOB_HWSPEC:
        {
            uint32 *words = (uint32 *)(void *)s_job.dataPtr;

            if (s_job.hwServiceId == MEM_HW_SERVICE_ID_GET_GEOMETRY) {
                words[0] = (uint32)ic->baseAddress;
                words[1] = (uint32)ic->size;
                words[2] = (uint32)ic->sectorSize;
                words[3] = (uint32)ic->pageSize;
            } else {
                words[0] = (uint32)s_job.done;
                words[1] = (uint32)s_job.length;
            }
            mem_job_finish(MEM_JOB_OK);
            break;
        }
        default:
            mem_job_finish(MEM_JOB_FAILED);
            break;
        }
    }
}

/* ============================ 扩展 API ============================ */
boolean Mem_IsJobPending(void)
{
    return s_jobActive;
}

uint32 Mem_GetInstanceCount(void)
{
    return (uint32)s_instanceCount;
}

#define MEM_STOP_SEC_CODE
#include "MemMap.h"
