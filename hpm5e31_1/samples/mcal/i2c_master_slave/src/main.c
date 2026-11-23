/*
 * i2c_master_slave —— AUTOSAR MCAL I2C 例程：I2C2(控制器) <-> I2C3(目标，地址 0x50)
 *
 * 接线（docs/MCAL开发约定.md §2）：
 *   I2C2_SCL PD02 <-> I2C3_SCL PD07；I2C2_SDA PD03 <-> I2C3_SDA PD06
 * 用法：只用 MCAL API（I2C_Init/I2C_SetupEB/I2C_SyncTransmit/I2C_MainFunction/...）。
 * 命令：i2c info | init 100k|400k|1m | xfer [n] | async [n] | ber [count] | det
 *       i2c regs | rdloop [n] [cnt] | wrloop [n] [cnt]
 */
#include "bsp.h"
#include "I2c.h"
#include "I2c_PBcfg.h"
#include "Det.h"
#include "hpm_csr_drv.h"
#include "hpm_soc.h"

#include <stdio.h>
#include <string.h>

#define BUF_SIZE (256u)

ATTR_PLACE_AT_NONCACHEABLE static uint8 s_m_tx[BUF_SIZE];
ATTR_PLACE_AT_NONCACHEABLE static uint8 s_m_rx[BUF_SIZE];
ATTR_PLACE_AT_NONCACHEABLE static uint8 s_t_tx[BUF_SIZE];
ATTR_PLACE_AT_NONCACHEABLE static uint8 s_t_rx[BUF_SIZE];

static volatile uint32 s_seq_end_cnt;
static uint32 s_cpu_hz;
static const char *s_speed = "100k";

/* 诊断计数器：按字节位置统计首次出现的差异（最多 8 个位置） */
static uint32 s_ehist[8];
static uint32 s_ehist_tail;      /* 超出 8 的差异个数 */
static uint32 s_rd_fail;
static uint32 s_wr_fail;

void I2C_SeqEndNotification_Cb(void) { s_seq_end_cnt++; }

static inline uint64_t cyc(void) { return hpm_csr_get_core_mcycle(); }

static void i2c_fill(uint32 n)
{
    uint32 i;
    for (i = 0u; i < n; i++) {
        s_m_tx[i] = (uint8)(i * 13u + 0x21u);
        s_t_tx[i] = (uint8)(i * 29u + 0x80u);
        s_m_rx[i] = 0u;
        s_t_rx[i] = 0u;
    }
}

static void ehist_reset(void)
{
    uint32 i;
    for (i = 0u; i < 8u; i++) { s_ehist[i] = 0u; }
    s_ehist_tail = 0u;
}

static void ehist_add(uint32 idx)
{
    if (idx < 8u) { s_ehist[idx]++; } else { s_ehist_tail++; }
}

static void ehist_dump(void)
{
    printf("  err-pos hist: 0:%lu 1:%lu 2:%lu 3:%lu 4:%lu 5:%lu 6:%lu 7:%lu >7:%lu\r\n",
           (unsigned long)s_ehist[0], (unsigned long)s_ehist[1], (unsigned long)s_ehist[2],
           (unsigned long)s_ehist[3], (unsigned long)s_ehist[4], (unsigned long)s_ehist[5],
           (unsigned long)s_ehist[6], (unsigned long)s_ehist[7], (unsigned long)s_ehist_tail);
}

/* 控制器写 n 字节 -> 目标收，校验目标缓冲 */
static int i2c_test_write(uint32 n, uint32 *errs, uint64_t *cycles)
{
    uint64_t t0;
    Std_ReturnType rc;

    if ((n == 0u) || (n > BUF_SIZE)) { return -1; }
    i2c_fill(n);
    if (I2C_SetupEB(I2C_JOB_TARGET, 0u, NULL, s_t_rx, (I2C_NumberOfDataType)n) != E_OK) { return -2; }
    if (I2C_SetupEB(I2C_JOB_MASTER_TX, I2C_TARGET_ADDR, s_m_tx, NULL, (I2C_NumberOfDataType)n) != E_OK) { return -3; }
    t0 = cyc();
    rc = I2C_SyncTransmit(I2C_SEQ_WRITE);
    *cycles = cyc() - t0;
    {
        uint32 i;
        uint32 e = 0u;
        for (i = 0u; i < n; i++) {
            if (s_t_rx[i] != s_m_tx[i]) { e++; ehist_add(i); }
        }
        *errs = e;
    }
    if (rc != E_OK) { return -4; }
    return (*errs == 0u) ? 0 : -5;
}

