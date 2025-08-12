#include "mcan.h"

#include <stdint.h>

#include "hpm_clock_drv.h"
#include "hpm_mcan_drv.h"

typedef struct mcan_param {
    uint32_t baud;
    uint16_t prescaler;
    uint16_t num_seg1;
    uint16_t num_seg2;
    uint8_t num_sjw;
} mcan_param_t;

typedef struct mcan_baud {
    uint32_t nbaud;
    uint32_t dbaud;
} mcan_baud_t;

// 80M
static mcan_param_t mcan_param[] = {
    {10000, 200, 29, 10, 10}, // 10K
    {20000, 100, 29, 10, 10}, // 20K
    {50000, 40, 29, 10, 10},  // 50K
    {100000, 20, 29, 10, 10}, // 100K
    {125000, 16, 29, 10, 10}, // 125K
    {250000, 8, 29, 10, 10},  // 250K
    {500000, 4, 29, 10, 10},  // 500K
    {800000, 10, 7, 2, 2},    // 800K
    {1000000, 2, 29, 10, 10}, // 1M
    {2000000, 1, 29, 10, 10}, // 2M
    {4000000, 1, 14, 5, 5},   // 4M
    {5000000, 1, 11, 4, 4},   // 5M
    {8000000, 1, 7, 2, 2},    // 8M
    {10000000, 1, 5, 2, 2}    // 10M
};

static mcan_baud_t mcan_baud[M_CAN_NUM] = {
    {500000, 2000000}, // M_CAN0
    {500000, 2000000}, // M_CAN1
    {500000, 2000000}, // M_CAN2
    {500000, 2000000}  // M_CAN3
};

static MCAN_Type *mcan[M_CAN_NUM] = {HPM_MCAN0, HPM_MCAN1, HPM_MCAN2, HPM_MCAN3};
static int mcan_irq_num[M_CAN_NUM] = {IRQn_MCAN0, IRQn_MCAN1, IRQn_MCAN2, IRQn_MCAN3};
static clock_name_t mcan_clock[M_CAN_NUM] = {clock_can0, clock_can1, clock_can2, clock_can3};

ATTR_PLACE_AT(".ahb_sram") uint32_t mcan_msg_buf[M_CAN_NUM][MCAN_MSG_BUF_SIZE_IN_WORDS];

static mcan_param_t *get_mcan_param(uint32_t baud)
{
    for (int i = 0; i < sizeof(mcan_param) / sizeof(mcan_param[0]); i++) {
        if (mcan_param[i].baud == baud) {
            return &mcan_param[i];
        }
    }
    return NULL; // Not found
}

int mcan_set_nbaud(mcan_channel_t channel, uint32_t nbaud)
{
    if (channel < M_CAN_NUM) {
        mcan_baud[channel].nbaud = nbaud;
        return 0;
    }
    return -1; // Invalid channel
}

int mcan_set_dbaud(mcan_channel_t channel, uint32_t dbaud)
{
    if (channel < M_CAN_NUM) {
        mcan_baud[channel].dbaud = dbaud;
        return 0;
    }
    return -1; // Invalid channel
}

int mcan_close(mcan_channel_t channel)
{
    mcan_deinit(mcan[channel]);
    mcan_disable_interrupts(mcan[channel], MCAN_EVENT_RECEIVE);
    intc_m_disable_irq(mcan_irq_num[channel]);
    return 0;
}

int mcan_open(mcan_channel_t channel)
{
    // 80M Clock
    clock_add_to_group(mcan_clock[channel], 0);
    clock_set_source_divider(mcan_clock[channel], clk_src_pll1_clk0, 10);
    uint32_t freq = clock_get_frequency(mcan_clock[channel]);
    MCAN_DEBUG("MCAN%d clock frequency: %d Hz\n", channel, freq);

    // RAM
    mcan_msg_buf_attr_t attr = {.ram_base = (uint32_t) mcan_msg_buf[channel],
                                .ram_size = sizeof(mcan_msg_buf[channel])};
    hpm_stat_t status = mcan_set_msg_buf_attr(mcan[channel], &attr);
    if (status != status_success) {
        MCAN_DEBUG(
            "Error setting message buffer attribute for MCAN%d, error code: %d\n", channel, status);
        return -1;
    }

    // bit timing nparam
    mcan_param_t *nparam = get_mcan_param(mcan_baud[channel].nbaud);
    if (nparam == NULL) {
        MCAN_DEBUG("Invalid baud rate %d for MCAN%d\n", mcan_baud[channel].nbaud, channel);
        return -2;
    }
    mcan_bit_timing_param_t ntiming_param = {.prescaler = nparam->prescaler,
                                             .num_seg1 = nparam->num_seg1,
                                             .num_seg2 = nparam->num_seg2,
                                             .num_sjw = nparam->num_sjw,
                                             .enable_tdc = true};
    // bit timing dparam
    mcan_param_t *dparam = get_mcan_param(mcan_baud[channel].dbaud);
    if (dparam == NULL) {
        MCAN_DEBUG("Invalid baud rate %d for MCAN%d\n", mcan_baud[channel].dbaud, channel);
        return -3;
    }
    mcan_bit_timing_param_t dtiming_param = {.prescaler = dparam->prescaler,
                                             .num_seg1 = dparam->num_seg1,
                                             .num_seg2 = dparam->num_seg2,
                                             .num_sjw = dparam->num_sjw,
                                             .enable_tdc = true};

    // tdc
    mcan_tdc_config_t tdc_config = {
        .ssp_offset = dtiming_param.num_seg1 + 1,          // TDC SSP offset
        .filter_window_length = dtiming_param.num_seg1 + 1 // TDC filter window length
    };

    mcan_config_t config;
    mcan_get_default_config(mcan[channel], &config);
    config.enable_canfd = true;
    config.use_lowlevel_timing_setting = true;
    config.can_timing = ntiming_param;
    config.canfd_timing = dtiming_param;
    config.enable_tdc = true;
    config.tdc_config = tdc_config;
    // TODO: Filter use default
    config.interrupt_mask = MCAN_EVENT_RECEIVE | MCAN_EVENT_TRANSMIT | MCAN_EVENT_ERROR;
    config.txbuf_trans_interrupt_mask = ~0UL;

    // TODO: msg ram split
    mcan_get_default_ram_config(mcan[channel], &config.ram_config, true);

    status = mcan_init(mcan[channel], &config, freq);
    if (status != status_success) {
        MCAN_DEBUG("MCAN%d initialization failed, error code: %d\n", channel, status);
        return -4;
    }
    mcan_enable_interrupts(mcan[channel], config.interrupt_mask);
    // TODO: priority
    intc_m_enable_irq_with_priority(mcan_irq_num[channel], 1);

    return 0;
}

