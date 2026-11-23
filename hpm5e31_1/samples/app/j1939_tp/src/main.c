/*
 * j1939_tp - J1939-21 传输协议实测：BAM 广播 与 RTS/CTS 点对点（CAN0 <-> CAN1）
 *
 * 硬件：CAN0(PD00/PD01) 与 CAN1(PD05/PD04) 同一段 250 kbit/s 总线。
 *       CAN0 = 发送节点 SA=0x00，CAN1 = 接收节点 SA=0xF9（板内双节点，Bench A）。
 *
 * 测试内容：
 *   1) BAM 100 B / 500 B：PGN 0xEF00 广播；接收方按序号重组、CRC32 校验；
 *      统计包间隔（预期 50 ms）与有效吞吐（B/s）。
 *   2) RTS/CTS 100 B / 500 B：PGN 0xEF01 点对点（DA=0xF9），走 RTS→CTS→DT→EOMA，
 *      测量端到端时间、EOMA 往返、包间隔（默认 cts_gap=1 ms，与 BAM 50 ms 对比）。
 *   3) 反向 500 B：SA=0xF9 -> SA=0x00，验证双向会话与 SA 路由。
 *   4) 丢包/超时：BAM 传输中让接收方停摆 900 ms（模拟丢包），验证接收会话被判错，
 *      随后再传 100 B 正常，证明会话可恢复。
 *
 * 现象：上电自动跑 bench 并打印 PASS/FAIL；shell: bench | bam <n> | rts <n> | rev <n> |
 *       drop | stat | info
 */
#include <stdarg.h>
#include <string.h>
#include "bsp.h"
#include "app_j1939.h"
#include "j1939_internal.h"
#include "j1939_port.h"
#include "ext/board_can.h"

#define SA_A (0x00U)
#define SA_B (0xF9U)
#define PGN_BAM (0xEF00U)
#define PGN_RTS (0xEF01U)

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_buf_a[MCAN_MSG_BUF_SIZE_IN_WORDS];
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_buf_b[MCAN_MSG_BUF_SIZE_IN_WORDS];
#else
static uint32_t s_buf_a[MCAN_MSG_BUF_SIZE_IN_WORDS];
static uint32_t s_buf_b[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static j1939_bus_t   s_bus_a;
static j1939_bus_t   s_bus_b;
static j1939_stack_t s_a;
static j1939_stack_t s_b;

static uint8_t s_payload[J1939_TP_MAX_LEN];

/* 单向校验上下文：dir 0 = A->B，1 = B->A */
static struct {
    uint16_t len;
    uint32_t crc;
    uint32_t rx_ok;
    uint32_t rx_bad;
    uint32_t last_us;
    uint16_t last_len;
} g_exp[2];

static uint32_t crc32_calc(const uint8_t *p, uint16_t n)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint16_t i = 0U; i < n; i++) {
        crc ^= p[i];
        for (uint8_t k = 0U; k < 8U; k++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t) (-(int32_t) (crc & 1U)));
        }
    }
    return ~crc;
}

static void fill_payload(uint16_t len)
{
    for (uint16_t i = 0U; i < len; i++) {
        s_payload[i] = (uint8_t) (i * 31U + 7U);
    }
}

static uint32_t g_pass, g_fail;

static void report(bool ok, const char *fmt, ...)
{
    printf(ok ? "[PASS] " : "[FAIL] ");
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
    }
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\r\n");
}

/* 接收回调：A->B 与 B->A 都到达各自的栈，按源地址区分方向 */
static void tp_rx_cb(j1939_stack_t *s, uint32_t pgn, uint8_t sa, uint8_t da,
                     const uint8_t *data, uint16_t len, void *user)
{
    uint8_t dir;
    (void) da;
    (void) user;
    if ((pgn != PGN_BAM) && (pgn != PGN_RTS)) {
        return;
    }
    dir = (s == &s_b) ? 0U : 1U; /* B 收到的是 A->B */
    g_exp[dir].last_us = j1939_micros();
    g_exp[dir].last_len = len;
    if ((len == g_exp[dir].len) && (crc32_calc(data, len) == g_exp[dir].crc) && (data[0] == 7U)) {
        g_exp[dir].rx_ok++;
    } else {
        g_exp[dir].rx_bad++;
    }
}

