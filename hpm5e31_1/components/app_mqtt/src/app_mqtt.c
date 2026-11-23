/*
 * app_mqtt.c - MQTT 3.1.1 客户端实现（裸机 lwIP raw TCP，无 OS、无 malloc）
 *
 * 分层：
 *   应用层（例程回调）  <- app_mqtt_publish/subscribe
 *   MQTT 报文层          <- 本文件：固定头 + 剩余长度变长编码 + 各类报文
 *   TCP 层（lwIP raw）   <- tcp_new/tcp_connect/tcp_write/tcp_recv 回调
 *   定时（1 ms 时基）    <- sys_now()，由 app_lwip 的 board_timer 驱动
 *
 * 所有状态推进都在 app_mqtt_poll() 里做；lwIP 回调里只做"收包解析 + 回 ACK"，
 * 不阻塞、不打印、不重连（重连留给 poll），保证回调短（约定见 docs/应用层开发约定.md）。
 *
 * 报文编码参考 OASIS MQTT 3.1.1 标准 §2/§3：
 *   - 固定头 1 B：type(4 bit) | flags(4 bit)
 *   - 剩余长度：1~4 字节，每字节 7 位数据 + 1 位延续位，最大 268 435 455
 *   - 变长字段：2 B 大端长度 + 字节
 *
 * 作者：tp-mqtt（第 38 章）
 */
#include <string.h>
#include <stdio.h>

#include "app_mqtt.h"

#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/err.h"
#include "lwip/sys.h"

/* ---------------- MQTT 报文类型（MQTT 3.1.1 表 2.1） ---------------- */
#define MQ_CONNECT      1U
#define MQ_CONNACK      2U
#define MQ_PUBLISH      3U
#define MQ_PUBACK       4U
#define MQ_PUBREC       5U
#define MQ_PUBREL       6U
#define MQ_PUBCOMP      7U
#define MQ_SUBSCRIBE    8U
#define MQ_SUBACK       9U
#define MQ_UNSUBSCRIBE  10U
#define MQ_UNSUBACK     11U
#define MQ_PINGREQ      12U
#define MQ_PINGRESP     13U
#define MQ_DISCONNECT   14U

static void mq_tx_flush(app_mqtt_t *m);
static int  mq_tx_enqueue(app_mqtt_t *m, const uint8_t *data, uint16_t len);
static void mq_schedule_reconnect(app_mqtt_t *m, int reason);
static void mq_close_tcp(app_mqtt_t *m, bool abortive);
static void mq_handle_packet(app_mqtt_t *m, uint8_t type, uint8_t flags,
                             const uint8_t *body, uint32_t len);

/* ---------------- 小工具 ---------------- */
static uint16_t mq_strnlen(const char *s, uint16_t max)
{
    uint16_t n = 0;
    if (s == NULL) {
        return 0;
    }
    while ((n < max) && (s[n] != '\0')) {
        n++;
    }
    return n;
}

static void mq_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) (v & 0xFFU);
}

static uint16_t mq_get_u16(const uint8_t *p)
{
    return (uint16_t) (((uint16_t) p[0] << 8) | (uint16_t) p[1]);
}

/* 把变长字段（2 B 长度 + 内容）写入 p，返回写入字节数 */
static uint16_t mq_put_str(uint8_t *p, const char *s, uint16_t len)
{
    mq_put_u16(p, len);
    if (len > 0U) {
        memcpy(&p[2], s, len);
    }
    return (uint16_t) (2U + len);
}

/* ---------------- 剩余长度变长编码（MQTT 3.1.1 §2.2.3） ---------------- */
int app_mqtt_remlen_encode(uint8_t *buf, uint32_t len)
{
    int i = 0;
    if (buf == NULL) {
        return 0;
    }
    do {
        uint8_t b = (uint8_t) (len % 128U);
        len /= 128U;
        if (len > 0U) {
            b |= 0x80U;
        }
        buf[i++] = b;
    } while ((len > 0U) && (i < 4));
    return i;
}

int app_mqtt_remlen_decode(const uint8_t *buf, int avail, uint32_t *len)
{
    uint32_t value = 0;
    uint32_t mult = 1;
    int i;

    if ((buf == NULL) || (len == NULL) || (avail <= 0)) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        uint8_t b;
        if (i >= avail) {
            return 0;               /* 数据还没到齐 */
        }
        b = buf[i];
        value += (uint32_t) (b & 0x7FU) * mult;
        if ((b & 0x80U) == 0U) {
            *len = value;
            return i + 1;
        }
        mult *= 128U;
    }
    return -1;                      /* 超过 4 字节，非法 */
}

/* ---------------- 主题过滤器匹配（MQTT 3.1.1 §4.7） ---------------- */
bool app_mqtt_topic_match(const char *filter, const char *topic)
{
    const char *f = filter;
    const char *t = topic;

    if ((filter == NULL) || (topic == NULL) || (*filter == '\0')) {
        return false;
    }
    /* 以 $ 开头的主题（$SYS 等）不被前导通配符匹配 */
    if ((*topic == '$') && ((*filter == '#') || (*filter == '+'))) {
        return false;
    }
    while (*f != '\0') {
        if (*f == '#') {
            return (f[1] == '\0');          /* # 只能出现在最后一级 */
        }
        if (*f == '+') {
            if ((f[1] != '\0') && (f[1] != '/')) {
                return false;               /* + 必须独占一级 */
            }
            if (*t == '\0') {
                return false;
            }
            while ((*t != '\0') && (*t != '/')) {
                t++;
            }
            f++;
            if (*f == '/') {
                f++;
            }
            if (*t == '/') {
                t++;
            }
            continue;
        }
        /* "sport/#" 也匹配父级 "sport"（§4.7.1.2） */
        if ((*f == '/') && (f[1] == '#') && (f[2] == '\0') && (*t == '\0')) {
            return true;
        }
        if ((*t == '\0') || (*f != *t)) {
            return false;
        }
        f++;
        t++;
    }
    return (*t == '\0');
}