/* 目标预置 n 字节 -> 控制器读，校验控制器缓冲 */
static int i2c_test_read(uint32 n, uint32 *errs, uint64_t *cycles)
{
    uint64_t t0;
    Std_ReturnType rc;

    if ((n == 0u) || (n > BUF_SIZE)) { return -1; }
    i2c_fill(n);
    if (I2C_SetupEB(I2C_JOB_TARGET, 0u, s_t_tx, NULL, (I2C_NumberOfDataType)n) != E_OK) { return -2; }
    if (I2C_SetupEB(I2C_JOB_MASTER_RX, I2C_TARGET_ADDR, NULL, s_m_rx, (I2C_NumberOfDataType)n) != E_OK) { return -3; }
    t0 = cyc();
    rc = I2C_SyncTransmit(I2C_SEQ_READ);
    *cycles = cyc() - t0;
    {
        uint32 i;
        uint32 e = 0u;
        for (i = 0u; i < n; i++) {
            if (s_m_rx[i] != s_t_tx[i]) {
                e++;
                ehist_add(i);
                if (e <= 3u) {
                    printf("  [rd-diff] i=%lu rx=%02X tx=%02X\r\n", (unsigned long)i,
                           s_m_rx[i], s_t_tx[i]);
                }
            }
        }
        *errs = e;
    }
    if (rc != E_OK) { return -4; }
    return (*errs == 0u) ? 0 : -5;
}

static void report(const char *dir, uint32 n, int rc, uint32 errs, uint64_t cycles)
{
    uint32 ns = (uint32)((cycles * 1000000000ull) / (uint64_t)s_cpu_hz);
    uint32 bps;
    if (cycles == 0u) { cycles = 1u; }
    bps = (uint32)(((uint64_t)n * 9ull * (uint64_t)s_cpu_hz) / cycles);   /* 8 data + 1 ack bit/byte */
    printf("  %-5s %-4s n=%-4lu %s err=%-3lu t=%lu.%02lu us 线速率=%lu.%01lu kbps(有效 %lu.%01lu kbps)\r\n",
           s_speed, dir, (unsigned long)n, ((rc == 0) && (errs == 0u)) ? "PASS" : "FAIL",
           (unsigned long)errs, (unsigned long)(ns / 1000u), (unsigned long)((ns % 1000u) / 10u),
           (unsigned long)(bps / 1000u), (unsigned long)((bps % 1000u) / 100u),
           (unsigned long)(bps * 8ull / 9ull / 1000u), (unsigned long)((bps * 8ull / 9ull % 1000u) / 100u));
}

