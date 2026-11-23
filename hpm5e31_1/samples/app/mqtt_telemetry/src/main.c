/*
 * mqtt_telemetry —— MQTT 3.1.1 车载遥测/下行命令最小闭环（板端 192.168.2.100 -> 主机 broker 192.168.2.1:1883）
 *
 * 硬件：hpm5e31_1（HPM5E31IPB1）
 *   ENET0 RGMII + YT8011AN(1000BASE-T1) <-> 千兆车载以太网盒 <-> 主机 enp6s0(192.168.2.1)
 *   CAN0(PD00/PD01) <-> CAN1(PD05/PD04) 板内两两相连（遥测里的 CAN 统计来自这条真实总线）
 *   ADC0.IN1 = PF26（J3 排针，悬空=噪声底）
 *   片内 TSNS 温度传感器
 *   USR LED = PA02（下行命令控制）
 *
 * MQTT 主题（与 host/mqtt/ 上位机、第 38 章一致）：
 *   发布 hpm5e31/telemetry      遥测 JSON，周期 1000 ms，QoS1
 *   发布 hpm5e31/status         在线状态 JSON，retained；遗嘱 = {"online":false,"reason":"lwt"}
 *   发布 hpm5e31/ack            命令回执 JSON，QoS1
 *   发布 hpm5e31/echo/reply     回声（压力测试用），QoS0
 *   发布 hpm5e31/bench          吞吐测试报文，QoS0/1 可选
 *   订阅 hpm5e31/cmd/#          下行命令：led / period / cansend / echo / reboot / kill
 *
 * shell：mqtt | mqtt conn|close|kill|pub|sub|unsub|stat|reset|hex|match|cfg|bench
 *         mtele（立即发一条遥测）| mstat（状态汇总 + PASS/FAIL）
 *
 * 结论行：每 5 s 一行 [mqtt] STAT ... => PASS/FAIL（hwrun 日志抓取用）
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bsp.h"
#include "board.h"
#include "common.h"

#include "app_lwip.h"
#include "app_can.h"
#include "app_mqtt.h"

#include "hpm_adc16_drv.h"
#include "hpm_tsns_drv.h"
#include "hpm_csr_drv.h"
#include "hpm_ppor_drv.h"

/* ------------------------------------------------------------------ */
/* 配置                                                               */
/* ------------------------------------------------------------------ */
#define APP_BROKER_IP        "192.168.2.1"
#define APP_BROKER_PORT      (1883U)
#define APP_CLIENT_ID        "hpm5e31"
#define APP_KEEPALIVE_S      (1080U)         /* 18 min：车规远传的常用档位，实测见第 38 章 */

#define TOPIC_TELEMETRY      "hpm5e31/telemetry"
#define TOPIC_STATUS         "hpm5e31/status"
#define TOPIC_ACK            "hpm5e31/ack"
#define TOPIC_ECHO_REPLY     "hpm5e31/echo/reply"
#define TOPIC_BENCH          "hpm5e31/bench"
#define TOPIC_BENCH_DONE     "hpm5e31/bench/done"
#define TOPIC_CMD_FILTER     "hpm5e31/cmd/#"

#define WILL_PAYLOAD         "{\"dev\":\"hpm5e31\",\"online\":false,\"reason\":\"lwt\"}"

#define TELEMETRY_PERIOD_MS  1000U
#define STAT_PERIOD_MS       5000U
#define ADC_CH               (1U)            /* PF26 */
#define ADC_SAMPLE_CYCLE     (20U)

/* ------------------------------------------------------------------ */
/* 状态                                                               */
/* ------------------------------------------------------------------ */
static app_mqtt_t g_mq;
static app_can_t  g_can0;                    /* 遥测帧发送 */
static app_can_t  g_can1;                    /* 板内对端接收，统计 rx */
static bool       g_can_ok;
static bool       g_adc_ok;
static bool       g_tsns_ok;

static uint32_t g_period_ms = TELEMETRY_PERIOD_MS;
static uint8_t  g_tele_qos = 1U;
static uint32_t g_seq;
static uint32_t g_up_base_ms;
static uint32_t g_next_tele_ms;
static uint32_t g_next_stat_ms;
static uint32_t g_stat_t0_ms;

static uint32_t g_can_tx;
static uint32_t g_can_rx;
static uint16_t g_adc_raw;
static int32_t  g_temp_mc = 25000;

static uint32_t g_led_state;
static uint32_t g_cmd_count;
static uint32_t g_ack_count;
static uint32_t g_rx_msgs;
static uint32_t g_bad_msgs;

/* 主循环耗时水位（us） */
static uint32_t g_loop_cnt;
static uint32_t g_loop_max_us;
static uint64_t g_loop_sum_us;
static uint32_t g_cpu_mhz = 480U;

