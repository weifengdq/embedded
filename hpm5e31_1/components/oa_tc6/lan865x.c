/*
 * Microchip LAN8650/LAN8651 10BASE-T1S MAC-PHY driver for HPM5E31 (bare metal).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <string.h>
#include "lan865x.h"
#include "board.h"
#include "hpm_gpio_drv.h"
#include "hpm_clock_drv.h"

/* ---- AN1760 rev F (DS60001760G), LAN865x Rev.B0/B1 configuration table ---- */
static const uint16_t lan865x_revb_fixup_regs[17] = {
    0x00D0, 0x00E0, 0x00E9, 0x00F5, 0x00F4, 0x00F8, 0x00F9, 0x0081,
    0x0091, 0x0043, 0x0044, 0x0045, 0x0053, 0x0054, 0x0055, 0x0040,
    0x0050,
};
static const uint16_t lan865x_revb_fixup_vals[17] = {
    0x3F31, 0xC000, 0x9E50, 0x1CF8, 0xC020, 0xB900, 0x4E53, 0x0080,
    0x9660, 0x00FF, 0xFFFF, 0x0000, 0x00FF, 0xFFFF, 0x0000, 0x0002,
    0x0002,
};
static const uint16_t lan865x_revb_cfgparam_regs[2] = {0x0084, 0x008A};
static const uint16_t lan865x_revb_sqi_cfgparam_regs[3] = {0x00AD, 0x00AE, 0x00AF};
static const uint16_t lan865x_revb_sqi_regs[12] = {
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
    0x00B8, 0x00B9, 0x00BA, 0x00BB,
};
static const uint16_t lan865x_revb_sqi_vals[12] = {
    0x0103, 0x0910, 0x1D26, 0x002A, 0x0103, 0x070D, 0x1720, 0x0027,
    0x0509, 0x0E13, 0x1C25, 0x002B,
};

const char *lan865x_strerror(int err)
{
    switch (err) {
    case 0:  return "ok";
    case -1: return "invalid parameter";
    case -2: return "SPI transfer failed";
    case -3: return "MAC-PHY echo mismatch (control/data protection error)";
    case -4: return "reset timeout (STATUS0.RESETC not set)";
    case -5: return "unexpected PHY id";
    case -6: return "PHY register access error";
    default: return "error";
    }
}

static int lan865x_mmd_w(oa_tc6_t *tc6, uint16_t reg, uint16_t val)
{
    return oa_tc6_mmd_write(tc6, OA_TC6_MMS_VEND2, reg, val);
}

static int lan865x_mmd_r(oa_tc6_t *tc6, uint16_t reg, uint16_t *val)
{
    return oa_tc6_mmd_read(tc6, OA_TC6_MMS_VEND2, reg, val);
}

/* AN1760 "indirect read": addr -> 0x00D8, ctrl -> 0x00DA, data <- 0x00D9 */
static int lan865x_cfgparam_read(oa_tc6_t *tc6, uint16_t addr, uint16_t *val)
{
    int ret = lan865x_mmd_w(tc6, LAN865X_REG_CFGPARAM_ADDR, addr);
    if (ret != 0) {
        return ret;
    }
    ret = lan865x_mmd_w(tc6, LAN865X_REG_CFGPARAM_CTRL, LAN865X_CFGPARAM_READ_EN);
    if (ret != 0) {
        return ret;
    }
    return lan865x_mmd_r(tc6, LAN865X_REG_CFGPARAM_DATA, val);
}

int lan865x_hw_reset(oa_tc6_t *tc6)
{
    (void) tc6;
    /* assert nRST (PA19 low), hold >= 1 ms, release and wait for POR */
    gpio_write_pin(BOARD_LAN8651_RST_GPIO_CTRL, BOARD_LAN8651_RST_GPIO_INDEX,
                   BOARD_LAN8651_RST_GPIO_PIN, 0);
    board_delay_ms(2);
    gpio_write_pin(BOARD_LAN8651_RST_GPIO_CTRL, BOARD_LAN8651_RST_GPIO_INDEX,
                   BOARD_LAN8651_RST_GPIO_PIN, 1);
    board_delay_ms(10);
    if (oa_tc6_wait_reset_complete(tc6, 200) != 0) {
        return -4;
    }
    /* clear the reset-complete status (write 1 to clear) */
    (void) oa_tc6_clear_status0(tc6);
    return 0;
}

