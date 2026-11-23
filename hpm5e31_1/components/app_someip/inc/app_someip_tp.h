/*
 * app_someip_tp.h —— SOME/IP-TP（Transport Protocol）分段与重组
 *
 * SOME/IP-TP 用于 UDP 载荷超过 MTU（约 1400 B）的场景：
 *   Message Type 的 bit5（0x20）表示"这是 TP 段"；
 *   16 B SOME/IP 头之后紧跟 4 B TP 头：Offset(28 bit，单位 16 B) | Reserved(3) | More(1)；
 *   其后是最多 1392 B 数据（1392 = 87 x 16，保证非末段 16 B 对齐）。
 *
 * 本实现只支持**按序重组**（域控里 SD/RPC 都是有线链路、基本不乱序），
 * 乱序/重叠段直接丢弃并计数（stats->tp_drop）。
 */
#ifndef APP_SOMEIP_TP_H
#define APP_SOMEIP_TP_H

#include <stdint.h>
#include <stdbool.h>
#include "app_someip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct someip_tp_reasm {
    uint8_t  buf[SOMEIP_MAX_PAYLOAD];   /* 重组缓冲（4 KB 逻辑报文） */
    uint16_t total_len;                 /* 已完成字节数 */
    uint16_t expect_off;                /* 下一个期望的 offset（字节） */
    uint16_t service_id;
    uint16_t method_id;
    uint16_t client_id;
    uint16_t session_id;
    uint8_t  iface_version;
    uint8_t  msg_type;                  /* 去掉 TP 位后的原始类型 */
    uint8_t  return_code;
    bool     active;
    uint32_t last_ms;
    uint32_t last_complete_ms;          /* 最近一次完整报文的完成时刻（用于统计重组耗时） */
} someip_tp_reasm_t;

void app_someip_tp_reasm_reset(someip_tp_reasm_t *r);

/* 喂一个 TP 段；返回 >0 = 完整报文长度（r->buf 有效），0 = 还需要下一段，<0 = 出错 */
int app_someip_tp_feed(someip_tp_reasm_t *r, const someip_msg_t *m, uint32_t now_ms);

/* 把 <=SOMEIP_MAX_PAYLOAD 的逻辑报文按 1392 B 分段发送；返回发送段数，<0 出错 */
int app_someip_tp_send(app_someip_t *s, const uint8_t dst_ip[4], uint16_t dst_port,
                       uint16_t method_id, uint16_t client_id, uint16_t session_id,
                       uint8_t msg_type, uint8_t return_code,
                       const uint8_t *payload, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* APP_SOMEIP_TP_H */
