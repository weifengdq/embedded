/*
 * xcp_can_daq —— XCP on CAN 从站 + DAQ 测量（hpm5e31_1 第 34 章例程 1）
 *
 * 硬件（无需上位机、无需飞线）：
 *   CAN0(PD00/PD01) 与 CAN1(PD05/PD04) 经板载 TCAN1044AV 接在同一段总线上，
 *   CAN0 = XCP 从站（0x555 CMD / 0x556 RES+DAQ），CAN1 = 板内最小 XCP 主机
 *   （src/xcp_test_master.c），两者对跑即可验证全流程；接上位机时把 CAN1 换成
 *   cannelloni 网关（tp-infra 的 app_can_gw），主机 vcan0 上用 pyxcp 当主机。
 *
 * 被测变量（A2L 由 tools/gen_a2l.py 从 ELF 符号表生成绝对地址）：
 *   g_adc_raw      u16  ADC0.IN1 原始值（PF26，悬空为噪声底）
 *   g_tick_cnt     u32  1 kHz 事件计数（主机用相邻两帧之差判断丢帧）
 *   g_pid_mid      i32  PID 积分项（定点，1/1000）
 *   g_cpu_load_pm  u16  主循环负载（千分比，1 - 实测迭代/空转基准）
 *   g_u8_cnt       u8   1 kHz 计数低 8 位（填满 ODT0 的 3 字节）
 *
 * DAQ 打包计划（与 src/xcp_test_master.c 的注释一致）：
 *   list0 @EVT0(1 kHz) + 时间戳：ODT0=[ts4|adc2|u8_1] ODT1=[tick4|cpu2] ODT2=[pid4]
 *   list1 @EVT1(100 Hz) 无时间戳：ODT0=[pid4|adc2]
 *
 * 命令：info / stat / selftest [ms] / daq [ms] / evstat / meas / prot <0|1> / clr
 * 上电自动跑一遍 selftest（600 ms）并在串口打印 PASS/FAIL 结论行。
 */
#include "bsp.h"
#include "board_timer.h"
#include "clock.h"
#include "hpm_adc16_drv.h"
#include "hpm_csr_drv.h"
#include "hpm_gptmr_drv.h"
#include "hpm_interrupt.h"

#include "xcp.h"
#include "xcp_port_can.h"
#include "xcp_test_master.h"

/* 共享底座：Bench B 网关（CAN1 <-> UDP 4000 <-> 主机 vcan0） */
#include "app_lwip.h"
#include "app_can_gw.h"
#include "app_can.h"

#include <string.h>

/* ---------------- 被测变量（A2L 用它们的绝对地址） ---------------- */
volatile uint16_t g_adc_raw;
volatile uint32_t g_tick_cnt;
volatile int32_t  g_pid_mid;
volatile uint16_t g_cpu_load_pm;
volatile uint8_t  g_u8_cnt;
volatile uint32_t g_sw_flag = 0x12345678u;   /* 内存读写/位操作测试用 */
volatile float    g_kp = 1.2f;               /* 标定量（标定例程用，这里展示 DAQ 时保持默认） */
volatile float    g_ki = 0.8f;

#define APP_CAN_SLAVE   BOARD_CAN0_BASE
#define APP_CAN_MASTER  BOARD_CAN1_BASE
#define APP_TMR         BOARD_TIMER_GPTMR0
#define APP_TMR_IRQ     BOARD_TIMER_GPTMR0_IRQ
#define APP_CH_1K       (0u)
#define APP_CH_100      (1u)
#define APP_CH_FREE     (3u)

#define APP_ADC_BASE    HPM_ADC0
#define APP_ADC_CH      (1u)

static uint32_t s_cpu_mhz;
static uint32_t s_tmr_clk;
static volatile uint32_t s_adc_req;
static volatile uint32_t s_ev_cycles;       /* xcp_event 累计周期数 */
static volatile uint32_t s_ev_cnt;
static uint32_t s_idle_base;                /* 空转基准（每秒迭代数） */
static uint32_t s_last_load_us;
static uint32_t s_load_cnt;

/* ---------------- 时基 ---------------- */

