/*
 * plb_lut_passthrough —— PLB TYPE A 查找表 + TRGM 内部互联 + GPTMR 捕获验证 (第 17 章)
 *
 * 数据通路(全部片内, 不需要飞线):
 *   GPTMR0 CH2 输出比较产生 1 kHz 方波(宽脉冲, 500 us 高/500 us 低)
 *     -> TRGM  PLB_IN_00
 *     -> PLB   TYPE A 通道0, LUT0 配置成"输出=输入0"(直通)
 *     -> TRGM  GPTMR0_CAPT_3
 *     -> GPTMR0 CH3 捕获, 主循环打印实测频率
 *
 * 为什么选宽脉冲: PWM 的 TRGO 是 5 ns 窄脉冲, 跨时钟域会漏采(见第 14 章坑 14-3);
 * GPTMR 的输出比较方波周期是 500 us, 捕获 100% 可靠。
 *
 * 串口命令:
 *   lstat   打印统计(捕获次数/实测频率/PLB 寄存器)
 *   linj    用 PLB 软件注入(SW_INJECT)打一个脉冲, 验证 PLB 输出通路
 */
#include "bsp.h"
#include "board_timer.h"
#include "hpm_gptmr_drv.h"
#include "hpm_plb_drv.h"
#include "hpm_interrupt.h"
#include "clock.h"

#define GPTMR      BOARD_TIMER_GPTMR0
#define OUT_CH     (2U)     /* 输出比较通道 */
#define CAP_CH     (3U)     /* 捕获通道 */
#define PLB_CH     plb_chn0
#define PLB_IN0    TRGM_TRGOCFG_PLB_IN_00
#define PLB_OUT00  HPM_TRGM0_INPUT_SRC_PLB_OUT00

static uint32_t s_clk_hz;
static volatile uint32_t s_cap_n, s_cap_first, s_cap_last;

SDK_DECLARE_EXT_ISR_M(BOARD_TIMER_GPTMR0_IRQ, cap_isr)
void cap_isr(void)
{
    if (gptmr_check_status(GPTMR, GPTMR_CH_CAP_STAT_MASK(CAP_CH))) {
        uint32_t v;
        gptmr_clear_status(GPTMR, GPTMR_CH_CAP_STAT_MASK(CAP_CH));
        v = gptmr_channel_get_counter(GPTMR, CAP_CH, gptmr_counter_type_rising_edge);
        if (s_cap_n == 0U) {
            s_cap_first = v;
        }
        s_cap_last = v;
        s_cap_n++;
    }
}

static void route_init(void)
{
    trgm_output_t cfg = {0};

    /* GPTMR0 OUT2 -> PLB_IN_00 */
    cfg.invert = false;
    cfg.type = trgm_output_same_as_input;
    cfg.input = HPM_TRGM0_INPUT_SRC_GPTMR0_OUT2;
    trgm_output_config(BOARD_TIMER_TRGM, PLB_IN0, &cfg);

    /* PLB_OUT00 -> GPTMR0 CAPT_3 */
    cfg.invert = false;
    cfg.type = trgm_output_same_as_input;
    cfg.input = PLB_OUT00;
    trgm_output_config(BOARD_TIMER_TRGM, BOARD_TIMER_GPTMR0_CAPT_3, &cfg);
}

static void plb_init(void)
{
    plb_type_a_truth_t lut;

    clock_add_to_group(BOARD_TIMER_PLB_CLK, 0);
    /* LUT4 索引 = {in3,in2,in1,in0}, 让输出 = in0: 奇数索引为 1 -> 0xAAAA */
    lut.val = 0xAAAA;
    plb_type_a_set_lut(BOARD_TIMER_PLB, PLB_CH, plb_type_a_table0, &lut);
    plb_type_a_inject_by_sw(BOARD_TIMER_PLB, PLB_CH, 0);
}

static void gptmr_init(void)
{
    gptmr_channel_config_t cfg;

    init_gptmr0_clock();
    s_clk_hz = clock_get_frequency(BOARD_TIMER_GPTMR0_CLK);

    /* CH2: 输出比较, 1 ms 周期 / 50% -> 1 kHz 方波 */
    gptmr_channel_get_default_config(GPTMR, &cfg);
    cfg.mode = gptmr_work_mode_no_capture;
    cfg.reload = s_clk_hz / 1000U;             /* 1 ms */
    cfg.cmp[0] = s_clk_hz / 2000U;             /* 0.5 ms 翻转 */
    cfg.cmp[1] = 0xFFFFFFFFUL;
    cfg.enable_cmp_output = false;
    cfg.cmp_initial_polarity_high = false;
    (void) gptmr_channel_config(GPTMR, OUT_CH, &cfg, false);
    gptmr_channel_enable(GPTMR, OUT_CH, true);   /* 使能比较输出 CMPEN, 否则内部 OUT2 不翻转 */
    gptmr_channel_reset_count(GPTMR, OUT_CH);
    gptmr_start_counter(GPTMR, OUT_CH);

    /* CH3: 捕获上升沿 */
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
    n = s_cap_n; f = s_cap_first; l = s_cap_last;
    restore_global_irq(key);

    printf("[plb] cap_n=%lu  PLB_OUT00 上升沿 ", (unsigned long) n);
    if (n >= 2U) {
        uint32_t span = l - f;
        printf("跨 %lu 个 tick = %.3f us -> %.3f Hz\r\n", (unsigned long) span,
               (double) span * 1e6 / (double) s_clk_hz,
               (double) (n - 1U) * (double) s_clk_hz / (double) span);
    } else {
        printf("(还没捕到, 检查 TRGM/PLB 路由)\r\n");
    }
    printf("      PLB LUT0=0x%08lx  OUT=%lu Hz  GPTMR=%lu Hz\r\n",
           (unsigned long) BOARD_TIMER_PLB->TYPE_A[0].LOOKUP_TABLE[0],
           (unsigned long) (s_clk_hz / 1000U), (unsigned long) s_clk_hz);
}

static void cmd_lstat(int argc, char *argv[])
{
    (void) argc; (void) argv;
    print_stat();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 lstat, cmd_lstat, print plb/trgm capture statistics);

static void cmd_linj(int argc, char *argv[])
{
    (void) argc; (void) argv;
    plb_type_a_inject_by_sw(BOARD_TIMER_PLB, PLB_CH, 1);
    board_delay_us(5);
    plb_type_a_inject_by_sw(BOARD_TIMER_PLB, PLB_CH, 0);
    printf("PLB SW_INJECT 1->0 done\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 linj, cmd_linj, inject one pulse by plb software inject);

int main(void)
{
    uint32_t last;

    bsp_init();
    board_init_led_pins();
    printf("\r\n=== plb_lut_passthrough (board %s) ===\r\n", BOARD_NAME);
    printf("path: GPTMR0_OUT2(1kHz) -> TRGM PLB_IN_00 -> PLB TYPEA LUT0(直通) -> TRGM -> GPTMR0 CAP3\r\n");
    plb_init();
    route_init();
    gptmr_init();
    printf("commands: lstat | linj\r\n");

    last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if ((bsp_uptime_ms() - last) >= 3000U) {
            last = bsp_uptime_ms();
            print_stat();
            board_led_toggle();
        }
    }
    return 0;
}