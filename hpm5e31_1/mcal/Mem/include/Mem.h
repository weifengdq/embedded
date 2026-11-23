/*
 * Mem.h —— AUTOSAR Memory Driver（Mem，模块 ID 91；R25-11 起原 Fls 并入 Mem）
 *
 * 依据：AUTOSAR_CP_SWS_MemoryDriver（R25-11），本地抽取文本
 *       /home/hpm5e31/temp2/autosar_txt/AUTOSAR_CP_SWS_MemoryDriver.txt
 *       §8.2 类型、§8.3 函数、§7.2.1 作业模型、§7.5.1 开发错误。
 * 对应 HPM5E31：片内 1MB XPI NOR（经 XPI0 映射到 0x80000000），RM 第 21 章 XPI/Flash。
 * 例程：samples/mcal/mem_flash_rw、samples/mcal/mem_bench。
 *
 * 说明：Mem 是"异步作业"驱动——Read/Write/Erase/BlankCheck 只受理作业并立即返回，
 *       真正的硬件动作在 Mem_MainFunction 里推进，结果由 Mem_GetJobResult 读取
 *       （[SWS_Mem_00066]）。本头文件不 include 任何 HPM SDK 头，可独立用于 BSW 侧编译。
 */
#ifndef MEM_H
#define MEM_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Mcal_ModuleId.h"
#include "Mcal_Version.h"   /* MCAL_VENDOR_ID */

/* ---- 版本信息（AUTOSAR BSW 版本宏） ---- */
#define MEM_VENDOR_ID                      (MCAL_VENDOR_ID)
#define MEM_AR_RELEASE_MAJOR_VERSION       (4u)
#define MEM_AR_RELEASE_MINOR_VERSION       (4u)
#define MEM_AR_RELEASE_REVISION_VERSION    (0u)
#define MEM_SW_MAJOR_VERSION               (1u)
#define MEM_SW_MINOR_VERSION               (0u)
#define MEM_SW_PATCH_VERSION               (0u)

/* ---- Service ID（SWS_Mem §8.3 每个 API 的 "Service ID [hex]"） ---- */
#define MEM_API_ID_INIT                    (0x01u)
#define MEM_API_ID_GET_VERSION_INFO        (0x02u)
#define MEM_API_ID_MAIN_FUNCTION           (0x03u)
#define MEM_API_ID_GET_JOB_RESULT          (0x04u)
#define MEM_API_ID_READ                    (0x05u)
#define MEM_API_ID_WRITE                   (0x06u)
#define MEM_API_ID_ERASE                   (0x07u)
#define MEM_API_ID_PROPAGATE_ERROR         (0x08u)
#define MEM_API_ID_BLANK_CHECK             (0x09u)
#define MEM_API_ID_HW_SPECIFIC_SERVICE     (0x0Au)
#define MEM_API_ID_DEINIT                  (0x0Bu)
#define MEM_API_ID_SUSPEND                 (0x0Cu)
#define MEM_API_ID_RESUME                  (0x0Du)

/* ---- 开发错误码（SWS_Mem §7.5.1 [SWS_Mem_00052]） ---- */
#define MEM_E_UNINIT                       (0x01u)
#define MEM_E_PARAM_POINTER                (0x02u)
#define MEM_E_PARAM_ADDRESS                (0x03u)
#define MEM_E_PARAM_LENGTH                 (0x04u)
#define MEM_E_PARAM_INSTANCE_ID            (0x05u)
#define MEM_E_JOB_PENDING                  (0x06u)
/* 本工程扩展（SWS 错误表未定义，用于 Init 阶段硬件配置失败；见 README "待确认项"） */
#define MEM_E_INIT_FAILED                  (0x07u)

/* ---- 返回值扩展：SWS_Mem 在 Mem_Suspend/Resume 等 API 的返回值里引用该符号，
 *      但 SWS 未给数值。按 Vector/EB Mem 驱动惯例取 0x02（E_NOT_OK 之外的第三个值），
 *      见 docs/chapters/48-mcal-mem-crc.md "待确认项"。 ---- */
#ifndef E_MEM_SERVICE_NOT_AVAIL
#define E_MEM_SERVICE_NOT_AVAIL            ((Std_ReturnType)0x02u)
#endif

/* ---- 8.2 类型定义（[SWS_Mem_10002..10026]） ---- */
typedef uint32 Mem_AddressType;      /* 物理存储器地址（本芯片 <4GB，内部用 32 位）*/
typedef uint8  Mem_DataType;         /* 读写用户缓冲区元素类型 */
typedef uint32 Mem_InstanceIdType;   /* 存储器驱动实例 ID */
typedef uint32 Mem_LengthType;       /* 物理长度（字节） */
typedef uint32 Mem_HwServiceIdType;  /* 硬件专有服务请求标识 */

