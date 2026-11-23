/*
 * app_isotp_port.h - ISO-TP 的 CAN 端口（SDK MCAN 直驱，Bench A / 独立运行）
 *
 * 为什么组件里自带一份端口实现：
 *   团队约定 §4.1 的 app_can 由 tp-infra 提供；在它落地前先用一份"直驱 SDK MCAN"的
 *   端口把 ISO-TP/UDS 跑通（语义与 app_can 对齐：经典 500k / FD 1M-2M / 64B / 29 位）。
 *   app_can 落地后只需另写一个 60 行左右的适配文件（把 tx_frame/recv/now_us 三个函数
 *   转调 app_can_*），ISO-TP/UDS 代码零改动。
 *
 * 注意：每路端口需要一个 MCAN Message RAM 缓冲（HPM5E00 的 Message RAM 在 AHB SRAM），
 *      默认最多 2 路（CAN0/CAN1），可通过 APP_ISOTP_PORT_MAX 调整。
 */
#ifndef APP_ISOTP_PORT_H
#define APP_ISOTP_PORT_H

#include "app_isotp.h"
#include "hpm_mcan_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APP_ISOTP_PORT_MAX
#define APP_ISOTP_PORT_MAX 2u
#endif

typedef struct {
    MCAN_Type *base;            /* HPM_MCAN0 / HPM_MCAN1 */
    bool       fd;              /* true: CAN FD（仲裁 1M / 数据 2M） */
    bool       brs;
    uint32_t   nominal_bitrate; /* 经典: 500000；FD: 1000000 */
    uint32_t   data_bitrate;    /* FD: 2000000 */
} app_isotp_port_cfg_t;

typedef struct {
    MCAN_Type *base;
    bool       fd;
    bool       brs;
    uint32_t   nominal_bitrate;
    uint32_t   data_bitrate;
    uint32_t   clk_hz;
    uint32_t   tx_cnt;
    uint32_t   rx_cnt;
    uint32_t   tx_err;
    uint32_t   rx_drop;
} app_isotp_port_t;

/* 初始化一路 CAN 端口（含引脚/收发器/时钟，内部调用 board_init_can*） */
int  app_isotp_port_init(app_isotp_port_t *p, const app_isotp_port_cfg_t *cfg);
/* 发一帧（阻塞到写入 TX FIFO；返回 0 成功） */
int  app_isotp_port_send(app_isotp_port_t *p, const app_isotp_frame_t *f);
/* 非阻塞收一帧：1=收到，0=空 */
int  app_isotp_port_recv(app_isotp_port_t *p, app_isotp_frame_t *f);
/* 单调微秒时基（MCHTMR 24 MHz） */
uint64_t app_isotp_port_now_us(void);

/* 生成可直接给 app_isotp_init() 用的 link */
app_isotp_link_t app_isotp_port_link(app_isotp_port_t *p);

/* 把该端口收到的帧分发到 n 个 ISO-TP 实例（返回本轮到投递的帧数） */
int app_isotp_port_pump(app_isotp_port_t *p, app_isotp_t *const *tps, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* APP_ISOTP_PORT_H */
