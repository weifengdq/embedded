/****************************************************************************
 * @file     slcan.c
 * @version  V1.00
 * @brief    slcan protocol source file
 *
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @copyright Copyright (C) 2024 Nuvoton Technology Corp. All rights reserved.
 * @copyright Copyright (C) 2024 RCSN
 ******************************************************************************/
#include "slcan.h"
#include "mcan.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"
#include "board.h"
#include "cdc_acm.h"
// #include "WS2812.h"
#include "hpm_clock_drv.h"

#ifndef USE_RGB_LED
#define USE_RGB_LED 0
#endif

/*------------------------------------------------------------------------------------------------*/
/* Global variables */
/*------------------------------------------------------------------------------------------------*/

UART_CFG_T def_uart_cfg = {115200, 0, 0, 8};


#if USE_RGB_LED
typedef struct
{
    uint8_t can_channel;
    uint8_t read_rgb_index;
    uint8_t write_rgb_index;
} can_rgb_index_t;

static const can_rgb_index_t can_rgb_index_table[SCLAN_NUM] =
{
    {0, 7, 8},
    {1, 6, 5},
    {2, 4, 3},
    {3, 2, 1},
};
#endif
static void can_led(uint8_t channel, bool read, bool status)
{
#if !USE_RGB_LED
    (void)channel;
    (void)read;
    (void)status;
#else
    uint8_t led_index;
    uint8_t r = 0x00, b = 0x00, g = 0x00;
    if (channel >= SCLAN_NUM) {
        return;
    }
    if (read == true) {
        led_index = can_rgb_index_table[channel].read_rgb_index;
        r = 0x0F;
    } else {
        led_index = can_rgb_index_table[channel].write_rgb_index;
        b = 0x0F;
    }
    if (status == true) {
        // WS2812_SetPixel(led_index, r, g, b);
    } else {
        // WS2812_SetPixel(led_index, 0, 0, 0);
    }
    // WS2812_Update(true);
#endif
}

/**
 * @brief       read can normal baudrate by index
 * @param[in]   can_baud_index :	can baudrate index
 * @return      Real baud rate
 * @details     define baud rate as 500kbps
 */
uint32_t sclcan_can_baud(uint8_t can_baud_index) {
    uint32_t baud = 500000;
    switch (can_baud_index) {
    case SLCAN_BAUD_10K:
        baud = 10000;
        break;
    case SLCAN_BAUD_20K:
        baud = 20000;
        break;
    case SLCAN_BAUD_50K:
        baud = 50000;
        break;
    case SLCAN_BAUD_100K:
        baud = 100000;
        break;
    case SLCAN_BAUD_125K:
        baud = 125000;
        break;
    case SLCAN_BAUD_250K:
        baud = 250000;
        break;
    case SLCAN_BAUD_500K:
        baud = 500000;
        break;
    case SLCAN_BAUD_800K:
        baud = 800000;
        break;
    case SLCAN_BAUD_1000K:
        baud = 1000000;
        break;
    }
    return baud;
}

/**
 * @brief       open one slcan
 * @param[in]   slcan_port : one can port will to be operated
 * @return
 * @details     if open debug will print debug menssage
 */
void slcan_can_open(struct slcan_t *slcan_port) {
    uint32_t can_src_clk_freq = board_init_can_clock(slcan_port->ptr);

    // PRINT can_src_clk_freq
    SLCAN_DEBUG("CAN[%d] Will Open  [%d bps][%d bps], master clock: [%d Hz] \r\n", slcan_port->candev_sn,
               slcan_port->candev_cfg.baudrate, slcan_port->candev_cfg.baudrate_fd, can_src_clk_freq);

    /* 设置低级时序配置以确保波特率准确性 */
    slcan_port->candev_cfg.use_lowlevel_timing_setting = true;
    
    /* 仲裁阶段时序配置，基于当前波特率 */
    uint32_t arb_baud = slcan_port->candev_cfg.baudrate;
    if (arb_baud <= 100000) {
        slcan_port->candev_cfg.can_timing.prescaler = 80;
    } else {
        slcan_port->candev_cfg.can_timing.prescaler = 8;
    }
    slcan_port->candev_cfg.can_timing.num_seg1 = 80000000.0 / slcan_port->candev_cfg.can_timing.prescaler * 0.8 - 1; // 80%采样点
    slcan_port->candev_cfg.can_timing.num_seg2 = 80000000.0 / slcan_port->candev_cfg.can_timing.prescaler * 0.2;
    slcan_port->candev_cfg.can_timing.num_sjw = slcan_port->candev_cfg.can_timing.num_seg2;
    
    /* 数据阶段时序配置 */
    // if (slcan_port->candev_cfg.baudrate_fd == 5000000) {
    //     /* 5Mbps: 80MHz / 2 / (1 + 5 + 2) = 5Mbps */
    //     slcan_port->candev_cfg.canfd_timing.prescaler = 2;
    //     slcan_port->candev_cfg.canfd_timing.num_seg1 = 5;
    //     slcan_port->candev_cfg.canfd_timing.num_seg2 = 2;
    //     slcan_port->candev_cfg.canfd_timing.num_sjw = 2;
    // } else {
    //     /* 默认2Mbps: 80MHz / 5 / (1 + 5 + 2) = 2Mbps */
    //     slcan_port->candev_cfg.canfd_timing.prescaler = 5;
    //     slcan_port->candev_cfg.canfd_timing.num_seg1 = 5;
    //     slcan_port->candev_cfg.canfd_timing.num_seg2 = 2;
    //     slcan_port->candev_cfg.canfd_timing.num_sjw = 2;
    // }
    slcan_port->candev_cfg.canfd_timing.prescaler = 1;
    if ((80000000 / slcan_port->candev_cfg.baudrate_fd ) % 8 == 0) {
        slcan_port->candev_cfg.canfd_timing.num_seg1 = 80000000.0 / slcan_port->candev_cfg.canfd_timing.prescaler * 0.75 - 1; // 80%采样点
        slcan_port->candev_cfg.canfd_timing.num_seg2 = 80000000.0 / slcan_port->candev_cfg.canfd_timing.prescaler * 0.25;
    } else {
        slcan_port->candev_cfg.canfd_timing.num_seg1 = 80000000.0 / slcan_port->candev_cfg.canfd_timing.prescaler * 0.8 - 1; // 80%采样点
        slcan_port->candev_cfg.canfd_timing.num_seg2 = 80000000.0 / slcan_port->candev_cfg.canfd_timing.prescaler * 0.2;
    }
    slcan_port->candev_cfg.canfd_timing.num_sjw = slcan_port->candev_cfg.canfd_timing.num_seg2;
    
    /* 启用TDC (发送延迟补偿) */
    slcan_port->candev_cfg.canfd_timing.enable_tdc = true;
    slcan_port->candev_cfg.enable_tdc = true;
    slcan_port->candev_cfg.tdc_config.ssp_offset = slcan_port->candev_cfg.canfd_timing.num_seg1 + 1;
    slcan_port->candev_cfg.tdc_config.filter_window_length = slcan_port->candev_cfg.tdc_config.ssp_offset;
    
    /* 配置滤波器接收所有消息 */
    slcan_port->candev_cfg.all_filters_config.std_id_filter_list.mcan_filter_elem_count = 0;
    slcan_port->candev_cfg.all_filters_config.ext_id_filter_list.mcan_filter_elem_count = 0;
    
    /* 配置中断掩码 */
    slcan_port->candev_cfg.interrupt_mask = MCAN_EVENT_RECEIVE | MCAN_EVENT_TRANSMIT | MCAN_EVENT_ERROR;
    slcan_port->candev_cfg.txbuf_trans_interrupt_mask = ~0UL;
    
        SLCAN_DEBUG("@CAN[%d] Open  [%d bps][%d bps] \r\n", slcan_port->candev_sn,
              slcan_port->candev_cfg.baudrate, slcan_port->candev_cfg.baudrate_fd);

    mcan_init(slcan_port->ptr, &slcan_port->candev_cfg, can_src_clk_freq);

        SLCAN_DEBUG("#CAN[%d] Open  [%d bps][%d bps] \r\n", slcan_port->candev_sn,
              slcan_port->candev_cfg.baudrate, slcan_port->candev_cfg.baudrate_fd);
    
    /* 启用完整的中断 */
    uint32_t interrupt_mask = MCAN_EVENT_RECEIVE | MCAN_EVENT_TRANSMIT | MCAN_EVENT_ERROR;
    mcan_enable_interrupts(slcan_port->ptr, interrupt_mask);
    intc_m_enable_irq_with_priority(slcan_port->irq_num, 1);
    slcan_port->candev_isopen = 1;
    SLCAN_DEBUG("CAN[%d] Open  [%d bps][%d bps] \r\n", slcan_port->candev_sn,
              slcan_port->candev_cfg.baudrate, slcan_port->candev_cfg.baudrate_fd);
}

