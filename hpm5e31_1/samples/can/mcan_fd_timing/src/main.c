/*
 * mcan_fd_timing - CAN FD 位时序矩阵：仲裁段 500k/1M x 数据段 2M/4M/5M/8M x 采样点
 *
 * 硬件: hpm5e31_1，CAN0(PD00/PD01) -> CAN1(PD05/PD04) 同一段真实总线上单向发 64B FD 帧，
 *       CAN1 负责 ACK 与接收。终端电阻板上已有（两端各 120 欧）。
 * 目的: 给出 HPM5E31 MCAN 在 80MHz CAN 时钟下的可用位时序/采样点组合与实测丢帧率，
 *       并验证 TDC（发送延迟补偿）在高速数据段的必要性。
 * 命令: info / all [n] / one <idx> [n] / tdc [n]
 */
#include "bsp.h"
#include "hpm_mcan_drv.h"
#include "ext/board_can.h"

#define NODE_TX BOARD_CAN0_BASE
#define NODE_RX BOARD_CAN1_BASE

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_buf_a[MCAN_MSG_BUF_SIZE_IN_WORDS];
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_buf_b[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static uint32_t s_cpu_mhz;

static uint64_t cyc_now(void)
{
    return (uint64_t) hpm_csr_get_core_mcycle();
}

static uint32_t cyc_to_us(uint64_t c)
{
    return (uint32_t) (c / (uint64_t) s_cpu_mhz);
}

typedef struct {
    const char *name;
    uint16_t arb_kbps;
    uint16_t data_kbps;
    uint16_t p_arb;
    uint16_t s1_arb;
    uint16_t s2_arb;
    uint16_t p_dat;
    uint16_t s1_dat;
    uint16_t s2_dat;
    uint8_t tdc;       /* 1 = 开 TDC */
} timing_case_t;

/* 80 MHz CAN 时钟。采样点 = (1+seg1)/(1+seg1+seg2)。
 * 仲裁段 500k: 160 TQ；1M: 80 TQ。
 * 数据段 2M: 40 TQ；4M: 20 TQ；5M: 16 TQ；8M: 10 TQ。
 * 注意 4M(20TQ)/8M(10TQ) 无法用整数 TQ 得到 87.5%，用最接近的 85% / 90% 代替，表中标注实际值。 */
static const timing_case_t s_cases[] = {
    /* --- 仲裁 500 kb/s --- */
    {"a500-d2m-sp75",  500, 2000, 1, 119, 40, 1, 29, 10, 1},
    {"a500-d2m-sp80",  500, 2000, 1, 127, 32, 1, 31,  8, 1},
    {"a500-d2m-sp825", 500, 2000, 1, 139, 20, 1, 32,  7, 1},
    {"a500-d4m-sp75",  500, 4000, 1, 119, 40, 1, 14,  5, 1},
    {"a500-d4m-sp80",  500, 4000, 1, 127, 32, 1, 15,  4, 1},
    {"a500-d4m-sp85",  500, 4000, 1, 139, 20, 1, 16,  3, 1},
    {"a500-d5m-sp75",  500, 5000, 1, 119, 40, 1, 11,  4, 1},
    {"a500-d5m-sp80",  500, 5000, 1, 127, 32, 1, 12,  3, 1},
    {"a500-d5m-sp875", 500, 5000, 1, 139, 20, 1, 13,  2, 1},
    {"a500-d8m-sp70",  500, 8000, 1, 119, 40, 1,  6,  3, 1},
    {"a500-d8m-sp80",  500, 8000, 1, 127, 32, 1,  7,  2, 1},
    {"a500-d8m-sp60",  500, 8000, 1, 139, 20, 1,  5,  4, 1},
    /* --- 仲裁 1 Mb/s --- */
    {"a1m-d2m-sp75",  1000, 2000, 1, 59, 20, 1, 29, 10, 1},
    {"a1m-d2m-sp80",  1000, 2000, 1, 63, 16, 1, 31,  8, 1},
    {"a1m-d2m-sp825", 1000, 2000, 1, 69, 10, 1, 32,  7, 1},
    {"a1m-d4m-sp75",  1000, 4000, 1, 59, 20, 1, 14,  5, 1},
    {"a1m-d4m-sp80",  1000, 4000, 1, 63, 16, 1, 15,  4, 1},
    {"a1m-d4m-sp85",  1000, 4000, 1, 69, 10, 1, 16,  3, 1},
    {"a1m-d5m-sp75",  1000, 5000, 1, 59, 20, 1, 11,  4, 1},
    {"a1m-d5m-sp80",  1000, 5000, 1, 63, 16, 1, 12,  3, 1},
    {"a1m-d5m-sp875", 1000, 5000, 1, 69, 10, 1, 13,  2, 1},
    {"a1m-d8m-sp70",  1000, 8000, 1, 59, 20, 1,  6,  3, 1},
    {"a1m-d8m-sp80",  1000, 8000, 1, 63, 16, 1,  7,  2, 1},
    {"a1m-d8m-sp60",  1000, 8000, 1, 69, 10, 1,  5,  4, 1},
};

#define CASE_COUNT (sizeof(s_cases) / sizeof(s_cases[0]))

/* 用给定 case 初始化一个节点；fd=true 时同时开 FD 与（可选）TDC */
static hpm_stat_t node_init_case(MCAN_Type *base, uint32_t *buf, const timing_case_t *c, bool fd)
{
    mcan_config_t cfg;
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    mcan_msg_buf_attr_t attr = {(uint32_t) buf, MCAN_MSG_BUF_SIZE_IN_WORDS * 4U};
    mcan_set_msg_buf_attr(base, &attr);
#endif
    mcan_get_default_config(base, &cfg);
    cfg.mode = mcan_mode_normal;
    cfg.enable_canfd = fd;
    cfg.use_lowlevel_timing_setting = true;
    cfg.can_timing.prescaler = c->p_arb;
    cfg.can_timing.num_seg1 = c->s1_arb;
    cfg.can_timing.num_seg2 = c->s2_arb;
    cfg.can_timing.num_sjw = (c->s2_arb >= 2U) ? 2U : 1U;
    cfg.canfd_timing.prescaler = c->p_dat;
    cfg.canfd_timing.num_seg1 = c->s1_dat;
    cfg.canfd_timing.num_seg2 = c->s2_dat;
    cfg.canfd_timing.num_sjw = (c->s2_dat >= 2U) ? 2U : 1U;
    if (fd) {
        cfg.canfd_timing.enable_tdc = (c->tdc != 0U);
        cfg.enable_tdc = (c->tdc != 0U);
        if (c->tdc != 0U) {
            cfg.tdc_config.ssp_offset = (uint8_t) (c->s1_dat + 1U);
            cfg.tdc_config.filter_window_length = (uint8_t) (c->s1_dat + 1U);
        }
        mcan_get_default_ram_config(base, &cfg.ram_config, true);
    }
    board_init_can(base);
    uint32_t clk = board_init_can_clock(base);
    mcan_deinit(base);
    return mcan_init(base, &cfg, clk);
}

static uint32_t permille_sp(uint16_t s1, uint16_t s2)
{
    return (uint32_t) ((1U + s1) * 1000U / (1U + s1 + s2));
}

static bool rx_wait(MCAN_Type *base, mcan_rx_message_t *rx, uint32_t timeout_us)
{
    uint64_t t0 = cyc_now();
    while (true) {
        if (mcan_read_rxfifo(base, 0, rx) == status_success) {
            return true;
        }
        if (cyc_to_us(cyc_now() - t0) > timeout_us) {
            return false;
        }
    }
}

/* 单向发 n 帧 64B FD（BRS=1），返回收到并校验通过的帧数，坏帧计入 *bad */
static uint32_t run_case(const timing_case_t *c, uint32_t n, uint32_t *bad, uint32_t *elapsed_us)
{
    mcan_tx_frame_t tx;
    mcan_rx_message_t rx;
    hpm_stat_t st = node_init_case(NODE_TX, s_buf_a, c, true);
    if (st != status_success) {
        printf("%s: TX init failed st=%d\r\n", c->name, (int) st);
        return 0;
    }
    st = node_init_case(NODE_RX, s_buf_b, c, true);
    if (st != status_success) {
        printf("%s: RX init failed st=%d\r\n", c->name, (int) st);
        return 0;
    }
    board_delay_us(100);

    uint32_t sent = 0, ok = 0;
    uint32_t badcnt = 0;
    uint64_t t0 = cyc_now();
    const uint32_t batch = 3U;
    while (sent < n) {
        uint32_t k = (n - sent) < batch ? (n - sent) : batch;
        for (uint32_t i = 0; i < k; i++) {
            uint32_t idx = 0;
            __builtin_memset(&tx, 0, sizeof(tx));
            tx.std_id = 0x1ABU;
            tx.dlc = 15U;
            tx.canfd_frame = 1U;
            tx.bitrate_switch = 1U;
            tx.data_32[0] = sent + i;
            tx.data_32[1] = 0xA5A5A5A5U;
            while (mcan_transmit_via_txfifo_nonblocking(NODE_TX, &tx, &idx) != status_success) {
            }
        }
        sent += k;
        for (uint32_t i = 0; i < k; i++) {
            if (!rx_wait(NODE_RX, &rx, 5000U)) {
                badcnt++;
                continue;
            }
            if (rx.std_id == 0x1ABU && rx.dlc == 15U && rx.canfd_frame != 0U &&
                rx.bitrate_switch != 0U) {
                ok++;
            } else {
                badcnt++;
            }
        }
    }
    *elapsed_us = cyc_to_us(cyc_now() - t0);
    *bad = badcnt;
    mcan_protocol_status_t ps;
    mcan_error_count_t ec;
    mcan_get_protocol_status(NODE_TX, &ps);
    mcan_get_error_counter(NODE_TX, &ec);
    printf("%-16s arb=%4uk sp=%u.%u%% dat=%4uk sp=%u.%u%% tdc=%u n=%u rx=%u bad=%u t=%u us "
           "fps=%.0f payload=%.2fMbps lec=%u tec=%u rec=%u tdcv=%u\r\n",
           c->name, (unsigned) c->arb_kbps, (unsigned) (permille_sp(c->s1_arb, c->s2_arb) / 10U),
           (unsigned) (permille_sp(c->s1_arb, c->s2_arb) % 10U), (unsigned) c->data_kbps,
           (unsigned) (permille_sp(c->s1_dat, c->s2_dat) / 10U),
           (unsigned) (permille_sp(c->s1_dat, c->s2_dat) % 10U), (unsigned) c->tdc, (unsigned) n,
           (unsigned) ok, (unsigned) badcnt, (unsigned) *elapsed_us,
           (double) ok * 1000000.0 / (double) (*elapsed_us ? *elapsed_us : 1U),
           (double) ok * 64.0 * 8.0 / (double) (*elapsed_us ? *elapsed_us : 1U),
           (unsigned) ps.last_error_code, (unsigned) ec.transmit_error_count,
           (unsigned) ec.receive_error_count, (unsigned) ps.tdc_val);
    return ok;
}

static void run_all(uint32_t n)
{
    uint32_t total_ok = 0, total_bad = 0;
    printf("\r\n=== MCAN FD timing matrix (80 MHz CAN clk, 64B payload, BRS=1) ===\r\n");
    for (uint32_t i = 0; i < CASE_COUNT; i++) {
        uint32_t bad = 0, us = 0;
        uint32_t ok = run_case(&s_cases[i], n, &bad, &us);
        total_ok += ok;
        total_bad += bad;
    }
    printf("MATRIX SUMMARY: cases=%u frames_ok=%u frames_bad=%u\r\n",
           (unsigned) CASE_COUNT, (unsigned) total_ok, (unsigned) total_bad);
}

/* TDC 开关对比：同一个 2M/5M/8M 数据段，TDC=0 vs TDC=1 */
static void run_tdc_compare(uint32_t n)
{
    printf("\r\n=== TDC on/off @ data 2M/5M/8M (arb 1M, SP80) ===\r\n");
    /* 复用矩阵里 arb=1M 的三档：index 13(sp80,d2m) / 19(sp80,d5m) / 22(sp80,d8m) */
    static const uint32_t idx[3] = {13U, 19U, 22U};
    for (uint32_t k = 0; k < 3U; k++) {
        for (uint32_t tdc = 0; tdc < 2U; tdc++) {
            uint32_t bad = 0, us = 0;
            timing_case_t c = s_cases[idx[k]];
            c.tdc = (uint8_t) tdc;
            c.name = tdc ? "tdc-on" : "tdc-off";
            uint32_t ok = run_case(&c, n, &bad, &us);
            printf("  -> %s data=%uk: ok=%u bad=%u us=%u\n", c.name, (unsigned) c.data_kbps,
                   (unsigned) ok, (unsigned) bad, (unsigned) us);
        }
    }
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("sample: mcan_fd_timing\r\n");
    printf("clock : CAN clk=80 MHz (pll1_clk0/5), TQ formulas: baud=clk/(presc*(1+seg1+seg2))\r\n");
    printf("cases : %u (arb 500k/1M x data 2M/4M/5M/8M x 3 sample points)\r\n", (unsigned) CASE_COUNT);
    printf("rx    : CAN0(PD00/PD01) -> CAN1(PD05/PD04), 64B FD, BRS=1, real ACK\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, show fd timing matrix info);

static void cmd_all(int argc, char *argv[])
{
    uint32_t n = 300;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    run_all(n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 all, cmd_all, run full timing matrix: all [frames_per_case]);

static void cmd_tdc(int argc, char *argv[])
{
    uint32_t n = 300;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    run_tdc_compare(n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 tdc, cmd_tdc, TDC on/off compare: tdc [n]);

static void cmd_one(int argc, char *argv[])
{
    uint32_t i = 0, n = 300;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &i);
    }
    if (argc >= 3) {
        bsp_parse_u32(argv[2], &n);
    }
    if (i >= CASE_COUNT) {
        printf("index out of range 0..%u\r\n", (unsigned) (CASE_COUNT - 1U));
        return;
    }
    uint32_t bad = 0, us = 0;
    run_case(&s_cases[i], n, &bad, &us);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 one, cmd_one, run single case: one <idx> [n]);

int main(void)
{
    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    printf("\r\n[boot] mcan_fd_timing\r\n");
    board_can_leds_init();
    board_can_init_pins();
    board_can_transceivers_enable();
    printf("[cfg] cpu=%u MHz, cases=%u\r\n", (unsigned) s_cpu_mhz, (unsigned) CASE_COUNT);
    run_all(300);
    run_tdc_compare(300);
    printf("done. commands: info | all [n] | tdc [n] | one <idx> [n]\r\n");
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
