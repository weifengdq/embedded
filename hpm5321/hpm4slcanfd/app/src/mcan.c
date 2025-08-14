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

// 80M, 优先采样点 0.8, 次之 0.75
static mcan_param_t mcan_param[] = {
    {10000, 200, 31, 8, 8}, // 10K, 0.8
    {20000, 100, 31, 8, 8}, // 20K, 0.8
    {50000, 40, 31, 8, 8},  // 50K, 0.8
    {100000, 20, 31, 8, 8}, // 100K, 0.8
    {125000, 16, 31, 8, 8}, // 125K, 0.8
    {250000, 8, 31, 8, 8},  // 250K, 0.8
    // {500000, 4, 31, 8, 8},  // 500K, 0.8
    {500000, 8, 15, 4, 4},  // 500K, 0.8
    {800000, 5, 15, 4, 4},  // 800K, 0.8
    {1000000, 2, 31, 8, 8}, // 1M, 0.8
    // {2000000, 1, 31, 8, 8}, // 2M, 0.8
    {2000000, 2, 15, 4, 4}, // 2M, 0.8
    {4000000, 1, 15, 4, 4}, // 4M, 0.8
    {5000000, 1, 11, 4, 4}, // 5M, 0.75
    {8000000, 1, 7, 2, 2},  // 8M, 0.8
    {10000000, 1, 5, 2, 2}  // 10M, 0.75
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

#define MCAN_MSG_BUF_SIZE_IN_WORDS_CUSTOM (1024U)
ATTR_PLACE_AT(".ahb_sram") uint32_t mcan_msg_buf[M_CAN_NUM][MCAN_MSG_BUF_SIZE_IN_WORDS_CUSTOM];

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

    // AHB MSG RAM
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
    // config.disable_auto_retransmission = true;
    config.enable_canfd = true;
    config.use_lowlevel_timing_setting = true;
    config.can_timing = ntiming_param;
    config.canfd_timing = dtiming_param;
    config.enable_tdc = true;
    config.tdc_config = tdc_config;
    // TODO: Filter use default
    config.interrupt_mask = MCAN_INT_RXFIFO0_NEW_MSG | MCAN_INT_RXFIFO0_WMK_REACHED |
                            MCAN_EVENT_TRANSMIT | MCAN_EVENT_ERROR;
    // config.txbuf_trans_interrupt_mask = ~0UL;
    config.txbuf_trans_interrupt_mask = MCAN_EVENT_TRANSMIT;

    // TODO: msg ram split
    // mcan_get_default_ram_config(mcan[channel], &config.ram_config, true);
    // MSG RAM SPLIT
    const uint32_t std_filter_elem_count = 128U;
    const uint32_t ext_filter_elem_count = 64U;
    const uint32_t rxfifo0_elem_count = 10U;
    const uint32_t txbuf_elem_count = 32U;

    /* word counts */
    const uint32_t words_std =
        std_filter_elem_count * (MCAN_FILTER_ELEM_STD_ID_SIZE / 4U); /* 1 word each */
    const uint32_t words_ext =
        ext_filter_elem_count * (MCAN_FILTER_ELEM_EXT_ID_SIZE / 4U); /* 2 words each */
    const uint32_t words_rxfifo0 =
        rxfifo0_elem_count * 18U; /* 64B data+8B header = 72B = 18 words */
    const uint32_t words_txbuf = txbuf_elem_count * 18U;

    const uint32_t total_words = words_std + words_ext + words_rxfifo0 + words_txbuf;

    if (total_words > MCAN_MSG_BUF_SIZE_IN_WORDS_CUSTOM) {
        /* not enough per-instance RAM */
        printf("MCAN MessageRAM per-instance words insufficient: need %u, have %u\n",
               total_words,
               MCAN_MSG_BUF_SIZE_IN_WORDS_CUSTOM);
        return false;
    }

    mcan_ram_config_t *ram_cfg = &config.ram_config;

    /* fill ram_cfg */
    (void) memset(ram_cfg, 0, sizeof(*ram_cfg));
    ram_cfg->enable_std_filter = true;
    ram_cfg->std_filter_elem_count = (uint8_t) std_filter_elem_count;
    ram_cfg->enable_ext_filter = true;
    ram_cfg->ext_filter_elem_count = (uint8_t) ext_filter_elem_count;
    // mcan_get_default_config 中已有:
    //      1 std id filter id:mask=0:0x7FF
    //      1 ext id filter id:mask=0:0x1FFFFFFF

    /* RXFIFO0 */
    ram_cfg->rxfifos[0].enable = 1U;
    ram_cfg->rxfifos[0].elem_count = (uint32_t) rxfifo0_elem_count;
    ram_cfg->rxfifos[0].watermark = 7U; /* half of 14 */
    ram_cfg->rxfifos[0].operation_mode =
        MCAN_FIFO_OPERATION_MODE_OVERWRITE; /* overwrite when full */
    ram_cfg->rxfifos[0].data_field_size = MCAN_DATA_FIELD_SIZE_64BYTES;

    /* RXFIFO1 disabled */
    ram_cfg->rxfifos[1].enable = 0U;

    /* RXBUF disabled */
    ram_cfg->enable_rxbuf = false;

    /* TXBUF */
    ram_cfg->enable_txbuf = true;
    ram_cfg->txbuf_data_field_size = MCAN_DATA_FIELD_SIZE_64BYTES;
    ram_cfg->txbuf_dedicated_txbuf_elem_count = 0U; /* dedicated = 0 */
    ram_cfg->txbuf_fifo_or_queue_elem_count = (uint8_t) txbuf_elem_count;
    ram_cfg->txfifo_or_txqueue_mode = MCAN_TXBUF_OPERATION_MODE_FIFO;

    /* TX Event FIFO disabled */
    ram_cfg->enable_tx_evt_fifo = false;

    // INIT
    status = mcan_init(mcan[channel], &config, freq);
    if (status != status_success) {
        MCAN_DEBUG("MCAN%d initialization failed, error code: %d\n", channel, status);
        return -4;
    }
    mcan_enable_interrupts(mcan[channel], config.interrupt_mask);
    // TODO: priority
    intc_m_enable_irq_with_priority(mcan_irq_num[channel], 1 + channel);

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
    tx_frame.rtr = (frame->can_id & CAN_RTR_FLAG) ? 1 : 0;
    tx_frame.use_ext_id = (frame->can_id & CAN_EFF_FLAG) ? 1 : 0;
    if (tx_frame.use_ext_id) {
        tx_frame.ext_id = frame->can_id & 0x1FFFFFFF; // Mask to 29 bits
    } else {
        tx_frame.std_id = frame->can_id & 0x7FF; // Mask to 11 bits
    }

    // fdf brs
    tx_frame.bitrate_switch = (frame->flags & CANFD_BRS) ? 1 : 0;
    tx_frame.canfd_frame = ((frame->flags & CANFD_FDF) || tx_frame.bitrate_switch) ? 1 : 0;

    // Set DLC and data
    tx_frame.dlc = len2dlc(frame->len);
    if (frame->len > CANFD_MAX_DLEN) {
        MCAN_DEBUG("Invalid Length: %d\n", frame->len);
        return -2; // Invalid Length
    }

    uint32_t msg_len = mcan_get_message_size_from_dlc(tx_frame.dlc);
    memcpy(tx_frame.data_8, frame->data, msg_len);

    // Send the frame, Blocking mode
    // hpm_stat_t status = mcan_transmit_blocking(mcan[channel], &tx_frame);
    // Send the frame, Non-blocking mode
    if (mcan_is_txfifo_full(mcan[channel])) {
        printf("TXFIFO full on channel %d\n", channel);
        return -3; // Send failed
    }
    hpm_stat_t tx_ret = mcan_write_txfifo(mcan[channel], &tx_frame);
    if (tx_ret != status_success) {
        MCAN_DEBUG("Failed to send message on channel %d, error code: %d\n", channel, tx_ret);
        return -4; // Write failed
    }
    uint32_t put_index = mcan_get_txfifo_put_index(mcan[channel]);
    mcan_send_add_request(mcan[channel], put_index);

    return 0; // Success
}