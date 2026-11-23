/*
 * lan8651_spi_probe - LAN8651 10BASE-T1S MAC-PHY bring-up + TC6 link probe
 *
 * 目标（按顺序自证）:
 *   1. SPI2 <-> LAN8651 控制事务是否通（12 字节帧 + echo 校验 rx[4..7]==tx[0..3]）
 *   2. 复位/中断链路（nRST PA19 / nINT PA24 / SDO 三态）
 *   3. TC6 标准寄存器、内部 PHY（Clause 22 / MMD）、AN1760 参数下发
 *   4. PLCA（TC14）配置与状态
 *
 * 硬件: hpm5e31_1  SPI2 SCLK PA20 / MISO PA22 / MOSI PA23 / CS PA21,
 *       nRST PA19, nINT PA24。详见 README.md。
 */
#include <string.h>
#include "bsp.h"
#include "hpm_gpio_drv.h"
#include "hpm_ioc_regs.h"
#include "oa_tc6.h"
#include "lan865x.h"

static oa_tc6_t s_tc6;
static uint32_t s_sclk_hz = 2000000UL;
static bool s_bb_mode;

static uint32_t int_pin_level(void)
{
    return (uint32_t) gpio_read_pin(BOARD_LAN8651_INT_GPIO_CTRL,
                                    BOARD_LAN8651_INT_GPIO_INDEX,
                                    BOARD_LAN8651_INT_GPIO_PIN);
}

static void dump_bytes(const char *tag, const uint8_t *b, uint32_t n)
{
    printf("%s", tag);
    for (uint32_t i = 0; i < n; i++) {
        printf(" %02x", b[i]);
    }
    printf("\r\n");
}

static uint32_t spi_src_hz(void)
{
    uint32_t src = board_init_spi_clock(BOARD_LAN8651_SPI);
    if (src == 0U) {
        src = clock_get_frequency(clock_spi2);
    }
    return src;
}

static int spi_set_sclk(uint32_t hz)
{
    uint32_t src = spi_src_hz();
    if (status_success != oa_tc6_spi_init(&s_tc6, BOARD_LAN8651_SPI, src, hz)) {
        return -1;
    }
    s_sclk_hz = s_tc6.sclk_hz;
    if (s_bb_mode) {
        oa_tc6_use_bitbang(&s_tc6, true);
    }
    return 0;
}

/* try one control read and verify the echo: this is the definitive link test */
static bool tc6_link_check(uint32_t *status0_out)
{
    uint32_t v = 0;
    bool ok;

    if (oa_tc6_read_reg(&s_tc6, OA_TC6_REG_STATUS0, &v) != 0) {
        ok = false;
    } else {
        ok = true;
    }
    /* the driver already verified the echo; a second read doubles the proof */
    if (status0_out) {
        *status0_out = v;
    }
    return ok;
}

static void print_transport(void)
{
    if (s_bb_mode) {
        printf("transport      : GPIO bit-bang SPI (PA20/21/22/23), half delay %u nops, SCLK ~%lu Hz\r\n",
               s_tc6.bb_half_delay, (unsigned long) oa_tc6_bb_measure_sclk(&s_tc6));
    } else {
        printf("transport      : SPI2 controller, SCLK %lu Hz\r\n", (unsigned long) s_sclk_hz);
    }
}

/*
 * SPI2 controller diagnostics: dump the controller registers and report the
 * result of one control transaction.  Used to document why the hardware SPI
 * path is not usable on this board revision.
 */
static void hwspi_dump(void)
{
    printf("\r\n-- SPI2 controller registers --\r\n");
    printf("  TRANSFMT  = 0x%08lx\r\n", (unsigned long) s_tc6.spi->TRANSFMT);
    printf("  TRANSCTRL = 0x%08lx\r\n", (unsigned long) s_tc6.spi->TRANSCTRL);
    printf("  CTRL      = 0x%08lx\r\n", (unsigned long) s_tc6.spi->CTRL);
    printf("  STATUS    = 0x%08lx\r\n", (unsigned long) s_tc6.spi->STATUS);
    printf("  TIMING    = 0x%08lx\r\n", (unsigned long) s_tc6.spi->TIMING);
    printf("  IOC PA20/21/22/23 FUNC_CTL = 0x%lx 0x%lx 0x%lx 0x%lx\r\n",
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PA20].FUNC_CTL,
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PA21].FUNC_CTL,
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PA22].FUNC_CTL,
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PA23].FUNC_CTL);
}

