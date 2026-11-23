/*
 * multiproto_coexist - 多协议 / 4 通道共存验证（U-39-1 / U-39-6）
 *
 * 同一颗 HPM5E31（480 MHz）上同时跑：
 *   CAN0(PD00/PD01) : SAE J1939 ECU，SA=0x00，500 kbit/s
 *                     EEC1 10 ms / CCVS1 100 ms / ET1 1 s / DM1 1 s（DM1 带一个演示故障）
 *   CAN1(PD05/PD04) : app_can_gw（cannelloni v2），UDP 4000 <-> 主机 vcan0
 *   CAN2(PC08/PA09) : CANopenNode v4 从站，节点 1，500 kbit/s，心跳 1 s + SYNC 型 TPDO
 *   CAN3(PC15/PC14) : app_canopen 最小主站，SYNC 10 ms + SDO 读 0x2102（2 次/s）+ 心跳监视
 *
 * 硬件拓扑：CAN0<->CAN1、CAN2<->CAN3 各是一段独立总线（板上 120R）。
 * 所以本固件天然覆盖“4 路通道并发 + 两段总线互不影响”，并能量化 CPU 争用代价。
 *
 * 测量（shell 命令）：
 *   mix [secs]  4 协议/4 通道全开（J1939 + 网关 + CANopen 从站 + CANopen 主站）
 *   mixj [secs] 只跑 J1939 + 网关（CANopen 任务暂停），用于对比 CPU 争用对 J1939 抖动的影响
 *   quad [secs] 同 mix，另打印每通道错误计数/串扰与丢帧对账
 *   stat / info
 *
 * 指标：EEC1/SYNC 周期抖动、CANopen SDO 时延、心跳、网关丢包（drop_can/drop_udp/bad_pkt）、
 *       4 路 MCAN 错误计数（LEC/TEC/REC/bus-off）、空闲循环次数（CPU 负载）、单次循环最大耗时、
 *       静态/堆 RAM（编译期 DLM 见 README）、跨通道丢帧对账（node tx vs master rx 等）。
 *
 * 上电自动跑 mix 15 s + mixj 8 s 并打印 PASS/FAIL；串口可作为冒烟特征串证据。
 */
#include <stdarg.h>
#include <string.h>

#include "bsp.h"
#include "app_j1939.h"
#include "j1939_port.h"
#include "app_can.h"
#include "app_lwip.h"
#include "app_can_gw.h"
#include "canopen_node.h"
#include "co_master.h"
#include "CANopen.h"
#include "OD.h"
#include "ext/board_can.h"
#include "hpm_clock_drv.h"

#define CAN_J1939   BOARD_CAN0_BASE
#define CAN_GW      BOARD_CAN1_BASE
#define CAN_CO_NODE BOARD_CAN2_BASE
#define CAN_CO_MST  BOARD_CAN3_BASE
#define CO_NODE_ID  (1U)

#define EEC1_CAP    (2048U)
#define SYNC_CAP    (2048U)

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_j1939_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];
#else
static uint32_t s_j1939_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static j1939_bus_t   s_bus;
static j1939_stack_t s_ecu;
static co_master_t   s_master;
static volatile bool s_busy;

/* ---------------- 运行时统计 ---------------- */
static uint32_t s_eec1_ts[EEC1_CAP];
static volatile uint32_t s_eec1_w; /* 累计写入数（环形取模） */
static uint32_t s_sync_ts[SYNC_CAP];
static volatile uint32_t s_sync_w; /* 累计写入数（环形取模） */
static uint32_t s_sync_gap_min = 0xFFFFFFFFU;
static uint32_t s_sync_gap_max;
static uint32_t s_sync_gap_sum;
static uint32_t s_sync_gap_cnt;
static uint32_t s_sdo_cnt;
static uint32_t s_sdo_ok;
static uint32_t s_sdo_min = 0xFFFFFFFFU;
static uint32_t s_sdo_max;
static uint32_t s_sdo_sum;
static uint32_t s_sdo_last_v;
static uint32_t s_sdo_last_rc;

