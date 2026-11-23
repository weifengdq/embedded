/*
 * adc_pwm_trigger_dma - PWM0 触发 ADC16 序列转换 + DMA 连续采样
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1), 无外部接线
 *   PWM0 第 0 路 TRGO (内部, 不需要引脚) --TRGM--> ADC0_STRGI --触发--> ADC0 序列转换
 *   ADC 结果由序列 DMA 连续写入 non-cacheable 缓冲(1024 个 32bit 采样字)
 *   触发频率 20 kHz, 序列长度 1 通道 (ADC0.IN01 / PF26)
 *
 * 现象: 见 README.md
 */
#include "bsp.h"
#include "hpm_adc16_drv.h"
#include "hpm_pwmv2_drv.h"
#include "hpm_trgm_drv.h"
#include "hpm_trgmmux_src.h"
#include "hpm_csr_drv.h"
#include "hpm_mchtmr_drv.h"

#define APP_ADC_BASE        HPM_ADC0
#define APP_ADC_NAME        "ADC0"
#define APP_ADC_CH          (1U)
#define APP_ADC_SAMPLE_CYCLE (20U)
#define APP_TRIG_HZ         (20000U)
#define APP_DMA_LEN         (1024U)
#define APP_TRIG_CH         (0U)
#define APP_CMP_INDEX       (16U)

ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT(ADC_SOC_DMA_ADDR_ALIGNMENT) static uint32_t s_seq_buff[APP_DMA_LEN];

static volatile uint32_t s_irq_cnt;
static volatile uint32_t s_last_tick;
static volatile uint32_t s_min_gap = 0xFFFFFFFFUL;
static volatile uint32_t s_max_gap;
static volatile uint64_t s_sum_gap;
static uint32_t s_tmr_hz;
static uint32_t s_conv_hz;
static bool s_pwm_running;

SDK_DECLARE_EXT_ISR_M(BOARD_APP_ADC16_IRQn, isr_adc16_dma)
void isr_adc16_dma(void)
{
    uint32_t status = adc16_get_status_flags(APP_ADC_BASE);
    adc16_clear_status_flags(APP_ADC_BASE, status);
    if (ADC16_INT_STS_SEQ_CVC_GET(status)) {
        uint32_t now = mchtmr_get_count(HPM_MCHTMR);
        if (s_irq_cnt > 0) {
            uint32_t gap = now - s_last_tick;
            if (gap < s_min_gap) s_min_gap = gap;
            if (gap > s_max_gap) s_max_gap = gap;
            s_sum_gap += gap;
        }
        s_last_tick = now;
        s_irq_cnt++;
    }
}


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

static void pwm_trigger_start(void)
{
    uint32_t mot_freq = clock_get_frequency(BOARD_APP_ADC16_HW_TRIG_SRC_CLK_NAME);
    PWMV2_Type *pwm = BOARD_APP_ADC16_HW_TRIG_SRC;

    pwmv2_shadow_register_unlock(pwm);
    pwmv2_set_reload_update_time(pwm, pwm_counter_0, pwm_reload_update_on_reload);
    pwmv2_set_shadow_val(pwm, PWMV2_SHADOW_INDEX(0), (mot_freq / APP_TRIG_HZ) - 1, 0, false);
    pwmv2_set_shadow_val(pwm, PWMV2_SHADOW_INDEX(1), ((mot_freq / APP_TRIG_HZ) - 1) >> 1, 0, false);
    pwmv2_select_cmp_source(pwm, APP_CMP_INDEX, cmp_value_from_shadow_val, PWMV2_SHADOW_INDEX(1));
    pwmv2_shadow_register_lock(pwm);

    pwmv2_counter_select_data_offset_from_shadow_value(pwm, pwm_counter_0, PWMV2_SHADOW_INDEX(0));
    pwmv2_counter_burst_disable(pwm, pwm_counter_0);
    if (pwmv2_set_trigout_cmp_index(pwm, APP_TRIG_CH, APP_CMP_INDEX) != status_success) {
        printf("set trigout cmp index failed\r\n");
    }
    pwmv2_enable_counter(pwm, pwm_counter_0);
    s_pwm_running = true;
}

