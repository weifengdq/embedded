/*
 * enet_phy_config - YT8011AN 寄存器级配置与链路验证（RGMII 千兆车载 PHY）
 *
 * 硬件: hpm5e31_1 + YT8011AN (RGMII, MDC=PA31 MDIO=PA30 nRST=PB29)
 * 功能:
 *   - 打印 PHY ID / 标准寄存器 / EXT 寄存器(0x9001 RGMII 配置) / MMD1 0x834 工作模式
 *   - shell 命令切换 1000BASE-T1 / 100BASE-T1、Master / Slave、RGMII 时钟延迟
 *   - 每次切换后轮询 link 状态并打印 0x11 PHY Specific Status
 * 现象/日志: 见 README.md 与 logs/
 */
#include <string.h>
#include "bsp.h"
#include "hpm_yt8011.h"
#include "hpm_gpio_drv.h"
#include "hpm_mchtmr_drv.h"
#include "hpm_iomux.h"

#define PHY_ADDR    BOARD_NET2_PHY_ADDR
static ENET_Type *const s_enet = BOARD_NET2_ENET_BASE;
static yt8011_config_t s_cfg;

static void dump_regs(void)
{
    const struct { uint8_t reg; const char *name; } regs[] = {
        {0x00, "BMCR "}, {0x01, "BMSR "}, {0x02, "ID1  "}, {0x03, "ID2  "},
        {0x04, "ANAR "}, {0x05, "ANLPAR"}, {0x06, "ANER "}, {0x09, "GBCR "},
        {0x0A, "GBSR "}, {0x0D, "MMDAC"}, {0x0E, "MMDAD"}, {0x11, "PHYSR"},
        {0x12, "INTMSK"}, {0x13, "INTST "}, {0x17, "INTST2"},
    };
    printf("---- MII(clause22) registers ----\r\n");
    for (uint32_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        uint16_t v = 0;
        if (enet_read_phy(s_enet, PHY_ADDR, regs[i].reg, &v) == status_success) {
            printf("  0x%02X %s = 0x%04X\r\n", regs[i].reg, regs[i].name, v);
        } else {
            printf("  0x%02X %s = <timeout>\r\n", regs[i].reg, regs[i].name);
        }
    }
}

static void dump_ext(void)
{
    const struct { uint16_t addr; const char *name; } ext[] = {
        {0x0000, "broadcast ctrl "},
        {0x9000, "SMI_SDS_PHY    "},
        {0x9001, "RGMII_CFG1     "},
        {0x9030, "COMMON_CTRL1   "},
        {0x9081, "LTT link train "},
        {0x9082, "LRT local rcv  "},
        {0x9083, "RRT remote rcv "},
    };
    printf("---- EXT registers (via mii 0x1E/0x1F) ----\r\n");
    for (uint32_t i = 0; i < sizeof(ext) / sizeof(ext[0]); i++) {
        uint16_t v = 0;
        if (yt8011_read_ext(s_enet, PHY_ADDR, ext[i].addr, &v) == status_success) {
            printf("  ext 0x%04X %s = 0x%04X\r\n", ext[i].addr, ext[i].name, v);
        } else {
            printf("  ext 0x%04X %s = <timeout>\r\n", ext[i].addr, ext[i].name);
        }
    }
}

static void dump_mmd(void)
{
    uint16_t v = 0;
    printf("---- MMD registers (via mii 0x0D/0x0E) ----\r\n");
    if (yt8011_read_mmd(s_enet, PHY_ADDR, 1U, 0x0834U, &v) == status_success) {
        printf("  MMD1 0x0834 = 0x%04X (force_config=%s, phy_type=%s)\r\n", v,
               (v & 0x4000U) ? "MASTER" : "SLAVE",
               ((v & 0x000FU) != 0U) ? "1000BASE-T1" : "100BASE-T1");
    } else {
        printf("  MMD1 0x0834 = <timeout>\r\n");
    }
    if (yt8011_read_mmd(s_enet, PHY_ADDR, 7U, 0x0200U, &v) == status_success) {
        printf("  MMD7 0x0200 = 0x%04X (an_en=%u)\r\n", v, (unsigned) !!(v & 0x1000U));
    } else {
        printf("  MMD7 0x0200 = <timeout>\r\n");
    }
}

