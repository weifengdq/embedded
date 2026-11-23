/*
 * gpt_periodic —— AUTOSAR Gpt 周期 tick：10 s 计数误差 + ISR 抖动实测
 *
 * 硬件（无需外部接线）：
 *   GPTMR0 CH0 -> Gpt 逻辑通道 0，连续模式，1 ms tick，reload = f_gptmr/1000
 *   GPTMR3     -> BSP 的 bsp_uptime_ms() 参考时基（同源时钟，用于长期频率比对）
 *   mcycle CSR -> CPU 周期计数，用于测 ISR 到达抖动与 API 调用开销
 *
 * 实测口径：
 *   1) 计数误差 = 10 s 内中断次数 vs bsp_uptime_ms() 走过 10 s 的偏差（ppm）；
 *   2) ISR 抖动 = 相邻两次中断入口 mcycle 差值的 min/max/stddev；
 *   3) ISR 延迟 = 中断入口读 Gpt_GetTimeElapsed(0)（计数器已回绕，值即"迟到 tick 数"）。
 *
 * 命令：
 *   gpt info              打印时钟/周期配置与 API 版本
 *   gpt bench <sec>       跑 <sec> 秒统计（默认 10）
 *   gpt stat              打印当前统计（含 ISR 抖动/延迟/API 开销）
 *   gpt clr               清统计
 *   gpt stop / gpt start  停表（读回 latched elapsed）/ 重新启表
 *   gpt predef            读 4 个 Predef Timer 值并核对相互一致
 *   gpt err               错误路径：非法通道/非法值/空指针 -> Det 计数
 */
#include "bsp.h"
#include "Gpt.h"
#include "Gpt_PBcfg.h"
#include "Det.h"
#include <stdio.h>

#define APP_CH        GPT_CHANNEL_0
#define APP_TICK_US   (1000u)     /* 1 ms tick */

/* ---- CPU 周期计数 ---- */
static inline uint32_t cpu_cycles(void)
{
    uint32_t c;
    __asm__ volatile("csrr %0, mcycle" : "=r"(c));
    return c;
}

/* ---- 统计量（ISR 写、主循环读，读写都在临界区或 32 位原子） ---- */
static volatile uint32_t s_tick;              /* 周期中断次数                */
static volatile uint32_t s_cyc_prev;          /* 上次 ISR 入口 mcycle        */
static volatile uint32_t s_cyc_min = 0xFFFFFFFFuL;
static volatile uint32_t s_cyc_max;
static volatile uint64_t s_cyc_sum;
static volatile uint64_t s_cyc_sq;
static volatile uint32_t s_lat_min = 0xFFFFFFFFuL; /* ISR 入口已过 tick 数  */
static volatile uint32_t s_lat_max;
static volatile uint64_t s_lat_sum;

static uint32_t s_clkHz;
static uint32_t s_reload;
static uint32_t s_cpuHz;

/* ---- AUTOSAR 通知回调（强符号覆盖 Gpt_PBcfg.c 里的弱默认实现） ---- */
void Gpt_Notification_0(void)
{
    uint32_t now = cpu_cycles();
    uint32_t lat = (uint32_t)Gpt_GetTimeElapsed(APP_CH);   /* 回绕后的 CNT = 迟到 tick 数 */

    if (s_tick != 0u)
    {
        uint32_t dt = now - s_cyc_prev;
        if (dt < s_cyc_min) { s_cyc_min = dt; }
        if (dt > s_cyc_max) { s_cyc_max = dt; }
        s_cyc_sum += dt;
        s_cyc_sq += (uint64_t)dt * (uint64_t)dt;
    }
    s_cyc_prev = now;
    if (lat < s_lat_min) { s_lat_min = lat; }
    if (lat > s_lat_max) { s_lat_max = lat; }
    s_lat_sum += lat;
    s_tick++;
}

/* 临界区读快照 */
typedef struct
{
    uint32_t tick, cmin, cmax, lmin, lmax;
    uint64_t csum, csq, lsum;
} snap_t;

static void snap_take(snap_t *s)
{
    uint32_t key = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    s->tick = s_tick;
    s->cmin = s_cyc_min; s->cmax = s_cyc_max;
    s->csum = s_cyc_sum; s->csq = s_cyc_sq;
    s->lmin = s_lat_min; s->lmax = s_lat_max; s->lsum = s_lat_sum;
    restore_global_irq(key);
}

