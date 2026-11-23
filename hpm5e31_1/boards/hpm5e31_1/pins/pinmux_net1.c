/*
 * pinmux for LAN8651 10BASE-T1S (SPI2 + nRST + nINT), owned by tp-net1.
 *
 * Pin numbers come from boards/hpm5e31_1/board.h (BOARD_LAN8651_*):
 *   SCLK PA20 / MISO PA22 / MOSI PA23 / CS PA21, nRST PA19, nINT PA24.
 *
 * This file is picked up automatically by boards/hpm5e31_1/CMakeLists.txt
 * (file(GLOB pins/pinmux_*.c)), no CMake edit required.
 */
#include "board.h"
#include "hpm_ioc_regs.h"
#include "hpm_gpio_drv.h"
#include "hpm_clock_drv.h"

/* LAN8651 is a 3.3 V device; PA19/PA24 are plain GPIOs */
void init_lan8651_pins(void)
{
    /* SPI2 pins (same settings as the HPM pinmux tool generated code) */
    /* HPM SPI master needs its own SCLK looped back through the pad input: the
     * SDK generated pinmux always adds IOC_PAD_FUNC_CTL_LOOP_BACK_MASK on the
     * SCLK pad (see the init_spiX_pins() functions in the SDK board pinmux).
     * Without it the controller shifts out nothing usable (all-zero echo). */
    HPM_IOC->PAD[IOC_PAD_PA20].FUNC_CTL = IOC_PA20_FUNC_CTL_SPI2_SCLK | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
    HPM_IOC->PAD[IOC_PAD_PA22].FUNC_CTL = IOC_PA22_FUNC_CTL_SPI2_MISO;
    HPM_IOC->PAD[IOC_PAD_PA22].PAD_CTL  = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    HPM_IOC->PAD[IOC_PAD_PA23].FUNC_CTL = IOC_PA23_FUNC_CTL_SPI2_MOSI;
    /* CS_N is driven as a plain GPIO (reference design: one CS assertion per
     * TC6 frame, with 1 us guard delays).  The SPI2 hardware CS output is not
     * used, and SPI_CTRL.CS_EN is explicitly cleared by the driver. */
    HPM_IOC->PAD[IOC_PAD_PA21].FUNC_CTL = IOC_PA21_FUNC_CTL_GPIO_A_21;
    gpio_set_pin_output_with_initial(BOARD_LAN8651_CS_GPIO_CTRL,
                                     BOARD_LAN8651_CS_GPIO_INDEX,
                                     BOARD_LAN8651_CS_GPIO_PIN, 1);

    /* nRST: output, idle high (de-asserted) */
    HPM_IOC->PAD[IOC_PAD_PA19].FUNC_CTL = IOC_PA19_FUNC_CTL_GPIO_A_19;
    gpio_set_pin_output_with_initial(BOARD_LAN8651_RST_GPIO_CTRL,
                                     BOARD_LAN8651_RST_GPIO_INDEX,
                                     BOARD_LAN8651_RST_GPIO_PIN, 1);

    /* nINT: input with pull-up (open-drain / active-low interrupt of LAN8651) */
    HPM_IOC->PAD[IOC_PAD_PA24].FUNC_CTL = IOC_PA24_FUNC_CTL_GPIO_A_24;
    HPM_IOC->PAD[IOC_PAD_PA24].PAD_CTL  = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    gpio_set_pin_input(BOARD_LAN8651_INT_GPIO_CTRL,
                       BOARD_LAN8651_INT_GPIO_INDEX,
                       BOARD_LAN8651_INT_GPIO_PIN);

    /* SPI2 peripheral clock: board_init_spi_clock() enables the gate
     * (boards/hpm5e31_1/clock.c: init_spi2_clock) and returns the source
     * frequency.  It has to be called before the first SPI2 register access,
     * otherwise every write is silently dropped. */
    (void) board_init_spi_clock(BOARD_LAN8651_SPI);
}

/* Release the bus so another user (e.g. an SPI2 loopback sample) can take it. */
void init_lan8651_pins_release(void)
{
    gpio_set_pin_output_with_initial(BOARD_LAN8651_RST_GPIO_CTRL,
                                     BOARD_LAN8651_RST_GPIO_INDEX,
                                     BOARD_LAN8651_RST_GPIO_PIN, 0); /* keep chip in reset */
    HPM_IOC->PAD[IOC_PAD_PA20].FUNC_CTL = IOC_PA20_FUNC_CTL_GPIO_A_20;
    HPM_IOC->PAD[IOC_PAD_PA21].FUNC_CTL = IOC_PA21_FUNC_CTL_GPIO_A_21;
    HPM_IOC->PAD[IOC_PAD_PA22].FUNC_CTL = IOC_PA22_FUNC_CTL_GPIO_A_22;
    HPM_IOC->PAD[IOC_PAD_PA23].FUNC_CTL = IOC_PA23_FUNC_CTL_GPIO_A_23;
}
