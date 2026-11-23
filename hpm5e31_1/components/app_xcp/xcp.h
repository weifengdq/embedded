/*
 * app_xcp —— ASAM XCP 1.x 从站协议层（MCU 侧最小实现，CAN / UDP 传输无关）
 *
 * 设计要点：
 *   1) 协议层与传输层分离：core 只处理 CTO/DTO 字节流，端口文件（xcp_port_can.c /
 *      xcp_port_udp.c）负责收发，通过 xcp_transport_ops_t 注册。
 *   2) 内存访问用绝对地址（与 A2L 里 MEASUREMENT/CHARACTERISTIC 的 ADDRESS 一致），
 *      从站不维护符号表；用"内存区域白名单"防止主机误写导致总线错误/死机。
 *   3) DAQ：动态列表（FREE_DAQ/ALLOC_DAQ/ALLOC_ODT/ALLOC_ODT_ENTRY/WRITE_DAQ），
 *      ODT 按 A2L 的条目顺序打包成一帧，PID = 绝对 ODT 号（IDF_ABS_ODT_NUMBER）。
 *      采样在事件中断里完成（时间戳取中断时刻），发送在主循环 xcp_poll() 里做。
 *
 * 协议层能力（CONNECT.RESOURCE / COMM_MODE_BASIC）：
 *   - CAL_PAG（单段单页 RAM 标定）+ DAQ（动态列表、预分频、时间戳）
 *   - 不实现 PGM（Flash 编程）、STIM、块传输、校验和、PACKED_MODE
 *
 * 用法（见 samples/app/xcp_can_daq/src/main.c）：
 *     xcp_set_transport(&xcp_can_ops);
 *     xcp_can_init(BOARD_CAN0_BASE, 0x555, 0x556);
 *     xcp_init(&cfg);                         // cfg 里给内存区域 / 事件通道 / 时基
 *     // 事件中断里：xcp_event(0, now_us);
 *     // 主循环里  ：xcp_poll();
 */
#ifndef APP_XCP_H
#define APP_XCP_H

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* 协议常量                                                            */
/* ------------------------------------------------------------------ */

/* DTO 包标识（PID） */
#define XCP_PID_RES          (0xFFu) /* 肯定响应 */
#define XCP_PID_ERR          (0xFEu) /* 错误响应 */
#define XCP_PID_EV           (0xFDu) /* 事件 */
#define XCP_PID_SERV         (0xFCu) /* 服务请求 */

/* 命令码（ASAM XCP 1.x 标准命令） */
#define XCP_CMD_CONNECT              (0xFFu)
#define XCP_CMD_DISCONNECT           (0xFEu)
#define XCP_CMD_GET_STATUS           (0xFDu)
#define XCP_CMD_SYNCH                (0xFCu)
#define XCP_CMD_GET_COMM_MODE_INFO   (0xFBu)
#define XCP_CMD_GET_ID               (0xFAu)
#define XCP_CMD_SET_REQUEST          (0xF9u)
#define XCP_CMD_GET_SEED             (0xF8u)
#define XCP_CMD_UNLOCK               (0xF7u)
#define XCP_CMD_SET_MTA              (0xF6u)
#define XCP_CMD_UPLOAD               (0xF5u)
#define XCP_CMD_SHORT_UPLOAD         (0xF4u)
#define XCP_CMD_BUILD_CHECKSUM       (0xF3u)
#define XCP_CMD_TRANSPORT_LAYER_CMD  (0xF2u)
#define XCP_CMD_USER_CMD             (0xF1u)
#define XCP_CMD_DOWNLOAD             (0xF0u)
#define XCP_CMD_DOWNLOAD_NEXT        (0xEFu)
#define XCP_CMD_DOWNLOAD_MAX         (0xEEu)
#define XCP_CMD_SHORT_DOWNLOAD       (0xEDu)
#define XCP_CMD_MODIFY_BITS          (0xECu)
#define XCP_CMD_SET_CAL_PAGE         (0xEBu)
#define XCP_CMD_GET_CAL_PAGE         (0xEAu)
#define XCP_CMD_GET_PAG_PROCESSOR_INFO (0xE9u)
#define XCP_CMD_GET_SEGMENT_INFO     (0xE8u)
#define XCP_CMD_GET_PAGE_INFO        (0xE7u)
#define XCP_CMD_SET_SEGMENT_MODE     (0xE6u)
#define XCP_CMD_GET_SEGMENT_MODE     (0xE5u)
#define XCP_CMD_COPY_CAL_PAGE        (0xE4u)
#define XCP_CMD_CLEAR_DAQ_LIST       (0xE3u)
#define XCP_CMD_SET_DAQ_PTR          (0xE2u)
#define XCP_CMD_WRITE_DAQ            (0xE1u)
#define XCP_CMD_SET_DAQ_LIST_MODE    (0xE0u)
#define XCP_CMD_GET_DAQ_LIST_MODE    (0xDFu)
#define XCP_CMD_START_STOP_DAQ_LIST  (0xDEu)
#define XCP_CMD_START_STOP_SYNCH     (0xDDu)
#define XCP_CMD_GET_DAQ_CLOCK        (0xDCu)
#define XCP_CMD_READ_DAQ             (0xDBu)
#define XCP_CMD_GET_DAQ_PROCESSOR_INFO (0xDAu)
#define XCP_CMD_GET_DAQ_RESOLUTION_INFO (0xD9u)
#define XCP_CMD_GET_DAQ_LIST_INFO    (0xD8u)
#define XCP_CMD_GET_DAQ_EVENT_INFO   (0xD7u)
#define XCP_CMD_FREE_DAQ             (0xD6u)
#define XCP_CMD_ALLOC_DAQ            (0xD5u)
#define XCP_CMD_ALLOC_ODT            (0xD4u)
#define XCP_CMD_ALLOC_ODT_ENTRY      (0xD3u)

