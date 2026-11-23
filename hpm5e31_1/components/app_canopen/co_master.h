/**
 * Minimal CANopen (CiA 301) master / test client for hpm5e31_1 Bench A.
 *
 * 用途：在没有上位机 + cannelloni 网关时，用同一块 MCU 的第二个 CAN 节点（CAN1）做
 * CANopen 主站，闭环验证 CAN0 上的从站（NMT/SDO/PDO/SYNC/心跳/EMCY）。
 * 同时它也是"主站侧最小实现"的教学参考：不依赖 CANopenNode，只用 MCAN 驱动 + 协议状态机。
 *
 * 非阻塞模型：co_master_poll() 每次把 MCAN RX FIFO0 的帧搬进环形队列；
 * SDO/等待类函数在等待期间反复调用 poll，并通过 *service* 回调驱动被测从站
 * （同 MCU 上从站的 co_node_poll()），否则会互相饿死。
 *
 * @file        co_master.h
 * @copyright   Apache-2.0 / hpm5e31_1 项目组
 */
#ifndef CO_MASTER_H
#define CO_MASTER_H

#include <stdint.h>
#include "hpm_mcan_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CO_MASTER_FIFO_LEN 64U

typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint32_t ts_us; /**< 收到时刻（mcycle/CPU MHz 换算的 µs，单调） */
} co_master_frame_t;

typedef struct {
    MCAN_Type* can;
    uint32_t cpu_mhz;
    uint8_t target_node;
    void (*service)(void); /**< 等待期间调用的服务函数（驱动同 MCU 的从站） */

    /* 帧环形队列 */
    co_master_frame_t fifo[CO_MASTER_FIFO_LEN];
    uint32_t fifo_head;  /* 写 */
    uint32_t fifo_tail;  /* 读 */
    uint32_t fifo_lost;

    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t hb_frames;   /**< 0x700+node 心跳帧计数（co_master_poll 内统计，不消费队列） */
    uint32_t emcy_frames; /**< 0x080+node EMCY 帧计数 */
    uint8_t emcy_last[8]; /**< 最近一条 EMCY 的数据字节 */
    uint32_t sdo_timeouts;
    uint32_t sdo_aborts;
    uint32_t last_abort_code;
} co_master_t;

/** NMT 命令字（CiA 301 表） */
#define CO_MASTER_NMT_START      0x01U
#define CO_MASTER_NMT_STOP       0x02U
#define CO_MASTER_NMT_PREOP      0x80U
#define CO_MASTER_NMT_RESET_APP  0x81U
#define CO_MASTER_NMT_RESET_COMM 0x82U

/** 释放 CAN 控制器（切到网关模式前必须调用，否则 app_can_init 会重复初始化 MCAN1）。 */
void co_master_disable(co_master_t* m);

/** 初始化 CAN1 为 500 kbit/s 测试主站。返回 0 成功。 */
int co_master_init(co_master_t* m, MCAN_Type* base, uint16_t bitrate_kbps);

/** 设置被测节点号（决定 SDO COB-ID）。 */
void co_master_set_node(co_master_t* m, uint8_t node);

/** 设置等待期间的服务回调（一般指向从站轮询函数）。 */
void co_master_set_service(co_master_t* m, void (*service)(void));

/** 非阻塞轮询：把 RX FIFO 的帧搬进队列；返回本次收到的帧数。 */
uint32_t co_master_poll(co_master_t* m);

/** 从队列取一帧；返回 1 表示取到。 */
int co_master_pop(co_master_t* m, co_master_frame_t* frame);

/** 等待指定 ID 的帧（期间调用 service）；返回 0 成功。 */
int co_master_wait_id(co_master_t* m, uint16_t id, uint32_t timeout_ms, co_master_frame_t* frame);

/** 发送任意经典帧（DLC<=8）。返回 0 成功。 */
int co_master_send(co_master_t* m, uint16_t id, const uint8_t* data, uint8_t dlc);

/** 发送 NMT 命令（0x000, DLC=2: cmd,node）。 */
int co_master_nmt(co_master_t* m, uint8_t node, uint8_t cmd);

/** 发送 SYNC（0x080, DLC=0 或 1）。 */
int co_master_sync(co_master_t* m);

/** 发送 SYNC + 计数器（DLC=1，用于 0x1019 非 0 的同步计数）。 */
int co_master_sync_counter(co_master_t* m, uint8_t counter);

/** SDO 加速读（<=4 字节）。value 按小端拼成 uint32。 */
int co_master_sdo_read(co_master_t* m, uint8_t node, uint16_t index, uint8_t sub, uint32_t* value,
                       uint32_t timeout_ms);

/** SDO 加速写。size 为 1..4。 */
int co_master_sdo_write(co_master_t* m, uint8_t node, uint16_t index, uint8_t sub, uint32_t value, uint8_t size,
                        uint32_t timeout_ms);

/** SDO 分段上传（upload）：把对象读到 buf。返回 0 成功，*len 为字节数。 */
int co_master_sdo_upload(co_master_t* m, uint8_t node, uint16_t index, uint8_t sub, uint8_t* buf, uint32_t cap,
                         uint32_t* len, uint32_t timeout_ms);

/** SDO 分段下载（download）：把 buf 写入对象。 */
int co_master_sdo_download(co_master_t* m, uint8_t node, uint16_t index, uint8_t sub, const uint8_t* buf,
                           uint32_t len, uint32_t timeout_ms);

/** 忙碌等待 ms，期间持续调用 service + poll。 */
void co_master_delay_ms(co_master_t* m, uint32_t ms);

/** 单调 µs 时基。 */
uint32_t co_master_now_us(co_master_t* m);

/** 打印队列中的帧（n=0 表示全部）。 */
void co_master_mon_print(co_master_t* m, uint32_t n);

#ifdef __cplusplus
}
#endif
#endif /* CO_MASTER_H */
