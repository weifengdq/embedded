/*
 * mem_bench —— AUTOSAR Mem 驱动的擦除/编程/读取吞吐与作业延迟实测
 *
 * 测什么：
 *  1) 擦除吞吐：擦 N 个 4KB 扇区，Mem_MainFunction 每次擦 1 个，统计 ms / KB·s^-1；
 *  2) 编程吞吐：写 N KB（每页 256B 一次 MainFunction），统计 ms / KB·s^-1；
 *  3) 读取吞吐：Mem_Read 走 MainFunction 分块拷贝，对比 XIP 直读 memcpy 基线；
 *  4) 作业延迟：每个作业需要多少次 Mem_MainFunction、单次调用最长阻塞多少 us。
 * Flash 寿命：默认只测 16KB、2 轮（擦写 ~128KB 量级），参数可调但请勿频繁大范围擦写。
 */
#include <string.h>
#include "bsp.h"
#include "Mem.h"
#include "Mem_PBcfg.h"
#include "Det.h"
#include "hpm_mchtmr_drv.h"

#define BENCH_ADDR      (0x800F0000UL)     /* 保留窗口起点 */
#define BENCH_SECTORS   (4UL)              /* 4 x 4KB = 16KB */
#define BENCH_SIZE      (BENCH_SECTORS * 4096UL)
#define PASS_STR        "MEMBENCH: PASS"

static uint32_t s_tmr_hz;
static uint8_t  s_wbuf[BENCH_SIZE] __attribute__((aligned(4)));
static uint8_t  s_rbuf[BENCH_SIZE] __attribute__((aligned(4)));
static uint8_t  s_cpy[BENCH_SIZE] __attribute__((aligned(4)));

static double ticks_to_ms(uint32_t t) { return (double)t * 1000.0 / (double)s_tmr_hz; }

/* 跑完一个作业，返回 MainFunction 调用次数与单次最长阻塞时间（ms） */
static uint32_t run_job(uint32_t timeout_ms, uint32_t *total_ms, double *max_call_ms)
{
    uint32_t calls = 0U, last, now;
    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    uint32_t limit = (uint32_t)(((uint64_t)timeout_ms * (uint64_t)s_tmr_hz) / 1000ULL);
    uint32_t worst = 0U;

    *max_call_ms = 0.0;
    while (Mem_IsJobPending()) {
        last = mchtmr_get_count(HPM_MCHTMR);
        Mem_MainFunction();
        now = mchtmr_get_count(HPM_MCHTMR);
        if ((now - last) > worst) { worst = now - last; }
        calls++;
        if ((now - t0) > limit) { break; }
    }
    *total_ms = (uint32_t)(mchtmr_get_count(HPM_MCHTMR) - t0);
    *max_call_ms = ticks_to_ms(worst);
    return calls;
}