static void print_status(void)
{
    yt8011_status_t st;
    if (yt8011_get_status(s_enet, PHY_ADDR, &st) != status_success) {
        printf("[phy] get_status failed\r\n");
        return;
    }
    printf("[phy] link=%s speed=%s duplex=%s role=%s local_rx_ok=%u remote_rx_ok=%u\r\n",
           st.link_up ? "UP" : "DOWN", yt8011_speed_str(st.speed), st.full_duplex ? "FULL" : "HALF",
           st.master ? "MASTER" : "SLAVE", (unsigned) st.local_rx_ok, (unsigned) st.remote_rx_ok);
    printf("[phy] BMCR=%04X BMSR=%04X GBCR=%04X GBSR=%04X PHYSR=%04X an_cmplt=%u\r\n",
           st.bmcr, st.bmsr, st.gbcr, st.gbsr, st.physr, (unsigned) st.an_complete);
}

static void wait_and_report(uint32_t timeout_ms)
{
    yt8011_status_t st;
    printf("[phy] waiting link up (<= %u ms) ...\r\n", (unsigned) timeout_ms);
    if (yt8011_wait_link_up(s_enet, PHY_ADDR, timeout_ms, &st) == status_success) {
        printf("[phy] LINK UP after poll: speed=%s role=%s\r\n",
               yt8011_speed_str(st.speed), st.master ? "MASTER" : "SLAVE");
    } else {
        printf("[phy] LINK DOWN (timeout)\r\n");
    }
    print_status();
}

/* ---------------- PHY nINT (PB28) interrupt ---------------- */
#define PHY_INT_CTRL   HPM_GPIO0
#define PHY_INT_PORT   GPIO_DI_GPIOB
#define PHY_INT_PIN    (28U)
#define PHY_INT_PAD    IOC_PAD_PB28

static volatile uint32_t s_phy_isr_cnt;       /* ISR entries */
static volatile uint32_t s_phy_evt_pending;   /* set by ISR, consumed in the main loop */
static uint32_t s_phy_st1_hist[2];            /* MII 0x13 history (current, previous) */
static uint32_t s_phy_st2_hist[2];            /* MII 0x17 history */
static uint32_t s_phy_link_events;
static bool s_phy_irq_on;

/* Keep the ISR as short as possible: clearing the GPIO latch is the only
 * register access here.  MDIO transactions take ~26 us and must not run in
 * interrupt context (they would block the ENET/lwIP main loop). */
SDK_DECLARE_EXT_ISR_M(BOARD_NET2_PHY_INT_IRQ, isr_phy_nint)
void isr_phy_nint(void)
{
    gpio_clear_pin_interrupt_flag(PHY_INT_CTRL, PHY_INT_PORT, PHY_INT_PIN);
    s_phy_isr_cnt++;
    s_phy_evt_pending = 1U;
}

static void phy_int_init(void)
{
    HPM_IOC->PAD[PHY_INT_PAD].FUNC_CTL = IOC_PB28_FUNC_CTL_GPIO_B_28;
    HPM_IOC->PAD[PHY_INT_PAD].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    gpio_set_pin_input(PHY_INT_CTRL, PHY_INT_PORT, PHY_INT_PIN);
    gpio_config_pin_interrupt(PHY_INT_CTRL, PHY_INT_PORT, PHY_INT_PIN,
                              gpio_interrupt_trigger_edge_falling);
    gpio_clear_pin_interrupt_flag(PHY_INT_CTRL, PHY_INT_PORT, PHY_INT_PIN);
    intc_m_enable_irq_with_priority(BOARD_NET2_PHY_INT_IRQ, 1);
    gpio_enable_pin_interrupt(PHY_INT_CTRL, PHY_INT_PORT, PHY_INT_PIN);
    s_phy_irq_on = true;
    printf("[irq] PHY nINT PB28: falling edge, IRQn=%u, idle level=%u\r\n",
           (unsigned) BOARD_NET2_PHY_INT_IRQ,
           (unsigned) gpio_read_pin(PHY_INT_CTRL, PHY_INT_PORT, PHY_INT_PIN));
}