/* 状态变化 / 待办（回调里只置位，动作在主循环做） */
static volatile uint32_t g_state_events;
static volatile int      g_last_state = APP_MQTT_STATE_IDLE;
static volatile int      g_last_reason;
static volatile bool     g_need_subscribe;
static volatile bool     g_need_online_status;
static volatile bool     g_pending_reboot;
static volatile bool     g_pending_kill;
static volatile bool     g_pending_reconnect;
static volatile uint32_t g_pending_bench_n;
static volatile uint8_t  g_pending_bench_qos;
static volatile bool     g_pending_ack;
static char              g_ack_text[96];
static char              g_ack_cmd[16] = "cmd";

/* ------------------------------------------------------------------ */
/* 小工具                                                             */
/* ------------------------------------------------------------------ */
static uint32_t now_ms(void)
{
    return app_lwip_now_ms();
}

static uint64_t now_cyc(void)
{
    return hpm_csr_get_core_mcycle();
}

static uint32_t cyc_to_us(uint64_t cyc)
{
    return (uint32_t) (cyc / (uint64_t) g_cpu_mhz);
}

static int32_t temp_mc_read(void)
{
    if (!g_tsns_ok) {
        return g_temp_mc;
    }
    /* TSNS：1 LSB = 1/256 C，转成 0.001 C 便于 JSON 用整数表达 */
    return (int32_t) ((float) tsns_get_current_temp(HPM_TSNS) * 1000.0f);
}

static void adc_sample(void)
{
    uint16_t raw = 0U;
    if (!g_adc_ok) {
        return;
    }
    if (adc16_get_oneshot_result(HPM_ADC0, ADC_CH, &raw) == status_success) {
        g_adc_raw = raw;
    }
}

/* ------------------------------------------------------------------ */
/* MQTT 回调（只做最少的活：置标志 / 回声，避免在 lwIP 回调里 printf）  */
/* ------------------------------------------------------------------ */
static void on_mqtt_state(app_mqtt_t *m, app_mqtt_state_t st, int reason, void *user)
{
    (void) m;
    (void) user;
    g_last_state = (int) st;
    g_last_reason = reason;
    g_state_events++;
    if (st == APP_MQTT_STATE_CONNECTED) {
        g_need_subscribe = true;
        g_need_online_status = true;
    }
}

static void publish_ack(const char *cmd, int ok, const char *extra)
{
    char buf[96];
    int n;

    n = snprintf(buf, sizeof(buf),
                 "{\"dev\":\"hpm5e31\",\"cmd\":\"%s\",\"ok\":%d%s%s}",
                 cmd, ok, (extra != NULL) ? "," : "", (extra != NULL) ? extra : "");
    if ((n > 0) && ((size_t) n < sizeof(buf))) {
        (void) app_mqtt_publish(&g_mq, TOPIC_ACK, buf, (uint16_t) n, 1U, false);
        g_ack_count++;
    }
}

