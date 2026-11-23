/*
 * hpm5e31_1 板级 timer 资源定义 —— tp-timer 负责 (书中第 14~17 章)
 *
 * 引脚选取原则(避开其它外设, 见 docs/chapters/14-pwmv2-hrpwm.md):
 *   控制台 UART0 : PA00/PA01
 *   CAN FD       : PD00/PD01(CAN0), PD05/PD04(CAN1), PC08/PA09(CAN2), PC15/PC14(CAN3)
 *   LAN8651      : PA19/PA20/PA21/PA22/PA23/PA24
 *   YT8011AN     : PF02~PF13, PA30(MDIO)/PA31(MDC)
 *   LED/按键     : PA02(USR LED), PA03(BTN), PF16/PF18/PF19/PF20/PF21/PF22/PF23/PF26(CAN 状态灯)
 *
 * 因此本文件选的引脚: PD06/PD07(PWM0 CH6/CH7), PF14/PF15(PWM1 CH6/CH7),
 *                     PF16(SDM0 DAT_0, 正好是 CAN 状态灯之一), PF17(SDM0 CLK_0),
 *                     PC05/PC06/PC07(QEI0 Z/B/A), PF05/PF06/PF07(QEO0 Z/B/A)
 *
 * 注意: PWM0 的 CH0~CH5 引脚本板落在 PD00~PD05(与 CAN0/CAN1 复用), 本文件不启用它们,
 *       需要 PWMv2 全 8 通道时请先确认 CAN 未使用对应引脚。
 */
#ifndef BOARD_EXT_TIMER_H
#define BOARD_EXT_TIMER_H

#include "hpm_soc.h"
#include "hpm_iomux.h"
#include "hpm_clock_drv.h"
#include "hpm_trgm_drv.h"

#define BOARD_TIMER_EXT_VERSION "1.0.0"

/* ---------------- PWMv2 ---------------- */
#define BOARD_TIMER_PWM0      HPM_PWM0
#define BOARD_TIMER_PWM0_CLK  clock_pwm0
#define BOARD_TIMER_PWM1      HPM_PWM1
#define BOARD_TIMER_PWM1_CLK  clock_pwm1
#define BOARD_TIMER_PWM0_IRQ  IRQn_PWM0
#define BOARD_TIMER_PWM1_IRQ  IRQn_PWM1

/* PWM0 CH6/CH7 输出: PD06/PD07 */
#define BOARD_TIMER_PWM0_CH6_PAD   IOC_PAD_PD06
#define BOARD_TIMER_PWM0_CH6_FUNC  IOC_PD06_FUNC_CTL_PWM0_P_6
#define BOARD_TIMER_PWM0_CH7_PAD   IOC_PAD_PD07
#define BOARD_TIMER_PWM0_CH7_FUNC  IOC_PD07_FUNC_CTL_PWM0_P_7

/* PWM1 CH6/CH7 输出: PF14/PF15 */
#define BOARD_TIMER_PWM1_CH6_PAD   IOC_PAD_PF14
#define BOARD_TIMER_PWM1_CH6_FUNC  IOC_PF14_FUNC_CTL_PWM1_P_6
#define BOARD_TIMER_PWM1_CH7_PAD   IOC_PAD_PF15
#define BOARD_TIMER_PWM1_CH7_FUNC  IOC_PF15_FUNC_CTL_PWM1_P_7

/* ---------------- TRGM 内部路由 (无需飞线) ----------------
 * PWM0 的 trigger-out n 由 pwmv2_set_trigout_cmp_index() 选择比较器事件产生;
 * TRGM 把 TRGO_6/TRGO_7 接到 GPTMR0 的 CAP2/CAP3 捕获输入,
 * 于是可以用 GPTMR 直接测量 PWM 的时间参数(周期/脉宽/死区), 不占用任何引脚。
 */
#define BOARD_TIMER_TRGM            HPM_TRGM0
#define BOARD_TIMER_PWM0_TRGO_6     HPM_TRGM0_INPUT_SRC_PWM0_TRGO_6
#define BOARD_TIMER_PWM0_TRGO_7     HPM_TRGM0_INPUT_SRC_PWM0_TRGO_7
#define BOARD_TIMER_PWM1_TRGO_6     HPM_TRGM0_INPUT_SRC_PWM1_TRGO_6
#define BOARD_TIMER_PWM1_TRGO_7     HPM_TRGM0_INPUT_SRC_PWM1_TRGO_7
#define BOARD_TIMER_GPTMR0_CAPT_2   HPM_TRGM0_OUTPUT_SRC_GPTMR0_CAPT_2
#define BOARD_TIMER_GPTMR0_CAPT_3   HPM_TRGM0_OUTPUT_SRC_GPTMR0_CAPT_3
#define BOARD_TIMER_GPTMR1_CAPT_2   HPM_TRGM0_OUTPUT_SRC_GPTMR1_CAPT_2
#define BOARD_TIMER_GPTMR1_CAPT_3   HPM_TRGM0_OUTPUT_SRC_GPTMR1_CAPT_3

