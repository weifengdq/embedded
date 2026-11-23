/*
 * app_can_gw.h - cannelloni v2 兼容的 CAN<->UDP 网关组件（裸机 lwIP）
 *
 * 线上格式与主机 stock cannelloni（v2.1.2）一致，可直接对接：
 *   主机：cannelloni -I vcan0 -R 192.168.2.100 -r 4000 -l 4000
 *   板端：本组件在 UDP 4000 上收/发，CAN1 的帧原样搬到 vcan0。
 *
 * UDP 载荷（全部多字节字段为**网络字节序/大端**）：
 *   +0  u8  version = 2
 *   +1  u8  op_code = 0 (DATA)
 *   +2  u8  seq_no  （发送方按通道自增，接收方不校验）
 *   +3  u16 count   （本包帧数，大端）
 *   +5  帧 0..count-1，每帧：
 *        u32 can_id  （大端，bit31=CAN_EFF_FLAG 扩展帧，bit30=CAN_RTR_FLAG 远程帧）
 *        u8  len     （bit7=0x80 表示 CAN FD 帧，低 7 位 = 真实字节数 0..64）
 *        [u8 flags]  （仅当 len bit7=0x80：Linux canfd_frame.flags，bit0=BRS，bit1=ESI）
 *        u8  data[len & 0x7F]，RTR 帧无数据段
 *
 * 语义：UDP 模式是 fire-and-forget（cannelloni 的 ACK/NACK 在 UDP 路径未实现），
 *       丢包由上层协议（UDS 的 N_Ar/N_As、J1939 TP、CANopen SDO 重传等）保证。
 *
 * 作者：tp-infra
 */
#ifndef APP_CAN_GW_H
#define APP_CAN_GW_H

#include <stdint.h>
#include <stdbool.h>

#include "app_can.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CAN_GW_VERSION            (2U)
#define APP_CAN_GW_OP_DATA            (0U)
#define APP_CAN_GW_HDR_LEN            (5U)
#define APP_CAN_GW_MAX_CH             (4U)
#define APP_CAN_GW_MAX_FRAMES_PER_PKT (16U)
#define APP_CAN_GW_PKT_MAX            (APP_CAN_GW_HDR_LEN + \
                                       APP_CAN_GW_MAX_FRAMES_PER_PKT * (4U + 1U + 1U + APP_CAN_MAX_DATA_LEN))

/* Linux can_id 标志位（线上格式） */
#define APP_CAN_GW_CAN_EFF_FLAG (0x80000000UL)
#define APP_CAN_GW_CAN_RTR_FLAG (0x40000000UL)
/* Linux canfd_frame.flags */
#define APP_CAN_GW_CANFD_BRS    (0x01U)
#define APP_CAN_GW_CANFD_ESI    (0x02U)
/* len 字节的 CAN FD 标志位 */
#define APP_CAN_GW_CANFD_FRAME  (0x80U)

/* 单个通道映射：CAN 实例 <-> 本端 UDP 端口 */
typedef struct app_can_gw_chan_cfg_struct {
    uint8_t can_ch;         /*!< 0..3 = MCAN0..MCAN3 */
    uint16_t udp_port;      /*!< 本端 UDP 端口（推荐 4000 + can_ch） */
    bool enabled;           /*!< 是否启用该通道 */
} app_can_gw_chan_cfg_t;

typedef struct app_can_gw_cfg_struct {
    const char *local_ip;   /*!< 板端 IP（NULL = 任意地址），如 "192.168.2.100" */
    const char *peer_ip;    /*!< 对端 IP（NULL/空 = 从收到的第一个包学习） */
    uint16_t peer_port;     /*!< 对端端口（0 = 从收到的包学习） */
    app_can_speed_t bus_speed;  /*!< 被映射 CAN 通道的位时序档位 */
    uint8_t max_frames_per_pkt; /*!< 单包最多帧数（默认 16） */
    app_can_gw_chan_cfg_t ch[APP_CAN_GW_MAX_CH];
    int ch_count;
} app_can_gw_cfg_t;

typedef struct app_can_gw_stats_struct {
    uint32_t rx_udp_pkts;   /*!< 收到的 UDP 包数 */
    uint32_t tx_udp_pkts;   /*!< 发出的 UDP 包数 */
    uint32_t rx_udp_frames; /*!< 从 UDP 解出的 CAN 帧数 */
    uint32_t tx_udp_frames; /*!< 通过 UDP 发出的 CAN 帧数 */
    uint32_t rx_can_frames; /*!< 从 CAN 收到并转发的帧数 */
    uint32_t tx_can_frames; /*!< 成功写入 CAN 的帧数 */
    uint32_t drop_can;      /*!< CAN 发送失败丢弃（FIFO 满） */
    uint32_t drop_udp;      /*!< UDP 分配/发送失败丢弃 */
    uint32_t bad_pkt;       /*!< 版本/op/长度非法的 UDP 包 */
} app_can_gw_stats_t;

/* ---------------- 契约 API（docs/应用层开发约定.md §4.3） ---------------- */

/**
 * @brief 初始化网关：起 CAN 通道、建 UDP socket、（可选）等 link
 * @param cfg 参数；NULL 用默认（仅 CAN1<->UDP4000，peer 自动学习）
 * @return 0 成功；<0 失败（-1 参数，-2 CAN 初始化失败，-3 UDP socket 失败）
 * @note 调用前必须先 app_lwip_init() 成功（需要 lwIP 已初始化）。
 */
int app_can_gw_init(const app_can_gw_cfg_t *cfg);

/**
 * @brief 主循环轮询：CAN->UDP 打包发送（UDP->CAN 在 udp 回调里完成）
 * @note 建议主循环顺序：app_lwip_poll(); app_can_gw_poll();
 */
void app_can_gw_poll(void);

/* ---------------- 便利 API ---------------- */

/** @brief 取统计快照 */
void app_can_gw_stats(app_can_gw_stats_t *st);

/** @brief 打印统计一行（shell stat 用） */
void app_can_gw_print(void);

/** @brief 使能/禁止转发（禁止时仍收包但不搬帧），用于运行期开关 */
void app_can_gw_set_forward(bool on);

/** @brief 是否正在转发 */
bool app_can_gw_forwarding(void);

/** @brief 取某通道的 UDP 端口（未启用返回 0） */
uint16_t app_can_gw_port(int ch);

/** @brief 强制设置对端（peer_ip 字符串 + 端口），便于 shell 里改主机地址 */
int app_can_gw_set_peer(const char *ip_str, uint16_t port);

/** @brief 上次收到的包来自哪个对端（调试用，返回静态字符串） */
const char *app_can_gw_peer_str(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAN_GW_H */
