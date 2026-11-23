/*
 * uds_flash - UDS 0x34/0x36/0x37 下载 64 KB 镜像到片内 Flash 空闲区 + 0x31 校验 + 0x11 复位
 *             （U-31-3：IMG_SIZE=65536，正好占满最后 64 KB 安全窗口，0x36 共 256 块）
 *
 * ECU（CAN0, 0x7E0/0x7E8）：
 *   0x10 02 编程会话 -> 0x27 01/02 解锁 -> 0x34 请求下载（擦除 32 KB）
 *   -> 256 × 0x36（每块 256 B）-> 0x37 -> 0x31 01 FF00 读回 CRC32
 *   -> 0x31 01 FF01 依赖检查 -> 0x11 01 复位
 * tester（CAN1）：板内，负责发上面的序列并逐条判定。
 *
 * 镜像布局（64 KB，从 0x800F0000 起）：
 *   [0..7]   "HPMIMG01" 魔数
 *   [8..11]  镜像长度 u32 LE（65536）
 *   [12..15] 数据区 CRC32 u32 LE
 *   [16..255] 保留（0xFF）
 *   [256..]  数据区（65280 B，xorshift 伪随机 + 偏移，便于定位错位）
 *
 * 为什么头要占满一页：NOR Flash 的 Page Program 命令要求**页对齐**；把数据区
 * 起点对齐到 0x800F0100 后，每块 256 B 都是"整页编程"，不依赖 ROM API 处理
 * 跨页 / 页内偏移（第一版起点 0x800F0010，每块都跨页，写出来数据整体错位）。
 *
 * 复位后重新上电：本程序在启动时检查 0x800F0000 的魔数，若有效则打印
 * "post-reset image present" 并跳过自测 —— 这同时验证了 0x11 01 与 Flash 掉电保持。
 */
#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include "app_isotp.h"
#include "app_isotp_port.h"
#include "app_uds.h"
#include "ext/board_can.h"

#define ID_REQ    (0x7E0u)
#define ID_RESP   (0x7E8u)
#define TP_MAX    1024u

#define IMG_ADDR   (0x800F0000UL)
#define IMG_SIZE   (65536UL)
#define IMG_HDR    256UL        /* 镜像头占满 1 个 Flash 页：之后每次 0x36 写入都是页对齐的 256 B */
#define BLOCK      256UL
#define MAGIC      "HPMIMG01"

static app_isotp_port_t s_port0;
static app_isotp_port_t s_port1;
static app_isotp_t s_srv;
static app_isotp_t s_cli;
static uint8_t s_srv_tx[TP_MAX];
static uint8_t s_srv_rx[TP_MAX];
static uint8_t s_cli_tx[TP_MAX];
static uint8_t s_cli_rx[TP_MAX];

static app_uds_t s_uds;
static app_uds_did_t s_dids[4];
static uint32_t s_did_count;
static app_uds_routine_t s_routines[2];
static uint32_t s_routine_count;

/* tester 侧 PDU 队列（同 uds_ecu：慢服务会先回 0x78） */
#define CLI_QDEPTH 8u
typedef struct {
    uint8_t  data[APP_UDS_MAX_RESP];
    uint32_t len;
} cli_pdu_t;
static cli_pdu_t s_q[CLI_QDEPTH];
static volatile uint32_t s_q_head;
static volatile uint32_t s_q_tail;
static uint32_t s_pending_seen;

static uint8_t s_img[IMG_SIZE] __attribute__((aligned(4)));
static uint8_t s_rd[256] __attribute__((aligned(4)));

static uint32_t crc32_sw(const uint8_t *p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t) (-(int32_t) (crc & 1u)));
        }
    }
    return ~crc;
}

static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

/* 例程结果里的 CRC32 用**大端**（与主机 mini_flasher.py、ecu_sim.py 一致） */
static void put_u32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

