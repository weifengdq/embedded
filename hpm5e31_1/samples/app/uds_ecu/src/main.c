/*
 * uds_ecu - UDS(ISO 14229-1:2013) 服务端完整 ECU + 板内测试仪
 *
 * CAN0 = ECU（收 0x7E0 / 发 0x7E8），CAN1 = 板内 tester（反向），
 * 二者经 ISO-TP(components/app_isotp) 传输，跑真实的诊断报文序列。
 *
 * 服务序列（run）：
 *   10 03 -> 22 F190/F195/F187/F18C -> 2E F190(未解锁，应 NRC 0x33)
 *          -> 27 01/02 -> 2E F190(成功) -> 22 F190(校验写入)
 *          -> 31 01 0203 -> 19 02 -> 14 FFFFFF -> 3E 00 -> 10 01
 * 额外：
 *   pend  31 01 0301（150 ms 慢例程）验证 NRC 0x78 pending 与 P2* 续期
 *   s3    10 03 后静默 5.5 s，验证 S3 超时回默认会话（2E 应 NRC 0x7F）
 *   sec   27 01 后用错误 key 连续 3 次，验证 NRC 0x35 -> 0x36 与 10 s 锁定
 *   dtc   打印故障码内存
 *   stat  服务端统计
 */
#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include "app_isotp.h"
#include "app_isotp_port.h"
#include "app_uds.h"
#include "ext/board_can.h"

#define ID_REQ   (0x7E0u)
#define ID_RESP  (0x7E8u)
#define TP_MAX   1024u

static app_isotp_port_t s_port0;
static app_isotp_port_t s_port1;
static app_isotp_t s_srv;        /* ECU 侧 */
static app_isotp_t s_cli;        /* tester 侧 */
static uint8_t s_srv_tx[TP_MAX];
static uint8_t s_srv_rx[TP_MAX];
static uint8_t s_cli_tx[TP_MAX];
static uint8_t s_cli_rx[TP_MAX];

static app_uds_t s_uds;
static app_uds_dtc_mem_t s_dtc;
static app_uds_did_t s_dids[8];
static uint32_t s_did_count;
static app_uds_routine_t s_routines[4];
static uint32_t s_routine_count;

/* tester 侧收到的 PDU 队列：慢服务会先回 0x78 再回最终响应，两条可能在同一轮
 * pump 里到达，用单缓冲会丢第一条（第一版 pending 计数一直是 0 的原因）。 */
#define CLI_QDEPTH 4u
typedef struct {
    uint8_t  data[APP_UDS_MAX_RESP];
    uint32_t len;
} cli_pdu_t;
static cli_pdu_t s_q[CLI_QDEPTH];
static volatile uint32_t s_q_head;
static volatile uint32_t s_q_tail;
static uint32_t s_pending_seen;      /* 本次请求里收到的 0x78 个数 */
static uint32_t s_reset_count;       /* 收到 0x11 的次数（本程序不真复位，只记录） */

/* 例程上下文 */
typedef struct {
    uint32_t self_test_runs;
    uint32_t slow_runs;
    uint32_t last_result;
} routine_ctx_t;
static routine_ctx_t s_rt;

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
        return;                       /* 队列满：丢弃（测试里不会发生） */
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

/* tester 发一条 UDS 请求并收完整响应（自动跳过 0x78 pending） */
static int uds_req(const uint8_t *req, uint32_t len, uint8_t *resp, uint32_t *rlen,
                   uint32_t *rtt_us, uint32_t timeout_ms)
{
    uint32_t pend = 0;
    uint64_t t0 = app_isotp_port_now_us();
    uint64_t limit = (uint64_t) timeout_ms * 1000u;
    cli_pdu_t pdu;

    s_q_head = 0;
    s_q_tail = 0;
    if (app_isotp_send(&s_cli, req, len) != APP_ISOTP_OK) {
        return -1;
    }
    for (;;) {
        while (!cli_pop(&pdu)) {
            pump_once();
            if ((app_isotp_port_now_us() - t0) > limit) {
                s_pending_seen = pend;
                return -2;
            }
        }
        if ((pdu.len == 3u) && (pdu.data[0] == 0x7Fu) && (pdu.data[2] == 0x78u)) {
            pend++;
            continue;                      /* 收到 pending，继续等最终响应 */
        }
        s_pending_seen = pend;
        if (pdu.len > *rlen) {
            return -3;
        }
        memcpy(resp, pdu.data, pdu.len);
        *rlen = pdu.len;
        *rtt_us = (uint32_t) (app_isotp_port_now_us() - t0);
        return 0;
    }
}

