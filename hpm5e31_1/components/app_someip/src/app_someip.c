/*
 * app_someip.c —— SOME/IP 服务端核心：报文头编解码 + 方法分发 + 事件发送 + 订阅管理
 *
 * 收包路径（全在 app_lwip_poll() 驱动的 UDP 回调里跑，单线程裸机）：
 *   UDP 回调 -> app_someip_on_rx() -> decode -> [SD | TP 重组 | 方法分发]
 *                                   -> app_someip_send_msg() -> 用户 send 回调 -> udp_sendto
 *
 * 时间基准：调用方传入 now_ms（uint32 差比较，天然处理回绕），组件内部不依赖任何 OS。
 */
#include <string.h>
#include <stdio.h>
#include "app_someip.h"
#include "app_someip_wire.h"
#include "app_someip_sd.h"
#include "app_someip_tp.h"

#define SOMEIP_VERSION_STRING "hpm5e31-someip/1.0.0"

const char *app_someip_decode_str(int err)
{
    switch (err) {
    case 0:  return "ok";
    case -1: return "short packet";
    case -2: return "length field mismatch";
    case -3: return "bad protocol version";
    default: return "unknown";
    }
}

/* ---------------- IEEE 802.3 CRC32（逐位，无表）；TP 大包完整性校验 ---------------- */
uint32_t someip_crc32(const uint8_t *data, uint16_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }
    return ~crc;
}

/* ---------------- 报文头编解码 ---------------- */
uint16_t app_someip_encode_header(uint8_t *buf, uint16_t cap,
                                  uint16_t service_id, uint16_t method_id,
                                  uint16_t client_id, uint16_t session_id,
                                  uint8_t interface_version, uint8_t message_type,
                                  uint8_t return_code, uint16_t payload_len)
{
    if (cap < SOMEIP_HEADER_LEN) {
        return 0;
    }
    someip_put_u16(&buf[0], service_id);
    someip_put_u16(&buf[2], method_id);
    /* Length：从 Request ID 起到报文末尾的字节数 = 8 + payload */
    someip_put_u32(&buf[4], (uint32_t)SOMEIP_LENGTH_MIN + (uint32_t)payload_len);
    someip_put_u16(&buf[8], client_id);
    someip_put_u16(&buf[10], session_id);
    buf[12] = SOMEIP_PROTOCOL_VERSION;
    buf[13] = interface_version;
    buf[14] = message_type;
    buf[15] = return_code;
    return SOMEIP_HEADER_LEN;
}

int app_someip_decode(const uint8_t *buf, uint16_t len, someip_msg_t *m)
{
    if ((buf == NULL) || (m == NULL) || (len < SOMEIP_HEADER_LEN)) {
        return -1;
    }
    uint32_t l = someip_get_u32(&buf[SOMEIP_LENGTH_OFFSET]);
    if (l < SOMEIP_LENGTH_MIN) {
        return -2;
    }
    /* UDP 帧可能带 padding（Length 比实际短可以接受），但反过来的"吹牛"必须拒绝 */
    if ((l + 8U) > (uint32_t)len) {
        return -2;
    }
    if (buf[12] != SOMEIP_PROTOCOL_VERSION) {
        return -3;
    }
    m->service_id        = someip_get_u16(&buf[0]);
    m->method_id         = someip_get_u16(&buf[2]);
    m->length            = l;
    m->client_id         = someip_get_u16(&buf[8]);
    m->session_id        = someip_get_u16(&buf[10]);
    m->protocol_version  = buf[12];
    m->interface_version = buf[13];
    m->message_type      = buf[14];
    m->return_code       = buf[15];
    m->payload           = &buf[SOMEIP_HEADER_LEN];
    m->payload_len       = (uint16_t)(l - SOMEIP_LENGTH_MIN);
    return 0;
}

/* ---------------- 初始化 / 注册 ---------------- */
const char *app_someip_ver_string(void)
{
    return SOMEIP_VERSION_STRING;
}

