/*
 * board_net2.h - hpm5e31_1 以太网（YT8011AN 千兆 RGMII PHY）板级资源
 * 负责人: tp-net2（见 docs/00-目录.md）
 *
 * 引脚（hpm5e31_1 原理图）:
 *   RGMII : TXD0-3 PF03/PF04/PF05/PF06, TXEN PF07, TXCK PF02
 *           RXD0-3 PF09/PF10/PF11/PF12, RXDV PF08, RXCK PF13
 *   MDIO  : MDC PA31, MDIO PA30
 *   PHY   : nRST PB29 (低有效), nINT PB28 (低有效, 内部上拉)
 */
#ifndef BOARD_EXT_NET2_H
#define BOARD_EXT_NET2_H

/* ---------------- ENET0 + YT8011AN 基本参数 ---------------- */
#define BOARD_NET2_ENET_BASE            HPM_ENET0
#define BOARD_NET2_ENET_IRQ             IRQn_ENET0
#define BOARD_NET2_PHY_ADDR             (1U)     /* YT8011AN strapping PHYADDR=1 */
#define BOARD_NET2_MDIO_SCAN_MIN        (0U)
#define BOARD_NET2_MDIO_SCAN_MAX        (31U)

/* RGMII 时钟延迟（ENET 内部 delay line, ENET_CTRL0 TXCLK_DLY_SEL/RXCLK_DLY_SEL） */
#ifndef BOARD_NET2_RGMII_TX_DLY
#define BOARD_NET2_RGMII_TX_DLY         (0U)
#endif
#ifndef BOARD_NET2_RGMII_RX_DLY
#define BOARD_NET2_RGMII_RX_DLY         (0U)
#endif

/* PHY 复位时序（ms） */
#define BOARD_NET2_PHY_RST_LOW_MS       (2U)
#define BOARD_NET2_PHY_RST_HIGH_MS      (30U)

#define BOARD_NET2_PHY_INT_IRQ          IRQn_GPIO0_B

/* ---------------- IEEE 802.3 Clause 22 标准寄存器 ---------------- */
#define MII_BMCR                        (0x00U)
#define MII_BMSR                        (0x01U)
#define MII_PHYID1                      (0x02U)
#define MII_PHYID2                      (0x03U)
#define MII_ANAR                        (0x04U)
#define MII_ANLPAR                      (0x05U)
#define MII_ANER                        (0x06U)
#define MII_ANNPTR                      (0x07U)
#define MII_ANNPRR                      (0x08U)
#define MII_GBCR                        (0x09U)  /* 1000BASE-T control */
#define MII_GBSR                        (0x0AU)  /* 1000BASE-T status  */
#define MII_MMD_ACR                     (0x0DU)
#define MII_MMD_ADR                     (0x0EU)
#define MII_EXTSTS                      (0x0FU)
#define MII_PHYSTS                      (0x11U)  /* YT8011: link/speed/duplex status */

/* BMCR 位 */
#define BMCR_RESET                      (0x8000U)
#define BMCR_LOOPBACK                   (0x4000U)
#define BMCR_SPEED_LSB                  (0x2000U)
#define BMCR_ANENABLE                   (0x1000U)
#define BMCR_POWERDOWN                  (0x0800U)
#define BMCR_ISOLATE                    (0x0400U)
#define BMCR_RESTART_AN                 (0x0200U)
#define BMCR_DUPLEX                     (0x0100U)
#define BMCR_SPEED_MSB                  (0x0040U)

/* BMSR 位 */
#define BMSR_100T4                      (0x8000U)
#define BMSR_100TX_FD                   (0x4000U)
#define BMSR_100TX_HD                   (0x2000U)
#define BMSR_10T_FD                     (0x1000U)
#define BMSR_10T_HD                     (0x0800U)
#define BMSR_100T2_FD                   (0x0400U)
#define BMSR_100T2_HD                   (0x0200U)
#define BMSR_EXTSTS                     (0x0100U)
#define BMSR_MF_PREAMBLE                (0x0040U)
#define BMSR_ANEGCOMPLETE               (0x0020U)
#define BMSR_REMOTE_FAULT               (0x0010U)
#define BMSR_ANEGCAPABLE                (0x0008U)
#define BMSR_LINK_STATUS                (0x0004U)
#define BMSR_JABBER                     (0x0002U)
#define BMSR_EXT_CAPABLE                (0x0001U)

/* GBCR (reg 9) 位：1000BASE-T 主从配置 */
#define GBCR_TEST_MASK                  (0xE000U)
#define GBCR_MS_MANUAL                  (0x1000U)  /* 1=手动配置主从 */
#define GBCR_MS_VALUE                   (0x0800U)  /* 1=Master 0=Slave */
#define GBCR_ADVERTISE_1000FULL         (0x0200U)
#define GBCR_ADVERTISE_1000HALF         (0x0100U)

/* GBSR (reg 10) 位 */
#define GBSR_MS_FAULT                   (0x8000U)
#define GBSR_MS_RESOLVED                (0x4000U)  /* 1=Master 0=Slave */
#define GBSR_LOCAL_RX_OK                (0x2000U)
#define GBSR_REMOTE_RX_OK               (0x1000U)
#define GBSR_LP_1000FULL                (0x0800U)
#define GBSR_LP_1000HALF                (0x0400U)
#define GBSR_IDLE_ERR_CNT_MASK          (0x00FFU)

/* ---------------- tp-net2 提供的板级函数 ---------------- */
#ifdef __cplusplus
extern "C" {
#endif

void net2_enet_rgmii_pins_init(void);
void net2_yt8011_ctrl_pins_init(void);
void net2_yt8011_hw_reset(void);
void net2_enet_board_init(void);

/*
 * lwIP 移植层钩子：samples/lwip/common/single/common.c 无条件调用这两个函数，
 * 而 board.h 中的声明在 CONFIG_ENET_PHY 保护内。本板不使用 SDK 的 CONFIG_ENET_PHY
 * （否则 board.c 会编译出基于 RTL8211 的同名函数），因此在板级头文件里补声明。
 */
#if !defined(CONFIG_ENET_PHY) || !CONFIG_ENET_PHY
hpm_stat_t board_init_enet_phy(ENET_Type *ptr);
void board_get_enet_phy_status(uint8_t idx, void *status);
#endif

#ifdef __cplusplus
}
#endif
#endif /* BOARD_EXT_NET2_H */
