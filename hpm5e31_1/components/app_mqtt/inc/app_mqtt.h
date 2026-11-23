/*
 * app_mqtt.h - MQTT 3.1.1 客户端组件（裸机 lwIP TCP，无 OS、无 malloc）
 *
 * 面向 HPM5E31 单线程裸机：所有收发都是非阻塞的，主循环调用 app_mqtt_poll()
 * 推进 TCP/keepalive/重连/QoS1 重传状态机。回调只会在 app_mqtt_poll() 内部触发。
 *
 * 最小用法（3 步）：
 *     static app_mqtt_t mq;
 *     app_mqtt_init(&mq, NULL);              // 默认 broker 192.168.2.1:1883
 *     app_mqtt_connect(&mq);
 *     while (1) { app_lwip_poll(); app_mqtt_poll(&mq); }
 *
 * 支持（MQTT 3.1.1 / OASIS Standard）：
 *   - CONNECT / CONNACK（含用户名口令、遗嘱 LWT、clean session）
 *   - PUBLISH QoS0 / QoS1（QoS1 未确认自动重发 DUP，窗口 8）
 *   - PUBACK（收：自动回；发：匹配未确认表）
 *   - SUBSCRIBE / SUBACK、UNSUBSCRIBE / UNSUBACK
 *   - PINGREQ / PINGRESP（keepalive 保活 + 超时判定）
 *   - DISCONNECT（优雅关闭）；异常断线自动重连（指数退避）
 *   - 剩余长度（Remaining Length）1~4 字节变长编码/解码
 *   - 主题过滤器通配符匹配 "+"/"#"（用于板内按订阅过滤分发与自测）
 *
 * 不支持（见第 38 章"待确认项"）：
 *   - QoS2（PUBREC/PUBREL/PUBCOMP）：收到 QoS2 PUBLISH 视为协议错误并断链
 *   - TLS（明文 TCP 1883）；MQTTS 需要 mbedTLS，本组件不内置
 *   - 会话持久化（clean_session=0 的离线下发）：本板不做本地会话缓存
 *   - 域名解析：broker 只接受 IPv4 点分十进制字符串
 *
 * 头文件：components/app_mqtt/inc/app_mqtt.h
 * 作者：tp-mqtt（第 38 章）
 */
#ifndef APP_MQTT_H
#define APP_MQTT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MQTT_VERSION_STR        "1.0.0"

/* ---- 编译期容量（改大要核算 RAM；当前总静态占用约 4.3 KB） ---- */
#define APP_MQTT_MAX_PACKET         (512U)   /* 单条报文上限（含固定头）；超长断链 */
#define APP_MQTT_RX_BUF             (2048U)  /* 接收重组缓冲；要 >= 一个 TCP MSS(1460)+ 最大报文 */
#define APP_MQTT_TXQ_SIZE           (1024U)  /* 发送环形队列，TCP sndbuf 满时暂存 */
#define APP_MQTT_INFLIGHT_MAX       (8U)     /* QoS1 未确认窗口 */
#define APP_MQTT_INFLIGHT_PAYLOAD   (192U)   /* 单条 QoS1 未确认载荷上限 */
#define APP_MQTT_TOPIC_MAX          (96U)    /* 主题/过滤器最大长度（不含结尾 0） */
#define APP_MQTT_CLIENT_ID_MAX      (48U)
#define APP_MQTT_USER_MAX           (32U)
#define APP_MQTT_PASS_MAX           (32U)
#define APP_MQTT_WILL_PAYLOAD_MAX   (128U)

/* ---- 连接状态 ---- */
typedef enum app_mqtt_state_enum {
    APP_MQTT_STATE_IDLE = 0,        /*!< 未连接、未发起 */
    APP_MQTT_STATE_TCP_CONNECTING,  /*!< TCP SYN 已发，等 tcp_connected 回调 */
    APP_MQTT_STATE_WAIT_CONNACK,    /*!< CONNECT 已发，等 CONNACK */
    APP_MQTT_STATE_CONNECTED,       /*!< CONNACK rc=0，可收发 */
    APP_MQTT_STATE_WAIT_RECONNECT,  /*!< 断线，退避定时中 */
    APP_MQTT_STATE_CLOSED           /*!< 主动关闭（不再自动重连） */
} app_mqtt_state_t;

