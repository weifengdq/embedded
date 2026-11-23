/*
 * plb_lut_blink —— PLB 查找表分频/输出 + 引脚自环回读观测   (书中第 17 章)
 *
 * 数据通路(全部片内, 不需要飞线):
 *   GPTMR0 CH2 输出比较 1 kHz 方波(500 us 宽)
 *     -> TRGM: PLB_IN_00
 *     -> PLB TYPE A 通道0 LUT0 = 直通(0xAAAA)
 *     -> TRGM: TRGM_P_15 (PF15 引脚输出)
 *   同时: PF15 的 IOC 打开 LOOP_BACK, 把引脚输出回环到引脚输入,
 *         于是 TRGM 输入源 TRGM0_P15 看到同一波形
 *     -> TRGM: GPTMR0 CAPT_3
 *     -> GPTMR0 CH3 捕获 -> 实测频率, 可验证 PLB 输出真的翻转了
 *
 * 为什么用 LOOP_BACK: 一个 pad 同时只能选一个复用功能, 不能既输出又用 GPIO 读回。
 * IOC 的 LOOP_BACK 位把 pad 的输出回灌到 pad 输入路径, 这是 I2C 自环测试用的同一机制,
 * 本示例用它把 PLB 输出"读回来", 从而在无示波器时也能拿到数字。
 *
 * 想用示波器看波形: 探头接 PF15(和 PF14 镜像输出)。
 *
 * 串口命令:
 *   bstat    打印捕获到的频率/周期与 PLB 寄存器
 *   bdiv <n> 用 GPTMR 输出把被测频率改成 clk/n/1000 档位(1=1kHz, 2=500Hz, 5=200Hz...)
 */
#include "bsp.h"
#include "board_timer.h"
#include "hpm_gptmr_drv.h"
#include "hpm_plb_drv.h"
#include "hpm_interrupt.h"
#include "clock.h"

#define GPTMR      BOARD_TIMER_GPTMR0
#define SRC_CH     (3U)   /* 信号源用 OUT3 */
#define CAP_CH     (2U)   /* 捕获用 CAPT_2 (实测可用) */
#define PLB_CH     plb_chn0
#define TRGM       BOARD_TIMER_TRGM
#define TRGM_P15   HPM_TRGM0_OUTPUT_SRC_TRGM0_P15
#define TRGM_P14   HPM_TRGM0_OUTPUT_SRC_TRGM0_P14

static uint32_t s_clk_hz;
static uint32_t s_div = 1U;            /* 被测频率 = 1 kHz / div */
static volatile uint32_t s_cap_n, s_first, s_last;

SDK_DECLARE_EXT_ISR_M(BOARD_TIMER_GPTMR0_IRQ, cap_isr)
void cap_isr(void)
{
    if (gptmr_check_status(GPTMR, GPTMR_CH_CAP_STAT_MASK(CAP_CH))) {
        uint32_t v;
        gptmr_clear_status(GPTMR, GPTMR_CH_CAP_STAT_MASK(CAP_CH));
        v = gptmr_channel_get_counter(GPTMR, CAP_CH, gptmr_counter_type_rising_edge);
        if (s_cap_n == 0U) { s_first = v; }
        s_last = v;
        s_cap_n++;
    }
}

static void src_config(uint32_t div)
{
    gptmr_channel_config_t cfg;

    if (div == 0U) { div = 1U; }
    s_div = div;
    gptmr_channel_get_default_config(GPTMR, &cfg);
    cfg.mode = gptmr_work_mode_no_capture;
    cfg.reload = (s_clk_hz / 1000U) * div;            /* div=1 -> 1 ms -> 1 kHz */
    cfg.cmp[0] = cfg.reload / 2U;                     /* 50% */
    cfg.cmp[1] = 0xFFFFFFFFUL;
    cfg.enable_cmp_output = true;   /* 关键: 使能比较输出, TRGM 才能看到 OUT2 */
    cfg.cmp_initial_polarity_high = false;
    (void) gptmr_channel_config(GPTMR, SRC_CH, &cfg, false);
    gptmr_channel_enable(GPTMR, SRC_CH, true);
    gptmr_channel_reset_count(GPTMR, SRC_CH);
    gptmr_start_counter(GPTMR, SRC_CH);
}

static void cap_config(void)
{
    gptmr_channel_config_t cfg;

    gptmr_channel_get_default_config(GPTMR, &cfg);
    cfg.mode = gptmr_work_mode_capture_at_rising_edge;
    cfg.reload = 0xFFFFFFFFUL;
    cfg.cmp[0] = 0xFFFFFFFFUL;
    cfg.cmp[1] = 0xFFFFFFFFUL;
    cfg.enable_cmp_output = false;
    (void) gptmr_channel_config(GPTMR, CAP_CH, &cfg, false);
    gptmr_channel_reset_count(GPTMR, CAP_CH);
    gptmr_start_counter(GPTMR, CAP_CH);
    gptmr_enable_irq(GPTMR, GPTMR_CH_CAP_IRQ_MASK(CAP_CH));
    intc_m_enable_irq_with_priority(BOARD_TIMER_GPTMR0_IRQ, 1);
}

