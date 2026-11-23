/*
 * xcp_can_cal —— XCP 在线标定（DOWNLOAD / SHORT_UPLOAD / MODIFY_BITS）+ 扫参实验
 *                （hpm5e31_1 第 34 章例程 2）
 *
 * 标定量（绝对地址由 tools/gen_a2l.py 从 ELF 符号表生成 A2L）：
 *   g_kp            FLOAT32  PID 比例增益
 *   g_ki            FLOAT32  PID 积分增益
 *   g_led_period_ms UWORD    LED 呼吸周期（ms）
 *   g_pwm_duty_pm   UWORD    PWM 占空比（千分比）
 *   g_ctrl_enable   UBYTE    控制环使能（MODIFY_BITS 改 bit 用）
 *   g_gain_axis[8]  UWORD    增益调度表 X 轴（AXIS_PTS）
 *   g_gain_curve[8] UWORD    增益调度表 Y 值（CURVE，千分比）
 *
 * 标定写后立即生效：PID 在 1 kHz 事件中断里读 g_kp/g_ki，主机 DOWNLOAD 后下一拍就变。
 *
 * 串口命令：
 *   case           打印当前标定值与阶跃响应统计
 *   cal            板内主机走一遍完整标定流程（读/写/回读/位操作/扫参 Kp=0.1..2.0）
 *   step           重新做一次阶跃响应（用当前 g_kp）
 *   clr            清统计
 *   info           配置说明
 *
 * 上电自动跑一遍 cal（约 4 s：10 个 Kp 点 x 300 ms 阶跃），打印 PASS/FAIL 结论行。
 */
#include "bsp.h"
#include "board_timer.h"
#include "clock.h"
#include "hpm_csr_drv.h"
#include "hpm_gptmr_drv.h"
#include "hpm_interrupt.h"

#include "xcp.h"
#include "xcp_port_can.h"
#include "xcp_test_master_cal.h"

/* ---------------- 标定量 / 观测量（A2L 用这些符号的地址） ---------------- */
volatile float    g_kp = 1.0f;
volatile float    g_ki = 2.0f;
volatile uint16_t g_led_period_ms = 500u;
volatile uint16_t g_pwm_duty_pm = 500u;
volatile uint8_t  g_ctrl_enable = 1u;
volatile uint16_t g_gain_axis[8] = {0u, 143u, 286u, 429u, 571u, 714u, 857u, 1000u};
volatile uint16_t g_gain_curve[8] = {1000u, 1000u, 950u, 900u, 850u, 800u, 750u, 700u};
volatile uint32_t g_sw_flag = 0x12345678u;   /* MODIFY_BITS 演示用 */

/* 观测（A2L MEASUREMENT） */
volatile float    g_ctrl_ref = 1000.0f;
volatile float    g_ctrl_y;
volatile float    g_ctrl_u;
volatile uint16_t g_ctrl_y_pm;      /* y/ref 的千分比，便于 DAQ/UPLOAD 观测 */
volatile uint16_t g_ctrl_max_pm;    /* 阶跃峰值（千分比） */
volatile uint16_t g_settle_ms;      /* 调节时间（进入 +-5% 并保持 20 ms） */
volatile uint8_t  g_step_active;    /* 阶跃测试进行中 */
volatile uint8_t  g_step_done;      /* 阶跃测试完成 */
volatile uint32_t g_tick_cnt;

#define APP_CAN_SLAVE   BOARD_CAN0_BASE
#define APP_CAN_MASTER  BOARD_CAN1_BASE
#define APP_TMR         BOARD_TIMER_GPTMR0
#define APP_TMR_IRQ     BOARD_TIMER_GPTMR0_IRQ
#define APP_CH_1K       (0u)

static uint32_t s_cpu_mhz;
static float s_integ;
static uint32_t s_settle_ok_ms;

static uint32_t now_us(void)
{
    return (uint32_t) (hpm_csr_get_core_mcycle() / (uint64_t) s_cpu_mhz);
}

/* 增益调度：按 ref 在 g_gain_axis 上线性插值 g_gain_curve（千分比） */
static float gain_schedule(float ref)
{
    float x = (ref / 1000.0f) * 1000.0f;    /* 轴表 0..1000 对应 ref 0..1000 */
    if (x <= (float) g_gain_axis[0]) {
        return (float) g_gain_curve[0] / 1000.0f;
    }
    for (uint8_t i = 0; i < 7u; i++) {
        float x0 = (float) g_gain_axis[i];
        float x1 = (float) g_gain_axis[i + 1u];
        if (x <= x1) {
            float k = (x1 > x0) ? (x - x0) / (x1 - x0) : 0.0f;
            return ((float) g_gain_curve[i] +
                    k * ((float) g_gain_curve[i + 1u] - (float) g_gain_curve[i])) / 1000.0f;
        }
    }
    return (float) g_gain_curve[7] / 1000.0f;
}

