/*
 * hpm_yt8011.c - YT8011AN 千兆车载以太网 PHY 驱动实现
 *
 * 参考资料（页码为 PDF 阅读器页码）:
 *   YT8011A_1.pdf V1.4:
 *     §2.1.2 100BASE-T1 (p.25)          RGMII 只支持 1000/100Mbps
 *     §3 RESET/PHY Address/Strapping (p.38)  RESET_N 低有效 >= 10ms
 *     §4.1.1 Basic Control Register (p.39)
 *     §4.1.2 Basic Status Register1 (p.40)
 *     §4.1.3/4 Identification (p.40)   ID1=0x4F51, ID2[15:10]=0x3A
 *     §4.4.2 RGMII Config Register1 (p.58)  0x9001
 *     §4.4.3 Common Control Register1 (p.58) 0x9030
 *   YT8011Ax_应用说明_V2.2.pdf:
 *     §2.1.2 EXT 寄存器访问 (p.10)      写 0x1E=ext addr, 读/写 0x1F
 *     §2.1.3 MMD 间接访问 (p.11)        0x0D/0x0E 序列
 *     §5.2 Link 状态获取 (p.25)         BMSR[2] / 0x11[15:14,13,10]
 *     §5.3 Master/Slave 配置 (p.26)     MMD1 0x834
 *     §5.4 1000BASE-T1/100BASE-T1 (p.27) 0x8140 / 0xA100
 */
#include "hpm_yt8011.h"
#include "hpm_yt8011_regs.h"
#include "hpm_clock_drv.h"
#include "hpm_csr_drv.h"

/* 软复位后等待时间（数据手册要求 >=10ms 稳定，§3 p.38） */
#define YT8011_RESET_STABLE_MS      (12U)
#define YT8011_RESET_RETRY_CNT      (200U)

static void yt8011_delay_ms(uint32_t ms)
{
    uint32_t cpu = clock_get_frequency(clock_cpu0);
    uint64_t start = hpm_csr_get_core_mcycle();
    uint64_t ticks = (uint64_t) cpu * (uint64_t) ms / 1000ULL;
    while ((hpm_csr_get_core_mcycle() - start) < ticks) {
    }
}

bool yt8011_check_id(ENET_Type *base, uint8_t addr)
{
    uint16_t id1 = 0, id2 = 0;

    if (enet_read_phy(base, addr, YT8011_PHYID1, &id1) != status_success ||
        enet_read_phy(base, addr, YT8011_PHYID2, &id2) != status_success) {
        return false;
    }
    return (id1 == YT8011_PHYID1_VAL) &&
           (((id2 >> 10) & 0x3FU) == YT8011_PHYID2_OUI_VAL);
}

hpm_stat_t yt8011_reset(ENET_Type *base, uint8_t addr)
{
    uint16_t data = 0;
    uint32_t retry = YT8011_RESET_RETRY_CNT;

    if (enet_write_phy(base, addr, YT8011_BMCR, YT8011_BMCR_RESET) != status_success) {
        return status_fail;
    }
    do {
        if (enet_read_phy(base, addr, YT8011_BMCR, &data) != status_success) {
            return status_fail;
        }
        if ((data & YT8011_BMCR_RESET) == 0U) {
            break;
        }
    } while (--retry > 0U);

    if (retry == 0U) {
        return status_timeout;
    }
    yt8011_delay_ms(YT8011_RESET_STABLE_MS);
    return status_success;
}

/*
 * 带速率编码的软复位。
 * 实测（logs/20261030-110711 / -110912）: 直接写 BMCR=0x8000 复位会把速度/主从回退到
 * 上电默认(1000BASE-T1/Slave)；必须按应用说明 §5.4 写 "0x8140"(1000BASE-T1) 或
 * "0xA100"(100BASE-T1) —— 这两位同时完成"软复位 + 速率选择"。
 */
