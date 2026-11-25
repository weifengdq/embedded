/*
 * lwipopts_app.h - app_lwip 组件的 lwIP 选项（应用层覆盖）
 *
 * 来源：SDK samples/lwip/lwip_iperf/inc/lwipopts_app.h（2026 版），
 *      内存预算与 samples/enet/enet_lwip_iperf 完全一致（第 13 章 935 Mbit/s 下行配置）。
 *      只做两处追加：
 *        1) MEMP_NUM_UDP_PCB = 8（网关 1~3 路 + SOME/IP SD/服务多路 socket）；
 *        2) 显式打开 UDP（默认 lwipopts.h 已开，写出来便于查）。
 * 通过 -DUSE_LWIPOPTS_APP_H=1 让 SDK ports/baremetal/lwipopts.h 包含本文件。
 */
#ifndef LWIPOPTS_APP_H
#define LWIPOPTS_APP_H

#include "common_cfg.h"

/*
 * Zero-copy TX holds each pbuf until DMA completes (up to one per TX descriptor).
 * Keep PBUF_POOL_SIZE >= ENET_TX_BUFF_COUNT so TX hold does not starve the pbuf pool.
 * RX zero-copy uses a separate custom pbuf pool sized by ENET_RX_BUFF_COUNT.
 */
#define PBUF_POOL_SIZE          (ENET_TX_BUFF_COUNT)

/* lwIP 堆：批量拷贝的报文与 socket 控制块都从这里分配 */
#define MEM_SIZE                (24 * 1024)

/* memp 结构体池 */
#define MEMP_NUM_PBUF           50
#define MEMP_NUM_TCP_SEG        40

/* ---------- UDP / socket 数量 ---------- */
#define LWIP_UDP                1
#define MEMP_NUM_UDP_PCB        8

/* ---------- TCP options（网关只用 UDP，但保留 TCP 供 ping/调参） ---------- */
#define LWIP_TCP                1
#define TCP_TTL                 255
#define TCP_QUEUE_OOSEQ         0
#define TCP_MSS                 (1500 - 40)
#define TCP_SND_BUF             (8 * TCP_MSS)
#define TCP_SND_QUEUELEN        (2 * TCP_SND_BUF  / TCP_MSS)
#define TCP_WND                 (8 * TCP_MSS)

/* ---------- ICMP（主机 ping 验证链路） ---------- */
#define LWIP_ICMP               1

/* ---------- 校验和：IP/TCP/UDP/ICMP 由 ENET 硬件插入 ---------- */
#define CHECKSUM_BY_HARDWARE

/* ENET TX HW CRC mode: 0=append, 1=replace, 2=off */
#ifndef ENET_TX_HW_CRC_MODE
#define ENET_TX_HW_CRC_MODE 1
#endif

/* ---------- Debug Options（全部关闭，避免 printf 进热路径） ---------- */
#define LWIP_DEBUG                 0
#define LWIP_DBG_MIN_LEVEL         0
#define PPP_DEBUG                  LWIP_DBG_OFF
#define MEM_DEBUG                  LWIP_DBG_OFF
#define MEMP_DEBUG                 LWIP_DBG_OFF
#define PBUF_DEBUG                 LWIP_DBG_OFF
#define API_LIB_DEBUG              LWIP_DBG_OFF
#define API_MSG_DEBUG              LWIP_DBG_OFF
#define TCPIP_DEBUG                LWIP_DBG_OFF
#define NETIF_DEBUG                LWIP_DBG_OFF
#define SOCKETS_DEBUG              LWIP_DBG_OFF
#define DNS_DEBUG                  LWIP_DBG_OFF
#define AUTOIP_DEBUG               LWIP_DBG_OFF
#define DHCP_DEBUG                 LWIP_DBG_OFF
#define IP_DEBUG                   LWIP_DBG_OFF
#define IP_REASS_DEBUG             LWIP_DBG_OFF
#define ICMP_DEBUG                 LWIP_DBG_OFF
#define IGMP_DEBUG                 LWIP_DBG_OFF
#define UDP_DEBUG                  LWIP_DBG_OFF
#define TCP_DEBUG                  LWIP_DBG_OFF
#define TCP_INPUT_DEBUG            LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG           LWIP_DBG_OFF
#define TCP_RTO_DEBUG              LWIP_DBG_OFF
#define TCP_CWND_DEBUG             LWIP_DBG_OFF
#define TCP_WND_DEBUG              LWIP_DBG_OFF
#define TCP_FR_DEBUG               LWIP_DBG_OFF
#define TCP_QLEN_DEBUG             LWIP_DBG_OFF
#define TCP_RST_DEBUG              LWIP_DBG_OFF
#define ETHARP_DEBUG               LWIP_DBG_OFF

#endif /* LWIPOPTS_APP_H */
