/*
 * pwm_basic —— AUTOSAR Pwm 基本波形 + 频率/占空比/相位实测
 *
 * 硬件接线（板载杜邦线，不需要额外飞线）：
 *   PWM0 P2 (PD02, I2C2_SCL) --杜邦线--> PD07 (I2C3_SCL) -> TRGM0_P07 -> GPTMR2_CAPT_3 (Icu ch1)
 *   PWM0 P3 (PD03, I2C2_SDA) --杜邦线--> PD06 (I2C3_SDA) -> TRGM0_P06 -> GPTMR2_CAPT_2 (Icu ch0)
 * 也就是说：Pwm 输出，Icu 在线的另一端做真实引脚捕获，测频率/占空比/相位。
 *
 * 测试内容：
 *   1) 占空比扫描 0 / 25 / 50 / 75 / 100%（0x0000 / 0x2000 / 0x4000 / 0x6000 / 0x8000）
 *   2) 频率扫描 1 / 10 / 50 kHz（Pwm_SetPeriodAndDuty）
 *   3) 相位：CH1 配置 25% 相位（PBcfg phasePermille=250），用两个 Icu 通道的
 *      原始边沿时间戳相减测相位差（同一 GPTMR2 自由运行计数器，可直接比较）
 *   4) 错误路径：非法通道 / 越界占空比 / 0 周期 -> Det 计数
 *
 * 命令：
 *   pwm set <permille>   设置 CH0 占空比（0..1000）
 *   pwm freq <hz>        设置周期（两通道共用计数器 1）
 *   pwm meas             打印一次实测
 *   pwm phase            打印相位测量
 *   pwm idle             输出到 idle 电平并回读状态
 *   pwm err              错误路径 + Det 计数
 */
#include "bsp.h"
#include "Pwm.h"
#include "Pwm_PBcfg.h"
#include "Icu.h"
#include "Icu_PBcfg.h"
#include "Det.h"
#include <stdio.h>
#include <string.h>

#define PWM_DEFAULT_FREQ   (10000u)
#define PWM_DEFAULT_DUTY   (PWM_DUTY_100_PERCENT / 2u)

static uint32 s_pwmClkHz;
static uint32 s_icuClkHz;
static uint32 s_freqHz = PWM_DEFAULT_FREQ;
static uint16 s_duty = PWM_DEFAULT_DUTY;

static const uint16 s_dutySweep[] = {
    PWM_DUTY_0_PERCENT,      /* 0%   */
    0x2000u,                 /* 25%  */
    0x4000u,                 /* 50%  */
    0x6000u,                 /* 75%  */
    PWM_DUTY_100_PERCENT     /* 100% */
};

typedef struct
{
    uint32 period;
    uint32 high;
    uint32 freqHz;
    double dutyPct;
} meas_t;

static void meas_icu(Icu_ChannelType ch, meas_t *m)
{
    Icu_DutyCycleType dc;

    dc.ActiveTime = 0u;
    dc.PeriodTime = 0u;
    Icu_GetDutyCycleValues(ch, &dc);
    m->period = dc.PeriodTime;
    m->high = dc.ActiveTime;
    if (dc.PeriodTime != 0u)
    {
        m->freqHz = (uint32)((double)s_icuClkHz / (double)dc.PeriodTime + 0.5);
        m->dutyPct = (double)dc.ActiveTime * 100.0 / (double)dc.PeriodTime;
    }
    else
    {
        m->freqHz = 0u;
        m->dutyPct = 0.0;
    }
}