/* ---------------- 例行程序回调 ---------------- */

static int rt_self_test_start(void *ctx, const uint8_t *opt, uint32_t opt_len, uint8_t *out, uint32_t *out_len)
{
    routine_ctx_t *c = (routine_ctx_t *) ctx;
    (void) opt;
    (void) opt_len;
    c->self_test_runs++;
    c->last_result = 0x00u;                 /* 0 = 自检通过 */
    out[0] = c->last_result;
    out[1] = (uint8_t) (c->self_test_runs & 0xFFu);
    *out_len = 2u;
    return 0;
}

static int rt_self_test_results(void *ctx, uint8_t *out, uint32_t *out_len)
{
    routine_ctx_t *c = (routine_ctx_t *) ctx;
    out[0] = c->last_result;
    out[1] = (uint8_t) (c->self_test_runs & 0xFFu);
    *out_len = 2u;
    return 0;
}

/* 慢例程：故意发 NRC 0x78 + 延时 150 ms，验证 P2* 续期（真实刷写擦除就是这样） */
static int rt_slow_start(void *ctx, const uint8_t *opt, uint32_t opt_len, uint8_t *out, uint32_t *out_len)
{
    routine_ctx_t *c = (routine_ctx_t *) ctx;
    (void) opt;
    (void) opt_len;
    (void) app_uds_send_pending(&s_uds, 0x31u);
    board_delay_ms(150u);
    c->slow_runs++;
    out[0] = 0x00u;
    out[1] = (uint8_t) (c->slow_runs & 0xFFu);
    *out_len = 2u;
    return 0;
}

static int rt_slow_results(void *ctx, uint8_t *out, uint32_t *out_len)
{
    routine_ctx_t *c = (routine_ctx_t *) ctx;
    out[0] = 0x00u;
    out[1] = (uint8_t) (c->slow_runs & 0xFFu);
    *out_len = 2u;
    return 0;
}

/* ---------------- 测试仪判定 ---------------- */

static uint32_t s_pass;
static uint32_t s_fail;

static bool expect_ok(const char *name, const uint8_t *req, uint32_t qlen,
                      const uint8_t *exp, uint32_t elen, uint8_t exp_nrc)
{
    uint8_t resp[APP_UDS_MAX_RESP];
    uint32_t rlen = sizeof(resp);
    uint32_t rtt = 0;
    bool ok;
    char reqstr[64];
    uint32_t p = 0;

    for (uint32_t i = 0; i < qlen && p + 3 < sizeof(reqstr); i++) {
        p += (uint32_t) snprintf(&reqstr[p], sizeof(reqstr) - p, "%02X ", req[i]);
    }
    reqstr[p] = 0;

    int rc = uds_req(req, qlen, resp, &rlen, &rtt, 8000u);
    if (rc != 0) {
        printf("  %-22s %-28s -> TIMEOUT/RC %d\r\n", name, reqstr, rc);
        s_fail++;
        return false;
    }
    if (exp_nrc != 0u) {
        ok = (rlen == 3u) && (resp[0] == 0x7Fu) && (resp[1] == req[0]) && (resp[2] == exp_nrc);
    } else {
        ok = (rlen >= elen) && (memcmp(resp, exp, elen) == 0);
    }
    printf("  %-22s %-28s -> ", name, reqstr);
    for (uint32_t i = 0; i < rlen && i < 10u; i++) {
        printf("%02X ", resp[i]);
    }
    if (rlen > 10u) {
        printf("... (%lu B)", (unsigned long) rlen);
    }
    printf(" %s rtt=%lu us%s\r\n", ok ? "PASS" : "FAIL", (unsigned long) rtt,
           s_pending_seen ? " (含 0x78)" : "");
    if (ok) {
        s_pass++;
    } else {
        s_fail++;
    }
    return ok;
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
    s_reset_count++;
    printf("[uds_ecu] 0x11 复位请求已执行（例程不真复位，避免中断本次测试）count=%lu\r\n",
           (unsigned long) s_reset_count);
}

