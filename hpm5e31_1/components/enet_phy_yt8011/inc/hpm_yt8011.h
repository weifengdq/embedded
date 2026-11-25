/*
 * hpm_yt8011.h - YT8011AN 千兆车载以太网 PHY 驱动（ENET MDIO 后端）
 *
 * 支持: RGMII MAC 接口; 线速 1000BASE-T1 / 100BASE-T1 (单对线车载以太网)
 *
 * 关键参考资料:
 *   YT8011A_1.pdf V1.4  §4 Register Overview (p.39-)
 *   YT8011Ax_应用说明_V2.2.pdf §2 寄存器类型及访问方法 (p.10-12)
 *                              §5.3 Master/Slave 配置 (p.26-27)
 *                              §5.4 1000BASE-T1/100BASE-T1 配置 (p.27)
 *                              §5.2 Link 状态获取 (p.25)
 *
 * 设计: API 形态对齐 hpm_sdk/components/enet_phy（rtl8211/dp83867），
 *       便于在 lwIP 移植时替换 hpm_enet_phy_port_init()/get_status()。
 */
#ifndef HPM_YT8011_H
#define HPM_YT8011_H

#include "hpm_common.h"
#include "hpm_enet_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YT8011_ADDR_DEFAULT      (1U)   /* strapping RXD[1:0]=01 -> PHYADDR=1 */
#define YT8011_ADDR_BROADCAST    (0U)   /* 器件固定响应地址 0 */

typedef enum {
    yt8011_speed_1000m = 0,
    yt8011_speed_100m  = 1,
} yt8011_speed_t;

typedef struct {
    yt8011_speed_t speed;       /* 1000BASE-T1 或 100BASE-T1 */
    bool master;                /* true=Master(强制), false=Slave(强制) */
    bool auto_negotiation;      /* true: 走 MMD7 0x200 自协商（会影响建链时间） */
    bool rx_delay_en;           /* EXT 0x9001[8] RXC 2ns 延迟, 板级 strap 默认 1 */
    uint8_t tx_delay_sel;       /* EXT 0x9001[7:4] TXC 延迟档位, 默认 4（R10：档位 1 有 ~1% CRC 错） */
    bool tx_clk_sel;            /* EXT 0x9001[9] 反相 GMII 时钟 */
} yt8011_config_t;

typedef struct {
    bool link_up;
    bool an_complete;
    yt8011_speed_t speed;
    bool full_duplex;           /* 1000/100BASE-T1 恒为全双工 */
    bool master;                /* 0x11[13] 主从结果 */
    bool local_rx_ok;           /* GBSR[13] */
    bool remote_rx_ok;          /* GBSR[12] */
    uint16_t bmcr;
    uint16_t bmsr;
    uint16_t gbcr;
    uint16_t gbsr;
    uint16_t physr;             /* 0x11 PHY Specific Status */
} yt8011_status_t;

/* ---------------- 基础 ---------------- */
bool yt8011_check_id(ENET_Type *base, uint8_t addr);
hpm_stat_t yt8011_reset(ENET_Type *base, uint8_t addr);

/* 带速率编码的软复位: 依据应用说明 §5.4 写 0x8140/0xA100 */
hpm_stat_t yt8011_soft_reset_cfg(ENET_Type *base, uint8_t addr, yt8011_speed_t speed);
hpm_stat_t yt8011_soft_reset_an(ENET_Type *base, uint8_t addr);

/* EXT 寄存器（0x0000~0x9xxx）间接访问: 写 mii 0x1E=地址, 读写 mii 0x1F */
hpm_stat_t yt8011_read_ext(ENET_Type *base, uint8_t addr, uint16_t ext_addr, uint16_t *val);
hpm_stat_t yt8011_write_ext(ENET_Type *base, uint8_t addr, uint16_t ext_addr, uint16_t val);

/* MMD 寄存器（IEEE802.3 clause45）间接访问: 0x0D/0x0E 序列 */
hpm_stat_t yt8011_read_mmd(ENET_Type *base, uint8_t addr, uint8_t devad, uint16_t reg, uint16_t *val);
hpm_stat_t yt8011_write_mmd(ENET_Type *base, uint8_t addr, uint8_t devad, uint16_t reg, uint16_t val);

/* ---------------- 配置 ---------------- */
void yt8011_basic_mode_default_config(ENET_Type *base, yt8011_config_t *cfg);
/* 应用 speed/master/AN + RGMII delay，并做软复位使配置生效 */
hpm_stat_t yt8011_basic_mode_init(ENET_Type *base, uint8_t addr, yt8011_config_t *cfg);

/* 单独设置工作模式: MMD1 0x834 = force_config|phy_type，然后软复位 */
hpm_stat_t yt8011_set_mode(ENET_Type *base, uint8_t addr, yt8011_speed_t speed, bool master);

/* 配置 RGMII 时钟延迟（EXT 0x9001），写后读回 */
hpm_stat_t yt8011_set_rgmii_delay(ENET_Type *base, uint8_t addr, bool rx_delay_en,
                                  uint8_t tx_delay_sel, bool tx_clk_sel);
hpm_stat_t yt8011_get_rgmii_delay(ENET_Type *base, uint8_t addr, uint16_t *raw);

/* ---------------- 状态 ---------------- */
hpm_stat_t yt8011_get_status(ENET_Type *base, uint8_t addr, yt8011_status_t *status);
hpm_stat_t yt8011_wait_link_up(ENET_Type *base, uint8_t addr, uint32_t timeout_ms, yt8011_status_t *status);

const char *yt8011_speed_str(yt8011_speed_t speed);

#ifdef __cplusplus
}
#endif
#endif /* HPM_YT8011_H */
