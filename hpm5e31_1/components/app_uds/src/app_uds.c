/*
 * app_uds.c - UDS(ISO 14229-1:2013) 服务端
 *
 * 分层：ISO-TP(传输) -> app_uds(会话/安全/服务) -> DID/DTC/例程/刷写回调
 * 无动态内存、无阻塞：每条请求同步算完，响应经 cfg.tx 交回 ISO-TP。
 */
#include "app_uds.h"

#include <stdio.h>
#include <string.h>

/* ---------------- 内部工具 ---------------- */

static uint64_t uds_now(app_uds_t *u)
{
    if ((u->cfg.now_us != NULL) && (u->cfg.now_us(u->cfg.time_ctx) != 0u)) {
        return u->cfg.now_us(u->cfg.time_ctx);
    }
    return u->now_cache_us;
}

static uint8_t sess_bit(app_uds_session_t s)
{
    switch (s) {
    case APP_UDS_SESSION_DEFAULT:  return APP_UDS_SESS_DEFAULT;
    case APP_UDS_SESSION_PROG:     return APP_UDS_SESS_PROG;
    case APP_UDS_SESSION_EXTENDED: return APP_UDS_SESS_EXT;
    default:                       return 0u;
    }
}

static int uds_tx(app_uds_t *u, const uint8_t *pdu, uint32_t len)
{
    if (len == 0u) {
        return -1;
    }
    if (u->cfg.tx == NULL) {
        return -1;
    }
    /* suppressPosRspMsgIndicationBit：只抑制"正响应"，NRC 必须照发 */
    if (u->suppress_pos && (pdu[0] != 0x7Fu)) {
        u->suppress_pos = false;
        return 0;
    }
    u->suppress_pos = false;
    return u->cfg.tx(u->cfg.tx_ctx, pdu, len);
}

static int uds_nrc(app_uds_t *u, uint8_t sid, uint8_t nrc)
{
    uint8_t buf[3];
    buf[0] = 0x7Fu;
    buf[1] = sid;
    buf[2] = nrc;
    u->stat_resp_nrc++;
    return uds_tx(u, buf, 3u);
}

int app_uds_send_pending(app_uds_t *u, uint8_t sid)
{
    uint8_t buf[3];
    buf[0] = 0x7Fu;
    buf[1] = sid;
    buf[2] = APP_UDS_NRC_RCRRP;
    u->stat_pending++;
    return uds_tx(u, buf, 3u);
}

/* 安全算法：与 host/uds/uds_sec.py、host/uds/rust 完全一致（第 31 章 31-4 节） */
uint32_t app_uds_calc_key(uint32_t seed, uint8_t level)
{
    uint32_t k = seed;
    for (uint32_t i = 0; i < 8u; i++) {
        k = (k << 1) | (k >> 31);
        k ^= 0x3A5C9E17u;
        if (k & 1u) {
            k += 0x5A5A5A5Au;
        }
    }
    k ^= (uint32_t) level * 0x01020304u;
    k = (k << 7) | (k >> 25);
    return k;
}

static uint32_t s_seed_lfsr = 0x1D2C3B4Au;

uint32_t app_uds_next_seed(app_uds_t *u)
{
    s_seed_lfsr ^= s_seed_lfsr << 13;
    s_seed_lfsr ^= s_seed_lfsr >> 17;
    s_seed_lfsr ^= s_seed_lfsr << 5;
    if (s_seed_lfsr == 0u) {
        s_seed_lfsr = 0xA5A5A5A5u;
    }
    u->seed = s_seed_lfsr;
    return u->seed;
}

/* ---------------- DID 表 ---------------- */

static void did_add(app_uds_did_t *d, uint16_t did, const uint8_t *data, uint8_t len,
                    uint8_t rd_mask, uint8_t wr_mask, uint8_t wr_sec)
{
    d->did = did;
    d->len = len;
    memcpy(d->data, data, len);
    d->read_mask = rd_mask;
    d->write_mask = wr_mask;
    d->write_sec = wr_sec;
}

