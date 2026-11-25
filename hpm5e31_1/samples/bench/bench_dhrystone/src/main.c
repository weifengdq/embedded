/*
 * bench_dhrystone - Dhrystone 2.1 基准（hpm5e31_1 / HPM5E31 @480MHz）
 *
 * 说明:
 *   - 被测代码是 HPM SDK 自带的 dhry_1.c / dhry_2.c（Reinhold P. Weicker 原始版本，
 *     与 hpm_sdk/samples/dhrystone 完全一致），本文件只重写"驱动 + 计时 + 统计"。
 *   - 计时用 mcycle(480MHz)，多轮运行给均值/标准差；每轮结束校验 Dhrystone 的
 *     "Final values"（Int_3_Loc==7 / Enum_Loc==Ident_1 / 两个字符串），PASS 才计入统计。
 *   - DMIPS = Dhrystones/s / 1757；DMIPS/MHz = DMIPS / (CPU MHz)。
 *
 * 注意（易错点）: HPM SDK 自带 dhrystone 例程打印的 "DMIPS per Second" 实际算的是
 *   DMIPS/MHz（它把 Dhrystones/s 又除以了 CPU MHz），本仓例程分开打印两个值。
 *
 * 硬件: hpm5e31_1，UART4 921600 8N1
 * 命令: dh / dhstat / dhinfo
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "bsp.h"
#include "hpm_csr_drv.h"
#include "hpm_clock_drv.h"
#include "dhry.h"

/* dhry_1.c 里定义的全局量 */
extern Rec_Pointer Ptr_Glob, Next_Ptr_Glob;
extern int Int_Glob;
extern Boolean Bool_Glob;
extern char Ch_1_Glob, Ch_2_Glob;
extern int Arr_1_Glob[50];
extern int Arr_2_Glob[50][50];

/* dhry_2.c 里的过程（原版是 K&R 风格，这里给现代原型） */
void Proc_1(Rec_Pointer Ptr_Val_Par);
void Proc_2(One_Fifty *Int_1_Par_Ref);
void Proc_3(Rec_Pointer *Ptr_Ref_Par);
void Proc_4(void);
void Proc_5(void);
void Proc_6(Enumeration Enum_Val_Par, Enumeration *Enum_Ref_Par);
void Proc_7(One_Fifty Int_1_Par_Val, One_Fifty Int_2_Par_Val, One_Fifty *Int_3_Par_Ref);
void Proc_8(Arr_1_Dim Arr_1_Par_Ref, Arr_2_Dim Arr_2_Par_Ref, int Int_1_Par_Val, int Int_2_Par_Val);
Enumeration Func_1(Capital_Letter Ch_1_Par_Val, Capital_Letter Ch_2_Par_Val);
Boolean Func_2(Str_30 Str_1_Par_Ref, Str_30 Str_2_Par_Ref);
Boolean Func_3(Enumeration Enum_Par_Val);

static uint32_t g_cpu_hz;
static double s_dmips[16];
static double s_dmips_mhz[16];
static int s_nrun;

/* 跑一轮 Dhrystone：返回秒数；*ok 输出自检结果 */
static double dhry_run_once(int *ok)
{
    One_Fifty Int_1_Loc;
    register One_Fifty Int_2_Loc;
    One_Fifty Int_3_Loc;
    register char Ch_Index;
    Enumeration Enum_Loc;
    Str_30 Str_1_Loc;
    Str_30 Str_2_Loc;
    register int Run_Index;

    /* --- 每轮重新初始化 Dhrystone 的全局状态 --- */
    Ptr_Glob->Ptr_Comp = Next_Ptr_Glob;
    Ptr_Glob->Discr = Ident_1;
    Ptr_Glob->variant.var_1.Enum_Comp = Ident_3;
    Ptr_Glob->variant.var_1.Int_Comp = 40;
    strcpy(Ptr_Glob->variant.var_1.Str_Comp, "DHRYSTONE PROGRAM, SOME STRING");
    strcpy(Str_1_Loc, "DHRYSTONE PROGRAM, 1'ST STRING");
    Arr_2_Glob[8][7] = 10;
    Int_Glob = 0;

    uint64_t c0 = hpm_csr_get_core_mcycle();

    for (Run_Index = 1; Run_Index <= LOOP_COUNT; ++Run_Index) {
        Proc_5();
        Proc_4();
        Int_1_Loc = 2;
        Int_2_Loc = 3;
        strcpy(Str_2_Loc, "DHRYSTONE PROGRAM, 2'ND STRING");
        Enum_Loc = Ident_2;
        Bool_Glob = !Func_2(Str_1_Loc, Str_2_Loc);
        while (Int_1_Loc < Int_2_Loc) {
            Int_3_Loc = 5 * Int_1_Loc - Int_2_Loc;
            Proc_7(Int_1_Loc, Int_2_Loc, &Int_3_Loc);
            Int_1_Loc += 1;
        }
        Proc_8(Arr_1_Glob, Arr_2_Glob, Int_1_Loc, Int_3_Loc);
        Proc_1(Ptr_Glob);
        for (Ch_Index = 'A'; Ch_Index <= Ch_2_Glob; ++Ch_Index) {
            if (Enum_Loc == Func_1(Ch_Index, 'C')) {
                Proc_6(Ident_1, &Enum_Loc);
                strcpy(Str_2_Loc, "DHRYSTONE PROGRAM, 3'RD STRING");
                Int_2_Loc = Run_Index;
                Int_Glob = Run_Index;
            }
        }
        Int_2_Loc = Int_2_Loc * Int_1_Loc;
        Int_1_Loc = Int_2_Loc / Int_3_Loc;
        Int_2_Loc = 7 * (Int_2_Loc - Int_3_Loc) - Int_1_Loc;
        Proc_2(&Int_1_Loc);
    }

    uint64_t c1 = hpm_csr_get_core_mcycle();

    /* --- Dhrystone 官方自检 --- */
    /* Dhrystone 2.1 官方自检值（原始 dhry_1.c 的 "should be" 表）：
       Int_3_Loc==7, Enum_Loc==1(Ident_2), Str_1_Loc/Str_2_Loc 保持 1'ST / 2'ND，
       Arr_2_Glob[8][7]==LOOP_COUNT+10 */
    *ok = (Int_3_Loc == 7) && ((int) Enum_Loc == 1) &&
          (strcmp(Str_1_Loc, "DHRYSTONE PROGRAM, 1'ST STRING") == 0) &&
          (strcmp(Str_2_Loc, "DHRYSTONE PROGRAM, 2'ND STRING") == 0) &&
          (Arr_2_Glob[8][7] == (int) (LOOP_COUNT + 10));

    return (double) (c1 - c0) / (double) g_cpu_hz;
}

