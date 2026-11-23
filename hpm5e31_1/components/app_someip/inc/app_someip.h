/*
 * app_someip.h —— 最小可用的 SOME/IP 服务端（AUTOSAR PRS SOME/IP over UDP）
 *
 * 设计目标：480 MHz 裸机、单线程、无 malloc。协议层与网络栈解耦——
 * 上层只提供两个回调（send / now_ms），因此同一份代码可以跑在：
 *   - lwIP raw UDP（本工程示例，见 components/app_someip/src/app_someip_net.c）
 *   - 将来接 components/app_lwip 底座（app_lwip_init/poll + app_udp_socket）
 *
 * 覆盖能力：
 *   报文头 16 B 全字段编解码（含 Length 语义校验）
 *   REQUEST / REQUEST_NO_RETURN / RESPONSE / ERROR / NOTIFICATION
 *   方法 RPC、事件（周期 NOTIFICATION）、字段（getter/setter 用方法号派生）
 *   SOME/IP-SD（见 app_someip_sd.h）：OfferService / StopOffer / FindService / Subscribe+Ack
 *   SOME/IP-TP（见 app_someip_tp.h）：>1400 B 分段
 *
 * 服务约定（与 host/someip/ 上位机、第 35 章一致）：
 *   service 0x1234, instance 0x0001, major 0x01, interface 0x01
 *   method 0x0001 add(u32,u32) -> u32
 *   method 0x0002 get_version() -> string(u32 len + chars)
 *   method 0x0003 set_led(bool) -> bool
 *   field  0x0004 temperature getter() -> f32   / 0x0005 setter(f32) -> f32
 *   event  0x8001 counter  (100 ms, u32)
 *   event  0x8002 adc      (10 ms,  u32, 12 bit ADC 原始值)
 *   event  0x8004 temperature notifier (500 ms, f32)
 *   eventgroup 0x0001 承载上面三个事件
 */
#ifndef APP_SOMEIP_H
#define APP_SOMEIP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 线上格式常量 ---------------- */
#define SOMEIP_HEADER_LEN            16U
#define SOMEIP_LENGTH_OFFSET         4U     /* Length 字段偏移：Message ID(4) 之后 */
#define SOMEIP_LENGTH_MIN            8U     /* Length = RequestID(4)+PV(1)+IV(1)+MT(1)+RC(1) + payload */
#define SOMEIP_PROTOCOL_VERSION      0x01U

#define SOMEIP_TP_FLAG               0x20U  /* Message Type 的 TP 位 */
#define SOMEIP_TP_HEADER_LEN         4U
#define SOMEIP_TP_SEG_SIZE           1392U  /* 87 x 16，保证非末段 16 B 对齐 */
#define SOMEIP_MAX_PAYLOAD           4096U  /* 本实现支持的逻辑报文上限（4 KB） */

#define SOMEIP_MAX_METHODS           8
#define SOMEIP_MAX_EVENTS            8
#define SOMEIP_MAX_SUBSCRIBERS       8
#define SOMEIP_MAX_EVENTGROUPS       4

/* ---------------- Message Type（AUTOSAR PRS SOME/IP Table） ---------------- */
#define SOMEIP_MT_REQUEST                  0x00U
#define SOMEIP_MT_REQUEST_NO_RETURN        0x01U
#define SOMEIP_MT_NOTIFICATION             0x02U
#define SOMEIP_MT_REQUEST_TP               0x20U
#define SOMEIP_MT_REQUEST_NO_RETURN_TP     0x21U
#define SOMEIP_MT_NOTIFICATION_TP          0x22U
#define SOMEIP_MT_RESPONSE                 0x80U
#define SOMEIP_MT_ERROR                    0x81U
#define SOMEIP_MT_RESPONSE_TP              0xA0U
#define SOMEIP_MT_ERROR_TP                 0xA1U

/* ---------------- Return Code ---------------- */
#define SOMEIP_RC_OK                       0x00U
#define SOMEIP_RC_NOT_OK                   0x01U
#define SOMEIP_RC_UNKNOWN_SERVICE          0x02U
#define SOMEIP_RC_UNKNOWN_METHOD           0x03U
#define SOMEIP_RC_NOT_READY                0x04U
#define SOMEIP_RC_NOT_REACHABLE            0x05U
#define SOMEIP_RC_TIMEOUT                  0x06U
#define SOMEIP_RC_WRONG_PROTOCOL_VERSION   0x07U
#define SOMEIP_RC_WRONG_INTERFACE_VERSION  0x08U
#define SOMEIP_RC_MALFORMED_MESSAGE        0x09U
#define SOMEIP_RC_WRONG_MESSAGE_TYPE       0x0AU

