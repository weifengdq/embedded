/*
 * mcan_two_node - CAN0 <-> CAN1 双节点真实总线收发（hpm5e31_1 板内两节点）
 *
 * 硬件: hpm5e31_1，CAN0(PD00/PD01) 与 CAN1(PD05/PD04) 经 TCAN1044AV 接到同一段
 *       CAN 总线，总线两端各 120 欧终端电阻（板上已接）。无需外部 CAN 分析仪。
 *
 * 测试内容:
 *   1) run  [n] : 经典帧 ping-pong：CAN0 发 0x123 -> CAN1 收并校验 -> CAN1 回 0x321 -> CAN0 收
 *   2) thr  [n] : 经典帧单向流水：CAN0 连续发 n 帧 8B，CAN1 全收并逐帧校验计数器
 *   3) fd   [n] : CAN FD ping-pong：仲裁 1M / 数据 2M，64B 载荷，开 TDC
 * 现象: 上电自动跑一遍默认规模并打印表格；命令可重跑。
 */
#include "bsp.h"
#include "hpm_mcan_drv.h"
#include "ext/board_can.h"

#define NODE_A BOARD_CAN0_BASE /* 发起端 */
#define NODE_B BOARD_CAN1_BASE /* 应答端 */

#define ID_REQ  (0x123U)
#define ID_ECHO (0x321U)

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_buf_a[MCAN_MSG_BUF_SIZE_IN_WORDS];
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_buf_b[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static uint32_t s_cpu_mhz;

static uint64_t cyc_now(void)
{
    return (uint64_t) hpm_csr_get_core_mcycle();
}

static uint32_t cyc_to_us(uint64_t cyc)
{
    return (uint32_t) (cyc / (uint64_t) s_cpu_mhz);
}

/* ---------- 初始化 ---------- */

static hpm_stat_t node_init(MCAN_Type *base, uint32_t *msg_buf, bool fd)
{
    mcan_config_t cfg;
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    mcan_msg_buf_attr_t attr = {(uint32_t) msg_buf, MCAN_MSG_BUF_SIZE_IN_WORDS * 4U};
    mcan_set_msg_buf_attr(base, &attr);
#endif
    mcan_get_default_config(base, &cfg);
    cfg.mode = mcan_mode_normal;
    cfg.enable_canfd = fd;
    if (!fd) {
        /* 经典帧：仲裁段 500 kbit/s，采样点 87.5%（80MHz/1/160TQ, seg1=139 seg2=20） */
        cfg.use_lowlevel_timing_setting = true;
        cfg.can_timing.prescaler = 1U;
        cfg.can_timing.num_seg1 = 139U;
        cfg.can_timing.num_seg2 = 20U;
        cfg.can_timing.num_sjw = 4U;
    } else {
        /* FD：仲裁 1 Mbit/s（75%，presc=2 seg1=29 seg2=10），数据 2 Mbit/s（75%），开 TDC */
        cfg.use_lowlevel_timing_setting = true;
        cfg.can_timing.prescaler = 2U;
        cfg.can_timing.num_seg1 = 29U;
        cfg.can_timing.num_seg2 = 10U;
        cfg.can_timing.num_sjw = 2U;
        cfg.canfd_timing.prescaler = 2U;
        cfg.canfd_timing.num_seg1 = 14U;
        cfg.canfd_timing.num_seg2 = 5U;
        cfg.canfd_timing.num_sjw = 2U;
        cfg.canfd_timing.enable_tdc = true;
        cfg.enable_tdc = true;
        cfg.tdc_config.ssp_offset = 15U;
        cfg.tdc_config.filter_window_length = 15U;
        mcan_get_default_ram_config(base, &cfg.ram_config, true);
    }
    board_init_can(base);
    uint32_t clk = board_init_can_clock(base); /* 必须先开时钟，否则 mcan_deinit 访问被门控的寄存器会挂死 */
    mcan_deinit(base);
    return mcan_init(base, &cfg, clk);
}

static hpm_stat_t bus_up(bool fd)
{
    hpm_stat_t st;
    board_can_transceivers_enable();
    st = node_init(NODE_A, s_buf_a, fd);
    if (st != status_success) {
        return st;
    }
    st = node_init(NODE_B, s_buf_b, fd);
    if (st != status_success) {
        return st;
    }
    board_delay_us(100);
    return status_success;
}

/* 非阻塞收一帧；timeout_us 内没收到返回 false */
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

/* 载荷：byte0..3 = 序号小端，byte4..7 = 0xA5+i；rx_match 只校验前 4 字节 */
static bool rx_match(const mcan_rx_message_t *rx, uint16_t id, uint32_t seq)
{
    if (rx->use_ext_id != 0U || rx->std_id != id || rx->dlc != 8U || rx->canfd_frame != 0U) {
        return false;
    }
    uint32_t v = (uint32_t) rx->data_8[0] | ((uint32_t) rx->data_8[1] << 8) |
                 ((uint32_t) rx->data_8[2] << 16) | ((uint32_t) rx->data_8[3] << 24);
    return v == seq;
}

static void fill_seq(mcan_tx_frame_t *tx, uint16_t id, uint32_t seq)
{
    tx->std_id = id;
    tx->dlc = 8U;
    tx->use_ext_id = 0U;
    tx->rtr = 0U;
    tx->canfd_frame = 0U;
    tx->bitrate_switch = 0U;
    for (uint8_t i = 0; i < 4U; i++) {
        tx->data_8[i] = (uint8_t) (seq >> (8U * i));
        tx->data_8[4U + i] = (uint8_t) (0xA5U + i);
    }
}

/* ---------- 1) 经典帧 ping-pong ---------- */

static void bench_pingpong(uint32_t n)
{
    mcan_tx_frame_t tx;
    mcan_rx_message_t rx;
    hpm_stat_t st = bus_up(false);
    if (st != status_success) {
        printf("PINGPONG init failed, status=%d\r\n", (int) st);
        return;
    }
    uint32_t ok = 0, lost_req = 0, lost_echo = 0, tmo_us = 0, tmax_us = 0;
    uint64_t t0 = cyc_now();
    for (uint32_t seq = 0; seq < n; seq++) {
        __builtin_memset(&tx, 0, sizeof(tx));
        fill_seq(&tx, ID_REQ, seq);
        if (mcan_transmit_blocking(NODE_A, &tx) != status_success) {
            lost_req++;
            continue;
        }
        if (!rx_wait(NODE_B, &rx, 2000U)) {
            lost_req++;
            continue;
        }
        if (!rx_match(&rx, ID_REQ, seq)) {
            lost_req++;
            continue;
        }
        /* 应答端：把同一序号以 0x321 回显 */
        __builtin_memset(&tx, 0, sizeof(tx));
        fill_seq(&tx, ID_ECHO, seq);
        if (mcan_transmit_blocking(NODE_B, &tx) != status_success) {
            lost_echo++;
            continue;
        }
        uint64_t t1 = cyc_now();
        if (!rx_wait(NODE_A, &rx, 2000U)) {
            lost_echo++;
            continue;
        }
        uint32_t dt = cyc_to_us(cyc_now() - t1);
        if (dt > tmax_us) {
            tmax_us = dt;
        }
        tmo_us += dt;
        if (rx_match(&rx, ID_ECHO, seq)) {
            ok++;
        } else {
            lost_echo++;
        }
    }
    uint32_t total_us = cyc_to_us(cyc_now() - t0);
    printf("PINGPONG classic 500k/SP87.5: n=%u ok=%u bad_req=%u bad_echo=%u total=%u us\r\n",
           (unsigned) n, (unsigned) ok, (unsigned) lost_req, (unsigned) lost_echo, (unsigned) total_us);
    if (ok != 0U) {
        printf("  echo_latency: avg=%u us max=%u us  roundtrip_rate=%.0f /s\r\n",
               (unsigned) (tmo_us / ok), (unsigned) tmax_us,
               (double) ok * 1000000.0 / (double) (total_us ? total_us : 1U));
    }
}

/* ---------- 2) 经典帧单向流水 ---------- */

static void bench_throughput(uint32_t n)
{
    mcan_tx_frame_t tx;
    mcan_rx_message_t rx;
    hpm_stat_t st = bus_up(false);
    if (st != status_success) {
        printf("THR init failed, status=%d\r\n", (int) st);
        return;
    }
    const uint32_t batch = 8U;
    uint32_t sent = 0, got = 0, bad = 0;
    uint64_t t0 = cyc_now();
    while (sent < n) {
        uint32_t k = (n - sent) < batch ? (n - sent) : batch;
        for (uint32_t i = 0; i < k; i++) {
            uint32_t idx = 0;
            __builtin_memset(&tx, 0, sizeof(tx));
            fill_seq(&tx, ID_REQ, sent + i);
            while (mcan_transmit_via_txfifo_nonblocking(NODE_A, &tx, &idx) != status_success) {
            }
        }
        sent += k;
        for (uint32_t i = 0; i < k; i++) {
            if (!rx_wait(NODE_B, &rx, 5000U)) {
                bad++;
                continue;
            }
            if (rx_match(&rx, ID_REQ, got)) {
                got++;
            } else {
                bad++;
                got++;
            }
        }
    }
    uint32_t total_us = cyc_to_us(cyc_now() - t0);
    double fps = (double) n * 1000000.0 / (double) (total_us ? total_us : 1U);
    printf("THROUGHPUT classic 500k/SP87.5: n=%u rx=%u bad=%u total=%u us => %.0f frame/s\r\n",
           (unsigned) n, (unsigned) got, (unsigned) bad, (unsigned) total_us, fps);
    printf("  payload: %.2f Mbit/s (8B/frame @%.0f fps), bus 500 kbit/s\r\n",
           fps * 8.0 * 8.0 / 1000000.0, fps);
}

/* ---------- 3) CAN FD ping-pong ---------- */

static void bench_fd(uint32_t n)
{
    mcan_tx_frame_t tx;
    mcan_rx_message_t rx;
    hpm_stat_t st = bus_up(true);
    if (st != status_success) {
        printf("FD init failed, status=%d\r\n", (int) st);
        return;
    }
    uint32_t ok = 0, errs = 0;
    uint64_t t0 = cyc_now();
    for (uint32_t seq = 0; seq < n; seq++) {
        __builtin_memset(&tx, 0, sizeof(tx));
        tx.std_id = ID_REQ;
        tx.dlc = 15U;
        tx.canfd_frame = 1U;
        tx.bitrate_switch = 1U;
        for (uint8_t i = 0; i < 64U; i++) {
            tx.data_8[i] = (uint8_t) (seq + i);
        }
        if (mcan_transmit_blocking(NODE_A, &tx) != status_success) {
            errs++;
            continue;
        }
        if (!rx_wait(NODE_B, &rx, 3000U)) {
            errs++;
            continue;
        }
        if (rx.std_id != ID_REQ || rx.dlc != 15U || rx.canfd_frame == 0U || rx.bitrate_switch == 0U) {
            errs++;
            continue;
        }
        tx.std_id = ID_ECHO;
        if (mcan_transmit_blocking(NODE_B, &tx) != status_success) {
            errs++;
            continue;
        }
        if (!rx_wait(NODE_A, &rx, 3000U)) {
            errs++;
            continue;
        }
        bool payload_ok = true;
        for (uint8_t i = 0; i < 64U; i++) {
            if (rx.data_8[i] != (uint8_t) (seq + i)) {
                payload_ok = false;
                break;
            }
        }
        if (rx.std_id == ID_ECHO && payload_ok) {
            ok++;
        } else {
            errs++;
        }
    }
    uint32_t total_us = cyc_to_us(cyc_now() - t0);
    mcan_protocol_status_t ps;
    mcan_get_protocol_status(NODE_A, &ps);
    printf("FD pingpong arb1M/data2M 64B: n=%u ok=%u err=%u total=%u us => %.0f roundtrip/s lec=%u\r\n",
           (unsigned) n, (unsigned) ok, (unsigned) errs, (unsigned) total_us,
           (double) ok * 1000000.0 / (double) (total_us ? total_us : 1U),
           (unsigned) ps.last_error_code);
}

/* ---------- 命令 ---------- */

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("sample: mcan_two_node  CAN0(PD00/PD01) <-> CAN1(PD05/PD04) via TCAN1044AV\r\n");
    printf("classic: 500 kbit/s, presc=1 seg1=139 seg2=20 sjw=4, SP=87.5%%\r\n");
    printf("fd     : arb 1 Mbit/s (75.0%%) / data 2 Mbit/s (75.0%%), TDC ssp=15, 64B\r\n");
    printf("cpu    : %u MHz, 1 us = %u cycles\r\n", (unsigned) s_cpu_mhz, (unsigned) s_cpu_mhz);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, show two-node bus config);

