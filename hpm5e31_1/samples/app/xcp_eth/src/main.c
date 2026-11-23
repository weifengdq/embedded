/*
 * xcp_eth —— XCP on UDP（XCPonEth）从站 + DAQ 测量（第 35 章例程 3）
 *
 * 硬件：ENET0 RGMII + YT8011AN <-> 千兆车载以太网盒 <-> 主机 enp6s0
 *       板 192.168.2.100/24，主机 192.168.2.1/24，UDP 5555（XCPonEth 标准端口）
 *
 * 与 xcp_can_daq 的区别：
 *   - 传输层换成 components/app_xcp/xcp_port_udp.c（4 B LEN/CTR 头、单包多 ODT）；
 *   - XCP_MAX_DTO 从 8 放大到 256（CMake -DXCP_MAX_DTO=256），一条 DTO 能装更多条目；
 *   - 事件通道 3 路：EVT0 1 kHz / EVT1 10 kHz / EVT2 100 Hz，主机可选事件做带宽压力测试；
 *   - 标定页仍是 RAM 单页（g_kp/g_ki/g_ctrl_max_pm/g_settle_ms/g_pwm_duty_pm/g_sw_flag）。
 *
 * 被测变量（A2L 由 tools/gen_a2l.py 从 ELF 生成，见 samples/app/xcp_eth/xcp_eth.a2l）：
 *   g_adc_raw      u16  ADC0.IN1（PF26，悬空噪声底）
 *   g_tick_cnt     u32  EVT0 计数（1 kHz，主机按连续差判丢帧）
 *   g_tick_10k     u32  EVT1 计数（10 kHz，带宽压力测试用）
 *   g_tick_100     u32  EVT2 计数（100 Hz）
 *   g_pid_mid      i32  PID 积分项（定点 1/1000）
 *   g_cpu_load_pm  u16  主循环负载（千分比）
 *   g_u8_cnt       u8   EVT0 计数低 8 位
 *   g_kp/g_ki      f32  标定量（PID 增益）
 *   g_ctrl_max_pm  u16  控制量限幅（千分比）
 *   g_settle_ms    u16  观察窗口（演示用标定量）
 *   g_pwm_duty_pm  u16  占空比标定量（写读回演示）
 *   g_sw_flag      u32  位操作测试用
 *
 * shell（letter-shell，921600 8N1）：
 *   xcp start | stop | stat | info | daq [ms] | cal ... | seed <0|1> | ip
 * 上电自动起网并打印 "XCP-ETH INIT PASS"（hwrun 抓这一行自证）。
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bsp.h"
#include "board.h"
#include "board_timer.h"
#include "clock.h"
#include "hpm_adc16_drv.h"
#include "hpm_csr_drv.h"
#include "hpm_gptmr_drv.h"
#include "hpm_interrupt.h"

#include "app_lwip.h"
#include "xcp.h"
#include "xcp_port_udp.h"

/* ---------------- 被测变量（A2L 用它们的绝对地址） ---------------- */
volatile uint16_t g_adc_raw;
volatile uint32_t g_tick_cnt;
volatile uint32_t g_tick_10k;
volatile uint32_t g_tick_100;
volatile int32_t  g_pid_mid;
volatile uint16_t g_cpu_load_pm;
volatile uint8_t  g_u8_cnt;
volatile uint32_t g_sw_flag = 0x12345678u;
volatile float    g_kp = 1.2f;
volatile float    g_ki = 0.8f;
volatile uint16_t g_ctrl_max_pm = 800u;
volatile uint16_t g_settle_ms = 120u;
volatile uint16_t g_pwm_duty_pm = 500u;

/* 板端统计镜像：主循环 1 Hz 刷新，主机用 SHORT_UPLOAD 读回（与主机侧统计交叉验证）。
 * 目的：区分"板端没发/发送失败"与"链路丢包"——UDP 没有重传，丢在哪一侧必须能说清。 */
