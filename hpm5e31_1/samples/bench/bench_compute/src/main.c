/*
 * bench_compute - FPU / DSP / 位运算吞吐基准（hpm5e31_1 / HPM5E31 @480MHz）
 *
 * 测什么:
 *   1) 单精度 FPU: 乘加 (MAC)、除法、sqrt、sin/cos（libm 软件实现）
 *   2) 整数: 32 位乘加、32 位除、64 位除
 *   3) 定点 DSP: Q15 增益 (int16 * int16) >> 15
 *   4) 位操作: Zbb/Zbs 的 ctz/clz/popcount/rev8 吞吐
 *   5) 浮点<->整数转换
 *   每个 kernel 都有两份代码：XIP flash 与 ILM(.fast 段, 0x0..0x20000)，
 *   同一固件内切换，直接对比"取指路径"的影响。
 *
 * 方法: mcycle 计时，每测点重复 3 次取最好；用函数指针调用防止内联/常量折叠。
 * 输出: MOPS / MFLOPS / 每次操作周期数 (cyc_per_op)。
 *
 * 注意: 板级默认编译选项是 rv32imac + zba/zbb/zbc/zbs（无 F/D，软浮点）。
 *   FPU 数据需用 -DBENCH_FPU=1 重新编译（-march=rv32imafdc -mabi=ilp32f）。
 *
 * 硬件: hpm5e31_1，UART4 921600 8N1
 * 命令: comp / compilm / compinfo
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "bsp.h"
#include "hpm_csr_drv.h"
#include "hpm_clock_drv.h"

#define ARR_N 256

static uint32_t g_cpu_hz;
static float f_a[ARR_N], f_b[ARR_N], f_y[ARR_N];
static int32_t i_a[ARR_N], i_b[ARR_N], i_y[ARR_N];
static int16_t s_a[ARR_N], s_b[ARR_N], s_y[ARR_N];

/* ---------------- kernel 主体（always_inline，供两份包装复用） ---------------- */
static inline __attribute__((always_inline)) void k_mac_f32_body(float *y, const float *a, const float *b)
{
    for (int i = 0; i < ARR_N; i++) {
        y[i] += a[i] * b[i];
    }
}

static inline __attribute__((always_inline)) void k_div_f32_body(float *y, const float *a, const float *b)
{
    for (int i = 0; i < ARR_N; i++) {
        y[i] = a[i] / b[i];
    }
}

static inline __attribute__((always_inline)) void k_sqrt_f32_body(float *y, const float *a)
{
    for (int i = 0; i < ARR_N; i++) {
        y[i] = sqrtf(a[i]);
    }
}

static inline __attribute__((always_inline)) void k_sin_f32_body(float *y, const float *a)
{
    for (int i = 0; i < ARR_N; i++) {
        y[i] = sinf(a[i]);
    }
}

static inline __attribute__((always_inline)) void k_cvt_body(int32_t *yi, const float *a)
{
    for (int i = 0; i < ARR_N; i++) {
        yi[i] = (int32_t) a[i];
    }
}

static inline __attribute__((always_inline)) void k_mac_i32_body(int32_t *y, const int32_t *a, const int32_t *b)
{
    for (int i = 0; i < ARR_N; i++) {
        y[i] += a[i] * b[i];
    }
}

static inline __attribute__((always_inline)) void k_div_i32_body(int32_t *y, const int32_t *a, const int32_t *b)
{
    for (int i = 0; i < ARR_N; i++) {
        y[i] = a[i] / b[i];
    }
}

static inline __attribute__((always_inline)) void k_div_u64_body(int32_t *y)
{
    uint64_t acc = (uint64_t) y[0] + 0x123456789ULL;
    for (int i = 0; i < ARR_N; i++) {
        acc = acc / ((uint64_t) (i | 1));
        y[i] = (int32_t) acc;
    }
}

static inline __attribute__((always_inline)) void k_q15_body(int16_t *y, const int16_t *a, const int16_t *b)
{
    for (int i = 0; i < ARR_N; i++) {
        y[i] = (int16_t) (((int32_t) a[i] * (int32_t) b[i]) >> 15);
    }
}