/* Read the latched PHY interrupt status (MII 0x13 / 0x17 are read-to-clear on
 * YT8011) and log the link transition.  Called from the main loop only. */
static void phy_event_poll(void)
{
    uint16_t st1 = 0, st2 = 0, msk = 0;
    yt8011_status_t st;

    if (s_phy_evt_pending == 0U) {
        return;
    }
    s_phy_evt_pending = 0U;
    (void) enet_read_phy(s_enet, PHY_ADDR, 0x13, &st1);
    (void) enet_read_phy(s_enet, PHY_ADDR, 0x17, &st2);
    (void) enet_read_phy(s_enet, PHY_ADDR, 0x12, &msk);
    s_phy_st1_hist[1] = s_phy_st1_hist[0]; s_phy_st1_hist[0] = st1;
    s_phy_st2_hist[1] = s_phy_st2_hist[0]; s_phy_st2_hist[0] = st2;
    s_phy_link_events++;
    if (yt8011_get_status(s_enet, PHY_ADDR, &st) == status_success) {
        printf("[irq] #%lu isr=%lu INTST(0x13)=0x%04X INTST2(0x17)=0x%04X INTMSK(0x12)=0x%04X link=%u speed=%s role=%s\r\n",
               (unsigned long) s_phy_link_events, (unsigned long) s_phy_isr_cnt,
               st1, st2, msk, (unsigned) st.link_up, yt8011_speed_str(st.speed),
               st.master ? "MASTER" : "SLAVE");
    } else {
        printf("[irq] #%lu isr=%lu INTST=0x%04X INTST2=0x%04X (get_status failed)\r\n",
               (unsigned long) s_phy_link_events, (unsigned long) s_phy_isr_cnt, st1, st2);
    }
}