/**
 * @brief       close one slcan
 * @param[in]   slcan_port : one can port will to be operated
 * @return
 * @details     if open debug will print debug menssage
 */
void slcan_can_close(struct slcan_t *slcan_port) {
    mcan_deinit(slcan_port->ptr);
    uint32_t interrupt_mask = MCAN_EVENT_RECEIVE | MCAN_EVENT_TRANSMIT | MCAN_EVENT_ERROR;
    mcan_disable_interrupts(slcan_port->ptr, interrupt_mask);
    intc_m_disable_irq(slcan_port->irq_num);
    slcan_port->candev_isopen = 0;
    SLCAN_DEBUG("CAN[%d] Close \r\n", slcan_port->candev_sn);
}

/**
 * @brief       set can normal baud rate
 * @param[in]   slcan_port : one can port will to be operated
 * @param[in]   can baudrate index
 * @return
 * @details     if open debug will print debug menssage
 */
void slcan_can_set_nbaud(struct slcan_t *slcan_port, uint8_t baud_index) {
    slcan_port->candev_cfg.baudrate = sclcan_can_baud(baud_index);
    SLCAN_DEBUG("CAN[%d]set NormBitRate: [%d bit/s]\r\n", slcan_port->candev_sn, slcan_port->candev_cfg.baudrate);
}

/**
 * @brief       set can data baud rate
 * @param[in]   slcan_port : one can port will to be operated
 * @param[in]   data_mbps:
                                                                                                                - \ref 2  =》 2Mbps
                                                                                                                - \ref 5  =》 5Mbps
 * @return
 * @details     if open debug will print debug menssage
 */
void slcan_can_set_dbaud(struct slcan_t *slcan_port, uint8_t data_mbps) {
    slcan_port->candev_cfg.baudrate_fd = data_mbps * 1000000;
    SLCAN_DEBUG("CAN[%d]set DataBitRate: [%dkbps]\r\n", slcan_port->candev_sn, slcan_port->candev_cfg.baudrate_fd);
}

/**
 * @brief       set can work mode
 * @param[in]   slcan_port : one can port will to be operated
 * @param[in]    mode_index:	select can work mode
                                                                                        - \ref SLCAN_MODE_NORMAL
                                                                                        - \ref SLCAN_MODE_LISTEN,
                                                                                        - \ref SLCAN_MODE_LOOPBACK,
                                                                                        - \ref SLCAN_MODE_LOOPBACKANLISTEN,

 * @return
 * @details     if open debug will print debug menssage
 */
void slcan_can_set_mode(struct slcan_t *slcan_port, uint8_t mode_index) {
    if (slcan_port->candev_isopen == 1)
        return;

    switch (mode_index) {
    case SLCAN_MODE_NORMAL:
        slcan_port->candev_cfg.mode = mcan_mode_normal;
        break;
    case SLCAN_MODE_LISTEN:
        slcan_port->candev_cfg.mode = mcan_mode_listen_only;
        break;
    case SLCAN_MODE_LOOPBACK:
        slcan_port->candev_cfg.mode = mcan_mode_loopback_external;
        break;
    case SLCAN_MODE_LOOPBACKANLISTEN:
        slcan_port->candev_cfg.mode = mcan_mode_loopback_internal;
        break;
    }

    SLCAN_DEBUG("can[%d]mode_index:[%d]\r\n", slcan_port->candev_sn, mode_index);
}

/**
 * @brief       read one message to slcan instance
 * @param[in]   slcan_port : one can port will to be operated
 * @return  		sizeof(mcan_tx_frame_t)
 * @details     if open debug will print debug menssage
 */
uint32_t slcan_can_read(struct slcan_t *slcan_port, uint8_t fifo_num) {
    memcpy(&slcan_port->can_rx_msg, &g_mcan_rx_frame[slcan_port->candev_sn][fifo_num], sizeof(mcan_rx_message_t));
    return sizeof(mcan_tx_frame_t);
}

/**
 * @brief       write one message to slcan can port
 * @param[in]   slcan_port : one can port will to be operated
 * @return  		sizeof(mcan_tx_frame_t)
 * @details
 */
