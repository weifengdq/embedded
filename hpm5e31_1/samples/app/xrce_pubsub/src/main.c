/*
 * xrce_pubsub —— Micro-XRCE-DDS 客户端最小闭环（板端 192.168.2.100 -> Agent 192.168.2.1:2018）
 *
 * 硬件：hpm5e31_1（HPM5E31IPB1）
 *   ENET0 RGMII + YT8011AN(1000BASE-T1) <-> 千兆车载以太网盒 <-> 主机 enp6s0
 *   ADC0.IN1 = PF26 / ADC0.IN5 = PF28 / ADC0.IN6 = PF30（J3 排针；悬空时为噪声）
 *   USR LED = PA02（订阅 /hpm5e31/cmd 控制）
 *
 * XRCE 实体（二进制创建，type_name 即 DDS 类型名；ROS 2 侧 topic 名带 "rt/" 前缀）：
 *   publish  rt/hpm5e31/hello  std_msgs::msg::dds_::String_           10 Hz  可靠流
 *   publish  rt/hpm5e31/adc    std_msgs::msg::dds_::UInt16MultiArray_ 100 Hz best-effort 流
 *   subscribe rt/hpm5e31/cmd   std_msgs::msg::dds_::UInt8_            cmd: 0=灭 1=亮 2=翻转
 *
 * shell：xrce | stat | pub [text] | rst
 * 结论行：每 5 s 打印一行 [xrce] STAT ... => PASS/FAIL（hwrun 日志抓取用）
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
#include "hpm_csr_drv.h"
#include "app_xrce.h"

/* ------------------------------------------------------------------ */
/* 配置                                                               */
/* ------------------------------------------------------------------ */
#define XRCE_AGENT_IP        0xC0A80201U   /* 192.168.2.1 */
#define XRCE_AGENT_PORT      2018U
#define XRCE_LOCAL_PORT      2019U
#define XRCE_SESSION_KEY     0x484D5035U   /* "HMP5" */
#define XRCE_DDS_DOMAIN_ID   0U

#define STREAM_HISTORY       8
#define HELLO_PERIOD_MS      100U          /* 10 Hz */
#ifndef ADC_PERIOD_MS
#define ADC_PERIOD_MS        10U           /* 100 Hz（-DADC_PERIOD_MS=2U -> 500 Hz） */
#endif
#define STAT_PERIOD_MS       5000U

#define ADC_CH0              1U            /* PF26 */
#define ADC_CH1              5U            /* PF28 */
#define ADC_CH2              6U            /* PF30 */
#define ADC_CH_NUM           3U
#define ADC_SAMPLE_CYCLE     20U

static uint8_t s_out_rel_buf[APP_XRCE_MTU * STREAM_HISTORY];
static uint8_t s_in_rel_buf[APP_XRCE_MTU * STREAM_HISTORY];
static uint8_t s_out_be_buf[APP_XRCE_MTU * 2];

static int s_pub_hello = -1;
static int s_pub_adc = -1;
static int s_sub_cmd = -1;

static bool     s_adc_ok;
static uint32_t s_adc_clk_hz;
static uint32_t s_hello_seq;
static uint32_t s_adc_seq;
static uint32_t s_cmd_count;
static uint8_t  s_led_state;
static uint8_t  s_last_cmd;
static uint32_t s_pub_err;
static uint32_t s_last_stat_ms;
static uint32_t s_last_hello_ms;
static uint32_t s_last_adc_ms;
/* 绝对截止时刻（避免"now - last >= period"每次丢一个主循环周期的误差） */
static uint32_t s_next_adc_ms;
static uint32_t s_next_hello_ms;
/* --- 诊断计时（perf 用；实测后可保留，开销 ~几 us） --- */
static uint32_t s_loop_cnt;
static uint32_t s_rate_t0_ms;   /* 发布循环起点：速率统计扣除建会话/建实体耗时 */
static uint64_t s_adc_cyc;
static uint64_t s_poll_cyc;

/* ------------------------------------------------------------------ */
/* CDR 编码（rosidl 序列化格式，不含 4 字节封装头）                     */
/* ------------------------------------------------------------------ */
static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

/* std_msgs/msg/String: uint32 长度(含结尾 NUL) + 字节 + NUL + 4 字节对齐填充 */
static uint32_t cdr_encode_string(uint8_t *buf, const char *s)
{
    uint32_t n = (uint32_t)strlen(s) + 1U;
    put_u32le(buf, n);
    memcpy(&buf[4], s, n - 1U);
    buf[4 + n - 1U] = 0U;
    uint32_t total = 4U + n;
    uint32_t pad = (4U - (total & 3U)) & 3U;
    for (uint32_t i = 0; i < pad; i++) {
        buf[total + i] = 0U;
    }
    return total + pad;
}

