/*
 * j1939_stack.c - J1939 节点协议栈：初始化、地址声明（J1939-81）、请求/应答、
 *                 周期 PGN 调度、收发分发
 *
 * 地址声明状态机（J1939-81 §4.3）：
 *
 *   UNCLAIMED --(上电/复位)--> 发 Request(address claimed, SA=0xFE)
 *        |
 *        v  250 ms 等待期
 *      WAIT --(无冲突)--> 发 Address Claimed(NAME) --> CLAIMED
 *        |--(收到同 SA 且 NAME 更小)--> 换地址并重新请求（任意地址能力）
 *        |                          \--> 固定地址：发 SA=0xFE 的 cannot claim
 *
 * 说明：本实现把"请求地址声明"的等待时间做成常量 250 ms（J1939-81 规定
 * 250 ms~2.5 s，本书取最小值以便测试仪快速判定）。
 */
#include "app_j1939.h"
#include "j1939_internal.h"

#define J1939_ADDR_CLAIM_WAIT_MS (250U)
#define J1939_NULL_ADDR          (0xFEU)
#define J1939_GLOBAL_ADDR        (0xFFU)

static uint32_t le24(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16);
}

static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0ULL;
    for (uint8_t i = 0U; i < 8U; i++) {
        v |= ((uint64_t) p[i]) << (8U * i);
    }
    return v;
}

static j1939_sched_t *find_sched(j1939_stack_t *s, uint32_t pgn)
{
    for (uint8_t i = 0U; i < s->sched_count; i++) {
        if (s->sched[i].pgn == pgn) {
            return &s->sched[i];
        }
    }
    return NULL;
}

/* ---------------- 地址声明 ---------------- */

void j1939_send_address_claim(j1939_stack_t *s, uint8_t da)
{
    j1939_frame_t f;
    f.id = j1939_id_from_pgn(J1939_PGN_ADDRESS_CLAIMED, 6U, da, s->sa);
    f.len = 8U;
    j1939_name_to_bytes(s->cfg.name, f.data);
    if (j1939_bus_send(s->bus, &f) == 0) {
        s->stats.addr_claims_tx++;
        s->stats.tx_frames++;
    } else {
        s->stats.tx_errors++;
    }
}

/* 未声明前用空地址 0xFE 广播"请求地址声明"（PGN 0xEA00，数据=0xEE00） */
static void claim_send_request(j1939_stack_t *s)
{
    j1939_frame_t f;
    f.id = j1939_id_from_pgn(J1939_PGN_REQUEST, 6U, J1939_GLOBAL_ADDR, J1939_NULL_ADDR);
    f.len = 3U;
    f.data[0] = (uint8_t) (J1939_PGN_ADDRESS_CLAIMED & 0xFFU);
    f.data[1] = (uint8_t) ((J1939_PGN_ADDRESS_CLAIMED >> 8) & 0xFFU);
    f.data[2] = (uint8_t) ((J1939_PGN_ADDRESS_CLAIMED >> 16) & 0xFFU);
    if (j1939_bus_send(s->bus, &f) == 0) {
        s->stats.tx_frames++;
    } else {
        s->stats.tx_errors++;
    }
}

static void claim_restart(j1939_stack_t *s, uint32_t now)
{
    s->addr_state = J1939_ADDR_WAIT;
    s->addr_conflict = 0U;
    claim_send_request(s);
    s->addr_timer_ms = now + J1939_ADDR_CLAIM_WAIT_MS;
}

/* 任意地址能力节点冲突后顺序换址（0x80..0xF7，跳过测试仪常用 0xF9） */
static void claim_choose_next(j1939_stack_t *s)
{
    uint8_t sa = s->sa;
    for (uint8_t i = 0U; i < 8U; i++) {
        sa = (uint8_t) (sa + 1U);
        if (sa >= 0xF8U) {
            sa = 0x80U;
        }
        if (sa != s->cfg.preferred_sa) {
            break;
        }
    }
    s->sa = sa;
    /* 换址后必须把"期望地址"一起改掉：等待期结束的声明成功分支用的是 cfg.preferred_sa，
       否则会把刚选好的新地址又写回原地址（2026-11-02 地址冲突实测发现）。 */
    s->cfg.preferred_sa = sa;
}

static void handle_address_claim(j1939_stack_t *s, const j1939_frame_t *f)
{
    if (f->len < 8U) {
        return;
    }
    uint64_t name = le64(f->data);
    uint8_t claim_sa = J1939_ID_SA(f->id);

    s->stats.addr_claims_rx++;
    if ((claim_sa != s->sa) || (name == s->cfg.name)) {
        return; /* 不是同地址冲突：交给上层回调记录即可 */
    }
    if (name >= s->cfg.name) {
        return; /* 我们的 NAME 更小，对方必须让出地址 */
    }

    if (s->addr_state == J1939_ADDR_WAIT) {
        s->addr_conflict = 1U; /* 等待期结束再处理，避免抖动 */
    } else if (s->addr_state == J1939_ADDR_CLAIMED) {
        if ((s->cfg.name & 0x01ULL) != 0ULL) {
            claim_choose_next(s);
            j1939_send_address_claim(s, J1939_GLOBAL_ADDR);
        } else {
            s->sa = J1939_NULL_ADDR;
            s->addr_state = J1939_ADDR_CANNOT_CLAIM;
            j1939_send_address_claim(s, J1939_GLOBAL_ADDR);
        }
    }
}

