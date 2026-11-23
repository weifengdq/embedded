/*
 * canopen_pdo - CANopen PDO 周期通信实测（SYNC 10 ms / TPDO1 延迟与抖动 / RPDO1 控制）
 *
 * 硬件: hpm5e31_1，CAN0(PD00/PD01) <-> CAN1(PD05/PD04) 同一段总线。
 *   - CAN0: CANopen 从站节点 1，500 kbit/s；TPDO1(0x181) 映射 0x2101(ADC16 IN01 采样, u16)
 *           + 0x2102(PDO 计数, u32)，transmission type=1（每个 SYNC 发送）；
 *           TPDO2(0x281) 映射 0x2102 + 0x2104，事件型 1000 ms；
 *           RPDO1(0x201) 映射 0x2100（LED/GPIO 状态）。
 *   - CAN1: components/app_canopen 最小主站，按 --period 发 SYNC，收 TPDO1 并统计
 *           端到端延迟（SYNC 发出 -> TPDO 收到，同一 MCU 的 mcycle 时基）、周期抖动、丢帧。
 *
 * 命令: run [period_ms] [secs] / stat / pdo
 * 上电自动 run 10 10（10 ms × 10 s）。
 *
 * 说明：Bench B（上位机 + cannelloni）用 host/canopen/co_pdo_bench.py 测同一组对象。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp.h"
#include "canopen_node.h"
#include "co_master.h"
#include "CANopen.h"
#include "OD.h"
#include "hpm_adc16_drv.h"
#include "hpm_csr_drv.h"
#include "ext/board_can.h"

#define DUT_CAN   BOARD_CAN0_BASE
#define TEST_CAN  BOARD_CAN1_BASE
#define DUT_NODE  1U
#define TPDO1_ID  (0x180U + DUT_NODE)
#define TPDO2_ID  (0x280U + DUT_NODE)
#define RPDO1_ID  (0x200U + DUT_NODE)

#define PDO_ADC_BASE HPM_ADC0
#define PDO_ADC_CH   1U /* PF26 / ADC0_IN01 */
#define LAT_MAX      4096U

static co_master_t s_master;
static uint32_t s_pass;
static uint32_t s_fail;
static uint32_t s_sync_sent;
static uint32_t s_tpdo1_rx;
static uint32_t s_tpdo2_rx;
static uint32_t s_adc_raw;
static uint32_t s_adc_err;

/* ------------------------------ ADC16 ------------------------------ */
static int adc_app_init(void)
{
    adc16_config_t cfg;
    adc16_channel_config_t ch_cfg;
    uint32_t div = 1U;
    uint32_t clk;

    board_init_adc16_pins();
    clk = board_init_adc_clock(PDO_ADC_BASE, true);
    if (adc16_calc_clock_divider(clk, ADC16_SOC_CONV_CLK_FREQ_MAX, &div) != status_success) {
        return -1;
    }
    adc16_get_default_config(&cfg);
    cfg.res = adc16_res_16_bits;
    cfg.conv_mode = adc16_conv_mode_oneshot;
    cfg.adc_clk_div = div;
#if !defined(HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB) || !HPM_IP_FEATURE_ADC16_FORCE_SYNC_AHB
    cfg.sel_sync_ahb = (clk_adc_src_ahb0 == clock_get_source(BOARD_APP_ADC16_CLK_NAME)) ? true : false;
#endif
    if (adc16_init(PDO_ADC_BASE, &cfg) != status_success) {
        return -2;
    }
    adc16_get_channel_default_config(&ch_cfg);
    ch_cfg.ch = PDO_ADC_CH;
    ch_cfg.sample_cycle = 20U;
    if (adc16_init_channel(PDO_ADC_BASE, &ch_cfg) != status_success) {
        return -3;
    }
#if defined(ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT) && ADC_SOC_BUSMODE_ENABLE_CTRL_SUPPORT
    adc16_enable_oneshot_mode(PDO_ADC_BASE);
#endif
    adc16_set_blocking_read(PDO_ADC_BASE);
    return 0;
}

static uint16_t adc_app_read(void)
{
    uint16_t raw = 0U;
    for (int i = 0; i < 8; i++) {
        if (adc16_get_oneshot_result(PDO_ADC_BASE, PDO_ADC_CH, &raw) == status_success) {
            return raw;
        }
    }
    s_adc_err++;
    return 0U;
}