static void print_meas(const char *tag, uint16 duty)
{
    meas_t m0;
    meas_t m1;

    if (duty == PWM_DUTY_0_PERCENT)
    {
        /* 0% 是恒定低电平，没有边沿，ICU 的测宽模式拿不到新值（会保留上一次捕获），
         * 这是硬件测量手段的限制，不是驱动 bug —— 第 45 章"待确认项与风险"已记录。 */
        printf("[pwm] %s ch0(set 0/0x8000): constant LOW, no edges -> ICU holds last captured value (unmeasurable)\r\n", tag);
        meas_icu(ICU_CHANNEL_0, &m1);
        printf("[pwm] %s ch1(phase 25%%):        period=%lu tick, freq=%lu Hz, duty=%.3f%%\r\n",
               tag, (unsigned long)m1.period, (unsigned long)m1.freqHz, m1.dutyPct);
        return;
    }
    meas_icu(ICU_CHANNEL_1, &m0);   /* 测 PWM CH0（PD02 -> PD07） */
    meas_icu(ICU_CHANNEL_0, &m1);   /* 测 PWM CH1（PD03 -> PD06） */
    printf("[pwm] %s ch0(set %u/0x8000): period=%lu tick, freq=%lu Hz, duty=%.3f%%\r\n",
           tag, (unsigned)duty, (unsigned long)m0.period, (unsigned long)m0.freqHz, m0.dutyPct);
    printf("[pwm] %s ch1(phase 25%%):        period=%lu tick, freq=%lu Hz, duty=%.3f%%\r\n",
           tag, (unsigned long)m1.period, (unsigned long)m1.freqHz, m1.dutyPct);
}

static void run_duty_sweep(void)
{
    uint32 i;

    printf("[sweep] duty 0/25/50/75/100%%, f=%lu Hz, ICU clk=%lu Hz\r\n",
           (unsigned long)s_freqHz, (unsigned long)s_icuClkHz);
    for (i = 0u; i < (uint32)(sizeof(s_dutySweep) / sizeof(s_dutySweep[0])); i++)
    {
        Pwm_SetDutyCycle(PWM_CHANNEL_0, s_dutySweep[i]);
        board_delay_ms(60);
        print_meas("sweep", s_dutySweep[i]);
    }
    Pwm_SetDutyCycle(PWM_CHANNEL_0, s_duty);
}

static void run_freq_sweep(void)
{
    static const uint32 freqs[] = {1000u, 10000u, 50000u};
    uint32 i;

    for (i = 0u; i < (uint32)(sizeof(freqs) / sizeof(freqs[0])); i++)
    {
        uint32 reload = s_pwmClkHz / freqs[i];
        Pwm_SetPeriodAndDuty(PWM_CHANNEL_0, reload, PWM_DUTY_100_PERCENT / 2u);
        board_delay_ms(60);
        printf("[fsweep] target=%lu Hz reload=%lu\r\n", (unsigned long)freqs[i], (unsigned long)reload);
        print_meas("fsweep", PWM_DUTY_100_PERCENT / 2u);
    }
    Pwm_SetPeriodAndDuty(PWM_CHANNEL_0, s_pwmClkHz / s_freqHz, s_duty);
}

/* 相位测量：把两个 Icu 通道切到边沿捕获模式，硬件捕获的边沿时间戳共享同一个
 * GPTMR2 自由运行计数器，两个通道的 ts 直接相减就是相位差（不需要示波器）。 */
