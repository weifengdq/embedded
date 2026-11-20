/**
 * @file    s2500.h
 * @brief   T30HM1TS2500 (S2500) 10BASE-T1S MAC-PHY 通用驱动 API
 *
 * 该驱动不依赖任何 RTOS / 厂商 SDK, 仅需要一个 SPI 全双工传输回调、
 * 微秒/毫秒延时回调即可工作。所有网络协议栈相关的胶水代码见
 * s2500_netif.c (lwIP), 命令行见 s2500_shell.c (letter-shell)。
 *
 * 支持的 S2500 特性:
 *   - OPEN Alliance 10BASE-T1x MAC-PHY SPI (控制事务 / 数据块事务)
 *   - Store & Forward 与 Cut-Through
 *   - MAC 地址过滤 (4 组过滤+掩码)、广播/组播过滤、混杂模式
 *   - 4KB TX / 8KB RX 缓冲, 64B chunk
 *   - PLCA: 协调者/跟随者、节点数、TO 定时器、突发模式、协调者冗余
 *   - SQI / SQI+ 信号质量
 *   - HDD 线束故障检测
 *   - OPEN Alliance TC14 拓扑发现 (距离/延时测量)
 *   - OPEN Alliance TC10 睡眠/唤醒 (本地/远程/唤醒音)
 *   - OPEN Alliance TC6 精确时间戳 (32/64-bit)、捕获、比较、PPS 输出
 *   - DIO0/DIO1 LED 与 SFD/时钟输出
 *
 * 许可: MIT
 */
#ifndef S2500_H
#define S2500_H

#include <stdint.h>
#include <stddef.h>
#include "s2500_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* 常量                                                               */
/* ------------------------------------------------------------------ */
#define S2500_CHUNK_SIZE        64u    /* CONFIG0.CPS = 64B */
#define S2500_CTRL_XFER_LEN     12u    /* 控制事务: hdr4 + dummy4 + data4 */
#define S2500_DATA_XFER_LEN     (4u + S2500_CHUNK_SIZE)   /* 68 */
#define S2500_MAX_FRAME         1536u  /* 含 FCS 的最大以太网帧 */
#define S2500_MIN_FRAME         60u    /* 不含 FCS 的最小帧 */

/* 返回值 */
#define S2500_OK                 0
#define S2500_ERR               -1
#define S2500_ERR_PARAM         -2
#define S2500_ERR_SPI           -3
#define S2500_ERR_TIMEOUT       -4
#define S2500_ERR_NOMEM         -5
#define S2500_ERR_PROTOCOL      -6
#define S2500_ERR_NO_FRAME      -7
#define S2500_ERR_TX            -8

/* ------------------------------------------------------------------ */
/* 平台移植接口                                                       */
/* ------------------------------------------------------------------ */
/**
 * @brief 平台 HAL。除 spi_xfer 外都可选。
 *
 * spi_xfer: 全双工传输 len 字节, 函数内部负责 CS_N 拉低/拉高
 *           (CS_N 必须覆盖整个 len 传输)。SPI 模式 0, MSB first。
 *           len 最大 68 字节, 部分平台可支持一次传输多块。
 */
typedef struct {
    void     *ctx;
    int     (*spi_xfer)(void *ctx, const uint8_t *tx, uint8_t *rx, uint32_t len);
    void    (*delay_us)(void *ctx, uint32_t us);
    uint32_t (*millis)(void *ctx);
    int     (*irq_level)(void *ctx);   /* 1 = IRQ_N 低(有中断) */
    void    (*log)(void *ctx, const char *fmt, ...);
} s2500_hal_t;

