/*
 * Pwm_Cfg.h —— PWM 预编译配置（SWS_Pwm §10.2）
 */
#ifndef PWM_CFG_H
#define PWM_CFG_H

#include "Mcal_Cfg.h"

/* 逻辑通道数：与 Pwm_PBcfg.c 的 Pwm_ChannelConfig[] 长度一致 */
#define PWM_NUM_CHANNELS        (2u)

#ifndef PWM_DEV_ERROR_DETECT
#define PWM_DEV_ERROR_DETECT    MCAL_DEV_ERROR_DETECT
#endif

#define PWM_DEINIT_API          STD_ON
#define PWM_VERSION_INFO_API    MCAL_VERSION_INFO_API
#define PWM_NOTIFICATION_SUPPORTED STD_ON
#define PWM_POWER_STATE_API     STD_OFF  /* 本子集不做低功耗状态机（无 EcuM） */

#define PWM_IRQ_PRIORITY        (2u)

/* PWMv2 计数器是 24 位整数部分（CNT_VAL 为 24.8 定点），周期上限 */
#define PWM_MAX_PERIOD_TICKS    (0x00FFFFFFuL)

#endif /* PWM_CFG_H */
