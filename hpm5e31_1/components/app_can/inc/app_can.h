/*
 * app_can.h - CAN FD 节点组件（hpm5e31_1，4x MCAN + TCAN1044AV）
 *
 * 设计目标：把 mcan_init/位时序/收发/统计/过滤器包装成"3 行起 CAN"，
 * 供 ISO-TP/UDS/J1939/CANopen/XCP/网关等上层直接复用，不再各自摸寄存器。
 *
 * 典型用法（经典 500k，CAN0）:
 *     static app_can_t can0;
 *     app_can_cfg_t cfg = APP_CAN_CFG_DEFAULT;
 *     cfg.speed = APP_CAN_SPEED_CLASSIC_500K;
 *     app_can_init(&can0, BOARD_CAN0_BASE, &cfg);
 *     app_can_frame_t f = { .id = 0x123, .len = 8 };
 *     app_can_send(&can0, &f, 1000);
 *     if (app_can_recv(&can0, &f, 10000) == 0) { ... }
 *
 * 注意：
 *  - app_can_init() 内部完成 board_init_can()/board_init_can_clock()/收发器使能，
 *    上层不要重复调用，否则 mcan_deinit 访问被门控的寄存器会挂死。
 *  - 首次调用会把 4 颗 TCAN1044AV 的 STB 拉低（板级 PE03 共用）。
 *  - 组件不含中断处理，全部轮询；单实例 RX 取样时间 = app_can_recv() 返回时刻。
 *
 * 头文件路径：components/app_can/inc/app_can.h
 * 作者：tp-infra（第二阶段共享底座）
 */
#ifndef APP_CAN_H
#define APP_CAN_H

#include <stdint.h>
#include <stdbool.h>
#include "hpm_common.h"
#include "hpm_mcan_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CAN FD 单帧最大数据长度（ISO 11898-1:2015） */
#define APP_CAN_MAX_DATA_LEN (64U)
/* 本板 MCAN 实例数 */
#define APP_CAN_INST_COUNT   (4U)

/* 预置位时序档位。
 * 经典 500 kbit/s：80 MHz / (1 * (1+139+20)) = 500 kbit/s，采样点 87.5%（与第 8/09 章实测一致）。
 * CAN FD：仲裁 1 Mbit/s（75%）、数据 2 Mbit/s（75%）、TDC 开（SSP=15, 滤波窗 15），DLC<=64。 */
typedef enum app_can_speed_enum {
    APP_CAN_SPEED_CLASSIC_500K = 0, /*!< 经典 CAN，500 kbit/s，SP 87.5% */
    APP_CAN_SPEED_FD_1M_2M     = 1, /*!< CAN FD，仲裁 1 Mbit/s / 数据 2 Mbit/s，TDC 开 */
} app_can_speed_t;

/* 一帧 CAN/CAN FD 报文。字段命名与 docs/应用层开发约定.md §4.1 一致。 */
typedef struct app_can_frame_struct {
    uint32_t id;                        /*!< 11 位标准 ID 或 29 位扩展 ID（右对齐） */
    uint8_t  ext;                       /*!< 1 = 29 位扩展帧 */
    uint8_t  rtr;                       /*!< 1 = 远程帧（仅经典帧有效） */
    uint8_t  fd;                        /*!< 1 = CAN FD 帧 */
    uint8_t  brs;                       /*!< 1 = 数据段切换高速率（仅 FD 有效） */
    uint8_t  len;                       /*!< 数据字节数 0..8(经典) / 0..64(FD) */
    uint8_t  data[APP_CAN_MAX_DATA_LEN];/*!< 载荷 */
    uint64_t ts_us;                     /*!< 接收时刻（app_can_now_us()，发送时忽略） */
} app_can_frame_t;

/* 一个验收过滤器元素：命中 (rx_id & mask) == (id & mask) 的帧进 RXFIFO0。
 * mask 传 0x7FF（标准）/0x1FFFFFFF（扩展）表示精确匹配；传 0 表示全部接收。 */
typedef struct app_can_filter_struct {
    uint32_t id;
    uint32_t mask;
    uint8_t  ext;                       /*!< 1 = 扩展 ID 过滤器 */
} app_can_filter_t;

/* 统计计数（软件计数 + 从 MCAN PSR/ECR 现场读取的错误状态） */
typedef struct app_can_stats_struct {
    uint32_t tx_frames;                 /*!< 成功交给 TX FIFO 的帧数 */
    uint32_t tx_fail;                   /*!< 入队失败（FIFO 满/参数非法/超时） */
    uint32_t rx_frames;                 /*!< 收到的帧数 */
    uint32_t rx_dropped;                /*!< 保留：上层缓冲区满丢弃（组件本身不丢） */
    uint32_t busoff_count;              /*!< 累计进入 bus-off 次数 */
    uint32_t error_passive_count;       /*!< 累计进入 error-passive 次数 */
    uint32_t last_error_code;           /*!< PSR.LEC 最近错误码 */
    uint8_t  tec;                       /*!< 发送错误计数 */
    uint8_t  rec;                       /*!< 接收错误计数 */
    uint8_t  bus_off;                   /*!< 1 = 当前处于 bus-off */
    uint8_t  error_passive;             /*!< 1 = 当前 error-passive */
} app_can_stats_t;

/* 自定义位时序（1 基，与 MCAN 寄存器语义一致）：
 *   NTSEG1 = num_seg1 - 1、NTSEG2 = num_seg2 - 1、NSJW = num_sjw - 1、NBRP = prescaler - 1
 *   位率 = 时钟 / (prescaler * (1 + num_seg1 + num_seg2)) */