/* ---------------- 发送队列 ---------------- */
static void mq_tx_flush(app_mqtt_t *m)
{
    if (m->pcb == NULL) {
        return;
    }
    while (m->txq_head != m->txq_tail) {
        uint16_t n;
        uint16_t avail;
        err_t err;

        if (m->txq_head > m->txq_tail) {
            n = (uint16_t) (m->txq_head - m->txq_tail);
        } else {
            n = (uint16_t) (APP_MQTT_TXQ_SIZE - m->txq_tail);
        }
        avail = tcp_sndbuf(m->pcb);
        if (avail == 0U) {
            break;                      /* sndbuf 满，下次 poll 再推 */
        }
        if (n > avail) {
            n = avail;
        }
        err = tcp_write(m->pcb, &m->txq[m->txq_tail], n, TCP_WRITE_FLAG_COPY);
        if (err != ERR_OK) {
            break;                      /* ERR_MEM：等 PUBACK 腾出 sndbuf */
        }
        m->txq_tail = (uint16_t) ((m->txq_tail + n) % APP_MQTT_TXQ_SIZE);
        m->stats.tx_bytes += n;
        m->last_tx_ms = sys_now();
    }
    (void) tcp_output(m->pcb);
}

/* 入队一条完整报文并尽量立刻发出去。返回 0 成功；-1 队列满 */
static int mq_tx_enqueue(app_mqtt_t *m, const uint8_t *data, uint16_t len)
{
    uint16_t used;
    uint16_t free_bytes;

    if ((m->pcb == NULL) || (len == 0U) || (len > APP_MQTT_TXQ_SIZE)) {
        return -1;
    }
    used = (uint16_t) ((m->txq_head + APP_MQTT_TXQ_SIZE - m->txq_tail) % APP_MQTT_TXQ_SIZE);
    free_bytes = (uint16_t) (APP_MQTT_TXQ_SIZE - 1U - used);
    if (len > free_bytes) {
        m->stats.drops++;
        return -1;
    }
    {
        uint16_t first = (uint16_t) (APP_MQTT_TXQ_SIZE - m->txq_head);
        if (first > len) {
            first = len;
        }
        memcpy(&m->txq[m->txq_head], data, first);
        if (first < len) {
            memcpy(&m->txq[0], &data[first], (size_t) (len - first));
        }
        m->txq_head = (uint16_t) ((m->txq_head + len) % APP_MQTT_TXQ_SIZE);
    }
    m->stats.tx_packets++;
    /* 调试：记下最近一条报文（CONNECT/PUBLISH 逐字节表用） */
    if (len <= sizeof(m->last_tx_pkt)) {
        memcpy(m->last_tx_pkt, data, len);
        m->last_tx_pkt_len = len;
    }
    mq_tx_flush(m);
    return 0;
}

/* ---------------- 固定头 ---------------- */
static uint16_t mq_build_header(uint8_t *p, uint8_t type, uint8_t flags, uint32_t remlen)
{
    int n;
    p[0] = (uint8_t) (((type & 0x0FU) << 4) | (flags & 0x0FU));
    n = app_mqtt_remlen_encode(&p[1], remlen);
    return (uint16_t) (1U + (uint16_t) n);
}

/* ---------------- QoS1 未确认表 ---------------- */
static app_mqtt_inflight_t *mq_find_free_slot(app_mqtt_t *m)
{
    uint8_t i;
    for (i = 0; i < APP_MQTT_INFLIGHT_MAX; i++) {
        if (!m->inflight[i].used) {
            return &m->inflight[i];
        }
    }
    return NULL;
}

static app_mqtt_inflight_t *mq_find_slot_by_pid(app_mqtt_t *m, uint16_t pid)
{
    uint8_t i;
    for (i = 0; i < APP_MQTT_INFLIGHT_MAX; i++) {
        if (m->inflight[i].used && (m->inflight[i].pid == pid)) {
            return &m->inflight[i];
        }
    }
    return NULL;
}

static bool mq_pid_in_use(app_mqtt_t *m, uint16_t pid)
{
    return (mq_find_slot_by_pid(m, pid) != NULL);
}

static void mq_free_slot(app_mqtt_t *m, app_mqtt_inflight_t *slot)
{
    if (slot != NULL) {
        slot->used = false;
        if (m->stats.inflight > 0U) {
            m->stats.inflight--;
        }
    }
}

static uint16_t mq_alloc_pid(app_mqtt_t *m)
{
    uint16_t pid;
    uint32_t guard = 0;

    do {
        m->next_pid++;
        if (m->next_pid == 0U) {
            m->next_pid = 1U;           /* packet id 0 非法（§2.3.1） */
        }
        pid = m->next_pid;
        guard++;
    } while (mq_pid_in_use(m, pid) && (guard < 65535U));
    m->stats.last_pid = pid;
    return pid;
}

