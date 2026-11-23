/*
 * pwm_deadtime_fault —— PWMv2 互补死区 + 同步故障保护配置与回读   (书中第 14 章)
 *
 * 配置对象: PWM0 子模块 3 -> P6(PD06)/P7(PD07) 互补输出
 *   死区: PWM_DEAD[DEAD_AREA] 24 位(高 16 位整数 + 低 8 位小数), 最小 0x200
 *         本示例用 pwmv2_set_dead_area()(内部左移 8 位), 再回读寄存器核对
 *   故障: 输出 6/7 的**同步故障输入固定为通用触发输入 3**
 *         (UM §37.4.11, p.616), 本示例用 TRGM 把 TRIG_IN_3 拉到固定电平来注入故障
 *
 * 说明: 死区与故障的**波形**必须用示波器确认; 本示例做的是
 *   ①配置正确性(寄存器回读) ②故障注入后状态位变化 ③死区时间的换算(print)
 *   波形级验证列入第 14 章 §10 待确认项 14-A。
 *
 * 串口命令:
 *   dinfo            打印 PWM_DEAD/故障相关寄存器与换算
 *   dead <ticks>     设置死区(单位: PWM 时钟周期, 200 MHz 下 1 tick = 5 ns)
 *   fault <0|1>      注入(1)/撤销(0)同步故障
 */
#include "bsp.h"
#include "board_timer.h"
#include "hpm_pwmv2_drv.h"
#include "hpm_trgm_drv.h"
#include "clock.h"

#define APP_PWM     BOARD_TIMER_PWM0
#define APP_PWM_CLK BOARD_TIMER_PWM0_CLK
#define APP_CNT     pwm_counter_3
#define APP_CH_X    pwm_channel_6
#define APP_CH_Y    pwm_channel_7
#define TRGM        BOARD_TIMER_TRGM
#define TRGM_TRIG3  HPM_TRGM0_OUTPUT_SRC_PWM0_TRIG_IN_3   /* 输出 6/7 的同步故障输入 */

static uint32_t s_pwm_clk_hz;
static uint32_t s_dead_ticks = 100U;   /* 默认 100 tick = 500 ns @200 MHz */
static uint8_t s_fault_on;

static uint32_t dead_to_ns(uint32_t ticks)
{
    if (s_pwm_clk_hz == 0U) {
        return 0U;
    }
    return (uint32_t) ((uint64_t) ticks * 1000000000ULL / (uint64_t) s_pwm_clk_hz);
}

/* 把 PWM0 的 TRIG_IN_3 拉成固定电平: type=pulse_at_input_falling_edge + input=1 时输出恒高 */
static void trgm_set_trig3(uint8_t level)
{
    trgm_output_t cfg = {0};

    cfg.invert = false;
    cfg.type = trgm_output_pulse_at_input_falling_edge;
    cfg.input = 1;                 /* 常量 1, 由 update_source 改电平 */
    trgm_output_config(TRGM, TRGM_TRIG3, &cfg);
    trgm_output_update_source(TRGM, TRGM_TRIG3, level);
}

static void pwm_setup(void)
{
    pwmv2_deinit(APP_PWM);
    pwmv2_shadow_register_unlock(APP_PWM);
    pwmv2_set_shadow_val(APP_PWM, PWMV2_SHADOW_INDEX(0), 20000U, 0, false);  /* 10 kHz */
    pwmv2_set_shadow_val(APP_PWM, PWMV2_SHADOW_INDEX(12), 0U, 0, false);
    pwmv2_set_shadow_val(APP_PWM, PWMV2_SHADOW_INDEX(13), 10000U, 0, false); /* 50% */
    pwmv2_counter_select_data_offset_from_shadow_value(APP_PWM, APP_CNT, PWMV2_SHADOW_INDEX(0));
    pwmv2_set_reload_update_time(APP_PWM, APP_CNT, pwm_reload_update_on_shlk);
    pwmv2_counter_burst_disable(APP_PWM, APP_CNT);
    pwmv2_select_cmp_source(APP_PWM, PWMV2_CMP_INDEX(12), cmp_value_from_shadow_val, PWMV2_SHADOW_INDEX(12));
    pwmv2_select_cmp_source(APP_PWM, PWMV2_CMP_INDEX(13), cmp_value_from_shadow_val, PWMV2_SHADOW_INDEX(13));
    pwmv2_shadow_register_lock(APP_PWM);

    pwmv2_disable_four_cmp(APP_PWM, APP_CH_X);
    pwmv2_enable_pair_mode(APP_PWM, APP_CH_X);        /* P7 = P6 的互补 */
    pwmv2_set_dead_area(APP_PWM, APP_CH_X, s_dead_ticks);
    pwmv2_set_dead_area(APP_PWM, APP_CH_Y, s_dead_ticks);

    /* 同步故障: 输出 6/7 固定用触发输入 3; 故障时输出强 0, 故障消失立即恢复 */
    pwmv2_set_fault_mode(APP_PWM, APP_CH_X, pwm_fault_output_0);
    pwmv2_set_fault_mode(APP_PWM, APP_CH_Y, pwm_fault_output_0);
    pwmv2_set_fault_recovery_time(APP_PWM, APP_CH_X, pwm_fault_recovery_immediately);
    pwmv2_set_fault_recovery_time(APP_PWM, APP_CH_Y, pwm_fault_recovery_immediately);
    pwmv2_enable_sync_fault(APP_PWM, APP_CH_X);
    pwmv2_enable_sync_fault(APP_PWM, APP_CH_Y);

    pwmv2_channel_enable_output(APP_PWM, APP_CH_X);
    pwmv2_channel_enable_output(APP_PWM, APP_CH_Y);
    pwmv2_enable_counter(APP_PWM, APP_CNT);
    pwmv2_start_pwm_output(APP_PWM, APP_CNT);
}