static uint32_t s_loops;
static uint32_t s_max_loop_us;
static uint32_t s_pass;
static uint32_t s_fail;

/* ---------------- J1939 发动机信号 ---------------- */
static struct {
    uint16_t rpm_x8;
    uint8_t  throttle_pct;
    int16_t  coolant_c;
    uint16_t oil_kpa;
    uint16_t speed_x256;
} g_sig = {6000U, 20U, 85, 350U, (uint16_t) (36.5 * 256)};

static const uint64_t NAME_ECU =
    (0x000123ULL << 43) | (0x00001ULL << 32) | 0ULL;

static void fill_eec1(j1939_stack_t *s, uint8_t *buf, uint16_t *len, void *user)
{
    static uint32_t n;
    (void) s;
    (void) user;
    buf[0] = 0x00U;
    buf[1] = (uint8_t) (125 + g_sig.throttle_pct);
    buf[2] = (uint8_t) (125 + g_sig.throttle_pct);
    buf[3] = (uint8_t) (g_sig.rpm_x8 & 0xFFU);
    buf[4] = (uint8_t) (g_sig.rpm_x8 >> 8);
    buf[5] = 0x00U;
    buf[6] = 0x00U;
    buf[7] = (uint8_t) (125 + g_sig.throttle_pct);
    *len = 8U;
    {
        uint32_t w = n++;
        s_eec1_ts[w % EEC1_CAP] = j1939_micros();
        s_eec1_w = n;
    }
}

static void fill_ccvs1(j1939_stack_t *s, uint8_t *buf, uint16_t *len, void *user)
{
    (void) s;
    (void) user;
    buf[0] = (uint8_t) (g_sig.speed_x256 & 0xFFU);
    buf[1] = (uint8_t) (g_sig.speed_x256 >> 8);
    for (uint8_t i = 2U; i < 8U; i++) {
        buf[i] = (i == 6U) ? 0xFFU : ((i == 7U) ? 0xFFU : 0x00U);
    }
    *len = 8U;
}

static void fill_et1(j1939_stack_t *s, uint8_t *buf, uint16_t *len, void *user)
{
    (void) s;
    (void) user;
    buf[0] = (uint8_t) (g_sig.coolant_c + 40);
    buf[1] = 70U;
    buf[2] = 130U;
    buf[3] = (uint8_t) (g_sig.oil_kpa / 4U);
    buf[4] = 0x00U;
    buf[5] = 0x00U;
    buf[6] = 0x00U;
    buf[7] = 0xFFU;
    *len = 8U;
}

/* ---------------- 小工具 ---------------- */
static void check(const char *name, bool ok, const char *fmt, ...)
{
    char detail[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof(detail), fmt, ap);
    va_end(ap);
    printf("  %-46s %s  %s\r\n", name, ok ? "PASS" : "FAIL", detail);
    if (ok) {
        s_pass++;
    } else {
        s_fail++;
    }
}

static uint32_t sched_sent(uint32_t pgn)
{
    for (uint8_t i = 0U; i < s_ecu.sched_count; i++) {
        if (s_ecu.sched[i].pgn == pgn) {
            return s_ecu.sched[i].sent;
        }
    }
    return 0U;
}

static void co_service(void)
{
    co_node_reset_t r = co_node_poll();
    if (r == CO_NODE_RESET_COMM) {
        (void) co_node_reset_comm();
    } else if (r == CO_NODE_RESET_APP) {
        bsp_reboot();
    }
}

