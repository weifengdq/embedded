/*
 * app_isotp.c - ISO 15765-2:2016 传输层实现（非阻塞状态机）
 *
 * 帧格式（2016 版，N_PCI 首字节）：
 *   SF: 0x0L        L=SF_DL(1..7)；CAN FD 下 SF_DL>7 时用 0x00 + 第二字节 SF_DL(8..62)
 *   FF: 0x1L LL     FF_DL=0x0LLL(12bit)；FF_DL==0 时后跟 4 字节 32 位长度
 *   CF: 0x2N        N=SN(1..15，0 跳过)
 *   FC: 0x3F BS ST  F=FS(0 CTS / 1 WAIT / 2 OVFLW)
 *
 * 时序参数（本实现按 ISO 15765-2 定义）：
 *   N_As 发送方：从"请求发帧"到"帧真正发到总线"的上限
 *   N_Ar 接收方：从"请求发 FC"到"FC 真正发到总线"的上限
 *   N_Bs 发送方：发出 FF/CF 块后等待 FC 的上限
 *   N_Cr 接收方：收到 FF 后等待下一个 CF 的上限
 *   本实现把 N_As/N_Ar 折进链路层（同步发送，返回即送达），只用 N_Bs/N_Cr 做超时。
 */
#include "app_isotp.h"

#include <string.h>

#define PCI_TYPE(b)  ((uint8_t) (((b) >> 4) & 0x07u))
#define PCI_SF       0x0u
#define PCI_FF       0x1u
#define PCI_CF       0x2u
#define PCI_FC       0x3u

#define FS_CTS       0x0u
#define FS_WAIT      0x1u
#define FS_OVFLW     0x2u

#define TX_IDLE      0u
#define TX_WAIT_FC   1u
#define TX_SEND_CF   2u

#define RX_IDLE      0u
#define RX_WAIT_CF   1u

#ifndef APP_ISOTP_DEFAULT_BURST
#define APP_ISOTP_DEFAULT_BURST 4u   /* 单次 poll 最多连发几个 CF（防止打爆对端 RX FIFO） */
#endif

static uint64_t tp_now(const app_isotp_t *t)
{
    return t->link.now_us(t->link.ctx);
}

static uint8_t tp_frame_max(const app_isotp_t *t)
{
    return t->cfg.fd ? 64u : 8u;
}

static uint8_t tp_hdr_len(const app_isotp_t *t)
{
    return t->cfg.ext_addr_en ? 1u : 0u;
}

/* 发送帧长度：经典固定 8；FD 按 ISO 15765-2 的合法 DLC 向上取整 */
static uint8_t tp_tx_len(const app_isotp_t *t, uint8_t len)
{
    static const uint8_t k_dlc[] = {8u, 12u, 16u, 20u, 24u, 32u, 48u, 64u};
    if (!t->cfg.tx_pad) {
        return len;
    }
    if (!t->cfg.fd) {
        return 8u;
    }
    for (uint32_t i = 0; i < sizeof(k_dlc); i++) {
        if (len <= k_dlc[i]) {
            return k_dlc[i];
        }
    }
    return 64u;
}

/* 发送一帧；buf 至少 64 字节，[0,len) 已填好，其余用 pad_byte 填 */
static int tp_send(app_isotp_t *t, uint8_t *buf, uint8_t len)
{
    app_isotp_frame_t f;
    uint8_t total = tp_tx_len(t, len);

    for (uint8_t i = len; i < total; i++) {
        buf[i] = t->cfg.pad_byte;
    }
    f.id = t->cfg.tx_id;
    f.len = total;
    f.ext = t->cfg.tx_ext;
    f.fd = t->cfg.fd;
    f.brs = t->cfg.brs;
    memcpy(f.data, buf, total);
    if (t->link.tx_frame(t->link.ctx, &f) != 0) {
        t->st.err_link++;
        return -1;
    }
    return 0;
}