int app_uds_default_dids(app_uds_did_t *tbl, uint32_t *count)
{
    uint32_t n = 0;

    if ((tbl == NULL) || (count == NULL)) {
        return -1;
    }
    /* F190 VIN：17 字节 ASCII，扩展会话 + 安全等级 1 可写 */
    did_add(&tbl[n++], 0xF190u, (const uint8_t *) "HPM5E31TESTVIN001", 17u,
            APP_UDS_SESS_ALL, APP_UDS_SESS_EXT, 1u);
    /* F195 软件版本：12 字节 ASCII */
    did_add(&tbl[n++], 0xF195u, (const uint8_t *) "APP-V1.02.03", 12u, APP_UDS_SESS_ALL, 0u, 0u);
    /* F187 零件号：11 字节 ASCII */
    did_add(&tbl[n++], 0xF187u, (const uint8_t *) "5E31-1A0001", 11u, APP_UDS_SESS_ALL, 0u, 0u);
    /* F18C 序列号：13 字节 ASCII */
    did_add(&tbl[n++], 0xF18Cu, (const uint8_t *) "SN20261030001", 13u, APP_UDS_SESS_ALL, 0u, 0u);
    /* F191 硬件版本：15 字节 ASCII */
    did_add(&tbl[n++], 0xF191u, (const uint8_t *) "HPM5E31_1-REV.A", 15u, APP_UDS_SESS_ALL, 0u, 0u);
    /* F010 演示用输出控制（0x2F）：1 字节，仅扩展会话可读写 */
    {
        static const uint8_t ctrl0 = 0x00u;
        did_add(&tbl[n++], 0xF010u, &ctrl0, 1u, APP_UDS_SESS_ALL, APP_UDS_SESS_EXT, 0u);
    }
    *count = n;
    return 0;
}

static app_uds_did_t *did_find(app_uds_t *u, uint16_t did)
{
    for (uint32_t i = 0; i < u->cfg.did_count; i++) {
        if (u->cfg.dids[i].did == did) {
            return &u->cfg.dids[i];
        }
    }
    return NULL;
}

static app_uds_routine_t *routine_find(app_uds_t *u, uint16_t rid)
{
    for (uint32_t i = 0; i < u->cfg.routine_count; i++) {
        if (u->cfg.routines[i].rid == rid) {
            return &u->cfg.routines[i];
        }
    }
    return NULL;
}

/* ---------------- 初始化 ---------------- */

int app_uds_init(app_uds_t *u, const app_uds_cfg_t *cfg)
{
    if ((u == NULL) || (cfg == NULL)) {
        return -1;
    }
    memset(u, 0, sizeof(*u));
    u->cfg = *cfg;
    if (u->cfg.p2_ms == 0u) { u->cfg.p2_ms = APP_UDS_P2_MS; }
    if (u->cfg.p2star_ms == 0u) { u->cfg.p2star_ms = APP_UDS_P2STAR_MS; }
    if (u->cfg.s3_ms == 0u) { u->cfg.s3_ms = APP_UDS_S3_MS; }
    u->session = APP_UDS_SESSION_DEFAULT;
    u->security = APP_UDS_SEC_LOCKED;
    u->dtc_setting_on = true;
    u->comm_mode = 0u;
    /* 注意：DTC 内存由调用者在 init 之前装填（app_uds_dtc_init + app_uds_dtc_set），
     * 这里不能 memset，否则会把已经装载的故障码清掉（第一版就踩了这个坑）。 */
    return 0;
}

/* ---------------- 0x10 会话控制 ---------------- */

static int svc_session(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[6];
    uint8_t sub;
    uint16_t p2star;

    if (len < 2u) {
        return uds_nrc(u, 0x10u, APP_UDS_NRC_IMLOIF);
    }
    sub = (uint8_t) (req[1] & 0x7Fu);
    if ((sub < 0x01u) || (sub > 0x03u)) {
        return uds_nrc(u, 0x10u, APP_UDS_NRC_SFNS);
    }
    u->session = (app_uds_session_t) sub;
    /* 会话切换：安全等级回到锁定，种子作废（ISO 14229-1 §10.2） */
    u->security = APP_UDS_SEC_LOCKED;
    u->seed_pending = 0u;
    p2star = (uint16_t) (u->cfg.p2star_ms / 10u);
    resp[0] = 0x50u;
    resp[1] = sub;
    resp[2] = (uint8_t) (u->cfg.p2_ms >> 8);
    resp[3] = (uint8_t) (u->cfg.p2_ms & 0xFFu);
    resp[4] = (uint8_t) (p2star >> 8);
    resp[5] = (uint8_t) (p2star & 0xFFu);
    u->stat_resp_pos++;
    return uds_tx(u, resp, 6u);
}

/* ---------------- 0x11 ECU 复位 ---------------- */

static int svc_reset(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[2];
    uint8_t sub;
    uint64_t now = uds_now(u);

    if (len < 2u) {
        return uds_nrc(u, 0x11u, APP_UDS_NRC_IMLOIF);
    }
    sub = (uint8_t) (req[1] & 0x7Fu);
    if ((sub < 0x01u) || (sub > 0x03u)) {
        return uds_nrc(u, 0x11u, APP_UDS_NRC_SFNS);
    }
    resp[0] = 0x51u;
    resp[1] = sub;
    u->stat_resp_pos++;
    (void) uds_tx(u, resp, 2u);
    /* 留 200 ms 让响应真的发完再复位 */
    u->reset_pending = true;
    u->reset_at_us = now + 200000u;
    return 0;
}