/* ---------------- 各类报文发送 ---------------- */
/* 把 CONNECT 报文组进 pkt（不含发送），返回报文长度；供发送与调试 hexdump 共用 */
static uint16_t mq_build_connect(app_mqtt_t *m, uint8_t *pkt, uint16_t cap)
{
    (void) cap;
    uint16_t cid_len = mq_strnlen(m->cfg_client_id, APP_MQTT_CLIENT_ID_MAX);
    uint16_t will_topic_len = mq_strnlen(m->cfg_will_topic, APP_MQTT_TOPIC_MAX);
    uint16_t will_payload_len = mq_strnlen(m->cfg_will_payload, APP_MQTT_WILL_PAYLOAD_MAX);
    uint16_t user_len = mq_strnlen(m->cfg_user, APP_MQTT_USER_MAX);
    uint16_t pass_len = mq_strnlen(m->cfg_pass, APP_MQTT_PASS_MAX);
    uint8_t flags = 0;
    uint32_t remlen;
    uint32_t off;
    uint16_t hdr;

    if (m->cfg.clean_session) {
        flags |= 0x02U;
    }
    if (will_topic_len > 0U) {
        flags |= 0x04U;
        flags |= (uint8_t) ((m->cfg.will_qos & 0x03U) << 3);
        if (m->cfg.will_retain) {
            flags |= 0x20U;
        }
    }
    if (user_len > 0U) {
        flags |= 0x80U;
    }
    if (pass_len > 0U) {
        flags |= 0x40U;
    }

    remlen = 10U;                       /* 协议名 6 + 级别 1 + 标志 1 + keepalive 2 */
    remlen += (uint32_t) (2U + cid_len);
    if (will_topic_len > 0U) {
        remlen += (uint32_t) (2U + will_topic_len) + (uint32_t) (2U + will_payload_len);
    }
    if (user_len > 0U) {
        remlen += (uint32_t) (2U + user_len);
    }
    if (pass_len > 0U) {
        remlen += (uint32_t) (2U + pass_len);
    }

    hdr = mq_build_header(pkt, MQ_CONNECT, 0U, remlen);
    off = hdr;
    pkt[off++] = 0x00U;                 /* 协议名长度 = 4 */
    pkt[off++] = 0x04U;
    pkt[off++] = 'M';
    pkt[off++] = 'Q';
    pkt[off++] = 'T';
    pkt[off++] = 'T';
    pkt[off++] = 0x04U;                 /* 协议级别 4 = MQTT 3.1.1 */
    pkt[off++] = flags;
    mq_put_u16(&pkt[off], m->cfg.keepalive_s);
    off += 2U;
    off += mq_put_str(&pkt[off], m->cfg_client_id, cid_len);
    if (will_topic_len > 0U) {
        off += mq_put_str(&pkt[off], m->cfg_will_topic, will_topic_len);
        off += mq_put_str(&pkt[off], m->cfg_will_payload, will_payload_len);
    }
    if (user_len > 0U) {
        off += mq_put_str(&pkt[off], m->cfg_user, user_len);
    }
    if (pass_len > 0U) {
        off += mq_put_str(&pkt[off], m->cfg_pass, pass_len);
    }
    return (uint16_t) off;
}

static void mq_send_connect(app_mqtt_t *m)
{
    uint8_t pkt[APP_MQTT_MAX_PACKET];
    uint16_t n = mq_build_connect(m, pkt, (uint16_t) sizeof(pkt));
    if (mq_tx_enqueue(m, pkt, n) != 0) {
        m->stats.drops++;
    }
}

static void mq_send_puback(app_mqtt_t *m, uint16_t pid)
{
    uint8_t pkt[4];
    mq_build_header(pkt, MQ_PUBACK, 0U, 2U);
    mq_put_u16(&pkt[2], pid);
    if (mq_tx_enqueue(m, pkt, 4U) == 0) {
        m->stats.puback_tx++;
    }
}

static void mq_send_pingreq(app_mqtt_t *m)
{
    uint8_t pkt[2];
    mq_build_header(pkt, MQ_PINGREQ, 0U, 0U);
    if (mq_tx_enqueue(m, pkt, 2U) == 0) {
        m->stats.pingreq_tx++;
        m->ping_outstanding = true;
        m->ping_tx_ms = sys_now();
    }
}

/* 从 QoS1 未确认表构造一条 PUBLISH 报文（dup=1 用于重发） */
static int mq_build_publish_from_slot(app_mqtt_t *m, const app_mqtt_inflight_t *slot, bool dup)
{
    uint8_t pkt[APP_MQTT_MAX_PACKET];
    uint32_t remlen = 2U + slot->topic_len + 2U + slot->payload_len;
    uint8_t flags = (uint8_t) (((dup ? 1U : 0U) << 3) | ((slot->qos & 0x03U) << 1)
                               | (slot->retain ? 1U : 0U));
    uint16_t hdr;
    uint32_t off;

    hdr = mq_build_header(pkt, MQ_PUBLISH, flags, remlen);
    off = hdr;
    mq_put_u16(&pkt[off], slot->topic_len);
    off += 2U;
    memcpy(&pkt[off], slot->topic, slot->topic_len);
    off += slot->topic_len;
    mq_put_u16(&pkt[off], slot->pid);
    off += 2U;
    memcpy(&pkt[off], slot->payload, slot->payload_len);
    off += slot->payload_len;
    return mq_tx_enqueue(m, pkt, (uint16_t) off);
}

/* ---------------- 连接关闭 / 重连 ---------------- */
static void mq_close_tcp(app_mqtt_t *m, bool abortive)
{
    m->closing = true;
    if (m->pcb != NULL) {
        struct tcp_pcb *pcb = m->pcb;
        m->pcb = NULL;                  /* 先置空：回调重入时看到的是"已关闭" */
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_sent(pcb, NULL);
        tcp_err(pcb, NULL);
        if (abortive) {
            tcp_abort(pcb);
        } else if (tcp_close(pcb) != ERR_OK) {
            tcp_abort(pcb);             /* 内存不足时降级为 RST */
        }
    }
    m->rx_len = 0;
    m->txq_head = 0;
    m->txq_tail = 0;
    m->ping_outstanding = false;
    m->closing = false;
}

