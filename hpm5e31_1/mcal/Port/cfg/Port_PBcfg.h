/*
 * Port_PBcfg.h —— Port post-build 配置（HPM5E31 / hpm5e31_1 板级映射）
 *
 * 引脚来源：docs/MCAL开发约定.md §2 的硬件资源分配表。
 *   回环线（SPI1 <-> SPI3 杜邦线）：
 *     nCS  PC11(出) -> PA27(入)   MISO PC12(出) -> PA28(入)
 *     MOSI PC13(出) -> PA29(入)   SCLK PC10(出) -> PA26(入)
 *   本轮 Port 例程用 nCS 对 + MISO 对（OD 测试）+ SCLK 对。
 *
 * 注意：PA02/PA03 兼作 BOOT_MODE0/1（复位时按住 PA03 会进 ISP）。
 */
#ifndef PORT_PBCFG_H
#define PORT_PBCFG_H

#include "Port.h"

/* 逻辑引脚 ID（Port_PinType 取值 = 引脚表下标） */
#define PORT_PIN_USR_LED          ((Port_PinType)0u)   /* PA02 输出，高亮 */
#define PORT_PIN_USR_BTN          ((Port_PinType)1u)   /* PA03 输入，按下为高 */
#define PORT_PIN_LOOP_CS_OUT      ((Port_PinType)2u)   /* PC11 输出 -> PA27 */
#define PORT_PIN_LOOP_CS_IN       ((Port_PinType)3u)   /* PA27 输入 */
#define PORT_PIN_CAN_LED0         ((Port_PinType)4u)   /* PF26 */
#define PORT_PIN_CAN_LED1         ((Port_PinType)5u)   /* PF18 */
#define PORT_PIN_CAN_LED2         ((Port_PinType)6u)   /* PF20 */
#define PORT_PIN_CAN_LED3         ((Port_PinType)7u)   /* PF16 */
#define PORT_PIN_CAN_LED4         ((Port_PinType)8u)   /* PF22 */
#define PORT_PIN_CAN_LED5         ((Port_PinType)9u)   /* PF21 */
#define PORT_PIN_CAN_LED6         ((Port_PinType)10u)  /* PF23 */
#define PORT_PIN_CAN_LED7         ((Port_PinType)11u)  /* PF19 */
#define PORT_PIN_LOOP_OD_OUT      ((Port_PinType)12u)  /* PC12 开漏输出 -> PA28 */
#define PORT_PIN_LOOP_OD_IN       ((Port_PinType)13u)  /* PA28 输入，内部上拉 */
#define PORT_PIN_LOOP_SCLK_OUT    ((Port_PinType)14u)  /* PC10 输出 -> PA26 */
#define PORT_PIN_LOOP_SCLK_IN     ((Port_PinType)15u)  /* PA26 输入 */

#endif /* PORT_PBCFG_H */
