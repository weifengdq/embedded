/*
 * icu_capture —— AUTOSAR Icu 输入捕获：1/10/100 kHz 周期误差 + 占空比误差
 *
 * 信号链（真实引脚 + 板载 I2C2<->I2C3 杜邦线，不是片内回环）：
 *   PWM0 P2 (PD02) --线--> PD07 -> TRGM0_P07 -> GPTMR2_CAPT_3 = Icu ch1
 *   所以 Icu 测到的就是 Pwm 实际输出引脚上的波形。
 *
 * 测试内容：
 *   1) 频率扫描 1/10/100 kHz（PWM 50% 占空比），报周期误差 ppm / 实测频率；
 *   2) 占空比误差：在 1 kHz 下扫 20/50/80%；
 *   3) 边沿检测模式：Icu_EnableEdgeDetection + 双边沿，1 s 内边沿数 vs 理论值；
 *   4) 时间戳缓冲：Icu_StartTimestamp 线性缓冲 32 点，报索引/相邻间隔；
 *   5) 错误路径：非法通道 / 非法边沿 / 空缓冲 / 缓冲长度 0 -> Det 计数。
 *
 * 命令：
 *   icu freq <hz>        设置被测信号频率（同时设 PWM 周期）
 *   icu duty <permille>  设置被测信号占空比
 *   icu meas             打印一次测量（周期/频率/占空比/误差）
 *   icu edge             边沿检测 1 s，打印边沿数
 *   icu ts               时间戳缓冲 32 点示例
 *   icu err              错误路径
 *   icu info             配置/版本
 */
#include "bsp.h"
#include "Pwm.h"
#include "Pwm_PBcfg.h"
#include "Icu.h"
#include "Icu_PBcfg.h"
#include "Det.h"
#include <stdio.h>
#include <string.h>

#define SRC_PWM_CH   PWM_CHANNEL_0
#define CAP_ICU_CH   ICU_CHANNEL_1      /* PD07 捕获 PD02 上的 PWM */

static uint32 s_pwmClkHz;
static uint32 s_icuClkHz;
static uint32 s_freqHz = 10000u;
static uint16 s_duty = PWM_DUTY_100_PERCENT / 2u;

static volatile uint32_t s_sigEdges;
static Icu_ValueType s_tsBuf[32];

void Icu_SignalNotification_1(void)
{
    s_sigEdges++;
}

static void set_src(uint32 freqHz, uint16 duty)
{
    uint32 reload = s_pwmClkHz / freqHz;
    s_freqHz = freqHz;
    s_duty = duty;
    Pwm_SetPeriodAndDuty(SRC_PWM_CH, reload, duty);
    board_delay_ms(60);
}

static void print_meas(void)
{
    Icu_DutyCycleType dc;
    double fMeas, ppm;
    double dutyPct, dutyErr;

    dc.ActiveTime = 0u;
    dc.PeriodTime = 0u;
    Icu_GetDutyCycleValues(CAP_ICU_CH, &dc);
    if (dc.PeriodTime == 0u)
    {
        printf("[icu] no capture (period=0): check PD02<->PD07 wire / PWM running\r\n");
        return;
    }
    fMeas = (double)s_icuClkHz / (double)dc.PeriodTime;
    ppm = (fMeas - (double)s_freqHz) / (double)s_freqHz * 1e6;
    dutyPct = (double)dc.ActiveTime * 100.0 / (double)dc.PeriodTime;
    dutyErr = dutyPct - (double)s_duty * 100.0 / (double)PWM_DUTY_100_PERCENT;
    printf("[icu] target=%lu Hz duty=%u/0x8000 -> period=%lu tick (%.3f Hz, %+.2f ppm) high=%lu tick duty=%.3f%% (%+.3f%%)\r\n",
           (unsigned long)s_freqHz, (unsigned)s_duty,
           (unsigned long)dc.PeriodTime, fMeas, ppm,
           (unsigned long)dc.ActiveTime, dutyPct, dutyErr);
}

static void run_freq_sweep(void)
{
    static const uint32 freqs[] = {1000u, 10000u, 100000u};
    uint32 i;

    printf("[sweep] frequency 1/10/100 kHz, duty=50%%, f_gptmr2=%lu Hz\r\n",
           (unsigned long)s_icuClkHz);
    for (i = 0u; i < (uint32)(sizeof(freqs) / sizeof(freqs[0])); i++)
    {
        set_src(freqs[i], PWM_DUTY_100_PERCENT / 2u);
        print_meas();
    }
}

