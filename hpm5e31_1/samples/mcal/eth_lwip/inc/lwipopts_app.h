/*
 * lwipopts_app.h - eth_lwip（裸机）的 lwIP 选项
 *
 * 内存预算刻意与 samples/enet/enet_lwip_iperf（第 13 章 935 Mbit/s 下行基线）保持一致，
 * 这样两份数据的差异只来自"驱动层"：
 *   - 基线：SDK enet 驱动 + 描述符直指 pbuf（零拷贝 TX）+ 硬件校验和插入
 *   - 本例：MCAL Eth + 驱动缓冲 memcpy TX + 软件校验和
 * 通过 -DUSE_LWIPOPTS_APP_H=1 被 SDK samples/lwip/ports/baremetal/lwipopts.h 包含。
 */
#ifndef LWIPOPTS_APP_H
#define LWIPOPTS_APP_H

/* PBUF_POOL 至少 >= Eth Tx 缓冲数（8），否则 TCP 重传时可能饿死 pbuf 池 */
/* 窗口大小：试过 16*MSS + MEM_SIZE 32 KB 的版本，board->host 反而从 131 Mbit/s
 * 掉到 47 Mbit/s 且出现 0.26 Mbit/s 的长时间停摆（TCP_SND_BUF 放大后一次进
 * 发送队列的段更多，1.6% 的发送帧 CRC 错更容易打中整窗，RTO 代价被放大；
 * 同时 MEM_SIZE 32 KB 把 DLM 用到 83.6%）。因此维持 8*MSS 的原始预算，
 * 与第 13 章 SDK 基线（935 Mbit/s）口径一致。见第 50 章 §50.9.15/§50.8.7。 */
#define PBUF_POOL_SIZE          (16)
#define MEM_SIZE                (24 * 1024)

#define MEMP_NUM_PBUF           (50)
#define MEMP_NUM_TCP_SEG        (40)
#define MEMP_NUM_UDP_PCB        (8)
#define MEMP_NUM_SYS_TIMEOUT    (12)

#define LWIP_UDP                1
#define LWIP_TCP                1
#define LWIP_ICMP               1
#define LWIP_RAW                1
#define LWIP_DHCP               0
#define LWIP_DNS                0

#define TCP_TTL                 255
#define TCP_QUEUE_OOSEQ         0
#define TCP_MSS                 (1500 - 40)
#define TCP_SND_BUF             (8 * TCP_MSS)
#define TCP_SND_QUEUELEN        (2 * TCP_SND_BUF / TCP_MSS)
#define TCP_WND                 (8 * TCP_MSS)

/* 注意：**不要**定义 CHECKSUM_BY_HARDWARE。MCAL Eth 没有校验和卸载 API，
   IP/TCP/UDP/ICMP 校验和由 lwIP 软件计算（第 50 章"性能"节给出开关对比）。 */

#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_STATUS_CALLBACK 0

#define LWIP_DEBUG              1
#define LWIP_DBG_MIN_LEVEL      0
#define NETIF_DEBUG             LWIP_DBG_OFF
#define PBUF_DEBUG              LWIP_DBG_OFF
#define MEM_DEBUG               LWIP_DBG_OFF
#define MEMP_DEBUG              LWIP_DBG_OFF
#define TCP_DEBUG               LWIP_DBG_OFF
#define TCP_INPUT_DEBUG         LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG        LWIP_DBG_OFF
#define TCPIP_DEBUG             LWIP_DBG_OFF
#define IP_DEBUG                LWIP_DBG_OFF
#define ICMP_DEBUG              LWIP_DBG_OFF
#define UDP_DEBUG               LWIP_DBG_OFF
#define ETHARP_DEBUG            LWIP_DBG_OFF

#endif /* LWIPOPTS_APP_H */