/* ------------------------------------------------------------------ */
/* 统计与状态                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t tx_frames;
    uint32_t tx_bytes;
    uint32_t tx_chunks;
    uint32_t tx_busy_waits;
    uint32_t tx_dropped;
    uint32_t tx_buf_overflow;
    uint32_t tx_underflow;
    uint32_t tx_protocol_err;
    uint32_t tx_fcs_err;
    uint32_t rx_frames;
    uint32_t rx_bytes;
    uint32_t rx_chunks;
    uint32_t rx_fcs_err;
    uint32_t rx_overflow;
    uint32_t rx_dropped;
    uint32_t rx_polls;
    uint32_t rx_resync;
    uint32_t ctrl_xfer;
    uint32_t ctrl_parity_err;
    uint32_t footer_parity_err;
    uint32_t header_bad;
    uint32_t irq_count;
} s2500_stats_t;

typedef struct {
    uint16_t link_up;        /* PHY STATUS.LKST */
    uint16_t plca_en;
    uint16_t plca_pst;
    uint16_t plca_id;
    uint16_t plca_ncnt;
    uint16_t plca_crps;      /* 协调者冗余正在代发 BEACON */
    uint8_t  sqi;            /* 0..7 */
    uint8_t  sqi_valid;
    uint8_t  sqip;           /* 0..31 分辨率, 值域 0..255 */
    uint8_t  sqip_valid;
    uint8_t  plca_diag;      /* RXINTO|UNEXPB|BCNBFTO 原始位 */
    uint32_t phys_collisions;/* PCSDIAG2 读清 */
    uint32_t remote_jabber;  /* PCSDIAG1 读清 */
    uint8_t  tp_mode;        /* 1 = PCS 环回, 2 = PHY(模拟)环回 */
} s2500_status_t;

/* ------------------------------------------------------------------ */
/* 设备对象                                                           */
/* ------------------------------------------------------------------ */
typedef struct {
    const s2500_hal_t *hal;
    uint8_t  mac[6];
    uint8_t  chunk_size;     /* 64 */
    uint8_t  plca_id;
    uint8_t  plca_ncnt;
    uint8_t  irq_enabled;
    uint32_t irq_mask;       /* IMSK0 缓存 */
    int32_t  tx_credit;      /* TX chunk 信用本地缓存, -1 = 未知 */
    s2500_stats_t stats;
    /* DMA/SPI 缓冲 (按 dword 对齐) */
    uint8_t  txb[S2500_DATA_XFER_LEN] __attribute__((aligned(8)));
    uint8_t  rxb[S2500_DATA_XFER_LEN] __attribute__((aligned(8)));
    /* 接口状态 */
    int      link_up;
    uint32_t last_link_poll_ms;
} s2500_dev_t;

typedef struct {
    const uint8_t *mac;      /* 为 NULL 时使用芯片出厂 MAC */
    uint8_t  plca_id;        /* 0 = 协调者, 1..254 = 跟随者, 255 = 不参与 PLCA */
    uint8_t  plca_ncnt;      /* PLCA 节点数, 默认 8 */
    uint8_t  plca_totmr;     /* TO 定时器, 默认 32 (bit times) */
    uint8_t  plca_en;
    uint8_t  plca_cr;        /* 协调者冗余 */
    uint8_t  plca_prec;      /* 优先级模式 */
    uint8_t  promiscuous;
    uint8_t  cut_through_tx;
    uint8_t  cut_through_rx;
    uint8_t  ts_en;          /* TC6 帧时间戳使能 */
    uint8_t  irq_enabled;
} s2500_cfg_t;

/* ------------------------------------------------------------------ */
/* 初始化 / 基本访问                                                  */
/* ------------------------------------------------------------------ */
int  s2500_init(s2500_dev_t *dev, const s2500_hal_t *hal);
int  s2500_soft_reset(s2500_dev_t *dev);
int  s2500_probe(s2500_dev_t *dev);                 /* 读 ID 并校验 */
int  s2500_read_reg(s2500_dev_t *dev, uint8_t mms, uint16_t addr, uint32_t *val);
int  s2500_write_reg(s2500_dev_t *dev, uint8_t mms, uint16_t addr, uint32_t val);
int  s2500_update_bits(s2500_dev_t *dev, uint8_t mms, uint16_t addr,
                       uint32_t clr, uint32_t set);
