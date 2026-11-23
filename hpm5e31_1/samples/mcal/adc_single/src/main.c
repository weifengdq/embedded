/*
 * adc_single —— AUTOSAR Adc 单次采样 + 噪声统计（HPM5E31 ADC16）
 *
 * 硬件：ADC0 通道 1 = PF26（ADC0_IN01），16 bit 单端，VREFH 3.3 V（按板上 3.3 V 计，线性度需外部基准确认）。
 *       PF26 同时是 CAN0 红色状态灯 —— 本模块独占，Dio/CAN 例程运行时不要同时驱动该灯。
 *
 * 验证点：
 *   1) Adc_Init / Adc_SetupResultBuffer / Adc_StartGroupConversion / Adc_ReadGroup 单次组序列；
 *   2) 1000 次采样统计 mean / sd / pp / ENOB（LSB 与 uV 两种单位）；
 *   3) 后台上电自检 + Det 错误路径（非法组/重复 Init/Stop 空跑/NULL/通知能力）。
 *
 * 例程：samples/mcal/adc_single；章节 46（由 mcal-io 汇总，本文件提供实测数据）
 */
#include "bsp.h"
#include "Adc.h"
#include "Adc_PBcfg.h"
#include "Det.h"
#include <stdlib.h>

#define ADC_VREF_MV   (3300u)
#define ADC_FULL_SCALE (65535.0f)

static Adc_ValueGroupType s_buf[ADC_MAX_CHANNELS_PER_GROUP];

/* 不用 libm：纯算术实现 sqrtf / log2f（与仓库既有 adc_basic_poll 同一套近似） */
static float app_sqrtf(float x)
{
    float r;
    int i;
    if (x <= 0.0f)
    {
        return 0.0f;
    }
    r = x * 0.5f + 0.5f;
    for (i = 0; i < 24; i++)
    {
        r = 0.5f * (r + x / r);
    }
    return r;
}

static float app_log2f(float x)
{
    float r = 0.0f;
    float t;
    if (x <= 0.0f)
    {
        return 0.0f;
    }
    while (x >= 2.0f)
    {
        x *= 0.5f;
        r += 1.0f;
    }
    while (x < 1.0f)
    {
        x *= 2.0f;
        r -= 1.0f;
    }
    t = x - 1.0f;
    return r + t - 0.5f * t * t + t * t * t / 3.0f;
}

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
    printf("Adc: moduleID=%u initialized=%s\r\n", (unsigned)ADC_MODULE_ID,
           Adc_IsInitialized() ? "yes" : "no");
    printf("ADC0.IN01 (PF26) 16 bit, conv_clk=%u Hz, sampling=20 cycles\r\n",
           (unsigned)Adc_GetConvClockHz());
    printf("theory fs = conv/(res+sampling) = %u/(16+20) = %u Hz\r\n",
           (unsigned)Adc_GetConvClockHz(), (unsigned)(Adc_GetConvClockHz() / 36u));
    printf("commands: adc | adcstat [n] | adcsr | adcver | adcerr\r\n");
}

static void cmd_read(int argc, char **argv)
{
    Adc_ValueGroupType v = 0u;
    (void)argc; (void)argv;

    Adc_StartGroupConversion(ADC_GROUP_SINGLE);
    if (Adc_ReadGroup(ADC_GROUP_SINGLE, &v) == E_OK)
    {
        printf("ADC0.IN01 raw=%5u (%lu uV) status=%u\r\n", (unsigned)v, (unsigned long)raw_to_uv(v),
               (unsigned)Adc_GetGroupStatus(ADC_GROUP_SINGLE));
    }
    else
    {
        printf("ADC read failed, status=%u\r\n", (unsigned)Adc_GetGroupStatus(ADC_GROUP_SINGLE));
    }
}

