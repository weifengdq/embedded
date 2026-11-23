/*
 * qei_gpio_sim —— GPIO 软件模拟 A/B 正交信号 + QEI0 解码   (第 16 章)
 *
 * ⚠ 硬件前提: QEI 的 A/B/Z 输入只接 pad 的 QEI 复用功能, 而 GPIO 输出占用同一个
 *   pad 的复用功能, 所以"同一个 pad 既输出又输入"是不可能的。
 *   本示例把正交信号发生器放在 **PD07(A) / PD06(B)**, QEI0 输入放在
 *   **PC07(A) / PC06(B)**, 需要 2 根飞线: PD07->PC07, PD06->PC06。
 *   没有飞线时, QEI 位置计数恒为 0 —— 这也是一种自证("没信号就是 0")。
 *
 * 发生器: 主循环按 1-2-3-4 状态机产生 4 倍频正交序列, 每步可调延时。
 *
 * 串口命令:
 *   qrun <edges_per_s>   设置模拟边沿速率并启动
 *   qstop                停止模拟
 *   qstat                打印 QEI 位置/方向/发生器计数
 */
#include "bsp.h"
#include "board_timer.h"
#include "hpm_gpio_drv.h"
#include "hpm_qeiv2_drv.h"
#include "hpm_qeiv2_soc_drv.h"
#include "clock.h"

#define SIM_A_PORT GPIO_DI_GPIOD
#define SIM_A_PIN  (7U)     /* PD07 -> 飞线到 PC07 */
#define SIM_B_PORT GPIO_DI_GPIOD
#define SIM_B_PIN  (6U)     /* PD06 -> 飞线到 PC06 */

static volatile uint32_t s_gen_edges;
static volatile uint32_t s_edge_us = 200U;
static volatile uint8_t s_running;

/* 正交状态机: 11 -> 10 -> 00 -> 01 循环(正向) */
static void sim_step(bool forward)
{
    static const uint8_t seq[4][2] = { {1,1}, {1,0}, {0,0}, {0,1} };
    static uint8_t idx;

    idx = forward ? (uint8_t)((idx + 1U) & 3U) : (uint8_t)((idx + 3U) & 3U);
    gpio_write_pin(HPM_GPIO0, SIM_A_PORT, SIM_A_PIN, seq[idx][0]);
    gpio_write_pin(HPM_GPIO0, SIM_B_PORT, SIM_B_PIN, seq[idx][1]);
    s_gen_edges++;
}

static void cmd_qrun(int argc, char *argv[])
{
    uint32_t v = 0;

    if ((argc >= 2) && (bsp_parse_u32(argv[1], &v) == 0) && (v >= 10U)) {
        s_edge_us = 1000000U / v;
        if (s_edge_us < 2U) {
            s_edge_us = 2U;
        }
    }
    s_running = 1;
    printf("sim start: edge period %lu us (~%lu edges/s)\r\n",
           (unsigned long) s_edge_us, (unsigned long) (1000000U / s_edge_us));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 qrun, cmd_qrun, start gpio quadrature simulation: qrun <edges_per_s>);

static void cmd_qstop(int argc, char *argv[])
{
    (void) argc; (void) argv;
    s_running = 0;
    printf("sim stopped, generated edges = %lu\r\n", (unsigned long) s_gen_edges);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 qstop, cmd_qstop, stop gpio quadrature simulation);

static void cmd_qstat(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("QEI0: phase=%lu  pos=%lu  angle=%lu  z=%lu  | sim edges=%lu running=%u\r\n",
           (unsigned long) qeiv2_get_phase_cnt(HPM_QEI0),
           (unsigned long) qeiv2_get_postion(HPM_QEI0),
           (unsigned long) qeiv2_get_angle(HPM_QEI0),
           (unsigned long) qeiv2_get_current_count(HPM_QEI0, qeiv2_counter_type_z),
           (unsigned long) s_gen_edges, (unsigned int) s_running);
    if ((qeiv2_get_phase_cnt(HPM_QEI0) == 0U) && (s_gen_edges > 0U)) {
        printf("  note: 位置为 0 -> 检查 PD07->PC07 / PD06->PC06 两根飞线\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 qstat, cmd_qstat, print qei phase/z/speed and sim counter);

int main(void)
{
    qeiv2_mode_config_t mode_config = {0};

    bsp_init();
    board_init_led_pins();
    board_timer_init_qei0_pins();          /* PC07/PC06/PC05 = QEI0 A/B/Z */
    bsp_gpio_output_init(SIM_A_PORT, SIM_A_PIN);
    bsp_gpio_output_init(SIM_B_PORT, SIM_B_PIN);

    clock_add_to_group(BOARD_TIMER_QEI0_CLK, 0);
    /* ABZ 模式, 1000 线 -> 4 倍频后 phcnt_max = 4000 */
    mode_config.work_mode = qeiv2_work_mode_abz;
    mode_config.spd_tmr_content_sel = qeiv2_spd_tmr_as_spd_tm;
    mode_config.z_count_inc_mode = qeiv2_z_count_inc_on_phase_count_max;
    mode_config.phcnt_max = 4000U;
    mode_config.z_cali_enable = false;
    mode_config.z_cali_ignore_ab = false;
    mode_config.phcnt_idx = 0;
    qeiv2_config_mode(HPM_QEI0, &mode_config);
    qeiv2_set_phase_cnt(HPM_QEI0, 0);
    qeiv2_set_z_phase(HPM_QEI0, 0);

    printf("\r\n=== qei_gpio_sim (board %s) ===\r\n", BOARD_NAME);
    printf("QEI0 in: PC07(A)/PC06(B)/PC05(Z);  sim out: PD07(A)/PD06(B)\r\n");
    printf("需要 2 根飞线 PD07->PC07, PD06->PC06; 无飞线时位置恒为 0\r\n");
    printf("commands: qrun <edges_per_s> | qstop | qstat\r\n");

    while (1) {
        bsp_shell_poll();
        if (s_running) {
            sim_step(true);
            board_delay_us(s_edge_us);
        }
    }
    return 0;
}