#ifndef LWIPOPTS_APP_H
#define LWIPOPTS_APP_H

#include "common_cfg.h"

/*
 * 10BASE-T1S is a 10 Mbit/s half-duplex shared segment; a 64 KB window is not
 * useful (BDP < 2 KB) but a small window trims RAM.  Keep two MSS in flight.
 */
#define LWIP_NO_SYS             1
#define NO_SYS                  1
#define SYS_LIGHTWEIGHT_PROT    0

#define MEM_SIZE                (20 * 1024)
#define MEMP_NUM_PBUF           (24)
#define PBUF_POOL_SIZE          (LAN8651_PBUF_POOL_SIZE)
#define MEMP_NUM_TCP_SEG        (16)
#define MEMP_NUM_UDP_PCB        (4)
#define MEMP_NUM_TCP_PCB        (4)
#define MEMP_NUM_ARP_QUEUE      (8)
#define MEMP_NUM_SYS_TIMEOUT    (8)

#define LWIP_ARP                1
#define LWIP_ETHERNET           1
#define LWIP_ICMP               1
#define LWIP_TCP                1
#define LWIP_UDP                1
#define LWIP_IPV4               1
#define LWIP_DHCP               0
#define LWIP_DNS                0
#define LWIP_SOCKET             0
#define LWIP_NETCONN            0
#define LWIP_NETIF_LINK_CALLBACK 1

#define TCP_MSS                 (1500 - 40)
#define TCP_SND_BUF             (4 * TCP_MSS)
#define TCP_SND_QUEUELEN        (2 * TCP_SND_BUF / TCP_MSS)
#define TCP_WND                 (4 * TCP_MSS)
#define TCP_QUEUE_OOSEQ         0
#define TCP_LISTEN_BACKLOG      1

#define LWIP_STATS              0
#define LWIP_DEBUG              0

/* TCP checksum is computed in software (LAN8651 only appends the FCS) */
#define CHECKSUM_GEN_IP         1
#define CHECKSUM_GEN_TCP        1
#define CHECKSUM_GEN_UDP        1

#endif /* LWIPOPTS_APP_H */
