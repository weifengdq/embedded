/*
 * Eth_GeneralTypes.h —— AUTOSAR Ethernet Driver 通用类型（SWS_Eth §8.3）
 *
 * 依据：AUTOSAR_CP_SWS_EthernetDriver（R25-11）§8.3 Type definitions
 *       本地抽取文本：autosar_txt/AUTOSAR_CP_SWS_EthernetDriver.txt
 * 约束：本文件不 include 任何 HPM SDK 头，只依赖 mcal/common 的 AUTOSAR 类型。
 *
 * 说明：BufReq_ReturnType 在 SWS 中由 ComStack_Types.h 导入；本仓库没有该文件，
 *       因此在这里按 ComStack_Types 的定义就地给出，并用 BUFREQ_RETURNTYPE_DEFINED
 *       保护，将来接入完整 ComStack 时由 ComStack_Types.h 优先定义。
 */
#ifndef ETH_GENERALTYPES_H
#define ETH_GENERALTYPES_H

#include "Std_Types.h"

/* ========================================================================
 * 缓冲区请求返回码（SWS_Eth §8.2 imported types / ComStack_Types.h）
 * ======================================================================== */
#ifndef BUFREQ_RETURNTYPE_DEFINED
#define BUFREQ_RETURNTYPE_DEFINED
typedef enum {
    BUFREQ_OK       = 0x00U,  /*!< 缓冲区申请成功 */
    BUFREQ_E_NOT_OK = 0x01U,  /*!< 申请被拒绝 */
    BUFREQ_E_BUSY   = 0x02U,  /*!< 所有缓冲区都在使用 */
    BUFREQ_E_OVFL   = 0x03U   /*!< 请求长度超过单帧上限 */
} BufReq_ReturnType;
#endif

/* ========================================================================
 * Eth_ModeType（SWS_Eth_91011）
 * 注：R25-11 新增 ACTIVE_WITH_WAKEUP_REQUEST / ACTIVE_TX_OFFLINE
 * ======================================================================== */
typedef enum {
    ETH_MODE_DOWN                       = 0x00U, /*!< 关闭 Rx/Tx，硬件可进低功耗 */
    ETH_MODE_ACTIVE                     = 0x01U, /*!< 打开 Rx/Tx */
    ETH_MODE_ACTIVE_WITH_WAKEUP_REQUEST = 0x02U, /*!< 打开 Rx/Tx 并请求网络唤醒 */
    ETH_MODE_ACTIVE_TX_OFFLINE          = 0x03U  /*!< 只收不发（EthIf 静默通信用） */
} Eth_ModeType;

/* ========================================================================
 * Eth_StateType（SWS_Eth_00159）
 * ======================================================================== */
typedef enum {
    ETH_STATE_UNINIT = 0x00U, /*!< 驱动未初始化 */
    ETH_STATE_INIT   = 0x01U  /*!< 驱动已初始化（配置已装载） */
} Eth_StateType;

/* ========================================================================
 * Eth_FrameType（SWS_Eth_00160）：Ethernet 帧头里的 Type/Length 字段
 * ======================================================================== */
typedef uint16 Eth_FrameType;

/* ========================================================================
 * Eth_DataType（SWS_Eth_00161）：帧数据单元
 * 本实现按 8 位 CPU 取 uint8（SWS 允许 8/16/32 位 CPU 用 uint8）
 * ======================================================================== */
typedef uint8 Eth_DataType;

/* ========================================================================
 * Eth_BufIdxType（SWS_Eth_00175）：缓冲区句柄
 * 本实现取值 = ETH_BUFIDX_BASE | 索引，满足 SWS_Eth_00414（DRAFT）
 * "返回值应大于 2^16-1" 的约束，见 Eth.h 的 ETH_BUFIDX_* 宏。
 * ======================================================================== */
typedef uint32 Eth_BufIdxType;

/* ========================================================================
 * Eth_RxStatusType（SWS_Eth_00162）
 * ======================================================================== */
typedef enum {
    ETH_RECEIVED                     = 0x00U, /*!< 收到一帧，队列已空 */
    ETH_NOT_RECEIVED                 = 0x01U, /*!< 没收到帧 */
    ETH_RECEIVED_MORE_DATA_AVAILABLE = 0x02U  /*!< 收到一帧，队列里还有 */
} Eth_RxStatusType;

/* ========================================================================
 * Eth_FilterActionType（SWS_Eth_00163）
 * ======================================================================== */
typedef enum {
    ETH_ADD_TO_FILTER      = 0x00U, /*!< 把该 MAC 加入接收过滤表（允许收） */
    ETH_REMOVE_FROM_FILTER = 0x01U  /*!< 从过滤表删除（禁止收） */
} Eth_FilterActionType;

