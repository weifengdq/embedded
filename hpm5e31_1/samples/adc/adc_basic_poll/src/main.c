/*
 * adc_basic_poll - ADC16 单通道轮询采样 / 噪声统计 / 轮询吞吐实测
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1)
 *   ADC0 通道 1 <= PF26 (ADC0_IN01 / ADC1_IN01, DS Rev0.4 p.53 引脚表)
 *   板上无电位器, PF26 通过排针引出; 悬空时读数即为噪声底, 可外接 0~VREFH 分压验证线性度。
 *
 * 现象: 见 README.md
 */
#include "bsp.h"
#include "hpm_adc16_drv.h"
#include "hpm_csr_drv.h"

#define APP_ADC_BASE         HPM_ADC0
#define APP_ADC_NAME         "ADC0"
#define APP_ADC_CH           (1U)
#define APP_ADC_SAMPLE_CYCLE (20U)   /* 20 个 ADC 时钟采样保持 */
#define APP_ADC_VREF_MV      (3300U) /* 待确认: 板上 VREFH 接 3.3V, 见 README "待确认项" */

static uint32_t s_clk_in_hz;
static uint32_t s_clk_div;
static uint32_t s_conv_hz;
static uint32_t s_fs_hz;
static uint16_t s_last_raw;
static uint32_t s_read_cnt;

static uint32_t raw_to_uv(uint32_t raw)
{
    return (uint32_t) (((uint64_t) raw * APP_ADC_VREF_MV * 1000ULL) / 65535ULL);
}

/* 不用 libm: FPU 上纯算术实现 sqrtf / log2f 近似, 避免链接 -lm */
static float app_sqrtf(float x)
{
    if (x <= 0.0f) {
        return 0.0f;
    }
    float r = x * 0.5f + 0.5f;
    for (int i = 0; i < 24; i++) {
        r = 0.5f * (r + x / r);
    }
    return r;
}

static float app_log2f(float x)
{
    float r = 0.0f;
    if (x <= 0.0f) {
        return 0.0f;
    }
    while (x >= 2.0f) {
        x *= 0.5f;
        r += 1.0f;
    }
    while (x < 1.0f) {
        x *= 2.0f;
        r -= 1.0f;
    }
    /* x in [1,2): log2(x) ~= (x-1) - (x-1)^2/2 + (x-1)^3/3 */
    float t = x - 1.0f;
    return r + t - 0.5f * t * t + t * t * t / 3.0f;
}


/* oneshot 一次读: 阻塞模式(WAIT_DIS=0)下一次读即完成一次转换; 失败最多重试 8 次 */
static bool adc_read_oneshot(uint8_t ch, uint16_t *raw)
{
    for (int i = 0; i < 8; i++) {
        if (adc16_get_oneshot_result(APP_ADC_BASE, ch, raw) == status_success) {
            return true;
        }
    }
    return false;
}

static void cmd_adcsr(void)
{
    printf("ADC16 %s single-ended oneshot\r\n", APP_ADC_NAME);
    printf("  input clk : %lu Hz (ahb bus clock)\r\n", (unsigned long) s_clk_in_hz);
    printf("  clk div   : %lu -> conv clk %lu Hz (max %lu)\r\n",
           (unsigned long) s_clk_div, (unsigned long) s_conv_hz,
           (unsigned long) ADC16_SOC_CONV_CLK_FREQ_MAX);
    printf("  res       : 16 bit, sample_cycle = %u\r\n", (unsigned) APP_ADC_SAMPLE_CYCLE);
    printf("  fs(theory): %lu Hz (conv %u cycles + sample %u cycles)\r\n",
           (unsigned long) s_fs_hz, (unsigned) adc16_res_16_bits, (unsigned) APP_ADC_SAMPLE_CYCLE);
    printf("  VREFH     : %lu mV (assumed, to be confirmed)\r\n", (unsigned long) APP_ADC_VREF_MV);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcsr, cmd_adcsr, ADC16 clock / sample-rate summary);

static void cmd_adc(void)
{
    uint16_t raw = 0;
    if (!adc_read_oneshot(APP_ADC_CH, &raw)) {
        printf("adc read failed\r\n");
        return;
    }
    s_last_raw = raw;
    s_read_cnt++;
    printf("ADC0.IN%02u raw=0x%04x (%5u) -> %lu uV\r\n", (unsigned) APP_ADC_CH, raw, raw,
           (unsigned long) raw_to_uv(raw));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adc, cmd_adc, read ADC0 channel 1 once: adc);