/* pcb 已关闭（或被 lwIP 回收）后调用：清理未确认表 + 退避排程 + 通知应用 */
static void mq_schedule_reconnect(app_mqtt_t *m, int reason)
{
    uint8_t i;

    for (i = 0; i < APP_MQTT_INFLIGHT_MAX; i++) {
        if (m->inflight[i].used) {
            m->inflight[i].used = false;
            m->stats.drops++;
        }
    }
    m->stats.inflight = 0;
    if (m->on_state != NULL) {
        m->on_state(m, APP_MQTT_STATE_WAIT_RECONNECT, reason, m->user);
    }
    if (m->cfg.auto_reconnect) {
        m->state = APP_MQTT_STATE_WAIT_RECONNECT;
        m->reconnect_at_ms = sys_now() + m->backoff_ms;
        m->backoff_ms *= 2U;
        if (m->backoff_ms < m->cfg.reconnect_min_ms) {
            m->backoff_ms = m->cfg.reconnect_min_ms;
        }
        if (m->backoff_ms > m->cfg.reconnect_max_ms) {
            m->backoff_ms = m->cfg.reconnect_max_ms;
        }
    } else {
        m->state = APP_MQTT_STATE_CLOSED;
    }
}

/* ---------------- lwIP TCP 回调 ---------------- */
static err_t mq_on_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    app_mqtt_t *m = (app_mqtt_t *) arg;

    if (err != ERR_OK) {
        m->stats.disconnects++;
        mq_close_tcp(m, false);
        mq_schedule_reconnect(m, APP_MQTT_EV_TCP_ERROR);
        return ERR_OK;
    }
    m->stats.tcp_connects++;
    /* 关 Nagle：MQTT 控制报文（PUBACK/SUBSCRIBE/PINGREQ）都是几十字节的小包，
       默认 Nagle + 对端 delayed ACK 会让 PUBACK 之后的 PUBLISH 等 ~40 ms（第 38 章实测 42 ms）。 */
    tcp_nagle_disable(tpcb);
    m->state = APP_MQTT_STATE_WAIT_CONNACK;
    m->connect_start_ms = sys_now();
    m->last_tx_ms = sys_now();
    mq_send_connect(m);
    return ERR_OK;
}

static err_t mq_on_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    app_mqtt_t *m = (app_mqtt_t *) arg;
    uint16_t plen;

    if (err != ERR_OK) {
        if (p != NULL) {
            pbuf_free(p);
        }
        return ERR_OK;
    }
    if (p == NULL) {
        /* 对端 FIN：正常关闭，但会话已断 */
        m->stats.disconnects++;
        mq_close_tcp(m, false);
        mq_schedule_reconnect(m, APP_MQTT_EV_TCP_LOST);
        return ERR_OK;
    }
    plen = p->tot_len;
    m->stats.rx_bytes += plen;
    if ((uint32_t) m->rx_len + (uint32_t) plen > APP_MQTT_RX_BUF) {
        pbuf_free(p);
        m->stats.protocol_errors++;
        mq_close_tcp(m, true);
        mq_schedule_reconnect(m, APP_MQTT_EV_TCP_LOST);
        return ERR_OK;
    }
    (void) pbuf_copy_partial(p, &m->rxbuf[m->rx_len], plen, 0);
    m->rx_len = (uint16_t) (m->rx_len + plen);
    pbuf_free(p);
    tcp_recved(tpcb, plen);

    m->last_rx_ms = sys_now();

    /* 逐条拆包（可能一次收到多条，也可能半条） */
    while (m->rx_len >= 2U) {
        uint32_t remlen = 0;
        int n = app_mqtt_remlen_decode(&m->rxbuf[1], (int) (m->rx_len - 1U), &remlen);
        uint32_t total;
        uint8_t type;
        uint8_t pflags;

        if (n == 0) {
            break;                      /* 剩余长度还没收全 */
        }
        if (n < 0) {
            m->stats.protocol_errors++;
            mq_close_tcp(m, true);
            mq_schedule_reconnect(m, APP_MQTT_EV_TCP_LOST);
            return ERR_OK;
        }
        total = 1U + (uint32_t) n + remlen;
        if (total > APP_MQTT_MAX_PACKET) {
            m->stats.protocol_errors++;
            mq_close_tcp(m, true);
            mq_schedule_reconnect(m, APP_MQTT_EV_TCP_LOST);
            return ERR_OK;
        }
        if ((uint32_t) m->rx_len < total) {
            break;                      /* 半条，等后续 */
        }
        type = (uint8_t) (m->rxbuf[0] >> 4);
        pflags = (uint8_t) (m->rxbuf[0] & 0x0FU);
        m->stats.rx_packets++;
        mq_handle_packet(m, type, pflags, &m->rxbuf[1U + (uint32_t) n], remlen);
        if (m->pcb == NULL) {
            m->rx_len = 0;              /* handler 里断链了 */
            return ERR_OK;
        }
        memmove(m->rxbuf, &m->rxbuf[total], (size_t) (m->rx_len - total));
        m->rx_len = (uint16_t) (m->rx_len - total);
    }
    return ERR_OK;
}

static err_t mq_on_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    app_mqtt_t *m = (app_mqtt_t *) arg;
    (void) tpcb;
    (void) len;
    mq_tx_flush(m);
    return ERR_OK;
}

