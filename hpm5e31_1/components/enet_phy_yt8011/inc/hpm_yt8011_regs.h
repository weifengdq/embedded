/*
 * hpm_yt8011_regs.h - YT8011AN (Motorcomm 千兆车载以太网 PHY) 寄存器定义
 *
 * 来源: YT8011A_1.pdf (Issue V1.4 2025-12-11) 第 4 章 Register Overview
 *   - PHY MII Register   (clause 22, 0x00~0x1F)   §4.1 (p.39-42)
 *   - PHY EXT Register   (间接访问, 0x1018 等)      §4.2 (p.42-57)
 *   - Common EXT Register(0x9000 等)                §4.4 (p.58-...)
 *   - MMD Register       (MMD7/MMD1/MMD3)           §4.3 (p.52-57)
 * EXT 寄存器访问方法: 见 hpm_yt8011.c 中 yt8011_read_ext()/yt8011_write_ext()
 */
#ifndef HPM_YT8011_REGS_H
#define HPM_YT8011_REGS_H

/* ============ Clause 22 标准寄存器 (PHY MII Register, §4.1) ============ */
#define YT8011_BMCR                      (0x00U)
#define YT8011_BMSR                      (0x01U)
#define YT8011_PHYID1                    (0x02U)
#define YT8011_PHYID2                    (0x03U)
#define YT8011_ANAR                      (0x04U)
#define YT8011_ANLPAR                    (0x05U)
#define YT8011_ANER                      (0x06U)
#define YT8011_ANNPTR                    (0x07U)
#define YT8011_ANNPRR                    (0x08U)
#define YT8011_GBCR                      (0x09U)
#define YT8011_GBSR                      (0x0AU)
#define YT8011_PHYSR                     (0x11U)  /* PHY Specific Status (应用说明 §5.2, p.25) */
#define YT8011_INT_MASK_REG              (0x12U)
#define YT8011_INT_STATUS                (0x13U)  /* Debug Register's Data Register */
#define YT8011_INT_STATUS2               (0x17U)

/* PHY ID 期望值（§4.1.3/§4.1.4） */
#define YT8011_PHYID1_VAL                (0x4F51U)
#define YT8011_PHYID2_OUI_VAL            (0x3AU)   /* ID2[15:10] */

/* BMCR (0x00) */
#define YT8011_BMCR_RESET                (0x8000U)
#define YT8011_BMCR_LOOPBACK             (0x4000U)
#define YT8011_BMCR_SPD_SEL_13           (0x2000U)  /* RO: spd_sel[0] */
#define YT8011_BMCR_AN_EN                (0x1000U)
#define YT8011_BMCR_PWR_DN               (0x0800U)
#define YT8011_BMCR_ISOLATE              (0x0400U)
#define YT8011_BMCR_RE_ATNEG             (0x0200U)
#define YT8011_BMCR_DUPLEX_OBS           (0x0100U)  /* 未文档化, 读回恒 1 */
#define YT8011_BMCR_SPD_SEL_6            (0x0040U)  /* RO: spd_sel[1] */
/* 应用说明 §5.4 (p.27) 给出的"软复位+速率选择"整字写入值 */
#define YT8011_BMCR_FORCE_1000M          (0x8140U)  /* 1000BASE-T1: bit15 reset|bit8|bit6 */
#define YT8011_BMCR_FORCE_100M           (0xA100U)  /* 100BASE-T1 : bit15 reset|bit13|bit8 */
#define YT8011_BMCR_RESET_AN             (0x9140U)  /* 软复位 + 使能自协商 */

/* BMSR (0x01) */
#define YT8011_BMSR_ANEG_CMPLT           (0x0020U)
#define YT8011_BMSR_RMT_FAULT            (0x0010U)
#define YT8011_BMSR_LINK_STATUS          (0x0004U)
#define YT8011_BMSR_JABBER               (0x0002U)

/* GBCR (0x09) 1000BASE-T control */
#define YT8011_GBCR_MS_MANUAL            (0x1000U)  /* 1=主从由本寄存器决定 */
#define YT8011_GBCR_MS_VALUE             (0x0800U)  /* 1=Master, 0=Slave */
#define YT8011_GBCR_ADV_1000FULL         (0x0200U)
#define YT8011_GBCR_ADV_1000HALF         (0x0100U)