/* ---------------- 0x14 清故障码 ---------------- */

static int svc_clear_dtc(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[1];
    uint32_t group;

    if (len != 4u) {
        return uds_nrc(u, 0x14u, APP_UDS_NRC_IMLOIF);
    }
    group = ((uint32_t) req[1] << 16) | ((uint32_t) req[2] << 8) | req[3];
    if (group != 0xFFFFFFu) {
        return uds_nrc(u, 0x14u, APP_UDS_NRC_ROOR);
    }
    if (u->cfg.dtc != NULL) {
        app_uds_dtc_clear_all(u->cfg.dtc);
    }
    resp[0] = 0x54u;
    u->stat_resp_pos++;
    return uds_tx(u, resp, 1u);
}

/* ---------------- 0x19 读故障码 ---------------- */

static int svc_read_dtc(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[APP_UDS_MAX_RESP];
    uint32_t rlen = 0;
    uint8_t sub;
    app_uds_dtc_mem_t *m = u->cfg.dtc;

    if (m == NULL) {
        return uds_nrc(u, 0x19u, APP_UDS_NRC_CNC);
    }
    if (len < 2u) {
        return uds_nrc(u, 0x19u, APP_UDS_NRC_IMLOIF);
    }
    sub = req[1];
    resp[rlen++] = 0x59u;
    resp[rlen++] = sub;
    switch (sub) {
    case 0x01u: {   /* reportNumberOfDTCByStatusMask */
        uint8_t mask;
        uint32_t cnt;
        if (len < 3u) {
            return uds_nrc(u, 0x19u, APP_UDS_NRC_IMLOIF);
        }
        mask = req[2];
        cnt = app_uds_dtc_count_by_mask(m, mask);
        resp[rlen++] = m->availability_mask;
        resp[rlen++] = 0x01u;                 /* DTCFormatIdentifier: ISO 14229-1 */
        resp[rlen++] = (uint8_t) (cnt >> 8);
        resp[rlen++] = (uint8_t) (cnt & 0xFFu);
        break;
    }
    case 0x02u: {   /* reportDTCByStatusMask */
        uint8_t mask;
        if (len < 3u) {
            return uds_nrc(u, 0x19u, APP_UDS_NRC_IMLOIF);
        }
        mask = req[2];
        resp[rlen++] = m->availability_mask;
        for (uint32_t i = 0; i < APP_UDS_DTC_MAX; i++) {
            const app_uds_dtc_entry_t *e = &m->ent[i];
            if (!e->used || ((e->status & mask) == 0u)) {
                continue;
            }
            if ((rlen + 4u) > sizeof(resp)) {
                break;
            }
            resp[rlen++] = (uint8_t) ((e->dtc >> 16) & 0xFFu);
            resp[rlen++] = (uint8_t) ((e->dtc >> 8) & 0xFFu);
            resp[rlen++] = (uint8_t) (e->dtc & 0xFFu);
            resp[rlen++] = e->status;
        }
        break;
    }
    case 0x04u: {   /* reportSnapshotRecordByDTCNumber */
        uint32_t dtc;
        uint8_t rec;
        app_uds_dtc_entry_t *e = NULL;
        if (len < 6u) {
            return uds_nrc(u, 0x19u, APP_UDS_NRC_IMLOIF);
        }
        dtc = ((uint32_t) req[2] << 16) | ((uint32_t) req[3] << 8) | req[4];
        rec = req[5];
        if ((rec != 0x01u) && (rec != 0xFFu)) {
            return uds_nrc(u, 0x19u, APP_UDS_NRC_ROOR);
        }
        (void) app_uds_dtc_find(m, dtc, &e);
        resp[rlen++] = (uint8_t) ((dtc >> 16) & 0xFFu);
        resp[rlen++] = (uint8_t) ((dtc >> 8) & 0xFFu);
        resp[rlen++] = (uint8_t) (dtc & 0xFFu);
        resp[rlen++] = m->availability_mask;
        if (e != NULL) {
            resp[rlen++] = (uint8_t) ((e->dtc >> 16) & 0xFFu);
            resp[rlen++] = (uint8_t) ((e->dtc >> 8) & 0xFFu);
            resp[rlen++] = (uint8_t) (e->dtc & 0xFFu);
            resp[rlen++] = e->status;
            resp[rlen++] = 0x01u;             /* recordNumber */
            resp[rlen++] = e->snap_cnt;
            for (uint32_t k = 0; k < e->snap_cnt; k++) {
                resp[rlen++] = (uint8_t) (e->snap[k].did >> 8);
                resp[rlen++] = (uint8_t) (e->snap[k].did & 0xFFu);
                resp[rlen++] = e->snap[k].len;
                memcpy(&resp[rlen], e->snap[k].data, e->snap[k].len);
                rlen += e->snap[k].len;
            }
        }
        break;
    }
    case 0x06u: {   /* reportExtendedDataRecordByDTCNumber */
        uint32_t dtc;
        uint8_t rec;
        app_uds_dtc_entry_t *e = NULL;
        if (len < 6u) {
            return uds_nrc(u, 0x19u, APP_UDS_NRC_IMLOIF);
        }
        dtc = ((uint32_t) req[2] << 16) | ((uint32_t) req[3] << 8) | req[4];
        rec = req[5];
        if ((rec != 0x02u) && (rec != 0xFFu)) {
            return uds_nrc(u, 0x19u, APP_UDS_NRC_ROOR);
        }
        (void) app_uds_dtc_find(m, dtc, &e);
        resp[rlen++] = (uint8_t) ((dtc >> 16) & 0xFFu);
        resp[rlen++] = (uint8_t) ((dtc >> 8) & 0xFFu);
        resp[rlen++] = (uint8_t) (dtc & 0xFFu);
        resp[rlen++] = m->availability_mask;
        if (e != NULL) {
            resp[rlen++] = (uint8_t) ((e->dtc >> 16) & 0xFFu);
            resp[rlen++] = (uint8_t) ((e->dtc >> 8) & 0xFFu);
            resp[rlen++] = (uint8_t) (e->dtc & 0xFFu);
            resp[rlen++] = e->status;
            resp[rlen++] = 0x02u;             /* recordNumber */
            resp[rlen++] = e->occ;
            resp[rlen++] = e->aging;
        }
        break;
    }
    default:
        return uds_nrc(u, 0x19u, APP_UDS_NRC_SFNS);
    }
    u->stat_resp_pos++;
    return uds_tx(u, resp, rlen);
}

