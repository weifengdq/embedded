/*
 * Eth.h —— AUTOSAR Ethernet Driver（模块 ID 88）公共接口
 *
 * 依据：AUTOSAR_CP_SWS_EthernetDriver（R25-11），本地抽取文本
 *       autosar_txt/AUTOSAR_CP_SWS_EthernetDriver.txt
 *       PDF: $HOME/tmp2/autosar_pdf/AUTOSAR_CP_SWS_EthernetDriver.pdf
 * 硬件：HPM5E31 ENET0（Synopsys DesignWare GMAC，RGMII）+ YT8011AN 千兆车载 PHY，
 *       板级映射（寄存器基址/中断号/MAC/PHY 地址）见 Eth_PBcfg.c。
 * 例程：samples/mcal/eth_raw_echo、eth_irq、eth_trcv。
 * 文档：docs/chapters/50-mcal-eth.md。
 *
 * 约束（docs/MCAL开发约定.md §3）：
 *   1) 本文件不 include 任何 HPM SDK 头；实例/引脚/时钟/中断号一律经
 *      Eth_PBcfg.c 的 post-build 配置注入（以 void* / uint32 传递）。
 *   2) 类型、函数名、Service ID、DET 错误码与 SWS 保持一致。
 *
 * 数据通路约定（重要，上层 EthIf/lwIP 适配层按此实现）：
 *   TX：Eth_ProvideTxBuffer() 返回的 BufPtr 指向“帧起始”（含 14 字节以太网头的位置），
 *       授予长度 = ETH_MAX_PAYLOAD（1500），单帧上限 ETH_MAX_FRAME_LEN（1518）。提交方式二选一：
 *         a) PhysAddrPtr != NULL（SWS 语义）：驱动在 BufPtr[0..13] 填目的 MAC/源 MAC/Type，
 *            调用方把“净荷”写在 BufPtr+14 起，LenByte = 净荷长度；
 *         b) PhysAddrPtr == NULL（本实现扩展，供 lwIP 直接 memcpy 整帧）：调用方把整帧
 *            （含 14 字节头）写在 BufPtr 起，LenByte = 整帧长度。
 *       驱动负责：必要时补 0 到 60 字节最小帧、cache clean、把描述符交给 DMA。
 *   RX：Eth_Receive() 只报状态（SWS 语义）；取数据用本实现扩展
 *       Eth_GetRxBuffer()（返回 DMA 环内真实缓冲指针，零拷贝），用完必须
 *       Eth_ReleaseRxBuffer() 归还（SWS_Eth_91023，DRAFT）。归还前该缓冲不会被复用。
 */
#ifndef ETH_H
#define ETH_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Mcal_ModuleId.h"
#include "Eth_Cfg.h"
#include "Eth_GeneralTypes.h"

/* ========================================================================
 * 版本信息（SWS_BSWGeneral §5.1；模块 ID 见 Mcal_ModuleId.h 的 ETH_MODULE_ID=88）
 * ======================================================================== */
#define ETH_AR_RELEASE_MAJOR_VERSION    (4U)
#define ETH_AR_RELEASE_MINOR_VERSION    (4U)
#define ETH_AR_RELEASE_REVISION_VERSION (0U)
#define ETH_SW_MAJOR_VERSION            (1U)
#define ETH_SW_MINOR_VERSION            (0U)
#define ETH_SW_PATCH_VERSION            (0U)

/* ========================================================================
 * API Service ID（SWS_Eth §8.4 各函数 "Service ID [hex]"）
 * ======================================================================== */
