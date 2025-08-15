/*
 * Copyright (c) 2024 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <stdio.h>

#include "hpm_debug_console.h"
#include "hpm_gpio_drv.h"

#include "board.h"

#include "cdc_acm.h"
#include "mcan.h"
#include "slcan.h"
#include "usb_config.h"

int main(void)
{
    board_init();
    init_pins();

    board_init_usb(HPM_USB0);
    intc_set_irq_priority(IRQn_USB0, 3);
    cdc_acm_init(0, HPM_USB0_BASE);

    // slcan_init();

    while (1) {
        slcan_process_task(&slcan[0]);
        slcan_process_task(&slcan[1]);
        slcan_process_task(&slcan[2]);
        slcan_process_task(&slcan[3]);
        /* process any received CAN frames and forward to host as SLCAN */
        mcan_process_received_frames();
    }
    return 0;
}
