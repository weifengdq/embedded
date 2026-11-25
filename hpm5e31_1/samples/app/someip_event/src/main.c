/*
 * someip_event - SOME/IP 事件（NOTIFICATION）发布 + 事件组订阅（SubscribeEventgroup/Ack）
 *
 * 服务同 someip_rpc（service 0x1234 instance 0x0001），重点是事件通路：
 *   event 0x8001 counter      100 ms  u32 计数 + u32 板端发送时刻(us)
 *   event 0x8002 adc           10 ms  u32 ADC16(ADC0.IN1/PF26) 16 bit 原始值
 *   event 0x8004 temperature  500 ms  f32 温度字段当前值
 *   事件组 0x0001
 *
 * 关键机制：
 *   - 事件只在"有订阅者"时才真正发包（app_someip_poll 里先查订阅表）；
 *   - 板端收到 SubscribeEventgroup 后回 SubscribeEventgroupAck，并把订阅者的事件端点
 *     （来自 SD 里的 IPv4 endpoint option）记进订阅表，TTL 到期自动摘除；
 *   - 上位机应每 TTL/2 重发一次订阅续约（host/someip/someip_client.py 默认 ttl=3 s）。
 *
 * shell: stat | run | selftest | offer | stopoffer | sub | ip | evstat
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
#define M_GET_TIME     0x0006U

#define E_COUNTER      0x8001U
#define E_ADC          0x8002U
#define E_TEMP         0x8004U

#define APP_ADC_BASE         HPM_ADC0
#define APP_ADC_CH           (1U)
#define APP_ADC_SAMPLE_CYCLE (20U)

/* 自测里模拟的订阅者事件端点 */
#define SIM_SUB_IP0 192U
#define SIM_SUB_IP1 168U
#define SIM_SUB_IP2 2U
#define SIM_SUB_IP3 1U
#define SIM_SUB_PORT 30600U

static app_someip_t      g_srv;
static someip_tp_reasm_t g_tp;

static int32_t  g_temp_mc = 31500;
static uint32_t g_counter;
static uint32_t g_ev_sent[3];
static uint16_t g_adc_raw;
static bool     g_adc_ok;

static uint32_t board_now_us(void)
{
    static uint32_t cpu_mhz;
    if (cpu_mhz == 0U) {
        cpu_mhz = (uint32_t)(clock_get_frequency(clock_cpu0) / 1000000U);
    }
    return (uint32_t)(hpm_csr_get_core_mcycle() / (uint64_t)cpu_mhz);
}

static void put_f32(uint8_t *p, float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    someip_put_u32(p, u);
}

/* ---------------- 事件填充 ---------------- */
static uint16_t fill_counter(app_someip_t *s, uint8_t *buf, uint16_t cap, void *user)
{
    (void)s; (void)user;
    if (cap < 8U) {
        return 0U;
    }
    someip_put_u32(&buf[0], ++g_counter);
    someip_put_u32(&buf[4], board_now_us());
    g_ev_sent[0]++;
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
    g_ev_sent[1]++;
    return 4U;
}

static uint16_t fill_temp(app_someip_t *s, uint8_t *buf, uint16_t cap, void *user)
{
    (void)s; (void)user;
    if (cap < 4U) {
        return 0U;
    }
    put_f32(buf, (float)g_temp_mc / 1000.0f);
    g_ev_sent[2]++;
    return 4U;
}

/* ---------------- 方法（事件例程只需要能应答几个基本方法，便于上位机联动验证） ---------------- */
static uint8_t m_add(app_someip_t *s, const someip_msg_t *req, uint8_t *reply, uint16_t cap,
                     uint16_t *reply_len, void *user)
{
    (void)s; (void)user;
    if ((req->payload_len < 8U) || (cap < 4U)) {
        return SOMEIP_RC_MALFORMED_MESSAGE;
    }
    someip_put_u32(reply, someip_get_u32(&req->payload[0]) + someip_get_u32(&req->payload[4]));
    *reply_len = 4U;
    return SOMEIP_RC_OK;
}

static uint8_t m_get_version(app_someip_t *s, const someip_msg_t *req, uint8_t *reply, uint16_t cap,
                             uint16_t *reply_len, void *user)
{
    (void)s; (void)req; (void)user;
    const char *v = app_someip_ver_string();
    uint16_t n = (uint16_t)strlen(v);
    if ((uint32_t)cap < (uint32_t)n + 4U) {
        return SOMEIP_RC_NOT_OK;
    }
    someip_put_u32(reply, n);
    memcpy(&reply[4], v, n);
    *reply_len = (uint16_t)(4U + n);
    return SOMEIP_RC_OK;
}

