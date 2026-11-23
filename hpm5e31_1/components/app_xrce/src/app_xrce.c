/*
 * app_xrce.c —— Micro-XRCE-DDS 会话/实体管理（板端，无 OS、无堆）
 *
 * 层次：
 *   app_xrce_*          本文件：会话、实体、发布/订阅、统计、断线重连
 *   app_xrce_transport  lwIP UDP custom transport
 *   app_xrce_platform   uxr_millis()/uxr_nanos() 时基钩子
 *
 * 与 micro-ROS 的关系：实体一律用"二进制表示"创建（REPRESENTATION_IN_BINARY），
 * 这与 micro-ROS 客户端走的是 Agent 的同一条代码路径（create_*_by_bin），
 * 因此 topic 名/类型名直接就是 DDS 名字，不需要 Agent 上的 XML 配置。
 *
 * 注意：Agent 侧 TopicPubSubType 在处理载荷时**自己会加/去 4 字节 CDR 封装头**，
 * 所以本组件收发的 cdr 缓冲区是"纯 CDR 体"，不含 0x00 0x01 0x00 0x00 头。
 */
#include "app_xrce.h"
#include <string.h>

#include <uxr/client/client.h>
#include <uxr/client/util/time.h>
#include <ucdr/microcdr.h>

/* ---- 传输层内部接口（app_xrce_transport.c） ---- */
void app_xrce_transport_configure(uint32_t agent_ip, uint16_t agent_port, uint16_t local_port);
void app_xrce_transport_set_poll(void (*fn)(void));
void app_xrce_transport_bind(uxrCustomTransport *transport);
void app_xrce_transport_get_counters(uint32_t *tx_dg, uint32_t *tx_bytes, uint32_t *rx_dg,
                                     uint32_t *rx_bytes, uint32_t *overrun, uint32_t *errors);

#define APP_XRCE_PARTICIPANT_OID 0x01
#define APP_XRCE_PUBLISHER_OID   0x01
#define APP_XRCE_SUBSCRIBER_OID  0x01

#define APP_XRCE_ENTITY_TIMEOUT_MS 2000
#define APP_XRCE_LINK_LOST_MS      5000   /* 可靠流这么久没有 ACK 前进即判定掉线 */
#define APP_XRCE_RETRY_MS          2000

typedef struct {
    bool used;
    app_xrce_pub_cfg_t cfg;
    uxrObjectId topic_id;
    uxrObjectId writer_id;
    uxrStreamId out;
} app_xrce_pub_state_t;

typedef struct {
    bool used;
    app_xrce_sub_cfg_t cfg;
    uxrObjectId topic_id;
    uxrObjectId reader_id;
    uint8_t raw_id[2];
} app_xrce_sub_state_t;

static app_xrce_cfg_t s_cfg;
static uxrCustomTransport s_transport;
static uxrSession s_session;
static uxrStreamId s_out_rel, s_out_be, s_in_rel;
static app_xrce_pub_state_t s_pubs[APP_XRCE_MAX_PUBS];
static app_xrce_sub_state_t s_subs[APP_XRCE_MAX_SUBS];
static app_xrce_stats_t s_stats;
static bool s_init;
static bool s_up;
static int64_t s_boot_ms;
static int64_t s_last_ack_ms;
static int64_t s_last_retry_ms;
static uxrSeqNum s_prev_ack;
static uxrSeqNum s_prev_sent;

/* 每个 READ_DATA 只取 1 个样本，取回后在 on_topic 里立刻重新武装。 */
static const uxrDeliveryControl s_delivery = {
    .max_samples = 1,
    .max_elapsed_time = 0,
    .max_bytes_per_second = 0,
    .min_pace_period = 0
};

static void arm_read(app_xrce_sub_state_t *sub);
static int  create_pub_entity(app_xrce_pub_state_t *pub);
static int  create_sub_entity(app_xrce_sub_state_t *sub);

