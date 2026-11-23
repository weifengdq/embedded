/*
 * app_j1939.h - SAE J1939 应用层协议栈（hpm5e31_1）
 *
 * 覆盖 SAE J1939-21（数据链路层/传输协议）、J1939-71（车辆应用层，周期 PGN）、
 * J1939-73（诊断，DM1/DM2/DM3/DM11）、J1939-81（网络管理，地址声明）。
 *
 * 设计：
 *   - 协议核心与硬件解耦：通过 j1939_bus_t（j1939_port.h 定义）收发 29 位扩展帧；
 *   - 每个 j1939_stack_t 是一个 J1939 节点（一个源地址 SA），可在同一 MCU 上实例化多个
 *     （例程里 ECU 用 CAN0、测试仪用 CAN1，构成 Bench A 双节点自测）；
 *   - 不做动态内存分配，所有会话/表都在栈结构体内。
 *
 * 线程模型：裸机，主循环调用 j1939_poll()（建议 ≥1 kHz，最差 500 Hz 也能容忍 10 ms 周期）。
 */
#ifndef APP_J1939_H
#define APP_J1939_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================= 29 位 CAN ID / PGN（J1939-21 §4） ======================= */

#define J1939_ID_MASK           (0x1FFFFFFFUL)
#define J1939_TP_MAX_LEN        (1785U) /* 255 个 TP.DT × 7 字节 */
#define J1939_TP_TX_SESS        (2U)
#define J1939_TP_RX_SESS        (2U)
#define J1939_SCHED_MAX         (8U)
#define J1939_DTC_MAX           (16U)

/* ID 位域：Prio(3) EDP(1) DP(1) PF(8) PS(8) SA(8) */
#define J1939_ID_PRIO(id)       (((id) >> 26) & 0x07U)
#define J1939_ID_EDP(id)        (((id) >> 25) & 0x01U)
#define J1939_ID_DP(id)         (((id) >> 24) & 0x01U)
#define J1939_ID_PF(id)         (((id) >> 16) & 0xFFU)
#define J1939_ID_PS(id)         (((id) >> 8) & 0xFFU)
#define J1939_ID_SA(id)         ((id) & 0xFFU)

/* 常用 PGN（PDU1：PF < 240，PS 是目的地址；PDU2：PF >= 240，PS 是组扩展） */
#define J1939_PGN_ACK               (0xE800U) /* ACK / NACK */
#define J1939_PGN_REQUEST           (0xEA00U) /* 请求 PGN */
#define J1939_PGN_TP_DT             (0xEB00U) /* 传输协议数据传送 */
#define J1939_PGN_TP_CM             (0xEC00U) /* 传输协议连接管理（BAM/RTS/CTS/EOMA/Abort） */
#define J1939_PGN_ADDRESS_CLAIMED   (0xEE00U) /* 地址声明 */
#define J1939_PGN_EEC1              (0xF004U) /* 电子发动机控制 1，10 ms */
#define J1939_PGN_CCVS1             (0xFEF1U) /* 巡航/车速，100 ms */
#define J1939_PGN_ET1               (0xFEEEU) /* 发动机温度 1，1 s */
#define J1939_PGN_LFE               (0xFEEFU) /* 燃油经济性，可选 */
#define J1939_PGN_DM1               (0xFECAU) /* 激活诊断码，1 s */
#define J1939_PGN_DM2               (0xFECBU) /* 历史诊断码，按请求 */
#define J1939_PGN_DM3               (0xFECCU) /* 清除历史诊断码 */
#define J1939_PGN_DM11              (0xFED3U) /* 清除激活诊断码 */

/* TP.CM 控制字节（J1939-21 §5.10） */
#define J1939_TP_CM_RTS             (0x10U)
#define J1939_TP_CM_CTS             (0x11U)
#define J1939_TP_CM_EOMA            (0x13U)
#define J1939_TP_CM_BAM             (0x20U)
#define J1939_TP_CM_ABORT           (0xFFU)

/* TP 超时（J1939-21 §5.10.4，单位 ms） */
#define J1939_T1_MS                 (750U)   /* 包间/CTS 后等 DT */
#define J1939_T2_MS                 (1250U)  /* 等 CTS */
#define J1939_T3_MS                 (1250U)  /* 等下一 DT */
#define J1939_T4_MS                 (1050U)  /* EOMA 后等下一 CTS */
#define J1939_BAM_GAP_MS            (50U)    /* BAM 每包最小间隔 */

/* ======================= NAME（J1939-81，64 位，8 字节小端） ======================= */

typedef struct {
    uint32_t identity;              /* 21 bit：Identity Number */
    uint16_t manufacturer;          /* 11 bit：Manufacturer Code */
    uint8_t  ecu_instance;          /* 3 bit */
    uint8_t  function_instance;     /* 5 bit */
    uint8_t  function;              /* 8 bit */
    uint8_t  industry_group;        /* 3 bit */
    uint8_t  vehicle_system;        /* 7 bit */
    uint8_t  vehicle_system_instance; /* 4 bit */
    bool     reserved;              /* 1 bit */
    bool     arbitrary_address_capable; /* 1 bit：支持动态换地址 */
} j1939_name_t;

