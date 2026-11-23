/*
 * core_portme.c - CoreMark 移植层（hpm5e31_1 / HPM5E31）
 *
 * 关键点:
 *   1. 计时用 mcycle（= CPU 时钟 480MHz，分辨率 ~2ns），比 24MHz 的 MCHTMR 细 20 倍；
 *      同时记录 MCHTMR 时间做交叉校验（两者差值反映 PLL/晶振偏差）。
 *   2. ee_printf 自己实现：输出到串口的同时抓出 "Iterations/Sec" 与 "Iterations"，
 *      固件因此能自算多轮运行的均值/标准差，不依赖离线解析日志。
 *   3. TOTAL_DATA_SIZE=2000 -> PERFORMANCE_RUN（CoreMark 官方要求，可报分）。
 */
#include "coremark.h"
#include "core_portme.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "board.h"
#include "hpm_csr_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_mchtmr_drv.h"

/* 与 main.c 共享的测量结果 */
uint32_t g_cpu_freq_hz;
uint32_t g_mchtmr_freq_hz;
volatile double g_run_secs;
volatile double g_run_secs_mt;
volatile double g_run_score;
volatile uint32_t g_run_iters;

static uint64_t s_cyc0, s_cyc1;
static uint64_t s_mt0, s_mt1;

#if VALIDATION_RUN
volatile ee_s32 seed1_volatile = 0x3415;
volatile ee_s32 seed2_volatile = 0x3415;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PERFORMANCE_RUN
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PROFILE_RUN
volatile ee_s32 seed1_volatile = 0x8;
volatile ee_s32 seed2_volatile = 0x8;
volatile ee_s32 seed3_volatile = 0x8;
#endif
/* 0 = 自动标定迭代次数（CoreMark 目标：单轮 >= 10s） */
volatile ee_s32 seed4_volatile = 0;
volatile ee_s32 seed5_volatile = 0;

ee_u32 default_num_contexts = 1;

/* ---- ee_printf: 串口输出 + 分数抓取 ---- */
void ee_printf(char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (n > 0) {
        fputs(line, stdout);
        if (strncmp(line, "Iterations/Sec", 14) == 0) {
            const char *p = strchr(line, ':');
            if (p) {
                g_run_score = atof(p + 1);
            }
        } else if (strncmp(line, "Iterations", 10) == 0 && line[10] == 0x20) {
            const char *p = strchr(line, ':');
            if (p) {
                g_run_iters = (ee_u32) strtoul(p + 1, NULL, 10);
            }
        }
    }
}

/* ---- 计时 ---- */
void start_time(void)
{
    s_mt0 = HPM_MCHTMR->MTIME;
    s_cyc0 = hpm_csr_get_core_mcycle();
}

void stop_time(void)
{
    s_cyc1 = hpm_csr_get_core_mcycle();
    s_mt1 = HPM_MCHTMR->MTIME;
    g_run_secs = (double) (s_cyc1 - s_cyc0) / (double) g_cpu_freq_hz;
    g_run_secs_mt = (double) (s_mt1 - s_mt0) / (double) g_mchtmr_freq_hz;
}

CORE_TICKS get_time(void)
{
    return (CORE_TICKS) (s_cyc1 - s_cyc0);
}

secs_ret time_in_secs(CORE_TICKS ticks)
{
    return (secs_ret) ((double) ticks / (double) g_cpu_freq_hz);
}

void portable_init(core_portable *p, int *argc, char *argv[])
{
    static int inited = 0;
    (void) argc;
    (void) argv;
    if (!inited) {
        bsp_init();
        inited = 1;
    }
    g_cpu_freq_hz = clock_get_frequency(clock_cpu0);
    g_mchtmr_freq_hz = clock_get_frequency(clock_mchtmr0);
    if (g_cpu_freq_hz == 0 || g_mchtmr_freq_hz == 0) {
        printf("ERROR: clock_get_frequency() failed (cpu=%lu mchtmr=%lu)\r\n",
               (unsigned long) g_cpu_freq_hz, (unsigned long) g_mchtmr_freq_hz);
    }
    if (sizeof(ee_ptr_int) != sizeof(ee_u8 *)) {
        printf("ERROR: ee_ptr_int must hold a pointer\r\n");
    }
    if (sizeof(ee_u32) != 4) {
        printf("ERROR: ee_u32 must be 32-bit\r\n");
    }
    p->portable_id = 1;
}

void portable_fini(core_portable *p)
{
    double ppm = 0.0;
    if (g_run_secs > 0.0) {
        ppm = (g_run_secs_mt - g_run_secs) / g_run_secs * 1e6;
    }
    printf("TIMEBASE mcycle_secs=%.6f mchtmr_secs=%.6f diff_ppm=%.1f cpu_hz=%lu mchtmr_hz=%lu\r\n",
           g_run_secs, g_run_secs_mt, ppm,
           (unsigned long) g_cpu_freq_hz, (unsigned long) g_mchtmr_freq_hz);
    p->portable_id = 0;
}