/* ------------------------------------------------------------------ */
/* 接收回调：Agent → 板端                                              */
/* ------------------------------------------------------------------ */
static void on_topic(uxrSession *session, uxrObjectId object_id, uint16_t request_id,
                     uxrStreamId stream_id, struct ucdrBuffer *ub, uint16_t length, void *args)
{
    (void)session; (void)request_id; (void)stream_id; (void)args;

    uint8_t raw[2] = {0};
    uxr_object_id_to_raw(object_id, raw);

    for (int i = 0; i < APP_XRCE_MAX_SUBS; i++) {
        app_xrce_sub_state_t *sub = &s_subs[i];
        if (!sub->used) {
            continue;
        }
        if ((sub->raw_id[0] != raw[0]) || (sub->raw_id[1] != raw[1])) {
            continue;
        }
        s_stats.rx_samples++;
        if (sub->cfg.on_data != NULL) {
            sub->cfg.on_data(sub->cfg.arg, ub->iterator, (uint32_t)length);
        }
        arm_read(sub);   /* 读请求在 Agent 侧是一次性的，必须重新武装 */
        return;
    }
}

/* ------------------------------------------------------------------ */
/* 实体创建                                                            */
/* ------------------------------------------------------------------ */
static uxrQoS_t qos_of(bool reliable, uint16_t depth)
{
    uxrQoS_t q;
    q.durability = UXR_DURABILITY_VOLATILE;
    q.reliability = reliable ? UXR_RELIABILITY_RELIABLE : UXR_RELIABILITY_BEST_EFFORT;
    q.history = UXR_HISTORY_KEEP_LAST;
    q.depth = (depth == 0U) ? 8U : depth;
    return q;
}

static int create_pub_entity(app_xrce_pub_state_t *pub)
{
    uxrObjectId pid = uxr_object_id(APP_XRCE_PARTICIPANT_OID, UXR_PARTICIPANT_ID);
    uxrObjectId pubid = uxr_object_id(APP_XRCE_PUBLISHER_OID, UXR_PUBLISHER_ID);

    pub->topic_id = uxr_object_id(pub->cfg.writer_oid, UXR_TOPIC_ID);
    pub->writer_id = uxr_object_id(pub->cfg.writer_oid, UXR_DATAWRITER_ID);

    uint16_t rq[2];
    uint8_t st[2];
    rq[0] = uxr_buffer_create_topic_bin(&s_session, s_out_rel, pub->topic_id, pid,
                                        pub->cfg.topic_name, pub->cfg.type_name, UXR_REPLACE);
    rq[1] = uxr_buffer_create_datawriter_bin(&s_session, s_out_rel, pub->writer_id, pubid, pub->topic_id,
                                             qos_of(pub->cfg.reliable_qos, pub->cfg.history_depth), UXR_REPLACE);
    if (!uxr_run_session_until_all_status(&s_session, APP_XRCE_ENTITY_TIMEOUT_MS, rq, st, 2)) {
        return -1;
    }
    pub->out = pub->cfg.reliable_stream ? s_out_rel : s_out_be;
    return 0;
}

static int create_sub_entity(app_xrce_sub_state_t *sub)
{
    uxrObjectId pid = uxr_object_id(APP_XRCE_PARTICIPANT_OID, UXR_PARTICIPANT_ID);
    uxrObjectId subid = uxr_object_id(APP_XRCE_SUBSCRIBER_OID, UXR_SUBSCRIBER_ID);

    sub->topic_id = uxr_object_id(sub->cfg.reader_oid, UXR_TOPIC_ID);
    sub->reader_id = uxr_object_id(sub->cfg.reader_oid, UXR_DATAREADER_ID);
    uxr_object_id_to_raw(sub->reader_id, sub->raw_id);

    uint16_t rq[2];
    uint8_t st[2];
    rq[0] = uxr_buffer_create_topic_bin(&s_session, s_out_rel, sub->topic_id, pid,
                                        sub->cfg.topic_name, sub->cfg.type_name, UXR_REPLACE);
    rq[1] = uxr_buffer_create_datareader_bin(&s_session, s_out_rel, sub->reader_id, subid, sub->topic_id,
                                             qos_of(sub->cfg.reliable_qos, sub->cfg.history_depth), UXR_REPLACE);
    if (!uxr_run_session_until_all_status(&s_session, APP_XRCE_ENTITY_TIMEOUT_MS, rq, st, 2)) {
        return -1;
    }
    arm_read(sub);
    return 0;
}

static void arm_read(app_xrce_sub_state_t *sub)
{
    (void)uxr_buffer_request_data(&s_session, s_out_rel, sub->reader_id, s_in_rel, &s_delivery);
}

