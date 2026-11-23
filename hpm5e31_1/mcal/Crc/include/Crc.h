/*
 * Crc.h —— AUTOSAR CRC Library（Crc，模块 ID 201）
 *
 * 依据：AUTOSAR_CP_SWS_CRCLibrary（R25-11），本地抽取文本
 *       /home/hpm5e31/temp2/autosar_txt/AUTOSAR_CP_SWS_CRCLibrary.txt
 *       §7.2 多项式参数与标准向量、§7.6 错误分类（"There are no development errors"）、
 *       §8.3 八个 Calculate API + Crc_GetVersionInfo。
 * 硬件：HPM5E31 CRC 外设（基址 0xF0080000，8 个通道，支持自定义 poly/init/refin/refout/
 *       xorout/poly_width，最多 32 位），见 hpm_sdk/drivers/src/hpm_crc_drv.c。
 * 例程：samples/mcal/crc_bench（HW/SW 吞吐 + 标准向量）、samples/mcal/crc_integrity
 *       （Flash 数据 CRC 校验：通过/失败自证）。
 *
 * 重要：SWS §7.6.1 明确 "There are no development errors"，且 [SWS_Crc_00072] 要求
 *       CRC 库函数不得调用任何 BSW 模块（含 Det）。因此本模块的 Calculate API 不做
 *       Det 上报，参数合法性由调用方保证（与 SWS 一致）；Crc_GetVersionInfo 的
 *       NULL 指针只做静默返回。详见第 48 章"待确认项"。
 */
#ifndef CRC_H
#define CRC_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Mcal_ModuleId.h"
#include "Mcal_Version.h"

/* ---- 版本信息 ---- */
#define CRC_VENDOR_ID                      (MCAL_VENDOR_ID)
#define CRC_AR_RELEASE_MAJOR_VERSION       (4u)
#define CRC_AR_RELEASE_MINOR_VERSION       (4u)
#define CRC_AR_RELEASE_REVISION_VERSION    (0u)
#define CRC_SW_MAJOR_VERSION               (1u)
#define CRC_SW_MINOR_VERSION               (0u)
#define CRC_SW_PATCH_VERSION               (0u)

/* ---- Service ID（SWS_Crc §8.3 各 API 的 "Service ID [hex]"） ---- */
#define CRC_API_ID_CALCULATE_CRC8          (0x01u)
#define CRC_API_ID_CALCULATE_CRC16         (0x02u)
#define CRC_API_ID_CALCULATE_CRC32         (0x03u)
#define CRC_API_ID_GET_VERSION_INFO        (0x04u)
#define CRC_API_ID_CALCULATE_CRC8H2F       (0x05u)
#define CRC_API_ID_CALCULATE_CRC32P4       (0x06u)
#define CRC_API_ID_CALCULATE_CRC64         (0x07u)
#define CRC_API_ID_CALCULATE_CRC16ARC      (0x08u)
#define CRC_API_ID_CALCULATE_CRC32P76      (0x09u)

/* ---- 8.3 API（语法/参数顺序/返回宽度严格照 SWS） ---- */
uint8  Crc_CalculateCRC8(const uint8 *Crc_DataPtr, uint32 Crc_Length,
                         uint8 Crc_StartValue8, boolean Crc_IsFirstCall);
uint8  Crc_CalculateCRC8H2F(const uint8 *Crc_DataPtr, uint32 Crc_Length,
                            uint8 Crc_StartValue8H2F, boolean Crc_IsFirstCall);
uint16 Crc_CalculateCRC16(const uint8 *Crc_DataPtr, uint32 Crc_Length,
                          uint16 Crc_StartValue16, boolean Crc_IsFirstCall);
uint16 Crc_CalculateCRC16ARC(const uint8 *Crc_DataPtr, uint32 Crc_Length,
                             uint16 Crc_StartValue16, boolean Crc_IsFirstCall);
uint32 Crc_CalculateCRC32(const uint8 *Crc_DataPtr, uint32 Crc_Length,
                          uint32 Crc_StartValue32, boolean Crc_IsFirstCall);
uint32 Crc_CalculateCRC32P4(const uint8 *Crc_DataPtr, uint32 Crc_Length,
                            uint32 Crc_StartValue32, boolean Crc_IsFirstCall);
uint32 Crc_CalculateCRC32P76(const uint8 *Crc_DataPtr, uint32 Crc_Length,
                             uint32 Crc_StartValue32, boolean Crc_IsFirstCall);
uint64 Crc_CalculateCRC64(const uint8 *Crc_DataPtr, uint32 Crc_Length,
                          uint64 Crc_StartValue64, boolean Crc_IsFirstCall);
void   Crc_GetVersionInfo(Std_VersionInfoType *Versioninfo);

/* ---- post-build 配置（SWS 没有 Crc_Init；配置实例由 PBcfg 提供，
 *      驱动在首次 Calculate 时惰性读取并初始化 CRC 外设） ---- */
typedef struct
{
    uint8  hwAccelEnabled;   /* STD_ON：优先用 CRC 外设；STD_OFF：纯软件表 */
    uint8  hwChannel;        /* CRC 通道号 0..7 */
    uint32 hwBaseAddress;    /* CRC 外设基址（PBcfg 给） */
    uint32 hwClockId;        /* 外设时钟名（PBcfg 给，clock_crc0 的枚举值） */
} Crc_ConfigType;

/* ---- 本工程扩展（非 AUTOSAR API）----
 * 1) Crc_Init：SWS 未定义初始化 API；CRC 外设需要开时钟，故提供幂等的可选初始化，
 *    不调用也可（Calculate 首次调用时会惰性初始化）；
 * 2) Crc_SetHwEnabled / Crc_IsHwActive：bench 例程用来对比 HW/SW 两条路径；
 * 3) Crc_CalculateByAlgo：按算法编号统一分发，便于 bench 与完整性例程复用；
 * 4) Crc_GetHwStats：返回 HW 命中次数/回退次数。 */
typedef enum
{
    CRC_ALGO_CRC8 = 0,
    CRC_ALGO_CRC8H2F,
    CRC_ALGO_CRC16,
    CRC_ALGO_CRC16ARC,
    CRC_ALGO_CRC32,
    CRC_ALGO_CRC32P4,
    CRC_ALGO_CRC32P76,
    CRC_ALGO_CRC64,
    CRC_ALGO_COUNT
} Crc_AlgoType;

void    Crc_Init(void);
void    Crc_SetHwEnabled(boolean enable);
boolean Crc_IsHwActive(Crc_AlgoType algo);
uint64  Crc_CalculateByAlgo(Crc_AlgoType algo, const uint8 *data, uint32 length,
                            uint64 startValue, boolean isFirstCall);
uint32  Crc_GetHwStats(uint32 *hwCalls, uint32 *swFallbacks);
uint32  Crc_HwRawProbe(uint32 variant, uint32 *outPreSet, uint32 *outMisc);
uint32  Crc_GetHwConv(Crc_AlgoType algo, uint32 *convMask);
const char *Crc_GetAlgoName(Crc_AlgoType algo);

#endif /* CRC_H */