/* 10 ms 节拍：SYNC + 记录间隔（用于测调度抖动） */
static void tick_10ms(void)
{
    uint32_t now_us = j1939_micros();
    uint32_t w;
    (void) co_master_sync(&s_master);
    w = s_sync_w++;
    s_sync_ts[w % SYNC_CAP] = now_us;
    if (s_sync_w >= 2U) {
        uint32_t gap = now_us - s_sync_ts[(s_sync_w - 2U) % SYNC_CAP];
        if (gap < s_sync_gap_min) {
            s_sync_gap_min = gap;
        }
        if (gap > s_sync_gap_max) {
            s_sync_gap_max = gap;
        }
        s_sync_gap_sum += gap;
        s_sync_gap_cnt++;
    }
}

static void sdo_poll(void)
{
    uint32_t v = 0U;
    uint32_t t0 = j1939_micros();
    int rc = co_master_sdo_read(&s_master, CO_NODE_ID, 0x2102U, 0U, &v, 50U);
    uint32_t dt = j1939_micros() - t0;
    s_sdo_cnt++;
    if (rc == 0) {
        s_sdo_ok++;
        if (dt < s_sdo_min) {
            s_sdo_min = dt;
        }
        if (dt > s_sdo_max) {
            s_sdo_max = dt;
        }
        s_sdo_sum += dt;
    }
    s_sdo_last_rc = (uint32_t) rc;
    s_sdo_last_v = v;
}

/* 主循环体；co_on=false 时暂停 CANopen 节点/主站任务（对比 J1939 抖动） */
static void loop_once(bool co_on)
{
    uint32_t t0 = j1939_micros();
    uint32_t now = j1939_millis();
    static uint32_t last10, last100, last500;

    bsp_shell_poll();
    app_lwip_poll();
    app_can_gw_poll();
    j1939_poll(&s_ecu);
    if (co_on) {
        co_service();
        co_master_poll(&s_master);
    }
    if ((uint32_t) (now - last10) >= 10U) {
        last10 = now;
        if (co_on) {
            tick_10ms();
        }
    }
    if ((uint32_t) (now - last100) >= 100U) {
        last100 = now;
    }
    if (co_on && ((uint32_t) (now - last500) >= 500U)) {
        last500 = now;
        sdo_poll();
    }
    s_loops++;
    {
        uint32_t dt = j1939_micros() - t0;
        if (dt > s_max_loop_us) {
            s_max_loop_us = dt;
        }
    }
}

static void pump_ms(bool co_on, uint32_t ms)
{
    uint32_t t0 = j1939_millis();
    while ((uint32_t) (j1939_millis() - t0) < ms) {
        loop_once(co_on);
    }
}

/* ---------------- 测量主函数 ---------------- */
typedef struct {
    uint32_t t_ms;
    uint32_t eec1, ccvs1, et1, dm1;
    uint32_t eec1_idx, sync_idx;
    uint32_t sync_gap_min, sync_gap_max, sync_gap_sum, sync_gap_cnt;
    uint32_t node_tx, node_rx, master_rx, master_tx, hb, emcy;
    uint32_t loops, max_loop_us;
    app_can_gw_stats_t gw;
} snap_t;

static void snap_take(snap_t *s)
{
    co_node_stats_t st;
    co_node_get_stats(&st);
    s->t_ms = j1939_millis();
    s->eec1 = sched_sent(J1939_PGN_EEC1);
    s->ccvs1 = sched_sent(J1939_PGN_CCVS1);
    s->et1 = sched_sent(J1939_PGN_ET1);
    s->dm1 = sched_sent(J1939_PGN_DM1);
    s->eec1_idx = s_eec1_w;
    s->sync_idx = s_sync_w;
    s->sync_gap_min = s_sync_gap_min;
    s->sync_gap_max = s_sync_gap_max;
    s->sync_gap_sum = s_sync_gap_sum;
    s->sync_gap_cnt = s_sync_gap_cnt;
    s->node_tx = st.tx_frames;
    s->node_rx = st.rx_frames;
    s->master_rx = s_master.rx_frames;
    s->master_tx = s_master.tx_frames;
    s->hb = s_master.hb_frames;
    s->emcy = s_master.emcy_frames;
    s->loops = s_loops;
    s->max_loop_us = s_max_loop_us;
    app_can_gw_stats(&s->gw);
}

