/*
 * Copyright (c) 2026, hpm5e31_1 应用层书稿项目
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * 本文件的 PTP 协议栈主体来自 HPM SDK v1.13.0 middleware/ptpd/v2（PTPd 2.0.1），
 * 由本组件做板级/工程化封装：ENET 硬件时间戳初始化、1 ms 时基、板上 offset 统计、
 * letter-shell 诊断命令。协议实现本身不改动，便于与上游比对。
 */
/**
 * @file app_ptp.c
 * @brief PTP 组件实现：ENET 硬件时钟 + ptpd v2 + lwIP UDP + 串口诊断
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>

#include "app_ptp.h"
#include "board.h"
#include "bsp.h"
#include "common.h"
#include "netconf.h"
#include "sys_arch.h"
#include "hpm_enet_drv.h"
#include "hpm_csr_drv.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "ptpd.h"

/* ptpd v2（middleware/ptpd/v2/src/ptpd.c）导出的全局实例 */
extern PtpClock    ptpClock;
extern RunTimeOpts rtOpts;

#define APP_PTP_TICK_MS  (1U)
#define APP_PTP_DEF_SEC  (1651074120UL)
#define APP_PTP_STAT_PERIOD_MS (1000U)

static app_ptp_cfg_t s_cfg = { .domain = 0, .enable_pps = false, .initial_sec = APP_PTP_DEF_SEC };
static volatile uint32_t s_localtime_ms;
static bool s_stack_started;

typedef struct {
    uint32_t n;            /* 采样数（每秒一次） */
    int64_t  sum;          /* 残差累加（ns） */
    int64_t  sumsq;        /* 残差平方累加 */
    int32_t  min_ns;
    int32_t  max_ns;
    int32_t  last_ns;
    uint32_t last_tick_ms;
    uint32_t settle_ms;     /* 进入 SLAVE 后稳定 5 s 才开始计入统计（0=尚未稳定） */
} app_ptp_stats_t;

static app_ptp_stats_t s_stats = { .min_ns = INT32_MAX, .max_ns = INT32_MIN };

/* ---------------- 1 ms 时基 ---------------- */
static void app_ptp_tick(void)
{
    s_localtime_ms += APP_PTP_TICK_MS;
    sys_timer_callback();   /* lwIP sys_now()/超时链表 */
}

int app_ptp_hw_init(const app_ptp_cfg_t *cfg)
{
    enet_ptp_config_t pcfg;
    enet_ptp_ts_update_t ts;

    if (cfg != NULL) {
        s_cfg = *cfg;
    }
    memset(&pcfg, 0, sizeof(pcfg));
    memset(&ts, 0, sizeof(ts));

    /* ENET0 的 PTP 时钟源：PLL1_CLK0 400 MHz / 4 = 100 MHz（board_init_enet_ptp_clock 内配置） */
    if (board_init_enet_ptp_clock(ENET) != status_success) {
        printf("[ptp] board_init_enet_ptp_clock failed\r\n");
        return -1;
    }
    if (enet_get_default_ptp_config(ENET, clock_get_frequency(ENET_PTP_CLK), &pcfg) != status_success) {
        printf("[ptp] enet_get_default_ptp_config failed\r\n");
        return -1;
    }
    pcfg.update_method     = enet_ptp_time_fine_update;
    pcfg.mode              = enet_pps_fixed_output;
    pcfg.param.control_freq = enet_pps_ctrl_pps;
    pcfg.pps_enable_output = s_cfg.enable_pps;
    ts.sec  = s_cfg.initial_sec;
    ts.nsec = 0;
    pcfg.ptp_timestamp = &ts;

    if (enet_init_ptp(ENET, &pcfg) != status_success) {
        printf("[ptp] enet_init_ptp failed\r\n");
        return -1;
    }

    app_ptp_reset_stats();
    printf("[ptp] ENET hw clock ok: ptp_clk=%lu Hz, ptr=%p, init=%lu s\r\n",
           (unsigned long) clock_get_frequency(ENET_PTP_CLK), (void *) ENET,
           (unsigned long) s_cfg.initial_sec);
    return 0;
}

