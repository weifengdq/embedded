/**
 * CANopenNode application wrapper for hpm5e31_1 (CiA 301 slave node).
 *
 * 使用方式：
 *   co_node_cfg_t cfg = { .can_base = BOARD_CAN0_BASE, .node_id = 1,
 *                         .bitrate_kbps = 500, .heartbeat_ms = 1000 };
 *   co_node_init(&cfg);
 *   while (1) {
 *       switch (co_node_poll()) {
 *       case CO_NODE_RESET_COMM: co_node_reset_comm(); break;
 *       case CO_NODE_RESET_APP:  bsp_reboot(); break;   // 应用复位 = MCU 重启
 *       default: break;
 *       }
 *   }
 *
 * @file        canopen_node.h
 * @copyright   Apache-2.0 (CANopenNode) / hpm5e31_1 项目组
 */
#ifndef CANOPEN_NODE_H
#define CANOPEN_NODE_H

#include <stdint.h>
#include "hpm_mcan_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

/** co_node_poll() 的返回值：栈请求的复位类型 */
typedef enum {
    CO_NODE_RESET_NONE = 0, /**< 正常运行 */
    CO_NODE_RESET_COMM,     /**< NMT Reset Communication：调用 co_node_reset_comm() */
    CO_NODE_RESET_APP       /**< NMT Reset Application：整机重启（bsp_reboot） */
} co_node_reset_t;

typedef struct {
    MCAN_Type* can_base;  /**< MCAN0 (BOARD_CAN0_BASE) */
    uint8_t node_id;      /**< 1..127 */
    uint16_t bitrate_kbps;/**< 500 */
    uint16_t heartbeat_ms;/**< 生产者心跳周期，写入 OD 0x1017，0=关闭 */
    uint8_t nmt_start_operational; /**< 1: 上电自动进入 Operational；0: Pre-operational */
} co_node_cfg_t;

typedef struct {
    uint32_t heap_bytes;   /**< CO_new() 分配的堆字节数 */
    uint32_t rx_frames;    /**< 已接收 CAN 帧数 */
    uint32_t tx_frames;    /**< 已发送 CAN 帧数 */
    uint32_t tx_retry;     /**< TX FIFO 满导致的重试次数 */
    uint32_t pol_loops;    /**< co_node_poll() 调用次数 */
    uint32_t max_cycle_us; /**< 单次 co_node_poll() 的最大耗时（估算） */
    uint8_t nmt_state;     /**< 0=Boot-up 4=Stopped 5=Operational 127=Pre-operational */
    uint16_t can_error_status; /**< CO_CAN_ERR_status_t 位域 */
} co_node_stats_t;

/** 初始化：CO_new -> CAN 硬件 -> NMT/SDO/PDO/SYNC/EMCY/HB 对象。返回 0 成功。 */
int co_node_init(const co_node_cfg_t* cfg);

/** 主循环调用：收帧分发 + 重发 + 协议处理。返回栈请求的复位类型。 */
co_node_reset_t co_node_poll(void);

/** NMT Reset Communication 的处理：重新初始化 CAN + CANopen 对象（保留 OD 变量）。 */
int co_node_reset_comm(void);

/** 读取统计。 */
void co_node_get_stats(co_node_stats_t* stats);

/** 当前 NMT 状态（0/4/5/127）。 */
uint8_t co_node_nmt_state(void);

/** 上报一个 EMCY（应用自定义错误位一般用 CO_EM_MANUFACTURER_START..END）。 */
void co_node_emcy_report(uint8_t error_bit, uint16_t error_code, uint32_t info);

/** 撤销 EMCY。 */
void co_node_emcy_reset(uint8_t error_bit, uint32_t info);

/** SYNC 计数器（OD 0x1019 非 0 时按 0..240 循环，否则恒 0）。 */
uint8_t co_node_sync_counter(void);

/** 底层 CAN 收发计数（CO_driver.c 维护，供日志使用）。 */
extern uint32_t co_can_rx_count;
extern uint32_t co_can_tx_count;
extern uint32_t co_can_tx_retry;

#ifdef __cplusplus
}
#endif
#endif /* CANOPEN_NODE_H */