volatile uint32_t g_stat_tx_datagrams;
volatile uint32_t g_stat_tx_msgs;
volatile uint32_t g_stat_tx_drop;
volatile uint32_t g_stat_tx_err;
volatile uint32_t g_stat_rx_datagrams;
volatile uint32_t g_stat_rx_ctr_gap;
volatile uint32_t g_stat_daq_frames;
volatile uint32_t g_stat_daq_dropped;
volatile uint32_t g_stat_daq_overload;
volatile uint32_t g_stat_cto_rx;

static uint32_t s_stat_ts_us;

#define APP_TMR         BOARD_TIMER_GPTMR0
#define APP_TMR_IRQ     BOARD_TIMER_GPTMR0_IRQ
#define APP_CH_1K       (0u)
#define APP_CH_10K      (1u)
#define APP_CH_100      (2u)

#define APP_ADC_BASE    HPM_ADC0
#define APP_ADC_CH      (1u)

/* 板端事件通道号（与 A2L 的 EVENT number 一致） */
#define EVT_1MS         (0u)
#define EVT_100US       (1u)
#define EVT_10MS        (2u)

static uint32_t s_cpu_mhz;
static uint32_t s_tmr_clk;
static volatile uint32_t s_adc_req;
static bool s_lwip_up;
static bool s_xcp_up;

static uint32_t s_idle_base;
static uint32_t s_last_load_us;
static uint32_t s_load_cnt;

/* ---------------- 时基 ---------------- */

static uint32_t now_us(void)
{
    return (uint32_t) (hpm_csr_get_core_mcycle() / (uint64_t) s_cpu_mhz);
}

/* ---------------- 1 kHz 控制环（一阶对象 + PI） ---------------- */

static float s_y;
static float s_integ;

static void pid_step(void)
{
    const float dt = 0.001f;
    float err = 1000.0f - s_y;
    float u;
    float lim = (float) g_ctrl_max_pm;

    s_integ += g_ki * err * dt;
    if (s_integ > lim) {
        s_integ = lim;
    } else if (s_integ < -lim) {
        s_integ = -lim;
    }
    u = g_kp * err + s_integ;
    if (u > lim) {
        u = lim;
    } else if (u < -lim) {
        u = -lim;
    }
    s_y += (u - s_y) * (dt / 0.05f);        /* 一阶对象 tau = 50 ms */
    if (s_y > 4000.0f) {
        s_y = 4000.0f;
    } else if (s_y < -4000.0f) {
        s_y = -4000.0f;
    }
    g_pid_mid = (int32_t) (s_integ * 1000.0f);
}

/* ---------------- 事件中断：1 kHz / 10 kHz / 100 Hz ---------------- */

SDK_DECLARE_EXT_ISR_M(APP_TMR_IRQ, app_tmr_isr)
void app_tmr_isr(void)
{
    if (gptmr_check_status(APP_TMR, GPTMR_CH_RLD_STAT_MASK(APP_CH_1K))) {
        gptmr_clear_status(APP_TMR, GPTMR_CH_RLD_STAT_MASK(APP_CH_1K));
        g_tick_cnt++;
        g_u8_cnt++;
        pid_step();
        s_adc_req = 1u;
        xcp_event(EVT_1MS, now_us());
    }
    if (gptmr_check_status(APP_TMR, GPTMR_CH_RLD_STAT_MASK(APP_CH_10K))) {
        gptmr_clear_status(APP_TMR, GPTMR_CH_RLD_STAT_MASK(APP_CH_10K));
        g_tick_10k++;
        xcp_event(EVT_100US, now_us());
    }
    if (gptmr_check_status(APP_TMR, GPTMR_CH_RLD_STAT_MASK(APP_CH_100))) {
        gptmr_clear_status(APP_TMR, GPTMR_CH_RLD_STAT_MASK(APP_CH_100));
        g_tick_100++;
        xcp_event(EVT_10MS, now_us());
    }
}

