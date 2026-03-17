#ifndef LWIPOPTS_APP_H
#define LWIPOPTS_APP_H

#define NO_SYS                           1
#define SYS_LIGHTWEIGHT_PROT             0
#define LWIP_CALLBACK_API                1
#define LWIP_EVENT_API                   0
#define LWIP_NETCONN                     0
#define LWIP_SOCKET                      0

#define MEM_ALIGNMENT                    4
#define MEM_SIZE                         (40 * 1024)

#define PBUF_POOL_SIZE                   24
#define PBUF_POOL_BUFSIZE                1536
#define PBUF_LINK_HLEN                   14
#define PBUF_LINK_ENCAPSULATION_HLEN     0

#define MEMP_NUM_PBUF                    24
#define MEMP_NUM_UDP_PCB                 6
#define MEMP_NUM_TCP_PCB                 6
#define MEMP_NUM_TCP_PCB_LISTEN          4
#define MEMP_NUM_TCP_SEG                 72

#define LWIP_ARP                         1
#define ARP_TABLE_SIZE                   10
#define ARP_QUEUEING                     1
#define ETHARP_SUPPORT_STATIC_ENTRIES    1

#define LWIP_ETHERNET                    1
#define LWIP_IPV4                        1
#define LWIP_IPV6                        0
#define IP_FORWARD                       0
#define IP_FRAG                          0
#define IP_REASSEMBLY                    0

#define LWIP_ICMP                        1
#define LWIP_RAW                         1
#define LWIP_DHCP                        0
#define LWIP_DNS                         0

#define LWIP_NETIF_HOSTNAME              1
#define LWIP_NETIF_LINK_CALLBACK         1
#define LWIP_NETIF_STATUS_CALLBACK       1

#define LWIP_UDP                         1
#define LWIP_TCP                         1
#define LWIP_TCP_KEEPALIVE               1
#define TCP_TTL                          255
#define TCP_QUEUE_OOSEQ                  0
#define TCP_MSS                          1460
#define TCP_SND_BUF                      (12 * TCP_MSS)
#define TCP_SND_QUEUELEN                 ((4 * TCP_SND_BUF) / TCP_MSS)
#define TCP_WND                          (12 * TCP_MSS)
#define TCP_WND_UPDATE_THRESHOLD         TCP_MSS

#define CHECKSUM_GEN_IP                  1
#define CHECKSUM_GEN_UDP                 1
#define CHECKSUM_GEN_TCP                 1
#define CHECKSUM_GEN_ICMP                1
#define CHECKSUM_CHECK_IP                1
#define CHECKSUM_CHECK_UDP               1
#define CHECKSUM_CHECK_TCP               1
#define CHECKSUM_CHECK_ICMP              1

#define LWIP_STATS                       0
#define LWIP_STATS_DISPLAY               0
#define LWIP_PROVIDE_ERRNO               1

#define LWIP_DEBUG                       0

#endif