uint32_t slcan_can_write(struct slcan_t *slcan_port) {
    can_led(slcan_port->candev_sn, false, true);
    mcan_transmit_blocking(slcan_port->ptr, &slcan_port->can_tx_msg);
    can_led(slcan_port->candev_sn, false, false);
    return sizeof(mcan_tx_frame_t);
}

/**
 * @brief       which channel uart port well open
 * @param[in]   slcan_port : one can port will to be operated
 * @return
 * @details     if use CAN to UART need open initialize
                                                                for example:
                                                                UART_Open(UART0,
 slcan_port->uartdev_cfg.u32BitRate); UART_SetLineConfig(UART0,
 slcan_port->uartdev_cfg.u32BitRate, slcan_port->uartdev_cfg.u8DataBits,
                                                                                                                        slcan_port->uartdev_cfg.u8ParityType, slcan_port->uartdev_cfg.u8CharFormat);
 */

void slcan_uart_open(struct slcan_t *slcan_port) {
    switch (slcan_port->uartdev_sn) {
    case 4: // add uart initialize code
    case 5:
        break;
    }
    SLCAN_DEBUG("com[%d] open \r\n", slcan_port->uartdev_sn);
}

/**
 * @brief       which channel uart port well close
 * @param[in]   slcan_port : one can port will to be operated
 * @return
 * @details    	for example :UART_Close(UART0)
 */
void slcan_uart_close(struct slcan_t *slcan_port) {
    switch (slcan_port->uartdev_sn) {
    case 4: // add uart close code
    case 5:
        break;
    }
    SLCAN_DEBUG("com[%d] close\r\n", slcan_port->uartdev_sn);
}

/**
 * @brief       read slcan uart data from buffer and check valid data
 * @param[in]   slcan_port : one can port will to be operated
 * @return  		return -1 : no data
                                                                return 0
 : only 1 valid data return 1 	: There still data left to process
 * @details
 */

int32_t slcan_uart_read(struct slcan_t *slcan_port) {
    uint32_t ticks_per_us = (hpm_core_clock + 1000000 - 1U) / 1000000;
    uint64_t expected_ticks = hpm_csr_get_core_cycle() + (uint64_t)ticks_per_us * 1000UL * 1000; //1000ms
    uint8_t data;
    if (slcan_port->candev_sn < SCLAN_NUM) {
        if (get_usb_out_is_empty(slcan_port->candev_sn) == false) {
            printf("slcan_uart_read: com%d\n", slcan_port->candev_sn);
            while (1) {
                if (get_usb_out_char(slcan_port->candev_sn, &data) == true) {
                    slcan_port->uart_rx_buffer[slcan_port->uart_rx_len++] = (uint8_t)data;
                    if (data == '\r') {
                        // printf("sl_r:%d\r\n", slcan_port->uart_rx_len);
                        return 1;
                    }
                }
                if (hpm_csr_get_core_cycle() > expected_ticks) {
                    slcan_port->uart_rx_len = 0;
                    return -1;
                }
            }
        }
    }
    return -1; // Data buffer empty
}

/**
 * @brief       send slcan uart data
 * @param[in]   slcan_port : one can port will to be operated
 * @param[in]   buffer : send buffer opint
 * @param[in]   size	 : buffer data lenght
 * @return  		The length of the sent data
 * @details
 */

uint32_t slcan_uart_write(struct slcan_t *slcan_port, void *buffer,
                          uint32_t size) {
    uint32_t res;
    if (slcan_port->uartdev_sn < SCLAN_NUM) // USB_VCOM used sn < 4
    {
        res = write_usb_data(slcan_port->uartdev_sn, (uint8_t *)buffer, size); //Data Point to USB_VCOM
    } else {
        // user add uart port data read code
        // Data Point to UART
    }
    return res;
}

/**
 * @brief       ASC2 convert to decimal
 * @param[in]   c 		 ：ASC2 code
 * @return  		decimal number
 * @details
 */

static int asc2nibble(char c) {
    if ((c >= '0') && (c <= '9'))
        return c - '0';

    if ((c >= 'A') && (c <= 'F'))
        return c - 'A' + 10;

    if ((c >= 'a') && (c <= 'f'))
        return c - 'a' + 10;

    return 16; /* error */
}

/**
 * @brief       hex sting convert to decimal
 * @param[in]   strbuffer		 	：hex sting
 * @param[in]   strlen		 		：sting lenght
 * @return  		decimal number
 * @details
 */
static int hexstr2int(uint8_t *strbuffer, uint8_t strlen) {
    uint32_t numcount = 0;
    int8_t num = 0;
    int8_t index = 0;
    while (index < strlen) {
        num = asc2nibble(strbuffer[index]);
        if ((0x00 <= num) && (num <= 0x0F)) {
            numcount = numcount << 4;
            numcount |= (num & 0x0F);
        }
        index++;
    }
    return numcount;
}

/**
 * @brief       Decode the Data Length Code.
 * @param[in]   code:   Data Length Code.
 * @return      Number of bytes in a message.
 * @details     Converts a Data Length Code into a number of message bytes.
 */
static uint8_t SLCAN_DecodeDLC(uint8_t code) {
    if (code >= '0' && code < '9') {
        return code - '0';
    } else {
        switch (code) {
        case '9':
            return 12;
        case 'A':
            return 16;
        case 'B':
            return 20;
        case 'C':
            return 24;
        case 'D':
            return 32;
        case 'E':
            return 48;
        case 'F':
            return 64;
        default:
            return 0xff;
        }
    }
}

/**
 * @brief       Encode the Data Length Code.
 * @param[in]   dlc:  Number of bytes in a message.
 * @return      Data Length Code.
 * @details     Converts number of bytes in a message into a Data Length Code.
 */
static uint8_t SLCAN_EncodeDLC(uint8_t dlc) {
    if (dlc < 9) {
        return dlc + '0';
    } else {
        switch (dlc) {
        case 12:
            return '9';
        case 16:
            return 'A';
        case 20:
            return 'B';
        case 24:
            return 'C';
        case 32:
            return 'D';
        case 48:
            return 'E';
        case 64:
            return 'F';
        default:
            return 0xff;
        }
    }
}

uint8_t mcan_get_dlc_from_message_size(uint8_t message_size)
{
    uint32_t dlc = 0;
    if (message_size <= 8U) {
        dlc = message_size;
    } else {
        switch (message_size)
        {
        case 12:
            dlc = 9;
            break;
        case 16:
            dlc = 10;
            break;
        case 20:
            dlc = 11;
            break;
        case 24:
            dlc = 12;
            break;
        case 32:
            dlc = 13;
            break;
        case 48:
            dlc = 14;
            break;
        case 64:
            dlc = 15;
            break; 
        default:
            break;
        }
    }
    return dlc;
}

