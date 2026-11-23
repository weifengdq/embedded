/*
 * Gpt_PBcfg.h —— GPT post-build 配置声明（SWS_Gpt §10.2）
 *
 * 通道号由这里固定给上层：GPT_CHANNEL_0 / GPT_CHANNEL_1。
 * 板级映射（实例/IRQ/时钟/回调）在 Gpt_PBcfg.c，与
 * docs/MCAL开发约定.md §2 的资源划分一致：
 *   GPT_CHANNEL_0 -> GPTMR0 CH0（1 ms 周期 tick / notification）
 *   GPT_CHANNEL_1 -> GPTMR1 CH0（oneshot / 时间戳）
 *   GPTMR0 CH3    -> Predef Timer 自由运行时基（1 tick = 10 ns @100 MHz，软件换算 1us）
 *   GPTMR3 归 BSP board_delay_us，本驱动不碰。
 */
#ifndef GPT_PBCFG_H
#define GPT_PBCFG_H

#include "Gpt.h"

#define GPT_CHANNEL_0    ((Gpt_ChannelType)0u)
#define GPT_CHANNEL_1    ((Gpt_ChannelType)1u)
#define GPT_CHANNEL_COUNT (GPT_NUM_CHANNELS)

/* post-build 配置实例（const，位于 Flash） */
extern const Gpt_ConfigType Gpt_Config;

/* AUTOSAR 可配置回调（SWS_Gpt §8.6.3.1）：弱符号默认空实现，
 * 应用可在自己的 .c 里定义同名强符号覆盖。 */
void Gpt_Notification_0(void);
void Gpt_Notification_1(void);

#endif /* GPT_PBCFG_H */