/* ---------------- 请求 / 应答 ---------------- */

static void handle_request(j1939_stack_t *s, const j1939_frame_t *f)
{
    s->stats.req_rx++;
    if (f->len < 3U) {
        return;
    }
    uint32_t req_pgn = le24(f->data);
    uint8_t requester = J1939_ID_SA(f->id);
    uint32_t t_rx_us = f->ts_us;

    if (req_pgn == J1939_PGN_ADDRESS_CLAIMED) {
        j1939_send_address_claim(s, requester);
    } else if (req_pgn == J1939_PGN_DM2) {
        uint16_t n = j1939_dm2_build(s, s->scratch);
        if (n > 0U) {
            (void) j1939_send(s, J1939_PGN_DM2, requester, 0U, s->scratch, n);
        }
    } else if (req_pgn == J1939_PGN_DM3) {
        j1939_dm_clear_history(s);
        s->stats.dm_clears++;
    } else if (req_pgn == J1939_PGN_DM11) {
        j1939_dm_clear_active(s);
        s->stats.dm_clears++;
    } else {
        j1939_sched_t *e = find_sched(s, req_pgn);
        if (e != NULL) {
            uint16_t n = 0U;
            e->fill(s, s->scratch, &n, e->user);
            if (n > 0U) {
                (void) j1939_send(s, req_pgn, requester, 0U, s->scratch, n);
            }
        } else {
            return; /* 未实现的 PGN：J1939 允许不应答 */
        }
    }

    s->stats.req_answered++;
    uint32_t dt = j1939_micros() - t_rx_us;
    if ((s->stats.req_rsp_n == 0U) || (dt < s->stats.req_rsp_min_us)) {
        s->stats.req_rsp_min_us = dt;
    }
    if (dt > s->stats.req_rsp_max_us) {
        s->stats.req_rsp_max_us = dt;
    }
    s->stats.req_rsp_sum_us += dt;
    s->stats.req_rsp_n++;
}

/* ---------------- 收发 ---------------- */

void j1939_deliver(j1939_stack_t *s, uint32_t pgn, uint8_t sa, uint8_t da,
                   const uint8_t *data, uint16_t len)
{
    switch (pgn) {
    case J1939_PGN_DM3:
        j1939_dm_clear_history(s);
        s->stats.dm_clears++;
        break;
    case J1939_PGN_DM11:
        j1939_dm_clear_active(s);
        s->stats.dm_clears++;
        break;
    default:
        break;
    }
    if (s->rx_cb != NULL) {
        s->rx_cb(s, pgn, sa, da, data, len, s->rx_user);
    }
}

static void j1939_rx_frame(j1939_stack_t *s, const j1939_frame_t *f)
{
    uint32_t id = f->id;
    uint32_t pgn = j1939_pgn_from_id(id);
    uint8_t sa = J1939_ID_SA(id);
    uint8_t da = j1939_is_pdu1(pgn) ? (uint8_t) J1939_ID_PS(id) : J1939_GLOBAL_ADDR;

    s->stats.rx_frames++;

    if ((pgn == J1939_PGN_TP_CM) || (pgn == J1939_PGN_TP_DT)) {
        (void) j1939_tp_rx_handle(s, f);
        return;
    }
    if (pgn == J1939_PGN_ADDRESS_CLAIMED) {
        handle_address_claim(s, f);
    } else if (pgn == J1939_PGN_REQUEST) {
        handle_request(s, f);
    }
    j1939_deliver(s, pgn, sa, da, f->data, f->len);
}

/* ---------------- 公共 API ---------------- */

void j1939_set_rx_cb(j1939_stack_t *s, j1939_rx_cb_t cb, void *user)
{
    s->rx_cb = cb;
    s->rx_user = user;
}

void j1939_init(j1939_stack_t *s, j1939_bus_t *bus, const j1939_cfg_t *cfg)
{
    __builtin_memset(s, 0, sizeof(*s));
    s->bus = bus;
    s->cfg = *cfg;
    if (s->cfg.bam_gap_ms == 0U) {
        s->cfg.bam_gap_ms = J1939_BAM_GAP_MS;
    }
    if (s->cfg.cts_gap_ms == 0U) {
        s->cfg.cts_gap_ms = 1U;
    }
    s->addr_state = J1939_ADDR_UNCLAIMED;
    if (cfg->auto_claim) {
        s->sa = J1939_NULL_ADDR; /* 声明完成前只能用空地址发"请求" */
        claim_restart(s, j1939_millis());
    } else {
        s->sa = cfg->preferred_sa;
        s->addr_state = J1939_ADDR_CLAIMED;
    }
}

