/*
 * EthTrcv_PBcfg.c —— EthTrcv 驱动 post-build 配置实例
 *
 * 板级参数：YT8011AN 的 MDIO 地址、默认速率、主从、RGMII 延迟档位、MAC 接口类型。
 * 依据：
 *   - boards/hpm5e31_1 原理图：YT8011AN MDIO 地址 strap RXD[1:0]=01 -> PHYADDR=1；
 *     RXD3/MS_SEL=0（Slave）、RXD2/MII_SEL=0（RGMII）、RXC/RXDLY_EN=1（strap 默认开）
 *   - YT8011A_1.pdf §3 RESET/PHY Address/Strapping（p.38）
 *   - YT8011Ax_应用说明_V2.2.pdf §5.3 Master/Slave 配置（p.26）、§5.4 速率（p.27）
 *   - 既有可用基线：components/enet_phy_yt8011 + samples/enet/enet_lwip_iperf（935/466 Mbit/s）
 */
#include "EthTrcv_PBcfg.h"

#define ETHTRCV_START_SEC_CONFIG_DATA_UNSPECIFIED
#include "MemMap.h"

static const EthTrcv_TrcvConfigType s_trcv_cfg[ETHTRCV_TRCV_COUNT] = {
    {
        .TrcvIdx         = 0U,
        .CtrlIdx         = 0U,                        /* ENET0 */
        .PhyAddr         = 1U,                        /* strap: RXD[1:0]=01 */
        .DefaultBaudRate = ETHTRCV_BAUD_RATE_1000MBIT,/* 板级默认 1000BASE-T1 */
        .Master          = FALSE,                     /* strap RXD3=0 -> Slave */
        .AutoNegotiation = FALSE,                     /* 强制模式（1000BASE-T1 主从固定） */
        .RxDelayEn       = TRUE,                      /* EXT 0x9001[8]：RXC 2 ns 延迟 */
        .TxDelaySel      = 1U,                        /* EXT 0x9001[7:4]：TXC 延迟档位 1 */
        .TxClkSel        = FALSE,
        .MacMethod       = ETHTRCV_MAC_TYPE_RGMII,
        .LinkPollDivider = ETHTRCV_LINK_POLL_DIVIDER_DEFAULT,
    },
};

const EthTrcv_ConfigType EthTrcv_Config = {
    .TrcvConfigPtr = s_trcv_cfg,
    .TrcvCount     = ETHTRCV_TRCV_COUNT,
};
