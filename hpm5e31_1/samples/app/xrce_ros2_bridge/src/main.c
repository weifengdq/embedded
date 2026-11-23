/*
 * xrce_ros2_bridge —— 面向 ROS 2 的 XRCE-DDS 桥接固件（与 xrce_pubsub 同一个 app_xrce 组件）
 *
 * 与 xrce_pubsub 的区别（为什么单独做一个例程）：
 *   1) 只保留 ROS 2 桥接必需的三条实体：/adc 发布、/cmd 订阅、/status 状态字符串（1 Hz）；
 *   2) 状态字符串把板端计数器直接送到 ROS 2 侧（adc=<n> cmd=<n> ack=<n> up=<ms>），
 *      便于 ros2 topic echo 一眼核对"板端发了多少、ROS 2 收到多少"；
 *   3) shell 只有 brstat/brrst 两条命令，日志输出更紧凑，适合配合 host/xrce/test_ros2_bridge.sh
 *      做"一次占锁完成串口 + ROS 2 双侧验证"。
 *
 * 硬件与网络同 xrce_pubsub：ENET0 RGMII + YT8011AN，板 192.168.2.100/24 -> Agent 192.168.2.1:2018
 * ROS 2 话题：/hpm5e31/adc (std_msgs/UInt16MultiArray)、/hpm5e31/cmd (std_msgs/UInt8)、
 *             /hpm5e31/status (std_msgs/String)
 */
#include <string.h>
#include "bsp.h"
#include "common.h"
#include "netconf.h"
#include "netinfo.h"
#include "ethernetif.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "hpm_adc16_drv.h"
#include "app_xrce.h"

#define AGENT_IP        0xC0A80201U
#define AGENT_PORT      2018U
#define LOCAL_PORT      2020U      /* 与 xrce_pubsub 的 2019 区分：两个例程可同时被 Agent 识别 */
#define SESSION_KEY     0x42524944U  /* "BRID" */
#define DOMAIN_ID       0U

#define STREAM_HISTORY  8
#ifndef ADC_PERIOD_MS
#define ADC_PERIOD_MS   10U
#endif
#define STATUS_PERIOD_MS 1000U
#define STAT_PERIOD_MS  5000U

#define ADC_CH0 1U
#define ADC_CH1 5U
#define ADC_CH2 6U
#define ADC_SAMPLE_CYCLE 20U

static uint8_t s_out_rel[APP_XRCE_MTU * STREAM_HISTORY];
static uint8_t s_in_rel[APP_XRCE_MTU * STREAM_HISTORY];
static uint8_t s_out_be[APP_XRCE_MTU * 2];

static int  s_pub_adc = -1, s_pub_status = -1, s_sub_cmd = -1;
static bool s_adc_ok;
static uint32_t s_adc_seq, s_cmd_cnt, s_pub_err, s_status_seq;
static uint8_t  s_led;
static uint32_t s_next_adc_ms, s_next_status_ms, s_next_stat_ms, s_rate_t0_ms;

static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU); p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU); p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

/* std_msgs/String 的 rosidl 序列化（不含 4 字节封装头） */
static uint32_t cdr_string(uint8_t *buf, const char *s)
{
    uint32_t n = (uint32_t)strlen(s) + 1U;
    put_u32le(buf, n);
    memcpy(&buf[4], s, n - 1U);
    buf[4 + n - 1U] = 0U;
    uint32_t total = 4U + n;
    uint32_t pad = (4U - (total & 3U)) & 3U;
    for (uint32_t i = 0; i < pad; i++) buf[total + i] = 0U;
    return total + pad;
}

/* std_msgs/UInt16MultiArray（layout.dim 为空，data 长度 n） */
static uint32_t cdr_u16_multiarray(uint8_t *buf, const uint16_t *v, uint32_t n)
{
    uint32_t o = 0;
    put_u32le(&buf[o], 0U); o += 4U;   /* layout.dim.size() = 0 */
    put_u32le(&buf[o], 0U); o += 4U;   /* layout.data_offset = 0 */
    put_u32le(&buf[o], n);  o += 4U;   /* data.size() */
    for (uint32_t i = 0; i < n; i++) {
        buf[o++] = (uint8_t)(v[i] & 0xFFU);
        buf[o++] = (uint8_t)((v[i] >> 8) & 0xFFU);
    }
    uint32_t pad = (4U - (o & 3U)) & 3U;
    for (uint32_t i = 0; i < pad; i++) buf[o++] = 0U;
    return o;
}

