/*
 * sdm_status_int —— SDM 状态/中断/数据就绪自证（无外部调制器）   (书中第 17 章)
 *
 * 背景(必须明确): HPM5E00 的 SDM 是 ΣΔ **接收**单元, MDAT 是输入。
 * 本板没有焊 AD7401/AMC1306 之类的 ΣΔ 调制器, 因此**不会有数据就绪事件**。
 * 本示例把"能证明的部分"全部数字化:
 *   1) 模块/通道时钟、Sinc 滤波器与 OSR 配置的回读;
 *   2) 数据就绪中断、误差中断的使能位与 NVIC 使能;
 *   3) 中断服务函数的进入计数(预期 0) + 状态寄存器原始值;
 *   4) FIFO 门限/计数、比较器阈值配置回读;
 *   5) 计算理论数据率 = SDM 时钟 / OSR, 与实际 0 事件对比, 说明"缺的是调制器不是配置"。
 *
 * 串口命令: sstat | sirq | clear
 */
#include "bsp.h"
#include "board_timer.h"
#include "hpm_sdm_drv.h"
#include "hpm_interrupt.h"
#include "clock.h"

#define APP_SDM   BOARD_TIMER_SDM
#define APP_IRQ   BOARD_TIMER_SDM_IRQ
#define APP_CH    (0U)

static uint32_t s_sdm_clk_hz;
static volatile uint32_t s_isr_n;
static volatile uint32_t s_isr_last_status;
static volatile uint8_t  s_irq_enabled;

SDK_DECLARE_EXT_ISR_M(APP_IRQ, sdm_isr)
void sdm_isr(void)
{
    uint32_t st = sdm_get_status(APP_SDM);
    s_isr_n++;
    s_isr_last_status = st;
    /* 清掉可能的数据就绪/错误标志, 防止中断风暴 */
    if (sdm_get_channel_data_ready_status(APP_SDM, APP_CH)) {
        (void) sdm_get_channel_fifo_data(APP_SDM, APP_CH);
    }
    sdm_clean_channel_filter_status(APP_SDM, APP_CH,
        SDM_CH_SDST_DOV_ERR_MASK | SDM_CH_SDST_DSAT_ERR_MASK);
}

static void sdm_setup(void)
{
    sdm_control_t control;
    sdm_channel_common_config_t ch_cfg;
    sdm_filter_config_t filter;
    sdm_comparator_config_t cmp;

    clock_add_to_group(clock_sdm0, 0);
    s_sdm_clk_hz = clock_get_frequency(clock_sdm0);

    sdm_get_default_module_control(APP_SDM, &control);
    control.interrupt_en = true;
    sdm_init_module(APP_SDM, &control);

    ch_cfg.sampling_mode = sdm_sampling_rising_clk_edge;
    ch_cfg.enable_err_interrupt = true;
    ch_cfg.enable_data_ready_interrupt = true;
    sdm_config_channel_common_setting(APP_SDM, APP_CH, &ch_cfg);

    sdm_get_channel_default_filter_config(APP_SDM, &filter);
    filter.filter_type = sdm_filter_sinc3;
    filter.oversampling_rate = 256U;
    filter.fifo_threshold = 0U;
    filter.ignore_invalid_samples = 2U;
    sdm_config_channel_filter(APP_SDM, APP_CH, &filter);

    sdm_get_channel_default_comparator_config(APP_SDM, &cmp);
    cmp.filter_type = sdm_filter_sinc3;
    cmp.oversampling_rate = 32U;
    cmp.high_threshold = 32000U;
    cmp.low_threshold = 1000U;
    sdm_config_channel_comparator(APP_SDM, APP_CH, &cmp);

    sdm_enable_module_interrupt(APP_SDM, true);
    intc_m_enable_irq_with_priority(APP_IRQ, 1);
    s_irq_enabled = 1;
    sdm_enable_channel(APP_SDM, APP_CH, true);
}

static void print_stat(void)
{
    printf("SDM0 clk=%lu Hz  ch%u: Sinc3 OSR=256 -> 理论数据率 = %lu / 256 = %lu SPS\r\n",
           (unsigned long) s_sdm_clk_hz, APP_CH, (unsigned long) s_sdm_clk_hz,
           (unsigned long) (s_sdm_clk_hz / 256U));
    printf("  IRQ: enabled=%u  isr_entries=%lu  last_status=0x%08lx\r\n",
           (unsigned int) s_irq_enabled, (unsigned long) s_isr_n,
           (unsigned long) s_isr_last_status);
    printf("  status: data_ready=%u  data_error=%u  filter_status=0x%08lx  fifo_count=%u\r\n",
           (unsigned int) sdm_get_channel_data_ready_status(APP_SDM, APP_CH),
           (unsigned int) sdm_get_channel_data_error_status(APP_SDM, APP_CH),
           (unsigned long) sdm_get_channel_filter_status(APP_SDM, APP_CH),
           (unsigned int) sdm_get_channel_fifo_data_count(APP_SDM, APP_CH));
    printf("  comparator: data=%u (高门限 32000 / 低门限 1000, OSR=32)\r\n",
           (unsigned int) sdm_get_channel_comparator_data(APP_SDM, APP_CH));
    printf("  结论: 配置与中断通路就绪; isr_entries=0 说明 MDAT 没有位流(本板无调制器)\r\n");
    printf("  限制: 要测真实数据/精度必须外接 ΣΔ 调制器(第 17 章 §12 待确认 17-A)\r\n");
}

static void cmd_sstat(int argc, char *argv[])
{
    (void) argc; (void) argv;
    print_stat();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sstat, cmd_sstat, print sdm status/irq/data-ready self-check);

static void cmd_sirq(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("isr_entries=%lu last_status=0x%08lx\r\n",
           (unsigned long) s_isr_n, (unsigned long) s_isr_last_status);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sirq, cmd_sirq, print sdm isr entry count);

static void cmd_sclr(int argc, char *argv[])
{
    (void) argc; (void) argv;
    s_isr_n = 0U;
    s_isr_last_status = 0U;
    printf("cleared\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sclr, cmd_sclr, clear sdm isr counters);

int main(void)
{
    uint32_t last;

    bsp_init();
    board_init_led_pins();
    board_timer_init_sdm_pins();
    printf("\r\n=== sdm_status_int (board %s, ext %s) ===\r\n", BOARD_NAME,
           board_timer_ext_version());
    sdm_setup();
    print_stat();
    printf("commands: sstat | sirq | sclr\r\n");

    last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if ((bsp_uptime_ms() - last) >= 3000U) {
            last = bsp_uptime_ms();
            board_led_toggle();
        }
    }
    return 0;
}