hpm_stat_t yt8011_soft_reset_cfg(ENET_Type *base, uint8_t addr, yt8011_speed_t speed)
{
    uint16_t data = 0;
    uint32_t retry = YT8011_RESET_RETRY_CNT;
    uint16_t bmcr = (speed == yt8011_speed_1000m) ? YT8011_BMCR_FORCE_1000M
                                                  : YT8011_BMCR_FORCE_100M;

    if (enet_write_phy(base, addr, YT8011_BMCR, bmcr) != status_success) {
        return status_fail;
    }
    do {
        if (enet_read_phy(base, addr, YT8011_BMCR, &data) != status_success) {
            return status_fail;
        }
        if ((data & YT8011_BMCR_RESET) == 0U) {
            break;
        }
    } while (--retry > 0U);
    if (retry == 0U) {
        return status_timeout;
    }
    yt8011_delay_ms(YT8011_RESET_STABLE_MS);
    return status_success;
}

/* 软复位 + 使能自协商（BMCR 0x9140） */
hpm_stat_t yt8011_soft_reset_an(ENET_Type *base, uint8_t addr)
{
    uint16_t data = 0;
    uint32_t retry = YT8011_RESET_RETRY_CNT;

    if (enet_write_phy(base, addr, YT8011_BMCR, YT8011_BMCR_RESET_AN) != status_success) {
        return status_fail;
    }
    do {
        if (enet_read_phy(base, addr, YT8011_BMCR, &data) != status_success) {
            return status_fail;
        }
        if ((data & YT8011_BMCR_RESET) == 0U) {
            break;
        }
    } while (--retry > 0U);
    if (retry == 0U) {
        return status_timeout;
    }
    yt8011_delay_ms(YT8011_RESET_STABLE_MS);
    return status_success;
}

hpm_stat_t yt8011_read_ext(ENET_Type *base, uint8_t addr, uint16_t ext_addr, uint16_t *val)
{
    hpm_stat_t stat;

    if (val == NULL) {
        return status_invalid_argument;
    }
    /* 应用说明 §2.1.2: 写 mii 0x1E = ext_reg 地址，然后读 mii 0x1F */
    stat = enet_write_phy(base, addr, 0x1EU, ext_addr);
    if (stat != status_success) {
        return stat;
    }
    return enet_read_phy(base, addr, 0x1FU, val);
}

hpm_stat_t yt8011_write_ext(ENET_Type *base, uint8_t addr, uint16_t ext_addr, uint16_t val)
{
    hpm_stat_t stat = enet_write_phy(base, addr, 0x1EU, ext_addr);
    if (stat != status_success) {
        return stat;
    }
    return enet_write_phy(base, addr, 0x1FU, val);
}

hpm_stat_t yt8011_read_mmd(ENET_Type *base, uint8_t addr, uint8_t devad, uint16_t reg, uint16_t *val)
{
    hpm_stat_t stat;

    if (val == NULL) {
        return status_invalid_argument;
    }
    /* 应用说明 §2.1.3 间接访问序列 */
    stat = enet_write_phy(base, addr, YT8011_MMD_ACR, devad);
    if (stat != status_success) { return stat; }
    stat = enet_write_phy(base, addr, YT8011_MMD_ADR, reg);
    if (stat != status_success) { return stat; }
    stat = enet_write_phy(base, addr, YT8011_MMD_ACR, YT8011_MMD_FUNC_DATA | devad);
    if (stat != status_success) { return stat; }
    return enet_read_phy(base, addr, YT8011_MMD_ADR, val);
}

hpm_stat_t yt8011_write_mmd(ENET_Type *base, uint8_t addr, uint8_t devad, uint16_t reg, uint16_t val)
{
    hpm_stat_t stat;

    stat = enet_write_phy(base, addr, YT8011_MMD_ACR, devad);
    if (stat != status_success) { return stat; }
    stat = enet_write_phy(base, addr, YT8011_MMD_ADR, reg);
    if (stat != status_success) { return stat; }
    stat = enet_write_phy(base, addr, YT8011_MMD_ACR, YT8011_MMD_FUNC_DATA | devad);
    if (stat != status_success) { return stat; }
    return enet_write_phy(base, addr, YT8011_MMD_ADR, val);
}