int  s2500_configure(s2500_dev_t *dev, const s2500_cfg_t *cfg);
int  s2500_get_factory_mac(s2500_dev_t *dev, uint8_t mac[6]);
int  s2500_set_mac(s2500_dev_t *dev, const uint8_t mac[6]);
int  s2500_start(s2500_dev_t *dev);                 /* TXEN/RXEN + SYNC */
int  s2500_stop(s2500_dev_t *dev);

/* ------------------------------------------------------------------ */
/* 数据收发                                                           */
/* ------------------------------------------------------------------ */
int s2500_send(s2500_dev_t *dev, const uint8_t *frame, uint16_t len, uint32_t flags);
#define S2500_XMIT_TS_A  0x1u   /* 捕获发送时间戳到 TTSCA */
#define S2500_XMIT_TS_B  0x2u
#define S2500_XMIT_TS_C  0x3u

/**
 * @brief 取出一个已接收帧。无帧返回 S2500_ERR_NO_FRAME。
 * @param frame  至少 S2500_MAX_FRAME 字节
 * @param len    输出帧长 (不含 FCS)
 * @param ts     可选, 输出 32-bit 时间戳 (ns 部分), NULL 表示不关心
 */
int s2500_recv(s2500_dev_t *dev, uint8_t *frame, uint16_t *len, uint32_t *ts);
uint32_t s2500_rx_chunks_available(s2500_dev_t *dev);
uint32_t s2500_tx_chunks_available(s2500_dev_t *dev);

/* ------------------------------------------------------------------ */
/* 状态 / 诊断                                                        */
/* ------------------------------------------------------------------ */
int  s2500_read_status(s2500_dev_t *dev, s2500_status_t *st);
int  s2500_read_counters(s2500_dev_t *dev, uint32_t out[35]);
int  s2500_read_pcs_counters(s2500_dev_t *dev, uint32_t *collisions, uint32_t *rjab);
int  s2500_clear_counters(s2500_dev_t *dev);
int  s2500_read_sqi(s2500_dev_t *dev, uint8_t *sqi, uint8_t *sqip);
int  s2500_plca_diag(s2500_dev_t *dev, uint8_t *diag);
int  s2500_read_irq_status(s2500_dev_t *dev, uint32_t *irqs);
int  s2500_clear_status0(s2500_dev_t *dev, uint32_t mask);

/* ------------------------------------------------------------------ */
/* PLCA                                                               */
/* ------------------------------------------------------------------ */
int  s2500_plca_configure(s2500_dev_t *dev, uint8_t id, uint8_t ncnt,
                          uint8_t totmr, int enable);
int  s2500_plca_set_id(s2500_dev_t *dev, uint8_t id);
int  s2500_plca_coordinator(s2500_dev_t *dev, int on);
int  s2500_plca_redundancy(s2500_dev_t *dev, int on);
int  s2500_plca_precedence(s2500_dev_t *dev, int on);
int  s2500_plca_burst(s2500_dev_t *dev, uint8_t maxbc, uint8_t btmr);
int  s2500_plca_status(s2500_dev_t *dev, uint8_t *pst, uint8_t *crps);

/* ------------------------------------------------------------------ */
/* 高级诊断: HDD / SQI / 拓扑发现                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    int      valid;
    int      ready;
    uint8_t  short_open;   /* 0=正常 1=开路/终端缺失 2=短路/多余终端 3=未知 */
    uint8_t  one_wire;     /* 单线短路 */
} s2500_hdd_result_t;

int  s2500_hdd_run(s2500_dev_t *dev, s2500_hdd_result_t *res, uint32_t timeout_ms);

typedef struct {
    uint8_t  auto_mode;
    uint8_t  delay_only;   /* 1 = 只做内部延时测量 (s2500 td dly) */
    uint8_t  auto_err;
    uint8_t  dm_err;
    uint8_t  dlym_err;
    uint8_t  dm_dur_ms;
    uint32_t dist_mr;      /* 收到的脉冲数 */
    uint32_t dly_mr;       /* 内部延时测量脉冲数 */
    uint32_t mndly_mr;     /* 被测节点延时脉冲数 */
    uint8_t  mndly_dur_ms;
} s2500_td_result_t;

