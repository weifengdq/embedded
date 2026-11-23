/*
 * bench_memory - 内存带宽基准（hpm5e31_1 / HPM5E31 @480MHz）
 *
 * 测量对象:
 *   - memcpy / memset / memcmp（newlib-nano 库函数）在不同大小 (1B~64KB) 下的吞吐
 *   - 自写 32 位循环 / 16 字展开循环（"手写内核"）的吞吐，与库函数对比
 *   - 三种存储位置: L1 D-Cache 可缓存 AXI SRAM / 非 cacheable AXI SRAM / DLM(紧耦合)
 *   - 跨区域拷贝（cached <-> noncacheable）与目标地址对齐的影响
 *
 * 方法:
 *   mcycle 计时（480MHz，~2ns 分辨率），每个测点重复 3 次取最好值；
 *   通过函数指针调用，避免编译器把库函数内联/常量折叠。
 *
 * 重要发现（见 README/第23章）:
 *   SDK 链接的是 newlib-nano（--specs=nano.specs），其 memcpy 是逐字节实现，
 *   480MHz 下只有 ~96MB/s（5 周期/字节），远低于内存实际带宽；换成 32 位循环快 ~6 倍。
 *   所以"内存带宽"必须用自写内核测，否则测到的是 CPU 指令吞吐。
 *
 * 硬件: hpm5e31_1，UART4 921600 8N1
 * 命令: membw / membwinfo
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "bsp.h"
#include "hpm_csr_drv.h"

#define CACHE_BUF_SIZE (64 * 1024)
#define NC_BUF_SIZE    (16 * 1024)
#define DLM_BUF_SIZE   (32 * 1024)

/* 可缓存 AXI SRAM (.bss -> AXI_SRAM, 走 L1 D-Cache) */
static uint8_t s_cache_a[CACHE_BUF_SIZE] __attribute__((aligned(64)));
static uint8_t s_cache_b[CACHE_BUF_SIZE] __attribute__((aligned(64)));
/* 非 cacheable AXI SRAM (linker: .noncacheable.bss -> AXI_SRAM_NONCACHEABLE) */
static uint8_t s_nc_a[NC_BUF_SIZE] __attribute__((section(".noncacheable.bss"), aligned(64)));
static uint8_t s_nc_b[NC_BUF_SIZE] __attribute__((section(".noncacheable.bss"), aligned(64)));
/* DLM 紧耦合 RAM (linker: .fast_ram.bss -> DLM 0x00200000) */
static uint8_t s_dlm_a[DLM_BUF_SIZE] __attribute__((section(".fast_ram.bss"), aligned(64)));
static uint8_t s_dlm_b[DLM_BUF_SIZE] __attribute__((section(".fast_ram.bss"), aligned(64)));

typedef struct {
    const char *name;
    uint8_t *a;
    uint8_t *b;
    uint32_t max_size;
} mem_region_t;

static uint32_t g_cpu_hz;
static const uint8_t FILL = 0x5a;   /* a/b 同内容：memcmp 才会扫完全部字节 */

typedef void (*memop_fn)(void *dst, const void *src, uint32_t n);

/* ---- 库函数 ---- */
static void op_memcpy(void *d, const void *s, uint32_t n)
{
    memcpy(d, s, n);
}

static void op_memset(void *d, const void *s, uint32_t n)
{
    (void) s;
    memset(d, FILL, n);
}

static void op_memcmp(void *d, const void *s, uint32_t n)
{
    volatile int r = memcmp(d, s, n);  /* 内容相同 -> 扫完全部字节 */
    (void) r;
}

/* ---- 手写内核 ---- */
static void op_copy_u32(void *d, const void *s, uint32_t n)
{
    uint32_t *dp = (uint32_t *) d;
    const uint32_t *sp = (const uint32_t *) s;
    uint32_t words = n >> 2;
    while (words--) {
        *dp++ = *sp++;
    }
    uint8_t *db = (uint8_t *) dp;
    const uint8_t *sb = (const uint8_t *) sp;
    uint32_t rest = n & 3u;
    while (rest--) {
        *db++ = *sb++;
    }
}