static inline __attribute__((always_inline)) void k_ctz_body(int32_t *y, const int32_t *a)
{
    for (int i = 0; i < ARR_N; i++) {
        y[i] = __builtin_ctz((uint32_t) a[i] | 1u);
    }
}

static inline __attribute__((always_inline)) void k_clz_body(int32_t *y, const int32_t *a)
{
    for (int i = 0; i < ARR_N; i++) {
        y[i] = __builtin_clz((uint32_t) a[i] | 1u);
    }
}

static inline __attribute__((always_inline)) void k_popcount_body(int32_t *y, const int32_t *a)
{
    for (int i = 0; i < ARR_N; i++) {
        y[i] = __builtin_popcount((uint32_t) a[i]);
    }
}

/* ---------------- 两份包装：XIP / ILM ---------------- */
#define WRAP2(fname, bodycall)                                                        \
    static void fname##_xip(void) { bodycall; }                                       \
    __attribute__((section(".fast"), noinline)) static void fname##_ilm(void) { bodycall; }

WRAP2(w_mac_f32, k_mac_f32_body(f_y, f_a, f_b))
WRAP2(w_div_f32, k_div_f32_body(f_y, f_a, f_b))
WRAP2(w_sqrt_f32, k_sqrt_f32_body(f_y, f_a))
WRAP2(w_sin_f32, k_sin_f32_body(f_y, f_a))
WRAP2(w_cvt, k_cvt_body(i_y, f_a))
WRAP2(w_mac_i32, k_mac_i32_body(i_y, i_a, i_b))
WRAP2(w_div_i32, k_div_i32_body(i_y, i_a, i_b))
WRAP2(w_div_u64, k_div_u64_body(i_y))
WRAP2(w_q15, k_q15_body(s_y, s_a, s_b))
WRAP2(w_ctz, k_ctz_body(i_y, i_a))
WRAP2(w_clz, k_clz_body(i_y, i_a))
WRAP2(w_popcount, k_popcount_body(i_y, i_a))

typedef void (*kfn_t)(void);

typedef struct {
    const char *name;
    kfn_t xip;
    kfn_t ilm;
    uint32_t ops_per_call;   /* 一次调用包含多少个"元素操作" */
    const char *unit;
} kernel_t;

static double measure(kfn_t f, uint32_t reps, uint64_t *cyc_out)
{
    uint64_t best = (uint64_t) -1;
    for (int t = 0; t < 3; t++) {
        uint64_t c0 = hpm_csr_get_core_mcycle();
        for (uint32_t r = 0; r < reps; r++) {
            f();
        }
        uint64_t c1 = hpm_csr_get_core_mcycle();
        if (c1 - c0 < best) {
            best = c1 - c0;
        }
    }
    *cyc_out = best;
    return (double) best / (double) g_cpu_hz;   /* 秒 */
}

static void run_kernel(const kernel_t *k)
{
    uint32_t reps = 1000000u / (k->ops_per_call ? k->ops_per_call : 1u);
    if (reps < 1) {
        reps = 1;
    }
    if (reps > 200000) {
        reps = 200000;
    }
    uint64_t cyc_xip, cyc_ilm;
    double secs = measure(k->xip, reps, &cyc_xip);
    double ops = (double) k->ops_per_call * (double) reps;
    double mops_xip = ops / secs / 1e6;
    double cpo_xip = (double) cyc_xip / ops;

    secs = measure(k->ilm, reps, &cyc_ilm);
    double mops_ilm = ops / secs / 1e6;
    double cpo_ilm = (double) cyc_ilm / ops;

    printf("BENCH_COMPUTE kernel=%-11s unit=%-5s xip_mops=%-9.2f ilm_mops=%-9.2f xip_cyc_op=%.3f ilm_cyc_op=%.3f speedup=%.3f\r\n",
           k->name, k->unit, mops_xip, mops_ilm, cpo_xip, cpo_ilm,
           (cpo_ilm > 0.0) ? (cpo_xip / cpo_ilm) : 0.0);
}

