/*
 * Copyright (c) 2024 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hpm_clock_drv.h"
#include "hpm_debug_console.h"
#include "hpm_gpio_drv.h"
#include "hpm_mcan_drv.h"

#include "board.h"

/* 定义TDC功能开启 */
// #define CANFD_ENABLE_TRANSMITTER_DELAY_COMPENSATION

/* 全局变量 */
static volatile bool has_new_rcv_msg = false;
static volatile bool has_sent_out = false;
static volatile bool has_error = false;
static volatile mcan_rx_message_t s_can_rx_buf;

/* MCAN0 定义 */
#define BOARD_APP_CAN_BASE HPM_MCAN0
#define BOARD_APP_CAN_IRQn IRQn_MCAN0

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
#if defined(HPM_MCAN0)
ATTR_PLACE_AT(".ahb_sram") uint32_t mcan0_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif
#endif

/* 中断服务程序 */
SDK_DECLARE_EXT_ISR_M(BOARD_APP_CAN_IRQn, board_can_isr)
void board_can_isr(void)
{
    uint32_t flags = mcan_get_interrupt_flags(BOARD_APP_CAN_BASE);

    if ((flags & MCAN_INT_RXFIFO0_NEW_MSG) != 0U) {
        has_new_rcv_msg = true;
        hpm_stat_t status =
            mcan_read_rxfifo(BOARD_APP_CAN_BASE, 0, (mcan_rx_message_t *) &s_can_rx_buf);
        if (status != status_success) {
            printf("Read RX FIFO failed\n");
        }
        // mcan_clear_interrupt_flags(BOARD_APP_CAN_BASE, MCAN_INT_RXFIFO0_NEW_MSG);
    }

    if ((flags & MCAN_INT_TX_EVT_FIFO_NEW_ENTRY) != 0U) {
        has_sent_out = true;
        // mcan_clear_interrupt_flags(BOARD_APP_CAN_BASE, MCAN_INT_TX_EVT_FIFO_NEW_ENTRY);
    }

    /* 简化错误处理, 此处只打印出来 */
    if ((flags & MCAN_INT_ERROR_PASSIVE) != 0U) {
        has_error = true;
        printf("CAN Error detected, flags: 0x%08lx\n", flags);
        // mcan_clear_interrupt_flags(BOARD_APP_CAN_BASE, flags);
    }
    mcan_clear_interrupt_flags(BOARD_APP_CAN_BASE, flags);
}

/* 显示接收到的消息 */
void show_received_can_message(volatile const mcan_rx_message_t *rx_msg)
{
    printf("Received message: ");
    if (rx_msg->use_ext_id) {
        printf("ExtID=0x%08lx ", rx_msg->ext_id);
    } else {
        printf("StdID=0x%03x ", rx_msg->std_id);
    }

    printf("DLC=%d ", rx_msg->dlc);

    if (rx_msg->canfd_frame) {
        printf("CANFD ");
        if (rx_msg->bitrate_switch) {
            printf("BRS ");
        }
    } else {
        printf("CAN2.0 ");
    }

    printf("Data: ");
    uint32_t msg_len = mcan_get_message_size_from_dlc(rx_msg->dlc);
    for (uint32_t i = 0; i < msg_len; i++) {
        printf("%02x ", rx_msg->data_8[i]);
    }
    printf("\n");
}

/* 简化的CAN初始化函数 */
void init_can_pins_and_clock(void)
{
    /* CAN引脚已在pinmux.c中配置 */

    /* 启用 CAN0 时钟 */
    clock_add_to_group(clock_can0, 0);
    clock_set_source_divider(clock_can0, clk_src_pll1_clk0, 10);
}