/* 状态回调的 reason 码 */
#define APP_MQTT_EV_CONNECTED       0   /*!< CONNACK rc=0 */
#define APP_MQTT_EV_CONNACK_REFUSED 1   /*!< CONNACK rc!=0（见 MQTT 3.1.1 §3.2.2.3） */
#define APP_MQTT_EV_TCP_LOST        2   /*!< TCP 断链/超时/协议错误 */
#define APP_MQTT_EV_TCP_ERROR       3   /*!< tcp_err 回调（RST 等） */
#define APP_MQTT_EV_DISCONNECTED    4   /*!< 主动 DISCONNECT */

/* ---- 配置（字符串会被拷贝进实例，shell 传参后失效也没关系） ---- */
typedef struct app_mqtt_cfg_struct {
    const char *host;            /*!< broker IPv4 点分十进制，缺省 192.168.2.1 */
    uint16_t    port;            /*!< 缺省 1883 */
    const char *client_id;       /*!< 缺省 "hpm5e31" */
    const char *username;        /*!< NULL/空 = 不填 */
    const char *password;        /*!< NULL/空 = 不填 */
    const char *will_topic;      /*!< NULL/空 = 不带遗嘱 */
    const char *will_payload;    /*!< 遗嘱载荷，NULL = 空 */
    uint8_t     will_qos;        /*!< 0 或 1 */
    bool        will_retain;     /*!< 遗嘱是否 retained */
    uint16_t    keepalive_s;     /*!< keepalive 秒（0 = 关闭；协议上限 65535） */
    bool        clean_session;   /*!< 1 = clean session（本组件只支持 1） */
    bool        auto_reconnect;  /*!< 断线自动重连 */
    uint16_t    connect_timeout_ms;  /*!< 等 CONNACK 上限，缺省 5000 */
    uint32_t    reconnect_min_ms;    /*!< 退避下限，缺省 1000 */
    uint32_t    reconnect_max_ms;    /*!< 退避上限，缺省 30000 */
    uint32_t    qos1_retry_ms;       /*!< QoS1 未确认重发间隔，缺省 5000 */
    uint8_t     qos1_max_retry;      /*!< 单条 QoS1 最大重发次数，缺省 3 */
} app_mqtt_cfg_t;

#define APP_MQTT_CFG_DEFAULT { \
    "192.168.2.1", 1883U, "hpm5e31", NULL, NULL, NULL, NULL, 0U, false, \
    1080U, true, true, 5000U, 1000U, 30000U, 5000U, 3U }

/* ---- 统计 ---- */
typedef struct app_mqtt_stats_struct {
    uint32_t tcp_connects;      /*!< TCP 建立成功次数 */
    uint32_t connack_ok;        /*!< CONNACK rc=0 次数 */
    uint32_t connack_refused;   /*!< CONNACK rc!=0 次数 */
    uint32_t disconnects;       /*!< 非正常断链次数（TCP 错误/超时/协议错） */
    uint32_t reconnects;        /*!< 自动重连发起次数 */
    uint32_t rx_packets;        /*!< 收到的 MQTT 报文总数 */
    uint32_t tx_packets;        /*!< 发出的 MQTT 报文总数 */
    uint32_t rx_bytes;          /*!< TCP 收到的字节数 */
    uint32_t tx_bytes;          /*!< TCP 发出的字节数 */
    uint32_t publish_tx;        /*!< 发出的 PUBLISH 条数 */
    uint32_t publish_tx_bytes;  /*!< 发出的 PUBLISH 载荷字节数 */
    uint32_t publish_rx;        /*!< 收到的 PUBLISH 条数 */
    uint32_t publish_rx_bytes;  /*!< 收到的 PUBLISH 载荷字节数 */
    uint32_t puback_tx;         /*!< 回给 broker 的 PUBACK 数 */
    uint32_t puback_rx;         /*!< 收到 broker 的 PUBACK 数 */
    uint32_t retransmits;       /*!< QoS1 重发（DUP=1）次数 */
    uint32_t pingreq_tx;        /*!< PINGREQ 发送次数 */
    uint32_t pingresp_rx;       /*!< PINGRESP 接收次数 */
    uint32_t ping_timeouts;     /*!< PINGRESP 超时次数 */
    uint32_t drops;             /*!< 队列满/发送失败丢弃 */
    uint32_t protocol_errors;   /*!< 协议错误（非法剩余长度/超长报文/QoS2 等） */
    uint16_t last_pid;          /*!< 最近一次分配的 packet id */
    uint16_t inflight;          /*!< 当前 QoS1 未确认条数 */
    uint32_t last_rtt_ms;       /*!< 最近一次 PINGREQ->PINGRESP 往返（ms） */
    uint32_t rtt_avg_ms;        /*!< PING 往返均值（ms） */
} app_mqtt_stats_t;

