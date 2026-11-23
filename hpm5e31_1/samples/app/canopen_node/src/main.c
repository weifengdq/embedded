/*
 * canopen_node - CANopen (CiA 301) 从站最小闭环
 *
 * 硬件: hpm5e31_1。CAN0(PD00/PD01) <-> CAN1(PD05/PD04) 同一段总线（板上 2x120R）。
 *       - CAN0: CANopenNode v4.0 从站，节点号 1，500 kbit/s，心跳 1 s，自动进 Operational
 *       - CAN1: components/app_canopen 的最小主站（NMT / SDO client / SYNC 生产者 / 帧监视）
 * 这样单块板即可完成 Bench A 闭环（无需上位机 + cannelloni 网关）。
 *
 * 命令: run / stat / nmt <start|stop|preop|reset|resetapp> / emcy / mon
 * 上电自动跑 run：NMT 启停、SDO expedited 读写、心跳、EMCY 注入，输出 PASS/FAIL 表。
 */
#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include "canopen_node.h"
#include "co_master.h"
#include "CANopen.h"
#include "OD.h"
#include "ext/board_can.h"
#include "app_lwip.h"
#include "app_can_gw.h"

#define DUT_CAN  BOARD_CAN0_BASE
#define TEST_CAN BOARD_CAN1_BASE
#define DUT_NODE 1U

static co_master_t s_master;
static uint32_t s_pass;
static uint32_t s_fail;
static uint32_t s_tpdo1_rx;

static void check(const char *name, int ok, const char *detail)
{
    if (ok) {
        s_pass++;
    } else {
        s_fail++;
    }
    printf("  %-36s %s  %s\r\n", name, ok ? "PASS" : "FAIL", detail ? detail : "");
}

/* ---------------- 从站侧应用逻辑 ---------------- */
static uint32_t s_emcy_trig_last;

static void app_update(void)
{
    /* 主机可写 0x2106 触发 EMCY（Bench B 测试钩子）；写 0 撤销 */
    if (OD_RAM.x2106_emcyTrigger != s_emcy_trig_last) {
        s_emcy_trig_last = OD_RAM.x2106_emcyTrigger;
        if (s_emcy_trig_last != 0U) {
            co_node_emcy_report(CO_EM_MANUFACTURER_START, (uint16_t) (s_emcy_trig_last & 0xFFFFU), s_emcy_trig_last);
            OD_RAM.x2107_emcyCount++;
        } else {
            co_node_emcy_reset(CO_EM_MANUFACTURER_START, 0U);
        }
    }
    OD_RAM.x2102_pdoCounter++;                        /* TPDO 计数值，主站侧可观察单调递增 */
    OD_RAM.x2104_syncCounter = co_node_sync_counter();
    /* 0x2100 低 2 位控制 CAN0 指示灯（PF26 红 / PF18 白，active high） */
    board_can_led_write(0U, true, (OD_RAM.x2100_ledState & 0x1U) ? true : false);
    board_can_led_write(0U, false, (OD_RAM.x2100_ledState & 0x2U) ? true : false);
}

/* 主站等待期间的服务：驱动同 MCU 的从站 + 处理 NMT 复位请求 */
static void app_service(void)
{
    co_node_reset_t rst = co_node_poll();
    if (rst == CO_NODE_RESET_COMM) {
        printf("[app] NMT reset communication\r\n");
        (void) co_node_reset_comm();
    } else if (rst == CO_NODE_RESET_APP) {
        printf("[app] NMT reset application -> reboot\r\n");
        bsp_reboot();
    }
}

/* ---------------- 测试项 ---------------- */
static void test_nmt(void)
{
    char buf[96];
    int r;

    printf("[test] NMT 状态切换\r\n");
    r = co_master_nmt(&s_master, DUT_NODE, CO_MASTER_NMT_STOP);
    co_master_delay_ms(&s_master, 30);
    snprintf(buf, sizeof(buf), "state=%u (期望 4=Stopped)", (unsigned) co_node_nmt_state());
    check("NMT stop", r == 0 && co_node_nmt_state() == 4U, buf);

    r = co_master_nmt(&s_master, DUT_NODE, CO_MASTER_NMT_START);
    co_master_delay_ms(&s_master, 30);
    snprintf(buf, sizeof(buf), "state=%u (期望 5=Operational)", (unsigned) co_node_nmt_state());
    check("NMT start", r == 0 && co_node_nmt_state() == 5U, buf);

    r = co_master_nmt(&s_master, DUT_NODE, CO_MASTER_NMT_PREOP);
    co_master_delay_ms(&s_master, 30);
    snprintf(buf, sizeof(buf), "state=%u (期望 127=Pre-op)", (unsigned) co_node_nmt_state());
    check("NMT pre-operational", r == 0 && co_node_nmt_state() == 127U, buf);

    (void) co_master_nmt(&s_master, DUT_NODE, CO_MASTER_NMT_START);
    co_master_delay_ms(&s_master, 30);
}