/* 初始化 MCAN0 配置 */
hpm_stat_t init_mcan0_config(void)
{
    MCAN_Type *mcan_base = BOARD_APP_CAN_BASE;
    mcan_config_t can_config;
    hpm_stat_t status;

    /* 初始化 CAN 引脚和时钟 */
    init_can_pins_and_clock();

    /* RAM */
    mcan_msg_buf_attr_t attr = {.ram_base = (uint32_t) mcan0_msg_buf,
                                .ram_size = sizeof(mcan0_msg_buf)};
    status = mcan_set_msg_buf_attr(mcan_base, &attr);
    if (status != status_success) {
        printf("Error was detected during setting message buffer attribute, please check the "
               "arguments\n");
        return status;
    }

    /* 获取默认配置 */
    mcan_get_default_config(mcan_base, &can_config);

    /* 配置为正常模式 */
    can_config.mode = mcan_mode_normal;

    /* 启用 CANFD */
    can_config.enable_canfd = true;

    /* 使用低级时序设置 */
    // 自己设置分频 seg sjw
    can_config.use_lowlevel_timing_setting = true;

    /* CAN 时钟为 80MHz */
    uint32_t can_src_clk_freq = clock_get_frequency(clock_can0);

    /* 仲裁阶段配置：1Mbps */
    /* 波特率 = 80MHz / 8 / (1 + 7 + 2) = 1Mbps */
    can_config.can_timing.prescaler = 8;
    can_config.can_timing.num_seg1 = 7; /* 采样点 80% */
    can_config.can_timing.num_seg2 = 2;
    can_config.can_timing.num_sjw = 2;

    /* 数据阶段配置：5Mbps */
    /* 波特率 = 80MHz / 2 / (1 + 5 + 2) = 5Mbps */
    can_config.canfd_timing.prescaler = 2;
    can_config.canfd_timing.num_seg1 = 5; /* 采样点 75% */
    can_config.canfd_timing.num_seg2 = 2;
    can_config.canfd_timing.num_sjw = 2;

    /* 启用 TDC (Transmitter Delay Compensation) */
#ifdef CANFD_ENABLE_TRANSMITTER_DELAY_COMPENSATION
    can_config.canfd_timing.enable_tdc = true;
    can_config.enable_tdc = true;
    can_config.tdc_config.ssp_offset = can_config.canfd_timing.num_seg1 + 1;
    can_config.tdc_config.filter_window_length = can_config.tdc_config.ssp_offset;
#else
    can_config.canfd_timing.enable_tdc = false;
    can_config.enable_tdc = false;
#endif

    /* 配置滤波器为掩码方式 0:0 (接收所有消息) */
    /* 使用默认的全局滤波器配置，不设置特定滤波器 */
    // TODO: Fix
    can_config.all_filters_config.std_id_filter_list.mcan_filter_elem_count = 0;
    can_config.all_filters_config.ext_id_filter_list.mcan_filter_elem_count = 0;

    /* 配置中断 */
    can_config.interrupt_mask = MCAN_EVENT_RECEIVE | MCAN_EVENT_TRANSMIT | MCAN_EVENT_ERROR;
    can_config.txbuf_trans_interrupt_mask = ~0UL;

    /* 获取默认 RAM 配置 */
    mcan_get_default_ram_config(mcan_base, &can_config.ram_config, true);

    /* 初始化 MCAN */
    status = mcan_init(mcan_base, &can_config, can_src_clk_freq);
    if (status != status_success) {
        printf("MCAN initialization failed, error code: %d\n", status);
        return status;
    }

    /* 启用中断 */
    mcan_enable_interrupts(mcan_base, MCAN_EVENT_RECEIVE | MCAN_EVENT_TRANSMIT | MCAN_EVENT_ERROR);
    intc_m_enable_irq_with_priority(BOARD_APP_CAN_IRQn, 1);

    printf("MCAN0 initialized successfully\n");
    printf("  Arbitration phase: 1Mbps\n");
    printf("  Data phase: 5Mbps\n");
    printf("  TDC: %s\n", can_config.enable_tdc ? "Enabled" : "Disabled");
    printf("  Filter: Accept all messages (0:0 mask)\n");

    return status_success;
}