typedef struct app_can_bit_timing_struct {
    uint16_t prescaler;
    uint16_t num_seg1;
    uint16_t num_seg2;
    uint16_t num_sjw;
} app_can_bit_timing_t;

/* 初始化参数。先用 APP_CAN_CFG_DEFAULT 再改字段。
 * 默认使用预置档位 speed；若 use_custom_timing=true，则改用 timing/data_timing + fd 决定位时序，
 * 供 gs_usb（内核按 BT_CONST 计算任意波特率）等需要任意位时序的上层使用。 */
typedef struct app_can_cfg_struct {
    app_can_speed_t speed;              /*!< 位时序档位 */
    bool loopback;                      /*!< true = 内部环回（无总线也能自测） */
    bool silent;                        /*!< true = 只听模式（restricted operation） */
    const app_can_filter_t *filters;    /*!< 初始化时安装的过滤器数组，NULL=全收 */
    int filter_count;                   /*!< filters 元素个数 */
    /* --- 以下为可选扩展（默认全 0/false，行为与旧版一致） --- */
    bool use_custom_timing;             /*!< 1 = 用下面自定义位时序替代预置档位 */
    bool fd;                            /*!< use_custom_timing 时：1 = 启用 CAN FD 帧格式 */
    bool one_shot;                      /*!< 1 = 关闭自动重传（gs_usb ONE_SHOT 语义） */
    bool tdc_enable;                    /*!< FD 数据段 TDC 使能（>1 Mbit/s 建议开） */
    uint8_t tdc_ssp_offset;             /*!< TDC SSP 偏移（源时钟周期数） */
    uint8_t tdc_filter_window;          /*!< TDC 滤波窗口 */
    app_can_bit_timing_t timing;        /*!< 仲裁段位时序 */
    app_can_bit_timing_t data_timing;   /*!< FD 数据段位时序（fd=1 时有效） */
} app_can_cfg_t;

/* 注意：历史上这里用"位置初始化"，结构体加字段后会错位（出现过 tdc_filter_window 漏填、
 * 末尾多一个 false 的 excess elements 警告）。改用指定初始化器 + 其余字段零值，加字段不会再错位。 */
#define APP_CAN_CFG_DEFAULT { .speed = APP_CAN_SPEED_CLASSIC_500K }

/* 组件实例（静态分配，不要放栈上） */
typedef struct app_can_struct {
    MCAN_Type *base;                    /*!< HPM_MCAN0..3 */
    app_can_speed_t speed;
    bool inited;
    bool loopback;
    uint32_t src_clk_hz;
    app_can_stats_t stats;
} app_can_t;

/* ---------------- 契约 API（docs/应用层开发约定.md §4.1） ---------------- */

/**
 * @brief 初始化一个 MCAN 节点（含引脚、时钟、收发器、位时序、过滤器）
 * @param c    实例
 * @param base HPM_MCANx
 * @param cfg  参数；NULL 表示 APP_CAN_CFG_DEFAULT（经典 500k，全收）
 * @return 0 成功；<0 失败（-1 参数，-2 时钟，-3 mcan_init 失败）
 */
int app_can_init(app_can_t *c, MCAN_Type *base, const app_can_cfg_t *cfg);

/**
 * @brief 发送一帧
 * @param timeout_us 等待 TX FIFO 空位的上限（0 = 只试一次，不等待）
 * @return 0 = 已入 TX FIFO；1 = 超时/失败（未发出）
 * @note 返回 0 表示"已被控制器接收并开始发送"，不等于总线上已有 ACK；
 *       需要 ACK 级的确认请用 mcan_get_protocol_status() 或对端回显。
 */
int app_can_send(app_can_t *c, const app_can_frame_t *f, uint32_t timeout_us);

/**
 * @brief 轮询接收一帧（RXFIFO0 优先，其次 RXFIFO1）
 * @param timeout_us 超时（0 = 只查一次）
 * @return 0 = 收到；1 = 超时
 */
int app_can_recv(app_can_t *c, app_can_frame_t *f, uint32_t timeout_us);

/**
 * @brief 运行期重装过滤器（会短暂进入 MCAN 初始化模式，可能丢弃正在发的帧）
 * @param n 过滤器个数；n=0 表示恢复"全部接收"
 * @note 调用前请确保总线业务可短暂中断；网关这类全收场景不需要调用。
 */
void app_can_set_filter(app_can_t *c, const app_can_filter_t *flt, int n);

/** @brief 单调递增微秒时基（mcycle / CPU MHz），用于时延统计 */
uint64_t app_can_now_us(void);

/** @brief CPU 主频（MHz），app_can_now_us 的换算基准 */
uint32_t app_can_cpu_mhz(void);

/** @brief 读取统计（软件计数 + PSR/ECR 现场值），并刷新 busoff/error-passive 累计 */
void app_can_get_stats(app_can_t *c, app_can_stats_t *st);

/* ---------------- 便利 API（不属于契约，上层可选用） ---------------- */

/** @brief 内部环回自测：发一帧 8B 经典帧并收回来校验（要求该实例当前空闲） */
int app_can_loopback_test(MCAN_Type *base, app_can_speed_t speed, uint32_t id);

/** @brief 状态码转可读字符串，便于 printf */
const char *app_can_strerror(int rc);

/** @brief 打印一行节点状态（act/lec/tec/rec/bo），返回统计快照 */
void app_can_dump(app_can_t *c, const char *tag);

/** @brief 位时序摘要字符串（如 "500k/SP87.5%"），供日志打印 */
const char *app_can_speed_str(app_can_speed_t speed);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAN_H */