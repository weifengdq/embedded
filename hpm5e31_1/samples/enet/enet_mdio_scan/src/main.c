/*
 * enet_mdio_scan - ENET0 MDIO(Clause 22) 扫描 YT8011AN 千兆 PHY
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1) + YT8011AN RGMII PHY
 *   MDC=PA31, MDIO=PA30, nRST=PB29, nINT=PB28
 * 功能:
 *   1) 上电复位 PHY 后自动扫描 MDIO 地址 0..31，打印 PHY ID / BMCR / BMSR
 *   2) shell 命令: scan / rd / wr / phy / rst / link
 * 现象: 见 README.md 与 logs/ 下串口日志
 */
#include <string.h>
#include "bsp.h"
#include "hpm_enet_drv.h"

static ENET_Type *const s_enet = BOARD_NET2_ENET_BASE;

/* ---------------- MDIO 基础读写 ---------------- */
static bool mdio_read(uint8_t pa, uint8_t reg, uint16_t *val)
{
    return enet_read_phy(s_enet, pa, reg, val) == status_success;
}

static bool mdio_write(uint8_t pa, uint8_t reg, uint16_t val)
{
    return enet_write_phy(s_enet, pa, reg, val) == status_success;
}

/* 读 2 次取与（BMSR 的 LINK 等位读后行为需要连读） */
static uint16_t mdio_read_stable(uint8_t pa, uint8_t reg)
{
    uint16_t a = 0, b = 0;
    (void) mdio_read(pa, reg, &a);
    (void) mdio_read(pa, reg, &b);
    return (uint16_t) (a & b);
}

static bool phy_present(uint8_t pa, uint16_t *id1, uint16_t *id2)
{
    uint16_t a = 0xFFFF, b = 0xFFFF;
    if (!mdio_read(pa, MII_PHYID1, &a) || !mdio_read(pa, MII_PHYID2, &b)) {
        return false;
    }
    *id1 = a;
    *id2 = b;
    /* 无器件时总线被上拉读回 0xFFFF；ID 全 0 也不合法 */
    return !((a == 0xFFFFU && b == 0xFFFFU) || (a == 0x0000U && b == 0x0000U));
}

static const char *speed_str(uint16_t bmcr, uint16_t bmsr)
{
    if (bmsr & BMSR_LINK_STATUS) {
        if (bmcr & BMCR_ANENABLE) {
            return "auto";
        }
        if (bmcr & BMCR_SPEED_MSB) {
            return "1000M";
        }
        return (bmcr & BMCR_SPEED_LSB) ? "100M" : "10M";
    }
    return "down";
}

/* ---------------- 扫描 ---------------- */
static void do_scan(void)
{
    printf("\r\n[mdio] scan MDIO bus (ENET0, clause22, addr 0..31)\r\n");
    printf("addr  ID1     ID2     OUI     MODEL REV  BMCR   BMSR   LINK\r\n");
    printf("----  ------  ------  ------  ----- ----  -----  -----  ----\r\n");
    uint32_t found = 0;
    for (uint32_t pa = 0; pa <= 31U; pa++) {
        uint16_t id1, id2;
        if (!phy_present((uint8_t) pa, &id1, &id2)) {
            continue;
        }
        uint16_t bmcr = mdio_read_stable((uint8_t) pa, MII_BMCR);
        uint16_t bmsr = mdio_read_stable((uint8_t) pa, MII_BMSR);
        uint32_t oui   = ((uint32_t) (id1 & 0xFFFFU) << 6) | ((uint32_t) (id2 >> 10) & 0x3FU);
        uint32_t model = (uint32_t) (id2 >> 4) & 0x3FU;
        uint32_t rev   = (uint32_t) id2 & 0x0FU;
        printf("%4u  %04X    %04X    %06X  %02u    %u    %04X   %04X   %s\r\n",
               (unsigned) pa, id1, id2, (unsigned) oui, (unsigned) model, (unsigned) rev,
               bmcr, bmsr, (bmsr & BMSR_LINK_STATUS) ? "UP" : "dn");
        found++;
    }
    printf("[mdio] %u device(s) found\r\n", (unsigned) found);
    if (found == 0U) {
        printf("[mdio] WARN: 无 PHY 应答，检查 MDC/MDIO 上拉、PHY 供电与 nRST(PB29)\r\n");
    }
}