int  s2500_td_run(s2500_dev_t *dev, const s2500_td_result_t *req,
                  s2500_td_result_t *res, uint32_t timeout_ms);
int  s2500_td_abort(s2500_dev_t *dev);

/* ------------------------------------------------------------------ */
/* TC6 时间戳 / 捕获 / 比较                                           */
/* ------------------------------------------------------------------ */
int  s2500_time_read(s2500_dev_t *dev, uint32_t *sec, uint32_t *nsec);
int  s2500_time_set(s2500_dev_t *dev, uint32_t sec, uint32_t nsec);
int  s2500_ttsc_read(s2500_dev_t *dev, uint8_t idx, uint64_t *ts64);
int  s2500_capture_configure(s2500_dev_t *dev, uint8_t edge, int continuous);
int  s2500_capture_read(s2500_dev_t *dev, uint32_t *sec, uint32_t *nsec);
int  s2500_compare_oneshot(s2500_dev_t *dev, uint32_t sec, uint32_t nsec, uint8_t dio_pol);
int  s2500_compare_clock(s2500_dev_t *dev, uint32_t period_ns, uint8_t dio_pol);
int  s2500_compare_stop(s2500_dev_t *dev);

/* ------------------------------------------------------------------ */
/* TC10 睡眠 / 唤醒                                                   */
/* ------------------------------------------------------------------ */
int  s2500_sleep(s2500_dev_t *dev);
int  s2500_send_wake_tone(s2500_dev_t *dev);
int  s2500_wake_config(s2500_dev_t *dev, uint8_t wfwd, uint8_t wdeb,
                       uint8_t lwd, uint8_t rwd, uint8_t lwth);
int  s2500_wake_pin_output(s2500_dev_t *dev, int on);
int  s2500_read_reset_reason(s2500_dev_t *dev, uint32_t *ws_status1);

/* ------------------------------------------------------------------ */
/* 自愈看门狗 (检测芯片自发复位并自动重新初始化)                     */
/* ------------------------------------------------------------------ */
/*
 * S2500 实测会在运行中无故回到上电默认态 (CONFIG0.SYNC=0, PLCA id=255,
 * DIOCFG0 归 0), 而 WS_STATUS1 没有 POR/RST 标志。驱动在 s2500_app_poll()
 * 里每 S2500_WD_PERIOD_MS 读一次 PLCA_CTL1, 与驱动当前配置比对; 不一致就
 * 判定芯片已复位, 由应用注册的回调 (复用 s2500_app_init 的初始化路径)
 * 重新初始化芯片, 无需 reboot MCU。
 *
 * 成本: 每次检查 1 次 12 字节控制事务 (约 6us @25MHz), 2s 一次可忽略。
 * 判据:
 *   1) PLCA_CTL1.ID   != dev->plca_id  -> 复位 (协调者 id=0 是正常值)
 *   2) PLCA_CTL1.NCNT != dev->plca_ncnt -> 复位
 *   3) PLCA 关闭 (id=255) 时 id 无法区分, 改用 CONFIG0.SYNC 兜底
 *   4) 每 10 次检查核对一次 IDVER == 0x11
 * SPI 读失败 (TC10 Sleep 期间芯片不响应) 只计 spi_errors, 不判定复位。
 */
#ifndef S2500_WD_PERIOD_MS
#define S2500_WD_PERIOD_MS  2000u   /* 健康检查周期 (ms), 编译时可用 -D 覆盖 */
#endif
#define S2500_WD_IDVER      0x11u   /* IDVER 期望值 */

/* s2500_wd_poll() 检出原因 (s2500_wd_t.last_reason) */
#define S2500_WD_REASON_NONE    0u
#define S2500_WD_REASON_ID      1u  /* PLCA_CTL1.ID 与配置不符 */
#define S2500_WD_REASON_NCNT    2u  /* PLCA_CTL1.NCNT 与配置不符 */
#define S2500_WD_REASON_SYNC    3u  /* CONFIG0.SYNC 丢失 (PLCA 关闭时兜底) */
#define S2500_WD_REASON_IDVER   4u  /* IDVER != 0x11 */