/* std_msgs/msg/UInt16MultiArray:
 *   MultiArrayLayout: dim(sequence, 这里长度 0) + data_offset(uint32)
 *   data: sequence<uint16>
 * 布局固定 20 字节（4 个元素），所有字段自然对齐。 */
static uint32_t cdr_encode_u16_multiarray(uint8_t *buf, const uint16_t *v, uint32_t n)
{
    uint32_t o = 0;
    put_u32le(&buf[o], 0U); o += 4U;          /* layout.dim 长度 = 0 */
    put_u32le(&buf[o], 0U); o += 4U;          /* layout.data_offset = 0 */
    put_u32le(&buf[o], n);  o += 4U;          /* data 序列长度 */
    for (uint32_t i = 0; i < n; i++) {
        buf[o++] = (uint8_t)(v[i] & 0xFFU);
        buf[o++] = (uint8_t)((v[i] >> 8) & 0xFFU);
    }
    uint32_t pad = (4U - (o & 3U)) & 3U;
    for (uint32_t i = 0; i < pad; i++) {
        buf[o++] = 0U;
    }
    return o;
}

/* ------------------------------------------------------------------ */
/* 订阅回调：/hpm5e31/cmd = std_msgs/UInt8                              */
/* ------------------------------------------------------------------ */
static void on_cmd(void *arg, const uint8_t *cdr, uint32_t len)
{
    (void)arg;
    if (len < 1U) {
        return;
    }
    uint8_t v = cdr[0];
    s_last_cmd = v;
    s_cmd_count++;
    if (v == 0U) {
        s_led_state = 0U;
    } else if (v == 1U) {
        s_led_state = 1U;
    } else if (v == 2U) {
        s_led_state = (uint8_t)!s_led_state;
    } else {
        return;   /* 其它值忽略 */
    }
    board_led_write(s_led_state);
    printf("[cmd] rx UInt8=%u -> LED %s (total %lu)\r\n",
           (unsigned)v, s_led_state ? "ON" : "OFF", (unsigned long)s_cmd_count);

    /* 立即回一条 String 作为 ack（上位机据此测命令往返 RTT） */
    if (s_pub_hello >= 0) {
        char ack[40];
        snprintf(ack, sizeof(ack), "cmd=%u ack=%lu", (unsigned)v, (unsigned long)s_cmd_count);
        uint8_t abuf[64];
        uint32_t an = cdr_encode_string(abuf, ack);
        (void)app_xrce_publish(s_pub_hello, abuf, an);
    }
}

/* ------------------------------------------------------------------ */
/* 网络轮询钩子：XRCE 传输层在等包时反复调用                            */
/* ------------------------------------------------------------------ */
static void net_poll(void)
{
    enet_common_handler(&gnetif);
}

/* ------------------------------------------------------------------ */
/* ADC 初始化（失败不致命：仍旧跑 XRCE，只是 ADC 通道为 0）             */
/* ------------------------------------------------------------------ */
static bool adc_init(void)
{
    board_init_adc16_pins();        /* PF26 -> ANALOG */
    board_init_adc16_diff_pins();   /* PF28/PF30 -> ANALOG */

    s_adc_clk_hz = board_init_adc_clock(HPM_ADC0, true);
    uint32_t div = 0;
    if (adc16_calc_clock_divider(s_adc_clk_hz, ADC16_SOC_CONV_CLK_FREQ_MAX, &div) != status_success) {
        printf("[adc] clock divider failed (clk=%lu)\r\n", (unsigned long)s_adc_clk_hz);
        return false;
    }
    adc16_config_t cfg;
    adc16_get_default_config(&cfg);
    cfg.res = adc16_res_16_bits;
    cfg.conv_mode = adc16_conv_mode_oneshot;
    cfg.adc_clk_div = div;
#if !defined(HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB) || !HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB
    cfg.sel_sync_ahb = (clk_adc_src_ahb0 == clock_get_source(BOARD_APP_ADC16_CLK_NAME)) ? true : false;
#endif
    if (adc16_init(HPM_ADC0, &cfg) != status_success) {
        printf("[adc] adc16_init failed\r\n");
        return false;
    }
    const uint8_t chs[ADC_CH_NUM] = { ADC_CH0, ADC_CH1, ADC_CH2 };
    for (uint32_t i = 0; i < ADC_CH_NUM; i++) {
        adc16_channel_config_t ch_cfg;
        adc16_get_channel_default_config(&ch_cfg);
        ch_cfg.ch = chs[i];
        ch_cfg.sample_cycle = ADC_SAMPLE_CYCLE;
        if (adc16_init_channel(HPM_ADC0, &ch_cfg) != status_success) {
            printf("[adc] channel %u init failed\r\n", (unsigned)chs[i]);
            return false;
        }
    }
#if defined(ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT) && ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT
    adc16_enable_oneshot_mode(HPM_ADC0);
#endif
    adc16_set_blocking_read(HPM_ADC0);
    printf("[adc] ADC0 IN%u/%u/%u oneshot, conv clk %lu Hz, sample_cycle %u\r\n",
           (unsigned)ADC_CH0, (unsigned)ADC_CH1, (unsigned)ADC_CH2,
           (unsigned long)(s_adc_clk_hz / div), (unsigned)ADC_SAMPLE_CYCLE);
    return true;
}