static void snap_clear(void)
{
    uint32_t key = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    s_tick = 0u;
    s_cyc_prev = 0u;
    s_cyc_min = 0xFFFFFFFFuL; s_cyc_max = 0u;
    s_cyc_sum = 0u; s_cyc_sq = 0u;
    s_lat_min = 0xFFFFFFFFuL; s_lat_max = 0u; s_lat_sum = 0u;
    restore_global_irq(key);
}

static double dsqrt_local(double x)
{
    double r;
    int i;
    if (x <= 0.0) { return 0.0; }
    r = (x > 1.0) ? x : 1.0;
    for (i = 0; i < 40; i++) { r = 0.5 * (r + x / r); }
    return r;
}

static void print_stat(void)
{
    snap_t s;
    double n, mean_cyc, var, sd_cyc, ns_per_cyc;

    snap_take(&s);
    if (s.tick < 3u)
    {
        printf("[gpt] only %lu ticks, wait\r\n", (unsigned long)s.tick);
        return;
    }
    n = (double)(s.tick - 1u);
    mean_cyc = (double)s.csum / n;
    var = (double)s.csq / n - mean_cyc * mean_cyc;
    sd_cyc = (var > 0.0) ? dsqrt_local(var) : 0.0;
    ns_per_cyc = 1e9 / (double)s_cpuHz;

    printf("[isr] n=%lu period=%.3f us (expect %.3f us)\r\n",
           (unsigned long)(s.tick - 1u), mean_cyc * ns_per_cyc / 1000.0,
           (double)APP_TICK_US);
    printf("[isr] jitter: min=%.3f max=%.3f pp=%.3f us  stddev=%.3f us (%.0f cycles)\r\n",
           (double)s.cmin * ns_per_cyc / 1000.0, (double)s.cmax * ns_per_cyc / 1000.0,
           (double)(s.cmax - s.cmin) * ns_per_cyc / 1000.0, sd_cyc * ns_per_cyc / 1000.0, sd_cyc);
    printf("[isr] entry latency: min=%lu max=%lu ticks (1 tick = %.1f ns) mean=%.1f ns\r\n",
           (unsigned long)s.lmin, (unsigned long)s.lmax,
           1e9 / (double)s_clkHz, (double)s.lsum / (double)s.tick * (1e9 / (double)s_clkHz));
}