static void test_sdo(void)
{
    char buf[128];
    uint32_t v = 0;
    uint8_t str[40];
    uint32_t str_len = 0;
    int r;

    printf("[test] SDO expedited 读写\r\n");
    r = co_master_sdo_read(&s_master, DUT_NODE, 0x1000U, 0U, &v, 200U);
    snprintf(buf, sizeof(buf), "0x1000=0x%08X (期望 0x00000191)", (unsigned) v);
    check("SDO read 0x1000 deviceType", r == 0 && v == 0x00000191U, buf);

    r = co_master_sdo_read(&s_master, DUT_NODE, 0x1017U, 0U, &v, 200U);
    snprintf(buf, sizeof(buf), "0x1017=%u ms (期望 1000)", (unsigned) v);
    check("SDO read 0x1017 heartbeat", r == 0 && v == 1000U, buf);

    r = co_master_sdo_write(&s_master, DUT_NODE, 0x1017U, 0U, 200U, 2U, 200U);
    v = 0;
    (void) co_master_sdo_read(&s_master, DUT_NODE, 0x1017U, 0U, &v, 200U);
    snprintf(buf, sizeof(buf), "写 200 -> 读回 %u", (unsigned) v);
    check("SDO write 0x1017 = 200", r == 0 && v == 200U, buf);

    r = co_master_sdo_write(&s_master, DUT_NODE, 0x1017U, 0U, 1000U, 2U, 200U);
    check("SDO write 0x1017 restore 1000", r == 0, "");

    r = co_master_sdo_read(&s_master, DUT_NODE, 0x1018U, 1U, &v, 200U);
    snprintf(buf, sizeof(buf), "vendorID=0x%08X", (unsigned) v);
    check("SDO read 0x1018:1 vendor-ID", r == 0 && v == 0x0000A531U, buf);

    /* 0x1008 是 VISIBLE_STRING（32 B），走分段上传 */
    r = co_master_sdo_upload(&s_master, DUT_NODE, 0x1008U, 0U, str, sizeof(str), &str_len, 300U);
    str[sizeof(str) - 1U] = 0U;
    snprintf(buf, sizeof(buf), "len=%u \"%s\"", (unsigned) str_len, (const char *) str);
    check("SDO segmented upload 0x1008", r == 0 && str_len > 8U && str[0] == 'H', buf);
}

static void test_heartbeat(void)
{
    char buf[96];
    uint32_t hb0;

    printf("[test] 心跳（生产者 1 s）\r\n");
    hb0 = s_master.hb_frames;
    co_master_delay_ms(&s_master, 2600U);
    snprintf(buf, sizeof(buf), "2.6 s 内收到 %u 帧（期望 2~3）", (unsigned) (s_master.hb_frames - hb0));
    check("heartbeat 0x701 @1s", (s_master.hb_frames - hb0) >= 2U && (s_master.hb_frames - hb0) <= 3U, buf);
}

static void test_emcy(void)
{
    char buf[112];
    uint32_t e0 = s_master.emcy_frames;

    printf("[test] EMCY 注入\r\n");
    co_node_emcy_report(CO_EM_MANUFACTURER_START, 0x1000U, 0x5A5A0001U);
    co_master_delay_ms(&s_master, 30U);
    snprintf(buf, sizeof(buf), "EMCY 计数 +%u, code=0x%02X%02X", (unsigned) (s_master.emcy_frames - e0),
             (unsigned) s_master.emcy_last[1], (unsigned) s_master.emcy_last[0]);
    check("EMCY 0x081 manufacturer", (s_master.emcy_frames - e0) == 1U
              && s_master.emcy_last[0] == 0x00U && s_master.emcy_last[1] == 0x10U, buf);
    co_node_emcy_reset(CO_EM_MANUFACTURER_START, 0x5A5A0001U);
    co_master_delay_ms(&s_master, 30U);
}

