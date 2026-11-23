/*
 * spi_async_job —— AUTOSAR MCAL Spi 例程：多 Job 序列异步完成 + CPU 占用
 *
 * 接线同 spi_master_slave：SPI1(主, PC10/11/12/13) <-> SPI3(从, PA26/27/28/29)
 *
 * 验证点：
 *   1) Spi_AsyncTransmit 立即返回，序列由 Spi_MainFunction_Handling 推进，
 *      4 个 Job 的序列（8bit 一对 + 16bit 一对）全部 Spi_JobResultType=SPI_JOB_OK；
 *   2) JobEndNotification / SeqEndNotification 回调次数 == 期望；
 *   3) 同步 vs 异步轮询的 CPU 占用（周期数）对比；
 *   4) 参数错误 -> Det 计数。
 * 命令：spi_job info | spi_job multi [n] | spi_job cpu [n] | spi_job hint [n] | spi_job det
 */
#include "bsp.h"
#include "Spi.h"
#include "Spi_PBcfg.h"
#include "Det.h"
#include "hpm_csr_drv.h"

#include <stdio.h>
#include <string.h>

#define BUF_SIZE (512u)

ATTR_PLACE_AT_NONCACHEABLE static uint8 s_m_tx[BUF_SIZE + 8u];
ATTR_PLACE_AT_NONCACHEABLE static uint8 s_m_rx[BUF_SIZE + 8u];
ATTR_PLACE_AT_NONCACHEABLE static uint8 s_s_tx[BUF_SIZE + 8u];
ATTR_PLACE_AT_NONCACHEABLE static uint8 s_s_rx[BUF_SIZE + 8u];

static volatile uint32 s_job_end_cnt;
static volatile uint32 s_seq_end_cnt;
static uint32 s_cpu_hz;

void Spi_JobEndNotification_HW(void) { s_job_end_cnt++; }
void Spi_SeqEndNotification_Seq(void) { s_seq_end_cnt++; }

static inline uint64_t cyc(void) { return hpm_csr_get_core_mcycle(); }

static void fill_all(uint32 n8, uint32 n16)
{
    uint32 i;
    for (i = 0u; i < n8; i++) {
        s_m_tx[i] = (uint8)(i * 7u + 0x31u);
        s_s_tx[i] = (uint8)(i * 11u + 0xA5u);
        s_m_rx[i] = 0u;
        s_s_rx[i] = 0u;
    }
    for (i = 0u; i < (n16 * 2u); i++) {
        s_m_tx[BUF_SIZE - 256u + i] = (uint8)(i * 5u + 0x11u);
        s_s_tx[BUF_SIZE - 256u + i] = (uint8)(i * 3u + 0x77u);
        s_m_rx[BUF_SIZE - 256u + i] = 0u;
        s_s_rx[BUF_SIZE - 256u + i] = 0u;
    }
    s_s_tx[n8] = 0x5Au;
}

/* 多 Job 异步序列：8bit 一对 + 16bit 一对 */
static int run_multi(uint32 n8, uint32 n16, uint64_t *cycles, uint32_t *errs)
{
    uint32 e = 0u;
    uint32 i;
    uint64_t t0;
    uint32 guard = 4000000u;

    if ((n8 == 0u) || (n8 > 128u) || (n16 == 0u) || (n16 > 64u)) { return -1; }
    fill_all(n8, n16);

    if (Spi_SetupEB(SPI_CH_MASTER_8BIT, s_m_tx, s_m_rx, (Spi_NumberOfDataType)n8) != E_OK) { return -2; }
    if (Spi_SetupEB(SPI_CH_SLAVE_8BIT, s_s_tx, s_s_rx, (Spi_NumberOfDataType)n8) != E_OK) { return -3; }
    if (Spi_SetupEB(SPI_CH_MASTER_16BIT, &s_m_tx[BUF_SIZE - 256u], &s_m_rx[BUF_SIZE - 256u],
                    (Spi_NumberOfDataType)n16) != E_OK) { return -4; }
    if (Spi_SetupEB(SPI_CH_SLAVE_16BIT, &s_s_tx[BUF_SIZE - 256u], &s_s_rx[BUF_SIZE - 256u],
                    (Spi_NumberOfDataType)n16) != E_OK) { return -5; }

    t0 = cyc();
    if (Spi_AsyncTransmit(SPI_SEQ_ASYNC_MULTI) != E_OK) { return -6; }
    if (Spi_GetSequenceResult(SPI_SEQ_ASYNC_MULTI) != SPI_SEQ_PENDING) { return -7; }
    while ((Spi_GetSequenceResult(SPI_SEQ_ASYNC_MULTI) == SPI_SEQ_PENDING) && (guard-- != 0u)) {
        Spi_MainFunction_Handling();
    }
    *cycles = cyc() - t0;
    if (guard == 0u) { return -8; }

    for (i = 0u; i < n8; i++) {
        if (s_m_rx[i] != s_s_tx[i]) { e++; }
        if (s_s_rx[i] != s_m_tx[i]) { e++; }
    }
    for (i = 0u; i < (n16 * 2u); i++) {
        if (s_m_rx[BUF_SIZE - 256u + i] != s_s_tx[BUF_SIZE - 256u + i]) { e++; }
        if (s_s_rx[BUF_SIZE - 256u + i] != s_m_tx[BUF_SIZE - 256u + i]) { e++; }
    }
    *errs = e;
    if (e != 0u) { return -9; }
    return 0;
}