static uint8_t m_get_time(app_someip_t *s, const someip_msg_t *req, uint8_t *reply, uint16_t cap,
                          uint16_t *reply_len, void *user)
{
    (void)s; (void)req; (void)user;
    if (cap < 4U) {
        return SOMEIP_RC_NOT_OK;
    }
    someip_put_u32(reply, board_now_us());
    *reply_len = 4U;
    return SOMEIP_RC_OK;
}
/* ---------------- 板端自测：订阅 + 事件通路（不需要上位机） ---------------- */
#define SIM_SLOTS    8
#define SIM_SLOT_LEN 256

static struct {
    uint8_t  buf[SIM_SLOTS][SIM_SLOT_LEN];
    uint16_t len[SIM_SLOTS];
    uint32_t count;
    uint32_t notif[3];   /* counter / adc / temp */
} s_cap;

static int sim_send(void *ctx, const uint8_t ip[4], uint16_t port, const uint8_t *buf, uint16_t len)
{
    (void)ctx; (void)ip; (void)port;
    if ((s_cap.count < SIM_SLOTS) && (len <= SIM_SLOT_LEN)) {
        memcpy(s_cap.buf[s_cap.count], buf, len);
        s_cap.len[s_cap.count] = len;
    }
    s_cap.count++;
    return 0;
}

/* 手工构造 SubscribeEventgroup：entry(16) + IPv4 endpoint option(12) */
static uint16_t build_subscribe(uint8_t *buf, uint16_t cap)
{
    uint8_t *p;
    uint16_t plen = 4U + 4U + 16U + 4U + 12U;   /* flags+reserved | entries | entry | options |
                                                   option */
    if (cap < (uint16_t)(SOMEIP_HEADER_LEN + plen)) {
        return 0U;
    }
    p = &buf[SOMEIP_HEADER_LEN];
    p[0] = 0xC0U;
    p[1] = 0U;
    p[2] = 0U;
    p[3] = 0U;
    someip_put_u32(&p[4], 16U);
    uint8_t *e = &p[8];
    memset(e, 0, 16U);
    e[0] = SOMEIP_SD_ENTRY_SUBSCRIBE_EVENTGROUP;
    e[1] = 0U;
    e[2] = 0U;
    e[3] = 1U;                                  /* 1 个 option */
    someip_put_u16(&e[4], APP_SERVICE_ID);
    someip_put_u16(&e[6], APP_INSTANCE_ID);
    e[8] = APP_MAJOR_VERSION;
    someip_put_u24(&e[9], 3U);                  /* TTL 3 s */
    someip_put_u16(&e[12], 1U);                 /* counter = 1 */
    someip_put_u16(&e[14], APP_EVENTGROUP_ID);
    someip_put_u32(&p[24], 12U);                /* options length */
    uint8_t *o = &p[28];
    someip_put_u16(&o[0], 9U);
    o[2] = SOMEIP_SD_OPT_IPV4_ENDPOINT;
    o[3] = 0U;
    o[4] = SIM_SUB_IP0;
    o[5] = SIM_SUB_IP1;
    o[6] = SIM_SUB_IP2;
    o[7] = SIM_SUB_IP3;
    o[8] = 0U;
    o[9] = SOMEIP_SD_PROTO_UDP;
    someip_put_u16(&o[10], SIM_SUB_PORT);
    (void)app_someip_encode_header(buf, cap, SOMEIP_SD_SERVICE_ID, SOMEIP_SD_METHOD_ID,
                                   0x0000U, 1U, SOMEIP_SD_INTERFACE_VERSION,
                                   SOMEIP_MT_NOTIFICATION, SOMEIP_RC_OK, plen);
    return (uint16_t)(SOMEIP_HEADER_LEN + plen);
}