static void op_copy_unroll16(void *d, const void *s, uint32_t n)
{
    uint32_t *dp = (uint32_t *) d;
    const uint32_t *sp = (const uint32_t *) s;
    uint32_t blocks = n >> 6;           /* 64B = 16 word */
    while (blocks--) {
        dp[0] = sp[0];   dp[1] = sp[1];   dp[2] = sp[2];   dp[3] = sp[3];
        dp[4] = sp[4];   dp[5] = sp[5];   dp[6] = sp[6];   dp[7] = sp[7];
        dp[8] = sp[8];   dp[9] = sp[9];   dp[10] = sp[10]; dp[11] = sp[11];
        dp[12] = sp[12]; dp[13] = sp[13]; dp[14] = sp[14]; dp[15] = sp[15];
        dp += 16;
        sp += 16;
    }
    uint32_t words = (n >> 2) & 15u;
    while (words--) {
        *dp++ = *sp++;
    }
    uint8_t *db = (uint8_t *) dp;
    const uint8_t *sb = (const uint8_t *) sp;
    uint32_t rest = n & 3u;
    while (rest--) {
        *db++ = *sb++;
    }
}

static void op_memset_u32(void *d, const void *s, uint32_t n)
{
    (void) s;
    uint32_t *dp = (uint32_t *) d;
    uint32_t v = 0x5a5a5a5au;
    uint32_t words = n >> 2;
    while (words--) {
        *dp++ = v;
    }
    uint8_t *db = (uint8_t *) dp;
    uint32_t rest = n & 3u;
    while (rest--) {
        *db++ = (uint8_t) v;
    }
}

static uint32_t reps_for(uint32_t size)
{
    /* 每个测点搬运 ~256KB，重复次数上限 2^18 */
    uint32_t target = 256u * 1024u;
    uint32_t reps = target / size;
    if (reps == 0) {
        reps = 1;
    }
    if (reps > (1u << 18)) {
        reps = 1u << 18;
    }
    return reps;
}

/* 返回 MB/s；best_cyc 输出最少周期数（3 次取最好） */
static double measure(memop_fn f, void *d, const void *s, uint32_t size, uint32_t reps, uint64_t *best_cyc)
{
    uint64_t best = (uint64_t) -1;
    for (int trial = 0; trial < 3; trial++) {
        uint64_t c0 = hpm_csr_get_core_mcycle();
        for (uint32_t r = 0; r < reps; r++) {
            f(d, s, size);
        }
        uint64_t c1 = hpm_csr_get_core_mcycle();
        if (c1 - c0 < best) {
            best = c1 - c0;
        }
    }
    *best_cyc = best;
    double bytes = (double) size * (double) reps;
    double secs = (double) best / (double) g_cpu_hz;
    return bytes / secs / 1e6;
}

static void report(const char *op, const char *region, uint32_t size, uint32_t reps, uint64_t cyc, double mbps)
{
    double cyc_per_byte = (double) cyc / ((double) size * (double) reps);
    printf("BENCH_MEM op=%-13s region=%-5s size=%-6lu reps=%-6lu mbps=%-9.2f cyc_per_byte=%.3f\r\n",
           op, region, (unsigned long) size, (unsigned long) reps, mbps, cyc_per_byte);
}

