/*
 * Copyright (c) 2024 RCSN
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "mcan.h"
#include "pinmux.h"

mcan_rx_message_t    g_mcan_rx_frame[CANFD_NUM][MCAN_RXBUF_SIZE_CAN_DEFAULT];
mcan_tx_frame_t      g_mcan_tx_frame[CANFD_NUM];
volatile uint8_t     g_mcan_rx_fifo_complete_flag[CANFD_NUM][MCAN_RXBUF_SIZE_CAN_DEFAULT];
struct slcan_t slcan0, slcan1, slcan2, slcan3;

void mcan_isr(struct slcan_t *slcan_port)
{
    MCAN_Type *base = slcan_port->ptr;
    uint32_t flags = mcan_get_interrupt_flags(base);
    printf("MCAN%d ISR, flags: 0x%08lx\n", slcan_port->candev_sn, flags);

    /* New message is available in RXFIFO0 */
    if ((flags & MCAN_INT_RXFIFO0_NEW_MSG) != 0) {
        mcan_read_rxfifo(base, 0, (mcan_rx_message_t *)&g_mcan_rx_frame[slcan_port->candev_sn][0]);
        g_mcan_rx_fifo_complete_flag[slcan_port->candev_sn][0] = true;
    }
    /* New message is available in RXFIFO1 */
    if ((flags & MCAN_INT_RXFIFO1_NEW_MSG) != 0U) {
        mcan_read_rxfifo(base, 1, (mcan_rx_message_t *) &g_mcan_rx_frame[slcan_port->candev_sn][1]);
        g_mcan_rx_fifo_complete_flag[slcan_port->candev_sn][1] = true;
    }
    /* New message is available in RXBUF */
    if ((flags & MCAN_INT_MSG_STORE_TO_RXBUF) != 0U) {
        /* NOTE: Below code is for demonstration purpose, the performance is not optimized
         *       Users should optimize the performance according to real use case.
         */
        for (uint32_t buf_index = 0; buf_index < MCAN_RXBUF_SIZE_CAN_DEFAULT; buf_index++) {
            if (mcan_is_rxbuf_data_available(base, buf_index)) {
                g_mcan_rx_fifo_complete_flag[slcan_port->candev_sn][buf_index] = true;
                mcan_read_rxbuf(base, buf_index, (mcan_rx_message_t *)&g_mcan_rx_frame[slcan_port->candev_sn][buf_index]);
                mcan_clear_rxbuf_data_available_flag(base, buf_index);
            }
        }
    }
    mcan_clear_interrupt_flags(base, flags);
}

void mcan0_isr(void)
{
    mcan_isr(&slcan0);
}
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN0, mcan0_isr);

void mcan1_isr(void)
{
    mcan_isr(&slcan1);
}
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN1, mcan1_isr);

void mcan2_isr(void)
{
    mcan_isr(&slcan2);
}
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN2, mcan2_isr);

void mcan3_isr(void)
{
    mcan_isr(&slcan3);
}
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN3, mcan3_isr);
void mcan_channel_init(uint8_t can_num)
{
    g_mcan_rx_fifo_complete_flag[can_num][0] = false;
    g_mcan_rx_fifo_complete_flag[can_num][1] = false;
}

void mcan_pinmux_init(uint8_t can_num)
{
    if (CANFD_NUM > 4) {
        return;
    }
    switch (can_num) {
    case 0:
        HPM_IOC->PAD[IOC_PAD_PA01].FUNC_CTL = IOC_PA01_FUNC_CTL_MCAN0_RXD;
        HPM_IOC->PAD[IOC_PAD_PA00].FUNC_CTL = IOC_PA00_FUNC_CTL_MCAN0_TXD;
        break;

    case 1:
        HPM_IOC->PAD[IOC_PAD_PA04].FUNC_CTL = IOC_PA04_FUNC_CTL_MCAN1_RXD;
        HPM_IOC->PAD[IOC_PAD_PA05].FUNC_CTL = IOC_PA05_FUNC_CTL_MCAN1_TXD;
        break;

    case 2:
        HPM_IOC->PAD[IOC_PAD_PA09].FUNC_CTL = IOC_PA09_FUNC_CTL_MCAN2_RXD;
        HPM_IOC->PAD[IOC_PAD_PA08].FUNC_CTL = IOC_PA08_FUNC_CTL_MCAN2_TXD;
        break;

    case 3:
        HPM_IOC->PAD[IOC_PAD_PA30].FUNC_CTL = IOC_PA30_FUNC_CTL_MCAN3_RXD;
        HPM_IOC->PAD[IOC_PAD_PA31].FUNC_CTL = IOC_PA31_FUNC_CTL_MCAN3_TXD;
        break;
    }
}
