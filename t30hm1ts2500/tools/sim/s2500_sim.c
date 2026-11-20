/**
 * @file  s2500_sim.c
 * @brief S2500 OA-SPI 协议模拟器 + 驱动单元测试 (纯软件, 不需硬件)
 *
 * 用途: 在没有板子的情况下验证 s2500.c 的
 *   - 控制事务 (回显/校验/读写)
 *   - 数据事务 chunk 化 (SV/DV/EV/EBO/TSC)
 *   - 接收重组 / FCS 校验剥离 / 最小帧填充 / 错误帧丢弃
 *
 * 编译: gcc -std=c11 -I../../common/s2500 s2500_sim.c ../../common/s2500/s2500.c -o s2500_sim
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "s2500.h"

/* ------------------------------------------------------------------ */
/* 模拟器状态                                                         */
/* ------------------------------------------------------------------ */
#define NMMS 13
static uint32_t g_regs[NMMS][0x10010];
static uint32_t g_status0_w1c;      /* 待清除位 */

typedef struct {
    uint8_t data[2048];
    uint16_t len;      /* 不含 FCS */
    int      fcs_bad;
} sim_frame_t;

static sim_frame_t g_txq[16];       /* 从机收帧队列 (主机->线) */
static int g_txq_head, g_txq_tail;
static sim_frame_t g_rxq[16];       /* 待发给主机的帧 (线->主机) */
static int g_rxq_head, g_rxq_tail;

static uint8_t g_cur[2048];
static uint16_t g_cur_len;
static int g_cur_active;
static int g_rx_block_pos;          /* 当前发送帧在 chunk 中的位置 */
static int g_rx_cur_active;

static uint8_t g_rx_block[64];
static int g_rx_block_valid, g_rx_block_sv, g_rx_block_ev, g_rx_block_dv;
static uint8_t g_rx_block_ebo, g_rx_block_swo;
static int g_rx_fcs_ok = 1;

static uint32_t crc32_le(const uint8_t *d, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
    }
    return ~c;
}

static uint32_t parity(uint32_t v)
{
    v >>= 1; v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return (v & 1u) ? 0u : 1u;
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v;
}

/* 模拟从机返回的一帧: 放入 RX 队列 (含 FCS) */
static void sim_push_rx(const uint8_t *frame, uint16_t len, int fcs_bad)
{
    int n = (g_rxq_tail + 1) % 16;
    if (n == g_rxq_head) return;
    memcpy(g_rxq[g_rxq_tail].data, frame, len);
    uint32_t fcs = crc32_le(frame, len);
    g_rxq[g_rxq_tail].data[len + 0] = fcs & 0xFF;
    g_rxq[g_rxq_tail].data[len + 1] = (fcs >> 8) & 0xFF;
    g_rxq[g_rxq_tail].data[len + 2] = (fcs >> 16) & 0xFF;
    g_rxq[g_rxq_tail].data[len + 3] = (fcs >> 24) & 0xFF;
    g_rxq[g_rxq_tail].len = len;
    g_rxq[g_rxq_tail].fcs_bad = fcs_bad;
    g_rxq_tail = n;
}

static int sim_pending_chunks(void)
{
    int cnt = 0, i = g_rxq_head;
    while (i != g_rxq_tail) {
        cnt += (g_rxq[i].len + 4 + 63) / 64;
        i = (i + 1) % 16;
    }
    return cnt;
}