static void run_duty_sweep(void)
{
    static const uint16 duties[] = {
        (uint16)((PWM_DUTY_100_PERCENT * 20u) / 100u),
        (uint16)((PWM_DUTY_100_PERCENT * 50u) / 100u),
        (uint16)((PWM_DUTY_100_PERCENT * 80u) / 100u)
    };
    uint32 i;

    set_src(1000u, PWM_DUTY_100_PERCENT / 2u);
    printf("[sweep] duty 20/50/80%% @1 kHz\r\n");
    for (i = 0u; i < (uint32)(sizeof(duties) / sizeof(duties[0])); i++)
    {
        set_src(1000u, duties[i]);
        print_meas();
    }
}

/* 边沿检测：1 s 内双边沿个数 vs 理论 2*f*1s */
static void run_edge_detect(void)
{
    uint32 t0, edges;

    Pwm_SetDutyCycle(SRC_PWM_CH, PWM_DUTY_100_PERCENT / 2u);
    Icu_StopSignalMeasurement(CAP_ICU_CH);
    Icu_SetActivationCondition(CAP_ICU_CH, ICU_BOTH_EDGES);
    Icu_EnableNotification(CAP_ICU_CH);
    s_sigEdges = 0u;
    Icu_EnableEdgeDetection(CAP_ICU_CH);
    t0 = bsp_uptime_ms();
    while ((bsp_uptime_ms() - t0) < 1000u)
    {
    }
    edges = s_sigEdges;
    Icu_DisableEdgeDetection(CAP_ICU_CH);
    printf("[edge] 1 s: edges=%lu expect=%lu (2*%luHz*1s) delta=%+ld\r\n",
           (unsigned long)edges, (unsigned long)(2u * s_freqHz),
           (unsigned long)s_freqHz, (long)edges - (long)(2u * s_freqHz));

    /* 恢复测量模式供后续使用 */
    Icu_StartSignalMeasurement(CAP_ICU_CH);
}

/* 时间戳缓冲：抓 32 个边沿 */
static void run_timestamp(void)
{
    uint32 i;
    uint16 idx;
    uint32 n;
    Icu_ValueType d0, d1;

    Icu_StopSignalMeasurement(CAP_ICU_CH);
    Icu_SetActivationCondition(CAP_ICU_CH, ICU_RISING_EDGE);
    Icu_StartTimestamp(CAP_ICU_CH, s_tsBuf, (uint16)32u, (uint16)8u);
    board_delay_ms(120);
    idx = Icu_GetTimestampIndex(CAP_ICU_CH);
    Icu_StopTimestamp(CAP_ICU_CH);
    n = (idx == 0u) ? 32u : (uint32)idx;
    d0 = (n >= 2u) ? (Icu_ValueType)(s_tsBuf[1] - s_tsBuf[0]) : 0u;
    d1 = (n >= 3u) ? (Icu_ValueType)(s_tsBuf[2] - s_tsBuf[1]) : 0u;
    printf("[ts] buffer len=32, idx=%u, first_delta=%lu tick (%.3f us), second_delta=%lu tick\r\n",
           (unsigned)idx, (unsigned long)d0, (double)d0 * 1e6 / (double)s_icuClkHz,
           (unsigned long)d1);
    for (i = 0u; (i < 4u) && (i < n); i++)
    {
        printf("[ts] buf[%lu] = %lu\r\n", (unsigned long)i, (unsigned long)s_tsBuf[i]);
    }
    Icu_StartSignalMeasurement(CAP_ICU_CH);
}

static void cmd_err(void)
{
    uint32 before = Det_GetErrorCount();
    uint16 mod = 0u; uint8 inst = 0u, api = 0u, err = 0u;

    Icu_SetActivationCondition((Icu_ChannelType)ICU_NUM_CHANNELS, ICU_RISING_EDGE);
    Icu_SetActivationCondition(CAP_ICU_CH, (Icu_ActivationType)9u);
    Icu_StartTimestamp(CAP_ICU_CH, NULL_PTR, 16u, 4u);
    Icu_StartTimestamp(CAP_ICU_CH, s_tsBuf, 0u, 4u);
    Icu_GetDutyCycleValues(CAP_ICU_CH, NULL_PTR);
    printf("[det] before=%lu after=%lu delta=%lu\r\n", (unsigned long)before,
           (unsigned long)Det_GetErrorCount(), (unsigned long)(Det_GetErrorCount() - before));
    if (Det_GetLastError(&mod, &inst, &api, &err) == E_OK)
    {
        printf("[det] last: mod=%u inst=%u api=0x%02X err=0x%02X\r\n",
               (unsigned)mod, (unsigned)inst, (unsigned)api, (unsigned)err);
    }
}