static void mq_on_err(void *arg, err_t err)
{
    app_mqtt_t *m = (app_mqtt_t *) arg;

    (void) err;
    /* lwIP 已释放 pcb，这里只能清引用 */
    m->pcb = NULL;
    m->rx_len = 0;
    m->txq_head = 0;
    m->txq_tail = 0;
    m->ping_outstanding = false;
    if (m->closing) {
        return;                     /* 自己触发的 tcp_abort，不重复排程 */
    }
    m->stats.disconnects++;
    mq_schedule_reconnect(m, APP_MQTT_EV_TCP_ERROR);
}

/* ---------------- 报文处理 ---------------- */
static void mq_handle_packet(app_mqtt_t *m, uint8_t type, uint8_t flags,
                             const uint8_t *body, uint32_t len)
{
    if (type == MQ_CONNACK) {
        uint8_t rc;
        if (len < 2U) {
            m->stats.protocol_errors++;
            return;
        }
        rc = body[1];
        if (rc == 0U) {
            m->stats.connack_ok++;
            m->state = APP_MQTT_STATE_CONNECTED;
            m->backoff_ms = m->cfg.reconnect_min_ms;
            m->ping_outstanding = false;
            m->last_rx_ms = sys_now();
            if (m->on_state != NULL) {
                m->on_state(m, APP_MQTT_STATE_CONNECTED, APP_MQTT_EV_CONNECTED, m->user);
            }
        } else {
            m->stats.connack_refused++;
            if (m->on_state != NULL) {
                m->on_state(m, APP_MQTT_STATE_CLOSED, APP_MQTT_EV_CONNACK_REFUSED, m->user);
            }
            mq_close_tcp(m, false);
            mq_schedule_reconnect(m, APP_MQTT_EV_CONNACK_REFUSED);
        }
        return;
    }

    if (type == MQ_PUBLISH) {
        uint16_t topic_len;
        uint16_t pid = 0;
        uint8_t qos = (uint8_t) ((flags >> 1) & 0x03U);
        uint32_t off;
        app_mqtt_msg_t msg;

        if (len < 2U) {
            m->stats.protocol_errors++;
            return;
        }
        topic_len = mq_get_u16(body);
        if ((uint32_t) topic_len + 2U > len) {
            m->stats.protocol_errors++;
            return;
        }
        if (topic_len > APP_MQTT_TOPIC_MAX) {
            m->stats.protocol_errors++;
            return;
        }
        off = 2U + topic_len;
        if (qos > 0U) {
            if (off + 2U > len) {
                m->stats.protocol_errors++;
                return;
            }
            pid = mq_get_u16(&body[off]);
            off += 2U;
        }
        if (qos == 2U) {
            /* 本组件不支持 QoS2 收包（见头文件"不支持"），直接断链避免半开状态 */
            m->stats.protocol_errors++;
            mq_close_tcp(m, true);
            mq_schedule_reconnect(m, APP_MQTT_EV_TCP_LOST);
            return;
        }
        memset(&msg, 0, sizeof(msg));
        memcpy(msg.topic, &body[2], topic_len);
        msg.topic[topic_len] = '\0';
        msg.payload = &body[off];
        msg.payload_len = (uint16_t) (len - off);
        msg.qos = qos;
        msg.retain = ((flags & 0x01U) != 0U);
        msg.dup = ((flags & 0x08U) != 0U);
        m->stats.publish_rx++;
        m->stats.publish_rx_bytes += msg.payload_len;
        if (qos == 1U) {
            mq_send_puback(m, pid);
        }
        if (m->on_message != NULL) {
            m->on_message(m, &msg, m->user);
        }
        return;
    }

    if (type == MQ_PUBACK) {
        uint16_t pid;
        app_mqtt_inflight_t *slot;
        if (len < 2U) {
            m->stats.protocol_errors++;
            return;
        }
        pid = mq_get_u16(body);
        slot = mq_find_slot_by_pid(m, pid);
        if (slot != NULL) {
            mq_free_slot(m, slot);
        }
        m->stats.puback_rx++;
        return;
    }

    if (type == MQ_SUBACK) {
        /* body = packet id(2) + 返回码列表；本组件只订阅单主题，记录返回码 0 与否由上层看 */
        if (len >= 3U) {
            /* 什么都不用做：订阅成功与否已由 broker 决定，例程只需知道 SUBACK 到了 */
        }
        return;
    }

    if (type == MQ_UNSUBACK) {
        return;
    }

    if (type == MQ_PINGRESP) {
        uint32_t rtt = sys_now() - m->ping_tx_ms;
        m->stats.pingresp_rx++;
        m->ping_outstanding = false;
        m->stats.last_rtt_ms = rtt;
        m->rtt_sum_ms += rtt;
        if (m->stats.pingresp_rx > 0U) {
            m->stats.rtt_avg_ms = m->rtt_sum_ms / m->stats.pingresp_rx;
        }
        return;
    }

    /* PUBREC/PUBREL/PUBCOMP 或未知类型：协议错误计数，不断链（broker 不应主动发） */
    m->stats.protocol_errors++;
}