static void cmd_irq(int argc, char *argv[])
{
    if (argc >= 2 && argv[1][0] == 'o') {
        if (!s_phy_irq_on) {
            gpio_clear_pin_interrupt_flag(PHY_INT_CTRL, PHY_INT_PORT, PHY_INT_PIN);
            gpio_enable_pin_interrupt(PHY_INT_CTRL, PHY_INT_PORT, PHY_INT_PIN);
            s_phy_irq_on = true;
        }
        printf("[irq] enabled=%u nINT level=%u\r\n", (unsigned) s_phy_irq_on,
               (unsigned) gpio_read_pin(PHY_INT_CTRL, PHY_INT_PORT, PHY_INT_PIN));
    } else if (argc >= 2 && argv[1][0] == 'f') {
        gpio_disable_pin_interrupt(PHY_INT_CTRL, PHY_INT_PORT, PHY_INT_PIN);
        s_phy_irq_on = false;
        printf("[irq] disabled (ISR count=%lu)\r\n", (unsigned long) s_phy_isr_cnt);
    } else {
        printf("[irq] on=%u isr=%lu events=%lu INTST=0x%04X/0x%04X INTST2=0x%04X/0x%04X\r\n",
               (unsigned) s_phy_irq_on, (unsigned long) s_phy_isr_cnt,
               (unsigned long) s_phy_link_events,
               s_phy_st1_hist[0], s_phy_st1_hist[1], s_phy_st2_hist[0], s_phy_st2_hist[1]);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 irq, cmd_irq, PHY nINT control/stat: irq [on|off]);

static void cmd_ints(int argc, char *argv[])
{
    uint16_t st1 = 0, st2 = 0, msk = 0;
    (void) argc; (void) argv;
    (void) enet_read_phy(s_enet, PHY_ADDR, 0x12, &msk);
    (void) enet_read_phy(s_enet, PHY_ADDR, 0x13, &st1);
    (void) enet_read_phy(s_enet, PHY_ADDR, 0x17, &st2);
    printf("[irq] INTMSK(0x12)=0x%04X INTST(0x13)=0x%04X INTST2(0x17)=0x%04X nINT=%u\r\n",
           msk, st1, st2, (unsigned) gpio_read_pin(PHY_INT_CTRL, PHY_INT_PORT, PHY_INT_PIN));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ints, cmd_ints, read/clear PHY interrupt status: ints);

/* ---------------- MDIO address scan / EXT 0x0000 investigation -------- */
static void mdio_scan_print(void)
{
    printf("[mdio] scanning PHY addresses 0..31:\r\n");
    for (uint32_t pa = 0; pa <= 31U; pa++) {
        uint16_t id1 = 0xFFFF, id2 = 0xFFFF;
        if ((enet_read_phy(s_enet, (uint8_t) pa, MII_PHYID1, &id1) != status_success) ||
            (enet_read_phy(s_enet, (uint8_t) pa, MII_PHYID2, &id2) != status_success)) {
            printf("  pa=%2u read timeout\r\n", (unsigned) pa);
            continue;
        }
        if ((id1 == 0xFFFFU && id2 == 0xFFFFU) || (id1 == 0x0000U && id2 == 0x0000U)) {
            continue;
        }
        printf("  pa=%2u ID1=0x%04X ID2=0x%04X OUI=0x%06X\r\n", (unsigned) pa, id1, id2,
               ((uint32_t) (id2 & 0xFFC0U) << 8) | (id1 >> 8));
    }
}

static void cmd_scan(int argc, char *argv[])
{
    (void) argc; (void) argv;
    mdio_scan_print();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 scan, cmd_scan, scan MDIO addresses 0..31);

/* Datasheet section 3 (p.38) says EXT 0x0000[4:0] is the broadcast address and
 * [5] enables it, default 0x1F/on.  This board read 0x0000 instead, so probe
 * whether the register behaves as documented: write 0x003F, read back, rescan. */
static void cmd_bcast(int argc, char *argv[])
{
    uint16_t v0 = 0, v1 = 0, v2 = 0;
    (void) argc; (void) argv;
    (void) yt8011_read_ext(s_enet, PHY_ADDR, 0x0000, &v0);
    printf("[bcast] ext 0x0000 read = 0x%04X\r\n", v0);
    (void) yt8011_write_ext(s_enet, PHY_ADDR, 0x0000, 0x003FU);
    (void) yt8011_read_ext(s_enet, PHY_ADDR, 0x0000, &v1);
    printf("[bcast] write 0x003F -> readback 0x%04X\r\n", v1);
    mdio_scan_print();
    (void) yt8011_write_ext(s_enet, PHY_ADDR, 0x0000, v0);
    (void) yt8011_read_ext(s_enet, PHY_ADDR, 0x0000, &v2);
    printf("[bcast] restored 0x%04X -> readback 0x%04X\r\n", v0, v2);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 bcast, cmd_bcast, EXT 0x0000 broadcast-address probe: bcast);

/* ---------------- shell ---------------- */
static void cmd_status(int argc, char *argv[])
{
    (void) argc; (void) argv;
    print_status();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 status, cmd_status, show PHY link/speed/role status);

static void cmd_dump(int argc, char *argv[])
{
    (void) argc; (void) argv;
    dump_regs(); dump_ext(); dump_mmd();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dump, cmd_dump, dump MII/EXT/MMD registers);

static void cmd_mode(int argc, char *argv[])
{
    if (argc < 3) {
        printf("usage: mode <1000|100> <master|slave|auto>\r\n");
        return;
    }
    uint32_t spd = 1000;
    bsp_parse_u32(argv[1], &spd);
    yt8011_config_t cfg;
    yt8011_basic_mode_default_config(s_enet, &cfg);

    if (strncmp(argv[2], "auto", 4) == 0) {
        cfg.auto_negotiation = true;
    } else {
        cfg.auto_negotiation = false;
        cfg.master = (strncmp(argv[2], "master", 6) == 0);
    }
    cfg.speed = (spd == 100U) ? yt8011_speed_100m : yt8011_speed_1000m;

    printf("[phy] apply mode: speed=%s role=%s an=%u ...\r\n", yt8011_speed_str(cfg.speed),
           cfg.auto_negotiation ? "AN" : (cfg.master ? "MASTER" : "SLAVE"),
           (unsigned) cfg.auto_negotiation);
    hpm_stat_t st = yt8011_basic_mode_init(s_enet, PHY_ADDR, &cfg);
    if (st != status_success) {
        printf("[phy] init failed: %d\r\n", (int) st);
        return;
    }
    s_cfg = cfg;
    dump_mmd();
    wait_and_report(cfg.auto_negotiation ? 6000U : 3000U);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mode, cmd_mode, set speed/role: mode <1000|100> <master|slave|auto>);

static void cmd_rgmii(int argc, char *argv[])
{
    uint32_t rxen = 1, txsel = 1, txclk = 0;
    if (argc >= 2) { bsp_parse_u32(argv[1], &rxen); }
    if (argc >= 3) { bsp_parse_u32(argv[2], &txsel); }
    if (argc >= 4) { bsp_parse_u32(argv[3], &txclk); }
    if (txsel > 15U) { txsel = 15U; }
    printf("[phy] set RGMII delay: rx_delay_en=%u tx_delay_sel=%u tx_clk_sel=%u\r\n",
           (unsigned) rxen, (unsigned) txsel, (unsigned) txclk);
    if (yt8011_set_rgmii_delay(s_enet, PHY_ADDR, rxen != 0U, (uint8_t) txsel, txclk != 0U) != status_success) {
        printf("[phy] set rgmii delay failed\r\n");
        return;
    }
    uint16_t raw = 0;
    if (yt8011_get_rgmii_delay(s_enet, PHY_ADDR, &raw) == status_success) {
        printf("[phy] EXT 0x9001 readback = 0x%04X\r\n", raw);
    }
    printf("[phy] note: 0x9001 生效需软复位, 已在 mode 命令中执行\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 rgmii, cmd_rgmii, set RGMII delay: rgmii [rx_delay_en] [tx_delay_sel] [tx_clk_sel]);

static void cmd_ext(int argc, char *argv[])
{
    uint32_t a = 0, v = 0;
    if (argc < 2) {
        printf("usage: ext <addr_hex> [value_hex]   (write when value given)\r\n");
        return;
    }
    bsp_parse_u32(argv[1], &a);
    if (argc >= 3) {
        bsp_parse_u32(argv[2], &v);
        printf("ext 0x%04X <- 0x%04X : %s\r\n", (unsigned) a, (unsigned) v,
               (yt8011_write_ext(s_enet, PHY_ADDR, (uint16_t) a, (uint16_t) v) == status_success) ? "ok" : "fail");
    }
    uint16_t rb = 0;
    if (yt8011_read_ext(s_enet, PHY_ADDR, (uint16_t) a, &rb) == status_success) {
        printf("ext 0x%04X = 0x%04X\r\n", (unsigned) a, rb);
    } else {
        printf("ext 0x%04X read timeout\r\n", (unsigned) a);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ext, cmd_ext, EXT register access: ext <addr> [value]);

static void cmd_mmd(int argc, char *argv[])
{
    uint32_t dev = 1, reg = 0, v = 0;
    if (argc < 3) {
        printf("usage: mmd <devad> <reg_hex> [value_hex]\r\n");
        return;
    }
    bsp_parse_u32(argv[1], &dev);
    bsp_parse_u32(argv[2], &reg);
    if (argc >= 4) {
        bsp_parse_u32(argv[3], &v);
        yt8011_write_mmd(s_enet, PHY_ADDR, (uint8_t) dev, (uint16_t) reg, (uint16_t) v);
    }
    uint16_t rb = 0;
    if (yt8011_read_mmd(s_enet, PHY_ADDR, (uint8_t) dev, (uint16_t) reg, &rb) == status_success) {
        printf("MMD%u 0x%04X = 0x%04X\r\n", (unsigned) dev, (unsigned) reg, rb);
    } else {
        printf("MMD%u 0x%04X read timeout\r\n", (unsigned) dev, (unsigned) reg);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mmd, cmd_mmd, MMD access: mmd <devad> <reg> [value]);

static void cmd_rd(int argc, char *argv[])
{
    uint32_t reg = 0, cnt = 1;
    if (argc < 2) {
        printf("usage: rd <reg> [count]\r\n");
        return;
    }
    bsp_parse_u32(argv[1], &reg);
    if (argc >= 3) { bsp_parse_u32(argv[2], &cnt); }
    for (uint32_t i = 0; i < cnt && (reg + i) <= 31U; i++) {
        uint16_t v = 0;
        if (enet_read_phy(s_enet, PHY_ADDR, (uint8_t) (reg + i), &v) == status_success) {
            printf("mii[0x%02X] = 0x%04X\r\n", (unsigned) (reg + i), v);
        } else {
            printf("mii[0x%02X] read timeout\r\n", (unsigned) (reg + i));
        }
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 rd, cmd_rd, read MII reg: rd <reg> [count]);

static void cmd_wr(int argc, char *argv[])
{
    uint32_t reg = 0, val = 0;
    if (argc < 3) {
        printf("usage: wr <reg> <value>\r\n");
        return;
    }
    bsp_parse_u32(argv[1], &reg);
    bsp_parse_u32(argv[2], &val);
    if (enet_write_phy(s_enet, PHY_ADDR, (uint8_t) reg, (uint16_t) val) != status_success) {
        printf("mii[0x%02X] write timeout\r\n", (unsigned) reg);
        return;
    }
    printf("mii[0x%02X] <- 0x%04X\r\n", (unsigned) reg, (unsigned) val);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 wr, cmd_wr, write MII reg: wr <reg> <value>);

static void cmd_poll(int argc, char *argv[])
{
    uint32_t secs = 5;
    if (argc >= 2) { bsp_parse_u32(argv[1], &secs); }
    printf("[phy] poll status every 200ms for %u s:\r\n", (unsigned) secs);
    for (uint32_t i = 0; i < secs * 5U; i++) {
        yt8011_status_t st;
        if (yt8011_get_status(s_enet, PHY_ADDR, &st) == status_success) {
            printf("  [%4u ms] link=%u speed=%s role=%s PHYSR=%04X GBSR=%04X\r\n",
                   (unsigned) (i * 200U), (unsigned) st.link_up, yt8011_speed_str(st.speed),
                   st.master ? "MASTER" : "SLAVE", st.physr, st.gbsr);
        }
        board_delay_ms(200);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 poll, cmd_poll, poll PHY status: poll [secs]);

static void cmd_restore(int argc, char *argv[])
{
    (void) argc; (void) argv;
    yt8011_config_t cfg;
    yt8011_basic_mode_default_config(s_enet, &cfg);
    printf("[phy] restore board default: 1000BASE-T1 slave, RXC delay on, tx_delay_sel=1\r\n");
    (void) yt8011_basic_mode_init(s_enet, PHY_ADDR, &cfg);
    wait_and_report(3000U);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 restore, cmd_restore, restore default PHY mode and report link);

int main(void)
{
    bsp_init();
    yt8011_basic_mode_default_config(s_enet, &s_cfg);

    printf("\r\n=== enet_phy_config: YT8011AN RGMII PHY register config ===\r\n");
    net2_enet_board_init();
    printf("[phy] PHY reset done (PB29), PHYADDR=%u\r\n", (unsigned) PHY_ADDR);
    phy_int_init();

    if (!yt8011_check_id(s_enet, PHY_ADDR)) {
        printf("[phy] ERROR: PHY ID mismatch (expect 0x4F51/0x3A)!\r\n");
    } else {
        printf("[phy] PHY ID OK: YT8011 (0x4F51 / OUI 0x3A)\r\n");
    }

    dump_regs();
    dump_ext();
    dump_mmd();
    print_status();
    wait_and_report(5000U);
    printf("[phy] commands: status | dump | rd/wr | mode <1000|100> <master|slave|auto> | rgmii [rxen] [txsel] [txclk] | ext <addr> [val] | mmd <dev> <reg> [val] | poll [secs] | restore\r\n");

    while (1) {
        bsp_shell_poll();
        phy_event_poll();
    }
    return 0;
}