void yt8011_basic_mode_default_config(ENET_Type *base, yt8011_config_t *cfg)
{
    (void) base;
    if (cfg == NULL) {
        return;
    }
    /* 上电 strap 实测值: RXD3/MS_SEL=0(Slave) RXD2/MII_SEL=0(RGMII)
     * RXC/RXDLY_EN=1, RXD[1:0]=01 -> PHYADDR=1 */
    cfg->speed             = yt8011_speed_1000m;
    cfg->master            = false;
    cfg->auto_negotiation  = false;
    cfg->rx_delay_en       = true;
    cfg->tx_delay_sel      = 1U;
    cfg->tx_clk_sel        = false;
}

hpm_stat_t yt8011_set_rgmii_delay(ENET_Type *base, uint8_t addr, bool rx_delay_en,
                                  uint8_t tx_delay_sel, bool tx_clk_sel)
{
    uint16_t old = 0, val = 0;
    hpm_stat_t stat;

    stat = yt8011_read_ext(base, addr, YT8011_EXT_RGMII_CFG1, &old);
    if (stat != status_success) {
        return stat;
    }
    val = (uint16_t) (old & ~(YT8011_RGMII_CFG1_TX_CLK_SEL |
                              YT8011_RGMII_CFG1_RX_DLY_EN |
                              YT8011_RGMII_CFG1_TX_DLY_MASK));
    if (rx_delay_en) {
        val |= YT8011_RGMII_CFG1_RX_DLY_EN;
    }
    if (tx_clk_sel) {
        val |= YT8011_RGMII_CFG1_TX_CLK_SEL;
    }
    val |= (uint16_t) ((tx_delay_sel << YT8011_RGMII_CFG1_TX_DLY_SHIFT) &
                       YT8011_RGMII_CFG1_TX_DLY_MASK);
    return yt8011_write_ext(base, addr, YT8011_EXT_RGMII_CFG1, val);
}

hpm_stat_t yt8011_get_rgmii_delay(ENET_Type *base, uint8_t addr, uint16_t *raw)
{
    return yt8011_read_ext(base, addr, YT8011_EXT_RGMII_CFG1, raw);
}

hpm_stat_t yt8011_set_mode(ENET_Type *base, uint8_t addr, yt8011_speed_t speed, bool master)
{
    uint16_t val = 0;
    hpm_stat_t stat;

    /* 应用说明 §5.3: MMD1 0x834
     *   bit14 force_config: 1=Master, 0=Slave
     *   bits[3:0] force_phy_type: 1=1000BASE-T1, 0=100BASE-T1 */
    val = (uint16_t) ((master ? 0x4000U : 0x0000U) |
                      ((speed == yt8011_speed_1000m) ? 0x0001U : 0x0000U));
    stat = yt8011_write_mmd(base, addr, 1U, 0x0834U, val);
    if (stat != status_success) {
        return stat;
    }
    /* 修改 MMD1 0x834 后必须做"带速率编码"的软复位，否则复位会回退到上电默认 */
    return yt8011_soft_reset_cfg(base, addr, speed);
}

hpm_stat_t yt8011_basic_mode_init(ENET_Type *base, uint8_t addr, yt8011_config_t *cfg)
{
    hpm_stat_t stat;

    if ((base == NULL) || (cfg == NULL)) {
        return status_invalid_argument;
    }
    if (!yt8011_check_id(base, addr)) {
        return status_fail;
    }

    /* 1) RGMII 时钟延迟（EXT 0x9001） */
    stat = yt8011_set_rgmii_delay(base, addr, cfg->rx_delay_en, cfg->tx_delay_sel, cfg->tx_clk_sel);
    if (stat != status_success) {
        return stat;
    }

    if (cfg->auto_negotiation) {
        /* 2a) 自协商: MMD7 0x200 bit12=1（应用说明 §5.4 注），再写 BMCR 使能 AN */
        stat = yt8011_write_mmd(base, addr, 7U, 0x0200U, 0x1000U);
        if (stat != status_success) {
            return stat;
        }
        return yt8011_soft_reset_an(base, addr);
    }

    /* 2b) 强制模式: 先写 MMD1 0x834 指定速率与主从 */
    stat = yt8011_write_mmd(base, addr, 1U, 0x0834U,
                            (uint16_t) ((cfg->master ? 0x4000U : 0x0000U) |
                                        ((cfg->speed == yt8011_speed_1000m) ? 0x0001U : 0x0000U)));
    if (stat != status_success) {
        return stat;
    }
    /* 3) 带速率编码的软复位让配置生效 */
    return yt8011_soft_reset_cfg(base, addr, cfg->speed);
}

