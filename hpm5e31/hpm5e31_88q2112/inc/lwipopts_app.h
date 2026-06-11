/*
 * Copyright (c) 2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef LWIPOPTS_APP_H
#define LWIPOPTS_APP_H

#define MEM_SIZE                16*1024

#define MEMP_NUM_PBUF           32
#define MEMP_NUM_TCP_SEG        32
#define PBUF_POOL_SIZE          24

#define MEM_OVERFLOW_CHECK      2
#define MEMP_OVERFLOW_CHECK     2
#define MEM_SANITY_CHECK        1
#define MEMP_SANITY_CHECK       1

/* ---------- TCP options ---------- */
#define LWIP_TCP                1
#define TCP_TTL                 255
#define TCP_QUEUE_OOSEQ         0
#define TCP_MSS                 (1500 - 40)
#define TCP_SND_BUF             (12 * TCP_MSS)
#define TCP_SND_QUEUELEN        (2 * TCP_SND_BUF / TCP_MSS)
#define TCP_WND                 (12 * TCP_MSS)

#define CHECKSUM_BY_HARDWARE 1

/*
 * Debug Options
 */
#define LWIP_DEBUG                 1
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