/* ---------------- 0x22 读 DID ---------------- */

static int svc_read_did(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[APP_UDS_MAX_RESP];
    uint32_t rlen = 0;
    uint8_t bit = sess_bit(u->session);

    if ((len < 3u) || ((len & 1u) == 0u)) {
        return uds_nrc(u, 0x22u, APP_UDS_NRC_IMLOIF);
    }
    resp[rlen++] = 0x62u;
    for (uint32_t i = 1u; i + 1u < len; i += 2u) {
        uint16_t did = (uint16_t) (((uint16_t) req[i] << 8) | req[i + 1u]);
        app_uds_did_t *d = did_find(u, did);
        if ((d == NULL) || ((d->read_mask & bit) == 0u)) {
            return uds_nrc(u, 0x22u, APP_UDS_NRC_ROOR);
        }
        resp[rlen++] = (uint8_t) (did >> 8);
        resp[rlen++] = (uint8_t) (did & 0xFFu);
        memcpy(&resp[rlen], d->data, d->len);
        rlen += d->len;
    }
    u->stat_resp_pos++;
    return uds_tx(u, resp, rlen);
}

/* ---------------- 0x2E 写 DID ---------------- */

static int svc_write_did(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[3];
    uint16_t did;
    app_uds_did_t *d;
    uint32_t dlen;
    uint8_t bit = sess_bit(u->session);

    if (len < 4u) {
        return uds_nrc(u, 0x2Eu, APP_UDS_NRC_IMLOIF);
    }
    did = (uint16_t) (((uint16_t) req[1] << 8) | req[2]);
    dlen = len - 3u;
    d = did_find(u, did);
    if (d == NULL) {
        return uds_nrc(u, 0x2Eu, APP_UDS_NRC_ROOR);
    }
    if ((d->write_mask & bit) == 0u) {
        return uds_nrc(u, 0x2Eu, APP_UDS_NRC_SNSIAS);
    }
    if ((d->write_sec != 0u) && ((uint8_t) u->security < d->write_sec)) {
        return uds_nrc(u, 0x2Eu, APP_UDS_NRC_SAD);
    }
    if (dlen != d->len) {
        return uds_nrc(u, 0x2Eu, APP_UDS_NRC_IMLOIF);
    }
    memcpy(d->data, &req[3], dlen);
    resp[0] = 0x6Eu;
    resp[1] = (uint8_t) (did >> 8);
    resp[2] = (uint8_t) (did & 0xFFu);
    u->stat_resp_pos++;
    return uds_tx(u, resp, 3u);
}

/* ---------------- 0x27 安全访问 ---------------- */

