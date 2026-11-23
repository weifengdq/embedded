/*
 * Adc_PBcfg.h —— ADC post-build 配置（HPM5E31 / hpm5e31_1 板级映射）
 *
 * 硬件：ADC0（ADC16），通道 1 = PF26（ADC0_IN01），16 bit 单端；
 *       PF26 同时是 CAN0 红色状态灯 —— 本模块独占 PF26（Dio/CAN 例程运行时不要同时驱动该灯）。
 * 时钟：ADC0 总线时钟 = ahb0 = 200 MHz，分频 4 -> 转换时钟 50 MHz（ADC16 上限 50 MHz）。
 */
#ifndef ADC_PBCFG_H
#define ADC_PBCFG_H

#include "Adc.h"

/* 组 ID */
#define ADC_GROUP_SINGLE      ((Adc_GroupType)0u)   /* 单次（one-shot）单通道 */
#define ADC_GROUP_CONTINUOUS  ((Adc_GroupType)1u)   /* 连续（continuous）单通道 */

/* ADC0 通道（PF26 = ADC0.IN01） */
#define ADC_PBCFG_CH_PF26     ((Adc_ChannelType)1u)

/* 每通道流式采样数（单次访问模式 = 1） */
#define ADC_PBCFG_STREAM_SAMPLES (1u)

#endif /* ADC_PBCFG_H */
