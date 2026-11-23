/*
 * Can_GeneralTypes.h —— AUTOSAR CAN 通用类型（SWS_CANDriver §8.2 / Can_GeneralTypes.h）
 *
 * 依据：AUTOSAR_CP_SWS_CANDriver（R25-11）第 8.2 节；
 *       本地抽取文本 autosar_txt/AUTOSAR_CP_SWS_CANDriver.txt
 *       §8.2.2 Can_PduType、§8.2.3 Can_IdType、§8.2.4 Can_HwHandleType、
 *       §8.2.5 Can_HwType、§8.2.6 Std_ReturnType 扩展（CAN_BUSY）、
 *       §8.2.7 Can_ErrorStateType、§8.2.8 Can_ControllerStateType、§8.2.9 Can_ErrorType。
 *
 * 说明：
 *  1. 本文件只依赖 Std_Types.h / Mcal_Cfg.h，不含任何 HPM SDK 头（契约 §3.2）；
 *  2. AUTOSAR 规定 Can_PduType 里的 swPduHandle 类型 PduIdType 来自 ComStack_Types.h，
 *     本仓库 mcal/common 暂未提供该头，因此这里做"有则用、无则兜底"处理：
 *     将来 Lead 在 mcal/common 增加 ComStack_Types.h 后，本文件自动切换为官方类型，
 *     不需要改驱动（见第 47 章"待确认项与风险"）。
 */
#ifndef CAN_GENERALTYPES_H
#define CAN_GENERALTYPES_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"

/* ---------------- ComStack 类型：优先用官方头，缺失时兜底 ---------------- */
#if defined(__has_include)
#  if __has_include("ComStack_Types.h")
#    include "ComStack_Types.h"
#    define CAN_COMSTACK_TYPES_AVAILABLE STD_ON
#  endif
#endif
#ifndef CAN_COMSTACK_TYPES_AVAILABLE
#define CAN_COMSTACK_TYPES_AVAILABLE STD_OFF
typedef uint16 PduIdType;         /*!< 兜底：L-PDU 句柄（CanIf 侧 PduId） */
typedef uint16 PduLengthType;     /*!< 兜底：SDU 长度类型 */
typedef struct {
    uint8         *SduDataPtr;    /*!< SDU 数据指针 */
    PduLengthType  SduLength;     /*!< SDU 长度 */
} PduInfoType;                    /*!< 兜底：PDU 信息（CanIf_RxIndication 用） */
#endif

/* ---------------- Can_IdType（SWS_Can_00416） ----------------
 * 32 位 ID，最高 2 位表示帧类型：
 *   00 = CAN 2.0 标准帧  01 = CAN FD 标准帧
 *   10 = CAN 2.0 扩展帧  11 = CAN FD 扩展帧
 */
typedef uint32 Can_IdType;

#define CAN_ID_TYPE_MASK        ((Can_IdType)0xC0000000UL) /*!< 帧类型掩码 */
#define CAN_ID_STD_FRAME        ((Can_IdType)0x00000000UL) /*!< CAN 2.0 标准帧 */
#define CAN_ID_FD_STD_FRAME     ((Can_IdType)0x40000000UL) /*!< CAN FD 标准帧 */
#define CAN_ID_EXT_FRAME        ((Can_IdType)0x80000000UL) /*!< CAN 2.0 扩展帧 */
#define CAN_ID_FD_EXT_FRAME     ((Can_IdType)0xC0000000UL) /*!< CAN FD 扩展帧 */
#define CAN_ID_VALUE_MASK       ((Can_IdType)0x1FFFFFFFUL) /*!< 29 位 ID 值掩码 */
#define CAN_ID_STD_VALUE_MASK   ((Can_IdType)0x000007FFUL) /*!< 11 位标准 ID 掩码 */
#define CAN_ID_EXT_VALUE_MASK   ((Can_IdType)0x1FFFFFFFUL) /*!< 29 位扩展 ID 掩码 */