static void print_stat(void)
{
    uint32_t n, f, l;
    uint32_t key = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    n = s_cap_n; f = s_first; l = s_last;
    restore_global_irq(key);

    printf("[blink] PLB_OUT -> PF15(LOOP_BACK) -> TRGM_P15 -> CAP2: cap_n=%lu\r\n",
           (unsigned long) n);
    if (n >= 2U) {
        uint32_t span = l - f;
        double us = (double) span * 1e6 / (double) s_clk_hz;
        printf("       实测: 跨 %lu tick = %.3f us, %lu 个周期 -> %.3f Hz\r\n",
               (unsigned long) span, us, (unsigned long) (n - 1U),
               (double) (n - 1U) * 1e6 / us);
        printf("       期望: %lu Hz (1 kHz / div=%lu)\r\n",
               (unsigned long) (1000U / s_div), (unsigned long) s_div);
    } else {
        printf("       未捕到 -> 检查 PLB 输出/LOOP_BACK/TRGM 路由\r\n");
    }
    printf("       PLB LUT0=0x%08lx  PF15.FUNC_CTL=0x%08lx  PF14.FUNC_CTL=0x%08lx\r\n",
           (unsigned long) BOARD_TIMER_PLB->TYPE_A[0].LOOKUP_TABLE[0],
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PF15].FUNC_CTL,
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PF14].FUNC_CTL);
}

static void cmd_bstat(int argc, char *argv[])
{
    (void) argc; (void) argv;
    print_stat();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 bstat, cmd_bstat, print plb output capture statistics);

static void cmd_bdiv(int argc, char *argv[])
{
    uint32_t d = 0;

    if ((argc < 2) || (bsp_parse_u32(argv[1], &d) != 0) || (d == 0U) || (d > 1000U)) {
        printf("usage: bdiv <1..1000>  measured freq = 1 kHz / div\r\n");
        return;
    }
    src_config(d);
    print_stat();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 bdiv, cmd_bdiv, set divider: bdiv <1..1000>);

int main(void)
{
    plb_type_a_truth_t lut;
    trgm_output_t cfg = {0};
    uint32_t last;

    bsp_init();
    board_init_led_pins();
    init_gptmr0_clock();
    clock_add_to_group(BOARD_TIMER_PLB_CLK, 0);
    s_clk_hz = clock_get_frequency(BOARD_TIMER_GPTMR0_CLK);

    printf("\r\n=== plb_lut_blink (board %s, ext %s) ===\r\n", BOARD_NAME,
           board_timer_ext_version());
    printf("path: GPTMR0_OUT2 -> PLB_IN_00 -> PLB LUT0 -> TRGM_P15(PF15) -LOOP_BACK-> TRGM_P15 -> CAP3\r\n");

    /* 1) PLB LUT0 = 直通(in0) */
    lut.val = 0xAAAA;
    plb_type_a_set_lut(BOARD_TIMER_PLB, PLB_CH, plb_type_a_table0, &lut);
    plb_type_a_inject_by_sw(BOARD_TIMER_PLB, PLB_CH, 0);

    /* 2) TRGM 输入: GPTMR0_OUT2 -> PLB_IN_00 */
    cfg.invert = false;
    cfg.type = trgm_output_same_as_input;
    cfg.input = HPM_TRGM0_INPUT_SRC_GPTMR0_OUT3;
    trgm_output_config(TRGM, TRGM_TRGOCFG_PLB_IN_00, &cfg);

    /* 3) PLB 输出 -> TRGM_P15 / P14 引脚 */
    cfg.invert = false;
    cfg.type = trgm_output_same_as_input;
    cfg.input = HPM_TRGM0_INPUT_SRC_PLB_OUT00;
    trgm_output_config(TRGM, TRGM_P15, &cfg);
    trgm_output_config(TRGM, TRGM_P14, &cfg);
    trgm_enable_io_output(TRGM, (1UL << 15) | (1UL << 14));

    /* 4) 引脚 PF15/PF14 设为 TRGM 输出, PF15 打开 LOOP_BACK 以便回读 */
    HPM_IOC->PAD[IOC_PAD_PF15].FUNC_CTL = IOC_PF15_FUNC_CTL_TRGM_P_15 | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
    HPM_IOC->PAD[IOC_PAD_PF14].FUNC_CTL = IOC_PF14_FUNC_CTL_TRGM_P_14;

    /* 5) TRGM 输入: TRGM0_P15 -> GPTMR0 CAPT_3 */
    cfg.invert = false;
    cfg.type = trgm_output_same_as_input;
    cfg.input = HPM_TRGM0_INPUT_SRC_TRGM0_P15;
    trgm_output_config(TRGM, BOARD_TIMER_GPTMR0_CAPT_2, &cfg);

    src_config(1);
    cap_config();
    printf("commands: bstat | bdiv <n>\r\n");

    last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if ((bsp_uptime_ms() - last) >= 2000U) {
            last = bsp_uptime_ms();
            print_stat();
            board_led_toggle();
        }
    }
    return 0;
}