static void adc_sample(uint16_t *v)
{
    const uint8_t chs[ADC_CH_NUM] = { ADC_CH0, ADC_CH1, ADC_CH2 };
    for (uint32_t i = 0; i < ADC_CH_NUM; i++) {
        uint16_t raw = 0;
        if (adc16_get_oneshot_result(HPM_ADC0, chs[i], &raw) != status_success) {
            raw = 0;
        }
        v[i] = raw;
    }
}

/* ------------------------------------------------------------------ */
/* 打印统计 / 结论                                                     */
/* ------------------------------------------------------------------ */
static void print_stats(void)
{
    app_xrce_stats_t st;
    app_xrce_get_stats(&st);
    printf("[xrce] state=%s up=%lu ms hello=%lu adc=%lu cmd=%lu led=%u\r\n",
           app_xrce_state_str(), (unsigned long)st.uptime_ms,
           (unsigned long)s_hello_seq, (unsigned long)s_adc_seq,
           (unsigned long)s_cmd_count, (unsigned)s_led_state);
    printf("[xrce] tx: samples=%lu dg=%lu bytes=%lu drop=%lu err=%lu | rx: dg=%lu bytes=%lu samples=%lu ovr=%lu\r\n",
           (unsigned long)st.tx_samples, (unsigned long)st.tx_datagrams, (unsigned long)st.tx_bytes,
           (unsigned long)st.tx_drops, (unsigned long)st.net_errors,
           (unsigned long)st.rx_datagrams, (unsigned long)st.rx_bytes,
           (unsigned long)st.rx_samples, (unsigned long)st.rx_overrun);
    printf("[xrce] cpu0=%lu MHz bsp_up=%lu ms loop=%lu/s adc_cost=%lu us poll_cost=%lu us\r\n",
           (unsigned long)(clock_get_frequency(clock_cpu0) / 1000000U), (unsigned long)bsp_uptime_ms(),
           (unsigned long)(s_loop_cnt * 1000U / ((st.uptime_ms != 0U) ? st.uptime_ms : 1U)),
           (unsigned long)(s_adc_cyc / ((s_adc_seq != 0U) ? s_adc_seq : 1U) / 480U),
           (unsigned long)(s_poll_cyc / ((s_loop_cnt != 0U) ? s_loop_cnt : 1U) / 480U));
    printf("[xrce] reliable: acked=%lu retrans=%lu reconnects=%lu pub_err=%lu\r\n",
           (unsigned long)st.acks, (unsigned long)st.retransmits,
           (unsigned long)st.reconnects, (unsigned long)s_pub_err);
    const char *verdict = (st.connected && (st.tx_samples > 0U)) ? "PASS" : "FAIL";
    uint32_t win = (st.uptime_ms > s_rate_t0_ms) ? (st.uptime_ms - s_rate_t0_ms) : 1U;
    printf("[xrce] STAT hello=%.1f/s adc=%.1f/s acked=%lu => %s\r\n",
           (double)s_hello_seq * 1000.0 / (double)win,
           (double)s_adc_seq * 1000.0 / (double)win,
           (unsigned long)st.acks, verdict);
}