static int cmd_run(int argc, char *argv[])
{
    (void)argc; (void)argv;
    int fails = 0;
    someip_send_fn saved_send = g_srv.cfg.send;
    void *saved_ctx = g_srv.cfg.net_ctx;
    uint8_t frame[128];
    uint16_t n;
    uint8_t src[4] = { SIM_SUB_IP0, SIM_SUB_IP1, SIM_SUB_IP2, SIM_SUB_IP3 };

    printf("[run] ---- 订阅 + 事件通路自测（模拟上位机 192.168.2.1:%u）----\r\n", (unsigned)SIM_SUB_PORT);
    g_srv.cfg.send = sim_send;
    g_srv.cfg.net_ctx = &s_cap;
    memset(&s_cap, 0, sizeof(s_cap));

    /* 1) 灌一条 SubscribeEventgroup 进去，期望：加订阅 + 回 Ack */
    n = build_subscribe(frame, sizeof(frame));
    app_someip_on_rx(&g_srv, src, APP_SOMEIP_SD_PORT, frame, n);
    bool ok = (app_someip_sub_count(&g_srv, APP_EVENTGROUP_ID) == 1) && (s_cap.count == 1U);
    if (ok) {
        someip_msg_t m;
        someip_sd_msg_t sd;
        ok = (app_someip_decode(s_cap.buf[0], s_cap.len[0], &m) == 0) &&
             (app_someip_sd_parse(m.payload, m.payload_len, &sd) == 0) &&
             (sd.n_entries == 1) && (sd.entries[0].type == SOMEIP_SD_ENTRY_SUBSCRIBE_ACK) &&
             (sd.entries[0].eventgroup == APP_EVENTGROUP_ID) && (sd.entries[0].ttl == 3U);
    }
    printf("[run] subscribe -> ack      : %s\r\n", ok ? "PASS" : "FAIL");
    fails += ok ? 0 : 1;

    /* 2) 模拟 1 s 时间推进（10 ms 步长），统计三类事件应当发出的条数 */
    s_cap.count = 0;
    s_cap.notif[0] = s_cap.notif[1] = s_cap.notif[2] = 0;
    /* 注意：订阅表里的 last_renew_ms 用的是"真实"时基（sys_now），模拟时间必须从同一时基起算，
     * 否则 now - last_renew 下溢，订阅会被误判为过期（遇到过）。 */
    const uint32_t t_base = app_someip_net_now_ms(NULL);
    for (uint32_t t = t_base; t <= (t_base + 1000U); t += 10U) {
        s_cap.count = 0;            /* 每步重新捕获 */
        app_someip_poll(&g_srv, t);
        for (uint32_t i = 0; i < s_cap.count; i++) {
            someip_msg_t m;
            if (app_someip_decode(s_cap.buf[i], s_cap.len[i], &m) != 0) {
                continue;
            }
            if (m.message_type == SOMEIP_MT_NOTIFICATION) {
                if (m.method_id == E_COUNTER) {
                    s_cap.notif[0]++;
                } else if (m.method_id == E_ADC) {
                    s_cap.notif[1]++;
                } else if (m.method_id == E_TEMP) {
                    s_cap.notif[2]++;
                }
            }
        }
    }
    printf("[run] 1 s 内事件条数         : counter=%lu adc=%lu temp=%lu\r\n",
           (unsigned long)s_cap.notif[0], (unsigned long)s_cap.notif[1], (unsigned long)s_cap.notif[2]);
    ok = (s_cap.notif[0] >= 9U) && (s_cap.notif[0] <= 11U) &&
         (s_cap.notif[1] >= 90U) && (s_cap.notif[1] <= 101U) &&
         (s_cap.notif[2] >= 1U) && (s_cap.notif[2] <= 3U);
    printf("[run] 事件周期 100/10/500ms  : %s\r\n", ok ? "PASS" : "FAIL");
    fails += ok ? 0 : 1;

    /* 3) 退订（TTL=0）：订阅表应清空 */
    uint8_t *e = &frame[SOMEIP_HEADER_LEN + 8];
    someip_put_u24(&e[9], 0U);
    app_someip_on_rx(&g_srv, src, APP_SOMEIP_SD_PORT, frame, n);
    ok = (app_someip_sub_count(&g_srv, APP_EVENTGROUP_ID) == 0);
    printf("[run] unsubscribe(TTL=0)     : %s\r\n", ok ? "PASS" : "FAIL");
    fails += ok ? 0 : 1;

    g_srv.cfg.send = saved_send;
    g_srv.cfg.net_ctx = saved_ctx;
    printf("[run] %s (%d failed)\r\n", (fails == 0) ? "PASS" : "FAIL", fails);
    return fails;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 run, cmd_run, subscribe + event path selftest);
/* ---------------- shell ---------------- */
static void cmd_stat(int argc, char *argv[])
{
    (void)argc; (void)argv;
    const someip_stats_t *st = &g_srv.stats;
    printf("[someip] link=%s uptime=%lu ms offer_state=%u\r\n",
           app_someip_net_link_up() ? "UP" : "DOWN", (unsigned long)bsp_uptime_ms(), (unsigned)g_srv.offer_state);
    printf("[someip] rx total=%lu req=%lu sd=%lu malformed=%lu | tx rsp=%lu offer=%lu ack=%lu notif=%lu\r\n",
           (unsigned long)st->rx_total, (unsigned long)st->rx_requests, (unsigned long)st->rx_sd,
           (unsigned long)st->err_malformed, (unsigned long)st->tx_responses,
           (unsigned long)st->tx_sd_offer, (unsigned long)st->tx_sd_ack, (unsigned long)st->tx_notifications);
    printf("[someip] subs added=%lu removed=%lu expired=%lu active=%d | fill counter=%lu adc=%lu temp=%lu\r\n",
           (unsigned long)st->sub_added, (unsigned long)st->sub_removed, (unsigned long)st->sub_expired,
           app_someip_sub_count(&g_srv, APP_EVENTGROUP_ID),
           (unsigned long)g_ev_sent[0], (unsigned long)g_ev_sent[1], (unsigned long)g_ev_sent[2]);
    printf("[someip] counter=%lu adc=%u temp=%ld.%03ld C\r\n",
           (unsigned long)g_counter, (unsigned)g_adc_raw,
           (long)(g_temp_mc / 1000), (long)(g_temp_mc % 1000));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, show SOME/IP counters);