void mcan_isr(MCAN_Type *ptr)
{
    uint32_t flags = mcan_get_interrupt_flags(ptr);
    mcan_clear_interrupt_flags(ptr, flags);
}

SDK_DECLARE_EXT_ISR_M(IRQn_MCAN0, mcan0_isr);
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN1, mcan1_isr);
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN2, mcan2_isr);
SDK_DECLARE_EXT_ISR_M(IRQn_MCAN3, mcan3_isr);

void mcan0_isr(void)
{
    mcan_isr(HPM_MCAN0);
}

void mcan1_isr(void)
{
    mcan_isr(HPM_MCAN1);
}

void mcan2_isr(void)
{
    mcan_isr(HPM_MCAN2);
}

void mcan3_isr(void)
{
    mcan_isr(HPM_MCAN3);
}

// typedef struct mcan_tx_message_struct {
//     union {
//         struct {
//             uint32_t ext_id: 29;                        /*!< Extended CAN Identifier */
//             uint32_t rtr: 1;                            /*!< Remote Transmission Request */
//             uint32_t use_ext_id: 1;                     /*!< Extended Identifier */
//             uint32_t error_state_indicator: 1;          /*!< Error State Indicator */
//         };
//         struct {
//             uint32_t : 18;
//             uint32_t std_id: 11;                        /*!< Standard CAN Identifier */
//             uint32_t : 3;
//         };
//     };
//     struct {
//         uint32_t : 8;
//         uint32_t message_marker_h: 8;                   /*!< Message Marker[15:8] */
//         uint32_t dlc: 4;                                /*!< Data Length Code */
//         uint32_t bitrate_switch: 1;                     /*!< Bit Rate Switch */
//         uint32_t canfd_frame: 1;                        /*!< CANFD frame */
//         uint32_t timestamp_capture_enable: 1;           /*!< Timestamp Capture Enable for TSU */
//         uint32_t event_fifo_control: 1;                 /*!< Event FIFO control */
//         uint32_t message_marker_l: 8;                   /*!< Message Marker[7:0] */
//     };
//     union {
//         uint8_t data_8[64];                             /*!< Data buffer as byte array */
//         uint32_t data_32[16];                           /*!< Data buffer as word array */
//     };
// } mcan_tx_frame_t;

// struct canfd_frame {
// 	canid_t can_id;  /* 32 bit CAN_ID + EFF/RTR/ERR flags */
// 	uint8_t    len;     /* frame payload length in byte */
// 	uint8_t    flags;   /* additional flags for CAN FD */
// 	uint8_t    __res0;  /* reserved / padding */
// 	uint8_t    __res1;  /* reserved / padding */
// 	uint8_t    data[CANFD_MAX_DLEN];
// };

static uint32_t len2dlc(uint8_t len)
{
    if (len <= 8) {
        return len; // Standard DLC
    } else if (len <= 12) {
        return 9;
    } else if (len <= 16) {
        return 10;
    } else if (len <= 20) {
        return 11;
    } else if (len <= 24) {
        return 12;
    } else if (len <= 32) {
        return 13;
    } else if (len <= 48) {
        return 14;
    } else if (len <= 64) {
        return 15;
    }
    return 15;
}

int mcan_send(mcan_channel_t channel, const struct canfd_frame *frame)
{
    if (channel >= M_CAN_NUM) {
        MCAN_DEBUG("Invalid channel %d\n", channel);
        return -1; // Invalid channel
    }

    mcan_tx_frame_t tx_frame;
    memset(&tx_frame, 0, sizeof(tx_frame));

    // Set CAN ID and flags
    tx_frame.ext_id = frame->can_id & 0x1FFFFFFF; // Mask to 29 bits
    tx_frame.rtr = (frame->can_id & CAN_RTR_FLAG) ? 1 : 0;
    tx_frame.use_ext_id = (frame->can_id & CAN_EFF_FLAG) ? 1 : 0;

    // Set DLC and data
    tx_frame.dlc = len2dlc(frame->len);
    if (frame->len > CANFD_MAX_DLEN) {
        MCAN_DEBUG("Invalid Length: %d\n", frame->len);
        return -2; // Invalid Length
    }

    memcpy(tx_frame.data_8, frame->data, frame->len);

    // Send the frame
    hpm_stat_t status = mcan_transmit_blocking(mcan[channel], &tx_frame);
    if (status != status_success) {
        MCAN_DEBUG("Failed to send message on channel %d, error code: %d\n", channel, status);
        return -3; // Send failed
    }

    return 0; // Success
}