#define ETH_API_ID_INIT                     (0x01U) /*!< Eth_Init */
#define ETH_API_ID_SET_CONTROLLER_MODE      (0x03U) /*!< Eth_SetControllerMode */
#define ETH_API_ID_GET_CONTROLLER_MODE      (0x04U) /*!< Eth_GetControllerMode */
#define ETH_API_ID_GET_PHYS_ADDR            (0x08U) /*!< Eth_GetPhysAddr */
#define ETH_API_ID_SET_PHYS_ADDR            (0x13U) /*!< Eth_SetPhysAddr */
#define ETH_API_ID_UPDATE_PHYS_ADDR_FILTER  (0x12U) /*!< Eth_UpdatePhysAddrFilter */
#define ETH_API_ID_WRITE_MII                (0xA0U) /*!< Eth_WriteMii */
#define ETH_API_ID_READ_MII                 (0xA1U) /*!< Eth_ReadMii */
#define ETH_API_ID_WRITE_MMD                (0xA2U) /*!< Eth_WriteMmd */
#define ETH_API_ID_READ_MMD                 (0xA3U) /*!< Eth_ReadMmd */
#define ETH_API_ID_GET_COUNTER_VALUES       (0x14U) /*!< Eth_GetCounterValues */
#define ETH_API_ID_GET_RX_STATS             (0x15U) /*!< Eth_GetRxStats */
#define ETH_API_ID_GET_TX_STATS             (0x1CU) /*!< Eth_GetTxStats */
#define ETH_API_ID_GET_TX_ERROR_COUNTER_VALUES (0x1DU) /*!< Eth_GetTxErrorCounterValues */
#define ETH_API_ID_PROVIDE_TX_BUFFER        (0x09U) /*!< Eth_ProvideTxBuffer */
#define ETH_API_ID_TRANSMIT                 (0x0AU) /*!< Eth_Transmit */
#define ETH_API_ID_RECEIVE                  (0x0BU) /*!< Eth_Receive */
#define ETH_API_ID_TX_CONFIRMATION          (0x0CU) /*!< Eth_TxConfirmation */
#define ETH_API_ID_RELEASE_RX_BUFFER        (0x27U) /*!< Eth_ReleaseRxBuffer（DRAFT） */
#define ETH_API_ID_GET_VERSION_INFO         (0x0DU) /*!< Eth_GetVersionInfo */
#define ETH_API_ID_MAIN_FUNCTION            (0x20U) /*!< Eth_MainFunction */
/* 非 SWS 扩展（本实现，见文件头“数据通路约定”） */
#define ETH_API_ID_GET_RX_BUFFER            (0xE1U) /*!< Eth_GetRxBuffer（扩展） */
#define ETH_API_ID_SET_LINE_SPEED           (0xE2U) /*!< Eth_SetLineSpeed（扩展） */
#define ETH_API_ID_GET_RX_ISR_COUNT         (0xE3U) /*!< Eth_GetRxIsrCount（扩展） */
#define ETH_API_ID_GET_LAST_ISR_CYCLE       (0xE4U) /*!< Eth_GetLastIsrCycle（扩展） */

/* ========================================================================
 * 开发错误码（SWS_Eth §7.2.1 SWS_Eth_00016）
 * ======================================================================== */
#define ETH_E_INV_CTRL_IDX       (0x01U) /*!< 控制器索引越界 */
#define ETH_E_UNINIT             (0x02U) /*!< 未初始化就调用 */
#define ETH_E_PARAM_POINTER      (0x03U) /*!< 参数指针为空 */
#define ETH_E_INV_PARAM          (0x04U) /*!< 参数非法（BufIdx/长度等） */
#define ETH_E_INV_MODE           (0x05U) /*!< 控制器模式非法 */
#define ETH_E_INV_CLKUNIT_IDX    (0x06U) /*!< 时钟单元索引越界（本实现不用） */
#define ETH_E_CLOCK_ADJUSTMENT_FAILED (0x07U) /*!< PHC 校正失败（本实现不用） */
#define ETH_E_EXCEED_EGRESS_QUEUE_ELEMENT (0x09U) /*!< 帧超过出口队列元素长度 */
#define ETH_E_HW_SUPPORTED_DATA_TRANSFER_REJECTED (0x0AU) /*!< 硬件卸载请求被拒 */
#define ETH_E_RX_HANDLE_ID_NOT_ASSOCIATED (0x0BU) /*!< RxHandleId 未关联 */

/* ========================================================================
 * 运行时错误码（SWS_Eth §7.2.2 SWS_Eth_91014）
 * ======================================================================== */
#define ETH_E_EGRESS_QUEUE_OCCUPIED   (0x01U) /*!< 出口队列元素耗尽 */
#define ETH_E_INRESS_QUEUE_OCCUPIED   (0x02U) /*!< 入口队列元素耗尽（SWS 原拼写） */
#define ETH_E_COMMUNICATION           (0x06U) /*!< 与以太网控制器通信失败 */
#define ETH_E_UNKNOWN_EGRESS_PRIORITY (0x08U) /*!< 没有该优先级的出口队列 */
#define ETH_E_NO_MATCHING_INGRESS_QUEUE_IDENTIFIED (0x0CU) /*!< 收到的帧没有匹配入口队列 */