static uint32_t now_us(void)
{
    return (uint32_t) (hpm_csr_get_core_mcycle() / (uint64_t) s_cpu_mhz);
}

/* ---------------- PID（1 kHz，带一阶被控对象） ---------------- */

static float s_y;           /* 被控量 */
static float s_integ;

static void pid_step(void)
{
    const float dt = 0.001f;
    float err = 1000.0f - s_y;
    float u;

    s_integ += g_ki * err * dt;
    if (s_integ > 500.0f) {
        s_integ = 500.0f;
    } else if (s_integ < -500.0f) {
        s_integ = -500.0f;
    }
    u = g_kp * err + s_integ;               /* 控制量 */
    s_y += (u - s_y) * (dt / 0.05f);        /* 一阶对象 tau=50 ms */
    if (s_y > 4000.0f) {
        s_y = 4000.0f;
    } else if (s_y < -4000.0f) {
        s_y = -4000.0f;
    }
    g_pid_mid = (int32_t) (s_integ * 1000.0f);
}

/* ---------------- 事件中断：1 kHz / 100 Hz ---------------- */

SDK_DECLARE_EXT_ISR_M(APP_TMR_IRQ, app_tmr_isr)
void app_tmr_isr(void)
{
    if (gptmr_check_status(APP_TMR, GPTMR_CH_RLD_STAT_MASK(APP_CH_1K))) {
        uint32_t ts;
        uint64_t c0;

        gptmr_clear_status(APP_TMR, GPTMR_CH_RLD_STAT_MASK(APP_CH_1K));
        ts = now_us();
        g_tick_cnt++;
        g_u8_cnt++;
        pid_step();
        s_adc_req = 1u;
        c0 = hpm_csr_get_core_mcycle();
        xcp_event(0u, ts);                  /* EVT0：1 kHz */
        s_ev_cycles += (uint32_t) (hpm_csr_get_core_mcycle() - c0);
        s_ev_cnt++;
    }
    if (gptmr_check_status(APP_TMR, GPTMR_CH_RLD_STAT_MASK(APP_CH_100))) {
        gptmr_clear_status(APP_TMR, GPTMR_CH_RLD_STAT_MASK(APP_CH_100));
        xcp_event(1u, now_us());            /* EVT1：100 Hz */
    }
}

static void tmr_setup(void)
{
    gptmr_channel_config_t cfg;

    init_gptmr0_clock();
    s_tmr_clk = clock_get_frequency(BOARD_TIMER_GPTMR0_CLK);

    gptmr_channel_get_default_config(APP_TMR, &cfg);
    cfg.mode = gptmr_work_mode_no_capture;
    cfg.reload = s_tmr_clk / 1000u;
    cfg.cmp[0] = 0xFFFFFFFFu;
    cfg.cmp[1] = 0xFFFFFFFFu;
    cfg.enable_cmp_output = false;
    (void) gptmr_channel_config(APP_TMR, APP_CH_1K, &cfg, false);
    gptmr_channel_reset_count(APP_TMR, APP_CH_1K);
    gptmr_enable_irq(APP_TMR, GPTMR_CH_RLD_IRQ_MASK(APP_CH_1K));

    cfg.reload = s_tmr_clk / 100u;
    (void) gptmr_channel_config(APP_TMR, APP_CH_100, &cfg, false);
    gptmr_channel_reset_count(APP_TMR, APP_CH_100);
    gptmr_enable_irq(APP_TMR, GPTMR_CH_RLD_IRQ_MASK(APP_CH_100));

    /* 自由运行时基：中断里不用（直接用 cpu mcycle），保留给示波器/扩展用 */
    cfg.reload = 0xFFFFFFFFu;
    (void) gptmr_channel_config(APP_TMR, APP_CH_FREE, &cfg, false);
    gptmr_channel_reset_count(APP_TMR, APP_CH_FREE);
    gptmr_start_counter(APP_TMR, APP_CH_FREE);

    gptmr_start_counter(APP_TMR, APP_CH_1K);
    gptmr_start_counter(APP_TMR, APP_CH_100);
    intc_m_enable_irq_with_priority(APP_TMR_IRQ, 1);
}

