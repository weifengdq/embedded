/*
 * j1939_tp.c - J1939-21 传输协议：BAM 广播 与 RTS/CTS 点对点（≤1785 字节）
 *
 * BAM（Broadcast Announce Message）：
 *   TP.CM 控制 0x20 -> 总字节数/总包数/PGN，随后 TP.DT 逐包广播，包间隔 ≥50 ms，
 *   接收方不回应（无流控、无确认）。
 *
 * RTS/CTS（Connection Mode）：
 *   RTS 0x10（总长/总包/PGN，byte4=对端每帧最多可收包数）
 *   CTS 0x11（总长/总包，byte4=本窗口允许包数，byte5-7=PGN）
 *   EOMA 0x13（接收完成）/ Abort 0xFF（错误码）
 *   超时：T1=750 ms（等 DT）、T2=1250 ms（等 CTS）、T3=1250 ms（包间隙）、T4=1050 ms（等 EOMA）。
 *
 * 注意：J1939-21 只对 BAM 规定了 50 ms 最小包间隔；CMDT（CTS 窗口内）本实现留
 * cts_gap_ms 可配置（默认 1 ms），以便在 250 kbit/s 总线上测出窗口背靠背的吞吐上限。
 */
#include "app_j1939.h"
#include "j1939_internal.h"

static j1939_tp_tx_t *tx_alloc(j1939_stack_t *s)
{
    for (uint8_t i = 0U; i < J1939_TP_TX_SESS; i++) {
        if (!s->tp_tx[i].active) {
            return &s->tp_tx[i];
        }
    }
    return NULL;
}

static j1939_tp_rx_t *rx_alloc(j1939_stack_t *s)
{
    for (uint8_t i = 0U; i < J1939_TP_RX_SESS; i++) {
        if (!s->tp_rx[i].active) {
            return &s->tp_rx[i];
        }
    }
    return NULL;
}

static j1939_tp_tx_t *tx_find(j1939_stack_t *s, uint32_t pgn, uint8_t da)
{
    for (uint8_t i = 0U; i < J1939_TP_TX_SESS; i++) {
        if (s->tp_tx[i].active && (s->tp_tx[i].pgn == pgn) && (s->tp_tx[i].da == da)) {
            return &s->tp_tx[i];
        }
    }
    return NULL;
}

static j1939_tp_rx_t *rx_find(j1939_stack_t *s, uint32_t pgn, uint8_t src)
{
    for (uint8_t i = 0U; i < J1939_TP_RX_SESS; i++) {
        if (s->tp_rx[i].active && (s->tp_rx[i].pgn == pgn) && (s->tp_rx[i].src == src)) {
            return &s->tp_rx[i];
        }
    }
    return NULL;
}

/* TP.CM：控制字节 + 总长(2) + 总包数 + 窗口 + PGN(3) */
static void cm_send(j1939_stack_t *s, uint8_t da, uint8_t ctrl, uint16_t total,
                    uint8_t npackets, uint8_t window, uint32_t pgn)
{
    j1939_frame_t f;
    f.id = j1939_id_from_pgn(J1939_PGN_TP_CM, 7U, da, s->sa);
    f.len = 8U;
    f.ts_us = 0U;
    f.data[0] = ctrl;
    f.data[1] = (uint8_t) (total & 0xFFU);
    f.data[2] = (uint8_t) ((total >> 8) & 0xFFU);
    f.data[3] = npackets;
    f.data[4] = window;
    f.data[5] = (uint8_t) (pgn & 0xFFU);
    f.data[6] = (uint8_t) ((pgn >> 8) & 0xFFU);
    f.data[7] = (uint8_t) ((pgn >> 16) & 0xFFU);
    if (j1939_bus_send(s->bus, &f) == 0) {
        s->stats.tx_frames++;
    } else {
        s->stats.tx_errors++;
    }
}

/* TP.DT：序号 1..255 + 7 字节数据 */
static void dt_send(j1939_stack_t *s, j1939_tp_tx_t *tx)
{
    j1939_frame_t f;
    uint8_t seq = tx->next_seq;
    uint16_t off = (uint16_t) (seq - 1U) * 7U;
    uint16_t remain = (uint16_t) (tx->total_len - off);
    uint8_t n = (remain > 7U) ? 7U : (uint8_t) remain;

    f.id = j1939_id_from_pgn(J1939_PGN_TP_DT, 7U, tx->da, s->sa);
    f.len = (uint8_t) (n + 1U);
    f.ts_us = 0U;
    f.data[0] = seq;
    for (uint8_t i = 0U; i < n; i++) {
        f.data[1U + i] = tx->buf[off + i];
    }
    if (j1939_bus_send(s->bus, &f) == 0) {
        s->stats.tx_frames++;
    } else {
        s->stats.tx_errors++;
    }
    tx->next_seq++;
    tx->sent_len = (uint16_t) (tx->sent_len + n);
}

