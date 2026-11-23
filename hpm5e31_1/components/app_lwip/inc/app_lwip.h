/*
 * app_lwip.h - 应用层 lwIP 组件（ENET0 RGMII + YT8011AN + baremetal lwIP 2.1.2）
 *
 * 把 SDK samples/lwip 的 5 步初始化（enet_init / lwip_init / netif_config /
 * enet_services / board_timer_create）收成一个 app_lwip_init()，主循环只调
 * app_lwip_poll()。默认静态 IP 192.168.2.100/24，网关 192.168.2.1（与主机 enp6s0 同网段）。
 *
 * 最小示例（3 行起网络）：
 *     #include "app_lwip.h"
 *     if (app_lwip_init(NULL) != 0) { printf("lwIP init FAIL\r\n"); while (1) {} }
 *     while (1) { app_lwip_poll(); }
 *
 * 典型用法（SOME/IP 等需要多 socket）：
 *     ip_addr_t any; IP_ADDR_ANY_VALUE(any);          // 或 NULL = 任意地址
 *     struct udp_pcb *sd = app_udp_socket(NULL, 30490);
 *     struct udp_pcb *sv = app_udp_socket(NULL, 30501);
 *     udp_recv(sd, my_sd_cb, NULL);                   // 回调由使用者注册
 *
 * 约束：
 *  - 单线程裸机（NO_SYS=1），所有 lwIP API 只能在主循环里调用。
 *  - 不要在回调里 printf/blocking；回调应 <100 us。
 *  - 本组件已经提供 YT8011AN 的板级钩子（board_init_enet_phy /
 *    board_get_enet_phy_status / lwip_enet_phy_adaptive_poll），
 *    不要再编译 enet_lwip_iperf 的 phy_glue.c，也不要打开 CONFIG_ENET_PHY。
 *
 * 头文件：components/app_lwip/inc/app_lwip.h
 * 作者：tp-infra（第二阶段共享底座）
 */
#ifndef APP_LWIP_H
#define APP_LWIP_H

#include <stdint.h>
#include <stdbool.h>

#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/udp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 默认网络参数（与 docs/应用层开发约定.md §2/§6 一致） */
#define APP_LWIP_DEFAULT_IP      "192.168.2.100"
#define APP_LWIP_DEFAULT_NETMASK "255.255.255.0"
#define APP_LWIP_DEFAULT_GW      "192.168.2.1"
#define APP_LWIP_DEFAULT_LINK_TIMEOUT_MS (3000U)

/* 初始化参数；传 NULL 用默认值。字符串为 NULL 表示用默认。 */
typedef struct app_lwip_cfg_struct {
    const char *ip;             /*!< 静态 IP，缺省 192.168.2.100 */
    const char *netmask;        /*!< 子网掩码，缺省 255.255.255.0 */
    const char *gw;             /*!< 网关，缺省 192.168.2.1 */
    uint32_t link_timeout_ms;   /*!< 等 PHY link up 的上限，0 表示不等待立即返回 */
    bool verbose;               /*!< 1 = 打印 netif/PHY 详情（默认 1） */
} app_lwip_cfg_t;

/**
 * @brief 初始化 ENET0(RGMII)+YT8011AN+lwIP，配置静态 IP 并等待 link up
 * @param cfg 参数，NULL = 默认（192.168.2.100/24, GW .1, 等 3 s）
 * @return 0 = 成功且 link up；1 = 网络已起但 link down（可继续轮询）；<0 = 失败
 *         （-1 参数/PHY ID 错，-2 ENET 控制器初始化失败，-3 netif 未建立）
 */
int app_lwip_init(const app_lwip_cfg_t *cfg);

/** @brief 取 lwIP netif（就是 SDK 的 gnetif） */
struct netif *app_lwip_netif(void);

/** @brief 毫秒时基（等于 lwIP 的 sys_now()，由 1 ms 硬件定时器驱动） */
uint32_t app_lwip_now_ms(void);

/**
 * @brief 主循环轮询：收包 + lwIP 定时器 + PHY link 自适应
 * @note 内部已包含 sys_check_timeouts()，主循环不要再单独调用。
 */
void app_lwip_poll(void);

/**
 * @brief 新建并 bind 一个 UDP socket
 * @param local 本地地址；NULL 表示任意地址（IP_ADDR_ANY）
 * @param port  本地端口
 * @return 已 udp_bind 的 pcb；失败返回 NULL。接收回调请自行 udp_recv(pcb, cb, arg) 注册，
 *         用完 udp_remove(pcb)。可多次调用建多个 socket。
 */
struct udp_pcb *app_udp_socket(const ip_addr_t *local, uint16_t port);

/* ---------------- 便利 API ---------------- */

/** @brief link 是否 up */
bool app_lwip_link_up(void);

/** @brief 打印 netif + PHY 状态（PHYSR/GBSR/BMSR/速率/主从） */
void app_lwip_print(void);

/** @brief 触发一次 PHY link 自适应（正常由 2 s 定时器自动做，调试可用） */
void app_lwip_link_poll(void);

/** @brief 字符串转 IPv4（方便 shell 命令），成功返回 true */
bool app_lwip_parse_ip(const char *s, ip_addr_t *out);

#ifdef __cplusplus
}
#endif

#endif /* APP_LWIP_H */