/* ------------------------------------------------------------------ */
/* shell 命令                                                          */
/* ------------------------------------------------------------------ */
static void cmd_xrce(int argc, char *argv[])
{
    (void)argc; (void)argv;
    printf("xrce: agent=%u.%u.%u.%u:%u local=%u domain=%u\r\n",
           (unsigned)((XRCE_AGENT_IP >> 24) & 0xFFU), (unsigned)((XRCE_AGENT_IP >> 16) & 0xFFU),
           (unsigned)((XRCE_AGENT_IP >> 8) & 0xFFU), (unsigned)(XRCE_AGENT_IP & 0xFFU),
           (unsigned)XRCE_AGENT_PORT, (unsigned)XRCE_LOCAL_PORT, (unsigned)XRCE_DDS_DOMAIN_ID);
    printf("xrce: pubs hello(rt/hpm5e31/hello, String, 10Hz, reliable)/adc(rt/hpm5e31/adc, UInt16MultiArray, 100Hz, best-effort)\r\n");
    printf("xrce: subs cmd(rt/hpm5e31/cmd, UInt8)  last_cmd=%u\r\n", (unsigned)s_last_cmd);
    print_stats();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 xrce, cmd_xrce, show XRCE session/entity/stats);

static void cmd_stat(int argc, char *argv[])
{
    (void)argc; (void)argv;
    print_stats();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, show XRCE counters + PASS/FAIL verdict);

static void cmd_pub(int argc, char *argv[])
{
    uint8_t buf[128];
    const char *msg = (argc >= 2) ? argv[1] : "manual";
    uint32_t n = cdr_encode_string(buf, msg);
    int rc = app_xrce_publish(s_pub_hello, buf, n);
    printf("[pub] manual \"%s\" len=%lu rc=%d\r\n", msg, (unsigned long)n, rc);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 pub, cmd_pub, publish one String sample: pub <text>);