/* ========================================================================
 * Eth_MacVlanType（SWS_Eth_91001）：交换机 ARL 表条目，本实现不使用
 * ======================================================================== */
typedef struct {
    uint8  MacAddr[6];  /*!< MAC 地址 */
    uint16 VlanId;      /*!< VLAN ID 0..65535 */
    uint32 SwitchPort;  /*!< 交换机端口位图 */
} Eth_MacVlanType;

/* ========================================================================
 * Eth_CounterType（SWS_Eth_91007）：RFC 1757/RFC 1643 口径的通用计数器
 * ======================================================================== */
typedef struct {
    uint32 DropPktBufOverrun; /*!< 因缓冲区溢出丢弃的帧 */
    uint32 DropPktCrc;        /*!< 因 CRC 错误丢弃的帧 */
    uint32 UndersizePkt;      /*!< 长度 < 64 字节的短帧 */
    uint32 OversizePkt;       /*!< 长度 > 1518 字节的长帧 */
    uint32 AlgnmtErr;         /*!< 对齐错误（长度非整字节且 CRC 不过） */
    uint32 SqeTestErr;        /*!< SQE 测试错误（本实现恒 0） */
    uint32 DiscInbdPkt;       /*!< 收到但因资源不足被丢弃（ifInDiscards） */
    uint32 ErrInbdPkt;        /*!< 收到的错误帧总数 */
    uint32 DiscOtbdPkt;       /*!< 发送时因资源不足被丢弃（ifOutDiscards） */
    uint32 ErrOtbdPkt;        /*!< 发送错误帧总数 */
    uint32 SnglCollPkt;       /*!< 单次冲突后发送成功的帧 */
    uint32 MultCollPkt;       /*!< 多次冲突后发送成功的帧 */
    uint32 DfrdPkt;           /*!< 因介质忙推迟首发的帧 */
    uint32 LatCollPkt;        /*!< 迟冲突次数 */
    uint32 HwDepCtr0;         /*!< 硬件相关计数器 0（=MMC rx crc error） */
    uint32 HwDepCtr1;         /*!< 硬件相关计数器 1（=MMC rx alignment error） */
    uint32 HwDepCtr2;         /*!< 硬件相关计数器 2（=MMC rx fifo overflow） */
    uint32 HwDepCtr3;         /*!< 硬件相关计数器 3（=MMC tx underflow） */
} Eth_CounterType;

/* ========================================================================
 * Eth_RxStatsType（SWS_Eth_91002）：RFC 2819 RMON 接收统计
 * ======================================================================== */
typedef struct {
    uint32 RxStatsDropEvents;          /*!< 因资源不足丢帧的事件数 */
    uint32 RxStatsOctets;              /*!< 收到的字节数（含 FCS，不含前导码） */
    uint32 RxStatsPkts;                /*!< 收到的帧数（含错误帧） */
    uint32 RxStatsBroadcastPkts;       /*!< 广播帧数 */
    uint32 RxStatsMulticastPkts;       /*!< 组播帧数 */
    uint32 RxStatsCrcAlignErrors;      /*!< CRC/对齐错误帧数 */
    uint32 RxStatsUndersizePkts;       /*!< 短帧数 */
    uint32 RxStatsOversizePkts;        /*!< 长帧数 */
    uint32 RxStatsFragments;           /*!< 碎片帧数 */
    uint32 RxStatsJabbers;             /*!< Jabber 帧数 */
    uint32 RxStatsCollisions;          /*!< 冲突次数 */
    uint32 RxStatsPkts64Octets;        /*!< 64 字节帧数 */
    uint32 RxStatsPkts65to127Octets;   /*!< 65~127 字节帧数 */
    uint32 RxStatsPkts128to255Octets;  /*!< 128~255 字节帧数 */
    uint32 RxStatsPkts256to511Octets;  /*!< 256~511 字节帧数 */
    uint32 RxStatsPkts512to1023Octets; /*!< 512~1023 字节帧数 */
    uint32 RxStatsPkts1024to1518Octets;/*!< 1024~1518 字节帧数 */
    uint32 RxUnicastFrames;            /*!< 单播帧数（ifInUcastPkts） */
} Eth_RxStatsType;

/* ========================================================================
 * Eth_TxStatsType（SWS_Eth_91003）
 * ======================================================================== */
typedef struct {
    uint32 TxNumberOfOctets; /*!< 发出的字节数（ifOutOctets） */
    uint32 TxNUcastPkts;     /*!< 非单播发送帧数（ifOutNUcastPkts） */
    uint32 TxUniCastPkts;    /*!< 单播发送帧数（ifOutUcastPkts） */
} Eth_TxStatsType;