/* ------------------------------ 应用逻辑 ------------------------------ */
static uint32_t s_emcy_trig_last;

static void app_update(void)
{
    static uint32_t last_adc_ms;
    uint32_t now = bsp_uptime_ms();

    if ((now - last_adc_ms) >= 10U) {
        last_adc_ms = now;
        s_adc_raw = adc_app_read();
        OD_RAM.x2101_adcSample = (uint16_t) s_adc_raw;
    }
    OD_RAM.x2102_pdoCounter++;
    OD_RAM.x2104_syncCounter = co_node_sync_counter();
    if (OD_RAM.x2106_emcyTrigger != s_emcy_trig_last) {
        s_emcy_trig_last = OD_RAM.x2106_emcyTrigger;
        if (s_emcy_trig_last != 0U) {
            co_node_emcy_report(CO_EM_MANUFACTURER_START, (uint16_t) (s_emcy_trig_last & 0xFFFFU), s_emcy_trig_last);
            OD_RAM.x2107_emcyCount++;
        } else {
            co_node_emcy_reset(CO_EM_MANUFACTURER_START, 0U);
        }
    }
    board_can_led_write(0U, true, (OD_RAM.x2100_ledState & 0x1U) ? true : false);
    board_can_led_write(0U, false, (OD_RAM.x2100_ledState & 0x2U) ? true : false);
}

static void app_service(void)
{
    co_node_reset_t rst = co_node_poll();
    if (rst == CO_NODE_RESET_COMM) {
        (void) co_node_reset_comm();
    } else if (rst == CO_NODE_RESET_APP) {
        bsp_reboot();
    }
    app_update();
}

static void check(const char *name, int ok, const char *detail)
{
    if (ok) {
        s_pass++;
    } else {
        s_fail++;
    }
    printf("  %-38s %s  %s\r\n", name, ok ? "PASS" : "FAIL", detail ? detail : "");
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *) a;
    uint32_t y = *(const uint32_t *) b;
    return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

