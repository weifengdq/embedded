/*
 * doip_server - DoIP(ISO 13400-2) 服务端 + UDS(ISO 14229-1) 诊断，单固件
 *
 * 网络：板 ENET0(RGMII) + YT8011AN，静态 IP 192.168.2.100/24，主机 enp6s0 = 192.168.2.1/24
 * 端口：UDP 13400（车辆声明/发现、实体状态、电源模式）
 *       TCP 13400（路由激活、诊断报文 0x8001、alive check）
 * 逻辑地址：本实体 0x1000，tester 0x0E00（可由 shell 改）
 *
 * 数据流：
 *   主机 host/doip/doip_client.py
 *     -> UDP 0x0001/0x0002/0x0003 车辆识别 -> 板回 0x0004（VIN/EID/GID/逻辑地址）
 *     -> TCP 0x0005 路由激活 -> 板回 0x0006（0x10 成功）
 *     -> TCP 0x8001 [SA|TA|UDS] -> 板回 0x8002 肯定应答 + 0x8001 [UDS 响应]
 *     -> UDS 服务由 components/app_uds 处理（0x10/0x22/0x27/0x2E/0x31/0x19/0x3E...）
 *
 * shell（letter-shell，SHELL_TYPE_CMD_MAIN）：
 *   doip                 用法 + 全局统计
 *   doip start|stop      起/停 13400 监听（UDP+TCP）
 *   doip stat            计数器（收发包/路由激活/超时/alive check/大报文）
 *   doip conn            每条 TCP 连接的状态
 *   doip announce [n]    立即发 n 次车辆声明（0=用配置的 A_DoIP_Announce_Num）
 *   doip vin [17字符]    读/写 VIN（写会同步到 UDS 的 DID F190）
 *   doip eid [12位HEX]   读/写 EID（缺省用板 MAC）
 *   doip route           路由激活策略（tester 地址、允许的激活类型、OEM 鉴权）
 *   doip timer <名> <ms> 改定时器：initial/general/alive/announce/period
 *   doip alive [idx]     主动对某条连接做 alive check（0x0007）
 *   doip selftest        DoIP 编解码固定向量自检（不跑网络）
 *   doip bench [n] [cnt] DoIP/TCP 大报文吞吐注入：发 cnt 条 n 字节 0x8001
 *   doip uds             打印 UDS 服务端统计（DID/会话/安全/例程）
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "board.h"
#include "bsp.h"
#include "hpm_csr_drv.h"

#include "app_lwip.h"
#include "app_doip.h"
#include "app_doip_wire.h"
#include "app_uds.h"

#define ECU_ENTITY_ADDR     0x1000u
#define TESTER_ADDR_DEFAULT 0x0E00u
#define DOIP_VIN_DEFAULT    "HPM5E31TESTVIN001"   /* 与 app_uds 默认 DID F190 一致 */

static app_doip_t       s_doip;
static app_uds_t        s_uds;
static app_uds_dtc_mem_t s_dtc;
static app_uds_did_t    s_dids[8];
static uint32_t         s_did_count;
static app_uds_routine_t s_routines[4];
static uint32_t         s_routine_count;

static uint16_t s_cur_ta = TESTER_ADDR_DEFAULT;   /* 当前 UDS 响应该发给哪个 tester */
static uint32_t s_uds_rx;
static uint32_t s_uds_tx;
static uint32_t s_uds_drop;
static uint8_t  s_bench[APP_DOIP_MAX_PAYLOAD];

/* 例程上下文 */
typedef struct {
    uint32_t self_test_runs;
    uint32_t slow_runs;
    uint32_t last_result;
} routine_ctx_t;
static routine_ctx_t s_rt;

/* ---------------- 时基 ---------------- */

static uint64_t board_now_us(void *ctx)
{
    static uint32_t cpu_mhz;
    (void) ctx;
    if (cpu_mhz == 0u) {
        cpu_mhz = (uint32_t) (clock_get_frequency(clock_cpu0) / 1000000U);
    }
    return (uint64_t) hpm_csr_get_core_mcycle() / (uint64_t) cpu_mhz;
}

/* ---------------- UDS <-> DoIP 桥 ---------------- */