/* 1 kHz 控制环：PID + 一阶对象（tau=50 ms）+ 增益调度 */
static void ctrl_step(void)
{
    const float dt = 0.001f;
    float ref = g_ctrl_ref;
    float err, u, kp;

    if (g_ctrl_enable == 0u) {
        s_integ = 0.0f;
        g_ctrl_y = 0.0f;
        g_ctrl_u = 0.0f;
        return;
    }
    kp = g_kp * gain_schedule(ref);
    err = ref - g_ctrl_y;
    s_integ += g_ki * err * dt;
    if (s_integ > 1500.0f) {
        s_integ = 1500.0f;
    } else if (s_integ < -1500.0f) {
        s_integ = -1500.0f;
    }
    u = kp * err + s_integ;
    g_ctrl_u = u;
    g_ctrl_y += (u - g_ctrl_y) * (dt / 0.05f);
    if (g_ctrl_y > 3000.0f) {
        g_ctrl_y = 3000.0f;
    } else if (g_ctrl_y < -3000.0f) {
        g_ctrl_y = -3000.0f;
    }

    /* 阶跃响应统计 */
    if (g_step_active != 0u) {
        float pm = g_ctrl_y * 1000.0f / ref;
        if (pm > (float) g_ctrl_max_pm) {
            g_ctrl_max_pm = (uint16_t) pm;
        }
        if ((g_ctrl_y > ref * 0.95f) && (g_ctrl_y < ref * 1.05f)) {
            s_settle_ok_ms++;
            if ((s_settle_ok_ms >= 20u) && (g_settle_ms == 0xFFFFu)) {
                g_settle_ms = (uint16_t) (s_settle_ok_ms - 20u);
                g_step_active = 0u;
                g_step_done = 1u;
            }
        } else {
            s_settle_ok_ms = 0u;
        }
    }
    g_ctrl_y_pm = (uint16_t) (g_ctrl_y * 1000.0f / ref);
}

SDK_DECLARE_EXT_ISR_M(APP_TMR_IRQ, app_tmr_isr)
void app_tmr_isr(void)
{
    if (gptmr_check_status(APP_TMR, GPTMR_CH_RLD_STAT_MASK(APP_CH_1K))) {
        gptmr_clear_status(APP_TMR, GPTMR_CH_RLD_STAT_MASK(APP_CH_1K));
        g_tick_cnt++;
        ctrl_step();
        /* LED/占空比标定量参与真实输出：每 1 ms 更新一次软件 PWM 计数 */
        static uint32_t led_cnt;
        static uint32_t pwm_cnt;
        led_cnt = (led_cnt + 1u) % (g_led_period_ms ? g_led_period_ms : 1u);
        pwm_cnt = (pwm_cnt + 1u) % 1000u;
        if (pwm_cnt == 0u) {
            /* 1 ms 边界：占空比控制 LED 亮灭（PA02 用户灯） */
            gpio_write_pin(HPM_GPIO0, GPIO_DI_GPIOA, 2u,
                           (led_cnt * 1000u / (g_led_period_ms ? g_led_period_ms : 1u))
                               < (uint32_t) g_pwm_duty_pm);
        }
    }
}

static void tmr_setup(void)
{
    gptmr_channel_config_t cfg;
    uint32_t clk;

    init_gptmr0_clock();
    clk = clock_get_frequency(BOARD_TIMER_GPTMR0_CLK);
    gptmr_channel_get_default_config(APP_TMR, &cfg);
    cfg.mode = gptmr_work_mode_no_capture;
    cfg.reload = clk / 1000u;
    cfg.cmp[0] = 0xFFFFFFFFu;
    cfg.cmp[1] = 0xFFFFFFFFu;
    cfg.enable_cmp_output = false;
    (void) gptmr_channel_config(APP_TMR, APP_CH_1K, &cfg, false);
    gptmr_channel_reset_count(APP_TMR, APP_CH_1K);
    gptmr_enable_irq(APP_TMR, GPTMR_CH_RLD_IRQ_MASK(APP_CH_1K));
    gptmr_start_counter(APP_TMR, APP_CH_1K);
    intc_m_enable_irq_with_priority(APP_TMR_IRQ, 1);
}

/* ---------------- 阶跃测试（主循环调用，等 ISR 里的统计结果） ---------------- */
void app_step_test(uint32_t timeout_ms)
{
    uint32_t t0 = now_us();

    g_ctrl_y = 0.0f;
    s_integ = 0.0f;
    g_ctrl_max_pm = 0u;
    g_settle_ms = 0xFFFFu;
    s_settle_ok_ms = 0u;
    g_step_done = 0u;
    g_step_active = 1u;
    while (g_step_done == 0u) {
        /* 主循环里继续跑从站协议栈 + shell，保证主机命令不被饿死 */
        bsp_shell_poll();
        xcp_poll();
        if ((uint32_t) (now_us() - t0) > (timeout_ms * 1000u)) {
            g_step_active = 0u;
            g_settle_ms = 0xFFFFu;      /* 超时记为未调节 */
            break;
        }
    }
    if (g_settle_ms == 0xFFFFu) {
        g_settle_ms = 9999u;
    }
}