uint32_t app_isotp_stmin_to_us(uint8_t stmin)
{
    if (stmin <= 0x7Fu) {
        return (uint32_t) stmin * 1000u;
    }
    if ((stmin >= 0xF1u) && (stmin <= 0xF9u)) {
        return (uint32_t) (stmin - 0xF0u) * 100u;
    }
    return 127u * 1000u;
}

uint8_t app_isotp_us_to_stmin(uint32_t us)
{
    if (us == 0u) {
        return 0u;
    }
    if (us < 1000u) {
        uint32_t code = (us + 99u) / 100u;
        if (code > 9u) {
            code = 9u;
        }
        return (uint8_t) (0xF0u + code);
    }
    if (us >= 127000u) {
        return 0x7Fu;
    }
    return (uint8_t) ((us + 999u) / 1000u);
}

int app_isotp_init(app_isotp_t *t, const app_isotp_cfg_t *cfg, const app_isotp_link_t *link,
                   uint8_t *tx_buf, uint32_t tx_cap, uint8_t *rx_buf, uint32_t rx_cap)
{
    if ((t == NULL) || (cfg == NULL) || (link == NULL) || (tx_buf == NULL) || (rx_buf == NULL)) {
        return APP_ISOTP_ERR_ARG;
    }
    memset(t, 0, sizeof(*t));
    t->cfg = *cfg;
    if (t->cfg.n_as_ms == 0u) { t->cfg.n_as_ms = 1000u; }
    if (t->cfg.n_ar_ms == 0u) { t->cfg.n_ar_ms = 1000u; }
    if (t->cfg.n_bs_ms == 0u) { t->cfg.n_bs_ms = 1000u; }
    if (t->cfg.n_cr_ms == 0u) { t->cfg.n_cr_ms = 1000u; }
    if (t->cfg.wftmax == 0u) { t->cfg.wftmax = 10u; }
    if (t->cfg.tx_pad && (t->cfg.pad_byte == 0u)) { t->cfg.pad_byte = 0xCCu; }
    t->link = *link;
    t->tx_buf = tx_buf;
    t->tx_cap = tx_cap;
    t->rx_buf = rx_buf;
    t->rx_cap = rx_cap;
    t->tx_state = TX_IDLE;
    t->rx_state = RX_IDLE;
    return APP_ISOTP_OK;
}

/* ---------------- 发送侧 ---------------- */

static void tp_tx_done(app_isotp_t *t, uint64_t now)
{
    uint32_t dur = (uint32_t) (now - t->tx_start_us);
    t->st.last_tx_us = dur;
    if (dur > t->st.max_tx_us) { t->st.max_tx_us = dur; }
    if (t->tx_len > t->st.max_tx_len) { t->st.max_tx_len = t->tx_len; }
    t->st.tx_msg++;
    t->tx_state = TX_IDLE;
}

static void tp_send_fc(app_isotp_t *t, uint8_t fs)
{
    uint8_t buf[64];
    uint8_t hdr = tp_hdr_len(t);
    uint8_t len;

    memset(buf, 0, sizeof(buf));
    if (t->cfg.ext_addr_en) {
        buf[0] = t->cfg.tx_ext_addr;
    }
    buf[hdr + 0u] = (uint8_t) ((PCI_FC << 4) | (fs & 0x0Fu));
    buf[hdr + 1u] = (uint8_t) (fs == FS_CTS ? t->cfg.bs : 0u);
    buf[hdr + 2u] = (uint8_t) (fs == FS_CTS ? t->cfg.stmin : 0u);
    len = (uint8_t) (hdr + 3u);
    t->st.fc_tx++;
    (void) tp_send(t, buf, len);
}