static void dtc_init(void)
{
    static const uint8_t vin_dummy[3] = {0};
    app_uds_dtc_snap_t snap[APP_UDS_DTC_SNAP_DIDS];
    (void) vin_dummy;

    app_uds_dtc_init(&s_dtc);
    /* U0100-87 与 ECM 通信丢失：当前失败 + 已确认 */
    (void) app_uds_dtc_set(&s_dtc, 0xC10087u,
                           APP_UDS_DTC_TEST_FAILED | APP_UDS_DTC_TEST_FAILED_THIS_CYC |
                           APP_UDS_DTC_PENDING | APP_UDS_DTC_CONFIRMED, 0);
    /* U0101-87 与 TCM 通信丢失：已确认（历史故障） */
    (void) app_uds_dtc_set(&s_dtc, 0xC10187u, APP_UDS_DTC_CONFIRMED, 0);
    /* 厂商自定义 0xC0DE01：待定 */
    (void) app_uds_dtc_set(&s_dtc, 0xC0DE01u, APP_UDS_DTC_PENDING, 0);

    /* 冻结帧：车速 0 km/h、电压 12.05 V (12050 mV)、水温 82 ℃ */
    snap[0].did = 0x0100u; snap[0].len = 1u; snap[0].data[0] = 0u;
    snap[1].did = 0x0101u; snap[1].len = 2u; snap[1].data[0] = 0x2Fu; snap[1].data[1] = 0x12u;
    snap[2].did = 0x0102u; snap[2].len = 1u; snap[2].data[0] = 82u;
    (void) app_uds_dtc_set_snapshot(&s_dtc, 0xC10087u, snap, APP_UDS_DTC_SNAP_DIDS);
}

static int ecu_setup(void)
{
    app_uds_cfg_t uc;

    dtc_init();
    (void) app_uds_default_dids(s_dids, &s_did_count);

    memset(s_routines, 0, sizeof(s_routines));
    s_routine_count = 0;
    s_routines[s_routine_count++] = (app_uds_routine_t){
        .rid = 0x0203u, .session_mask = APP_UDS_SESS_EXT, .sec_level = 0u,
        .name = "self-test", .start = rt_self_test_start, .stop = NULL,
        .results = rt_self_test_results, .ctx = &s_rt };
    s_routines[s_routine_count++] = (app_uds_routine_t){
        .rid = 0x0301u, .session_mask = APP_UDS_SESS_EXT, .sec_level = 0u,
        .name = "slow-150ms", .start = rt_slow_start, .stop = NULL,
        .results = rt_slow_results, .ctx = &s_rt };

    memset(&uc, 0, sizeof(uc));
    uc.tx = ecu_tx;
    uc.reset = ecu_reset;
    uc.now_us = NULL;                  /* 由 app_uds_poll() 提供时基 */
    uc.dids = s_dids;
    uc.did_count = s_did_count;
    uc.routines = s_routines;
    uc.routine_count = s_routine_count;
    uc.dtc = &s_dtc;
    return app_uds_init(&s_uds, &uc);
}

/* ---------------- 测试序列 ---------------- */