static void tmr_setup(void)
{
    gptmr_channel_config_t cfg;

    init_gptmr0_clock();
    s_tmr_clk = clock_get_frequency(BOARD_TIMER_GPTMR0_CLK);

    gptmr_channel_get_default_config(APP_TMR, &cfg);
    cfg.mode = gptmr_work_mode_no_capture;
    cfg.cmp[0] = 0xFFFFFFFFu;
    cfg.cmp[1] = 0xFFFFFFFFu;
    cfg.enable_cmp_output = false;

    cfg.reload = s_tmr_clk / 1000u;             /* EVT0 1 kHz */
    (void) gptmr_channel_config(APP_TMR, APP_CH_1K, &cfg, false);
    gptmr_channel_reset_count(APP_TMR, APP_CH_1K);
    gptmr_enable_irq(APP_TMR, GPTMR_CH_RLD_IRQ_MASK(APP_CH_1K));

    cfg.reload = s_tmr_clk / 10000u;            /* EVT1 10 kHz（压力测试） */
    (void) gptmr_channel_config(APP_TMR, APP_CH_10K, &cfg, false);
    gptmr_channel_reset_count(APP_TMR, APP_CH_10K);
    gptmr_enable_irq(APP_TMR, GPTMR_CH_RLD_IRQ_MASK(APP_CH_10K));

    cfg.reload = s_tmr_clk / 100u;              /* EVT2 100 Hz */
    (void) gptmr_channel_config(APP_TMR, APP_CH_100, &cfg, false);
    gptmr_channel_reset_count(APP_TMR, APP_CH_100);
    gptmr_enable_irq(APP_TMR, GPTMR_CH_RLD_IRQ_MASK(APP_CH_100));

    gptmr_start_counter(APP_TMR, APP_CH_1K);
    gptmr_start_counter(APP_TMR, APP_CH_10K);
    gptmr_start_counter(APP_TMR, APP_CH_100);
    intc_m_enable_irq_with_priority(APP_TMR_IRQ, 1);
}

/* ---------------- ADC（1 kHz 触发，主循环读） ---------------- */

static bool adc_setup(void)
{
    adc16_config_t cfg;
    adc16_channel_config_t ch_cfg;
    uint32_t adc_clk;
    uint32_t adc_div = 0u;

    board_init_adc16_pins();
    adc_clk = board_init_adc_clock(APP_ADC_BASE, true);
    if ((adc_clk == 0u) ||
        (adc16_calc_clock_divider(adc_clk, ADC16_SOC_CONV_CLK_FREQ_MAX, &adc_div) != status_success)) {
        return false;
    }
    adc16_get_default_config(&cfg);
    cfg.res = adc16_res_16_bits;
    cfg.conv_mode = adc16_conv_mode_oneshot;
    cfg.adc_clk_div = adc_div;
#if !defined(HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB) || !HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB
    cfg.sel_sync_ahb = (clk_adc_src_ahb0 == clock_get_source(BOARD_APP_ADC16_CLK_NAME)) ? true : false;
#endif
    if (adc16_init(APP_ADC_BASE, &cfg) != status_success) {
        return false;
    }
    adc16_get_channel_default_config(&ch_cfg);
    ch_cfg.ch = APP_ADC_CH;
    ch_cfg.sample_cycle = 20u;
    if (adc16_init_channel(APP_ADC_BASE, &ch_cfg) != status_success) {
        return false;
    }
#if defined(ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT) && ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT
    adc16_enable_oneshot_mode(APP_ADC_BASE);
#endif
    adc16_set_blocking_read(APP_ADC_BASE);
    return true;
}

static void adc_service(void)
{
    uint16_t raw;

    if (s_adc_req == 0u) {
        return;
    }
    s_adc_req = 0u;
    for (int i = 0; i < 4; i++) {
        if (adc16_get_oneshot_result(APP_ADC_BASE, APP_ADC_CH, &raw) == status_success) {
            g_adc_raw = raw;
            return;
        }
    }
}

/* ---------------- 板端统计镜像（1 Hz） ---------------- */

