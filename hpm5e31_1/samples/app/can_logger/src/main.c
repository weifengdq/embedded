/*
 * can_logger - CAN 帧记录 / candump 格式导出 / 回放顺序校验
 *
 * 硬件: hpm5e31_1，CAN0(PD00/PD01) <-> CAN1(PD05/PD04) 同一段总线。
 *   - CAN0: 记录通道（环型缓冲，默认 512 帧，每帧含 1us 单位时间戳）
 *   - CAN1: 回放注入通道（按 seq 发 0x300 帧，CAN0 侧记录并校验顺序/丢帧）
 *
 * 命令:
 *   log start | log stop | log clear | log stat | log dump [n]
 *   gen on [rate] | gen off        CAN1 周期发 0x300 计数帧（含 seq）
 *   replay [n] [gap_us]            CAN1 按序回放 n 帧（0x300，seq=0..n-1）
 *   verify                        校验记录的 0x300 帧 seq 单调连续（回放顺序 PASS）
 *   stat                          统计
 *
 * candump 格式导出（可直接被 host/canlog/candump_parse.py 解析）：
 *   (1700000000.123456) can0 300#01000000A5A5A5A5
 *   时间戳 = bsp_uptime_ms()/1000 秒 + 帧内 1us 余数，日期基准记为 2026-10-30（仅用于相对顺序分析）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp.h"
#include "hpm_mcan_drv.h"
#include "hpm_csr_drv.h"
#include "ext/board_can.h"
#include "app_lwip.h"
#include "app_can_gw.h"

#define LOG_CAN_DEV   BOARD_CAN0_BASE /* 记录通道 */
#define GEN_CAN_DEV   BOARD_CAN1_BASE /* 回放/生成通道 */
#define LOG_CAP       512U            /* 环形缓冲帧数（512 x 32B = 16 KB RAM） */
#define ID_GEN        0x300U
#define ID_SELFTEST   0x301U /* 上电自检用，避免污染 0x300 回放校验 */
#define ID_LOG_MAX    0x7FFU

typedef struct {
    uint32_t ts_us;   /* 自启动起的 us（mcycle） */
    uint16_t id;      /* 11 位 ID */
    uint8_t  dlc;
    uint8_t  flags;   /* bit0: 扩展帧（本板 CANopen/J1939 之外的一般帧不用） */
    uint8_t  data[8];
} log_frame_t;