/* ---------------- ADC（主循环读，1 kHz 触发一次） ---------------- */

static bool adc_setup(void)
{
    adc16_config_t cfg;
    adc16_channel_config_t ch_cfg;

    board_init_adc16_pins();
    /* 关键：ADC16 时钟门控必须先开（板级 clock.c），否则 adc16_init 会卡住 */
    uint32_t adc_clk = board_init_adc_clock(APP_ADC_BASE, true);
    uint32_t adc_div = 0u;
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

/* ---------------- CPU 负载 ---------------- */

static void load_service(void)
{
    uint32_t t = now_us();

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

    while ((uint32_t) (now_us() - t0) < 50000u) {   /* 50 ms 空转 */
        cnt++;
    }
    s_idle_base = cnt * 20u;                        /* 折算到每秒 */
    s_last_load_us = now_us();
    s_load_cnt = 0u;
}

/* ---------------- XCP 配置 ---------------- */

static const xcp_mem_region_t s_regions[] = {
    /* A2L 里的绝对地址必须落在这些区间内；区间来自链接脚本（见第 34 章表 34-x） */
    {(uint32_t) 0x01200000u, (uint32_t) 0x01240000u, (uint8_t) (XCP_MEM_READ | XCP_MEM_WRITE), "AXI SRAM .data/.bss"},
    {(uint32_t) 0xF0200000u, (uint32_t) 0xF0204000u, (uint8_t) (XCP_MEM_READ | XCP_MEM_WRITE), "AHB SRAM"},
    {(uint32_t) 0x80000000u, (uint32_t) 0x80100000u, (uint8_t) XCP_MEM_READ, "XIP flash (.text/.rodata)"},
};

static const xcp_event_def_t s_events[] = {
    {"EVT_1ms", 1u, XCP_TIMEUNIT_1MS, 0u, 4u},
    {"EVT_10ms", 10u, XCP_TIMEUNIT_1MS, 1u, 4u},
};

static const uint8_t s_id_string[] = "HPM5E31-XCP-CAN 1.0 (DAQ demo)";

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

/* ---------------- 命令 ---------------- */

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("sample   : xcp_can_daq（XCP on CAN 从站 + DAQ 测量）\r\n");
    printf("CAN      : CAN0 从站 0x555(CMD)/0x556(RES+DAQ) 500 kbit/s；CAN1 板内主机\r\n");
    printf("events   : EVT0 = 1 kHz（时间戳 1 us）、EVT1 = 100 Hz，GPTMR0 触发\r\n");
    printf("DAQ 上限 : %u 列表 x %u ODT x %u 条目/ODT，MAX_DTO=8\r\n",
           (unsigned) XCP_MAX_DAQ_LISTS, (unsigned) XCP_MAX_ODT_PER_LIST, (unsigned) XCP_MAX_ENTRY_PER_ODT);
    printf("内存区域 : AXI SRAM 0x01200000+256KB(RW) / AHB SRAM 0xF0200000+16KB(RW) / XIP 0x80000000+1MB(R)\r\n");
    printf("时基     : %lu MHz cpu mcycle -> 1 us\r\n", (unsigned long) s_cpu_mhz);
    printf("命令     : selftest [ms] / daq [ms] / stat / evstat / meas / prot <0|1> / clr / info\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, show XCP/DAQ configuration);

static void cmd_meas(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("g_adc_raw=%u g_tick_cnt=%lu g_pid_mid=%ld g_cpu_load_pm=%u g_u8_cnt=%u\r\n",
           (unsigned) g_adc_raw, (unsigned long) g_tick_cnt, (long) g_pid_mid,
           (unsigned) g_cpu_load_pm, (unsigned) g_u8_cnt);
    printf("addr: adc=0x%08lX tick=0x%08lX pid=0x%08lX cpu=0x%08lX u8=0x%08lX\r\n",
           (unsigned long) (uintptr_t) &g_adc_raw, (unsigned long) (uintptr_t) &g_tick_cnt,
           (unsigned long) (uintptr_t) &g_pid_mid, (unsigned long) (uintptr_t) &g_cpu_load_pm,
           (unsigned long) (uintptr_t) &g_u8_cnt);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 meas, cmd_meas, dump measurement values and their addresses);

static void cmd_evstat(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    uint32_t n = s_ev_cnt;
    uint32_t cyc = s_ev_cycles;
    float us_per;

    if (n == 0u) {
        printf("evstat: 还没有事件（先在 selftest/daq 里启动 DAQ）\r\n");
        return;
    }
    us_per = (float) cyc / (float) s_cpu_mhz / (float) n;
    printf("evstat: events=%lu cycles/event=%lu => %.2f us/event（xcp_event 打包开销，不含 CAN 发送）\r\n",
           (unsigned long) n, (unsigned long) (cyc / n), (double) us_per);
    xcp_stats_t xs;
    xcp_get_stats(&xs);
    printf("        daq_events[0]=%lu daq_events[1]=%lu daq_frames=%lu dropped=%lu overload=%lu\r\n",
           (unsigned long) xs.daq_events[0], (unsigned long) xs.daq_events[1],
           (unsigned long) xs.daq_frames, (unsigned long) xs.daq_dropped, (unsigned long) xs.daq_overload);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 evstat, cmd_evstat, per-event packing cost);

static void cmd_stat(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    xcp_stats_t xs;
    uint32_t busoff, rxov, txfull;

    xcp_get_stats(&xs);
    xcp_can_get_stats(&busoff, &rxov, &txfull);
    printf("xcp    : connected=%u cto_rx=%lu res_tx=%lu err_tx=%lu ignored=%lu last_err=0x%02X\r\n",
           (unsigned) (xcp_is_connected() ? 1 : 0), (unsigned long) xs.cto_rx,
           (unsigned long) xs.res_tx, (unsigned long) xs.err_tx,
           (unsigned long) xs.cmd_ignored, (unsigned) xcp_last_error());
    printf("daq    : frames=%lu samples=%lu dropped=%lu overload=%lu mem_denied=%lu\r\n",
           (unsigned long) xs.daq_frames, (unsigned long) xs.daq_samples,
           (unsigned long) xs.daq_dropped, (unsigned long) xs.daq_overload,
           (unsigned long) xs.mem_denied);
    printf("can    : busoff=%lu rx_overflow=%lu tx_fifo_full=%lu\r\n",
           (unsigned long) busoff, (unsigned long) rxov, (unsigned long) txfull);
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
    tm_stat();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, dump XCP/DAQ/CAN statistics);

static void cmd_selftest(int argc, char *argv[])
{
    uint32_t ms = 800u;

    if (argc >= 2) {
        (void) bsp_parse_u32(argv[1], &ms);
        if ((ms < 100u) || (ms > 20000u)) {
            ms = 800u;
        }
    }
    (void) tm_selftest(ms);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 selftest, cmd_selftest, board-to-board XCP self test: selftest [daq_ms]);

static void cmd_daq(int argc, char *argv[])
{
    uint32_t ms = 1000u;

    if (argc >= 2) {
        (void) bsp_parse_u32(argv[1], &ms);
        if ((ms < 100u) || (ms > 20000u)) {
            ms = 1000u;
        }
    }
    (void) tm_daq_run(ms);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 daq, cmd_daq, DAQ measurement only: daq [ms]);

static void cmd_prot(int argc, char *argv[])
{
    static const uint8_t seed[4] = {0x11u, 0x22u, 0x33u, 0x44u};
    uint32_t on = 0u;

    if (argc >= 2) {
        (void) bsp_parse_u32(argv[1], &on);
    }
    if (on != 0u) {
        xcp_set_seed(seed, 4u);
        printf("资源保护：开（seed = 11 22 33 44，key = seed ^ 0xA5）\r\n");
    } else {
        xcp_set_seed(NULL, 0u);
        printf("资源保护：关（GET_SEED 返回长度 0）\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 prot, cmd_prot, enable/disable seed&key protection: prot <0|1>);

/* ---------------- Bench B 网关（CAN1 <-> 主机 vcan0） ---------------- */
static bool s_gw_on;

static int gw_start(void)
{
    if (s_gw_on) {
        return 0;
    }
    int rc = app_lwip_init(NULL);
    printf("[gw] app_lwip_init rc=%d\r\n", rc);
    app_can_gw_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.local_ip = NULL;            /* bind 0.0.0.0 */
    cfg.peer_ip = NULL;             /* 从主机第一个包学习 */
    cfg.peer_port = 0;
    cfg.bus_speed = APP_CAN_SPEED_CLASSIC_500K;
    cfg.max_frames_per_pkt = APP_CAN_GW_MAX_FRAMES_PER_PKT;
    cfg.ch_count = 1;
    cfg.ch[0].can_ch = 1u;          /* CAN1 = 网关通道 */
    cfg.ch[0].udp_port = 4000u;
    cfg.ch[0].enabled = true;
    rc = app_can_gw_init(&cfg);
    printf("[gw] app_can_gw_init rc=%d port=%u peer=%s\r\n", rc,
           (unsigned) app_can_gw_port(1), app_can_gw_peer_str());
    if (rc != 0) {
        return rc;
    }
    app_can_gw_set_forward(true);
    s_gw_on = true;
    printf("[gw] 转发已开启：主机 vcan0 <-> UDP 4000 <-> CAN1（CAN0 上跑 XCP 从站 0x555/0x556）\r\n");
    printf("GW ON %s\r\n", "PASS");
    return 0;
}

static void cmd_gw(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: gw on|off|stat\r\n");
        return;
    }
    if (strcmp(argv[1], "on") == 0) {
        (void) gw_start();
    } else if (strcmp(argv[1], "off") == 0) {
        app_can_gw_set_forward(false);
        s_gw_on = false;
        printf("[gw] 转发已关闭（重新 tm_init 恢复板内主机）\r\n");
        tm_init(APP_CAN_MASTER);
    } else if (strcmp(argv[1], "stat") == 0) {
        app_can_gw_print();
    } else {
        printf("usage: gw on|off|stat\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gw, cmd_gw, cannelloni gateway control: gw on|off|stat);

static void cmd_clr(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    xcp_reset_stats();
    xcp_can_reset_stats();
    s_ev_cycles = 0u;
    s_ev_cnt = 0u;
    printf("stats cleared\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 clr, cmd_clr, clear statistics);

/* ---------------- main ---------------- */

int main(void)
{
    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000u;
    printf("\r\n[boot] xcp_can_daq：XCP on CAN 从站 + DAQ 测量\r\n");
    printf("[cfg ] cpu=%lu MHz\r\n", (unsigned long) s_cpu_mhz);

    board_can_init_pins();
    board_can_leds_init();

    /* 从站 CAN0 + 板内主机 CAN1 */
    printf("[init] CAN0 slave ...\r\n");
    int rc = xcp_can_init(APP_CAN_SLAVE, 0x555u, 0x556u, false);
    if (rc != 0) {
        printf("[FAIL] xcp_can_init(CAN0) rc=%d\r\n", rc);
    }
    printf("[init] CAN1 master ...\r\n");
    tm_init(APP_CAN_MASTER);

    xcp_set_transport(&xcp_can_ops);
    if (xcp_init(&s_xcp_cfg) != 0) {
        printf("[FAIL] xcp_init\r\n");
    }

    printf("[init] ADC ...\r\n");
    if (!adc_setup()) {
        printf("[WARN] ADC16 初始化失败，g_adc_raw 恒 0\r\n");
    }
    printf("[init] GPTMR events ...\r\n");
    tmr_setup();
    idle_baseline();
    printf("[init] 事件通道就绪（EVT0=1 kHz, EVT1=100 Hz），ADC0.IN%u\r\n", (unsigned) APP_ADC_CH);

    /* 上电自证：板内主机跑一遍完整 XCP 流程 */
    (void) tm_selftest(600u);

    while (1) {
        bsp_shell_poll();
        xcp_poll();             /* 1) XCP：先发 DAQ 帧，再收 CTO */
        if (s_gw_on) {
            app_lwip_poll();    /* 2) 网关：收 UDP -> CAN1，再 CAN1 -> UDP */
            app_can_gw_poll();
        }
        adc_service();          /* 3) 应用 */
        s_load_cnt++;
        load_service();
    }
    return 0;
}
