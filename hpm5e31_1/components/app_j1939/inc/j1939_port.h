/*
 * j1939_port.h - J1939 端口层（HPM MCAN 实现）
 *
 * 协议核心只依赖 app_j1939.h 里的 4 个函数声明；本文件把 j1939_bus_t 绑定到
 * 一路 MCAN（CAN0..CAN3）。同一 MCU 可建多个 bus 实例 = 多个 J1939 节点。
 */
#ifndef J1939_PORT_H
#define J1939_PORT_H

#include "app_j1939.h"
#include "hpm_mcan_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

struct j1939_bus {
    MCAN_Type *base;
    uint32_t *msg_buf;      /* MCAN 消息 RAM（部分 SoC 需放 AHB SRAM） */
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t tx_errors;
    uint32_t dropped;       /* 非扩展帧/远程帧/FD 帧被丢弃计数 */
};

/*
 * 初始化一路 MCAN 为 J1939 经典帧模式。
 *   bitrate: 250000（标准）或 500000（实验）；采样点固定 87.5%
 *   msg_buf: MCAN_MSG_BUF_SIZE_IN_WORDS 个 32 位字的缓冲（调用者提供）
 * 返回 0 成功。
 */
int j1939_bus_init(j1939_bus_t *bus, MCAN_Type *base, uint32_t *msg_buf, uint32_t bitrate);

void j1939_bus_stats(j1939_bus_t *bus, uint32_t *tx, uint32_t *rx, uint32_t *err);

#ifdef __cplusplus
}
#endif
#endif /* J1939_PORT_H */
