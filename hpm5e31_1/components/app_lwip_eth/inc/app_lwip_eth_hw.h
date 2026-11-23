/*
 * app_lwip_eth_hw.h - Eth 硬件后端接口（netif 与具体驱动之间的唯一缝）
 *
 * 设计目的：把"以太网帧怎么进出控制器"抽成几个回调，netif 层（app_lwip_eth.c）只认这个接口。
 * 正式后端 app_lwip_eth_hw_mcal.c 用 AUTOSAR Eth MCAL API 实现；对照实验后端（SDK enet 驱动）
 * 只存在于 temp2 原型工程，不进仓库。
 *
 * 语义约定（按 mcal/Eth/include/Eth.h 头的"数据通路约定"实现，写后端必须遵守）：
 *   - tx_buffer_get() 返回**帧起始**（含 14 B 以太网头的位置），frame_len 是要写的整帧长度；
 *   - 写入整帧后用 tx_commit() 提交，driver 用 Eth_Transmit(..., PhysAddrPtr=NULL) 语义
 *     （整帧已在缓冲里），并负责补 0 到 60 B、cache clean、交 DMA；
 *   - rx_get() 返回的 frame 指向**驱动内部 Rx 缓冲**（含 14 B 头，不含 FCS），零拷贝；
 *     lwIP 释放 pbuf 时回调 rx_release() 归还，归还前缓冲不会被 DMA 复用；
 *   - cache 维护全在驱动里做（Eth.h §5），本层拿到指针只读；
 *   - 本层不保证回调的并发安全，由 lock/unlock 包住（FreeRTOS 后端用互斥量，裸机为空操作）。
 *
 * 作者：eth-stack（task-2）
 */
#ifndef APP_LWIP_ETH_HW_H
#define APP_LWIP_ETH_HW_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 后端统计（由后端自己维护，netif 层只读） */
typedef struct app_lwip_eth_hw_stats_struct {
    uint32_t tx_frames;         /*!< 成功提交的帧数 */
    uint32_t tx_bytes;          /*!< 提交的字节数（整帧，含 14 B 头，不含 FCS） */
    uint32_t tx_busy;           /*!< 无空闲 Tx 缓冲的次数（丢帧） */
    uint32_t tx_err;            /*!< Eth_Transmit 返回 E_NOT_OK 的次数 */
    uint32_t rx_frames;         /*!< 从驱动取走的帧数 */
    uint32_t rx_bytes;          /*!< 取走的字节数（含 14 B 头） */
    uint32_t rx_err;            /*!< Eth_GetRxBuffer 返回错误状态的次数 */
    uint32_t rx_empty;          /*!< 轮询时"没有新帧"的次数（观察空转率） */
} app_lwip_eth_hw_stats_t;

/** 链路信息（来自 EthTrcv / Eth 控制器状态） */
typedef struct app_lwip_eth_hw_link_struct {
    bool up;                    /*!< 链路是否 up */
    bool known;                 /*!< 状态是否可信（EthTrcv 未接/未就绪时 false） */
    uint16_t speed_mbps;        /*!< 1000 / 100 / 0=未知 */
    bool full_duplex;           /*!< 全双工 */
    bool master;                /*!< 主从（1000BASE-T1 有意义） */
} app_lwip_eth_hw_link_t;

/** 后端操作表 */
typedef struct app_lwip_eth_hw_ops_struct {
    const char *name;           /*!< 后端名，打印用（"mcal-eth" / "sdk-enet"） */
    uint16_t max_frame_len;     /*!< 单帧上限（含 14 B 头，不含 FCS） */
    uint16_t rx_buf_count;      /*!< Rx 缓冲个数（决定 custom pbuf 池大小） */
    uint16_t tx_buf_count;      /*!< Tx 缓冲个数（决定 PBUF_POOL_SIZE 建议值） */

    /** @brief 探测/初始化后端；返回 0 = Eth 已初始化且控制器 ACTIVE */
    int  (*init)(uint8_t ctrl_idx);

    /** @brief 轮询驱动内部事务（Eth_MainFunction），可为 NULL */
    void (*poll)(uint8_t ctrl_idx);

    /** @brief 读控制器 MAC（网络字节序 6 B） */
    void (*get_mac)(uint8_t ctrl_idx, uint8_t mac[6]);

    /**
     * @brief 申请 Tx 缓冲（帧起始，含 14 B 头）
     * @param frame_len  要写入的整帧长度
     * @param cookie_out 驱动句柄（交给 tx_commit）
     * @return 可写指针；NULL = 无空闲缓冲
     */
    uint8_t *(*tx_buffer_get)(uint8_t ctrl_idx, uint16_t frame_len, void **cookie_out);

    /** @brief 提交整帧（含 14 B 头）；返回 0 成功 */
    int  (*tx_commit)(uint8_t ctrl_idx, void *cookie, uint16_t frame_len);

    /**
     * @brief 取一帧（零拷贝）
     * @param frame_out 指向驱动 Rx 缓冲（含 14 B 头）
     * @param len_out   帧长度（含 14 B 头，不含 FCS）
     * @param cookie_out 释放句柄（交给 rx_release）
     * @return 0 = 取到一帧；1 = 暂无帧；<0 = 出错
     */
    int  (*rx_get)(uint8_t ctrl_idx, const uint8_t **frame_out, uint16_t *len_out, void **cookie_out);

    /** @brief 归还 Rx 缓冲（lwIP 释放 pbuf 时回调） */
    void (*rx_release)(uint8_t ctrl_idx, void *cookie);

    /** @brief 读链路状态 */
    void (*link_get)(uint8_t ctrl_idx, app_lwip_eth_hw_link_t *out);

    /** @brief 打印后端私有寄存器/计数（可为 NULL） */
    void (*print)(uint8_t ctrl_idx);

    /** @brief 取后端统计 */
    void (*stats_get)(uint8_t ctrl_idx, app_lwip_eth_hw_stats_t *out);

    /** @brief 串行化保护（FreeRTOS 多任务访问 Eth 驱动时用；裸机为空） */
    void (*lock)(uint8_t ctrl_idx);
    void (*unlock)(uint8_t ctrl_idx);
} app_lwip_eth_hw_ops_t;

/** 取 MCAL Eth 后端（app_lwip_eth_hw_mcal.c 提供） */
const app_lwip_eth_hw_ops_t *app_lwip_eth_hw_mcal(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LWIP_ETH_HW_H */