/**
 * @brief       uart ascii data convert to command data
 * @param[in]   slcan_port : one can port will to be operated
 * @return
 * @details     slcan port operate and send can message
 */
void slcan_parse_ascii(struct slcan_t *slcan_port) {

    /* for answers to received commands */
    int rx_out_len = 0;                        // uart ack data lenght
    char replybuf[10] = {0};                   // uart ack data buffer
    uint8_t uart_status;                       // uart cmd ack status
    uint8_t *buf = slcan_port->uart_rx_buffer; // cmd data buffer

    uint8_t cmd_bytes; // uart cmd lenght :	cmd_bytes = 1 has no parameter
                       //									cmd_bytes
                       //= 2 has 1 parameter

    int tmp = 0;

    uint8_t cmd = buf[0]; // slcan uart cmd

    switch (cmd) // command analyse
    {

    /* 	Open the CAN channel in normal mode (sending & receiving).
                        This command is only active if the CAN channel is closed
       and has been set up prior with either the S or s command (i.e.
       initiated)*/
    case 'O':
        slcan_can_close(slcan_port);
        if (slcan_port->candev_cfg.baudrate == 0) {
            slcan_port->candev_cfg.baudrate = 500000;
        }

        slcan_can_open(slcan_port);
        cmd_bytes = 1;
        uart_status = SLCAN_UART_ACK;
        break;

    /* 	Close the CAN channel.
                        This command is only active if the CAN channel is open
     */
    case 'C':
        slcan_can_close(slcan_port);
        cmd_bytes = 1;
        uart_status = SLCAN_UART_ACK;
        break;

    /* Setup with standard CAN FD Data bit-rates where n is 2 or 5.
        2: Data bit rates is 2Mbps ;  5: Data bit rates is 5Mbps ； */
    case 'Y': // only Y2 or Y5
        cmd_bytes = 2;
        if (buf[1] == '2' || buf[1] == '4' || buf[1] == '5' || buf[1] == '8') {
            slcan_can_set_dbaud(slcan_port, buf[1] - '0');
            uart_status = SLCAN_UART_ACK;
        } else
            uart_status = SLCAN_UART_NACK;
        break;

    /* Setup with standard CAN bit-rates where n is 0-8.
        This command is only active if the CAN channel is close */
    case 'S': // S0~S8
        cmd_bytes = 2;
        if ((buf[1] - '0') < 9) {
            printf("slcan_can_set_nbaud: %d\n", buf[1] - '0');
            slcan_can_set_nbaud(slcan_port, buf[1] - '0');
            uart_status = SLCAN_UART_ACK;
        } else
            uart_status = SLCAN_UART_NACK;

        break;

        //---------
        // canableV2.0 CANFD mode,	FD mode	none remote frame
    case 'r': // Transmit an standard RTR (11bit) CAN frame
    case 't': // Transmit an standard (11bit) CAN frame.
    case 'd': // Transmit an standard CAN FD frame
    case 'b': // Transmit an standard CAN FD BRS frame
        memset(&slcan_port->can_tx_msg, 0, sizeof(slcan_port->can_tx_msg));
        tmp = SLCAN_DecodeDLC(buf[4]);
        if (tmp == 0xff) {
            uart_status = SLCAN_UART_NACK;
            break;
        } else {
            slcan_port->can_tx_msg.dlc = mcan_get_dlc_from_message_size(tmp);
        }
        tmp = hexstr2int(buf + 1, 3);
        slcan_port->can_tx_msg.std_id = tmp; // Frame ID
        slcan_port->can_tx_msg.use_ext_id = false;// 11bit id  Standard frame format
        cmd_bytes = 5;    // "Riiin********\a"
        if (cmd == 'r') { // tiiil[CR]
            slcan_port->can_tx_msg.rtr = true; // Remote frame
        } else {
            switch (cmd) {
            case 'b':
                slcan_port->can_tx_msg.bitrate_switch = true;
                slcan_port->can_tx_msg.canfd_frame = true;
                break;
            case 'd':
                slcan_port->can_tx_msg.bitrate_switch = false;
                slcan_port->can_tx_msg.canfd_frame = true;
                break;
            default:
                slcan_port->can_tx_msg.canfd_frame = false;
                slcan_port->can_tx_msg.bitrate_switch = false;
                break;
            }

            // tiiildd...[CR]
            if (mcan_get_message_size_from_dlc(slcan_port->can_tx_msg.dlc) > 0) {
                cmd_bytes += (mcan_get_message_size_from_dlc(slcan_port->can_tx_msg.dlc) * 2); // add dlc
                for (tmp = 0; tmp < mcan_get_message_size_from_dlc(slcan_port->can_tx_msg.dlc); tmp++) {
                    slcan_port->can_tx_msg.data_8[tmp] = hexstr2int(buf + 5 + (tmp * 2), 2);
                }
            }
        }
        rx_out_len = 1;
        uart_status = SLCAN_UART_SEND;
        break;

    case 'R': // Transmit an extended RTR (29bit) CAN frame.
    case 'T': // Transmit an extended (29bit) CAN frame.
    case 'D': // Transmit an extended CAN FD frame
    case 'B': // Transmit an extended CAN FD BRS frame
        memset(&slcan_port->can_tx_msg, 0, sizeof(slcan_port->can_tx_msg));
        tmp = SLCAN_DecodeDLC(buf[9]);
        if (tmp == 0xff) {
            uart_status = SLCAN_UART_NACK;
            break;
        } else
            slcan_port->can_tx_msg.dlc = mcan_get_dlc_from_message_size(tmp);
        tmp = hexstr2int(buf + 1, 8);
        slcan_port->can_tx_msg.ext_id = tmp; // Frame ID
        slcan_port->can_tx_msg.use_ext_id = true; // 29bit id Extend frame format
        cmd_bytes = 10;
        ;                 // "Riiiiiiii********\a"
        if (cmd == 'R') { // Riiiiiiiil[CR]
            slcan_port->can_tx_msg.rtr = true; // Remote frame
        } else {
            switch (cmd) {
            case 'B':
                slcan_port->can_tx_msg.bitrate_switch = true;
                slcan_port->can_tx_msg.canfd_frame = true;
                break;
            case 'D':
                slcan_port->can_tx_msg.bitrate_switch = false;
                slcan_port->can_tx_msg.canfd_frame = true;
                break;
            default:
                slcan_port->can_tx_msg.canfd_frame = false;
                slcan_port->can_tx_msg.bitrate_switch = false;
                break;
            }

            // Data frame
            if (mcan_get_message_size_from_dlc(slcan_port->can_tx_msg.dlc) > 0) { // Tiiiiiiiildd...[CR]
                cmd_bytes += (mcan_get_message_size_from_dlc(slcan_port->can_tx_msg.dlc) * 2); // add dlc
                for (tmp = 0; tmp < mcan_get_message_size_from_dlc(slcan_port->can_tx_msg.dlc); tmp++) {
                    slcan_port->can_tx_msg.data_8[tmp] = hexstr2int(buf + 10 + (tmp * 2), 2);
                }
            }
        }
        rx_out_len = 1;
        uart_status = SLCAN_UART_SEND;
        break;

    /* Get Version number of both CAN232 hardware and software
                This command is only active always */
    case 'V': // software version
        rx_out_len = sprintf(replybuf, "V2401\r");
        cmd_bytes = 1;
        uart_status = SLCAN_UART_REPLY;
        break;

    /* check for 'v'ersion command */
    case 'v': // Hardware version
        rx_out_len = sprintf(replybuf, "v2401\r");
        cmd_bytes = 1;
        uart_status = SLCAN_UART_REPLY;
        break;

    /* Get Serial number of the CAN232.
                This command is only active always */
    case 'N':
        rx_out_len = sprintf(replybuf, "N0001\r");
        cmd_bytes = 1;
        uart_status = SLCAN_UART_REPLY;
        break;

        //---------------------------------------------------------------------------//
        // The following functions need to be added,	release it in the future
        // version //
        //---------------------------------------------------------------------------//

    /* 	Open the CAN channel in listen only mode (receiving).
                        This command is only active if the CAN channel is closed
       and has been set up prior with either the S or s command (i.e. initiated)
     */
    case 'L':
        cmd_bytes = 1;
        uart_status = SLCAN_UART_ACK;
        break;
    /* 	Read Status Flags.
                        This command is only active if the CAN channel is open
        Bit 0 CAN receive FIFO queue full
        Bit 1 CAN transmit FIFO queue full
        Bit 2 Error warning (EI), see SJA1000 datasheet
        Bit 3 Data Overrun (DOI), see SJA1000 datasheet
        Bit 4 Not used.
        Bit 5 Error Passive (EPI), see SJA1000 datasheet
        Bit 6 Arbitration Lost (ALI), see SJA1000 datasheet *
        Bit 7 Bus Error (BEI), see SJA1000 datasheet **
        */
    case 'F':
        rx_out_len = sprintf(replybuf, "F00\r");
        cmd_bytes = 1;
        uart_status = SLCAN_UART_REPLY;
        break;

    /* Setup UART with a new baud rate where n is 0-6.
                This command is only active if the CAN channel is closed.
                The value is saved in EEPROM and is remembered next time
                the CAN232 is powered up.
                U0 • Setup 230400 baud (not guaranteed to work)
                U1 •• Setup 115200 baud
                U2 ••• Setup 57600 baud (default when delivered)
                U3 •••• Setup 38400 baud
                U4 ••••• Setup 19200 baud
                U5 •••••• Setup 9600 baud
                U6 ••••••• Setup 2400 bau */
    case 'U':
        cmd_bytes = 2;
        break;

    /*Setup with BTR0/BTR1 CAN bit-rates where xx and yy is a hex
    value. This command is only active if the CAN channel is closed.*/
    case 's': // sxxyy
        cmd_bytes = 5;
        uart_status = SLCAN_UART_NACK;
        break;

        // poll functio set  ---------
        /*Poll incomming FIFO for CAN frames (single poll)
                This command is only active if the CAN channel is open.
                NOTE: This command is disabled in the new AUTO POLL/SEND
                feature from version V1220. It will then reply BELL if used.
                Example 1: P[CR]
                Poll one CAN frame from the FIFO queue.
                Returns: A CAN frame with same formatting as when sending
                frames and ends with a CR (Ascii 13) for OK. If there
                are no pendant frames it returns only CR. If CAN
                channel isn’t open it returns BELL (Ascii 7). If the
                TIME STAMP is enabled, it will reply back the time
                in milliseconds as well after the last data byte (before
                the CR). For more information, see the Z command.*/
    case 'P':
        /*Polls incomming FIFO for CAN frames (all pending frames)
          This command is only active if the CAN channel is open.
          NOTE: This command is disabled in the new AUTO POLL/SEND
          feature from version V1220. It will then reply BELL if used.
          Example 1: A[CR]
          Polls all CAN frame from the FIFO queue.
          Returns: CAN frames with same formatting as when sending
          frames seperated with a CR (Ascii 13). When all
          frames are polled it ends with an A and
          a CR (Ascii 13) for OK. If there are no pending
          frames it returns only an A and CR. If CAN
          channel isn’t open it returns BELL (Ascii 7). If the
          TIME STAMP is enabled, it will reply back the time
          in milliseconds as well after the last data byte (before
          the CR). For more information, see the Z command*/
    case 'A':
        cmd_bytes = 1;
        uart_status = SLCAN_UART_NACK;
        break;
    /*Sets Auto Poll/Send ON/OFF for received frames.*/
    case 'X':
        cmd_bytes = 2;
        if (buf[1] & 0x01)
            uart_status = SLCAN_UART_ACK;
        else
            uart_status = SLCAN_UART_NACK;
        break;

    /* Setting Acceptance Code and Mask registers*/
    case 'W': // Wn[CR] Filter mode setting.
        cmd_bytes = 2;
        uart_status = SLCAN_UART_ACK;
        break;
    case 'M':       // Mxxxxxxxx[CR] Sets Acceptance Mask Register
    case 'm':       // mxxxxxxxx[CR] Sets Acceptance Code Register
        buf[9] = 0; /* terminate filter string */
        cmd_bytes = 9;
        uart_status = SLCAN_UART_ACK;
        break;

    /* Sets Time Stamp ON/OFF for received frames only */
    case 'Z':
        if ((buf[1] & 0x01)) {
            slcan_port->set_flag |= SLCAN_SFLAG_TIMESTAMP;
        } else {
            slcan_port->set_flag &= ~SLCAN_SFLAG_TIMESTAMP;
        }
        cmd_bytes = 2;
        uart_status = SLCAN_UART_ACK;
        break;

    case '\r':
    /*Auto Startup feature (from power on).*/
    case 'Q':
    default:
        uart_status = SLCAN_UART_NACK;
    }

    switch (uart_status) // State process
    {
    case SLCAN_UART_EXIT:
        rx_out_len = 0;
        break;
    case SLCAN_UART_NACK:
        replybuf[0] = '\a';
        rx_out_len = 1;
        break;
    case SLCAN_UART_REPLY:
        break;
    case SLCAN_UART_SEND:
        slcan_can_write(slcan_port);
    case SLCAN_UART_ACK:
        replybuf[0] = '\r';
        rx_out_len = 0; // cangroo don't need ack
    default:
        break;
    }

    if (rx_out_len > 0) // uart ack
    {
        slcan_uart_write(slcan_port, replybuf, rx_out_len);
        // SLCAN_DEBUG("COM[%d]Reply:[%d][%s]\r\n", slcan_port->uartdev_sn, rx_out_len,
        //           replybuf);
    }
}