static void run_all(void)
{
    static const kernel_t kernels[] = {
        { "mac_f32",  w_mac_f32_xip,  w_mac_f32_ilm,  ARR_N * 2, "MFLOPS" },
        { "div_f32",  w_div_f32_xip,  w_div_f32_ilm,  ARR_N,     "MOPS" },
        { "sqrt_f32", w_sqrt_f32_xip, w_sqrt_f32_ilm, ARR_N,     "MOPS" },
        { "sin_f32",  w_sin_f32_xip,  w_sin_f32_ilm,  ARR_N,     "MOPS" },
        { "cvt_f2i",  w_cvt_xip,      w_cvt_ilm,      ARR_N,     "MOPS" },
        { "mac_i32",  w_mac_i32_xip,  w_mac_i32_ilm,  ARR_N * 2, "MOPS" },
        { "div_i32",  w_div_i32_xip,  w_div_i32_ilm,  ARR_N,     "MOPS" },
        { "div_u64",  w_div_u64_xip,  w_div_u64_ilm,  ARR_N,     "MOPS" },
        { "q15_gain", w_q15_xip,      w_q15_ilm,      ARR_N,     "MOPS" },
        { "ctz32",    w_ctz_xip,      w_ctz_ilm,      ARR_N,     "MOPS" },
        { "clz32",    w_clz_xip,      w_clz_ilm,      ARR_N,     "MOPS" },
        { "popcount", w_popcount_xip, w_popcount_ilm, ARR_N,     "MOPS" },
    };
    for (unsigned i = 0; i < sizeof(kernels) / sizeof(kernels[0]); i++) {
        run_kernel(&kernels[i]);
    }
    printf("COMPUTE_SWEEP_DONE\r\n");
}

static void cmd_comp(int argc, char *argv[])
{
    (void) argc; (void) argv;
    run_all();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 comp, cmd_comp, run compute throughput sweep);

static void cmd_compinfo(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("cpu        : %lu Hz\r\n", (unsigned long) g_cpu_hz);
    printf("opt        : %s\r\n", BENCH_OPT_STR);
#if defined(__riscv_flen)
    printf("fpu        : hardware FPU, __riscv_flen=%d, abi=%s\r\n", __riscv_flen,
#if defined(__riscv_float_abi_single)
           "ilp32f"
#elif defined(__riscv_float_abi_double)
           "ilp32d"
#else
           "soft-float"
#endif
    );
#else
    printf("fpu        : soft-float (no F extension compiled in)\r\n");
#endif
#if defined(__riscv_zbb)
    printf("zbb        : yes (ctz/clz/cpop/rev8 hardware)\r\n");
#else
    printf("zbb        : no\r\n");
#endif
    printf("arr        : %d elements\r\n", ARR_N);
    printf("ilm body   : mac_f32 = %p (ILM 0x0..0x20000), xip = %p\r\n",
           (void *) w_mac_f32_ilm, (void *) w_mac_f32_xip);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 compinfo, cmd_compinfo, show compute benchmark environment);

int main(void)
{
    bsp_init();
    g_cpu_hz = clock_get_frequency(clock_cpu0);
    printf("\r\n=== bench_compute (hpm5e31_1) ===\r\n");
    printf("board=%s cpu=%.0f MHz opt=%s BENCH_FPU=%d\r\n",
           BOARD_NAME, (double) g_cpu_hz / 1e6, BENCH_OPT_STR,
#if defined(__riscv_flen)
           1
#else
           0
#endif
    );
    printf("commands: comp | compinfo\r\n");
    /* 等 1.5s：hwrun 的串口抓取在烧写后 0.3s 才启动，先等它接上，否则最早的 kernel 输出会丢 */
    board_delay_ms(1500);
    for (int i = 0; i < ARR_N; i++) {
        f_a[i] = (float) (i + 1) * 0.5f;
        f_b[i] = (float) (i + 3) * 0.25f;
        f_y[i] = 1.0f;
        i_a[i] = (i * 37) + 1;
        i_b[i] = (i & 15) + 2;
        i_y[i] = 0;
        s_a[i] = (int16_t) (i * 100);
        s_b[i] = 16384;
        s_y[i] = 0;
    }
    run_all();
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