/* ---------------- SOME/IP 报文（解码结果，payload 指向原始缓冲） ---------------- */
typedef struct {
    uint16_t service_id;
    uint16_t method_id;
    uint32_t length;            /* Length 字段原值 = 8 + payload_len */
    uint16_t client_id;
    uint16_t session_id;
    uint8_t  protocol_version;
    uint8_t  interface_version;
    uint8_t  message_type;      /* 原样（若为 TP 段则最高位 0x20 保留） */
    uint8_t  return_code;
    const uint8_t *payload;
    uint16_t payload_len;
} someip_msg_t;

/* 解码：成功返回 0；<0 = 报文非法（原因见 app_someip_decode_str） */
int  app_someip_decode(const uint8_t *buf, uint16_t len, someip_msg_t *m);
const char *app_someip_decode_str(int err);

/* 只写 16 B 头，返回头长度（16）；payload_len 为头后载荷长度 */
uint16_t app_someip_encode_header(uint8_t *buf, uint16_t cap,
                                  uint16_t service_id, uint16_t method_id,
                                  uint16_t client_id, uint16_t session_id,
                                  uint8_t interface_version, uint8_t message_type,
                                  uint8_t return_code, uint16_t payload_len);

/* ---------------- 统计 ---------------- */
typedef struct {
    uint32_t rx_total;
    uint32_t rx_requests;
    uint32_t rx_requests_no_return;
    uint32_t tx_responses;
    uint32_t rx_sd;
    uint32_t tx_sd_offer;
    uint32_t tx_sd_ack;
    uint32_t tx_notifications;
    uint32_t err_unknown_service;
    uint32_t err_unknown_method;
    uint32_t err_malformed;
    uint32_t err_wrong_type;
    uint32_t tp_seg_rx;
    uint32_t tp_msg_rx;
    uint32_t tp_seg_tx;
    uint32_t tp_drop;
    uint32_t sub_added;
    uint32_t sub_removed;
    uint32_t sub_expired;
    uint16_t last_client_id;
    uint16_t last_session_id;
    uint32_t max_rx_len;
} someip_stats_t;

/* ---------------- 回调签名 ---------------- */
struct app_someip;
struct someip_tp_reasm;   /* 定义见 app_someip_tp.h（避免头文件循环包含） */

/* 发送：dst_ip 为 4 字节网络序（点分地址的字节顺序），返回 0 成功 */
typedef int (*someip_send_fn)(void *ctx, const uint8_t dst_ip[4], uint16_t dst_port,
                              const uint8_t *buf, uint16_t len);
typedef uint32_t (*someip_now_ms_fn)(void *ctx);

/* 方法处理：返回 Return Code（SOMEIP_RC_*），载荷写入 reply，长度写入 reply_len */
typedef uint8_t (*someip_method_fn)(struct app_someip *s, const someip_msg_t *req,
                                    uint8_t *reply, uint16_t cap, uint16_t *reply_len,
                                    void *user);

/* 事件填充：把事件载荷写进 buf，返回载荷长度 */
typedef uint16_t (*someip_event_fill_fn)(struct app_someip *s, uint8_t *buf, uint16_t cap,
                                         void *user);

/* ---------------- 订阅表项 ---------------- */
typedef struct {
    uint8_t  ip[4];        /* 订阅者的**事件接收**端点（来自 SD IPv4 endpoint option） */
    uint16_t port;
    uint16_t eventgroup;
    uint32_t ttl_s;
    uint32_t last_renew_ms;
    bool     active;
} someip_subscriber_t;

typedef struct {
    uint16_t method_id;
    someip_method_fn fn;
    void *user;
} someip_method_entry_t;

typedef struct {
    uint16_t event_id;
    uint16_t eventgroup;
    uint32_t period_ms;
    uint32_t next_ms;
    someip_event_fill_fn fill;
    void *user;
} someip_event_entry_t;

/* ---------------- 配置 ---------------- */
typedef struct {
    uint16_t service_id;
    uint16_t instance_id;
    uint8_t  major_version;
    uint8_t  interface_version;
    uint32_t minor_version;
    uint8_t  local_ip[4];      /* 板端 IP，用于 SD endpoint option，如 192.168.2.100 */
    uint16_t sd_port;          /* 30490 */
    uint16_t service_port;     /* 30501 */
    uint32_t offer_ttl_s;      /* OfferService TTL，秒（3~5 推荐） */
    uint32_t tp_seg_gap_us;    /* SOME/IP-TP 相邻段之间的保护间隔（us）。0 = 默认 100 us。
                                * 实测：连续 3 个 1412 B 的 TP 段背靠背发出时，只有最后一段能上线；
                                * 每段之间留 ~100 us 让 ENET 把上一帧发完（1448 B @1Gbit/s ≈ 11.6 us）就正常。 */
    uint8_t  sd_multicast_ip[4]; /* 224.244.224.245 */
    uint16_t sd_multicast_port;  /* 30490 */
    someip_send_fn send;
    someip_now_ms_fn now_ms;
    void *net_ctx;
} someip_cfg_t;

