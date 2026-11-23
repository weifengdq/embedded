/*
 * EthTrcv.h —— AUTOSAR Ethernet Transceiver Driver（模块 ID 73）公共接口
 *
 * 依据：AUTOSAR_CP_SWS_EthernetTransceiverDriver（R25-11）
 *       autosar_txt/AUTOSAR_CP_SWS_EthernetTransceiverDriver.txt
 * 硬件：YT8011AN（Motorcomm 千兆车载以太网 PHY，1000BASE-T1/100BASE-T1，RGMII），
 *       MDIO 挂在 ENET0 的 MDC(PA31)/MDIO(PA30) 上，PHY 地址 1（strap RXD[1:0]=01）。
 * 例程：samples/mcal/eth_trcv（链路/速率/主从实测）
 * 文档：docs/chapters/50-mcal-eth.md
 *
 * 分层：EthTrcv 不直接碰寄存器，全部经 Eth_ReadMii/Eth_WriteMii/Eth_ReadMmd/
 *       Eth_WriteMmd 访问 PHY（AUTOSAR 规定的 Eth→EthTrcv 服务关系）。
 * 约束：本文件不 include 任何 HPM SDK 头（板级参数在 EthTrcv_PBcfg.c）。
 */
#ifndef ETHTRCV_H
#define ETHTRCV_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Mcal_ModuleId.h"
#include "EthTrcv_Cfg.h"
#include "Eth_GeneralTypes.h"

/* ========================================================================
 * 版本信息（模块 ID 见 Mcal_ModuleId.h 的 ETHTRCV_MODULE_ID=73）
 * ======================================================================== */
#define ETHTRCV_AR_RELEASE_MAJOR_VERSION    (4U)
#define ETHTRCV_AR_RELEASE_MINOR_VERSION    (4U)
#define ETHTRCV_AR_RELEASE_REVISION_VERSION (0U)
#define ETHTRCV_SW_MAJOR_VERSION            (1U)
#define ETHTRCV_SW_MINOR_VERSION            (0U)
#define ETHTRCV_SW_PATCH_VERSION            (0U)

/* ========================================================================
 * API Service ID（SWS_EthTrcv §8.3）
 * ======================================================================== */
#define ETHTRCV_API_ID_INIT                         (0x01U) /*!< EthTrcv_Init */
#define ETHTRCV_API_ID_SET_TRANSCEIVER_MODE         (0xA8U) /*!< EthTrcv_SetTransceiverMode */
#define ETHTRCV_API_ID_GET_TRANSCEIVER_MODE         (0x04U) /*!< EthTrcv_GetTransceiverMode */
#define ETHTRCV_API_ID_TRANSCEIVER_LINK_STATE_REQUEST (0x02U) /*!< EthTrcv_TransceiverLinkStateRequest */
#define ETHTRCV_API_ID_CHECK_WAKEUP                 (0x0FU) /*!< EthTrcv_CheckWakeup */
#define ETHTRCV_API_ID_GET_BUS_WU_REASON            (0x17U) /*!< EthTrcv_GetBusWuReason */
#define ETHTRCV_API_ID_START_AUTO_NEGOTIATION       (0xA9U) /*!< EthTrcv_StartAutoNegotiation */
#define ETHTRCV_API_ID_GET_LINK_STATE               (0x06U) /*!< EthTrcv_GetLinkState */
#define ETHTRCV_API_ID_GET_BAUD_RATE                (0x07U) /*!< EthTrcv_GetBaudRate */
#define ETHTRCV_API_ID_GET_DUPLEX_MODE              (0x08U) /*!< EthTrcv_GetDuplexMode */
#define ETHTRCV_API_ID_GET_PHY_IDENTIFIER           (0x15U) /*!< EthTrcv_GetPhyIdentifier */
#define ETHTRCV_API_ID_SET_PHY_TEST_MODE            (0x11U) /*!< EthTrcv_SetPhyTestMode */
#define ETHTRCV_API_ID_SET_PHY_LOOPBACK_MODE        (0x12U) /*!< EthTrcv_SetPhyLoopbackMode */
#define ETHTRCV_API_ID_GET_PHY_SIGNAL_QUALITY       (0x10U) /*!< EthTrcv_GetPhySignalQuality */
#define ETHTRCV_API_ID_SET_PHY_TX_MODE              (0x13U) /*!< EthTrcv_SetPhyTxMode */
#define ETHTRCV_API_ID_RUN_CABLE_DIAGNOSTIC         (0x16U) /*!< EthTrcv_RunCableDiagnostic */
#define ETHTRCV_API_ID_GET_CABLE_DIAGNOSTICS_RESULT (0x14U) /*!< EthTrcv_GetCableDiagnosticsResult */
#define ETHTRCV_API_ID_GET_MAC_METHOD               (0x18U) /*!< EthTrcv_GetMacMethod */
#define ETHTRCV_API_ID_GET_VERSION_INFO             (0x0BU) /*!< EthTrcv_GetVersionInfo */
#define ETHTRCV_API_ID_MAIN_FUNCTION                (0x0CU) /*!< EthTrcv_MainFunction */
#define ETHTRCV_API_ID_MAIN_FUNCTION_MACSEC         (0x25U) /*!< EthTrcv_MainFunctionMacSec */