/* 错误码（ERR packet 的 error code） */
#define XCP_ERR_CMD_OK                  (0x00u)
#define XCP_ERR_CMD_SYNTAX              (0x21u)
#define XCP_ERR_CMD_UNKNOWN             (0x20u)
#define XCP_ERR_OUT_OF_RANGE            (0x22u)
#define XCP_ERR_WRITE_PROTECTED         (0x23u)
#define XCP_ERR_ACCESS_DENIED           (0x24u)
#define XCP_ERR_ACCESS_LOCKED           (0x25u)
#define XCP_ERR_PAGE_NOT_VALID          (0x26u)
#define XCP_ERR_MODE_NOT_VALID          (0x27u)
#define XCP_ERR_SEGMENT_NOT_VALID       (0x28u)
#define XCP_ERR_SEQUENCE                (0x29u)
#define XCP_ERR_DAQ_CONFIG              (0x2Au)
#define XCP_ERR_MEMORY_OVERFLOW         (0x30u)
#define XCP_ERR_GENERIC                 (0x31u)
#define XCP_ERR_RESOURCE_TEMP_NA        (0x33u)

/* 资源掩码（CONNECT.RESOURCE / GET_STATUS.RESOURCE_PROTECTION_STATUS） */
#define XCP_RESOURCE_CAL_PAG            (0x01u)
#define XCP_RESOURCE_DAQ                (0x04u)
#define XCP_RESOURCE_STIM               (0x08u)
#define XCP_RESOURCE_PGM                (0x10u)

/* CommModeBasic：bit0 字节序，bit1..2 地址粒度，bit6 从站块传输，bit7 OPTIONAL */
#define XCP_CMB_BYTE_ORDER_INTEL        (0x00u)
#define XCP_CMB_ADDR_GRAN_BYTE          (0x00u)
#define XCP_CMB_OPTIONAL                (0x80u)

/* 会话状态（GET_STATUS.sessionStatus） */
#define XCP_SS_DAQ_RUNNING              (0x0040u)

/* DAQ 列表模式位（XCP 1.5：bit4 时间戳、bit5 PID_OFF、bit1 STIM） */
#define XCP_DAQ_MODE_STIM               (0x02u)
#define XCP_DAQ_MODE_TIMESTAMP          (0x10u)
#define XCP_DAQ_MODE_PID_OFF            (0x20u)

/* DAQ 列表状态 */
#define XCP_DAQ_STATE_SELECTED          (0x01u)
#define XCP_DAQ_STATE_RUNNING           (0x02u)
#define XCP_DAQ_STATE_OVERRUN           (0x04u)

/* DAQ_PROPERTIES（GET_DAQ_PROCESSOR_INFO） */
#define XCP_DAQ_PROP_DYNAMIC            (0x01u)
#define XCP_DAQ_PROP_PRESCALER          (0x02u)
#define XCP_DAQ_PROP_TIMESTAMP          (0x10u)

/* GET_DAQ_RESOLUTION_INFO.timestampMode：1 us 单位 + S4 可变长度 */
#define XCP_TS_UNIT_1US                 (3u)
#define XCP_TS_FIXED                    (0x08u)
#define XCP_TS_SIZE_S4                  (0x04u)
#define XCP_TS_TICKS                    (1u)

/* 时间单位编码（GET_DAQ_EVENT_INFO / A2L） */
#define XCP_TIMEUNIT_1US                (3u)
#define XCP_TIMEUNIT_1MS                (6u)

/* 资源上限（编译期可改；改大改小只需重编组件） */
#ifndef XCP_MAX_DAQ_LISTS
#define XCP_MAX_DAQ_LISTS               (4u)
#endif
#ifndef XCP_MAX_ODT_PER_LIST
#define XCP_MAX_ODT_PER_LIST            (4u)
#endif
#ifndef XCP_MAX_ENTRY_PER_ODT
#define XCP_MAX_ENTRY_PER_ODT           (8u)
#endif
#ifndef XCP_MAX_EVENTS
#define XCP_MAX_EVENTS                  (4u)
#endif
#ifndef XCP_DAQ_TX_QUEUE_LEN
#define XCP_DAQ_TX_QUEUE_LEN            (16u)
#endif
/* 传输层相关上限：CAN 默认 8 B；以太网构型在例程 CMake 里用 -DXCP_MAX_DTO=256 放大
 * （XCPonEth 一个 UDP 数据报可装多条消息，但单条消息仍受 MAX_DTO 约束）。 */