static void on_mqtt_message(app_mqtt_t *m, const app_mqtt_msg_t *msg, void *user)
{
    (void) user;
    g_rx_msgs++;
    g_cmd_count++;

    /* 回声：压力/时延测试，原样回发（QoS0，尽量减少板端开销） */
    if (app_mqtt_topic_match("hpm5e31/cmd/echo", msg->topic)) {
        (void) app_mqtt_publish(m, TOPIC_ECHO_REPLY, msg->payload, msg->payload_len, 0U, false);
        return;
    }
    if (app_mqtt_topic_match("hpm5e31/cmd/led", msg->topic)) {
        if ((msg->payload_len >= 2U) && (memcmp(msg->payload, "on", 2) == 0)) {
            g_led_state = 1U;
        } else if ((msg->payload_len >= 3U) && (memcmp(msg->payload, "off", 3) == 0)) {
            g_led_state = 0U;
        } else if ((msg->payload_len >= 6U) && (memcmp(msg->payload, "toggle", 6) == 0)) {
            g_led_state ^= 1U;
        }
        board_led_write((uint8_t) g_led_state);
        g_pending_ack = true;
        (void) snprintf(g_ack_cmd, sizeof(g_ack_cmd), "led");
        (void) snprintf(g_ack_text, sizeof(g_ack_text), "\"led\":%lu", (unsigned long) g_led_state);
        return;
    }
    if (app_mqtt_topic_match("hpm5e31/cmd/period", msg->topic)) {
        char tmp[16];
        uint32_t v;
        uint16_t n = (msg->payload_len < sizeof(tmp) - 1U) ? msg->payload_len
                                                           : (uint16_t) (sizeof(tmp) - 1U);
        memcpy(tmp, msg->payload, n);
        tmp[n] = '\0';
        v = (uint32_t) strtoul(tmp, NULL, 10);
        if ((v >= 100U) && (v <= 60000U)) {
            g_period_ms = v;
        }
        g_pending_ack = true;
        (void) snprintf(g_ack_cmd, sizeof(g_ack_cmd), "period");
        (void) snprintf(g_ack_text, sizeof(g_ack_text), "\"period\":%lu",
                        (unsigned long) g_period_ms);
        return;
    }
    if (app_mqtt_topic_match("hpm5e31/cmd/cansend", msg->topic)) {
        /* 载荷格式："0x123,11,22,33" 或 "112233"（奇数长度按 0x1 补） */
        app_can_frame_t f;
        char tmp[96];
        char *p;
        uint16_t n = (msg->payload_len < sizeof(tmp) - 1U) ? msg->payload_len
                                                           : (uint16_t) (sizeof(tmp) - 1U);
        int ok = 0;
        memcpy(tmp, msg->payload, n);
        tmp[n] = '\0';
        memset(&f, 0, sizeof(f));
        p = tmp;
        f.id = (uint32_t) strtoul(p, &p, 0);
        while ((*p == ',') || (*p == ' ')) {
            p++;
        }
        while ((*p != '\0') && (f.len < APP_CAN_MAX_DATA_LEN)) {
            char *end = NULL;
            uint32_t b = (uint32_t) strtoul(p, &end, 16);
            if (end == p) {
                break;
            }
            f.data[f.len++] = (uint8_t) (b & 0xFFU);
            p = end;
            while ((*p == ',') || (*p == ' ')) {
                p++;
            }
        }
        if (g_can_ok && (f.len > 0U)) {
            ok = (app_can_send(&g_can0, &f, 1000U) == 0);
            if (ok) {
                g_can_tx++;
            }
        }
        g_pending_ack = true;
        (void) snprintf(g_ack_cmd, sizeof(g_ack_cmd), "cansend");
        (void) snprintf(g_ack_text, sizeof(g_ack_text), "\"can_id\":%lu,\"can_len\":%u",
                        (unsigned long) f.id, (unsigned) f.len);
        return;
    }
    if (app_mqtt_topic_match("hpm5e31/cmd/reboot", msg->topic)) {
        g_pending_reboot = true;
        g_pending_ack = true;
        (void) snprintf(g_ack_cmd, sizeof(g_ack_cmd), "reboot");
        (void) snprintf(g_ack_text, sizeof(g_ack_text), "\"reboot\":1");
        return;
    }
    if (app_mqtt_topic_match("hpm5e31/cmd/kill", msg->topic)) {
        g_pending_kill = true;          /* 不打招呼断开，验证 LWT */
        return;
    }
    if (app_mqtt_topic_match("hpm5e31/cmd/keepalive", msg->topic)) {
        char tmp[16];
        uint32_t v;
        uint16_t n = (msg->payload_len < sizeof(tmp) - 1U) ? msg->payload_len
                                                           : (uint16_t) (sizeof(tmp) - 1U);
        memcpy(tmp, msg->payload, n);
        tmp[n] = '\0';
        v = (uint32_t) strtoul(tmp, NULL, 10);
        if (v <= 65535U) {
            g_mq.cfg.keepalive_s = (uint16_t) v;
        }
        g_pending_ack = true;
        (void) snprintf(g_ack_cmd, sizeof(g_ack_cmd), "keepalive");
        (void) snprintf(g_ack_text, sizeof(g_ack_text), "\"keepalive\":%u",
                        (unsigned) g_mq.cfg.keepalive_s);
        return;
    }
    if (app_mqtt_topic_match("hpm5e31/cmd/reconnect", msg->topic)) {
        g_pending_reconnect = true;
        g_pending_ack = true;
        (void) snprintf(g_ack_cmd, sizeof(g_ack_cmd), "reconnect");
        (void) snprintf(g_ack_text, sizeof(g_ack_text), "\"reconnect\":1");
        return;
    }
    if (app_mqtt_topic_match("hpm5e31/cmd/bench", msg->topic)) {
        /* 载荷 "n,qos"：让板端向 hpm5e31/bench 连发 n 条，测板->云吞吐 */
        char tmp[32];
        char *p;
        uint16_t n = (msg->payload_len < sizeof(tmp) - 1U) ? msg->payload_len
                                                           : (uint16_t) (sizeof(tmp) - 1U);
        memcpy(tmp, msg->payload, n);
        tmp[n] = '\0';
        p = tmp;
        g_pending_bench_n = (uint32_t) strtoul(p, &p, 10);
        if ((*p == ',') || (*p == ' ')) {
            p++;
        }
        g_pending_bench_qos = (uint8_t) ((strtoul(p, NULL, 10) != 0U) ? 1U : 0U);
        return;
    }
    g_bad_msgs++;
}

