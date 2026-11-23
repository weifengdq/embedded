/*
 * app_someip_sd.c —— SOME/IP-SD 服务端实现（Encode/Decode + Offer/Find/Subscribe 状态机）
 *
 * 线上格式（全部大端）：
 *   SOME/IP 头 16 B：Service=0xFFFF Method=0x8100 Client=0x0000 Session=++sd_session
 *                    Protocol=0x01 Interface=0x01 MsgType=0x02(NOTIFICATION) RC=0x00
 *   SD 载荷：
 *     Flags(1) | Reserved(3) | Entries-Length(4) | Entry*N (每个 16 B)
 *     | Options-Length(4) | Option*M
 *
 *   Entry（16 B）：Type | Idx1stRun | Idx2ndRun | NumOpt(高4位第2run/低4位第1run) |
 *                  ServiceID(2) | InstanceID(2) | MajorVer(1) | TTL(3) | ...(4 B 尾)
 *     ServiceEntry 尾部 4 B = MinorVersion(u32)
 *     EventGroupEntry 尾部 4 B = Reserved(12bit)+Counter(4bit) | EventGroupID(2)
 *
 *   Option：Length(2) | Type(1) | Reserved(1) | Value(Length-1 B)
 *     IPv4 Endpoint(0x04)：Length=0x0009，Value = IPv4(4)+Reserved(1)+L4Proto(1)+Port(2)
 *     注意 Length **不含** Length 字段本身和 Type 字段 → 单个 option 总长 = 3 + Length = 12 B
 */
#include <string.h>
#include <stdio.h>
#include "app_someip.h"
#include "app_someip_wire.h"
#include "app_someip_sd.h"

#define SD_ENDPOINT_OPT_LEN   9U
#define SD_ENDPOINT_OPT_TOTAL 12U

/* ---------------- 编码 ---------------- */
static void sd_entry_encode(uint8_t *p, const someip_sd_entry_t *e)
{
    p[0] = e->type;
    p[1] = e->index_first_option;
    p[2] = e->index_second_option;
    p[3] = (uint8_t)((e->num_options & 0x0FU));   /* 第 2 个 run 的个数放高 4 位，这里只用低 4 位 */
    someip_put_u16(&p[4], e->service_id);
    someip_put_u16(&p[6], e->instance_id);
    p[8] = e->major_version;
    someip_put_u24(&p[9], e->ttl & 0x00FFFFFFU);
    if ((e->type == SOMEIP_SD_ENTRY_FIND_SERVICE) || (e->type == SOMEIP_SD_ENTRY_OFFER_SERVICE)) {
        someip_put_u32(&p[12], e->minor_version);
    } else {
        someip_put_u16(&p[12], (uint16_t)(e->counter & 0x0FU));   /* Reserved(12) + Counter(4) */
        someip_put_u16(&p[14], e->eventgroup);
    }
}

static uint16_t sd_option_encode(uint8_t *p, const someip_sd_option_t *o)
{
    someip_put_u16(p, SD_ENDPOINT_OPT_LEN);
    p[2] = o->type;
    p[3] = 0x00U;                      /* Reserved */
    memcpy(&p[4], o->ip, 4);
    p[8] = 0x00U;                      /* Reserved */
    p[9] = o->proto;
    someip_put_u16(&p[10], o->port);
    return SD_ENDPOINT_OPT_TOTAL;
}

static uint16_t sd_build(uint8_t *buf, uint16_t cap, uint8_t flags,
                         const someip_sd_entry_t *entries, int n_entries,
                         const someip_sd_option_t *options, int n_options)
{
    uint16_t need = 1U + 3U + 4U + (uint16_t)(n_entries * SOMEIP_SD_ENTRY_LEN) + 4U +
                    (uint16_t)(n_options * SD_ENDPOINT_OPT_TOTAL);
    if (cap < need) {
        return 0;
    }
    buf[0] = flags;
    buf[1] = 0x00U;
    buf[2] = 0x00U;
    buf[3] = 0x00U;
    someip_put_u32(&buf[4], (uint32_t)(n_entries * SOMEIP_SD_ENTRY_LEN));
    uint16_t off = 8U;
    for (int i = 0; i < n_entries; i++) {
        sd_entry_encode(&buf[off], &entries[i]);
        off = (uint16_t)(off + SOMEIP_SD_ENTRY_LEN);
    }
    someip_put_u32(&buf[off], (uint32_t)(n_options * SD_ENDPOINT_OPT_TOTAL));
    off = (uint16_t)(off + 4U);
    for (int i = 0; i < n_options; i++) {
        off = (uint16_t)(off + sd_option_encode(&buf[off], &options[i]));
    }
    return off;
}