static void dhry_report(void)
{
    if (s_nrun == 0) {
        printf("no run yet, use 'dh <n>'\r\n");
        return;
    }
    double sum = 0.0, sum2 = 0.0;
    double mn = s_dmips_mhz[0], mx = s_dmips_mhz[0];
    for (int i = 0; i < s_nrun; i++) {
        sum += s_dmips_mhz[i];
        sum2 += s_dmips_mhz[i] * s_dmips_mhz[i];
        if (s_dmips_mhz[i] < mn) mn = s_dmips_mhz[i];
        if (s_dmips_mhz[i] > mx) mx = s_dmips_mhz[i];
    }
    double mean = sum / s_nrun;
    double var = (s_nrun > 1) ? (sum2 - sum * sum / s_nrun) / (s_nrun - 1) : 0.0;
    double sd = 0.0;
    if (var > 0.0) {
        sd = var;
        for (int k = 0; k < 40; k++) {
            sd = 0.5 * (sd + var / sd);
        }
    }
    double mean_dmips = 0.0;
    for (int i = 0; i < s_nrun; i++) {
        mean_dmips += s_dmips[i];
    }
    mean_dmips /= s_nrun;
    printf("\r\n===== Dhrystone summary (n=%d, LOOP_COUNT=%lu) =====\r\n",
           s_nrun, (unsigned long) LOOP_COUNT);
    printf("cpu=%.0f MHz  opt=%s  -fno-inline\r\n", (double) g_cpu_hz / 1e6, BENCH_OPT_STR);
    printf("DMIPS     mean=%.2f\r\n", mean_dmips);
    printf("DMIPS/MHz mean=%.4f sd=%.4f min=%.4f max=%.4f\r\n", mean, sd, mn, mx);
    printf("BENCH_STAT metric=dhrystone n=%d mean_dmips=%.2f dmips_per_mhz=%.4f sd=%.4f min=%.4f max=%.4f\r\n",
           s_nrun, mean_dmips, mean, sd, mn, mx);
}

static void dhry_run(int n)
{
    for (int i = 0; i < n; i++) {
        if (s_nrun >= 16) {
            printf("run buffer full, reboot\r\n");
            break;
        }
        int ok = 0;
        double secs = dhry_run_once(&ok);
        double dps = (double) LOOP_COUNT / secs;
        double dmips = dps / 1757.0;
        double dmips_mhz = dmips / ((double) g_cpu_hz / 1e6);
        s_dmips[s_nrun] = dmips;
        s_dmips_mhz[s_nrun] = dmips_mhz;
        s_nrun++;
        printf("BENCH_RUN idx=%d secs=%.6f dhrystones_per_s=%.2f dmips=%.2f dmips_per_mhz=%.4f verify=%s\r\n",
               s_nrun, secs, dps, dmips, dmips_mhz, ok ? "PASS" : "FAIL");
    }
    dhry_report();
}

static void cmd_dh(int argc, char *argv[])
{
    uint32_t n = 1;
    if (argc >= 2) {
        if (bsp_parse_u32(argv[1], &n) != 0 || n == 0) {
            printf("usage: dh <n>\r\n");
            return;
        }
    }
    dhry_run((int) n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dh, cmd_dh, run Dhrystone n more times: dh <n>);

static void cmd_dhstat(int argc, char *argv[])
{
    (void) argc; (void) argv;
    dhry_report();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dhstat, cmd_dhstat, show Dhrystone statistics);

static void cmd_dhinfo(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("board      : %s\r\n", BOARD_NAME);
    printf("cpu        : %lu Hz\r\n", (unsigned long) g_cpu_hz);
    printf("dst source : HPM SDK samples/dhrystone/src/dhry_1.c + dhry_2.c\r\n");
    printf("LOOP_COUNT : %lu\r\n", (unsigned long) LOOP_COUNT);
    printf("opt flags  : %s -fno-inline\r\n", BENCH_OPT_STR);
    printf("time source: mcycle (480MHz)\r\n");
    printf("runs so far: %d\r\n", s_nrun);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dhinfo, cmd_dhinfo, show Dhrystone environment);

int main(void)
{
    bsp_init();
    g_cpu_hz = clock_get_frequency(clock_cpu0);
    printf("\r\n=== bench_dhrystone (Dhrystone 2.1) ===\r\n");
    printf("board=%s cpu=%.0f MHz opt=%s LOOP_COUNT=%lu runs=%d\r\n",
           BOARD_NAME, (double) g_cpu_hz / 1e6, BENCH_OPT_STR, (unsigned long) LOOP_COUNT, BENCH_RUNS);
    printf("commands: dh <n> | dhstat | dhinfo\r\n");
    dhry_run(BENCH_RUNS);
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
