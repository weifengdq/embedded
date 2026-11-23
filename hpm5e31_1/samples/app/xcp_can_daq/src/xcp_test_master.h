/*
 * xcp_test_master.h —— 板内 XCP 主机（CAN1），用于无上位机时的自证测试
 *
 * 板上有 CAN0/CAN1 两个节点接在同一段总线上（见 docs/应用层开发约定.md §2），
 * 因此可以让 CAN0 跑 XCP 从站、CAN1 跑一个"最小 XCP 主机"，把上位机要做的
 * CONNECT / SET_MTA / UPLOAD / DOWNLOAD / DAQ 配置全流程在板内跑一遍。
 *
 * 这样即使主机 vcan/cannelloni 网关还没就绪，也能得到真实的线上时序与采样率。
 */
#ifndef XCP_TEST_MASTER_H
#define XCP_TEST_MASTER_H

#include <stdint.h>
#include <stdbool.h>
#include "hpm_mcan_drv.h"

/* 被测/标定量（从站读取的就是这些变量的绝对地址） */
extern volatile uint16_t g_adc_raw;
extern volatile uint32_t g_tick_cnt;
extern volatile int32_t  g_pid_mid;
extern volatile uint16_t g_cpu_load_pm;
extern volatile uint8_t  g_u8_cnt;
extern volatile uint32_t g_sw_flag;
extern volatile float    g_kp;
extern volatile float    g_ki;

void tm_init(MCAN_Type *base);

/* 完整自证脚本：连接/识别/内存读写/位操作/种子解锁/DAQ 采集。返回 FAIL 项数（0 = 全 PASS） */
uint32_t tm_selftest(uint32_t daq_ms);

/* 只跑 DAQ 采集部分（DAQ 已配置好时用） */
uint32_t tm_daq_run(uint32_t ms);

void tm_stat(void);

#endif /* XCP_TEST_MASTER_H */