/* ---------------- GPTMR ---------------- */
#define BOARD_TIMER_GPTMR0        HPM_GPTMR0
#define BOARD_TIMER_GPTMR0_IRQ    IRQn_GPTMR0
#define BOARD_TIMER_GPTMR0_CLK    clock_gptmr0
#define BOARD_TIMER_GPTMR1        HPM_GPTMR1
#define BOARD_TIMER_GPTMR1_IRQ    IRQn_GPTMR1
#define BOARD_TIMER_GPTMR1_CLK    clock_gptmr1
#define BOARD_TIMER_GPTMR2        HPM_GPTMR2
#define BOARD_TIMER_GPTMR2_IRQ    IRQn_GPTMR2
#define BOARD_TIMER_GPTMR2_CLK    clock_gptmr2

/* ---------------- SDM ---------------- */
#define BOARD_TIMER_SDM            HPM_SDM0
#define BOARD_TIMER_SDM_IRQ        IRQn_SDM0
#define BOARD_TIMER_SDM_CLK        clock_sdm0
#define BOARD_TIMER_SDM_DAT0_PAD   IOC_PAD_PF16   /* 板上 CAN 状态灯之一 */
#define BOARD_TIMER_SDM_DAT0_FUNC  IOC_PF16_FUNC_CTL_SDM0_DAT_0
#define BOARD_TIMER_SDM_CLK0_PAD   IOC_PAD_PF17
#define BOARD_TIMER_SDM_CLK0_FUNC  IOC_PF17_FUNC_CTL_SDM0_CLK_0

/* ---------------- QEIv2 / QEO ---------------- */
#define BOARD_TIMER_QEI0        HPM_QEI0
#define BOARD_TIMER_QEI0_IRQ    IRQn_QEI0
#define BOARD_TIMER_QEI0_CLK    clock_qei0
#define BOARD_TIMER_QEI0_A_PAD  IOC_PAD_PC07
#define BOARD_TIMER_QEI0_A_FUNC IOC_PC07_FUNC_CTL_QEI0_A
#define BOARD_TIMER_QEI0_B_PAD  IOC_PAD_PC06
#define BOARD_TIMER_QEI0_B_FUNC IOC_PC06_FUNC_CTL_QEI0_B
#define BOARD_TIMER_QEI0_Z_PAD  IOC_PAD_PC05
#define BOARD_TIMER_QEI0_Z_FUNC IOC_PC05_FUNC_CTL_QEI0_Z

#define BOARD_TIMER_QEO0        HPM_QEO0
#define BOARD_TIMER_QEO0_CLK    clock_qeo0
#define BOARD_TIMER_QEO0_A_PAD  IOC_PAD_PF07
#define BOARD_TIMER_QEO0_A_FUNC IOC_PF07_FUNC_CTL_QEO0_A
#define BOARD_TIMER_QEO0_B_PAD  IOC_PAD_PF06
#define BOARD_TIMER_QEO0_B_FUNC IOC_PF06_FUNC_CTL_QEO0_B
#define BOARD_TIMER_QEO0_Z_PAD  IOC_PAD_PF05
#define BOARD_TIMER_QEO0_Z_FUNC IOC_PF05_FUNC_CTL_QEO0_Z
/* QEO0 的 A/B/Z 与 QEI0 的 A/B/Z 不在同一组引脚上, 因此内部无法自环,
 * 必须用 3 根飞线: PF07->PC07(A), PF06->PC06(B), PF05->PC05(Z) */
#define BOARD_TIMER_QEO_QEI_WIRES_NEEDED 3

/* ---------------- PLB ---------------- */
#define BOARD_TIMER_PLB       HPM_PLB
#define BOARD_TIMER_PLB_CLK   clock_plb0

#ifdef __cplusplus
extern "C" {
#endif

/* pins/pinmux_timer.c */
void board_timer_init_pwm0_pins(void);
void board_timer_init_pwm1_pins(void);
void board_timer_init_trgm_pwm0_to_gptmr0_cap(void);
void board_timer_init_trgm_pwm1_to_gptmr1_cap(void);
void board_timer_init_trgm_gptmr_p00_to_cap(void);
void board_timer_init_sdm_pins(void);
void board_timer_init_qei0_pins(void);
void board_timer_init_qeo0_pins(void);
const char *board_timer_ext_version(void);

#ifdef __cplusplus
}
#endif
#endif /* BOARD_EXT_TIMER_H */