typedef struct {
    uint8_t  enabled;          /* 1 = 看门狗运行 (默认 on) */
    uint8_t  started;          /* 已记录首次检查时间 */
    uint16_t period_ms;        /* 检查周期 */
    uint32_t checks;           /* 检查次数 */
    uint32_t detections;       /* 检出复位次数 */
    uint32_t reinit_ok;        /* 自动重新初始化成功次数 */
    uint32_t reinit_fail;      /* 自动重新初始化失败次数 */
    uint32_t spi_errors;       /* 检查时 SPI 读失败次数 (Sleep 等, 不算复位) */
    uint32_t last_check_ms;    /* 上次检查时刻 (hal->millis, 自 MCU 启动) */
    uint32_t last_detect_ms;   /* 上次检出时刻 */
    uint32_t last_recover_ms;  /* 上次重初始化完成时刻 */
    uint8_t  last_reason;      /* 上次检出原因 */
} s2500_wd_t;

/** 检出复位后的重初始化回调: 返回 0 表示成功, 非 0 下个周期重试 */
typedef int (*s2500_wd_reinit_fn)(void *ctx);

/** 注册重初始化回调 (应用层复用 s2500_app_init 的初始化路径) */
void        s2500_wd_set_reinit_cb(s2500_wd_reinit_fn cb, void *ctx);
/** 看门狗状态 (shell 统计/开关使用) */
s2500_wd_t *s2500_wd_state(void);
void        s2500_wd_set_enabled(int on);
int         s2500_wd_enabled(void);
/** 清统计并重新计时 (s2500_app_init 成功后调用) */
void        s2500_wd_reset(void);
/**
 * @brief 主循环调用: 到周期后检查一次芯片是否回到上电默认态
 * @return 1 = 本次检出复位并已尝试自动重初始化
 */
int         s2500_wd_poll(s2500_dev_t *dev);

/* ------------------------------------------------------------------ */
/* DIO / LED / 模拟参数                                               */
/* ------------------------------------------------------------------ */
int  s2500_dio_config(s2500_dev_t *dev, uint8_t dio, uint8_t fn, uint8_t val,
                      uint8_t pull_en, uint8_t pull_down, uint8_t slew_fast);
int  s2500_led_config(s2500_dev_t *dev, uint8_t act_fn, uint8_t link_fn);
int  s2500_analog_config(s2500_dev_t *dev, uint8_t tx_amp, uint8_t edth,
                         uint8_t edthcd, uint8_t tx_slw, uint8_t clko_en,
                         uint8_t tx_cmc);
int  s2500_loopback(s2500_dev_t *dev, uint8_t mode);   /* 0=off 1=PCS 2=PHY */
typedef struct {
    int      rc;
    uint32_t rx_frames;
    uint16_t rxlen;
    uint8_t  head[16];
} s2500_lb_result_t;

/** 环回自检: 发送一帧并从环回路径收回, 不需要外部总线 */
int  s2500_loopback_test(s2500_dev_t *dev, uint8_t mode, uint32_t timeout_ms);
int  s2500_raw_block_dump(s2500_dev_t *dev, int verbose);
int  s2500_loopback_test_ex(s2500_dev_t *dev, uint8_t mode, uint32_t timeout_ms,
                            s2500_lb_result_t *res);

/* ------------------------------------------------------------------ */
/* 工具                                                               */
/* ------------------------------------------------------------------ */
void s2500_dump_info(s2500_dev_t *dev);
uint32_t s2500_oaspi_parity(uint32_t v);
int  s2500_reg_field_write(s2500_dev_t *dev, uint8_t mms, uint16_t addr,
                           uint32_t mask, uint32_t shift, uint32_t value);

#ifdef __cplusplus
}
#endif
#endif /* S2500_H */
