/*
 * gs_usb.h - gs_usb / candleLight 兼容协议的纯软件层（不依赖 USB 栈、不依赖 HPM SDK）
 *
 * 事实来源：本机内核 7.0.0 gs_usb.ko 驱动源码（drivers/net/can/usb/gs_usb.c）与
 *           Roboparty/roboparty_EtherCANFD（同 SoC 参考实现）对协议行为的交叉核对；
 *           本文件只按协议事实重写结构与编解码，不包含任何 GPL 源码。
 *
 * 客户端：samples/app/gs_usb_candle/（CherryUSB vendor class + MCAN 桥接）
 * 协议层提供：结构体/常量、位时序换算、RX/TX 帧编解码、状态映射。
 *
 * 作者：tp-gs-fw（2026-11-02）
 */
#ifndef GS_USB_H
#define GS_USB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- USB 描述符事实 ---------------- */
#define GS_USB_VID                 (0x1209U)
#define GS_USB_PID                 (0x2323U)
#define GS_USB_EP_IN               (0x81U)
#define GS_USB_EP_OUT              (0x02U)
#define GS_USB_EP_MPS_HS           (512U)
#define GS_USB_EP_MPS_FS           (64U)
#define GS_USB_CHANNELS            (4U)
#define GS_USB_ICOUNT              (GS_USB_CHANNELS - 1U)

/* ---------------- bRequest（wIndex = 通道号；HOST_FORMAT/DEVICE_CONFIG 用 0） ---------------- */
enum {
    GS_USB_BREQ_HOST_FORMAT    = 0,
    GS_USB_BREQ_BITTIMING      = 1,
    GS_USB_BREQ_MODE           = 2,
    GS_USB_BREQ_BERR           = 3,
    GS_USB_BREQ_BT_CONST       = 4,
    GS_USB_BREQ_DEVICE_CONFIG  = 5,
    GS_USB_BREQ_TIMESTAMP      = 6,
    GS_USB_BREQ_IDENTIFY       = 7,
    GS_USB_BREQ_GET_USER_ID    = 8,
    GS_USB_BREQ_SET_USER_ID    = 9,
    GS_USB_BREQ_DATA_BITTIMING = 10,
    GS_USB_BREQ_BT_CONST_EXT   = 11,
    GS_USB_BREQ_SET_TERMINATION = 12,
    GS_USB_BREQ_GET_TERMINATION = 13,
    GS_USB_BREQ_GET_STATE      = 14,
};

/* HOST_FORMAT 请求里主机写入的字节序标记（小端 0x0000beef） */
#define GS_USB_BYTE_ORDER_BEEF     (0x0000BEEFU)

/* ---------------- MODE 请求 ---------------- */
#define GS_CAN_MODE_RESET             (0U)
#define GS_CAN_MODE_START             (1U)

#define GS_CAN_MODE_LISTEN_ONLY       (1U << 0)
#define GS_CAN_MODE_LOOP_BACK         (1U << 1)
#define GS_CAN_MODE_TRIPLE_SAMPLE     (1U << 2)   /* 不实现/不 advertise */
#define GS_CAN_MODE_ONE_SHOT          (1U << 3)
#define GS_CAN_MODE_HW_TIMESTAMP      (1U << 4)
#define GS_CAN_MODE_PAD_PKTS          (1U << 7)
#define GS_CAN_MODE_FD                (1U << 8)
#define GS_CAN_MODE_BERR_REPORTING    (1U << 12)

/* ---------------- BT_CONST.feature 位 ---------------- */
#define GS_CAN_FEATURE_LISTEN_ONLY    (1U << 0)
#define GS_CAN_FEATURE_LOOP_BACK      (1U << 1)
#define GS_CAN_FEATURE_TRIPLE_SAMPLE  (1U << 2)
#define GS_CAN_FEATURE_ONE_SHOT       (1U << 3)
#define GS_CAN_FEATURE_HW_TIMESTAMP   (1U << 4)
#define GS_CAN_FEATURE_IDENTIFY       (1U << 5)
#define GS_CAN_FEATURE_USER_ID        (1U << 6)
#define GS_CAN_FEATURE_PAD_PKTS       (1U << 7)
#define GS_CAN_FEATURE_FD             (1U << 8)
#define GS_CAN_FEATURE_BT_CONST_EXT   (1U << 10)
#define GS_CAN_FEATURE_TERMINATION    (1U << 11)  /* 不 advertise（手动拨码） */
#define GS_CAN_FEATURE_BERR_REPORTING (1U << 12)
#define GS_CAN_FEATURE_GET_STATE      (1U << 13)

