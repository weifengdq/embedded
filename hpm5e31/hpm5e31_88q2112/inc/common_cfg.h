/*
 * Copyright (c) 2022-2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef COMMON_CFG
#define COMMON_CFG

#define Q2112_ADDR             (7U)  /* PHYAD strapping default: 111 = 7 */
#define APP_UDP_ECHO_PORT      (5005U)
#define APP_ENET_RGMII_TX_DLY  (0U)
#define APP_ENET_RGMII_RX_DLY  (0U)

#define LWIP_APP_TIMER_INTERVAL (1) /* 1 ms*/

#define ENET_TX_BUFF_COUNT  (30U)
#define ENET_RX_BUFF_COUNT  (30U)
#define ENET_TX_BUFF_SIZE   (1536U)
#define ENET_RX_BUFF_SIZE   (1536U)

#endif