/* UDS 响应 -> DoIP 0x8001（SA=实体, TA=tester） */
static int uds_tx(void *ctx, const uint8_t *pdu, uint32_t len)
{
    (void) ctx;
    if (app_doip_send_diag(&s_doip, s_cur_ta, pdu, len) == 0) {
        s_uds_tx++;
        return 0;
    }
    s_uds_drop++;
    return -1;
}

/* 2E F190 改了 VIN 后，车辆声明/识别响应要跟着变（否则主机发现到的 VIN 和 UDS 读到的不一致） */
static void sync_vin_from_uds(void)
{
    uint32_t i;

    for (i = 0; i < s_did_count; i++) {
        if ((s_dids[i].did == 0xF190u) && (s_dids[i].len == 17u)) {
            (void) app_doip_set_vin(&s_doip, (const char *) s_dids[i].data);
            return;
        }
    }
}

/* 诊断请求队列：DoIP 回调里**只入队**，UDS 处理放到主循环。
 *
 * 为什么不在回调里直接调 app_uds_handle()：lwIP 的回调在收包路径上，而 UDS 慢服务
 * （0x31 例程、擦 Flash、0x78 pending）会阻塞上百 ms。阻塞期间 ENET 发送环不再被处理，
 * 已经 tcp_write 出去的 0x8002 肯定应答会跟着迟到——第 32 章“坑 3”有 tcpdump 实测
 * （ACK 推迟 150 ms、三条帧被拼进同一个 TCP 段）。入队后回调立即返回，ACK 走独立小包。 */
#define DIAG_QDEPTH 2u

typedef struct {
    uint16_t sa;
    uint32_t len;
    uint8_t  data[APP_DOIP_MAX_PAYLOAD];
} diag_req_t;

static diag_req_t s_q[DIAG_QDEPTH];
static volatile uint32_t s_q_head;
static volatile uint32_t s_q_tail;
static uint32_t s_q_drop;

static int on_diag(void *ctx, uint16_t sa, uint16_t ta, const uint8_t *uds, uint32_t len)
{
    uint32_t next = (s_q_head + 1u) % DIAG_QDEPTH;

    (void) ctx;
    (void) ta;
    if ((next == s_q_tail) || (len > APP_DOIP_MAX_PAYLOAD)) {
        s_q_drop++;
        return -1;
    }
    s_q[s_q_head].sa = sa;
    s_q[s_q_head].len = len;
    memcpy(s_q[s_q_head].data, uds, len);
    s_q_head = next;
    return 0;
}

/* 主循环里调用：把排队的 UDS 请求交给 app_uds（可能阻塞，所以不在回调里做） */
static void diag_pump(void)
{
    while (s_q_tail != s_q_head) {
        diag_req_t *r = &s_q[s_q_tail];
        s_cur_ta = r->sa;
        s_uds_rx++;
        (void) app_uds_handle(&s_uds, r->data, r->len);
        sync_vin_from_uds();
        s_q_tail = (s_q_tail + 1u) % DIAG_QDEPTH;
    }
}

/* DoIP/TCP 大报文吞吐注入：直接构造 n 字节 0x8001（伪 UDS 响应 62 F1 90 + 填充）。
 * 只用于测 DoIP 层的分帧/TCP 分段/吞吐，不参与 UDS 状态机；主机侧 bulk 用例靠它测出向大报文。 */
static int doip_bench_send(uint32_t n, uint32_t cnt)
{
    uint32_t i;
    int rc = 0;

    if (n < 8u) {
        n = 8u;
    }
    if (n > (APP_DOIP_MAX_PAYLOAD - 4u)) {
        n = APP_DOIP_MAX_PAYLOAD - 4u;
    }
    s_bench[0] = 0x62u;
    s_bench[1] = 0xF1u;
    s_bench[2] = 0x90u;
    for (i = 3u; i < n; i++) {
        s_bench[i] = (uint8_t) (i & 0xFFu);
    }
    for (i = 0; i < cnt; i++) {
        uint32_t t0 = app_lwip_now_ms();
        /* TCP_SND_BUF(8*MSS=11.68 KB) 只放得下 2 条 4 KB 报文；第 3 条起 tcp_write
         * 返回 ERR_MEM，要等对端 ACK 腾出窗口。这里在**主循环上下文**里 pump lwIP
         * （不是在 lwIP 回调里，所以安全），最多等 50 ms/帧（第 32 章坑 5）。 */
        while (app_doip_send_diag(&s_doip, s_cur_ta, s_bench, n) != 0) {
            app_lwip_poll();
            app_doip_poll(&s_doip);
            if ((app_lwip_now_ms() - t0) > 50u) {
                rc = -1;
                break;
            }
        }
        if (rc != 0) {
            break;
        }
        app_lwip_poll();
    }
    return rc;
}