static void pump_a_only(uint32_t us)
{
    uint32_t t0 = j1939_micros();
    while ((j1939_micros() - t0) < us) {
        j1939_poll(&s_a);
        bsp_shell_poll();
    }
}

static void pump_both_until(volatile uint32_t *counter, uint32_t target, uint32_t timeout_us)
{
    uint32_t t0 = j1939_micros();
    while ((*counter < target) && ((j1939_micros() - t0) < timeout_us)) {
        j1939_poll(&s_a);
        j1939_poll(&s_b);
        bsp_shell_poll();
    }
}

static void drain_ms(uint32_t ms)
{
    uint32_t t0 = j1939_millis();
    while ((j1939_millis() - t0) < ms) {
        j1939_poll(&s_a);
        j1939_poll(&s_b);
        bsp_shell_poll();
    }
}

static void reset_gap_stats(j1939_stack_t *s)
{
    s->stats.tp_gap_min_us = 0U;
    s->stats.tp_gap_max_us = 0U;
    s->stats.tp_gap_sum_us = 0U;
    s->stats.tp_gap_n = 0U;
}

/* 正式实现：sender 固定为 s_a；reverse=true 时 s_b 发、s_a 收 */
static bool one_transfer(bool bam, uint16_t len, bool reverse)
{
    j1939_stack_t *snd = reverse ? &s_b : &s_a;
    j1939_stack_t *rcv = reverse ? &s_a : &s_b;
    uint8_t dir = reverse ? 1U : 0U;
    uint32_t pgn = bam ? PGN_BAM : PGN_RTS;
    uint8_t da = bam ? 0xFFU : (reverse ? SA_A : SA_B);
    uint32_t ok_before = g_exp[dir].rx_ok;
    uint32_t t0, dt;
    uint16_t npackets = (uint16_t) ((len + 6U) / 7U);
    uint32_t timeout_us = 2000000U + (uint32_t) npackets * 70000U;

    j1939_tp_reset(snd);
    j1939_tp_reset(rcv);
    drain_ms(5U); /* 清掉上一次传输残留的 EOMA/CTS，避免误判新会话 */
    fill_payload(len);
    g_exp[dir].len = len;
    g_exp[dir].crc = crc32_calc(s_payload, len);
    g_exp[dir].rx_bad = 0U;
    reset_gap_stats(rcv);
    rcv->stats.tp_errors = 0U;
    rcv->stats.tp_timeouts = 0U;
    rcv->stats.tp_gap_n = 0U;

    t0 = j1939_micros();
    if (j1939_send(snd, pgn, da, 6U, s_payload, len) != 0) {
        printf("  send failed (no TP session?)\r\n");
        return false;
    }
    pump_both_until(&g_exp[dir].rx_ok, ok_before + 1U, timeout_us);
    if (g_exp[dir].rx_ok <= ok_before) {
        printf("  detail: snd(tx: bam=%u rts=%u done=%u) rcv(rx: bam=%u rts=%u done=%u err=%u tmo=%u abort=%u)\r\n",
               snd->stats.tp_bam_tx, snd->stats.tp_rts_tx, snd->stats.tp_done_tx,
               rcv->stats.tp_bam_rx, rcv->stats.tp_rts_rx, rcv->stats.tp_done_rx,
               rcv->stats.tp_errors, rcv->stats.tp_timeouts, rcv->stats.tp_aborts);
        return false;
    }
    dt = g_exp[dir].last_us - t0;

    {
        uint32_t avg_gap = (rcv->stats.tp_gap_n != 0U)
                               ? (rcv->stats.tp_gap_sum_us / rcv->stats.tp_gap_n) : 0U;
        double bytes_per_s = (double) len * 1000000.0 / (double) ((dt != 0U) ? dt : 1U);
        printf("  %s %uB: %u pkts, time=%u us, %.1f B/s, gap min/avg/max=%u/%u/%u us, "
               "err=%u timeout=%u\r\n",
               bam ? "BAM " : "RTS/CTS", len, npackets, dt, bytes_per_s,
               rcv->stats.tp_gap_min_us, avg_gap, rcv->stats.tp_gap_max_us,
               rcv->stats.tp_errors, rcv->stats.tp_timeouts);
    }
    drain_ms(5U);
    return true;
}