/* /hpm5e31/cmd = std_msgs/UInt8 */
static void on_cmd(void *arg, const uint8_t *cdr, uint32_t len)
{
    (void)arg;
    if (len < 1U) return;
    uint8_t v = cdr[0];
    s_cmd_cnt++;
    if (v == 0U) s_led = 0U;
    else if (v == 1U) s_led = 1U;
    else if (v == 2U) s_led = (uint8_t)!s_led;
    else return;
    board_led_write(s_led);
    printf("[bridge] cmd=%u -> LED %s (n=%lu)\\r\\n", (unsigned)v, s_led ? "ON" : "OFF",
           (unsigned long)s_cmd_cnt);

    /* 立即回 ack（status 话题，前缀 cmd=），上位机据此测命令往返 RTT */
    if (s_pub_status >= 0) {
        char ack[40];
        snprintf(ack, sizeof(ack), "cmd=%u ack=%lu", (unsigned)v, (unsigned long)s_cmd_cnt);
        uint8_t abuf[64];
        uint32_t an = cdr_string(abuf, ack);
        (void)app_xrce_publish(s_pub_status, abuf, an);
    }
}

static void net_poll(void) { enet_common_handler(&gnetif); }

static bool adc_init(void)
{
    board_init_adc16_pins();
    board_init_adc16_diff_pins();
    uint32_t clk = board_init_adc_clock(HPM_ADC0, true);
    uint32_t div = 0;
    if (adc16_calc_clock_divider(clk, ADC16_SOC_CONV_CLK_FREQ_MAX, &div) != status_success) return false;
    adc16_config_t cfg;
    adc16_get_default_config(&cfg);
    cfg.res = adc16_res_16_bits;
    cfg.conv_mode = adc16_conv_mode_oneshot;
    cfg.adc_clk_div = div;
#if !defined(HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB) || !HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB
    cfg.sel_sync_ahb = (clk_adc_src_ahb0 == clock_get_source(BOARD_APP_ADC16_CLK_NAME)) ? true : false;
#endif
    if (adc16_init(HPM_ADC0, &cfg) != status_success) return false;
    const uint8_t chs[3] = { ADC_CH0, ADC_CH1, ADC_CH2 };
    for (uint32_t i = 0; i < 3U; i++) {
        adc16_channel_config_t ch;
        adc16_get_channel_default_config(&ch);
        ch.ch = chs[i];
        ch.sample_cycle = ADC_SAMPLE_CYCLE;
        if (adc16_init_channel(HPM_ADC0, &ch) != status_success) return false;
    }
#if defined(ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT) && ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT
    adc16_enable_oneshot_mode(HPM_ADC0);
#endif
    adc16_set_blocking_read(HPM_ADC0);
    return true;
}

static void print_bridge(void)
{
    app_xrce_stats_t st;
    app_xrce_get_stats(&st);
    uint32_t win = (st.uptime_ms > s_rate_t0_ms) ? (st.uptime_ms - s_rate_t0_ms) : 1U;
    printf("[bridge] up=%lu ms state=%s adc=%lu (%.1f Hz) cmd=%lu ack_tx=%lu status=%lu\\r\\n",
           (unsigned long)st.uptime_ms, app_xrce_state_str(), (unsigned long)s_adc_seq,
           (double)s_adc_seq * 1000.0 / (double)win, (unsigned long)s_cmd_cnt,
           (unsigned long)st.acks, (unsigned long)s_status_seq);
    printf("[bridge] tx dg=%lu bytes=%lu drop=%lu err=%lu | rx dg=%lu samples=%lu retrans=%lu\\r\\n",
           (unsigned long)st.tx_datagrams, (unsigned long)st.tx_bytes, (unsigned long)st.tx_drops,
           (unsigned long)st.net_errors, (unsigned long)st.rx_datagrams,
           (unsigned long)st.rx_samples, (unsigned long)st.retransmits);
    printf("[bridge] STAT adc_hz=%.1f cmd=%lu => %s\\r\\n",
           (double)s_adc_seq * 1000.0 / (double)win, (unsigned long)s_cmd_cnt,
           (st.connected && (s_adc_seq > 0U)) ? "PASS" : "FAIL");
}

static void cmd_brstat(int argc, char *argv[]) { (void)argc; (void)argv; print_bridge(); }
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 brstat, cmd_brstat, print XRCE bridge counters + PASS/FAIL);