static void tx_free(j1939_stack_t *s, j1939_tp_tx_t *tx, bool ok)
{
    if (ok) {
        s->stats.tp_done_tx++;
        s->stats.tp_bytes_tx += tx->total_len;
    } else {
        s->stats.tp_aborts++;
    }
    tx->active = false;
}

int j1939_tp_send(j1939_stack_t *s, uint32_t pgn, uint8_t da, uint8_t prio,
                  const void *data, uint16_t len)
{
    j1939_tp_tx_t *tx;
    (void) prio;
    if ((len == 0U) || (len > J1939_TP_MAX_LEN)) {
        return -1;
    }
    tx = tx_alloc(s);
    if (tx == NULL) {
        return -2;
    }
    __builtin_memset(tx, 0, sizeof(*tx));
    tx->active = true;
    tx->bam = (da == 0xFFU);
    tx->pgn = pgn;
    tx->da = da;
    tx->total_len = len;
    tx->npackets = (uint16_t) ((len + 6U) / 7U);
    tx->next_seq = 1U;
    tx->sent_len = 0U;
    tx->window = 0U;
    tx->waiting_cts = false;
    tx->started_us = j1939_micros();
    for (uint16_t i = 0U; i < len; i++) {
        tx->buf[i] = ((const uint8_t *) data)[i];
    }

    if (tx->bam) {
        s->stats.tp_bam_tx++;
        cm_send(s, 0xFFU, J1939_TP_CM_BAM, len, (uint8_t) tx->npackets, 0xFFU, pgn);
        tx->next_ms = j1939_millis(); /* 第一包立即发，之后每 bam_gap_ms 一包 */
    } else {
        s->stats.tp_rts_tx++;
        cm_send(s, da, J1939_TP_CM_RTS, len, (uint8_t) tx->npackets, 0xFFU, pgn);
        tx->waiting_cts = true;
        tx->deadline_ms = j1939_millis() + J1939_T2_MS;
    }
    return 0;
}

void j1939_tp_poll(j1939_stack_t *s)
{
    uint32_t now = j1939_millis();
    uint32_t now_us = j1939_micros();

    /* ---------- 发送侧 ---------- */
    for (uint8_t i = 0U; i < J1939_TP_TX_SESS; i++) {
        j1939_tp_tx_t *tx = &s->tp_tx[i];
        if (!tx->active) {
            continue;
        }
        if (tx->bam) {
            if (j1939_time_reached(now, tx->next_ms)) {
                dt_send(s, tx);
                if (tx->sent_len >= tx->total_len) {
                    tx_free(s, tx, true);
                } else {
                    tx->next_ms = now + s->cfg.bam_gap_ms;
                }
            }
        } else if (tx->waiting_cts) {
            if (j1939_time_reached(now, tx->deadline_ms)) {
                s->stats.tp_timeouts++;
                tx_free(s, tx, false);
            }
        } else if (tx->window > 0U) {
            if (j1939_time_reached(now, tx->next_ms)) {
                dt_send(s, tx);
                tx->window--;
                if (tx->sent_len >= tx->total_len) {
                    tx->waiting_cts = true;      /* 已发完，等 EOMA */
                    tx->deadline_ms = now + J1939_T4_MS;
                } else if (tx->window == 0U) {
                    tx->waiting_cts = true;      /* 窗口用完，等下一个 CTS */
                    tx->deadline_ms = now + J1939_T3_MS;
                } else {
                    tx->next_ms = now + s->cfg.cts_gap_ms;
                }
            }
        }
    }

    /* ---------- 接收侧超时 ---------- */
    for (uint8_t i = 0U; i < J1939_TP_RX_SESS; i++) {
        j1939_tp_rx_t *rx = &s->tp_rx[i];
        if (!rx->active) {
            continue;
        }
        if (j1939_time_reached(now, rx->deadline_ms)) {
            s->stats.tp_timeouts++;
            if (!rx->bam) {
                cm_send(s, rx->src, J1939_TP_CM_ABORT, 0U, 0U, 0U, rx->pgn);
            }
            rx->active = false;
        }
        (void) now_us;
    }
}