hpm_stat_t yt8011_get_status(ENET_Type *base, uint8_t addr, yt8011_status_t *status)
{
    hpm_stat_t stat;
    uint16_t data = 0;

    if (status == NULL) {
        return status_invalid_argument;
    }
    status->link_up = false;
    status->an_complete = false;
    status->full_duplex = true;

    stat = enet_read_phy(base, addr, YT8011_BMCR, &status->bmcr);
    if (stat != status_success) { return stat; }
    stat = enet_read_phy(base, addr, YT8011_BMSR, &status->bmsr);
    if (stat != status_success) { return stat; }
    /* BMSR[2] 为锁存类型，连续读两次取"当前"值（应用说明 §5.2） */
    stat = enet_read_phy(base, addr, YT8011_BMSR, &data);
    if (stat != status_success) { return stat; }
    status->bmsr = data;
    status->link_up = ((data & YT8011_BMSR_LINK_STATUS) != 0U);
    status->an_complete = ((data & YT8011_BMSR_ANEG_CMPLT) != 0U);

    stat = enet_read_phy(base, addr, YT8011_GBCR, &status->gbcr);
    if (stat != status_success) { return stat; }
    stat = enet_read_phy(base, addr, YT8011_GBSR, &status->gbsr);
    if (stat != status_success) { return stat; }
    status->local_rx_ok  = ((status->gbsr & YT8011_GBSR_LOCAL_RX_OK) != 0U);
    status->remote_rx_ok = ((status->gbsr & YT8011_GBSR_REMOTE_RX_OK) != 0U);

    stat = enet_read_phy(base, addr, YT8011_PHYSR, &status->physr);
    if (stat != status_success) { return stat; }

    /* 0x11: [15:14] speed_mode (10=1000M, 01=100M), [13] master, [10] real-time link */
    status->link_up = ((status->physr & YT8011_PHYSR_LINK_REALTIME) != 0U);
    status->master  = ((status->physr & YT8011_PHYSR_MS_MASTER) != 0U);
    switch ((status->physr >> YT8011_PHYSR_SPEED_SHIFT) & 0x3U) {
    case 0x2U:
        status->speed = yt8011_speed_1000m;
        break;
    case 0x1U:
        status->speed = yt8011_speed_100m;
        break;
    default:
        /* 0x11 速率位无效（链路未建/未解析）时，回退到配置寄存器 MMD1 0x834[3:0] */
        {
            uint16_t mode = 0;
            if (yt8011_read_mmd(base, addr, 1U, 0x0834U, &mode) == status_success) {
                status->speed = ((mode & 0x000FU) != 0U) ? yt8011_speed_1000m : yt8011_speed_100m;
            } else {
                status->speed = yt8011_speed_1000m;
            }
        }
        break;
    }
    return status_success;
}

hpm_stat_t yt8011_wait_link_up(ENET_Type *base, uint8_t addr, uint32_t timeout_ms, yt8011_status_t *status)
{
    uint32_t waited = 0;
    hpm_stat_t stat = status_fail;

    while (waited <= timeout_ms) {
        stat = yt8011_get_status(base, addr, status);
        if ((stat == status_success) && status->link_up) {
            return status_success;
        }
        yt8011_delay_ms(10U);
        waited += 10U;
    }
    return (stat == status_success) ? status_timeout : stat;
}

const char *yt8011_speed_str(yt8011_speed_t speed)
{
    return (speed == yt8011_speed_1000m) ? "1000M" : "100M";
}