static void cmd_run(int argc, char *argv[])
{
    static const uint8_t new_vin[17] = "HPM5E31NEWVIN0002";
    uint8_t req[64];
    uint8_t resp[APP_UDS_MAX_RESP];
    uint32_t rlen;
    uint32_t rtt;
    (void) argc;
    (void) argv;

    s_pass = 0;
    s_fail = 0;
    /* 复位服务端状态，保证可重复运行 */
    (void) ecu_setup();
    s_rt.self_test_runs = 0;
    s_rt.slow_runs = 0;
    s_rt.last_result = 0;

    printf("\r\n[uds_ecu] run: 0x7E0/0x7E8 服务序列，板内 tester（经典 500k，ISO-TP BS=0 STmin=0）\r\n");
    printf("  %-22s %-28s %s\r\n", "步骤", "请求", "响应");

    /* 1) 10 03 */
    req[0] = 0x10u; req[1] = 0x03u;
    expect_ok("10 03 扩展会话", req, 2u, (const uint8_t *) "\x50\x03", 2u, 0u);
    /* 2) 22 F190/F195/F187/F18C */
    req[0] = 0x22u; req[1] = 0xF1u; req[2] = 0x90u;
    req[3] = 0xF1u; req[4] = 0x95u; req[5] = 0xF1u; req[6] = 0x87u;
    req[7] = 0xF1u; req[8] = 0x8Cu;
    expect_ok("22 4x DID", req, 9u, (const uint8_t *) "\x62\xF1\x90", 3u, 0u);
    /* 3) 2E F190 未解锁 -> NRC 0x33 */
    req[0] = 0x2Eu; req[1] = 0xF1u; req[2] = 0x90u;
    memcpy(&req[3], new_vin, 17u);
    expect_ok("2E F190 未解锁", req, 20u, NULL, 0u, APP_UDS_NRC_SAD);
    /* 4) 27 01 取种子 -> 计算 key -> 27 02 */
    {
        uint32_t seed;
        uint32_t key;
        req[0] = 0x27u; req[1] = 0x01u;
        rlen = sizeof(resp);
        if (uds_req(req, 2u, resp, &rlen, &rtt, 2000u) == 0 && rlen == 6u && resp[0] == 0x67u) {
            seed = ((uint32_t) resp[2] << 24) | ((uint32_t) resp[3] << 16) |
                   ((uint32_t) resp[4] << 8) | resp[5];
            key = app_uds_calc_key(seed, 1u);
            printf("  %-22s %-28s -> seed=%08lX key=%08lX rtt=%lu us\r\n", "27 01 取种子",
                   "27 01", (unsigned long) seed, (unsigned long) key, (unsigned long) rtt);
            req[0] = 0x27u; req[1] = 0x02u;
            req[2] = (uint8_t) (key >> 24); req[3] = (uint8_t) (key >> 16);
            req[4] = (uint8_t) (key >> 8);  req[5] = (uint8_t) key;
            if (expect_ok("27 02 发密钥", req, 6u, (const uint8_t *) "\x67\x02", 2u, 0u)) {
                s_pass++;   /* 种子步骤也计一次 PASS */
            }
        } else {
            printf("  %-22s -> FAIL (无种子)\r\n", "27 01 取种子");
            s_fail++;
        }
    }
    /* 5) 2E F190 已解锁 -> 成功 */
    req[0] = 0x2Eu; req[1] = 0xF1u; req[2] = 0x90u;
    memcpy(&req[3], new_vin, 17u);
    expect_ok("2E F190 已解锁", req, 20u, (const uint8_t *) "\x6E\xF1\x90", 3u, 0u);
    /* 6) 22 F190 校验写入 */
    {
        static const uint8_t exp[20] = {0x62u, 0xF1u, 0x90u};
        req[0] = 0x22u; req[1] = 0xF1u; req[2] = 0x90u;
        rlen = sizeof(resp);
        if ((uds_req(req, 3u, resp, &rlen, &rtt, 2000u) == 0) && (rlen == 20u) &&
            (memcmp(resp, exp, 3u) == 0) && (memcmp(&resp[3], new_vin, 17u) == 0)) {
            printf("  %-22s %-28s -> 62 F1 90 %.*s rtt=%lu us PASS\r\n", "22 F190 回读",
                   "22 F190", 17, (const char *) &resp[3], (unsigned long) rtt);
            s_pass++;
        } else {
            printf("  %-22s %-28s -> FAIL\r\n", "22 F190 回读", "22 F190");
            s_fail++;
        }
    }
    /* 7) 31 01 0203 自检 */
    req[0] = 0x31u; req[1] = 0x01u; req[2] = 0x02u; req[3] = 0x03u;
    expect_ok("31 01 0203 自检", req, 4u, (const uint8_t *) "\x71\x01\x02\x03", 4u, 0u);
    /* 8) 19 02 读 DTC */
    req[0] = 0x19u; req[1] = 0x02u; req[2] = 0xFFu;
    expect_ok("19 02 读 DTC", req, 3u, (const uint8_t *) "\x59\x02", 2u, 0u);
    /* 9) 14 FFFFFF 清故障码 */
    req[0] = 0x14u; req[1] = 0xFFu; req[2] = 0xFFu; req[3] = 0xFFu;
    expect_ok("14 FFFFFF 清码", req, 4u, (const uint8_t *) "\x54", 1u, 0u);
    /* 10) 19 02 再读 -> 应为空 */
    req[0] = 0x19u; req[1] = 0x02u; req[2] = 0xFFu;
    rlen = sizeof(resp);
    if ((uds_req(req, 3u, resp, &rlen, &rtt, 2000u) == 0) && (rlen == 3u) && (resp[0] == 0x59u) &&
        (resp[1] == 0x02u) && (resp[2] == 0x09u)) {
        printf("  %-22s %-28s -> 59 02 09 (0 个 DTC) rtt=%lu us PASS\r\n", "19 02 清码后", "19 02 FF",
               (unsigned long) rtt);
        s_pass++;
    } else {
        printf("  %-22s %-28s -> FAIL (rlen=%lu)\r\n", "19 02 清码后", "19 02 FF", (unsigned long) rlen);
        s_fail++;
    }
    /* 11) 3E 00 */
    req[0] = 0x3Eu; req[1] = 0x00u;
    expect_ok("3E 00 会话保持", req, 2u, (const uint8_t *) "\x7E\x00", 2u, 0u);
    /* 12) 10 01 回默认 */
    req[0] = 0x10u; req[1] = 0x01u;
    expect_ok("10 01 默认会话", req, 2u, (const uint8_t *) "\x50\x01", 2u, 0u);

    printf("[uds_ecu] %s steps=%lu/%lu pass=%lu fail=%lu (服务端 req=%lu pos=%lu nrc=%lu)\r\n",
           s_fail == 0 ? "PASS" : "FAIL", (unsigned long) (s_pass + s_fail),
           (unsigned long) (s_pass + s_fail), (unsigned long) s_pass, (unsigned long) s_fail,
           (unsigned long) s_uds.stat_req, (unsigned long) s_uds.stat_resp_pos,
           (unsigned long) s_uds.stat_resp_nrc);
}

