/*
 * app_someip_net.h —— SOME/IP 的 lwIP UDP 传输层（组件自带的"薄"网络适配）
 *
 * 说明：协议层（app_someip.c / app_someip_sd.c / app_someip_tp.c）不依赖任何网络栈，
 * 本文件是给它配套的 lwIP 裸机传输实现：
 *   - ENET(RGMII) + YT8011AN 初始化、静态 IP 192.168.2.100/24、1 ms 时基
 *   - 两个 UDP socket：SD 30490、服务实例 30501
 *   - 收包 -> app_someip_on_rx()；发包用 udp_sendto()
 *
 * 与 components/app_lwip（tp-infra 底座）的关系：底座交付后，本文件只需把
 *   app_someip_net_init()  换成 app_lwip_init(&cfg) + app_udp_socket()
 *   app_someip_net_poll()  换成 app_lwip_poll()
 * 协议层与例程代码完全不用改。
 */
#ifndef APP_SOMEIP_NET_H
#define APP_SOMEIP_NET_H

#include <stdint.h>
#include <stdbool.h>
#include "app_someip.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 与团队约定一致（docs/应用层开发约定.md §6） */
#define APP_SOMEIP_SD_PORT      30490U
#define APP_SOMEIP_SERVICE_PORT 30501U

/* 板端静态地址 */
#define APP_SOMEIP_LOCAL_IP0 192U
#define APP_SOMEIP_LOCAL_IP1 168U
#define APP_SOMEIP_LOCAL_IP2 2U
#define APP_SOMEIP_LOCAL_IP3 100U

/* SOME/IP-SD 组播组 */
#define APP_SOMEIP_MCAST_IP0 224U
#define APP_SOMEIP_MCAST_IP1 244U
#define APP_SOMEIP_MCAST_IP2 224U
#define APP_SOMEIP_MCAST_IP3 245U

int  app_someip_net_init(void);      /* 0 = 成功 */
void app_someip_net_poll(void);      /* 主循环每轮调用（收包 + lwIP 定时） */
int  app_someip_net_open(app_someip_t *srv, uint16_t sd_port, uint16_t svc_port);
void app_someip_net_info(void);      /* 打印 netif/MAC/IP/PHY 状态 */
bool app_someip_net_link_up(void);
uint32_t app_someip_net_rx_drop(void);

/* 供 someip_cfg_t 使用 */
int      app_someip_net_send(void *ctx, const uint8_t dst_ip[4], uint16_t dst_port,
                             const uint8_t *buf, uint16_t len);
uint32_t app_someip_net_now_ms(void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_SOMEIP_NET_H */
