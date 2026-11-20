/**
 * @file  s2500_netif.h
 * @brief S2500 + lwIP (NO_SYS) 以太网接口
 */
#ifndef S2500_NETIF_H
#define S2500_NETIF_H

#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "s2500.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 创建并注册 netif (lwip_init 之后调用) */
struct netif *s2500_netif_add(s2500_dev_t *dev, const ip4_addr_t *ip,
                              const ip4_addr_t *mask, const ip4_addr_t *gw);

/** 轮询接收 (主循环中周期调用) */
void s2500_netif_poll(struct netif *netif);

/** 返回 S2500 设备 */
s2500_dev_t *s2500_netif_dev(struct netif *netif);

#ifdef __cplusplus
}
#endif
#endif