/* ---- 收到的消息（回调内有效，payload 指向组件内部接收缓冲） ---- */
typedef struct app_mqtt_msg_struct {
    char        topic[APP_MQTT_TOPIC_MAX + 1U]; /*!< 主题（结尾 0） */
    const uint8_t *payload;     /*!< 载荷指针（回调返回后失效） */
    uint16_t    payload_len;
    uint8_t     qos;
    bool        retain;
    bool        dup;
} app_mqtt_msg_t;

struct tcp_pcb;   /* 前向声明，避免公共头引入 lwIP */

/* QoS1 未确认条目（内部） */
typedef struct app_mqtt_inflight_struct {
    bool     used;
    uint16_t pid;
    uint8_t  qos;
    bool     retain;
    uint8_t  retries;
    uint16_t topic_len;
    uint16_t payload_len;
    uint32_t last_ms;
    char     topic[APP_MQTT_TOPIC_MAX];
    uint8_t  payload[APP_MQTT_INFLIGHT_PAYLOAD];
} app_mqtt_inflight_t;

/* 客户端实例。除 cfg/callback 外的字段都是内部状态，用户不要直接改。 */
typedef struct app_mqtt_struct {
    /* --- 配置（字符串副本） --- */
    app_mqtt_cfg_t cfg;
    char     cfg_host[16];
    char     cfg_client_id[APP_MQTT_CLIENT_ID_MAX];
    char     cfg_user[APP_MQTT_USER_MAX];
    char     cfg_pass[APP_MQTT_PASS_MAX];
    char     cfg_will_topic[APP_MQTT_TOPIC_MAX];
    char     cfg_will_payload[APP_MQTT_WILL_PAYLOAD_MAX];

    /* --- 回调 --- */
    void (*on_state)(struct app_mqtt_struct *m, app_mqtt_state_t st, int reason, void *user);
    void (*on_message)(struct app_mqtt_struct *m, const app_mqtt_msg_t *msg, void *user);
    void  *user;

    /* --- 传输层 --- */
    struct tcp_pcb *pcb;
    app_mqtt_state_t state;
    uint8_t  txq[APP_MQTT_TXQ_SIZE];
    uint16_t txq_head;
    uint16_t txq_tail;
    uint8_t  rxbuf[APP_MQTT_RX_BUF];
    uint16_t rx_len;
    bool     in_callback;      /*!< 正在 lwIP 回调里，禁止 tcp_close 重入 */
    bool     closing;          /*!< 正在主动关链（抑制 tcp_abort 触发的 err 回调重复排程） */

    /* --- 定时 --- */
    uint32_t last_tx_ms;
    uint32_t last_rx_ms;
    uint32_t connect_start_ms;
    uint32_t reconnect_at_ms;
    uint32_t backoff_ms;
    uint32_t ping_tx_ms;
    bool     ping_outstanding;

    /* --- 协议 --- */
    uint16_t next_pid;
    app_mqtt_inflight_t inflight[APP_MQTT_INFLIGHT_MAX];
    app_mqtt_stats_t stats;
    uint32_t rtt_sum_ms;

    /* --- 调试：最近一条发出的报文（shell 里 hexdump，书稿逐字节表用） --- */
    uint8_t  last_tx_pkt[160];
    uint16_t last_tx_pkt_len;
} app_mqtt_t;

/* ---------------- API ---------------- */