int lan865x_phy_id(oa_tc6_t *tc6, uint32_t *id)
{
    uint16_t hi = 0;
    uint16_t lo = 0;
    int ret = oa_tc6_c22_read(tc6, 2, &hi);
    if (ret != 0) {
        return -6;
    }
    ret = oa_tc6_c22_read(tc6, 3, &lo);
    if (ret != 0) {
        return -6;
    }
    *id = ((uint32_t) hi << 16) | lo;
    return 0;
}

static int lan865x_generate_cfg_offsets(oa_tc6_t *tc6, int8_t offsets[2])
{
    static const uint16_t regs[2] = {0x0004, 0x0008};
    for (int i = 0; i < 2; i++) {
        uint16_t v = 0;
        int ret = lan865x_cfgparam_read(tc6, regs[i], &v);
        if (ret != 0) {
            return ret;
        }
        v &= 0x1FU;
        offsets[i] = (v & 0x10U) ? (int8_t) (v | 0xE0U) : (int8_t) v;
    }
    return 0;
}

int lan865x_phy_config_init(oa_tc6_t *tc6)
{
    int8_t offsets[2] = {0, 0};
    uint16_t v;
    int ret;

    ret = lan865x_generate_cfg_offsets(tc6, offsets);
    if (ret != 0) {
        return -6;
    }

    for (int i = 0; i < 17; i++) {
        ret = lan865x_mmd_w(tc6, lan865x_revb_fixup_regs[i], lan865x_revb_fixup_vals[i]);
        if (ret != 0) {
            return -6;
        }
        if (i == 1) {
            /* AN1760: computed configuration parameters go to 0x0084 / 0x008A */
            uint16_t cfg[2];
            cfg[0] = (uint16_t) ((((9 + offsets[0]) & 0x3F) << 10) |
                                 (((14 + offsets[0]) & 0x3F) << 4) | 0x03);
            cfg[1] = (uint16_t) (((40 + offsets[1]) & 0x3F) << 10);
            for (int k = 0; k < 2; k++) {
                (void) lan865x_mmd_r(tc6, lan865x_revb_cfgparam_regs[k], &v);   /* keep read-modify-write semantics */
                ret = lan865x_mmd_w(tc6, lan865x_revb_cfgparam_regs[k], cfg[k]);
                if (ret != 0) {
                    return -6;
                }
            }
        }
    }

    /* SQI configuration parameters (AN1760) */
    for (int k = 0; k < 3; k++) {
        (void) lan865x_mmd_r(tc6, lan865x_revb_sqi_cfgparam_regs[k], &v);
    }
    {
        uint16_t sqi[3];
        sqi[0] = (uint16_t) ((((5 + offsets[0]) & 0x3F) << 8) | ((9 + offsets[0]) & 0xFF));
        sqi[1] = (uint16_t) ((((9 + offsets[0]) & 0x3F) << 8) | ((14 + offsets[0]) & 0xFF));
        sqi[2] = (uint16_t) ((((17 + offsets[0]) & 0x3F) << 8) | ((22 + offsets[0]) & 0xFF));
        for (int k = 0; k < 3; k++) {
            ret = lan865x_mmd_w(tc6, lan865x_revb_sqi_cfgparam_regs[k], sqi[k]);
            if (ret != 0) {
                return -6;
            }
        }
    }
    for (int i = 0; i < 12; i++) {
        ret = lan865x_mmd_w(tc6, lan865x_revb_sqi_regs[i], lan865x_revb_sqi_vals[i]);
        if (ret != 0) {
            return -6;
        }
    }

    /* PLCA mode is the normal operating mode for 10BASE-T1S: collision
     * detection can be disabled to improve signal quality (AN1760). */
    (void) lan865x_mmd_w(tc6, LAN86XX_REG_COL_DET_CTRL0, COL_DET_DISABLE);
    return 0;
}