static void cmd_adcstat(int argc, char *argv[])
{
    uint32_t n = 256;
    if (argc >= 2) {
        if (bsp_parse_u32(argv[1], &n) || n == 0 || n > 200000UL) {
            printf("usage: adcstat [1..200000]\r\n");
            return;
        }
    }
    uint32_t t0 = (uint32_t) hpm_csr_get_core_mcycle();
    uint16_t vmin = 0xFFFF, vmax = 0;
    uint64_t sum = 0;
    double sum2 = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t raw = 0;
        if (!adc_read_oneshot(APP_ADC_CH, &raw)) {
            printf("adc read failed at %lu\r\n", (unsigned long) i);
            return;
        }
        if (raw < vmin) vmin = raw;
        if (raw > vmax) vmax = raw;
        sum += raw;
        sum2 += (double) raw * (double) raw;
    }
    uint32_t t1 = (uint32_t) hpm_csr_get_core_mcycle();
    uint32_t cpu_hz = clock_get_frequency(clock_cpu0);
    double mean = (double) sum / (double) n;
    double var = sum2 / (double) n - mean * mean;
    float sd = app_sqrtf((float) (var > 0.0 ? var : 0.0));
    /* ENOB ~= log2(FSR / (RMS_noise * sqrt(12))) */
    float enob = (sd > 0.01f) ? (16.0f - app_log2f(sd * 3.4641016f)) : 16.0f;
    uint32_t cycles = t1 - t0;
    float us_per = (float) cycles * 1e6f / (float) cpu_hz / (float) n;
    printf("adcstat: n=%lu mean=%.2f sd=%.2f min=%u max=%u pp=%u\r\n",
           (unsigned long) n, mean, (double) sd, vmin, vmax, (unsigned) (vmax - vmin));
    printf("         mean=%lu uV  pp=%lu uV  ENOB~%.2f bit\r\n",
           (unsigned long) (mean * (double) APP_ADC_VREF_MV * 1000.0 / 65535.0),
           (unsigned long) ((double) (vmax - vmin) * (double) APP_ADC_VREF_MV * 1000.0 / 65535.0),
           (double) enob);
    printf("         poll rate=%.1f kHz (%.2f us/sample, cpu %lu Hz)\r\n",
           1000.0f / us_per, (double) us_per, (unsigned long) cpu_hz);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcstat, cmd_adcstat, ADC noise statistics: adcstat [n]);



static hpm_stat_t adc_reinit(uint32_t div, uint32_t sample_cycle)
{
    adc16_config_t cfg;
    adc16_channel_config_t ch_cfg;

    adc16_get_default_config(&cfg);
    cfg.res         = adc16_res_16_bits;
    cfg.conv_mode   = adc16_conv_mode_oneshot;
    cfg.adc_clk_div = div;
#if !defined(HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB) || !HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB
    cfg.sel_sync_ahb = (clk_adc_src_ahb0 == clock_get_source(BOARD_APP_ADC16_CLK_NAME)) ? true : false;
#endif
    if (adc16_init(APP_ADC_BASE, &cfg) != status_success) {
        return status_fail;
    }
    adc16_get_channel_default_config(&ch_cfg);
    ch_cfg.ch = APP_ADC_CH;
    ch_cfg.sample_cycle = (uint16_t) sample_cycle;
    if (adc16_init_channel(APP_ADC_BASE, &ch_cfg) != status_success) {
        return status_fail;
    }
#if defined(ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT) && ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT
    adc16_enable_oneshot_mode(APP_ADC_BASE);
#endif
    adc16_set_blocking_read(APP_ADC_BASE);
    return status_success;
}