/* ========================================================================
 * 缓冲区句柄编码（SWS_Eth_00414 DRAFT：BufIdx 应 > 2^16-1）
 *   句柄 = ETH_BUFIDX_BASE | 索引；索引 0..ETH_TX_BUF_COUNT-1（TX）
 *                               或 0..ETH_RX_BUF_COUNT-1（RX）
 * ======================================================================== */
#define ETH_BUFIDX_BASE          (0x00010000UL) /*!< 句柄基址 */
#define ETH_BUFIDX_INDEX_MASK    (0x0000FFFFUL) /*!< 索引掩码 */
#define ETH_BUFIDX_INVALID       (0xFFFFFFFFUL) /*!< 无效句柄 */
#define ETH_BUFIDX_MAKE(i)       (ETH_BUFIDX_BASE | ((Eth_BufIdxType)(i)))
#define ETH_BUFIDX_INDEX(b)      ((uint16)((b) & ETH_BUFIDX_INDEX_MASK))
#define ETH_BUFIDX_IS_VALID(b)   (((b) & ~ETH_BUFIDX_INDEX_MASK) == ETH_BUFIDX_BASE)

/* ========================================================================
 * 以太网帧常量（不含前导码/SFD；FCS 由 MAC 生成，不计入 LenByte）
 * ======================================================================== */
#define ETH_HEADER_LEN           (14U)  /*!< 目的 MAC(6)+源 MAC(6)+Type/Length(2) */
#define ETH_VLAN_TAG_LEN         (4U)   /*!< 802.1Q 标签 */
#define ETH_MIN_FRAME_LEN        (60U)  /*!< 最小帧长（不含 FCS） */
#define ETH_MAX_PAYLOAD          (1500U) /*!< 以太网净荷上限 */
#define ETH_MAX_FRAME_LEN        (ETH_HEADER_LEN + ETH_VLAN_TAG_LEN + ETH_MAX_PAYLOAD) /*!< 1518 */
#define ETH_FCS_LEN              (4U)   /*!< FCS 长度 */
#define ETH_BROADCAST_ADDR       { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU }

/* ========================================================================
 * Post-build 配置类型（板级/实例参数只在 Eth_PBcfg.c 实例化）
 * ======================================================================== */

/*! 单个以太网控制器配置（对应 SWS EthCtrlConfig） */
typedef struct {
    uint8   CtrlIdx;             /*!< 控制器索引 0..ETH_CTRL_COUNT-1 */
    void   *BaseAddress;         /*!< ENET 寄存器基址（Eth_PBcfg.c 里给 HPM_ENET0） */
    uint32  IrqNumber;           /*!< 中断号（IRQn_ENET0 的数值） */
    uint8   PhyAddr;             /*!< 该控制器的 MDIO/PHY 地址（YT8011AN = 1） */
    uint8   MacAddr[6];          /*!< 上电默认 MAC（Eth_GetPhysAddr 返回它） */
    uint8   DmaPbl;              /*!< DMA 突发长度（DMA_BUS_MODE.PBL） */
    uint8   TxBufCount;          /*!< TX 缓冲个数（<= ETH_TX_BUF_COUNT） */
    uint8   RxBufCount;          /*!< RX 缓冲个数（<= ETH_RX_BUF_COUNT） */
    uint16  TxBufLenByte;        /*!< 单个 TX 缓冲字节数（<= ETH_TX_BUF_SIZE） */
    uint16  RxBufLenByte;        /*!< 单个 RX 缓冲字节数（<= ETH_RX_BUF_SIZE） */
    boolean RxInterruptEnable;   /*!< TRUE = RX 中断模式；FALSE = 轮询 */
    boolean TxInterruptEnable;   /*!< TRUE = TX 完成中断（否则在 MainFunction 轮询） */
    boolean RxChecksumOffload;   /*!< TRUE = 打开 MAC 硬件接收校验和检查 */
    uint32  RxPollBudget;        /*!< 单次 Eth_MainFunction 最多处理的 RX 帧数 */
    Eth_LinkInfoType DefaultLink;/*!< 未接 EthTrcv 时的固定链路参数 */
} Eth_CtrlConfigType;