int app_ptp_start(void)
{
    if (ptpd_Init() != 0) {
        printf("[ptp] ptpd_Init failed\r\n");
        return -1;
    }
    board_timer_create(APP_PTP_TICK_MS, app_ptp_tick);
    s_stack_started = true;
    printf("[ptp] stack started: domain=%u delayMech=%s slaveOnly=%s\r\n",
           (unsigned) rtOpts.domainNumber,
           (rtOpts.delayMechanism == E2E) ? "E2E" : "P2P",
           rtOpts.slaveOnly ? "true" : "false");
    return 0;
}

void app_ptp_read_hw_clock(uint32_t *sec, uint32_t *nsec)
{
    enet_ptp_ts_system_t ts;

    enet_get_ptp_timestamp(ENET, &ts);
    if (sec != NULL)  { *sec  = ts.sec; }
    if (nsec != NULL) { *nsec = ts.nsec; }
}

void app_ptp_set_hw_clock(uint32_t sec, uint32_t nsec)
{
    enet_ptp_ts_update_t ts;

    memset(&ts, 0, sizeof(ts));
    ts.sec  = sec;
    ts.nsec = nsec;
    enet_set_ptp_timestamp(ENET, &ts);
}

void app_ptp_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.min_ns = INT32_MAX;
    s_stats.max_ns = INT32_MIN;
}

/* 路径延迟：E2E 存在 currentDS.meanPathDelay，P2P 存在 portDS.peerMeanPathDelay（PTPd 的语义差异，
 * 只看 currentDS 会让 P2P 模式的板端统计永远停在 "no sample yet"）。*/
static int32_t app_ptp_path_delay_ns(void)
{
    if (rtOpts.delayMechanism == P2P) {
        return (int32_t) ptpClock.portDS.peerMeanPathDelay.nanoseconds;
    }
    return (int32_t) ptpClock.currentDS.meanPathDelay.nanoseconds;
}

/* 不引 libm（newlib-nano 下 -lm 链接与 %f 支持都要额外配置），用牛顿迭代求平方根 */
static double app_ptp_sqrt(double x)
{
    double r = x, t;
    int i;

    if (x <= 0.0) {
        return 0.0;
    }
    for (i = 0; i < 40; i++) {
        t = 0.5 * (r + x / r);
        if (t == r) {
            break;
        }
        r = t;
    }
    return r;
}

static void app_ptp_sample_stats(void)
{
    int64_t  off64;
    int32_t  off;
    uint32_t now = s_localtime_ms;

    if ((uint32_t)(now - s_stats.last_tick_ms) < APP_PTP_STAT_PERIOD_MS) {
        return;
    }
    s_stats.last_tick_ms = now;
    if (ptpClock.portDS.portState != PTP_SLAVE) {
        s_stats.settle_ms = 0;      /* 丢掉锁定前的暂态 */
        return;
    }
    /* E2E/P2P 路径延迟要等第一次 Delay_Resp/Pdelay_Resp 才有值（期间 offset 会有一次
     * 十几 us 的台阶，属于正常收敛过程），所以必须等到 meanPathDelay != 0 再开始计时。*/
    /* 稳定判据：路径延迟已知（第一次 Delay_Resp 已到）且 |offset| 已进入 5 us 以内。
     * 这两个条件之前 offset 会有一次十几 us 的台阶（meanPathDelay 从 0 变成实测值），
     * 属于正常收敛过程，不能计入统计。*/
    if ((s_stats.settle_ms == 0U) ||
        (app_ptp_path_delay_ns() == 0) ||
        (labs((long) ptpClock.currentDS.offsetFromMaster.nanoseconds) > 5000L)) {
        s_stats.settle_ms = now;    /* 反复重置，直到真正进入稳态 */
        return;
    }
    if ((uint32_t)(now - s_stats.settle_ms) < 5000U) {
        return;                     /* 伺服收敛期不计入 */
    }
    /* offset 的 seconds/nanoseconds 两段都要计入（正常锁定时 seconds 恒为 0） */
    off64 = (int64_t) ptpClock.currentDS.offsetFromMaster.seconds * 1000000000LL
          + (int64_t) ptpClock.currentDS.offsetFromMaster.nanoseconds;
    if (off64 > (int64_t) INT32_MAX)       { off = INT32_MAX; }
    else if (off64 < (int64_t) INT32_MIN)  { off = INT32_MIN; }
    else                                   { off = (int32_t) off64; }
    s_stats.last_ns = off;
    s_stats.sum   += off;
    s_stats.sumsq += (int64_t) off * (int64_t) off;
    if (off < s_stats.min_ns) { s_stats.min_ns = off; }
    if (off > s_stats.max_ns) { s_stats.max_ns = off; }
    s_stats.n++;
}