/* 扫描: conv clk 分频 x 采样保持周期, 输出 fs / 噪声 / ENOB / 轮询吞吐 (CSV 便于做图) */
static void cmd_adcsweep(int argc, char *argv[])
{
    static const uint32_t divs[] = {4, 5, 8, 10, 16};
    static const uint32_t cycles[] = {5, 20, 100, 511};
    uint32_t n = 512;
    if (argc >= 2 && (bsp_parse_u32(argv[1], &n) || n < 16 || n > 20000)) {
        printf("usage: adcsweep [16..20000]\r\n");
        return;
    }
    printf("#div,conv_hz,sample_cycle,fs_hz,mean_lsb,sd_lsb,pp_lsb,enob,poll_khz\r\n");
    for (uint32_t di = 0; di < sizeof(divs) / sizeof(divs[0]); di++) {
        uint32_t div = divs[di];
        uint32_t conv_hz = s_clk_in_hz / div;
        for (uint32_t ci = 0; ci < sizeof(cycles) / sizeof(cycles[0]); ci++) {
            uint32_t sc = cycles[ci];
            uint32_t fs = 0;
            if (adc_reinit(div, sc) != status_success) {
                printf("reinit failed div=%lu sc=%lu\r\n", (unsigned long) div, (unsigned long) sc);
                continue;
            }
            adc16_calc_sample_rate(conv_hz, sc, adc16_res_16_bits, &fs);
            uint32_t t0 = (uint32_t) hpm_csr_get_core_mcycle();
            uint16_t vmin = 0xFFFF, vmax = 0;
            uint64_t sum = 0;
            double sum2 = 0.0;
            for (uint32_t i = 0; i < n; i++) {
                uint16_t raw = 0;
                if (!adc_read_oneshot(APP_ADC_CH, &raw)) {
                    break;
                }
                if (raw < vmin) vmin = raw;
                if (raw > vmax) vmax = raw;
                sum += raw;
                sum2 += (double) raw * (double) raw;
            }
            uint32_t t1 = (uint32_t) hpm_csr_get_core_mcycle();
            double mean = (double) sum / (double) n;
            double var = sum2 / (double) n - mean * mean;
            float sd = app_sqrtf((float) (var > 0.0 ? var : 0.0));
            float enob = (sd > 0.01f) ? (16.0f - app_log2f(sd * 3.4641016f)) : 16.0f;
            float us_per = (float) (t1 - t0) * 1e6f / (float) clock_get_frequency(clock_cpu0) / (float) n;
            printf("%lu,%lu,%lu,%lu,%.2f,%.2f,%u,%.2f,%.1f\r\n",
                   (unsigned long) div, (unsigned long) conv_hz, (unsigned long) sc,
                   (unsigned long) fs, (double) mean, (double) sd, (unsigned) (vmax - vmin),
                   (double) enob, (double) (1000.0f / us_per));
        }
    }
    /* 恢复默认配置 */
    adc_reinit(s_clk_div, APP_ADC_SAMPLE_CYCLE);
    adc16_calc_sample_rate(s_conv_hz, APP_ADC_SAMPLE_CYCLE, adc16_res_16_bits, &s_fs_hz);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcsweep, cmd_adcsweep, sweep conv clk x sample cycle (CSV): adcsweep [n]);