static void stat_service(void)
{
    uint32_t t = now_us();
    xcp_stats_t xs;
    xcp_udp_stats_t us;

    if ((uint32_t) (t - s_stat_ts_us) < 1000000u) {
        return;
    }
    s_stat_ts_us = t;
    xcp_get_stats(&xs);
    xcp_udp_get_stats(&us);
    g_stat_tx_datagrams = us.tx_datagrams;
    g_stat_tx_msgs = us.tx_msgs;
    g_stat_tx_drop = us.tx_drop;
    g_stat_tx_err = us.tx_err;
    g_stat_rx_datagrams = us.rx_datagrams;
    g_stat_rx_ctr_gap = us.rx_ctr_gap;
    g_stat_daq_frames = xs.daq_frames;
    g_stat_daq_dropped = xs.daq_dropped;
    g_stat_daq_overload = xs.daq_overload;
    g_stat_cto_rx = xs.cto_rx;
}

/* ---------------- CPU 负载 ---------------- */

static void load_service(void)
{
    uint32_t t = now_us();

    s_load_cnt++;
    if ((uint32_t) (t - s_last_load_us) < 1000000u) {
        return;
    }
    s_last_load_us = t;
    if (s_idle_base != 0u) {
        uint32_t pm;
        if (s_load_cnt >= s_idle_base) {
            pm = 0u;
        } else {
            pm = 1000u - (uint32_t) (((uint64_t) s_load_cnt * 1000u) / s_idle_base);
        }
        g_cpu_load_pm = (uint16_t) pm;
    }
    s_load_cnt = 0u;
}

static void idle_baseline(void)
{
    uint32_t cnt = 0u;
    uint32_t t0 = now_us();

    while ((uint32_t) (now_us() - t0) < 50000u) {
        cnt++;
    }
    s_idle_base = cnt * 20u;
    s_last_load_us = now_us();
    s_load_cnt = 0u;
}

/* ---------------- XCP 配置 ---------------- */

static const xcp_mem_region_t s_regions[] = {
    {(uint32_t) 0x01200000u, (uint32_t) 0x01240000u, (uint8_t) (XCP_MEM_READ | XCP_MEM_WRITE), "AXI SRAM .data/.bss"},
    {(uint32_t) 0xF0200000u, (uint32_t) 0xF0204000u, (uint8_t) (XCP_MEM_READ | XCP_MEM_WRITE), "AHB SRAM"},
    {(uint32_t) 0x80000000u, (uint32_t) 0x80100000u, (uint8_t) XCP_MEM_READ, "XIP flash (.text/.rodata)"},
};

static const xcp_event_def_t s_events[] = {
    {"EVT_1ms", 1u, XCP_TIMEUNIT_1MS, 0u, 4u},
    {"EVT_100us", 100u, XCP_TIMEUNIT_1US, 1u, 4u},
    {"EVT_10ms", 10u, XCP_TIMEUNIT_1MS, 2u, 4u},
};

static const uint8_t s_id_string[] = "HPM5E31-XCP-ETH 1.0 (UDP 5555, DAQ)";

static const xcp_cfg_t s_xcp_cfg = {
    .regions = s_regions,
    .region_count = (uint8_t) (sizeof(s_regions) / sizeof(s_regions[0])),
    .events = s_events,
    .event_count = (uint8_t) (sizeof(s_events) / sizeof(s_events[0])),
    .now_us = now_us,
    .id_string = (const char *) s_id_string,
    .seed = NULL,
    .seed_len = 0u,
    .on_connect = NULL,
    .on_disconnect = NULL,
};

/* ---------------- 网络 / XCP 启停 ---------------- */

static int net_start(void)
{
    int rc;

    if (!s_lwip_up) {
        rc = app_lwip_init(NULL);
        printf("[net] app_lwip_init rc=%d\r\n", rc);
        if (rc < 0) {
            return rc;
        }
        s_lwip_up = true;
    }
    rc = xcp_udp_init(XCP_UDP_DEFAULT_PORT);
    if (rc != 0) {
        printf("[net] xcp_udp_init FAIL rc=%d（检查 lwIP 是否已初始化/UDP pcb 是否耗尽）\r\n", rc);
        return rc;
    }
    xcp_set_transport(&xcp_udp_ops);
    if (xcp_init(&s_xcp_cfg) != 0) {
        printf("[net] xcp_init FAIL\r\n");
        xcp_udp_close();
        return -1;
    }
    s_xcp_up = true;
    printf("[net] XCPonEth 从站就绪：%s:%u，MAX_CTO=%u MAX_DTO=%u\r\n",
           APP_LWIP_DEFAULT_IP, (unsigned) XCP_UDP_DEFAULT_PORT,
           (unsigned) XCP_MAX_CTO, (unsigned) XCP_MAX_DTO);
    return 0;
}