/* ---------------- PHY 状态 ---------------- */
static void show_phy(uint8_t pa)
{
    uint16_t id1 = 0, id2 = 0, bmcr = 0, bmsr = 0, gbcr = 0, gbsr = 0, sr = 0;
    if (!phy_present(pa, &id1, &id2)) {
        printf("PHY@%u: no response\r\n", (unsigned) pa);
        return;
    }
    (void) mdio_read(pa, MII_BMCR, &bmcr);
    bmsr = mdio_read_stable(pa, MII_BMSR);
    (void) mdio_read(pa, MII_GBCR, &gbcr);
    (void) mdio_read(pa, MII_GBSR, &gbsr);
    (void) mdio_read(pa, MII_PHYSTS, &sr);

    printf("PHY@%u ID1=%04X ID2=%04X\r\n", (unsigned) pa, id1, id2);
    printf("  BMCR=%04X reset=%u loopback=%u speed_bit=%u an_en=%u pwr_dn=%u isolate=%u restrt_an=%u duplex=%u\r\n",
           bmcr,
           (unsigned) !!(bmcr & BMCR_RESET), (unsigned) !!(bmcr & BMCR_LOOPBACK),
           (unsigned) !!((bmcr & BMCR_SPEED_MSB) || (bmcr & BMCR_SPEED_LSB)),
           (unsigned) !!(bmcr & BMCR_ANENABLE), (unsigned) !!(bmcr & BMCR_POWERDOWN),
           (unsigned) !!(bmcr & BMCR_ISOLATE), (unsigned) !!(bmcr & BMCR_RESTART_AN),
           (unsigned) !!(bmcr & BMCR_DUPLEX));
    printf("  BMSR=%04X link=%u an_done=%u an_able=%u 10FD=%u 100FD=%u lp_1000FD=%u\r\n",
           bmsr,
           (unsigned) !!(bmsr & BMSR_LINK_STATUS), (unsigned) !!(bmsr & BMSR_ANEGCOMPLETE),
           (unsigned) !!(bmsr & BMSR_ANEGCAPABLE), (unsigned) !!(bmsr & BMSR_10T_FD),
           (unsigned) !!(bmsr & BMSR_100TX_FD), (unsigned) !!(gbsr & GBSR_LP_1000FULL));
    printf("  GBCR=%04X (ms_manual=%u ms=%s adv1000full=%u)  GBSR=%04X (ms_resolved=%s local_rx_ok=%u lp1000full=%u)\r\n",
           gbcr, (unsigned) !!(gbcr & GBCR_MS_MANUAL),
           (gbcr & GBCR_MS_VALUE) ? "MASTER" : "SLAVE",
           (unsigned) !!(gbcr & GBCR_ADVERTISE_1000FULL),
           gbsr, (gbsr & GBSR_MS_RESOLVED) ? "MASTER" : "SLAVE",
           (unsigned) !!(gbsr & GBSR_LOCAL_RX_OK), (unsigned) !!(gbsr & GBSR_LP_1000FULL));
    printf("  PHYSTS(0x11)=%04X  mode=%s\r\n", sr, speed_str(bmcr, bmsr));
}

/* ---------------- shell 命令 ---------------- */
static void cmd_scan(int argc, char *argv[])
{
    (void) argc; (void) argv;
    do_scan();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 scan, cmd_scan, scan MDIO bus addr 0-31);

