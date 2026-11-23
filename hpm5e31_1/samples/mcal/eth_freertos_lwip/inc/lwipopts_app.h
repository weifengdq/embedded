/*
 * lwipopts_app.h - eth_freertos_lwip 的 lwIP 选项（NO_SYS=0，FreeRTOS 版）
 *
 * 由 SDK samples/lwip/ports/freertos/lwipopts.h 在 -DUSE_LWIPOPTS_APP_H=1 时包含。
 * 与裸机版（samples/mcal/eth_lwip/inc/lwipopts_app.h）的区别只在"线程/邮箱"这一类，
 * 协议栈内存预算相同，便于对比裸机 vs RTOS 的吞吐差异。
 */
#ifndef LWIPOPTS_APP_H
#define LWIPOPTS_APP_H

#define NO_SYS                      0
#define SYS_LIGHTWEIGHT_PROT        1
#define LWIP_COMPAT_MUTEX           0
/* FreeRTOS 下从任意任务调 raw API 必须持核心锁（lwIP 2.x 官方模型） */
#define LWIP_TCPIP_CORE_LOCKING     1

#define MEM_SIZE                    (24 * 1024)
#define MEMP_NUM_PBUF               (50)
#define PBUF_POOL_SIZE              (16)
#define MEMP_NUM_TCP_SEG            (40)
#define MEMP_NUM_UDP_PCB            (8)
#define MEMP_NUM_NETCONN            (8)
#define MEMP_NUM_SYS_TIMEOUT        (16)

#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_DHCP                   0
#define LWIP_DNS                    0
#define LWIP_NETCONN                1
#define LWIP_SOCKET                 0
#define LWIP_NETIF_API              0
#define LWIP_NETIF_LINK_CALLBACK    1

#define TCP_TTL                     255
#define TCP_QUEUE_OOSEQ             0
#define TCP_MSS                     (1500 - 40)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            (2 * TCP_SND_BUF / TCP_MSS)
#define TCP_WND                     (8 * TCP_MSS)

/* tcpip 线程：优先级低于以太网收包任务（收包任务要能立刻抢占），高于应用任务 */
#define TCPIP_THREAD_NAME           "tcpip"
#define TCPIP_THREAD_STACKSIZE      (4096)
#define TCPIP_THREAD_PRIO           (5)
#define TCPIP_MBOX_SIZE             (32)
#define DEFAULT_UDP_RECVMBOX_SIZE   (32)
#define DEFAULT_TCP_RECVMBOX_SIZE   (32)
#define DEFAULT_ACCEPTMBOX_SIZE     (8)

/* 同裸机版：不定义 CHECKSUM_BY_HARDWARE，校验和由 lwIP 软件算 */
#define LWIP_DEBUG                  1
#define LWIP_DBG_MIN_LEVEL          0
#define NETIF_DEBUG                 LWIP_DBG_OFF
#define PBUF_DEBUG                  LWIP_DBG_OFF
#define MEM_DEBUG                   LWIP_DBG_OFF
#define TCP_DEBUG                   LWIP_DBG_OFF
#define TCP_INPUT_DEBUG             LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG            LWIP_DBG_OFF
#define TCPIP_DEBUG                 LWIP_DBG_OFF
#define IP_DEBUG                    LWIP_DBG_OFF
#define ICMP_DEBUG                  LWIP_DBG_OFF
#define UDP_DEBUG                   LWIP_DBG_OFF
#define ETHARP_DEBUG                LWIP_DBG_OFF

#endif /* LWIPOPTS_APP_H */
