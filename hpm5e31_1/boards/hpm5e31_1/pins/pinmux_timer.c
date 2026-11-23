/*
 * hpm5e31_1 引脚复用 —— tp-timer 负责的 timer/PWM/QEI/QEO/SDM 域
 *
 * 本文件由 boards/hpm5e31_1/CMakeLists.txt 里的 file(GLOB pins/pinmux_*.c) 自动加入编译,
 * 不需要修改 CMakeLists。引脚清单与理由见 ext/board_timer.h 顶部注释。
 */
#include "board.h"
#include "board_timer.h"

/* PWM0 CH6/CH7 -> PD06/PD07 */
void board_timer_init_pwm0_pins(void)
{
    HPM_IOC->PAD[BOARD_TIMER_PWM0_CH6_PAD].FUNC_CTL = BOARD_TIMER_PWM0_CH6_FUNC;
    HPM_IOC->PAD[BOARD_TIMER_PWM0_CH7_PAD].FUNC_CTL = BOARD_TIMER_PWM0_CH7_FUNC;
}

/* PWM1 CH6/CH7 -> PF14/PF15 */
void board_timer_init_pwm1_pins(void)
{
    HPM_IOC->PAD[BOARD_TIMER_PWM1_CH6_PAD].FUNC_CTL = BOARD_TIMER_PWM1_CH6_FUNC;
    HPM_IOC->PAD[BOARD_TIMER_PWM1_CH7_PAD].FUNC_CTL = BOARD_TIMER_PWM1_CH7_FUNC;
}

/* TRGM: PWM0 TRGO_6 -> GPTMR0 CAP2, PWM0 TRGO_7 -> GPTMR0 CAP3 (纯内部, 不占引脚) */
void board_timer_init_trgm_pwm0_to_gptmr0_cap(void)
{
    trgm_output_t cfg = {0};

    cfg.invert = false;
    cfg.type = trgm_output_same_as_input;
    cfg.input = BOARD_TIMER_PWM0_TRGO_6;
    trgm_output_config(BOARD_TIMER_TRGM, BOARD_TIMER_GPTMR0_CAPT_2, &cfg);

    cfg.invert = false;
    cfg.type = trgm_output_same_as_input;
    cfg.input = BOARD_TIMER_PWM0_TRGO_7;
    trgm_output_config(BOARD_TIMER_TRGM, BOARD_TIMER_GPTMR0_CAPT_3, &cfg);
}

/* TRGM: PWM1 TRGO_6/7 -> GPTMR1 CAP2/CAP3 */
void board_timer_init_trgm_pwm1_to_gptmr1_cap(void)
{
    trgm_output_t cfg = {0};

    cfg.invert = false;
    cfg.type = trgm_output_same_as_input;
    cfg.input = BOARD_TIMER_PWM1_TRGO_6;
    trgm_output_config(BOARD_TIMER_TRGM, BOARD_TIMER_GPTMR1_CAPT_2, &cfg);

    cfg.invert = false;
    cfg.type = trgm_output_same_as_input;
    cfg.input = BOARD_TIMER_PWM1_TRGO_7;
    trgm_output_config(BOARD_TIMER_TRGM, BOARD_TIMER_GPTMR1_CAPT_3, &cfg);
}

/* 外部信号捕获入口: PC00 -> IOC 复用成 TRGM_P_00 -> TRGM -> GPTMR0 CAPT_3 */
void board_timer_init_trgm_gptmr_p00_to_cap(void)
{
    trgm_output_t cfg = {0};

    HPM_IOC->PAD[IOC_PAD_PC00].FUNC_CTL = IOC_PC00_FUNC_CTL_TRGM_P_00;
    cfg.invert = false;
    cfg.type = trgm_output_same_as_input;
    cfg.input = HPM_TRGM0_INPUT_SRC_TRGM0_P00;
    trgm_output_config(BOARD_TIMER_TRGM, BOARD_TIMER_GPTMR0_CAPT_2, &cfg);
}

/* SDM0 位流输出: DAT_0 -> PF16 (板上 CAN 状态灯之一), CLK_0 -> PF17 */
void board_timer_init_sdm_pins(void)
{
    HPM_IOC->PAD[BOARD_TIMER_SDM_DAT0_PAD].FUNC_CTL = BOARD_TIMER_SDM_DAT0_FUNC;
    HPM_IOC->PAD[BOARD_TIMER_SDM_CLK0_PAD].FUNC_CTL = BOARD_TIMER_SDM_CLK0_FUNC;
}

/* QEI0 输入: A->PC07, B->PC06, Z->PC05 */
void board_timer_init_qei0_pins(void)
{
    HPM_IOC->PAD[BOARD_TIMER_QEI0_A_PAD].FUNC_CTL = BOARD_TIMER_QEI0_A_FUNC;
    HPM_IOC->PAD[BOARD_TIMER_QEI0_B_PAD].FUNC_CTL = BOARD_TIMER_QEI0_B_FUNC;
    HPM_IOC->PAD[BOARD_TIMER_QEI0_Z_PAD].FUNC_CTL = BOARD_TIMER_QEI0_Z_FUNC;
}

/* QEO0 输出: A->PF07, B->PF06, Z->PF05 */
void board_timer_init_qeo0_pins(void)
{
    HPM_IOC->PAD[BOARD_TIMER_QEO0_A_PAD].FUNC_CTL = BOARD_TIMER_QEO0_A_FUNC;
    HPM_IOC->PAD[BOARD_TIMER_QEO0_B_PAD].FUNC_CTL = BOARD_TIMER_QEO0_B_FUNC;
    HPM_IOC->PAD[BOARD_TIMER_QEO0_Z_PAD].FUNC_CTL = BOARD_TIMER_QEO0_Z_FUNC;
}