/* ---------------- Bench B：把 CAN1 交给 cannelloni 网关 ---------------- */
static bool s_gw_on;

static int gw_start(void)
{
    app_can_gw_cfg_t gw;
    int rc_ip;
    int rc_gw;

    co_master_disable(&s_master); /* 释放 MCAN1，随后由 app_can 重新初始化 */
    rc_ip = app_lwip_init(NULL);
    printf("[gw] lwip init rc=%d link=%s\r\n", rc_ip, app_lwip_link_up() ? "UP" : "DOWN");
    memset(&gw, 0, sizeof(gw));
    gw.local_ip = "192.168.2.100";
    gw.peer_ip = NULL; /* 从主机 cannelloni 的第一个包学习源 IP:port */
    gw.peer_port = 0U;
    gw.bus_speed = APP_CAN_SPEED_CLASSIC_500K;
    gw.max_frames_per_pkt = APP_CAN_GW_MAX_FRAMES_PER_PKT;
    gw.ch_count = 1;
    gw.ch[0].can_ch = 1U;
    gw.ch[0].udp_port = 4000U;
    gw.ch[0].enabled = true;
    rc_gw = app_can_gw_init(&gw);
    printf("[gw] CAN1 <-> UDP4000 rc=%d (host: cannelloni -I vcan0 -R 192.168.2.100 -r 4000 -l 4000)\r\n", rc_gw);
    if (rc_gw == 0) {
        s_gw_on = true;
        app_can_gw_set_forward(true);
        printf("[gw] forwarding ON, CAN0 node1 仍为 CANopen 从站\r\n");
    }
    return rc_gw;
}