static void cmd_pend(int argc, char *argv[])
{
    uint8_t req[8];
    (void) argc;
    (void) argv;
    printf("\r\n[uds_ecu] pend: 31 01 0301（150 ms 慢例程，应先回 7F 31 78）\r\n");
    (void) ecu_setup();
    req[0] = 0x10u; req[1] = 0x03u;
    expect_ok("10 03 前置", req, 2u, (const uint8_t *) "\x50\x03", 2u, 0u);
    req[0] = 0x31u; req[1] = 0x01u; req[2] = 0x03u; req[3] = 0x01u;
    s_pass = 0;
    s_fail = 0;
    expect_ok("31 01 0301 慢例程", req, 4u, (const uint8_t *) "\x71\x01\x03\x01", 4u, 0u);
    printf("[uds_ecu] %s pending=%lu 个（测试仪看到 7F 31 78，P2* 续期生效）\r\n",
           (s_fail == 0 && s_pending_seen >= 1u) ? "PASS" : "FAIL",
           (unsigned long) s_pending_seen);
}

static void cmd_s3(int argc, char *argv[])
{
    uint8_t req[64];
    uint32_t rlen;
    uint32_t rtt;
    (void) argc;
    (void) argv;

    printf("\r\n[uds_ecu] s3: 10 03 后静默 5.5 s，验证 S3=5 s 超时回默认会话\r\n");
    (void) ecu_setup();
    req[0] = 0x10u; req[1] = 0x03u;
    (void) uds_req(req, 2u, s_cli.rx_buf, &rlen, &rtt, 2000u);
    /* 静默 5.5 s，期间只 pump 不发请求 */
    {
        uint64_t t0 = app_isotp_port_now_us();
        while ((app_isotp_port_now_us() - t0) < 5500000u) {
            pump_once();
        }
    }
    req[0] = 0x2Eu; req[1] = 0xF1u; req[2] = 0x90u;
    memcpy(&req[3], "HPM5E31TESTVIN001", 17u);
    rlen = sizeof(s_cli_rx);
    {
        uint8_t s3resp[64];
        rlen = sizeof(s3resp);
        if ((uds_req(req, 20u, s3resp, &rlen, &rtt, 2000u) == 0) && (rlen == 3u) &&
            (s3resp[0] == 0x7Fu) && (s3resp[2] == APP_UDS_NRC_SNSIAS)) {
            printf("[uds_ecu] PASS s3: 超时后 2E 返回 NRC 0x7F（%lu 次 S3 超时/5 s 静默）\r\n",
                   (unsigned long) s_uds.stat_s3_timeout);
        } else {
            printf("[uds_ecu] FAIL s3: rlen=%lu [0]=%02X [2]=%02X\r\n", (unsigned long) rlen,
                   s3resp[0], s3resp[2]);
        }
    }
}

