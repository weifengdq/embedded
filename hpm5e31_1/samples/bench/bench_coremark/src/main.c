/*
 * bench_coremark - CoreMark 1.0 基准测试（HPM5E31 @480MHz）
 *
 * 功能:
 *   - 运行 EEMBC CoreMark 1.0（官方源码，来自 HPM SDK middleware/coremark）
 *   - 每次上电自动连续跑 BENCH_RUNS 轮，输出每轮分数与均值/标准差/最小/最大
 *   - 计时用 mcycle(480MHz)，并用 MCHTMR(24MHz) 交叉校验时间基准
 *   - letter-shell 命令: cm / cmstat / cminfo
 *
 * 硬件: hpm5e31_1，串口 UART4 921600 8N1
 * 现象: 见 README.md
 */
#include <stdio.h>
#include <string.h>
#include "bsp.h"
#include "hpm_csr_drv.h"

extern int coremark_main(void);

/* core_portme.c 中定义、ee_printf 钩子解析得到 */
extern uint32_t g_cpu_freq_hz;
extern uint32_t g_mchtmr_freq_hz;
extern volatile double g_run_secs;
extern volatile double g_run_secs_mt;
extern volatile double g_run_score;
extern volatile uint32_t g_run_iters;

#define MAX_RUNS 16
static double s_score[MAX_RUNS];
static int s_nrun;

#if defined(BENCH_DATA_SRAM)
#define DATA_LOC_STR "data=BSS(AXI_SRAM,cached)"
#else
#define DATA_LOC_STR "data=STACK(DLM)"
#endif

static double sqrt_iter(double x)
{
    /* 牛顿迭代开方：避免为统计额外依赖 libm 的正确性 */
    if (x <= 0.0) {
        return 0.0;
    }
    double g = x;
    for (int i = 0; i < 40; i++) {
        g = 0.5 * (g + x / g);
    }
    return g;
}

static void stats_report(void)
{
    if (s_nrun == 0) {
        printf("no run yet, use 'cm <n>'\r\n");
        return;
    }
    double sum = 0.0, mn = s_score[0], mx = s_score[0];
    for (int i = 0; i < s_nrun; i++) {
        sum += s_score[i];
        if (s_score[i] < mn) mn = s_score[i];
        if (s_score[i] > mx) mx = s_score[i];
    }
    double mean = sum / s_nrun;
    double var = 0.0;
    for (int i = 0; i < s_nrun; i++) {
        double d = s_score[i] - mean;
        var += d * d;
    }
    var = (s_nrun > 1) ? var / (s_nrun - 1) : 0.0;
    double sd = sqrt_iter(var);
    double mhz = (double) g_cpu_freq_hz / 1000000.0;
    printf("\r\n===== CoreMark summary (n=%d) =====\r\n", s_nrun);
    printf("cpu=%.0f MHz  %s  opt=%s\r\n", mhz, DATA_LOC_STR, BENCH_OPT_STR);
    printf("CoreMark mean=%.4f sd=%.4f min=%.4f max=%.4f\r\n", mean, sd, mn, mx);
    printf("CoreMark/MHz mean=%.4f sd=%.4f\r\n", mean / mhz, sd / mhz);
    printf("BENCH_STAT metric=coremark n=%d mean=%.4f sd=%.4f min=%.4f max=%.4f cmhz=%.4f\r\n",
           s_nrun, mean, sd, mn, mx, mean / mhz);
}

static void run_bench(int n)
{
    for (int i = 0; i < n; i++) {
        if (s_nrun >= MAX_RUNS) {
            printf("run buffer full (%d), reboot to reset\r\n", MAX_RUNS);
            break;
        }
        uint32_t idx = (uint32_t) (s_nrun + 1);
        printf("\r\n---- CoreMark run %u ----\r\n", idx);
        uint64_t c0 = hpm_csr_get_core_mcycle();
        coremark_main();
        uint64_t c1 = hpm_csr_get_core_mcycle();
        double score = (double) g_run_score;
        double secs = (double) g_run_secs;
        double secs_mt = (double) g_run_secs_mt;
        double wall = (double) (c1 - c0) / (double) g_cpu_freq_hz;
        printf("BENCH_RUN idx=%u score=%.4f iters=%lu secs=%.6f wall=%.6f mtime_secs=%.6f cmhz=%.4f\r\n",
               idx, score, (unsigned long) g_run_iters, secs, wall, secs_mt,
               score / ((double) g_cpu_freq_hz / 1000000.0));
        s_score[s_nrun++] = score;
    }
    stats_report();
}

static void cmd_cm(int argc, char *argv[])
{
    uint32_t n = 1;
    if (argc >= 2) {
        if (bsp_parse_u32(argv[1], &n) != 0 || n == 0) {
            printf("usage: cm <n>\r\n");
            return;
        }
    }
    run_bench((int) n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 cm, cmd_cm, run CoreMark n more times: cm <n>);

static void cmd_cmstat(int argc, char *argv[])
{
    (void) argc; (void) argv;
    stats_report();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 cmstat, cmd_cmstat, show CoreMark statistics);

static void cmd_cminfo(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("board      : %s\r\n", BOARD_NAME);
    printf("cpu        : %lu Hz (%s)\r\n", (unsigned long) g_cpu_freq_hz, DATA_LOC_STR);
    printf("mchtmr     : %lu Hz\r\n", (unsigned long) g_mchtmr_freq_hz);
    printf("coremark   : EEMBC CoreMark 1.0, src=HPM SDK middleware/coremark\r\n");
    printf("opt flags  : %s\r\n", BENCH_OPT_STR);
    printf("time source: mcycle (cross-check MCHTMR)\r\n");
    printf("runs so far: %d\r\n", s_nrun);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 cminfo, cmd_cminfo, show CoreMark environment);

int main(void)
{
    bsp_init();
    printf("\r\n=== bench_coremark (EEMBC CoreMark 1.0) ===\r\n");
    printf("board=%s cpu=%.0f MHz %s opt=%s runs=%d\r\n",
           BOARD_NAME, (double) g_cpu_freq_hz / 1000000.0, DATA_LOC_STR, BENCH_OPT_STR, BENCH_RUNS);
    printf("commands: cm <n> | cmstat | cminfo\r\n");

    run_bench(BENCH_RUNS);

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