void app_ptp_poll(void)
{
    if (!s_stack_started) {
        return;
    }
    if (netif_is_link_up(&gnetif)) {
        ptpd_periodic_handle(s_localtime_ms);
    }
    app_ptp_sample_stats();
}

static const char *app_ptp_state_str(uint8_t st)
{
    switch (st) {
    case PTP_INITIALIZING: return "INITIALIZING";
    case PTP_FAULTY:       return "FAULTY";
    case PTP_DISABLED:     return "DISABLED";
    case PTP_LISTENING:    return "LISTENING";
    case PTP_PASSIVE:      return "PASSIVE";
    case PTP_UNCALIBRATED: return "UNCALIBRATED";
    case PTP_SLAVE:        return "SLAVE";
    case PTP_MASTER:       return "MASTER";
    default:               return "?";
    }
}

void app_ptp_print_status(void)
{
    uint32_t sec, nsec;
    int32_t  ofm;

    app_ptp_read_hw_clock(&sec, &nsec);
    ofm = (int32_t) ptpClock.currentDS.offsetFromMaster.nanoseconds;

    printf("[ptp] state=%s domain=%u mech=%s slaveOnly=%s\r\n",
           app_ptp_state_str(ptpClock.portDS.portState),
           (unsigned) ptpClock.defaultDS.domainNumber,
           (ptpClock.portDS.delayMechanism == E2E) ? "E2E" : "P2P",
           ptpClock.defaultDS.slaveOnly ? "true" : "false");
    printf("[ptp] hwclock=%lu.%09lu s  offset=%ld ns  pathdelay=%ld ns  drift=%ld (ppb*2^-16)\r\n",
           (unsigned long) sec, (unsigned long) nsec, (long) ofm,
           (long) app_ptp_path_delay_ns(),
           (long) ptpClock.observedDrift);
    printf("[ptp] stepsRemoved=%u gm=%02X%02X%02X..%02X%02X prio1=%u class=%u link=%s\r\n",
           (unsigned) ptpClock.currentDS.stepsRemoved,
           ptpClock.parentDS.grandmasterIdentity[0], ptpClock.parentDS.grandmasterIdentity[1],
           ptpClock.parentDS.grandmasterIdentity[2],
           ptpClock.parentDS.grandmasterIdentity[6], ptpClock.parentDS.grandmasterIdentity[7],
           (unsigned) ptpClock.parentDS.grandmasterPriority1,
           (unsigned) ptpClock.parentDS.grandmasterClockQuality.clockClass,
           netif_is_link_up(&gnetif) ? "UP" : "DOWN");
}

/* 注意：这里用本组件的 1 ms 软时基 s_localtime_ms，而不是 bsp_uptime_ms()。
 * bsp_uptime_ms() 是“按调用间隔增量累加”的实现，间隔 < 1 ms 时整数除法把增量截断成 0，
 * 在每轮几十 µs 的主循环里会整段冻结（本板实测：循环永不退出、周期打印不出现）。
 * 详见第 37 章“易错点”一节。*/
