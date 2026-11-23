/*
 * someip_rpc - SOME/IP 方法 RPC（SOME/IP-SD OfferService + REQUEST/RESPONSE）
 *
 * 服务定义（与 host/someip/ 上位机、第 35 章一致）
 *   service 0x1234 / instance 0x0001 / major 0x01 / interface 0x01 / UDP 30501
 *   method 0x0001 add(u32,u32) -> u32
 *   method 0x0002 get_version() -> string(u32 len + chars)
 *   method 0x0003 set_led(bool) -> bool(4 B, 低字节有效)
 *   field  0x0004 temperature getter() -> f32
 *   field  0x0005 temperature setter(f32) -> f32
 *
 * 硬件: hpm5e31_1 ENET RGMII + YT8011AN <-> 千兆车载以太网盒 <-> 主机 enp6s0
 *       板 192.168.2.100/24，主机 192.168.2.1/24
 *
 * shell: stat | run | selftest | offer | stopoffer | sub | ip
 */
#include <string.h>
#include <stdio.h>
#include "bsp.h"
#include "board.h"
#include "hpm_adc16_drv.h"
#include "hpm_csr_drv.h"
#include "app_someip.h"
#include "app_someip_wire.h"
#include "app_someip_sd.h"
#include "app_someip_tp.h"
#include "app_someip_net.h"

#define APP_SERVICE_ID      0x1234U
#define APP_INSTANCE_ID     0x0001U
#define APP_MAJOR_VERSION   0x01U
#define APP_MINOR_VERSION   0x00000001UL
#define APP_INTERFACE_VER   0x01U
#define APP_EVENTGROUP_ID   0x0001U

#define M_ADD          0x0001U
#define M_GET_VERSION  0x0002U
#define M_SET_LED      0x0003U
#define M_TEMP_GET     0x0004U
#define M_TEMP_SET     0x0005U
#define M_GET_TIME     0x0006U   /* 返回 u32 微秒时基，供上位机做 NTP 式单向时延估计 */

#define E_COUNTER      0x8001U
#define E_ADC          0x8002U
#define E_TEMP         0x8004U

#define APP_ADC_BASE        HPM_ADC0
#define APP_ADC_CH          (1U)
#define APP_ADC_SAMPLE_CYCLE (20U)

static app_someip_t     g_srv;
static someip_tp_reasm_t g_tp;

static int32_t  g_temp_mc   = 31500;      /* 温度"字段"，单位 0.001 摄氏度，初值 31.5 C */
static uint32_t g_led_state;
static uint32_t g_counter;
static uint16_t g_adc_raw;
static bool     g_adc_ok;

/* ---------------- 浮点 & 字段编解码 ---------------- */
static void put_f32(uint8_t *p, float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    someip_put_u32(p, u);
}