int j1939_send(j1939_stack_t *s, uint32_t pgn, uint8_t da, uint8_t prio,
               const void *data, uint16_t len)
{
    uint8_t p;
    if ((data == NULL) || (len == 0U)) {
        return -1;
    }
    p = (prio != 0U) ? prio : j1939_default_priority(pgn);
    if (len <= 8U) {
        j1939_frame_t f;
        f.id = j1939_id_from_pgn(pgn, p, da, s->sa);
        f.len = (uint8_t) len;
        f.ts_us = 0U;
        for (uint8_t i = 0U; i < len; i++) {
            f.data[i] = ((const uint8_t *) data)[i];
        }
        int rc = j1939_bus_send(s->bus, &f);
        if (rc == 0) {
            s->stats.tx_frames++;
        } else {
            s->stats.tx_errors++;
        }
        return rc;
    }
    return j1939_tp_send(s, pgn, da, p, data, len);
}

int j1939_request(j1939_stack_t *s, uint32_t pgn, uint8_t da)
{
    uint8_t d[3];
    d[0] = (uint8_t) (pgn & 0xFFU);
    d[1] = (uint8_t) ((pgn >> 8) & 0xFFU);
    d[2] = (uint8_t) ((pgn >> 16) & 0xFFU);
    return j1939_send(s, J1939_PGN_REQUEST, da, 6U, d, 3U);
}

int j1939_send_ack(j1939_stack_t *s, uint32_t pgn, uint8_t da, uint8_t ack_code)
{
    uint8_t d[8];
    (void) pgn;
    d[0] = (uint8_t) (ack_code & 0x03U);
    for (uint8_t i = 1U; i < 8U; i++) {
        d[i] = 0xFFU;
    }
    return j1939_send(s, J1939_PGN_ACK, da, 6U, d, 8U);
}

int j1939_sched_add(j1939_stack_t *s, uint32_t pgn, uint16_t period_ms, uint8_t priority,
                    j1939_fill_cb_t fill, void *user)
{
    j1939_sched_t *e = find_sched(s, pgn);
    if (e == NULL) {
        if (s->sched_count >= J1939_SCHED_MAX) {
            return -1;
        }
        e = &s->sched[s->sched_count++];
        __builtin_memset(e, 0, sizeof(*e));
        e->pgn = pgn;
        e->next_ms = j1939_millis();
    }
    e->period_ms = period_ms;
    e->priority = priority;
    e->fill = fill;
    e->user = user;
    e->enabled = true;
    return 0;
}

bool j1939_sched_set_enabled(j1939_stack_t *s, uint32_t pgn, bool enabled)
{
    j1939_sched_t *e = find_sched(s, pgn);
    if (e == NULL) {
        return false;
    }
    e->enabled = enabled;
    return true;
}

bool j1939_sched_set_period(j1939_stack_t *s, uint32_t pgn, uint16_t period_ms)
{
    j1939_sched_t *e = find_sched(s, pgn);
    if (e == NULL) {
        return false;
    }
    e->period_ms = period_ms;
    e->next_ms = j1939_millis() + period_ms;
    return true;
}

uint8_t j1939_sa(const j1939_stack_t *s)
{
    return s->sa;
}

bool j1939_claimed(const j1939_stack_t *s)
{
    return s->addr_state == J1939_ADDR_CLAIMED;
}

void j1939_poll(j1939_stack_t *s)
{
    uint32_t now = j1939_millis();
    j1939_frame_t f;
    uint32_t rx_budget = 32U;

    while ((rx_budget-- > 0U) && (j1939_bus_recv(s->bus, &f) == 1)) {
        j1939_rx_frame(s, &f);
    }

    if ((s->addr_state == J1939_ADDR_WAIT) && j1939_time_reached(now, s->addr_timer_ms)) {
        if (s->addr_conflict != 0U) {
            if ((s->cfg.name & 0x01ULL) != 0ULL) {
                claim_choose_next(s);
                claim_restart(s, now); /* 换址后重新请求+等待 */
            } else {
                s->sa = J1939_NULL_ADDR;
                s->addr_state = J1939_ADDR_CANNOT_CLAIM;
                j1939_send_address_claim(s, J1939_GLOBAL_ADDR);
            }
        } else {
            s->sa = s->cfg.preferred_sa; /* 声明成功：正式启用期望地址 */
            j1939_send_address_claim(s, J1939_GLOBAL_ADDR);
            s->addr_state = J1939_ADDR_CLAIMED;
        }
    }

    bool can_tx = (s->addr_state == J1939_ADDR_CLAIMED);
    for (uint8_t i = 0U; (can_tx) && (i < s->sched_count); i++) {
        j1939_sched_t *e = &s->sched[i];
        if (!e->enabled) {
            continue;
        }
        if (j1939_time_reached(now, e->next_ms)) {
            uint16_t n = 0U;
            e->fill(s, s->scratch, &n, e->user);
            if (n > 0U) {
                (void) j1939_send(s, e->pgn, J1939_GLOBAL_ADDR, e->priority, s->scratch, n);
            }
            e->next_ms = now + e->period_ms; /* 以当前时刻为基准，避免长期漂移累积 */
            e->sent++;
        }
    }

    j1939_tp_poll(s);
}