static uint32_t get_u32le(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

/* ---------------- ISO-TP 回调 ---------------- */

static void on_srv_msg(void *user, const uint8_t *data, uint32_t len)
{
    (void) user;
    (void) app_uds_handle(&s_uds, data, len);
}

static void on_cli_msg(void *user, const uint8_t *data, uint32_t len)
{
    uint32_t next = (s_q_head + 1u) % CLI_QDEPTH;
    (void) user;
    if ((next == s_q_tail) || (len > APP_UDS_MAX_RESP)) {
        return;
    }
    memcpy(s_q[s_q_head].data, data, len);
    s_q[s_q_head].len = len;
    s_q_head = next;
}

static bool cli_pop(cli_pdu_t *out)
{
    if (s_q_tail == s_q_head) {
        return false;
    }
    *out = s_q[s_q_tail];
    s_q_tail = (s_q_tail + 1u) % CLI_QDEPTH;
    return true;
}

static void pump_once(void)
{
    app_isotp_frame_t f;

    while (app_isotp_port_recv(&s_port0, &f)) {
        (void) app_isotp_on_frame(&s_srv, &f);
    }
    while (app_isotp_port_recv(&s_port1, &f)) {
        (void) app_isotp_on_frame(&s_cli, &f);
    }
    app_isotp_poll(&s_srv);
    app_isotp_poll(&s_cli);
    app_uds_poll(&s_uds, app_isotp_port_now_us());
}

static int uds_req(const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rlen,
                   uint32_t *rtt_us, uint32_t timeout_ms)
{
    uint64_t t0 = app_isotp_port_now_us();
    uint64_t limit = (uint64_t) timeout_ms * 1000u;
    cli_pdu_t pdu;
    uint32_t pend = 0;

    s_q_head = 0;
    s_q_tail = 0;
    if (app_isotp_send(&s_cli, req, len) != APP_ISOTP_OK) {
        return -1;
    }
    for (;;) {
        while (!cli_pop(&pdu)) {
            pump_once();
            if ((app_isotp_port_now_us() - t0) > limit) {
                s_pending_seen += pend;
                return -2;
            }
        }
        if ((pdu.len == 3u) && (pdu.data[0] == 0x7Fu) && (pdu.data[2] == 0x78u)) {
            pend++;
            continue;
        }
        s_pending_seen += pend;
        if (pdu.len > *rlen) {
            return -3;
        }
        memcpy(resp, pdu.data, pdu.len);
        *rlen = pdu.len;
        *rtt_us = (uint32_t) (app_isotp_port_now_us() - t0);
        return 0;
    }
}

/* ---------------- ECU 例程 ---------------- */

static int rt_check_crc(void *ctx, const uint8_t *opt, uint32_t opt_len, uint8_t *out, uint32_t *out_len)
{
    uint32_t crc = 0;
    (void) ctx;
    (void) opt;
    (void) opt_len;
    if (app_uds_flash_default()->crc32(NULL, IMG_ADDR + IMG_HDR, IMG_SIZE - IMG_HDR, &crc) != 0) {
        out[0] = 0x01u;
        *out_len = 1u;
        return 0;
    }
    out[0] = 0x00u;                       /* 0 = 校验完成 */
    put_u32be(&out[1], crc);
    *out_len = 5u;
    return 0;
}

static int rt_check_deps(void *ctx, const uint8_t *opt, uint32_t opt_len, uint8_t *out, uint32_t *out_len)
{
    const uint8_t *p = (const uint8_t *) (uintptr_t) IMG_ADDR;
    (void) ctx;
    (void) opt;
    (void) opt_len;
    if (memcmp(p, MAGIC, 8u) != 0) {
        out[0] = 0x02u;                   /* 魔数不对 */
    } else if (get_u32le(&p[8]) != IMG_SIZE) {
        out[0] = 0x03u;                   /* 长度不对 */
    } else if (get_u32le(&p[12]) != crc32_sw(&p[IMG_HDR], IMG_SIZE - IMG_HDR)) {
        out[0] = 0x04u;                   /* 头部 CRC 与数据区不一致 */
    } else {
        out[0] = 0x00u;                   /* 依赖检查通过 */
    }
    *out_len = 1u;
    return 0;
}

/* ---------------- 初始化 ---------------- */

static int bus_setup(void)
{
    app_isotp_port_cfg_t pc;
    app_isotp_cfg_t tc;
    app_isotp_link_t lk0;
    app_isotp_link_t lk1;

    board_can_transceivers_enable();
    memset(&pc, 0, sizeof(pc));
    pc.fd = false;
    pc.nominal_bitrate = 500000UL;
    pc.base = BOARD_CAN0_BASE;
    if (app_isotp_port_init(&s_port0, &pc) != APP_ISOTP_OK) {
        return -1;
    }
    pc.base = BOARD_CAN1_BASE;
    if (app_isotp_port_init(&s_port1, &pc) != APP_ISOTP_OK) {
        return -2;
    }
    lk0 = app_isotp_port_link(&s_port0);
    lk1 = app_isotp_port_link(&s_port1);

    memset(&tc, 0, sizeof(tc));
    tc.fd = false;
    tc.tx_pad = true;
    tc.pad_byte = 0xCCu;
    tc.bs = 0u;
    tc.stmin = 0u;
    tc.n_bs_ms = 1000u;
    tc.n_cr_ms = 1000u;
    tc.rx_id = ID_REQ;
    tc.tx_id = ID_RESP;
    tc.on_msg = on_srv_msg;
    if (app_isotp_init(&s_srv, &tc, &lk0, s_srv_tx, sizeof(s_srv_tx), s_srv_rx, sizeof(s_srv_rx)) != APP_ISOTP_OK) {
        return -3;
    }
    tc.rx_id = ID_RESP;
    tc.tx_id = ID_REQ;
    tc.on_msg = on_cli_msg;
    if (app_isotp_init(&s_cli, &tc, &lk1, s_cli_tx, sizeof(s_cli_tx), s_cli_rx, sizeof(s_cli_rx)) != APP_ISOTP_OK) {
        return -4;
    }
    return 0;
}

static void ecu_tx(void *ctx, const uint8_t *pdu, uint32_t len)
{
    (void) ctx;
    (void) app_isotp_send(&s_srv, pdu, len);
}

static void ecu_reset(void *ctx)
{
    (void) ctx;
    printf("[uds_flash] 0x11 01 -> 执行 MCU 复位（PPOR 软复位）\r\n");
    for (volatile int i = 0; i < 200000; i++) {
        /* 等串口 FIFO 发完 */
    }
    bsp_reboot();
}

static int ecu_setup(void)
{
    app_uds_cfg_t uc;

    (void) app_uds_default_dids(s_dids, &s_did_count);
    memset(s_routines, 0, sizeof(s_routines));
    s_routine_count = 0;
    s_routines[s_routine_count++] = (app_uds_routine_t){
        .rid = 0xFF00u, .session_mask = APP_UDS_SESS_PROG, .sec_level = 1u,
        .name = "check-crc32", .start = rt_check_crc, .results = rt_check_crc, .ctx = NULL };
    s_routines[s_routine_count++] = (app_uds_routine_t){
        .rid = 0xFF01u, .session_mask = APP_UDS_SESS_PROG, .sec_level = 1u,
        .name = "check-deps", .start = rt_check_deps, .results = rt_check_deps, .ctx = NULL };

    memset(&uc, 0, sizeof(uc));
    uc.tx = ecu_tx;
    uc.reset = ecu_reset;
    uc.dids = s_dids;
    uc.did_count = s_did_count;
    uc.routines = s_routines;
    uc.routine_count = s_routine_count;
    uc.flash = app_uds_flash_default();
    return app_uds_init(&s_uds, &uc);
}

/* ---------------- 测试仪 ---------------- */

static uint32_t s_pass;
static uint32_t s_fail;

static void chk(const char *name, bool ok, const char *extra)
{
    printf("  %-24s %s %s\r\n", name, ok ? "PASS" : "FAIL", extra ? extra : "");
    if (ok) {
        s_pass++;
    } else {
        s_fail++;
    }
}

static void build_image(void)
{
    memset(s_img, 0xFF, sizeof(s_img));
    memcpy(s_img, MAGIC, 8u);
    put_u32le(&s_img[8], IMG_SIZE);
    {
        uint32_t x = 0xC0FFEE11u;
        for (uint32_t i = IMG_HDR; i < IMG_SIZE; i++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            s_img[i] = (uint8_t) (x >> 24);
        }
    }
    put_u32le(&s_img[12], crc32_sw(&s_img[IMG_HDR], IMG_SIZE - IMG_HDR));
}

static void report_region(void)
{
    uint32_t erase_us = 0, write_us = 0, prog = 0;
    const uint8_t *p = (const uint8_t *) (uintptr_t) IMG_ADDR;
    app_uds_flash_stats(&erase_us, &write_us, &prog);
    printf("  flash: erase=%lu us write=%lu us programmed=%lu B (%.1f KB/s)\r\n",
           (unsigned long) erase_us, (unsigned long) write_us, (unsigned long) prog,
           (write_us > 0u) ? (double) prog / ((double) write_us / 1e6) / 1024.0 : 0.0);
    printf("  region@0x%08lX: magic=%.8s len=%lu crc=0x%08lX\r\n", (unsigned long) IMG_ADDR,
           (const char *) p, (unsigned long) get_u32le(&p[8]), (unsigned long) get_u32le(&p[12]));
}

static void cmd_run(int argc, char *argv[])
{
    uint8_t req[300];
    uint8_t resp[APP_UDS_MAX_RESP];
    uint32_t rlen;
    uint32_t rtt;
    uint32_t crc_tx;
    uint32_t crc_ecu = 0;
    uint64_t t_all0;
    uint32_t blocks;
    (void) argc;
    (void) argv;

    s_pass = 0;
    s_fail = 0;
    s_pending_seen = 0;
    (void) ecu_setup();
    build_image();
    crc_tx = crc32_sw(&s_img[IMG_HDR], IMG_SIZE - IMG_HDR);

    printf("\r\n[uds_flash] run: 下载 %lu B 到 0x%08lX（最后 64 KB 安全区）\r\n",
           (unsigned long) IMG_SIZE, (unsigned long) IMG_ADDR);

    /* 1) 10 02 编程会话 */
    req[0] = 0x10u; req[1] = 0x02u;
    rlen = sizeof(resp);
    bool ok = (uds_req(req, 2u, resp, &rlen, &rtt, 2000u) == 0) && (rlen >= 2u) && (resp[0] == 0x50u);
    chk("10 02 编程会话", ok, "");

    /* 2) 27 01/02 解锁（编程会话里安全等级 1） */
    req[0] = 0x27u; req[1] = 0x01u;
    rlen = sizeof(resp);
    ok = (uds_req(req, 2u, resp, &rlen, &rtt, 2000u) == 0) && (rlen == 6u) && (resp[0] == 0x67u);
    if (ok) {
        uint32_t seed = ((uint32_t) resp[2] << 24) | ((uint32_t) resp[3] << 16) |
                        ((uint32_t) resp[4] << 8) | resp[5];
        uint32_t key = app_uds_calc_key(seed, 1u);
        printf("  %-24s PASS seed=0x%08lX key=0x%08lX\r\n", "27 01 取种子", (unsigned long) seed,
               (unsigned long) key);
        s_pass++;
        req[0] = 0x27u; req[1] = 0x02u;
        req[2] = (uint8_t) (key >> 24); req[3] = (uint8_t) (key >> 16);
        req[4] = (uint8_t) (key >> 8);  req[5] = (uint8_t) key;
        rlen = sizeof(resp);
        ok = (uds_req(req, 6u, resp, &rlen, &rtt, 2000u) == 0) && (rlen == 2u) && (resp[0] == 0x67u);
        chk("27 02 解锁", ok, "");
    } else {
        chk("27 01 取种子", false, "");
    }

    /* 3) 34 请求下载：2 字节地址 + 2 字节长度，ALFID=0x22 */
    req[0] = 0x34u;
    req[1] = 0x00u;                        /* dataFormatIdentifier：无压缩加密 */
    req[2] = 0x44u;                        /* 高 4 位=长度字节数(4)，低 4 位=地址字节数(4) */
    req[3] = (uint8_t) (IMG_ADDR >> 24);
    req[4] = (uint8_t) (IMG_ADDR >> 16);
    req[5] = (uint8_t) (IMG_ADDR >> 8);
    req[6] = (uint8_t) IMG_ADDR;
    req[7] = (uint8_t) (IMG_SIZE >> 24);
    req[8] = (uint8_t) (IMG_SIZE >> 16);
    req[9] = (uint8_t) (IMG_SIZE >> 8);
    req[10] = (uint8_t) IMG_SIZE;
    t_all0 = app_isotp_port_now_us();
    s_pending_seen = 0;
    rlen = sizeof(resp);
    ok = (uds_req(req, 11u, resp, &rlen, &rtt, 8000u) == 0) && (rlen >= 4u) && (resp[0] == 0x74u);
    {
        char ex[96];
        uint32_t maxblk = ok ? (((uint32_t) resp[2] << 8) | resp[3]) : 0u;
        if (ok) {
            snprintf(ex, sizeof(ex), "rtt=%lu us maxBlock=%lu pending=%lu", (unsigned long) rtt,
                     (unsigned long) maxblk, (unsigned long) s_pending_seen);
        } else {
            snprintf(ex, sizeof(ex), "resp=%02X %02X %02X", resp[0], rlen > 1u ? resp[1] : 0u,
                     rlen > 2u ? resp[2] : 0u);
        }
        chk("34 请求下载(擦除)", ok && (s_pending_seen >= 1u), ex);
    }

    /* 4) 36 传输数据：每块 256 B */
    blocks = (IMG_SIZE + BLOCK - 1u) / BLOCK;
    {
        uint32_t seq = 1u;
        uint32_t sent = 0;
        uint32_t rtt_sum = 0;
        uint32_t rtt_max = 0;
        bool all_ok = true;
        for (uint32_t b = 0; b < blocks; b++) {
            uint32_t n = IMG_SIZE - sent;
            if (n > BLOCK) {
                n = BLOCK;
            }
            req[0] = 0x36u;
            req[1] = (uint8_t) seq;
            memcpy(&req[2], &s_img[sent], n);   /* 整个镜像（含 256 B 头）一起下发：写入从 0x800F0000 起且页对齐 */
            rlen = sizeof(resp);
            if ((uds_req(req, n + 2u, resp, &rlen, &rtt, 4000u) != 0) || (rlen != 2u) ||
                (resp[0] != 0x76u) || (resp[1] != req[1])) {
                all_ok = false;
                break;
            }
            sent += n;
            seq++;
            rtt_sum += rtt;
            if (rtt > rtt_max) {
                rtt_max = rtt;
            }
        }
        {
            uint64_t dt = app_isotp_port_now_us() - t_all0;
            char ex[128];
            snprintf(ex, sizeof(ex), "%lu 块/%.1f KB/s 总耗时 %lu us blk_rtt_avg=%lu max=%lu",
                     (unsigned long) ((sent + BLOCK - 1u) / BLOCK),
                     (double) sent / ((double) dt / 1e6) / 1024.0,
                     (unsigned long) dt, (unsigned long) (blocks ? rtt_sum / blocks : 0u),
                     (unsigned long) rtt_max);
            chk("36 传输数据", all_ok && (sent == IMG_SIZE), ex);
        }
    }

    /* 5) 37 退出传输 */
    req[0] = 0x37u;
    rlen = sizeof(resp);
    ok = (uds_req(req, 1u, resp, &rlen, &rtt, 2000u) == 0) && (rlen == 1u) && (resp[0] == 0x77u);
    {
        char ex[48];
        snprintf(ex, sizeof(ex), ok ? "" : "resp=%02X %02X %02X", resp[0], rlen > 1u ? resp[1] : 0u,
                 rlen > 2u ? resp[2] : 0u);
        chk("37 退出传输", ok, ex);
    }

    /* 6) 31 01 FF00 读回 CRC32 */
    req[0] = 0x31u; req[1] = 0x01u; req[2] = 0xFFu; req[3] = 0x00u;
    rlen = sizeof(resp);
    ok = (uds_req(req, 4u, resp, &rlen, &rtt, 4000u) == 0) && (rlen == 9u) && (resp[0] == 0x71u);
    if (ok) {
        crc_ecu = ((uint32_t) resp[5] << 24) | ((uint32_t) resp[6] << 16) |
                  ((uint32_t) resp[7] << 8) | resp[8];
    }
    {
        char ex[96];
        snprintf(ex, sizeof(ex), "ECU=0x%08lX 本地=0x%08lX", (unsigned long) crc_ecu,
                 (unsigned long) crc_tx);
        chk("31 01 FF00 校验 CRC32", ok && (resp[4] == 0x00u) && (crc_ecu == crc_tx), ex);
    }

    /* 7) 31 01 FF01 依赖检查 */
    req[0] = 0x31u; req[1] = 0x01u; req[2] = 0xFFu; req[3] = 0x01u;
    rlen = sizeof(resp);
    ok = (uds_req(req, 4u, resp, &rlen, &rtt, 4000u) == 0) && (rlen == 5u) && (resp[0] == 0x71u) &&
         (resp[4] == 0x00u);
    chk("31 01 FF01 依赖检查", ok, ok ? "magic/len/crc 一致" : "status!=0");

    /* 8) 用 XIP 直读交叉验证（不依赖 ECU 的例程实现） */
    {
        const uint8_t *p = (const uint8_t *) (uintptr_t) IMG_ADDR;
        uint32_t crc_xip = crc32_sw(&p[IMG_HDR], IMG_SIZE - IMG_HDR);
        uint32_t bad = 0xFFFFFFFFu;
        uint32_t bad_cnt = 0;
        char ex[96];
        for (uint32_t i = 0; i < IMG_SIZE; i++) {
            if (p[i] != s_img[i]) {
                if (bad == 0xFFFFFFFFu) {
                    bad = i;
                }
                bad_cnt++;
            }
        }
        if (bad == 0xFFFFFFFFu) {
            snprintf(ex, sizeof(ex), "XIP crc=0x%08lX memcmp=identical", (unsigned long) crc_xip);
        } else {
            snprintf(ex, sizeof(ex), "首个不符 @%lu: flash=0x%02X 期望=0x%02X 共 %lu B 不符",
                     (unsigned long) bad, p[bad], s_img[bad], (unsigned long) bad_cnt);
        }
        chk("XIP 直读全镜像", (bad == 0xFFFFFFFFu) && (crc_xip == crc_tx), ex);
    }
    report_region();

    /* 9) 11 01 复位（复位后 main 里会打印 post-reset 校验） */
    req[0] = 0x11u; req[1] = 0x01u;
    rlen = sizeof(resp);
    ok = (uds_req(req, 2u, resp, &rlen, &rtt, 2000u) == 0) && (rlen == 2u) && (resp[0] == 0x51u);
    chk("11 01 复位", ok, "");

    printf("[uds_flash] %s pass=%lu fail=%lu（%.1f KB/s，镜像 %lu B）\r\n",
           (s_fail == 0) ? "PASS" : "FAIL", (unsigned long) s_pass, (unsigned long) s_fail,
           (double) (IMG_SIZE - IMG_HDR) * 1e6 / (double) (app_isotp_port_now_us() - t_all0) / 1024.0,
           (unsigned long) IMG_SIZE);
    /* ecu_reset() 已在 uds_req 返回后由 app_uds_poll 触发 */
}

static void cmd_erase(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    if (app_uds_flash_default()->begin(NULL, IMG_ADDR, IMG_SIZE) != 0) {
        printf("[uds_flash] erase FAIL\r\n");
        return;
    }
    printf("[uds_flash] erase 0x%08lX %lu B OK（再次 run 前必须擦，否则魔数会留在 Flash 里）\r\n",
           (unsigned long) IMG_ADDR, (unsigned long) IMG_SIZE);
}

static void cmd_info(int argc, char *argv[])
{
    uint32_t ws = 0, we = 0;
    (void) argc;
    (void) argv;
    app_uds_flash_window(&ws, &we);
    printf("\r\n[uds_flash] 镜像 %lu B @0x%08lX，安全窗口 0x%08lX~0x%08lX，块 %lu B\r\n",
           (unsigned long) IMG_SIZE, (unsigned long) IMG_ADDR, (unsigned long) ws,
           (unsigned long) we, (unsigned long) BLOCK);
    printf("  session=%s security=%u dl_active=%u\r\n", app_uds_session_name(s_uds.session),
           (unsigned) s_uds.security, (unsigned) s_uds.dl_active);
}

static void cmd_check(int argc, char *argv[])
{
    const uint8_t *p = (const uint8_t *) (uintptr_t) IMG_ADDR;
    (void) argc;
    (void) argv;
    printf("\r\n[uds_flash] 0x%08lX: %.8s len=%lu crc=0x%08lX calc=0x%08lX\r\n",
           (unsigned long) IMG_ADDR, (const char *) p, (unsigned long) get_u32le(&p[8]),
           (unsigned long) get_u32le(&p[12]), (unsigned long) crc32_sw(&p[IMG_HDR], IMG_SIZE - IMG_HDR));
}

static void cmd_stat(int argc, char *argv[])
{
    uint32_t erase_us = 0, write_us = 0, prog = 0;
    (void) argc;
    (void) argv;
    app_uds_flash_stats(&erase_us, &write_us, &prog);
    printf("\r\n[uds_flash] req=%lu pos=%lu nrc=%lu pending=%lu dl_active=%u\r\n",
           (unsigned long) s_uds.stat_req, (unsigned long) s_uds.stat_resp_pos,
           (unsigned long) s_uds.stat_resp_nrc, (unsigned long) s_uds.stat_pending,
           (unsigned) s_uds.dl_active);
    printf("  erase=%lu us (%.1f KB/s) write=%lu us (%.1f KB/s) programmed=%lu B\r\n",
           (unsigned long) erase_us, (erase_us ? (double) IMG_SIZE / ((double) erase_us / 1e6) / 1024.0 : 0.0),
           (unsigned long) write_us, (write_us ? (double) prog / ((double) write_us / 1e6) / 1024.0 : 0.0),
           (unsigned long) prog);
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 run, cmd_run, full UDS flashing sequence: run);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 erase, cmd_erase, erase download region: erase);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 check, cmd_check, verify image in flash: check);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 fi, cmd_info, flashing config: fi);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, flashing statistics: stat);

