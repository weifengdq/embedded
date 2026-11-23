/**
 * CANopenNode application wrapper for hpm5e31_1 - 单线程轮询式实现。
 *
 * @file        canopen_node.c
 * @copyright   Apache-2.0 (CANopenNode) / hpm5e31_1 项目组
 */
#include <string.h>

#include "canopen_node.h"

#include "CANopen.h"
#include "OD.h"
#include "301/CO_driver.h"

#include "hpm_csr_drv.h"
#include "hpm_clock_drv.h"
#include "ext/board_can.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CO_driver.c 中的计数器 */
uint32_t co_can_rx_count;
uint32_t co_can_tx_count;
uint32_t co_can_tx_retry;

static CO_t* s_co;
static co_node_cfg_t s_cfg;
static uint32_t s_ticks_per_us;
static uint64_t s_last_cycle;
static uint32_t s_heap_bytes;
static uint32_t s_loops;
static uint32_t s_max_cycle_us;

/* 按参考 main_blank.c 的流程初始化 CAN + CANopen 对象（不含 CO_new） */
static int co_stack_start(void) {
    CO_ReturnError_t err;
    uint32_t err_info = 0U;
    uint16_t nmt_control = CO_NMT_STARTUP_TO_OPERATIONAL | CO_NMT_ERR_ON_ERR_REG | CO_ERR_REG_GENERIC_ERR
                           | CO_ERR_REG_COMMUNICATION;

    if (s_cfg.nmt_start_operational == 0U) {
        nmt_control = (uint16_t) (nmt_control & (uint16_t) (~CO_NMT_STARTUP_TO_OPERATIONAL));
    }
    s_co->CANmodule->CANnormal = false;
    err = CO_CANinit(s_co, (void*) s_cfg.can_base, s_cfg.bitrate_kbps);
    if (err != CO_ERROR_NO) {
        return -10 + (int) err;
    }
    err = CO_CANopenInit(s_co, NULL, NULL, OD, NULL, nmt_control, 500U, 1000U, 500U, false, s_cfg.node_id,
                         &err_info);
    if (err != CO_ERROR_NO && err != CO_ERROR_NODE_ID_UNCONFIGURED_LSS) {
        return -20 + (int) err;
    }
    err = CO_CANopenInitPDO(s_co, s_co->em, OD, s_cfg.node_id, &err_info);
    if (err != CO_ERROR_NO && err != CO_ERROR_NODE_ID_UNCONFIGURED_LSS) {
        return -30 + (int) err;
    }
    CO_CANsetNormalMode(s_co->CANmodule);
    s_last_cycle = hpm_csr_get_core_mcycle();
    return 0;
}

int co_node_init(const co_node_cfg_t* cfg) {
    int ret;

    if ((cfg == NULL) || (cfg->can_base == NULL) || (cfg->node_id < 1U) || (cfg->node_id > 127U)) {
        return -1;
    }
    s_cfg = *cfg;
    if (s_cfg.bitrate_kbps == 0U) {
        s_cfg.bitrate_kbps = 500U;
    }
    s_ticks_per_us = clock_get_frequency(clock_cpu0) / 1000000U;
    if (s_ticks_per_us == 0U) {
        s_ticks_per_us = 480U;
    }
    if (s_cfg.heartbeat_ms != 0U) {
        OD_PERSIST_COMM.x1017_producerHeartbeatTime = s_cfg.heartbeat_ms;
    }
    s_co = CO_new(NULL, &s_heap_bytes);
    if (s_co == NULL) {
        return -2;
    }
    board_can_transceivers_enable();
    ret = co_stack_start();
    if (ret != 0) {
        return ret;
    }
    s_loops = 0U;
    s_max_cycle_us = 0U;
    return 0;
}

co_node_reset_t co_node_poll(void) {
    CO_NMT_reset_cmd_t reset;
    bool_t sync_was = false;
    uint64_t now;
    uint32_t dt_us;

    if (s_co == NULL) {
        return CO_NODE_RESET_NONE;
    }
    now = hpm_csr_get_core_mcycle();
    dt_us = (uint32_t) ((now - s_last_cycle) / (uint64_t) s_ticks_per_us);
    s_last_cycle = now;
    s_loops++;
    if (dt_us > s_max_cycle_us) {
        s_max_cycle_us = dt_us;
    }

    CO_CANpoll_rx(s_co->CANmodule);
    CO_CANpoll_tx(s_co->CANmodule);
    CO_CANmodule_process(s_co->CANmodule);

    if ((s_co->CANmodule->CANnormal == 0U) || (s_co->nodeIdUnconfigured != 0U)) {
        return CO_NODE_RESET_NONE;
    }
    /* 参考 main_blank.c 的 RT 线程：SYNC -> RPDO -> TPDO，然后主线程 CO_process */
    sync_was = CO_process_SYNC(s_co, dt_us, NULL);
    CO_process_RPDO(s_co, sync_was, dt_us, NULL);
    CO_process_TPDO(s_co, sync_was, dt_us, NULL);
    reset = CO_process(s_co, false, dt_us, NULL);

    if (reset == CO_RESET_COMM) {
        return CO_NODE_RESET_COMM;
    }
    if (reset == CO_RESET_APP) {
        return CO_NODE_RESET_APP;
    }
    return CO_NODE_RESET_NONE;
}

int co_node_reset_comm(void) {
    if (s_co == NULL) {
        return -1;
    }
    return co_stack_start();
}

void co_node_get_stats(co_node_stats_t* stats) {
    if (stats == NULL) {
        return;
    }
    (void) memset(stats, 0, sizeof(*stats));
    stats->heap_bytes = s_heap_bytes;
    stats->rx_frames = co_can_rx_count;
    stats->tx_frames = co_can_tx_count;
    stats->tx_retry = co_can_tx_retry;
    stats->pol_loops = s_loops;
    stats->max_cycle_us = s_max_cycle_us;
    if (s_co != NULL) {
        stats->can_error_status = s_co->CANmodule->CANerrorStatus;
        if (s_co->NMT != NULL) {
            stats->nmt_state = (uint8_t) s_co->NMT->operatingState;
        }
    }
}

uint8_t co_node_nmt_state(void) {
    if ((s_co == NULL) || (s_co->NMT == NULL)) {
        return 0U;
    }
    return (uint8_t) s_co->NMT->operatingState;
}

void co_node_emcy_report(uint8_t error_bit, uint16_t error_code, uint32_t info) {
    if ((s_co != NULL) && (s_co->em != NULL)) {
        CO_errorReport(s_co->em, error_bit, error_code, info);
    }
}

void co_node_emcy_reset(uint8_t error_bit, uint32_t info) {
    if ((s_co != NULL) && (s_co->em != NULL)) {
        CO_errorReset(s_co->em, error_bit, info);
    }
}

uint8_t co_node_sync_counter(void) {
    if ((s_co == NULL) || (s_co->SYNC == NULL)) {
        return 0U;
    }
    return s_co->SYNC->counter;
}

#ifdef __cplusplus
}
#endif