static int svc_security(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[6];
    uint8_t sub;
    uint8_t level;
    uint64_t now = uds_now(u);

    if (len < 2u) {
        return uds_nrc(u, 0x27u, APP_UDS_NRC_IMLOIF);
    }
    sub = (uint8_t) (req[1] & 0x7Fu);
    if ((sub < 0x01u) || (sub > 0x04u)) {
        return uds_nrc(u, 0x27u, APP_UDS_NRC_SFNS);
    }
    level = (uint8_t) ((sub + 1u) / 2u);   /* 01/02 -> 1, 03/04 -> 2 */
    /* 二级解锁必须在扩展/编程会话（第 31 章表 31-6） */
    if ((level == 2u) && (u->session == APP_UDS_SESSION_DEFAULT)) {
        return uds_nrc(u, 0x27u, APP_UDS_NRC_SNSIAS);
    }
    /* 超次锁定期 */
    if (now < u->sec_lock_until_us) {
        return uds_nrc(u, 0x27u, APP_UDS_NRC_RTDNE);
    }
    if ((sub & 1u) != 0u) {
        /* 请求种子 */
        uint32_t seed;
        if ((uint8_t) u->security >= level) {
            /* 已解锁：按标准回全 0 种子 */
            memset(resp, 0, sizeof(resp));
            resp[0] = 0x67u;
            resp[1] = sub;
            u->stat_resp_pos++;
            return uds_tx(u, resp, 6u);
        }
        seed = app_uds_next_seed(u);
        u->seed_pending = level;
        resp[0] = 0x67u;
        resp[1] = sub;
        resp[2] = (uint8_t) (seed >> 24);
        resp[3] = (uint8_t) (seed >> 16);
        resp[4] = (uint8_t) (seed >> 8);
        resp[5] = (uint8_t) seed;
        u->stat_resp_pos++;
        return uds_tx(u, resp, 6u);
    }
    /* 发送密钥 */
    if (len != 6u) {
        return uds_nrc(u, 0x27u, APP_UDS_NRC_IMLOIF);
    }
    if (u->seed_pending != level) {
        return uds_nrc(u, 0x27u, APP_UDS_NRC_RSE);
    }
    {
        uint32_t key = ((uint32_t) req[2] << 24) | ((uint32_t) req[3] << 16) |
                       ((uint32_t) req[4] << 8) | (uint32_t) req[5];
        if (key == app_uds_calc_key(u->seed, level)) {
            u->security = (app_uds_sec_t) level;
            u->seed_pending = 0u;
            u->sec_attempts = 0u;
            resp[0] = 0x67u;
            resp[1] = sub;
            u->stat_resp_pos++;
            return uds_tx(u, resp, 2u);
        }
        u->seed_pending = 0u;
        u->sec_attempts++;
        if (u->sec_attempts >= 3u) {
            u->sec_lock_until_us = now + (uint64_t) APP_UDS_SEC_LOCKOUT_MS * 1000u;
            u->sec_attempts = 0u;
            return uds_nrc(u, 0x27u, APP_UDS_NRC_ENOA);
        }
        return uds_nrc(u, 0x27u, APP_UDS_NRC_IK);
    }
}

/* ---------------- 0x28 通信控制 ---------------- */

static int svc_comm_control(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[2];
    uint8_t sub;
    uint8_t type;

    if (u->session == APP_UDS_SESSION_DEFAULT) {
        return uds_nrc(u, 0x28u, APP_UDS_NRC_SNSIAS);
    }
    if (len < 3u) {
        return uds_nrc(u, 0x28u, APP_UDS_NRC_IMLOIF);
    }
    sub = (uint8_t) (req[1] & 0x7Fu);
    type = req[2];
    if (sub > 0x03u) {
        return uds_nrc(u, 0x28u, APP_UDS_NRC_SFNS);
    }
    if ((type < 0x01u) || (type > 0x03u)) {
        return uds_nrc(u, 0x28u, APP_UDS_NRC_ROOR);
    }
    u->comm_mode = sub;
    resp[0] = 0x68u;
    resp[1] = sub;
    u->stat_resp_pos++;
    return uds_tx(u, resp, 2u);
}

/* ---------------- 0x2F IO 控制 ---------------- */

static int svc_io_control(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[6];
    uint32_t rlen = 0;
    uint8_t sub;
    uint16_t did;
    app_uds_did_t *d;

    if (u->session == APP_UDS_SESSION_DEFAULT) {
        return uds_nrc(u, 0x2Fu, APP_UDS_NRC_SNSIAS);
    }
    if (len < 4u) {
        return uds_nrc(u, 0x2Fu, APP_UDS_NRC_IMLOIF);
    }
    sub = (uint8_t) (req[1] & 0x7Fu);
    did = (uint16_t) (((uint16_t) req[2] << 8) | req[3]);
    if (sub > 0x03u) {
        return uds_nrc(u, 0x2Fu, APP_UDS_NRC_SFNS);
    }
    d = did_find(u, did);
    if ((d == NULL) || ((d->write_mask & sess_bit(u->session)) == 0u)) {
        return uds_nrc(u, 0x2Fu, APP_UDS_NRC_ROOR);
    }
    resp[rlen++] = 0x6Fu;
    resp[rlen++] = sub;
    resp[rlen++] = (uint8_t) (did >> 8);
    resp[rlen++] = (uint8_t) (did & 0xFFu);
    if (sub == 0x03u) {          /* shortTermAdjustment：带控制值 */
        if (len < 5u) {
            return uds_nrc(u, 0x2Fu, APP_UDS_NRC_IMLOIF);
        }
        d->data[0] = req[4];
    }
    /* 回控制状态值（controlState） */
    resp[rlen++] = d->data[0];
    u->stat_resp_pos++;
    return uds_tx(u, resp, rlen);
}