/* ========================================================================
 * Eth_TxErrorCounterValuesType（SWS_Eth_91004）
 * ======================================================================== */
typedef struct {
    uint32 TxDroppedNoErrorPkts; /*!< 无错误但因资源不足被丢弃的发送帧 */
    uint32 TxDroppedErrorPkts;   /*!< 因错误未发出的帧 */
    uint32 TxDeferredTrans;      /*!< 首传推迟次数 */
    uint32 TxSingleCollision;    /*!< 单次冲突 */
    uint32 TxMultipleCollision;  /*!< 多次冲突 */
    uint32 TxLateCollision;      /*!< 迟冲突 */
    uint32 TxExcessiveCollison;  /*!< 过多冲突（SWS 原拼写 Collison） */
    uint32 TxCarrierLost;        /*!< 载波丢失 */
    uint32 TxUnderflowError;     /*!< 发送 FIFO 下溢 */
} Eth_TxErrorCounterValuesType;

/* ========================================================================
 * Eth_SpiStatusType（SWS_Eth_91008）：SPI 接口以太网（本实现不用）
 * ======================================================================== */
typedef enum {
    ETH_SPI_STATUS_OK    = 0x00U, /*!< SPI 状态正常 */
    ETH_SPI_STATUS_ERROR = 0x01U  /*!< SPI 状态异常 */
} Eth_SpiStatusType;

/* ========================================================================
 * Eth_StreamStatisticCounterType（SWS_Eth_91009）：帧抢占统计（本实现不用）
 * ======================================================================== */
typedef struct {
    uint32 StreamCounterRx; /*!< 接收流计数 */
    uint32 StreamCounterTx; /*!< 发送流计数 */
} Eth_StreamStatisticCounterType;

/* ========================================================================
 * 本实现扩展类型（非 SWS）：链路信息，供 EthTrcv→Eth 联动与例程打印
 * ======================================================================== */
typedef struct {
    boolean LinkUp;      /*!< TRUE = 链路建立 */
    uint16  SpeedMbit;   /*!< 1000 / 100 / 10 */
    boolean FullDuplex;  /*!< TRUE = 全双工 */
} Eth_LinkInfoType;


/* ========================================================================
 * 以下为 Ethernet Transceiver Driver（EthTrcv，模块 ID 73）的类型
 * 依据：AUTOSAR_CP_SWS_EthernetTransceiverDriver（R25-11）§8.2
 *       SWS 明确这些类型 "Available via Eth_GeneralTypes.h"
 * ======================================================================== */

/*! EthTrcv_LinkStateType（SWS_EthTrcv_00100）*/
typedef enum {
    ETHTRCV_LINK_STATE_DOWN   = 0x00U, /*!< 物理链路未建立 */
    ETHTRCV_LINK_STATE_ACTIVE = 0x01U  /*!< 物理链路已建立 */
} EthTrcv_LinkStateType;

/*! EthTrcv_StateType（SWS_EthTrcv_00101）*/
typedef enum {
    ETHTRCV_STATE_UNINIT = 0x00U, /*!< 未初始化 */
    ETHTRCV_STATE_INIT   = 0x01U  /*!< 已初始化 */
} EthTrcv_StateType;

/*! EthTrcv_BaudRateType（SWS_EthTrcv_00102）*/
typedef enum {
    ETHTRCV_BAUD_RATE_10MBIT    = 0x00U, /*!< 10 Mbit/s */
    ETHTRCV_BAUD_RATE_100MBIT   = 0x01U, /*!< 100 Mbit/s */
    ETHTRCV_BAUD_RATE_1000MBIT  = 0x02U, /*!< 1000 Mbit/s */
    ETHTRCV_BAUD_RATE_2500MBIT  = 0x03U, /*!< 2500 Mbit/s（本板不支持） */
    ETHTRCV_BAUD_RATE_5000MBIT  = 0x04U, /*!< 5000 Mbit/s（本板不支持） */
    ETHTRCV_BAUD_RATE_10000MBIT = 0x05U  /*!< 10000 Mbit/s（本板不支持） */
} EthTrcv_BaudRateType;

/*! EthTrcv_DuplexModeType（SWS_EthTrcv_00103）*/
typedef enum {
    ETHTRCV_DUPLEX_MODE_HALF = 0x00U, /*!< 半双工 */
    ETHTRCV_DUPLEX_MODE_FULL = 0x01U  /*!< 全双工 */
} EthTrcv_DuplexModeType;