static void cmd_info(int argc, char **argv)
{
    Std_VersionInfoType vi;
    (void)argc; (void)argv;
    Gpt_GetVersionInfo(&vi);
    printf("[gpt] HPM5E31 MCAL Gpt v%u.%u.%u moduleID=%u vendor=0x%04X\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, (unsigned)vi.moduleID, (unsigned)vi.vendorID);
    printf("[gpt] ch%u: GPTMR0 CH0 continuous, f_clk=%lu Hz reload=%lu (1 tick = %.1f ns)\r\n",
           (unsigned)APP_CH, (unsigned long)s_clkHz, (unsigned long)s_reload,
           1e9 / (double)s_clkHz);
    printf("[gpt] target period = %lu ticks = %.3f us, CPU=%lu Hz\r\n",
           (unsigned long)s_reload, (double)s_reload * 1e6 / (double)s_clkHz,
           (unsigned long)s_cpuHz);
    printf("[gpt] elapsed=%lu remaining=%lu\r\n",
           (unsigned long)Gpt_GetTimeElapsed(APP_CH), (unsigned long)Gpt_GetTimeRemaining(APP_CH));
}

static void cmd_stat(int argc, char **argv)
{
    (void)argc; (void)argv;
    print_stat();
}

static void cmd_clr(int argc, char **argv)
{
    (void)argc; (void)argv;
    snap_clear();
    printf("[gpt] stats cleared\r\n");
}

/* 10 s 长窗口计数误差：中断次数 vs BSP 参考时基 */
static void run_bench(uint32_t seconds)
{
    uint32_t t0_ms, t1_ms, tick0, tick1, elapsed_ms, expect, got;
    double ppm;

    if (seconds == 0u) { seconds = 10u; }
    printf("[bench] start %lu s window ...\r\n", (unsigned long)seconds);
    snap_clear();
    /* 丢弃第一个 tick 的相位误差：先等一个整周期 */
    t0_ms = bsp_uptime_ms();
    tick0 = s_tick;
    /* 测量窗口内不轮询 shell：其它命令会 Stop/Start 定时器，污染 tick 计数（第 45 章坑 45-2） */
    while ((bsp_uptime_ms() - t0_ms) < (seconds * 1000u))
    {
    }
    t1_ms = bsp_uptime_ms();
    tick1 = s_tick;
    elapsed_ms = t1_ms - t0_ms;
    expect = elapsed_ms;                 /* 1 tick = 1 ms */
    got = tick1 - tick0;
    ppm = ((double)got - (double)expect) / (double)expect * 1e6;
    printf("[bench] window=%lu ms ticks=%lu expect=%lu error=%+.3f ppm\r\n",
           (unsigned long)elapsed_ms, (unsigned long)got, (unsigned long)expect, ppm);
    printf("[bench] 1 ms tick actual = %.6f us, freq = %.3f Hz\r\n",
           (double)elapsed_ms * 1000.0 / (double)got, (double)got * 1000.0 / (double)elapsed_ms);
    print_stat();
}

static void cmd_bench(int argc, char **argv)
{
    uint32_t sec = 10u;
    if (argc >= 2)
    {
        (void)bsp_parse_u32(argv[1], &sec);
    }
    run_bench(sec);
}

/* API 调用开销：mcycle 差 / CPU 频率 */
static void cmd_api(int argc, char **argv)
{
    uint32_t i, c0, c1, min = 0xFFFFFFFFuL, max = 0u;
    uint64_t sum = 0u;
    const uint32_t loops = 1000u;
    (void)argc; (void)argv;

    for (i = 0u; i < loops; i++)
    {
        c0 = cpu_cycles();
        (void)Gpt_GetTimeElapsed(APP_CH);
        c1 = cpu_cycles() - c0;
        if (c1 < min) { min = c1; }
        if (c1 > max) { max = c1; }
        sum += c1;
    }
    printf("[api] Gpt_GetTimeElapsed: mean=%.1f cycles (%.1f ns) min=%lu max=%lu\r\n",
           (double)sum / (double)loops, (double)sum / (double)loops * 1e9 / (double)s_cpuHz,
           (unsigned long)min, (unsigned long)max);

    sum = 0u; min = 0xFFFFFFFFuL; max = 0u;
    for (i = 0u; i < loops; i++)
    {
        c0 = cpu_cycles();
        (void)Gpt_GetTimeRemaining(APP_CH);
        c1 = cpu_cycles() - c0;
        if (c1 < min) { min = c1; }
        if (c1 > max) { max = c1; }
        sum += c1;
    }
    printf("[api] Gpt_GetTimeRemaining: mean=%.1f cycles (%.1f ns) min=%lu max=%lu\r\n",
           (double)sum / (double)loops, (double)sum / (double)loops * 1e9 / (double)s_cpuHz,
           (unsigned long)min, (unsigned long)max);

    /* StartTimer/StopTimer 开销（会短暂改硬件状态，测完恢复） */
    sum = 0u; min = 0xFFFFFFFFuL; max = 0u;
    for (i = 0u; i < 100u; i++)
    {
        Gpt_StopTimer(APP_CH);
        c0 = cpu_cycles();
        Gpt_StartTimer(APP_CH, s_reload);
        c1 = cpu_cycles() - c0;
        if (c1 < min) { min = c1; }
        if (c1 > max) { max = c1; }
        sum += c1;
    }
    printf("[api] Gpt_StartTimer: mean=%.1f cycles (%.1f ns) min=%lu max=%lu\r\n",
           (double)sum / 100.0, (double)sum / 100.0 * 1e9 / (double)s_cpuHz,
           (unsigned long)min, (unsigned long)max);
    snap_clear();
}

/* GPTMR 实际输入时钟标定：只用 CPU mcycle + Gpt 的 Predef Timer（GPTMR0 CH3 自由运行）。
 * 窗口内 predef 值增量 d_us 是"按标称频率换算出的微秒数"，d_us*100 = 实际 tick 增量，
 * 于是   f_actual = d_us * 100 * f_cpu / dmcycle
 * 这个方法不依赖任何其它外设时钟，也不假设 GPTMR 频率（只用它自己的计数器）。
 * 为什么要标定：clock_get_frequency() 返回时钟树"应然值"，第 15 章实测过 GPTMR 实际
 * 输入约 98.44 MHz 而非 100 MHz；物理时间换算必须用实测值。
 */
static void cmd_cal(int argc, char **argv)
{
    uint32 c0, c1, dcyc, us0 = 0u, us1 = 0u, d_us;
    uint32 window_ms = 1000u;
    double f_meas, dev_ppm;

    if (argc >= 2)
    {
        uint32_t tmp = 0u;
        (void)bsp_parse_u32(argv[1], &tmp);
        window_ms = tmp;
        if (window_ms == 0u) { window_ms = 1000u; }
    }
    if (window_ms > 5000u) { window_ms = 5000u; }

    (void)Gpt_GetPredefTimerValue(GPT_PREDEF_TIMER_1US_32BIT, &us0);
    c0 = cpu_cycles();
    while ((cpu_cycles() - c0) < (uint32_t)((double)s_cpuHz * (double)window_ms / 1000.0)) { }
    c1 = cpu_cycles();
    (void)Gpt_GetPredefTimerValue(GPT_PREDEF_TIMER_1US_32BIT, &us1);

    dcyc = c1 - c0;
    d_us = us1 - us0;
    if ((dcyc == 0u) || (d_us == 0u))
    {
        printf("[cal] measurement failed (dcyc=%lu d_us=%lu)\r\n",
               (unsigned long)dcyc, (unsigned long)d_us);
        return;
    }
    f_meas = (double)d_us * 100.0 * (double)s_cpuHz / (double)dcyc;
    dev_ppm = (f_meas - (double)s_clkHz) / (double)s_clkHz * 1e6;
    printf("[cal] window: dmcycle=%lu = %.6f s @ f_cpu=%lu Hz, predef d=%lu us\r\n",
           (unsigned long)dcyc, (double)dcyc / (double)s_cpuHz, (unsigned long)s_cpuHz,
           (unsigned long)d_us);
    printf("[cal] GPTMR0 actual = %.0f Hz, clock_get_frequency = %lu Hz, deviation = %+.1f ppm\r\n",
           f_meas, (unsigned long)s_clkHz, dev_ppm);
    printf("[cal] reload=%lu -> tick = %.6f us, 1 s wall-clock ticks = %.3f\r\n",
           (unsigned long)s_reload, (double)s_reload * 1e6 / f_meas, f_meas / (double)s_reload);
}

static void cmd_stop(int argc, char **argv)
{
    (void)argc; (void)argv;
    Gpt_StopTimer(APP_CH);
    printf("[gpt] stopped: elapsed(latched)=%lu remaining(latched)=%lu\r\n",
           (unsigned long)Gpt_GetTimeElapsed(APP_CH),
           (unsigned long)Gpt_GetTimeRemaining(APP_CH));
}

static void cmd_start(int argc, char **argv)
{
    (void)argc; (void)argv;
    Gpt_EnableNotification(APP_CH);
    Gpt_StartTimer(APP_CH, s_reload);
    printf("[gpt] restarted, target=%lu ticks\r\n", (unsigned long)s_reload);
}

/* Predef Timer 一致性：1us/100us 时基来自同一自由运行计数器 */
static void cmd_predef(int argc, char **argv)
{
    uint32 v16 = 0u, v24 = 0u, v32 = 0u, v100 = 0u;
    uint32 i;
    (void)argc; (void)argv;

    (void)Gpt_GetPredefTimerValue(GPT_PREDEF_TIMER_1US_16BIT, &v16);
    (void)Gpt_GetPredefTimerValue(GPT_PREDEF_TIMER_1US_24BIT, &v24);
    (void)Gpt_GetPredefTimerValue(GPT_PREDEF_TIMER_1US_32BIT, &v32);
    (void)Gpt_GetPredefTimerValue(GPT_PREDEF_TIMER_100US_32BIT, &v100);
    printf("[predef] raw: 1us16=%lu 1us24=%lu 1us32=%lu 100us32=%lu\r\n",
           (unsigned long)v16, (unsigned long)v24, (unsigned long)v32, (unsigned long)v100);

    for (i = 0u; i < 200000u; i++) { }     /* 忙等约 1 ms */
    (void)Gpt_GetPredefTimerValue(GPT_PREDEF_TIMER_1US_32BIT, &v32);
    (void)Gpt_GetPredefTimerValue(GPT_PREDEF_TIMER_100US_32BIT, &v100);
    printf("[predef] delta: 1us32=%lu us, 100us32=%lu (x100 us), ratio=%.2f\r\n",
           (unsigned long)v32, (unsigned long)v100,
           (v100 != 0u) ? ((double)v32 / ((double)v100 * 100.0)) : 0.0);
}

/* 错误路径：3 个 API 各触发一条 DET，验证 Det 计数增长 */
static void cmd_err(int argc, char **argv)
{
    uint16 mod = 0u; uint8 inst = 0u, api = 0u, err = 0u;
    uint32_t before = Det_GetErrorCount();
    (void)argc; (void)argv;

    Gpt_StartTimer((Gpt_ChannelType)GPT_NUM_CHANNELS, 1000u);  /* -> GPT_E_PARAM_CHANNEL */
    Gpt_StartTimer(APP_CH, 0u);                               /* -> GPT_E_PARAM_VALUE   */
    Gpt_GetVersionInfo(NULL_PTR);                             /* -> GPT_E_PARAM_POINTER */

    printf("[det] before=%lu after=%lu delta=%lu\r\n", (unsigned long)before,
           (unsigned long)Det_GetErrorCount(),
           (unsigned long)(Det_GetErrorCount() - before));
    if (Det_GetLastError(&mod, &inst, &api, &err) == E_OK)
    {
        printf("[det] last: mod=%u inst=%u api=0x%02X err=0x%02X (expect mod=%u api=0x00/0x05/0x16)\r\n",
               (unsigned)mod, (unsigned)inst, (unsigned)api, (unsigned)err,
               (unsigned)GPT_MODULE_ID);
    }
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gpt_info, cmd_info, print gpt config/version);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gpt_bench, cmd_bench, run N-second tick accuracy bench: gpt_bench [sec]);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gpt_cal, cmd_cal, calibrate GPTMR clock vs CPU mcycle: gpt_cal [window_ms]);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gpt_stat, cmd_stat, print isr jitter/latency stats);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gpt_clr, cmd_clr, clear stats);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gpt_api, cmd_api, measure MCAL API call overhead);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gpt_stop, cmd_stop, stop timer and show latched elapsed);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gpt_start, cmd_start, restart 1ms timer);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gpt_predef, cmd_predef, read predef timers and check ratio);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gpt_err, cmd_err, trigger dev-error paths and show Det counter);

