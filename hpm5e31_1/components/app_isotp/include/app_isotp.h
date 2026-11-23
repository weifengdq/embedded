/*
 * app_isotp.h - ISO 15765-2:2016 (ISO-TP) 传输层，与 CAN 驱动解耦
 *
 * 设计要点
 *   1) 全非阻塞状态机：app_isotp_send() 只"启动"传输，真正的 CF 发送、FC 发送、
 *      超时判定都由主循环里周期调用的 app_isotp_poll() 推进，收到的 CAN 帧由
 *      app_isotp_on_frame() 投喂。这样同一颗 MCU 上可以同时跑 DUT 与测试节点，
 *      互不嵌套、无重入死锁（见第 30 章 30-5 节）。
 *   2) 链路抽象 app_isotp_link_t 只有两个回调（发帧 / 单调时基），因此同一份
 *      ISO-TP 代码可以跑在 SDK MCAN 直驱、tp-infra 的 app_can、甚至 SocketCAN 上。
 *   3) 覆盖 ISO 15765-2:2016 全部帧类型与参数：SF/FF/CF/FC、BS/STmin、
 *      N_As/N_Ar/N_Bs/N_Cr、padding、29 位 ID、CAN FD（DLC>8 / SF escape /
 *      FC.CT 64B）、1 字节地址扩展 (AE)、多通道并发。
 *
 * 用法（单通道）
 *   static app_isotp_t s_tp;
 *   static const app_isotp_link_t link = { &port, port_tx, port_now };
 *   app_isotp_init(&s_tp, &cfg, &link, tx_buf, sizeof(tx_buf), rx_buf, sizeof(rx_buf));
 *   while (1) { app_isotp_port_pump(&port, &s_tp, NULL); ... app_isotp_poll(&s_tp); }
 */
#ifndef APP_ISOTP_H
#define APP_ISOTP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ISO-TP 经典寻址单条消息上限 4095 字节（FF_DL 12 bit）；扩展长度上限本实现按 8192 截断 */
#define APP_ISOTP_MAX_DL       4095u
#define APP_ISOTP_MAX_DL_EXT   8192u

/* 返回值 */
typedef enum {
    APP_ISOTP_OK          = 0,
    APP_ISOTP_ERR_ARG     = -1,   /* 参数非法 */
    APP_ISOTP_ERR_BUSY    = -2,   /* 上一次传输还没结束 */
    APP_ISOTP_ERR_TOOLONG = -3,   /* 超过缓冲区/协议上限 */
    APP_ISOTP_ERR_LINK    = -4,   /* 底层发送失败 */
    APP_ISOTP_ERR_OVFLW   = -5,   /* 收到 FC.OVFLW 或本地缓冲不足 */
    APP_ISOTP_ERR_SEQ     = -6,   /* 序号错 */
    APP_ISOTP_ERR_TIMEOUT = -7,   /* N_Bs / N_Cr 超时 */
    APP_ISOTP_ERR_ABORT   = -8    /* 被本端/对端中止 */
} app_isotp_rc_t;

/* 一帧 CAN(FD) 数据。id 是否 29 位由 ext 决定 */
typedef struct {
    uint32_t id;
    uint8_t  len;
    bool     ext;
    bool     fd;
    bool     brs;
    uint8_t  data[64];
} app_isotp_frame_t;

/* 链路抽象：发一帧 + 单调微秒时基 */
typedef struct {
    void *ctx;
    int      (*tx_frame)(void *ctx, const app_isotp_frame_t *f);
    uint64_t (*now_us)(void *ctx);
} app_isotp_link_t;

/* 完整消息接收回调：在 on_frame() 上下文里同步调用，不要做长耗时操作 */
typedef void (*app_isotp_rx_cb_t)(void *user, const uint8_t *data, uint32_t len);

typedef struct {
    uint32_t rx_id;          /* 接收（请求）CAN ID：C0 用 0x7E0，C1 用 0x708 */
    uint32_t tx_id;          /* 发送（响应）CAN ID：C0 用 0x7E8 */
    bool     rx_ext;         /* rx_id 为 29 位 */
    bool     tx_ext;         /* tx_id 为 29 位 */
    bool     fd;             /* 使用 CAN FD 帧（DLC > 8，允许 SF/FF 大于 8 字节） */
    bool     brs;            /* FD 数据段变速 */
    bool     ext_addr_en;    /* ISO-TP 1 字节地址扩展 (N_TAtype = AE) */
    uint8_t  rx_ext_addr;    /* 本端地址，接收时过滤 */
    uint8_t  tx_ext_addr;    /* 对端地址，发送时前置 */
    bool     tx_pad;         /* 发送帧做 padding */
    uint8_t  pad_byte;       /* padding 字节：0xCC / 0xAA / 0x00 */
    uint16_t bs;             /* 本端作为接收方通告的 BlockSize，0 = 不分块 */
    uint8_t  stmin;          /* 本端作为接收方通告的 STmin（0x00~0x7F ms，0xF1~0xF9 100us） */
    uint32_t n_as_ms;        /* 发送帧确认超时，默认 1000 ms */
    uint32_t n_ar_ms;        /* 接收帧确认超时，默认 1000 ms */
    uint32_t n_bs_ms;        /* 等待 FC 超时 (sender)，默认 1000 ms */
    uint32_t n_cr_ms;        /* 等待 CF 超时 (receiver)，默认 1000 ms */
    uint8_t  wftmax;         /* 允许的连续 FC.WAIT 数，0 = 不限制，默认 10 */
    app_isotp_rx_cb_t on_msg;
    void *user;
} app_isotp_cfg_t;