static void cmd_gw(int argc, char *argv[])
{
    const char *arg = (argc >= 2) ? argv[1] : NULL;

    if ((arg != NULL) && (strcmp(arg, "on") == 0)) {
        (void) gw_start();
    } else if ((arg != NULL) && (strcmp(arg, "off") == 0)) {
        app_can_gw_set_forward(false);
        s_gw_on = false;
        printf("[gw] forwarding OFF（恢复 Bench A 需 reboot）\r\n");
    } else {
        printf("[gw] gw on|off   (Bench B: CAN1<->UDP4000<->vcan0)\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gw, cmd_gw, switch CAN1 to cannelloni gateway: gw on|off);

static void run_all(void)
{
    co_node_stats_t st;

    s_pass = 0U;
    s_fail = 0U;
    printf("\r\n=== CANopen node1 self test (CAN0 DUT <- CAN1 master) ===\r\n");
    test_nmt();
    test_sdo();
    test_heartbeat();
    test_emcy();
    co_node_get_stats(&st);
    printf("  stats: heap=%uB rx=%u tx=%u retry=%u loops=%u maxloop=%uus canerr=0x%04X\r\n",
           (unsigned) st.heap_bytes, (unsigned) st.rx_frames, (unsigned) st.tx_frames,
           (unsigned) st.tx_retry, (unsigned) st.pol_loops, (unsigned) st.max_cycle_us,
           (unsigned) st.can_error_status);
    printf("=== RESULT: %u PASS / %u FAIL ===\r\n", (unsigned) s_pass, (unsigned) s_fail);
}

/* ---------------- shell ---------------- */
static void cmd_stat(void)
{
    co_node_stats_t st;
    co_node_get_stats(&st);
    printf("nmt=%u heap=%uB rx=%u tx=%u retry=%u loops=%u maxloop=%uus canerr=0x%04X\r\n",
           (unsigned) st.nmt_state, (unsigned) st.heap_bytes, (unsigned) st.rx_frames,
           (unsigned) st.tx_frames, (unsigned) st.tx_retry, (unsigned) st.pol_loops,
           (unsigned) st.max_cycle_us, (unsigned) st.can_error_status);
    printf("OD: 0x2100=0x%04X 0x2101=%u 0x2102=%u 0x2103=%u 0x2104=%u hb=%u emcy=%u\r\n",
           (unsigned) OD_RAM.x2100_ledState, (unsigned) OD_RAM.x2101_adcSample,
           (unsigned) OD_RAM.x2102_pdoCounter, (unsigned) OD_RAM.x2103_loopCounter,
           (unsigned) OD_RAM.x2104_syncCounter, (unsigned) s_master.hb_frames, (unsigned) s_master.emcy_frames);
    co_master_mon_print(&s_master, 8U);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, CANopen node + master statistics);

static void cmd_run(void)
{
    run_all();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 run, cmd_run, run CANopen self test);

static void cmd_emcy(void)
{
    co_node_emcy_report(CO_EM_MANUFACTURER_START, 0x1000U, 0x5A5A0002U);
    printf("EMCY injected: error bit 0x30, code 0x1000\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 emcy, cmd_emcy, inject a manufacturer EMCY);

static void cmd_nmt(int argc, char *argv[])
{
    const char *s = (argc >= 2) ? argv[1] : NULL;
    uint8_t cmd = CO_MASTER_NMT_START;
    if (s != NULL) {
        if (strcmp(s, "start") == 0) {
            cmd = CO_MASTER_NMT_START;
        } else if (strcmp(s, "stop") == 0) {
            cmd = CO_MASTER_NMT_STOP;
        } else if (strcmp(s, "preop") == 0) {
            cmd = CO_MASTER_NMT_PREOP;
        } else if (strcmp(s, "reset") == 0) {
            cmd = CO_MASTER_NMT_RESET_COMM;
        } else if (strcmp(s, "resetapp") == 0) {
            cmd = CO_MASTER_NMT_RESET_APP;
        }
    }
    printf("NMT 0x%02X -> rc=%d\r\n", (unsigned) cmd, co_master_nmt(&s_master, DUT_NODE, cmd));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 nmt, cmd_nmt, NMT command: start|stop|preop|reset|resetapp);

static void cmd_mon(void)
{
    co_master_mon_print(&s_master, 0U);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mon, cmd_mon, dump CAN frames seen on CAN1);

/* ---------------- main ---------------- */
int main(void)
{
    co_node_cfg_t cfg;
    int r;

    board_init();
    bsp_init();
    board_can_leds_init();

    printf("\r\n[app] canopen_node (CANopenNode v%d.%d, node %u, 500k)\r\n",
           CO_VERSION_MAJOR, CO_VERSION_MINOR, (unsigned) DUT_NODE);
    memset(&cfg, 0, sizeof(cfg));
    cfg.can_base = DUT_CAN;
    cfg.node_id = DUT_NODE;
    cfg.bitrate_kbps = 500U;
    cfg.heartbeat_ms = 1000U;
    cfg.nmt_start_operational = 1U;
    r = co_node_init(&cfg);
    printf("[app] co_node_init=%d\r\n", r);
    if (r == 0) {
        co_node_stats_t st;
        co_node_get_stats(&st);
        printf("[app] CANopen objects heap=%u B\r\n", (unsigned) st.heap_bytes);
    }
    r = co_master_init(&s_master, TEST_CAN, 500U);
    printf("[app] co_master_init=%d (CAN1 master)\r\n", r);
    co_master_set_node(&s_master, DUT_NODE);
    co_master_set_service(&s_master, app_service);
    for (uint32_t i = 0U; i < sizeof(OD_RAM.x2001_vendorDomain1k); i++) {
        OD_RAM.x2001_vendorDomain1k[i] = (uint8_t) (i & 0xFFU);
    }
    for (uint32_t i = 0U; i < sizeof(OD_RAM.x2002_vendorDomain4k); i++) {
        OD_RAM.x2002_vendorDomain4k[i] = (uint8_t) ((i * 7U) & 0xFFU);
    }
    printf("[app] canopen_node ready\r\n");

    run_all();

    uint32_t last_tick = 0;
    while (1) {
        bsp_shell_poll();
        if (s_gw_on) {
            app_lwip_poll();
            app_can_gw_poll();
        }
        app_service();
        co_master_poll(&s_master);
        app_update();
        uint32_t now = bsp_uptime_ms();
        if ((now - last_tick) >= 1000U) {
            last_tick = now;
            OD_RAM.x2101_adcSample = (uint16_t) (now & 0x0FFFU);
        }
    }
}
