/*
 * Can.h —— AUTOSAR CAN Driver（模块 ID 80）公共接口
 *
 * 依据：AUTOSAR_CP_SWS_CANDriver（R25-11），本地抽取文本
 *       autosar_txt/AUTOSAR_CP_SWS_CANDriver.txt
 * 硬件：HPM5E31 内置 4 路 Bosch M_CAN（HPM_MCAN0~3），板级映射见 Can_PBcfg.c。
 * 例程：samples/mcal/can_classic_echo、can_fd_echo、can_irq_perf、can_busoff、can_filter。
 * 文档：docs/chapters/47-mcal-can.md。
 *
 * 约束（契约 §3.2）：本文件不 include 任何 HPM SDK 头，实例/引脚/时钟/中断号
 * 一律经 Can_PBcfg.c 的 post-build 配置注入。
 */
#ifndef CAN_H
#define CAN_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Mcal_ModuleId.h"
#include "Can_Cfg.h"
#include "Can_GeneralTypes.h"

/* ========================================================================
 * 版本信息（SWS_Can_00143 / SWS_BSWGeneral §5.1）
 * 模块 ID CAN_MODULE_ID（=80）统一由 mcal/common/Mcal_ModuleId.h 提供，
 * 不在本文件重复定义（避免与公共头冲突，契约 §3.4）。
 * ======================================================================== */

/* ========================================================================
 * API Service ID（SWS_CANDriver §8.3 各函数 "Service ID [hex]"）
 * 与 SWS 保持一字不差，便于 DET 追踪表比对
 * ======================================================================== */
#define CAN_API_ID_INIT                          (0x00U) /*!< Can_Init */
#define CAN_API_ID_MAIN_FUNCTION_WRITE           (0x01U) /*!< Can_MainFunction_Write */
#define CAN_API_ID_SET_CONTROLLER_MODE           (0x03U) /*!< Can_SetControllerMode */
#define CAN_API_ID_DISABLE_CONTROLLER_INTERRUPTS (0x04U) /*!< Can_DisableControllerInterrupts */
#define CAN_API_ID_ENABLE_CONTROLLER_INTERRUPTS  (0x05U) /*!< Can_EnableControllerInterrupts */
#define CAN_API_ID_WRITE                         (0x06U) /*!< Can_Write */
#define CAN_API_ID_GET_VERSION_INFO              (0x07U) /*!< Can_GetVersionInfo */
#define CAN_API_ID_MAIN_FUNCTION_READ            (0x08U) /*!< Can_MainFunction_Read */
#define CAN_API_ID_MAIN_FUNCTION_BUSOFF          (0x09U) /*!< Can_MainFunction_BusOff */
#define CAN_API_ID_MAIN_FUNCTION_WAKEUP          (0x0AU) /*!< Can_MainFunction_Wakeup */
#define CAN_API_ID_CHECK_WAKEUP                  (0x0BU) /*!< Can_CheckWakeup */
#define CAN_API_ID_MAIN_FUNCTION_MODE            (0x0CU) /*!< Can_MainFunction_Mode */
#define CAN_API_ID_SET_BAUDRATE                  (0x0FU) /*!< Can_SetBaudrate */
#define CAN_API_ID_DEINIT                        (0x10U) /*!< Can_DeInit */
#define CAN_API_ID_GET_CONTROLLER_ERROR_STATE    (0x11U) /*!< Can_GetControllerErrorState */
#define CAN_API_ID_GET_CONTROLLER_MODE           (0x12U) /*!< Can_GetControllerMode */
#define CAN_API_ID_GET_CONTROLLER_RX_ERROR_COUNTER (0x30U) /*!< Can_GetControllerRxErrorCounter */
#define CAN_API_ID_GET_CONTROLLER_TX_ERROR_COUNTER (0x31U) /*!< Can_GetControllerTxErrorCounter */

/* ========================================================================
 * 开发错误码（SWS_CANDriver §7.12.1）
 * ======================================================================== */