static volatile bool s_bench_running;

static bool run_bench(void)
{
    bool ok_all = true;
    if (s_bench_running) {
        printf("[busy] bench already running, command ignored\r\n");
        return false;
    }
    s_bench_running = true;
    printf("\r\n=== j1939_tp bench (250 kbit/s, CAN0 SA=0x00 <-> CAN1 SA=0xF9) ===\r\n");
    g_pass = 0U;
    g_fail = 0U;

    printf("[1] BAM 100 B (broadcast PGN 0x%X)\r\n", PGN_BAM);
    bool o = one_transfer(true, 100U, false);
    report(o, "BAM 100B reassembled + CRC32 OK");
    ok_all &= o;

    printf("[2] BAM 500 B\r\n");
    o = one_transfer(true, 500U, false);
    report(o, "BAM 500B reassembled + CRC32 OK");
    ok_all &= o;

    printf("[3] RTS/CTS 100 B (PGN 0x%X, DA=0x%02X)\r\n", PGN_RTS, SA_B);
    o = one_transfer(false, 100U, false);
    report(o, "RTS/CTS 100B reassembled + CRC32 OK");
    ok_all &= o;

    printf("[4] RTS/CTS 500 B\r\n");
    o = one_transfer(false, 500U, false);
    report(o, "RTS/CTS 500B reassembled + CRC32 OK");
    ok_all &= o;

    printf("[5] reverse RTS/CTS 500 B (SA=0xF9 -> SA=0x00)\r\n");
    o = one_transfer(false, 500U, true);
    report(o, "reverse RTS/CTS 500B reassembled + CRC32 OK");
    ok_all &= o;

    /* [6] 丢包/超时恢复 */
    printf("[6] drop/timeout: receiver stalled 2.6 s during BAM 300B (> total BAM duration)\r\n");
    {
        uint32_t done_before;
        fill_payload(300U);
        g_exp[0].len = 300U;
        g_exp[0].crc = crc32_calc(s_payload, 300U);
        j1939_tp_reset(&s_a);
        j1939_tp_reset(&s_b);
        drain_ms(5U);
        s_b.stats.tp_timeouts = 0U;
        s_b.stats.tp_errors = 0U;
        done_before = s_b.stats.tp_done_rx;
        bool sent = (j1939_send(&s_a, PGN_BAM, 0xFFU, 6U, s_payload, 300U) == 0);
        pump_a_only(2600000U);         /* 只有发送方在跑：整帧传输期间接收方"停摆" */
        pump_both_until(&g_exp[0].rx_ok, g_exp[0].rx_ok + 1U, 2000000U);
        bool dropped = (s_b.stats.tp_done_rx == done_before);
        bool err_seen = ((s_b.stats.tp_errors + s_b.stats.tp_timeouts) > 0U);
        report(sent && dropped && err_seen,
               "stalled receiver: not reassembled (done_rx=%u), timeout/err seen=%u",
               s_b.stats.tp_done_rx - done_before,
               s_b.stats.tp_errors + s_b.stats.tp_timeouts);
        ok_all &= (sent && dropped && err_seen);
        o = one_transfer(true, 100U, false);
        report(o, "recovery after drop: BAM 100B OK");
        ok_all &= o;
    }

    s_bench_running = false;
    printf("\r\n=== RESULT: %s (%u PASS / %u FAIL) ===\r\n",
           (g_fail == 0U) ? "PASS" : "FAIL", g_pass, g_fail);
    return ok_all;
}

/* ---------------- shell ---------------- */

static void cmd_bench(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    run_bench();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 bench, cmd_bench, run BAM/RTS-CTs/loss bench);