int lan865x_mac_set_addr(oa_tc6_t *tc6, const uint8_t mac[6])
{
    uint32_t low = ((uint32_t) mac[3] << 24) | ((uint32_t) mac[2] << 16) |
                   ((uint32_t) mac[1] << 8) | (uint32_t) mac[0];
    uint32_t high = ((uint32_t) mac[5] << 8) | (uint32_t) mac[4];
    int ret = oa_tc6_write_reg(tc6, LAN865X_REG_MAC_L_SADDR1, low);
    if (ret != 0) {
        return -3;
    }
    ret = oa_tc6_write_reg(tc6, LAN865X_REG_MAC_H_SADDR1, high);
    if (ret != 0) {
        return -3;
    }
    return 0;
}

int lan865x_mac_get_addr(oa_tc6_t *tc6, uint8_t mac[6])
{
    uint32_t low = 0;
    uint32_t high = 0;
    if (oa_tc6_read_reg(tc6, LAN865X_REG_MAC_L_SADDR1, &low) != 0) {
        return -3;
    }
    if (oa_tc6_read_reg(tc6, LAN865X_REG_MAC_H_SADDR1, &high) != 0) {
        return -3;
    }
    mac[0] = (uint8_t) (low >> 0);
    mac[1] = (uint8_t) (low >> 8);
    mac[2] = (uint8_t) (low >> 16);
    mac[3] = (uint8_t) (low >> 24);
    mac[4] = (uint8_t) (high >> 0);
    mac[5] = (uint8_t) (high >> 8);
    return 0;
}

int lan865x_mac_enable(oa_tc6_t *tc6, bool tx_enable, bool rx_enable)
{
    uint32_t v = 0;
    if (oa_tc6_read_reg(tc6, LAN865X_REG_MAC_NET_CTL, &v) != 0) {
        return -3;
    }
    if (tx_enable) {
        v |= MAC_NET_CTL_TXEN;
    } else {
        v &= ~MAC_NET_CTL_TXEN;
    }
    if (rx_enable) {
        v |= MAC_NET_CTL_RXEN;
    } else {
        v &= ~MAC_NET_CTL_RXEN;
    }
    if (oa_tc6_write_reg(tc6, LAN865X_REG_MAC_NET_CTL, v) != 0) {
        return -3;
    }
    return 0;
}

int lan865x_mac_set_promisc(oa_tc6_t *tc6, bool enable)
{
    uint32_t cfg = enable ? MAC_NET_CFG_PROMISCUOUS : 0U;
    (void) oa_tc6_write_reg(tc6, LAN865X_REG_MAC_L_HASH, enable ? 0xFFFFFFFFU : 0U);
    (void) oa_tc6_write_reg(tc6, LAN865X_REG_MAC_H_HASH, enable ? 0xFFFFFFFFU : 0U);
    if (oa_tc6_write_reg(tc6, LAN865X_REG_MAC_NET_CFG, cfg) != 0) {
        return -3;
    }
    return 0;
}

int lan865x_plca_configure(oa_tc6_t *tc6, lan865x_plca_cfg_t *cfg)
{
    uint16_t ctrl1 = (uint16_t) (((uint16_t) cfg->node_cnt << 8) | cfg->node_id);
    uint16_t burst = (uint16_t) (((uint16_t) cfg->burst_cnt << 8) | cfg->burst_tmr);

    /* 1) disable PLCA */
    if (lan865x_mmd_w(tc6, MDIO_OATC14_PLCA_CTRL0, 0) != 0) {
        return -6;
    }
    /* 2) reset the PLCA sub-block */
    if (lan865x_mmd_w(tc6, MDIO_OATC14_PLCA_CTRL0, PLCA_CTRL0_RST) != 0) {
        return -6;
    }
    /* 3) node count / node id, TO timer, burst mode */
    if (lan865x_mmd_w(tc6, MDIO_OATC14_PLCA_CTRL1, ctrl1) != 0) {
        return -6;
    }
    if (lan865x_mmd_w(tc6, MDIO_OATC14_PLCA_TOTMR, cfg->to_tmr) != 0) {
        return -6;
    }
    if (lan865x_mmd_w(tc6, MDIO_OATC14_PLCA_BURST, burst) != 0) {
        return -6;
    }
    /* 4) enable (or keep disabled) */
    if (lan865x_mmd_w(tc6, MDIO_OATC14_PLCA_CTRL0, cfg->enabled ? PLCA_CTRL0_EN : 0) != 0) {
        return -6;
    }
    return lan865x_plca_read(tc6, cfg);
}