uint64_t j1939_name_encode(const j1939_name_t *n);
void     j1939_name_decode(uint64_t v, j1939_name_t *n);
void     j1939_name_to_bytes(uint64_t v, uint8_t out[8]);

/* ======================= 帧 / ID 编解码 ======================= */

typedef struct {
    uint32_t id;        /* 29 位扩展 ID */
    uint8_t  len;       /* 0..8（J1939 只用经典帧，单帧 ≤8 字节） */
    uint8_t  data[8];
    uint32_t ts_us;     /* 接收时间戳（端口层填） */
} j1939_frame_t;

uint32_t j1939_pgn_from_id(uint32_t id);
uint32_t j1939_id_from_pgn(uint32_t pgn, uint8_t priority, uint8_t da, uint8_t sa);
bool     j1939_is_pdu1(uint32_t pgn);
uint8_t  j1939_default_priority(uint32_t pgn);

/* ======================= 端口层（j1939_port.h 提供实现） ======================= */

typedef struct j1939_bus j1939_bus_t;

int  j1939_bus_send(j1939_bus_t *bus, const j1939_frame_t *f);
int  j1939_bus_recv(j1939_bus_t *bus, j1939_frame_t *f); /* 1 = 收到一帧 */
uint32_t j1939_millis(void);
uint32_t j1939_micros(void);

/* ======================= 节点（协议栈实例） ======================= */

typedef enum {
    J1939_ADDR_UNCLAIMED = 0,   /* 未声明（上电初始态） */
    J1939_ADDR_WAIT,            /* 已发请求，等 250 ms 看有无冲突 */
    J1939_ADDR_CLAIMED,         /* 声明成功 */
    J1939_ADDR_CANNOT_CLAIM,    /* 无法声明（SA=0xFE） */
} j1939_addr_state_t;

typedef struct {
    uint32_t spn;   /* Suspect Parameter Number，0..524287 */
    uint8_t  fmi;   /* Failure Mode Identifier，0..31 */
    uint8_t  oc;    /* Occurrence Count，0..126 */
    uint8_t  lamp;  /* 该故障对应的最高灯：0=off 1=MIL 2=RSL 3=AWL 4=PL */
} j1939_dtc_t;

typedef struct j1939_stack j1939_stack_t;
typedef void (*j1939_rx_cb_t)(j1939_stack_t *s, uint32_t pgn, uint8_t sa, uint8_t da,
                              const uint8_t *data, uint16_t len, void *user);
/* 周期 PGN 填充回调：把当前信号值写进 buf，返回长度（≤8 单帧，>8 由 TP 发送） */
typedef void (*j1939_fill_cb_t)(j1939_stack_t *s, uint8_t *buf, uint16_t *len, void *user);

typedef struct {
    uint32_t pgn;
    uint8_t  priority;      /* 0 = 按 PGN 默认优先级 */
    uint16_t period_ms;
    j1939_fill_cb_t fill;
    void *user;
    bool enabled;
    /* 内部 */
    uint32_t next_ms;
    uint32_t sent;
} j1939_sched_t;

typedef struct {
    bool active;
    bool bam;
    uint32_t pgn;
    uint8_t  da;
    uint16_t total_len;
    uint16_t npackets;
    uint8_t  next_seq;
    uint8_t  window;
    bool     waiting_cts;   /* RTS 已发/窗口用完，等 CTS 或 EOMA */
    uint32_t next_ms;
    uint32_t deadline_ms;
    uint32_t started_us;
    uint16_t sent_len;
    uint8_t  buf[J1939_TP_MAX_LEN];
} j1939_tp_tx_t;

typedef struct {
    bool active;
    bool bam;
    uint32_t pgn;
    uint8_t  src;
    uint16_t total_len;
    uint16_t npackets;
    uint8_t  next_seq;
    uint8_t  cts_left;      /* 本窗口还剩多少包可收，0 时需要再发 CTS */
    uint16_t recv_len;
    uint32_t last_us;
    uint32_t deadline_ms;
    uint32_t gap_min_us;
    uint32_t gap_max_us;
    uint32_t gap_sum_us;
    uint32_t gap_n;
    uint8_t  buf[J1939_TP_MAX_LEN];
} j1939_tp_rx_t;