/* 本轮 advertise 的 feature：LISTEN_ONLY|LOOP_BACK|ONE_SHOT|HW_TIMESTAMP|FD|BT_CONST_EXT|BERR|GET_STATE
 * = 1+2+8+16+256+1024+4096+8192 = 13595；不包含 TRIPLE_SAMPLE / TERMINATION / PAD_PKTS */
#define GS_CAN_FEATURE_DEFAULT        (13595U)

/* ---------------- 帧 flags（gs_host_frame_header.flags） ---------------- */
#define GS_CAN_FLAG_OVERFLOW          (1U << 0)
#define GS_CAN_FLAG_FD                (1U << 1)
#define GS_CAN_FLAG_BRS               (1U << 2)
#define GS_CAN_FLAG_ESI               (1U << 3)

/* ---------------- Linux CAN id 标志（与 SocketCAN 一致） ---------------- */
#define GS_CAN_EFF_FLAG               (0x80000000UL)
#define GS_CAN_RTR_FLAG               (0x40000000UL)
#define GS_CAN_ERR_FLAG               (0x20000000UL)

/* CAN 错误帧类别位（can_id 低 29 位，位定义取自 linux/can/error.h；内核 gs_usb
 * 驱动把带 CAN_ERR_FLAG 的经典帧直接当错误帧处理，见 gs_usb.c 的 ERROR frames 分支） */
#define GS_CAN_ERR_CRTL               (0x00000004UL)   /* 控制器状态：data[1] 放状态位 */
#define GS_CAN_ERR_PROT               (0x00000008UL)   /* 协议错误：data[2..3] */
#define GS_CAN_ERR_BUSOFF             (0x00000040UL)
#define GS_CAN_ERR_RESTARTED          (0x00000100UL)
#define GS_CAN_ERR_CNT                (0x00000200UL)   /* data[6]=TEC、data[7]=REC */

/* CAN_ERR_CRTL 的 data[1] 状态位 */
#define GS_CAN_ERR_CRTL_RX_OVERFLOW   (0x01U)
#define GS_CAN_ERR_CRTL_RX_WARNING    (0x04U)
#define GS_CAN_ERR_CRTL_TX_WARNING    (0x08U)
#define GS_CAN_ERR_CRTL_RX_PASSIVE    (0x10U)
#define GS_CAN_ERR_CRTL_TX_PASSIVE    (0x20U)
#define GS_CAN_SFF_MASK               (0x000007FFUL)
#define GS_CAN_EFF_MASK               (0x1FFFFFFFUL)

/* ---------------- 帧长度（内核按此校验，短帧会 rx_length_errors） ---------------- */
#define GS_USB_FRAME_HDR_LEN          (12U)   /* echo_id/can_id/dlc/channel/flags/reserved */
#define GS_USB_RX_CLASSIC_LEN         (GS_USB_FRAME_HDR_LEN + 8U + 4U)    /* 24 B，含 1 MHz 时间戳 */
#define GS_USB_RX_FD_LEN              (GS_USB_FRAME_HDR_LEN + 64U + 4U)   /* 80 B，含 1 MHz 时间戳 */
#define GS_USB_TX_CLASSIC_LEN         (GS_USB_FRAME_HDR_LEN + 8U)         /* 20 B */
#define GS_USB_TX_FD_LEN              (GS_USB_FRAME_HDR_LEN + 64U)        /* 76 B */
#define GS_USB_ECHO_ID_RX             (0xFFFFFFFFUL)
#define GS_USB_MAX_ECHO_ID            (10U)   /* 内核 GS_MAX_TX_URBS */

/* ---------------- DEVICE_CONFIG / GET_STATE 枚举 ---------------- */
#define GS_CAN_STATE_ERROR_ACTIVE     (0U)
#define GS_CAN_STATE_ERROR_WARNING    (1U)
#define GS_CAN_STATE_ERROR_PASSIVE    (2U)
#define GS_CAN_STATE_BUS_OFF          (3U)
#define GS_CAN_STATE_STOPPED          (4U)
#define GS_CAN_STATE_SLEEPING         (5U)

/* ---------------- 协议结构体（全部 packed，小端） ---------------- */
struct gs_host_config {
    uint32_t byte_order;                    /* 0x0000beef */
} __attribute__((packed));

struct gs_device_config {
    uint8_t reserved1;
    uint8_t reserved2;
    uint8_t reserved3;
    uint8_t icount;                         /* 通道数 - 1 */
    uint32_t sw_version;
    uint32_t hw_version;
} __attribute__((packed));

struct gs_device_bt_const {
    uint32_t feature;
    uint32_t fclk_can;
    uint32_t tseg1_min;
    uint32_t tseg1_max;
    uint32_t tseg2_min;
    uint32_t tseg2_max;
    uint32_t sjw_max;
    uint32_t brp_min;
    uint32_t brp_max;
    uint32_t brp_inc;
} __attribute__((packed));