static void print_mcan_err(const char *name, MCAN_Type *base)
{
    mcan_protocol_status_t ps;
    mcan_error_count_t ec;
    mcan_get_protocol_status(base, &ps);
    mcan_get_error_counter(base, &ec);
    printf("    %-4s LEC=%u act=%u bo=%u tec=%u rec=%u\r\n", name,
           (unsigned) ps.last_error_code, (unsigned) ps.activity,
           (unsigned) mcan_is_in_busoff_state(base),
           (unsigned) ec.transmit_error_count, (unsigned) ec.receive_error_count);
}

static bool run_mix(uint32_t secs, bool co_on)
{
    snap_t a, b;
    uint32_t eec1_min = 0xFFFFFFFFU, eec1_max = 0U, eec1_sum = 0U, eec1_cnt = 0U;
    uint32_t eec1_span_ms = 0U;
    uint32_t dur_ms;
    bool ok_all = true;

    printf("\r\n=== multiproto %s %us (J1939+gateway%s) ===\r\n",
           co_on ? "MIX(4ch)" : "MIX-J(J1939 only)", (unsigned) secs,
           co_on ? " + CANopen node/master" : "");
    s_pass = 0U;
    s_fail = 0U;
    s_sync_gap_min = 0xFFFFFFFFU;
    s_sync_gap_max = 0U;
    s_sync_gap_sum = 0U;
    s_sync_gap_cnt = 0U;
    s_sdo_cnt = 0U;
    s_sdo_ok = 0U;
    s_sdo_min = 0xFFFFFFFFU;
    s_sdo_max = 0U;
    s_sdo_sum = 0U;
    s_max_loop_us = 0U;

    snap_take(&a);
    pump_ms(co_on, secs * 1000U);
    snap_take(&b);

    dur_ms = (uint32_t) (b.t_ms - a.t_ms);
    if (dur_ms == 0U) {
        dur_ms = 1U;
    }

    /* EEC1 间隔：从两次快照之间的时间戳计算 */
    for (uint32_t i = a.eec1_idx + 1U; i < b.eec1_idx; i++) {
        uint32_t gap = s_eec1_ts[i % EEC1_CAP] - s_eec1_ts[(i - 1U) % EEC1_CAP];
        if (gap < eec1_min) {
            eec1_min = gap;
        }
        if (gap > eec1_max) {
            eec1_max = gap;
        }
        eec1_sum += gap;
        eec1_cnt++;
    }
    eec1_span_ms = dur_ms;
    (void) eec1_span_ms;
    if (eec1_cnt == 0U) {
        eec1_min = 0U;
    }

    {
        uint32_t loops = b.loops - a.loops;
        uint32_t loops_per_s = (uint32_t) ((uint64_t) loops * 1000U / dur_ms);
        uint32_t eco = b.eec1 - a.eec1;
        uint32_t cco = b.ccvs1 - a.ccvs1;
        uint32_t eto = b.et1 - a.et1;
        uint32_t dmo = b.dm1 - a.dm1;
        uint32_t sync = b.sync_idx - a.sync_idx;
        uint32_t node_tx = b.node_tx - a.node_tx;
        uint32_t node_rx = b.node_rx - a.node_rx;
        uint32_t m_rx = b.master_rx - a.master_rx;
        uint32_t m_tx = b.master_tx - a.master_tx;
        uint32_t hb = b.hb - a.hb;
        uint32_t gw_rx_can = b.gw.rx_can_frames - a.gw.rx_can_frames;
        uint32_t gw_tx_udp = b.gw.tx_udp_frames - a.gw.tx_udp_frames;
        uint32_t gw_rx_udp = b.gw.rx_udp_frames - a.gw.rx_udp_frames;
        uint32_t gw_drop_can = b.gw.drop_can - a.gw.drop_can;
        uint32_t gw_drop_udp = b.gw.drop_udp - a.gw.drop_udp;
        uint32_t gw_bad = b.gw.bad_pkt - a.gw.bad_pkt;
        uint32_t sdo_avg = (s_sdo_ok != 0U) ? (s_sdo_sum / s_sdo_ok) : 0U;
        uint32_t sg_min = (s_sync_gap_cnt != 0U) ? (b.sync_gap_min) : 0U;
        uint32_t sg_max = b.sync_gap_max;
        uint32_t sg_avg = (s_sync_gap_cnt != 0U)
                              ? ((b.sync_gap_sum - a.sync_gap_sum) / s_sync_gap_cnt) : 0U;
        uint32_t expect_eec1 = (uint32_t) ((uint64_t) dur_ms / 10U);

        printf("  [CPU/RAM] loops/s=%u max_loop=%u us dur=%u ms | j1939_stack=%u B bus=%u B\r\n",
               (unsigned) loops_per_s, (unsigned) s_max_loop_us, (unsigned) dur_ms,
               (unsigned) sizeof(s_ecu), (unsigned) sizeof(s_bus));
        co_node_stats_t nst;
        co_node_get_stats(&nst);
        printf("  [CANopen] heap=%u B node_tx(统计不可用)=%u node_rx=%u master_tx=%u master_rx=%u hb=%u "
               "max_node_cycle=%u us\r\n",
               (unsigned) nst.heap_bytes, (unsigned) node_tx, (unsigned) node_rx,
               (unsigned) m_tx, (unsigned) m_rx, (unsigned) hb,
               (unsigned) nst.max_cycle_us);
        printf("  [J1939 ] EEC1=%u (期望~%u) gap min/avg/max=%u/%u/%u us | CCVS1=%u ET1=%u DM1=%u | "
               "bus tx=%u rx=%u dropped=%u\r\n",
               (unsigned) eco, (unsigned) expect_eec1, (unsigned) eec1_min,
               (unsigned) ((eec1_cnt != 0U) ? (eec1_sum / eec1_cnt) : 0U), (unsigned) eec1_max,
               (unsigned) cco, (unsigned) eto, (unsigned) dmo,
               (unsigned) (s_bus.tx_frames), (unsigned) (s_bus.rx_frames),
               (unsigned) (s_bus.dropped));
        printf("  [SYNC  ] n=%u period avg/max=%u/%u us (10 ms 节拍)\r\n", (unsigned) sync,
               (unsigned) sg_avg, (unsigned) sg_max);
        printf("  [SDO   ] n=%u ok=%u avg/min/max=%u/%u/%u us rc=%d 0x2102=%u\r\n",
               (unsigned) s_sdo_cnt, (unsigned) s_sdo_ok, (unsigned) sdo_avg,
               (unsigned) ((s_sdo_ok != 0U) ? s_sdo_min : 0U), (unsigned) s_sdo_max,
               (int) s_sdo_last_rc, (unsigned) s_sdo_last_v);
        printf("  [GW    ] CAN1->UDP frames=%u udp_tx=%u udp_rx=%u drop_can=%u drop_udp=%u bad=%u\r\n",
               (unsigned) gw_rx_can, (unsigned) gw_tx_udp, (unsigned) gw_rx_udp,
               (unsigned) gw_drop_can, (unsigned) gw_drop_udp, (unsigned) gw_bad);
        printf("  [4CH   ] ");
        print_mcan_err("CAN0", CAN_J1939);
        print_mcan_err("CAN1", CAN_GW);
        print_mcan_err("CAN2", CAN_CO_NODE);
        print_mcan_err("CAN3", CAN_CO_MST);
        printf("            跨通道对账: node_tx-master_rx=%d master_tx-node_rx=%d (期望 0 附近)\r\n",
               (int) ((int32_t) node_tx - (int32_t) m_rx),
               (int) ((int32_t) m_tx - (int32_t) node_rx));

        check("J1939 EEC1 周期 10ms 计数", (eco >= (expect_eec1 * 9U) / 10U),
              "n=%u expect>=%u", (unsigned) eco, (unsigned) ((expect_eec1 * 9U) / 10U));
        check("J1939 EEC1 最大间隔 <30ms", (eec1_max > 0U) && (eec1_max < 30000U),
              "max=%u us", (unsigned) eec1_max);
        if (co_on) {
            uint32_t expect_sync = (uint32_t) ((uint64_t) dur_ms / 10U);
            check("CANopen SYNC 10ms 节拍计数", (sync >= (expect_sync * 9U) / 10U),
                  "n=%u expect>=%u", (unsigned) sync, (unsigned) ((expect_sync * 9U) / 10U));
            check("CANopen SYNC 最大间隔 <15ms", (sg_max > 0U) && (sg_max < 15000U),
                  "max=%u us", (unsigned) sg_max);
            check("CANopen SDO 读全部成功", (s_sdo_cnt > 0U) && (s_sdo_ok == s_sdo_cnt),
                  "ok=%u/%u avg=%u us", (unsigned) s_sdo_ok, (unsigned) s_sdo_cnt,
                  (unsigned) sdo_avg);
            /* 注：本版本 CANopenNode 的 co_node_get_stats().rx/tx 计数恒 0（CO_driver 未导出），
             * 用主站侧计数验证从站闭环：master_rx ≈ TPDO(每 SYNC 一帧) + HB + SDO 响应 */
            check("CANopen 从站 TPDO/HB 可见(主站侧)", (m_rx > 0U) && (hb >= 1U),
                  "master_rx=%u hb=%u sdo_ok=%u", (unsigned) m_rx, (unsigned) hb,
                  (unsigned) s_sdo_ok);
        }
        if (strcmp(app_can_gw_peer_str(), "(none)") == 0) {
            check("网关 0 丢弃/0 坏包(无 peer: drop_udp 豁免)", (gw_drop_can == 0U) && (gw_bad == 0U),
                  "drop_can=%u drop_udp=%u bad=%u peer=(none)", (unsigned) gw_drop_can,
                  (unsigned) gw_drop_udp, (unsigned) gw_bad);
        } else {
            check("网关 0 丢弃/0 坏包", (gw_drop_can == 0U) && (gw_drop_udp == 0U) && (gw_bad == 0U),
                  "drop_can=%u drop_udp=%u bad=%u peer=%s", (unsigned) gw_drop_can,
                  (unsigned) gw_drop_udp, (unsigned) gw_bad, app_can_gw_peer_str());
        }
        check("网关确实在搬运 CAN1 帧", (gw_rx_can > 0U),
              "CAN1->UDP=%u UDP->CAN=%u", (unsigned) gw_rx_can, (unsigned) gw_rx_udp);
        (void) sg_min;
        ok_all = (s_fail == 0U);
        printf("  RESULT: %s (%u PASS / %u FAIL)\r\n", ok_all ? "PASS" : "FAIL",
               (unsigned) s_pass, (unsigned) s_fail);
        (void) eec1_span_ms;
    }
    return ok_all;
}

