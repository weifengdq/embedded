/*
 * gpt_oneshot —— AUTOSAR Gpt 单次定时（one-shot）精度与 API 开销实测
 *
 * 硬件（无需外部接线）：
 *   GPTMR1 CH0 -> Gpt 逻辑通道 1，one-shot 模式（GptChannelMode = GPT_CH_MODE_ONESHOT）
 *   GPTMR0 CH3 -> Gpt Predef Timer 自由运行时基（仅用于粗对齐，精度靠 mcycle）
 *   mcycle     -> CPU 周期计数（480 MHz，1 cycle = 2.083 ns）
 *
 * 测量口径（不依赖任何外部仪器）：
 *   c0 = mcycle; Gpt_StartTimer(ch1, ticks); c1 = mcycle;
 *   e0 = Gpt_GetTimeElapsed(ch1)  // 已计 tick 数
 *   c2 = ISR 入口 mcycle
 *   期望剩余周期 = (ticks - e0) * f_cpu / f_gptmr
 *   误差 = (c2 - c1) - 期望剩余周期，换算成 ns / ppm
 * 这样把"StartTimer 里那段不可观测的启动时间"从误差里扣掉了，剩下的就是
 * 定时器硬件 one-shot 精度 + 中断响应延迟。
 *
 * 命令：
 *   one <us>    单次定时 <us> 微秒，打印详细时间线
 *   sweep       跑 10/50/100/500/1000/5000 us x 20 次的精度扫描
 *   info        时钟/版本
 *   err         错误路径（非法通道 / 0 值 / 空指针）-> Det 计数
 */
#include "bsp.h"
#include "Gpt.h"
#include "Gpt_PBcfg.h"
#include "Det.h"
#include <stdio.h>

#define APP_CH  GPT_CHANNEL_1

static inline uint32_t cpu_cycles(void)
{
    uint32_t c;
    __asm__ volatile("csrr %0, mcycle" : "=r"(c));
    return c;
}

static volatile uint32_t s_done;       /* ISR 置位：one-shot 到期 */
static volatile uint32_t s_isr_cyc;    /* ISR 入口 mcycle          */
static volatile uint32_t s_isr_lat;    /* ISR 入口已过 tick 数     */

static uint32_t s_clkHz;
static uint32_t s_cpuHz;