static void cmd_sec(int argc, char *argv[])
{
    uint8_t req[8];
    uint8_t resp[APP_UDS_MAX_RESP];
    uint32_t rlen;
    uint32_t rtt;
    (void) argc;
    (void) argv;

    bool ok = true;
    printf("\r\n[uds_ecu] sec: 每次错误 key 前重新取种子；3 次后锁 10 s\r\n");
    (void) ecu_setup();
    for (int i = 1; i <= 3; i++) {
        req[0] = 0x27u; req[1] = 0x01u;
        rlen = sizeof(resp);
        if (uds_req(req, 2u, resp, &rlen, &rtt, 2000u) != 0) {
            printf("  第 %d 次取种子失败\r\n", i);
            ok = false;
            break;
        }
        req[0] = 0x27u; req[1] = 0x02u;
        req[2] = 0xDEu; req[3] = 0xADu; req[4] = 0xBEu; req[5] = 0xEFu;
        rlen = sizeof(resp);
        (void) uds_req(req, 6u, resp, &rlen, &rtt, 2000u);
        uint8_t want = (i <= 2) ? APP_UDS_NRC_IK : APP_UDS_NRC_ENOA;
        bool pass = (rlen == 3u) && (resp[0] == 0x7Fu) && (resp[2] == want);
        printf("  第 %d 次错误 key -> NRC 0x%02X（期望 0x%02X）= %s\r\n", i,
               (rlen >= 3u) ? resp[2] : 0u, want, pass ? "PASS" : "FAIL");
        ok = ok && pass;
    }
    /* 锁定期内再要种子：应 NRC 0x37 requiredTimeDelayNotExpired */
    req[0] = 0x27u; req[1] = 0x01u;
    rlen = sizeof(resp);
    (void) uds_req(req, 2u, resp, &rlen, &rtt, 2000u);
    {
        bool pass = (rlen == 3u) && (resp[2] == APP_UDS_NRC_RTDNE);
        printf("[uds_ecu] %s 锁定期内取种子 -> NRC 0x%02X（期望 0x37）\r\n",
               (ok && pass) ? "PASS" : "FAIL", (rlen >= 3u) ? resp[2] : 0u);
    }
}