/**
 * @brief 初始化客户端实例（不发包）。cfg=NULL 用 APP_MQTT_CFG_DEFAULT。
 * @return 0 成功；-1 参数非法（host/client_id 空或超长）
 */
int app_mqtt_init(app_mqtt_t *m, const app_mqtt_cfg_t *cfg);

/** @brief 注册回调（可选，可在 init 后调用） */
void app_mqtt_set_state_cb(app_mqtt_t *m, void (*cb)(app_mqtt_t *, app_mqtt_state_t, int, void *), void *user);
void app_mqtt_set_message_cb(app_mqtt_t *m, void (*cb)(app_mqtt_t *, const app_mqtt_msg_t *, void *), void *user);

/**
 * @brief 发起连接（非阻塞）。已连接/正在连时返回 0 不重复发起。
 * @return 0 已发起；-1 参数错；-2 tcp_new/tcp_connect 失败
 */
int app_mqtt_connect(app_mqtt_t *m);

/**
 * @brief 主循环轮询：推进 TCP 发送、keepalive、QoS1 重传、自动重连、超时
 * @note 必须频繁调用（建议 <= 10 ms 一次）；内部会调用 tcp_output()
 */
void app_mqtt_poll(app_mqtt_t *m);

/**
 * @brief 关闭连接
 * @param graceful true = 先发 DISCONNECT 再关 TCP；false = 直接 tcp_abort（LWT 测试用）
 */
void app_mqtt_disconnect(app_mqtt_t *m, bool graceful);

/**
 * @brief 发布消息
 * @param qos 0 或 1（QoS2 返回 -4）
 * @return >=0 packet id（QoS0 返回 0）；-1 参数错；-2 未连接；-3 缓冲/窗口满；-4 不支持
 * @note QoS1 会拷贝 topic+payload 进未确认表，回调前可安全释放入参
 */
int app_mqtt_publish(app_mqtt_t *m, const char *topic, const void *payload, uint16_t len,
                     uint8_t qos, bool retain);

/** @brief 订阅一个主题过滤器 @return 0 成功；-2 未连接；-3 参数错 */
int app_mqtt_subscribe(app_mqtt_t *m, const char *topic_filter, uint8_t qos);

/** @brief 取消订阅 @return 0 成功；-2 未连接 */
int app_mqtt_unsubscribe(app_mqtt_t *m, const char *topic_filter);

/** @brief 是否已连接（CONNACK rc=0） */
bool app_mqtt_is_connected(const app_mqtt_t *m);

/** @brief 当前状态 / 状态名 */
app_mqtt_state_t app_mqtt_state(const app_mqtt_t *m);
const char *app_mqtt_state_str(app_mqtt_state_t st);

/** @brief 取统计快照 */
void app_mqtt_get_stats(const app_mqtt_t *m, app_mqtt_stats_t *out);
/** @brief 清统计（不影响未确认表） */
void app_mqtt_reset_stats(app_mqtt_t *m);

/** @brief 取最近一条发出报文（调试/书稿逐字节表），返回长度 */
uint16_t app_mqtt_last_packet(const app_mqtt_t *m, const uint8_t **buf);

/**
 * @brief 构造当前配置下的 CONNECT 报文到 buf（不发送），返回长度；0 = 参数/容量不足
 * @note  供 shell hexdump 与书稿"逐字节表"取证用，不改变连接状态
 */
uint16_t app_mqtt_debug_connect(const app_mqtt_t *m, uint8_t *buf, uint16_t cap);

/* ---------------- 纯函数（可单独自测，不依赖网络） ---------------- */

/** @brief 剩余长度编码：返回写入字节数（1~4）；buf 至少 4 B */
int app_mqtt_remlen_encode(uint8_t *buf, uint32_t len);

/** @brief 剩余长度解码：返回消耗字节数（1~4），0 = 数据不够；-1 = 非法（>4 字节或越界） */
int app_mqtt_remlen_decode(const uint8_t *buf, int avail, uint32_t *len);

/** @brief MQTT 主题过滤器匹配（§4.7：+ 单层，# 多层，$ 主题不匹配前导通配符） */
bool app_mqtt_topic_match(const char *filter, const char *topic);

#ifdef __cplusplus
}
#endif

#endif /* APP_MQTT_H */