static void cmd_membench(int argc, char **argv)
{
    uint32_t total_ms = 0U, calls;
    double max_call_ms;
    double erase_ms, prog_ms, read_ms, xip_ms;
    boolean ok = TRUE;

    (void)argc; (void)argv;
    printf("--- mem bench: %lu KB @0x%08lx (reserved window) ---\r\n",
           (unsigned long)(BENCH_SIZE / 1024U), (unsigned long)BENCH_ADDR);

    /* 1) 擦除 */
    if (Mem_Erase(0U, BENCH_ADDR, BENCH_SIZE) != E_OK) {
        printf("erase not accepted\r\n"); return;
    }
    calls = run_job(5000U, &total_ms, &max_call_ms);
    erase_ms = ticks_to_ms(total_ms);
    ok = ok && (Mem_GetJobResult(0U) == MEM_JOB_OK);
    printf("erase : %7.3f ms  %8.1f KB/s  %lu sectors, mf_calls=%lu, max_call=%.3f ms\r\n",
           erase_ms, (double)(BENCH_SIZE / 1024U) / (erase_ms / 1000.0),
           (unsigned long)BENCH_SECTORS, (unsigned long)calls, max_call_ms);

    /* 2) 编程 */
    for (uint32_t i = 0; i < BENCH_SIZE; i++) {
        s_wbuf[i] = (uint8_t)((i * 29U + 0x3CU) & 0xFFU);
    }
    if (Mem_Write(0U, BENCH_ADDR, s_wbuf, BENCH_SIZE) != E_OK) {
        printf("write not accepted\r\n"); return;
    }
    calls = run_job(8000U, &total_ms, &max_call_ms);
    prog_ms = ticks_to_ms(total_ms);
    ok = ok && (Mem_GetJobResult(0U) == MEM_JOB_OK);
    printf("write : %7.3f ms  %8.1f KB/s  %lu pages,  mf_calls=%lu, max_call=%.3f ms\r\n",
           prog_ms, (double)(BENCH_SIZE / 1024U) / (prog_ms / 1000.0),
           (unsigned long)(BENCH_SIZE / 256U), (unsigned long)calls, max_call_ms);

    /* 3) 读（Mem_Read 走 MainFunction 分块） */
    memset(s_rbuf, 0, sizeof(s_rbuf));
    if (Mem_Read(0U, BENCH_ADDR, s_rbuf, BENCH_SIZE) != E_OK) {
        printf("read not accepted\r\n"); return;
    }
    calls = run_job(5000U, &total_ms, &max_call_ms);
    read_ms = ticks_to_ms(total_ms);
    ok = ok && (Mem_GetJobResult(0U) == MEM_JOB_OK) && (memcmp(s_rbuf, s_wbuf, BENCH_SIZE) == 0);
    printf("read  : %7.3f ms  %8.1f KB/s  %lu chunks, mf_calls=%lu, max_call=%.3f ms\r\n",
           read_ms, (double)(BENCH_SIZE / 1024U) / (read_ms / 1000.0),
           (unsigned long)(BENCH_SIZE / 512U), (unsigned long)calls, max_call_ms);

    /* 4) XIP 直读基线（memcpy，含 D-Cache） */
    {
        uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
        memcpy(s_cpy, (const void *)(uintptr_t)BENCH_ADDR, BENCH_SIZE);
        xip_ms = ticks_to_ms(mchtmr_get_count(HPM_MCHTMR) - t0);
        printf("xip   : %7.3f ms  %8.1f KB/s  (memcpy baseline,不含 Mem 作业开销)\r\n",
               xip_ms, (double)(BENCH_SIZE / 1024U) / (xip_ms / 1000.0));
        ok = ok && (memcmp(s_cpy, s_wbuf, BENCH_SIZE) == 0);
    }

    printf("throughput summary: erase=%.1f KB/s write=%.1f KB/s read=%.1f KB/s\r\n",
           (double)(BENCH_SIZE / 1024U) / (erase_ms / 1000.0),
           (double)(BENCH_SIZE / 1024U) / (prog_ms / 1000.0),
           (double)(BENCH_SIZE / 1024U) / (read_ms / 1000.0));
    printf("%s\r\n", ok ? PASS_STR : "MEMBENCH: FAIL");
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 membench, cmd_membench, "Mem: erase/write/read throughput + job latency");

int main(void)
{
    Std_VersionInfoType vi;

    bsp_init();
    s_tmr_hz = clock_get_frequency(clock_mchtmr0);
    Det_Init();
    Mem_Init(&Mem_Config);

    printf("\r\n=== mem_bench: Mem driver throughput (internal XPI NOR) ===\r\n");
    Mem_GetVersionInfo(&vi);
    printf("Mem %u.%u.%u module=%u, window 0x%08lx..0x%08lx\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version, (unsigned)vi.sw_patch_version,
           (unsigned)vi.moduleID, (unsigned long)BENCH_ADDR, (unsigned long)(BENCH_ADDR + 0x10000UL));
    printf("shell: membench  (只擦写保留窗口内 %lu KB，注意 Flash 寿命)\r\n",
           (unsigned long)(BENCH_SIZE / 1024U));

    board_delay_ms(1500);
    cmd_membench(0, NULL);

    while (1) {
        Mem_MainFunction();
        bsp_shell_poll();
    }
    return 0;
}
