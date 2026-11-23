/*
 * xcp_port_can.h —— XCP on CAN 传输层接口（MCAN 经典帧 500 kbit/s）
 */
#ifndef XCP_PORT_CAN_H
#define XCP_PORT_CAN_H

#include "xcp.h"
#include "hpm_mcan_drv.h"

/* 初始化从站 CAN 节点；返回 0 成功，<0 失败
 * base   : MCAN_Type*（本板 DUT 用 BOARD_CAN0_BASE）
 * cmd_id : 主机 -> 从站 的 CAN ID（A2L/上位机配置里的 CAN_ID_MASTER）
 * res_id : 从站 -> 主机 的 CAN ID（含 DAQ DTO，CAN_ID_SLAVE）
 */
int xcp_can_init(MCAN_Type *base, uint16_t cmd_id, uint16_t res_id, bool ext_id);

/* 传输层统计：总线关闭次数 / RX FIFO 溢出 / TX FIFO 满（发送失败） */
void xcp_can_get_stats(uint32_t *busoff, uint32_t *rx_overflow, uint32_t *tx_full);
void xcp_can_reset_stats(void);

/* 交给 xcp_set_transport() 用 */
extern const xcp_transport_ops_t xcp_can_ops;

#endif /* XCP_PORT_CAN_H */