static void sweep_region(const char *region, uint8_t *a, uint8_t *b, uint32_t max_size)
{
    static const uint32_t sizes[] = { 1, 4, 16, 64, 256, 1024, 4096, 16384, 32768, 65536 };
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        uint32_t size = sizes[i];
        if (size > max_size) {
            continue;
        }
        uint32_t reps = reps_for(size);
        uint64_t cyc;

        report("memcpy", region, size, reps, cyc, measure(op_memcpy, a, b, size, reps, &cyc));
        report("memset", region, size, reps, cyc, measure(op_memset, a, b, size, reps, &cyc));
        report("memcmp", region, size, reps, cyc, measure(op_memcmp, a, b, size, reps, &cyc));
        report("copy_u32", region, size, reps, cyc, measure(op_copy_u32, a, b, size, reps, &cyc));
        report("copy_unroll16", region, size, reps, cyc, measure(op_copy_unroll16, a, b, size, reps, &cyc));
        report("memset_u32", region, size, reps, cyc, measure(op_memset_u32, a, b, size, reps, &cyc));
    }
}

static void sweep_mixed(void)
{
    /* 跨区域拷贝：缓存 <-> 非 cacheable / DLM 的代价（用 32 位内核，避免库函数 CPU 瓶颈） */
    static const uint32_t sizes[] = { 256, 1024, 4096 };
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        uint32_t size = sizes[i];
        uint32_t reps = reps_for(size);
        uint64_t cyc;
        report("copy_u32", "c2nc", size, reps, cyc, measure(op_copy_u32, s_cache_a, s_nc_a, size, reps, &cyc));
        report("copy_u32", "nc2c", size, reps, cyc, measure(op_copy_u32, s_nc_a, s_cache_a, size, reps, &cyc));
        report("copy_u32", "c2dlm", size, reps, cyc, measure(op_copy_u32, s_cache_a, s_dlm_a, size, reps, &cyc));
        report("copy_u32", "dlm2c", size, reps, cyc, measure(op_copy_u32, s_dlm_a, s_cache_a, size, reps, &cyc));
    }
}

static void sweep_align(void)
{
    /* 目标地址偏移 0/1/2/3 字节（源保持 64B 对齐），大小 1024，32 位内核 */
    const uint32_t size = 1024;
    uint32_t reps = reps_for(size);
    for (uint32_t off = 0; off < 4; off++) {
        uint64_t best = (uint64_t) -1;
        for (int trial = 0; trial < 3; trial++) {
            uint64_t c0 = hpm_csr_get_core_mcycle();
            for (uint32_t r = 0; r < reps; r++) {
                memcpy(s_cache_a + off, s_cache_b, size);
            }
            uint64_t c1 = hpm_csr_get_core_mcycle();
            if (c1 - c0 < best) {
                best = c1 - c0;
            }
        }
        char opname[24];
        snprintf(opname, sizeof(opname), "memcpy_dst+%lu", (unsigned long) off);
        double mbps = (double) size * reps / ((double) best / g_cpu_hz) / 1e6;
        report(opname, "cache", size, reps, best, mbps);
    }
}

static void print_tables(void)
{
    static const uint32_t sizes[] = { 1, 4, 16, 64, 256, 1024, 4096, 16384, 32768, 65536 };

    printf("\r\n table 1: cache(AXI SRAM + L1 D-Cache) MB/s, 库函数 vs 手写内核\r\n");
    printf(" size(B)  memcpy  memset   copy_u32  unroll16  memset_u32\r\n");
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        uint32_t size = sizes[i];
        uint32_t reps = reps_for(size);
        uint64_t c;
        double m_memcpy = measure(op_memcpy, s_cache_a, s_cache_b, size, reps, &c);
        double m_memset = measure(op_memset, s_cache_a, s_cache_b, size, reps, &c);
        double m_c32 = measure(op_copy_u32, s_cache_a, s_cache_b, size, reps, &c);
        double m_unr = measure(op_copy_unroll16, s_cache_a, s_cache_b, size, reps, &c);
        double m_set32 = measure(op_memset_u32, s_cache_a, s_cache_b, size, reps, &c);
        printf(" %-7lu  %-7.1f %-7.1f  %-9.1f %-9.1f %-9.1f\r\n",
               (unsigned long) size, m_memcpy, m_memset, m_c32, m_unr, m_set32);
    }

    printf("\r\n table 2: 三种存储位置 @ size=4096, MB/s\r\n");
    printf(" kernel        cache      dlm        nc\r\n");
    const mem_region_t regions[3] = {
        { "cache", s_cache_a, s_cache_b, CACHE_BUF_SIZE },
        { "dlm", s_dlm_a, s_dlm_b, DLM_BUF_SIZE },
        { "nc", s_nc_a, s_nc_b, NC_BUF_SIZE },
    };
    const char *names[3] = { "copy_u32", "copy_unroll16", "memset_u32" };
    memop_fn fns[3] = { op_copy_u32, op_copy_unroll16, op_memset_u32 };
    for (int k = 0; k < 3; k++) {
        printf(" %-13s", names[k]);
        for (int r = 0; r < 3; r++) {
            uint64_t c;
            printf(" %-10.1f", measure(fns[k], regions[r].a, regions[r].b, 4096, reps_for(4096), &c));
        }
        printf("\r\n");
    }
}