void Gpt_Notification_1(void)
{
    s_isr_cyc = cpu_cycles();
    s_isr_lat = (uint32_t)Gpt_GetTimeElapsed(APP_CH);
    s_done = 1u;
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

typedef struct
{
    double err_ns_mean, err_ns_sd, err_ns_min, err_ns_max, err_ppm_mean;
    uint32_t n;
} shot_stat_t;

/* 一次 one-shot 测量：返回误差（cycles 修正到"从 c1 起算的期望剩余"） */
static int32_t shot_once(uint32_t target_us, uint32_t *api_cycles)
{
    uint32_t ticks = (uint32_t)(((uint64_t)target_us * (uint64_t)s_clkHz) / 1000000ULL);
    uint32_t c0, c1, c2, e0, remain;
    double cyc_per_tick = (double)s_cpuHz / (double)s_clkHz;
    int32_t err_cyc;

    if (ticks == 0u) { ticks = 1u; }
    s_done = 0u;
    c0 = cpu_cycles();
    Gpt_StartTimer(APP_CH, ticks);
    c1 = cpu_cycles();
    e0 = (uint32_t)Gpt_GetTimeElapsed(APP_CH);     /* 启动瞬间已计的 tick 数 */
    if (api_cycles != NULL) { *api_cycles = c1 - c0; }

    {   /* 有界等待：1 s 内没进中断就报错，避免固件挂死看不到任何输出 */
        uint32_t cw = cpu_cycles();
        while (s_done == 0u)
        {
            if ((cpu_cycles() - cw) > s_cpuHz)
            {
                return (int32_t)0x7FFFFFFF;
            }
        }
    }
    c2 = s_isr_cyc;

    remain = (ticks > e0) ? (ticks - e0) : 0u;
    err_cyc = (int32_t)(c2 - c1) - (int32_t)((double)remain * cyc_per_tick);
    return err_cyc;
}

static void run_sweep(void)
{
    static const uint32_t targets[] = {10u, 50u, 100u, 500u, 1000u, 5000u};
    uint32_t i, k;
    double ns_per_cyc = 1e9 / (double)s_cpuHz;

    printf("[sweep] one-shot accuracy, 20 shots each, f_gptmr1=%lu Hz, f_cpu=%lu Hz\r\n",
           (unsigned long)s_clkHz, (unsigned long)s_cpuHz);
    printf("%-8s %-10s %-12s %-12s %-12s %-12s\r\n",
           "target", "api_ns", "err_mean_ns", "err_sd_ns", "err_min_ns", "err_ppm");
    for (i = 0u; i < (uint32_t)(sizeof(targets) / sizeof(targets[0])); i++)
    {
        double sum = 0.0, sum2 = 0.0, mn = 1e18, mx = -1e18, api_sum = 0.0;
        uint32_t api = 0u;
        uint32_t n = 20u;
        double mean, sd, ppm;

        for (k = 0u; k < n; k++)
        {
            int32_t e = shot_once(targets[i], &api);
            double e_ns = (double)e * ns_per_cyc;
            sum += e_ns; sum2 += e_ns * e_ns;
            if (e_ns < mn) { mn = e_ns; }
            if (e_ns > mx) { mx = e_ns; }
            api_sum += (double)api * ns_per_cyc;
        }
        mean = sum / (double)n;
        sd = dsqrt_local((sum2 / (double)n) - (mean * mean));
        ppm = mean / ((double)targets[i] * 1000.0) * 1e6;
        printf("%-8lu %-10.1f %-12.1f %-12.1f %-12.1f %-12.2f\r\n",
               (unsigned long)targets[i], api_sum / (double)n, mean, sd, mn, ppm);
    }
}

static void cmd_one(int argc, char **argv)
{
    uint32_t us = 1000u, api = 0u, ticks;
    int32_t e;
    double ns_per_cyc = 1e9 / (double)s_cpuHz;

    if (argc >= 2)
    {
        if (bsp_parse_u32(argv[1], &us) != 0) { us = 1000u; }
    }
    if (us == 0u) { us = 1u; }
    ticks = (uint32_t)(((uint64_t)us * (uint64_t)s_clkHz) / 1000000ULL);
    e = shot_once(us, &api);
    printf("[one] target=%lu us -> ticks=%lu\r\n", (unsigned long)us, (unsigned long)ticks);
    printf("[one] api=%.1f ns, err=%+.1f ns (%+.2f ppm), isr_lat=%lu ticks (%.1f ns)\r\n",
           (double)api * ns_per_cyc, (double)e * ns_per_cyc,
           (double)e * ns_per_cyc / ((double)us * 1000.0) * 1e6,
           (unsigned long)s_isr_lat, (double)s_isr_lat * 1e9 / (double)s_clkHz);
    printf("[one] remaining after start=%lu, state elapsed=%lu\r\n",
           (unsigned long)Gpt_GetTimeRemaining(APP_CH),
           (unsigned long)Gpt_GetTimeElapsed(APP_CH));
}

static void cmd_sweep(int argc, char **argv)
{
    (void)argc; (void)argv;
    run_sweep();
}

static void cmd_info(int argc, char **argv)
{
    Std_VersionInfoType vi;
    (void)argc; (void)argv;
    Gpt_GetVersionInfo(&vi);
    printf("[gpt] Gpt v%u.%u.%u moduleID=%u; ch%u = GPTMR1 CH0 ONESHOT\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, (unsigned)vi.moduleID, (unsigned)APP_CH);
    printf("[gpt] f_gptmr1=%lu Hz (1 tick=%.1f ns), f_cpu=%lu Hz\r\n",
           (unsigned long)s_clkHz, 1e9 / (double)s_clkHz, (unsigned long)s_cpuHz);
    printf("[gpt] one-shot 精度下限 = 1 tick = %.1f ns；API 开销见 sweep 表 api_ns 列\r\n",
           1e9 / (double)s_clkHz);
}

static void cmd_err(int argc, char **argv)
{
    uint16 mod = 0u; uint8 inst = 0u, api = 0u, err = 0u;
    uint32_t before = Det_GetErrorCount();
    (void)argc; (void)argv;

    Gpt_StartTimer((Gpt_ChannelType)GPT_NUM_CHANNELS, 100u);  /* GPT_E_PARAM_CHANNEL */
    Gpt_StartTimer(APP_CH, 0u);                              /* GPT_E_PARAM_VALUE   */
    Gpt_StartTimer(APP_CH, 100u);                            /* 合法：立即启动      */
    Gpt_StartTimer(APP_CH, 100u);                            /* 运行中再启动：GPT_E_BUSY (runtime) */
    Gpt_GetVersionInfo(NULL_PTR);                            /* GPT_E_PARAM_POINTER */
    Gpt_StopTimer(APP_CH);

    printf("[det] before=%lu after=%lu delta=%lu\r\n", (unsigned long)before,
           (unsigned long)Det_GetErrorCount(),
           (unsigned long)(Det_GetErrorCount() - before));
    if (Det_GetLastError(&mod, &inst, &api, &err) == E_OK)
    {
        printf("[det] last: mod=%u inst=%u api=0x%02X err=0x%02X\r\n",
               (unsigned)mod, (unsigned)inst, (unsigned)api, (unsigned)err);
    }
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 one, cmd_one, single one-shot <us>: one 500);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sweep, cmd_sweep, one-shot accuracy sweep 10..5000 us);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, gpt oneshot info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 err, cmd_err, trigger dev-error paths);

int main(void)
{
    Std_VersionInfoType vi;

    bsp_init();
    Det_Init();
    Gpt_Init(&Gpt_Config);
    Gpt_EnableNotification(APP_CH);

    s_clkHz = clock_get_frequency(clock_gptmr1);
    s_cpuHz = clock_get_frequency(clock_cpu0);

    Gpt_GetVersionInfo(&vi);
    printf("\r\n=== mcal gpt_oneshot (board %s) ===\r\n", BOARD_NAME);
    printf("Gpt v%u.%u.%u: GPTMR1 CH0 one-shot, f_gptmr1=%lu Hz, f_cpu=%lu Hz\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, (unsigned long)s_clkHz, (unsigned long)s_cpuHz);

    printf("[gpt_oneshot] auto sweep start (shell idle during run)\r\n");
    board_delay_ms(1000);
    run_sweep();
    cmd_info(0, NULL);
    cmd_err(0, NULL);
    cmd_one(0, NULL);
    printf("[gpt_oneshot] auto sweep done. cmds: one <us> | sweep | info | err\r\n");

    {
        uint32_t last = bsp_uptime_ms();
        while (1)
        {
            bsp_shell_poll();
            if ((bsp_uptime_ms() - last) >= 3000u)
            {
                last = bsp_uptime_ms();
                cmd_one(0, NULL);   /* 心跳复打一次 1 ms one-shot 实测，防抓包丢字节 */
            }
        }
    }
    return 0;
}