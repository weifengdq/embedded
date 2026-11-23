/*
 * Pwm_PBcfg.c —— PWM post-build 配置实例 + 板级 glue
 *
 * 引脚依据：soc/HPM5E00/HPM5E31/hpm_iomux.h
 *   PD02: ALT16 = PWM0_P_2   (I2C2_SCL，与 PD07=I2C3_SCL 杜邦互连)
 *   PD03: ALT16 = PWM0_P_3   (I2C2_SDA，与 PD06=I2C3_SDA 杜邦互连)
 * 计数器/比较器映射依据 RM §37.4.8 (p.613) 与 docs/chapters/14-pwmv2-hrpwm.md 表 14-4：
 *   P2/P3 属计数器 1；P2 用 CMP4/CMP5，P3 用 CMP6/CMP7；影子 0=周期，4/5/6/7=比较值。
 */
#include "Pwm.h"
#include "Pwm_PBcfg.h"
#include "hpm_soc.h"
#include "hpm_iomux.h"
#include "hpm_clock_drv.h"
#include "hpm_pwmv2_drv.h"
#include "hpm_interrupt.h"

/* 中断向量桩（SDK 要求 IRQn 字面量；驱动只暴露 Pwm_IsrHandler()） */
SDK_DECLARE_EXT_ISR_M(IRQn_PWM0, pwm0_isr_stub)
void pwm0_isr_stub(void)
{
    Pwm_IsrHandler();
}

MCAL_WEAK void Pwm_Notification_0(void) { }
MCAL_WEAK void Pwm_Notification_1(void) { }

/* 影子寄存器索引：0 = 计数器重载值，4/5 = P2 比较，6/7 = P3 比较（RM §37.4.5） */
#define PWM_SHADOW_RELOAD (0u)
#define PWM_SHADOW_CMP4   (4u)
#define PWM_SHADOW_CMP5   (5u)
#define PWM_SHADOW_CMP6   (6u)
#define PWM_SHADOW_CMP7   (7u)

static const Pwm_ChannelConfigType Pwm_ChannelConfig[] = {
    /* PWM_CHANNEL_0: PWM0 P2 -> PD02，计数器 1，CMP4(上升)/CMP5(下降) */
    {
        (uint32)(uintptr_t)HPM_PWM0,
        (uint32)clock_pwm0,
        (uint32)(uintptr_t)HPM_IOC,
        (uint32)IOC_PAD_PD02,
        (uint32)IOC_PD02_FUNC_CTL_PWM0_P_2,
        0u,                              /* PAD_CTL：输出默认（与 board pinmux.c 一致） */
        (uint8)pwm_channel_2,
        (uint8)pwm_counter_1,
        4u, 5u,
        PWM_SHADOW_CMP4, PWM_SHADOW_CMP5,
        PWM_LOW,                         /* idle 电平 */
        PWM_VARIABLE_PERIOD,
        PWM_DUTY_0_PERCENT / 4u,         /* 25% */
        0u,                              /* phase = 0 */
        10000u,                          /* 10 kHz @100 MHz */
        &Pwm_Notification_0
    },
    /* PWM_CHANNEL_1: PWM0 P3 -> PD03，计数器 1，CMP6(上升)/CMP7(下降) */
    {
        (uint32)(uintptr_t)HPM_PWM0,
        (uint32)clock_pwm0,
        (uint32)(uintptr_t)HPM_IOC,
        (uint32)IOC_PAD_PD03,
        (uint32)IOC_PD03_FUNC_CTL_PWM0_P_3,
        0u,
        (uint8)pwm_channel_3,
        (uint8)pwm_counter_1,
        6u, 7u,
        PWM_SHADOW_CMP6, PWM_SHADOW_CMP7,
        PWM_LOW,
        PWM_VARIABLE_PERIOD,
        PWM_DUTY_0_PERCENT / 2u,         /* 50% */
        250u,                            /* phase = 25% 周期（相位演示） */
        10000u,
        &Pwm_Notification_1
    },
};

const Pwm_ConfigType Pwm_Config = {
    (uint32)IRQn_PWM0,
    (uint8)PWM_NUM_CHANNELS,
    Pwm_ChannelConfig
};