static void cmd_bam(int argc, char *argv[])
{
    uint32_t n = 500U;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    if (n > J1939_TP_MAX_LEN) {
        n = J1939_TP_MAX_LEN;
    }
    bool o = one_transfer(true, (uint16_t) n, false);
    report(o, "BAM %uB reassembled + CRC32 OK", (unsigned) n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 bam, cmd_bam, BAM transfer: bam [bytes]);

static void cmd_rts(int argc, char *argv[])
{
    uint32_t n = 500U;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    if (n > J1939_TP_MAX_LEN) {
        n = J1939_TP_MAX_LEN;
    }
    bool o = one_transfer(false, (uint16_t) n, false);
    report(o, "RTS/CTS %uB reassembled + CRC32 OK", (unsigned) n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 rts, cmd_rts, RTS/CTS transfer: rts [bytes]);

static void cmd_rev(int argc, char *argv[])
{
    uint32_t n = 500U;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    if (n > J1939_TP_MAX_LEN) {
        n = J1939_TP_MAX_LEN;
    }
    bool o = one_transfer(false, (uint16_t) n, true);
    report(o, "reverse RTS/CTS %uB reassembled + CRC32 OK", (unsigned) n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 rev, cmd_rev, reverse RTS/CTS: rev [bytes]);

static void cmd_stat(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    j1939_stats_t *a = &s_a.stats;
    j1939_stats_t *b = &s_b.stats;
    printf("A(SA=0x%02X): bam_tx=%u rts_tx=%u done_tx=%u bytes_tx=%u eoma min/avg/max=%u/%u/%u us\r\n",
           s_a.sa, a->tp_bam_tx, a->tp_rts_tx, a->tp_done_tx, a->tp_bytes_tx,
           a->eoma_rtt_min_us,
           (a->eoma_rtt_n != 0U) ? (a->eoma_rtt_sum_us / a->eoma_rtt_n) : 0U,
           a->eoma_rtt_max_us);
    printf("B(SA=0x%02X): bam_rx=%u rts_rx=%u done_rx=%u bytes_rx=%u gap min/avg/max=%u/%u/%u us "
           "err=%u timeout=%u abort=%u\r\n",
           s_b.sa, b->tp_bam_rx, b->tp_rts_rx, b->tp_done_rx, b->tp_bytes_rx,
           b->tp_gap_min_us,
           (b->tp_gap_n != 0U) ? (b->tp_gap_sum_us / b->tp_gap_n) : 0U,
           b->tp_gap_max_us, b->tp_errors, b->tp_timeouts, b->tp_aborts);
    printf("A rx_ok/bad=%u/%u  B rx_ok/bad=%u/%u\r\n", g_exp[1].rx_ok, g_exp[1].rx_bad,
           g_exp[0].rx_ok, g_exp[0].rx_bad);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, dump TP stats);

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("j1939_tp: CAN0 SA=0x%02X <-> CAN1 SA=0x%02X, 250 kbit/s, BAM gap=%u ms, CTS gap=%u ms\r\n",
           SA_A, SA_B, s_a.cfg.bam_gap_ms, s_a.cfg.cts_gap_ms);
    printf("TP max payload=%u B (255 x 7), T1=%u T2=%u T3=%u T4=%u ms\r\n",
           J1939_TP_MAX_LEN, J1939_T1_MS, J1939_T2_MS, J1939_T3_MS, J1939_T4_MS);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, show TP config);

int main(void)
{
    j1939_cfg_t cfg;
    bsp_init();
    printf("\r\n[boot] j1939_tp: CAN0 SA=0x00 <-> CAN1 SA=0xF9 @250k\r\n");
    board_can_leds_init();
    board_can_init_pins();
    board_can_transceivers_enable();

    if (j1939_bus_init(&s_bus_a, BOARD_CAN0_BASE, s_buf_a, 250000U) != 0) {
        printf("[FAIL] CAN0 init\r\n");
    }
    if (j1939_bus_init(&s_bus_b, BOARD_CAN1_BASE, s_buf_b, 250000U) != 0) {
        printf("[FAIL] CAN1 init\r\n");
    }
    __builtin_memset(&cfg, 0, sizeof(cfg));
    cfg.auto_claim = false; /* 地址声明在 j1939_ecu 例程覆盖，这里直接固定 SA */
    cfg.preferred_sa = SA_A;
    j1939_init(&s_a, &s_bus_a, &cfg);
    cfg.preferred_sa = SA_B;
    j1939_init(&s_b, &s_bus_b, &cfg);
    j1939_set_rx_cb(&s_a, tp_rx_cb, NULL);
    j1939_set_rx_cb(&s_b, tp_rx_cb, NULL);

    run_bench();
    printf("type 'bench|bam <n>|rts <n>|rev <n>|stat|info'\r\n");
    while (1) {
        bsp_shell_poll();
        j1939_poll(&s_a);
        j1939_poll(&s_b);
    }
    return 0;
}