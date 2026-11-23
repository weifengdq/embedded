/**
 * CANopenNode target definitions for HPM5E31 (HPMicro MCAN, classic CAN).
 *
 * 本文件是 CANopenNode v4.0 的 `CO_driver_target.h` 移植层（HPM5E31 / hpm5e31_1 板）。
 * 设计要点：
 *   - 单线程轮询式驱动：接收由 co_node_poll() -> CO_CANpoll_rx() 主动从 MCAN RX FIFO0 取出，
 *     不做 CAN 中断；发送失败时把 CO_CANtx_t::bufferFull 置位，由 CO_CANpoll_tx() 重试。
 *   - 因此 CO_LOCK_* 全部为空宏，CO_FLAG_* 用 GCC 内存屏障保证可见性。
 *   - 只接收经典帧、11 位 ID（CANopen CiA 301 不使用扩展帧/CAN FD）。
 *
 * @file        CO_driver_target.h
 * @copyright   Apache-2.0 (CANopenNode) / hpm5e31_1 项目组
 */
#ifndef CO_DRIVER_TARGET_H
#define CO_DRIVER_TARGET_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------- 栈配置覆盖（必须在各对象头之前生效） ------------------------- */
/* 单线程主循环：无 RT 线程、无回调前置钩子 */
#define CO_CONFIG_GLOBAL_FLAG_CALLBACK_PRE  (0)
#define CO_CONFIG_GLOBAL_RT_FLAG_CALLBACK_PRE (0)
#define CO_CONFIG_GLOBAL_FLAG_TIMERNEXT     (0)
/* 本移植不做非易失存储（0x1010/0x1011 未实现），关闭 storage 相关代码 */
#define CO_CONFIG_STORAGE                   (0)
/* 分段 SDO 的 OD 读写缓冲：默认 32 B，加大到 64 B 可减少 1 KB/4 KB 对象的读写次数 */
#define CO_CONFIG_SDO_SRV_BUFFER_SIZE       (64)
/* 本移植不使用 LSS（节点号/波特率固定），也不做 SRDO/GFC/网关/Trace */
#define CO_CONFIG_LSS                       (0)
/* 使用 303/CO_LEDs.c 计算 CANopen 状态灯（RUN/ERR） */
#define CO_CONFIG_LEDS                      (CO_CONFIG_LEDS_ENABLE | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT)

/* ------------------------------- 基本类型 / 字节序 ------------------------------- */
#define CO_LITTLE_ENDIAN
#define CO_SWAP_16(x) (x)
#define CO_SWAP_32(x) (x)
#define CO_SWAP_64(x) (x)

typedef uint_fast8_t bool_t;
typedef float float32_t;
typedef double float64_t;

/* ------------------------------ 收到的 CAN 帧结构 ------------------------------ */
/** 经典 CAN 帧（CANopen 只用 0..8 字节）。由 CO_CANpoll_rx() 填好后传给各对象回调。 */
typedef struct {
    uint16_t ident; /**< 11 位标准 ID */
    uint8_t DLC;    /**< 数据长度 0..8 */
    uint8_t data[8];
} CO_CANrxMsg_t;

#define CO_CANrxMsg_readIdent(msg) (((const CO_CANrxMsg_t*) (msg))->ident)
#define CO_CANrxMsg_readDLC(msg)   (((const CO_CANrxMsg_t*) (msg))->DLC)
#define CO_CANrxMsg_readData(msg)  (((const CO_CANrxMsg_t*) (msg))->data)

/** 一个接收对象（由栈的 CO_CANrxBufferInit() 配置，软件匹配 ID/MASK） */
typedef struct {
    uint16_t ident;
    uint16_t mask;
    void* object;
    void (*CANrx_callback)(void* object, void* message);
} CO_CANrx_t;

/** 一个发送对象（栈填 data[]，驱动负责送进 MCAN TX FIFO） */
typedef struct {
    uint32_t ident;
    uint8_t DLC;
    uint8_t data[8];
    volatile bool_t bufferFull;
    volatile bool_t syncFlag;
} CO_CANtx_t;

/** CAN 模块对象（每个 CANopen 实例一个） */
typedef struct {
    void* CANptr;                    /**< MCAN_Type* */
    CO_CANrx_t* rxArray;
    uint16_t rxSize;
    CO_CANtx_t* txArray;
    uint16_t txSize;
    uint16_t CANerrorStatus;
    volatile bool_t CANnormal;
    volatile bool_t useCANrxFilters; /**< 恒为 false：软件过滤 */
    volatile bool_t bufferInhibitFlag;
    volatile bool_t firstCANtxMessage;
    volatile uint16_t CANtxCount;
    uint32_t errOld;
} CO_CANmodule_t;

/** 存储对象（本移植未实现，仅保留类型以满足栈头文件） */
typedef struct {
    void* addr;
    size_t len;
    uint8_t subIndexOD;
    uint8_t attr;
    void* addrNV;
} CO_storage_entry_t;

/* -------------------------------- 临界区 / 同步 -------------------------------- */
/* 全部代码在 main loop 中顺序执行，没有并发访问；中断里不做 CANopen 处理。 */
#define CO_LOCK_CAN_SEND(CAN_MODULE)
#define CO_UNLOCK_CAN_SEND(CAN_MODULE)
#define CO_LOCK_EMCY(CAN_MODULE)
#define CO_UNLOCK_EMCY(CAN_MODULE)
#define CO_LOCK_OD(CAN_MODULE)
#define CO_UNLOCK_OD(CAN_MODULE)

#define CO_MemoryBarrier() __sync_synchronize()
#define CO_FLAG_READ(rxNew) ((rxNew) != NULL)
#define CO_FLAG_SET(rxNew)                                                                                             \
    {                                                                                                                  \
        CO_MemoryBarrier();                                                                                            \
        rxNew = (void*) 1L;                                                                                            \
    }
#define CO_FLAG_CLEAR(rxNew)                                                                                           \
    {                                                                                                                  \
        CO_MemoryBarrier();                                                                                            \
        rxNew = NULL;                                                                                                  \
    }

/* --------------------- 本移植额外的轮询接口（非 CANopenNode 标准 API） --------------------- */
/** 把 MCAN RX FIFO0 里已收到的帧取出并按 ID/MASK 分发给 rxArray 中的回调。主循环调用。 */
void CO_CANpoll_rx(CO_CANmodule_t* CANmodule);
/** 重试 txArray 中 bufferFull 的帧（上次 TX FIFO 满）。主循环调用。 */
void CO_CANpoll_tx(CO_CANmodule_t* CANmodule);

#ifdef __cplusplus
}
#endif
#endif /* CO_DRIVER_TARGET_H */