static void sim_data_xfer(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    uint32_t hdr = rd32(tx);
    int dv = (hdr >> 21) & 1, sv = (hdr >> 20) & 1, ev = (hdr >> 14) & 1;
    uint8_t ebo = (hdr >> 8) & 0x3F;

    /* ---- 接收方向: 主机 -> 线 ---- */
    if (dv) {
        if (sv) { g_cur_len = 0; g_cur_active = 1; }
        if (g_cur_active) {
            uint32_t n = ev ? (uint32_t)ebo + 1u : 64u;
            if (g_cur_len + n <= sizeof(g_cur)) {
                memcpy(&g_cur[g_cur_len], &tx[4], n);
                g_cur_len += (uint16_t)n;
            }
            if (ev) {
                int nx = (g_txq_tail + 1) % 16;
                if (nx != g_txq_head) {
                    memcpy(g_txq[g_txq_tail].data, g_cur, g_cur_len);
                    g_txq[g_txq_tail].len = g_cur_len;
                    g_txq_tail = nx;
                }
                g_cur_active = 0;
            }
        }
    }

    /* ---- 发送方向: 线 -> 主机 ---- */
    memset(g_rx_block, 0, 64);
    g_rx_block_valid = 0;
    g_rx_block_sv = g_rx_block_ev = g_rx_block_dv = 0;
    g_rx_block_ebo = 63; g_rx_block_swo = 0;

    if (g_rxq_head != g_rxq_tail) {
        sim_frame_t *f = &g_rxq[g_rxq_head];
        g_rx_fcs_ok = !f->fcs_bad;
        uint16_t total = (uint16_t)(f->len + 4);
        if (!g_rx_cur_active) {
            g_rx_cur_active = 1;
            g_rx_block_sv = 1;
        }
        uint16_t remain = (uint16_t)(total - g_rx_block_pos);
        uint16_t n = remain > 64 ? 64 : remain;
        memcpy(g_rx_block, &f->data[g_rx_block_pos], n);
        g_rx_block_dv = 1;
        /* 真实芯片: EBO 只在 EV=1 时有效, 中间块通常写 0 */
        g_rx_block_ebo = 0;
        g_rx_block_pos += n;
        if (g_rx_block_pos >= total) {
            g_rx_block_ev = 1;
            g_rx_block_ebo = (uint8_t)(n - 1);
            g_rx_cur_active = 0;
            g_rx_block_pos = 0;
            g_rxq_head = (g_rxq_head + 1) % 16;
        }
    }

    /* 组装 RX: 负载 64B + 尾 4B */
    memcpy(rx, g_rx_block, 64);
    {
        uint32_t ftr = 0;
        if (g_rx_block_dv) {
            ftr |= 1u << 21;
            ftr |= (uint32_t)g_rx_block_sv << 20;
            ftr |= (uint32_t)g_rx_block_swo << 16;
            ftr |= (uint32_t)g_rx_block_ev << 14;
            ftr |= (uint32_t)g_rx_block_ebo << 8;
        }
        if (g_rxq_head != g_rxq_tail || 1) {
            /* FD 由当前帧的 fcs_bad 决定 */
        }
        if (g_rx_block_dv && g_rx_block_ev && !g_rx_fcs_ok) ftr |= 1u << 15;
        ftr |= (uint32_t)(sim_pending_chunks() & 0x1F) << 24;
        ftr |= (uint32_t)((g_regs[0][S2500_REG_BUFSTS] >> 8) & 0x1F) << 1;
        ftr |= parity(ftr);
        wr32(&rx[64], ftr);
    }
}

static int g_spi_fail;             /* 1 = 模拟 TC10 Sleep 期间 SPI 无响应 */
static int sim_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    (void)ctx;
    uint32_t hdr = rd32(tx);
    if (g_spi_fail) return -1;
    if (len == 12) {
        /* 控制事务 */
        memset(rx, 0, 12);
        wr32(&rx[4], hdr);                 /* 回显 */
        uint8_t mms = (hdr >> 24) & 0xF;
        uint16_t addr = (hdr >> 8) & 0xFFFF;
        int wr = (hdr >> 29) & 1;
        if (mms >= NMMS) return 0;
        if (wr) {
            uint32_t v = rd32(&tx[4]);
            g_regs[mms][addr] = v;
        } else if (mms == 0 && addr == S2500_REG_BUFSTS) {
            uint32_t v = g_regs[0][S2500_REG_BUFSTS] & 0xFF00u;
            v |= (uint32_t)(sim_pending_chunks() & 0xFF);
            wr32(&rx[8], v);
        } else {
            wr32(&rx[8], g_regs[mms][addr]);
        }
        return 0;
    }
    if (len == 68) {
        sim_data_xfer(tx, rx, len);
        return 0;
    }
    return -1;
}

