/**
 * CANopenNode CO_driver port for HPM5E31 / HPMicro MCAN (classic CAN).
 *
 * 与 components/app_can 的关系：本文件当前直接调用 hpm_mcan_drv + 板级 board_init_can()，
 * 因为 app_can 组件尚未发布（见协作约定 §4.1）。函数边界只用到
 * mcan_init / mcan_read_rxfifo / mcan_transmit_via_txfifo_nonblocking 三个 SDK API，
 * 后续切换成 app_can_init/app_can_send/app_can_recv 时只改本文件。
 *
 * @file        CO_driver.c
 * @copyright   Apache-2.0 (CANopenNode) / hpm5e31_1 项目组
 */
#include <string.h>

#include "301/CO_driver.h"

#include "hpm_mcan_drv.h"
#include "board.h"
#include "ext/board_can.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CO_HPM_RX_FIFO 0U

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_co_msg_buf[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif
#define CO_HPM_TQ_COUNT 16U /**< 每 bit 16 个时间份额，采样点 87.5% */

/** 匹配全部标准帧的经典过滤器（ID/mask = 0），统一进 RX FIFO0，后续软件分发。
 *  CANopen 的 PDO COB-ID 可在运行时改写，硬件过滤表无法覆盖，因此用软件过滤。 */
static const mcan_filter_elem_t s_co_accept_all_std = {
    MCAN_FILTER_TYPE_CLASSIC_FILTER,
    MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO0_IF_MATCH,
    0U,
    0U,
    .filter_id = 0U,
    .filter_mask = 0U,
};

static hpm_stat_t co_hpm_can_hw_init(MCAN_Type* base, uint16_t bitrate_kbps) {
    mcan_config_t cfg;
    uint32_t clk;
    uint32_t presc;

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    mcan_msg_buf_attr_t attr = {(uint32_t) s_co_msg_buf, (uint32_t) sizeof(s_co_msg_buf)};
    mcan_set_msg_buf_attr(base, &attr);
#endif
    mcan_get_default_config(base, &cfg);
    cfg.mode = mcan_mode_normal;
    cfg.enable_canfd = false;
    cfg.use_lowlevel_timing_setting = true;
    /* 16 TQ、采样点 87.5%：presc = fclk / (bitrate * 16)。80 MHz 时 500 kbit/s -> presc=10 */
    clk = board_init_can_clock(base);
    presc = (bitrate_kbps == 0U) ? 10U : (clk / ((uint32_t) bitrate_kbps * 1000U * CO_HPM_TQ_COUNT));
    if (presc == 0U) {
        presc = 1U;
    }
    cfg.can_timing.prescaler = (uint16_t) presc;
    cfg.can_timing.num_seg1 = 13U;
    cfg.can_timing.num_seg2 = 2U;
    cfg.can_timing.num_sjw = 2U;

    /* 过滤器：一个经典过滤器接收全部标准帧 -> FIFO0；扩展帧/远程帧拒收 */
    cfg.all_filters_config.std_id_filter_list.filter_elem_list = &s_co_accept_all_std;
    cfg.all_filters_config.std_id_filter_list.mcan_filter_elem_count = 1U;
    cfg.all_filters_config.ext_id_filter_list.filter_elem_list = NULL;
    cfg.all_filters_config.ext_id_filter_list.mcan_filter_elem_count = 0U;
    cfg.all_filters_config.global_filter_config.accept_non_matching_std_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;
    cfg.all_filters_config.global_filter_config.accept_non_matching_ext_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_REJECT;
    cfg.all_filters_config.global_filter_config.reject_remote_std_frame = true;
    cfg.all_filters_config.global_filter_config.reject_remote_ext_frame = true;

    board_init_can(base);
    mcan_deinit(base);
    return mcan_init(base, &cfg, clk);
}

/** 把一个 CO_CANtx_t 尝试塞进 MCAN TX FIFO（非阻塞）。 */
static hpm_stat_t co_hpm_tx(MCAN_Type* base, CO_CANtx_t* buffer) {
    mcan_tx_frame_t tx;
    uint8_t i;

    (void) memset(&tx, 0, sizeof(tx));
    tx.std_id = (uint16_t) (buffer->ident & 0x7FFU);
    tx.dlc = (uint8_t) ((buffer->DLC > 8U) ? 8U : buffer->DLC);
    tx.use_ext_id = 0U;
    tx.rtr = 0U;
    tx.canfd_frame = 0U;
    tx.bitrate_switch = 0U;
    for (i = 0U; i < 8U; i++) {
        tx.data_8[i] = buffer->data[i];
    }
    return mcan_transmit_via_txfifo_nonblocking(base, &tx, NULL);
}

void CO_CANsetConfigurationMode(void* CANptr) {
    if (CANptr != NULL) {
        mcan_deinit((MCAN_Type*) CANptr);
    }
}

void CO_CANsetNormalMode(CO_CANmodule_t* CANmodule) {
    if (CANmodule != NULL) {
        CANmodule->CANnormal = true;
        CANmodule->firstCANtxMessage = true;
    }
}

CO_ReturnError_t CO_CANmodule_init(CO_CANmodule_t* CANmodule, void* CANptr, CO_CANrx_t rxArray[], uint16_t rxSize,
                                   CO_CANtx_t txArray[], uint16_t txSize, uint16_t CANbitRate) {
    uint16_t i;

    if ((CANmodule == NULL) || (CANptr == NULL) || (rxArray == NULL) || (txArray == NULL) || (rxSize == 0U)
        || (txSize == 0U)) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }
    CANmodule->CANptr = CANptr;
    CANmodule->rxArray = rxArray;
    CANmodule->rxSize = rxSize;
    CANmodule->txArray = txArray;
    CANmodule->txSize = txSize;
    CANmodule->CANerrorStatus = 0U;
    CANmodule->CANnormal = false;
    CANmodule->useCANrxFilters = false;
    CANmodule->bufferInhibitFlag = false;
    CANmodule->firstCANtxMessage = true;
    CANmodule->CANtxCount = 0U;
    CANmodule->errOld = 0U;

    for (i = 0U; i < rxSize; i++) {
        rxArray[i].ident = 0U;
        rxArray[i].mask = 0x7FFU;
        rxArray[i].object = NULL;
        rxArray[i].CANrx_callback = NULL;
    }
    for (i = 0U; i < txSize; i++) {
        (void) memset(&txArray[i], 0, sizeof(txArray[i]));
    }
    if (co_hpm_can_hw_init((MCAN_Type*) CANptr, CANbitRate) != status_success) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }
    return CO_ERROR_NO;
}