/* 建会话 + 流 + Participant/Publisher/Subscriber + 所有已注册实体 */
static int bringup(void)
{
    if (!uxr_create_session(&s_session)) {
        return -1;
    }
    s_out_rel = uxr_create_output_reliable_stream(&s_session, s_cfg.out_reliable_buf,
                                                  s_cfg.out_reliable_size, s_cfg.out_reliable_history);
    s_in_rel  = uxr_create_input_reliable_stream(&s_session, s_cfg.in_reliable_buf,
                                                 s_cfg.in_reliable_size, s_cfg.in_reliable_history);
    s_out_be  = uxr_create_output_best_effort_stream(&s_session, s_cfg.out_be_buf, s_cfg.out_be_size);

    uxrObjectId pid = uxr_object_id(APP_XRCE_PARTICIPANT_OID, UXR_PARTICIPANT_ID);
    uxrObjectId pubid = uxr_object_id(APP_XRCE_PUBLISHER_OID, UXR_PUBLISHER_ID);
    uxrObjectId subid = uxr_object_id(APP_XRCE_SUBSCRIBER_OID, UXR_SUBSCRIBER_ID);

    uint16_t rq[3];
    uint8_t st[3];
    rq[0] = uxr_buffer_create_participant_bin(&s_session, s_out_rel, pid, s_cfg.domain_id,
                                              s_cfg.participant_name, UXR_REPLACE);
    rq[1] = uxr_buffer_create_publisher_bin(&s_session, s_out_rel, pubid, pid, UXR_REPLACE);
    rq[2] = uxr_buffer_create_subscriber_bin(&s_session, s_out_rel, subid, pid, UXR_REPLACE);
    if (!uxr_run_session_until_all_status(&s_session, APP_XRCE_ENTITY_TIMEOUT_MS, rq, st, 3)) {
        return -2;
    }

    for (int i = 0; i < APP_XRCE_MAX_PUBS; i++) {
        if (s_pubs[i].used && (create_pub_entity(&s_pubs[i]) != 0)) {
            return -3;
        }
    }
    for (int i = 0; i < APP_XRCE_MAX_SUBS; i++) {
        if (s_subs[i].used && (create_sub_entity(&s_subs[i]) != 0)) {
            return -4;
        }
    }

    s_prev_ack = s_session.streams.output_reliable[0].last_acknown;
    s_prev_sent = s_session.streams.output_reliable[0].last_sent;
    s_last_ack_ms = uxr_millis();
    return 0;
}

/* ------------------------------------------------------------------ */
/* 公共 API                                                            */
/* ------------------------------------------------------------------ */
int app_xrce_init(const app_xrce_cfg_t *cfg)
{
    if ((cfg == NULL) || (cfg->out_reliable_buf == NULL) || (cfg->in_reliable_buf == NULL) ||
        (cfg->out_be_buf == NULL)) {
        return -1;
    }
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_pubs, 0, sizeof(s_pubs));
    memset(s_subs, 0, sizeof(s_subs));
    s_cfg = *cfg;
    s_init = false;
    s_up = false;
    s_boot_ms = uxr_millis();
    s_last_retry_ms = 0;

    app_xrce_transport_configure(cfg->agent_ip, cfg->agent_port, cfg->local_port);
    app_xrce_transport_bind(&s_transport);
    if (!uxr_init_custom_transport(&s_transport, NULL)) {
        return -2;
    }
    uxr_init_session(&s_session, &s_transport.comm, cfg->key);
    uxr_set_topic_callback(&s_session, on_topic, NULL);

    s_init = true;
    int rc = bringup();
    if (rc != 0) {
        return -3;
    }
    s_up = true;
    return 0;
}

int app_xrce_add_pub(const app_xrce_pub_cfg_t *cfg)
{
    if ((cfg == NULL) || (cfg->topic_name == NULL) || (cfg->type_name == NULL) || (cfg->writer_oid == 0U)) {
        return -1;
    }
    for (int i = 0; i < APP_XRCE_MAX_PUBS; i++) {
        if (!s_pubs[i].used) {
            s_pubs[i].used = true;
            s_pubs[i].cfg = *cfg;
            if (s_up && (create_pub_entity(&s_pubs[i]) != 0)) {
                s_pubs[i].used = false;
                return -2;
            }
            return i;
        }
    }
    return -3;
}