/* ---------------- 0x31 例程控制 ---------------- */

static int svc_routine(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[APP_UDS_MAX_RESP];
    uint32_t rlen = 0;
    uint8_t sub;
    uint16_t rid;
    app_uds_routine_t *r;
    uint8_t out[64];
    uint32_t out_len = 0;
    int rc = 0;

    if (len < 4u) {
        return uds_nrc(u, 0x31u, APP_UDS_NRC_IMLOIF);
    }
    sub = (uint8_t) (req[1] & 0x7Fu);
    rid = (uint16_t) (((uint16_t) req[2] << 8) | req[3]);
    r = routine_find(u, rid);
    if (r == NULL) {
        return uds_nrc(u, 0x31u, APP_UDS_NRC_ROOR);
    }
    if ((r->session_mask & sess_bit(u->session)) == 0u) {
        return uds_nrc(u, 0x31u, APP_UDS_NRC_SNSIAS);
    }
    if ((r->sec_level != 0u) && ((uint8_t) u->security < r->sec_level)) {
        return uds_nrc(u, 0x31u, APP_UDS_NRC_SAD);
    }
    memset(out, 0, sizeof(out));
    switch (sub) {
    case 0x01u:   /* start */
        if (r->state == 1u) {
            return uds_nrc(u, 0x31u, APP_UDS_NRC_CNC);
        }
        if (r->start != NULL) {
            rc = r->start(r->ctx, &req[4], len - 4u, out, &out_len);
        }
        if (rc != 0) {
            return uds_nrc(u, 0x31u, (rc > 0) ? (uint8_t) rc : APP_UDS_NRC_GPF);
        }
        r->state = 1u;
        resp[rlen++] = 0x71u;
        resp[rlen++] = 0x01u;
        break;
    case 0x02u:   /* stop */
        if (r->state != 1u) {
            return uds_nrc(u, 0x31u, APP_UDS_NRC_RSE);
        }
        if (r->stop != NULL) {
            rc = r->stop(r->ctx, &req[4], len - 4u, out, &out_len);
        }
        r->state = 2u;
        resp[rlen++] = 0x71u;
        resp[rlen++] = 0x02u;
        break;
    case 0x03u:   /* requestResults */
        if (r->state == 0u) {
            return uds_nrc(u, 0x31u, APP_UDS_NRC_RSE);
        }
        if (r->results != NULL) {
            rc = r->results(r->ctx, out, &out_len);
        }
        if (rc != 0) {
            return uds_nrc(u, 0x31u, APP_UDS_NRC_GPF);
        }
        resp[rlen++] = 0x71u;
        resp[rlen++] = 0x03u;
        break;
    default:
        return uds_nrc(u, 0x31u, APP_UDS_NRC_SFNS);
    }
    resp[rlen++] = (uint8_t) (rid >> 8);
    resp[rlen++] = (uint8_t) (rid & 0xFFu);
    if ((out_len > 0u) && ((rlen + out_len) <= sizeof(resp))) {
        memcpy(&resp[rlen], out, out_len);
        rlen += out_len;
    }
    u->stat_resp_pos++;
    return uds_tx(u, resp, rlen);
}

/* ---------------- 0x34/0x36/0x37 刷写 ---------------- */

static uint32_t be_parse(const uint8_t *p, uint8_t n)
{
    uint32_t v = 0;
    for (uint8_t i = 0; i < n; i++) {
        v = (v << 8) | p[i];
    }
    return v;
}

