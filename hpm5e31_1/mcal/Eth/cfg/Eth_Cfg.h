/*
 * Eth_Cfg.h —— Eth 驱动预编译配置（Pre-compile 阶段，契约 §5.1）
 *
 * 依据：AUTOSAR_CP_SWS_EthernetDriver §10（EthGeneral / EthCtrlConfig 的规模类参数）。
 * 改这里影响“编进固件的能力集合/缓冲规模”；改板级参数请改 Eth_PBcfg.c。
 */
#ifndef ETH_CFG_H
#define ETH_CFG_H

#include "Mcal_Cfg.h"

/* ---------------- 控制器与队列规模 ---------------- */
#define ETH_CTRL_COUNT          (1U)  /*!< ENET0（本板只有一路 RGMII 千兆口） */
#define ETH_QUEUE_COUNT         (1U)  /*!< 入口队列数（SWS QueueIdx 0..N-1） */
#define ETH_PRIORITY_COUNT      (1U)  /*!< 出口优先级数（单优先级） */

/* ---------------- 缓冲区规模 ---------------- */
#define ETH_TX_BUF_COUNT        (8U)     /*!< TX 缓冲池个数（=DMA 描述符个数） */
#define ETH_RX_BUF_COUNT        (16U)    /*!< RX 缓冲池个数（=DMA 描述符个数） */
#define ETH_TX_BUF_SIZE         (1536U)  /*!< 单个 TX 缓冲字节数（8 字节对齐） */
#define ETH_RX_BUF_SIZE         (1536U)  /*!< 单个 RX 缓冲字节数（8 字节对齐） */
#define ETH_DMA_DESC_ALIGN      (8U)     /*!< 描述符对齐（ENET_SOC_DESC_ADDR_ALIGNMENT） */
#define ETH_BUF_ALIGN           (64U)    /*!< 数据缓冲对齐（=L1 D-cache line） */

/* ---------------- 帧长 ---------------- */
#define ETH_TX_MAX_LEN          (ETH_TX_BUF_SIZE) /*!< 单帧上限（含头，不含 FCS） */
#define ETH_RX_MAX_LEN          (ETH_RX_BUF_SIZE)

/* ---------------- 中断/轮询模型 ---------------- */
/*! RX 中断能力开关：STD_OFF 时 RxInterruptEnable 配置被忽略，强制轮询 */
#define ETH_RX_INTERRUPT_SUPPORT (STD_ON)
/*! 中断优先级（RV32 PLIC，数值越大越高，1..15） */
#define ETH_IRQ_PRIORITY         (4U)

/* ---------------- cache 维护策略（与第 13 章结论一致） ---------------- */
/*! 收发缓冲默认放 .noncacheable.bss（AXI SRAM，NOBITS/NOLOAD，DMA 可达且无 cache 一致性问题），
 *  此时下面的 per-frame cache 维护是多余的，默认关闭。
 *  若把 Eth.c 里 s_tx_buf/s_rx_buf 改回 cached 内存，必须把这两个开关置 STD_ON：
 *   - TX 提交前 clean（否则 DMA 可能读到旧数据）
 *   - RX 取帧前 invalidate（否则 CPU 可能读到 cache 里的旧行）
 *  第 13 章实测：一次 l1c_dc_writeback 约 109 cycles/cacheline。 */
#ifndef ETH_TX_CACHE_MAINTENANCE
#define ETH_TX_CACHE_MAINTENANCE (STD_OFF)
#endif
#ifndef ETH_RX_CACHE_MAINTENANCE
#define ETH_RX_CACHE_MAINTENANCE (STD_OFF)
#endif

/* ---------------- 开发错误检测 ---------------- */
#ifndef ETH_DEV_ERROR_DETECT
#define ETH_DEV_ERROR_DETECT     (MCAL_DEV_ERROR_DETECT)
#endif

/* ---------------- 可选 API 开关 ---------------- */
#define ETH_VERSION_INFO_API     (MCAL_VERSION_INFO_API) /*!< Eth_GetVersionInfo */
#define ETH_EXT_API_ENABLE       (STD_ON)  /*!< 非 SWS 扩展（Eth_GetRxBuffer 等） */
/*! 是否调用 EthIf_* 回调（本仓库 EthIf 桩）：STD_OFF 时驱动不依赖上层 */
#ifndef ETH_USE_ETHIF_CALLBACK
#define ETH_USE_ETHIF_CALLBACK   (STD_OFF)
#endif

#endif /* ETH_CFG_H */
