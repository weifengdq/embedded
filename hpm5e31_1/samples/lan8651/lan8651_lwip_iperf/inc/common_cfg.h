#ifndef COMMON_CFG_H
#define COMMON_CFG_H

/* 1 ms lwIP application timer (board_timer_create in main.c) */
#define LWIP_APP_TIMER_INTERVAL (1)

/* LAN8651 link: 10 Mbit/s half duplex on a shared 10BASE-T1S segment */
#define LAN8651_NETIF_MTU       (1500U)
#define LAN8651_PBUF_POOL_SIZE  (12U)

#endif /* COMMON_CFG_H */