static log_frame_t s_ring[LOG_CAP];
static uint32_t s_head;      /* 下一个写入位置 */
static uint32_t s_count;     /* 已记录总数 */
static uint32_t s_dropped;   /* 覆盖丢弃数 */
static uint32_t s_logging;
static uint32_t s_gen_on;
static uint32_t s_gen_period_ms = 10U;
static uint32_t s_gen_seq;

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_msg_buf_log[MCAN_MSG_BUF_SIZE_IN_WORDS];
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_msg_buf_gen[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static const mcan_filter_elem_t s_accept_all_std = {
    MCAN_FILTER_TYPE_CLASSIC_FILTER, MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO0_IF_MATCH, 0U, 0U,
    .filter_id = 0U, .filter_mask = 0U};

static const mcan_filter_elem_t s_accept_gen = {
    MCAN_FILTER_TYPE_CLASSIC_FILTER, MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO0_IF_MATCH, 0U, 0U,
    .filter_id = ID_GEN, .filter_mask = 0x7FFU};

static uint32_t s_cpu_mhz;

static uint32_t now_us(void)
{
    return (uint32_t) (hpm_csr_get_core_mcycle() / (uint64_t) s_cpu_mhz);
}

static int can_hw_init(MCAN_Type *base, uint32_t *msg_buf, const mcan_filter_elem_t *filter, uint32_t n_filter)
{
    mcan_config_t cfg;
    uint32_t clk;
    uint32_t presc;

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    mcan_msg_buf_attr_t attr = {(uint32_t) msg_buf, MCAN_MSG_BUF_SIZE_IN_WORDS * 4U};
    mcan_set_msg_buf_attr(base, &attr);
#endif
    mcan_get_default_config(base, &cfg);
    cfg.mode = mcan_mode_normal;
    cfg.enable_canfd = false;
    cfg.use_lowlevel_timing_setting = true;
    clk = board_init_can_clock(base);
    presc = clk / (500U * 1000U * 16U);
    if (presc == 0U) {
        presc = 1U;
    }
    cfg.can_timing.prescaler = (uint16_t) presc;
    cfg.can_timing.num_seg1 = 13U;
    cfg.can_timing.num_seg2 = 2U;
    cfg.can_timing.num_sjw = 2U;
    cfg.all_filters_config.std_id_filter_list.filter_elem_list = filter;
    cfg.all_filters_config.std_id_filter_list.mcan_filter_elem_count = n_filter;
    cfg.all_filters_config.ext_id_filter_list.filter_elem_list = NULL;
    cfg.all_filters_config.ext_id_filter_list.mcan_filter_elem_count = 0U;
    cfg.all_filters_config.global_filter_config.accept_non_matching_std_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;
    cfg.all_filters_config.global_filter_config.accept_non_matching_ext_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_REJECT;
    cfg.all_filters_config.global_filter_config.reject_remote_std_frame = true;
    cfg.all_filters_config.global_filter_config.reject_remote_ext_frame = true;
    board_init_can(base);
    mcan_deinit(base);
    return (mcan_init(base, &cfg, clk) == status_success) ? 0 : -1;
}

static int gen_send(uint32_t seq)
{
    mcan_tx_frame_t tx;
    __builtin_memset(&tx, 0, sizeof(tx));
    tx.std_id = ID_GEN;
    tx.dlc = 8U;
    tx.data_8[0] = (uint8_t) (seq & 0xFFU);
    tx.data_8[1] = (uint8_t) ((seq >> 8) & 0xFFU);
    tx.data_8[2] = (uint8_t) ((seq >> 16) & 0xFFU);
    tx.data_8[3] = (uint8_t) ((seq >> 24) & 0xFFU);
    tx.data_8[4] = 0xA5U;
    tx.data_8[5] = 0x5AU;
    tx.data_8[6] = 0xC3U;
    tx.data_8[7] = 0x3CU;
    return (mcan_transmit_via_txfifo_nonblocking(GEN_CAN_DEV, &tx, NULL) == status_success) ? 0 : -1;
}

static void ring_push(const mcan_rx_message_t *rx)
{
    log_frame_t *f = &s_ring[s_head];
    uint8_t i;

    f->ts_us = now_us();
    f->id = (uint16_t) (rx->std_id & ID_LOG_MAX);
    f->dlc = (uint8_t) ((rx->dlc > 8U) ? 8U : rx->dlc);
    f->flags = (rx->use_ext_id != 0U) ? 1U : 0U;
    for (i = 0U; i < 8U; i++) {
        f->data[i] = rx->data_8[i];
    }
    s_head = (s_head + 1U) % LOG_CAP;
    if (s_count >= LOG_CAP) {
        s_dropped++;
    } else {
        s_count++;
    }
}

static void log_poll(void)
{
    mcan_rx_message_t rx;

    if (s_logging == 0U) {
        return;
    }
    while (mcan_read_rxfifo(LOG_CAN_DEV, 0U, &rx) == status_success) {
        if ((rx.use_ext_id == 0U) && (rx.canfd_frame == 0U)) {
            ring_push(&rx);
        }
    }
}

static uint32_t ring_index(uint32_t i)
{
    /* i = 0 是最旧一帧 */
    if (s_count < LOG_CAP) {
        return i;
    }
    return (s_head + i) % LOG_CAP;
}

/* ---------------- shell ---------------- */
/* ---------------- Bench B：CAN1 交给 cannelloni 网关 ---------------- */
static bool s_gw_on;

static int gw_start(void)
{
    app_can_gw_cfg_t gw;
    int rc_gw;

    /* 释放 MCAN1（回放注入通道），交给 app_can_gw 重新初始化 */
    mcan_deinit(GEN_CAN_DEV);
    (void) app_lwip_init(NULL);
    memset(&gw, 0, sizeof(gw));
    gw.local_ip = "192.168.2.100";
    gw.peer_ip = "192.168.2.1"; /* 纯接收/发送都要显式 peer（auto-learn 需要主机先发） */
    gw.peer_port = 4000U;
    gw.bus_speed = APP_CAN_SPEED_CLASSIC_500K;
    gw.max_frames_per_pkt = APP_CAN_GW_MAX_FRAMES_PER_PKT;
    gw.ch_count = 1;
    gw.ch[0].can_ch = 1U;
    gw.ch[0].udp_port = 4000U;
    gw.ch[0].enabled = true;
    rc_gw = app_can_gw_init(&gw);
    printf("[gw] CAN1 <-> UDP4000 rc=%d\r\n", rc_gw);
    if (rc_gw == 0) {
        app_can_gw_set_forward(true);
        s_gw_on = true;
        /* 清空环形缓冲并停掉 CAN1 周期生成器：回放校验要求唯一 0x300 源。
         * 注意：还要把 CAN0 RX FIFO 里"切模式前残留的 gen 帧"排空，否则它们会在
         * 清空之后被 log_poll() 重新写入（首次实测 verify 看到 20 个旧 seq 就是这个原因）。 */
        s_gen_on = 0U;
        {
            mcan_rx_message_t rx;
            while (mcan_read_rxfifo(LOG_CAN_DEV, 0U, &rx) == status_success) {
                /* discard stale frames */
            }
        }
        s_head = 0U;
        s_count = 0U;
        s_dropped = 0U;
        s_logging = 1U;
        printf("[gw] forwarding ON, CAN0 记录中（ring 已清空，等待主机 canplayer 回放）\r\n");
    }
    return rc_gw;
}

static void cmd_gw(int argc, char *argv[])
{
    const char *a = (argc >= 2) ? argv[1] : NULL;
    if ((a != NULL) && (strcmp(a, "on") == 0)) {
        (void) gw_start();
    } else if ((a != NULL) && (strcmp(a, "off") == 0)) {
        app_can_gw_set_forward(false);
        s_gw_on = false;
        printf("[gw] forwarding OFF\r\n");
    } else {
        printf("usage: gw on|off\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gw, cmd_gw, Bench B: switch CAN1 to cannelloni gateway: gw on|off);

static void cmd_log(int argc, char *argv[])
{
    const char *a = (argc >= 2) ? argv[1] : NULL;

    if ((a != NULL) && (strcmp(a, "start") == 0)) {
        s_logging = 1U;
        printf("log: start (cap=%u frames)\r\n", (unsigned) LOG_CAP);
    } else if ((a != NULL) && (strcmp(a, "stop") == 0)) {
        s_logging = 0U;
        printf("log: stop, count=%u dropped=%u\r\n", (unsigned) s_count, (unsigned) s_dropped);
    } else if ((a != NULL) && (strcmp(a, "clear") == 0)) {
        s_head = 0U;
        s_count = 0U;
        s_dropped = 0U;
        printf("log: cleared\r\n");
    } else if ((a != NULL) && (strcmp(a, "stat") == 0)) {
        printf("log: %s count=%u cap=%u dropped=%u uptime=%u ms\r\n", s_logging ? "RUN" : "STOP",
               (unsigned) s_count, (unsigned) LOG_CAP, (unsigned) s_dropped, (unsigned) bsp_uptime_ms());
    } else if ((a != NULL) && (strcmp(a, "dump") == 0)) {
        uint32_t n = (argc >= 3) ? (uint32_t) atoi(argv[2]) : 0U;
        uint32_t total = s_count;
        uint32_t start = 0U;
        if ((n != 0U) && (n < total)) {
            start = total - n;
        }
        printf("# candump: %u frames (start=%u) epoch_base=1793000000\r\n", (unsigned) (total - start),
               (unsigned) start);
        for (uint32_t i = start; i < total; i++) {
            const log_frame_t *f = &s_ring[ring_index(i)];
            /* 注意：newlib-nano 的 printf 不支持 %llu，这里用 32 位秒（1793000000+uptime 足够） */
            uint32_t epoch_s = 1793000000UL + (f->ts_us / 1000000U);
            uint32_t frac_us = f->ts_us % 1000000U;
            printf("(%u.%06u) can0 %03X#", (unsigned) epoch_s, (unsigned) frac_us, (unsigned) f->id);
            for (uint8_t b = 0U; b < f->dlc; b++) {
                printf("%02X", f->data[b]);
            }
            printf("\r\n");
        }
        printf("# end candump\r\n");
    } else {
        printf("usage: log start|stop|clear|stat|dump [n]\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 log, cmd_log, CAN frame ring buffer: log start|stop|clear|stat|dump [n]);

static void cmd_gen(int argc, char *argv[])
{
    uint32_t n = 0U;
    const char *a = (argc >= 2) ? argv[1] : NULL;

    if ((a != NULL) && (strcmp(a, "on") == 0)) {
        if (argc >= 3) {
            (void) bsp_parse_u32(argv[2], &n);
        }
        s_gen_on = 1U;
        s_gen_period_ms = (n == 0U) ? 10U : n;
        printf("gen: on period=%u ms\r\n", (unsigned) s_gen_period_ms);
    } else if ((a != NULL) && (strcmp(a, "off") == 0)) {
        s_gen_on = 0U;
        printf("gen: off sent=%u\r\n", (unsigned) s_gen_seq);
    } else {
        printf("usage: gen on [period_ms] | gen off\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gen, cmd_gen, generate 0x300 counter frames on CAN1: gen on [period_ms]|off);

static int cmd_verify(int argc, char *argv[]);

static void cmd_replay(int argc, char *argv[])
{
    uint32_t n = 20U;
    uint32_t gap_us = 200U;
    uint32_t ok = 0U;
    uint32_t fail = 0U;

    if (argc >= 2) {
        (void) bsp_parse_u32(argv[1], &n);
    }
    if (argc >= 3) {
        (void) bsp_parse_u32(argv[2], &gap_us);
    }
    if (n == 0U || n > LOG_CAP) {
        n = LOG_CAP;
    }
    /* 回放校验要求单一 0x300 源：先停周期生成器并清空环形缓冲 */
    s_gen_on = 0U;
    s_head = 0U;
    s_count = 0U;
    s_dropped = 0U;
    s_logging = 1U;
    log_poll();
    for (uint32_t i = 0U; i < n; i++) {
        if (gen_send(i) == 0) {
            ok++;
        } else {
            fail++;
        }
        uint32_t t0 = now_us();
        while ((uint32_t) (now_us() - t0) < gap_us) {
            log_poll();
        }
    }
    uint32_t t0 = now_us();
    while ((uint32_t) (now_us() - t0) < 3000U) {
        log_poll();
    }
    printf("replay: sent=%u ok=%u fail=%u gap=%u us\r\n", (unsigned) n, (unsigned) ok, (unsigned) fail,
           (unsigned) gap_us);
    (void) cmd_verify(0, NULL);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 replay, cmd_replay, replay N frames on CAN1 (seq 0..N-1): replay [n] [gap_us]);

static int cmd_verify(int argc, char *argv[])
{
    uint32_t expect = 0U;
    uint32_t got = 0U;
    uint32_t bad = 0U;
    uint32_t first_bad = 0xFFFFFFFFU;
    (void) argc;
    (void) argv;

    for (uint32_t i = 0U; i < s_count; i++) {
        const log_frame_t *f = &s_ring[ring_index(i)];
        if (f->id != ID_GEN || f->dlc < 4U) {
            continue;
        }
        uint32_t seq = (uint32_t) f->data[0] | ((uint32_t) f->data[1] << 8) | ((uint32_t) f->data[2] << 16)
                       | ((uint32_t) f->data[3] << 24);
        if (seq != expect) {
            bad++;
            if (first_bad == 0xFFFFFFFFU) {
                first_bad = i;
            }
        }
        expect = seq + 1U;
        got++;
    }
    printf("verify: 0x300 frames=%u bad_order=%u first_bad_idx=%d result=%s\r\n", (unsigned) got,
           (unsigned) bad, (first_bad == 0xFFFFFFFFU) ? -1 : (int) first_bad, (bad == 0U) ? "PASS" : "FAIL");
    return (bad == 0U) ? 0 : 1;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 verify, cmd_verify, verify recorded 0x300 seq order);

static void cmd_stat(void)
{
    printf("stat: log=%s count=%u dropped=%u gen_on=%u gen_seq=%u uptime=%u ms\r\n", s_logging ? "RUN" : "STOP",
           (unsigned) s_count, (unsigned) s_dropped, (unsigned) s_gen_on, (unsigned) s_gen_seq,
           (unsigned) bsp_uptime_ms());
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, CAN logger statistics);

static void gen_tick(void)
{
    static uint32_t last_ms;
    uint32_t now = bsp_uptime_ms();

    if ((s_gen_on != 0U) && ((now - last_ms) >= s_gen_period_ms)) {
        last_ms = now;
        if (gen_send(s_gen_seq) == 0) {
            s_gen_seq++;
        }
    }
}

int main(void)
{
    int rc_log;
    int rc_gen;

    board_init();
    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    if (s_cpu_mhz == 0U) {
        s_cpu_mhz = 480U;
    }
    board_can_transceivers_enable();
    rc_log = can_hw_init(LOG_CAN_DEV, s_msg_buf_log, &s_accept_all_std, 1U);
    rc_gen = can_hw_init(GEN_CAN_DEV, s_msg_buf_gen, &s_accept_gen, 1U);
    printf("\r\n[app] can_logger ready (CAN0=record, CAN1=generator) rc=%d/%d\r\n", rc_log, rc_gen);
    printf("[app] cmds: log start|stop|dump [n] | gen on [ms]|off | replay [n] [gap_us] | verify | stat | gw on\r\n");

    s_logging = 1U;
    s_gen_on = 1U;
    s_gen_period_ms = 10U;
    {
        uint32_t t0 = bsp_uptime_ms();
        while ((bsp_uptime_ms() - t0) < 1500U) {
            bsp_shell_poll();
            log_poll();
            gen_tick();
        }
    }
    printf("[auto] 1.5 s 采集完成: ");
    cmd_stat();

    while (1) {
        bsp_shell_poll();
        if (s_gw_on) {
            app_lwip_poll();
            app_can_gw_poll();
        }
        log_poll();
        gen_tick();
    }
}