/* ========================================================================
 * 开发错误码（SWS_EthTrcv §7.2.1 / §7.2.2）
 * ======================================================================== */
#define ETHTRCV_E_INV_TRCV_IDX    (0x01U) /*!< 收发器索引越界 */
#define ETHTRCV_E_UNINIT          (0x02U) /*!< 未初始化就调用 */
#define ETHTRCV_E_PARAM_POINTER   (0x03U) /*!< 参数指针为空 */
#define ETHTRCV_E_MDI_COMMUNICATION (0x04U) /*!< 运行时错误：MDIO 通信失败 */
/*! SWS 正文引用了 ETHTRCV_E_NOT_SUPPORTED 但错误码表未给数值；
 *  本实现顺延取 0x05，并在文档“待确认项”里标注（SWS 缺陷）。 */
#define ETHTRCV_E_NOT_SUPPORTED   (0x05U)

/* ========================================================================
 * Post-build 配置类型（板级/PHY 参数只在 EthTrcv_PBcfg.c 实例化）
 * ======================================================================== */

/*! 单颗收发器配置（对应 SWS EthTrcvConfig / EthTrcvPhyConfig） */
typedef struct {
    uint8                TrcvIdx;         /*!< 收发器索引 0..ETHTRCV_TRCV_COUNT-1 */
    uint8                CtrlIdx;         /*!< 所属 Eth 控制器索引（0=ENET0） */
    uint8                PhyAddr;         /*!< MDIO/PHY 地址（YT8011AN strap = 1） */
    EthTrcv_BaudRateType DefaultBaudRate; /*!< 上电强制速率：1000M 或 100M */
    boolean              Master;          /*!< TRUE=Master，FALSE=Slave（1000BASE-T1 主从） */
    boolean              AutoNegotiation; /*!< TRUE=走自协商；FALSE=强制模式 */
    boolean              RxDelayEn;       /*!< RGMII RXC 2 ns 延迟（EXT 0x9001[8]） */
    uint8                TxDelaySel;      /*!< RGMII TXC 延迟档位（EXT 0x9001[7:4]） */
    boolean              TxClkSel;        /*!< 反相 GMII 时钟（EXT 0x9001[9]） */
    EthTrcv_MacMethodType MacMethod;      /*!< MAC-PHY 接口类型（RGMII） */
    uint32               LinkPollDivider; /*!< MainFunction 每 N 次查一次链路（省 MDIO） */
} EthTrcv_TrcvConfigType;

/*! EthTrcv post-build 配置（SWS：EthTrcv_ConfigType，实现相关结构） */
typedef struct {
    const EthTrcv_TrcvConfigType *TrcvConfigPtr; /*!< 收发器配置数组 */
    uint8                         TrcvCount;     /*!< 收发器个数 */
} EthTrcv_ConfigType;

/* ========================================================================
 * 上层（EthIf）回调声明 —— 本仓库没有完整 EthIf，给桩用
 *   开关：EthTrcv_Cfg.h 的 ETHTRCV_USE_ETHIF_CALLBACK
 * ======================================================================== */
#if (ETHTRCV_USE_ETHIF_CALLBACK == STD_ON)
#ifndef ETHIF_TRCV_CALLBACKS_DECLARED
#define ETHIF_TRCV_CALLBACKS_DECLARED
/*! 链路状态变化通知（AUTOSAR EthIf_TrcvLinkStateChg） */
extern void EthIf_TrcvLinkStateChg(uint8 TrcvIdx, EthTrcv_LinkStateType LinkState);
#endif
#endif

/* ========================================================================
 * API 原型（SWS_EthTrcv §8.3）
 * ======================================================================== */

