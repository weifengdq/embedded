/*
 * pwm_notify —— AUTOSAR Pwm 边沿通知（notification）与中断抖动实测
 *
 * 硬件：PWM0 P2 = PD02（I2C2_SCL 复用），本示例不需要外部接线；
 *       通知来自 PWMv2 比较器中断（CMP4 上升沿 / CMP5 下降沿）。
 *       CH0 配置相位 0、占空比 50%：上升沿 = 计数器重载事件（用重载中断），
 *       下降沿 = CMP5 比较事件。两条路径都覆盖到。
 *
 * 测量口径（mcycle，CPU 480 MHz，1 cycle ≈ 2.083 ns）：
 *   - 相邻两次 notification 的周期抖动 min/max/stddev；
 *   - 单位时间通知次数 vs 理论值 2*f*t（检查有无丢失/重复通知）；
 *   - notification 入口到下一次的间隔分布用于判断调度延迟。
 *
 * 命令：
 *   pwm set <permille>        设置 CH0 占空比
 *   pwm notify <0|1|2>        0=上升沿 1=下降沿 2=双边沿
 *   pwm nstat                 打印统计（并清零窗口）
 *   pwm nclr                  清统计
 *   pwm err                   错误路径（非法通道/非法通知类型/空指针）
 *   pwm info                  配置信息
 */
#include "bsp.h"
#include "Pwm.h"
#include "Pwm_PBcfg.h"
#include "Det.h"
#include <stdio.h>
#include <string.h>

static inline uint32_t cpu_cycles(void)
{
    uint32_t c;
    __asm__ volatile("csrr %0, mcycle" : "=r"(c));
    return c;
}

static uint32_t s_cpuHz;
static uint32_t s_pwmClkHz;
static uint16   s_duty = PWM_DUTY_100_PERCENT / 2u;
static uint32   s_freqHz = 10000u;

static volatile uint32_t s_edges;
static volatile uint32_t s_lastCyc;
static volatile uint32_t s_minDt = 0xFFFFFFFFuL;
static volatile uint32_t s_maxDt;
static volatile uint64_t s_sumDt;
static volatile uint64_t s_sumSq;
static volatile uint32_t s_winStartMs;
static volatile uint32_t s_winEdges;

void Pwm_Notification_0(void)
{
    uint32_t now = cpu_cycles();
    uint32_t dt;

    if (s_edges != 0u)
    {
        dt = now - s_lastCyc;
        if (dt < s_minDt) { s_minDt = dt; }
        if (dt > s_maxDt) { s_maxDt = dt; }
        s_sumDt += dt;
        s_sumSq += (uint64_t)dt * (uint64_t)dt;
    }
    s_lastCyc = now;
    s_edges++;
    s_winEdges++;
}

void Pwm_Notification_1(void)
{
}

static double dsqrt_local(double x)
{
    double r;
    int i;
    if (x <= 0.0) { return 0.0; }
    r = (x > 1.0) ? x : 1.0;
    for (i = 0; i < 40; i++) { r = 0.5 * (r + x / r); }
    return r;
}

static void nclr(void)
{
    uint32_t key = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    s_edges = 0u;
    s_lastCyc = 0u;
    s_minDt = 0xFFFFFFFFuL;
    s_maxDt = 0u;
    s_sumDt = 0u;
    s_sumSq = 0u;
    restore_global_irq(key);
}

static void nstat(void)
{
    uint32_t key, edges, mn, mx;
    uint64_t sum, sq;
    uint32_t nowMs, dtMs;
    double n, nsPerCyc, mean, sd;

    key = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    edges = s_edges; mn = s_minDt; mx = s_maxDt; sum = s_sumDt; sq = s_sumSq;
    nowMs = bsp_uptime_ms();
    dtMs = nowMs - s_winStartMs;
    s_edges = 0u; s_minDt = 0xFFFFFFFFuL; s_maxDt = 0u; s_sumDt = 0u; s_sumSq = 0u;
    s_winStartMs = nowMs;
    s_winEdges = 0u;
    restore_global_irq(key);

    if (edges < 3u)
    {
        printf("[notify] edges=%lu (too few, wait)\r\n", (unsigned long)edges);
        return;
    }
    n = (double)(edges - 1u);
    nsPerCyc = 1e9 / (double)s_cpuHz;
    mean = (double)sum / n;
    sd = dsqrt_local((double)sq / n - mean * mean);
    printf("[notify] edges=%lu in %lu ms  (rate=%.1f Hz)\r\n",
           (unsigned long)edges, (unsigned long)dtMs,
           (dtMs != 0u) ? ((double)edges * 1000.0 / (double)dtMs) : 0.0);
    printf("[notify] expect=%.1f edges for 2*%luHz*%lums  -> lost/extra=%+.0f\r\n",
           2.0 * (double)s_freqHz * (double)dtMs / 1000.0, (unsigned long)s_freqHz,
           (unsigned long)dtMs,
           2.0 * (double)s_freqHz * (double)dtMs / 1000.0 - (double)edges);
    printf("[notify] edge interval: mean=%.3f us min=%.3f max=%.3f pp=%.3f us stddev=%.3f us\r\n",
           mean * nsPerCyc / 1000.0, (double)mn * nsPerCyc / 1000.0,
           (double)mx * nsPerCyc / 1000.0, (double)(mx - mn) * nsPerCyc / 1000.0,
           sd * nsPerCyc / 1000.0);
}