void handle_can_error(MCAN_Type *ptr)
{
    mcan_protocol_status_t protocol_status;
    mcan_error_count_t error_count;
    mcan_parse_protocol_status(ptr->PSR, &protocol_status);
    mcan_get_error_counter(ptr, &error_count);
    const char *error_msg = NULL;
    switch (protocol_status.last_error_code) {
    case mcan_last_error_code_no_error:
        error_msg = "No Error";
        break;
    case mcan_last_error_code_stuff_error:
        error_msg = "Stuff Error";
        break;
    case mcan_last_error_code_format_error:
        error_msg = "Format Error";
        break;
    case mcan_last_error_code_ack_error:
        error_msg = "Acknowledge Error";
        break;
    case mcan_last_error_code_bit1_error:
        error_msg = "Bit1 Error: Sent 1 but monitored as 0";
        break;
    case mcan_last_error_code_bit0_error:
        error_msg = "Bit0 Error: Sent 0 but monitored as 1";
        break;
    case mcan_last_error_code_crc_error:
        error_msg = "CRC Error";
        break;
    case mcan_last_error_code_no_change:
        error_msg = "Last Error was not changed";
        break;
    default:
        /* Suppress compiling warning */
        break;
    }
    printf("Last Error: %s\n", error_msg);
    printf("Error Count:\n");
    printf("Transmit Errors: %d\n", error_count.transmit_error_count);
    printf("Receive Errors: %d\n", error_count.receive_error_count);
    if (protocol_status.in_bus_off_state) {
        printf("CAN is in Bus-off mode\n");
    } else if (protocol_status.in_error_passive_state) {
        printf("CAN is in Error Passive Mode\n");
    } else if (protocol_status.in_warning_state) {
        printf("CAN is in Error Warning mode\n");
    } else {
        /* Suppress warnings*/
    }
}

/* ECHO 测试功能 */
void can_echo_test(void)
{
    printf("\n=== CAN ECHO Test Start ===\n");

    mcan_tx_frame_t tx_buf;
    uint32_t test_count = 0;

    while (1) {
        /* 测试 1: CAN2.0 标准帧数据帧 */
        memset(&tx_buf, 0, sizeof(tx_buf));
        tx_buf.std_id = 0x123;
        tx_buf.dlc = 8;
        tx_buf.canfd_frame = 0;
        tx_buf.bitrate_switch = 0;
        tx_buf.use_ext_id = 0;
        for (uint32_t i = 0; i < 8; i++) {
            tx_buf.data_8[i] = (uint8_t) (i + test_count);
        }

        printf("\nTest %lu: Sending CAN2.0 Standard Data Frame (ID=0x%03x)\n",
               test_count + 1,
               tx_buf.std_id);
        hpm_stat_t status = mcan_transmit_blocking(BOARD_APP_CAN_BASE, &tx_buf);
        if (status != status_success) {
            printf("Failed to send CAN message, error: %d\n", status);
        } else {
            printf("CAN message sent successfully\n");
        }

        /* 等待并显示接收到的消息 */
        uint32_t timeout = 0;
        while (!has_new_rcv_msg && timeout < 1000) {
            board_delay_ms(1);
            timeout++;
        }

        if (has_new_rcv_msg) {
            has_new_rcv_msg = false;
            show_received_can_message(&s_can_rx_buf);
        } else {
            printf("No echo received (timeout)\n");
        }

        board_delay_ms(1000);
        test_count++;

        /* 测试 2: CAN2.0 扩展帧数据帧 */
        if (test_count % 4 == 1) {
            memset(&tx_buf, 0, sizeof(tx_buf));
            tx_buf.ext_id = 0x12345678;
            tx_buf.dlc = 8;
            tx_buf.canfd_frame = 0;
            tx_buf.bitrate_switch = 0;
            tx_buf.use_ext_id = 1;
            for (uint32_t i = 0; i < 8; i++) {
                tx_buf.data_8[i] = (uint8_t) (0xA0 + i);
            }

            printf("\nSending CAN2.0 Extended Data Frame (ID=0x%08lx)\n", tx_buf.ext_id);
            status = mcan_transmit_blocking(BOARD_APP_CAN_BASE, &tx_buf);
            if (status != status_success) {
                printf("Failed to send CAN message, error: %d\n", status);
            } else {
                printf("CAN message sent successfully\n");
            }

            timeout = 0;
            while (!has_new_rcv_msg && timeout < 1000) {
                board_delay_ms(1);
                timeout++;
            }

            if (has_new_rcv_msg) {
                has_new_rcv_msg = false;
                show_received_can_message(&s_can_rx_buf);
            } else {
                printf("No echo received (timeout)\n");
            }

            board_delay_ms(1000);
        }

        /* 测试 3: CANFD 标准帧数据帧 */
        if (test_count % 4 == 2) {
            memset(&tx_buf, 0, sizeof(tx_buf));
            tx_buf.std_id = 0x456;
            tx_buf.dlc = 15; /* CANFD 64字节 */
            tx_buf.canfd_frame = 1;
            tx_buf.bitrate_switch = 0;
            tx_buf.use_ext_id = 0;
            uint32_t msg_len = mcan_get_message_size_from_dlc(tx_buf.dlc);
            for (uint32_t i = 0; i < msg_len; i++) {
                tx_buf.data_8[i] = (uint8_t) (i & 0xFF);
            }

            printf("\nSending CANFD Standard Data Frame (ID=0x%03x, %lu bytes)\n",
                   tx_buf.std_id,
                   msg_len);
            status = mcan_transmit_blocking(BOARD_APP_CAN_BASE, &tx_buf);
            if (status != status_success) {
                printf("Failed to send CANFD message, error: %d\n", status);
            } else {
                printf("CANFD message sent successfully\n");
            }

            timeout = 0;
            while (!has_new_rcv_msg && timeout < 1000) {
                board_delay_ms(1);
                timeout++;
            }

            if (has_new_rcv_msg) {
                has_new_rcv_msg = false;
                show_received_can_message(&s_can_rx_buf);
            } else {
                printf("No echo received (timeout)\n");
            }

            board_delay_ms(1000);
        }

        /* 测试 4: CANFD+BRS 扩展帧数据帧 */
        if (test_count % 4 == 3) {
            memset(&tx_buf, 0, sizeof(tx_buf));
            tx_buf.ext_id = 0x1FFFFFFF;
            tx_buf.dlc = 15; /* CANFD 64字节 */
            tx_buf.canfd_frame = 1;
            tx_buf.bitrate_switch = 1;
            tx_buf.use_ext_id = 1;
            uint32_t msg_len = mcan_get_message_size_from_dlc(tx_buf.dlc);
            for (uint32_t i = 0; i < msg_len; i++) {
                tx_buf.data_8[i] = (uint8_t) ((i * 2) & 0xFF);
            }

            printf("\nSending CANFD+BRS Extended Data Frame (ID=0x%08lx, %lu bytes)\n",
                   tx_buf.ext_id,
                   msg_len);
            status = mcan_transmit_blocking(BOARD_APP_CAN_BASE, &tx_buf);
            if (status != status_success) {
                printf("Failed to send CANFD+BRS message, error: %d\n", status);
            } else {
                printf("CANFD+BRS message sent successfully\n");
            }

            timeout = 0;
            while (!has_new_rcv_msg && timeout < 1000) {
                board_delay_ms(1);
                timeout++;
            }

            if (has_new_rcv_msg) {
                has_new_rcv_msg = false;
                show_received_can_message(&s_can_rx_buf);
            } else {
                printf("No echo received (timeout)\n");
            }

            board_delay_ms(1000);
        }

        /* LED 指示 */
        board_led_toggle();

        /* 错误检查 */
        if (has_error) {
            printf("CAN Error detected, resetting...\n");
            has_error = false;
            handle_can_error(BOARD_APP_CAN_BASE);
            /* 重置 MCAN, 此处略 */
            board_delay_ms(2000);
        }
    }
}