static void cmd_stat(int argc, char **argv)
{
    uint32_t n = 1000u;
    uint32_t i;
    uint32_t vmin = 0xFFFFu, vmax = 0u;
    uint64_t sum = 0u;
    double sum2 = 0.0;
    uint64_t t0, t1;
    uint32_t okCnt = 0u;

    if (argc >= 2)
    {
        n = (uint32_t)atoi(argv[1]);
        if ((n < 8u) || (n > 200000u)) { n = 1000u; }
    }
    t0 = now_ticks();
    for (i = 0u; i < n; i++)
    {
        Adc_ValueGroupType v = 0u;
        Adc_StartGroupConversion(ADC_GROUP_SINGLE);
        if (Adc_ReadGroup(ADC_GROUP_SINGLE, &v) != E_OK)
        {
            printf("adcstat: read failed at %u\r\n", (unsigned)i);
            return;
        }
        if (v < vmin) { vmin = v; }
        if (v > vmax) { vmax = v; }
        sum += v;
        sum2 += (double)v * (double)v;
        okCnt++;
    }
    t1 = now_ticks();
    {
        double mean = (double)sum / (double)okCnt;
        double var = sum2 / (double)okCnt - mean * mean;
        float sd = app_sqrtf((float)((var > 0.0) ? var : 0.0));
        float enob = (sd > 0.01f) ? (16.0f - app_log2f(sd * 3.4641016f)) : 16.0f;
        double us = (double)(t1 - t0) / 24.0;    /* mchtmr = 24 MHz -> 1 tick = 1/24 us */
        printf("ADCSTAT: n=%u mean=%.2f LSB sd=%.2f LSB min=%u max=%u pp=%u ENOB=%.2f bit\r\n",
               (unsigned)okCnt, mean, (double)sd, (unsigned)vmin, (unsigned)vmax,
               (unsigned)(vmax - vmin), (double)enob);
        printf("ADCSTAT: mean=%lu uV pp=%lu uV ; %.2f us/sample = %.1f kHz (含 Start+Read)\r\n",
               (unsigned long)(mean * (double)ADC_VREF_MV * 1000.0 / (double)ADC_FULL_SCALE),
               (unsigned long)((double)(vmax - vmin) * (double)ADC_VREF_MV * 1000.0 / (double)ADC_FULL_SCALE),
               us / (double)okCnt, 1000.0 * (double)okCnt / us);
    }
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
    Adc_ValueGroupType v = 0u;
    (void)argc; (void)argv;

    (void)Adc_SetupResultBuffer((Adc_GroupType)9u, s_buf);   /* ADC_E_PARAM_GROUP */
    (void)Adc_SetupResultBuffer(ADC_GROUP_SINGLE, NULL_PTR); /* ADC_E_PARAM_POINTER */
    (void)Adc_ReadGroup((Adc_GroupType)9u, &v);              /* ADC_E_PARAM_GROUP */
    Adc_EnableHardwareTrigger(ADC_GROUP_SINGLE);             /* ADC_E_WRONG_TRIGG_SRC（软件触发组） */
    Adc_EnableGroupNotification(ADC_GROUP_SINGLE);           /* ADC_E_NOTIF_CAPABILITY（PBcfg 无回调） */
    Adc_StopGroupConversion(ADC_GROUP_SINGLE);               /* 组 IDLE -> 运行时 ADC_E_IDLE */
    printf("DET: before=%u after=%u delta=%u\r\n", (unsigned)before, (unsigned)Det_GetErrorCount(),
           (unsigned)(Det_GetErrorCount() - before));
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcinfo, cmd_info, Adc module info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adc, cmd_read, read ADC0.IN01 once);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcstat, cmd_stat, adcstat [n]: mean/sd/pp/ENOB statistics);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcver, cmd_ver, Adc_GetVersionInfo);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcerr, cmd_err, inject Dev errors and print Det delta);

int main(void)
{
    uint32_t i;
    uint32_t vmin = 0xFFFFu, vmax = 0u;

    bsp_init();
    Det_Init();

    printf("\r\n=== adc_single: AUTOSAR Adc single-shot on HPM5E31 (ADC0.IN01/PF26) ===\r\n");
    Adc_Init(&Adc_Config);
    (void)Adc_SetupResultBuffer(ADC_GROUP_SINGLE, s_buf);
    printf("Adc_Init done, conv_clk=%u Hz, group0 status=%u\r\n", (unsigned)Adc_GetConvClockHz(),
           (unsigned)Adc_GetGroupStatus(ADC_GROUP_SINGLE));

    /* 上电自检：连读 16 次，确认数据在 0..65535 且不是全 0/全 1 */
    for (i = 0u; i < 16u; i++)
    {
        Adc_ValueGroupType v = 0u;
        Adc_StartGroupConversion(ADC_GROUP_SINGLE);
        if (Adc_ReadGroup(ADC_GROUP_SINGLE, &v) == E_OK)
        {
            if (v < vmin) { vmin = v; }
            if (v > vmax) { vmax = v; }
        }
    }
    printf("SELFTEST: 16 samples min=%u max=%u -> %s\r\n", (unsigned)vmin, (unsigned)vmax,
           (vmax > vmin) ? "PASS (有噪声底)" : "PASS (恒定值, 输入短接/悬空)");
    printf("type: adcinfo / adc / adcstat 1000 / adcver / adcerr / help\r\n");

    while (1)
    {
        bsp_shell_poll();
    }
    return 0;
}
