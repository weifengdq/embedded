/*
 * app_doip.h - DoIP(ISO 13400-2) 服务端组件：TCP/UDP 13400 + 路由激活 + 诊断报文转发给 UDS
 *
 * 分工（与第 32 章图 32-1 一致）：
 *   本组件只管 DoIP 层：通用头、车辆声明/发现、路由激活、alive check、诊断报文 ACK/NACK、
 *   连接超时；UDS(ISO 14229-1) 服务由 components/app_uds 处理，二者用两个回调解耦：
 *     diag_rx  : DoIP 收到 0x8001 -> 交给 UDS（应用层自己调 app_uds_handle）
 *     send_diag: UDS 响应     -> app_doip_send_diag() 打成 0x8001 发出
 *
 * 最小用法（裸机，无 OS；主循环里轮询）：
 *     static app_doip_t g_doip;
 *     app_doip_cfg_t cfg = { .entity_addr = 0x1000u, ... };
 *     lwip_init(...);                          // 由 app_lwip 组件负责
 *     app_doip_init(&g_doip, &cfg);
 *     app_doip_start(&g_doip);                 // 监听 TCP/UDP 13400
 *     while (1) { app_lwip_poll(); app_doip_poll(&g_doip); }
 *
 * 约束：
 *  - 单线程（NO_SYS=1）：所有 API 只能在主循环里调用，lwIP 回调也跑在主循环上下文；
 *  - 回调里不要 printf/阻塞（lwIP 回调 = 收包路径）；
 *  - 收发缓冲全部静态分配：APP_DOIP_MAX_CONN x APP_DOIP_MAX_PAYLOAD（默认 3 x 4096 = 12 KB）。
 *
 * 作者：doip（task-3）
 */
#ifndef APP_DOIP_H
#define APP_DOIP_H

#include <stdint.h>
#include <stdbool.h>

#include "app_doip_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 静态缓冲上限（编译期）；运行时 cfg.max_payload / cfg.max_conn 只能 <= 这两个值 */
#define APP_DOIP_MAX_CONN       3u
#define APP_DOIP_MAX_PAYLOAD    4096u
#define APP_DOIP_MAX_DIAG_UDS   512u    /* 与 components/app_uds 的 APP_UDS_MAX_REQ 对齐 */

/* ISO 13400-2 表 A.1/A.2 的定时器典型值 */
#define APP_DOIP_INITIAL_INACTIVITY_MS  2000u      /* TCP 建连后等路由激活请求 */
#define APP_DOIP_GENERAL_INACTIVITY_MS  300000u    /* 路由激活后无任何报文则断开（5 min） */
#define APP_DOIP_ALIVE_CHECK_MS         500u       /* 发了 0x0007 后等 0x0008 */
#define APP_DOIP_ANNOUNCE_NUM           3u         /* 上电后连发 3 次车辆声明 */
#define APP_DOIP_ANNOUNCE_INTERVAL_MS   500u       /* 声明间隔 */
#define APP_DOIP_ANNOUNCE_WAIT_MS       500u       /* 上电到第一次声明前的随机等待上限 */

/* 连接状态（第 32 章图 32-3 状态机） */
typedef enum {
    APP_DOIP_CONN_CLOSED = 0,      /* 空槽位 */
    APP_DOIP_CONN_CONNECTED,       /* TCP 已建立，等路由激活（initial inactivity 计时中） */
    APP_DOIP_CONN_ACTIVATED,       /* 路由激活成功，可收发 0x8001（general inactivity 计时中） */
    APP_DOIP_CONN_ALIVE_CHECK      /* 已发 alive check 请求，等 0x0008（alive_check_ms 计时中） */
} app_doip_conn_state_t;

typedef struct app_doip app_doip_t;

/* 一条 TCP 连接 */
typedef struct {
    struct tcp_pcb *pcb;
    app_doip_t     *owner;
    app_doip_conn_state_t state;
    uint16_t        tester_addr;        /* 路由激活时定下来的 tester 逻辑地址 */
    uint8_t         activation_type;
    uint8_t         oem[4];
    char            peer[24];           /* 远端 IP 字符串（打印/日志用，ipaddr_ntoa_r 填） */
    uint16_t        peer_port;
    /* 分帧状态：先攒 8 B 头，再按头里的长度攒载荷 */
    uint8_t         rx_hdr[APP_DOIP_HDR_LEN];
    uint32_t        rx_hdr_got;
    uint16_t        rx_type;
    uint32_t        rx_want;
    uint32_t        rx_got;
    uint8_t         rx_buf[APP_DOIP_MAX_PAYLOAD];
    /* 定时器 */
    uint32_t        last_rx_ms;
    uint32_t        state_since_ms;
    uint32_t        alive_deadline_ms;
    bool            close_pending;      /* 处理完当前帧后关闭（如载荷超限） */
    /* 统计 */
    uint32_t        st_rx_frames;
    uint32_t        st_tx_frames;
    uint32_t        st_rx_diag;
    uint32_t        st_tx_diag;
    uint32_t        st_tx_ack;
    uint32_t        st_tx_nack;
    uint32_t        st_err_hdr;
    uint32_t        st_err_frame;
} app_doip_conn_t;