static void cmd_rst(int argc, char *argv[])
{
    (void)argc; (void)argv;
    s_hello_seq = 0;
    s_adc_seq = 0;
    s_cmd_count = 0;
    s_pub_err = 0;
    printf("[xrce] counters reset\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 rst, cmd_rst, reset XRCE counters);

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */
int main(void)
{
    bsp_init();
    printf("\r\n=== xrce_pubsub: Micro-XRCE-DDS Client over lwIP UDP (custom transport) ===\r\n");
    printf("lwIP %s\r\n", LWIP_VERSION_STRING);

    if (enet_init(ENET) != status_success) {
        printf("Enet initialization fails !!!\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }
    lwip_init();
    netif_config(&gnetif);
    enet_services(&gnetif);
    board_timer_create(LWIP_APP_TIMER_INTERVAL, sys_timer_callback);   /* lwIP 1 ms 时基 */

    printf("[net] board %s/%s gw %s link=%s\r\n",
           ipaddr_ntoa(netif_ip_addr4(&gnetif)), ipaddr_ntoa(netif_ip_netmask4(&gnetif)),
           ipaddr_ntoa(netif_ip_gw4(&gnetif)), netif_is_link_up(&gnetif) ? "UP" : "DOWN");

    board_init_led_pins();
    gpio_set_pin_output(BOARD_LED_GPIO_CTRL, BOARD_LED_GPIO_INDEX, BOARD_LED_GPIO_PIN);
    board_led_write(0);

    s_adc_ok = adc_init();

    /* ---- XRCE 初始化 ---- */
    app_xrce_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.agent_ip = XRCE_AGENT_IP;
    cfg.agent_port = XRCE_AGENT_PORT;
    cfg.local_port = XRCE_LOCAL_PORT;
    cfg.key = XRCE_SESSION_KEY;
    cfg.domain_id = XRCE_DDS_DOMAIN_ID;
    cfg.participant_name = "hpm5e31_participant";
    cfg.out_reliable_buf = s_out_rel_buf;
    cfg.out_reliable_size = sizeof(s_out_rel_buf);
    cfg.out_reliable_history = STREAM_HISTORY;
    cfg.in_reliable_buf = s_in_rel_buf;
    cfg.in_reliable_size = sizeof(s_in_rel_buf);
    cfg.in_reliable_history = STREAM_HISTORY;
    cfg.poll_timeout_ms = 1U;   /* R5 U-36-5: 会话等待可配；0 = 非阻塞（实验特性） */
    cfg.out_be_buf = s_out_be_buf;
    cfg.out_be_size = sizeof(s_out_be_buf);

    app_xrce_set_net_poll(net_poll);
    int rc = app_xrce_init(&cfg);
    printf("[xrce] init rc=%d state=%s\r\n", rc, app_xrce_state_str());

    app_xrce_pub_cfg_t pub_hello = {
        .topic_name = "rt/hpm5e31/hello",
        .type_name = "std_msgs::msg::dds_::String_",
        .writer_oid = 0x01,
        .reliable_qos = true,
        .reliable_stream = true,
        .history_depth = 8,
    };
    app_xrce_pub_cfg_t pub_adc = {
        .topic_name = "rt/hpm5e31/adc",
        .type_name = "std_msgs::msg::dds_::UInt16MultiArray_",
        .writer_oid = 0x02,
        .reliable_qos = true,       /* ROS 2 默认 Subscriber 是 RELIABLE，必须匹配 */
        .reliable_stream = false,   /* 但数据走 best-effort 流：100 Hz 不占用可靠窗口 */
        .history_depth = 8,
    };
    app_xrce_sub_cfg_t sub_cmd = {
        .topic_name = "rt/hpm5e31/cmd",
        .type_name = "std_msgs::msg::dds_::UInt8_",
        .reader_oid = 0x03,
        .reliable_qos = false,      /* 对端 Publisher(RELIABLE) -> 本端 BE 是兼容的 */
        .history_depth = 8,
        .on_data = on_cmd,
        .arg = NULL,
    };
    s_pub_hello = app_xrce_add_pub(&pub_hello);
    s_pub_adc = app_xrce_add_pub(&pub_adc);
    s_sub_cmd = app_xrce_add_sub(&sub_cmd);
    printf("[xrce] entities: hello=%d adc=%d cmd=%d\r\n", s_pub_hello, s_pub_adc, s_sub_cmd);
    printf("[xrce] shell: xrce | stat | pub [text] | rst\r\n");

    s_rate_t0_ms = app_xrce_now_ms();
    s_last_stat_ms = s_rate_t0_ms;
    s_last_hello_ms = s_last_stat_ms;
    s_last_adc_ms = s_last_stat_ms;
    s_next_adc_ms = s_last_stat_ms + ADC_PERIOD_MS;
    s_next_hello_ms = s_last_stat_ms + HELLO_PERIOD_MS;
    print_stats();

    while (1) {
        uint64_t t0 = hpm_csr_get_core_mcycle();
        bsp_shell_poll();
        app_xrce_poll();      /* 内部驱动 lwIP（通过 net_poll 钩子）+ 跑 1 ms 会话 */
        s_poll_cyc += hpm_csr_get_core_mcycle() - t0;
        s_loop_cnt++;

        uint32_t now = app_xrce_now_ms();

        if ((int32_t)(now - s_next_adc_ms) >= 0) {
            s_next_adc_ms += ADC_PERIOD_MS;
            if ((int32_t)(now - s_next_adc_ms) > 200) {
                s_next_adc_ms = now + ADC_PERIOD_MS;   /* 落后太多则重新对齐，避免突发补发 */
            }
            s_last_adc_ms = now;
            uint16_t v[4];
            uint64_t ta = hpm_csr_get_core_mcycle();
            if (s_adc_ok) {
                adc_sample(v);
            } else {
                v[0] = v[1] = v[2] = 0;
            }
            s_adc_cyc += hpm_csr_get_core_mcycle() - ta;
            s_adc_seq++;
            v[3] = (uint16_t)(s_adc_seq & 0xFFFFU);   /* 通道 4 = 采样序号（丢包核对用） */
            uint8_t cdr[32];
            uint32_t n = cdr_encode_u16_multiarray(cdr, v, 4);
            if (app_xrce_publish(s_pub_adc, cdr, n) != 0) {
                s_pub_err++;
            }
        }

        if ((int32_t)(now - s_next_hello_ms) >= 0) {
            s_next_hello_ms += HELLO_PERIOD_MS;
            if ((int32_t)(now - s_next_hello_ms) > 500) {
                s_next_hello_ms = now + HELLO_PERIOD_MS;
            }
            s_last_hello_ms = now;
            s_hello_seq++;
            char text[40];
            snprintf(text, sizeof(text), "hello %lu", (unsigned long)s_hello_seq);
            uint8_t cdr[64];
            uint32_t n = cdr_encode_string(cdr, text);
            if (app_xrce_publish(s_pub_hello, cdr, n) != 0) {
                s_pub_err++;
            }
        }

        if ((now - s_last_stat_ms) >= STAT_PERIOD_MS) {
            s_last_stat_ms = now;
            print_stats();
        }
    }
    return 0;
}
