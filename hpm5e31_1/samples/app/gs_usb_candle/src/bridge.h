/*
 * bridge.h - gs_usb 协议 ↔ MCAN 4 通道桥接（samples/app/gs_usb_candle 内部接口）
 */
#ifndef GS_USB_BRIDGE_H
#define GS_USB_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include "gs_usb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 4 路 MCAN（默认 500k，经典帧；等待主机 MODE START 后按内核位时序重配） */
int gs_bridge_init(void);

/* 主循环：CAN→USB（RX）、USB→CAN（TX）与 TX 回显 */
void gs_bridge_poll(void);

/* 控制请求回调（由 usb_gs.c 的 vendor_handler 调用） */
void gs_bridge_can_store_btim(uint8_t ch, bool data_phase, const struct gs_device_bittiming *bt);
int  gs_bridge_can_mode(uint8_t ch, uint32_t mode, uint32_t flags);
void gs_bridge_can_get_state(uint8_t ch, struct gs_device_state *st);
void gs_bridge_set_berr(uint8_t ch, bool enable);
void gs_bridge_set_identify(uint32_t mode);

/* USB OUT 回调把主机 TX 帧投递到这里（中断上下文，只入队不解析） */
void gs_bridge_tx_from_host(const uint8_t *data, uint32_t len);

/* 时间戳 */
uint32_t gs_bridge_now_us(void);
uint32_t gs_bridge_ts_high(void);

/* 统计 */
void gs_bridge_counters(uint32_t *rx, uint32_t *tx, uint32_t *drop, uint32_t *echo);
bool gs_bridge_channel_started(uint8_t ch);
uint32_t gs_bridge_berr_enable_mask(void);
uint32_t gs_bridge_err_frames(void);           /* 已上报的 CAN_ERR_* 帧数（诊断） */

/* R8: MCAN TX FIFO 满导致的退避次数（背压生效证据；非法帧/丢帧另计 tx_drop） */
uint32_t gs_bridge_tx_retry(void);

#ifdef __cplusplus
}
#endif
#endif /* GS_USB_BRIDGE_H */
