/*
 * Pwm_PBcfg.h —— PWM post-build 配置声明
 *
 * 板级映射（与 docs/MCAL开发约定.md §2、"回环信号"约定一致）：
 *   PWM_CHANNEL_0 -> PWM0 输出 2（P2），引脚 PD02 = IOC_PD02_FUNC_CTL_PWM0_P_2
 *                    PD02 是 I2C2_SCL，与 I2C3 的 PD07 有板载杜邦线；
 *   PWM_CHANNEL_1 -> PWM0 输出 3（P3），引脚 PD03 = IOC_PD03_FUNC_CTL_PWM0_P_3
 *                    PD03 是 I2C2_SDA，与 I2C3 的 PD06 有板载杜邦线；
 *   于是同一根线对端可以被 ICU（GPTMR2 CAPT）捕获，用于真实占空比测量。
 *   注意：PWM0 输出 0/1 落在 PD00/PD01（CAN0），本配置不用。
 */
#ifndef PWM_PBCFG_H
#define PWM_PBCFG_H

#include "Pwm.h"

#define PWM_CHANNEL_0    ((Pwm_ChannelType)0u)
#define PWM_CHANNEL_1    ((Pwm_ChannelType)1u)
#define PWM_CHANNEL_COUNT (PWM_NUM_CHANNELS)

extern const Pwm_ConfigType Pwm_Config;

/* 通知回调（SWS_Pwm §8.6.3）：弱符号默认空实现，应用可定义强符号覆盖 */
void Pwm_Notification_0(void);
void Pwm_Notification_1(void);

#endif /* PWM_PBCFG_H */