static void net_stop(void)
{
    if (s_xcp_up && xcp_is_connected()) {
        static const uint8_t disc = XCP_CMD_DISCONNECT;
        xcp_rx_cto(&disc, 1u);
    }
    xcp_udp_close();
    s_xcp_up = false;
    printf("[net] XCPonEth 已停止（socket 关闭；板端事件仍在跑，DAQ 无列表时只是空转）\r\n");
}

/* ---------------- shell ---------------- */

static void cmd_xcp_info(void)
{
    printf("sample   : xcp_eth（XCP on UDP / XCPonEth 从站 + DAQ）\r\n");
    printf("net      : %s/24 gw %s，UDP %u；主机 enp6s0 = 192.168.2.1\r\n",
           APP_LWIP_DEFAULT_IP, APP_LWIP_DEFAULT_GW, (unsigned) XCP_UDP_DEFAULT_PORT);
    printf("limits   : MAX_CTO=%u MAX_DTO=%u，%u 列表 x %u ODT x %u 条目/ODT，发送队列 %u 帧\r\n",
           (unsigned) XCP_MAX_CTO, (unsigned) XCP_MAX_DTO,
           (unsigned) XCP_MAX_DAQ_LISTS, (unsigned) XCP_MAX_ODT_PER_LIST,
           (unsigned) XCP_MAX_ENTRY_PER_ODT, (unsigned) XCP_DAQ_TX_QUEUE_LEN);
    printf("events   : EVT0 1 kHz / EVT1 10 kHz / EVT2 100 Hz（GPTMR0 三通道）\r\n");
    printf("regions  : AXI SRAM 0x01200000+256KB(RW) / AHB SRAM 0xF0200000+16KB(RW) / XIP 0x80000000+1MB(R)\r\n");
    printf("timebase : %lu MHz cpu mcycle -> 1 us；4 B 传输层头（LEN/CTR 小端，单包多 ODT）\r\n",
           (unsigned long) s_cpu_mhz);
    printf("commands : xcp start|stop|stat|info|daq [ms]|cal ...|seed <0|1>|ip\r\n");
}