typedef struct {
    uint32_t tx_msg;         /* 成功发出的完整消息数 */
    uint32_t rx_msg;         /* 成功收到的完整消息数 */
    uint32_t tx_frames;      /* SF/FF/CF 帧数 */
    uint32_t rx_frames;
    uint32_t fc_tx;          /* 发出的 FC 数 */
    uint32_t fc_rx;
    uint32_t fc_wait_rx;     /* 收到的 FC.WAIT 数 */
    uint32_t err_seq;        /* 序号错 */
    uint32_t err_ovflw;      /* 溢出 / FC.OVFLW */
    uint32_t err_timeout;    /* N_Bs / N_Cr 超时 */
    uint32_t err_link;
    uint32_t last_tx_us;     /* 最近一次完整消息的发送耗时（首帧到末帧） */
    uint32_t last_rx_us;     /* 最近一次完整消息的接收耗时（首帧到末帧） */
    uint32_t max_tx_us;
    uint32_t max_rx_us;
    uint32_t max_tx_len;
    uint32_t max_rx_len;
    /* 最近一条消息的 CF 发送间隔统计（发送侧，含阻塞发送耗时与软件 pump 节拍） */
    uint32_t cf_cnt;         /* 参与统计的 CF 间隔个数 */
    uint32_t cf_gap_sum_us;  /* 间隔累计（us），均值 = sum/cnt */
    uint32_t cf_gap_min_us;
    uint32_t cf_gap_max_us;
    uint32_t ff_ext_cnt;     /* 已发出的 32 位长度转义 FF 数（>4095 B 路径） */
} app_isotp_stats_t;

/* 传输状态（同时是内部实现字段，公开只为方便静态分配） */
typedef struct app_isotp {
    app_isotp_cfg_t  cfg;
    app_isotp_link_t link;
    uint8_t *tx_buf;
    uint32_t tx_cap;
    uint8_t *rx_buf;
    uint32_t rx_cap;
    /* --- 发送侧 --- */
    uint8_t  tx_state;       /* 0=idle 1=等 FC 2=发 CF */
    uint32_t tx_len;
    uint32_t tx_off;         /* 已发送的载荷字节数 */
    uint8_t  tx_sn;          /* 下一个 CF 序号 1..15 */
    uint16_t tx_bs_left;     /* 本块内还可发的 CF 数，0xFFFF=不限 */
    uint32_t tx_stmin_us;    /* 对端（接收方）要求的 STmin */
    uint64_t tx_next_us;     /* 下一帧最早发送时刻 */
    uint64_t tx_tmr_us;      /* N_Bs 计时起点 */
    uint64_t tx_start_us;    /* 统计用 */
    uint8_t  tx_wft;         /* 已收到的连续 WAIT 数 */
    uint64_t cf_last_us;     /* 上一个 CF 的发送起始时刻，用于统计间隔 */
    /* --- 接收侧 --- */
    uint8_t  rx_state;       /* 0=idle 1=等 CF */
    uint32_t rx_len;
    uint32_t rx_got;
    uint8_t  rx_sn;
    uint16_t rx_bs_cnt;
    uint64_t rx_tmr_us;
    uint64_t rx_start_us;
    /* --- 统计 --- */
    app_isotp_stats_t st;
} app_isotp_t;

/* 初始化：tx_buf/rx_buf 由调用者提供（避免组件里放 4 KB 静态数组） */
int  app_isotp_init(app_isotp_t *t, const app_isotp_cfg_t *cfg, const app_isotp_link_t *link,
                    uint8_t *tx_buf, uint32_t tx_cap, uint8_t *rx_buf, uint32_t rx_cap);

/* 启动一次发送（非阻塞）。返回 APP_ISOTP_OK 表示已发出 SF 或 FF。 */
int  app_isotp_send(app_isotp_t *t, const uint8_t *data, uint32_t len);

/* 投喂一帧收到的 CAN 帧；返回 1 表示本实例消费了该帧（ID 匹配） */
int  app_isotp_on_frame(app_isotp_t *t, const app_isotp_frame_t *f);

/* 主循环周期调用：推进 CF 发送节奏、发送延迟的 FC、判定 N_Bs/N_Cr 超时 */
void app_isotp_poll(app_isotp_t *t);

/* 主动中止本端发送（对端会因 N_Cr 超时复位） */
void app_isotp_abort(app_isotp_t *t);

static inline bool app_isotp_tx_idle(const app_isotp_t *t) { return t->tx_state == 0u; }
static inline bool app_isotp_rx_idle(const app_isotp_t *t) { return t->rx_state == 0u; }
static inline const app_isotp_stats_t *app_isotp_stats(const app_isotp_t *t) { return &t->st; }

/* STmin 编码 -> 微秒（0x00~0x7F: ms；0xF1~0xF9: 100~900 us；保留值按 127 ms 处理） */
uint32_t app_isotp_stmin_to_us(uint8_t stmin);
/* 微秒 -> STmin 编码（取能表示的最小值，>=127ms 时返回 0x7F） */
uint8_t  app_isotp_us_to_stmin(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* APP_ISOTP_H */