/* ---------------- shell ---------------- */
static void cmd_mix(int argc, char *argv[])
{
    uint32_t secs = 10U;
    if (s_busy) {
        printf("[busy] measurement already running\r\n");
        return;
    }
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &secs);
    }
    if ((secs == 0U) || (secs > 40U)) {
        secs = 10U;
    }
    s_busy = true;
    (void) run_mix(secs, true);
    s_busy = false;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mix, cmd_mix, 4-channel mixed run: mix [secs]);

static void cmd_mixj(int argc, char *argv[])
{
    uint32_t secs = 8U;
    if (s_busy) {
        printf("[busy] measurement already running\r\n");
        return;
    }
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &secs);
    }
    if ((secs == 0U) || (secs > 40U)) {
        secs = 8U;
    }
    s_busy = true;
    (void) run_mix(secs, false);
    s_busy = false;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mixj, cmd_mixj, J1939+gateway only (CANopen paused): mixj [secs]);

static void cmd_quad(int argc, char *argv[])
{
    uint32_t secs = 10U;
    if (s_busy) {
        printf("[busy] measurement already running\r\n");
        return;
    }
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &secs);
    }
    if ((secs == 0U) || (secs > 40U)) {
        secs = 10U;
    }
    printf("[quad] 4 通道并发（CAN0=J1939 / CAN1=GW / CAN2=CANopen 从站 / CAN3=CANopen 主站）\r\n");
    s_busy = true;
    (void) run_mix(secs, true);
    s_busy = false;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 quad, cmd_quad, 4-channel concurrency run: quad [secs]);