/*! Eth 驱动 post-build 配置（SWS：Eth_ConfigType，实现相关结构） */
typedef struct {
    const Eth_CtrlConfigType *CtrlConfigPtr; /*!< 控制器配置数组 */
    uint8                     CtrlCount;     /*!< 控制器个数 */
} Eth_ConfigType;

/* ========================================================================
 * 上层（EthIf）回调声明 —— 本仓库没有完整 EthIf，给桩用
 *   开关：Eth_Cfg.h 的 ETH_USE_ETHIF_CALLBACK
 * ======================================================================== */
#if (ETH_USE_ETHIF_CALLBACK == STD_ON)
#ifndef ETHIF_CALLBACKS_DECLARED
#define ETHIF_CALLBACKS_DECLARED
/*! 收到一帧后由 Eth 驱动调用（经典 AUTOSAR EthIf_RxIndication 签名） */
extern void EthIf_RxIndication(uint8 CtrlIdx, Eth_FrameType FrameType, boolean IsBroadcast,
                               const uint8 *PhysAddrPtr, const uint8 *DataPtr, uint16 LenByte);
/*! 发送完成确认（SWS_Eth_00101） */
extern void EthIf_TxConfirmation(uint8 CtrlIdx, Eth_BufIdxType BufIdx, Std_ReturnType Result);
/*! 控制器模式变化（SWS_Eth_00240） */
extern void EthIf_CtrlModeIndication(uint8 CtrlIdx, Eth_ModeType CtrlMode);
#endif
#endif /* ETH_USE_ETHIF_CALLBACK */

/* ========================================================================
 * API 原型（SWS_Eth §8.4）
 * ======================================================================== */

/*! 初始化整个 Eth 驱动（SWS_Eth_00027，Service ID 0x01） */
void Eth_Init(const Eth_ConfigType *CfgPtr);

/*! 控制器模式切换（SWS_Eth_91009，0x03；R25-11 标为异步，本实现同步完成） */
Std_ReturnType Eth_SetControllerMode(uint8 CtrlIdx, Eth_ModeType CtrlMode);

/*! 读控制器模式（SWS_Eth_91010，0x04） */
Std_ReturnType Eth_GetControllerMode(uint8 CtrlIdx, Eth_ModeType *CtrlModePtr);

/*! 读 MAC 地址（SWS_Eth_00052，0x08，网络字节序） */
void Eth_GetPhysAddr(uint8 CtrlIdx, uint8 *PhysAddrPtr);

/*! 写 MAC 地址（SWS_Eth_00151，0x13） */
void Eth_SetPhysAddr(uint8 CtrlIdx, const uint8 *PhysAddrPtr);

/*! MAC 地址过滤表增删（SWS_Eth_00152，0x12；本实现用 MAC 地址槽 + 完美过滤） */
Std_ReturnType Eth_UpdatePhysAddrFilter(uint8 CtrlIdx, const uint8 *PhysAddrPtr,
                                        uint16 VlanId, Eth_FilterActionType Action);

/*! Clause 22 MDIO 写（SWS_Eth_00062，0xA0） */
Std_ReturnType Eth_WriteMii(uint8 CtrlIdx, uint8 TrcvIdx, uint8 RegIdx, uint16 RegVal);

/*! Clause 22 MDIO 读（SWS_Eth_00069，0xA1） */
Std_ReturnType Eth_ReadMii(uint8 CtrlIdx, uint8 TrcvIdx, uint8 RegIdx, uint16 *RegValPtr);

/*! Clause 45 MMD 写（SWS_Eth_00404，0xA2；经 0x0D/0x0E 间接访问） */
Std_ReturnType Eth_WriteMmd(uint8 CtrlIdx, uint8 TrcvIdx, uint8 Mmd, uint16 RegIdx, uint16 RegVal);

/*! Clause 45 MMD 读（SWS_Eth_00403，0xA3） */
Std_ReturnType Eth_ReadMmd(uint8 CtrlIdx, uint8 TrcvIdx, uint8 Mmd, uint16 RegIdx, uint16 *RegValPtr);

/*! 通用计数器（SWS_Eth_00231，0x14） */
Std_ReturnType Eth_GetCounterValues(uint8 CtrlIdx, Eth_CounterType *CounterPtr);

/*! RMON 接收统计（SWS_Eth_91002，0x15） */
Std_ReturnType Eth_GetRxStats(uint8 CtrlIdx, Eth_RxStatsType *RxStats);