static void cmd_run(int argc, char *argv[])
{
    uint32_t n = 500;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    bench_pingpong(n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 run, cmd_run, classic ping-pong: run [n]);

static void cmd_thr(int argc, char *argv[])
{
    uint32_t n = 2000;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    bench_throughput(n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 thr, cmd_thr, classic one-way throughput: thr [n]);

static void cmd_fd(int argc, char *argv[])
{
    uint32_t n = 500;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    bench_fd(n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 fd, cmd_fd, CAN FD ping-pong: fd [n]);

static void cmd_stat(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    MCAN_Type *bases[2] = {NODE_A, NODE_B};
    const char *names[2] = {"CAN0", "CAN1"};
    for (uint32_t i = 0; i < 2U; i++) {
        mcan_protocol_status_t ps;
        mcan_error_count_t ec;
        mcan_get_protocol_status(bases[i], &ps);
        mcan_get_error_counter(bases[i], &ec);
        printf("%s: act=%u lec=%u bo=%u ep=%u ew=%u tec=%u rec=%u tdcv=%u\r\n", names[i],
               (unsigned) ps.activity, (unsigned) ps.last_error_code,
               (unsigned) mcan_is_in_busoff_state(bases[i]), (unsigned) ps.in_error_passive_state,
               (unsigned) ps.in_warning_state, (unsigned) ec.transmit_error_count,
               (unsigned) ec.receive_error_count, (unsigned) ps.tdc_val);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, dump CAN0/CAN1 PSR/ECR);

int main(void)
{
    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    printf("\r\n[boot] mcan_two_node (CAN0 <-> CAN1)\r\n");
    board_can_leds_init();
    board_can_init_pins();
    printf("[cfg] cpu=%u MHz\r\n", (unsigned) s_cpu_mhz);
    bench_pingpong(500);
    bench_throughput(2000);
    bench_fd(500);
    printf("type 'run|thr|fd [n]' to rerun, 'info'/'stat' for details\r\n");
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
