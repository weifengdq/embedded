/*
 * app_xrce.h —— HPM5E31 上的 Micro-XRCE-DDS 客户端组件（lwIP UDP custom transport）
 *
 * 设计要点（详见 components/app_xrce/README.md）：
 *   1) 无堆：会话、流、实体状态都是静态的；收发缓冲由调用者按 cfg 传入。
 *   2) 裸机单线程：主循环调用 app_xrce_poll()，内部驱动 lwIP（回调 net_poll）并跑 XRCE 会话。
 *   3) 只做"字节级发布/订阅"：CDR 编码由应用负责（见 samples/app/xrce_pubsub），
 *      组件不依赖任何 IDL 生成代码，因此可以对接 ROS 2 任意标准消息类型。
 *   4) 二进制实体创建（CREATE 的 REPRESENTATION_IN_BINARY），与 micro-ROS 走同一条 Agent 路径，
 *      因此 topic/type 名字直接就是 DDS 名字（ROS 2 侧为 "rt/<topic>" + "pkg::msg::dds_::Type_"）。
 */
#ifndef APP_XRCE_H
#define APP_XRCE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 编译期上限：每个实体都要占静态状态，按需改（同时要改 out/in 缓冲配置）。 */
#define APP_XRCE_MAX_PUBS   4
#define APP_XRCE_MAX_SUBS   2

/* 与 uxr/client/config.h 的 UXR_CONFIG_CUSTOM_TRANSPORT_MTU 保持一致。 */
#define APP_XRCE_MTU        512
/* 接收环形槽数：Agent 一个轮次可能连发多报文（状态 + READ_DATA），留 4 槽余量。 */
#define APP_XRCE_RX_SLOTS   4

typedef struct {
    /* ---- 网络（Bench C：主机 192.168.2.1，Agent 默认 2018/udp4） ---- */
    uint32_t agent_ip;          /* 主机字节序；192.168.2.1 -> 0xC0A80201 */
    uint16_t agent_port;        /* 2018 */
    uint16_t local_port;        /* 板端源端口，建议 2019 */
    uint16_t poll_timeout_ms;   /* app_xrce_poll() 的会话等待(ms)：0=非阻塞（实验，省 CPU），1=旧行为；样本默认 1 */

    /* ---- 会话 / DDS ---- */
    uint32_t key;               /* 会话 key，任意 32 bit */
    uint16_t domain_id;         /* DDS domain id，ROS 2 默认 0 */
    const char *participant_name;   /* 可为 NULL */

    /* ---- 静态缓冲（调用者提供，禁止传 NULL） ---- */
    uint8_t *out_reliable_buf;  uint32_t out_reliable_size;  uint16_t out_reliable_history;
    uint8_t *in_reliable_buf;   uint32_t in_reliable_size;   uint16_t in_reliable_history;
    uint8_t *out_be_buf;        uint32_t out_be_size;
} app_xrce_cfg_t;

typedef struct {
    const char *topic_name;     /* DDS topic 名；ROS 2 侧为 "rt/<ros_topic>" */
    const char *type_name;      /* DDS 类型名；ROS 2 侧为 "std_msgs::msg::dds_::String_" 之类 */
    uint8_t  writer_oid;        /* XRCE DataWriter 对象 ID（同类型内唯一，1..0xFF） */
    bool     reliable_qos;      /* DDS QoS：true=RELIABLE（ROS 2 默认 Subscriber 要求） */
    bool     reliable_stream;   /* XRCE 输出流：true=可靠流（有 ACK/重传计数） */
    uint16_t history_depth;     /* KEEP_LAST 深度，建议 8 */
} app_xrce_pub_cfg_t;

typedef struct {
    const char *topic_name;
    const char *type_name;
    uint8_t  reader_oid;
    bool     reliable_qos;
    uint16_t history_depth;

    /* 收到一个样本时回调；cdr 为去掉 4 字节封装头后的 CDR 负载，len 为负载长度。
     * 注意：回调在 app_xrce_poll() 上下文（主循环）里执行，不要做长耗时操作。 */
    void (*on_data)(void *arg, const uint8_t *cdr, uint32_t len);
    void *arg;
} app_xrce_sub_cfg_t;

typedef struct {
    uint32_t tx_samples;      /* app_xrce_publish() 成功入队的样本数 */
    uint32_t tx_datagrams;    /* 传输层实际写出的 UDP 报文数 */
    uint32_t tx_bytes;
    uint32_t tx_drops;        /* 流缓冲满 / 编码失败导致的丢弃 */
    uint32_t rx_datagrams;
    uint32_t rx_bytes;
    uint32_t rx_samples;      /* 订阅回调次数 */
    uint32_t rx_overrun;      /* 接收环槽满导致的丢弃 */
    uint32_t acks;            /* 可靠流 last_acknown 前进量（≈ 被 Agent 确认的 WRITE 数） */
    uint32_t retransmits;     /* 可靠流 last_sent 回退次数（重传/心跳重发） */
    uint32_t reconnects;      /* 会话重建次数 */
    uint32_t net_errors;      /* udp_send 失败 / pbuf 分配失败 */
    bool     connected;
    uint32_t uptime_ms;
} app_xrce_stats_t;

/* 初始化：打开传输、建会话与基础实体（Participant/Publisher/Subscriber）。
 * 返回 0 成功；<0 失败（-1 参数错，-2 传输打不开，-3 会话建不起来）。 */
int  app_xrce_init(const app_xrce_cfg_t *cfg);

/* 注册发布者/订阅者：返回 >=0 为索引（发布用），<0 失败。 */
int  app_xrce_add_pub(const app_xrce_pub_cfg_t *cfg);
int  app_xrce_add_sub(const app_xrce_sub_cfg_t *cfg);

/* 发布一个样本（cdr 不含 4 字节封装头；由 poll 统一 flush）。 */
int  app_xrce_publish(int pub_idx, const void *cdr, uint32_t len);

/* 主循环调用：驱动 lwIP + 跑 XRCE 会话（约 1 ms），并维护断线重连与统计。 */
void app_xrce_poll(void);

/* 注入"网络轮询"函数（裸机上 = enet_common_handler(&gnetif)）。
 * 传输层在等待收包时反复调用它，否则 lwIP 收不到包。 */
void app_xrce_set_net_poll(void (*fn)(void));

void        app_xrce_get_stats(app_xrce_stats_t *out);

/* 单调毫秒时基（mcycle@CPU 主频，不依赖 BSP 的累加式时基）。 */
uint32_t app_xrce_now_ms(void);
bool        app_xrce_connected(void);
const char *app_xrce_state_str(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_XRCE_H */