static int svc_request_download(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[5];
    uint8_t dfi;
    uint8_t alfi;
    uint8_t addr_len;
    uint8_t size_len;
    uint32_t addr;
    uint32_t size;
    uint32_t win_s = 0;
    uint32_t win_e = 0;

    if (u->session != APP_UDS_SESSION_PROG) {
        return uds_nrc(u, 0x34u, APP_UDS_NRC_SNSIAS);
    }
    if ((uint8_t) u->security < 1u) {
        return uds_nrc(u, 0x34u, APP_UDS_NRC_SAD);
    }
    if (u->cfg.flash == NULL) {
        return uds_nrc(u, 0x34u, APP_UDS_NRC_CNC);
    }
    if (len < 4u) {
        return uds_nrc(u, 0x34u, APP_UDS_NRC_IMLOIF);
    }
    dfi = req[1];
    alfi = req[2];
    addr_len = (uint8_t) (alfi & 0x0Fu);
    size_len = (uint8_t) ((alfi >> 4) & 0x0Fu);
    if ((addr_len < 2u) || (addr_len > 4u) || (size_len < 2u) || (size_len > 4u)) {
        return uds_nrc(u, 0x34u, APP_UDS_NRC_IMLOIF);
    }
    if (len != (uint32_t) (3u + addr_len + size_len)) {
        return uds_nrc(u, 0x34u, APP_UDS_NRC_IMLOIF);
    }
    if (dfi != 0x00u) {   /* 不支持压缩/加密 */
        return uds_nrc(u, 0x34u, APP_UDS_NRC_ROOR);
    }
    addr = be_parse(&req[3], addr_len);
    size = be_parse(&req[3u + addr_len], size_len);
    app_uds_flash_window(&win_s, &win_e);
    if ((size == 0u) || (addr < win_s) || ((addr + size) > win_e)) {
        return uds_nrc(u, 0x34u, APP_UDS_NRC_ROOR);
    }
    /* 擦除可能超过 P2(50 ms)：先发 0x78，再擦 */
    (void) app_uds_send_pending(u, 0x34u);
    if (u->cfg.flash->begin(u->cfg.flash->ctx, addr, size) != 0) {
        return uds_nrc(u, 0x34u, APP_UDS_NRC_GPF);
    }
    u->dl_active = true;
    u->dl_addr = addr;
    u->dl_size = size;
    u->dl_got = 0u;
    u->dl_seq_next = 1u;
    u->dl_block_max = (u->cfg.flash->max_block != NULL) ? u->cfg.flash->max_block(u->cfg.flash->ctx) : 258u;
    resp[0] = 0x74u;
    resp[1] = 0x20u;                                  /* lengthFormatIdentifier: 2 字节 */
    resp[2] = (uint8_t) (u->dl_block_max >> 8);
    resp[3] = (uint8_t) (u->dl_block_max & 0xFFu);
    u->stat_resp_pos++;
    return uds_tx(u, resp, 4u);
}

static int svc_transfer_data(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[2];
    uint8_t seq;
    uint32_t dlen;

    if (!u->dl_active) {
        return uds_nrc(u, 0x36u, APP_UDS_NRC_RSE);
    }
    if (len < 2u) {
        return uds_nrc(u, 0x36u, APP_UDS_NRC_IMLOIF);
    }
    seq = req[1];
    if (seq != u->dl_seq_next) {
        return uds_nrc(u, 0x36u, APP_UDS_NRC_WBSC);
    }
    dlen = len - 2u;
    if ((dlen == 0u) || ((u->dl_got + dlen) > u->dl_size)) {
        return uds_nrc(u, 0x36u, APP_UDS_NRC_IMLOIF);
    }
    if (u->cfg.flash->write(u->cfg.flash->ctx, u->dl_addr + u->dl_got, &req[2], dlen) != 0) {
        return uds_nrc(u, 0x36u, APP_UDS_NRC_GPF);
    }
    u->dl_got += dlen;
    u->dl_seq_next++;
    resp[0] = 0x76u;
    resp[1] = seq;
    u->stat_resp_pos++;
    return uds_tx(u, resp, 2u);
}

static int svc_transfer_exit(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[3];
    uint32_t rlen = 0;

    (void) req;
    if (!u->dl_active) {
        return uds_nrc(u, 0x37u, APP_UDS_NRC_RSE);
    }
    if ((len != 1u) && (len != 2u)) {
        return uds_nrc(u, 0x37u, APP_UDS_NRC_IMLOIF);
    }
    if ((u->cfg.flash->finish != NULL) && (u->cfg.flash->finish(u->cfg.flash->ctx) != 0)) {
        u->dl_active = false;
        return uds_nrc(u, 0x37u, APP_UDS_NRC_GPF);
    }
    u->dl_active = false;
    resp[rlen++] = 0x77u;
    if (len == 2u) {
        resp[rlen++] = req[1];   /* 回显 transferRequestParameterRecord 前的参数（示例仅 1 字节） */
    }
    u->stat_resp_pos++;
    return uds_tx(u, resp, rlen);
}

/* ---------------- 0x3E / 0x85 ---------------- */

static int svc_tester_present(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[2];
    uint8_t sub;

    if (len != 2u) {
        return uds_nrc(u, 0x3Eu, APP_UDS_NRC_IMLOIF);
    }
    sub = (uint8_t) (req[1] & 0x7Fu);
    if (sub != 0x00u) {
        return uds_nrc(u, 0x3Eu, APP_UDS_NRC_SFNS);
    }
    resp[0] = 0x7Eu;
    resp[1] = 0x00u;
    u->stat_resp_pos++;
    return uds_tx(u, resp, 2u);
}