static void cmd_rd(int argc, char *argv[])
{
    uint32_t pa = 1, reg = 0, cnt = 1;
    if (argc < 3) {
        printf("usage: rd <phy_addr> <reg> [count]\r\n");
        return;
    }
    bsp_parse_u32(argv[1], &pa);
    bsp_parse_u32(argv[2], &reg);
    if (argc >= 4) {
        bsp_parse_u32(argv[3], &cnt);
    }
    if (pa > 31U || reg > 31U) {
        printf("bad arg: phy_addr 0-31, reg 0-31\r\n");
        return;
    }
    if (cnt == 0U || cnt > 32U) {
        cnt = 1;
    }
    for (uint32_t i = 0; i < cnt && (reg + i) <= 31U; i++) {
        uint16_t v = 0;
        if (mdio_read((uint8_t) pa, (uint8_t) (reg + i), &v)) {
            printf("PHY%u REG[0x%02X] = 0x%04X (%u)\r\n", (unsigned) pa,
                   (unsigned) (reg + i), v, v);
        } else {
            printf("PHY%u REG[0x%02X] read timeout\r\n", (unsigned) pa, (unsigned) (reg + i));
        }
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 rd, cmd_rd, read PHY reg: rd pa reg [count]);

static void cmd_wr(int argc, char *argv[])
{
    uint32_t pa = 1, reg = 0, val = 0;
    if (argc < 4) {
        printf("usage: wr <phy_addr> <reg> <value>\r\n");
        return;
    }
    bsp_parse_u32(argv[1], &pa);
    bsp_parse_u32(argv[2], &reg);
    bsp_parse_u32(argv[3], &val);
    if (pa > 31U || reg > 31U || val > 0xFFFFU) {
        printf("bad arg\r\n");
        return;
    }
    if (mdio_write((uint8_t) pa, (uint8_t) reg, (uint16_t) val)) {
        uint16_t rb = 0;
        (void) mdio_read((uint8_t) pa, (uint8_t) reg, &rb);
        printf("PHY%u REG[0x%02X] <- 0x%04X (readback 0x%04X)\r\n",
               (unsigned) pa, (unsigned) reg, (unsigned) val, rb);
    } else {
        printf("write timeout\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 wr, cmd_wr, write PHY reg: wr pa reg value);

static void cmd_phy(int argc, char *argv[])
{
    uint32_t pa = BOARD_NET2_PHY_ADDR;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &pa);
    }
    show_phy((uint8_t) pa);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 phy, cmd_phy, show PHY status: phy [addr]);

static void cmd_rst(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("[mdio] hardware reset YT8011AN via PB29 ...\r\n");
    net2_yt8011_hw_reset();
    printf("[mdio] reset done\r\n");
    show_phy(BOARD_NET2_PHY_ADDR);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 rst, cmd_rst, hardware reset PHY PB29 then show status);

static void cmd_link(int argc, char *argv[])
{
    uint32_t pa = BOARD_NET2_PHY_ADDR;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &pa);
    }
    printf("[mdio] poll BMSR.LINK of PHY@%u, 20 x 100ms ...\r\n", (unsigned) pa);
    for (uint32_t i = 0; i < 20U; i++) {
        uint16_t bmsr = mdio_read_stable((uint8_t) pa, MII_BMSR);
        uint16_t gbsr = 0, sr = 0;
        (void) mdio_read((uint8_t) pa, MII_GBSR, &gbsr);
        (void) mdio_read((uint8_t) pa, MII_PHYSTS, &sr);
        printf("  [%2u] BMSR=%04X link=%u an_done=%u  GBSR=%04X PHYSTS=%04X\r\n",
               (unsigned) i, bmsr, (unsigned) !!(bmsr & BMSR_LINK_STATUS),
               (unsigned) !!(bmsr & BMSR_ANEGCOMPLETE), gbsr, sr);
        if (bmsr & BMSR_LINK_STATUS) {
            printf("[mdio] LINK UP\r\n");
            break;
        }
        board_delay_ms(100);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 link, cmd_link, poll link status: link [addr]);

int main(void)
{
    bsp_init();
    printf("\r\n=== enet_mdio_scan: YT8011AN MDIO(Clause 22) scan ===\r\n");
    printf("ENET0 RGMII: TXD PF03-PF06/TXEN PF07/TXCK PF02, RXD PF09-PF12/RXDV PF08/RXCK PF13\r\n");
    printf("MDC=PA31 MDIO=PA30, nRST=PB29 nINT=PB28, PHYADDR=%u\r\n",
           (unsigned) BOARD_NET2_PHY_ADDR);

    net2_enet_board_init();
    printf("[mdio] PHY released from reset (PB29 high), eth0 clock on\r\n");

    do_scan();
    show_phy(BOARD_NET2_PHY_ADDR);
    printf("[mdio] commands: scan | rd <pa> <reg> [cnt] | wr <pa> <reg> <val> | phy [pa] | rst | link [pa]\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