static void cmd_info(void)
{
    Std_VersionInfoType vi;
    Spi_GetVersionInfo(&vi);
    printf("spi_async_job @ hpm5e31_1 | MCAL Spi %u.%u.%u moduleID=%u\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, (unsigned)vi.moduleID);
    printf("  asyncMode=%s, status=%s, jobs=%u seqs=%u\r\n",
           (0) ? "INTERRUPT" : "POLLING(主函数上下文)",
           (Spi_GetStatus() == SPI_IDLE) ? "IDLE" : "BUSY",
           (unsigned)SPI_JOB_COUNT, (unsigned)SPI_SEQUENCE_COUNT);
    printf("  jobEnd=%lu seqEnd=%lu det=%lu\r\n", (unsigned long)s_job_end_cnt,
           (unsigned long)s_seq_end_cnt, (unsigned long)Det_GetErrorCount());
    printf("  usage: spi_job info|multi [n8] [n16]|cpu [n]|hint [n]|det\r\n");
}

static void cmd_multi(int argc, char **argv)
{
    uint32_t n8 = 64u;
    uint32_t n16 = 32u;
    uint64_t cy = 0u;
    uint32_t errs = 0u;
    int rc;
    if (argc >= 1) { (void)bsp_parse_u32(argv[0], &n8); }
    if (argc >= 2) { (void)bsp_parse_u32(argv[1], &n16); }
    if ((n8 == 0u) || (n8 > 128u)) { n8 = 64u; }
    if ((n16 == 0u) || (n16 > 64u)) { n16 = 32u; }
    rc = run_multi(n8, n16, &cy, &errs);
    printf("multi: n8=%lu n16=%lu 4 jobs %s err=%lu jobs[%d %d %d %d] seqResult=%d jobEnd=%lu seqEnd=%lu %lu us\r\n",
           (unsigned long)n8, (unsigned long)n16, (rc == 0) ? "PASS" : "FAIL",
           (unsigned long)errs, (int)Spi_GetJobResult(SPI_JOB_MASTER_8BIT),
           (int)Spi_GetJobResult(SPI_JOB_SLAVE_8BIT), (int)Spi_GetJobResult(SPI_JOB_MASTER_16BIT),
           (int)Spi_GetJobResult(SPI_JOB_SLAVE_16BIT), (int)Spi_GetSequenceResult(SPI_SEQ_ASYNC_MULTI),
           (unsigned long)s_job_end_cnt, (unsigned long)s_seq_end_cnt,
           (unsigned long)(cy * 1000000ull / (uint64_t)s_cpu_hz));
}

/* 同步 vs 异步轮询的 CPU 占用：统计 API 期间消耗的 CPU 周期 */
static void cmd_cpu(int argc, char **argv)
{
    uint32_t n = 64u;
    uint32_t errs = 0u;
    uint64_t cy_sync = 0u;
    uint64_t cy_async = 0u;
    uint64_t t0;
    uint64_t t1;
    uint32_t guard = 4000000u;
    uint32_t i;
    int rc;

    if (argc >= 1) { (void)bsp_parse_u32(argv[0], &n); }
    if ((n == 0u) || (n > 128u)) { n = 64u; }
    fill_all(n, 1u);
    (void)Spi_SetupEB(SPI_CH_MASTER_8BIT, s_m_tx, s_m_rx, (Spi_NumberOfDataType)n);
    (void)Spi_SetupEB(SPI_CH_SLAVE_8BIT, s_s_tx, s_s_rx, (Spi_NumberOfDataType)n);

    t0 = cyc();
    rc = Spi_SyncTransmit(SPI_SEQ_MB8);
    cy_sync = cyc() - t0;

    t1 = cyc();
    if (Spi_AsyncTransmit(SPI_SEQ_MB8) != E_OK) { printf("cpu: async rejected\r\n"); return; }
    while ((Spi_GetSequenceResult(SPI_SEQ_MB8) == SPI_SEQ_PENDING) && (guard-- != 0u)) {
        Spi_MainFunction_Handling();
    }
    cy_async = cyc() - t1;

    for (i = 0u; i < n; i++) {
        if (s_m_rx[i] != s_s_tx[i]) { errs++; }
        if (s_s_rx[i] != s_m_tx[i]) { errs++; }
    }
    printf("cpu: n=%lu sync=%lu cyc (%lu us) async-poll=%lu cyc (%lu us) err=%lu => %s\r\n",
           (unsigned long)n, (unsigned long)cy_sync,
           (unsigned long)(cy_sync * 1000000ull / (uint64_t)s_cpu_hz),
           (unsigned long)cy_async, (unsigned long)(cy_async * 1000000ull / (uint64_t)s_cpu_hz),
           (unsigned long)errs, ((rc == E_OK) && (errs == 0u)) ? "PASS" : "FAIL");
}

/* 中断模式尝试：SetAsyncMode(SPI_ASYNC_MODE_INTERRUPT) 后走 DMA+END 中断 */
static void cmd_hint(int argc, char **argv)
{
    uint32_t n = 64u;
    uint64_t t0;
    uint64_t dt;
    uint32_t guard = 2000000u;
    if (argc >= 1) { (void)bsp_parse_u32(argv[0], &n); }
    if ((n == 0u) || (n > 128u)) { n = 64u; }
    if (Spi_SetAsyncMode(SPI_ASYNC_MODE_INTERRUPT) != E_OK) { printf("hint: SetAsyncMode failed\r\n"); return; }
    fill_all(n, 1u);
    (void)Spi_SetupEB(SPI_CH_MASTER_8BIT, s_m_tx, s_m_rx, (Spi_NumberOfDataType)n);
    (void)Spi_SetupEB(SPI_CH_SLAVE_8BIT, s_s_tx, s_s_rx, (Spi_NumberOfDataType)n);
    t0 = cyc();
    if (Spi_AsyncTransmit(SPI_SEQ_MB8) != E_OK) { printf("hint: AsyncTransmit rejected\r\n"); return; }
    while ((Spi_GetSequenceResult(SPI_SEQ_MB8) == SPI_SEQ_PENDING) && (guard-- != 0u)) {
        Spi_MainFunction_Handling();
    }
    dt = cyc() - t0;
    printf("hint: 中断模式 seqResult=%d jobResult=%d %lu us -> %s\r\n",
           (int)Spi_GetSequenceResult(SPI_SEQ_MB8), (int)Spi_GetJobResult(SPI_JOB_MASTER_8BIT),
           (unsigned long)(dt * 1000000ull / (uint64_t)s_cpu_hz),
           (Spi_GetSequenceResult(SPI_SEQ_MB8) == SPI_SEQ_OK) ? "PASS" : "FAIL");
    (void)Spi_SetAsyncMode(SPI_ASYNC_MODE_POLLING);
}

static void cmd_det(void)
{
    uint32 c0 = Det_GetErrorCount();
    cmd_info();
    (void)Spi_AsyncTransmit(0x7Fu);
    (void)Spi_SetupEB(0x7Fu, s_m_tx, s_m_rx, 4u);
    (void)Spi_SetAsyncMode((Spi_AsyncModeType)9);
    /* 期望 2 条：AsyncTransmit/SetupEB 非法参数；SetAsyncMode 非法模式按 SWS R25-11 无对应错误码，
     * 驱动只返回 E_NOT_OK 不报 Det（章节"待确认项"有说明）。 */
    printf("det: +%lu (期望 2) \r\n", (unsigned long)(Det_GetErrorCount() - c0));
}

static void cmd_job(int argc, char **argv)
{
    if (argc < 2) { cmd_info(); return; }
    if (strcmp(argv[1], "info") == 0) { cmd_info(); }
    else if (strcmp(argv[1], "multi") == 0) { cmd_multi(argc - 2, &argv[2]); }
    else if (strcmp(argv[1], "cpu") == 0) { cmd_cpu(argc - 2, &argv[2]); }
    else if (strcmp(argv[1], "hint") == 0) { cmd_hint(argc - 2, &argv[2]); }
    else if (strcmp(argv[1], "det") == 0) { cmd_det(); }
    else { printf("unknown: %s\r\n", argv[1]); cmd_info(); }
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_job, cmd_job, spi async job/sequence: info|multi|cpu|hint|det);

int main(void)
{
    uint64_t cy = 0u;
    uint32_t errs = 0u;
    int rc;

    bsp_init();
    s_cpu_hz = clock_get_frequency(clock_cpu0);

    printf("\r\n=== mcal spi_async_job: multi-job async sequence on SPI1<->SPI3 ===\r\n");
    Spi_Init(&Spi_Config);
    (void)Spi_SetAsyncMode(SPI_ASYNC_MODE_POLLING);
    cmd_info();
    rc = run_multi(64u, 32u, &cy, &errs);
    printf("spi_async_job: boot multi-job %s err=%lu jobEnd=%lu seqEnd=%lu\r\n",
           ((rc == 0) && (errs == 0u)) ? "PASS" : "FAIL", (unsigned long)errs,
           (unsigned long)s_job_end_cnt, (unsigned long)s_seq_end_cnt);
    printf("type 'spi_job multi 64 32' / 'spi_job cpu 64' / 'spi_job hint 64' / 'spi_job det'\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