int main(void)
{
    const uint8_t *p = (const uint8_t *) (uintptr_t) IMG_ADDR;
    bool valid_img;

    board_init();
    bsp_init();
    printf("\r\n[app] uds_flash ready (UDS 0x34/0x36/0x37 -> 片内 Flash 0x%08lX)\r\n",
           (unsigned long) IMG_ADDR);
    if ((bus_setup() != 0) || (ecu_setup() != 0)) {
        printf("[uds_flash] FAIL: init error\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }
    board_delay_ms(1500);

    /* 复位后自检：如果下载区已有有效镜像，说明上一次 0x11 01 + Flash 写入都成功了 */
    valid_img = (memcmp(p, MAGIC, 8u) == 0) && (get_u32le(&p[8]) == IMG_SIZE) &&
                (get_u32le(&p[12]) == crc32_sw(&p[IMG_HDR], IMG_SIZE - IMG_HDR));
    if (valid_img) {
        printf("[uds_flash] PASS post-reset: 0x%08lX 存在有效镜像 magic=%.8s len=%lu crc=0x%08lX\r\n",
               (unsigned long) IMG_ADDR, (const char *) p, (unsigned long) get_u32le(&p[8]),
               (unsigned long) get_u32le(&p[12]));
    } else {
        printf("[uds_flash] post-reset: 下载区无有效镜像（尚未刷写或被擦除）\r\n");
    }
    /* 不在启动时自动刷写：刷写末尾会 0x11 01 复位，自动跑会变成"复位-刷写"死循环。
     * 用 shell 命令 run 触发（hwrun --send "run"）。 */
    cmd_info(0, NULL);

    while (1) {
        pump_once();
        bsp_shell_poll();
    }
}