static void cmd_info(void)
{
    Std_VersionInfoType vi;
    I2C_GetVersionInfo(&vi);
    printf("i2c_master_slave @ hpm5e31_1 | MCAL I2c %u.%u.%u moduleID=%u\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, (unsigned)vi.moduleID);
    printf("  I2C2 controller: SCL PD02 / SDA PD03；I2C3 target addr=0x%02X SCL PD07 / SDA PD06\r\n",
           (unsigned)I2C_TARGET_ADDR);
    printf("  速度=%s，seqEnd=%lu，runtimeErr=%lu，chStatus: I2C2=%s I2C3=%s\r\n", s_speed,
           (unsigned long)s_seq_end_cnt, (unsigned long)I2C_GetRuntimeErrorCount(),
           (I2C_GetChannelStatus(I2C_CH_I2C2_MASTER) == I2C_IDLE) ? "IDLE" : "BUSY",
           (I2C_GetChannelStatus(I2C_CH_I2C3_TARGET) == I2C_IDLE) ? "IDLE" : "BUSY");
    printf("  usage: i2c info|init 100k|400k|1m|xfer [n]|async [n]|ber [count]|det\r\n");
    printf("         i2c regs|rdloop [n] [cnt]|wrloop [n] [cnt]\r\n");
}

static void cmd_regs(void)
{
    printf("  I2C2(master) SETUP=0x%08lx TPM=0x%08lx CTRL=0x%08lx STATUS=0x%08lx ADDR=0x%08lx\r\n",
           (unsigned long)HPM_I2C2->SETUP, (unsigned long)HPM_I2C2->TPM,
           (unsigned long)HPM_I2C2->CTRL, (unsigned long)HPM_I2C2->STATUS,
           (unsigned long)HPM_I2C2->ADDR);
    printf("  I2C3(target) SETUP=0x%08lx TPM=0x%08lx CTRL=0x%08lx STATUS=0x%08lx ADDR=0x%08lx\r\n",
           (unsigned long)HPM_I2C3->SETUP, (unsigned long)HPM_I2C3->TPM,
           (unsigned long)HPM_I2C3->CTRL, (unsigned long)HPM_I2C3->STATUS,
           (unsigned long)HPM_I2C3->ADDR);
}

static void cmd_rdloop(int argc, char **argv)
{
    uint32_t n = 24u, cnt = 100u, k;
    uint32 fail = 0u, err_total = 0u;
    uint64_t cmin = ~0ull, cmax = 0ull;
    if (argc >= 1) { (void)bsp_parse_u32(argv[0], &n); }
    if (argc >= 2) { (void)bsp_parse_u32(argv[1], &cnt); }
    if ((n == 0u) || (n > BUF_SIZE)) { n = 24u; }
    if (cnt == 0u) { cnt = 100u; }
    ehist_reset(); s_rd_fail = 0u;
    for (k = 0u; k < cnt; k++) {
        uint32 errs = 0u; uint64_t cy = 0u;
        int rc = i2c_test_read(n, &errs, &cy);
        if ((rc != 0) || (errs != 0u)) { fail++; }
        err_total += errs;
        if (cy < cmin) { cmin = cy; }
        if (cy > cmax) { cmax = cy; }
    }
    s_rd_fail = fail;
    printf("rdloop: n=%lu x cnt=%lu @ %s -> fail=%lu err_bytes=%lu\r\n",
           (unsigned long)n, (unsigned long)cnt, s_speed,
           (unsigned long)fail, (unsigned long)err_total);
    printf("  cycles min=%lu max=%lu (us: %lu.%02lu .. %lu.%02lu)\r\n",
           (unsigned long)cmin, (unsigned long)cmax,
           (unsigned long)(cmin * 1000000ull / (uint64_t)s_cpu_hz / 1000ull),
           (unsigned long)(cmin * 1000000ull / (uint64_t)s_cpu_hz % 1000ull / 10ull),
           (unsigned long)(cmax * 1000000ull / (uint64_t)s_cpu_hz / 1000ull),
           (unsigned long)(cmax * 1000000ull / (uint64_t)s_cpu_hz % 1000ull / 10ull));
    ehist_dump();
    printf("  slave ISR: addr_hit=%d\r\n", 0);
}

static void cmd_wrloop(int argc, char **argv)
{
    uint32_t n = 24u, cnt = 100u, k;
    uint32 fail = 0u, err_total = 0u;
    if (argc >= 1) { (void)bsp_parse_u32(argv[0], &n); }
    if (argc >= 2) { (void)bsp_parse_u32(argv[1], &cnt); }
    if ((n == 0u) || (n > BUF_SIZE)) { n = 24u; }
    if (cnt == 0u) { cnt = 100u; }
    ehist_reset(); s_wr_fail = 0u;
    for (k = 0u; k < cnt; k++) {
        uint32 errs = 0u; uint64_t cy = 0u;
        int rc = i2c_test_write(n, &errs, &cy);
        if ((rc != 0) || (errs != 0u)) { fail++; }
        err_total += errs;
    }
    s_wr_fail = fail;
    printf("wrloop: n=%lu x cnt=%lu @ %s -> fail=%lu err_bytes=%lu\r\n",
           (unsigned long)n, (unsigned long)cnt, s_speed,
           (unsigned long)fail, (unsigned long)err_total);
    ehist_dump();
}

static void i2c_reinit(const I2C_ConfigType *cfg, const char *name)
{
    if (I2C_GetChannelStatus(I2C_CH_I2C2_MASTER) == I2C_BUSY) { printf("i2c busy\r\n"); return; }
    I2C_DeInit();
    I2C_Init(cfg);
    s_speed = name;
    printf("i2c: re-init -> %s\r\n", name);
    cmd_regs();
}

static void cmd_init(int argc, char **argv)
{
    if (argc < 1) { return; }
    if (strcmp(argv[0], "400k") == 0) { i2c_reinit(&I2C_Config_400k, "400k"); }
    else if (strcmp(argv[0], "1m") == 0) { i2c_reinit(&I2C_Config_1M, "1m"); }
    else { i2c_reinit(&I2C_Config_100k, "100k"); }
}

static void cmd_xfer(int argc, char **argv)
{
    uint32_t n = 16u;
    uint32 errs = 0u;
    uint64_t cy = 0u;
    int rc;
    if (argc >= 1) { (void)bsp_parse_u32(argv[0], &n); }
    if ((n == 0u) || (n > BUF_SIZE)) { printf("usage: i2c xfer [1..%u]\r\n", (unsigned)BUF_SIZE); return; }
    ehist_reset();
    rc = i2c_test_write(n, &errs, &cy);
    report("wr", n, rc, errs, cy);
    rc = i2c_test_read(n, &errs, &cy);
    report("rd", n, rc, errs, cy);
}

static void cmd_async(int argc, char **argv)
{
    uint32_t n = 16u;
    uint64_t t0;
    uint64_t dt;
    uint32 guard = 2000000u;
    if (argc >= 1) { (void)bsp_parse_u32(argv[0], &n); }
    if ((n == 0u) || (n > BUF_SIZE)) { n = 16u; }
    i2c_fill(n);
    (void)I2C_SetupEB(I2C_JOB_TARGET, 0u, NULL, s_t_rx, (I2C_NumberOfDataType)n);
    (void)I2C_SetupEB(I2C_JOB_MASTER_TX, I2C_TARGET_ADDR, s_m_tx, NULL, (I2C_NumberOfDataType)n);
    t0 = cyc();
    if (I2C_AsyncTransmit(I2C_SEQ_WRITE) != E_OK) { printf("async: request rejected\r\n"); return; }
    printf("async: AsyncTransmit 返回后 result=%d (PENDING=1)\r\n", (int)I2C_GetSequenceResult(I2C_SEQ_WRITE));
    while ((I2C_GetSequenceResult(I2C_SEQ_WRITE) == I2C_SEQ_PENDING) && (guard-- != 0u)) {
        I2C_MainFunction();
    }
    dt = cyc() - t0;
    printf("async: done result=%d, n=%lu, %lu us\r\n", (int)I2C_GetSequenceResult(I2C_SEQ_WRITE),
           (unsigned long)n, (unsigned long)(dt * 1000000ull / (uint64_t)s_cpu_hz));
}

static void cmd_ber(int argc, char **argv)
{
    uint32_t cnt = 200u;
    uint32 i;
    uint32 err_bytes = 0u;
    uint32 fail_wr = 0u, fail_rd = 0u;
    uint64_t total = 0ull;
    uint64_t t0;
    uint64_t dt;
    if (argc >= 1) { (void)bsp_parse_u32(argv[0], &cnt); }
    if (cnt == 0u) { cnt = 200u; }
    if (cnt > 20000u) { cnt = 20000u; }
    ehist_reset();
    t0 = cyc();
    for (i = 0u; i < cnt; i++) {
        uint32 errs = 0u;
        uint64_t cy = 0u;
        if (i2c_test_write(24u, &errs, &cy) != 0) { fail_wr++; }
        err_bytes += errs;
        if (i2c_test_read(24u, &errs, &cy) != 0) { fail_rd++; }
        err_bytes += errs;
        total += 48ull;
    }
    dt = cyc() - t0;
    {
        uint64_t ms100 = dt * 100000ull / (uint64_t)s_cpu_hz;
        uint64_t bps = (dt != 0u) ? (total * (uint64_t)s_cpu_hz / dt) : 0ull;
        printf("ber: %lu 次 x (24B 写 + 24B 读) @ %s\r\n", (unsigned long)cnt, s_speed);
        printf("  total=%lu B fail_wr=%lu fail_rd=%lu err_bytes=%lu, %lu.%02lu ms, 有效=%lu B/s\r\n",
               (unsigned long)total, (unsigned long)fail_wr, (unsigned long)fail_rd,
               (unsigned long)err_bytes,
               (unsigned long)(ms100 / 100ull), (unsigned long)(ms100 % 100ull),
               (unsigned long)bps);
        ehist_dump();
        printf("  => %s\r\n",
               ((fail_wr == 0u) && (fail_rd == 0u) && (err_bytes == 0u)) ? "PASS (0 误码)" : "FAIL");
    }
}

static void cmd_det(void)
{
    uint32 c0 = Det_GetErrorCount();
    cmd_info();
    printf("det: count0=%lu\r\n", (unsigned long)c0);
    (void)I2C_SetupEB(0x7Fu, I2C_TARGET_ADDR, s_m_tx, s_m_rx, 4u);
    (void)I2C_SyncTransmit(0x7Fu);
    (void)I2C_AsyncTransmit(0x7Fu);
    (void)I2C_GetSequenceResult(0x7Fu);
    (void)I2C_GetChannelStatus(0x7Fu);
    (void)I2C_SetupEB(I2C_JOB_MASTER_TX, I2C_TARGET_ADDR, NULL, NULL, 4u);
    (void)I2C_SetupEB(I2C_JOB_MASTER_TX, I2C_TARGET_ADDR, s_m_tx, NULL, 0u);
    I2C_GetVersionInfo(NULL);
    printf("det: after inject count=%lu (dev errors)\r\n", (unsigned long)Det_GetErrorCount());
}

static void cmd_i2c(int argc, char **argv)
{
    if (argc < 2) { cmd_info(); return; }
    if (strcmp(argv[1], "info") == 0) { cmd_info(); }
    else if (strcmp(argv[1], "init") == 0) { cmd_init(argc - 2, &argv[2]); }
    else if (strcmp(argv[1], "xfer") == 0) { cmd_xfer(argc - 2, &argv[2]); }
    else if (strcmp(argv[1], "async") == 0) { cmd_async(argc - 2, &argv[2]); }
    else if (strcmp(argv[1], "ber") == 0) { cmd_ber(argc - 2, &argv[2]); }
    else if (strcmp(argv[1], "det") == 0) { cmd_det(); }
    else if (strcmp(argv[1], "regs") == 0) { cmd_regs(); }
    else if (strcmp(argv[1], "rdloop") == 0) { cmd_rdloop(argc - 2, &argv[2]); }
    else if (strcmp(argv[1], "wrloop") == 0) { cmd_wrloop(argc - 2, &argv[2]); }
    else { printf("unknown: %s\r\n", argv[1]); cmd_info(); }
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c, cmd_i2c, i2c: info|init|xfer|async|ber|det|regs|slvmode|rdloop|wrloop);

int main(void)
{
    uint32 errs = 0u;
    uint64_t cy = 0u;
    int rc;

    bsp_init();
    s_cpu_hz = clock_get_frequency(clock_cpu0);

    printf("\r\n=== mcal i2c_master_slave: I2C2(controller) <-> I2C3(target 0x50) ===\r\n");
    I2C_Init(&I2C_Config);
    cmd_info();
    cmd_regs();
    rc = i2c_test_write(32u, &errs, &cy);
    report("boot-wr", 32u, rc, errs, cy);
    rc = i2c_test_read(32u, &errs, &cy);
    report("boot-rd", 32u, rc, errs, cy);
    printf("i2c: boot self-test %s (seqEnd=%lu)\r\n",
           ((rc == 0) && (errs == 0u)) ? "PASS" : "FAIL", (unsigned long)s_seq_end_cnt);
    printf("type 'i2c info' / 'i2c xfer 32' / 'i2c async 16' / 'i2c ber 200' / 'i2c det'\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