/* ---------------- 接收侧 ---------------- */

static void rx_complete(j1939_stack_t *s, j1939_tp_rx_t *rx)
{
    s->stats.tp_done_rx++;
    s->stats.tp_bytes_rx += rx->total_len;
    if (!rx->bam) {
        cm_send(s, rx->src, J1939_TP_CM_EOMA, rx->total_len, (uint8_t) rx->npackets,
                0xFFU, rx->pgn);
    }
    j1939_deliver(s, rx->pgn, rx->src, rx->bam ? 0xFFU : s->sa, rx->buf, rx->total_len);
    rx->active = false;
}

static void rx_start_cm(j1939_stack_t *s, const j1939_frame_t *f, bool bam)
{
    uint16_t total = (uint16_t) (f->data[1] | ((uint16_t) f->data[2] << 8));
    uint8_t npackets = f->data[3];
    uint32_t pgn = (uint32_t) f->data[5] | ((uint32_t) f->data[6] << 8) |
                   ((uint32_t) f->data[7] << 16);
    uint8_t src = J1939_ID_SA(f->id);
    j1939_tp_rx_t *rx;
    uint32_t now = j1939_millis();

    if ((total == 0U) || (total > J1939_TP_MAX_LEN) ||
        ((uint16_t) npackets != ((total + 6U) / 7U))) {
        s->stats.tp_errors++;
        if (!bam) {
            cm_send(s, src, J1939_TP_CM_ABORT, total, npackets, 0U, pgn);
        }
        return;
    }
    rx = rx_find(s, pgn, src);
    if (rx == NULL) {
        rx = rx_alloc(s);
    }
    if (rx == NULL) {
        s->stats.tp_errors++;
        if (!bam) {
            cm_send(s, src, J1939_TP_CM_ABORT, total, npackets, 0U, pgn);
        }
        return;
    }
    __builtin_memset(rx, 0, sizeof(*rx));
    rx->active = true;
    rx->bam = bam;
    rx->pgn = pgn;
    rx->src = src;
    rx->total_len = total;
    rx->npackets = npackets;
    rx->next_seq = 1U;
    rx->recv_len = 0U;
    rx->last_us = f->ts_us;
    rx->gap_min_us = 0xFFFFFFFFUL;
    rx->deadline_ms = now + (bam ? J1939_T1_MS : J1939_T1_MS);
    if (bam) {
        s->stats.tp_bam_rx++;
    } else {
        s->stats.tp_rts_rx++;
        /* CTS：窗口取 min(剩余包数, 16)，演示多次 CTS 往返 */
        uint8_t win = (npackets > 16U) ? 16U : npackets;
        if ((f->data[4] != 0U) && (f->data[4] < win)) {
            win = f->data[4];
        }
        cm_send(s, src, J1939_TP_CM_CTS, total, npackets, win, pgn);
        rx->cts_left = win;
        rx->deadline_ms = now + J1939_T3_MS;
    }
}