static void pwm_trigger_stop(void)
{
    pwmv2_disable_counter(BOARD_APP_ADC16_HW_TRIG_SRC, pwm_counter_0);
    s_pwm_running = false;
}

static hpm_stat_t adc_seq_setup(void)
{
    adc16_config_t cfg;
    adc16_channel_config_t ch_cfg;
    adc16_seq_config_t seq_cfg;
    adc16_dma_config_t dma_cfg;

    board_init_adc16_pins();
    uint32_t in_hz = board_init_adc_clock(APP_ADC_BASE, true);
    uint32_t div = 0;
    if (adc16_calc_clock_divider(in_hz, ADC16_SOC_CONV_CLK_FREQ_MAX, &div) != status_success) {
        return status_fail;
    }
    s_conv_hz = in_hz / div;

    adc16_get_default_config(&cfg);
    cfg.res = adc16_res_16_bits;
    cfg.conv_mode = adc16_conv_mode_sequence;
    cfg.adc_clk_div = div;
#if !defined(HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB) || !HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB
    cfg.sel_sync_ahb = (clk_adc_src_ahb0 == clock_get_source(BOARD_APP_ADC16_CLK_NAME)) ? true : false;
#endif
    if (adc16_init(APP_ADC_BASE, &cfg) != status_success) {
        return status_fail;
    }

    adc16_get_channel_default_config(&ch_cfg);
    ch_cfg.ch = APP_ADC_CH;
    ch_cfg.sample_cycle = APP_ADC_SAMPLE_CYCLE;
    if (adc16_init_channel(APP_ADC_BASE, &ch_cfg) != status_success) {
        return status_fail;
    }

    seq_cfg.seq_len = 1;
    seq_cfg.restart_en = false;
    seq_cfg.cont_en = true;
    seq_cfg.hw_trig_en = true;
    seq_cfg.sw_trig_en = false;
    seq_cfg.queue[0].ch = APP_ADC_CH;
    seq_cfg.queue[0].seq_int_en = true;
    adc16_set_seq_config(APP_ADC_BASE, &seq_cfg);

    dma_cfg.start_addr = s_seq_buff;
    dma_cfg.buff_len_in_4bytes = APP_DMA_LEN;
    dma_cfg.stop_en = false;
    dma_cfg.stop_pos = 0;
    adc16_init_seq_dma(APP_ADC_BASE, &dma_cfg);

    return status_success;
}

static void cmd_adcinfo(void)
{
    printf("ADC16 %s sequence + PWM trigger + DMA\r\n", APP_ADC_NAME);
    printf("  conv clk  : %lu Hz, sample_cycle=%u, 1 channel (IN%u)\r\n",
           (unsigned long) s_conv_hz, (unsigned) APP_ADC_SAMPLE_CYCLE, (unsigned) APP_ADC_CH);
    printf("  trigger   : PWM0 TRGO_0 -> TRGM -> ADC0_STRGI @ %lu Hz\r\n", (unsigned long) APP_TRIG_HZ);
    printf("  dma buffer: %lu words @0x%08lx (non-cacheable)\r\n",
           (unsigned long) APP_DMA_LEN, (unsigned long) (uintptr_t) s_seq_buff);
    printf("  pwm state : %s, irq_cnt=%lu\r\n", s_pwm_running ? "running" : "stopped",
           (unsigned long) s_irq_cnt);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcinfo, cmd_adcinfo, PWM-trigger ADC DMA setup summary);

