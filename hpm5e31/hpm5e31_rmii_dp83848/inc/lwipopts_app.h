/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef LWIPOPTS_APP_H
#define LWIPOPTS_APP_H

/* Heap size: 24KB saves 8KB DLM vs 32KB, making room for 24/24 descriptors */
#define MEM_SIZE                (24 * 1024)

/* PBUF pool */
#define MEMP_NUM_PBUF           32
#define PBUF_POOL_SIZE          16

/* TCP segments */
#define MEMP_NUM_TCP_SEG        32

/* TCP options */
#define LWIP_TCP                1
#define TCP_TTL                 255
#define TCP_QUEUE_OOSEQ         0
#define TCP_MSS                 (1500 - 40)
#define TCP_SND_BUF             (12 * TCP_MSS)
#define TCP_SND_QUEUELEN        (2 * TCP_SND_BUF / TCP_MSS)
#define TCP_WND                 (12 * TCP_MSS)

/* UDP options */
#define LWIP_UDP                1
#define UDP_TTL                 255
#define MEMP_NUM_UDP_PCB        8

/* Checksum offload */
#define CHECKSUM_BY_HARDWARE    1

/* Debug (disabled by default for release) */
#define LWIP_DEBUG              0

#endif /* LWIPOPTS_APP_H */
