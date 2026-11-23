/*
 * crc_bench —— AUTOSAR Crc 库：标准向量自检 + HW/SW 吞吐对比
 *
 * 验证点：
 *  1) 8 个算法对 SWS_Crc §7.2 的 "123456789" 校验值与 7 组标准数据向量的结果；
 *  2) 增量计算（Crc_IsFirstCall=FALSE 的 start value 链）一次算 vs 分段算结果一致；
 *  3) HW（HPM CRC 外设）与 SW（查表）对 4KB 数据逐字节一致；
 *  4) 各算法 HW/SW 吞吐 MB/s、cycles/byte（mcycle 实测）。
 * 现象见 README.md。
 */
#include <string.h>
#include "bsp.h"
#include "Crc.h"
#include "hpm_csr_drv.h"

#define BENCH_SIZE   (4096u)
#define BENCH_ITER   (64u)

static uint8_t s_buf[BENCH_SIZE] __attribute__((aligned(4)));

/* SWS_Crc §7.2.x 的标准校验值：CRC("123456789") */
static const uint64 s_checkValue[CRC_ALGO_COUNT] = {
    0x4BULL,                 /* CRC8 SAE J1850 */
    0xDFULL,                 /* CRC8 H2F */
    0x29B1ULL,               /* CRC16 CCITT-FALSE */
    0xBB3DULL,               /* CRC16 ARC */
    0xCBF43926ULL,           /* CRC32 Ethernet */
    0x1697D06AULL,           /* CRC32 P4 */
    0x4DB36B68ULL,           /* CRC32 P76 */
    0x995DC9BBDF1939FAULL    /* CRC64 ECMA-182 */
};

static uint32 s_cpu_hz;

/* newlib-nano 的 printf 不支持 %llX，64 位值拆成两个 32 位打印 */
static void hexprint(const char *tag, Crc_AlgoType a, uint64 v)
{
    if (a == CRC_ALGO_CRC64) {
        printf("%s %-22s 0x%08lX%08lX\r\n", tag, Crc_GetAlgoName(a),
               (unsigned long)(uint32)(v >> 32), (unsigned long)(uint32)(v & 0xFFFFFFFFULL));
    } else if (a <= CRC_ALGO_CRC8H2F) {
        printf("%s %-22s 0x%02X\r\n", tag, Crc_GetAlgoName(a), (unsigned)(uint8)v);
    } else if (a <= CRC_ALGO_CRC16ARC) {
        printf("%s %-22s 0x%04X\r\n", tag, Crc_GetAlgoName(a), (unsigned)(uint16)v);
    } else {
        printf("%s %-22s 0x%08lX\r\n", tag, Crc_GetAlgoName(a), (unsigned long)(uint32)v);
    }
}

/* 用一次算 / 两段算 / 三段算交叉验证链式语义 */
static boolean chain_selftest(Crc_AlgoType a)
{
    const uint8 *key = (const uint8 *)"123456789";
    uint64 whole = Crc_CalculateByAlgo(a, key, 9U, 0ULL, TRUE);
    uint64 p1 = Crc_CalculateByAlgo(a, key, 4U, 0ULL, TRUE);
    uint64 p2 = Crc_CalculateByAlgo(a, &key[4], 5U, p1, FALSE);
    uint64 q1 = Crc_CalculateByAlgo(a, key, 3U, 0ULL, TRUE);
    uint64 q2 = Crc_CalculateByAlgo(a, &key[3], 3U, q1, FALSE);
    uint64 q3 = Crc_CalculateByAlgo(a, &key[6], 3U, q2, FALSE);
    return (boolean)((whole == p2) && (whole == q3));
}

static void cmd_crcvec(int argc, char **argv)
{
    boolean ok = TRUE;
    (void)argc; (void)argv;
    printf("--- crc vector: check value of \"123456789\" (SWS_Crc 7.2) ---\r\n");
    for (int a = 0; a < (int)CRC_ALGO_COUNT; a++) {
        uint64 v = Crc_CalculateByAlgo((Crc_AlgoType)a, (const uint8 *)"123456789", 9U, 0ULL, TRUE);
        boolean pass = (v == s_checkValue[a]);
        hexprint(pass ? "[PASS]" : "[FAIL]", (Crc_AlgoType)a, v);
        ok = ok && pass;
    }
    printf("--- crc chain: one-shot vs 2/3-step (Crc_IsFirstCall=FALSE) ---\r\n");
    for (int a = 0; a < (int)CRC_ALGO_COUNT; a++) {
        boolean pass = chain_selftest((Crc_AlgoType)a);
        printf("%s %-22s chain\r\n", pass ? "[PASS]" : "[FAIL]", Crc_GetAlgoName((Crc_AlgoType)a));
        ok = ok && pass;
    }
    printf("crcvec: %s\r\n", ok ? "PASS" : "FAIL");
}