static void cmd_dtc(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    dtc_init();     /* 重新装填演示故障码（14 FFFFFF 已经把表清空过） */
    printf("\r\n[uds_ecu] DTC memory: count=%lu cleared=%lu\r\n",
           (unsigned long) s_dtc.count, (unsigned long) s_dtc.cleared_cnt);
    for (uint32_t i = 0; i < APP_UDS_DTC_MAX; i++) {
        const app_uds_dtc_entry_t *e = &s_dtc.ent[i];
        if (!e->used) {
            continue;
        }
        printf("  DTC 0x%06lX status=0x%02X occ=%u aging=%u snap=%u\r\n",
               (unsigned long) e->dtc, (unsigned) e->status, (unsigned) e->occ,
               (unsigned) e->aging, (unsigned) e->snap_cnt);
    }
}

static void cmd_stat(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("\r\n[uds_ecu] session=%s sec=%u dtc_setting=%u comm_mode=%u\r\n",
           app_uds_session_name(s_uds.session), (unsigned) s_uds.security,
           (unsigned) s_uds.dtc_setting_on, (unsigned) s_uds.comm_mode);
    printf("  req=%lu pos=%lu nrc=%lu pending=%lu s3_timeout=%lu dl_active=%u\r\n",
           (unsigned long) s_uds.stat_req, (unsigned long) s_uds.stat_resp_pos,
           (unsigned long) s_uds.stat_resp_nrc, (unsigned long) s_uds.stat_pending,
           (unsigned long) s_uds.stat_s3_timeout, (unsigned) s_uds.dl_active);
    printf("  iso-tp: txmsg=%lu rxmsg=%lu txf=%lu rxf=%lu timeout=%lu seq=%lu\r\n",
           (unsigned long) s_srv.st.tx_msg, (unsigned long) s_srv.st.rx_msg,
           (unsigned long) s_srv.st.tx_frames, (unsigned long) s_srv.st.rx_frames,
           (unsigned long) s_srv.st.err_timeout, (unsigned long) s_srv.st.err_seq);
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("\r\n[uds_ecu] CAN0=ECU 0x%03lX/0x%03lX  CAN1=tester（经典 500 kbit/s）\r\n",
           (unsigned long) ID_REQ, (unsigned long) ID_RESP);
    printf("  P2=%lu ms P2*=%lu ms S3=%lu ms DIDs=%lu routines=%lu\r\n",
           (unsigned long) s_uds.cfg.p2_ms, (unsigned long) s_uds.cfg.p2star_ms,
           (unsigned long) s_uds.cfg.s3_ms, (unsigned long) s_did_count,
           (unsigned long) s_routine_count);
    printf("  27 算法：key=ROL8(seed ^ 0x3A5C9E17/if-odd-add 0x5A5A5A5A)^(level*0x01020304)，ROL7\r\n");
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 run, cmd_run, full UDS service sequence: run);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 pend, cmd_pend, NRC 0x78 pending test: pend);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 s3, cmd_s3, S3 timeout test: s3);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sec, cmd_sec, security wrong-key test: sec);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dtc, cmd_dtc, dump DTC memory: dtc);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, UDS server statistics: stat);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, UDS config: info);

int main(void)
{
    board_init();
    bsp_init();

    printf("\r\n[app] uds_ecu ready (ISO 14229-1, CAN0 ECU 0x7E0/0x7E8, CAN1 tester)\r\n");
    if ((bus_setup() != 0) || (ecu_setup() != 0)) {
        printf("[uds_ecu] FAIL: init error\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }
    board_delay_ms(1500);      /* 等 hwrun 的串口采集起来 */
    cmd_info(0, NULL);
    cmd_run(0, NULL);
    cmd_pend(0, NULL);
    cmd_s3(0, NULL);
    cmd_sec(0, NULL);
    cmd_dtc(0, NULL);
    cmd_stat(0, NULL);

    while (1) {
        pump_once();
        bsp_shell_poll();
    }
}