static void cmd_xcp_stat(void)
{
    xcp_stats_t xs;
    xcp_udp_stats_t us;

    xcp_get_stats(&xs);
    xcp_udp_get_stats(&us);
    printf("state    : lwip=%u xcp=%u connected=%u last_err=0x%02X\r\n",
           (unsigned) (s_lwip_up ? 1 : 0), (unsigned) (s_xcp_up ? 1 : 0),
           (unsigned) (xcp_is_connected() ? 1 : 0), (unsigned) xcp_last_error());
    printf("core     : cto_rx=%lu res_tx=%lu err_tx=%lu ignored=%lu mem_denied=%lu\r\n",
           (unsigned long) xs.cto_rx, (unsigned long) xs.res_tx, (unsigned long) xs.err_tx,
           (unsigned long) xs.cmd_ignored, (unsigned long) xs.mem_denied);
    printf("daq      : frames=%lu samples=%lu dropped=%lu overload=%lu\r\n",
           (unsigned long) xs.daq_frames, (unsigned long) xs.daq_samples,
           (unsigned long) xs.daq_dropped, (unsigned long) xs.daq_overload);
    printf("events   : evt0=%lu evt1=%lu evt2=%lu（tick: 1k=%lu 10k=%lu 100=%lu）\r\n",
           (unsigned long) xs.daq_events[0], (unsigned long) xs.daq_events[1],
           (unsigned long) xs.daq_events[2], (unsigned long) g_tick_cnt,
           (unsigned long) g_tick_10k, (unsigned long) g_tick_100);
    printf("udp rx   : datagrams=%lu msgs=%lu bytes=%lu framing_err=%lu ctr_gap=%lu ctr_dup=%lu\r\n",
           (unsigned long) us.rx_datagrams, (unsigned long) us.rx_msgs, (unsigned long) us.rx_bytes,
           (unsigned long) us.rx_framing_err, (unsigned long) us.rx_ctr_gap, (unsigned long) us.rx_ctr_dup);
    printf("udp tx   : datagrams=%lu msgs=%lu bytes=%lu drop=%lu err=%lu max_msgs/pkt=%lu max_pkt=%luB\r\n",
           (unsigned long) us.tx_datagrams, (unsigned long) us.tx_msgs, (unsigned long) us.tx_bytes,
           (unsigned long) us.tx_drop, (unsigned long) us.tx_err,
           (unsigned long) us.max_msgs_per_pkt, (unsigned long) us.max_pkt_bytes);
    printf("udp peer : %s %u.%u.%u.%u:%u（tx_ctr=%u rx_ctr=%u）\r\n",
           us.peer_valid ? "valid" : "none",
           us.peer_ip[0], us.peer_ip[1], us.peer_ip[2], us.peer_ip[3], (unsigned) us.peer_port,
           (unsigned) us.tx_ctr, (unsigned) us.rx_ctr_last);
    printf("meas     : adc=%u pid_mid=%ld cpu_load=%u‰ u8=%u tick=%lu tick10k=%lu tick100=%lu\r\n",
           (unsigned) g_adc_raw, (long) g_pid_mid, (unsigned) g_cpu_load_pm,
           (unsigned) g_u8_cnt, (unsigned long) g_tick_cnt,
           (unsigned long) g_tick_10k, (unsigned long) g_tick_100);
    for (uint8_t i = 0; i < XCP_MAX_DAQ_LISTS; i++) {
        xcp_daq_list_info_t li;
        if (xcp_get_daq_list_info(i, &li) != 0) {
            continue;
        }
        printf("  list%u: state=0x%02X mode=0x%02X evt=%u presc=%u first_pid=%u odts=%u entries=",
               (unsigned) i, (unsigned) li.state, (unsigned) li.mode, (unsigned) li.event,
               (unsigned) li.prescaler, (unsigned) li.first_pid, (unsigned) li.odt_count);
        for (uint8_t o = 0; o < li.odt_count; o++) {
            printf("%u ", (unsigned) li.odt_entry_count[o]);
        }
        printf("\r\n");
    }
}

