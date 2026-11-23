/*
 * app_lwip_eth.h - 在 AUTOSAR MCAL Eth 驱动之上跑 lwIP 2.1.2 的适配层
 *
 * 与 components/app_lwip（SDK enet 驱动 + baremetal lwIP）的区别：
 *   - 硬件访问全部走 Eth MCAL API（Eth_ProvideTxBuffer / Eth_Transmit / Eth_Receive /
 *     Eth_ReleaseRxBuffer / Eth_GetPhysAddr），不再调用 SDK 的 enet_* 驱动；
 *   - 接收走"零拷贝 custom pbuf"：pbuf->payload 直接指向驱动 Rx 环形缓冲，lwIP 释放 pbuf 时
 *     回调 Eth_ReleaseRxBuffer() 把缓冲还给 DMA；
 *   - 发送走"驱动缓冲直填"：Eth_ProvideTxBuffer() 拿到驱动内 Tx 缓冲，pbuf 链一次 memcpy 进去，
 *     再 Eth_Transmit() 提交（等价于 SDK 驱动里 enet_lwip_output 的拷贝次数）。
 *
 * 硬件后端被隔离在 app_lwip_eth_hw.h 的 ops 接口后面：
 *   - src/app_lwip_eth_hw_mcal.c  -> 正式实现（mcal/Eth）
 *   - 实验对照用 SDK enet 驱动后端只存在于 temp2 的原型工程，不进仓库
 * 这样同一份 netif 代码可以分别跑在两种驱动上，用于第 50 章"MCAL vs SDK 驱动开销"对照。
 *
 * 用法（裸机 NO_SYS=1）：
 *     Eth_Init(&Eth_Config);                       // 例程自己做 MCAL 初始化
 *     Eth_SetControllerMode(0, ETH_MODE_ACTIVE);
 *     app_lwip_eth_init(NULL);                     // 建 netif、配静态 IP
 *     while (1) { app_lwip_eth_poll(); bsp_shell_poll(); }
 *
 * 用法（FreeRTOS NO_SYS=0，在任务里）：
 *     tcpip_init(NULL, NULL);
 *     app_lwip_eth_init(NULL);
 *     xTaskCreate(app_lwip_eth_rx_task, "ethrx", ...);   // 组件自带，见下
 *
 * 作者：eth-stack（task-2）
 */
#ifndef APP_LWIP_ETH_H
#define APP_LWIP_ETH_H

#include <stdint.h>
#include <stdbool.h>

#include "lwip/netif.h"
#include "lwip/ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 与 docs/ROUND7-任务约定.md §4 一致：板 192.168.2.100/24，主机 enp6s0=192.168.2.1/24 */
#define APP_LWIP_ETH_DEFAULT_IP          "192.168.2.100"
#define APP_LWIP_ETH_DEFAULT_NETMASK     "255.255.255.0"
#define APP_LWIP_ETH_DEFAULT_GW          "192.168.2.1"
#define APP_LWIP_ETH_DEFAULT_LINK_TIMEOUT_MS  (3000U)

/** 初始化参数；NULL = 全默认 */
typedef struct app_lwip_eth_cfg_struct {
    const char *ip;             /*!< 静态 IP，缺省 192.168.2.100 */
    const char *netmask;        /*!< 子网掩码，缺省 255.255.255.0 */
    const char *gw;             /*!< 网关，缺省 192.168.2.1 */
    uint8_t ctrl_idx;           /*!< Eth 控制器索引（Eth_GetPhysAddr 的 CtrlIdx），缺省 0 */
    uint32_t link_timeout_ms;   /*!< 等 link up 的上限，0 = 不等直接返回 */
    bool verbose;               /*!< 1 = 打印 netif/驱动详情 */
    bool assume_link_up;        /*!< 1 = 不查 PHY，直接认为链路 up（EthTrcv 未就绪时的 bring-up 开关） */
} app_lwip_eth_cfg_t;

/**
 * @brief 建立 lwIP netif 并绑定到已激活的 Eth 控制器
 * @note  调用前必须已完成 Eth_Init()/Eth_SetControllerMode(ETH_MODE_ACTIVE)；
 *        NO_SYS=0 时必须在 tcpip_init() 之后、从任务上下文调用。
 * @param cfg 参数，NULL = 默认
 * @return 0 = 成功且 link up；1 = netif 已起但 link down；<0 = 失败
 *         （-1 参数错，-2 Eth_GetPhysAddr 失败/控制器未激活，-3 netif_add 失败）
 */