/* ------------------------------------------------------------------ */
/* 遥测 / 状态发布                                                    */
/* ------------------------------------------------------------------ */
static void publish_status_online(void)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf),
                     "{\"dev\":\"hpm5e31\",\"online\":true,\"up_ms\":%lu,\"fw\":\"%s\"}",
                     (unsigned long) (now_ms() - g_up_base_ms), APP_MQTT_VERSION_STR);
    if ((n > 0) && ((size_t) n < sizeof(buf))) {
        (void) app_mqtt_publish(&g_mq, TOPIC_STATUS, buf, (uint16_t) n, 1U, true);
    }
}

static void publish_telemetry(void)
{
    char buf[224];
    app_mqtt_stats_t st;
    int n;
    uint32_t loop_avg = (g_loop_cnt > 0U) ? (uint32_t) (g_loop_sum_us / g_loop_cnt) : 0U;

    app_mqtt_get_stats(&g_mq, &st);
    n = snprintf(buf, sizeof(buf),
                 "{\"dev\":\"hpm5e31\",\"seq\":%lu,\"up_ms\":%lu,\"can_tx\":%lu,\"can_rx\":%lu,"
                 "\"adc\":%u,\"temp_mc\":%ld,\"loop_us\":%lu,\"loop_max_us\":%lu,"
                 "\"mqtt_rx\":%lu,\"mqtt_tx\":%lu}",
                 (unsigned long) g_seq, (unsigned long) (now_ms() - g_up_base_ms),
                 (unsigned long) g_can_tx, (unsigned long) g_can_rx,
                 (unsigned) g_adc_raw, (long) g_temp_mc, (unsigned long) loop_avg,
                 (unsigned long) g_loop_max_us, (unsigned long) st.publish_rx,
                 (unsigned long) st.publish_tx);
    if ((n > 0) && ((size_t) n < sizeof(buf))) {
        (void) app_mqtt_publish(&g_mq, TOPIC_TELEMETRY, buf, (uint16_t) n, g_tele_qos, false);
    }
    g_seq++;
}

/* 每周期在 CAN0 发一帧遥测，同时在 CAN1 上收（板内 CAN0<->CAN1 已接线） */
static void can_periodic(void)
{
    app_can_frame_t f;

    if (!g_can_ok) {
        return;
    }
    memset(&f, 0, sizeof(f));
    f.id = 0x5A5U;
    f.len = 8U;
    f.data[0] = (uint8_t) (g_seq & 0xFFU);
    f.data[1] = (uint8_t) ((g_seq >> 8) & 0xFFU);
    f.data[2] = (uint8_t) (g_adc_raw & 0xFFU);
    f.data[3] = (uint8_t) (g_adc_raw >> 8);
    f.data[4] = 0xA5U;
    if (app_can_send(&g_can0, &f, 0U) == 0) {
        g_can_tx++;
    }
    /* 清空 CAN1 RX FIFO，统计真实收到的帧 */
    for (;;) {
        if (app_can_recv(&g_can1, &f, 0U) != 0) {
            break;
        }
        g_can_rx++;
    }
}

static void print_stat(void)
{
    app_mqtt_stats_t st;
    uint32_t up_s = (now_ms() - g_stat_t0_ms) / 1000U;
    bool pass;

    app_mqtt_get_stats(&g_mq, &st);
    printf("[mqtt] STAT st=%s up=%lus sub=%s conn=%lu ok=%lu pub_tx=%lu pub_rx=%lu "
           "retx=%lu ping=%lu/%lu rtt=%lums\r\n",
           app_mqtt_state_str(app_mqtt_state(&g_mq)), (unsigned long) up_s,
           app_mqtt_is_connected(&g_mq) ? "OK" : "NO", (unsigned long) st.tcp_connects,
           (unsigned long) st.connack_ok, (unsigned long) st.publish_tx,
           (unsigned long) st.publish_rx, (unsigned long) st.retransmits,
           (unsigned long) st.pingreq_tx, (unsigned long) st.pingresp_rx,
           (unsigned long) st.rtt_avg_ms);
    pass = app_mqtt_is_connected(&g_mq) && (st.connack_ok > 0U) && (st.publish_tx > 0U);
    printf("[mqtt] STAT seq=%lu can_tx=%lu can_rx=%lu adc=%u temp=%ldmC loop_max=%luus "
           "evt=%lu => %s\r\n",
           (unsigned long) g_seq, (unsigned long) g_can_tx, (unsigned long) g_can_rx,
           (unsigned) g_adc_raw, (long) g_temp_mc, (unsigned long) g_loop_max_us,
           (unsigned long) g_state_events, pass ? "PASS" : "FAIL");
}