static void cmd_xcp_daq(uint32_t ms)
{
    xcp_stats_t a, b;
    xcp_udp_stats_t ua, ub;
    uint32_t t0 = now_us();
    uint32_t dt_us;
    uint32_t ev0, ev1, ev2;
    uint32_t frames, datags, bytes;
    uint32_t tick0 = g_tick_cnt, tick10k0 = g_tick_10k, tick1000 = g_tick_100;

    if (ms < 100u) {
        ms = 1000u;
    }
    if (ms > 60000u) {
        ms = 60000u;
    }
    xcp_get_stats(&a);
    xcp_udp_get_stats(&ua);

    /* 观察窗口：照常 poll，让 USB/shell/网络都不停 */
    while ((uint32_t) (now_us() - t0) < (ms * 1000u)) {
        app_lwip_poll();
        xcp_poll();
    }
    dt_us = (uint32_t) (now_us() - t0);
    if (dt_us == 0u) {
        dt_us = 1u;
    }
    xcp_get_stats(&b);
    xcp_udp_get_stats(&ub);

    ev0 = b.daq_events[0] - a.daq_events[0];
    ev1 = b.daq_events[1] - a.daq_events[1];
    ev2 = b.daq_events[2] - a.daq_events[2];
    frames = b.daq_frames - a.daq_frames;
    datags = ub.tx_datagrams - ua.tx_datagrams;
    bytes = ub.tx_bytes - ua.tx_bytes;

    printf("window   : %lu ms（%lu us）\r\n", (unsigned long) ms, (unsigned long) dt_us);
    printf("events   : evt0=%lu (%.1f Hz) evt1=%lu (%.1f Hz) evt2=%lu (%.1f Hz)\r\n",
           (unsigned long) ev0, (double) ev0 * 1e6 / (double) dt_us,
           (unsigned long) ev1, (double) ev1 * 1e6 / (double) dt_us,
           (unsigned long) ev2, (double) ev2 * 1e6 / (double) dt_us);
    printf("ticks    : d1k=%lu d10k=%lu d100=%lu\r\n",
           (unsigned long) (g_tick_cnt - tick0), (unsigned long) (g_tick_10k - tick10k0),
           (unsigned long) (g_tick_100 - tick1000));
    printf("daq      : frames=%lu (%.0f frame/s) dropped=%lu overload=%lu\r\n",
           (unsigned long) frames, (double) frames * 1e6 / (double) dt_us,
           (unsigned long) (b.daq_dropped - a.daq_dropped),
           (unsigned long) (b.daq_overload - a.daq_overload));
    printf("udp tx   : datagrams=%lu (%.0f pkt/s) bytes=%lu (%.1f KB/s, %.2f Mbit/s) max_msgs/pkt=%lu\r\n",
           (unsigned long) datags, (double) datags * 1e6 / (double) dt_us,
           (unsigned long) bytes, (double) bytes / 1024.0 * 1e6 / (double) dt_us,
           (double) bytes * 8.0 * 1e6 / (double) dt_us / 1e6,
           (unsigned long) ub.max_msgs_per_pkt);
    printf("udp drop : tx_drop=%lu tx_err=%lu rx_framing_err=%lu rx_ctr_gap=%lu\r\n",
           (unsigned long) (ub.tx_drop - ua.tx_drop), (unsigned long) (ub.tx_err - ua.tx_err),
           (unsigned long) (ub.rx_framing_err - ua.rx_framing_err),
           (unsigned long) (ub.rx_ctr_gap - ua.rx_ctr_gap));
}

static void cmd_xcp_cal(int argc, char *argv[])
{
    if (argc >= 3) {
        if ((strcmp(argv[2], "kp") == 0) && (argc >= 4)) {
            g_kp = (float) atof(argv[3]);
        } else if ((strcmp(argv[2], "ki") == 0) && (argc >= 4)) {
            g_ki = (float) atof(argv[3]);
        } else if ((strcmp(argv[2], "max") == 0) && (argc >= 4)) {
            uint32_t v = 0u;
            (void) bsp_parse_u32(argv[3], &v);
            g_ctrl_max_pm = (uint16_t) v;
        } else if ((strcmp(argv[2], "duty") == 0) && (argc >= 4)) {
            uint32_t v = 0u;
            (void) bsp_parse_u32(argv[3], &v);
            g_pwm_duty_pm = (uint16_t) v;
        } else if (strcmp(argv[2], "reset") == 0) {
            g_kp = 1.2f;
            g_ki = 0.8f;
            g_ctrl_max_pm = 800u;
            g_pwm_duty_pm = 500u;
            g_sw_flag = 0x12345678u;
        } else {
            printf("usage: xcp cal [kp <f>|ki <f>|max <u16>|duty <u16>|reset]\r\n");
            return;
        }
    }
    printf("cal RAM page（单段单页，地址供 A2L/上位机对照）：\r\n");
    printf("  g_kp          = %.4f  @ 0x%08lX\r\n", (double) g_kp, (unsigned long) (uintptr_t) &g_kp);
    printf("  g_ki          = %.4f  @ 0x%08lX\r\n", (double) g_ki, (unsigned long) (uintptr_t) &g_ki);
    printf("  g_ctrl_max_pm = %u     @ 0x%08lX\r\n", (unsigned) g_ctrl_max_pm, (unsigned long) (uintptr_t) &g_ctrl_max_pm);
    printf("  g_settle_ms   = %u     @ 0x%08lX\r\n", (unsigned) g_settle_ms, (unsigned long) (uintptr_t) &g_settle_ms);
    printf("  g_pwm_duty_pm = %u     @ 0x%08lX\r\n", (unsigned) g_pwm_duty_pm, (unsigned long) (uintptr_t) &g_pwm_duty_pm);
    printf("  g_sw_flag     = 0x%08lX @ 0x%08lX\r\n", (unsigned long) g_sw_flag, (unsigned long) (uintptr_t) &g_sw_flag);
}

