/*
 * board-level resources owned by tp-net1 (LAN8651 10BASE-T1S / 10-11 chapter)
 *
 * Included by boards/hpm5e31_1/board.h at the very end, so every sample sees it.
 * Pin map (hpm5e31_1 v1):
 *   SPI2 SCLK PA20, MISO PA22, MOSI PA23, CS PA21
 *   LAN8651 nRST PA19 (output, active low), nINT PA24 (input, active low)
 */
#ifndef BOARD_EXT_NET1_H
#define BOARD_EXT_NET1_H

/* LAN8651 SPI clock source (SoC clock name, not in board clock.c) */
#define BOARD_LAN8651_SPI_CLK_SRC   clock_spi2

/* chip select as plain GPIO (fallback if the SPI2 hardware CS is not driven) */
#define BOARD_LAN8651_CS_GPIO_CTRL  HPM_GPIO0
#define BOARD_LAN8651_CS_GPIO_INDEX GPIO_DO_GPIOA
#define BOARD_LAN8651_CS_GPIO_PIN   (21U)

/* MAC address used by this board (locally administered, unicast) */
#define BOARD_LAN8651_MAC0          0x02U
#define BOARD_LAN8651_MAC1          0x00U
#define BOARD_LAN8651_MAC2          0x00U
#define BOARD_LAN8651_MAC3          0x5EU
#define BOARD_LAN8651_MAC4          0x31U
#define BOARD_LAN8651_MAC5          0x01U

/* Default network parameters for host <-> board 10BASE-T1S link */
#define BOARD_NET1_IP               "192.168.1.100"
#define BOARD_NET1_NETMASK          "255.255.255.0"
#define BOARD_NET1_HOST_IP          "192.168.1.1"

/* Default PLCA parameters (validated on RK3506 + same USB adapter) */
#define BOARD_PLCA_NODE_CNT_DEFAULT 8U
#define BOARD_PLCA_TO_TMR_DEFAULT   32U
#define BOARD_PLCA_BURST_CNT_DEFAULT 0U
#define BOARD_PLCA_BURST_TMR_DEFAULT 128U

/* pinmux_net1.c */
void init_lan8651_pins(void);        /* SPI2 + nRST/nINT + clock enable */
void init_lan8651_pins_release(void);/* release SPI2 pins back to GPIO input */

#endif /* BOARD_EXT_NET1_H */