/* ---------------- 公共 API ---------------- */
int app_mqtt_init(app_mqtt_t *m, const app_mqtt_cfg_t *cfg)
{
    static const app_mqtt_cfg_t def = APP_MQTT_CFG_DEFAULT;
    uint16_t n;

    if (m == NULL) {
        return -1;
    }
    if (cfg == NULL) {
        cfg = &def;
    }
    memset(m, 0, sizeof(*m));
    m->cfg = *cfg;
    if ((m->cfg.host == NULL) || (m->cfg.client_id == NULL)) {
        return -1;
    }
    n = mq_strnlen(m->cfg.host, sizeof(m->cfg_host) - 1U);
    if ((n == 0U) || (n >= sizeof(m->cfg_host))) {
        return -1;
    }
    memcpy(m->cfg_host, m->cfg.host, n);
    m->cfg_host[n] = '\0';

    n = mq_strnlen(m->cfg.client_id, sizeof(m->cfg_client_id) - 1U);
    if (n == 0U) {
        return -1;
    }
    memcpy(m->cfg_client_id, m->cfg.client_id, n);

    if (m->cfg.username != NULL) {
        n = mq_strnlen(m->cfg.username, sizeof(m->cfg_user) - 1U);
        memcpy(m->cfg_user, m->cfg.username, n);
    }
    if (m->cfg.password != NULL) {
        n = mq_strnlen(m->cfg.password, sizeof(m->cfg_pass) - 1U);
        memcpy(m->cfg_pass, m->cfg.password, n);
    }
    if (m->cfg.will_topic != NULL) {
        n = mq_strnlen(m->cfg.will_topic, sizeof(m->cfg_will_topic) - 1U);
        memcpy(m->cfg_will_topic, m->cfg.will_topic, n);
    }
    if (m->cfg.will_payload != NULL) {
        n = mq_strnlen(m->cfg.will_payload, sizeof(m->cfg_will_payload) - 1U);
        memcpy(m->cfg_will_payload, m->cfg.will_payload, n);
    }
    m->cfg.host = m->cfg_host;
    m->cfg.client_id = m->cfg_client_id;
    m->cfg.username = (m->cfg_user[0] != '\0') ? m->cfg_user : NULL;
    m->cfg.password = (m->cfg_pass[0] != '\0') ? m->cfg_pass : NULL;
    m->cfg.will_topic = (m->cfg_will_topic[0] != '\0') ? m->cfg_will_topic : NULL;
    m->cfg.will_payload = m->cfg_will_payload;

    if (m->cfg.port == 0U) {
        m->cfg.port = 1883U;
    }
    if (m->cfg.connect_timeout_ms == 0U) {
        m->cfg.connect_timeout_ms = 5000U;
    }
    if (m->cfg.reconnect_min_ms == 0U) {
        m->cfg.reconnect_min_ms = 1000U;
    }
    if (m->cfg.reconnect_max_ms < m->cfg.reconnect_min_ms) {
        m->cfg.reconnect_max_ms = m->cfg.reconnect_min_ms;
    }
    if (m->cfg.qos1_retry_ms == 0U) {
        m->cfg.qos1_retry_ms = 5000U;
    }
    if (m->cfg.qos1_max_retry == 0U) {
        m->cfg.qos1_max_retry = 3U;
    }
    if (m->cfg.keepalive_s > 65535U) {
        m->cfg.keepalive_s = 65535U;
    }
    m->next_pid = 0U;
    m->backoff_ms = m->cfg.reconnect_min_ms;
    m->state = APP_MQTT_STATE_IDLE;
    return 0;
}

void app_mqtt_set_state_cb(app_mqtt_t *m, void (*cb)(app_mqtt_t *, app_mqtt_state_t, int, void *),
                           void *user)
{
    if (m != NULL) {
        m->on_state = cb;
        if (user != NULL) {
            m->user = user;
        }
    }
}

void app_mqtt_set_message_cb(app_mqtt_t *m, void (*cb)(app_mqtt_t *, const app_mqtt_msg_t *, void *),
                             void *user)
{
    if (m != NULL) {
        m->on_message = cb;
        if (user != NULL) {
            m->user = user;
        }
    }
}

int app_mqtt_connect(app_mqtt_t *m)
{
    ip4_addr_t ip4;
    ip_addr_t ipa;
    struct tcp_pcb *pcb;
    err_t err;

    if (m == NULL) {
        return -1;
    }
    if ((m->pcb != NULL) || (m->state == APP_MQTT_STATE_CONNECTED) ||
        (m->state == APP_MQTT_STATE_WAIT_CONNACK)) {
        return 0;
    }
    if (!ip4addr_aton(m->cfg_host, &ip4)) {
        return -1;
    }
    ip_addr_copy_from_ip4(ipa, ip4);

    pcb = tcp_new();
    if (pcb == NULL) {
        return -2;
    }
    tcp_arg(pcb, m);
    tcp_err(pcb, mq_on_err);
    tcp_recv(pcb, mq_on_recv);
    tcp_sent(pcb, mq_on_sent);
    m->pcb = pcb;
    m->state = APP_MQTT_STATE_TCP_CONNECTING;
    m->connect_start_ms = sys_now();
    m->rx_len = 0;
    m->txq_head = 0;
    m->txq_tail = 0;
    m->ping_outstanding = false;

    err = tcp_connect(pcb, &ipa, m->cfg.port, mq_on_connected);
    if (err != ERR_OK) {
        mq_close_tcp(m, true);
        m->state = APP_MQTT_STATE_IDLE;
        return -2;
    }
    return 0;
}