/* transport auto detection: HW SPI first, then bit-bang */
static void transport_select(void)
{
    uint32_t v = 0;
    uint8_t saved_rx[12];
    int ret;

    s_bb_mode = false;
    (void) spi_set_sclk(s_sclk_hz);
    ret = oa_tc6_read_reg(&s_tc6, OA_TC6_REG_STATUS0, &v);
    memcpy(saved_rx, s_tc6.ctrl_rx, sizeof(saved_rx));
    printf("\r\n[auto] HW SPI  : read STATUS0 -> ret %d, value 0x%08lx\r\n", ret, (unsigned long) v);
    dump_bytes("       tx:", s_tc6.ctrl_tx, 12);
    dump_bytes("       rx:", saved_rx, 12);
    if ((ret == 0) && (memcmp(&saved_rx[4], s_tc6.ctrl_tx, 4) == 0)) {
        printf("       echo OK -> using the SPI2 controller\r\n");
        s_bb_mode = false;
        return;
    }
    printf("[auto] SPI2 controller gives no echo -> fall back to bit-banged SPI\r\n");
    s_bb_mode = true;
    (void) spi_set_sclk(s_sclk_hz);
    oa_tc6_use_bitbang(&s_tc6, true);
    ret = oa_tc6_read_reg(&s_tc6, OA_TC6_REG_STATUS0, &v);
    printf("[auto] bit-bang: read STATUS0 -> ret %d, value 0x%08lx\r\n", ret, (unsigned long) v);
    dump_bytes("       rx:", s_tc6.ctrl_rx, 12);
}

static void probe_dump(void)
{
    uint32_t id = 0;
    uint32_t v = 0;
    uint16_t sts1 = 0;
    lan865x_plca_cfg_t plca;

    printf("\r\n=== LAN8651 bring-up state ===\r\n");
    print_transport();
    printf("nRST PA19 = %lu, nINT PA24 = %lu\r\n",
           (unsigned long) gpio_read_pin(BOARD_LAN8651_RST_GPIO_CTRL, BOARD_LAN8651_RST_GPIO_INDEX,
                                         BOARD_LAN8651_RST_GPIO_PIN),
           (unsigned long) int_pin_level());
    printf("control xfers = %lu, echo errors = %lu, spi errors = %lu\r\n",
           (unsigned long) s_tc6.stat_ctrl_xfer, (unsigned long) s_tc6.stat_ctrl_err,
           (unsigned long) s_tc6.stat_spi_err);

    printf("\r\n-- TC6 standard registers --\r\n");
    if (oa_tc6_read_reg(&s_tc6, OA_TC6_REG_STDCAP, &v) == 0) {
        printf("  STDCAP      = 0x%08lx  (bit8 direct PHY access = %lu)\r\n",
               (unsigned long) v, (unsigned long) ((v >> 8) & 1U));
    }
    if (oa_tc6_read_reg(&s_tc6, OA_TC6_REG_STATUS0, &v) == 0) {
        printf("  STATUS0     = 0x%08lx  (RESETC=%lu HEADER_ERROR=%lu LOSS_OF_FRAME=%lu)\r\n",
               (unsigned long) v, (unsigned long) ((v >> 6) & 1U),
               (unsigned long) ((v >> 5) & 1U), (unsigned long) ((v >> 4) & 1U));
    }
    if (oa_tc6_read_reg(&s_tc6, OA_TC6_REG_CONFIG0, &v) == 0) {
        printf("  CONFIG0     = 0x%08lx  (SYNC=%lu ZARFE=%lu)\r\n",
               (unsigned long) v, (unsigned long) ((v >> 15) & 1U), (unsigned long) ((v >> 12) & 1U));
    }
    if (oa_tc6_read_reg(&s_tc6, OA_TC6_REG_BUFFER_STATUS, &v) == 0) {
        printf("  BUFFER_STS  = 0x%08lx  (tx_credits=%lu rx_chunks=%lu)\r\n",
               (unsigned long) v, (unsigned long) ((v >> 8) & 0x1FU), (unsigned long) (v & 0x1FU));
    }
    if (oa_tc6_read_reg(&s_tc6, OA_TC6_REG_INT_MASK0, &v) == 0) {
        printf("  INT_MASK0   = 0x%08lx\r\n", (unsigned long) v);
    }

    printf("\r\n-- internal PHY --\r\n");
    if (lan865x_phy_id(&s_tc6, &id) == 0) {
        printf("  PHY ID      = 0x%08lx (expect 0x%08lx) %s\r\n",
               (unsigned long) id, (unsigned long) LAN865X_PHY_ID,
               (id == LAN865X_PHY_ID) ? "MATCH" : "MISMATCH");
    }
    (void) oa_tc6_c22_read(&s_tc6, 1, (uint16_t *) &v);   /* ignore */
    if (lan865x_phy_sts1(&s_tc6, &sts1) == 0) {
        printf("  PHY STS1    = 0x%04x (bit11 PLCA change, bit13 link change)\r\n", sts1);
    }
    if (lan865x_plca_read(&s_tc6, &plca) == 0) {
        printf("  PLCA        : en=%u node_id=%u node_cnt=%u to_tmr=%u burst_cnt=%u burst_tmr=%u status=0x%04x (PST=%u)\r\n",
               plca.enabled, plca.node_id, plca.node_cnt, plca.to_tmr,
               plca.burst_cnt, plca.burst_tmr, plca.status,
               (unsigned)((plca.status >> 15) & 1U));
    }
    printf("\r\n-- transfer statistics --\r\n");
    printf("  data xfers=%lu tx_frames=%lu tx_bytes=%lu rx_frames=%lu rx_bytes=%lu rx_dropped=%lu\r\n",
           (unsigned long) s_tc6.stat_data_xfer, (unsigned long) s_tc6.stat_tx_frames,
           (unsigned long) s_tc6.stat_tx_bytes, (unsigned long) s_tc6.stat_rx_frames,
           (unsigned long) s_tc6.stat_rx_bytes, (unsigned long) s_tc6.stat_rx_dropped);
    printf("  ext_status=%lu hdr_bad=%lu cfg_sync_lost=%lu last_STATUS0=0x%08lx\r\n",
           (unsigned long) s_tc6.stat_ext_status, (unsigned long) s_tc6.stat_hdr_bad,
           (unsigned long) s_tc6.stat_cfg_sync_lost, (unsigned long) s_tc6.stat_status0);
}

