/*
 * app_someip_sd.h —— SOME/IP-SD（Service Discovery）服务端
 *
 * 线上：UDP 30490，组播 224.244.224.245:30490。
 * 报文 = 16 B SOME/IP 头（Service=0xFFFF / Method=0x8100 / MsgType=0x02 NOTIFICATION）
 *        + SD 载荷：
 *          Flags(1) | Reserved(3) | Entries Length(4) | Entries(N*16)
 *          | Options Length(4) | Options
 *
 * 支持的 Entry：
 *   0x00 FindService            —— 收到后回 OfferService（单播给请求方 SD 端点）
 *   0x01 OfferService           —— 板端周期发送 / TTL=0 即 StopOfferService
 *   0x06 SubscribeEventgroup    —— 收到后加订阅 + 回 0x07 Ack
 *   0x07 SubscribeEventgroupAck —— 板端回给订阅者
 *
 * 支持的 Option：0x04 IPv4 Endpoint（UDP，含 IP/协议/端口）
 *
 * 订阅语义（与 vsomeip / 规范一致）：
 *   - 订阅请求送到 OfferService 里通告的 **SD 端口（30490）**；
 *   - Ack 送回订阅者的 **SD 端口（即报文源端口，通常也是 30490）**；
 *   - 事件（NOTIFICATION）送到 Subscribe 条目 option run 里给出的**事件端点**（IP:Port）；
 *   - 订阅有 TTL，过期自动摘除；订阅者每 TTL/2 重发一次 Subscribe 续约。
 */
#ifndef APP_SOMEIP_SD_H
#define APP_SOMEIP_SD_H

#include <stdint.h>
#include <stdbool.h>
#include "app_someip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOMEIP_SD_SERVICE_ID          0xFFFFU
#define SOMEIP_SD_METHOD_ID           0x8100U
#define SOMEIP_SD_PROTOCOL_VERSION    0x01U
#define SOMEIP_SD_INTERFACE_VERSION   0x01U
#define SOMEIP_SD_ENTRY_LEN           16U
#define SOMEIP_SD_MAX_ENTRIES         8
#define SOMEIP_SD_MAX_OPTIONS         8

/* Entry Type */
#define SOMEIP_SD_ENTRY_FIND_SERVICE          0x00U
#define SOMEIP_SD_ENTRY_OFFER_SERVICE         0x01U
#define SOMEIP_SD_ENTRY_SUBSCRIBE_EVENTGROUP  0x06U
#define SOMEIP_SD_ENTRY_SUBSCRIBE_ACK         0x07U
#define SOMEIP_SD_ENTRY_SUBSCRIBE_NACK        0x0EU

/* Option Type */
#define SOMEIP_SD_OPT_IPV4_ENDPOINT     0x04U
#define SOMEIP_SD_OPT_IPV4_MULTICAST    0x14U
#define SOMEIP_SD_OPT_IPV4_SD_ENDPOINT  0x24U

/* L4 Protocol */
#define SOMEIP_SD_PROTO_UDP 0x11U
#define SOMEIP_SD_PROTO_TCP 0x06U

/* Flags 位定义 */
#define SOMEIP_SD_FLAG_REBOOT   0x80U
#define SOMEIP_SD_FLAG_UNICAST  0x40U

typedef struct {
    uint8_t  type;
    uint8_t  index_first_option;   /* 第一个 option run 在 options[] 中的起始下标 */
    uint8_t  index_second_option;  /* 第二个 option run 起始下标 */
    uint8_t  num_options;          /* 前面两段 run 各自的 option 个数（低 4 位） */
    uint16_t service_id;
    uint16_t instance_id;
    uint8_t  major_version;
    uint32_t ttl;
    /* service entry 用 minor_version；eventgroup entry 用 counter+eventgroup */
    uint32_t minor_version;
    uint16_t eventgroup;
    uint8_t  counter;
} someip_sd_entry_t;

typedef struct {
    uint8_t  type;
    uint8_t  ip[4];
    uint8_t  proto;
    uint16_t port;
} someip_sd_option_t;

typedef struct {
    uint8_t  flags;
    uint8_t  n_entries;
    someip_sd_entry_t entries[SOMEIP_SD_MAX_ENTRIES];
    uint8_t  n_options;
    someip_sd_option_t options[SOMEIP_SD_MAX_OPTIONS];
} someip_sd_msg_t;

/* 解析 SD 载荷（16 B SOME/IP 头之后的部分）；返回 0 成功，<0 错误 */
int app_someip_sd_parse(const uint8_t *payload, uint16_t len, someip_sd_msg_t *m);

/* 收包处理：由 app_someip_on_rx 调用 */
void app_someip_sd_on_rx(app_someip_t *s, const uint8_t src_ip[4], uint16_t src_port,
                         const uint8_t *payload, uint16_t len);

/* 主动发送 OfferService（ttl_s=0 等价 StopOfferService） */
int app_someip_sd_send_offer(app_someip_t *s, const uint8_t dst_ip[4], uint16_t dst_port,
                             uint32_t ttl_s);

/* 回 SubscribeEventgroupAck */
int app_someip_sd_send_ack(app_someip_t *s, const uint8_t dst_ip[4], uint16_t dst_port,
                           const someip_sd_entry_t *req, uint32_t ttl_s, uint8_t return_code);

/* 解析出来的条目里挑第一个指定类型的条目，找不到返回 NULL */
const someip_sd_entry_t *app_someip_sd_find_entry(const someip_sd_msg_t *m, uint8_t type);

#ifdef __cplusplus
}
#endif

#endif /* APP_SOMEIP_SD_H */