void app_mqtt_poll(app_mqtt_t *m)
{
    uint32_t now;

    if (m == NULL) {
        return;
    }
    now = sys_now();

    switch (m->state) {
    case APP_MQTT_STATE_TCP_CONNECTING:
        if ((now - m->connect_start_ms) > m->cfg.connect_timeout_ms) {
            m->stats.disconnects++;
            mq_close_tcp(m, true);
            mq_schedule_reconnect(m, APP_MQTT_EV_TCP_LOST);
        }
        break;

    case APP_MQTT_STATE_WAIT_CONNACK:
        if ((now - m->connect_start_ms) > m->cfg.connect_timeout_ms) {
            m->stats.disconnects++;
            mq_close_tcp(m, false);
            mq_schedule_reconnect(m, APP_MQTT_EV_TCP_LOST);
        }
        break;

    case APP_MQTT_STATE_CONNECTED: {
        uint8_t i;
        /* keepalive：空闲够久就发 PINGREQ；PINGRESP 超时就断链重连 */
        if (m->cfg.keepalive_s > 0U) {
            uint32_t ka = (uint32_t) m->cfg.keepalive_s * 1000U;
            uint32_t idle_limit = (ka > 2000U) ? (ka - 1000U) : (ka / 2U);
            uint32_t pong_limit = (ka < 5000U) ? ka : 5000U;
            if (m->ping_outstanding) {
                if ((now - m->ping_tx_ms) > pong_limit) {
                    m->stats.ping_timeouts++;
                    mq_close_tcp(m, true);
                    mq_schedule_reconnect(m, APP_MQTT_EV_TCP_LOST);
                    break;
                }
            } else if ((now - m->last_tx_ms) >= idle_limit) {
                mq_send_pingreq(m);
            }
        }
        /* QoS1 超时重发（同 packet id + DUP=1，MQTT 3.1.1 §3.3.1.1） */
        for (i = 0; i < APP_MQTT_INFLIGHT_MAX; i++) {
            app_mqtt_inflight_t *slot = &m->inflight[i];
            if (!slot->used) {
                continue;
            }
            if ((now - slot->last_ms) < m->cfg.qos1_retry_ms) {
                continue;
            }
            if (slot->retries >= m->cfg.qos1_max_retry) {
                mq_free_slot(m, slot);
                m->stats.drops++;
                continue;
            }
            if (mq_build_publish_from_slot(m, slot, true) == 0) {
                slot->retries++;
                slot->last_ms = now;
                m->stats.retransmits++;
            }
        }
        break;
    }

    case APP_MQTT_STATE_WAIT_RECONNECT:
        if (now >= m->reconnect_at_ms) {
            m->stats.reconnects++;
            m->state = APP_MQTT_STATE_IDLE;
            (void) app_mqtt_connect(m);
        }
        break;

    case APP_MQTT_STATE_IDLE:
    case APP_MQTT_STATE_CLOSED:
    default:
        break;
    }

    if (m->pcb != NULL) {
        mq_tx_flush(m);
    }
}

void app_mqtt_disconnect(app_mqtt_t *m, bool graceful)
{
    if (m == NULL) {
        return;
    }
    if (m->pcb == NULL) {
        m->state = APP_MQTT_STATE_IDLE;
        return;
    }
    if (graceful) {
        uint8_t pkt[2];
        /* 正常流程：先发 DISCONNECT，broker 收到后不会发遗嘱（MQTT 3.1.1 §3.14） */
        mq_build_header(pkt, MQ_DISCONNECT, 0U, 0U);
        (void) mq_tx_enqueue(m, pkt, 2U);
        mq_tx_flush(m);
        m->cfg.auto_reconnect = false;
        mq_close_tcp(m, false);
        m->state = APP_MQTT_STATE_CLOSED;
        if (m->on_state != NULL) {
            m->on_state(m, APP_MQTT_STATE_CLOSED, APP_MQTT_EV_DISCONNECTED, m->user);
        }
    } else {
        /* 异常流程：直接 RST/断链（模拟掉电、拔网线），交给自动重连，并让 broker 发遗嘱 */
        m->stats.disconnects++;
        mq_close_tcp(m, true);
        mq_schedule_reconnect(m, APP_MQTT_EV_TCP_LOST);
    }
}

int app_mqtt_publish(app_mqtt_t *m, const char *topic, const void *payload, uint16_t len,
                     uint8_t qos, bool retain)
{
    uint8_t pkt[APP_MQTT_MAX_PACKET];
    uint16_t topic_len;
    uint16_t pid = 0U;
    uint32_t remlen;
    uint16_t hdr;
    uint32_t off;
    app_mqtt_inflight_t *slot = NULL;

    if ((m == NULL) || (topic == NULL) || (payload == NULL)) {
        return -1;
    }
    if (qos > 1U) {
        return -4;
    }
    topic_len = mq_strnlen(topic, APP_MQTT_TOPIC_MAX + 1U);
    if ((topic_len == 0U) || (topic_len > APP_MQTT_TOPIC_MAX)) {
        return -1;
    }
    if (m->state != APP_MQTT_STATE_CONNECTED) {
        return -2;
    }
    if (qos == 1U) {
        slot = mq_find_free_slot(m);
        if ((slot == NULL) || (len > APP_MQTT_INFLIGHT_PAYLOAD)) {
            return -3;
        }
    }
    remlen = 2U + topic_len + ((qos > 0U) ? 2U : 0U) + len;
    if (remlen > (APP_MQTT_MAX_PACKET - 5U)) {
        return -1;
    }
    hdr = mq_build_header(pkt, MQ_PUBLISH,
                          (uint8_t) ((qos << 1) | (retain ? 1U : 0U)), remlen);
    off = hdr;
    mq_put_u16(&pkt[off], topic_len);
    off += 2U;
    memcpy(&pkt[off], topic, topic_len);
    off += topic_len;
    if (qos == 1U) {
        pid = mq_alloc_pid(m);
        mq_put_u16(&pkt[off], pid);
        off += 2U;
    }
    if (len > 0U) {
        memcpy(&pkt[off], payload, len);
        off += len;
    }
    if (qos == 1U) {
        slot->used = true;
        slot->pid = pid;
        slot->qos = 1U;
        slot->retain = retain;
        slot->retries = 0U;
        slot->topic_len = topic_len;
        slot->payload_len = len;
        slot->last_ms = sys_now();
        memcpy(slot->topic, topic, topic_len);
        if (len > 0U) {
            memcpy(slot->payload, payload, len);
        }
        m->stats.inflight++;
    }
    if (mq_tx_enqueue(m, pkt, (uint16_t) off) != 0) {
        if (slot != NULL) {
            mq_free_slot(m, slot);
        }
        return -3;
    }
    m->stats.publish_tx++;
    m->stats.publish_tx_bytes += len;
    return (int) pid;
}

