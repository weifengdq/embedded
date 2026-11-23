/*
 * xcp_test_master_cal.h —— 板内 XCP 主机（CAN1）仅做标定相关测试
 */
#ifndef XCP_TEST_MASTER_CAL_H
#define XCP_TEST_MASTER_CAL_H

#include <stdint.h>
#include "hpm_mcan_drv.h"

void tm_init(MCAN_Type *base);

/* 标定自证：读/写/回读 Kp、Ki、占空比、LED 周期；MODIFY_BITS 改 g_ctrl_enable；
 * GET_SEED/UNLOCK 错误/正确 key；Kp 从 0.1 到 2.0 扫 10 点看阶跃响应。
 * 返回 FAIL 项数（0 = 全 PASS） */
uint32_t tm_cal_selftest(void);

/* 由 main.c 的 1 kHz 事件里推进的阶跃测试，供扫参使用 */
void app_step_test(uint32_t timeout_ms);

#endif /* XCP_TEST_MASTER_CAL_H */