static int tp_start_ff(app_isotp_t *t, uint64_t now)
{
    uint8_t buf[64];
    uint8_t hdr = tp_hdr_len(t);
    uint8_t maxf = tp_frame_max(t);
    uint32_t avail;
    uint32_t hlen;

    memset(buf, 0, sizeof(buf));
    if (t->cfg.ext_addr_en) {
        buf[0] = t->cfg.tx_ext_addr;
    }
    if (t->tx_len <= APP_ISOTP_MAX_DL) {
        buf[hdr + 0u] = (uint8_t) ((PCI_FF << 4) | ((t->tx_len >> 8) & 0x0Fu));
        buf[hdr + 1u] = (uint8_t) (t->tx_len & 0xFFu);
        hlen = 2u;
    } else {
        buf[hdr + 0u] = (uint8_t) (PCI_FF << 4);
        buf[hdr + 1u] = 0x00u;
        buf[hdr + 2u] = (uint8_t) ((t->tx_len >> 24) & 0xFFu);
        buf[hdr + 3u] = (uint8_t) ((t->tx_len >> 16) & 0xFFu);
        buf[hdr + 4u] = (uint8_t) ((t->tx_len >> 8) & 0xFFu);
        buf[hdr + 5u] = (uint8_t) (t->tx_len & 0xFFu);
        hlen = 6u;
        t->st.ff_ext_cnt++;
    }
    avail = (uint32_t) maxf - hdr - hlen;
    if (avail > t->tx_len) {
        avail = t->tx_len;
    }
    memcpy(&buf[hdr + hlen], t->tx_buf, avail);
    t->tx_off = avail;
    t->tx_state = TX_WAIT_FC;
    t->tx_tmr_us = now;
    t->st.tx_frames++;
    if (tp_send(t, buf, (uint8_t) (hdr + hlen + avail)) != 0) {
        t->tx_state = TX_IDLE;
        return APP_ISOTP_ERR_LINK;
    }
    return APP_ISOTP_OK;
}

int app_isotp_send(app_isotp_t *t, const uint8_t *data, uint32_t len)
{
    uint64_t now;
    uint8_t hdr;
    uint8_t maxf;
    uint32_t limit = t->cfg.fd ? APP_ISOTP_MAX_DL_EXT : APP_ISOTP_MAX_DL;

    if (t == NULL) {
        return APP_ISOTP_ERR_ARG;
    }
    if (t->tx_state != TX_IDLE) {
        return APP_ISOTP_ERR_BUSY;
    }
    if ((data == NULL) || (len == 0u)) {
        return APP_ISOTP_ERR_ARG;
    }
    if ((len > t->tx_cap) || (len > limit)) {
        return APP_ISOTP_ERR_TOOLONG;
    }
    memcpy(t->tx_buf, data, len);
    t->tx_len = len;
    t->tx_off = 0;
    t->tx_sn = 1u;
    t->tx_wft = 0u;
    t->tx_bs_left = 0u;
    t->tx_stmin_us = 0u;
    t->cf_last_us = 0u;
    t->st.cf_cnt = 0u;
    t->st.cf_gap_sum_us = 0u;
    t->st.cf_gap_min_us = 0xFFFFFFFFu;
    t->st.cf_gap_max_us = 0u;
    now = tp_now(t);
    t->tx_start_us = now;
    hdr = tp_hdr_len(t);
    maxf = tp_frame_max(t);

    /* 单帧：经典 <=7 用 nibble 形式；FD 8..62 用转义形式 */
    if (len <= 7u) {
        uint8_t buf[64];
        memset(buf, 0, sizeof(buf));
        if (t->cfg.ext_addr_en) {
            buf[0] = t->cfg.tx_ext_addr;
        }
        buf[hdr] = (uint8_t) ((PCI_SF << 4) | (uint8_t) len);
        memcpy(&buf[hdr + 1u], t->tx_buf, len);
        t->st.tx_frames++;
        t->tx_state = TX_SEND_CF;
        t->tx_off = len;
        if (tp_send(t, buf, (uint8_t) (hdr + 1u + len)) != 0) {
            t->tx_state = TX_IDLE;
            return APP_ISOTP_ERR_LINK;
        }
        tp_tx_done(t, tp_now(t));
        return APP_ISOTP_OK;
    }
    if (t->cfg.fd && (len <= (uint32_t) (maxf - hdr - 2u))) {
        uint8_t buf[64];
        memset(buf, 0, sizeof(buf));
        if (t->cfg.ext_addr_en) {
            buf[0] = t->cfg.tx_ext_addr;
        }
        buf[hdr] = 0x00u;                    /* SF_DL = 0 -> 转义，长度在下一字节 */
        buf[hdr + 1u] = (uint8_t) len;
        memcpy(&buf[hdr + 2u], t->tx_buf, len);
        t->st.tx_frames++;
        t->tx_state = TX_SEND_CF;
        t->tx_off = len;
        if (tp_send(t, buf, (uint8_t) (hdr + 2u + len)) != 0) {
            t->tx_state = TX_IDLE;
            return APP_ISOTP_ERR_LINK;
        }
        tp_tx_done(t, tp_now(t));
        return APP_ISOTP_OK;
    }
    return tp_start_ff(t, now);
}