#define CAN_E_PARAM_POINTER        (0x01U) /*!< API 参数指针为空 */
#define CAN_E_PARAM_HANDLE         (0x02U) /*!< API 参数句柄越界 */
#define CAN_E_PARAM_DATA_LENGTH    (0x03U) /*!< 数据长度非法 */
#define CAN_E_PARAM_CONTROLLER     (0x04U) /*!< 控制器号越界 */
#define CAN_E_UNINIT               (0x05U) /*!< 未初始化就调用 */
#define CAN_E_TRANSITION           (0x06U) /*!< 当前状态不允许该状态迁移 */
#define CAN_E_PARAM_BAUDRATE       (0x07U) /*!< 波特率配置 ID 非法 */
#define CAN_E_INIT_FAILED          (0x09U) /*!< 硬件初始化失败 */
#define CAN_E_PARAM_LPDU           (0x0AU) /*!< PDU ID 非法 */

/* ========================================================================
 * 运行时错误码（SWS_CANDriver §7.12.2）
 * ======================================================================== */
#define CAN_E_DATALOST             (0x01U) /*!< 收到但被覆盖/丢失的报文 */

/* ========================================================================
 * 帧格式辅助宏（Can_IdType 高 2 位）
 * ======================================================================== */
#define CAN_ID_IS_EXTENDED(id)  (((id) & CAN_ID_EXT_FRAME) != 0U)
#define CAN_ID_IS_FD(id)        (((id) & CAN_ID_FD_STD_FRAME) != 0U)
#define CAN_ID_RAW_VALUE(id)    ((id) & CAN_ID_VALUE_MASK)

/* ========================================================================
 * Post-build 配置类型（契约 §5.3：板级/实例/时序参数只在 Can_PBcfg.c 实例化）
 * ======================================================================== */

/*! 波特率位时序参数（物理值，毫 bps；驱动换算成 NBTP/DBTP 的 N-1 编码） */
typedef struct {
    uint32  Baudrate;        /*!< 仲裁段（标称）波特率，bit/s，如 500000 */
    uint16  Prescaler;       /*!< 标称段预分频，1..512 */
    uint16  Seg1;            /*!< 标称段 Seg1（不含同步段），1..256 */
    uint16  Seg2;            /*!< 标称段 Seg2，2..128 */
    uint16  Sjw;             /*!< 标称段 SJW，1..128 */
    boolean FdEnabled;       /*!< 该配置是否启用 CAN FD（CCCR.FDOE/BRSE） */
    boolean BrsEnabled;      /*!< 发送 FD 帧时是否置 BRS（数据段加速） */
    uint32  DataBaudrate;    /*!< FD 数据段波特率，bit/s */
    uint16  DataPrescaler;   /*!< 数据段预分频，1..32（开 TDC 时必须 <=2） */
    uint16  DataSeg1;        /*!< 数据段 Seg1，1..32 */
    uint16  DataSeg2;        /*!< 数据段 Seg2，2..16 */
    uint16  DataSjw;         /*!< 数据段 SJW，1..16 */
    uint8   TdcOffset;       /*!< TDC 二次采样点偏移（mtq），0..127；0 = 关 TDC */
    uint8   TdcFilter;       /*!< TDC 滤波窗口（mtq），0..127 */
} Can_BaudrateConfigType;

/*! 一条带 ID 的波特率配置（对应 CanControllerBaudrateConfig） */
typedef struct {
    uint16                   ConfigId; /*!< Can_SetBaudrate 的 BaudRateConfigID */
    Can_BaudrateConfigType   Timing;
} Can_BaudrateConfigItemType;