void app_someip_init(app_someip_t *s, const someip_cfg_t *cfg, struct someip_tp_reasm *tp_storage)
{
    memset(s, 0, sizeof(*s));
    s->cfg = *cfg;
    s->tp = tp_storage;
    if (tp_storage != NULL) {
        app_someip_tp_reasm_reset(tp_storage);
    }
    if (s->cfg.offer_ttl_s == 0U) {
        s->cfg.offer_ttl_s = 3U;
    }
    if (s->cfg.tp_seg_gap_us == 0U) {
        s->cfg.tp_seg_gap_us = 100U;   /* TP 段间保护间隔，见 someip_cfg_t 注释 */
    }
    s->offer_state = 0U;
}

void app_someip_register_method(app_someip_t *s, uint16_t method_id, someip_method_fn fn, void *user)
{
    if ((s == NULL) || (fn == NULL) || (s->n_methods >= SOMEIP_MAX_METHODS)) {
        return;
    }
    s->methods[s->n_methods].method_id = method_id;
    s->methods[s->n_methods].fn = fn;
    s->methods[s->n_methods].user = user;
    s->n_methods++;
}

void app_someip_register_event(app_someip_t *s, uint16_t event_id, uint16_t eventgroup,
                               uint32_t period_ms, someip_event_fill_fn fill, void *user)
{
    if ((s == NULL) || (fill == NULL) || (s->n_events >= SOMEIP_MAX_EVENTS)) {
        return;
    }
    someip_event_entry_t *e = &s->events[s->n_events++];
    e->event_id = event_id;
    e->eventgroup = eventgroup;
    e->period_ms = (period_ms == 0U) ? 100U : period_ms;
    e->next_ms = 0U;   /* 0 = 首轮立即发 */
    e->fill = fill;
    e->user = user;
}

/* ---------------- 订阅表 ---------------- */
static bool ip_equal(const uint8_t a[4], const uint8_t b[4])
{
    return (a[0] == b[0]) && (a[1] == b[1]) && (a[2] == b[2]) && (a[3] == b[3]);
}

int app_someip_sub_add(app_someip_t *s, const uint8_t ip[4], uint16_t port,
                       uint16_t eventgroup, uint32_t ttl_s, uint32_t now_ms)
{
    /* 已存在则续约（同一 endpoint + eventgroup） */
    for (int i = 0; i < SOMEIP_MAX_SUBSCRIBERS; i++) {
        someip_subscriber_t *sub = &s->subs[i];
        if (sub->active && ip_equal(sub->ip, ip) && (sub->port == port) &&
            (sub->eventgroup == eventgroup)) {
            sub->ttl_s = ttl_s;
            sub->last_renew_ms = now_ms;
            return 0;
        }
    }
    for (int i = 0; i < SOMEIP_MAX_SUBSCRIBERS; i++) {
        someip_subscriber_t *sub = &s->subs[i];
        if (!sub->active) {
            memcpy(sub->ip, ip, 4);
            sub->port = port;
            sub->eventgroup = eventgroup;
            sub->ttl_s = ttl_s;
            sub->last_renew_ms = now_ms;
            sub->active = true;
            s->stats.sub_added++;
            return 0;
        }
    }
    return -1;   /* 订阅表满 */
}

int app_someip_sub_remove(app_someip_t *s, const uint8_t ip[4], uint16_t port, uint16_t eventgroup)
{
    for (int i = 0; i < SOMEIP_MAX_SUBSCRIBERS; i++) {
        someip_subscriber_t *sub = &s->subs[i];
        if (sub->active && ip_equal(sub->ip, ip) && (sub->port == port) &&
            (sub->eventgroup == eventgroup)) {
            sub->active = false;
            s->stats.sub_removed++;
            return 0;
        }
    }
    return -1;
}

int app_someip_sub_count(app_someip_t *s, uint16_t eventgroup)
{
    int n = 0;
    for (int i = 0; i < SOMEIP_MAX_SUBSCRIBERS; i++) {
        if (s->subs[i].active && (s->subs[i].eventgroup == eventgroup)) {
            n++;
        }
    }
    return n;
}