/* ---------------- XCP 配置 ---------------- */
static const xcp_mem_region_t s_regions[] = {
    {(uint32_t) 0x01200000u, (uint32_t) 0x01240000u, (uint8_t) (XCP_MEM_READ | XCP_MEM_WRITE), "AXI SRAM .data/.bss"},
    {(uint32_t) 0xF0200000u, (uint32_t) 0xF0204000u, (uint8_t) (XCP_MEM_READ | XCP_MEM_WRITE), "AHB SRAM"},
    {(uint32_t) 0x80000000u, (uint32_t) 0x80100000u, (uint8_t) XCP_MEM_READ, "XIP flash (.text/.rodata)"},
};

static const xcp_event_def_t s_events[] = {
    {"EVT_1ms", 1u, XCP_TIMEUNIT_1MS, 0u, 2u},
};

static const uint8_t s_id_string[] = "HPM5E31-XCP-CAN 1.0 (CAL demo)";

static const xcp_cfg_t s_xcp_cfg = {
    .regions = s_regions,
    .region_count = (uint8_t) (sizeof(s_regions) / sizeof(s_regions[0])),
    .events = s_events,
    .event_count = (uint8_t) (sizeof(s_events) / sizeof(s_events[0])),
    .now_us = now_us,
    .id_string = (const char *) s_id_string,
    .seed = NULL,
    .seed_len = 0u,
    .on_connect = NULL,
    .on_disconnect = NULL,
};

/* ---------------- shell 命令 ---------------- */
static void print_cal(void)
{
    printf("cal: kp=%.3f ki=%.3f led_period=%u ms duty=%u permille ctrl_en=%u\r\n",
           (double) g_kp, (double) g_ki, (unsigned) g_led_period_ms,
           (unsigned) g_pwm_duty_pm, (unsigned) g_ctrl_enable);
    printf("obs: ref=%.1f y=%.3f u=%.3f y_pm=%u max_pm=%u settle=%u ms step_done=%u\r\n",
           (double) g_ctrl_ref, (double) g_ctrl_y, (double) g_ctrl_u,
           (unsigned) g_ctrl_y_pm, (unsigned) g_ctrl_max_pm,
           (unsigned) g_settle_ms, (unsigned) g_step_done);
    printf("curve: ");
    for (uint8_t i = 0; i < 8u; i++) {
        printf("%u:%u ", (unsigned) g_gain_axis[i], (unsigned) g_gain_curve[i]);
    }
    printf("\r\n");
}

static void cmd_case(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    print_cal();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 case, cmd_case, show calibration values and control response);

static void cmd_step(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    app_step_test(1000u);
    printf("step: kp=%.3f max_pm=%u settle=%u ms y=%.2f\r\n", (double) g_kp,
           (unsigned) g_ctrl_max_pm, (unsigned) g_settle_ms, (double) g_ctrl_y);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 step, cmd_step, run one step response with current gains);

static void cmd_cal(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    (void) tm_cal_selftest();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 cal, cmd_cal, board-to-board calibration self test + Kp sweep);

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("sample : xcp_can_cal（XCP 在线标定 + 扫参）\r\n");
    printf("CAN    : CAN0 从站 0x555/0x556 500k，CAN1 板内主机\r\n");
    printf("可标定 : g_kp g_ki（FLOAT32）g_led_period_ms g_pwm_duty_pm（UWORD）g_ctrl_enable（UBYTE）\r\n");
    printf("表     : g_gain_axis[8]（AXIS_PTS）g_gain_curve[8]（CURVE）\r\n");
    printf("地址   : kp=0x%08lX ki=0x%08lX duty=0x%08lX flag=0x%08lX\r\n",
           (unsigned long) (uintptr_t) &g_kp, (unsigned long) (uintptr_t) &g_ki,
           (unsigned long) (uintptr_t) &g_pwm_duty_pm, (unsigned long) (uintptr_t) &g_sw_flag);
    printf("命令   : cal / case / step / clr / info\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, show calibration configuration);

static void cmd_clr(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    xcp_reset_stats();
    xcp_can_reset_stats();
    printf("stats cleared\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 clr, cmd_clr, clear statistics);

int main(void)
{
    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000u;
    printf("\r\n[boot] xcp_can_cal：XCP 在线标定 + 扫参\r\n");
    printf("[cfg ] cpu=%lu MHz\r\n", (unsigned long) s_cpu_mhz);

    board_can_init_pins();
    int rc = xcp_can_init(APP_CAN_SLAVE, 0x555u, 0x556u, false);
    if (rc != 0) {
        printf("[FAIL] xcp_can_init(CAN0) rc=%d\r\n", rc);
    }
    tm_init(APP_CAN_MASTER);
    xcp_set_transport(&xcp_can_ops);
    if (xcp_init(&s_xcp_cfg) != 0) {
        printf("[FAIL] xcp_init\r\n");
    }
    tmr_setup();
    printf("[init] 1 kHz 控制环就绪（EVT0），g_ctrl_enable=%u\r\n", (unsigned) g_ctrl_enable);

    (void) tm_cal_selftest();

    while (1) {
        bsp_shell_poll();
        xcp_poll();
    }
    return 0;
}