/* SYNC 周期 -> TPDO1 端到端统计 */
static void bench_sync_tpdo(uint32_t period_ms, uint32_t secs)
{
    static uint32_t lat[LAT_MAX];
    static uint32_t per[LAT_MAX];
    uint32_t n = (period_ms == 0U) ? 1U : (secs * 1000U / period_ms);
    uint32_t got = 0U;
    uint32_t np = 0U;
    uint32_t i;
    uint64_t t_prev = 0U;
    uint64_t t_first = 0U;
    uint32_t lat_min = 0xFFFFFFFFU, lat_max = 0U, lat_sum = 0U;
    uint32_t per_min = 0xFFFFFFFFU, per_max = 0U, per_sum = 0U;
    uint32_t lost = 0U;

    if (n > LAT_MAX) {
        n = LAT_MAX;
    }
    printf("[bench] start: SYNC %u ms x %u s -> %u 帧\r\n", (unsigned) period_ms, (unsigned) secs, (unsigned) n);
    s_sync_sent = 0U;
    s_tpdo1_rx = 0U;
    t_first = hpm_csr_get_core_mcycle();
    for (i = 0U; i < n; i++) {
        co_master_frame_t f;
        uint64_t t0;

        t0 = hpm_csr_get_core_mcycle();
        if (co_master_sync(&s_master) != 0) {
            lost++;
        }
        s_sync_sent++;
        if (co_master_wait_id(&s_master, TPDO1_ID, period_ms + 20U, &f) != 0) {
            lost++;
        } else {
            uint32_t l = f.ts_us - (uint32_t) (t0 / (uint64_t) s_master.cpu_mhz);
            if (got < LAT_MAX) {
                lat[got] = l;
                got++;
                s_tpdo1_rx++;
            }
            if (l < lat_min) {
                lat_min = l;
            }
            if (l > lat_max) {
                lat_max = l;
            }
            lat_sum += l;
            if (t_prev != 0U) {
                uint32_t p = (uint32_t) ((t0 - t_prev) / (uint64_t) s_master.cpu_mhz);
                if (p < per_min) {
                    per_min = p;
                }
                if (p > per_max) {
                    per_max = p;
                }
                per_sum += p;
                if (np < LAT_MAX) {
                    per[np] = p;
                    np++;
                }
            }
            t_prev = t0;
        }
        /* 周期补齐 */
        while (((hpm_csr_get_core_mcycle() - t0) / (uint64_t) s_master.cpu_mhz) < period_ms * 1000U) {
            app_service();
        }
    }
    {
        uint32_t total_us = (uint32_t) ((hpm_csr_get_core_mcycle() - t_first) / (uint64_t) s_master.cpu_mhz);
        uint32_t avg_lat = (got != 0U) ? (lat_sum / got) : 0U;
        uint32_t avg_per = ((got > 1U) ? (per_sum / (got - 1U)) : 0U);
        uint32_t p99 = 0U;
        uint32_t jit_max = 0U;
        float jit_std = 0.0f;
        char buf[160];

        if (got > 1U) {
            qsort(lat, got, sizeof(lat[0]), cmp_u32);
            p99 = lat[(got * 99U) / 100U];
            {
                float acc = 0.0f;
                uint32_t k;
                for (k = 0U; k < np; k++) {
                    float d = (float) ((int32_t) per[k] - (int32_t) avg_per);
                    acc += d * d;
                }
                jit_std = (np != 0U) ? (acc / (float) np) : 0.0f;
                {
                    float r = jit_std * 0.5f + 0.5f;
                    for (int it = 0; it < 20; it++) {
                        r = 0.5f * (r + jit_std / r);
                    }
                    jit_std = (jit_std > 0.0f) ? r : 0.0f;
                }
            }
            /* 周期最大偏差 */
            jit_max = (per_max > period_ms * 1000U) ? (per_max - period_ms * 1000U)
                                                     : (period_ms * 1000U - per_min);
        }
        printf("  统计: SYNC=%u TPDO=%u 丢帧=%u\r\n", (unsigned) s_sync_sent, (unsigned) got, (unsigned) lost);
        snprintf(buf, sizeof(buf), "SYNC=%u TPDO=%u 丢帧=%u 总时长=%u us", (unsigned) s_sync_sent, (unsigned) got,
                 (unsigned) lost, (unsigned) total_us);
        check("TPDO1 每 SYNC 一帧（0 丢帧）", (lost == 0U) && (got == s_sync_sent), buf);

        printf("  指标                    数值\r\n");
        printf("  ----------------------- --------------------------\r\n");
        printf("  SYNC 帧数               %u\r\n", (unsigned) s_sync_sent);
        printf("  TPDO1 帧数              %u\r\n", (unsigned) got);
        printf("  丢帧                    %u (%.2f %%)\r\n", (unsigned) lost,
               (s_sync_sent != 0U) ? (100.0 * (double) lost / (double) s_sync_sent) : 0.0);
        printf("  端到端延迟 min/avg/p99/max  %u / %u / %u / %u us\r\n", (unsigned) lat_min, (unsigned) avg_lat,
               (unsigned) p99, (unsigned) lat_max);
        printf("  TPDO 周期 min/avg/max   %u / %u / %u us\r\n", (unsigned) per_min, (unsigned) avg_per,
               (unsigned) per_max);
        printf("  周期抖动 sigma / maxdev  %.3f / %u us\r\n", (double) jit_std, (unsigned) jit_max);

        snprintf(buf, sizeof(buf), "avg=%u us p99=%u us", (unsigned) avg_lat, (unsigned) p99);
        check("TPDO1 平均延迟 < 10 ms", avg_lat < 10000U, buf);
        snprintf(buf, sizeof(buf), "sigma=%.3f us maxdev=%u us", (double) jit_std, (unsigned) jit_max);
        check("TPDO1 周期抖动 sigma < 1000 us", jit_std < 1000.0f, buf);
    }
}

static void test_tpdo2(void)
{
    char buf[96];
    uint32_t n = 0U;
    uint32_t deadline = bsp_uptime_ms() + 5000U;

    s_tpdo2_rx = 0U;
    while ((int32_t) (bsp_uptime_ms() - deadline) < 0) {
        co_master_frame_t f;
        app_service();
        while (co_master_pop(&s_master, &f)) {
            if (f.id == TPDO2_ID) {
                s_tpdo2_rx++;
                n++;
            }
        }
    }
    snprintf(buf, sizeof(buf), "5 s 收到 %u 帧（事件型 1000 ms，期望 3~7）", (unsigned) n);
    check("TPDO2 事件型 1000 ms", n >= 3U && n <= 7U, buf);
}

