/*
 * Copyright (c) 2025-2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * 应用层 lwIP 选项（PTP 三个例程共用）
 * 与 SDK samples/lwip/lwip_ptp/v2/slave/inc/lwipopts_app.h 一致，仅补充注释。
 */

#ifndef LWIPOPTS_APP_H
#define LWIPOPTS_APP_H

#include "common_cfg.h"

/* 零拷贝 TX 会把 pbuf 扣到 DMA 完成，PBUF_POOL_SIZE 不能小于 TX 描述符数 */
#define PBUF_POOL_SIZE          (ENET_TX_BUFF_COUNT)

/* PTP 依赖 IGMP 加入 224.0.1.129 / 224.0.0.107 组播组 */
#define LWIP_IGMP 1

/* IP/UDP/TCP 校验和由 ENET 硬件插入（common.c 在 CHECKSUM_BY_HARDWARE 下调用
 * enet_set_hw_checksum_config(..., true)）；关闭 lwIP 软件校验和生成。
 * 注意：PTP 报文若校验和错，主机 ptp4l 会直接丢弃，现象是"板子有打印但主机不认主"。*/
#define CHECKSUM_BY_HARDWARE

/* ENET TX HW CRC 模式：1 = replace（硬件在末尾 4 字节写 FCS） */
#ifndef ENET_TX_HW_CRC_MODE
#define ENET_TX_HW_CRC_MODE 1
#endif

/* 调试：PTP 协议栈的 DBG 走 ptpd 自己的 PTPD_DBG 宏，这里全部关掉 lwIP 调试 */
#define LWIP_DEBUG                 0

#endif /* LWIPOPTS_APP_H */