/* ---------------- Can_HwHandleType（SWS_Can_00429） ----------------
 * 硬件对象句柄（HTH/HRH）。本工程 4 路 MCAN，HTH/HRH 总数 < 256，用 8 位扩展为 16 位
 * 便于将来扩容；AUTOSAR 允许 Baseline 用 uint8、Extended 用 uint16。
 */
typedef uint16 Can_HwHandleType;

/* ---------------- Can_ReturnType（SWS_Can_00039，Std_ReturnType 扩展） ---------------- */
typedef uint8 Can_ReturnType;
#define CAN_OK          ((Can_ReturnType)0x00U) /*!< 请求被接受 */
#define CAN_NOT_OK      ((Can_ReturnType)0x01U) /*!< 发生开发错误/请求被拒绝 */
#define CAN_BUSY        ((Can_ReturnType)0x02U) /*!< 无可用发送对象（Can_Write 专有） */

/* ---------------- Can_PduType（SWS_Can_00415） ---------------- */
typedef struct {
    PduIdType   swPduHandle; /*!< L-PDU 句柄，发送确认时原样回传 */
    uint8       length;      /*!< SDU 字节数，0..8（CAN 2.0）/ 0..64（CAN FD） */
    Can_IdType  id;          /*!< 帧 ID + 帧类型（见上） */
    uint8      *sdu;         /*!< SDU 数据指针 */
} Can_PduType;

/* ---------------- Can_HwType（SWS_CAN_00496）：接收指示的邮箱信息 ---------------- */
typedef struct {
    Can_IdType       CanId;       /*!< 收到的 CAN ID（含帧类型位） */
    Can_HwHandleType Hoh;         /*!< 接收硬件对象句柄（HRH） */
    uint8            ControllerId;/*!< 抽象 CanIf 控制器号 */
} Can_HwType;

/* ---------------- Can_ControllerStateType（SWS_Can_91013） ---------------- */
typedef enum {
    CAN_CS_UNINIT  = 0x00U, /*!< 控制器未初始化 */
    CAN_CS_STARTED = 0x01U, /*!< 控制器参与总线通信 */
    CAN_CS_STOPPED = 0x02U, /*!< 控制器停止参与总线通信 */
    CAN_CS_SLEEP   = 0x03U  /*!< 控制器睡眠（总线唤醒检测中） */
} Can_ControllerStateType;

/* ---------------- Can_ErrorStateType（SWS_Can_91003） ---------------- */
typedef enum {
    CAN_ERRORSTATE_ACTIVE  = 0x00U, /*!< Error Active：TEC<96 且 REC<96 */
    CAN_ERRORSTATE_PASSIVE = 0x01U, /*!< Error Passive：TEC>=128 或 REC>=128 */
    CAN_ERRORSTATE_BUSOFF  = 0x02U  /*!< Bus-Off：TEC>=256，脱离总线 */
} Can_ErrorStateType;

/* ---------------- Can_ErrorType（SWS_Can_91021） ---------------- */
typedef enum {
    CAN_ERROR_BIT_MONITORING1  = 0x01U, /*!< 发 0 读回 1 */
    CAN_ERROR_BIT_MONITORING0  = 0x02U, /*!< 发 1 读回 0 */
    CAN_ERROR_BIT              = 0x03U, /*!< 位错误（硬件不区分上述两种） */
    CAN_ERROR_CHECK_ACK_FAILED = 0x04U, /*!< ACK 检查失败 */
    CAN_ERROR_ACK_DELIMITER    = 0x05U, /*!< ACK 界定符检查失败 */
    CAN_ERROR_ARBITRATION_LOST = 0x06U, /*!< 仲裁丢失 */
    CAN_ERROR_OVERLOAD         = 0x07U, /*!< 过载帧 */
    CAN_ERROR_CHECK_FORM_FAILED= 0x08U, /*!< 固定位场格式错 */
    CAN_ERROR_CHECK_STUFFING_FAILED = 0x09U, /*!< 位填充错 */
    CAN_ERROR_CHECK_CRC_FAILED = 0x0AU, /*!< CRC 错 */
    CAN_ERROR_BUS_LOCK         = 0x0BU  /*!< 总线被锁在显性电平 */
} Can_ErrorType;

#endif /* CAN_GENERALTYPES_H */
