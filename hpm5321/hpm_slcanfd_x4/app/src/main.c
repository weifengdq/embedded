/*
 * Copyright (c) 2024 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

// #include <hpm_dma_mgr.h>
#include <stdio.h>

#include "hpm_debug_console.h"

#include "board.h"

#include "bsp_gpio.h"
#include "cdc_acm.h"
#include "mcan.h"
#include "slcan.h"
#include "usb_config.h"

int main(void)
{
    board_init();
    init_pins();
    bsp_led_init();
    printf("1\r\n");

    // dma_mgr_init();
    board_init_usb(HPM_USB0);
    printf("2\r\n");
    intc_set_irq_priority(CONFIG_HPM_USBD_IRQn, 3);
    printf("3\r\n");
    cdc_acm_init(USB_BUS_ID, CONFIG_HPM_USBD_BASE);
    printf("4\r\n");
    slcan_init();

    printf("SLCAN application started.\r\n");

    while (1) {
        // static uint32_t main_loop_counter = 0;
        // if (main_loop_counter++ % 50000 == 0) {  // 每50000次循环打印一次
        //     printf("Main loop running, counter: %u\r\n", main_loop_counter);
        // }

        slcan_process_task(&slcan0);
        slcan_process_task(&slcan1);
        slcan_process_task(&slcan2);
        slcan_process_task(&slcan3);
    }
    return 0;
}