static int svc_dtc_setting(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t resp[2];
    uint8_t sub;

    if (u->session == APP_UDS_SESSION_DEFAULT) {
        return uds_nrc(u, 0x85u, APP_UDS_NRC_SNSIAS);
    }
    if (len < 2u) {
        return uds_nrc(u, 0x85u, APP_UDS_NRC_IMLOIF);
    }
    sub = (uint8_t) (req[1] & 0x7Fu);
    if (sub == 0x01u) {
        u->dtc_setting_on = true;
    } else if (sub == 0x02u) {
        u->dtc_setting_on = false;
    } else {
        return uds_nrc(u, 0x85u, APP_UDS_NRC_SFNS);
    }
    resp[0] = 0xC5u;
    resp[1] = sub;
    u->stat_resp_pos++;
    return uds_tx(u, resp, 2u);
}

/* ---------------- 分发 ---------------- */

int app_uds_handle(app_uds_t *u, const uint8_t *req, uint32_t len)
{
    uint8_t sid;
    uint8_t sub_suppress = 0u;
    uint64_t now;

    if ((u == NULL) || (req == NULL) || (len == 0u) || (len > APP_UDS_MAX_REQ)) {
        return -1;
    }
    now = uds_now(u);
    u->stat_req++;
    u->last_req_us = now;
    u->suppress_pos = false;
    u->s3_deadline_us = now + (uint64_t) u->cfg.s3_ms * 1000u;
    sid = req[0];
    if ((sid == 0x10u) || (sid == 0x11u) || (sid == 0x27u) || (sid == 0x28u) ||
        (sid == 0x2Fu) || (sid == 0x31u) || (sid == 0x3Eu) || (sid == 0x85u)) {
        if (len >= 2u) {
            sub_suppress = (uint8_t) (req[1] & 0x80u);
        }
    }
    switch (sid) {
    case 0x10u: (void) svc_session(u, req, len); break;
    case 0x11u: (void) svc_reset(u, req, len); break;
    case 0x14u: (void) svc_clear_dtc(u, req, len); break;
    case 0x19u: (void) svc_read_dtc(u, req, len); break;
    case 0x22u: (void) svc_read_did(u, req, len); break;
    case 0x27u: (void) svc_security(u, req, len); break;
    case 0x28u: (void) svc_comm_control(u, req, len); break;
    case 0x2Eu: (void) svc_write_did(u, req, len); break;
    case 0x2Fu: (void) svc_io_control(u, req, len); break;
    case 0x31u: (void) svc_routine(u, req, len); break;
    case 0x34u: (void) svc_request_download(u, req, len); break;
    case 0x36u: (void) svc_transfer_data(u, req, len); break;
    case 0x37u: (void) svc_transfer_exit(u, req, len); break;
    case 0x3Eu: (void) svc_tester_present(u, req, len); break;
    case 0x85u: (void) svc_dtc_setting(u, req, len); break;
    case 0x01u: case 0x02u: case 0x03u: case 0x04u: case 0x09u:
        if (u->cfg.obd != NULL) {
            uint8_t resp[APP_UDS_MAX_RESP];
            uint32_t rlen = APP_UDS_MAX_RESP;
            int rc = u->cfg.obd(u->cfg.obd_ctx, req, len, resp, &rlen);
            if (rc == 0) {
                u->stat_resp_pos++;
                (void) uds_tx(u, resp, rlen);
            } else if (rc > 0) {
                (void) uds_nrc(u, sid, (uint8_t) rc);
            }
            break;
        }
        (void) uds_nrc(u, sid, APP_UDS_NRC_SNS);
        break;
    default:
        (void) uds_nrc(u, sid, APP_UDS_NRC_SNS);
        break;
    }
    u->suppress_pos = (sub_suppress != 0u);
    return 0;
}

void app_uds_poll(app_uds_t *u, uint64_t now_us)
{
    if (u == NULL) {
        return;
    }
    u->now_cache_us = now_us;
    /* S3：非默认会话 5 s 内没有请求 -> 回默认会话并重新锁安全访问 */
    if ((u->session != APP_UDS_SESSION_DEFAULT) && (now_us > u->s3_deadline_us)) {
        u->session = APP_UDS_SESSION_DEFAULT;
        u->security = APP_UDS_SEC_LOCKED;
        u->seed_pending = 0u;
        u->dl_active = false;
        u->stat_s3_timeout++;
    }
    if (u->reset_pending && (now_us >= u->reset_at_us)) {
        u->reset_pending = false;
        if (u->cfg.reset != NULL) {
            u->cfg.reset(u->cfg.reset_ctx);
        }
    }
}