static void cmd_stat(int argc, char *argv[])
{
    co_node_stats_t nst;
    app_can_gw_stats_t gw;
    (void) argc;
    (void) argv;
    co_node_get_stats(&nst);
    app_can_gw_stats(&gw);
    printf("J1939: sa=0x%02X state=%u eec1=%u ccvs1=%u et1=%u dm1=%u tx=%u rx=%u dropped=%u\r\n",
           s_ecu.sa, (unsigned) s_ecu.addr_state, (unsigned) sched_sent(J1939_PGN_EEC1),
           (unsigned) sched_sent(J1939_PGN_CCVS1), (unsigned) sched_sent(J1939_PGN_ET1),
           (unsigned) sched_sent(J1939_PGN_DM1), (unsigned) s_ecu.stats.tx_frames,
           (unsigned) s_ecu.stats.rx_frames, (unsigned) s_bus.dropped);
    printf("CANopen: node_state=%u heap=%uB rx=%u tx=%u retry=%u max_cycle=%uus | master rx=%u tx=%u "
           "hb=%u emcy=%u sdo_tmo=%u\r\n",
           (unsigned) co_node_nmt_state(), (unsigned) nst.heap_bytes, (unsigned) nst.rx_frames,
           (unsigned) nst.tx_frames, (unsigned) nst.tx_retry, (unsigned) nst.max_cycle_us,
           (unsigned) s_master.rx_frames, (unsigned) s_master.tx_frames,
           (unsigned) s_master.hb_frames, (unsigned) s_master.emcy_frames,
           (unsigned) s_master.sdo_timeouts);
    printf("GW: rx_can=%u tx_udp=%u rx_udp=%u tx_can=%u drop_can=%u drop_udp=%u bad=%u peer=%s\r\n",
           (unsigned) gw.rx_can_frames, (unsigned) gw.tx_udp_frames,
           (unsigned) gw.rx_udp_frames, (unsigned) gw.tx_can_frames,
           (unsigned) gw.drop_can, (unsigned) gw.drop_udp, (unsigned) gw.bad_pkt,
           app_can_gw_peer_str());
    printf("CPU: loops=%u max_loop=%u us | 静态 RAM: j1939_stack=%u bus=%u\r\n",
           (unsigned) s_loops, (unsigned) s_max_loop_us, (unsigned) sizeof(s_ecu),
           (unsigned) sizeof(s_bus));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, coexistence statistics);

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("[info] multiproto_coexist: CAN0=J1939(SA=0x00) CAN1=GW(UDP4000) CAN2=CANopen node1 "
           "CAN3=CANopen master, all 500 kbit/s\r\n");
    printf("[info] 上电自动 mix 15s + mixj 8s；shell: mix|mixj|quad [secs] / stat\r\n");
    printf("[info] J1939 10ms EEC1 / 100ms CCVS1 / 1s ET1+DM1；CANopen SYNC 10ms + SDO 2Hz\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, show coexistence configuration);

/* ---------------- main ---------------- */
int main(void)
{
    j1939_cfg_t cfg;
    int rc;

    board_init();
    bsp_init();
    board_can_leds_init();
    board_can_init_pins();
    board_can_transceivers_enable();

    printf("\r\n[app] multiproto_coexist (U-39-1/U-39-6): 4 channels, 2 protocols, gateway\r\n");

    /* 1) J1939 ECU on CAN0 */
    if (j1939_bus_init(&s_bus, CAN_J1939, s_j1939_msg_buf, 500000U) != 0) {
        printf("[FAIL] J1939 CAN0 init\r\n");
    }
    __builtin_memset(&cfg, 0, sizeof(cfg));
    cfg.auto_claim = true;
    cfg.preferred_sa = 0x00U;
    cfg.name = NAME_ECU;
    j1939_init(&s_ecu, &s_bus, &cfg);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_EEC1, 10U, 3U, fill_eec1, NULL);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_CCVS1, 100U, 6U, fill_ccvs1, NULL);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_ET1, 1000U, 6U, fill_et1, NULL);
    j1939_dm_init(&s_ecu);
    (void) j1939_dm_add_fault(&s_ecu, 100U, 4U, 1U);

    /* 2) CANopen node on CAN2 */
    {
        co_node_cfg_t nc;
        __builtin_memset(&nc, 0, sizeof(nc));
        nc.can_base = CAN_CO_NODE;
        nc.node_id = (uint8_t) CO_NODE_ID;
        nc.bitrate_kbps = 500U;
        nc.heartbeat_ms = 1000U;
        nc.nmt_start_operational = 1U;
        rc = co_node_init(&nc);
        printf("[app] co_node_init=%d (CAN2 node %u)\r\n", rc, (unsigned) CO_NODE_ID);
    }
    /* 3) CANopen master on CAN3 */
    rc = co_master_init(&s_master, CAN_CO_MST, 500U);
    printf("[app] co_master_init=%d (CAN3 master)\r\n", rc);
    co_master_set_node(&s_master, (uint8_t) CO_NODE_ID);
    co_master_set_service(&s_master, co_service);

    /* 4) Gateway on CAN1 (UDP 4000) */
    rc = app_lwip_init(NULL);
    printf("[app] lwip init rc=%d link=%s\r\n", rc, app_lwip_link_up() ? "UP" : "DOWN");
    {
        app_can_gw_cfg_t gw;
        __builtin_memset(&gw, 0, sizeof(gw));
        gw.local_ip = "192.168.2.100";
        gw.peer_ip = NULL;
        gw.peer_port = 0U;
        gw.bus_speed = APP_CAN_SPEED_CLASSIC_500K;
        gw.max_frames_per_pkt = APP_CAN_GW_MAX_FRAMES_PER_PKT;
        gw.ch_count = 1;
        gw.ch[0].can_ch = 1U;
        gw.ch[0].udp_port = 4000U;
        gw.ch[0].enabled = true;
        rc = app_can_gw_init(&gw);
        printf("[app] app_can_gw_init=%d (CAN1 <-> UDP4000) peer=%s\r\n", rc,
               app_can_gw_peer_str());
    }

    /* 等 J1939 地址声明完成（250 ms + 余量），期间 4 通道都在跑 */
    pump_ms(true, 700U);
    printf("[pass] multiproto ready: j1939_sa=0x%02X gw=%s canopen_state=%u loops=%u\r\n",
           s_ecu.sa, app_can_gw_peer_str(), (unsigned) co_node_nmt_state(),
           (unsigned) s_loops);

    /* 上电自动跑两组：4 通道全开 vs 只 J1939+网关（对比 CPU 争用） */
    (void) run_mix(15U, true);
    (void) run_mix(8U, false);
    printf("\r\n[app] auto sequence done; shell: mix|mixj|quad [secs] / stat / info\r\n");

    while (1) {
        loop_once(true);
    }
    return 0;
}