void CO_CANmodule_disable(CO_CANmodule_t* CANmodule) {
    if ((CANmodule == NULL) || (CANmodule->CANptr == NULL)) {
        return;
    }
    CANmodule->CANnormal = false;
    mcan_deinit((MCAN_Type*) CANmodule->CANptr);
}

CO_ReturnError_t CO_CANrxBufferInit(CO_CANmodule_t* CANmodule, uint16_t index, uint16_t ident, uint16_t mask,
                                    bool_t rtr, void* object, void (*CANrx_callback)(void* object, void* message)) {
    CO_CANrx_t* buffer;

    if ((CANmodule == NULL) || (object == NULL) || (CANrx_callback == NULL) || (index >= CANmodule->rxSize)) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }
    buffer = &CANmodule->rxArray[index];
    buffer->ident = ident;
    buffer->mask = mask;
    buffer->object = object;
    buffer->CANrx_callback = CANrx_callback;
    (void) rtr; /* CANopen 不用远程帧；MCAN 过滤器已拒收 */
    return CO_ERROR_NO;
}

CO_CANtx_t* CO_CANtxBufferInit(CO_CANmodule_t* CANmodule, uint16_t index, uint16_t ident, bool_t rtr, uint8_t noOfBytes,
                               bool_t syncFlag) {
    CO_CANtx_t* buffer;

    if ((CANmodule == NULL) || (index >= CANmodule->txSize) || (noOfBytes > 8U) || (rtr != 0U)) {
        return NULL;
    }
    buffer = &CANmodule->txArray[index];
    buffer->ident = (uint32_t) ident;
    buffer->DLC = noOfBytes;
    buffer->bufferFull = false;
    buffer->syncFlag = syncFlag;
    return buffer;
}

