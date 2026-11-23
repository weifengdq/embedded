/*
 * EthTrcv_Cfg.h —— EthTrcv 驱动预编译配置（Pre-compile 阶段）
 *
 * 依据：AUTOSAR_CP_SWS_EthernetTransceiverDriver §10（EthTrcvGeneral / EthTrcvConfig）
 */
#ifndef ETHTRCV_CFG_H
#define ETHTRCV_CFG_H

#include "Mcal_Cfg.h"

/* ---------------- 规模 ---------------- */
#define ETHTRCV_TRCV_COUNT          (1U)  /*!< 本板 1 颗 YT8011AN */

/* ---------------- 开发错误检测 ---------------- */
#ifndef ETHTRCV_DEV_ERROR_DETECT
#define ETHTRCV_DEV_ERROR_DETECT    (MCAL_DEV_ERROR_DETECT)
#endif

/* ---------------- 可选 API 开关 ---------------- */
#define ETHTRCV_VERSION_INFO_API    (MCAL_VERSION_INFO_API)
/*! 是否调用 EthIf_* 回调（本仓库 EthIf 桩）：STD_OFF 时驱动不依赖上层 */
#ifndef ETHTRCV_USE_ETHIF_CALLBACK
#define ETHTRCV_USE_ETHIF_CALLBACK  (STD_OFF)
#endif

/* ---------------- 链路轮询 ---------------- */
/*! MainFunction 每调用 N 次查一次链路（默认 1 = 每次都查） */
#define ETHTRCV_LINK_POLL_DIVIDER_DEFAULT (1U)
/*! 链路建立/断开去抖次数（连续 N 次读到同一状态才上报） */
#define ETHTRCV_LINK_DEBOUNCE_COUNT (2U)
/*! PHY 软复位等待上限（ms，YT8011 数据手册 §3 要求 >=10 ms） */
#define ETHTRCV_RESET_TIMEOUT_MS    (50U)
/*! MDIO 操作失败重试次数 */
#define ETHTRCV_MDIO_RETRY          (3U)

#endif /* ETHTRCV_CFG_H */