/**
 * @brief       uart process
 * @param[in]   slcan_port : one can port will to be operated
 * @return
 * @details     serial  data ---> can message
 */
void slcan_process_uart(struct slcan_t *slcan_port) {
    int32_t rc = 1;

    // 添加调试信息检查是否有数据要处理
    if (!get_usb_out_is_empty(slcan_port->candev_sn)) {
        printf("slcan_process_uart: can%d has data to process\r\n", slcan_port->candev_sn);
    }

    while (rc) // Buffer still has command. Keep checking
    {
        rc = slcan_uart_read(slcan_port); // check uart data correct

        if (rc < 0) // No valid data
        {
            memset(slcan_port->uart_rx_buffer, 0, SLCAN_MTU);
            return;
        }
        SLCAN_DEBUG("[com%d]>>[can%d]:", slcan_port->candev_sn, slcan_port->candev_sn);
        for (uint32_t i = 0; i < slcan_port->uart_rx_len; i++) {
            SLCAN_DEBUG("%c",slcan_port->uart_rx_buffer[i]);
        }
        SLCAN_DEBUG("\n");

        slcan_parse_ascii(slcan_port); // uart data conversion
        slcan_port->uart_rx_len = 0;
        memset(slcan_port->uart_rx_buffer, 0, SLCAN_MTU);
    }
}