/* 收到 FC 后由 on_frame 调用 */
static void tp_tx_on_fc(app_isotp_t *t, const uint8_t *pci, uint64_t now)
{
    uint8_t fs = (uint8_t) (pci[0] & 0x0Fu);
    uint8_t bs = pci[1];
    uint8_t stmin = pci[2];

    if (t->tx_state != TX_WAIT_FC) {
        return;                              /* 没在等 FC：丢弃 */
    }
    t->st.fc_rx++;
    switch (fs) {
    case FS_CTS:
        t->tx_wft = 0u;
        t->tx_stmin_us = app_isotp_stmin_to_us(stmin);
        t->tx_bs_left = (bs == 0u) ? 0xFFFFu : (uint16_t) bs;
        t->tx_next_us = now;
        t->tx_state = TX_SEND_CF;
        break;
    case FS_WAIT:
        t->st.fc_wait_rx++;
        t->tx_wft++;
        t->tx_tmr_us = now;                  /* WAIT 重置 N_Bs */
        if ((t->cfg.wftmax != 0u) && (t->tx_wft > t->cfg.wftmax)) {
            t->tx_state = TX_IDLE;           /* 超过 N_WFTmax：放弃 */
            t->st.err_timeout++;
        }
        break;
    case FS_OVFLW:
    default:
        t->st.err_ovflw++;
        t->tx_state = TX_IDLE;
        break;
    }
}

/* 一次 poll 里尽量发 CF（受 STmin 与 burst 约束） */
static void tp_tx_pump(app_isotp_t *t)
{
    uint32_t burst = 0;

    while (t->tx_state == TX_SEND_CF) {
        uint64_t now = tp_now(t);
        uint8_t buf[64];
        uint8_t hdr = tp_hdr_len(t);
        uint8_t maxf = tp_frame_max(t);
        uint32_t chunk;

        if (now < t->tx_next_us) {
            break;
        }
        if (burst >= APP_ISOTP_DEFAULT_BURST) {
            break;
        }
        chunk = (uint32_t) maxf - hdr - 1u;
        if (chunk > (t->tx_len - t->tx_off)) {
            chunk = t->tx_len - t->tx_off;
        }
        uint64_t t0 = now;
        memset(buf, 0, sizeof(buf));
        if (t->cfg.ext_addr_en) {
            buf[0] = t->cfg.tx_ext_addr;
        }
        buf[hdr] = (uint8_t) ((PCI_CF << 4) | (t->tx_sn & 0x0Fu));
        memcpy(&buf[hdr + 1u], &t->tx_buf[t->tx_off], chunk);
        if (tp_send(t, buf, (uint8_t) (hdr + 1u + chunk)) != 0) {
            break;
        }
        if (t->cf_last_us != 0u) {
            uint32_t gap = (uint32_t) (t0 - t->cf_last_us);
            t->st.cf_gap_sum_us += gap;
            t->st.cf_cnt++;
            if (gap < t->st.cf_gap_min_us) { t->st.cf_gap_min_us = gap; }
            if (gap > t->st.cf_gap_max_us) { t->st.cf_gap_max_us = gap; }
        }
        t->cf_last_us = t0;
        t->st.tx_frames++;
        t->tx_off += chunk;
        t->tx_sn++;
        if (t->tx_sn > 15u) {
            t->tx_sn = 1u;
        }
        burst++;
        if (t->tx_bs_left != 0xFFFFu) {
            if (t->tx_bs_left > 0u) {
                t->tx_bs_left--;
            }
            if (t->tx_bs_left == 0u) {
                /* 本块发完，等下一个 FC */
                t->tx_state = TX_WAIT_FC;
                t->tx_tmr_us = now;
                break;
            }
        }
        t->tx_next_us = now + t->tx_stmin_us;
        if (t->tx_off >= t->tx_len) {
            tp_tx_done(t, tp_now(t));
            break;
        }
    }
}

