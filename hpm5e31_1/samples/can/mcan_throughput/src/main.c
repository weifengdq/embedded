/*
 * mcan_throughput - CAN 2.0 / CAN FD 单向吞吐实测（CAN0 -> CAN1 真实总线）
 *
 * 硬件: hpm5e31_1，CAN0(PD00/PD01) 发送，CAN1(PD05/PD04) 接收并 ACK，同一段总线两端 120 欧。
 * 方法: 批量提交（TX FIFO 深度决定批次），接收端轮询 RXFIFO0 逐帧校验，统计
 *       frame/s、payload Mbit/s、每帧平均 CPU 周期。
 * 命令: info / thr [n] / one <idx> [n]
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
    bool fd;
    uint16_t p_arb, s1_arb, s2_arb;
    uint16_t p_dat, s1_dat, s2_dat;
    uint8_t dlc;
} thr_case_t;

/* 80 MHz CAN 时钟；经典 500k: 160TQ, 1M: 80TQ；数据段 2M: 40TQ, 5M: 16TQ, 8M: 10TQ */
static const thr_case_t s_cases[] = {
    {"can20-500k-8B",  false, 1, 139, 20, 0, 0, 0, 8},
    {"can20-1M-8B",    false, 1,  69, 10, 0, 0, 0, 8},
    {"fd-500k-2M-64B", true,  1, 139, 20, 1, 29, 10, 15},
    {"fd-1M-2M-64B",   true,  1,  69, 10, 1, 29, 10, 15},
    {"fd-1M-5M-64B",   true,  1,  69, 10, 1, 13,  2, 15},
    {"fd-1M-8M-64B",   true,  1,  69, 10, 1,  7,  2, 15},
};
#define CASE_COUNT (sizeof(s_cases) / sizeof(s_cases[0]))

static hpm_stat_t node_init_thr(MCAN_Type *base, uint32_t *buf, const thr_case_t *c, bool fd)
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
    cfg.can_timing.num_sjw = 2U;
    if (fd) {
        cfg.canfd_timing.prescaler = c->p_dat;
        cfg.canfd_timing.num_seg1 = c->s1_dat;
        cfg.canfd_timing.num_seg2 = c->s2_dat;
        cfg.canfd_timing.num_sjw = 2U;
        cfg.canfd_timing.enable_tdc = true;
        cfg.enable_tdc = true;
        cfg.tdc_config.ssp_offset = (uint8_t) (c->s1_dat + 1U);
        cfg.tdc_config.filter_window_length = (uint8_t) (c->s1_dat + 1U);
        mcan_get_default_ram_config(base, &cfg.ram_config, true);
    }
    board_init_can(base);
    uint32_t clk = board_init_can_clock(base);
    mcan_deinit(base);
    return mcan_init(base, &cfg, clk);
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

static void run_thr(uint32_t idx, uint32_t n)
{
    const thr_case_t *c = &s_cases[idx];
    mcan_tx_frame_t tx;
    mcan_rx_message_t rx;
    hpm_stat_t st = node_init_thr(NODE_TX, s_buf_a, c, c->fd);
    if (st != status_success) {
        printf("%s: TX init failed st=%d\r\n", c->name, (int) st);
        return;
    }
    st = node_init_thr(NODE_RX, s_buf_b, c, c->fd);
    if (st != status_success) {
        printf("%s: RX init failed st=%d\r\n", c->name, (int) st);
        return;
    }
    board_delay_us(100);

    const uint32_t batch = c->fd ? 3U : 6U;
    uint32_t sent = 0, ok = 0, bad = 0;
    uint8_t payload_bytes = mcan_get_message_size_from_dlc(c->dlc);
    uint64_t t0 = cyc_now();
    while (sent < n) {
        uint32_t k = (n - sent) < batch ? (n - sent) : batch;
        for (uint32_t i = 0; i < k; i++) {
            uint32_t fi = 0;
            __builtin_memset(&tx, 0, sizeof(tx));
            tx.std_id = 0x155U;
            tx.dlc = c->dlc;
            tx.canfd_frame = c->fd ? 1U : 0U;
            tx.bitrate_switch = c->fd ? 1U : 0U;
            tx.data_32[0] = sent + i;
            tx.data_32[1] = 0x5AA55AA5U;
            while (mcan_transmit_via_txfifo_nonblocking(NODE_TX, &tx, &fi) != status_success) {
            }
        }
        sent += k;
        for (uint32_t i = 0; i < k; i++) {
            if (!rx_wait(NODE_RX, &rx, 5000U)) {
                bad++;
                continue;
            }
            if (rx.std_id == 0x155U && rx.dlc == c->dlc) {
                ok++;
            } else {
                bad++;
            }
        }
    }
    uint32_t us = cyc_to_us(cyc_now() - t0);
    double fps = (double) ok * 1000000.0 / (double) (us ? us : 1U);
    double mbps = fps * (double) payload_bytes * 8.0 / 1000000.0;
    mcan_protocol_status_t ps;
    mcan_error_count_t ec;
    mcan_get_protocol_status(NODE_TX, &ps);
    mcan_get_error_counter(NODE_TX, &ec);
    printf("%-16s n=%u rx=%u bad=%u t=%u us fps=%.0f payload=%.2fMbps cyc/frame=%.0f "
           "lec=%u tec=%u rec=%u\r\n",
           c->name, (unsigned) n, (unsigned) ok, (unsigned) bad, (unsigned) us, fps, mbps,
           (double) s_cpu_mhz * (double) us / (double) (ok ? ok : 1U),
           (unsigned) ps.last_error_code, (unsigned) ec.transmit_error_count,
           (unsigned) ec.receive_error_count);
}

static void run_all(uint32_t n)
{
    printf("\r\n=== MCAN throughput (CAN0->CAN1 one-way, real bus, polling) ===\r\n");
    for (uint32_t i = 0; i < CASE_COUNT; i++) {
        run_thr(i, n);
    }
    printf("THR SUMMARY done\r\n");
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("sample: mcan_throughput  cases=%u\r\n", (unsigned) CASE_COUNT);
    for (uint32_t i = 0; i < CASE_COUNT; i++) {
        printf("  [%u] %-16s fd=%u dlc=%u\r\n", (unsigned) i, s_cases[i].name,
               (unsigned) s_cases[i].fd, (unsigned) s_cases[i].dlc);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, list throughput cases);

static void cmd_thr(int argc, char *argv[])
{
    uint32_t n = 2000;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    run_all(n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 thr, cmd_thr, run all throughput cases: thr [n]);

static void cmd_one(int argc, char *argv[])
{
    uint32_t i = 0, n = 2000;
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
    run_thr(i, n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 one, cmd_one, run one throughput case: one <idx> [n]);

int main(void)
{
    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    printf("\r\n[boot] mcan_throughput\r\n");
    board_can_leds_init();
    board_can_init_pins();
    board_can_transceivers_enable();
    printf("[cfg] cpu=%u MHz, cases=%u\r\n", (unsigned) s_cpu_mhz, (unsigned) CASE_COUNT);
    run_all(2000);
    printf("done. commands: info | thr [n] | one <idx> [n]\r\n");
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