/*! 控制器（Controller）配置：实例/时钟/引脚/消息 RAM/过滤器规模 */
typedef struct {
    uint8   ControllerId;             /*!< 0..CAN_CONTROLLER_COUNT-1（=抽象 CanIf ControllerId） */
    void   *BaseAddress;              /*!< MCAN 寄存器基址（以 void* 传递，公共头不依赖 SDK） */
    uint32  IrqNumber;                /*!< 中断号（IRQn_MCANx 的数值，由 PBcfg 给出） */
    uint32  MsgBufAddress;            /*!< AHB SRAM 消息 RAM 基址 */
    uint32  MsgBufSize;               /*!< 消息 RAM 字节数 */

    uint8   DedicatedTxBufCount;      /*!< 专用发送缓冲个数 */
    uint8   TxFifoQueueCount;         /*!< 发送 FIFO/队列缓冲个数 */
    uint8   RxFifo0Count;             /*!< RXFIFO0 元素个数 */
    uint8   RxFifo1Count;             /*!< RXFIFO1 元素个数 */
    uint8   RxFifo0Watermark;         /*!< RXFIFO0 水位（0=关闭） */
    uint8   RxFifo1Watermark;         /*!< RXFIFO1 水位（0=关闭） */
    uint8   StdFilterCount;           /*!< 标准 ID 过滤器元素个数 */
    uint8   ExtFilterCount;           /*!< 扩展 ID 过滤器元素个数 */
    uint8   TxEvtFifoCount;           /*!< 发送事件 FIFO 元素个数（0=不使用） */
    uint8   TxEvtFifoWatermark;       /*!< 发送事件 FIFO 水位 */

    uint8   AcceptNonMatchingStd;     /*!< 未命中过滤器的标准帧：0=进FIFO0 1=进FIFO1 2=丢弃 */
    uint8   AcceptNonMatchingExt;     /*!< 未命中过滤器的扩展帧：同上 */
    boolean RejectRemoteStd;          /*!< 丢弃标准远程帧 */
    boolean RejectRemoteExt;          /*!< 丢弃扩展远程帧 */

    boolean EnableEdgeFiltering;      /*!< CCCR.EFBI：RX 边沿滤波（抗毛刺） */
    boolean UseTimestampCounter;      /*!< 使用内部 16 位时间戳计数器 */

    const Can_BaudrateConfigItemType *BaudrateConfigs; /*!< 波特率配置表 */
    uint8   BaudrateConfigCount;      /*!< 表项个数 */
    uint16  DefaultBaudrateConfigId;  /*!< Can_Init 使用的默认配置 ID */

    /* 板级动作（实现在 Can_PBcfg.c，驱动只调用，保证零硬编码） */
    void  (*PinInit)(void);           /*!< 引脚复用初始化 */
    uint32 (*ClockInit)(void);        /*!< 外设时钟初始化，返回 Hz */
    void  (*TransceiverEnable)(void); /*!< 收发器 STB 使能（进正常模式） */
} Can_ControllerConfigType;

/*! 发送硬件对象 HTH：一个 HTH 对应控制器的一个专用发送缓冲 */
typedef struct {
    uint8 ControllerId;   /*!< 所属控制器 */
    uint8 TxBufferIndex;  /*!< 专用发送缓冲序号 */
} Can_HthConfigType;

/*! 接收硬件对象 HRH：一个 HRH 对应控制器的一个 RX FIFO */
typedef struct {
    uint8 ControllerId;   /*!< 所属控制器 */
    uint8 RxFifoIndex;    /*!< 0 = RXFIFO0，1 = RXFIFO1 */
} Can_HrhConfigType;

/*! 过滤器元素（对应 M_CAN SIDFC/XIDFC 元素） */
#define CAN_FILTER_LIST_STD          (0U) /*!< 标准 ID 过滤器列表 */
#define CAN_FILTER_LIST_EXT          (1U) /*!< 扩展 ID 过滤器列表 */
#define CAN_FILTER_TYPE_RANGE        (0U) /*!< 范围过滤 [Id1, Id2] */
#define CAN_FILTER_TYPE_DUAL         (1U) /*!< 双 ID 精确匹配 {Id1, Id2} */
#define CAN_FILTER_TYPE_MASK         (2U) /*!< 掩码过滤 Id1 & Id2（Id2 为掩码，1=必须匹配） */
#define CAN_FILTER_TYPE_DISABLED     (3U) /*!< 过滤器元素关闭 */
#define CAN_FILTER_STORE_FIFO0       (0U) /*!< 命中后存入 RXFIFO0 */
#define CAN_FILTER_STORE_FIFO1       (1U) /*!< 命中后存入 RXFIFO1 */
#define CAN_FILTER_STORE_REJECT      (2U) /*!< 命中后丢弃 */