/* ---------------- 接收侧 ---------------- */

static void tp_rx_complete(app_isotp_t *t, uint64_t now)
{
    uint32_t dur = (uint32_t) (now - t->rx_start_us);
    t->st.last_rx_us = dur;
    if (dur > t->st.max_rx_us) { t->st.max_rx_us = dur; }
    if (t->rx_len > t->st.max_rx_len) { t->st.max_rx_len = t->rx_len; }
    t->st.rx_msg++;
    t->rx_state = RX_IDLE;
    if (t->cfg.on_msg != NULL) {
        t->cfg.on_msg(t->cfg.user, t->rx_buf, t->rx_len);
    }
}

int app_isotp_on_frame(app_isotp_t *t, const app_isotp_frame_t *f)
{
    uint64_t now;
    uint8_t off = 0;
    uint8_t pci;
    uint32_t dlen;

    if ((t == NULL) || (f == NULL)) {
        return 0;
    }
    if ((f->id != t->cfg.rx_id) || (f->ext != t->cfg.rx_ext)) {
        return 0;
    }
    if (f->len == 0u) {
        return 1;
    }
    t->st.rx_frames++;
    now = tp_now(t);
    dlen = f->len;
    if (t->cfg.ext_addr_en) {
        if (f->data[0] != t->cfg.rx_ext_addr) {
            return 1;                        /* 地址不匹配：丢弃 */
        }
        off = 1u;
    }
    pci = f->data[off];
    switch (PCI_TYPE(pci)) {
    case PCI_SF: {
        uint32_t slen = (uint32_t) (pci & 0x0Fu);
        uint8_t doff = (uint8_t) (off + 1u);
        if (slen == 0u) {                    /* FD 转义形式：下一字节为 SF_DL */
            slen = f->data[off + 1u];
            doff++;
        }
        if ((dlen < doff) || (slen > (dlen - doff))) {
            t->st.err_ovflw++;
            return 1;
        }
        if (slen > t->rx_cap) {
            t->st.err_ovflw++;
            return 1;
        }
        t->rx_start_us = now;
        memcpy(t->rx_buf, &f->data[doff], slen);
        t->rx_len = slen;
        t->rx_got = slen;
        tp_rx_complete(t, now);
        return 1;
    }
    case PCI_FF: {
        uint32_t flen = (uint32_t) (((uint32_t) (pci & 0x0Fu) << 8) | f->data[off + 1u]);
        uint8_t doff = (uint8_t) (off + 2u);
        uint32_t avail;
        if (flen == 0u) {
            if (dlen < (uint32_t) (off + 6u)) {
                t->st.err_ovflw++;
                return 1;
            }
            flen = ((uint32_t) f->data[off + 2u] << 24) | ((uint32_t) f->data[off + 3u] << 16) |
                   ((uint32_t) f->data[off + 4u] << 8) | (uint32_t) f->data[off + 5u];
            doff = (uint8_t) (off + 6u);
        }
        if ((flen == 0u) || (flen > t->rx_cap)) {
            t->st.err_ovflw++;
            tp_send_fc(t, FS_OVFLW);
            t->rx_state = RX_IDLE;
            return 1;
        }
        if (flen <= APP_ISOTP_MAX_DL) {
            /* 12 bit 长度：FF_DL 不应小于 8（7 字节以内应走 SF） */
            if (flen <= 7u) {
                t->st.err_ovflw++;
                t->rx_state = RX_IDLE;
                return 1;
            }
        }
        avail = dlen - doff;
        if (avail > flen) {
            avail = flen;
        }
        memcpy(t->rx_buf, &f->data[doff], avail);
        t->rx_got = avail;
        t->rx_len = flen;
        t->rx_sn = 1u;
        t->rx_bs_cnt = 0u;
        t->rx_start_us = now;
        t->rx_tmr_us = now;
        if (t->rx_got >= t->rx_len) {
            tp_rx_complete(t, now);
        } else {
            t->rx_state = RX_WAIT_CF;
            tp_send_fc(t, FS_CTS);
        }
        return 1;
    }
    case PCI_CF: {
        uint8_t sn = (uint8_t) (pci & 0x0Fu);
        uint32_t chunk;
        if (t->rx_state != RX_WAIT_CF) {
            t->st.err_seq++;                 /* 无 FF 的孤立 CF：忽略 */
            return 1;
        }
        if (sn != t->rx_sn) {
            t->st.err_seq++;
            t->rx_state = RX_IDLE;
            return 1;
        }
        chunk = dlen - (uint32_t) (off + 1u);
        if ((off + 1u + chunk) > dlen) {
            chunk = 0;
        }
        if ((t->rx_got + chunk) > t->rx_len) {
            chunk = t->rx_len - t->rx_got;
        }
        memcpy(&t->rx_buf[t->rx_got], &f->data[off + 1u], chunk);
        t->rx_got += chunk;
        t->rx_sn++;
        if (t->rx_sn > 15u) {
            t->rx_sn = 1u;
        }
        t->rx_bs_cnt++;
        t->rx_tmr_us = now;
        if (t->rx_got >= t->rx_len) {
            tp_rx_complete(t, now);
        } else if ((t->cfg.bs != 0u) && (t->rx_bs_cnt >= t->cfg.bs)) {
            t->rx_bs_cnt = 0u;
            tp_send_fc(t, FS_CTS);
        }
        return 1;
    }
    case PCI_FC:
    default:
        if (dlen >= (uint32_t) (off + 3u)) {
            tp_tx_on_fc(t, &f->data[off], now);
        }
        return 1;
    }
}

/* ---------------- 超时与轮询 ---------------- */

void app_isotp_poll(app_isotp_t *t)
{
    uint64_t now;

    if (t == NULL) {
        return;
    }
    now = tp_now(t);
    if (t->tx_state == TX_WAIT_FC) {
        if ((now - t->tx_tmr_us) > ((uint64_t) t->cfg.n_bs_ms * 1000u)) {
            t->st.err_timeout++;
            t->tx_state = TX_IDLE;
        }
    } else if (t->tx_state == TX_SEND_CF) {
        tp_tx_pump(t);
    }
    if (t->rx_state == RX_WAIT_CF) {
        if ((now - t->rx_tmr_us) > ((uint64_t) t->cfg.n_cr_ms * 1000u)) {
            t->st.err_timeout++;
            t->rx_state = RX_IDLE;
        }
    }
}

void app_isotp_abort(app_isotp_t *t)
{
    if (t == NULL) {
        return;
    }
    t->tx_state = TX_IDLE;
    t->rx_state = RX_IDLE;
}