static void cmd_evstat(int argc, char *argv[])
{
    (void)argc; (void)argv;
    printf("[ev] 事件生成次数（fill 调用）：counter(0x8001,100ms)=%lu adc(0x8002,10ms)=%lu temp(0x8004,500ms)=%lu\r\n",
           (unsigned long)g_ev_sent[0], (unsigned long)g_ev_sent[1], (unsigned long)g_ev_sent[2]);
    printf("[ev] 实际发出 NOTIFICATION=%lu 条，活跃订阅者=%d\r\n",
           (unsigned long)g_srv.stats.tx_notifications, app_someip_sub_count(&g_srv, APP_EVENTGROUP_ID));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 evstat, cmd_evstat, show event counters);

static void cmd_selftest(int argc, char *argv[])
{
    (void)argc; (void)argv;
    (void)app_someip_selftest(&g_srv, true);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 selftest, cmd_selftest, protocol level selftest);

static void cmd_offer(int argc, char *argv[])
{
    (void)argc; (void)argv;
    int r = app_someip_offer(&g_srv);
    printf("[someip] offer %s (224.244.224.245:30490 ttl=%lu s)\r\n",
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
                 stopoffer, cmd_stopoffer, send StopOfferService);

static void cmd_subs(int argc, char *argv[])
{
    (void)argc; (void)argv;
    int n = 0;
    for (int i = 0; i < SOMEIP_MAX_SUBSCRIBERS; i++) {
        const someip_subscriber_t *sub = &g_srv.subs[i];
        if (sub->active) {
            n++;
            printf("[someip] sub%d eg=0x%04X endpoint=%u.%u.%u.%u:%u ttl=%lu s\r\n", n,
                   (unsigned)sub->eventgroup, (unsigned)sub->ip[0], (unsigned)sub->ip[1],
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

/* ---------------- ADC16 ---------------- */
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

int main(void)
{
    someip_cfg_t cfg;

    bsp_init();
    printf("\r\n=== someip_event: SOME/IP events over UDP (Subscribe + NOTIFICATION) ===\r\n");
    printf("[someip] service 0x%04X instance 0x%04X eventgroup 0x%04X, %s\r\n",
           (unsigned)APP_SERVICE_ID, (unsigned)APP_INSTANCE_ID, (unsigned)APP_EVENTGROUP_ID,
           app_someip_ver_string());

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
    app_someip_register_method(&g_srv, M_GET_TIME, m_get_time, NULL);
    app_someip_register_event(&g_srv, E_COUNTER, APP_EVENTGROUP_ID, 100U, fill_counter, NULL);
    app_someip_register_event(&g_srv, E_ADC, APP_EVENTGROUP_ID, 10U, fill_adc, NULL);
    app_someip_register_event(&g_srv, E_TEMP, APP_EVENTGROUP_ID, 500U, fill_temp, NULL);

    g_adc_ok = adc_init();
    printf("[adc] ADC16 ADC0 ch%u %s\r\n", (unsigned)APP_ADC_CH,
           g_adc_ok ? "ready (PF26 悬空=噪声底)" : "NOT available (event 0x8002 发 0)");

    if (app_someip_net_open(&g_srv, APP_SOMEIP_SD_PORT, APP_SOMEIP_SERVICE_PORT) != 0) {
        printf("[someip] UDP socket open FAILED\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }

    uint32_t t0 = bsp_uptime_ms();
    while (!app_someip_net_link_up() && ((bsp_uptime_ms() - t0) < 3000U)) {
        app_someip_net_poll();
    }
    (void)app_someip_offer(&g_srv);
    printf("[someip] OfferService -> 224.244.224.245:30490, endpoint 192.168.2.100:30501, ttl=%u s\r\n",
           (unsigned)g_srv.cfg.offer_ttl_s);
    printf("[someip] 无订阅者时事件不计流量；收到 SubscribeEventgroup 后按 100/10/500 ms 发 NOTIFICATION\r\n");
    printf("[someip] shell: stat | evstat | run | selftest | offer | stopoffer | sub | ip\r\n");

    while (1) {
        app_someip_net_poll();
        app_someip_poll(&g_srv, app_someip_net_now_ms(NULL));
        bsp_shell_poll();
    }
    return 0;
}
