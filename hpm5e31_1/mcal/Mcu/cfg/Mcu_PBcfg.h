/*
 * Mcu_PBcfg.h —— MCU post-build 配置（板级）声明
 *
 * 板级映射（HPM5E31，hpm5e31_1 板）:
 *   PLL0 = 24 MHz XTAL x 40 = 960 MHz；POSTDIV0 = /2 -> 480 MHz；POSTDIV1 = /2.4 -> 400 MHz
 *   CPU0  = pll0_clk0 / 1 = 480 MHz（BOARD_CPU_FREQ）
 *   MCHTMR0 = osc24m / 1 = 24 MHz（BSP 的 board_delay_us 用它）
 * 改板只改 Mcu_PBcfg.c。
 */
#ifndef MCU_PBCFG_H
#define MCU_PBCFG_H

#include "Mcu.h"

/* 时钟设置下标（Mcu_InitClock 的 Mcu_ClockType 取值） */
#define MCU_PBCFG_CLOCK_480MHZ     (0u)
#define MCU_PBCFG_CLOCK_240MHZ     (1u)

/* SYSCTL 时钟节点号（与 HPM5E31 SYSCTL 寄存器一一对应） */
#define MCU_PBCFG_NODE_CPU0        (0u)
#define MCU_PBCFG_NODE_MCHTMR0     (1u)
#define MCU_PBCFG_NODE_AHB0        (2u)

/* 时钟源编号（SYSCTL CLOCKx.MUX 的取值） */
#define MCU_PBCFG_SRC_OSC24M       (0u)
#define MCU_PBCFG_SRC_PLL0_CLK0    (1u)
#define MCU_PBCFG_SRC_PLL0_CLK1    (2u)

#endif /* MCU_PBCFG_H */