/* ---------------- 例程回调 ---------------- */

static int rt_self_test_start(void *ctx, const uint8_t *opt, uint32_t opt_len,
                              uint8_t *out, uint32_t *out_len)
{
    routine_ctx_t *c = (routine_ctx_t *) ctx;
    (void) opt;
    (void) opt_len;
    c->self_test_runs++;
    c->last_result = 0x00u;
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

/* 150 ms 慢例程：验证 DoIP 上的 NRC 0x78 + P2* 续期（真实刷写擦除就是这样） */
static int rt_slow_start(void *ctx, const uint8_t *opt, uint32_t opt_len,
                         uint8_t *out, uint32_t *out_len)
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

static int rt_stats_results(void *ctx, uint8_t *out, uint32_t *out_len);

/* 0x0302 doip-stats 的 start：例程状态机要求 start 非空，这里只把状态置为“已启动” */
static int rt_stats_start(void *ctx, const uint8_t *opt, uint32_t opt_len,
                          uint8_t *out, uint32_t *out_len)
{
    (void) ctx;
    (void) opt;
    (void) opt_len;
    return rt_stats_results(NULL, out, out_len);
}

/* 0x0302 doip-stats：把 DoIP 计数器以 u32 大端返回（例程 requestResults 里最多 64 B） */
static int rt_stats_results(void *ctx, uint8_t *out, uint32_t *out_len)
{
    uint32_t v[14];
    (void) ctx;
    v[0] = s_doip.st_diag_rx;
    v[1] = s_doip.st_diag_tx;
    v[2] = s_doip.st_big_msg;
    v[3] = s_doip.st_route_ok;
    v[4] = s_doip.st_route_deny;
    v[5] = s_doip.st_udp_rx;
    v[6] = s_doip.st_err_hdr;
    v[7] = s_doip.st_alive_timeout;
    v[8] = s_doip.st_tcp_accept;
    v[9] = s_doip.st_tcp_close;
    v[10] = s_doip.st_initial_timeout;
    /* 每条连接：state(高 16 位) | tester_addr(低 16 位) */
    v[11] = ((uint32_t) s_doip.conn[0].state << 16) | s_doip.conn[0].tester_addr;
    v[12] = ((uint32_t) s_doip.conn[1].state << 16) | s_doip.conn[1].tester_addr;
    v[13] = ((uint32_t) s_doip.conn[2].state << 16) | s_doip.conn[2].tester_addr;
    for (uint32_t i = 0; i < 14u; i++) {
        out[i * 4u + 0u] = (uint8_t) (v[i] >> 24);
        out[i * 4u + 1u] = (uint8_t) (v[i] >> 16);
        out[i * 4u + 2u] = (uint8_t) (v[i] >> 8);
        out[i * 4u + 3u] = (uint8_t) v[i];
    }
    *out_len = 56u;
    return 0;
}

/* 0x0204 doip-bench：opt = n(2 B 大端) + cnt(2 B 大端)，触发 DoIP 大报文注入 */
static int rt_bench_start(void *ctx, const uint8_t *opt, uint32_t opt_len,
                          uint8_t *out, uint32_t *out_len)
{
    uint32_t n = 4000u;
    uint32_t cnt = 1u;
    int rc;

    (void) ctx;
    if (opt_len >= 2u) {
        n = ((uint32_t) opt[0] << 8) | (uint32_t) opt[1];
    }
    if (opt_len >= 4u) {
        cnt = ((uint32_t) opt[2] << 8) | (uint32_t) opt[3];
    }
    if (cnt == 0u) {
        cnt = 1u;
    }
    if (cnt > 1000u) {
        cnt = 1000u;
    }
    rc = doip_bench_send(n, cnt);
    out[0] = (uint8_t) ((rc == 0) ? 0u : 1u);
    out[1] = (uint8_t) (cnt & 0xFFu);
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

/* ---------------- 初始化 ---------------- */

static void dtc_init(void)
{
    app_uds_dtc_snap_t snap[APP_UDS_DTC_SNAP_DIDS];

    app_uds_dtc_init(&s_dtc);
    (void) app_uds_dtc_set(&s_dtc, 0xC10087u,
                           APP_UDS_DTC_TEST_FAILED | APP_UDS_DTC_TEST_FAILED_THIS_CYC |
                           APP_UDS_DTC_PENDING | APP_UDS_DTC_CONFIRMED, 0);
    (void) app_uds_dtc_set(&s_dtc, 0xC10187u, APP_UDS_DTC_CONFIRMED, 0);
    snap[0].did = 0x0100u; snap[0].len = 1u; snap[0].data[0] = 0u;
    snap[1].did = 0x0101u; snap[1].len = 2u; snap[1].data[0] = 0x2Fu; snap[1].data[1] = 0x12u;
    snap[2].did = 0x0102u; snap[2].len = 1u; snap[2].data[0] = 82u;
    (void) app_uds_dtc_set_snapshot(&s_dtc, 0xC10087u, snap, APP_UDS_DTC_SNAP_DIDS);
}

static int uds_setup(void)
{
    app_uds_cfg_t uc;

    dtc_init();
    (void) app_uds_default_dids(s_dids, &s_did_count);

    memset(s_routines, 0, sizeof(s_routines));
    s_routine_count = 0u;
    s_routines[s_routine_count++] = (app_uds_routine_t){
        .rid = 0x0203u, .session_mask = APP_UDS_SESS_EXT, .sec_level = 0u,
        .name = "self-test", .start = rt_self_test_start, .stop = NULL,
        .results = rt_self_test_results, .ctx = &s_rt };
    s_routines[s_routine_count++] = (app_uds_routine_t){
        .rid = 0x0301u, .session_mask = APP_UDS_SESS_EXT, .sec_level = 0u,
        .name = "slow-150ms", .start = rt_slow_start, .stop = NULL,
        .results = rt_slow_results, .ctx = &s_rt };
    s_routines[s_routine_count++] = (app_uds_routine_t){
        .rid = 0x0204u, .session_mask = APP_UDS_SESS_EXT, .sec_level = 0u,
        .name = "doip-bench", .start = rt_bench_start, .stop = NULL,
        .results = rt_slow_results, .ctx = &s_rt };
    s_routines[s_routine_count++] = (app_uds_routine_t){
        .rid = 0x0302u, .session_mask = APP_UDS_SESS_ALL, .sec_level = 0u,
        .name = "doip-stats", .start = rt_stats_start, .stop = NULL,
        .results = rt_stats_results, .ctx = NULL };

    memset(&uc, 0, sizeof(uc));
    uc.tx = uds_tx;
    uc.reset = NULL;
    uc.now_us = NULL;                 /* 由 app_uds_poll() 传时基 */
    uc.dids = s_dids;
    uc.did_count = s_did_count;
    uc.routines = s_routines;
    uc.routine_count = s_routine_count;
    uc.dtc = &s_dtc;
    return app_uds_init(&s_uds, &uc);
}

static int doip_setup(void)
{
    app_doip_cfg_t cfg;
    struct netif *ni;

    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.vin, DOIP_VIN_DEFAULT, 17u);
    cfg.vin[17] = 0;
    /* EID 用板 MAC（ISO 13400-2 推荐 EID 与 MAC 一致，便于主机侧关联） */
    cfg.eid[0] = 0x02u; cfg.eid[1] = 0x00u; cfg.eid[2] = 0x5Eu;
    cfg.eid[3] = 0x10u; cfg.eid[4] = 0x00u; cfg.eid[5] = 0x01u;
    ni = app_lwip_netif();
    if ((ni != NULL) && (ni->hwaddr_len == 6u)) {
        memcpy(cfg.eid, ni->hwaddr, 6u);
    }
    cfg.gid[5] = 0x01u;                /* GID：同一“车辆”内多个 DoIP 实体共用 */
    cfg.entity_addr = ECU_ENTITY_ADDR;
    cfg.tester_addr = TESTER_ADDR_DEFAULT;
    cfg.further_action = 0x00u;        /* 0x00 = 不需要额外动作；0x10 = 必须路由激活 */
    cfg.version = APP_DOIP_VER_2012;
    cfg.sync_status = 0x00u;
    cfg.power_mode = APP_DOIP_PWR_READY;
    cfg.activation_mask = 0x03u;       /* 允许 0x00 默认 + 0x01 WWH-OBD；0xE0 未开 */
    cfg.require_oem_auth = false;
    cfg.initial_inactivity_ms = APP_DOIP_INITIAL_INACTIVITY_MS;
    cfg.general_inactivity_ms = APP_DOIP_GENERAL_INACTIVITY_MS;
    cfg.alive_check_ms = APP_DOIP_ALIVE_CHECK_MS;
    cfg.announce_num = APP_DOIP_ANNOUNCE_NUM;
    cfg.announce_interval_ms = APP_DOIP_ANNOUNCE_INTERVAL_MS;
    cfg.announce_wait_ms = APP_DOIP_ANNOUNCE_WAIT_MS;
    cfg.announce_period_ms = 0u;       /* 0 = 只有上电那 3 次（周期声明用 doip timer period 开） */
    cfg.max_conn = APP_DOIP_MAX_CONN;
    cfg.max_payload = APP_DOIP_MAX_PAYLOAD;     /* DoIP 帧能收 4096 B */
    cfg.max_diag_uds = APP_UDS_MAX_REQ;         /* 交 UDS 的只有 512 B（app_uds 请求缓冲上限） */
    cfg.diag_rx = on_diag;
    cfg.diag_ctx = NULL;
    cfg.now_us = board_now_us;
    cfg.now_ctx = NULL;
    cfg.verbose = true;
    return app_doip_init(&s_doip, &cfg);
}

/* ---------------- shell 命令 ---------------- */

static void doip_usage(void)
{
    printf("\r\n[doip] 用法：\r\n");
    printf("  doip start|stop          起/停 TCP+UDP 13400\r\n");
    printf("  doip stat                全局统计\r\n");
    printf("  doip conn                连接表\r\n");
    printf("  doip announce [n]        立即发 n 次车辆声明（0=配置值）\r\n");
    printf("  doip vin [17字符]        读/写 VIN（同步到 UDS DID F190）\r\n");
    printf("  doip eid [12位HEX]       读/写 EID\r\n");
    printf("  doip route               路由激活策略\r\n");
    printf("  doip timer <名> <ms>     initial|general|alive|announce|period\r\n");
    printf("  doip alive [idx]         对连接做 alive check（0x0007）\r\n");
    printf("  doip selftest            DoIP 编解码固定向量自检\r\n");
    printf("  doip bench [n] [cnt]     大报文吞吐注入（n 字节 0x8001 x cnt 条）\r\n");
    printf("  doip uds                 UDS 服务端统计\r\n");
}

static void cmd_doip_stat(void)
{
    app_doip_print(&s_doip);
    app_doip_print_conns(&s_doip);
    printf("[doip] uds bridge: rx=%lu tx=%lu drop=%lu qdrop=%lu\r\n",
           (unsigned long) s_uds_rx, (unsigned long) s_uds_tx, (unsigned long) s_uds_drop,
           (unsigned long) s_q_drop);
}

static void cmd_doip(int argc, char *argv[])
{
    const char *sub = (argc >= 2) ? argv[1] : NULL;

    if (sub == NULL) {
        doip_usage();
        cmd_doip_stat();
        return;
    }
    if (strcmp(sub, "start") == 0) {
        int rc = app_doip_start(&s_doip);
        printf("[doip] start rc=%d %s\r\n", rc, (rc == 0) ? "PASS" : "FAIL");
        return;
    }
    if (strcmp(sub, "stop") == 0) {
        int rc = app_doip_stop(&s_doip);
        printf("[doip] stop rc=%d\r\n", rc);
        return;
    }
    if (strcmp(sub, "stat") == 0) {
        cmd_doip_stat();
        return;
    }
    if (strcmp(sub, "conn") == 0) {
        app_doip_print_conns(&s_doip);
        return;
    }
    if (strcmp(sub, "announce") == 0) {
        uint32_t n = (argc >= 3) ? (uint32_t) atoi(argv[2]) : 0u;
        int rc = app_doip_send_announce(&s_doip, n);
        printf("[doip] announce(%lu) rc=%d tx=%lu\r\n", (unsigned long) n, rc,
               (unsigned long) s_doip.st_announce_tx);
        return;
    }
    if (strcmp(sub, "vin") == 0) {
        if (argc >= 3) {
            if (app_doip_set_vin(&s_doip, argv[2]) != 0) {
                printf("[doip] VIN 必须是 17 个字符（当前 '%s' 长度 %u）FAIL\r\n",
                       argv[2], (unsigned) strlen(argv[2]));
                return;
            }
            /* 同步写回 UDS DID F190，保证两条路径读到同一个 VIN */
            for (uint32_t i = 0; i < s_did_count; i++) {
                if ((s_dids[i].did == 0xF190u) && (s_dids[i].len == 17u)) {
                    memcpy(s_dids[i].data, argv[2], 17u);
                }
            }
        }
        printf("[doip] VIN=%.17s\r\n", s_doip.cfg.vin);
        return;
    }
    if (strcmp(sub, "eid") == 0) {
        if (argc >= 3) {
            const char *h = argv[2];
            uint8_t eid[6];
            uint32_t v = 0;
            if (strlen(h) != 12u) {
                printf("[doip] EID 必须是 12 位 HEX（当前长度 %u）FAIL\r\n", (unsigned) strlen(h));
                return;
            }
            for (uint32_t i = 0; i < 6u; i++) {
                char c1 = h[i * 2u];
                char c2 = h[i * 2u + 1u];
                v = 0;
                for (int k = 0; k < 2; k++) {
                    char c = (k == 0) ? c1 : c2;
                    v <<= 4;
                    if ((c >= '0') && (c <= '9')) {
                        v |= (uint32_t) (c - '0');
                    } else if ((c >= 'a') && (c <= 'f')) {
                        v |= (uint32_t) (c - 'a' + 10);
                    } else if ((c >= 'A') && (c <= 'F')) {
                        v |= (uint32_t) (c - 'A' + 10);
                    } else {
                        printf("[doip] EID 含非 HEX 字符 FAIL\r\n");
                        return;
                    }
                }
                eid[i] = (uint8_t) v;
            }
            (void) app_doip_set_eid(&s_doip, eid);
        }
        printf("[doip] EID=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
               s_doip.cfg.eid[0], s_doip.cfg.eid[1], s_doip.cfg.eid[2],
               s_doip.cfg.eid[3], s_doip.cfg.eid[4], s_doip.cfg.eid[5]);
        return;
    }
    if (strcmp(sub, "route") == 0) {
        printf("[doip] tester_addr=0x%04X activation_mask=0x%02X require_oem=%u oem_key=%02X%02X%02X%02X\r\n",
               (unsigned) s_doip.cfg.tester_addr, (unsigned) s_doip.cfg.activation_mask,
               (unsigned) s_doip.cfg.require_oem_auth, s_doip.cfg.oem_key[0],
               s_doip.cfg.oem_key[1], s_doip.cfg.oem_key[2], s_doip.cfg.oem_key[3]);
        printf("[doip] 已激活连接：\r\n");
        app_doip_print_conns(&s_doip);
        return;
    }
    if (strcmp(sub, "timer") == 0) {
        if (argc < 4) {
            printf("[doip] 用法：doip timer <initial|general|alive|announce|period> <ms>\r\n");
            return;
        }
        uint32_t v = (uint32_t) atoi(argv[3]);
        if (strcmp(argv[2], "initial") == 0) {
            s_doip.cfg.initial_inactivity_ms = v;
        } else if (strcmp(argv[2], "general") == 0) {
            s_doip.cfg.general_inactivity_ms = v;
        } else if (strcmp(argv[2], "alive") == 0) {
            s_doip.cfg.alive_check_ms = v;
        } else if (strcmp(argv[2], "announce") == 0) {
            s_doip.cfg.announce_interval_ms = v;
        } else if (strcmp(argv[2], "period") == 0) {
            s_doip.cfg.announce_period_ms = v;
            s_doip.ann_period_next_ms = app_lwip_now_ms() + v;
        } else {
            printf("[doip] 未知定时器 '%s'\r\n", argv[2]);
            return;
        }
        printf("[doip] timer %s = %lu ms OK\r\n", argv[2], (unsigned long) v);
        return;
    }
    if (strcmp(sub, "alive") == 0) {
        int idx = (argc >= 3) ? atoi(argv[2]) : -1;
        int rc = app_doip_alive_check(&s_doip, idx);
        printf("[doip] alive check idx=%d rc=%d tx=%lu timeout=%lu\r\n", rc, rc,
               (unsigned long) s_doip.st_alive_tx, (unsigned long) s_doip.st_alive_timeout);
        return;
    }
    if (strcmp(sub, "selftest") == 0) {
        uint32_t checks = 0;
        uint32_t fail = 0;
        (void) app_doip_wire_selftest(&checks, &fail);
        printf("[doip] selftest: %lu 项检查，%lu 失败 -> %s\r\n",
               (unsigned long) checks, (unsigned long) fail, (fail == 0u) ? "PASS" : "FAIL");
        return;
    }
    if (strcmp(sub, "bench") == 0) {
        uint32_t n = (argc >= 3) ? (uint32_t) atoi(argv[2]) : 4000u;
        uint32_t cnt = (argc >= 4) ? (uint32_t) atoi(argv[3]) : 1u;
        int rc;
        rc = doip_bench_send(n, cnt);
        printf("[doip] bench payload=%lu B x %lu 条 rc=%d diag_tx=%lu big=%lu -> %s\r\n",
               (unsigned long) n, (unsigned long) cnt, rc, (unsigned long) s_doip.st_diag_tx,
               (unsigned long) s_doip.st_big_msg, (rc == 0) ? "PASS" : "FAIL");
        return;
    }
    if (strcmp(sub, "uds") == 0) {
        printf("[doip] UDS session=%s sec=%u req=%lu pos=%lu nrc=%lu pending=%lu s3=%lu\r\n",
               app_uds_session_name(s_uds.session), (unsigned) s_uds.security,
               (unsigned long) s_uds.stat_req, (unsigned long) s_uds.stat_resp_pos,
               (unsigned long) s_uds.stat_resp_nrc, (unsigned long) s_uds.stat_pending,
               (unsigned long) s_uds.stat_s3_timeout);
        printf("[doip] DIDs=%lu routines=%lu (F190 VIN=%.17s)\r\n",
               (unsigned long) s_did_count, (unsigned long) s_routine_count,
               (const char *) s_dids[0].data);
        return;
    }
    doip_usage();
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 doip, cmd_doip, DoIP server: doip start|stop|stat|conn|announce|vin|eid|route|timer|alive|selftest|bench|uds);

/* ---------------- main ---------------- */

int main(void)
{
    int rc;
    uint32_t checks = 0;
    uint32_t fail = 0;

    board_init();
    bsp_init();

    printf("\r\n[app] doip_server: DoIP(ISO 13400-2) + UDS(ISO 14229-1) over TCP/UDP 13400\r\n");

    rc = app_lwip_init(NULL);
    printf("[app] lwip init rc=%d (0=link up, 1=link down 可继续等)\r\n", rc);
    if (rc < 0) {
        printf("[doip] FAIL: 网络底座初始化失败 rc=%d\r\n", rc);
        while (1) {
            bsp_shell_poll();
        }
    }

    if ((doip_setup() != 0) || (uds_setup() != 0)) {
        printf("[doip] FAIL: DoIP/UDS 初始化失败\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }
    rc = app_doip_start(&s_doip);
    printf("[doip] listen TCP+UDP %u rc=%d %s\r\n", (unsigned) APP_DOIP_PORT, rc,
           (rc == 0) ? "PASS" : "FAIL");

    /* 编解码自检（不依赖网络，先证明协议层是对的） */
    (void) app_doip_wire_selftest(&checks, &fail);
    printf("[doip] selftest: %lu 项检查，%lu 失败 -> %s\r\n",
           (unsigned long) checks, (unsigned long) fail, (fail == 0u) ? "PASS" : "FAIL");

    board_delay_ms(1500);       /* 等 hwrun 的串口采集起来 */
    app_doip_print(&s_doip);
    printf("[doip] ready: 主机跑 bash host/doip/run_client.sh all\r\n");

    while (1) {
        app_lwip_poll();        /* 收包：0x8002 肯定应答在这一步发出去 */
        diag_pump();            /* 再跑 UDS（慢服务阻塞也不影响上一步的 ACK） */
        app_doip_poll(&s_doip);
        app_uds_poll(&s_uds, board_now_us(NULL));
        bsp_shell_poll();
    }
}
