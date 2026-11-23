/*
 * app_someip_tp.c —— SOME/IP-TP 分段的发送与重组
 *
 * TP 头（4 B，大端 u32）：bit31..4 = Offset（单位 16 B），bit3..1 = Reserved，bit0 = More Segments。
 * 规则：
 *   - 非末段的载荷长度必须是 16 的整数倍（1392 = 87 x 16 ✓）；
 *   - 首段 Offset = 0，末段 More = 0；
 *   - 同一逻辑报文的所有分段 Service/Method/Client/Session/Interface Version 必须一致。
 */
#include <string.h>
#include <stdio.h>
#include "app_someip.h"
#include "app_someip_wire.h"
#include "app_someip_tp.h"

void app_someip_tp_reasm_reset(someip_tp_reasm_t *r)
{
    if (r == NULL) {
        return;
    }
    memset(r, 0, sizeof(*r));
}

int app_someip_tp_feed(someip_tp_reasm_t *r, const someip_msg_t *m, uint32_t now_ms)
{
    if ((r == NULL) || (m == NULL) || (m->payload_len < SOMEIP_TP_HEADER_LEN)) {
        return -1;
    }
    uint32_t hdr = someip_get_u32(m->payload);
    uint32_t offset = (hdr >> 4) * 16U;
    bool more = (hdr & 0x01U) != 0U;
    const uint8_t *data = m->payload + SOMEIP_TP_HEADER_LEN;
    uint16_t dlen = (uint16_t)(m->payload_len - SOMEIP_TP_HEADER_LEN);

    if (offset == 0U) {
        /* 新报文的首段 */
        uint16_t service = r->service_id;
        memset(r, 0, sizeof(*r));
        (void)service;
        r->active = true;
        r->service_id = m->service_id;
        r->method_id = m->method_id;
        r->client_id = m->client_id;
        r->session_id = m->session_id;
        r->iface_version = m->interface_version;
        r->return_code = m->return_code;
        r->msg_type = (uint8_t)(m->message_type & (uint8_t)~SOMEIP_TP_FLAG);
        r->last_ms = now_ms;
    }

    if (!r->active) {
        return -2;   /* 从中间段开始（首段丢了），丢弃 */
    }
    if (offset != r->expect_off) {
        app_someip_tp_reasm_reset(r);
        return -3;   /* 乱序/重叠，丢弃整个报文 */
    }
    if (((uint32_t)offset + (uint32_t)dlen) > SOMEIP_MAX_PAYLOAD) {
        app_someip_tp_reasm_reset(r);
        return -4;   /* 超出本实现缓冲上限（4 KB），丢弃 */
    }
    /* 一致性校验：同一逻辑报文的所有分段头字段必须相同 */
    if ((m->service_id != r->service_id) || (m->method_id != r->method_id) ||
        (m->client_id != r->client_id) || (m->session_id != r->session_id)) {
        app_someip_tp_reasm_reset(r);
        return -5;
    }

    memcpy(&r->buf[offset], data, dlen);
    r->total_len = (uint16_t)(offset + dlen);
    r->expect_off = r->total_len;
    r->last_ms = now_ms;

    if (more) {
        return 0;
    }
    /* 末段到齐 */
    uint16_t total = r->total_len;
    r->active = false;
    r->last_complete_ms = now_ms;
    return (int)total;
}

/* 段间保护间隔：约 us 微秒的忙等（480 MHz 下按每次循环 4 个周期保守估计） */
static void tp_gap_delay(uint32_t us)
{
    volatile uint32_t n = us * 120U;
    while (n != 0U) {
        n--;
    }
}

int app_someip_tp_send(app_someip_t *s, const uint8_t dst_ip[4], uint16_t dst_port,
                       uint16_t method_id, uint16_t client_id, uint16_t session_id,
                       uint8_t msg_type, uint8_t return_code,
                       const uint8_t *payload, uint16_t len)
{
    int segs = 0;
    uint16_t off = 0U;

    if ((s == NULL) || (s->cfg.send == NULL)) {
        return -1;
    }
    if (len == 0U) {
        return 0;
    }
    while (off < len) {
        uint16_t seg = (uint16_t)((len - off) > SOMEIP_TP_SEG_SIZE ? SOMEIP_TP_SEG_SIZE
                                                                  : (len - off));
        bool more = (uint16_t)(off + seg) < len;
        uint16_t n = app_someip_encode_header(s->seg_buf, sizeof(s->seg_buf),
                                              s->cfg.service_id, method_id, client_id, session_id,
                                              s->cfg.interface_version,
                                              (uint8_t)(msg_type | SOMEIP_TP_FLAG),
                                              return_code,
                                              (uint16_t)(SOMEIP_TP_HEADER_LEN + seg));
        uint32_t tp_hdr = ((uint32_t)(off / 16U) << 4) | (more ? 0x01U : 0x00U);
        someip_put_u32(&s->seg_buf[n], tp_hdr);
        memcpy(&s->seg_buf[n + SOMEIP_TP_HEADER_LEN], &payload[off], seg);
        if (s->cfg.send(s->cfg.net_ctx, dst_ip, dst_port, s->seg_buf,
                        (uint16_t)(n + SOMEIP_TP_HEADER_LEN + seg)) != 0) {
            return -2;
        }
        s->stats.tp_seg_tx++;
        segs++;
        off = (uint16_t)(off + seg);
        if (off < len) {
            tp_gap_delay(s->cfg.tp_seg_gap_us);   /* 让上一帧真正发完再发下一段（U-35-10） */
        }
    }
    return segs;
}