/* ------------------------------------------------------------------ */
/* 初始化                                                             */
/* ------------------------------------------------------------------ */
static bool can_init(void)
{
    /* 注意：app_can.h 的 APP_CAN_CFG_DEFAULT 宏与结构体字段不同步（多了一个 false），
       直接用会触发 excess elements 警告且 tdc/timing 字段错位，这里用 memset + 显式赋值。 */
    app_can_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.speed = APP_CAN_SPEED_CLASSIC_500K;
    if (app_can_init(&g_can0, BOARD_CAN0_BASE, &cfg) != 0) {
        printf("[can] CAN0 init FAIL\r\n");
        return false;
    }
    if (app_can_init(&g_can1, BOARD_CAN1_BASE, &cfg) != 0) {
        printf("[can] CAN1 init FAIL\r\n");
        return false;
    }
    printf("[can] CAN0/CAN1 classic 500k ready (CAN0 tx -> CAN1 rx)\r\n");
    return true;
}

static bool adc_init(void)
{
    uint32_t clk_hz;
    uint32_t div = 0U;
    adc16_config_t cfg;
    adc16_channel_config_t ch_cfg;

    board_init_adc16_pins();
    clk_hz = board_init_adc_clock(HPM_ADC0, true);
    if (adc16_calc_clock_divider(clk_hz, ADC16_SOC_CONV_CLK_FREQ_MAX, &div) != status_success) {
        printf("[adc] clock divider FAIL\r\n");
        return false;
    }
    adc16_get_default_config(&cfg);
    cfg.res = adc16_res_16_bits;
    cfg.conv_mode = adc16_conv_mode_oneshot;
    cfg.adc_clk_div = div;
#if !defined(HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB) || !HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB
    cfg.sel_sync_ahb = (clk_adc_src_ahb0 == clock_get_source(BOARD_APP_ADC16_CLK_NAME)) ? true : false;
#endif
    if (adc16_init(HPM_ADC0, &cfg) != status_success) {
        printf("[adc] adc16_init FAIL\r\n");
        return false;
    }
    adc16_get_channel_default_config(&ch_cfg);
    ch_cfg.ch = ADC_CH;
    ch_cfg.sample_cycle = ADC_SAMPLE_CYCLE;
    if (adc16_init_channel(HPM_ADC0, &ch_cfg) != status_success) {
        printf("[adc] channel init FAIL\r\n");
        return false;
    }
#if defined(ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT) && ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT
    adc16_enable_oneshot_mode(HPM_ADC0);
#endif
    adc16_set_blocking_read(HPM_ADC0);
    printf("[adc] ADC0 IN%u oneshot, clk %lu Hz\r\n", (unsigned) ADC_CH,
           (unsigned long) (clk_hz / div));
    return true;
}

static bool tsns_init(void)
{
    clock_add_to_group(clock_tsns, 0);
    tsns_enable_continuous_mode(HPM_TSNS);
    tsns_enable(HPM_TSNS);
    printf("[tsns] on-chip temperature sensor enabled\r\n");
    return true;
}

static void mqtt_default_cfg(app_mqtt_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->host = APP_BROKER_IP;
    cfg->port = APP_BROKER_PORT;
    cfg->client_id = APP_CLIENT_ID;
    cfg->will_topic = TOPIC_STATUS;
    cfg->will_payload = WILL_PAYLOAD;
    cfg->will_qos = 1U;
    cfg->will_retain = true;
    cfg->keepalive_s = APP_KEEPALIVE_S;
    cfg->clean_session = true;
    cfg->auto_reconnect = true;
    cfg->connect_timeout_ms = 5000U;
    cfg->reconnect_min_ms = 1000U;
    cfg->reconnect_max_ms = 30000U;
    cfg->qos1_retry_ms = 5000U;
    cfg->qos1_max_retry = 3U;
}

static int mqtt_setup(void)
{
    app_mqtt_cfg_t cfg;
    int rc;

    mqtt_default_cfg(&cfg);
    rc = app_mqtt_init(&g_mq, &cfg);
    if (rc != 0) {
        printf("[mqtt] init FAIL rc=%d\r\n", rc);
        return rc;
    }
    app_mqtt_set_state_cb(&g_mq, on_mqtt_state, NULL);
    app_mqtt_set_message_cb(&g_mq, on_mqtt_message, NULL);
    rc = app_mqtt_connect(&g_mq);
    printf("[mqtt] init ok, connect rc=%d -> %s:%u cid=%s keepalive=%us will=%s\r\n",
           rc, APP_BROKER_IP, (unsigned) APP_BROKER_PORT, APP_CLIENT_ID,
           (unsigned) APP_KEEPALIVE_S, TOPIC_STATUS);
    return rc;
}

