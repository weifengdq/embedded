/*
 * mcan_loopback_internal - 4 路 MCAN 内部环回自检（CAN 2.0B + CAN FD）
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1)，仅需串口；内部环回（TEST.LBCK）不驱动总线引脚，
 *       因此不接外部节点也能自证 MCAN IP / 时钟 / 消息 RAM 通路。
 *
 * 现象: 上电后串口打印 4 个实例 x (经典帧 + FD 帧) 的自检结果，最后一行 SUMMARY。
 * 命令: info / test / stat
 */
#include "bsp.h"
#include "hpm_mcan_drv.h"
#include "ext/board_can.h"

#define CAN_COUNT (4U)

static MCAN_Type *const s_base[CAN_COUNT] = {BOARD_CAN0_BASE, BOARD_CAN1_BASE,
                                             BOARD_CAN2_BASE, BOARD_CAN3_BASE};
static const char *const s_name[CAN_COUNT] = {"CAN0", "CAN1", "CAN2", "CAN3"};

static uint32_t s_pass;
static uint32_t s_fail;
static uint32_t s_clk_hz;

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
/* MCAN 消息 RAM 必须位于 AHB_SRAM(0xF0200000, 32KB)，见 hpm_mcan_soc.h */
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_msg_buf[CAN_COUNT][MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static bool frame_match(const mcan_tx_frame_t *tx, const mcan_rx_message_t *rx)
{
    if (tx->dlc != rx->dlc || tx->use_ext_id != rx->use_ext_id || tx->rtr != rx->rtr ||
        tx->canfd_frame != rx->canfd_frame || tx->bitrate_switch != rx->bitrate_switch) {
        return false;
    }
    if (tx->use_ext_id != 0U) {
        if (tx->ext_id != rx->ext_id) {
            return false;
        }
    } else {
        if (tx->std_id != rx->std_id) {
            return false;
        }
    }
    uint8_t n = mcan_get_message_size_from_dlc(tx->dlc);
    for (uint8_t i = 0; i < n; i++) {
        if (tx->data_8[i] != rx->data_8[i]) {
            return false;
        }
    }
    return true;
}

static void fill_payload(mcan_tx_frame_t *tx)
{
    uint8_t n = mcan_get_message_size_from_dlc(tx->dlc);
    for (uint8_t i = 0; i < n; i++) {
        tx->data_8[i] = (uint8_t) (i * 7U + 0x5AU);
    }
}

/* 初始化一个实例为内部环回模式；fd=true 时用 CAN FD 帧格式 */
static hpm_stat_t loopback_init(uint32_t idx, bool fd)
{
    MCAN_Type *base = s_base[idx];
    mcan_config_t cfg;
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    mcan_msg_buf_attr_t attr = {(uint32_t) s_msg_buf[idx], sizeof(s_msg_buf[idx])};
    mcan_set_msg_buf_attr(base, &attr);
#endif
    mcan_get_default_config(base, &cfg);
    cfg.mode = mcan_mode_loopback_internal;
    cfg.enable_canfd = fd;
    if (fd) {
        cfg.baudrate = 500000U;      /* 仲裁段 500 kbit/s */
        cfg.baudrate_fd = 2000000U;  /* 数据段 2 Mbit/s */
        cfg.enable_tdc = false;      /* 内部环回无收发器环回延迟，TDC 可关 */
        mcan_get_default_ram_config(base, &cfg.ram_config, true);
    }
    board_init_can(base);
    s_clk_hz = board_init_can_clock(base);
    mcan_deinit(base);
    return mcan_init(base, &cfg, s_clk_hz);
}

/* round: 0=经典帧标准 ID, 1=经典帧扩展 ID, 2=FD 标准 ID, 3=FD 扩展 ID */
static bool loopback_round(uint32_t idx, uint8_t round)
{
    MCAN_Type *base = s_base[idx];
    mcan_tx_frame_t tx;
    mcan_rx_message_t rx;
    bool fd = (round >= 2U);
    hpm_stat_t st = loopback_init(idx, fd);
    if (st != status_success) {
        printf("  %s round%u init FAILED, status=%d\r\n", s_name[idx], round, (int) st);
        return false;
    }
    __builtin_memset(&tx, 0, sizeof(tx));
    __builtin_memset(&rx, 0, sizeof(rx));
    tx.canfd_frame = fd ? 1U : 0U;
    tx.bitrate_switch = (fd && round >= 2U) ? 1U : 0U;
    if (round == 0U) {
        tx.std_id = 0x123U;
        tx.dlc = 8U;
    } else if (round == 1U) {
        tx.use_ext_id = 1U;
        tx.ext_id = 0x12345678U;
        tx.dlc = 8U;
    } else if (round == 2U) {
        tx.std_id = 0x1ABU;
        tx.dlc = 15U; /* 64 字节 */
    } else {
        tx.use_ext_id = 1U;
        tx.ext_id = 0x1ABCDEF0U;
        tx.dlc = 15U;
    }
    fill_payload(&tx);

    st = mcan_transmit_blocking(base, &tx);
    if (st != status_success) {
        printf("  %s round%u tx FAILED, status=%d\r\n", s_name[idx], round, (int) st);
        return false;
    }
    st = mcan_receive_from_fifo_blocking(base, 0, &rx);
    if (st != status_success) {
        printf("  %s round%u rx FAILED, status=%d\r\n", s_name[idx], round, (int) st);
        return false;
    }
    bool ok = frame_match(&tx, &rx);
    mcan_protocol_status_t ps;
    mcan_get_protocol_status(base, &ps);
    mcan_error_count_t ec;
    mcan_get_error_counter(base, &ec);
    printf("  %s round%u %s id=0x%03X dlc=%u len=%u lec=%u tec=%u rec=%u\r\n",
           s_name[idx], round, ok ? "PASS" : "FAIL",
           (unsigned) (tx.use_ext_id ? (tx.ext_id & 0x1FFFFFFFUL) : tx.std_id),
           (unsigned) tx.dlc, (unsigned) mcan_get_message_size_from_dlc(tx.dlc),
           (unsigned) ps.last_error_code, (unsigned) ec.transmit_error_count,
           (unsigned) ec.receive_error_count);
    return ok;
}

static void run_all(void)
{
    s_pass = 0;
    s_fail = 0;
    printf("\r\n=== mcan_loopback_internal: 4 x MCAN 内部环回 ===\r\n");
    for (uint32_t i = 0; i < CAN_COUNT; i++) {
        printf("[%s]\r\n", s_name[i]);
        for (uint8_t round = 0; round < 4U; round++) {
            board_can_led_write((uint8_t) i, (round & 1U) == 0U, true);
            if (loopback_round(i, round)) {
                s_pass++;
            } else {
                s_fail++;
            }
            board_can_led_write((uint8_t) i, (round & 1U) == 0U, false);
        }
        mcan_deinit(s_base[i]);
    }
    printf("SUMMARY: pass=%u fail=%u %s src_clk=%u Hz\r\n", (unsigned) s_pass, (unsigned) s_fail,
           (s_fail == 0U) ? "ALL PASS" : "FAILED", (unsigned) s_clk_hz);
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("sample   : mcan_loopback_internal\r\n");
    printf("chip     : HPM5E31IPB1, MCAN0..3 @0xF0300000/4000/8000/C000, IRQ %d..%d\r\n",
           (int) IRQn_MCAN0, (int) IRQn_MCAN3);
    printf("clock    : CAN clock = %u Hz (pll1_clk0 / 5)\r\n", (unsigned) s_clk_hz);
    printf("mode     : internal loopback, 4 rounds each (std/ext + classic/FD)\r\n");
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    printf("msg ram  : .ahb_sram @0x%08X, %u words/instance\r\n",
           (unsigned) (uint32_t) s_msg_buf, (unsigned) MCAN_MSG_BUF_SIZE_IN_WORDS);
#endif
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, show mcan loopback info);

static void cmd_test(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    run_all();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 test, cmd_test, run all loopback rounds);

static void cmd_stat(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    for (uint32_t i = 0; i < CAN_COUNT; i++) {
        mcan_protocol_status_t ps;
        mcan_error_count_t ec;
        mcan_get_protocol_status(s_base[i], &ps);
        mcan_get_error_counter(s_base[i], &ec);
        printf("%s: act=%u lec=%u bo=%u ep=%u ew=%u tec=%u rec=%u\r\n", s_name[i],
               (unsigned) ps.activity, (unsigned) ps.last_error_code, (unsigned) ps.in_bus_off_state,
               (unsigned) ps.in_error_passive_state, (unsigned) ps.in_warning_state,
               (unsigned) ec.transmit_error_count, (unsigned) ec.receive_error_count);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, dump MCAN PSR/ECR of 4 instances);

int main(void)
{
    bsp_init();
    printf("\r\n[boot] mcan_loopback_internal\r\n");
    board_can_leds_init();
    board_can_init_pins();
    run_all();
    printf("type 'test' to run again, 'info' for config, 'stat' for PSR/ECR\r\n");
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