static void run_phase(void)
{
    Icu_ValueType ts0 = 0u;
    Icu_ValueType ts1 = 0u;
    uint32 period;
    uint32 diff;
    double measuredPct;
    const uint32 expectPermille = 250u;   /* 与 Pwm_PBcfg.c 的 phasePermille 一致 */

    Pwm_SetDutyCycle(PWM_CHANNEL_0, PWM_DUTY_100_PERCENT / 2u);
    Pwm_SetDutyCycle(PWM_CHANNEL_1, PWM_DUTY_100_PERCENT / 2u);
    board_delay_ms(30);

    Icu_StopSignalMeasurement(ICU_CHANNEL_1);
    Icu_StopSignalMeasurement(ICU_CHANNEL_0);
    (void)Icu_SetActivationCondition(ICU_CHANNEL_1, ICU_RISING_EDGE);
    (void)Icu_SetActivationCondition(ICU_CHANNEL_0, ICU_RISING_EDGE);
    Icu_EnableEdgeDetection(ICU_CHANNEL_1);
    Icu_EnableEdgeDetection(ICU_CHANNEL_0);
    board_delay_ms(30);

    period = s_icuClkHz / s_freqHz;   /* 时间戳单位是 GPTMR tick，不是 PWM tick */
    (void)Icu_GetLastEdgeTimestamp(ICU_CHANNEL_1, &ts0);  /* PWM CH0 上升沿 -> PD07 */
    (void)Icu_GetLastEdgeTimestamp(ICU_CHANNEL_0, &ts1);  /* PWM CH1 上升沿 -> PD06 */
    if ((ts0 == 0u) && (ts1 == 0u))
    {
        printf("[phase] no edge captured; check PD02<->PD07 / PD03<->PD06 wires\r\n");
    }
    else
    {
        diff = (ts1 >= ts0) ? (ts1 - ts0) : (ts1 + period - ts0);
        measuredPct = (double)diff * 100.0 / (double)period;
        printf("[phase] ch0 edge=%lu, ch1 edge=%lu, diff=%lu tick = %.3f%% of period (expect %.1f%%)\r\n",
               (unsigned long)ts0, (unsigned long)ts1, (unsigned long)diff, measuredPct,
               (double)expectPermille / 10.0);
        printf("[phase] phase error = %+.3f%% of period\r\n", measuredPct - (double)expectPermille / 10.0);
    }

    Icu_DisableEdgeDetection(ICU_CHANNEL_1);
    Icu_DisableEdgeDetection(ICU_CHANNEL_0);
    Icu_StartSignalMeasurement(ICU_CHANNEL_1);
    Icu_StartSignalMeasurement(ICU_CHANNEL_0);
}

static void cmd_err(void)
{
    uint32 before = Det_GetErrorCount();
    uint16 mod = 0u; uint8 inst = 0u, api = 0u, err = 0u;

    Pwm_SetDutyCycle((Pwm_ChannelType)PWM_NUM_CHANNELS, 0x4000u);   /* PWM_E_PARAM_CHANNEL */
    Pwm_SetDutyCycle(PWM_CHANNEL_0, 0x8001u);                       /* DET_E_PARAM_VALUE   */
    Pwm_SetPeriodAndDuty(PWM_CHANNEL_0, 0u, 0x4000u);               /* DET_E_PARAM_VALUE   */
    Pwm_GetVersionInfo(NULL_PTR);                                   /* PWM_E_PARAM_POINTER */
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
    Pwm_GetVersionInfo(&vi);
    printf("[pwm] Pwm v%u.%u.%u moduleID=%u; f_pwm0=%lu Hz, f_gptmr2=%lu Hz\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version, (unsigned)vi.sw_patch_version,
           (unsigned)vi.moduleID, (unsigned long)s_pwmClkHz, (unsigned long)s_icuClkHz);
    printf("[pwm] ch0=PD02(P2,cnt1,CMP4/5) ch1=PD03(P3,cnt1,CMP6/7); icu ch1<-PD07, icu ch0<-PD06\r\n");
    printf("[pwm] state: ch0 %s, ch1 %s, period=%lu ticks\r\n",
           (Pwm_GetOutputState(PWM_CHANNEL_0) == PWM_HIGH) ? "HIGH" : "LOW",
           (Pwm_GetOutputState(PWM_CHANNEL_1) == PWM_HIGH) ? "HIGH" : "LOW",
           (unsigned long)(s_pwmClkHz / s_freqHz));
}