/* 诊断回调：收 0x8001 后调用（在 lwIP 收包回调里，也就是主循环上下文） */
typedef int (*app_doip_diag_fn)(void *ctx, uint16_t sa, uint16_t ta,
                                const uint8_t *uds, uint32_t len);
/* 单调微秒时基（给 P2/S3 与超时用）；为 NULL 时退回 app_lwip_now_ms()*1000 */
typedef uint64_t (*app_doip_now_fn)(void *ctx);

typedef struct {
    /* ---- 车辆信息 ---- */
    char     vin[18];              /* 17 B VIN + '\0'；不满 17 位补 0x00 上发 */
    uint8_t  eid[6];               /* 实体 ID，一般用 MAC */
    uint8_t  gid[6];               /* 组 ID：同组实体共用一个 GID */
    uint16_t entity_addr;          /* 实体逻辑地址，例 0x1000 */
    uint16_t tester_addr;          /* 允许的 tester SA，例 0x0E00；0 = 不校验 */
    uint8_t  further_action;       /* 0x00 无需进一步动作 / 0x10 需要路由激活 */
    uint8_t  version;              /* 上报的协议版本：0x02(2012) 或 0x03(2019) */
    uint8_t  sync_status;          /* 仅 2019：VIN/GID 同步状态 */
    uint8_t  power_mode;           /* 0x4004 回复的电源模式 */

    /* ---- 路由激活策略 ---- */
    uint8_t  activation_mask;      /* 允许的激活类型位掩码；0 = 默认允许 0x00/0x01/0xE0 */
    bool     require_oem_auth;     /* true = 必须带 4 B OEM 字段且等于 oem_key */
    uint8_t  oem_key[4];

    /* ---- 定时器 / 公告 ---- */
    uint32_t initial_inactivity_ms;
    uint32_t general_inactivity_ms;
    uint32_t alive_check_ms;
    uint32_t announce_num;
    uint32_t announce_interval_ms;
    uint32_t announce_wait_ms;
    uint32_t announce_period_ms;   /* >0 = 周期声明（OEM 扩展）；0 = 只有上电那几次 */

    /* ---- 容量 ---- */
    uint16_t max_conn;
    uint32_t max_payload;      /* DoIP 帧载荷上限（通用头长度字段能收多大） */
    uint32_t max_diag_uds;     /* 单条诊断报文里 UDS 部分的上限（交上层处理，默认 512） */

    /* ---- 回调 ---- */
    app_doip_diag_fn diag_rx;
    void            *diag_ctx;
    app_doip_now_fn  now_us;
    void            *now_ctx;
    bool             verbose;
} app_doip_cfg_t;

struct app_doip {
    app_doip_cfg_t cfg;
    bool           inited;
    bool           running;
    struct tcp_pcb *listen_pcb;
    struct udp_pcb *udp_pcb;
    app_doip_conn_t conn[APP_DOIP_MAX_CONN];
    /* 车辆声明调度 */
    bool     ann_active;
    uint32_t ann_left;
    uint32_t ann_next_ms;
    uint32_t ann_period_next_ms;
    /* 统计 */
    uint32_t st_udp_rx;
    uint32_t st_udp_ident_req;
    uint32_t st_announce_tx;
    uint32_t st_tcp_accept;
    uint32_t st_tcp_close;
    uint32_t st_route_ok;
    uint32_t st_route_deny;
    uint32_t st_alive_tx;
    uint32_t st_alive_ok;
    uint32_t st_alive_timeout;
    uint32_t st_initial_timeout;
    uint32_t st_general_timeout;
    uint32_t st_diag_rx;
    uint32_t st_diag_tx;
    uint32_t st_big_msg;           /* 单帧 >1500 B（跨以太网 MTU）计数 */
    uint32_t st_err_hdr;
};

int  app_doip_init(app_doip_t *d, const app_doip_cfg_t *cfg);
int  app_doip_start(app_doip_t *d);
int  app_doip_stop(app_doip_t *d);
void app_doip_poll(app_doip_t *d);

/* UDS 响应 -> 0x8001 诊断报文；ta = tester 逻辑地址 */
int  app_doip_send_diag(app_doip_t *d, uint16_t ta, const uint8_t *uds, uint32_t len);

/* 立即发 num 次车辆声明（num=0 用 cfg.announce_num） */
int  app_doip_send_announce(app_doip_t *d, uint32_t num);
/* 手动触发 alive check（给 shell 用），返回被检查的连接号，-1 = 没有可检查的 */
int  app_doip_alive_check(app_doip_t *d, int conn_idx);
/* 找一条已激活、且 tester 地址匹配的连接；-1 = 没有 */
int  app_doip_find_conn(const app_doip_t *d, uint16_t tester_sa);

void app_doip_print(const app_doip_t *d);            /* 全局状态 + 计数 */
void app_doip_print_conns(const app_doip_t *d);      /* 每条连接一行 */
int  app_doip_set_vin(app_doip_t *d, const char *vin);
int  app_doip_set_eid(app_doip_t *d, const uint8_t eid[6]);
const char *app_doip_conn_state_name(app_doip_conn_state_t s);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOIP_H */