int app_lwip_eth_init(const app_lwip_eth_cfg_t *cfg);

/** @brief 取 lwIP netif */
struct netif *app_lwip_eth_netif(void);

/**
 * @brief 裸机主循环轮询：收包 + Eth_MainFunction + 1 s 链路检测
 * @note  NO_SYS=0 时不要调用（收包由 app_lwip_eth_rx_task 负责）
 */
void app_lwip_eth_poll(void);

/**
 * @brief 把驱动 Rx 环里的帧全部搬进 lwIP（返回处理帧数）
 * @note  裸机由 app_lwip_eth_poll() 调用；FreeRTOS 由收包任务循环调用。
 */
int app_lwip_eth_rx_drain(void);

/**
 * @brief 链路状态轮询（1 s 一次即可）：EthTrcv 链路变化 -> netif_set_link_up/down
 * @return 1 = link up，0 = link down
 */
int app_lwip_eth_link_poll(void);

/** @brief 当前 link 是否 up */
bool app_lwip_eth_link_up(void);

/** @brief 打印 netif + 驱动 + 链路信息（IP/MAC/速率/主从/计数） */
void app_lwip_eth_print(void);

/** @brief 打印本层统计（帧数/字节/丢弃/pbuf 池水位） */
void app_lwip_eth_print_stats(void);

/** @brief 清零本层统计 */
void app_lwip_eth_stats_reset(void);

/* ---------------- iperf2（lwiperf，端口 5001） ---------------- */

/** @brief 启动 iperf2 服务端；udp=1 走 UDP。返回 0 成功 */
int app_lwip_eth_iperf_server(bool udp);

/** @brief 启动 iperf2 客户端连到 remote_ip；udp=1 走 UDP。返回 0 成功 */
int app_lwip_eth_iperf_client(const char *remote_ip, bool udp);

/** @brief 中止当前 iperf 会话 */
void app_lwip_eth_iperf_stop(void);

/** @brief 当前是否有 iperf 会话在跑 */
bool app_lwip_eth_iperf_running(void);

/* ---------------- ping（ICMP echo，含抖动统计） ---------------- */

typedef struct app_lwip_eth_ping_result_struct {
    uint32_t sent;              /*!< 发出个数 */
    uint32_t recv;              /*!< 收到个数 */
    uint32_t lost;              /*!< 丢包个数 */
    uint32_t rtt_min_us;        /*!< 最小 RTT */
    uint32_t rtt_avg_us;        /*!< 平均 RTT */
    uint32_t rtt_max_us;        /*!< 最大 RTT */
    uint32_t rtt_jitter_us;     /*!< 抖动（相邻 RTT 差的平均绝对值，RFC3550 简化版） */
} app_lwip_eth_ping_result_t;

/**
 * @brief 阻塞式 ping（内部自己 poll/延时，调用期间不要做别的事）
 * @param dst_ip   目标 IP 字符串
 * @param count    个数
 * @param interval_ms 两个 echo 之间间隔
 * @param out      统计结果（可为 NULL）
 * @return 0 = 全部收到；1 = 有丢包；<0 = 参数/资源错
 */
int app_lwip_eth_ping(const char *dst_ip, uint32_t count, uint32_t interval_ms,
                      app_lwip_eth_ping_result_t *out);

/** @brief 字符串转 IPv4（shell 命令用） */
bool app_lwip_eth_parse_ip(const char *s, ip_addr_t *out);

/*! 启动裸机 1 ms lwIP 时基（NO_SYS=1 时必须在 app_lwip_eth_init 之前调用） */
void app_lwip_eth_timebase_start(void);

/* ---------------- FreeRTOS 支持（NO_SYS=0 时才有实现） ---------------- */

#if !NO_SYS
/** @brief 收包任务体：xTaskCreate(app_lwip_eth_rx_task, "ethrx", ...) */
void app_lwip_eth_rx_task(void *arg);

/** @brief 网络统计/链路任务体：每秒刷一次 CPU 占用、链路、统计（可选） */
void app_lwip_eth_mon_task(void *arg);
#endif

#ifdef __cplusplus
}
#endif

#endif /* APP_LWIP_ETH_H */