/* ---------------- 发送 ---------------- */
int app_someip_send_msg(app_someip_t *s, const uint8_t dst_ip[4], uint16_t dst_port,
                        uint16_t method_id, uint16_t client_id, uint16_t session_id,
                        uint8_t msg_type, uint8_t return_code,
                        const uint8_t *payload, uint16_t len)
{
    if (s->cfg.send == NULL) {
        return -1;
    }
    if (len > SOMEIP_MAX_PAYLOAD) {
        return -1;
    }
    /* 超过 1392 B 自动走 SOME/IP-TP（MSG_TYPE 带 0x20，4 B TP 头 + 分段） */
    if (len > SOMEIP_TP_SEG_SIZE) {
        return app_someip_tp_send(s, dst_ip, dst_port, method_id, client_id, session_id,
                                  msg_type, return_code, payload, len);
    }
    uint16_t n = app_someip_encode_header(s->tx_buf, sizeof(s->tx_buf), s->cfg.service_id,
                                          method_id, client_id, session_id,
                                          s->cfg.interface_version, msg_type, return_code, len);
    uint8_t *dst = &s->tx_buf[SOMEIP_HEADER_LEN];
    if ((len != 0U) && (payload != NULL) && (payload != dst)) {
        memcpy(dst, payload, len);
    }
    return s->cfg.send(s->cfg.net_ctx, dst_ip, dst_port, s->tx_buf, (uint16_t)(n + len));
}

/* ---------------- 方法分发 ---------------- */
static someip_method_entry_t *find_method(app_someip_t *s, uint16_t method_id)
{
    for (int i = 0; i < s->n_methods; i++) {
        if (s->methods[i].method_id == method_id) {
            return &s->methods[i];
        }
    }
    return NULL;
}

/* 把"待发大响应"立刻发出去（payload 已在 tx_buf + 16） */
static void tx_slot_flush(app_someip_t *s)
{
    if (!s->tx_slot.pending) {
        return;
    }
    s->tx_slot.pending = false;
    (void)app_someip_send_msg(s, s->tx_slot.dst_ip, s->tx_slot.dst_port, s->tx_slot.method_id,
                              s->tx_slot.client_id, s->tx_slot.session_id, s->tx_slot.msg_type,
                              s->tx_slot.return_code, &s->tx_buf[SOMEIP_HEADER_LEN], s->tx_slot.len);
}

static void send_error(app_someip_t *s, const uint8_t src_ip[4], uint16_t src_port,
                       const someip_msg_t *req, uint8_t rc)
{
    (void)app_someip_send_msg(s, src_ip, src_port, req->method_id, req->client_id, req->session_id,
                              SOMEIP_MT_ERROR, rc, NULL, 0);
}