/* ------------------------------------------------------------------ */
/* shell 命令                                                         */
/* ------------------------------------------------------------------ */
/* 连发 n 条 bench 报文（shell 和 MQTT 下行命令共用），返回发出条数 */
static uint32_t run_bench(uint32_t n, uint8_t qos)
{
    char payload[48];
    char done[128];
    uint32_t i = 0U;
    uint32_t sent = 0U;
    uint32_t fail = 0U;
    uint32_t t0 = now_ms();
    uint32_t dt;
    uint32_t rate;
    int len;

    while (i < n) {
        len = snprintf(payload, sizeof(payload), "{\"seq\":%lu,\"pad\":\"0123456789\"}",
                       (unsigned long) i);
        if (app_mqtt_publish(&g_mq, TOPIC_BENCH, payload, (uint16_t) len, qos, false) >= 0) {
            sent++;
            i++;
        } else {
            fail++;
            app_lwip_poll();
            app_mqtt_poll(&g_mq);
            if (fail > (n * 20U + 1000U)) {
                break;
            }
        }
        if ((i & 0x0FU) == 0U) {
            app_lwip_poll();
            app_mqtt_poll(&g_mq);
        }
    }
    /* QoS1 时等 PUBACK 清空 inflight（最多 3 s） */
    for (i = 0U; i < 3000U; i++) {
        app_lwip_poll();
        app_mqtt_poll(&g_mq);
        if (g_mq.stats.inflight == 0U) {
            break;
        }
        board_delay_ms(1);
    }
    dt = now_ms() - t0;
    rate = (dt > 0U) ? (sent * 1000U / dt) : 0U;
    printf("[mqtt] BENCH n=%lu qos=%u sent=%lu fail=%lu elapsed=%lums rate=%lu msg/s => %s\r\n",
           (unsigned long) n, (unsigned) qos, (unsigned long) sent, (unsigned long) fail,
           (unsigned long) dt, (unsigned long) rate, (sent == n) ? "PASS" : "FAIL");
    len = snprintf(done, sizeof(done),
                   "{\"dev\":\"hpm5e31\",\"bench\":\"done\",\"n\":%lu,\"qos\":%u,"
                   "\"sent\":%lu,\"ms\":%lu,\"rate\":%lu}",
                   (unsigned long) n, (unsigned) qos, (unsigned long) sent,
                   (unsigned long) dt, (unsigned long) rate);
    if ((len > 0) && ((size_t) len < sizeof(done))) {
        (void) app_mqtt_publish(&g_mq, TOPIC_BENCH_DONE, done, (uint16_t) len, 0U, false);
    }
    return sent;
}