int app_mqtt_subscribe(app_mqtt_t *m, const char *topic_filter, uint8_t qos)
{
    uint8_t pkt[APP_MQTT_MAX_PACKET];
    uint16_t tlen;
    uint32_t remlen;
    uint16_t hdr;
    uint32_t off;

    if ((m == NULL) || (topic_filter == NULL)) {
        return -3;
    }
    tlen = mq_strnlen(topic_filter, APP_MQTT_TOPIC_MAX + 1U);
    if ((tlen == 0U) || (tlen > APP_MQTT_TOPIC_MAX) || (qos > 1U)) {
        return -3;
    }
    if (m->state != APP_MQTT_STATE_CONNECTED) {
        return -2;
    }
    remlen = 2U + 2U + tlen + 1U;
    hdr = mq_build_header(pkt, MQ_SUBSCRIBE, 0x02U, remlen);   /* SUBSCRIBE 固定头低 4 位必须 0b0010 */
    off = hdr;
    mq_put_u16(&pkt[off], mq_alloc_pid(m));
    off += 2U;
    off += mq_put_str(&pkt[off], topic_filter, tlen);
    pkt[off++] = qos;
    return (mq_tx_enqueue(m, pkt, (uint16_t) off) == 0) ? 0 : -3;
}

int app_mqtt_unsubscribe(app_mqtt_t *m, const char *topic_filter)
{
    uint8_t pkt[APP_MQTT_MAX_PACKET];
    uint16_t tlen;
    uint32_t remlen;
    uint16_t hdr;
    uint32_t off;

    if ((m == NULL) || (topic_filter == NULL)) {
        return -3;
    }
    tlen = mq_strnlen(topic_filter, APP_MQTT_TOPIC_MAX + 1U);
    if ((tlen == 0U) || (tlen > APP_MQTT_TOPIC_MAX)) {
        return -3;
    }
    if (m->state != APP_MQTT_STATE_CONNECTED) {
        return -2;
    }
    remlen = 2U + 2U + tlen;
    hdr = mq_build_header(pkt, MQ_UNSUBSCRIBE, 0x02U, remlen);
    off = hdr;
    mq_put_u16(&pkt[off], mq_alloc_pid(m));
    off += 2U;
    off += mq_put_str(&pkt[off], topic_filter, tlen);
    return (mq_tx_enqueue(m, pkt, (uint16_t) off) == 0) ? 0 : -3;
}

bool app_mqtt_is_connected(const app_mqtt_t *m)
{
    return (m != NULL) && (m->state == APP_MQTT_STATE_CONNECTED);
}

app_mqtt_state_t app_mqtt_state(const app_mqtt_t *m)
{
    return (m != NULL) ? m->state : APP_MQTT_STATE_IDLE;
}

const char *app_mqtt_state_str(app_mqtt_state_t st)
{
    switch (st) {
    case APP_MQTT_STATE_IDLE:              return "IDLE";
    case APP_MQTT_STATE_TCP_CONNECTING:    return "TCP_CONNECTING";
    case APP_MQTT_STATE_WAIT_CONNACK:      return "WAIT_CONNACK";
    case APP_MQTT_STATE_CONNECTED:         return "CONNECTED";
    case APP_MQTT_STATE_WAIT_RECONNECT:    return "WAIT_RECONNECT";
    case APP_MQTT_STATE_CLOSED:            return "CLOSED";
    default:                               return "?";
    }
}

void app_mqtt_get_stats(const app_mqtt_t *m, app_mqtt_stats_t *out)
{
    if ((m != NULL) && (out != NULL)) {
        *out = m->stats;
    }
}

void app_mqtt_reset_stats(app_mqtt_t *m)
{
    if (m != NULL) {
        memset(&m->stats, 0, sizeof(m->stats));
        m->rtt_sum_ms = 0U;
        m->stats.inflight = 0U;
        {
            uint8_t i;
            for (i = 0; i < APP_MQTT_INFLIGHT_MAX; i++) {
                if (m->inflight[i].used) {
                    m->stats.inflight++;
                }
            }
        }
    }
}

uint16_t app_mqtt_debug_connect(const app_mqtt_t *m, uint8_t *buf, uint16_t cap)
{
    uint16_t n;
    if ((m == NULL) || (buf == NULL) || (cap < 16U)) {
        return 0U;
    }
    n = mq_build_connect((app_mqtt_t *) m, buf, cap);
    return (n <= cap) ? n : 0U;
}

uint16_t app_mqtt_last_packet(const app_mqtt_t *m, const uint8_t **buf)
{
    if ((m == NULL) || (buf == NULL)) {
        return 0U;
    }
    *buf = m->last_tx_pkt;
    return m->last_tx_pkt_len;
}
