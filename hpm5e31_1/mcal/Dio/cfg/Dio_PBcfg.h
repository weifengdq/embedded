/*
 * Dio_PBcfg.h —— Dio post-build 通道/通道组映射（HPM5E31 / hpm5e31_1 板）
 *
 * 通道号与 Port 例程的引脚逻辑号一一对应（同一块板的同一张表）：
 *   0 PA02 LED / 1 PA03 BTN / 2 PC11 / 3 PA27 / 4..11 CAN 状态灯 PF26/PF18/PF20/PF16/PF22/PF21/PF23/PF19
 *   12 PC12 开漏 / 13 PA28 / 14 PC10 / 15 PA26
 * 注意：PF26 同时是 ADC0_IN01，跑 Adc 例程时不要同时驱动这颗灯。
 */
#ifndef DIO_PBCFG_H
#define DIO_PBCFG_H

#include "Dio.h"

/* 逻辑通道号（Dio_ChannelType 取值 = 通道表下标） */
#define DIO_CHANNEL_USR_LED     ((Dio_ChannelType)0u)
#define DIO_CHANNEL_USR_BTN     ((Dio_ChannelType)1u)
#define DIO_CHANNEL_LOOP_OUT    ((Dio_ChannelType)2u)
#define DIO_CHANNEL_LOOP_IN     ((Dio_ChannelType)3u)
#define DIO_CHANNEL_CAN_LED0    ((Dio_ChannelType)4u)
#define DIO_CHANNEL_CAN_LED1    ((Dio_ChannelType)5u)
#define DIO_CHANNEL_CAN_LED2    ((Dio_ChannelType)6u)
#define DIO_CHANNEL_CAN_LED3    ((Dio_ChannelType)7u)
#define DIO_CHANNEL_CAN_LED4    ((Dio_ChannelType)8u)
#define DIO_CHANNEL_CAN_LED5    ((Dio_ChannelType)9u)
#define DIO_CHANNEL_CAN_LED6    ((Dio_ChannelType)10u)
#define DIO_CHANNEL_CAN_LED7    ((Dio_ChannelType)11u)
#define DIO_CHANNEL_LOOP_OD_OUT ((Dio_ChannelType)12u)
#define DIO_CHANNEL_LOOP_OD_IN  ((Dio_ChannelType)13u)
#define DIO_CHANNEL_LOOP_SCLK_OUT ((Dio_ChannelType)14u)
#define DIO_CHANNEL_LOOP_SCLK_IN  ((Dio_ChannelType)15u)

/* GPIO 端口索引 */
#define DIO_PORT_GPIOA          ((Dio_PortType)0u)
#define DIO_PORT_GPIOC          ((Dio_PortType)2u)
#define DIO_PORT_GPIOF          ((Dio_PortType)5u)

/* 通道组：CAN 状态灯所在的 GPIOF[23:16]（含 8 颗灯中的 6 颗 + 2 个未用位） */
extern const Dio_ChannelGroupType Dio_ChannelGroup_CanLeds;
/* 通道组：GPIOA[3:2]（bit2 = USR LED，bit3 = USR BTN），只读用 */
extern const Dio_ChannelGroupType Dio_ChannelGroup_Usr;
/* 通道组：GPIOF[26:16]，跨 8 颗灯的完整范围（mask 0x7FF） */
extern const Dio_ChannelGroupType Dio_ChannelGroup_CanLedsWide;

#endif /* DIO_PBCFG_H */