/* ---------------- 服务对象 ---------------- */
typedef struct app_someip {
    someip_cfg_t cfg;

    someip_method_entry_t methods[SOMEIP_MAX_METHODS];
    int n_methods;
    someip_event_entry_t events[SOMEIP_MAX_EVENTS];
    int n_events;

    someip_subscriber_t subs[SOMEIP_MAX_SUBSCRIBERS];

    uint16_t session;          /* 服务端自己发起的报文（事件/响应）session 计数 */
    uint16_t sd_session;       /* SD 报文 session 计数 */
    uint32_t last_offer_ms;
    uint8_t  offer_state;      /* 0=未 offer, 1=已 offer, 2=已 stop */

    someip_stats_t stats;
    struct someip_tp_reasm *tp;  /* 由 app_someip_init 传入的静态重组缓冲（可为 NULL） */

    /* 延迟发送槽：收包回调里只登记，真正发送放到主循环 app_someip_poll()。
     * 原因（第 35 章 U-35-10 实测）：在 RX 回调上下文里连续发 3 个大帧（>1400 B）时，
     * 只有部分帧能上线；同样的 3 帧从主循环发就基本都上线。大响应因此改为"回调登记、主循环发"。 */
    struct {
        bool     pending;
        uint8_t  dst_ip[4];
        uint16_t dst_port;
        uint16_t method_id;
        uint16_t client_id;
        uint16_t session_id;
        uint8_t  msg_type;
        uint8_t  return_code;
        uint16_t len;          /* payload 长度，数据在 tx_buf + SOMEIP_HEADER_LEN */
    } tx_slot;

    uint8_t  tx_buf[SOMEIP_MAX_PAYLOAD + SOMEIP_HEADER_LEN + 16];
    uint8_t  seg_buf[SOMEIP_HEADER_LEN + SOMEIP_TP_HEADER_LEN + SOMEIP_TP_SEG_SIZE + 16];
    uint8_t  sd_buf[512];
    void    *user;
} app_someip_t;

/* ---------------- API ---------------- */
struct someip_tp_reasm;
void app_someip_init(app_someip_t *s, const someip_cfg_t *cfg, struct someip_tp_reasm *tp_storage);
void app_someip_register_method(app_someip_t *s, uint16_t method_id, someip_method_fn fn, void *user);
void app_someip_register_event(app_someip_t *s, uint16_t event_id, uint16_t eventgroup,
                               uint32_t period_ms, someip_event_fill_fn fill, void *user);

/* 收包入口：由网络层回调调用（src_ip 为 4 字节，src_port 为对端 UDP 源端口） */
void app_someip_on_rx(app_someip_t *s, const uint8_t src_ip[4], uint16_t src_port,
                      const uint8_t *buf, uint16_t len);

/* 周期任务：SD 周期 offer / 订阅超时 / 事件发送 */
void app_someip_poll(app_someip_t *s, uint32_t now_ms);

/* SD 主动动作 */
int  app_someip_offer(app_someip_t *s);
int  app_someip_stop_offer(app_someip_t *s);

/* 发送一个逻辑报文（>1392+ 自动走 TP 分段） */
int  app_someip_send_msg(app_someip_t *s, const uint8_t dst_ip[4], uint16_t dst_port,
                         uint16_t method_id, uint16_t client_id, uint16_t session_id,
                         uint8_t msg_type, uint8_t return_code,
                         const uint8_t *payload, uint16_t len);

/* 内部使用：把订阅者加成/移除（SD 模块调用） */
int  app_someip_sub_add(app_someip_t *s, const uint8_t ip[4], uint16_t port,
                        uint16_t eventgroup, uint32_t ttl_s, uint32_t now_ms);
int  app_someip_sub_remove(app_someip_t *s, const uint8_t ip[4], uint16_t port, uint16_t eventgroup);
int  app_someip_sub_count(app_someip_t *s, uint16_t eventgroup);

/* 版本字符串（get_version 方法返回） */
const char *app_someip_ver_string(void);

/* 协议层自证测试（app_someip_selftest.c）：返回失败用例数，0 = 全 PASS */
int app_someip_selftest(app_someip_t *s, bool verbose);

#ifdef __cplusplus
}
#endif

#endif /* APP_SOMEIP_H */