static void cmd_info(void)
{
    Std_VersionInfoType vi;
    Icu_ValueType ts = 0u;
    Icu_GetVersionInfo(&vi);
    printf("[icu] Icu v%u.%u.%u moduleID=%u f_gptmr2=%lu Hz\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, (unsigned)vi.moduleID, (unsigned long)s_icuClkHz);
    printf("[icu] ch1 = GPTMR2 CAPT_3 <- TRGM0_P07 <- PD07; ch0 = GPTMR2 CAPT_2 <- TRGM0_P06 <- PD06\r\n");
    (void)Icu_GetLastEdgeTimestamp(CAP_ICU_CH, &ts);
    printf("[icu] last edge timestamp ch1 = %lu, elapsed=%lu tick, input=%s\r\n",
           (unsigned long)ts, (unsigned long)Icu_GetTimeElapsed(CAP_ICU_CH),
           (Icu_GetInputState(CAP_ICU_CH) == ICU_ACTIVE) ? "ACTIVE" : "IDLE");
}

static void cmd_icu(int argc, char **argv)
{
    const char *sub = (argc >= 2) ? argv[1] : "info";

    if ((strcmp(sub, "freq") == 0) && (argc >= 3))
    {
        uint32_t f = 0u;
        if ((bsp_parse_u32(argv[2], &f) != 0) || (f == 0u) || (f > 200000u))
        {
            printf("usage: icu freq <hz 1..200000>\r\n");
            return;
        }
        set_src(f, s_duty);
        printf("[icu] source -> %lu Hz\r\n", (unsigned long)f);
        print_meas();
    }
    else if ((strcmp(sub, "duty") == 0) && (argc >= 3))
    {
        uint32_t pm = 0u;
        if ((bsp_parse_u32(argv[2], &pm) != 0) || (pm > 1000u))
        {
            printf("usage: icu duty <permille>\r\n");
            return;
        }
        set_src(s_freqHz, (uint16)((pm * (uint32)PWM_DUTY_100_PERCENT) / 1000u));
        print_meas();
    }
    else if (strcmp(sub, "meas") == 0)
    {
        print_meas();
    }
    else if (strcmp(sub, "edge") == 0)
    {
        run_edge_detect();
    }
    else if (strcmp(sub, "ts") == 0)
    {
        run_timestamp();
    }
    else if (strcmp(sub, "err") == 0)
    {
        cmd_err();
    }
    else
    {
        cmd_info();
        printf("usage: icu freq|duty|meas|edge|ts|err|info\r\n");
    }
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 icu, cmd_icu, icu capture control: icu <freq|duty|meas|edge|ts|err|info>);

int main(void)
{
    Std_VersionInfoType vi;
    uint32_t last;

    bsp_init();
    Det_Init();
    Pwm_Init(&Pwm_Config);
    Icu_Init(&Icu_Config);
    s_pwmClkHz = clock_get_frequency(clock_pwm0);
    s_icuClkHz = clock_get_frequency(clock_gptmr2);

    Icu_GetVersionInfo(&vi);
    printf("\r\n=== mcal icu_capture (board %s) ===\r\n", BOARD_NAME);
    printf("Icu v%u.%u.%u f_gptmr2=%lu Hz | Pwm f_pwm0=%lu Hz\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, (unsigned long)s_icuClkHz, (unsigned long)s_pwmClkHz);

    set_src(10000u, PWM_DUTY_100_PERCENT / 2u);
    Icu_StartSignalMeasurement(CAP_ICU_CH);

    printf("[icu_capture] auto sweep start (shell idle during run)\r\n");
    board_delay_ms(1000);
    run_freq_sweep();
    run_duty_sweep();
    set_src(1000u, PWM_DUTY_100_PERCENT / 2u);
    Icu_StartSignalMeasurement(CAP_ICU_CH);
    run_edge_detect();
    run_timestamp();
    cmd_err();
    cmd_info();
    printf("[icu_capture] auto sweep done. cmds: icu freq|duty|meas|edge|ts|err|info\r\n");

    last = bsp_uptime_ms();
    while (1)
    {
        bsp_shell_poll();
        if ((bsp_uptime_ms() - last) >= 4000u)
        {
            last = bsp_uptime_ms();
            print_meas();    /* 心跳复打，防抓包丢字节 */
        }
    }
    return 0;
}