static void cmd_adcdma(int argc, char *argv[])
{
    uint32_t ms = 1000;
    if (argc >= 2 && (bsp_parse_u32(argv[1], &ms) || ms < 10 || ms > 10000)) {
        printf("usage: adcdma [10..10000] ms\r\n");
        return;
    }
    /* 清统计 */
    s_irq_cnt = 0;
    s_min_gap = 0xFFFFFFFFUL;
    s_max_gap = 0;
    s_sum_gap = 0;
    s_last_tick = mchtmr_get_count(HPM_MCHTMR);

    if (!s_pwm_running) {
        pwm_trigger_start();
    }
    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    while ((mchtmr_get_count(HPM_MCHTMR) - t0) < (uint64_t) ms * s_tmr_hz / 1000U) {
    }
    pwm_trigger_stop();

    uint32_t irq = s_irq_cnt;
    uint32_t got = irq ? (irq - 1) : 0;   /* 首个间隔无效 */
    double mean_gap_us = (got) ? ((double) s_sum_gap / (double) got) * 1e6 / (double) s_tmr_hz : 0.0;
    double min_gap_us = (got) ? (double) s_min_gap * 1e6 / (double) s_tmr_hz : 0.0;
    double max_gap_us = (got) ? (double) s_max_gap * 1e6 / (double) s_tmr_hz : 0.0;
    printf("adcdma: %lu ms, irq=%lu -> %.1f SPS (expected %lu)\r\n",
           (unsigned long) ms, (unsigned long) irq,
           (double) irq * 1000.0 / (double) ms, (unsigned long) APP_TRIG_HZ);
    printf("  interval: mean=%.2f us min=%.2f max=%.2f us (jitter pp=%.2f us)\r\n",
           mean_gap_us, min_gap_us, max_gap_us, max_gap_us - min_gap_us);

    /* 缓冲区统计: 取前 256 个采样 (result 在低 16 位) */
    uint32_t n = 256;
    uint32_t vmin = 0xFFFF, vmax = 0, sum = 0;
    double sum2 = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t v = (uint16_t) (s_seq_buff[i] & 0xFFFFU);
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
        sum += v;
        sum2 += (double) v * (double) v;
    }
    double mean = (double) sum / (double) n;
    double var = sum2 / (double) n - mean * mean;
    printf("  buffer[0..%lu]: mean=%.1f sd=%.1f min=%u max=%u\r\n",
           (unsigned long) (n - 1), mean, (double) app_sqrtf((float) (var > 0 ? var : 0.0)),
           vmin, vmax);
    printf("  raw[0..3]: 0x%08lx 0x%08lx 0x%08lx 0x%08lx\r\n",
           (unsigned long) s_seq_buff[0], (unsigned long) s_seq_buff[1],
           (unsigned long) s_seq_buff[2], (unsigned long) s_seq_buff[3]);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcdma, cmd_adcdma, run PWM-triggered ADC DMA for N ms: adcdma [ms]);

static void cmd_adcstop(void)
{
    pwm_trigger_stop();
    printf("PWM trigger stopped\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 adcstop, cmd_adcstop, stop PWM trigger);

int main(void)
{
    bsp_init();
    s_tmr_hz = clock_get_frequency(clock_mchtmr0);

    printf("\r\n=== adc_pwm_trigger_dma: PWM0 -> TRGM -> ADC0 seq + DMA ===\r\n");

    if (adc_seq_setup() != status_success) {
        printf("ADC sequence setup failed\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }

    /* TRGM: PWM0 TRGO_0 -> ADC0 序列触发 */
    trgm_output_t trgm_cfg;
    trgm_cfg.invert = false;
    trgm_cfg.type = trgm_output_same_as_input;
    trgm_cfg.input = BOARD_APP_ADC16_HW_TRGM_IN;              /* HPM_TRGM0_INPUT_SRC_PWM0_TRGO_0 */
    trgm_output_config(BOARD_APP_ADC16_HW_TRGM, BOARD_APP_ADC16_HW_TRGM_OUT_SEQ, &trgm_cfg);

    adc16_enable_interrupts(APP_ADC_BASE, adc16_event_seq_single_complete);
    intc_m_enable_irq_with_priority(BOARD_APP_ADC16_IRQn, 1);

    cmd_adcinfo();
    printf("shell: adcinfo | adcdma [ms] | adcstop\r\n");
    pwm_trigger_start();

    uint32_t last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if (bsp_uptime_ms() - last >= 5000U) {
            last = bsp_uptime_ms();
            printf("pwm=%s irq=%lu\r\n", s_pwm_running ? "on" : "off", (unsigned long) s_irq_cnt);
        }
    }
    return 0;
}