static void test_rpdo1(void)
{
    char buf[128];
    uint32_t v = 0U;
    int ok = 1;
    uint8_t d[2];

    d[0] = 0x01U;
    d[1] = 0x00U;
    (void) co_master_send(&s_master, RPDO1_ID, d, 2U);
    co_master_delay_ms(&s_master, 30U);
    if (co_master_sdo_read(&s_master, DUT_NODE, 0x2100U, 0U, &v, 200U) != 0 || v != 0x0001U) {
        ok = 0;
    }
    d[0] = 0x00U;
    (void) co_master_send(&s_master, RPDO1_ID, d, 2U);
    co_master_delay_ms(&s_master, 30U);
    snprintf(buf, sizeof(buf), "写 0x0001 -> 0x2100=0x%04X", (unsigned) v);
    check("RPDO1(0x201) -> 0x2100 回环", ok, buf);
}

static void run_bench(uint32_t period_ms, uint32_t secs)
{
    s_pass = 0U;
    s_fail = 0U;
    printf("\r\n=== CANopen PDO bench: SYNC %u ms, %u s (CAN0 DUT <- CAN1 master) ===\r\n",
           (unsigned) period_ms, (unsigned) secs);
    /* 配置 TPDO1 为同步型（1），TPDO2 事件型 1000 ms；与 OD 默认值一致，SDO 写一遍验证可配 */
    (void) co_master_sdo_write(&s_master, DUT_NODE, 0x1800U, 2U, 1U, 1U, 200U);
    (void) co_master_sdo_write(&s_master, DUT_NODE, 0x1801U, 2U, 254U, 1U, 200U);
    (void) co_master_sdo_write(&s_master, DUT_NODE, 0x1801U, 5U, 1000U, 2U, 200U);
    /* 0x1019 保持 0：SYNC 必须 DLC=0（CiA 301：计数器模式才允许 1 字节 SYNC） */
    (void) co_master_sdo_write(&s_master, DUT_NODE, 0x1019U, 0U, 0U, 1U, 200U);
    bench_sync_tpdo(period_ms, secs);
    test_tpdo2();
    test_rpdo1();
    printf("=== RESULT: %u PASS / %u FAIL ===\r\n", (unsigned) s_pass, (unsigned) s_fail);
    printf("  ADC16 IN%02u raw=%u (0=悬空/0V，PF26 排针可接 0~3.3V)\r\n", (unsigned) PDO_ADC_CH,
           (unsigned) s_adc_raw);
}

static void cmd_run(int argc, char *argv[])
{
    uint32_t period = 10U;
    uint32_t secs = 10U;
    if (argc >= 2) {
        if (bsp_parse_u32(argv[1], &period) || period < 1U || period > 1000U) {
            printf("usage: run [period_ms 1..1000] [secs]\r\n");
            return;
        }
    }
    if (argc >= 3) {
        if (bsp_parse_u32(argv[2], &secs) || secs < 1U || secs > 60U) {
            printf("usage: run [period_ms] [secs 1..60]\r\n");
            return;
        }
    }
    run_bench(period, secs);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 run, cmd_run, run PDO bench: run [period_ms] [secs]);


/* U-33-10 定位：先读回 0x1801 通信参数，再同时统计 0x280 / 0x281 两路帧，
 * 区分"CAN-ID 不匹配"与"事件定时器根本没发"两种情况。 */
static void cmd_t2(int argc, char *argv[])
{
    uint32_t secs = 5U;
    uint32_t v_cob = 0U, v_type = 0U, v_timer = 0U, v_cob1 = 0U;
    uint32_t n280 = 0U, n281 = 0U, n_total = 0U;
    uint32_t deadline;

    if (argc >= 2) {
        (void) bsp_parse_u32(argv[1], &secs);
    }
    if ((secs == 0U) || (secs > 30U)) {
        secs = 5U;
    }
    (void) co_master_sdo_read(&s_master, DUT_NODE, 0x1801U, 1U, &v_cob, 300U);
    (void) co_master_sdo_read(&s_master, DUT_NODE, 0x1801U, 2U, &v_type, 300U);
    (void) co_master_sdo_read(&s_master, DUT_NODE, 0x1801U, 5U, &v_timer, 300U);
    (void) co_master_sdo_read(&s_master, DUT_NODE, 0x1800U, 1U, &v_cob1, 300U);
    printf("\r\n[t2] OD: 0x1800:1=0x%08lX | 0x1801:1=0x%08lX :2(type)=%lu :5(timer)=%lu ms\r\n",
           (unsigned long) v_cob1, (unsigned long) v_cob, (unsigned long) v_type,
           (unsigned long) v_timer);

    deadline = bsp_uptime_ms() + secs * 1000U;
    while ((int32_t) (bsp_uptime_ms() - deadline) < 0) {
        co_master_frame_t f;
        app_service();
        while (co_master_pop(&s_master, &f)) {
            n_total++;
            if (f.id == 0x280U) { n280++; }
            if (f.id == 0x281U) { n281++; }
        }
    }
    printf("[t2] %lu s: total=%lu id=0x280:%lu id=0x281:%lu\r\n", (unsigned long) secs,
           (unsigned long) n_total, (unsigned long) n280, (unsigned long) n281);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 t2, cmd_t2, TPDO2 event-timer diagnosis: t2 [secs]);

