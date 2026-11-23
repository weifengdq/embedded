/*
 * qeo_abz_generate —— QEO 产生 ABZ 正交输出 + 位置/角度寄存器自证   (书中第 16 章)
 *
 * 结构:
 *   QEI0 的位置计数器(QEIV2 COUNT[PH]) 被软件写入位置值 ->
 *   TRGM 位置矩阵 -> QEO0 -> ABZ 输出(PF07=A / PF06=B / PF05=Z)
 *   主循环递增位置, 于是 QEO 的 A/B/Z 会按设定线数输出正交波形。
 *
 * 观测方式(三选一):
 *   1) 示波器/逻辑分析仪: 探头接 PF07/PF06/PF05(推荐, 能看相位与 Z 宽度);
 *   2) 3 根飞线到 QEI0 输入(PC07/PC06/PC05) -> 用 qei_gpio_sim 或本示例读位置闭环验证;
 *   3) 无仪器时: 本示例的寄存器自证 —— 打印 QEO 位置/角度寄存器与 QEI 位置寄存器,
 *      确认"位置 -> QEO"这条内部通路是通的(QEO ABZ 输出本身无法回读)。
 *
 * 串口命令:
 *   qgen <pos_per_s> <lines>  设定每秒位置增量与线数, 启动
 *   qhalt                     停止
 *   qshow                     打印 QEI 位置 / QEO 位置 / QEO 角度 / 配置
 */
#include "bsp.h"
#include "board_timer.h"
#include "hpm_qeiv2_drv.h"
#include "hpm_qeov2_drv.h"
#include "hpm_trgm_drv.h"
#include "hpm_trgm_soc_drv.h"
#include "clock.h"

static uint32_t s_step_hz = 2000U;     /* 每秒位置增量 */
static uint32_t s_lines = 1000U;       /* 编码器线数 */
static volatile uint8_t s_running;

static void qeo_setup(uint32_t lines)
{
    qeo_abz_mode_t abz;
    qeiv2_mode_config_t mode_cfg = {0};

    clock_add_to_group(BOARD_TIMER_QEO0_CLK, 0);
    clock_add_to_group(BOARD_TIMER_QEI0_CLK, 0);

    /* QEI0: 只当"位置寄存器"用, 软件写值; 不接外部信号 */
    mode_cfg.work_mode = qeiv2_work_mode_abz;
    mode_cfg.phcnt_max = lines * 4U;
    mode_cfg.z_count_inc_mode = qeiv2_z_count_inc_on_phase_count_max;
    mode_cfg.spd_tmr_content_sel = qeiv2_spd_tmr_as_spd_tm;
    mode_cfg.phcnt_idx = 0;
    qeiv2_config_mode(HPM_QEI0, &mode_cfg);
    qeiv2_set_phase_cnt(HPM_QEI0, 0);

    /* TRGM 位置矩阵: QEI0 位置 -> QEO0 */
    trgm_pos_matrix_config(HPM_TRGM0, BOARD_QEO_TRGM_POS_PWM, BOARD_QEO_PWM_TRGM_POS_IN, false);

    /* QEO0: ABZ 输出模式 + 线数 */
    qeo_abz_get_default_mode_config(BOARD_TIMER_QEO0, &abz);
    abz.output_type = qeo_abz_output_abz;
    qeo_abz_config_mode(BOARD_TIMER_QEO0, &abz);
    qeo_abz_set_resolution_lines(BOARD_TIMER_QEO0, lines);
}

static void print_show(void)
{
    printf("QEI0 phase=%lu (软件写, 即 QEO 的位置源)  pos=%lu  angle=%lu\r\n",
           (unsigned long) qeiv2_get_phase_cnt(HPM_QEI0),
           (unsigned long) qeiv2_get_postion(HPM_QEI0),
           (unsigned long) qeiv2_get_angle(HPM_QEI0));
    printf("  QEO0 ABZ.RESOLUTION=0x%08lx (线数回读)  mode 已设为 qeo_abz_output_abz\r\n",
           (unsigned long) BOARD_TIMER_QEO0->ABZ.RESOLUTION);
    printf("  lines=%lu step=%lu/s running=%u  ABZ 引脚: A=PF07 B=PF06 Z=PF05\r\n",
           (unsigned long) s_lines, (unsigned long) s_step_hz, (unsigned int) s_running);
    printf("  说明: QEO 的 ABZ 是输出引脚, 无法回读; 观测请用示波器 PF07/06/05,\r\n");
    printf("        或飞线 PF07->PC07 / PF06->PC06 / PF05->PC05 后用 QEI0 解码验证\r\n");
}

static void cmd_qgen(int argc, char *argv[])
{
    uint32_t step = 0, lines = 0;

    if ((argc >= 3) && (bsp_parse_u32(argv[1], &step) == 0) && (bsp_parse_u32(argv[2], &lines) == 0) &&
        (step > 0U) && (lines > 0U)) {
        s_step_hz = step;
        if (lines != s_lines) {
            s_lines = lines;
            qeo_setup(lines);
        }
    }
    s_running = 1;
    print_show();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 qgen, cmd_qgen, start qeo abz generation: qgen <pos_per_s> <lines>);

static void cmd_qhalt(int argc, char *argv[])
{
    (void) argc; (void) argv;
    s_running = 0;
    print_show();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 qhalt, cmd_qhalt, stop qeo abz generation);

static void cmd_qshow(int argc, char *argv[])
{
    (void) argc; (void) argv;
    print_show();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 qshow, cmd_qshow, show qei/qeo position and angle);

int main(void)
{
    uint32_t last, acc = 0;

    bsp_init();
    board_init_led_pins();
    board_timer_init_qeo0_pins();     /* PF07/PF06/PF05 = QEO0 A/B/Z */
    board_timer_init_qei0_pins();     /* PC07/PC06/PC05 = QEI0 A/B/Z (留给飞线闭环) */

    printf("\r\n=== qeo_abz_generate (board %s, ext %s) ===\r\n", BOARD_NAME,
           board_timer_ext_version());
    printf("QEO0 ABZ out: A=PF07 B=PF06 Z=PF05; 位置来源 = QEI0 位置寄存器(软件写)\r\n");
    qeo_setup(s_lines);
    print_show();
    printf("commands: qgen <pos_per_s> <lines> | qhalt | qshow\r\n");

    last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if (s_running) {
            /* 每 1 ms 前进 1 个位置(简化实现, 实际频率由主循环速度决定) */
            if ((bsp_uptime_ms() != last)) {
                last = bsp_uptime_ms();
                acc += 1;
                if (acc >= (1000U / (s_step_hz / 1000U + 1U))) {
                    acc = 0;
                }
                qeiv2_set_phase_cnt(HPM_QEI0, (qeiv2_get_phase_cnt(HPM_QEI0) + 1U) % (s_lines * 4U));
            }
        }
        if ((bsp_uptime_ms() % 3000U) < 20U) {
            static uint32_t shown;
            if (shown != bsp_uptime_ms() / 3000U) {
                shown = bsp_uptime_ms() / 3000U;
                board_led_toggle();
            }
        }
    }
    return 0;
}