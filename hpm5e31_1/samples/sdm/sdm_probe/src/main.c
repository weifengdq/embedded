/*
 * sdm_probe —— SDM（ΣΔ 信号接收单元）初始化与状态自检   (书中第 17 章)
 *
 * ⚠ 重要结论: HPM5E00 的 SDM 是 ΣΔ **接收**单元（UM §43.2, p.723）:
 *   - SDM0_DAT_x 是**数据输入**引脚（接收外部调制器的 1 bit 位流）
 *   - SDM 没有任何"发送位流"的接口, 因此**不能用 SDM 驱动 LED 做亮度**
 *     调光请用 PWM（第 14 章）或 PLB 做 ΣΔ 调制。
 *
 * 本示例做的是: 打开 SDM 时钟 -> 配 Sinc3 + OSR 256 -> 读回寄存器与状态,
 * 用于在没有外部调制器时自检"配置通路是否正常"。有调制器时同一份配置即可出数据。
 *
 * 串口命令: sinfo | sdata
 */
#include "bsp.h"
#include "board_timer.h"
#include "hpm_sdm_drv.h"
#include "clock.h"

#define APP_SDM   BOARD_TIMER_SDM
#define APP_CH    (0U)

static uint32_t s_sdm_clk_hz;

static void sdm_setup(void)
{
    sdm_control_t control;
    sdm_channel_common_config_t ch_cfg;
    sdm_filter_config_t filter;

    clock_add_to_group(clock_sdm0, 0);
    s_sdm_clk_hz = clock_get_frequency(clock_sdm0);

    sdm_get_default_module_control(APP_SDM, &control);
    control.interrupt_en = false;
    sdm_init_module(APP_SDM, &control);

    ch_cfg.sampling_mode = sdm_sampling_rising_clk_edge;
    ch_cfg.enable_err_interrupt = false;
    ch_cfg.enable_data_ready_interrupt = false;
    sdm_config_channel_common_setting(APP_SDM, APP_CH, &ch_cfg);

    sdm_get_channel_default_filter_config(APP_SDM, &filter);
    filter.filter_type = sdm_filter_sinc3;
    filter.oversampling_rate = 256U;
    filter.fifo_threshold = 0U;
    filter.ignore_invalid_samples = 2U;
    sdm_config_channel_filter(APP_SDM, APP_CH, &filter);

    sdm_enable_channel(APP_SDM, APP_CH, true);
}

static void print_info(void)
{
    printf("SDM0 clk=%lu Hz  ch%u: Sinc3 OSR=256 (数据率 = 调制器时钟/256)\r\n",
           (unsigned long) s_sdm_clk_hz, APP_CH);
    printf("  引脚: SDM0_DAT_0=PF16(输入) SDM0_CLK_0=PF17\r\n");
    printf("  本板无外部 ΣΔ 调制器 -> MDAT 无有效位流, 读回数据无意义\r\n");
    printf("  状态: data_ready=%u  data_error=%u  filter_status=0x%08lx\r\n",
           (unsigned int) sdm_get_channel_data_ready_status(APP_SDM, APP_CH),
           (unsigned int) sdm_get_channel_data_error_status(APP_SDM, APP_CH),
           (unsigned long) sdm_get_channel_filter_status(APP_SDM, APP_CH));
    printf("  说明: 想验证真实数据请接 AD7401/AMC1306 等调制器(见第 17 章 §12)\r\n");
}

static void cmd_sinfo(int argc, char *argv[])
{
    (void) argc; (void) argv;
    print_info();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sinfo, cmd_sinfo, print sdm configuration and status);

static void cmd_sdata(int argc, char *argv[])
{
    uint32_t n = 0;
    (void) argc; (void) argv;

    n = sdm_get_channel_fifo_data_count(APP_SDM, APP_CH);
    printf("FIFO count=%lu\r\n", (unsigned long) n);
    if (n > 0U) {
        printf("  first data = %ld\r\n", (long) sdm_get_channel_fifo_data(APP_SDM, APP_CH));
    } else {
        printf("  FIFO 空: 没有 MDAT 位流(正常, 本板无调制器)\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sdata, cmd_sdata, read sdm fifo data);

int main(void)
{
    uint32_t last;

    bsp_init();
    board_init_led_pins();
    board_timer_init_sdm_pins();
    printf("\r\n=== sdm_probe (board %s) ===\r\n", BOARD_NAME);
    sdm_setup();
    print_info();
    printf("commands: sinfo | sdata\r\n");

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