/* ---------------- 解析 ---------------- */
int app_someip_sd_parse(const uint8_t *p, uint16_t len, someip_sd_msg_t *m)
{
    memset(m, 0, sizeof(*m));
    if ((p == NULL) || (len < 8U)) {
        return -1;
    }
    m->flags = p[0];
    uint32_t entries_len = someip_get_u32(&p[4]);
    uint16_t off = 8U;
    if ((uint32_t)off + entries_len + 4U > (uint32_t)len) {
        return -2;
    }
    int n = (int)(entries_len / SOMEIP_SD_ENTRY_LEN);
    if (n > SOMEIP_SD_MAX_ENTRIES) {
        n = SOMEIP_SD_MAX_ENTRIES;
    }
    m->n_entries = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        const uint8_t *q = &p[off + i * SOMEIP_SD_ENTRY_LEN];
        someip_sd_entry_t *e = &m->entries[i];
        e->type = q[0];
        e->index_first_option = q[1];
        e->index_second_option = q[2];
        e->num_options = q[3];
        e->service_id = someip_get_u16(&q[4]);
        e->instance_id = someip_get_u16(&q[6]);
        e->major_version = q[8];
        e->ttl = someip_get_u24(&q[9]);
        if ((e->type == SOMEIP_SD_ENTRY_FIND_SERVICE) || (e->type == SOMEIP_SD_ENTRY_OFFER_SERVICE)) {
            e->minor_version = someip_get_u32(&q[12]);
        } else {
            e->counter = (uint8_t)(someip_get_u16(&q[12]) & 0x0FU);
            e->eventgroup = someip_get_u16(&q[14]);
        }
    }
    off = (uint16_t)(off + entries_len);
    uint32_t options_len = someip_get_u32(&p[off]);
    off = (uint16_t)(off + 4U);
    if ((uint32_t)off + options_len > (uint32_t)len) {
        return -3;
    }
    uint32_t used = 0U;
    while ((used + 3U) <= options_len) {
        const uint8_t *q = &p[off + used];
        uint16_t olen = someip_get_u16(q);
        uint8_t otype = q[2];
        if (olen < 1U) {
            break;
        }
        uint32_t total = 3U + (uint32_t)olen;
        if ((used + total) > options_len) {
            break;
        }
        if ((otype == SOMEIP_SD_OPT_IPV4_ENDPOINT) || (otype == SOMEIP_SD_OPT_IPV4_SD_ENDPOINT) ||
            (otype == SOMEIP_SD_OPT_IPV4_MULTICAST)) {
            if ((m->n_options < SOMEIP_SD_MAX_OPTIONS) && (olen >= SD_ENDPOINT_OPT_LEN)) {
                someip_sd_option_t *o = &m->options[m->n_options++];
                o->type = otype;
                memcpy(o->ip, &q[4], 4);
                o->proto = q[9];
                o->port = someip_get_u16(&q[10]);
            }
        }
        used += total;
    }
    return 0;
}

const someip_sd_entry_t *app_someip_sd_find_entry(const someip_sd_msg_t *m, uint8_t type)
{
    for (int i = 0; i < m->n_entries; i++) {
        if (m->entries[i].type == type) {
            return &m->entries[i];
        }
    }
    return NULL;
}

/* ---------------- 发送 ---------------- */
static int sd_send(app_someip_t *s, const uint8_t dst_ip[4], uint16_t dst_port, uint8_t flags,
                   const someip_sd_entry_t *entries, int n_entries,
                   const someip_sd_option_t *options, int n_options)
{
    if (s->cfg.send == NULL) {
        return -1;
    }
    uint16_t cap = (uint16_t)(sizeof(s->sd_buf) - SOMEIP_HEADER_LEN);
    uint16_t plen = sd_build(&s->sd_buf[SOMEIP_HEADER_LEN], cap, flags, entries, n_entries,
                             options, n_options);
    if (plen == 0U) {
        return -1;
    }
    uint16_t n = app_someip_encode_header(s->sd_buf, sizeof(s->sd_buf),
                                          SOMEIP_SD_SERVICE_ID, SOMEIP_SD_METHOD_ID,
                                          0x0000U, ++s->sd_session,
                                          SOMEIP_SD_INTERFACE_VERSION, SOMEIP_MT_NOTIFICATION,
                                          SOMEIP_RC_OK, plen);
    return s->cfg.send(s->cfg.net_ctx, dst_ip, dst_port, s->sd_buf, (uint16_t)(n + plen));
}