/**
 * @brief       can message convert to uart data
 * @param[in]   slcan_port : one can port will to be operated
 * @return
 * @details     can data ---> serial  data
 */
static int slcan_can2ascii(struct slcan_t *slcan_port) {
    int pos = 0;
    uint32_t temp = 0;
    memset(slcan_port->uart_tx_buffer, 0, SLCAN_MTU);

    /* RTR frame */
    if (slcan_port->can_rx_msg.rtr == true) {
        if (slcan_port->can_rx_msg.use_ext_id == true) {
            slcan_port->uart_tx_buffer[pos] = 'R';
        } else {
            slcan_port->uart_tx_buffer[pos] = 'r';
        }
    } else /* data frame */
    {
        if (slcan_port->can_rx_msg.use_ext_id == true) {
            if (slcan_port->can_rx_msg.bitrate_switch == true)
                slcan_port->uart_tx_buffer[pos] = 'B';
            else if (slcan_port->can_rx_msg.canfd_frame == true)
                slcan_port->uart_tx_buffer[pos] = 'D';
            else
                slcan_port->uart_tx_buffer[pos] = 'T';
        } else {
            if (slcan_port->can_rx_msg.bitrate_switch == true)
                slcan_port->uart_tx_buffer[pos] = 'b';
            else if (slcan_port->can_rx_msg.canfd_frame == true)
                slcan_port->uart_tx_buffer[pos] = 'd';
            else
                slcan_port->uart_tx_buffer[pos] = 't';
        }
    }

    pos++;
    /* id */
    if (slcan_port->can_rx_msg.use_ext_id == true) {
        temp = slcan_port->can_rx_msg.ext_id;
        snprintf((char *)(slcan_port->uart_tx_buffer + pos), 9, "%08X", temp);
        pos += 8;
    } else {
        temp = slcan_port->can_rx_msg.std_id;
        snprintf((char *)(slcan_port->uart_tx_buffer + pos), 4, "%03X", temp);
        pos += 3;
    }
    /* len */
    slcan_port->uart_tx_buffer[pos] = SLCAN_EncodeDLC(mcan_get_message_size_from_dlc(slcan_port->can_rx_msg.dlc));
    pos++;
    /* data */
    if (slcan_port->can_rx_msg.rtr != true) {
        for (int i = 0; i < mcan_get_message_size_from_dlc(slcan_port->can_rx_msg.dlc); i++) {
            snprintf((char *)(slcan_port->uart_tx_buffer + pos), 3, "%02X",
                     slcan_port->can_rx_msg.data_8[i]);
            pos += 2;
        }
    }
    /* timestamp */

    if (slcan_port->set_flag & SLCAN_SFLAG_TIMESTAMP) {
        uint32_t tick = slcan_port->timestamp;
        sprintf((char *)(slcan_port->uart_tx_buffer + pos), "%04X",
                (tick & 0x0000FFFF));
        pos += 4;
    }
    /* end */
    slcan_port->uart_tx_buffer[pos] = '\r';
    pos++;
    slcan_port->uart_tx_buffer[pos] = 0;
    return pos;
}

/**
 * @brief       can process
 * @param[in]   slcan_port : one can port will to be operated
 * @return
 * @details     can data ---> serial  data
 */
void slcan_process_can(struct slcan_t *slcan_port)
{
    int read_rc = 0;
    int tx_len = 0;
    for (uint8_t i = 0; i < MCAN_RXBUF_SIZE_CAN_DEFAULT; i++) {
        if (g_mcan_rx_fifo_complete_flag[slcan_port->candev_sn][i]) {
            can_led(slcan_port->candev_sn, true, true);
            g_mcan_rx_fifo_complete_flag[slcan_port->candev_sn][i] = 0;
            read_rc = slcan_can_read(slcan_port, i);

            if (read_rc != sizeof(mcan_rx_message_t)) {
                return;
            }
            tx_len = slcan_can2ascii(slcan_port);
            if (tx_len <= 0) {
                return;
            }
            // SLCAN_DEBUG("[can%d]>>[com%d]: fifo:%d tx_len:%d\r\n", slcan_port->candev_sn,
            //             slcan_port->candev_sn, i, tx_len);
            slcan_uart_write(slcan_port, slcan_port->uart_tx_buffer, tx_len);
            memset(&slcan_port->can_rx_msg, 0, sizeof(mcan_rx_message_t));
            can_led(slcan_port->candev_sn, true, false);
        }
    }
}

/**
 * @brief       both can and process
 * @param[in]   slcan_port : one can port will to be operated
 * @return
 * @details
 */