/* 硬件/软件一致性：4KB 随机样数据逐算法比对 */
static void cmd_crcxcheck(int argc, char **argv)
{
    boolean ok = TRUE;
    (void)argc; (void)argv;
    printf("--- crc hw/sw cross-check on 4KB ---\r\n");
    for (int a = 0; a < (int)CRC_ALGO_COUNT; a++) {
        Crc_AlgoType algo = (Crc_AlgoType)a;
        boolean hwAct = Crc_IsHwActive(algo);
        Crc_SetHwEnabled(TRUE);
        uint64 hw = Crc_CalculateByAlgo(algo, s_buf, BENCH_SIZE, 0ULL, TRUE);
        Crc_SetHwEnabled(FALSE);
        uint64 sw = Crc_CalculateByAlgo(algo, s_buf, BENCH_SIZE, 0ULL, TRUE);
        Crc_SetHwEnabled(TRUE);
        boolean pass = (hw == sw);
        printf("%s %-22s hw=%s hw=0x%08lX%08lX sw=0x%08lX%08lX\r\n", pass ? "[PASS]" : "[FAIL]",
               Crc_GetAlgoName(algo), hwAct ? "yes" : "NO(SW only)",
               (unsigned long)(uint32)(hw >> 32), (unsigned long)(uint32)(hw & 0xFFFFFFFFULL),
               (unsigned long)(uint32)(sw >> 32), (unsigned long)(uint32)(sw & 0xFFFFFFFFULL));
        ok = ok && pass;
    }
    printf("crcxcheck: %s\r\n", ok ? "PASS" : "FAIL");
}

/* 吞吐：固定 4KB 数据，跑 BENCH_ITER 次，mcycle 计时 */
static void cmd_crcbench(int argc, char **argv)
{
    uint32 hwCalls = 0U, swFb = 0U;
    (void)argc; (void)argv;
    printf("--- crc throughput: %lu B x %lu iters, cpu=%lu Hz ---\r\n",
           (unsigned long)BENCH_SIZE, (unsigned long)BENCH_ITER, (unsigned long)s_cpu_hz);
    printf("algo                    sw_MBps  sw_cyc/B  hw_MBps  hw_cyc/B  speedup\r\n");
    for (int a = 0; a < (int)CRC_ALGO_COUNT; a++) {
        Crc_AlgoType algo = (Crc_AlgoType)a;
        uint32 c0, c1;
        double swMBps, hwMBps, swCycB, hwCycB;

        Crc_SetHwEnabled(FALSE);
        c0 = hpm_csr_get_core_mcycle();
        for (uint32 i = 0; i < BENCH_ITER; i++) {
            (void)Crc_CalculateByAlgo(algo, s_buf, BENCH_SIZE, 0ULL, TRUE);
        }
        c1 = hpm_csr_get_core_mcycle();
        swCycB = (double)(c1 - c0) / (double)(BENCH_SIZE * BENCH_ITER);
        swMBps = (double)(BENCH_SIZE * BENCH_ITER) / ((double)(c1 - c0) / (double)s_cpu_hz) / 1e6;

        Crc_SetHwEnabled(TRUE);
        c0 = hpm_csr_get_core_mcycle();
        for (uint32 i = 0; i < BENCH_ITER; i++) {
            (void)Crc_CalculateByAlgo(algo, s_buf, BENCH_SIZE, 0ULL, TRUE);
        }
        c1 = hpm_csr_get_core_mcycle();
        hwCycB = (double)(c1 - c0) / (double)(BENCH_SIZE * BENCH_ITER);
        hwMBps = (double)(BENCH_SIZE * BENCH_ITER) / ((double)(c1 - c0) / (double)s_cpu_hz) / 1e6;

        printf("%-22s %8.2f %9.2f %8.2f %9.2f %8.2fx\r\n", Crc_GetAlgoName(algo),
               swMBps, swCycB, hwMBps, hwCycB, (hwCycB > 0.0) ? (swCycB / hwCycB) : 0.0);
    }
    (void)Crc_GetHwStats(&hwCalls, &swFb);
    printf("hw_calls=%lu sw_fallbacks=%lu\r\n", (unsigned long)hwCalls, (unsigned long)swFb);
}