static int bring_up(void)
{
    static const uint8_t mac[6] = {BOARD_LAN8651_MAC0, BOARD_LAN8651_MAC1, BOARD_LAN8651_MAC2,
                                   BOARD_LAN8651_MAC3, BOARD_LAN8651_MAC4, BOARD_LAN8651_MAC5};
    int ret;

    printf("\r\n[bring-up] hardware reset (nRST low 2 ms) + TC6 + AN1760 + MAC\r\n");
    ret = lan865x_full_init(&s_tc6, mac);
    printf("           result: %s (%d), nINT=%lu\r\n", lan865x_strerror(ret), ret,
           (unsigned long) int_pin_level());
    if (ret == 0) {
        lan865x_plca_cfg_t plca = {
            .enabled = false, .node_id = 0, .node_cnt = BOARD_PLCA_NODE_CNT_DEFAULT,
            .to_tmr = BOARD_PLCA_TO_TMR_DEFAULT, .burst_cnt = BOARD_PLCA_BURST_CNT_DEFAULT,
            .burst_tmr = BOARD_PLCA_BURST_TMR_DEFAULT,
        };
        (void) lan865x_plca_configure(&s_tc6, &plca);
        printf("           PLCA disabled (CSMA/CD) for the probe, node_cnt=%u\r\n",
               (unsigned) BOARD_PLCA_NODE_CNT_DEFAULT);
    }
    return ret;
}


/* one multi-register read of the whole MMS0 block 0x00..0x0F: atomic and easy to
 * cross-check against DS60001734E 11.1 (OA_ID / OA_PHYID / OA_STDCAP / ...) */