static void cmd_mqtt(int argc, char *argv[])
{
    if (argc < 2) {
        app_mqtt_stats_t st;
        app_mqtt_get_stats(&g_mq, &st);
        printf("mqtt: state=%s connected=%d broker=%s:%u cid=%s keepalive=%us period=%lums qos=%u\r\n",
               app_mqtt_state_str(app_mqtt_state(&g_mq)), app_mqtt_is_connected(&g_mq) ? 1 : 0,
               APP_BROKER_IP, (unsigned) APP_BROKER_PORT, APP_CLIENT_ID,
               (unsigned) APP_KEEPALIVE_S, (unsigned long) g_period_ms, (unsigned) g_tele_qos);
        printf("  conn=%lu ok=%lu refuse=%lu disc=%lu reconn=%lu rx_pkt=%lu tx_pkt=%lu\r\n",
               (unsigned long) st.tcp_connects, (unsigned long) st.connack_ok,
               (unsigned long) st.connack_refused, (unsigned long) st.disconnects,
               (unsigned long) st.reconnects, (unsigned long) st.rx_packets,
               (unsigned long) st.tx_packets);
        printf("  pub_tx=%lu pub_rx=%lu puback_tx=%lu puback_rx=%lu retx=%lu inflight=%u\r\n",
               (unsigned long) st.publish_tx, (unsigned long) st.publish_rx,
               (unsigned long) st.puback_tx, (unsigned long) st.puback_rx,
               (unsigned long) st.retransmits, (unsigned) st.inflight);
        printf("  ping_tx=%lu ping_rx=%lu ping_to=%lu rtt=%lums avg=%lums drop=%lu err=%lu\r\n",
               (unsigned long) st.pingreq_tx, (unsigned long) st.pingresp_rx,
               (unsigned long) st.ping_timeouts, (unsigned long) st.last_rtt_ms,
               (unsigned long) st.rtt_avg_ms, (unsigned long) st.drops,
               (unsigned long) st.protocol_errors);
        return;
    }
    if (strcmp(argv[1], "conn") == 0) {
        g_mq.cfg.auto_reconnect = true;
        printf("mqtt conn rc=%d\r\n", app_mqtt_connect(&g_mq));
        return;
    }
    if (strcmp(argv[1], "close") == 0) {
        app_mqtt_disconnect(&g_mq, true);       /* 先发 DISCONNECT，broker 不发遗嘱 */
        printf("mqtt close (graceful, no LWT)\r\n");
        return;
    }
    if (strcmp(argv[1], "kill") == 0) {
        app_mqtt_disconnect(&g_mq, false);      /* 直接 abort，broker 应发遗嘱 */
        printf("mqtt kill (abortive, LWT expected)\r\n");
        return;
    }
    if (strcmp(argv[1], "stat") == 0) {
        print_stat();
        return;
    }
    if (strcmp(argv[1], "reset") == 0) {
        app_mqtt_reset_stats(&g_mq);
        printf("mqtt stats reset\r\n");
        return;
    }
    if (strcmp(argv[1], "hex") == 0) {
        static uint8_t connect_buf[APP_MQTT_MAX_PACKET];
        const uint8_t *p = NULL;
        uint16_t n;
        if ((argc >= 3) && (strcmp(argv[2], "connect") == 0)) {
            n = app_mqtt_debug_connect(&g_mq, connect_buf, (uint16_t) sizeof(connect_buf));
            p = connect_buf;
            printf("mqtt CONNECT packet (%u B):\r\n", (unsigned) n);
        } else {
            n = app_mqtt_last_packet(&g_mq, &p);
            printf("mqtt last tx packet (%u B):\r\n", (unsigned) n);
        }
        for (uint16_t i = 0; i < n; i++) {
            printf("%02X%s", p[i], ((i % 16U) == 15U) ? "\r\n" : " ");
        }
        printf("\r\n");
        return;
    }
    if (strcmp(argv[1], "match") == 0) {
        static const char *tests[][2] = {
            {"hpm5e31/cmd/#", "hpm5e31/cmd/led"},
            {"hpm5e31/+/led", "hpm5e31/cmd/led"},
            {"hpm5e31/+/led", "hpm5e31/cmd/x/led"},
            {"hpm5e31/#", "hpm5e31"},
            {"#", "$SYS/broker/uptime"},
            {"a/+/c", "a/b/c"},
        };
        uint32_t i;
        for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
            printf("  match(%s, %s) = %d\r\n", tests[i][0], tests[i][1],
                   app_mqtt_topic_match(tests[i][0], tests[i][1]) ? 1 : 0);
        }
        return;
    }
    if (strcmp(argv[1], "cfg") == 0) {
        if (argc < 4) {
            printf("usage: mqtt cfg period <ms> | qos <0|1> | keepalive <s>\r\n");
            return;
        }
        if (strcmp(argv[2], "period") == 0) {
            uint32_t v = (uint32_t) strtoul(argv[3], NULL, 10);
            if ((v >= 100U) && (v <= 60000U)) {
                g_period_ms = v;
            }
            printf("period=%lums\r\n", (unsigned long) g_period_ms);
        } else if (strcmp(argv[2], "qos") == 0) {
            g_tele_qos = (uint8_t) ((strtoul(argv[3], NULL, 10) != 0U) ? 1U : 0U);
            printf("telemetry qos=%u\r\n", (unsigned) g_tele_qos);
        } else if (strcmp(argv[2], "keepalive") == 0) {
            uint32_t v = (uint32_t) strtoul(argv[3], NULL, 10);
            if (v <= 65535U) {
                g_mq.cfg.keepalive_s = (uint16_t) v;
                printf("keepalive=%us (下次 CONNECT 生效；先 mqtt close 再 mqtt conn)\r\n",
                       (unsigned) g_mq.cfg.keepalive_s);
            }
        } else {
            printf("unknown cfg key: %s\r\n", argv[2]);
        }
        return;
    }
    if (strcmp(argv[1], "pub") == 0) {
        uint8_t qos = (uint8_t) ((argc >= 5) ? strtoul(argv[4], NULL, 10) : 0U);
        int rc;
        if (argc < 4) {
            printf("usage: mqtt pub <topic> <payload> [qos]\r\n");
            return;
        }
        rc = app_mqtt_publish(&g_mq, argv[2], argv[3], (uint16_t) strlen(argv[3]), qos, false);
        printf("mqtt pub rc=%d (qos=%u)\r\n", rc, (unsigned) qos);
        return;
    }
    if (strcmp(argv[1], "sub") == 0) {
        uint8_t qos = (uint8_t) ((argc >= 4) ? strtoul(argv[3], NULL, 10) : 1U);
        printf("mqtt sub rc=%d\r\n", app_mqtt_subscribe(&g_mq, argv[2], qos));
        return;
    }
    if (strcmp(argv[1], "unsub") == 0) {
        printf("mqtt unsub rc=%d\r\n", app_mqtt_unsubscribe(&g_mq, argv[2]));
        return;
    }
    if (strcmp(argv[1], "bench") == 0) {
        uint32_t n = (uint32_t) ((argc >= 3) ? strtoul(argv[2], NULL, 10) : 100U);
        uint8_t qos = (uint8_t) ((argc >= 4) ? strtoul(argv[3], NULL, 10) : 0U);
        (void) run_bench(n, qos);
        return;
    }
    printf("usage: mqtt [conn|close|kill|stat|reset|hex|match|cfg|pub|sub|unsub|bench]\r\n");
}