/* GBSR (0x0A) 1000BASE-T status */
#define YT8011_GBSR_MS_FAULT             (0x8000U)
#define YT8011_GBSR_MS_RESOLVED          (0x4000U)  /* 1=Master, 0=Slave */
#define YT8011_GBSR_LOCAL_RX_OK          (0x2000U)
#define YT8011_GBSR_REMOTE_RX_OK         (0x1000U)
#define YT8011_GBSR_LP_1000FULL          (0x0800U)
#define YT8011_GBSR_LP_1000HALF          (0x0400U)
#define YT8011_GBSR_IDLE_ERR_CNT_MASK    (0x00FFU)

/* ============ PHY EXT Register（间接访问）============ */
#define YT8011_EXT_BROADCAST_CTRL        (0x0000U)  /* [5]=广播地址使能, [4:0]=广播地址 */
#define YT8011_EXT_DAC_CFG1              (0x1018U)
#define YT8011_EXT_DAC_CFG2              (0x1019U)
#define YT8011_EXT_DAC_CFG3              (0x101AU)
#define YT8011_EXT_PTP_CLK_EN            (0x1072U)
#define YT8011_EXT_SLEEP_WAKE_CFG1       (0x1088U)
#define YT8011_EXT_LOOPBACK_CFG          (0x2036U)
#define YT8011_EXT_SLEEP_CTRL1           (0x3008U)
#define YT8011_EXT_SLEEP_CTRL2           (0x3009U)
#define YT8011_EXT_SQI_DEBUG             (0x3074U)
#define YT8011_EXT_PKG_CFG4              (0x40A0U)
#define YT8011_EXT_SMI_SDS_PHY           (0x9000U)  /* [15] 1=SDS, 0=PHY */
#define YT8011_EXT_RGMII_CFG1            (0x9001U)  /* 见下 */
#define YT8011_EXT_COMMON_CTRL1          (0x9030U)
#define YT8011_EXT_LTT                   (0x9081U)  /* Link Training Time [10:0] */
#define YT8011_EXT_LRT                   (0x9082U)  /* Local Receiver Time [7:0] */
#define YT8011_EXT_RRT                   (0x9083U)  /* Remote Receiver Time [7:0] */

/* EXT 0x9001 RGMII Config Register1 (§4.4.2 p.58) */
#define YT8011_RGMII_CFG1_TX_CLK_SEL     (0x0200U)  /* [9] 反相 GMII 时钟采样 RGMII 数据 */
#define YT8011_RGMII_CFG1_RX_DLY_EN      (0x0100U)  /* [8] 1=给 RXC 加 2ns 延迟(默认1, strap) */
#define YT8011_RGMII_CFG1_TX_DLY_SHIFT   (4U)
#define YT8011_RGMII_CFG1_TX_DLY_MASK    (0x00F0U)  /* [7:4] TXC 延迟档位, 默认 1 */

/* EXT 0x0000 默认: 广播地址使能=1, 地址=0x1F */
#define YT8011_EXT_BROADCAST_EN          (0x0020U)

/* 0x11 PHY Specific Status（应用说明 §5.2 p.25, Table 8） */
#define YT8011_PHYSR_SPEED_MASK          (0xC000U)  /* [15:14] 10=1000M, 01=100M */
#define YT8011_PHYSR_SPEED_SHIFT         (14U)
#define YT8011_PHYSR_MS_MASTER           (0x2000U)  /* [13] 1=Master 0=Slave */
#define YT8011_PHYSR_LINK_REALTIME       (0x0400U)  /* [10] 实时 link 状态 */

/* MMD 访问（clause 22 reg 0x0D/0x0E） */
#define YT8011_MMD_ACR                   (0x0DU)
#define YT8011_MMD_ADR                   (0x0EU)
#define YT8011_MMD_FUNC_ADDR             (0x0000U)
#define YT8011_MMD_FUNC_DATA             (0x4000U)

#endif /* HPM_YT8011_REGS_H */
