/*
 * app_uds_dtc.c - UDS 故障码内存（RAM 表）
 */
#include "app_uds_dtc.h"
#include "app_uds.h"   /* DTC 状态位宏定义在 app_uds.h */

#include <string.h>

void app_uds_dtc_init(app_uds_dtc_mem_t *m)
{
    if (m == NULL) {
        return;
    }
    memset(m, 0, sizeof(*m));
    /* 支持的状态位：bit0..bit3 + bit5 是常见组合，这里全支持便于试验 */
    m->availability_mask = 0x09u;   /* testFailed | confirmedDTC */
}

int app_uds_dtc_find(app_uds_dtc_mem_t *m, uint32_t dtc, app_uds_dtc_entry_t **out)
{
    if ((m == NULL) || (out == NULL)) {
        return -1;
    }
    for (uint32_t i = 0; i < APP_UDS_DTC_MAX; i++) {
        if (m->ent[i].used && (m->ent[i].dtc == dtc)) {
            *out = &m->ent[i];
            return 0;
        }
    }
    *out = NULL;
    return -1;
}

int app_uds_dtc_set(app_uds_dtc_mem_t *m, uint32_t dtc, uint8_t set_bits, uint8_t clear_bits)
{
    app_uds_dtc_entry_t *e = NULL;

    if (m == NULL) {
        return -1;
    }
    if (app_uds_dtc_find(m, dtc, &e) != 0) {
        if (m->count >= APP_UDS_DTC_MAX) {
            return -1;
        }
        for (uint32_t i = 0; i < APP_UDS_DTC_MAX; i++) {
            if (!m->ent[i].used) {
                e = &m->ent[i];
                memset(e, 0, sizeof(*e));
                e->used = true;
                e->dtc = dtc;
                e->status = APP_UDS_DTC_NOT_COMPLETE_SINCE_CLR | APP_UDS_DTC_NOT_COMPLETE_THIS_CYC;
                m->count++;
                break;
            }
        }
        if (e == NULL) {
            return -1;
        }
    }
    e->status |= set_bits;
    e->status = (uint8_t) (e->status & (uint8_t) ~clear_bits);
    if ((set_bits & (APP_UDS_DTC_TEST_FAILED | APP_UDS_DTC_PENDING | APP_UDS_DTC_CONFIRMED)) != 0u) {
        if (e->occ < 0xFFu) {
            e->occ++;
        }
    }
    return 0;
}

int app_uds_dtc_set_snapshot(app_uds_dtc_mem_t *m, uint32_t dtc, const app_uds_dtc_snap_t *snap, uint32_t n)
{
    app_uds_dtc_entry_t *e = NULL;

    if ((m == NULL) || (snap == NULL)) {
        return -1;
    }
    if (app_uds_dtc_find(m, dtc, &e) != 0) {
        return -1;
    }
    if (n > APP_UDS_DTC_SNAP_DIDS) {
        n = APP_UDS_DTC_SNAP_DIDS;
    }
    for (uint32_t i = 0; i < n; i++) {
        e->snap[i] = snap[i];
    }
    e->snap_cnt = (uint8_t) n;
    return 0;
}

void app_uds_dtc_clear_all(app_uds_dtc_mem_t *m)
{
    if (m == NULL) {
        return;
    }
    memset(m->ent, 0, sizeof(m->ent));
    m->count = 0;
    m->cleared_cnt++;
}

uint32_t app_uds_dtc_count_by_mask(const app_uds_dtc_mem_t *m, uint8_t mask)
{
    uint32_t cnt = 0;

    if (m == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < APP_UDS_DTC_MAX; i++) {
        if (m->ent[i].used && ((m->ent[i].status & mask) != 0u)) {
            cnt++;
        }
    }
    return cnt;
}

void app_uds_dtc_end_operation_cycle(app_uds_dtc_mem_t *m)
{
    if (m == NULL) {
        return;
    }
    for (uint32_t i = 0; i < APP_UDS_DTC_MAX; i++) {
        app_uds_dtc_entry_t *e = &m->ent[i];
        if (!e->used) {
            continue;
        }
        e->status = (uint8_t) (e->status & ~(APP_UDS_DTC_TEST_FAILED_THIS_CYC |
                                             APP_UDS_DTC_NOT_COMPLETE_THIS_CYC));
        if ((e->status & APP_UDS_DTC_CONFIRMED) && ((e->status & APP_UDS_DTC_TEST_FAILED) == 0u)) {
            if (e->aging < 0xFFu) {
                e->aging++;
            }
            if (e->aging > 40u) {
                e->status = (uint8_t) (e->status & ~APP_UDS_DTC_CONFIRMED);
            }
        }
    }
}