static void run_all(void)
{
    printf("\r\n=== bench_memory: memcpy/memset/memcmp throughput ===\r\n");
    printf("cpu=%.0f MHz  buffers: cache=%dKB nc=%dKB dlm=%dKB  opt=%s\r\n",
           (double) g_cpu_hz / 1e6, CACHE_BUF_SIZE / 1024, NC_BUF_SIZE / 1024, DLM_BUF_SIZE / 1024, BENCH_OPT_STR);
    printf("cache=AXI_SRAM(.bss,L1 D-Cache)  nc=AXI_SRAM_NONCACHEABLE  dlm=0x00200000(DLM)\r\n");
    printf("note: libc memcpy 来自 newlib-nano，逐字节实现，是 CPU 瓶颈而非内存瓶颈\r\n");

    sweep_region("cache", s_cache_a, s_cache_b, CACHE_BUF_SIZE);
    sweep_region("dlm", s_dlm_a, s_dlm_b, DLM_BUF_SIZE);
    sweep_region("nc", s_nc_a, s_nc_b, NC_BUF_SIZE);
    sweep_mixed();
    sweep_align();
    print_tables();
    printf("MEM_SWEEP_DONE\r\n");
}

static void cmd_membw(int argc, char *argv[])
{
    (void) argc; (void) argv;
    run_all();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 membw, cmd_membw, run memory bandwidth sweep);

static void cmd_membwinfo(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("cpu      : %lu Hz\r\n", (unsigned long) g_cpu_hz);
    printf("cache buf: %p..%p (64KB, .bss/AXI_SRAM)\r\n", (void *) s_cache_a, (void *) (s_cache_a + CACHE_BUF_SIZE));
    printf("nc buf   : %p..%p (16KB, .noncacheable.bss)\r\n", (void *) s_nc_a, (void *) (s_nc_a + NC_BUF_SIZE));
    printf("dlm buf  : %p..%p (32KB, .fast_ram.bss/DLM)\r\n", (void *) s_dlm_a, (void *) (s_dlm_a + DLM_BUF_SIZE));
    printf("opt      : %s\r\n", BENCH_OPT_STR);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 membwinfo, cmd_membwinfo, show memory benchmark buffers);

int main(void)
{
    bsp_init();
    g_cpu_hz = clock_get_frequency(clock_cpu0);
    printf("\r\n=== bench_memory (hpm5e31_1) ===\r\n");
    printf("commands: membw | membwinfo\r\n");
    memset(s_cache_a, FILL, CACHE_BUF_SIZE);
    memset(s_cache_b, FILL, CACHE_BUF_SIZE);
    memset(s_nc_a, FILL, NC_BUF_SIZE);
    memset(s_nc_b, FILL, NC_BUF_SIZE);
    memset(s_dlm_a, FILL, DLM_BUF_SIZE);
    memset(s_dlm_b, FILL, DLM_BUF_SIZE);
    run_all();
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