void slcan_process_task(struct slcan_t *slcan_port) {
    // 添加调试信息显示正在处理哪个SLCAN端口
    // static uint32_t debug_counter = 0;
    // if (debug_counter++ % 1000 == 0) {  // 每1000次循环打印一次，更频繁
    //     printf("slcan_process_task: processing can%d\r\n", slcan_port->candev_sn);
    // }
    
    slcan_process_can(slcan_port);  // data from CANFD to VCOM
    slcan_process_uart(slcan_port); // data from VCOM  to CANFD
}

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
#if defined(HPM_MCAN0)
ATTR_PLACE_AT(".ahb_sram") uint32_t mcan0_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif
#endif

/**
 * @brief       slcan instance initialization
 * @param[in]
 * @return
 * @details
 */
void slcan_port0_init(void) {
    struct slcan_t *slcan = &slcan0;

    /*slcan port 0 initialization */
    slcan->candev_sn = 0; // CANFD Port0
    slcan->ptr = HPM_MCAN0;
    slcan->irq_num = IRQn_MCAN0;

    clock_add_to_group(clock_can0, 0);
    clock_set_source_divider(clock_can0, clk_src_pll1_clk0, 10);

    /* RAM */
    mcan_msg_buf_attr_t attr = {.ram_base = (uint32_t) mcan0_msg_buf,
                                .ram_size = sizeof(mcan0_msg_buf)};
    hpm_stat_t status = mcan_set_msg_buf_attr(HPM_MCAN0, &attr);
    if (status != status_success) {
        printf("Error was detected during setting message buffer attribute, please check the "
               "arguments\n");
        return;
    }

    printf("a\r\n");
    mcan_get_default_config(slcan->ptr, &slcan->candev_cfg);
    slcan->candev_cfg.mode = mcan_mode_normal; // Normal mode
    slcan->candev_cfg.enable_canfd = true;

    slcan->candev_cfg.use_lowlevel_timing_setting = true;
    uint32_t can_src_clk_freq = clock_get_frequency(clock_can0);
    /* 仲裁阶段配置：1Mbps */
    /* 波特率 = 80MHz / 8 / (1 + 15 + 4) = 500kbps */
    slcan->candev_cfg.can_timing.prescaler = 8;
    slcan->candev_cfg.can_timing.num_seg1 = 15; /* 采样点 80% */
    slcan->candev_cfg.can_timing.num_seg2 = 4;
    slcan->candev_cfg.can_timing.num_sjw = 4;

    /* 数据阶段配置：5Mbps */
    /* 波特率 = 80MHz / 2 / (1 + 5 + 2) = 5Mbps */
    slcan->candev_cfg.canfd_timing.prescaler = 2;
    slcan->candev_cfg.canfd_timing.num_seg1 = 5; /* 采样点 75% */
    slcan->candev_cfg.canfd_timing.num_seg2 = 2;
    slcan->candev_cfg.canfd_timing.num_sjw = 2;

    slcan->candev_cfg.all_filters_config.std_id_filter_list.mcan_filter_elem_count = 0;
    slcan->candev_cfg.all_filters_config.ext_id_filter_list.mcan_filter_elem_count = 0;

    /* 配置中断 */
    slcan->candev_cfg.interrupt_mask = MCAN_EVENT_RECEIVE | MCAN_EVENT_TRANSMIT | MCAN_EVENT_ERROR;
    slcan->candev_cfg.txbuf_trans_interrupt_mask = ~0UL;

    printf("b\r\n");
    mcan_get_default_ram_config(slcan->ptr, &slcan->candev_cfg.ram_config, true);
    printf("b1\r\n");

    status = mcan_init(slcan->ptr, &slcan->candev_cfg, can_src_clk_freq);
    if (status != status_success) {
        printf("MCAN initialization failed, error code: %d\n", status);
        return;
    }

    printf("c\r\n");
    mcan_enable_interrupts(slcan->ptr, MCAN_EVENT_RECEIVE | MCAN_EVENT_TRANSMIT | MCAN_EVENT_ERROR);
    intc_m_enable_irq_with_priority(slcan->irq_num, 1);

    slcan->candev_oflag = 0; // can hardware switch flag

    slcan->uartdev_sn = 0;    // USB VCOM1
    slcan->uartdev_oflag = 0; // uart hardware switch flag
    slcan->uart_rx_len = 0;

    slcan->run_state = 0;
    slcan->run_flag = 0;
    slcan->set_flag = 0;
    slcan->timestamp_isopen = 0;
    slcan->candev_isopen = 0; // cangaroo app switch flag
    mcan_pinmux_init(slcan->candev_sn);
    printf("d\r\n");
    mcan_channel_init(slcan->candev_sn);
    SLCAN_DEBUG("SLCAN0 Port Init: CAN[%d]<=>VCOM[%d]\r\n", slcan->candev_sn,
                slcan->uartdev_sn);
}

#if (SCLAN_NUM >= 2)
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
#if defined(HPM_MCAN1)
ATTR_PLACE_AT(".ahb_sram") uint32_t mcan1_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif
#endif
/**
 * @brief       slcan instance initialization
 * @param[in]
 * @return
 * @details
 */
void slcan_port1_init(void) {
    struct slcan_t *slcan = &slcan1;

    /*slcan port 0 initialization */
    slcan->candev_sn = 1; // CANFD Port0
    slcan->ptr = HPM_MCAN1;
    slcan->irq_num = IRQn_MCAN1;
    slcan->candev_oflag = 0; // can hardware switch flag

    clock_add_to_group(clock_can1, 0);
    clock_set_source_divider(clock_can1, clk_src_pll1_clk0, 10);

    /* RAM */
    mcan_msg_buf_attr_t attr = {.ram_base = (uint32_t) mcan1_msg_buf,
                                .ram_size = sizeof(mcan1_msg_buf)};
    hpm_stat_t status = mcan_set_msg_buf_attr(HPM_MCAN1, &attr);
    if (status != status_success) {
        printf("Error was detected during setting message buffer attribute, please check the "
               "arguments\n");
        return;
    }

    mcan_get_default_config(slcan->ptr, &slcan->candev_cfg);
    slcan->candev_cfg.mode = mcan_mode_normal; // Normal mode
    slcan->candev_cfg.enable_canfd = true;
    mcan_get_default_ram_config(slcan->ptr, &slcan->candev_cfg.ram_config, true);
    slcan->uartdev_sn = 1;    // USB VCOM2
    slcan->uartdev_oflag = 0; // uart hardware switch flag
    slcan->uart_rx_len = 0;

    slcan->run_state = 0;
    slcan->run_flag = 0;
    slcan->set_flag = 0;
    slcan->timestamp_isopen = 0;
    slcan->candev_isopen = 0; // cangaroo app switch flag
    mcan_pinmux_init(slcan->candev_sn);
    mcan_channel_init(slcan->candev_sn);
    SLCAN_DEBUG("SLCAN1 Port Init: CAN[%d]<=>VCOM[%d]\r\n", slcan->candev_sn,
              slcan->uartdev_sn);
}
#endif

