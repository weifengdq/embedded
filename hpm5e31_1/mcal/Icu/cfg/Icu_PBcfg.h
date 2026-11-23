/*
 * Icu_PBcfg.h —— ICU post-build 配置声明
 *
 * 捕获源（真实引脚 + 板载杜邦线，见 docs/MCAL开发约定.md §2"回环信号"）：
 *   ICU_CHANNEL_0: GPTMR2 CH2 捕获 <- TRGM0_P06 输入 <- PD06（I2C3_SDA）
 *                  对端 PD03 由 PWM0 P3 输出（I2C2_SDA），即 PWM->ICU 真实回环；
 *   ICU_CHANNEL_1: GPTMR2 CH3 捕获 <- TRGM0_P07 输入 <- PD07（I2C3_SCL）
 *                  对端 PD02 由 PWM0 P2 输出（I2C2_SCL）。
 * GPTMR2 的捕获通道 2/3 只能经 TRGM 引入（RM：TRGM_OUTPUT_SRC_GPTMR2_CAPT_2/3）。
 */
#ifndef ICU_PBCFG_H
#define ICU_PBCFG_H

#include "Icu.h"

#define ICU_CHANNEL_0    ((Icu_ChannelType)0u)
#define ICU_CHANNEL_1    ((Icu_ChannelType)1u)
#define ICU_CHANNEL_COUNT (ICU_NUM_CHANNELS)

extern const Icu_ConfigType Icu_Config;

void Icu_SignalNotification_0(void);
void Icu_SignalNotification_1(void);
void Icu_TimestampNotification_0(void);
void Icu_TimestampNotification_1(void);

#endif /* ICU_PBCFG_H */