typedef struct {
    uint8  ControllerId;   /*!< 所属控制器 */
    uint8  FilterList;     /*!< CAN_FILTER_LIST_STD / EXT */
    uint8  ElementIndex;   /*!< 在对应列表中的元素序号 */
    uint8  FilterType;     /*!< CAN_FILTER_TYPE_* */
    uint8  StoreTarget;    /*!< CAN_FILTER_STORE_* */
    uint32 Id1;            /*!< 起始 ID / ID1 / 过滤 ID */
    uint32 Id2;            /*!< 结束 ID / ID2 / 掩码 */
} Can_FilterConfigType;

/*! CAN 驱动总配置（post-build，常量实例 Can_Config 在 Can_PBcfg.c） */
typedef struct {
    const Can_ControllerConfigType *Controllers; /*!< 控制器表 */
    uint8 ControllerCount;                       /*!< 控制器个数 */
    const Can_HthConfigType *Hths;               /*!< 发送对象表 */
    uint8 HthCount;                              /*!< 发送对象个数 */
    const Can_HrhConfigType *Hrhs;               /*!< 接收对象表 */
    uint8 HrhCount;                              /*!< 接收对象个数 */
    const Can_FilterConfigType *Filters;         /*!< 过滤器表 */
    uint8 FilterCount;                           /*!< 过滤器元素个数 */
} Can_ConfigType;

/* ========================================================================
 * API 原型（SWS_CANDriver §8.3）
 * ======================================================================== */

/* --- 影响整个硬件单元的服务 --- */
void Can_Init(const Can_ConfigType *Config);
void Can_DeInit(void);
void Can_GetVersionInfo(Std_VersionInfoType *versioninfo);
Std_ReturnType Can_SetBaudrate(uint8 Controller, uint16 BaudRateConfigID);
Std_ReturnType Can_SetControllerMode(uint8 Controller, Can_ControllerStateType Transition);
void Can_DisableControllerInterrupts(uint8 Controller);
void Can_EnableControllerInterrupts(uint8 Controller);
Std_ReturnType Can_CheckWakeup(uint8 Controller);
Std_ReturnType Can_GetControllerErrorState(uint8 Controller, Can_ErrorStateType *ErrorStatePtr);
Std_ReturnType Can_GetControllerMode(uint8 Controller, Can_ControllerStateType *ControllerModePtr);
Std_ReturnType Can_GetControllerRxErrorCounter(uint8 Controller, uint8 *RxErrorCounterPtr);
Std_ReturnType Can_GetControllerTxErrorCounter(uint8 Controller, uint8 *TxErrorCounterPtr);

/* --- 影响硬件句柄的服务 --- */
Std_ReturnType Can_Write(Can_HwHandleType Hth, const Can_PduType *PduInfo);

/* --- 周期性主函数（由 CanIf/OS 周期调度） --- */
void Can_MainFunction_Write(void);
void Can_MainFunction_Read(void);
void Can_MainFunction_BusOff(void);
void Can_MainFunction_Wakeup(void);
void Can_MainFunction_Mode(void);

/* ========================================================================
 * 上层回调（CanIf 提供；本工程给 weak 桩，例程可覆盖做计数/打印）
 * 依据：SWS_CANDriver §8.4 "Expected Interfaces"
 * ======================================================================== */
void CanIf_TxConfirmation(PduIdType CanTxPduId);
void CanIf_RxIndication(const Can_HwType *Mailbox, const PduInfoType *PduInfoPtr);
void CanIf_ControllerBusOff(uint8 ControllerId);
void CanIf_ControllerModeIndication(uint8 ControllerId, Can_ControllerStateType ControllerMode);
void CanIf_ControllerWakeup(uint8 ControllerId);
void CanIf_ControllerErrorStatePassive(uint8 ControllerId, uint16 RxErrorCounter, uint16 TxErrorCounter);
void CanIf_ErrorNotification(uint8 ControllerId, Can_ErrorType CanError);

#endif /* CAN_H */