static void apply(uint32 freqHz, uint16 duty)
{
    uint32 reload = s_pwmClkHz / freqHz;
    s_freqHz = freqHz;
    s_duty = duty;
    Pwm_SetPeriodAndDuty(PWM_CHANNEL_0, reload, duty);
    Pwm_SetDutyCycle(PWM_CHANNEL_1, duty);
    nclr();
    s_winStartMs = bsp_uptime_ms();
}

static void cmd_info(void)
{
    Std_VersionInfoType vi;
    Pwm_GetVersionInfo(&vi);
    printf("[pwm] Pwm v%u.%u.%u moduleID=%u f_pwm0=%lu Hz\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, (unsigned)vi.moduleID, (unsigned long)s_pwmClkHz);
    printf("[pwm] ch0 PD02 duty=%u/0x8000 f=%lu Hz; notification=%u; cpu=%lu Hz\r\n",
           (unsigned)s_duty, (unsigned long)s_freqHz, 2u, (unsigned long)s_cpuHz);
    printf("[pwm] state ch0=%s\r\n", (Pwm_GetOutputState(PWM_CHANNEL_0) == PWM_HIGH) ? "HIGH" : "LOW");
}

static void cmd_err(void)
{
    uint32 before = Det_GetErrorCount();
    Pwm_EnableNotification((Pwm_ChannelType)PWM_NUM_CHANNELS, PWM_BOTH_EDGES);
    Pwm_EnableNotification(PWM_CHANNEL_0, (Pwm_EdgeNotificationType)7u);
    Pwm_GetVersionInfo(NULL_PTR);
    printf("[det] before=%lu after=%lu delta=%lu\r\n", (unsigned long)before,
           (unsigned long)Det_GetErrorCount(), (unsigned long)(Det_GetErrorCount() - before));
}

static void cmd_pwm(int argc, char **argv)
{
    const char *sub = (argc >= 2) ? argv[1] : "info";

    if ((strcmp(sub, "set") == 0) && (argc >= 3))
    {
        uint32_t pm = 0u;
        if ((bsp_parse_u32(argv[2], &pm) != 0) || (pm > 1000u))
        {
            printf("usage: pwm set <permille>\r\n");
            return;
        }
        apply(s_freqHz, (uint16)((pm * (uint32)PWM_DUTY_100_PERCENT) / 1000u));
        printf("[pwm] duty -> %u / 0x8000\r\n", (unsigned)s_duty);
    }
    else if ((strcmp(sub, "notify") == 0) && (argc >= 3))
    {
        uint32_t t = 0u;
        if ((bsp_parse_u32(argv[2], &t) != 0) || (t > 2u))
        {
            printf("usage: pwm notify <0|1|2>\r\n");
            return;
        }
        Pwm_EnableNotification(PWM_CHANNEL_0, (Pwm_EdgeNotificationType)t);
        nclr();
        s_winStartMs = bsp_uptime_ms();
        printf("[pwm] notification type -> %lu (0=rising 1=falling 2=both)\r\n", (unsigned long)t);
    }
    else if (strcmp(sub, "nstat") == 0)
    {
        nstat();
    }
    else if (strcmp(sub, "nclr") == 0)
    {
        nclr();
        s_winStartMs = bsp_uptime_ms();
        printf("[pwm] stats cleared\r\n");
    }
    else if (strcmp(sub, "err") == 0)
    {
        cmd_err();
    }
    else
    {
        cmd_info();
        printf("usage: pwm set|notify|nstat|nclr|err|info\r\n");
    }
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 pwm, cmd_pwm, pwm notify control: pwm <set|notify|nstat|nclr|err|info>);

int main(void)
{
    Std_VersionInfoType vi;
    uint32_t last;

    bsp_init();
    Det_Init();
    Pwm_Init(&Pwm_Config);
    s_pwmClkHz = clock_get_frequency(clock_pwm0);
    s_cpuHz = clock_get_frequency(clock_cpu0);

    Pwm_GetVersionInfo(&vi);
    printf("\r\n=== mcal pwm_notify (board %s) ===\r\n", BOARD_NAME);
    printf("Pwm v%u.%u.%u f_pwm0=%lu Hz cpu=%lu Hz\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, (unsigned long)s_pwmClkHz, (unsigned long)s_cpuHz);

    apply(10000u, PWM_DUTY_100_PERCENT / 2u);
    Pwm_EnableNotification(PWM_CHANNEL_0, PWM_BOTH_EDGES);
    s_winStartMs = bsp_uptime_ms();

    board_delay_ms(2000u);
    printf("[pwm_notify] auto 2s measurement:\r\n");
    nstat();

    last = bsp_uptime_ms();
    while (1)
    {
        bsp_shell_poll();
        if ((bsp_uptime_ms() - last) >= 3000u)
        {
            last = bsp_uptime_ms();
            nstat();     /* 心跳：每 3 s 复打一次统计 */
        }
    }
    return 0;
}