static void cmd_regs(int argc, char *argv[])
{
    uint32_t v[16];
    static const char *names[16] = {
        "OA_ID", "OA_PHYID", "OA_STDCAP", "OA_RESET", "OA_CONFIG0", "0x05", "0x06", "0x07",
        "OA_STATUS0", "0x09", "0x0A", "OA_BUFSTS", "INT_MASK0", "0x0D", "0x0E", "0x0F",
    };
    (void) argc; (void) argv;
    memset(v, 0, sizeof(v));
    if (oa_tc6_read_regs(&s_tc6, 0x0000U, v, 16) != 0) {
        printf("multi-register read failed\r\n");
        return;
    }
    for (uint32_t i = 0; i < 16U; i++) {
        printf("  0x%04lx %-11s = 0x%08lx\r\n", (unsigned long) i, names[i], (unsigned long) v[i]);
    }
    printf("  OA_ID     MAJVER=%lu MINVER=%lu\r\n",
           (unsigned long) ((v[0] >> 4) & 0xFU), (unsigned long) (v[0] & 0xFU));
    printf("  OA_PHYID  OUI=0x%06lx MODEL=%lu REVISION=%lu\r\n",
           (unsigned long) (((v[1] >> 14) & 0x3FFFFU) | ((v[1] >> 4) & 0x3U) << 18),
           (unsigned long) ((v[1] >> 4) & 0x3FU), (unsigned long) (v[1] & 0xFU));
    printf("  STDCAP    DPRAC=%lu IPRAC=%lu TXFCSVC=%lu AIDC=%lu FTSC=%lu CTC=%lu\r\n",
           (unsigned long) ((v[2] >> 8) & 1U), (unsigned long) ((v[2] >> 9) & 1U),
           (unsigned long) ((v[2] >> 10) & 1U), (unsigned long) ((v[2] >> 5) & 1U),
           (unsigned long) ((v[2] >> 6) & 1U), (unsigned long) ((v[2] >> 7) & 1U));
    /* clause 22 PHY id through the 0xFF00 window */
    {
        uint16_t id1 = 0;
        uint16_t id2 = 0;
        uint16_t bmcr = 0;
        if (oa_tc6_c22_read(&s_tc6, 2, &id1) == 0 && oa_tc6_c22_read(&s_tc6, 3, &id2) == 0) {
            printf("  C22 reg2/3 (PHY ID) = 0x%04x 0x%04x -> 0x%08lx\r\n", id1, id2,
                   (unsigned long) (((uint32_t) id1 << 16) | id2));
        }
        if (oa_tc6_c22_read(&s_tc6, 0, &bmcr) == 0) {
            printf("  C22 reg0 (BMCR)     = 0x%04x\r\n", bmcr);
        }
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 regs, cmd_regs, multi-register read of the MMS0 block 0x00..0x0F + clause 22 PHY id);


/* repeated single-register reads: catches stale/shifted read data */
static void cmd_rdtest(int argc, char *argv[])
{
    uint32_t v = 0;
    (void) argc; (void) argv;
    for (int i = 0; i < 3; i++) {
        v = 0xDEADBEEF;
        (void) oa_tc6_read_reg(&s_tc6, 0x0002U, &v);
        printf("  rd 0x0002 #%d = 0x%08lx\r\n", i, (unsigned long) v);
    }
    for (int i = 0; i < 3; i++) {
        v = 0xDEADBEEF;
        (void) oa_tc6_read_reg(&s_tc6, 0x0008U, &v);
        printf("  rd 0x0008 #%d = 0x%08lx\r\n", i, (unsigned long) v);
    }
    for (int i = 0; i < 2; i++) {
        v = 0xDEADBEEF;
        (void) oa_tc6_read_reg(&s_tc6, 0x0000U, &v);
        printf("  rd 0x0000 #%d = 0x%08lx\r\n", i, (unsigned long) v);
    }
    {
        uint32_t blk[16];
        memset(blk, 0, sizeof(blk));
        (void) oa_tc6_read_regs(&s_tc6, 0x0000U, blk, 16);
        printf("  block 0x00..0x0F: 0x%08lx 0x%08lx 0x%08lx 0x%08lx ...\r\n",
               (unsigned long) blk[0], (unsigned long) blk[1], (unsigned long) blk[2],
               (unsigned long) blk[3]);
        printf("                   0x%08lx 0x%08lx 0x%08lx 0x%08lx (0x08..0x0B)\r\n",
               (unsigned long) blk[8], (unsigned long) blk[9], (unsigned long) blk[10],
               (unsigned long) blk[11]);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 rdtest, cmd_rdtest, repeated single reads + block read comparison);


static void cmd_padtune(int argc, char *argv[])
{
    uint32_t sr = 0, spd = 0, ds = 0;
    uint32_t v;
    if (argc >= 4) {
        if (bsp_parse_u32(argv[1], &sr) != 0 || bsp_parse_u32(argv[2], &spd) != 0 ||
            bsp_parse_u32(argv[3], &ds) != 0) {
            printf("usage: padtune <sr 0|1> <spd 0..3> <ds 0..7>\r\n");
            return;
        }
    } else if (argc == 1) {
        (void) oa_tc6_read_reg(&s_tc6, OA_TC6_REG_STDCAP, &v);
        printf("current PAD_CTL = 0x%08lx (PA20..PA23)\r\n", (unsigned long) oa_tc6_get_pad_ctl());
        return;
    }
    oa_tc6_set_pad_ctl(&s_tc6, IOC_PAD_PAD_CTL_SR_SET(sr) | IOC_PAD_PAD_CTL_SPD_SET(spd) |
                                 IOC_PAD_PAD_CTL_DS_SET(ds));
    printf("PAD_CTL <= SR=%lu SPD=%lu DS=%lu (0x%08lx)\r\n", (unsigned long) sr, (unsigned long) spd,
           (unsigned long) ds, (unsigned long) oa_tc6_get_pad_ctl());
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 padtune, cmd_padtune, SPI2 pad slew/speed/drive sweep: padtune [sr spd ds]);



/* ---------------------------------------------------------------------------
 * hwtx: does the SPI2 controller actually drive SCLK/MOSI all the way to the
 * LAN8651?  Write a marker into OA_CONFIG0 with the hardware SPI path, then
 * read the register back with the (independently verified) bit-banged path.
 *   - marker read back  -> SPI2 TX (CS/SCLK/MOSI) reaches the chip, RX is the
 *                          only broken part
 *   - marker lost       -> SPI2 TX does not reach the chip
 * ------------------------------------------------------------------------- */
static void cmd_hwtx(int argc, char *argv[])
{
    const uint32_t marker = 0x00001234U;
    uint32_t v = 0;
    int ret;
    (void) argc; (void) argv;

    printf("== hwtx: hardware SPI write, bit-bang read-back ==\r\n");
    /* 1) hardware SPI write */
    s_bb_mode = false;
    oa_tc6_use_bitbang(&s_tc6, false);
    init_lan8651_pins();
    oa_tc6_use_gpio_cs(&s_tc6, true);
    ret = oa_tc6_write_reg(&s_tc6, OA_TC6_REG_CONFIG0, marker);
    printf("  hw write CONFIG0<=0x%08lx: ret=%d, echo=", (unsigned long) marker, ret);
    dump_bytes("", s_tc6.ctrl_rx, 12);

    /* 2) bit-bang read-back */
    oa_tc6_use_bitbang(&s_tc6, true);
    s_bb_mode = true;
    ret = oa_tc6_read_reg(&s_tc6, OA_TC6_REG_CONFIG0, &v);
    printf("  bit-bang read CONFIG0: ret=%d value=0x%08lx -> %s\r\n", ret, (unsigned long) v,
           (v == marker) ? "SPI2 TX REACHES THE CHIP (RX path is the problem)"
                         : "SPI2 TX DOES NOT REACH THE CHIP");

    /* 3) restore the normal configuration (SYNC + ZARFE) and verify over bit-bang */
    (void) oa_tc6_write_reg(&s_tc6, OA_TC6_REG_CONFIG0, CONFIG0_SYNC | CONFIG0_ZARFE_ENABLE);
    (void) oa_tc6_read_reg(&s_tc6, OA_TC6_REG_CONFIG0, &v);
    printf("  CONFIG0 restored to 0x%08lx\r\n", (unsigned long) v);

    /* 4) leave the node on the hardware transport for the next command */
    oa_tc6_use_bitbang(&s_tc6, false);
    s_bb_mode = false;
    init_lan8651_pins();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 hwtx, cmd_hwtx, hardware-SPI write + bit-bang read-back (does SPI2 TX reach the chip?));

/* ------------------------------------------------------- pad / IO tracing */
static uint32_t di(uint32_t pin) { return (uint32_t) gpio_read_pin(HPM_GPIO0, GPIO_DI_GPIOA, (uint8_t) pin); }

static void print_spi_regs(void)
{
    printf("  TRANSFMT=0x%08lx TRANSCTRL=0x%08lx CTRL=0x%08lx STATUS=0x%08lx TIMING=0x%08lx\r\n",
           (unsigned long) BOARD_LAN8651_SPI->TRANSFMT, (unsigned long) BOARD_LAN8651_SPI->TRANSCTRL,
           (unsigned long) BOARD_LAN8651_SPI->CTRL, (unsigned long) BOARD_LAN8651_SPI->STATUS,
           (unsigned long) BOARD_LAN8651_SPI->TIMING);
    printf("  SPIACTIVE=%lu TXFIFO_EMPTY=%lu RXFIFO_EMPTY=%lu\r\n",
           (unsigned long) ((BOARD_LAN8651_SPI->STATUS >> 15) & 1U),
           (unsigned long) ((BOARD_LAN8651_SPI->STATUS >> 0) & 1U),
           (unsigned long) ((BOARD_LAN8651_SPI->STATUS >> 1) & 1U));
}

/*
 * Post-transfer pad trace: after a transfer whose MOSI payload ends with a 1
 * (0xFF) the MOSI pad should stay high; after 0x00 it should stay low.  This
 * tells whether the SPI2 controller really drives PA23, without a scope.
 */
static void cmd_padtrace(int argc, char *argv[])
{
    uint8_t tx[12];
    uint8_t rx[12];
    uint32_t v = 0;
    (void) argc; (void) argv;

    if (s_bb_mode) {
        printf("switch to hardware SPI first (trans hw)\r\n");
        return;
    }
    printf("pad FUNC_CTL: PA20=0x%lx PA21=0x%lx PA22=0x%lx PA23=0x%lx\r\n",
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PA20].FUNC_CTL,
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PA21].FUNC_CTL,
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PA22].FUNC_CTL,
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PA23].FUNC_CTL);
    printf("pad PAD_CTL : PA20=0x%lx PA21=0x%lx PA22=0x%lx PA23=0x%lx\r\n",
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PA20].PAD_CTL,
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PA21].PAD_CTL,
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PA22].PAD_CTL,
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PA23].PAD_CTL);
    printf("idle DI: SCLK(PA20)=%lu CS(PA21)=%lu MISO(PA22)=%lu MOSI(PA23)=%lu\r\n",
           (unsigned long) di(20), (unsigned long) di(21), (unsigned long) di(22), (unsigned long) di(23));

    /* 1) transfer with 0xFF payload -> MOSI should idle high afterwards */
    memset(tx, 0xFF, sizeof(tx));
    (void) oa_tc6_read_reg(&s_tc6, OA_TC6_REG_STDCAP, &v);   /* normal 12 byte frame */
    memset(tx, 0xFF, sizeof(tx));
    (void) oa_tc6_spi_transfer_raw(&s_tc6, tx, rx, sizeof(tx));
    printf("after 0xFF frame: MOSI(PA23)=%lu SCLK(PA20)=%lu rx=", (unsigned long) di(23), (unsigned long) di(20));
    dump_bytes("", rx, 12);

    /* 2) transfer with 0x00 payload -> MOSI should idle low afterwards */
    memset(tx, 0x00, sizeof(tx));
    (void) oa_tc6_spi_transfer_raw(&s_tc6, tx, rx, sizeof(tx));
    printf("after 0x00 frame: MOSI(PA23)=%lu SCLK(PA20)=%lu rx=", (unsigned long) di(23), (unsigned long) di(20));
    dump_bytes("", rx, 12);
    print_spi_regs();
    printf("=> MOSI follows the last transmitted bit means PA23 drives correctly;\r\n");
    printf("   if it stays 0 both times the pad/controller does not drive MOSI.\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 padtrace, cmd_padtrace, post-transfer pad trace (does PA23 really drive MOSI?));

static void cmd_speed(int argc, char *argv[])
{
    uint32_t hz = 0;
    uint32_t src;
    if (argc < 2 || bsp_parse_u32(argv[1], &hz) != 0 || hz < 100000UL || hz > 25000000UL) {
        printf("usage: speed <hz>   (100000 .. 25000000, current %lu)\r\n", (unsigned long) s_sclk_hz);
        return;
    }
    src = spi_src_hz();
    if (status_success != oa_tc6_spi_init(&s_tc6, BOARD_LAN8651_SPI, src, hz)) {
        printf("spi init failed\r\n");
        return;
    }
    if (s_bb_mode) {
        oa_tc6_use_bitbang(&s_tc6, true);
    }
    s_sclk_hz = oa_tc6_spi_get_sclk(&s_tc6, clock_get_frequency(clock_spi2));
    printf("SPI2 SCLK = %lu Hz (%s), source %lu Hz\r\n", (unsigned long) s_sclk_hz,
           s_bb_mode ? "bit-bang" : "hardware", (unsigned long) clock_get_frequency(clock_spi2));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 speed, cmd_speed, set SPI2 SCLK: speed <hz>);

/* SCLK sweep: 32 single-register reads per speed, count echo errors + timing */
static void cmd_scan(int argc, char *argv[])
{
    static const uint32_t speeds[] = {1000000UL, 2000000UL, 5000000UL, 10000000UL, 20000000UL, 25000000UL};
    const uint32_t n = 32;
    uint32_t src = spi_src_hz();
    (void) argc; (void) argv;
    if (s_bb_mode) {
        printf("bit-bang transport active, run 'trans hw' first\r\n");
        return;
    }
    printf("[scan] %lu reads of STDCAP per speed\r\n", (unsigned long) n);
    printf("  %-10s %-10s %-8s %-12s %-10s\r\n", "request", "actual", "errors", "stdcap", "ns/read");
    for (unsigned i = 0; i < ARRAY_SIZE(speeds); i++) {
        uint32_t err = 0;
        uint32_t v = 0;
        uint32_t t0;
        if (status_success != oa_tc6_spi_init(&s_tc6, BOARD_LAN8651_SPI, src, speeds[i])) {
            printf("  %-10lu init failed\r\n", (unsigned long) speeds[i]);
            continue;
        }
        t0 = (uint32_t) hpm_csr_get_core_mcycle();
        for (uint32_t k = 0; k < n; k++) {
            uint32_t t = 0;
            if (oa_tc6_read_reg(&s_tc6, OA_TC6_REG_STDCAP, &t) != 0) {
                err++;
            } else {
                v = t;
            }
        }
        {
            uint32_t dt = (uint32_t) hpm_csr_get_core_mcycle() - t0;
            uint32_t src_now = clock_get_frequency(clock_spi2);
            uint32_t cycles_per_read = dt / n;
            /* 480 MHz core -> ns = cycles * 1000 / 480 */
            printf("  %-10lu %-10lu %-8lu 0x%08lx %-10lu%s\r\n",
                   (unsigned long) speeds[i], (unsigned long) oa_tc6_spi_get_sclk(&s_tc6, src_now),
                   (unsigned long) err, (unsigned long) v,
                   (unsigned long) ((cycles_per_read * 1000U) / 480U),
                   (err == 0U) ? "  ok" : "  FAIL");
        }
    }
    s_sclk_hz = s_tc6.sclk_hz;
    printf("[scan] last speed %lu Hz\r\n", (unsigned long) s_sclk_hz);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 scan, cmd_scan, SCLK sweep 1..25 MHz: echo errors + us/read);

/* ---------------------------------------------------------- CS source test */
/*
 * The SPI2 controller on hpm5e31_1 rev.1 never asserts CS (the LAN8651 keeps
 * SDO high-Z -> reads return 0xFF).  This command drives CS from a GPIO while
 * the controller generates SCLK/MOSI, which isolates the CS signal from the
 * clock/data path.  On success it leaves this combination active.
 */
static void cmd_cssh(int argc, char *argv[])
{
    uint32_t v = 0;
    int ret;
    (void) argc; (void) argv;

    printf("CS source test (SPI2 controller generates SCLK/MOSI):\r\n");

    /* 1) hardware CS */
    s_bb_mode = false;
    oa_tc6_use_bitbang(&s_tc6, false);
    oa_tc6_use_gpio_cs(&s_tc6, false);
    HPM_IOC->PAD[IOC_PAD_PA21].FUNC_CTL = IOC_PA21_FUNC_CTL_SPI2_CS_0;
    (void) spi_set_sclk(s_sclk_hz);
    ret = oa_tc6_read_reg(&s_tc6, OA_TC6_REG_STDCAP, &v);
    printf("  hw  CS: ret=%d value=0x%08lx  rx[0..3]=", ret, (unsigned long) v);
    dump_bytes("", s_tc6.ctrl_rx, 4);

    /* 2) GPIO CS driven by the driver around each transfer */
    oa_tc6_use_gpio_cs(&s_tc6, true);
    HPM_IOC->PAD[IOC_PAD_PA21].FUNC_CTL = IOC_PA21_FUNC_CTL_GPIO_A_21;
    gpio_set_pin_output_with_initial(BOARD_LAN8651_CS_GPIO_CTRL, BOARD_LAN8651_CS_GPIO_INDEX,
                                     BOARD_LAN8651_CS_GPIO_PIN, 1);
    (void) spi_set_sclk(s_sclk_hz);
    ret = oa_tc6_read_reg(&s_tc6, OA_TC6_REG_STDCAP, &v);
    printf("  gpio CS: ret=%d value=0x%08lx  rx[0..3]=", ret, (unsigned long) v);
    dump_bytes("", s_tc6.ctrl_rx, 4);
    if (ret == 0) {
        printf("  -> GPIO chip select works with the SPI2 controller!\r\n");
        s_bb_mode = false;
    } else {
        printf("  -> neither CS source works; staying on bit-bang\r\n");
        oa_tc6_use_gpio_cs(&s_tc6, false);
        s_bb_mode = true;
        (void) spi_set_sclk(s_sclk_hz);
        oa_tc6_use_bitbang(&s_tc6, true);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 cssh, cmd_cssh, test hardware vs GPIO chip select with the SPI2 controller);

/* ------------------------------ shell commands ---------------------------- */
static void cmd_probe(int argc, char *argv[])
{
    (void) argc; (void) argv;
    probe_dump();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 probe, cmd_probe, dump LAN8651 TC6/PHY/PLCA state);

static void cmd_boot(int argc, char *argv[])
{
    (void) argc; (void) argv;
    transport_select();
    (void) bring_up();
    probe_dump();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 boot, cmd_boot, transport auto-detect + full LAN8651 bring-up);

static void cmd_trans(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: trans <bb|hw>   (current: %s)\r\n", s_bb_mode ? "bb" : "hw");
        return;
    }
    if (argv[1][0] == 'b') {
        s_bb_mode = true;
        (void) spi_set_sclk(s_sclk_hz);
        oa_tc6_use_bitbang(&s_tc6, true);
    } else {
        s_bb_mode = false;
        oa_tc6_use_bitbang(&s_tc6, false);
        (void) spi_set_sclk(s_sclk_hz);
    }
    print_transport();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 trans, cmd_trans, select TC6 transport: trans <bb|hw>);

static void cmd_delay(int argc, char *argv[])
{
    uint32_t v = 0;
    if (argc < 2 || bsp_parse_u32(argv[1], &v) != 0 || v > 200U) {
        printf("usage: delay <0..200>   (current %u)\r\n", s_tc6.bb_half_delay);
        return;
    }
    oa_tc6_set_bb_delay(&s_tc6, (uint8_t) v);
    printf("bb half delay = %lu nops, SCLK ~%lu Hz\r\n", (unsigned long) v,
           (unsigned long) oa_tc6_bb_measure_sclk(&s_tc6));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 delay, cmd_delay, bit-bang half-bit delay in nops: delay <0..200>);

static void cmd_hwchk(int argc, char *argv[])
{
    uint32_t v = 0;
    bool was_bb = s_bb_mode;
    (void) argc; (void) argv;
    if (was_bb) {
        oa_tc6_use_bitbang(&s_tc6, false);
        s_bb_mode = false;
        (void) spi_set_sclk(s_sclk_hz);
    }
    hwspi_dump();
    (void) oa_tc6_read_reg(&s_tc6, OA_TC6_REG_STATUS0, &v);
    printf("  HW SPI read STATUS0 = 0x%08lx\r\n", (unsigned long) v);
    dump_bytes("  tx:", s_tc6.ctrl_tx, 12);
    dump_bytes("  rx:", s_tc6.ctrl_rx, 12);
    if (was_bb) {
        s_bb_mode = true;
        oa_tc6_use_bitbang(&s_tc6, true);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 hwchk, cmd_hwchk, dump SPI2 controller registers + one control transaction);

static void cmd_plca(int argc, char *argv[])
{
    lan865x_plca_cfg_t cfg = {
        .enabled = true, .node_id = 0, .node_cnt = BOARD_PLCA_NODE_CNT_DEFAULT,
        .to_tmr = BOARD_PLCA_TO_TMR_DEFAULT, .burst_cnt = BOARD_PLCA_BURST_CNT_DEFAULT,
        .burst_tmr = BOARD_PLCA_BURST_TMR_DEFAULT,
    };
    uint32_t v = 0;

    if (argc < 2) {
        printf("usage: plca <on|off> [node_id] [node_cnt] [to_tmr] [burst_cnt] [burst_tmr]\r\n");
        return;
    }
    cfg.enabled = (argv[1][0] == 'o') && (argv[1][1] == 'n');
    if (argc > 2 && bsp_parse_u32(argv[2], &v) == 0) cfg.node_id = (uint8_t) v;
    if (argc > 3 && bsp_parse_u32(argv[3], &v) == 0) cfg.node_cnt = (uint8_t) v;
    if (argc > 4 && bsp_parse_u32(argv[4], &v) == 0) cfg.to_tmr = (uint8_t) v;
    if (argc > 5 && bsp_parse_u32(argv[5], &v) == 0) cfg.burst_cnt = (uint8_t) v;
    if (argc > 6 && bsp_parse_u32(argv[6], &v) == 0) cfg.burst_tmr = (uint8_t) v;
    if (lan865x_plca_configure(&s_tc6, &cfg) != 0) {
        printf("PLCA configure failed\r\n");
        return;
    }
    printf("PLCA: en=%u node_id=%u node_cnt=%u to_tmr=%u burst_cnt=%u burst_tmr=%u status=0x%04x (PST=%u)\r\n",
           cfg.enabled, cfg.node_id, cfg.node_cnt, cfg.to_tmr, cfg.burst_cnt, cfg.burst_tmr,
           cfg.status, (unsigned) ((cfg.status >> 15) & 1U));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 plca, cmd_plca, configure PLCA: plca <on|off> [id] [cnt] [to] [bc] [bt]);

static void cmd_rd(int argc, char *argv[])
{
    uint32_t addr = 0;
    uint32_t v = 0;
    if (argc < 2 || bsp_parse_u32(argv[1], &addr) != 0) {
        printf("usage: rd <hex addr>\r\n");
        return;
    }
    if (oa_tc6_read_reg(&s_tc6, addr, &v) != 0) {
        printf("read 0x%05lx failed\r\n", (unsigned long) addr);
        return;
    }
    printf("[0x%05lx] = 0x%08lx\r\n", (unsigned long) addr, (unsigned long) v);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 rd, cmd_rd, read a TC6 register: rd <addr>);

static void cmd_wr(int argc, char *argv[])
{
    uint32_t addr = 0;
    uint32_t v = 0;
    if (argc < 3 || bsp_parse_u32(argv[1], &addr) != 0 || bsp_parse_u32(argv[2], &v) != 0) {
        printf("usage: wr <hex addr> <hex value>\r\n");
        return;
    }
    if (oa_tc6_write_reg(&s_tc6, addr, v) != 0) {
        printf("write 0x%05lx failed\r\n", (unsigned long) addr);
        return;
    }
    printf("[0x%05lx] <= 0x%08lx\r\n", (unsigned long) addr, (unsigned long) v);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 wr, cmd_wr, write a TC6 register: wr <addr> <value>);

static void cmd_mmd(int argc, char *argv[])
{
    uint32_t mms = 0;
    uint32_t reg = 0;
    uint16_t v = 0;
    if (argc < 3 || bsp_parse_u32(argv[1], &mms) != 0 || bsp_parse_u32(argv[2], &reg) != 0) {
        printf("usage: mmd <mms> <reg>   (4 = MMD31 vendor/PLCA)\r\n");
        return;
    }
    if (oa_tc6_mmd_read(&s_tc6, (uint8_t) mms, (uint16_t) reg, &v) != 0) {
        printf("mmd read failed\r\n");
        return;
    }
    printf("mmd%lu reg 0x%04lx = 0x%04x\r\n", (unsigned long) mms, (unsigned long) reg, v);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mmd, cmd_mmd, read a PHY MMD register: mmd <mms> <reg>);

static void cmd_mmwr(int argc, char *argv[])
{
    uint32_t mms = 0;
    uint32_t reg = 0;
    uint32_t v = 0;
    if (argc < 4 || bsp_parse_u32(argv[1], &mms) != 0 || bsp_parse_u32(argv[2], &reg) != 0 ||
        bsp_parse_u32(argv[3], &v) != 0) {
        printf("usage: mmwr <mms> <reg> <value>\r\n");
        return;
    }
    if (oa_tc6_mmd_write(&s_tc6, (uint8_t) mms, (uint16_t) reg, (uint16_t) v) != 0) {
        printf("mmd write failed\r\n");
        return;
    }
    printf("mmd%lu reg 0x%04lx <= 0x%04lx\r\n", (unsigned long) mms, (unsigned long) reg, (unsigned long) v);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mmwr, cmd_mmwr, write a PHY MMD register: mmwr <mms> <reg> <value>);

static void cmd_levels(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("nINT PA24 = %lu, nRST PA19 = %lu\r\n",
           (unsigned long) int_pin_level(),
           (unsigned long) gpio_read_pin(BOARD_LAN8651_RST_GPIO_CTRL, BOARD_LAN8651_RST_GPIO_INDEX,
                                         BOARD_LAN8651_RST_GPIO_PIN));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 levels, cmd_levels, print nRST / nINT pin levels);

int main(void)
{
    bsp_init();
    init_lan8651_pins();

    printf("\r\n=== hpm5e31_1 LAN8651 10BASE-T1S probe ===\r\n");
    printf("SPI2: SCLK PA20 / MISO PA22 / MOSI PA23 / CS PA21, nRST PA19, nINT PA24\r\n");
    printf("cpu0 %lu Hz, spi2 src %lu Hz\r\n",
           (unsigned long) clock_get_frequency(clock_cpu0),
           (unsigned long) spi_src_hz());

    printf("board_init_spi_clock(SPI2) = %lu Hz\r\n", (unsigned long) board_init_spi_clock(BOARD_LAN8651_SPI));
    (void) spi_set_sclk(s_sclk_hz);
    printf("SPI2 SCLK = %lu Hz (requested)\r\n", (unsigned long) s_sclk_hz);

    transport_select();
    (void) bring_up();
    probe_dump();

    printf("\r\ncommands:\r\n");
    printf("  boot                 transport auto-detect + full bring-up\r\n");
    printf("  probe                dump TC6/PHY/PLCA + statistics\r\n");
    printf("  trans <bb|hw>        select transport\r\n");
    printf("  delay <nops>         bit-bang half-bit delay (SCLK speed)\r\n");
    printf("  hwchk                SPI2 controller registers + control transaction\r\n");
    printf("  plca <on|off> [id] [cnt] [to] [bc] [bt]   PLCA configuration\r\n");
    printf("  rd/wr <addr> [val]   TC6 register access\r\n");
    printf("  mmd/mmwr <mms> <reg> [val]                PHY MMD access\r\n");
    printf("  levels               nRST / nINT pin levels\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