void app_ptp_bench(uint32_t secs)
{
    uint32_t t0, last, now;

    if (secs == 0U) {
        secs = 30U;
    }
    app_ptp_reset_stats();
    printf("[ptpbench] sampling %lu s (keep-alive polling on)\r\n", (unsigned long) secs);
    t0 = s_localtime_ms;
    last = t0;
    while ((uint32_t)(s_localtime_ms - t0) < secs * 1000U) {
        enet_common_handler(&gnetif);
        app_ptp_poll();
        now = s_localtime_ms;
        if ((uint32_t)(now - last) >= 5000U) {
            last = now;
            app_ptp_print_stats();
        }
    }
    app_ptp_print_stats();
    printf("[ptpbench] done (%lu s)\r\n", (unsigned long) secs);
}

void app_ptp_print_stats(void)
{
    double mean = 0.0, var = 0.0, stddev = 0.0;

    if (s_stats.n == 0U) {
        printf("[ptpstats] no sample yet (state=%s)\r\n", app_ptp_state_str(ptpClock.portDS.portState));
        return;
    }
    mean = (double) s_stats.sum / (double) s_stats.n;
    var  = (double) s_stats.sumsq / (double) s_stats.n - mean * mean;
    if (var < 0.0) {
        var = 0.0;
    }
    stddev = app_ptp_sqrt(var);
    printf("[ptpstats] n=%lu mean=%.1f ns std=%.1f ns min=%ld ns max=%ld ns last=%ld ns\r\n",
           (unsigned long) s_stats.n, mean, stddev,
           (long) s_stats.min_ns, (long) s_stats.max_ns, (long) s_stats.last_ns);
    printf("[ptpstats] PASS/FAIL 判据：|mean|<1000 ns 且 std<500 ns 视为锁定\r\n");
}

/* ---------------- letter-shell 命令 ---------------- */
static void cmd_ptp(int argc, char *argv[])
{
    (void) argc; (void) argv;
    app_ptp_print_status();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ptp, cmd_ptp, show PTP state/offset/pathDelay);

static void cmd_ptpstats(int argc, char *argv[])
{
    if ((argc >= 2) && (strncmp(argv[1], "reset", 5) == 0)) {
        app_ptp_reset_stats();
        printf("[ptpstats] reset\r\n");
        return;
    }
    app_ptp_print_stats();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ptpstats, cmd_ptpstats, PTP offset stats: ptpstats [reset]);

static void cmd_ptpreset(int argc, char *argv[])
{
    uint32_t sec = APP_PTP_DEF_SEC;
    if (argc >= 2) {
        (void) bsp_parse_u32(argv[1], &sec);
    }
    app_ptp_set_hw_clock(sec, 0);
    printf("[ptp] hw clock set to %lu.000000000 s\r\n", (unsigned long) sec);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ptpreset, cmd_ptpreset, set ENET PTP hw clock: ptpreset <sec>);

/* 阻塞式测量：内部保持 enet/PTP 轮询，串口日志里直接拿到统计表 */
static void cmd_ptpbench(int argc, char *argv[])
{
    uint32_t secs = 30;
    if (argc >= 2) {
        (void) bsp_parse_u32(argv[1], &secs);
    }
    app_ptp_bench(secs);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ptpbench, cmd_ptpbench, blocking PTP sample window: ptpbench [secs]);

/* hwrun.sh --send 串里用：延时但保持 PTP/网络轮询（板子的 shell 是单线程，普通 delay 会把协议栈冻住） */
static void cmd_sleep(int argc, char *argv[])
{
    uint32_t ms = 1000;
    uint32_t t0;

    if (argc >= 2) {
        (void) bsp_parse_u32(argv[1], &ms);
    }
    printf("[sys] sleep %lu ms (PTP keep-alive)\r\n", (unsigned long) ms);
    t0 = s_localtime_ms;
    while ((uint32_t)(s_localtime_ms - t0) < ms) {
        enet_common_handler(&gnetif);
        app_ptp_poll();
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sleep, cmd_sleep, delay N ms keeping PTP alive: sleep <ms>);