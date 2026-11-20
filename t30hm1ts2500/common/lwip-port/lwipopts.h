/**
 * @file  lwipopts.h
 * @brief lwIP 2.2.0 配置 - NO_SYS 裸机 + S2500 10BASE-T1S
 *
 * 10BASE-T1S 线速仅 10 Mbit/s, 半双工, PLCA 下无冲突, 因此不需要很大窗口。
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* ---- 基础 ---- */
#define NO_SYS                      1
#define SYS_LIGHTWEIGHT_PROT        0
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0
#define LWIP_TIMERS                 1
#define LWIP_SINGLE_NETIF           1
#define LWIP_NETIF_API              0
#define LWIP_COMPAT_MUTEX           0

/* ---- 协议 ---- */
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_DHCP                   0
#define LWIP_AUTOIP                 0
#define LWIP_DNS                    0
#define LWIP_IGMP                   1
#define LWIP_IPV4_FRAG              0

/* ---- 内存 ---- */
#define MEM_ALIGNMENT               4
#ifndef MEM_SIZE
#define MEM_SIZE                    (24 * 1024)
#endif
#ifndef MEMP_NUM_PBUF
#define MEMP_NUM_PBUF               16
#endif
#define MEMP_NUM_RAW_PCB            2
#define MEMP_NUM_UDP_PCB            4
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_PCB_LISTEN     4
#ifndef MEMP_NUM_TCP_SEG
#define MEMP_NUM_TCP_SEG            48
#endif
#define MEMP_NUM_ARP_QUEUE          8
#define MEMP_NUM_SYS_TIMEOUT        12
#ifndef PBUF_POOL_SIZE
#define PBUF_POOL_SIZE              24
#endif
#define PBUF_POOL_BUFSIZE           1536

/* ---- TCP ---- */
#define TCP_MSS                     1460
#ifndef TCP_WND
#define TCP_WND                     (2 * TCP_MSS)
#endif
#ifndef TCP_SND_BUF
#define TCP_SND_BUF                 (4 * TCP_MSS)
#endif
#ifndef TCP_SND_QUEUELEN
#define TCP_SND_QUEUELEN            (8 * (TCP_SND_BUF) / (TCP_MSS))
#endif
#define TCP_QUEUE_OOSEQ             0
#define TCP_LISTEN_BACKLOG          0
#define LWIP_WND_SCALE              0
#define LWIP_TCP_SACK_OUT           0
#define TCP_OVERSIZE                TCP_MSS
#define LWIP_NETIF_TX_SINGLE_PBUF   1

/* ---- 统计/调试 ---- */
#define LWIP_STATS                  1
#define LWIP_STATS_DISPLAY          0
#define LINK_STATS                  1
#define IP_STATS                    1
#define ICMP_STATS                  1
#define UDP_STATS                   1
#define TCP_STATS                   1
#define MEM_STATS                   1
#define MEMP_STATS                  1
#define PBUF_STATS                  1
#define SYS_STATS                   1
#define LWIP_DEBUG                  0

/* ---- 其它 ---- */
#define LWIP_NETIF_HOSTNAME         0
#define LWIP_NETIF_STATUS_CALLBACK  0
#define LWIP_NETIF_LINK_CALLBACK    0
#define LWIP_NETIF_LOOPBACK         0
#define LWIP_HAVE_LOOPIF            0
#define LWIP_BROADCAST_PING         1
#define LWIP_MULTICAST_PING         1
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_CHECKSUM_ON_COPY       0

#endif /* LWIPOPTS_H */