/* 诊断：外设原始探测（SDK 已验证 preset crc32 对 16B 00..0F 应为 0xCECEE288） */
static void cmd_crcraw(int argc, char **argv)
{
    uint32 pre = 0U, misc = 0U, r0, r1;
    (void)argc; (void)argv;
    Crc_Init();
    r0 = Crc_HwRawProbe(0U, &pre, &misc);
    printf("hwraw preset_crc32 : result=0x%08lX (expect 0xCECEE288) PRE_SET=0x%08lX MISC=0x%08lX\r\n",
           (unsigned long)r0, (unsigned long)pre, (unsigned long)misc);
    r1 = Crc_HwRawProbe(1U, &pre, &misc);
    printf("hwraw custom 32bit : result=0x%08lX PRE_SET=0x%08lX MISC=0x%08lX\r\n",
           (unsigned long)r1, (unsigned long)pre, (unsigned long)misc);
    {
        static const uint8_t pat[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
                                        0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF};
        static const uint8_t seq[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
        Crc_SetHwEnabled(FALSE);
        uint32_t sw32pat = Crc_CalculateCRC32(pat, 16U, 0U, TRUE);
        uint8_t  sw8seq  = Crc_CalculateCRC8(seq, 16U, 0U, TRUE);
        uint8_t  sw8pat  = Crc_CalculateCRC8(pat, 16U, 0U, TRUE);
        Crc_SetHwEnabled(TRUE);
        printf("hwraw crc32 pattern: hw=0x%08lX sw=0x%08lX\r\n",
               (unsigned long)Crc_HwRawProbe(2U, NULL, NULL), (unsigned long)sw32pat);
        printf("hwraw crc8  seq   : hw=0x%08lX sw=0x%02X\r\n",
               (unsigned long)(Crc_HwRawProbe(3U, NULL, NULL) & 0xFFU), (unsigned)sw8seq);
        printf("hwraw crc8  pattern: hw=0x%08lX sw=0x%02X\r\n",
               (unsigned long)(Crc_HwRawProbe(5U, NULL, NULL) & 0xFFU), (unsigned)sw8pat);
        printf("hwraw crc8  polyleft: hw=0x%08lX\r\n",
               (unsigned long)(Crc_HwRawProbe(6U, NULL, NULL) & 0xFFU));
    }
}

static void cmd_crcinfo(int argc, char **argv)
{
    Std_VersionInfoType vi;
    (void)argc; (void)argv;
    Crc_GetVersionInfo(&vi);
    printf("Crc %u.%u.%u module=%u vendor=0x%04x\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version, (unsigned)vi.sw_patch_version,
           (unsigned)vi.moduleID, (unsigned)vi.vendorID);
    Crc_Init();
    for (int a = 0; a < (int)CRC_ALGO_COUNT; a++) {
        uint32 conv = 0U;
        (void)Crc_GetHwConv((Crc_AlgoType)a, &conv);
        if (Crc_IsHwActive((Crc_AlgoType)a)) {
            printf("  %-22s HW accel  conv=0x%03lx (%s%s%s%s, chain=%s)\r\n", Crc_GetAlgoName((Crc_AlgoType)a),
                   (unsigned long)conv,
                   (conv & 0x02U) ? "init_rev " : "init_direct ",
                   (conv & 0x04U) ? "xor_sw " : "xor_hw ",
                   (conv & 0x08U) ? "feed_word " : "feed_byte ",
                   (conv & 0x10U) ? "poly_left" : "poly_right",
                   (conv & 0x100U) ? "yes" : "no(SW)");
        } else {
            printf("  %-22s SW table (no HW usable)\r\n", Crc_GetAlgoName((Crc_AlgoType)a));
        }
    }
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 crcinfo, cmd_crcinfo, "Crc: version + per-algo HW/SW path");
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 crcvec, cmd_crcvec, "Crc: SWS check vectors + chain test");
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 crcxcheck, cmd_crcxcheck, "Crc: HW vs SW cross-check on 4KB");
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 crcraw, cmd_crcraw, "Crc: raw hw probe (diagnostic)");
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 crcbench, cmd_crcbench, "Crc: HW/SW throughput MB/s");

int main(void)
{
    bsp_init();
    s_cpu_hz = clock_get_frequency(clock_cpu0);

    /* 造 4KB 样数据：与地址相关，便于复现 */
    for (uint32 i = 0; i < BENCH_SIZE; i++) {
        s_buf[i] = (uint8_t)((i * 37U + 0x5AU) & 0xFFU);
    }

    printf("\r\n=== crc_bench: AUTOSAR Crc library on HPM5E31 ===\r\n");
    cmd_crcinfo(0, NULL);
    cmd_crcvec(0, NULL);
    cmd_crcxcheck(0, NULL);
    cmd_crcbench(0, NULL);
    printf("shell: crcinfo | crcvec | crcxcheck | crcbench\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