static float get_f32(const uint8_t *p)
{
    uint32_t u = someip_get_u32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

/* ---------------- 方法实现 ---------------- */
static uint8_t m_add(app_someip_t *s, const someip_msg_t *req, uint8_t *reply, uint16_t cap,
                     uint16_t *reply_len, void *user)
{
    (void)s; (void)user;
    if (req->payload_len < 8U) {
        return SOMEIP_RC_MALFORMED_MESSAGE;
    }
    if (cap < 4U) {
        return SOMEIP_RC_NOT_OK;
    }
    uint32_t a = someip_get_u32(&req->payload[0]);
    uint32_t b = someip_get_u32(&req->payload[4]);
    someip_put_u32(reply, a + b);
    *reply_len = 4U;
    return SOMEIP_RC_OK;
}

static uint8_t m_get_version(app_someip_t *s, const someip_msg_t *req, uint8_t *reply, uint16_t cap,
                             uint16_t *reply_len, void *user)
{
    (void)req; (void)user;
    const char *v = app_someip_ver_string();
    uint16_t n = (uint16_t)strlen(v);
    if ((uint32_t)cap < (uint32_t)n + 4U) {
        return SOMEIP_RC_NOT_OK;
    }
    /* SOME/IP 字符串：u32 长度 + 字符（不含结尾 0），与 vsomeip 的 serializer 一致 */
    someip_put_u32(reply, n);
    memcpy(&reply[4], v, n);
    *reply_len = (uint16_t)(4U + n);
    (void)s;
    return SOMEIP_RC_OK;
}

static uint8_t m_set_led(app_someip_t *s, const someip_msg_t *req, uint8_t *reply, uint16_t cap,
                         uint16_t *reply_len, void *user)
{
    (void)s; (void)user;
    if (req->payload_len < 1U) {
        return SOMEIP_RC_MALFORMED_MESSAGE;
    }
    if (cap < 4U) {
        return SOMEIP_RC_NOT_OK;
    }
    g_led_state = (req->payload[0] != 0U) ? 1U : 0U;
    board_led_write((uint8_t)g_led_state);
    /* bool 回包按 vsomeip 对齐规则补到 4 B */
    memset(reply, 0, 4U);
    reply[0] = (uint8_t)g_led_state;
    *reply_len = 4U;
    return SOMEIP_RC_OK;
}

static uint8_t m_temp_get(app_someip_t *s, const someip_msg_t *req, uint8_t *reply, uint16_t cap,
                          uint16_t *reply_len, void *user)
{
    (void)req; (void)s; (void)user;
    if (cap < 4U) {
        return SOMEIP_RC_NOT_OK;
    }
    put_f32(reply, (float)g_temp_mc / 1000.0f);
    *reply_len = 4U;
    return SOMEIP_RC_OK;
}

static uint8_t m_temp_set(app_someip_t *s, const someip_msg_t *req, uint8_t *reply, uint16_t cap,
                          uint16_t *reply_len, void *user)
{
    (void)s; (void)user;
    if (req->payload_len < 4U) {
        return SOMEIP_RC_MALFORMED_MESSAGE;
    }
    if (cap < 4U) {
        return SOMEIP_RC_NOT_OK;
    }
    float f = get_f32(req->payload);
    g_temp_mc = (int32_t)(f * 1000.0f + ((f >= 0.0f) ? 0.5f : -0.5f));
    put_f32(reply, (float)g_temp_mc / 1000.0f);
    *reply_len = 4U;
    return SOMEIP_RC_OK;
}

/* ---------------- 事件 ---------------- */
/* 板端微秒时基（mcycle / CPU MHz），用于上位机 NTP 式时延估计 */
static uint32_t board_now_us(void)
{
    static uint32_t cpu_mhz;
    if (cpu_mhz == 0U) {
        cpu_mhz = (uint32_t)(clock_get_frequency(clock_cpu0) / 1000000U);
    }
    return (uint32_t)(hpm_csr_get_core_mcycle() / (uint64_t)cpu_mhz);
}

static uint8_t m_get_time(app_someip_t *s, const someip_msg_t *req, uint8_t *reply, uint16_t cap,
                          uint16_t *reply_len, void *user)
{
    (void)req; (void)s; (void)user;
    if (cap < 4U) {
        return SOMEIP_RC_NOT_OK;
    }
    someip_put_u32(reply, board_now_us());
    *reply_len = 4U;
    return SOMEIP_RC_OK;
}

/* 事件 0x8001：u32 计数器 + u32 板端发送时刻(us)，上位机据此算单向时延 */
static uint16_t fill_counter(app_someip_t *s, uint8_t *buf, uint16_t cap, void *user)
{
    (void)s; (void)user;
    if (cap < 8U) {
        return 0U;
    }
    someip_put_u32(&buf[0], ++g_counter);
    someip_put_u32(&buf[4], board_now_us());
    return 8U;
}

static uint16_t fill_adc(app_someip_t *s, uint8_t *buf, uint16_t cap, void *user)
{
    (void)s; (void)user;
    if (cap < 4U) {
        return 0U;
    }
    uint16_t raw = 0;
    if (g_adc_ok) {
        (void)adc16_get_oneshot_result(APP_ADC_BASE, APP_ADC_CH, &raw);
        g_adc_raw = raw;
    }
    someip_put_u32(buf, (uint32_t)g_adc_raw);
    return 4U;
}

static uint16_t fill_temp(app_someip_t *s, uint8_t *buf, uint16_t cap, void *user)
{
    (void)s; (void)user;
    if (cap < 4U) {
        return 0U;
    }
    put_f32(buf, (float)g_temp_mc / 1000.0f);
    return 4U;
}
/* ---------------- 板端自测：方法级回环（不需要上位机） ---------------- */
#define TEST_SLOTS     4
#define TEST_SLOT_LEN  1500

static struct {
    uint8_t  buf[TEST_SLOTS][TEST_SLOT_LEN];
    uint16_t len[TEST_SLOTS];
    uint32_t count;
} s_tcap;

static int test_send(void *ctx, const uint8_t ip[4], uint16_t port, const uint8_t *buf, uint16_t len)
{
    (void)ctx; (void)ip; (void)port;
    if ((s_tcap.count < TEST_SLOTS) && (len <= TEST_SLOT_LEN)) {
        memcpy(s_tcap.buf[s_tcap.count], buf, len);
        s_tcap.len[s_tcap.count] = len;
    }
    s_tcap.count++;
    return 0;
}

/* 把一条 REQUEST 灌进协议栈，取回 RESPONSE/ERROR 的载荷 */
static int test_call(uint16_t method, const uint8_t *payload, uint16_t plen,
                     uint8_t *rsp, uint16_t *rsp_len, uint8_t *rc, uint8_t msg_type)
{
    someip_send_fn saved = g_srv.cfg.send;
    void *saved_ctx = g_srv.cfg.net_ctx;
    uint8_t frame[1600];
    uint16_t n;
    someip_msg_t m;

    s_tcap.count = 0;
    g_srv.cfg.send = test_send;
    g_srv.cfg.net_ctx = &s_tcap;
    n = app_someip_encode_header(frame, sizeof(frame), APP_SERVICE_ID, method, 0x00A5U, 0x1234U,
                                 APP_INTERFACE_VER, msg_type, SOMEIP_RC_OK, plen);
    if (plen != 0U) {
        memcpy(&frame[n], payload, plen);
    }
    app_someip_on_rx(&g_srv, g_srv.cfg.local_ip, APP_SOMEIP_SERVICE_PORT, frame, (uint16_t)(n + plen));
    g_srv.cfg.send = saved;
    g_srv.cfg.net_ctx = saved_ctx;

    if ((msg_type == SOMEIP_MT_REQUEST_NO_RETURN) && (s_tcap.count == 0U)) {
        return 0;   /* 无回包，符合 REQUEST_NO_RETURN 语义 */
    }
    if (s_tcap.count != 1U) {
        return -1;
    }
    if (app_someip_decode(s_tcap.buf[0], s_tcap.len[0], &m) != 0) {
        return -2;
    }
    *rc = m.return_code;
    if (rsp != NULL) {
        memcpy(rsp, m.payload, m.payload_len);
    }
    if (rsp_len != NULL) {
        *rsp_len = m.payload_len;
    }
    return 0;
}

static int cmd_run(int argc, char *argv[])
{
    (void)argc; (void)argv;
    int fails = 0;
    uint8_t rsp[64];
    uint16_t rlen = 0;
    uint8_t rc = 0xFFU;
    uint8_t req[8];
    int r;

    printf("[run] ---- 方法级回环自测 ----\r\n");

    /* 1) add(2,3) == 5 */
    someip_put_u32(&req[0], 2U);
    someip_put_u32(&req[4], 3U);
    r = test_call(M_ADD, req, 8U, rsp, &rlen, &rc, SOMEIP_MT_REQUEST);
    bool ok = (r == 0) && (rc == SOMEIP_RC_OK) && (rlen == 4U) && (someip_get_u32(rsp) == 5U);
    printf("[run] add(2,3)            : %s (%lu)\r\n", ok ? "PASS" : "FAIL",
           (unsigned long)((r == 0 && rlen >= 4U) ? someip_get_u32(rsp) : 0U));
    fails += ok ? 0 : 1;

    /* 2) add(0xFFFFFFFF,1) 回绕为 0 */
    someip_put_u32(&req[0], 0xFFFFFFFFU);
    someip_put_u32(&req[4], 1U);
    r = test_call(M_ADD, req, 8U, rsp, &rlen, &rc, SOMEIP_MT_REQUEST);
    ok = (r == 0) && (rc == SOMEIP_RC_OK) && (someip_get_u32(rsp) == 0U);
    printf("[run] add(0xFFFFFFFF,1)   : %s\r\n", ok ? "PASS" : "FAIL");
    fails += ok ? 0 : 1;

    /* 3) get_version: u32 长度 + 字符串 */
    r = test_call(M_GET_VERSION, NULL, 0U, rsp, &rlen, &rc, SOMEIP_MT_REQUEST);
    ok = (r == 0) && (rc == SOMEIP_RC_OK) && (rlen >= 4U) &&
         (someip_get_u32(rsp) == (rlen - 4U)) &&
         (memcmp(&rsp[4], app_someip_ver_string(), rlen - 4U) == 0);
    printf("[run] get_version         : %s (%.*s)\r\n", ok ? "PASS" : "FAIL",
           ok ? (int)(rlen - 4U) : 0, ok ? (const char *)&rsp[4] : "");
    fails += ok ? 0 : 1;

    /* 4) set_led(1/0) 回读状态 */
    req[0] = 1U;
    r = test_call(M_SET_LED, req, 1U, rsp, &rlen, &rc, SOMEIP_MT_REQUEST);
    bool on_ok = (r == 0) && (rc == SOMEIP_RC_OK) && (rlen == 4U) && (rsp[0] == 1U) && (g_led_state == 1U);
    req[0] = 0U;
    r = test_call(M_SET_LED, req, 1U, rsp, &rlen, &rc, SOMEIP_MT_REQUEST);
    ok = on_ok && (r == 0) && (rc == SOMEIP_RC_OK) && (rsp[0] == 0U) && (g_led_state == 0U);
    printf("[run] set_led(1)->(0)     : %s\r\n", ok ? "PASS" : "FAIL");
    fails += ok ? 0 : 1;

    /* 5) 温度字段 getter/setter */
    r = test_call(M_TEMP_GET, NULL, 0U, rsp, &rlen, &rc, SOMEIP_MT_REQUEST);
    ok = (r == 0) && (rc == SOMEIP_RC_OK) && (rlen == 4U) &&
         ((int32_t)(get_f32(rsp) * 1000.0f + 0.5f) == 31500);
    printf("[run] temp get (31.5 C)   : %s\r\n", ok ? "PASS" : "FAIL");
    fails += ok ? 0 : 1;

    put_f32(req, 42.25f);
    r = test_call(M_TEMP_SET, req, 4U, rsp, &rlen, &rc, SOMEIP_MT_REQUEST);
    int32_t got = (r == 0) ? (int32_t)(get_f32(rsp) * 1000.0f + 0.5f) : 0;
    ok = (r == 0) && (rc == SOMEIP_RC_OK) && (got == 42250) && (g_temp_mc == 42250);
    printf("[run] temp set 42.25 C    : %s (readback %ld mc)\r\n", ok ? "PASS" : "FAIL", (long)got);
    fails += ok ? 0 : 1;

    /* 6) 未知方法 -> ERROR(0x03) */
    r = test_call(0x00FFU, NULL, 0U, rsp, &rlen, &rc, SOMEIP_MT_REQUEST);
    ok = (r == 0) && (rc == SOMEIP_RC_UNKNOWN_METHOD);
    printf("[run] unknown method      : %s (rc=0x%02X)\r\n", ok ? "PASS" : "FAIL", (unsigned)rc);
    fails += ok ? 0 : 1;

    /* 7) REQUEST_NO_RETURN 不回包 */
    someip_put_u32(&req[0], 7U);
    someip_put_u32(&req[4], 8U);
    r = test_call(M_ADD, req, 8U, NULL, NULL, &rc, SOMEIP_MT_REQUEST_NO_RETURN);
    ok = (r == 0) && (s_tcap.count == 0U);
    printf("[run] request_no_return   : %s\r\n", ok ? "PASS" : "FAIL");
    fails += ok ? 0 : 1;

    printf("[run] %s (%d failed)\r\n", (fails == 0) ? "PASS" : "FAIL", fails);
    return fails;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 run, cmd_run, method level loopback selftest);
/* ---------------- shell 命令 ---------------- */
static void cmd_stat(int argc, char *argv[])
{
    (void)argc; (void)argv;
    const someip_stats_t *st = &g_srv.stats;
    printf("[someip] link=%s uptime=%lu ms offer_state=%u\r\n",
           app_someip_net_link_up() ? "UP" : "DOWN", (unsigned long)bsp_uptime_ms(),
           (unsigned)g_srv.offer_state);
    printf("[someip] rx total=%lu req=%lu req_nr=%lu sd=%lu malformed=%lu wrong_type=%lu\r\n",
           (unsigned long)st->rx_total, (unsigned long)st->rx_requests,
           (unsigned long)st->rx_requests_no_return, (unsigned long)st->rx_sd,
           (unsigned long)st->err_malformed, (unsigned long)st->err_wrong_type);
    printf("[someip] tx rsp=%lu offer=%lu ack=%lu notif=%lu | err svc=%lu method=%lu\r\n",
           (unsigned long)st->tx_responses, (unsigned long)st->tx_sd_offer,
           (unsigned long)st->tx_sd_ack, (unsigned long)st->tx_notifications,
           (unsigned long)st->err_unknown_service, (unsigned long)st->err_unknown_method);
    printf("[someip] tp seg_rx=%lu msg_rx=%lu seg_tx=%lu drop=%lu | max_rx=%lu B\r\n",
           (unsigned long)st->tp_seg_rx, (unsigned long)st->tp_msg_rx,
           (unsigned long)st->tp_seg_tx, (unsigned long)st->tp_drop,
           (unsigned long)st->max_rx_len);
    printf("[someip] subs added=%lu removed=%lu expired=%lu active=%d | last client=0x%04X session=%u\r\n",
           (unsigned long)st->sub_added, (unsigned long)st->sub_removed,
           (unsigned long)st->sub_expired, app_someip_sub_count(&g_srv, APP_EVENTGROUP_ID),
           (unsigned)st->last_client_id, (unsigned)st->last_session_id);
    printf("[someip] led=%lu temp=%ld.%03ld C counter=%lu adc=%u(uptime %lu ms)\r\n",
           (unsigned long)g_led_state, (long)(g_temp_mc / 1000), (long)(g_temp_mc % 1000),
           (unsigned long)g_counter, (unsigned)g_adc_raw, (unsigned long)bsp_uptime_ms());
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, show SOME/IP counters);

static void cmd_selftest(int argc, char *argv[])
{
    (void)argc; (void)argv;
    (void)app_someip_selftest(&g_srv, true);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 selftest, cmd_selftest, protocol level selftest (no host needed));

static void cmd_offer(int argc, char *argv[])
{
    (void)argc; (void)argv;
    int r = app_someip_offer(&g_srv);
    printf("[someip] offer %s (multicast 224.244.224.245:30490, ttl=%lu s)\r\n",
           (r == 0) ? "sent" : "FAILED", (unsigned long)g_srv.cfg.offer_ttl_s);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 offer, cmd_offer, send OfferService now);

static void cmd_stopoffer(int argc, char *argv[])
{
    (void)argc; (void)argv;
    (void)app_someip_stop_offer(&g_srv);
    printf("[someip] StopOfferService sent (ttl=0)\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stopoffer, cmd_stopoffer, send StopOfferService (ttl=0));

static void cmd_subs(int argc, char *argv[])
{
    (void)argc; (void)argv;
    int n = 0;
    for (int i = 0; i < SOMEIP_MAX_SUBSCRIBERS; i++) {
        const someip_subscriber_t *sub = &g_srv.subs[i];
        if (sub->active) {
            n++;
            printf("[someip] sub%d eg=0x%04X endpoint=%u.%u.%u.%u:%u ttl=%lu s\r\n",
                   n, (unsigned)sub->eventgroup, (unsigned)sub->ip[0], (unsigned)sub->ip[1],
                   (unsigned)sub->ip[2], (unsigned)sub->ip[3], (unsigned)sub->port,
                   (unsigned long)sub->ttl_s);
        }
    }
    printf("[someip] %d subscriber(s)\r\n", n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sub, cmd_subs, list eventgroup subscribers);

static void cmd_ip(int argc, char *argv[])
{
    (void)argc; (void)argv;
    app_someip_net_info();
    printf("[net] rx_drop=%lu\r\n", (unsigned long)app_someip_net_rx_drop());
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ip, cmd_ip, show netif info);

/* ---------------- ADC16（事件 0x8002 的数据来源） ---------------- */
static bool adc_init(void)
{
    uint32_t clk_in = board_init_adc_clock(APP_ADC_BASE, true);
    uint32_t div = 0;
    adc16_config_t cfg;
    adc16_channel_config_t ch_cfg;

    board_init_adc16_pins();
    if (adc16_calc_clock_divider(clk_in, ADC16_SOC_CONV_CLK_FREQ_MAX, &div) != status_success) {
        return false;
    }
    adc16_get_default_config(&cfg);
    cfg.res = adc16_res_16_bits;
    cfg.conv_mode = adc16_conv_mode_oneshot;
    cfg.adc_clk_div = div;
#if !defined(HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB) || !HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB
    cfg.sel_sync_ahb = (clk_adc_src_ahb0 == clock_get_source(BOARD_APP_ADC16_CLK_NAME)) ? true : false;
#endif
    if (adc16_init(APP_ADC_BASE, &cfg) != status_success) {
        return false;
    }
    adc16_get_channel_default_config(&ch_cfg);
    ch_cfg.ch = APP_ADC_CH;
    ch_cfg.sample_cycle = APP_ADC_SAMPLE_CYCLE;
    if (adc16_init_channel(APP_ADC_BASE, &ch_cfg) != status_success) {
        return false;
    }
#if defined(ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT) && ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT
    adc16_enable_oneshot_mode(APP_ADC_BASE);
#endif
    adc16_set_blocking_read(APP_ADC_BASE);
    return true;
}

/* ---------------- main ---------------- */
int main(void)
{
    someip_cfg_t cfg;

    bsp_init();
    printf("\r\n=== someip_rpc: SOME/IP over UDP (SD + Request/Response) ===\r\n");
    printf("[someip] service 0x%04X instance 0x%04X major %u iface %u, %s\r\n",
           (unsigned)APP_SERVICE_ID, (unsigned)APP_INSTANCE_ID, (unsigned)APP_MAJOR_VERSION,
           (unsigned)APP_INTERFACE_VER, app_someip_ver_string());

    if (app_someip_net_init() != 0) {
        printf("[net] ENET/lwIP init FAILED\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }
    app_someip_net_info();

    memset(&cfg, 0, sizeof(cfg));
    cfg.service_id = APP_SERVICE_ID;
    cfg.instance_id = APP_INSTANCE_ID;
    cfg.major_version = APP_MAJOR_VERSION;
    cfg.interface_version = APP_INTERFACE_VER;
    cfg.minor_version = APP_MINOR_VERSION;
    cfg.local_ip[0] = APP_SOMEIP_LOCAL_IP0;
    cfg.local_ip[1] = APP_SOMEIP_LOCAL_IP1;
    cfg.local_ip[2] = APP_SOMEIP_LOCAL_IP2;
    cfg.local_ip[3] = APP_SOMEIP_LOCAL_IP3;
    cfg.sd_port = APP_SOMEIP_SD_PORT;
    cfg.service_port = APP_SOMEIP_SERVICE_PORT;
    cfg.offer_ttl_s = 3U;
    cfg.sd_multicast_ip[0] = APP_SOMEIP_MCAST_IP0;
    cfg.sd_multicast_ip[1] = APP_SOMEIP_MCAST_IP1;
    cfg.sd_multicast_ip[2] = APP_SOMEIP_MCAST_IP2;
    cfg.sd_multicast_ip[3] = APP_SOMEIP_MCAST_IP3;
    cfg.sd_multicast_port = APP_SOMEIP_SD_PORT;
    cfg.send = app_someip_net_send;
    cfg.now_ms = app_someip_net_now_ms;

    app_someip_init(&g_srv, &cfg, &g_tp);
    app_someip_register_method(&g_srv, M_ADD, m_add, NULL);
    app_someip_register_method(&g_srv, M_GET_VERSION, m_get_version, NULL);
    app_someip_register_method(&g_srv, M_SET_LED, m_set_led, NULL);
    app_someip_register_method(&g_srv, M_TEMP_GET, m_temp_get, NULL);
    app_someip_register_method(&g_srv, M_TEMP_SET, m_temp_set, NULL);
    app_someip_register_method(&g_srv, M_GET_TIME, m_get_time, NULL);
    app_someip_register_event(&g_srv, E_COUNTER, APP_EVENTGROUP_ID, 100U, fill_counter, NULL);
    app_someip_register_event(&g_srv, E_ADC, APP_EVENTGROUP_ID, 10U, fill_adc, NULL);
    app_someip_register_event(&g_srv, E_TEMP, APP_EVENTGROUP_ID, 500U, fill_temp, NULL);

    g_adc_ok = adc_init();
    printf("[adc] ADC16 %s ch%u %s\r\n", "ADC0", (unsigned)APP_ADC_CH,
           g_adc_ok ? "ready (PF26, 悬空=噪声底)" : "NOT available (event 0x8002 将发 0)");

    if (app_someip_net_open(&g_srv, APP_SOMEIP_SD_PORT, APP_SOMEIP_SERVICE_PORT) != 0) {
        printf("[someip] UDP socket open FAILED\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }

    /* 等链路起来（最多 3 s）再发 Offer，避免 SD 组播打在 DOWN 的网口上 */
    uint32_t t0 = bsp_uptime_ms();
    while (!app_someip_net_link_up() && ((bsp_uptime_ms() - t0) < 3000U)) {
        app_someip_net_poll();
    }
    (void)app_someip_offer(&g_srv);
    printf("[someip] OfferService -> 224.244.224.245:30490, endpoint 192.168.2.100:30501, ttl=%u s\r\n",
           (unsigned)g_srv.cfg.offer_ttl_s);
    printf("[someip] shell: stat | run | selftest | offer | stopoffer | sub | ip\r\n");

    while (1) {
        app_someip_net_poll();
        app_someip_poll(&g_srv, app_someip_net_now_ms(NULL));
        bsp_shell_poll();
    }
    return 0;
}