int app_xrce_add_sub(const app_xrce_sub_cfg_t *cfg)
{
    if ((cfg == NULL) || (cfg->topic_name == NULL) || (cfg->type_name == NULL) || (cfg->reader_oid == 0U)) {
        return -1;
    }
    for (int i = 0; i < APP_XRCE_MAX_SUBS; i++) {
        if (!s_subs[i].used) {
            s_subs[i].used = true;
            s_subs[i].cfg = *cfg;
            if (s_up && (create_sub_entity(&s_subs[i]) != 0)) {
                s_subs[i].used = false;
                return -2;
            }
            return i;
        }
    }
    return -3;
}

int app_xrce_publish(int pub_idx, const void *cdr, uint32_t len)
{
    if (!s_init || !s_up || (pub_idx < 0) || (pub_idx >= APP_XRCE_MAX_PUBS) || !s_pubs[pub_idx].used) {
        return -1;
    }
    if ((cdr == NULL) || (len == 0U) || (len > (APP_XRCE_MTU - 16U))) {
        return -2;
    }
    app_xrce_pub_state_t *pub = &s_pubs[pub_idx];

    ucdrBuffer ub;
    uint16_t rq = uxr_prepare_output_stream(&s_session, pub->out, pub->writer_id, &ub, len);
    if ((rq == UXR_INVALID_REQUEST_ID) || ub.error) {
        s_stats.tx_drops++;
        return -3;
    }
    if (!ucdr_serialize_array_uint8_t(&ub, (const uint8_t *)cdr, len)) {
        s_stats.tx_drops++;
        return -4;
    }
    s_stats.tx_samples++;
    return 0;
}

void app_xrce_poll(void)
{
    if (!s_init) {
        return;
    }
    int64_t now = uxr_millis();

    if (!s_up) {
        if ((now - s_last_retry_ms) >= APP_XRCE_RETRY_MS) {
            s_last_retry_ms = now;
            if (bringup() == 0) {
                s_up = true;
                s_stats.reconnects++;
            }
        }
        return;
    }

    (void)uxr_run_session_time(&s_session, (int) s_cfg.poll_timeout_ms);

    /* 可靠流统计：last_acknown 前进 = 被确认；last_sent 回退 = 重传 */
    if (s_session.streams.output_reliable_size > 0U) {
        const uxrOutputReliableStream *rs = &s_session.streams.output_reliable[0];
        int d = (int)(int16_t)(rs->last_acknown - s_prev_ack);
        if (d > 0) {
            s_stats.acks += (uint32_t)d;
            s_prev_ack = rs->last_acknown;
            s_last_ack_ms = now;
        }
        int e = (int)(int16_t)(rs->last_sent - s_prev_sent);
        if (e < 0) {
            s_stats.retransmits += (uint32_t)(-e);
        }
        s_prev_sent = rs->last_sent;
    }

    if ((s_stats.tx_samples > 0U) && ((now - s_last_ack_ms) > APP_XRCE_LINK_LOST_MS)) {
        s_up = false;
        s_last_retry_ms = now;
    }
}

void app_xrce_set_net_poll(void (*fn)(void))
{
    app_xrce_transport_set_poll(fn);
}

void app_xrce_get_stats(app_xrce_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    uint32_t tx_dg = 0, tx_b = 0, rx_dg = 0, rx_b = 0, ovr = 0, err = 0;
    app_xrce_transport_get_counters(&tx_dg, &tx_b, &rx_dg, &rx_b, &ovr, &err);
    s_stats.tx_datagrams = tx_dg;
    s_stats.tx_bytes = tx_b;
    /* tx_drops 由 app_xrce_publish() 累加，这里只做汇总 */
    s_stats.rx_datagrams = rx_dg;
    s_stats.rx_bytes = rx_b;
    s_stats.rx_overrun = ovr;
    s_stats.net_errors = err;
    s_stats.connected = s_up;
    s_stats.uptime_ms = (uint32_t)(uxr_millis() - s_boot_ms);
    *out = s_stats;
}

bool app_xrce_connected(void)
{
    return s_up;
}

const char *app_xrce_state_str(void)
{
    if (!s_init) {
        return "uninit";
    }
    return s_up ? "connected" : "down";
}
