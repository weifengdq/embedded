/* board-level resource macros owned by tp-can (see docs/00-目录.md) */
#ifndef BOARD_EXT_CAN_H
#define BOARD_EXT_CAN_H

#include "board.h"
#include "pinmux.h"
#include "hpm_gpio_drv.h"

/*
 * hpm5e31_1 CAN 资源（4x MCAN + TCAN1044AV）
 *
 * 控制器         TXD/RXD         总线端子            终端电阻
 *   MCAN0        PD00 / PD01     CAN0_H / CAN0_L     R120 <-> CAN1（同一总线两端）
 *   MCAN1        PD05 / PD04     CAN1_H / CAN1_L     R120
 *   MCAN2        PC08 / PA09     CAN2_H / CAN2_L     R120 <-> CAN3（同一总线两端）
 *   MCAN3        PC15 / PC14     CAN3_H / CAN3_L     R120
 *
 * 收发器 TCAN1044AV（SOIC-8）: 1=TXD 2=GND 3=VCC 4=RXD 5=STB 6=CANL 7=CANH 8=VCC
 * 板上 STB 统一由 PE03 控制：低电平 = 正常工作，高电平 = standby。
 * 8 颗 CAN 状态灯（高电平点亮）：每个通道 红=TX/ERR, 白=RX。
 */
#define BOARD_CAN_COUNT (4U)

#define BOARD_CAN0_BASE HPM_MCAN0
#define BOARD_CAN1_BASE HPM_MCAN1
#define BOARD_CAN2_BASE HPM_MCAN2
#define BOARD_CAN3_BASE HPM_MCAN3

#define BOARD_CAN0_IRQ IRQn_MCAN0
#define BOARD_CAN1_IRQ IRQn_MCAN1
#define BOARD_CAN2_IRQ IRQn_MCAN2
#define BOARD_CAN3_IRQ IRQn_MCAN3

/* TCAN1044AV standby（板上 4 颗共用 PE03；0=normal, 1=standby） */
#define BOARD_CAN_STB_CTRL HPM_GPIO0
#define BOARD_CAN_STB_PORT GPIO_DI_GPIOE
#define BOARD_CAN_STB_PIN  (3U)
#define BOARD_CAN_STB_LEVEL_NORMAL (0U)

/* CAN 状态灯：{ctrl, port, pin}，active high */
#define BOARD_CAN_LED_PORT GPIO_DI_GPIOF

#define BOARD_CAN0_LED_TX_PIN (26U) /* PF26 红 */
#define BOARD_CAN0_LED_RX_PIN (18U) /* PF18 白 */
#define BOARD_CAN1_LED_TX_PIN (20U) /* PF20 红 */
#define BOARD_CAN1_LED_RX_PIN (16U) /* PF16 白 */
#define BOARD_CAN2_LED_TX_PIN (22U) /* PF22 红 */
#define BOARD_CAN2_LED_RX_PIN (21U) /* PF21 白 */
#define BOARD_CAN3_LED_TX_PIN (23U) /* PF23 红 */
#define BOARD_CAN3_LED_RX_PIN (19U) /* PF19 白 */

static inline void board_can_init_pins(void)
{
    init_mcan0_pins();
    init_mcan1_pins();
    init_mcan2_pins();
    init_mcan3_pins();
}

/* 使能 4 颗 TCAN1044AV（STB=0），上电后必须调用一次才能上真实总线 */
static inline void board_can_transceivers_enable(void)
{
    init_mcan1_transceiver_phy_pin(); /* 把 PE03 切成 GPIO 功能 */
    gpio_set_pin_output_with_initial(BOARD_CAN_STB_CTRL, BOARD_CAN_STB_PORT,
                                     BOARD_CAN_STB_PIN, BOARD_CAN_STB_LEVEL_NORMAL);
    gpio_write_pin(BOARD_CAN_STB_CTRL, BOARD_CAN_STB_PORT, BOARD_CAN_STB_PIN,
                   BOARD_CAN_STB_LEVEL_NORMAL);
}

static inline void board_can_leds_init(void)
{
    init_canled_pins();
    for (uint8_t i = 0; i < BOARD_CAN_COUNT; i++) {
        static const uint8_t tx_pin[BOARD_CAN_COUNT] = {BOARD_CAN0_LED_TX_PIN, BOARD_CAN1_LED_TX_PIN,
                                                        BOARD_CAN2_LED_TX_PIN, BOARD_CAN3_LED_TX_PIN};
        static const uint8_t rx_pin[BOARD_CAN_COUNT] = {BOARD_CAN0_LED_RX_PIN, BOARD_CAN1_LED_RX_PIN,
                                                        BOARD_CAN2_LED_RX_PIN, BOARD_CAN3_LED_RX_PIN};
        gpio_set_pin_output_with_initial(HPM_GPIO0, BOARD_CAN_LED_PORT, tx_pin[i], 0);
        gpio_set_pin_output_with_initial(HPM_GPIO0, BOARD_CAN_LED_PORT, rx_pin[i], 0);
    }
}

static inline void board_can_led_write(uint8_t ch, bool tx_led, bool on)
{
    static const uint8_t tx_pin[BOARD_CAN_COUNT] = {BOARD_CAN0_LED_TX_PIN, BOARD_CAN1_LED_TX_PIN,
                                                    BOARD_CAN2_LED_TX_PIN, BOARD_CAN3_LED_TX_PIN};
    static const uint8_t rx_pin[BOARD_CAN_COUNT] = {BOARD_CAN0_LED_RX_PIN, BOARD_CAN1_LED_RX_PIN,
                                                    BOARD_CAN2_LED_RX_PIN, BOARD_CAN3_LED_RX_PIN};
    if (ch >= BOARD_CAN_COUNT) {
        return;
    }
    gpio_write_pin(HPM_GPIO0, BOARD_CAN_LED_PORT, tx_led ? tx_pin[ch] : rx_pin[ch], on ? 1U : 0U);
}

#endif /* BOARD_EXT_CAN_H */