#ifndef XCP_MAX_CTO
#define XCP_MAX_CTO                     (8u)
#endif
#ifndef XCP_MAX_DTO
#define XCP_MAX_DTO                     (8u)
#endif

/* ------------------------------------------------------------------ */
/* 数据结构                                                            */
/* ------------------------------------------------------------------ */

/* 内存区域权限位 */
#define XCP_MEM_READ                    (0x01u)
#define XCP_MEM_WRITE                   (0x02u)

typedef struct {
    uint32_t start;                 /* 起始地址（含） */
    uint32_t end;                   /* 结束地址（不含） */
    uint8_t  flags;                 /* XCP_MEM_READ / XCP_MEM_WRITE */
    const char *name;               /* 仅用于调试打印 */
} xcp_mem_region_t;

typedef struct {
    const char *name;               /* 事件名（A2L EVENT 名，ASCII） */
    uint16_t cycle;                 /* 事件周期数值 */
    uint8_t  unit;                  /* 周期单位（XCP_TIMEUNIT_*），A2L 用 1ms 档 */
    uint8_t  priority;              /* 事件优先级 */
    uint8_t  max_daq_list;          /* 该事件允许的 DAQ 列表数 */
} xcp_event_def_t;

typedef struct {
    const xcp_mem_region_t  *regions;
    uint8_t                  region_count;
    const xcp_event_def_t   *events;
    uint8_t                  event_count;

    uint32_t (*now_us)(void);       /* DAQ 时间戳/GET_DAQ_CLOCK 用的单调时基（1 us 分辨率） */

    const char *id_string;          /* GET_ID 返回的 ASCII 串 */
    const uint8_t *seed;            /* 4 字节种子；seed_len = 0 表示资源不保护 */
    uint8_t     seed_len;

    void (*on_connect)(void);       /* 可空：主机连上时回调（应用可重算标定） */
    void (*on_disconnect)(void);    /* 可空 */
} xcp_cfg_t;

/* 传输层需要实现的三个动作（CAN / UDP 各一份） */
typedef struct {
    const char *name;
    void (*poll)(void);                                  /* 收 CTO，收到后调 xcp_rx_cto() */
    int  (*send_dto)(const uint8_t *buf, uint16_t len);  /* 0 = 已入队/已发出，<0 = 失败 */
} xcp_transport_ops_t;

typedef struct {
    uint32_t cto_rx;                /* 收到的 CTO 帧数 */
    uint32_t res_tx;                /* 发出的肯定响应数 */
    uint32_t err_tx;                /* 发出的错误响应数 */
    uint32_t cmd_ignored;           /* 未连接被忽略的命令数 */
    uint32_t daq_frames;            /* 已发出的 DAQ DTO 帧数 */
    uint32_t daq_samples;           /* 已采样的事件数（各列表累加） */
    uint32_t daq_dropped;           /* DAQ 发送队列满而丢弃的帧数 */
    uint32_t daq_overload;          /* 发送失败（CAN TX FIFO 满 / 网卡忙）次数 */
    uint32_t daq_events[XCP_MAX_EVENTS];
    uint32_t mem_denied;            /* 内存区域白名单拒绝次数 */
} xcp_stats_t;

typedef struct {
    uint8_t  in_use;
    uint8_t  state;
    uint8_t  mode;
    uint8_t  event;
    uint8_t  prescaler;
    uint8_t  prescaler_cnt;
    uint8_t  priority;
    uint8_t  first_pid;
    uint8_t  odt_count;
    uint8_t  odt_entry_count[XCP_MAX_ODT_PER_LIST];
} xcp_daq_list_info_t;

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/* 注册传输层（在 xcp_init 之前调用） */
void xcp_set_transport(const xcp_transport_ops_t *ops);

/* 协议层初始化；返回 0 成功 */
int  xcp_init(const xcp_cfg_t *cfg);

/* 主循环调用：先发 DAQ 队列，再收 CTO */
void xcp_poll(void);

/* 传输层收到一帧 CTO 后调用（buf[0] = 命令码） */
void xcp_rx_cto(const uint8_t *buf, uint16_t len);

/* 事件通道触发时调用（在中断或主循环里，ts_us = 本机单调时基低 32 位，1 us） */
void xcp_event(uint8_t evt, uint32_t ts_us);

/* 运行期改种子长度：len = 0 关闭资源保护（默认），>0 打开（演示 GET_SEED/UNLOCK） */
void xcp_set_seed(const uint8_t *seed, uint8_t len);

void xcp_get_stats(xcp_stats_t *st);
void xcp_reset_stats(void);
bool xcp_is_connected(void);
uint8_t xcp_last_error(void);
int  xcp_get_daq_list_info(uint8_t idx, xcp_daq_list_info_t *out);
uint8_t xcp_daq_list_count(void);

#endif /* APP_XCP_H */
