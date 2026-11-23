/*
 * Port_PBcfg.c —— Port post-build 引脚表（HPM5E31 / hpm5e31_1）
 *
 * 一张表就是全部板级知识：IOC PAD 索引 / GPIO 端口 / 方向 / 上下拉 / 开漏 / 初值。
 * padIndex 规律：PA00=0, PB00=32, PC00=64, PD00=96, PE00=128, PF00=160，再加引脚号。
 * 例程：samples/mcal/port_pin_config、samples/mcal/dio_led_button
 */
#include "Port_PBcfg.h"
#include "hpm_soc.h"

#define IO_C   ((uint32)HPM_IOC_BASE)
#define GPIO_C ((uint32)HPM_GPIO0_BASE)

/* GPIO 端口索引（GPIO0 的端口 A..F = 0..5）；DIO/Port 共用同一编号 */
#define GPA (0u)
#define GPC (2u)
#define GPF (5u)

const Port_PinConfigType Port_PinTable[PORT_PIN_COUNT_CFG] =
{
    /* PA02 USR LED：输出，高电平点亮；无上下拉 */
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PA02, .gpioPort = GPA, .gpioPin = 2u,
      .direction = (uint8)PORT_PIN_OUT, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_NONE,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 1u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    /* PA03 USR BTN：输入，按下为高；板上有下拉需求，这里开内部下拉 */
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PA03, .gpioPort = GPA, .gpioPin = 3u,
      .direction = (uint8)PORT_PIN_IN, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_DOWN,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 0u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    /* PC11 回环输出（SPI1 nCS 端） */
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PC11, .gpioPort = GPC, .gpioPin = 11u,
      .direction = (uint8)PORT_PIN_OUT, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_NONE,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 1u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    /* PA27 回环输入（SPI3 nCS 端） */
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PA27, .gpioPort = GPA, .gpioPin = 27u,
      .direction = (uint8)PORT_PIN_IN, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_NONE,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 1u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    /* CAN 状态灯 8 颗（高电平亮）：PF26/PF18, PF20/PF16, PF22/PF21, PF23/PF19 */
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PF26, .gpioPort = GPF, .gpioPin = 26u,
      .direction = (uint8)PORT_PIN_OUT, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_NONE,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 0u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PF18, .gpioPort = GPF, .gpioPin = 18u,
      .direction = (uint8)PORT_PIN_OUT, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_NONE,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 0u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PF20, .gpioPort = GPF, .gpioPin = 20u,
      .direction = (uint8)PORT_PIN_OUT, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_NONE,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 0u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PF16, .gpioPort = GPF, .gpioPin = 16u,
      .direction = (uint8)PORT_PIN_OUT, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_NONE,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 0u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PF22, .gpioPort = GPF, .gpioPin = 22u,
      .direction = (uint8)PORT_PIN_OUT, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_NONE,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 0u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PF21, .gpioPort = GPF, .gpioPin = 21u,
      .direction = (uint8)PORT_PIN_OUT, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_NONE,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 0u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PF23, .gpioPort = GPF, .gpioPin = 23u,
      .direction = (uint8)PORT_PIN_OUT, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_NONE,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 0u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PF19, .gpioPort = GPF, .gpioPin = 19u,
      .direction = (uint8)PORT_PIN_OUT, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_NONE,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 0u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    /* PC12 开漏输出（MISO 对）-> PA28 输入内部上拉 */
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PC12, .gpioPort = GPC, .gpioPin = 12u,
      .direction = (uint8)PORT_PIN_OUT, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_NONE,
      .openDrain = 1u, .initialLevel = 1u, .directionChangeable = 1u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PA28, .gpioPort = GPA, .gpioPin = 28u,
      .direction = (uint8)PORT_PIN_IN, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_UP,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 1u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    /* PC10 回环输出（SCLK 对）-> PA26 输入 */
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PC10, .gpioPort = GPC, .gpioPin = 10u,
      .direction = (uint8)PORT_PIN_OUT, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_NONE,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 1u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
    { .iocBase = IO_C, .gpioBase = GPIO_C, .padIndex = (uint16)IOC_PAD_PA26, .gpioPort = GPA, .gpioPin = 26u,
      .direction = (uint8)PORT_PIN_IN, .mode = (uint8)PORT_PIN_MODE_GPIO, .pull = PORT_PIN_PULL_NONE,
      .openDrain = 0u, .initialLevel = 0u, .directionChangeable = 1u, .modeChangeable = 1u,
      .driveStrength = 5u, .slewRate = 1u, .speed = 3u, .hysteresis = 1u },
};

const Port_ConfigType Port_Config =
{
    .pins = Port_PinTable,
    .pinCount = (uint16)PORT_PIN_COUNT_CFG
};