static void print_info(void)
{
    uint32_t dead_reg = APP_PWM->PWM[APP_CH_X].DEAD_AREA;

    printf("PWM0 clk=%lu Hz  P6=PD06 P7=PD07 互补  PAIR_MODE=1\r\n",
           (unsigned long) s_pwm_clk_hz);
    printf("  死区: ticks=%lu -> DEAD_AREA=0x%08lx (高16位整数=%lu, 低8位小数=%lu)\r\n",
           (unsigned long) s_dead_ticks, (unsigned long) dead_reg,
           (unsigned long) (dead_reg >> 8), (unsigned long) (dead_reg & 0xFFU));
    printf("        换算: %lu ns = %.3f us @ %lu Hz\r\n",
           (unsigned long) dead_to_ns(s_dead_ticks),
           (double) dead_to_ns(s_dead_ticks) / 1000.0, (unsigned long) s_pwm_clk_hz);
    printf("  故障: 同步故障输入=TRIG_IN_3, 当前注入=%u, IRQ_STS_FAULT=0x%08lx\r\n",
           (unsigned int) s_fault_on, (unsigned long) APP_PWM->IRQ_STS_FAULT);
    printf("        PWM_CFG0[6]=0x%08lx (FAULT_EN_SYNC/FAULT_MODE 位域)\r\n",
           (unsigned long) APP_PWM->PWM[APP_CH_X].CFG0);
    printf("  注意: 死区/故障的波形需示波器确认, 本示例只验证寄存器与状态位\r\n");
}

static void cmd_dinfo(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    print_info();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dinfo, cmd_dinfo, print deadtime/fault registers);

static void cmd_dead(int argc, char *argv[])
{
    uint32_t t = 0;

    if ((argc < 2) || (bsp_parse_u32(argv[1], &t) != 0) || (t < 2U) || (t > 65535U)) {
        printf("usage: dead <ticks 2..65535>  (2 tick = 最小死区, 200MHz 下 1 tick=5ns)\r\n");
        return;
    }
    s_dead_ticks = t;
    pwmv2_set_dead_area(APP_PWM, APP_CH_X, t);
    pwmv2_set_dead_area(APP_PWM, APP_CH_Y, t);
    print_info();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dead, cmd_dead, set deadtime in PWM clock ticks: dead <ticks>);

static void cmd_fault(int argc, char *argv[])
{
    uint32_t v = 0;

    if ((argc < 2) || (bsp_parse_u32(argv[1], &v) != 0)) {
        printf("usage: fault <0|1>\r\n");
        return;
    }
    s_fault_on = (uint8_t) (v != 0U);
    trgm_set_trig3(s_fault_on);
    board_delay_ms(1);
    printf("同步故障注入=%u -> IRQ_STS_FAULT=0x%08lx, PWM_CFG0[6]=0x%08lx\r\n",
           (unsigned int) s_fault_on, (unsigned long) APP_PWM->IRQ_STS_FAULT,
           (unsigned long) APP_PWM->PWM[APP_CH_X].CFG0);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 fault, cmd_fault, inject sync fault: fault <0|1>);

int main(void)
{
    uint32_t last;

    bsp_init();
    board_init_led_pins();
    board_timer_init_pwm0_pins();
    s_pwm_clk_hz = clock_get_frequency(APP_PWM_CLK);

    printf("\r\n=== pwm_deadtime_fault (board %s, ext %s) ===\r\n", BOARD_NAME,
           board_timer_ext_version());
    trgm_set_trig3(0);
    pwm_setup();
    print_info();
    printf("commands: dinfo | dead <ticks> | fault <0|1>\r\n");

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