/*! 发送统计（SWS_Eth_91003，0x1C） */
Std_ReturnType Eth_GetTxStats(uint8 CtrlIdx, Eth_TxStatsType *TxStats);

/*! 发送错误计数（SWS_Eth_91004，0x1D） */
Std_ReturnType Eth_GetTxErrorCounterValues(uint8 CtrlIdx,
                                           Eth_TxErrorCounterValuesType *TxErrorCounterValues);

/*! 申请发送缓冲（SWS_Eth_00077，0x09）
 *  LenBytePtr: in=期望净荷长度, out=授予长度（=ETH_MAX_PAYLOAD=1500，整帧上限 1518） */
BufReq_ReturnType Eth_ProvideTxBuffer(uint8 CtrlIdx, uint8 Priority, Eth_BufIdxType *BufIdxPtr,
                                      uint8 **BufPtr, uint16 *LenBytePtr);

/*! 提交发送（SWS_Eth_00087，0x0A）
 *  PhysAddrPtr != NULL：驱动填 14 字节头，LenByte=净荷长度（净荷须在 BufPtr+14）
 *  PhysAddrPtr == NULL：整帧已在 BufPtr（含头），LenByte=整帧长度 */
Std_ReturnType Eth_Transmit(uint8 CtrlIdx, Eth_BufIdxType BufIdx, Eth_FrameType FrameType,
                            boolean TxConfirmation, uint16 LenByte, const uint8 *PhysAddrPtr);

/*! 查询接收状态（SWS_Eth_00095，0x0B；不返回数据，数据用 Eth_GetRxBuffer） */
void Eth_Receive(uint8 CtrlIdx, uint8 QueueIdx, Eth_RxStatusType *RxStatusPtr);

/*! 归还接收缓冲（SWS_Eth_91023，0x27 DRAFT）：重新挂回 DMA 环 */
void Eth_ReleaseRxBuffer(uint8 CtrlIdx, Eth_BufIdxType RxHandleId);

/*! 轮询发送完成并回调 EthIf_TxConfirmation（SWS_Eth_00100，0x0C） */
void Eth_TxConfirmation(uint8 CtrlIdx);

/*! 周期函数（SWS_Eth_00171，0x20）：查错、丢帧、控制器模式变化 */
void Eth_MainFunction(void);

/*! 版本信息（SWS_Eth_00106，0x0D） */
void Eth_GetVersionInfo(Std_VersionInfoType *VersionInfoPtr);

/* ========================================================================
 * 本实现扩展 API（非 SWS，见文件头“数据通路约定”；用 ETH_EXT_API_ENABLE 裁剪）
 * ======================================================================== */
#if (ETH_EXT_API_ENABLE == STD_ON)
/*! 取一帧接收数据（零拷贝）：DataPtr 指向 DMA 环缓冲，长度不含 FCS；
 *  取到后必须 Eth_ReleaseRxBuffer(CtrlIdx, *BufIdxPtr) 归还。
 *  返回 ETH_RECEIVED / ETH_RECEIVED_MORE_DATA_AVAILABLE / ETH_NOT_RECEIVED */
Eth_RxStatusType Eth_GetRxBuffer(uint8 CtrlIdx, uint8 QueueIdx, Eth_BufIdxType *BufIdxPtr,
                                 const uint8 **DataPtr, uint16 *LenBytePtr,
                                 Eth_FrameType *FrameTypePtr);

/*! 设置 MAC 侧线速/双工（EthTrcv 链路联动用；内部改 MAC_CONFIG 的 FES/DM 位） */
Std_ReturnType Eth_SetLineSpeed(uint8 CtrlIdx, uint16 SpeedMbit, boolean FullDuplex);

/*! 读 RX 中断进入次数（eth_irq 例程测 ISR 频次用） */
uint32 Eth_GetRxIsrCount(uint8 CtrlIdx);

/*! 读最近一次 RX 中断进入时的 core mcycle（eth_irq 例程测"ISR→消费者"延迟） */
uint32 Eth_GetLastIsrCycle(uint8 CtrlIdx);

/*! 调试转储：MAC/DMA 关键寄存器 + 前 4 个 TX/RX 描述符（排查 OWN 位/使能位用） */
void Eth_DebugDump(uint8 CtrlIdx);
#endif /* ETH_EXT_API_ENABLE */

#endif /* ETH_H */