static void sim_init_regs(void)
{
    memset(g_regs, 0, sizeof(g_regs));
    g_regs[0][S2500_REG_IDVER]    = 0x00000011;
    g_regs[0][S2500_REG_PHYID]    = 0x180FF411;
    g_regs[0][S2500_REG_STDCAP]   = 0x000005E3;
    g_regs[0][S2500_REG_CONFIG0]  = 0x00000006;
    g_regs[0][S2500_REG_STATUS0]  = 0x00000040;
    g_regs[0][S2500_REG_BUFSTS]   = 0x00003C00;
    g_regs[0][S2500_REG_IMSK0]    = 0x00001FBF;
    g_regs[0][S2500_REG_PHY_STATUS] = 0x00000809;
    g_regs[1][S2500_REG_MCONTROL0] = 0x00010100;
    g_regs[4][S2500_REG_PLCACTL1]  = 0x08FF;
    g_regs[4][S2500_REG_PLCATOTMR] = 0x20;
    g_regs[S2500_MMS_VENDOR][S2500_REG_MACID0] = 0x1EC0D1C3;
    g_regs[S2500_MMS_VENDOR][S2500_REG_MACID1] = 0x0053;
    g_regs[S2500_MMS_VENDOR][S2500_REG_DIOCFG0] = 0x0000C0C0;
    g_regs[S2500_MMS_VENDOR][S2500_REG_TWEAKS2] = 0x00002CC1;
    g_regs[4][S2500_REG_ONIPVER] = 0x30C0;
    g_regs[S2500_MMS_VENDOR][S2500_REG_ONICVER] = 0xA7A813D3;
}

static uint32_t g_ms = 1;
static uint32_t g_ms_offset;
static uint32_t sim_millis(void *ctx) { (void)ctx; return g_ms_offset + g_ms++; }
static void sim_delay(void *ctx, uint32_t us) { (void)ctx; (void)us; }
static void sim_log(void *ctx, const char *fmt, ...) { (void)ctx; (void)fmt; }

static const s2500_hal_t sim_hal = {
    .ctx = NULL, .spi_xfer = sim_xfer, .delay_us = sim_delay,
    .millis = sim_millis, .log = sim_log,
};

/* ------------------------------------------------------------------ */
/* 测试                                                               */
/* ------------------------------------------------------------------ */
static int g_fail;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_ctrl(void)
{
    s2500_dev_t dev;
    uint32_t v = 0;
    sim_init_regs();
    CHECK(s2500_init(&dev, &sim_hal) == S2500_OK, "init");
    CHECK(dev.mac[0] == 0x00 && dev.mac[1] == 0x53 && dev.mac[5] == 0xC3,
          "mac %02X:%02X:%02X:%02X:%02X:%02X",
          dev.mac[0], dev.mac[1], dev.mac[2], dev.mac[3], dev.mac[4], dev.mac[5]);
    CHECK(s2500_read_reg(&dev, S2500_MMS_STD, S2500_REG_IDVER, &v) == S2500_OK, "read idver");
    CHECK(v == 0x11, "idver=0x%X", v);
    CHECK(s2500_write_reg(&dev, S2500_MMS_MAC, S2500_REG_MADDRFILT0L, 0x11223344) == S2500_OK, "wr");
    v = 0;
    CHECK(s2500_read_reg(&dev, S2500_MMS_MAC, S2500_REG_MADDRFILT0L, &v) == S2500_OK, "rd");
    CHECK(v == 0x11223344, "filt=0x%X", v);
    CHECK(dev.stats.ctrl_parity_err == 0, "parity err %u", dev.stats.ctrl_parity_err);
    printf("ctrl: ok (mac=%02X:%02X:%02X:%02X:%02X:%02X)\n",
           dev.mac[0], dev.mac[1], dev.mac[2], dev.mac[3], dev.mac[4], dev.mac[5]);
}