int main(void)
{
    board_init();
    init_pins();

    printf("\n");
    printf("*****************************************************\n");
    printf("*                                                   *\n");
    printf("*          HPM5321 MCAN0 ECHO Test Demo             *\n");
    printf("*                                                   *\n");
    printf("*  Features:                                        *\n");
    printf("*  - MCAN0: 1Mbps + 5Mbps with TDC                  *\n");
    printf("*  - Filter: 0:0 mask (accept all messages)         *\n");
    printf("*  - Test: CAN/CANFD/CANFD+BRS frames               *\n");
    printf("*  - Test: Standard/Extended ID                     *\n");
    printf("*  - Test: Data/Remote frames                       *\n");
    printf("*                                                   *\n");
    printf("*****************************************************\n");

    /* 初始化 MCAN0 */
    if (init_mcan0_config() != status_success) {
        printf("MCAN0 initialization failed!\n");
        while (1) {
            board_led_toggle();
            board_delay_ms(500);
        }
    }

    printf("\nMCAN0 ECHO test ready. Connect CAN bus for loopback testing.\n");
    printf("Or connect to another CAN device for communication testing.\n\n");

    /* 延迟一下让系统稳定 */
    board_delay_ms(1000);

    /* 开始 ECHO 测试 */
    can_echo_test();

    return 0;
}