#if (SCLAN_NUM >= 3)
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
#if defined(HPM_MCAN2)
ATTR_PLACE_AT(".ahb_sram") uint32_t mcan2_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif
#endif
/**
 * @brief       slcan instance initialization
 * @param[in]
 * @return
 * @details
 */
void slcan_port2_init(void) {
    struct slcan_t *slcan = &slcan2;

    /*slcan port 0 initialization */
    slcan->candev_sn = 2; // CANFD Port0
    slcan->ptr = HPM_MCAN2;
    slcan->irq_num = IRQn_MCAN2;
    slcan->candev_oflag = 0; // can hardware switch flag

    clock_add_to_group(clock_can2, 0);
    clock_set_source_divider(clock_can2, clk_src_pll1_clk0, 10);

    /* RAM */
    mcan_msg_buf_attr_t attr = {.ram_base = (uint32_t) mcan2_msg_buf,
                                .ram_size = sizeof(mcan2_msg_buf)};
    hpm_stat_t status = mcan_set_msg_buf_attr(HPM_MCAN2, &attr);
    if (status != status_success) {
        printf("Error was detected during setting message buffer attribute, please check the "
               "arguments\n");
        return;
    }

    mcan_get_default_config(slcan->ptr, &slcan->candev_cfg);
    slcan->candev_cfg.mode = mcan_mode_normal; // Normal mode
    slcan->candev_cfg.enable_canfd = true;
    mcan_get_default_ram_config(slcan->ptr, &slcan->candev_cfg.ram_config, true);
    slcan->uartdev_sn = 2;    // USB VCOM3
    slcan->uartdev_oflag = 0; // uart hardware switch flag
    slcan->uart_rx_len = 0;

    slcan->run_state = 0;
    slcan->run_flag = 0;
    slcan->set_flag = 0;
    slcan->timestamp_isopen = 0;
    slcan->candev_isopen = 0; // cangaroo app switch flag
    mcan_pinmux_init(slcan->candev_sn);
    mcan_channel_init(slcan->candev_sn);
    SLCAN_DEBUG("SLCAN2 Port Init: CAN[%d]<=>VCOM[%d]\r\n", slcan->candev_sn,
              slcan->uartdev_sn);
}
#endif

#if (SCLAN_NUM >= 4)
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
#if defined(HPM_MCAN3)
ATTR_PLACE_AT(".ahb_sram") uint32_t mcan3_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif
#endif
/**
 * @brief       slcan instance initialization
 * @param[in]
 * @return
 * @details
 */
void slcan_port3_init(void) {
    struct slcan_t *slcan = &slcan3;

    /*slcan port 0 initialization */
    slcan->candev_sn = 3; // CANFD Port0
    slcan->ptr = HPM_MCAN3;
    slcan->irq_num = IRQn_MCAN3;
    slcan->candev_oflag = 0; // can hardware switch flag

    clock_add_to_group(clock_can3, 0);
    clock_set_source_divider(clock_can3, clk_src_pll1_clk0, 10);

    /* RAM */
    mcan_msg_buf_attr_t attr = {.ram_base = (uint32_t) mcan3_msg_buf,
                                .ram_size = sizeof(mcan3_msg_buf)};
    hpm_stat_t status = mcan_set_msg_buf_attr(HPM_MCAN3, &attr);
    if (status != status_success) {
        printf("Error was detected during setting message buffer attribute, please check the "
               "arguments\n");
        return;
    }

    mcan_get_default_config(slcan->ptr, &slcan->candev_cfg);
    slcan->candev_cfg.mode = mcan_mode_normal; // Normal mode
    slcan->candev_cfg.enable_canfd = true;
    mcan_get_default_ram_config(slcan->ptr, &slcan->candev_cfg.ram_config, true);
    slcan->uartdev_sn = 3;    // USB VCOM4
    slcan->uartdev_oflag = 0; // uart hardware switch flag
    slcan->uart_rx_len = 0;

    slcan->run_state = 0;
    slcan->run_flag = 0;
    slcan->set_flag = 0;
    slcan->timestamp_isopen = 0;
    slcan->candev_isopen = 0; // cangaroo app switch flag
    mcan_pinmux_init(slcan->candev_sn);
    mcan_channel_init(slcan->candev_sn);
    SLCAN_DEBUG("SLCAN3 Port Init: CAN[%d]<=>VCOM[%d]\r\n", slcan->candev_sn,
              slcan->uartdev_sn);
}

#endif

/**
 * @brief       all slcan instance initialization one time
 * @param[in]
 * @return
 * @details
 */
void slcan_init(void) {
    printf("SLCAN Init: %d ports\r\n", SCLAN_NUM);
#if (SCLAN_NUM >= 1)
    slcan_port0_init();
    printf("SLCAN0 Port Init: CAN[%d]<=>VCOM[%d]\r\n", slcan0.candev_sn,
           slcan0.uartdev_sn);
#endif
#if (SCLAN_NUM >= 2)
    slcan_port1_init();
    printf("SLCAN1 Port Init: CAN[%d]<=>VCOM[%d]\r\n", slcan1.candev_sn,
           slcan1.uartdev_sn);
#endif
#if (SCLAN_NUM >= 3)
    slcan_port2_init();
    printf("SLCAN2 Port Init: CAN[%d]<=>VCOM[%d]\r\n", slcan2.candev_sn,
           slcan2.uartdev_sn);
#endif
#if (SCLAN_NUM >= 4)
    slcan_port3_init();
    printf("SLCAN3 Port Init: CAN[%d]<=>VCOM[%d]\r\n", slcan3.candev_sn,
           slcan3.uartdev_sn);
#endif
}

/**
 * @brief      slcan  timestamp  increase
 * @param[in]
 * @return
 * @details
 */
void slcan_timestamp(void) {

#if (SCLAN_NUM >= 1)
    if (slcan0.timestamp_isopen)
        slcan0.timestamp++;
#endif
#if (SCLAN_NUM >= 2)
    if (slcan1.timestamp_isopen)
        slcan1.timestamp++;
#endif
#if (SCLAN_NUM >= 3)
    if (slcan2.timestamp_isopen)
        slcan2.timestamp++;
#endif
#if (SCLAN_NUM >= 4)
    if (slcan3.timestamp_isopen)
        slcan3.timestamp++;
#endif
}