static void test_send(void)
{
    s2500_dev_t dev;
    uint8_t frame[1600];
    int i;
    sim_init_regs();
    g_txq_head = g_txq_tail = 0;
    CHECK(s2500_init(&dev, &sim_hal) == S2500_OK, "init");
    s2500_start(&dev);

    for (int len = 42; len <= 1500; len += 487) {
        for (i = 0; i < len; i++) frame[i] = (uint8_t)(i * 7 + len);
        CHECK(s2500_send(&dev, frame, (uint16_t)len, 0) == S2500_OK, "send %d", len);
        CHECK(g_txq_head != g_txq_tail, "no frame captured len=%d", len);
        sim_frame_t *f = &g_txq[g_txq_head];
        int eff = len < 60 ? 60 : len;
        CHECK(f->len == eff, "len %d != %d", f->len, eff);
        CHECK(memcmp(f->data, frame, (size_t)len) == 0, "payload mismatch len=%d", len);
        for (i = len; i < eff; i++) CHECK(f->data[i] == 0, "pad");
        g_txq_head = (g_txq_head + 1) % 16;
    }
    CHECK(dev.stats.tx_frames == 3, "tx_frames=%u", dev.stats.tx_frames);
    printf("send: ok (%u frames, %u chunks)\n", dev.stats.tx_frames, dev.stats.tx_chunks);
}

static void test_recv(void)
{
    s2500_dev_t dev;
    uint8_t frame[1600], out[1600];
    uint16_t len = 0;
    int i;
    sim_init_regs();
    g_rxq_head = g_rxq_tail = 0;
    g_rx_cur_active = 0;
    g_rx_block_pos = 0;
    g_rx_fcs_ok = 1;
    CHECK(s2500_init(&dev, &sim_hal) == S2500_OK, "init");

    for (int flen = 60; flen <= 1500; flen += 360) {
        for (i = 0; i < flen; i++) frame[i] = (uint8_t)(i ^ flen);
        sim_push_rx(frame, (uint16_t)flen, 0);
        CHECK(s2500_recv(&dev, out, &len, NULL) == S2500_OK, "recv %d", flen);
        CHECK(len == flen, "recv len %u != %d", len, flen);
        CHECK(memcmp(out, frame, (size_t)flen) == 0, "recv payload %d", flen);
    }

    /* FCS 错误帧应被丢弃 */
    g_rx_fcs_ok = 1;
    sim_push_rx(frame, 100, 1);
    CHECK(s2500_recv(&dev, out, &len, NULL) != S2500_OK, "bad fcs accepted");
    g_rx_fcs_ok = 1;

    /* 空队列 */
    CHECK(s2500_recv(&dev, out, &len, NULL) == S2500_ERR_NO_FRAME, "empty");
    printf("recv: ok (frames=%u fcs_err=%u)\n", dev.stats.rx_frames, dev.stats.rx_fcs_err);
}

/* ------------------------------------------------------------------ */
/* 自愈看门狗: 正常不误判 / 芯片自发复位后检出 / Sleep 无响应不误判   */
/* ------------------------------------------------------------------ */
static int g_wd_id;
static int g_wd_reinit_count;

static int sim_wd_reinit(void *ctx)
{
    (void)ctx;
    /* 模拟应用层重新初始化: 恢复 PLCA 配置与 CONFIG0.SYNC */
    g_regs[S2500_MMS_PLCA][S2500_REG_PLCACTL1] =
        (8u << S2500_PLCA_NCNT_SHIFT) | (uint32_t)g_wd_id;
    g_regs[S2500_MMS_STD][S2500_REG_CONFIG0] |= S2500_CONFIG0_SYNC;
    g_wd_reinit_count++;
    return 0;
}

static void wd_advance(uint32_t ms)
{
    g_ms_offset += ms;
}