static void dispatch_request(app_someip_t *s, const uint8_t src_ip[4], uint16_t src_port,
                             const someip_msg_t *m, const uint8_t *payload, uint16_t plen,
                             bool want_response)
{
    /* 说明：SOME/IP 报文头里没有 instance id（instance 只存在于 SD），故只校验 service id */
    if (m->service_id != s->cfg.service_id) {
        s->stats.err_unknown_service++;
        if (want_response) {
            send_error(s, src_ip, src_port, m, SOMEIP_RC_UNKNOWN_SERVICE);
        }
        return;
    }
    if (m->interface_version != s->cfg.interface_version) {
        if (want_response) {
            send_error(s, src_ip, src_port, m, SOMEIP_RC_WRONG_INTERFACE_VERSION);
        }
        return;
    }
    tx_slot_flush(s);       /* 上一帧大响应还没发就先发掉（否则 tx_buf 会被下面的回复覆盖） */

    someip_method_entry_t *e = find_method(s, m->method_id);
    if (e == NULL) {
        s->stats.err_unknown_method++;
        if (want_response) {
            send_error(s, src_ip, src_port, m, SOMEIP_RC_UNKNOWN_METHOD);
        }
        return;
    }
    someip_msg_t req = *m;
    req.payload = payload;
    req.payload_len = plen;
    uint8_t *reply = &s->tx_buf[SOMEIP_HEADER_LEN];
    uint16_t reply_len = 0;
    uint8_t rc = e->fn(s, &req, reply, SOMEIP_MAX_PAYLOAD, &reply_len, e->user);
    if (!want_response) {
        return;   /* REQUEST_NO_RETURN：执行但不回包 */
    }
    if (rc != SOMEIP_RC_OK) {
        send_error(s, src_ip, src_port, m, rc);
        return;
    }
    if (reply_len > SOMEIP_MAX_PAYLOAD) {
        reply_len = SOMEIP_MAX_PAYLOAD;
    }
    /* 响应：Method ID 回显、Client ID/Session ID 回显（规范要求）。
     * 大响应（需要 SOME/IP-TP 分段）不在 RX 回调里发，改由 app_someip_poll() 发。 */
    if (reply_len > SOMEIP_TP_SEG_SIZE) {
        memcpy(s->tx_slot.dst_ip, src_ip, 4);
        s->tx_slot.dst_port = src_port;
        s->tx_slot.method_id = m->method_id;
        s->tx_slot.client_id = m->client_id;
        s->tx_slot.session_id = m->session_id;
        s->tx_slot.msg_type = SOMEIP_MT_RESPONSE;
        s->tx_slot.return_code = SOMEIP_RC_OK;
        s->tx_slot.len = reply_len;
        s->tx_slot.pending = true;
        return;
    }
    if (app_someip_send_msg(s, src_ip, src_port, m->method_id, m->client_id, m->session_id,
                            SOMEIP_MT_RESPONSE, SOMEIP_RC_OK, reply, reply_len) == 0) {
        s->stats.tx_responses++;
    }
}

/* ---------------- 收包入口 ---------------- */
void app_someip_on_rx(app_someip_t *s, const uint8_t src_ip[4], uint16_t src_port,
                      const uint8_t *buf, uint16_t len)
{
    someip_msg_t m;
    int err;

    s->stats.rx_total++;
    if ((uint32_t)len > s->stats.max_rx_len) {
        s->stats.max_rx_len = len;
    }
    err = app_someip_decode(buf, len, &m);
    if (err != 0) {
        s->stats.err_malformed++;
        return;
    }

    /* 1) SOME/IP-SD：Service=0xFFFF / Method=0x8100 */
    if ((m.service_id == SOMEIP_SD_SERVICE_ID) && (m.method_id == SOMEIP_SD_METHOD_ID)) {
        s->stats.rx_sd++;
        app_someip_sd_on_rx(s, src_ip, src_port, m.payload, m.payload_len);
        return;
    }

    /* 2) SOME/IP-TP 分段：先重组，凑齐后按原始类型重新分发 */
    if ((m.message_type & SOMEIP_TP_FLAG) != 0U) {
        s->stats.tp_seg_rx++;
        if (s->tp == NULL) {
            s->stats.tp_drop++;
            return;
        }
        uint32_t now = (s->cfg.now_ms != NULL) ? s->cfg.now_ms(s->cfg.net_ctx) : 0U;
        int r = app_someip_tp_feed(s->tp, &m, now);
        if (r < 0) {
            s->stats.tp_drop++;
            return;
        }
        if (r == 0) {
            return;
        }
        s->stats.tp_msg_rx++;
        someip_msg_t full = m;
        full.message_type = (uint8_t)(m.message_type & (uint8_t)~SOMEIP_TP_FLAG);
        full.payload = s->tp->buf;
        full.payload_len = (uint16_t)r;
        m = full;
    }

    /* 3) 分发 */
    switch (m.message_type) {
    case SOMEIP_MT_REQUEST:
        s->stats.rx_requests++;
        s->stats.last_client_id = m.client_id;
        s->stats.last_session_id = m.session_id;
        dispatch_request(s, src_ip, src_port, &m, m.payload, m.payload_len, true);
        break;
    case SOMEIP_MT_REQUEST_NO_RETURN:
        s->stats.rx_requests_no_return++;
        dispatch_request(s, src_ip, src_port, &m, m.payload, m.payload_len, false);
        break;
    default:
        /* 服务端不消费 RESPONSE/ERROR/NOTIFICATION（那是客户端角色） */
        s->stats.err_wrong_type++;
        break;
    }
}