/*! EthTrcv_WakeupReasonType（SWS_EthTrcv_00114）*/
typedef enum {
    ETHTRCV_WUR_NONE     = 0x00U, /*!< 无唤醒原因 */
    ETHTRCV_WUR_GENERAL  = 0x01U, /*!< 通用唤醒 */
    ETHTRCV_WUR_INTERNAL = 0x03U, /*!< 内部唤醒 */
    ETHTRCV_WUR_RESET    = 0x04U, /*!< 复位唤醒 */
    ETHTRCV_WUR_POWER_ON = 0x05U, /*!< 上电唤醒 */
    ETHTRCV_WUR_PIN      = 0x06U, /*!< 引脚唤醒 */
    ETHTRCV_WUR_SYSERR   = 0x07U, /*!< 系统错误唤醒 */
    ETHTRCV_WUR_WODL_WUP = 0x08U, /*!< 链路 down 时的数据线唤醒脉冲 */
    ETHTRCV_WUR_WODL_WUR = 0x09U, /*!< 链路 active 时的数据线唤醒请求 */
    ETHTRCV_WUR_TRANSFER = 0x0AU  /*!< 相邻 PHY 的数据线唤醒转发 */
} EthTrcv_WakeupReasonType;

/*! EthTrcv_PhyTestModeType（SWS_EthTrcv_91002）*/
typedef enum {
    ETHTRCV_PHYTESTMODE_NONE = 0x00U, /*!< 正常模式 */
    ETHTRCV_PHYTESTMODE_1    = 0x01U, /*!< 发射 droop 测试 */
    ETHTRCV_PHYTESTMODE_2    = 0x02U, /*!< Master 时序抖动测试 */
    ETHTRCV_PHYTESTMODE_3    = 0x03U, /*!< Slave 时序抖动测试 */
    ETHTRCV_PHYTESTMODE_4    = 0x04U, /*!< 发射失真测试 */
    ETHTRCV_PHYTESTMODE_5    = 0x05U  /*!< 发射 PSD mask 测试 */
} EthTrcv_PhyTestModeType;

/*! EthTrcv_PhyLoopbackModeType（SWS_EthTrcv_91004）*/
typedef enum {
    ETHTRCV_PHYLOOPBACK_NONE     = 0x00U, /*!< 正常模式 */
    ETHTRCV_PHYLOOPBACK_INTERNAL = 0x01U, /*!< 内部回环（BMCR[14]） */
    ETHTRCV_PHYLOOPBACK_EXTERNAL = 0x02U, /*!< 外部回环 */
    ETHTRCV_PHYLOOPBACK_REMOTE   = 0x03U  /*!< 远端回环 */
} EthTrcv_PhyLoopbackModeType;

/*! EthTrcv_PhyTxModeType（SWS_EthTrcv_91006）*/
typedef enum {
    ETHTRCV_PHYTXMODE_NORMAL        = 0x00U, /*!< 正常发送 */
    ETHTRCV_PHYTXMODE_TX_OFF        = 0x01U, /*!< 关闭发送 */
    ETHTRCV_PHYTXMODE_SCRAMBLER_OFF = 0x02U  /*!< 关闭扰码 */
} EthTrcv_PhyTxModeType;

/*! EthTrcv_CableDiagResultType（SWS_EthTrcv_91008）*/
typedef enum {
    ETHTRCV_CABLEDIAG_OK             = 0x00U, /*!< 诊断通过 */
    ETHTRCV_CABLEDIAG_ERROR          = 0x01U, /*!< 诊断失败 */
    ETHTRCV_CABLEDIAG_SHORT          = 0x02U, /*!< 短路 */
    ETHTRCV_CABLEDIAG_OPEN           = 0x03U, /*!< 开路 */
    ETHTRCV_CABLEDIAG_PENDING        = 0x04U, /*!< 诊断进行中 */
    ETHTRCV_CABLEDIAG_WRONG_POLARITY = 0x05U  /*!< 极性反 */
} EthTrcv_CableDiagResultType;

/*! EthTrcv_MacMethodType（SWS_EthTrcv_91013，DRAFT）*/
typedef enum {
    ETHTRCV_MAC_TYPE_UNKNOWN = 0x00U, /*!< 未知 */
    ETHTRCV_MAC_TYPE_MII     = 0x01U, /*!< MII */
    ETHTRCV_MAC_TYPE_RMII    = 0x02U, /*!< RMII */
    ETHTRCV_MAC_TYPE_RGMII   = 0x03U, /*!< RGMII */
    ETHTRCV_MAC_TYPE_SGMII   = 0x04U  /*!< SGMII */
} EthTrcv_MacMethodType;

#endif /* ETH_GENERALTYPES_H */