static void test_wd(void)
{
    s2500_dev_t dev;
    s2500_cfg_t cfg;
    uint32_t det;

    sim_init_regs();
    CHECK(s2500_init(&dev, &sim_hal) == S2500_OK, "init");
    memset(&cfg, 0, sizeof(cfg));
    cfg.plca_id = 2;
    cfg.plca_ncnt = 8;
    cfg.plca_totmr = 32;
    cfg.plca_en = 1;
    g_wd_id = 2;
    CHECK(s2500_configure(&dev, &cfg) == S2500_OK, "configure");
    s2500_wd_set_reinit_cb(sim_wd_reinit, NULL);
    s2500_wd_set_enabled(1);
    s2500_wd_reset();
    g_wd_reinit_count = 0;
    g_spi_fail = 0;
    wd_advance(100000);

    /* 正常: 两个周期都不应检出 */
    CHECK(s2500_wd_poll(&dev) == 0, "wd seed");
    wd_advance(S2500_WD_PERIOD_MS + 100);
    CHECK(s2500_wd_poll(&dev) == 0, "wd false positive");
    wd_advance(S2500_WD_PERIOD_MS + 100);
    CHECK(s2500_wd_poll(&dev) == 0, "wd false positive 2");
    CHECK(s2500_wd_state()->checks == 2, "checks=%u", s2500_wd_state()->checks);
    CHECK(s2500_wd_state()->detections == 0, "detections=%u", s2500_wd_state()->detections);

    /* 模拟芯片自发复位: PLCA_CTL1 回到默认 0x08FF, CONFIG0.SYNC=0 */
    g_regs[S2500_MMS_PLCA][S2500_REG_PLCACTL1] = 0x08FFu;
    g_regs[S2500_MMS_STD][S2500_REG_CONFIG0] = 0x00000006u;
    wd_advance(S2500_WD_PERIOD_MS + 100);
    CHECK(s2500_wd_poll(&dev) == 1, "wd missed reset");
    CHECK(g_wd_reinit_count == 1, "reinit=%d", g_wd_reinit_count);
    CHECK(s2500_wd_state()->detections == 1, "detections=%u", s2500_wd_state()->detections);
    CHECK(s2500_wd_state()->last_reason == S2500_WD_REASON_ID,
          "reason=%u", s2500_wd_state()->last_reason);

    /* 自愈后恢复正常, 不再重复检出 */
    det = s2500_wd_state()->detections;
    wd_advance(S2500_WD_PERIOD_MS + 100);
    CHECK(s2500_wd_poll(&dev) == 0, "wd re-detect after heal");
    CHECK(s2500_wd_state()->detections == det, "detections changed");

    /* 协调者模式 id=0 是正常值, 不能误判 */
    cfg.plca_id = 0;
    g_wd_id = 0;
    CHECK(s2500_configure(&dev, &cfg) == S2500_OK, "configure coord");
    s2500_wd_reset();
    wd_advance(100000);
    CHECK(s2500_wd_poll(&dev) == 0, "wd coord seed");
    wd_advance(S2500_WD_PERIOD_MS + 100);
    CHECK(s2500_wd_poll(&dev) == 0, "wd coord false positive");

    /* PLCA 关闭 (id=255): ID 无法区分, 由 CONFIG0.SYNC 兜底 */
    cfg.plca_id = 255;
    cfg.plca_en = 0;
    g_wd_id = 255;
    CHECK(s2500_configure(&dev, &cfg) == S2500_OK, "configure plca off");
    s2500_wd_reset();
    wd_advance(100000);
    CHECK(s2500_wd_poll(&dev) == 0, "wd plca-off seed");
    g_regs[S2500_MMS_STD][S2500_REG_CONFIG0] = 0x00000006u;   /* SYNC 丢失 */
    wd_advance(S2500_WD_PERIOD_MS + 100);
    CHECK(s2500_wd_poll(&dev) == 1, "wd missed sync loss");
    CHECK(s2500_wd_state()->last_reason == S2500_WD_REASON_SYNC,
          "reason=%u", s2500_wd_state()->last_reason);

    /* TC10 Sleep: SPI 无响应只计 spi_errors, 不判定复位 */
    s2500_wd_reset();
    wd_advance(100000);
    CHECK(s2500_wd_poll(&dev) == 0, "wd sleep seed");
    g_spi_fail = 1;
    wd_advance(S2500_WD_PERIOD_MS + 100);
    CHECK(s2500_wd_poll(&dev) == 0, "wd false positive on spi error");
    CHECK(s2500_wd_state()->spi_errors == 1, "spi_errors=%u", s2500_wd_state()->spi_errors);
    g_spi_fail = 0;

    printf("wd: ok (checks=%u detections=%u reinit=%d spi_err=%u)\n",
           s2500_wd_state()->checks, s2500_wd_state()->detections,
           g_wd_reinit_count, s2500_wd_state()->spi_errors);
}

int main(void)
{
    test_ctrl();
    test_send();
    test_recv();
    test_wd();
    if (g_fail) {
        printf("\n%d 个断言失败\n", g_fail);
        return 1;
    }
    printf("\n全部通过\n");
    return 0;
}