struct gs_device_bt_const_ext {
    uint32_t feature;
    uint32_t fclk_can;
    uint32_t tseg1_min;
    uint32_t tseg1_max;
    uint32_t tseg2_min;
    uint32_t tseg2_max;
    uint32_t sjw_max;
    uint32_t brp_min;
    uint32_t brp_max;
    uint32_t brp_inc;
    uint32_t dtseg1_min;
    uint32_t dtseg1_max;
    uint32_t dtseg2_min;
    uint32_t dtseg2_max;
    uint32_t dsjw_max;
    uint32_t dbrp_min;
    uint32_t dbrp_max;
    uint32_t dbrp_inc;
} __attribute__((packed));

struct gs_device_bittiming {
    uint32_t prop_seg;
    uint32_t phase_seg1;
    uint32_t phase_seg2;
    uint32_t sjw;
    uint32_t brp;
} __attribute__((packed));

struct gs_device_mode {
    uint32_t mode;
    uint32_t flags;
} __attribute__((packed));

struct gs_device_state {
    uint32_t state;
    uint32_t rxerr;
    uint32_t txerr;
} __attribute__((packed));

struct gs_device_timestamp {
    uint32_t ts_low;
    uint32_t ts_high;
    uint32_t flags;
} __attribute__((packed));

/* 帧头（12 B，与内核 struct gs_host_frame header 一致） */
struct gs_host_frame_header {
    uint32_t echo_id;
    uint32_t can_id;
    uint8_t can_dlc;
    uint8_t channel;
    uint8_t flags;
    uint8_t reserved;
} __attribute__((packed));

/* 协议层中立的 CAN 帧描述（与 MCAN 无关，便于单测） */
struct gs_can_frame {
    uint32_t id;          /* 11/29 bit，右对齐 */
    uint8_t ext;          /* 1 = 扩展帧 */
    uint8_t rtr;          /* 1 = 远程帧 */
    uint8_t fd;           /* 1 = CAN FD */
    uint8_t brs;          /* 1 = BRS */
    uint8_t esi;          /* 1 = ESI */
    uint8_t err;          /* 1 = CAN 错误帧（can_id 带 GS_CAN_ERR_FLAG，data 按 linux/can/error.h） */
    uint8_t len;          /* 0..8 / 0..64 */
    uint8_t data[64];
};

/* ---------------- 工具函数 ---------------- */

/** 填 BT_CONST（标称段），fclk_can = 80 MHz */
void gs_usb_fill_bt_const(struct gs_device_bt_const *c);
/** 填 BT_CONST_EXT（标称段 + 数据段） */
void gs_usb_fill_bt_const_ext(struct gs_device_bt_const_ext *c);
/** 填 DEVICE_CONFIG（icount=3） */
void gs_usb_fill_device_config(struct gs_device_config *c);

/** 内核的 gs_device_bittiming → MCAN 驱动位时序字段（prescaler/seg1/seg2/sjw，均 1 基） */
void gs_usb_bt_to_mcan(const struct gs_device_bittiming *bt, uint32_t *prescaler,
                       uint32_t *num_seg1, uint32_t *num_seg2, uint32_t *num_sjw);

/** DLC（0..15）→ 数据字节数（FD 编码）；非法返回 0 */
uint8_t gs_usb_dlc_to_len(uint8_t dlc);
/** 数据字节数 → DLC（0..15）；>64 按 64 */
uint8_t gs_usb_len_to_dlc(uint8_t len);

/**
 * @brief 编码一帧 RX（设备→主机）到 out
 * @param echo_id GS_USB_ECHO_ID_RX 表示正常接收；否则为 TX 回显序号
 * @param ts_us   1 MHz 时间戳低 32 位
 * @return 写入字节数（24 或 80）
 */
uint32_t gs_usb_encode_frame(uint8_t *out, uint32_t echo_id, const struct gs_can_frame *f,
                             uint32_t ts_us);

/**
 * @brief 解析主机 TX 帧（20 或 76 B）
 * @return 0 成功；<0 非法（长度/通道/DLC）
 */
int gs_usb_decode_tx(const uint8_t *in, uint32_t len, uint32_t *echo_id,
                     struct gs_can_frame *f, uint8_t *channel, uint8_t *frame_flags);

/** MCAN 协议状态 → gs_usb GET_STATE 的 state 值 */
uint32_t gs_usb_map_state(bool bus_off, bool error_passive, bool error_warning, bool started);

/** 便利：取 frame flags（FD/BRS/ESI） */
uint8_t gs_usb_frame_flags(const struct gs_can_frame *f);

#ifdef __cplusplus
}
#endif

#endif /* GS_USB_H */
