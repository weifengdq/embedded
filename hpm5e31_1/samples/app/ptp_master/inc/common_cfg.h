/*
 * Copyright (c) 2023,2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * 网络/LwIP 缓冲配置（PTP 三个例程共用；与 SDK samples/lwip/lwip_ptp/v2/slave/inc/common_cfg.h 一致）
 */

#ifndef COMMON_CFG
#define COMMON_CFG

#define LWIP_APP_TIMER_INTERVAL (1) /* 1 ms：PTP 时基精度直接取决于它 */

#define ENET_TX_BUFF_COUNT  (10U)
#define ENET_RX_BUFF_COUNT  (20U)
#define ENET_TX_BUFF_SIZE   (1536U)
#define ENET_RX_BUFF_SIZE   (1536U)

#endif
