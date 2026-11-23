/*
 * bench_bus - 总线/存储吞吐基准（hpm5e31_1 / HPM5E31 @480MHz）
 *
 * 测什么:
 *   1) XPI flash 顺序读带宽（MB/s）:
 *      - cold: 每一轮拷贝前 invalidate D-Cache 中该 flash 区间 -> 每轮都真正走 XPI 总线
 *      - warm: 不 invalidate -> L1 D-Cache 命中（受 cache 容量限制）
 *      两种搬运内核：libc memcpy（newlib-nano，字节级实现，先撞 CPU 上限）与 16 字展开内核
 *   2) UART 实际发送吞吐（B/s、bit/s）vs 配置波特率 921600（等 TEMT 真正发送完再计时）
 *   3) 总线时钟树（ahb/axi/axif/axis/axic/xpi0/mchtmr）
 *
 * 方法: mcycle 计时，每测点 3 次取最好。
 *
 * 硬件: hpm5e31_1，UART4 921600 8N1
 * 命令: bus / xiprep / uartbw / busclocks / businfo
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "bsp.h"
#include "hpm_csr_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_l1c_drv.h"
#include "hpm_uart_drv.h"

#define FLASH_SRC_SIZE (64 * 1024)
#define DST_SIZE       (64 * 1024)

/* 放在 flash(.rodata) 的只读数据源：链接脚本把 .rodata 归到 FLASH(0x80000000) */
static const uint8_t s_flash_src[FLASH_SRC_SIZE] __attribute__((aligned(64))) = {
    0x5a, 0xa5, 0x11, 0x22,
};
static uint8_t s_dst[DST_SIZE] __attribute__((aligned(64)));

static uint32_t g_cpu_hz;

/* 16 字(64B)展开拷贝内核 */
static void copy_unroll16(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint32_t *dp = (uint32_t *) dst;
    const uint32_t *sp = (const uint32_t *) src;
    uint32_t blocks = n >> 6;
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
}

static double measure_copy(const uint8_t *src, uint32_t size, uint32_t reps, int cold, int unroll,
                           uint64_t *cyc_out)
{
    uint64_t best = (uint64_t) -1;
    for (int t = 0; t < 3; t++) {
        uint64_t c0 = hpm_csr_get_core_mcycle();
        for (uint32_t r = 0; r < reps; r++) {
            if (cold) {
                /* flash 只读，invalidate 不会丢数据；放循环内保证每轮都走 XPI。
                   invalidate 自身耗时也计入（按 cache line 计，量级远小于拷贝） */
                l1c_dc_invalidate((uint32_t) (uintptr_t) src, size);
            }
            if (unroll) {
                copy_unroll16(s_dst, src, size);
            } else {
                memcpy(s_dst, src, size);
            }
        }
        uint64_t c1 = hpm_csr_get_core_mcycle();
        if (c1 - c0 < best) {
            best = c1 - c0;
        }
    }
    *cyc_out = best;
    double bytes = (double) size * (double) reps;
    double secs = (double) best / (double) g_cpu_hz;
    return bytes / secs / 1e6;
}

static void xip_report(const char *op, int unroll)
{
    static const uint32_t sizes[] = { 1024, 4096, 16384, 32768, 65536 };
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        uint32_t size = sizes[i];
        uint32_t reps = (uint32_t) (262144u / size);
        if (reps == 0) {
            reps = 1;
        }
        uint64_t cx, cw;
        double cold = measure_copy(s_flash_src, size, reps, 1, unroll, &cx);
        double warm = measure_copy(s_flash_src, size, reps, 0, unroll, &cw);
        printf("BENCH_BUS op=%-10s size=%-6lu reps=%-4lu cold_mbps=%-9.2f warm_mbps=%-9.2f cold_cyc_byte=%.3f warm_cyc_byte=%.3f\r\n",
               op, (unsigned long) size, (unsigned long) reps, cold, warm,
               (double) cx / ((double) size * reps), (double) cw / ((double) size * reps));
    }
}

static void run_xiprep(void)
{
    printf("\r\n=== XPI flash read bandwidth (src=%p in FLASH) ===\r\n", (void *) s_flash_src);
    xip_report("xip_memcpy", 0);
    xip_report("xip_unroll", 1);
    printf("note: cold=每轮 invalidate D-Cache 后读（走 XPI 总线）; warm=L1 D-Cache 命中\r\n");
    printf("      libc memcpy 是 newlib-nano 字节级实现，CPU 上限约 96MB/s，会掩盖 XPI 峰值带宽\r\n");
}

static uint8_t s_tx_str[16 * 1024];

