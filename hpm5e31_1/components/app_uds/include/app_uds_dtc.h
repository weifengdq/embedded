/*
 * app_uds_dtc.h - UDS 故障码内存（RAM 表 + 可选 Flash 持久化）
 *
 * 状态位按 ISO 14229-1:2013 附录 D：
 *   bit0 testFailed                当前测试失败（本次上电后仍失败）
 *   bit1 testFailedThisOperationCycle  本操作循环内失败过
 *   bit2 pendingDTC                待定故障
 *   bit3 confirmedDTC              已确认故障（会点亮 MIL）
 *   bit4 testNotCompletedSinceLastClear
 *   bit5 testFailedSinceLastClear
 *   bit6 testNotCompletedThisOperationCycle
 *   bit7 warningIndicatorRequested
 *
 * 快照（0x19 04）与扩展数据（0x19 06）内容为厂商自定义，本实现：
 *   快照 record 0x01：3 个 DID（0x0100 车速 km/h、0x0101 蓄电池电压 mV、0x0102 冷却液温度 ℃）
 *   扩展 record 0x02：发生次数 occ、老化计数 aging
 */
#ifndef APP_UDS_DTC_H
#define APP_UDS_DTC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_UDS_DTC_MAX        8u
#define APP_UDS_DTC_SNAP_DIDS  3u
#define APP_UDS_DTC_SNAP_DLEN  4u

typedef struct {
    uint16_t did;
    uint8_t  len;
    uint8_t  data[APP_UDS_DTC_SNAP_DLEN];
} app_uds_dtc_snap_t;

typedef struct {
    uint32_t dtc;            /* 3 字节 DTC，右对齐 */
    uint8_t  status;
    uint8_t  occ;            /* 发生次数（0xFF 饱和） */
    uint8_t  aging;          /* 老化计数 */
    bool     used;
    app_uds_dtc_snap_t snap[APP_UDS_DTC_SNAP_DIDS];
    uint8_t  snap_cnt;
} app_uds_dtc_entry_t;

typedef struct {
    app_uds_dtc_entry_t ent[APP_UDS_DTC_MAX];
    uint32_t count;
    uint8_t  availability_mask;   /* 本 ECU 支持的状态位 */
    uint32_t cleared_cnt;
} app_uds_dtc_mem_t;

void app_uds_dtc_init(app_uds_dtc_mem_t *m);
/* 置位/清位：set_bits 与 status 相或；clear_bits 取反再与。返回 0 成功，-1 表满 */
int  app_uds_dtc_set(app_uds_dtc_mem_t *m, uint32_t dtc, uint8_t set_bits, uint8_t clear_bits);
/* 记录一次快照（故障发生瞬间的冻结帧） */
int  app_uds_dtc_set_snapshot(app_uds_dtc_mem_t *m, uint32_t dtc, const app_uds_dtc_snap_t *snap, uint32_t n);
int  app_uds_dtc_find(app_uds_dtc_mem_t *m, uint32_t dtc, app_uds_dtc_entry_t **out);
void app_uds_dtc_clear_all(app_uds_dtc_mem_t *m);
uint32_t app_uds_dtc_count_by_mask(const app_uds_dtc_mem_t *m, uint8_t mask);
/* 老化：本操作循环结束时调用，confirmed 但未失败的 DTC aging+1，>40 清 confirmed */
void app_uds_dtc_end_operation_cycle(app_uds_dtc_mem_t *m);

#ifdef __cplusplus
}
#endif

#endif /* APP_UDS_DTC_H */