/* 单入口命令：pwm <sub> [args]（契约 §6.2 命令命名规范） */
static void cmd_pwm(int argc, char **argv)
{
    const char *sub = (argc >= 2) ? argv[1] : "info";

    if ((strcmp(sub, "set") == 0) && (argc >= 3))
    {
        uint32_t pm = 0u;
        if ((bsp_parse_u32(argv[2], &pm) != 0) || (pm > 1000u))
        {
            printf("usage: pwm set <permille 0..1000>\r\n");
            return;
        }
        s_duty = (uint16)((pm * (uint32)PWM_DUTY_100_PERCENT) / 1000u);
        Pwm_SetDutyCycle(PWM_CHANNEL_0, s_duty);
        board_delay_ms(50);
        print_meas("set", s_duty);
    }
    else if ((strcmp(sub, "freq") == 0) && (argc >= 3))
    {
        uint32_t f = 0u;
        if ((bsp_parse_u32(argv[2], &f) != 0) || (f == 0u) || (f > 100000u))
        {
            printf("usage: pwm freq <hz 1..100000>\r\n");
            return;
        }
        s_freqHz = f;
        Pwm_SetPeriodAndDuty(PWM_CHANNEL_0, s_pwmClkHz / f, s_duty);
        board_delay_ms(50);
        printf("[pwm] period -> %lu tick (target %lu Hz)\r\n",
               (unsigned long)(s_pwmClkHz / f), (unsigned long)f);
        print_meas("freq", s_duty);
    }
    else if (strcmp(sub, "meas") == 0)
    {
        print_meas("meas", s_duty);
    }
    else if (strcmp(sub, "phase") == 0)
    {
        run_phase();
    }
    else if (strcmp(sub, "idle") == 0)
    {
        Pwm_SetOutputToIdle(PWM_CHANNEL_0);
        printf("[idle] ch0 output state = %s\r\n",
               (Pwm_GetOutputState(PWM_CHANNEL_0) == PWM_HIGH) ? "HIGH" : "LOW");
        Pwm_SetDutyCycle(PWM_CHANNEL_0, s_duty);
    }
    else if (strcmp(sub, "err") == 0)
    {
        cmd_err();
    }
    else if (strcmp(sub, "sweep") == 0)
    {
        run_duty_sweep();
        run_freq_sweep();
    }
    else
    {
        cmd_info();
        printf("usage: pwm set <pm> | pwm freq <hz> | pwm meas | pwm phase | pwm idle | pwm sweep | pwm err | pwm info\r\n");
    }
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 pwm, cmd_pwm, pwm control: pwm <set|freq|meas|phase|idle|sweep|err|info>);

int main(void)
{
    Std_VersionInfoType vi;

    bsp_init();
    Det_Init();
    Pwm_Init(&Pwm_Config);
    Icu_Init(&Icu_Config);

    s_pwmClkHz = clock_get_frequency(clock_pwm0);
    s_icuClkHz = clock_get_frequency(clock_gptmr2);

    Pwm_GetVersionInfo(&vi);
    printf("\r\n=== mcal pwm_basic (board %s) ===\r\n", BOARD_NAME);
    printf("Pwm v%u.%u.%u f_pwm0=%lu Hz | Icu f_gptmr2=%lu Hz\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version, (unsigned)vi.sw_patch_version,
           (unsigned long)s_pwmClkHz, (unsigned long)s_icuClkHz);

    Pwm_SetPeriodAndDuty(PWM_CHANNEL_0, s_pwmClkHz / PWM_DEFAULT_FREQ, s_duty);
    Pwm_SetDutyCycle(PWM_CHANNEL_1, PWM_DUTY_100_PERCENT / 2u);
    Icu_StartSignalMeasurement(ICU_CHANNEL_0);
    Icu_StartSignalMeasurement(ICU_CHANNEL_1);
    board_delay_ms(30);

    printf("[pwm_basic] auto sweep start (shell idle during run)\r\n");
    board_delay_ms(1000);   /* 给串口抓包留启动时间，避免丢开头 */
    run_duty_sweep();
    run_freq_sweep();
    run_phase();
    cmd_err();
    Pwm_SetOutputToIdle(PWM_CHANNEL_0);
    printf("[idle] ch0 output state = %s\r\n", (Pwm_GetOutputState(PWM_CHANNEL_0) == PWM_HIGH) ? "HIGH" : "LOW");
    Pwm_SetDutyCycle(PWM_CHANNEL_0, s_duty);
    cmd_info();
    printf("[pwm_basic] auto sweep done. cmds: pwm_set/pwm_freq/pwm_meas/pwm_phase/pwm_idle/pwm_err/pwm_info\r\n");

    {
        uint32 last = bsp_uptime_ms();
        while (1)
        {
            bsp_shell_poll();
            if ((bsp_uptime_ms() - last) >= 5000u)
            {
                last = bsp_uptime_ms();
                print_meas("hb", s_duty);   /* 心跳复打，防串口抓包丢字节 */
            }
        }
    }
    return 0;
}