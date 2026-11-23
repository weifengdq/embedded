/*
 * j1939_internal.h - 协议栈内部接口（不对外暴露）
 */
#ifndef J1939_INTERNAL_H
#define J1939_INTERNAL_H

#include "app_j1939.h"

/* 32 位毫秒/微秒计数回绕安全的时间比较 */
static inline bool j1939_time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t) (now - deadline) >= 0;
}

/* ---- 传输协议（j1939_tp.c） ---- */
int  j1939_tp_send(j1939_stack_t *s, uint32_t pgn, uint8_t da, uint8_t prio,
                   const void *data, uint16_t len);
void j1939_tp_poll(j1939_stack_t *s);
bool j1939_tp_rx_handle(j1939_stack_t *s, const j1939_frame_t *f); /* true = 已消费 */
void j1939_tp_reset(j1939_stack_t *s);

/* ---- 消息分发（单帧或 TP 重组完成） ---- */
void j1939_deliver(j1939_stack_t *s, uint32_t pgn, uint8_t sa, uint8_t da,
                   const uint8_t *data, uint16_t len);

/* ---- 诊断（j1939_dm.c） ---- */
uint16_t j1939_dm1_build(j1939_stack_t *s, uint8_t *buf);
uint16_t j1939_dm2_build(j1939_stack_t *s, uint8_t *buf);

/* ---- 栈内部 ---- */
void j1939_send_address_claim(j1939_stack_t *s, uint8_t da);

#endif /* J1939_INTERNAL_H */