int app_someip_sd_send_offer(app_someip_t *s, const uint8_t dst_ip[4], uint16_t dst_port,
                             uint32_t ttl_s)
{
    /* Flags：Reboot 位只在"重启后的第一条 Offer"置 1，之后的周期 Offer 必须清 0。
     * 我们原来每条 Offer 都带 Reboot(0x80)，vsomeip 会把每条 Offer 当成"对端重启"，
     * 反复 NO_STOP_OFFER/删服务，服务永远到不了 AVAILABLE（第 35 章 U-35-1 的根因之一）。 */
    static bool reboot_sent;
    uint8_t flags = SOMEIP_SD_FLAG_UNICAST;
    if (!reboot_sent) {
        flags = (uint8_t)(SOMEIP_SD_FLAG_REBOOT | SOMEIP_SD_FLAG_UNICAST);
        reboot_sent = true;
    }
    if (ttl_s == 0U) {
        flags = (uint8_t)(SOMEIP_SD_FLAG_REBOOT | SOMEIP_SD_FLAG_UNICAST);   /* StopOffer 也带 Reboot */
    }
    someip_sd_entry_t e;
    someip_sd_option_t o;
    memset(&e, 0, sizeof(e));
    memset(&o, 0, sizeof(o));
    e.type = SOMEIP_SD_ENTRY_OFFER_SERVICE;
    e.index_first_option = 0U;
    e.index_second_option = 0U;
    e.num_options = 1U;
    e.service_id = s->cfg.service_id;
    e.instance_id = s->cfg.instance_id;
    e.major_version = s->cfg.major_version;
    e.minor_version = s->cfg.minor_version;
    e.ttl = ttl_s;
    o.type = SOMEIP_SD_OPT_IPV4_ENDPOINT;
    memcpy(o.ip, s->cfg.local_ip, 4);
    o.proto = SOMEIP_SD_PROTO_UDP;
    o.port = s->cfg.service_port;
    return sd_send(s, dst_ip, dst_port, flags, &e, 1, &o, 1);
}

int app_someip_sd_send_ack(app_someip_t *s, const uint8_t dst_ip[4], uint16_t dst_port,
                           const someip_sd_entry_t *req, uint32_t ttl_s, uint8_t return_code)
{
    someip_sd_entry_t e;
    memset(&e, 0, sizeof(e));
    e.type = (return_code == SOMEIP_RC_OK) ? SOMEIP_SD_ENTRY_SUBSCRIBE_ACK
                                           : SOMEIP_SD_ENTRY_SUBSCRIBE_NACK;
    e.index_first_option = 0U;
    e.index_second_option = 0U;
    e.num_options = 0U;
    e.service_id = req->service_id;
    e.instance_id = req->instance_id;
    e.major_version = req->major_version;
    e.counter = req->counter;
    e.eventgroup = req->eventgroup;
    e.ttl = (return_code == SOMEIP_RC_OK) ? ttl_s : 0U;
    /* Ack 里带一个 IPv4 endpoint option，告知订阅者"事件从哪个端点发出"（部分实现会据此更新） */
    someip_sd_option_t o;
    memset(&o, 0, sizeof(o));
    o.type = SOMEIP_SD_OPT_IPV4_ENDPOINT;
    memcpy(o.ip, s->cfg.local_ip, 4);
    o.proto = SOMEIP_SD_PROTO_UDP;
    o.port = s->cfg.service_port;
    e.num_options = 1U;
    int r = sd_send(s, dst_ip, dst_port, (uint8_t)(SOMEIP_SD_FLAG_REBOOT | SOMEIP_SD_FLAG_UNICAST),
                    &e, 1, &o, 1);
    if (r == 0) {
        s->stats.tx_sd_ack++;
    }
    return r;
}

