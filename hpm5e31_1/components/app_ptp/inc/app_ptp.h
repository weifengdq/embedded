/*
 * Copyright (c) 2026, hpm5e31_1 应用层书稿项目
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * 本文件的 PTP 协议栈主体来自 HPM SDK v1.13.0 middleware/ptpd/v2（PTPd 2.0.1），
 * 由本组件做板级/工程化封装：ENET 硬件时间戳初始化、1 ms 时基、板上 offset 统计、
 * letter-shell 诊断命令。协议实现本身不改动，便于与上游比对。
 */
/**
 * @file app_ptp.h
 * @brief IEEE 1588 PTP 普通时钟（Ordinary Clock）组件 —— hpm5e31_1 / ENET0 + lwIP + ptpd v2
 *
 * 设计要点：
 *  - 时间戳全部来自 ENET MAC 硬件（RX: RDES6/RDES7, TX: TDES6/TDES7 + TTSE），不用软件打戳；
 *  - 传输层用 lwIP UDP，IPv4 组播 224.0.1.129（E2E/一般报文）、224.0.0.107（P2P 路径延迟）；
 *  - two-step 为主（Follow_Up 携带精确发送时刻），E2E/P2P 延迟机制由编译宏选择；
 *  - 从钟侧 PI 伺服通过 ENET TS_ADDEND 微调频率、通过 SYST_*_UPD 粗调相位。
 *
 * 依赖（由使用方 CMakeLists 提供）：
 *  - SDK samples/lwip/common/single/{common,netconf}.c、samples/lwip/ports/baremetal 下的 sys_arch.c/ethernetif.c
 *  - SDK middleware/ptpd/v2（set(CONFIG_PTPD_V2 1)）
 *  - 编译宏：-DLWIP_PTP=1 -DUSE_LWIPOPTS_APP_H=1 -DLWIP_SUPPORT_CUSTOM_PBUF=1
 *  - 本组件 src/phy_glue.c 提供 YT8011AN 的板级 PHY 钩子（不启用 SDK CONFIG_ENET_PHY）
 */
#ifndef APP_PTP_H
#define APP_PTP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** PTP 角色（运行期只读展示用；真正的角色由编译宏 SLAVE_ONLY 决定） */
typedef enum {
    APP_PTP_ROLE_SLAVE = 0,
    APP_PTP_ROLE_MASTER = 1,
} app_ptp_role_t;

typedef struct {
    uint8_t  domain;        /**< PTP 域号，默认 0，必须与主机 ptp4l 的 domainNumber 一致 */
    bool     enable_pps;    /**< 是否打开 ENET PPS0 输出（本板 PPS 未引出，默认 false） */
    uint32_t initial_sec;   /**< 硬件 PTP 时钟上电初值（秒），默认 1651074120 */
} app_ptp_cfg_t;

/**
 * @brief 初始化 ENET 硬件 PTP 时钟（TS_ADDEND/SYST_*、可选 PPS0）
 * @note 必须在 enet_init() 之后、lwip_init() 之前调用
 * @return 0 成功
 */
int app_ptp_hw_init(const app_ptp_cfg_t *cfg);

/**
 * @brief 启动 PTP 协议栈（ptpd_Init）并注册 1 ms 时基
 * @note 必须在 lwip_init()/netif_config()/enet_services() 之后调用
 * @return 0 成功
 */
int app_ptp_start(void);

/**
 * @brief 主循环调用：链路 up 时推进协议状态机，并每秒采样一次 offset 做统计
 */
void app_ptp_poll(void);

/** @brief 读 ENET 硬件 PTP 时钟 */
void app_ptp_read_hw_clock(uint32_t *sec, uint32_t *nsec);

/** @brief 直接设置 ENET 硬件 PTP 时钟 */
void app_ptp_set_hw_clock(uint32_t sec, uint32_t nsec);

/** @brief 打印当前 PTP 状态（shell: ptp） */
void app_ptp_print_status(void);

/** @brief 打印板上累计的 offset 统计（shell: ptpstats） */
void app_ptp_print_stats(void);

/** @brief 阻塞式测量 N 秒（期间保持网络/PTP 轮询），打印周期统计（shell: ptpbench <secs>） */
void app_ptp_bench(uint32_t secs);

/** @brief 复位统计窗口（shell: ptpstats reset） */
void app_ptp_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_PTP_H */