/* 软件过采样: 比较"单次采样"与"m 次平均"的噪声与 ENOB (HPM5E31 无硬件 ADC_LOOP) */
static void cmd_adcavg(int argc, char *argv[])
{
    static const uint32_t ms[] = {1, 2, 4, 16, 64};
    uint32_t groups = 256;
    if (argc >= 2 && (bsp_parse_u32(argv[1], &groups) || groups < 16 || groups > 4096)) {
        printf("usage: adcavg [groups 16..4096]\r\n");
        return;
    }
    printf("#m,gaps,mean_lsb,sd_lsb,pp_lsb,enob,poll_khz\r\n");
    for (uint32_t mi = 0; mi < sizeof(ms) / sizeof(ms[0]); mi++) {
        uint32_t m = ms[mi];
        uint16_t vmin = 0xFFFF, vmax = 0;
        uint64_t sum = 0;
        double sum2 = 0.0;
        uint32_t t0 = (uint32_t) hpm_csr_get_core_mcycle();
        for (uint32_t g = 0; g < groups; g++) {
            uint64_t acc = 0;
            for (uint32_t k = 0; k < m; k++) {
                uint16_t raw = 0;
                if (!adc_read_oneshot(APP_ADC_CH, &raw)) {
                    acc = 0;
                    break;
                }
                acc += raw;
            }
            uint16_t avg = (uint16_t) (acc / m);
            if (avg < vmin) vmin = avg;
            if (avg > vmax) vmax = avg;
            sum += avg;
            sum2 += (double) avg * (double) avg;
        }
        uint32_t t1 = (uint32_t) hpm_csr_get_core_mcycle();
        double mean = (double) sum / (double) groups;
        double var = sum2 / (double) groups - mean * mean;
        float sd = app_sqrtf((float) (var > 0.0 ? var : 0.0));
        float enob = (sd > 0.01f) ? (16.0f - app_log2f(sd * 3.4641016f)) : 16.0f;
        float us_per = (float) (t1 - t0) * 1e6f / (float) clock_get_frequency(clock_cpu0)
                       / (float) (groups * m);
        printf("%lu,%lu,%.2f,%.2f,%u,%.2f,%.1f\r\n",
               (unsigned long) m, (unsigned long) groups, mean, (double) sd,
               (unsigned) (vmax - vmin), (double) enob, (double) (1000.0f / us_per));
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcavg, cmd_adcavg, software oversampling: adcavg [groups]);

int main(void)
{
    bsp_init();

    printf("\r\n=== adc_basic_poll: ADC16 oneshot polling ===\r\n");
    printf("channel: ADC0.IN%u (PF26)\r\n", (unsigned) APP_ADC_CH);

    board_init_adc16_pins();

    s_clk_in_hz = board_init_adc_clock(APP_ADC_BASE, true);
    if (adc16_calc_clock_divider(s_clk_in_hz, ADC16_SOC_CONV_CLK_FREQ_MAX, &s_clk_div) != status_success) {
        printf("ADC16: cannot derive %lu Hz conv clock from %lu Hz\r\n",
               (unsigned long) ADC16_SOC_CONV_CLK_FREQ_MAX, (unsigned long) s_clk_in_hz);
        while (1) {
            bsp_shell_poll();
        }
    }
    s_conv_hz = s_clk_in_hz / s_clk_div;

    adc16_config_t cfg;
    adc16_get_default_config(&cfg);
    cfg.res         = adc16_res_16_bits;
    cfg.conv_mode   = adc16_conv_mode_oneshot;
    cfg.adc_clk_div = s_clk_div;
#if !defined(HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB) || !HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB
    cfg.sel_sync_ahb = (clk_adc_src_ahb0 == clock_get_source(BOARD_APP_ADC16_CLK_NAME)) ? true : false;
#endif
    if (adc16_init(APP_ADC_BASE, &cfg) != status_success) {
        printf("ADC16 init failed\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }

    adc16_channel_config_t ch_cfg;
    adc16_get_channel_default_config(&ch_cfg);
    ch_cfg.ch = APP_ADC_CH;
    ch_cfg.sample_cycle = APP_ADC_SAMPLE_CYCLE;
    if (adc16_init_channel(APP_ADC_BASE, &ch_cfg) != status_success) {
        printf("ADC16 channel init failed\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }
#if defined(ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT) && ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT
    /* HPM5E31: ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT=1, oneshot 必须打开 bus mode, 否则 BUS_RESULT.VALID 不置位 */
    adc16_enable_oneshot_mode(APP_ADC_BASE);
#endif
    /* 默认 wait_dis=true(非阻塞): 读到的 VALID 可能还没置位, 这里改回阻塞读, 保证"一次读=一次转换" */
    adc16_set_blocking_read(APP_ADC_BASE);
    adc16_calc_sample_rate(s_conv_hz, APP_ADC_SAMPLE_CYCLE, adc16_res_16_bits, &s_fs_hz);

    cmd_adcsr();
    printf("shell: adc | adcstat [n] | adcsr\r\n");

    uint32_t last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if (bsp_uptime_ms() - last >= 1000U) {
            last = bsp_uptime_ms();
            uint16_t raw = 0;
            if (adc_read_oneshot(APP_ADC_CH, &raw)) {
                s_last_raw = raw;
                s_read_cnt++;
                printf("t=%lu ms  ADC0.IN%02u raw=%5u (%lu uV)\r\n",
                       (unsigned long) last, (unsigned) APP_ADC_CH, raw,
                       (unsigned long) raw_to_uv(raw));
            }
        }
    }
    return 0;
}