/* ---------------- 事件发送 ---------------- */
static void send_event(app_someip_t *s, someip_event_entry_t *e)
{
    if (app_someip_sub_count(s, e->eventgroup) == 0) {
        return;
    }
    uint8_t *pl = &s->tx_buf[SOMEIP_HEADER_LEN];
    uint16_t len = e->fill(s, pl, SOMEIP_MAX_PAYLOAD, e->user);
    if (len > SOMEIP_MAX_PAYLOAD) {
        len = SOMEIP_MAX_PAYLOAD;
    }
    uint16_t session = ++s->session;
    for (int i = 0; i < SOMEIP_MAX_SUBSCRIBERS; i++) {
        someip_subscriber_t *sub = &s->subs[i];
        if (!sub->active || (sub->eventgroup != e->eventgroup)) {
            continue;
        }
        if (app_someip_send_msg(s, sub->ip, sub->port, e->event_id, 0x0000U, session,
                                SOMEIP_MT_NOTIFICATION, SOMEIP_RC_OK, pl, len) == 0) {
            s->stats.tx_notifications++;
        }
    }
}

/* ---------------- 周期任务 ---------------- */
void app_someip_poll(app_someip_t *s, uint32_t now_ms)
{
    tx_slot_flush(s);       /* 先发回调里登记的大响应（U-35-10 修复点） */
    /* 1) SD 周期重复 Offer（规范建议 TTL/2，避免对端 TTL 过期） */
    if (s->offer_state == 1U) {
        uint32_t period = (s->cfg.offer_ttl_s * 1000U) / 2U;
        if (period < 500U) {
            period = 500U;
        }
        if ((uint32_t)(now_ms - s->last_offer_ms) >= period) {
            (void)app_someip_offer(s);
        }
    }
    /* 2) 订阅 TTL 过期摘除 */
    for (int i = 0; i < SOMEIP_MAX_SUBSCRIBERS; i++) {
        someip_subscriber_t *sub = &s->subs[i];
        if (!sub->active) {
            continue;
        }
        uint32_t ttl_ms = sub->ttl_s * 1000U;
        if (ttl_ms == 0U) {
            ttl_ms = 3000U;
        }
        if ((uint32_t)(now_ms - sub->last_renew_ms) > (ttl_ms + 500U)) {
            sub->active = false;
            s->stats.sub_expired++;
        }
    }
    /* 3) 周期事件（只在有订阅者时真正发包，但计时照走） */
    for (int i = 0; i < s->n_events; i++) {
        someip_event_entry_t *e = &s->events[i];
        if ((int32_t)(now_ms - e->next_ms) >= 0) {
            send_event(s, e);
            e->next_ms += e->period_ms;
            /* 长时间没跑（例如调试打断）时重新对齐，避免补发风暴 */
            if ((int32_t)(now_ms - e->next_ms) > 1000) {
                e->next_ms = now_ms + e->period_ms;
            }
        }
    }
}

/* ---------------- SD 主动动作 ---------------- */
int app_someip_offer(app_someip_t *s)
{
    uint32_t now = (s->cfg.now_ms != NULL) ? s->cfg.now_ms(s->cfg.net_ctx) : 0U;
    s->last_offer_ms = now;
    s->offer_state = 1U;
    if (app_someip_sd_send_offer(s, s->cfg.sd_multicast_ip, s->cfg.sd_multicast_port,
                                 s->cfg.offer_ttl_s) == 0) {
        s->stats.tx_sd_offer++;
        return 0;
    }
    return -1;
}

int app_someip_stop_offer(app_someip_t *s)
{
    s->offer_state = 2U;
    return app_someip_sd_send_offer(s, s->cfg.sd_multicast_ip, s->cfg.sd_multicast_port, 0U);
}