int lan865x_plca_read(oa_tc6_t *tc6, lan865x_plca_cfg_t *cfg)
{
    uint16_t ctrl0 = 0;
    uint16_t ctrl1 = 0;
    uint16_t totmr = 0;
    uint16_t burst = 0;
    uint16_t status = 0;

    if (lan865x_mmd_r(tc6, MDIO_OATC14_PLCA_CTRL0, &ctrl0) != 0) {
        return -6;
    }
    if (lan865x_mmd_r(tc6, MDIO_OATC14_PLCA_CTRL1, &ctrl1) != 0) {
        return -6;
    }
    if (lan865x_mmd_r(tc6, MDIO_OATC14_PLCA_TOTMR, &totmr) != 0) {
        return -6;
    }
    if (lan865x_mmd_r(tc6, MDIO_OATC14_PLCA_BURST, &burst) != 0) {
        return -6;
    }
    if (lan865x_mmd_r(tc6, MDIO_OATC14_PLCA_STATUS, &status) != 0) {
        return -6;
    }
    cfg->enabled = (ctrl0 & PLCA_CTRL0_EN) != 0U;
    cfg->node_cnt = (uint8_t) ((ctrl1 >> 8) & 0xFFU);
    cfg->node_id = (uint8_t) (ctrl1 & 0xFFU);
    cfg->to_tmr = (uint8_t) (totmr & 0xFFU);
    cfg->burst_cnt = (uint8_t) ((burst >> 8) & 0xFFU);
    cfg->burst_tmr = (uint8_t) (burst & 0xFFU);
    cfg->status = status;
    return 0;
}

int lan865x_plca_status(oa_tc6_t *tc6, uint16_t *status)
{
    return lan865x_mmd_r(tc6, MDIO_OATC14_PLCA_STATUS, status);
}

int lan865x_phy_sts1(oa_tc6_t *tc6, uint16_t *sts1)
{
    return lan865x_mmd_r(tc6, LAN86XX_REG_STS1, sts1);
}

int lan865x_phy_irq_enable(oa_tc6_t *tc6, bool enable)
{
    uint16_t mask = LAN86XX_STS1_PLCA_STS_CHANGED;
    uint16_t v = 0;
    (void) lan865x_mmd_r(tc6, LAN86XX_REG_STS1, &v);   /* clear pending */
    if (enable) {
        /* a mask bit of 0 enables the interrupt source */
        if (lan865x_mmd_r(tc6, LAN86XX_REG_IMSK1, &v) != 0) {
            return -6;
        }
        v &= (uint16_t) ~mask;
    } else {
        if (lan865x_mmd_r(tc6, LAN86XX_REG_IMSK1, &v) != 0) {
            return -6;
        }
        v |= mask;
    }
    if (lan865x_mmd_w(tc6, LAN86XX_REG_IMSK1, v) != 0) {
        return -6;
    }
    (void) lan865x_mmd_r(tc6, LAN86XX_REG_STS1, &v);   /* clear pending again */
    return 0;
}

int lan865x_full_init(oa_tc6_t *tc6, const uint8_t mac[6])
{
    int ret;

    ret = lan865x_hw_reset(tc6);
    if (ret != 0) {
        return ret;
    }
    ret = oa_tc6_unmask_error_interrupts(tc6);
    if (ret != 0) {
        return ret;
    }
    ret = lan865x_phy_config_init(tc6);
    if (ret != 0) {
        return ret;
    }
    ret = oa_tc6_enable_data_transfer(tc6);
    if (ret != 0) {
        return ret;
    }
    ret = oa_tc6_zarfe_enable(tc6);
    if (ret != 0) {
        return ret;
    }
    ret = lan865x_mac_set_addr(tc6, mac);
    if (ret != 0) {
        return ret;
    }
    ret = lan865x_mac_set_promisc(tc6, false);
    if (ret != 0) {
        return ret;
    }
    ret = lan865x_mac_enable(tc6, true, true);
    if (ret != 0) {
        return ret;
    }
    (void) oa_tc6_update_buffer_status(tc6);
    return 0;
}
