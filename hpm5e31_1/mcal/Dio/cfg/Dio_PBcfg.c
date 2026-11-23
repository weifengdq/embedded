/*
 * Dio_PBcfg.c —— Dio post-build 通道表与通道组（HPM5E31 / hpm5e31_1）
 *
 * 通道 -> (GPIO 端口, 位) 映射。驱动里不出现任何引脚名，改板只改本文件。
 * 例程：samples/mcal/dio_led_button
 */
#include "Dio_PBcfg.h"
#include "hpm_soc.h"

#define GPIO_C ((uint32)HPM_GPIO0_BASE)

#define GPA (0u)
#define GPC (2u)
#define GPF (5u)

static const Dio_ChannelConfigType Dio_Channels[DIO_CHANNEL_COUNT_CFG] =
{
    { .gpioPort = GPA, .gpioPin = 2u },   /* 0  PA02 USR LED */
    { .gpioPort = GPA, .gpioPin = 3u },   /* 1  PA03 USR BTN */
    { .gpioPort = GPC, .gpioPin = 11u },  /* 2  PC11 回环输出 */
    { .gpioPort = GPA, .gpioPin = 27u },  /* 3  PA27 回环输入 */
    { .gpioPort = GPF, .gpioPin = 26u },  /* 4  CAN0 red  */
    { .gpioPort = GPF, .gpioPin = 18u },  /* 5  CAN0 white*/
    { .gpioPort = GPF, .gpioPin = 20u },  /* 6  CAN1 red  */
    { .gpioPort = GPF, .gpioPin = 16u },  /* 7  CAN1 white*/
    { .gpioPort = GPF, .gpioPin = 22u },  /* 8  CAN2 red  */
    { .gpioPort = GPF, .gpioPin = 21u },  /* 9  CAN2 white*/
    { .gpioPort = GPF, .gpioPin = 23u },  /* 10 CAN3 red  */
    { .gpioPort = GPF, .gpioPin = 19u },  /* 11 CAN3 white*/
    { .gpioPort = GPC, .gpioPin = 12u },  /* 12 PC12 开漏输出 */
    { .gpioPort = GPA, .gpioPin = 28u },  /* 13 PA28 输入（上拉） */
    { .gpioPort = GPC, .gpioPin = 10u },  /* 14 PC10 回环输出 */
    { .gpioPort = GPA, .gpioPin = 26u },  /* 15 PA26 回环输入 */
};

const Dio_ChannelGroupType Dio_ChannelGroup_CanLeds =
{
    .port = DIO_PORT_GPIOF, .offset = 16u, .mask = 0x000000FFu
};

const Dio_ChannelGroupType Dio_ChannelGroup_Usr =
{
    .port = DIO_PORT_GPIOA, .offset = 2u, .mask = 0x00000003u
};

const Dio_ChannelGroupType Dio_ChannelGroup_CanLedsWide =
{
    .port = DIO_PORT_GPIOF, .offset = 16u, .mask = 0x000007FFu
};

static const Dio_ChannelGroupType Dio_Groups[DIO_CHANNEL_GROUP_COUNT_CFG] =
{
    { .port = DIO_PORT_GPIOF, .offset = 16u, .mask = 0x000000FFu },
    { .port = DIO_PORT_GPIOA, .offset = 2u,  .mask = 0x00000003u },
    { .port = DIO_PORT_GPIOF, .offset = 16u, .mask = 0x000007FFu },
};

const Dio_ConfigType Dio_Config =
{
    .gpioBase = GPIO_C,
    .channels = Dio_Channels,
    .channelCount = (uint16)DIO_CHANNEL_COUNT_CFG,
    .groups = Dio_Groups,
    .groupCount = (uint16)DIO_CHANNEL_GROUP_COUNT_CFG
};