bool j1939_tp_rx_handle(j1939_stack_t *s, const j1939_frame_t *f)
{
    uint32_t pgn = j1939_pgn_from_id(f->id);
    uint8_t src = J1939_ID_SA(f->id);
    uint32_t now = j1939_millis();

    if (pgn == J1939_PGN_TP_CM) {
        uint8_t ctrl = f->data[0];
        uint32_t tp_pgn = (uint32_t) f->data[5] | ((uint32_t) f->data[6] << 8) |
                          ((uint32_t) f->data[7] << 16);
        if (ctrl == J1939_TP_CM_BAM) {
            rx_start_cm(s, f, true);
        } else if (ctrl == J1939_TP_CM_RTS) {
            rx_start_cm(s, f, false);
        } else if (ctrl == J1939_TP_CM_CTS) {
            j1939_tp_tx_t *tx = tx_find(s, tp_pgn, src);
            if (tx != NULL) {
                uint8_t win = f->data[4];
                tx->window = win;
                tx->waiting_cts = false;
                tx->next_ms = now;
                tx->deadline_ms = now + J1939_T3_MS;
            }
        } else if (ctrl == J1939_TP_CM_EOMA) {
            j1939_tp_tx_t *tx = tx_find(s, tp_pgn, src);
            if (tx != NULL) {
                uint32_t rtt = j1939_micros() - tx->started_us;
                if ((s->stats.eoma_rtt_n == 0U) || (rtt < s->stats.eoma_rtt_min_us)) {
                    s->stats.eoma_rtt_min_us = rtt;
                }
                if (rtt > s->stats.eoma_rtt_max_us) {
                    s->stats.eoma_rtt_max_us = rtt;
                }
                s->stats.eoma_rtt_sum_us += rtt;
                s->stats.eoma_rtt_n++;
                tx_free(s, tx, true);
            }
        } else if (ctrl == J1939_TP_CM_ABORT) {
            j1939_tp_tx_t *tx = tx_find(s, tp_pgn, src);
            j1939_tp_rx_t *rx = rx_find(s, tp_pgn, src);
            if (tx != NULL) {
                tx_free(s, tx, false);
            }
            if (rx != NULL) {
                rx->active = false;
            }
            s->stats.tp_aborts++;
        }
        return true;
    }

    if (pgn == J1939_PGN_TP_DT) {
        /* 找到与该源地址匹配的接收会话 */
        j1939_tp_rx_t *rx = NULL;
        for (uint8_t i = 0U; i < J1939_TP_RX_SESS; i++) {
            if (s->tp_rx[i].active && (s->tp_rx[i].src == src)) {
                rx = &s->tp_rx[i];
                break;
            }
        }
        if (rx == NULL) {
            s->stats.tp_errors++;
            return true;
        }
        if (f->data[0] != rx->next_seq) {
            s->stats.tp_errors++;
            if (!rx->bam) {
                cm_send(s, rx->src, J1939_TP_CM_ABORT, rx->total_len,
                        (uint8_t) rx->npackets, 0U, rx->pgn);
            }
            rx->active = false;
            return true;
        }
        {
            uint16_t off = (uint16_t) (rx->next_seq - 1U) * 7U;
            uint16_t remain = (uint16_t) (rx->total_len - off);
            uint8_t n = (remain > 7U) ? 7U : (uint8_t) remain;
            if (f->len < (uint8_t) (n + 1U)) {
                s->stats.tp_errors++;
                rx->active = false;
                return true;
            }
            for (uint8_t i = 0U; i < n; i++) {
                rx->buf[off + i] = f->data[1U + i];
            }
            rx->recv_len = (uint16_t) (rx->recv_len + n);
            rx->next_seq++;

            if (rx->recv_len > n) { /* 非首包：统计包间隔 */
                uint32_t gap = f->ts_us - rx->last_us;
                if ((rx->gap_n == 0U) || (gap < rx->gap_min_us)) {
                    rx->gap_min_us = gap;
                }
                if (gap > rx->gap_max_us) {
                    rx->gap_max_us = gap;
                }
                rx->gap_sum_us += gap;
                rx->gap_n++;
                if ((s->stats.tp_gap_n == 0U) || (gap < s->stats.tp_gap_min_us)) {
                    s->stats.tp_gap_min_us = gap;
                }
                if (gap > s->stats.tp_gap_max_us) {
                    s->stats.tp_gap_max_us = gap;
                }
                s->stats.tp_gap_sum_us += gap;
                s->stats.tp_gap_n++;
            }
            rx->last_us = f->ts_us;

            if (rx->recv_len >= rx->total_len) {
                rx_complete(s, rx);
            } else {
                rx->deadline_ms = now + (rx->bam ? J1939_T1_MS : J1939_T3_MS);
                if (!rx->bam) {
                    /* 一个 CTS 窗口收满后续发下一个 CTS（J1939-21 §5.10.2） */
                    if (rx->cts_left > 0U) {
                        rx->cts_left--;
                    }
                    if (rx->cts_left == 0U) {
                        uint16_t remain = (uint16_t) (rx->npackets - (rx->next_seq - 1U));
                        uint8_t win = (remain > 16U) ? 16U : (uint8_t) remain;
                        rx->cts_left = win;
                        cm_send(s, rx->src, J1939_TP_CM_CTS, rx->total_len,
                                (uint8_t) rx->npackets, win, rx->pgn);
                    }
                }
            }
        }
        return true;
    }
    return false;
}

void j1939_tp_reset(j1939_stack_t *s)
{
    for (uint8_t i = 0U; i < J1939_TP_TX_SESS; i++) {
        s->tp_tx[i].active = false;
    }
    for (uint8_t i = 0U; i < J1939_TP_RX_SESS; i++) {
        s->tp_rx[i].active = false;
    }
}