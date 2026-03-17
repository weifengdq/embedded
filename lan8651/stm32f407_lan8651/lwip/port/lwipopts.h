/**
 * @file  lwipopts.h
 * @brief lwIP configuration for bare-metal STM32H563 + LAN8651
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* ---------- NO OS (bare-metal) ---------- */
#define NO_SYS                      1
#define SYS_LIGHTWEIGHT_PROT        0
#define LWIP_CALLBACK_API           1
#define LWIP_EVENT_API              0
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0

/* ---------- Memory ---------- */
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (40 * 1024)

/* ---------- Pbuf ---------- */
#define PBUF_POOL_SIZE              32
#define PBUF_POOL_BUFSIZE           1536
#define PBUF_LINK_HLEN              14       /* Ethernet header */
#define PBUF_LINK_ENCAPSULATION_HLEN 0

/* ---------- TCP ---------- */
#define LWIP_TCP                    1
#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * TCP_SND_BUF) / TCP_MSS)
#define TCP_WND_UPDATE_THRESHOLD    TCP_MSS
#define TCP_QUEUE_OOSEQ             0
#define LWIP_TCP_KEEPALIVE          1
#define MEMP_NUM_TCP_PCB            6
#define MEMP_NUM_TCP_PCB_LISTEN     4
#define MEMP_NUM_TCP_SEG            64
#define MEMP_NUM_PBUF               32

/* ---------- UDP ---------- */
#define LWIP_UDP                    1

/* ---------- ICMP (ping) ---------- */
#define LWIP_ICMP                   1
#define LWIP_RAW                    1

/* ---------- ARP ---------- */
#define LWIP_ARP                    1
#define ARP_TABLE_SIZE              10
#define ARP_QUEUEING                1
#define ETHARP_SUPPORT_STATIC_ENTRIES 1

/* ---------- IP ---------- */
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define IP_FORWARD                  0
#define IP_FRAG                     0
#define IP_REASSEMBLY               0

/* ---------- DHCP ---------- */
#define LWIP_DHCP                   0    /* Static IP for now */

/* ---------- Ethernet ---------- */
#define LWIP_ETHERNET               1

/* ---------- Statistics ---------- */
#define LWIP_STATS                  0
#define LWIP_STATS_DISPLAY          0

/* ---------- Debug ---------- */
#define LWIP_DEBUG                  0
/* Uncomment to enable specific debug: */
/* #define ETHARP_DEBUG      LWIP_DBG_ON */
/* #define ICMP_DEBUG        LWIP_DBG_ON */
/* #define UDP_DEBUG         LWIP_DBG_ON */
/* #define IP_DEBUG          LWIP_DBG_ON */
/* #define NETIF_DEBUG       LWIP_DBG_ON */

/* ---------- Checksum ---------- */
/* Use software checksums (no HW offload for SPI ethernet) */
#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_GEN_TCP            1
#define CHECKSUM_GEN_ICMP           1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_TCP          1
#define CHECKSUM_CHECK_ICMP         1

/* ---------- Misc ---------- */
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1

#endif /* LWIPOPTS_H */