static void cmd_brrst(int argc, char *argv[])
{
    (void)argc; (void)argv;
    s_adc_seq = s_cmd_cnt = s_status_seq = 0;
    printf("[bridge] counters reset\\r\\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 brrst, cmd_brrst, reset bridge counters);

int main(void)
{
    bsp_init();
    printf("\\r\\n=== xrce_ros2_bridge: XRCE-DDS <-> ROS 2 (adc/cmd/status) ===\\r\\n");

    if (enet_init(ENET) != status_success) {
        printf("Enet initialization fails !!!\\r\\n");
        while (1) bsp_shell_poll();
    }
    lwip_init();
    netif_config(&gnetif);
    enet_services(&gnetif);
    board_timer_create(LWIP_APP_TIMER_INTERVAL, sys_timer_callback);

    board_init_led_pins();
    gpio_set_pin_output(BOARD_LED_GPIO_CTRL, BOARD_LED_GPIO_INDEX, BOARD_LED_GPIO_PIN);
    board_led_write(0);
    s_adc_ok = adc_init();

    app_xrce_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.agent_ip = AGENT_IP;
    cfg.agent_port = AGENT_PORT;
    cfg.local_port = LOCAL_PORT;
    cfg.key = SESSION_KEY;
    cfg.domain_id = DOMAIN_ID;
    cfg.participant_name = "hpm5e31_bridge";
    cfg.out_reliable_buf = s_out_rel;
    cfg.out_reliable_size = sizeof(s_out_rel);
    cfg.out_reliable_history = STREAM_HISTORY;
    cfg.in_reliable_buf = s_in_rel;
    cfg.in_reliable_size = sizeof(s_in_rel);
    cfg.in_reliable_history = STREAM_HISTORY;
    cfg.out_be_buf = s_out_be;
    cfg.poll_timeout_ms = 1U;   /* R5 U-36-5: 会话等待可配；0 = 非阻塞（实验特性） */
    cfg.out_be_size = sizeof(s_out_be);

    app_xrce_set_net_poll(net_poll);
    int rc = app_xrce_init(&cfg);
    printf("[bridge] xrce init rc=%d state=%s\\r\\n", rc, app_xrce_state_str());

    app_xrce_pub_cfg_t pub_adc = {
        .topic_name = "rt/hpm5e31/adc",
        .type_name = "std_msgs::msg::dds_::UInt16MultiArray_",
        .writer_oid = 0x01, .reliable_qos = true, .reliable_stream = false, .history_depth = 8,
    };
    app_xrce_pub_cfg_t pub_status = {
        .topic_name = "rt/hpm5e31/status",
        .type_name = "std_msgs::msg::dds_::String_",
        .writer_oid = 0x02, .reliable_qos = true, .reliable_stream = true, .history_depth = 8,
    };
    app_xrce_sub_cfg_t sub_cmd = {
        .topic_name = "rt/hpm5e31/cmd",
        .type_name = "std_msgs::msg::dds_::UInt8_",
        .reader_oid = 0x03, .reliable_qos = false, .history_depth = 8,
        .on_data = on_cmd, .arg = NULL,
    };
    s_pub_adc = app_xrce_add_pub(&pub_adc);
    s_pub_status = app_xrce_add_pub(&pub_status);
    s_sub_cmd = app_xrce_add_sub(&sub_cmd);
    printf("[bridge] entities adc=%d status=%d cmd=%d ; shell: brstat | brrst\\r\\n",
           s_pub_adc, s_pub_status, s_sub_cmd);

    uint32_t now = app_xrce_now_ms();
    s_rate_t0_ms = now;
    s_next_adc_ms = now + ADC_PERIOD_MS;
    s_next_status_ms = now + STATUS_PERIOD_MS;
    s_next_stat_ms = now + STAT_PERIOD_MS;
    print_bridge();

    while (1) {
        bsp_shell_poll();
        app_xrce_poll();
        now = app_xrce_now_ms();

        if ((int32_t)(now - s_next_adc_ms) >= 0) {
            s_next_adc_ms += ADC_PERIOD_MS;
            if ((int32_t)(now - s_next_adc_ms) > 200) s_next_adc_ms = now + ADC_PERIOD_MS;
            uint16_t v[4] = { 0, 0, 0, 0 };
            if (s_adc_ok) {
                const uint8_t chs[3] = { ADC_CH0, ADC_CH1, ADC_CH2 };
                for (uint32_t i = 0; i < 3U; i++) {
                    uint16_t raw = 0;
                    if (adc16_get_oneshot_result(HPM_ADC0, chs[i], &raw) != status_success) raw = 0;
                    v[i] = raw;
                }
            }
            s_adc_seq++;
            v[3] = (uint16_t)(s_adc_seq & 0xFFFFU);
            uint8_t cdr[32];
            uint32_t n = cdr_u16_multiarray(cdr, v, 4);
            if (app_xrce_publish(s_pub_adc, cdr, n) != 0) s_pub_err++;
        }

        if ((int32_t)(now - s_next_status_ms) >= 0) {
            s_next_status_ms += STATUS_PERIOD_MS;
            if ((int32_t)(now - s_next_status_ms) > 500) s_next_status_ms = now + STATUS_PERIOD_MS;
            s_status_seq++;
            char text[64];
            snprintf(text, sizeof(text), "adc=%lu cmd=%lu ack=%lu up=%lu",
                     (unsigned long)s_adc_seq, (unsigned long)s_cmd_cnt,
                     (unsigned long)s_status_seq, (unsigned long)now);
            uint8_t cdr[80];
            uint32_t n = cdr_string(cdr, text);
            if (app_xrce_publish(s_pub_status, cdr, n) != 0) s_pub_err++;
        }

        if ((int32_t)(now - s_next_stat_ms) >= 0) {
            s_next_stat_ms += STAT_PERIOD_MS;
            print_bridge();
        }
    }
    return 0;
}
