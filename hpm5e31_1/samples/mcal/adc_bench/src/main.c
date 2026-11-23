/*
 * adc_bench —— AUTOSAR Adc 连续组吞吐实测（HPM5E31 ADC16）
 *
 * 硬件：ADC0 通道 1 = PF26（ADC0_IN01），16 bit 单端，转换时钟 50 MHz，采样保持 20 cycles。
 *       理论 fs = conv_clk / (res + sampling) = 50e6 / 36 = 1,388,888 Hz。
 *
 * 验证点：
 *   1) 连续组（ADC_CONV_MODE_CONTINUOUS）的 Start / ReadGroup / Stop 序列与状态迁移；
 *   2) 实测连续组吞吐（每次 ReadGroup 完成一轮转换，mchtmr 24 MHz 计时），与理论 fs 对照；
 *   3) Adc_GetStreamLastPointer 的只读接口；
 *   4) Det 错误路径（连续组非法操作）。
 *
 * 说明：本驱动的连续组是"轮询泵动"实现（一次 ReadGroup = 一轮转换），不是硬件周期触发，
 *       因此实测吞吐 = 软件轮询上限；报告里会同时给理论 fs 与实测吞吐，口径不混。
 *
 * 例程：samples/mcal/adc_bench；章节 46（由 mcal-io 汇总，本文件提供实测数据）
 */
#include "bsp.h"
#include "Adc.h"
#include "Adc_PBcfg.h"
#include "Det.h"
#include <stdlib.h>

#define ADC_VREF_MV    (3300u)
#define ADC_THEORY_FS  (1388888u)   /* 50 MHz / (16 + 20) */

static Adc_ValueGroupType s_buf[ADC_MAX_CHANNELS_PER_GROUP];
static uint32_t s_total = 0u;

static uint64_t now_ticks(void)
{
    return HPM_MCHTMR->MTIME;
}

static uint32_t raw_to_uv(uint32_t raw)
{
    return (uint32_t)(((uint64_t)raw * ADC_VREF_MV * 1000ull) / 65535ull);
}

static void cmd_info(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Adc bench: ADC0.IN01 (PF26), conv_clk=%u Hz, theory fs=%u Hz\r\n",
           (unsigned)Adc_GetConvClockHz(), (unsigned)(Adc_GetConvClockHz() / 36u));
    printf("group1 status=%u, total conversions=%u\r\n",
           (unsigned)Adc_GetGroupStatus(ADC_GROUP_CONTINUOUS), (unsigned)Adc_GetGroupSampleCount(ADC_GROUP_CONTINUOUS));
    printf("commands: adcbench [n] | adcstream | adcver | adcerr\r\n");
}

/* 连续组吞吐：Start -> n 次 ReadGroup -> Stop，mchtmr 计时 */
static void cmd_bench(int argc, char **argv)
{
    uint32_t n = 20000u;
    uint32_t i;
    uint64_t t0, t1;
    uint64_t ticks;
    Adc_ValueGroupType v = 0u;
    double us;

    if (argc >= 2)
    {
        n = (uint32_t)atoi(argv[1]);
        if ((n < 100u) || (n > 2000000u)) { n = 20000u; }
    }
    Adc_StartGroupConversion(ADC_GROUP_CONTINUOUS);
    t0 = now_ticks();
    for (i = 0u; i < n; i++)
    {
        if (Adc_ReadGroup(ADC_GROUP_CONTINUOUS, &v) != E_OK)
        {
            printf("adcbench: ReadGroup failed at %u\r\n", (unsigned)i);
            return;
        }
    }
    t1 = now_ticks();
    ticks = t1 - t0;
    Adc_StopGroupConversion(ADC_GROUP_CONTINUOUS);
    s_total += n;

    us = (double)ticks / 24.0;              /* 24 MHz mchtmr */
    printf("ADCBENCH: n=%u ticks=%u us=%.1f -> %.2f us/sample, throughput=%.1f kHz\r\n",
           (unsigned)n, (unsigned)ticks, us, (us / (double)n) * 1000.0,
           (double)n * 1000.0 / us);
    printf("ADCBENCH: theory fs=%u Hz, measured/theory=%.1f%%, last raw=%u (%lu uV)\r\n",
           (unsigned)(Adc_GetConvClockHz() / 36u),
           ((double)n * 1000000.0 / us) * 100.0 / (double)(Adc_GetConvClockHz() / 36u),
           (unsigned)v, (unsigned long)raw_to_uv(v));
    printf("ADCBENCH: group1 total conversions=%u, det count=%u\r\n",
           (unsigned)Adc_GetGroupSampleCount(ADC_GROUP_CONTINUOUS), (unsigned)Det_GetErrorCount());
}