static void cmd_xcp_seed(int argc, char *argv[])
{
    static const uint8_t seed[4] = {0x11u, 0x22u, 0x33u, 0x44u};
    uint32_t on = 0u;

    if (argc >= 3) {
        (void) bsp_parse_u32(argv[2], &on);
    }
    if (on != 0u) {
        xcp_set_seed(seed, 4u);
        printf("资源保护：开（seed = 11 22 33 44，key = seed ^ 0xA5）\r\n");
    } else {
        xcp_set_seed(NULL, 0u);
        printf("资源保护：关（GET_SEED 返回长度 0）\r\n");
    }
}

static void cmd_xcp(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: xcp start|stop|stat|info|daq [ms]|cal ...|seed <0|1>|ip\r\n");
        return;
    }
    if (strcmp(argv[1], "start") == 0) {
        if (net_start() == 0) {
            printf("XCP-ETH START PASS\r\n");
        } else {
            printf("XCP-ETH START FAIL\r\n");
        }
    } else if (strcmp(argv[1], "stop") == 0) {
        net_stop();
        printf("XCP-ETH STOP\r\n");
    } else if (strcmp(argv[1], "stat") == 0) {
        cmd_xcp_stat();
    } else if (strcmp(argv[1], "info") == 0) {
        cmd_xcp_info();
    } else if (strcmp(argv[1], "daq") == 0) {
        uint32_t ms = 1000u;
        if (argc >= 3) {
            (void) bsp_parse_u32(argv[2], &ms);
        }
        cmd_xcp_daq(ms);
    } else if (strcmp(argv[1], "cal") == 0) {
        cmd_xcp_cal(argc, argv);
    } else if (strcmp(argv[1], "seed") == 0) {
        cmd_xcp_seed(argc, argv);
    } else if (strcmp(argv[1], "ip") == 0) {
        app_lwip_print();
    } else {
        printf("unknown subcommand: %s\r\n", argv[1]);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 xcp, cmd_xcp, XCPonEth control: start|stop|stat|info|daq|cal|seed|ip);

/* ---------------- main ---------------- */

int main(void)
{
    int rc;

    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000u;
    printf("\r\n[boot] xcp_eth：XCP on UDP（XCPonEth）从站 + DAQ 测量\r\n");
    printf("[cfg ] cpu=%lu MHz\r\n", (unsigned long) s_cpu_mhz);

    printf("[init] ADC ...\r\n");
    if (!adc_setup()) {
        printf("[WARN] ADC16 初始化失败，g_adc_raw 恒 0\r\n");
    }
    printf("[init] GPTMR events（1 kHz / 10 kHz / 100 Hz）...\r\n");
    tmr_setup();
    idle_baseline();

    rc = net_start();
    if (rc == 0) {
        app_lwip_print();
        printf("XCP-ETH INIT PASS ip=%s port=%u\r\n", APP_LWIP_DEFAULT_IP, (unsigned) XCP_UDP_DEFAULT_PORT);
    } else {
        printf("XCP-ETH INIT FAIL rc=%d\r\n", rc);
    }

    while (1) {
        bsp_shell_poll();
        app_lwip_poll();        /* 1) lwIP：收 UDP -> XCP 回调，命令响应立即回包 */
        xcp_poll();             /* 2) XCP：先发 DAQ 聚合缓冲，再收（UDP 的收在回调里） */
        adc_service();          /* 3) 应用 */
        stat_service();         /* 4) 板端统计镜像（1 Hz，主机可读回） */
        load_service();
    }
    return 0;
}