static void cmd_mtele(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    adc_sample();
    g_temp_mc = temp_mc_read();
    can_periodic();
    publish_telemetry();
    printf("[mqtt] telemetry sent seq=%lu\r\n", (unsigned long) (g_seq - 1U));
}

static void cmd_mstat(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    print_stat();
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mqtt, cmd_mqtt, MQTT client: conn/close/kill/pub/sub/stat/bench/cfg);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mtele, cmd_mtele, publish one telemetry sample now);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mstat, cmd_mstat, MQTT stats + PASS/FAIL verdict);

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */
int main(void)
{
    uint32_t t_loop;
    uint64_t c_loop;
    int rc;

    board_init();
    bsp_init();

    g_cpu_mhz = (uint32_t) (clock_get_frequency(clock_cpu0) / 1000000UL);

    printf("\r\n=== mqtt_telemetry: MQTT 3.1.1 telemetry + command downlink ===\r\n");
    printf("[app] cpu=%lu MHz, board hpm5e31_1\r\n", (unsigned long) g_cpu_mhz);

    g_can_ok = can_init();
    g_adc_ok = adc_init();
    g_tsns_ok = tsns_init();

    rc = app_lwip_init(NULL);
    if (rc < 0) {
        printf("[app] lwIP init FAIL rc=%d\r\n", rc);
        while (1) {
            bsp_shell_poll();
        }
    }
    g_up_base_ms = app_lwip_now_ms();
    g_stat_t0_ms = g_up_base_ms;

    (void) mqtt_setup();
    board_led_write(0);

    g_next_tele_ms = app_lwip_now_ms();
    g_next_stat_ms = app_lwip_now_ms() + STAT_PERIOD_MS;
    printf("[app] mqtt_telemetry ready: broker=%s:%u topics telemetry/status/ack, cmd=%s\r\n",
           APP_BROKER_IP, (unsigned) APP_BROKER_PORT, TOPIC_CMD_FILTER);
    printf("[app] shell: mqtt | mtele | mstat\r\n");

    while (1) {
        c_loop = now_cyc();
        t_loop = app_lwip_now_ms();

        bsp_shell_poll();
        app_lwip_poll();
        app_mqtt_poll(&g_mq);

        /* 连接建立后：订阅命令 + 发布 retained 在线状态 */
        if (g_need_subscribe) {
            g_need_subscribe = false;
            rc = app_mqtt_subscribe(&g_mq, TOPIC_CMD_FILTER, 1U);
            printf("[mqtt] subscribe %s rc=%d\r\n", TOPIC_CMD_FILTER, rc);
        }
        if (g_need_online_status) {
            g_need_online_status = false;
            publish_status_online();
            printf("[mqtt] online status published (retained)\r\n");
        }

        /* 周期任务 */
        t_loop = app_lwip_now_ms();
        if ((int32_t) (t_loop - g_next_tele_ms) >= 0) {
            adc_sample();
            g_temp_mc = temp_mc_read();
            can_periodic();
            publish_telemetry();
            g_next_tele_ms += g_period_ms;
            if ((int32_t) (t_loop - g_next_tele_ms) >= 0) {
                g_next_tele_ms = t_loop + g_period_ms;   /* 落后太多重新对齐 */
            }
        }
        if (g_pending_ack) {
            g_pending_ack = false;
            publish_ack(g_ack_cmd, 1, g_ack_text);
        }
        if ((int32_t) (t_loop - g_next_stat_ms) >= 0) {
            g_next_stat_ms += STAT_PERIOD_MS;
            print_stat();
        }
        if (g_pending_kill) {
            g_pending_kill = false;
            app_mqtt_disconnect(&g_mq, false);  /* abortive：broker 应发遗嘱，1 s 后自动重连 */
            printf("[mqtt] killed by command (LWT expected), auto-reconnect scheduled\r\n");
        }
        if (g_pending_reconnect) {
            g_pending_reconnect = false;
            app_mqtt_disconnect(&g_mq, true);   /* 优雅断开（无遗嘱）后立刻重连 */
            g_mq.cfg.auto_reconnect = true;
            (void) app_mqtt_connect(&g_mq);
            printf("[mqtt] reconnect by command (graceful close, no LWT)\r\n");
        }
        if (g_pending_bench_n > 0U) {
            uint32_t bn = g_pending_bench_n;
            uint8_t bq = g_pending_bench_qos;
            g_pending_bench_n = 0U;
            (void) run_bench(bn, bq);
        }
        if (g_pending_reboot) {
            printf("[mqtt] reboot command received, resetting in 200 ms\r\n");
            board_delay_ms(200);
            ppor_sw_reset(HPM_PPOR, 1000U);
        }

        /* 主循环耗时水位 */
        {
            uint32_t us = cyc_to_us(now_cyc() - c_loop);
            g_loop_cnt++;
            g_loop_sum_us += us;
            if (us > g_loop_max_us) {
                g_loop_max_us = us;
            }
        }
    }
}