/* ---- 作业结果（[SWS_MemAcc_91016] MemAcc_MemJobResultType 的取值；
 *      本工程不引入 MemAcc，按同一数值在本头文件声明同名枚举，语义与 SWS 一致） ---- */
typedef enum
{
    MEM_JOB_OK            = 0x00u,   /* 上一个作业成功完成 */
    MEM_JOB_PENDING       = 0x01u,   /* 作业正在处理  */
    MEM_JOB_FAILED        = 0x02u,   /* 作业失败（未指明原因） */
    MEM_INCONSISTENT      = 0x03u,   /* 空白检查发现非空 */
    MEM_ECC_UNCORRECTED   = 0x04u,   /* 不可纠正 ECC 错误 */
    MEM_ECC_CORRECTED     = 0x05u    /* 可纠正 ECC 错误（已纠正并继续） */
} Mem_JobResultType;

/* ---- 硬件专有服务 ID（[SWS_Mem_00053] 所有硬件专有功能都必须走
 *      Mem_HwSpecificService 分发，不允许另开 API） ---- */
#define MEM_HW_SERVICE_ID_GET_GEOMETRY     (0x0001u) /* 出参 4 个 32 位字：base/size/sector/page */
#define MEM_HW_SERVICE_ID_GET_PROGRESS     (0x0002u) /* 出参 2 个 32 位字：已处理字节/总字节 */

/* ---- 单个实例的配置（post-build，来自 Mem_PBcfg.c） ---- */
typedef struct
{
    Mem_AddressType baseAddress;       /* 该存储器设备在 CPU 地址空间的映射基址 */
    Mem_AddressType size;              /* 设备总容量（字节） */
    Mem_AddressType eraseWindowStart;  /* 允许擦/写/空白检查的窗口起址（安全窗口，防止覆盖 app） */
    Mem_AddressType eraseWindowEnd;    /* 窗口止址（不含） */
    Mem_LengthType  sectorSize;        /* 擦除粒度（字节） */
    Mem_LengthType  pageSize;          /* 编程粒度（字节） */
    Mem_LengthType  readChunk;         /* 每次 Mem_MainFunction 读取的字节数（决定单次最长阻塞） */
    boolean         eccSupported;      /* 该设备是否提供可读的 ECC 状态 */
} Mem_InstanceConfigType;

/* ---- post-build 配置结构（[SWS_Mem_10000]） ---- */
typedef struct
{
    uint8                            numInstances;      /* 实例个数（<= MEM_MAX_INSTANCES） */
    uint32                           hwBaseAddress;     /* 存储器控制器（XPI）基址，PBcfg 给 */
    uint32                           hwCfgOptionHeader; /* ROM API 自动配置头（板级宏） */
    uint32                           hwCfgOption0;
    uint32                           hwCfgOption1;
    const Mem_InstanceConfigType    *instances;         /* 实例表 */
} Mem_ConfigType;

/* ---- 8.3 API（函数名/参数/返回类型严格照 SWS_Mem §8.3） ---- */
void              Mem_Init(const Mem_ConfigType *configPtr);
void              Mem_DeInit(void);
void              Mem_GetVersionInfo(Std_VersionInfoType *versionInfoPtr);
Mem_JobResultType Mem_GetJobResult(Mem_InstanceIdType instanceId);
Std_ReturnType    Mem_Read(Mem_InstanceIdType instanceId, Mem_AddressType sourceAddress,
                           Mem_DataType *destinationDataPtr, Mem_LengthType length);
Std_ReturnType    Mem_Write(Mem_InstanceIdType instanceId, Mem_AddressType targetAddress,
                            const Mem_DataType *sourceDataPtr, Mem_LengthType length);
Std_ReturnType    Mem_Erase(Mem_InstanceIdType instanceId, Mem_AddressType targetAddress,
                            Mem_LengthType length);
Std_ReturnType    Mem_BlankCheck(Mem_InstanceIdType instanceId, Mem_AddressType targetAddress,
                                 Mem_LengthType length);
Std_ReturnType    Mem_Suspend(Mem_InstanceIdType instanceId);
Std_ReturnType    Mem_Resume(Mem_InstanceIdType instanceId);
void              Mem_PropagateError(Mem_InstanceIdType instanceId);
Std_ReturnType    Mem_HwSpecificService(Mem_InstanceIdType instanceId, Mem_HwServiceIdType hwServiceId,
                                        Mem_DataType *dataPtr, Mem_LengthType *lengthPtr);
void              Mem_MainFunction(void);

/* ---- 本工程扩展（非 AUTOSAR API，仅供例程/测试观察内部状态） ---- */
boolean Mem_IsJobPending(void);
uint32  Mem_GetInstanceCount(void);

#endif /* MEM_H */