/*! 初始化收发器驱动并配置 PHY（0x01） */
void EthTrcv_Init(const EthTrcv_ConfigType *CfgPtr);

/*! 收发器模式切换：ETH_MODE_ACTIVE / ETH_MODE_DOWN（0xA8） */
Std_ReturnType EthTrcv_SetTransceiverMode(uint8 TrcvIdx, Eth_ModeType TrcvMode);

/*! 读收发器模式（0x04） */
Std_ReturnType EthTrcv_GetTransceiverMode(uint8 TrcvIdx, Eth_ModeType *TrcvModePtr);

/*! 链路状态请求（0x02；YT8011AN 不支持强制建链，返回 E_NOT_OK） */
Std_ReturnType EthTrcv_TransceiverLinkStateRequest(uint8 TrcvIdx, EthTrcv_LinkStateType LinkState);

/*! 唤醒检查（0x0F；本板 PHY 无 WAKE 引脚，返回 E_NOT_OK） */
Std_ReturnType EthTrcv_CheckWakeup(uint8 TrcvIdx);

/*! 读总线唤醒原因（0x17） */
Std_ReturnType EthTrcv_GetBusWuReason(uint8 TrcvIdx, EthTrcv_WakeupReasonType *WakeupReasonPtr);

/*! 启动自协商（0xA9） */
Std_ReturnType EthTrcv_StartAutoNegotiation(uint8 TrcvIdx);

/*! 读链路状态（0x06） */
Std_ReturnType EthTrcv_GetLinkState(uint8 TrcvIdx, EthTrcv_LinkStateType *LinkStatePtr);

/*! 读速率（0x07） */
Std_ReturnType EthTrcv_GetBaudRate(uint8 TrcvIdx, EthTrcv_BaudRateType *BaudRatePtr);

/*! 读双工模式（0x08） */
Std_ReturnType EthTrcv_GetDuplexMode(uint8 TrcvIdx, EthTrcv_DuplexModeType *DuplexModePtr);

/*! 读 PHY 标识（0x15）：OUI/Model/Revision */
Std_ReturnType EthTrcv_GetPhyIdentifier(uint8 TrcvIdx, uint32 *OrgUniqueIdPtr,
                                        uint8 *ModelNrPtr, uint8 *RevisionNrPtr);

/*! PHY 测试模式（0x11；YT8011AN 未公开 T1 测试模式，仅 NONE 支持） */
Std_ReturnType EthTrcv_SetPhyTestMode(uint8 TrcvIdx, EthTrcv_PhyTestModeType Mode);

/*! PHY 回环（0x12）：NONE / INTERNAL（BMCR[14]） */
Std_ReturnType EthTrcv_SetPhyLoopbackMode(uint8 TrcvIdx, EthTrcv_PhyLoopbackModeType Mode);

/*! 读信号质量（0x10；YT8011AN EXT 0x3074 SQI 调试寄存器） */
Std_ReturnType EthTrcv_GetPhySignalQuality(uint8 TrcvIdx, uint32 *SignalQualityPtr);

/*! PHY 发送模式（0x13）：仅 NORMAL 支持 */
Std_ReturnType EthTrcv_SetPhyTxMode(uint8 TrcvIdx, EthTrcv_PhyTxModeType Mode);

/*! 电缆诊断（0x16；单对线 T1 PHY 不支持，返回 E_NOT_OK） */
Std_ReturnType EthTrcv_RunCableDiagnostic(uint8 TrcvIdx);

/*! 电缆诊断结果（0x14） */
Std_ReturnType EthTrcv_GetCableDiagnosticsResult(uint8 TrcvIdx,
                                                 EthTrcv_CableDiagResultType *ResultPtr);

/*! MAC-PHY 接口类型（0x18，DRAFT）：返回 RGMII */
Std_ReturnType EthTrcv_GetMacMethod(uint8 *TrcvIdx, EthTrcv_MacMethodType *MacModePtr);

/*! 版本信息（0x0B） */
void EthTrcv_GetVersionInfo(Std_VersionInfoType *VersionInfoPtr);

/*! 周期函数（0x0C）：轮询链路，变化时通知 EthIf 并联动 Eth_SetLineSpeed */
void EthTrcv_MainFunction(void);

/*! MACsec 周期函数（0x25）：本实现无 MACsec，留空 */
void EthTrcv_MainFunctionMacSec(void);

#endif /* ETHTRCV_H */