static void run_uartbw(void)
{
    /* 方法 A：SDK 的控制台路径（printf -> _write -> 轮询 THR），测"应用真实可用"的吞吐 */
    for (unsigned i = 0; i < sizeof(s_tx_str) - 1; i++) {
        s_tx_str[i] = (uint8_t) ('A' + (i % 26));
    }
    s_tx_str[sizeof(s_tx_str) - 1] = 0;

    while (!uart_check_status(BOARD_APP_UART_BASE, uart_stat_transmitter_empty)) {
    }
    uint64_t c0 = hpm_csr_get_core_mcycle();
    printf("%s", (const char *) s_tx_str);
    fflush(stdout);
    while (!uart_check_status(BOARD_APP_UART_BASE, uart_stat_transmitter_empty)) {
    }
    uint64_t c1 = hpm_csr_get_core_mcycle();
    double secs = (double) (c1 - c0) / (double) g_cpu_hz;
    uint32_t n = (uint32_t) (sizeof(s_tx_str) - 1);
    printf("\r\n\r\nBENCH_BUS op=uart_tx_console bytes=%lu secs=%.6f bytes_per_s=%.1f bit_per_s=%.1f baud_cfg=921600 theoretical_Bps=92160\r\n",
           (unsigned long) n, secs, (double) n / secs, (double) n * 10.0 / secs);

    /* 方法 B：直接写 THR（保留作对照，注意 FIFO 语义） */
    c0 = hpm_csr_get_core_mcycle();
    for (uint32_t i = 0; i < n; i++) {
        while (!uart_check_status(BOARD_APP_UART_BASE, uart_stat_tx_slot_avail)) {
        }
        uart_write_byte(BOARD_APP_UART_BASE, (uint8_t) ('a' + (i % 26)));
    }
    while (!uart_check_status(BOARD_APP_UART_BASE, uart_stat_transmitter_empty)) {
    }
    c1 = hpm_csr_get_core_mcycle();
    secs = (double) (c1 - c0) / (double) g_cpu_hz;
    printf("\r\nBENCH_BUS op=uart_tx_reg bytes=%lu secs=%.6f bytes_per_s=%.1f baud_cfg=921600\r\n",
           (unsigned long) n, secs, (double) n / secs);
}

static void run_busclocks(void)
{
    printf("\r\n=== bus clocks ===\r\n");
    printf("cpu0   : %lu Hz\r\n", (unsigned long) clock_get_frequency(clock_cpu0));
    printf("ahb0   : %lu Hz\r\n", (unsigned long) clock_get_frequency(clock_ahb0));
    printf("axis   : %lu Hz\r\n", (unsigned long) clock_get_frequency(clock_axis));
    printf("axic   : %lu Hz\r\n", (unsigned long) clock_get_frequency(clock_axic));
    printf("axif   : %lu Hz\r\n", (unsigned long) clock_get_frequency(clock_axif));
    printf("mchtmr : %lu Hz\r\n", (unsigned long) clock_get_frequency(clock_mchtmr0));
    printf("xpi0   : %lu Hz\r\n", (unsigned long) clock_get_frequency(clock_xpi0));
    printf("理论 AXI(S) 32bit 带宽 = %.0f MB/s\r\n", (double) clock_get_frequency(clock_axis) * 4.0 / 1e6);
}

static void cmd_bus(int argc, char *argv[])
{
    (void) argc; (void) argv;
    run_busclocks();
    run_xiprep();
    run_uartbw();
    printf("BUS_SWEEP_DONE\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 bus, cmd_bus, run bus throughput sweep);

static void cmd_xiprep(int argc, char *argv[])
{
    (void) argc; (void) argv;
    run_xiprep();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 xiprep, cmd_xiprep, measure XPI flash read bandwidth);

static void cmd_uartbw(int argc, char *argv[])
{
    (void) argc; (void) argv;
    run_uartbw();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uartbw, cmd_uartbw, measure UART TX throughput);

static void cmd_busclocks(int argc, char *argv[])
{
    (void) argc; (void) argv;
    run_busclocks();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 busclocks, cmd_busclocks, print bus clock tree);

static void cmd_businfo(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("cpu      : %lu Hz\r\n", (unsigned long) g_cpu_hz);
    printf("flash src: %p (%d KB, FLASH 0x80000000 映射)\r\n", (void *) s_flash_src, FLASH_SRC_SIZE / 1024);
    printf("dst      : %p (%d KB, AXI SRAM cached)\r\n", (void *) s_dst, DST_SIZE / 1024);
    printf("opt      : %s\r\n", BENCH_OPT_STR);
    printf("uart     : UART4 @921600\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 businfo, cmd_businfo, show bus benchmark info);

int main(void)
{
    bsp_init();
    g_cpu_hz = clock_get_frequency(clock_cpu0);
    printf("\r\n=== bench_bus (hpm5e31_1) ===\r\n");
    printf("commands: bus | xiprep | uartbw | busclocks | businfo\r\n");
    board_delay_ms(1500);
    cmd_bus(0, NULL);
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
