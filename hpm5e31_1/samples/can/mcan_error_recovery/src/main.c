/*
 * mcan_error_recovery - 错误帧 / 错误计数器 / bus-off / 自动恢复
 *
 * 硬件: hpm5e31_1，CAN0(PD00/PD01) 与 CAN1(PD05/PD04) 接同一段总线。
 * 方法: 故意把 CAN1 配成 250 kbit/s（CAN0 为 500 kbit/s），CAN0 发出的帧收不到 ACK，
 *       硬件自动重传使 TEC 每次 +8；记录 TEC/REC/LEC/PSR，观察
 *       错误主动 -> 错误被动(TEC>=128) -> bus-off(TEC>255)，随后修好 CAN1 并调用
 *       mcan_recover_from_busoff() 验证恢复后能正常收发。
 * 命令: info / run
 */
#include "bsp.h"
#include "hpm_mcan_drv.h"
#include "ext/board_can.h"

#define NODE_A BOARD_CAN0_BASE
#define NODE_B BOARD_CAN1_BASE

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_buf_a[MCAN_MSG_BUF_SIZE_IN_WORDS];
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_buf_b[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static uint32_t s_cpu_mhz;
static uint32_t s_pass;
static uint32_t s_fail;

static uint64_t cyc_now(void)
{
    return (uint64_t) hpm_csr_get_core_mcycle();
}

static uint32_t cyc_to_us(uint64_t c)
{
    return (uint32_t) (c / (uint64_t) s_cpu_mhz);
}

/* 500k: presc=1 seg1=139 seg2=20；250k: presc=1 seg1=319? 超范围，用 presc=2 seg1=159? 也超范围。
 * 250k 用 presc=2, 320TQ -> seg1=239 seg2=80（160TQ/2=80? 不对）：
 * 80MHz/(2*(1+239+80)) = 80e6/640 = 125k。正确：250k 需要 presc=2 且 1+seg1+seg2=160。 */
static hpm_stat_t node_init_bps(MCAN_Type *base, uint32_t *buf, uint32_t bps, bool listen_only)
{
    mcan_config_t cfg;
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    mcan_msg_buf_attr_t attr = {(uint32_t) buf, MCAN_MSG_BUF_SIZE_IN_WORDS * 4U};
    mcan_set_msg_buf_attr(base, &attr);
#endif
    mcan_get_default_config(base, &cfg);
    cfg.mode = listen_only ? mcan_mode_listen_only : mcan_mode_normal;
    cfg.enable_canfd = false;
    cfg.use_lowlevel_timing_setting = true;
    cfg.can_timing.prescaler = 2U;
    if (bps == 500000U) {
        cfg.can_timing.num_seg1 = 63U;   /* 80MHz/2/(1+63+16)=500k, SP=80% */
        cfg.can_timing.num_seg2 = 16U;
    } else {
        cfg.can_timing.num_seg1 = 127U;  /* 80MHz/2/(1+127+32)=250k, SP=80% */
        cfg.can_timing.num_seg2 = 32U;
    }
    cfg.can_timing.num_sjw = 4U;
    board_init_can(base);
    uint32_t clk = board_init_can_clock(base);
    mcan_deinit(base);
    return mcan_init(base, &cfg, clk);
}

static void print_status(const char *tag, MCAN_Type *base)
{
    mcan_protocol_status_t ps;
    mcan_error_count_t ec;
    mcan_get_protocol_status(base, &ps);
    mcan_get_error_counter(base, &ec);
    printf("  %-10s TEC=%3u REC=%3u LEC=%u BO=%u EP=%u EW=%u ACT=%u tdcv=%u\r\n", tag,
           (unsigned) ec.transmit_error_count, (unsigned) ec.receive_error_count,
           (unsigned) ps.last_error_code, (unsigned) ps.in_bus_off_state,
           (unsigned) ps.in_error_passive_state, (unsigned) ps.in_warning_state,
           (unsigned) ps.activity, (unsigned) ps.tdc_val);
}

static bool send_one_and_wait(MCAN_Type *base, uint32_t timeout_us)
{
    mcan_tx_frame_t tx;
    __builtin_memset(&tx, 0, sizeof(tx));
    tx.std_id = 0x123U;
    tx.dlc = 8U;
    tx.data_32[0] = 0x11223344U;
    uint32_t idx = 0;
    if (mcan_transmit_via_txfifo_nonblocking(base, &tx, &idx) != status_success) {
        return false;
    }
    uint64_t t0 = cyc_now();
    while (!mcan_is_transmit_occurred(base, idx)) {
        if (cyc_to_us(cyc_now() - t0) > timeout_us) {
            return false;
        }
    }
    return true;
}

/* 等一个帧从 B 的 RXFIFO0 收回来 */
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

static void run_error_test(void)
{
    printf("\r\n=== mcan_error_recovery: CAN1 故意错配 250k，CAN0 500k ===\r\n");
    printf("--- 阶段1: 制造 ACK 错误直到 bus-off ---\r\n");
    node_init_bps(NODE_A, s_buf_a, 500000U, false);
    node_init_bps(NODE_B, s_buf_b, 500000U, true);  /* 只听模式：不应答、不发错误帧 */
    board_delay_us(200);
    print_status("CAN0@500k", NODE_A);
    print_status("CAN1@500k\n         listen-only", NODE_B);

    mcan_tx_frame_t tx;
    __builtin_memset(&tx, 0, sizeof(tx));
    tx.std_id = 0x123U;
    tx.dlc = 8U;
    tx.data_32[0] = 0xDEADBEEFU;
    uint32_t idx = 0;
    hpm_stat_t st = mcan_transmit_via_txfifo_nonblocking(NODE_A, &tx, &idx);
    printf("[tx] submit st=%d idx=%u\r\n", (int) st, (unsigned) idx);

    uint32_t last_tec = 0;
    bool bo = false;
    uint64_t t0 = cyc_now();
    uint32_t refill = 0;
    while (cyc_to_us(cyc_now() - t0) < 200000U) {
        /* 每 200us 补一帧，让多个 TX 元素同时重传，TEC 增长更快 */
        if ((refill++ % 10U) == 0U) {
            (void) mcan_transmit_via_txfifo_nonblocking(NODE_A, &tx, &idx);
        }
        mcan_protocol_status_t ps;
        mcan_error_count_t ec;
        mcan_get_protocol_status(NODE_A, &ps);
        mcan_get_error_counter(NODE_A, &ec);
        if (ec.transmit_error_count != last_tec) {
            last_tec = ec.transmit_error_count;
            if ((last_tec == 96U) || (last_tec == 128U) || (last_tec >= 248U)) {
                printf("  t=%u us TEC=%u REC=%u LEC=%u EP=%u EW=%u\r\n",
                       (unsigned) cyc_to_us(cyc_now() - t0), (unsigned) last_tec,
                       (unsigned) ec.receive_error_count, (unsigned) ps.last_error_code,
                       (unsigned) ps.in_error_passive_state, (unsigned) ps.in_warning_state);
            }
        }
        if (ps.in_bus_off_state) {
            bo = true;
            printf("  bus-off at t=%u us, TEC=%u REC=%u\r\n",
                   (unsigned) cyc_to_us(cyc_now() - t0), (unsigned) ec.transmit_error_count,
                   (unsigned) ec.receive_error_count);
            break;
        }
        board_delay_us(20);
    }
    print_status("CAN0(busoff)", NODE_A);
    if (!bo) {
        printf("  RESULT: 未在 200ms 内进入 bus-off (FAIL)\r\n");
        s_fail++;
    } else {
        s_pass++;
    }

    printf("--- 阶段2: 修好 CAN1=500k，调用 mcan_recover_from_busoff() ---\r\n");
    node_init_bps(NODE_B, s_buf_b, 500000U, false);
    hpm_stat_t rst = mcan_recover_from_busoff(NODE_A);
    uint64_t tr = cyc_now();
    bool recovered = false;
    while (cyc_to_us(cyc_now() - tr) < 200000U) {
        mcan_protocol_status_t ps;
        mcan_get_protocol_status(NODE_A, &ps);
        if (!ps.in_bus_off_state) {
            recovered = true;
            break;
        }
        board_delay_us(20);
    }
    printf("  recover st=%d recovered=%u, took=%u us\r\n", (int) rst, (unsigned) recovered,
           (unsigned) cyc_to_us(cyc_now() - tr));
    print_status("CAN0(recover)", NODE_A);
    if (recovered) {
        s_pass++;
    } else {
        s_fail++;
    }

    printf("--- 阶段3: 恢复后正常收发 ---\r\n");
    bool sent = send_one_and_wait(NODE_A, 5000U);
    mcan_rx_message_t rx;
    bool got = rx_wait(NODE_B, &rx, 5000U);
    bool ok = sent && got && (rx.std_id == 0x123U) && (rx.dlc == 8U) &&
              (rx.data_32[0] == 0xDEADBEEFU) && (rx.canfd_frame == 0U);
    printf("  sent=%u got=%u id=0x%03X dlc=%u data0=0x%08X => %s\r\n", (unsigned) sent,
           (unsigned) got, (unsigned) rx.std_id, (unsigned) rx.dlc,
           (unsigned) rx.data_32[0], ok ? "PASS" : "FAIL");
    if (ok) {
        s_pass++;
    } else {
        s_fail++;
    }
    print_status("CAN0(after)", NODE_A);
    print_status("CAN1(after)", NODE_B);
    printf("SUMMARY: pass=%u fail=%u %s\r\n", (unsigned) s_pass, (unsigned) s_fail,
           (s_fail == 0U) ? "ALL PASS" : "FAILED");
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("sample: mcan_error_recovery\r\n");
    printf("phase1: CAN0 500k sends, CAN1 500k listen-only (no ACK) -> TEC+8/retry, bus-off\r\n");
    printf("phase2: fix CAN1=500k, mcan_recover_from_busoff(), measure recovery time\r\n");
    printf("phase3: verify normal tx/rx after recovery\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, show error recovery sample info);

static void cmd_run(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    run_error_test();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 run, cmd_run, run error recovery test);

int main(void)
{
    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    printf("\r\n[boot] mcan_error_recovery\r\n");
    board_can_leds_init();
    board_can_init_pins();
    board_can_transceivers_enable();
    run_error_test();
    printf("type 'run' to rerun, 'info' for details\r\n");
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