/* ---------------- 收包状态机 ---------------- */
/* 从 entry 的两个 option run 里找第一个 IPv4 endpoint option */
static const someip_sd_option_t *sd_entry_endpoint(const someip_sd_msg_t *m,
                                                   const someip_sd_entry_t *e)
{
    uint8_t n1 = (uint8_t)(e->num_options & 0x0FU);
    uint8_t n2 = (uint8_t)((e->num_options >> 4) & 0x0FU);
    for (int run = 0; run < 2; run++) {
        uint8_t base = (run == 0) ? e->index_first_option : e->index_second_option;
        uint8_t n = (run == 0) ? n1 : n2;
        for (int i = 0; i < n; i++) {
            uint8_t idx = (uint8_t)(base + i);
            if (idx < m->n_options) {
                return &m->options[idx];
            }
        }
    }
    return NULL;
}

static bool sd_service_match(app_someip_t *s, const someip_sd_entry_t *e)
{
    bool svc_ok = (e->service_id == s->cfg.service_id) || (e->service_id == 0xFFFFU);
    bool inst_ok = (e->instance_id == s->cfg.instance_id) || (e->instance_id == 0xFFFFU);
    bool major_ok = (e->major_version == s->cfg.major_version) || (e->major_version == 0xFFU);
    return svc_ok && inst_ok && major_ok;
}

static bool sd_has_eventgroup(app_someip_t *s, uint16_t eventgroup)
{
    for (int i = 0; i < s->n_events; i++) {
        if (s->events[i].eventgroup == eventgroup) {
            return true;
        }
    }
    return false;
}

void app_someip_sd_on_rx(app_someip_t *s, const uint8_t src_ip[4], uint16_t src_port,
                         const uint8_t *payload, uint16_t len)
{
    someip_sd_msg_t m;
    uint32_t now;

    if (app_someip_sd_parse(payload, len, &m) != 0) {
        s->stats.err_malformed++;
        return;
    }
    now = (s->cfg.now_ms != NULL) ? s->cfg.now_ms(s->cfg.net_ctx) : 0U;

    for (int i = 0; i < m.n_entries; i++) {
        const someip_sd_entry_t *e = &m.entries[i];
        switch (e->type) {
        case SOMEIP_SD_ENTRY_FIND_SERVICE:
            /* 有人找服务：单播回 Offer（回给请求方的 SD 端点） */
            if (sd_service_match(s, e)) {
                if (app_someip_sd_send_offer(s, src_ip, src_port, s->cfg.offer_ttl_s) == 0) {
                    s->stats.tx_sd_offer++;
                }
            }
            break;

        case SOMEIP_SD_ENTRY_OFFER_SERVICE:
            /* 其它节点的 Offer：本机是服务端，忽略（双向服务可在此扩展） */
            break;

        case SOMEIP_SD_ENTRY_SUBSCRIBE_EVENTGROUP: {
            const someip_sd_option_t *opt = sd_entry_endpoint(&m, e);
            uint8_t ev_ip[4];
            uint16_t ev_port;
            if (opt != NULL) {
                memcpy(ev_ip, opt->ip, 4);
                ev_port = opt->port;
            } else {
                /* 没有 endpoint option 时退回"订阅者 SD 端点"，规范允许 SD endpoint 收事件 */
                memcpy(ev_ip, src_ip, 4);
                ev_port = src_port;
            }
            if (e->ttl == 0U) {
                /* StopSubscribeEventgroup */
                (void)app_someip_sub_remove(s, ev_ip, ev_port, e->eventgroup);
                (void)app_someip_sd_send_ack(s, src_ip, src_port, e, 0U, SOMEIP_RC_OK);
                break;
            }
            if (!sd_service_match(s, e) || !sd_has_eventgroup(s, e->eventgroup)) {
                (void)app_someip_sd_send_ack(s, src_ip, src_port, e, 0U, SOMEIP_RC_NOT_OK);
                break;
            }
            if (app_someip_sub_add(s, ev_ip, ev_port, e->eventgroup, e->ttl, now) != 0) {
                (void)app_someip_sd_send_ack(s, src_ip, src_port, e, 0U, SOMEIP_RC_NOT_OK);
                break;
            }
            (void)app_someip_sd_send_ack(s, src_ip, src_port, e, e->ttl, SOMEIP_RC_OK);
            break;
        }

        case SOMEIP_SD_ENTRY_SUBSCRIBE_ACK:
        case SOMEIP_SD_ENTRY_SUBSCRIBE_NACK:
            /* 本机是服务端，不处理对端 Ack */
            break;

        default:
            break;
        }
    }
}