int main(void)
{
    Std_VersionInfoType vi;

    bsp_init();
    Det_Init();

    Gpt_Init(&Gpt_Config);
    s_clkHz = clock_get_frequency(clock_gptmr0);
    s_cpuHz = clock_get_frequency(clock_cpu0);
    if ((s_clkHz == 0u) || (s_cpuHz == 0u))
    {
        printf("!! clock read failed (gptmr0=%lu cpu0=%lu)\r\n",
               (unsigned long)s_clkHz, (unsigned long)s_cpuHz);
        while (1) { bsp_shell_poll(); }
    }
    s_reload = s_clkHz / 1000u;

    Gpt_GetVersionInfo(&vi);
    printf("\r\n=== mcal gpt_periodic (board %s) ===\r\n", BOARD_NAME);
    printf("Gpt v%u.%u.%u: GPTMR0 CH0 continuous, 1 ms tick\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version, (unsigned)vi.sw_patch_version);
    printf("f_gptmr0=%lu Hz, reload=%lu (expect 1 tick = %.1f ns)\r\n",
           (unsigned long)s_clkHz, (unsigned long)s_reload, 1e9 / (double)s_clkHz);

    Gpt_EnableNotification(APP_CH);
    Gpt_StartTimer(APP_CH, s_reload);
    printf("notification enabled, timer started; commands: gpt_info gpt_bench gpt_api gpt_predef gpt_err\r\n");

    /* 上板自证：开机自动跑完整测试套件（期间不轮询 shell，避免命令扰动定时器状态） */
    printf("[gpt_periodic] auto suite start (13 s, shell idle during run)\r\n");
    board_delay_ms(1000);
    run_bench(10u);
    cmd_cal(0, NULL);
    cmd_api(0, NULL);
    cmd_predef(0, NULL);
    cmd_err(0, NULL);
    printf("[gpt_periodic] auto suite done. commands: gpt_info | gpt_cal [ms] | gpt_bench [sec] | gpt_api | gpt_predef | gpt_err | gpt_stat\r\n");

    while (1)
    {
        bsp_shell_poll();
    }
    return 0;
}