CO_ReturnError_t CO_CANsend(CO_CANmodule_t* CANmodule, CO_CANtx_t* buffer) {
    if ((CANmodule == NULL) || (buffer == NULL)) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }
    if (!CANmodule->CANnormal) {
        buffer->bufferFull = false;
        return CO_ERROR_TX_UNCONFIGURED;
    }
    if (buffer->bufferFull) {
        /* 上一帧还没进 MCAN，拒绝覆盖（栈会重试/报 EMCY） */
        return CO_ERROR_TX_OVERFLOW;
    }
    if (co_hpm_tx((MCAN_Type*) CANmodule->CANptr, buffer) == status_success) {
        if (CANmodule->firstCANtxMessage) {
            CANmodule->firstCANtxMessage = false;
        }
        return CO_ERROR_NO;
    }
    buffer->bufferFull = true;
    CANmodule->CANtxCount++;
    return CO_ERROR_TX_OVERFLOW;
}

void CO_CANclearPendingSyncPDOs(CO_CANmodule_t* CANmodule) {
    uint16_t i;

    if (CANmodule == NULL) {
        return;
    }
    for (i = 0U; i < CANmodule->txSize; i++) {
        CO_CANtx_t* buffer = &CANmodule->txArray[i];
        if (buffer->bufferFull && buffer->syncFlag) {
            buffer->bufferFull = false;
            if (CANmodule->CANtxCount > 0U) {
                CANmodule->CANtxCount--;
            }
        }
    }
    CANmodule->bufferInhibitFlag = false;
}

void CO_CANmodule_process(CO_CANmodule_t* CANmodule) {
    MCAN_Type* base;
    uint16_t status = 0U;

    if ((CANmodule == NULL) || (CANmodule->CANptr == NULL)) {
        return;
    }
    base = (MCAN_Type*) CANmodule->CANptr;
    if (mcan_is_in_busoff_state(base)) {
        status |= CO_CAN_ERRTX_BUS_OFF;
        CANmodule->CANnormal = false; /* bus-off 后 MCAN 需重新初始化 */
    }
    if (mcan_is_in_err_passive_state(base)) {
        status |= CO_CAN_ERRTX_PASSIVE | CO_CAN_ERRRX_PASSIVE;
    }
    if (mcan_is_in_error_warning_state(base)) {
        status |= CO_CAN_ERRTX_WARNING | CO_CAN_ERRRX_WARNING;
    }
    CANmodule->CANerrorStatus = status;
}

void CO_CANpoll_rx(CO_CANmodule_t* CANmodule) {
    MCAN_Type* base;
    mcan_rx_message_t rx;
    CO_CANrxMsg_t msg;
    uint16_t i;

    if ((CANmodule == NULL) || (CANmodule->CANptr == NULL)) {
        return;
    }
    base = (MCAN_Type*) CANmodule->CANptr;
    while (mcan_read_rxfifo(base, CO_HPM_RX_FIFO, &rx) == status_success) {
        if ((rx.use_ext_id != 0U) || (rx.canfd_frame != 0U)) {
            continue; /* CANopen 只用 11 位经典帧 */
        }
        msg.ident = (uint16_t) (rx.std_id & 0x7FFU);
        msg.DLC = (uint8_t) ((rx.dlc > 8U) ? 8U : rx.dlc);
        for (i = 0U; i < 8U; i++) {
            msg.data[i] = rx.data_8[i];
        }
        /* 软件过滤：低索引优先，命中第一个即分发（与 CO_driver.h 约定一致） */
        for (i = 0U; i < CANmodule->rxSize; i++) {
            CO_CANrx_t* buffer = &CANmodule->rxArray[i];
            if ((buffer->CANrx_callback != NULL) && (((msg.ident ^ buffer->ident) & buffer->mask) == 0U)) {
                buffer->CANrx_callback(buffer->object, &msg);
                break;
            }
        }
    }
}

void CO_CANpoll_tx(CO_CANmodule_t* CANmodule) {
    MCAN_Type* base;
    uint16_t i;

    if ((CANmodule == NULL) || (CANmodule->CANptr == NULL) || (CANmodule->CANtxCount == 0U)) {
        return;
    }
    base = (MCAN_Type*) CANmodule->CANptr;
    for (i = 0U; i < CANmodule->txSize; i++) {
        CO_CANtx_t* buffer = &CANmodule->txArray[i];
        if (buffer->bufferFull) {
            if (co_hpm_tx(base, buffer) != status_success) {
                break; /* TX FIFO 仍满，下一轮再试 */
            }
            buffer->bufferFull = false;
            if (CANmodule->CANtxCount > 0U) {
                CANmodule->CANtxCount--;
            }
        }
    }
}

#ifdef __cplusplus
}
#endif