typedef struct {
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t tx_errors;
    uint32_t addr_claims_rx;
    uint32_t addr_claims_tx;
    uint32_t req_rx;
    uint32_t req_answered;
    uint32_t req_rsp_min_us;
    uint32_t req_rsp_max_us;
    uint32_t req_rsp_sum_us;
    uint32_t req_rsp_n;
    uint32_t tp_bam_rx;
    uint32_t tp_bam_tx;
    uint32_t tp_rts_rx;
    uint32_t tp_rts_tx;
    uint32_t tp_bytes_rx;
    uint32_t tp_bytes_tx;
    uint32_t tp_gap_min_us;
    uint32_t tp_gap_max_us;
    uint32_t tp_gap_sum_us;
    uint32_t tp_gap_n;
    uint32_t tp_done_rx;
    uint32_t tp_done_tx;
    uint32_t tp_errors;
    uint32_t tp_timeouts;
    uint32_t tp_aborts;
    uint32_t eoma_rtt_min_us;
    uint32_t eoma_rtt_max_us;
    uint32_t eoma_rtt_sum_us;
    uint32_t eoma_rtt_n;
    uint32_t dm_clears;
} j1939_stats_t;

typedef struct {
    bool auto_claim;            /* true：上电做地址声明状态机 */
    uint8_t preferred_sa;       /* 期望源地址，0..253（254=null, 255=broadcast） */
    uint64_t name;              /* 64 位 NAME */
    uint16_t bam_gap_ms;        /* BAM 包间隔，0 = 默认 50 ms */
    uint16_t cts_gap_ms;        /* RTS/CTS 包间隔，0 = 默认 1 ms（见第 32 章说明） */
} j1939_cfg_t;

struct j1939_stack {
    j1939_bus_t *bus;
    j1939_cfg_t cfg;
    volatile uint8_t sa;
    j1939_addr_state_t addr_state;
    uint32_t addr_timer_ms;
    uint8_t  next_addr_candidate;
    uint8_t  addr_conflict;   /* 等待期内收到同名更低 NAME 的声明 */
    j1939_rx_cb_t rx_cb;
    void *rx_user;
    j1939_sched_t sched[J1939_SCHED_MAX];
    uint8_t sched_count;
    j1939_tp_tx_t tp_tx[J1939_TP_TX_SESS];
    j1939_tp_rx_t tp_rx[J1939_TP_RX_SESS];
    j1939_dtc_t dtc[J1939_DTC_MAX];
    uint8_t dtc_count;
    j1939_dtc_t dtc_hist[J1939_DTC_MAX];
    uint8_t dtc_hist_count;
    uint8_t scratch[J1939_TP_MAX_LEN];
    j1939_stats_t stats;
};

/* ======================= 公共 API ======================= */

void j1939_init(j1939_stack_t *s, j1939_bus_t *bus, const j1939_cfg_t *cfg);
void j1939_poll(j1939_stack_t *s);

/* 发一条应用层消息：len<=8 单帧；>8 自动选 BAM（da=0xFF）或 RTS/CTS。prio=0 用默认优先级 */
int  j1939_send(j1939_stack_t *s, uint32_t pgn, uint8_t da, uint8_t prio,
                const void *data, uint16_t len);
/* 请求一个 PGN（PGN 0xEA00） */
int  j1939_request(j1939_stack_t *s, uint32_t pgn, uint8_t da);
/* 应答（PGN 0xE800）：ack_code 0=ACK 1=NAK 2=access denied 3=busy */
int  j1939_send_ack(j1939_stack_t *s, uint32_t pgn, uint8_t da, uint8_t ack_code);

void j1939_set_rx_cb(j1939_stack_t *s, j1939_rx_cb_t cb, void *user);
int  j1939_sched_add(j1939_stack_t *s, uint32_t pgn, uint16_t period_ms, uint8_t priority,
                     j1939_fill_cb_t fill, void *user);
bool j1939_sched_set_enabled(j1939_stack_t *s, uint32_t pgn, bool enabled);
bool j1939_sched_set_period(j1939_stack_t *s, uint32_t pgn, uint16_t period_ms);

uint8_t j1939_sa(const j1939_stack_t *s);
bool    j1939_claimed(const j1939_stack_t *s);

/* ---------------- 诊断（J1939-73） ---------------- */
void     j1939_dm_init(j1939_stack_t *s);       /* 注册 DM1 周期（1 s） */
int      j1939_dm_add_fault(j1939_stack_t *s, uint32_t spn, uint8_t fmi, uint8_t oc);
void     j1939_dm_clear_active(j1939_stack_t *s);
void     j1939_dm_clear_history(j1939_stack_t *s);
uint16_t j1939_dm_build(j1939_stack_t *s, uint32_t pgn, uint8_t *buf); /* DM1/DM2 编码，返回长度 */
uint8_t  j1939_dm_lamp(const j1939_stack_t *s);  /* 0=off 1=MIL 2=RSL 3=AWL 4=PL */

/* 灯状态字节位域（DM1/DM2 第 1 字节） */
#define J1939_DM_LAMP_OFF    (0U)
#define J1939_DM_LAMP_ON     (1U)
#define J1939_DM_LAMP_ERROR  (2U)
#define J1939_DM_LAMP_NA     (3U)
uint8_t j1939_dm_lamp_byte(uint8_t mil, uint8_t rsl, uint8_t awl, uint8_t pl);

#ifdef __cplusplus
}
#endif
#endif /* APP_J1939_H */