static void cmd_stat(void)
{
    co_node_stats_t st;
    co_node_get_stats(&st);
    printf("nmt=%u rx=%u tx=%u retry=%u heap=%uB canerr=0x%04X\r\n", (unsigned) st.nmt_state,
           (unsigned) st.rx_frames, (unsigned) st.tx_frames, (unsigned) st.tx_retry, (unsigned) st.heap_bytes,
           (unsigned) st.can_error_status);
    printf("od: 0x2100=0x%04X 0x2101=%u(adc) 0x2102=%u 0x2104=%u sync=%u\r\n",
           (unsigned) OD_RAM.x2100_ledState, (unsigned) OD_RAM.x2101_adcSample,
           (unsigned) OD_RAM.x2102_pdoCounter, (unsigned) OD_RAM.x2104_syncCounter,
           (unsigned) co_node_sync_counter());
    printf("bench: sync_sent=%u tpdo1_rx=%u tpdo2_rx=%u\r\n", (unsigned) s_sync_sent, (unsigned) s_tpdo1_rx,
           (unsigned) s_tpdo2_rx);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, PDO bench statistics);

static void cmd_pdo(void)
{
    printf("TPDO1 0x%03X: 0x2101 adc=%u, 0x2102 counter=%u\r\n", (unsigned) TPDO1_ID,
           (unsigned) OD_RAM.x2101_adcSample, (unsigned) OD_RAM.x2102_pdoCounter);
    printf("TPDO2 0x%03X: 0x2102 counter=%u, 0x2104 sync=%u\r\n", (unsigned) TPDO2_ID,
           (unsigned) OD_RAM.x2102_pdoCounter, (unsigned) OD_RAM.x2104_syncCounter);
    printf("RPDO1 0x%03X: 0x2100 led=0x%04X\r\n", (unsigned) RPDO1_ID, (unsigned) OD_RAM.x2100_ledState);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 pdo, cmd_pdo, show PDO mapped values);

int main(void)
{
    co_node_cfg_t cfg;
    int r;
    int ar;

    board_init();
    bsp_init();
    board_can_leds_init();

    printf("\r\n[app] canopen_pdo (CANopenNode v%d.%d, node %u, SYNC PDO bench)\r\n", CO_VERSION_MAJOR,
           CO_VERSION_MINOR, (unsigned) DUT_NODE);
    ar = adc_app_init();
    printf("[app] ADC16 init=%d (IN%02u = PF26)\r\n", ar, (unsigned) PDO_ADC_CH);
    memset(&cfg, 0, sizeof(cfg));
    cfg.can_base = DUT_CAN;
    cfg.node_id = DUT_NODE;
    cfg.bitrate_kbps = 500U;
    cfg.heartbeat_ms = 1000U;
    cfg.nmt_start_operational = 1U;
    r = co_node_init(&cfg);
    printf("[app] co_node_init=%d\r\n", r);
    r = co_master_init(&s_master, TEST_CAN, 500U);
    printf("[app] co_master_init=%d (CAN1 master)\r\n", r);
    co_master_set_node(&s_master, DUT_NODE);
    co_master_set_service(&s_master, app_service);
    printf("[app] canopen_pdo ready (shell: run [period_ms] [secs] | stat | pdo)\r\n");

    run_bench(10U, 5U);

    while (1) {
        bsp_shell_poll();
        app_service();
        co_master_poll(&s_master);
    }
}