/* GetStreamLastPointer 演示：读上一轮结果指针与有效样本数 */
static void cmd_stream(int argc, char **argv)
{
    Adc_ValueGroupType *ptr = NULL_PTR;
    Adc_StreamNumSampleType samples;
    (void)argc; (void)argv;

    Adc_StartGroupConversion(ADC_GROUP_CONTINUOUS);
    (void)Adc_ReadGroup(ADC_GROUP_CONTINUOUS, s_buf);
    samples = Adc_GetStreamLastPointer(ADC_GROUP_CONTINUOUS, &ptr);
    printf("STREAM: samples=%u ptr=%s value=%u status=%u\r\n", (unsigned)samples,
           (ptr != NULL_PTR) ? "valid" : "NULL",
           (unsigned)((ptr != NULL_PTR) ? ptr[0] : 0u),
           (unsigned)Adc_GetGroupStatus(ADC_GROUP_CONTINUOUS));
    Adc_StopGroupConversion(ADC_GROUP_CONTINUOUS);
}

static void cmd_ver(int argc, char **argv)
{
    Std_VersionInfoType vi;
    (void)argc; (void)argv;
    Adc_GetVersionInfo(&vi);
    printf("Adc_GetVersionInfo: vendor=0x%04X module=%u sw=%u.%u.%u\r\n", (unsigned)vi.vendorID,
           (unsigned)vi.moduleID, (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version);
}

static void cmd_err(int argc, char **argv)
{
    uint32_t before = Det_GetErrorCount();
    Adc_ValueGroupType *ptr = NULL_PTR;
    Adc_ValueGroupType v = 0u;
    (void)argc; (void)argv;

    Adc_StartGroupConversion((Adc_GroupType)9u);                 /* ADC_E_PARAM_GROUP */
    Adc_StopGroupConversion(ADC_GROUP_CONTINUOUS);               /* IDLE -> ADC_E_IDLE 运行时 */
    (void)Adc_ReadGroup(ADC_GROUP_CONTINUOUS, &v);               /* 无结果 -> ADC_E_IDLE */
    (void)Adc_GetStreamLastPointer(ADC_GROUP_CONTINUOUS, &ptr);  /* NULL 指针 -> ADC_E_PARAM_POINTER */
    Adc_Init(&Adc_Config);                                       /* 重复 Init -> ADC_E_ALREADY_INITIALIZED */
    printf("DET: before=%u after=%u delta=%u\r\n", (unsigned)before, (unsigned)Det_GetErrorCount(),
           (unsigned)(Det_GetErrorCount() - before));
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcbinfo, cmd_info, Adc bench info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcbench, cmd_bench, adcbench [n]: continuous group throughput);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcstream, cmd_stream, Adc_GetStreamLastPointer demo);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcver, cmd_ver, Adc_GetVersionInfo);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcerr, cmd_err, inject Dev errors and print Det delta);

int main(void)
{
    Adc_ValueGroupType v = 0u;

    bsp_init();
    Det_Init();

    printf("\r\n=== adc_bench: AUTOSAR Adc continuous group throughput (ADC0.IN01/PF26) ===\r\n");
    Adc_Init(&Adc_Config);
    (void)Adc_SetupResultBuffer(ADC_GROUP_CONTINUOUS, s_buf);
    printf("Adc_Init done, conv_clk=%u Hz, theory fs=%u Hz (16 bit / 20 cycles)\r\n",
           (unsigned)Adc_GetConvClockHz(), (unsigned)(Adc_GetConvClockHz() / 36u));

    Adc_StartGroupConversion(ADC_GROUP_CONTINUOUS);
    if (Adc_ReadGroup(ADC_GROUP_CONTINUOUS, &v) == E_OK)
    {
        printf("SELFTEST: first sample raw=%u (%lu uV), group status=%u\r\n", (unsigned)v,
               (unsigned long)raw_to_uv(v), (unsigned)Adc_GetGroupStatus(ADC_GROUP_CONTINUOUS));
    }
    Adc_StopGroupConversion(ADC_GROUP_CONTINUOUS);
    printf("type: adcbinfo / adcbench 20000 / adcstream / adcver / adcerr / help\r\n");

    while (1)
    {
        bsp_shell_poll();
    }
    return 0;
}
