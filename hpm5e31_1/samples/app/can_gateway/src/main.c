/*
 * can_gateway - cannelloni v2 兼容 CAN<->UDP 网关 + CAN0 监视/回显节点
 *
 * 硬件（hpm5e31_1，无需外部 CAN 分析仪）:
 *   CAN0(PD00/PD01) <--板内总线--> CAN1(PD05/PD04) <--UDP 4000--> 主机 vcan0
 *   主机: enp6s0 192.168.2.1/24  <-->  板 ens0 192.168.2.100/24
 *
 * 功能:
 *   1) CAN0 = DUT 演示节点：周期 100 ms 发 0x321 心跳；收到 0x123 时把同样载荷以 0x321 回显；
 *      收到的帧在串口打印（quiet 可关）。
 *   2) CAN1 = 网关（cannelloni v2 / UDP 4000），CAN2/CAN3 可选映射到 4002/4003（gw full）。
 *
 * 主机侧（host/can_bridge/）:
 *   sudo ./vcan_setup.sh && ./build_cannelloni.sh && ./run_tunnel.sh
 *   candump vcan0                     # 看到板端 0x321 心跳
 *   cansend vcan0 123#1122334455667788 # 板端 CAN0 收到并回显，candump 看到 321#1122334455667788
 *
 * 串口命令:
 *   gw on|off|stat|full|lite|peer <ip> <port>
 *   echo on|off   hb on|off   send <id> <hex>   stat   loop   ip   quiet|verbose
 *
 * 结论行: [pass] ... / [fail] ...（hwrun 日志抓取用）
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bsp.h"
#include "ext/board_can.h"
#include "app_can.h"
#include "app_lwip.h"
#include "app_can_gw.h"

#define ID_ECHO_REQ (0x123U)
#define ID_ECHO_RES (0x321U)
#define HB_PERIOD_US (100000U)   /* 10 Hz 心跳 */

static app_can_t s_can0;
static bool s_echo = true;
static bool s_hb = true;
static bool s_verbose = true;
static bool s_gw_full;
static uint32_t s_hb_seq;
static uint32_t s_hb_sent;
static uint32_t s_echo_cnt;
static uint32_t s_rx_cnt;

/* ---------------- 网关配置 ---------------- */

static int gw_config(bool full)
{
    app_can_gw_cfg_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.local_ip = NULL;            /* bind 0.0.0.0 */
    cfg.peer_ip = NULL;             /* 从主机 cannelloni 的第一个包学习源 IP:port */
    cfg.peer_port = 0;
    cfg.bus_speed = APP_CAN_SPEED_CLASSIC_500K;
    cfg.max_frames_per_pkt = APP_CAN_GW_MAX_FRAMES_PER_PKT;
    cfg.ch_count = full ? 3 : 1;
    cfg.ch[0].can_ch = 1U;
    cfg.ch[0].udp_port = 4000U;
    cfg.ch[0].enabled = true;
    cfg.ch[1].can_ch = 2U;
    cfg.ch[1].udp_port = 4002U;
    cfg.ch[1].enabled = full;
    cfg.ch[2].can_ch = 3U;
    cfg.ch[2].udp_port = 4003U;
    cfg.ch[2].enabled = full;

    int rc = app_can_gw_init(&cfg);
    if (rc == 0) {
        s_gw_full = full;
        printf("[gw] mode=%s peer=%s\r\n", full ? "full(CAN1/2/3)" : "lite(CAN1)",
               app_can_gw_peer_str());
    } else {
        printf("[gw] init FAILED rc=%d\r\n", rc);
    }
    return rc;
}

/* ---------------- CAN0 演示节点 ---------------- */

static void can0_poll(void)
{
    app_can_frame_t f;
    uint32_t budget = 16U;

    while ((budget-- > 0U) && (app_can_recv(&s_can0, &f, 0U) == 0)) {
        s_rx_cnt++;
        if (s_verbose) {
            printf("[can0] rx id=0x%03lX%s len=%u data=%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                   (unsigned long) f.id, f.ext ? "x" : "", (unsigned) f.len,
                   f.data[0], f.data[1], f.data[2], f.data[3],
                   f.data[4], f.data[5], f.data[6], f.data[7]);
        }
        if (s_echo && !f.ext && (f.id == ID_ECHO_REQ)) {
            app_can_frame_t r = f;
            r.id = ID_ECHO_RES;
            r.ts_us = 0;
            if (app_can_send(&s_can0, &r, 0U) == 0) {
                s_echo_cnt++;
            }
        }
    }
}

static void hb_tick(void)
{
    static uint64_t next_us;
    uint64_t now;

    if (!s_hb) {
        return;
    }
    now = app_can_now_us();
    if (now < next_us) {
        return;
    }
    next_us = now + HB_PERIOD_US;
    {
        app_can_frame_t f;
        memset(&f, 0, sizeof(f));
        f.id = ID_ECHO_RES;
        f.len = 8U;
        f.data[0] = (uint8_t) s_hb_seq;
        f.data[1] = (uint8_t) (s_hb_seq >> 8);
        f.data[2] = (uint8_t) (s_hb_seq >> 16);
        f.data[3] = (uint8_t) (s_hb_seq >> 24);
        f.data[4] = 0xCAU;
        f.data[5] = 0xFEU;
        f.data[6] = 0xBAU;
        f.data[7] = 0xBEU;
        if (app_can_send(&s_can0, &f, 0U) == 0) {
            s_hb_seq++;
            s_hb_sent++;
        }
    }
}

/* ---------------- shell 命令 ---------------- */

static void winfo(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("sample: can_gateway  CAN0(PD00/01) <=> CAN1(PD05/04) <=> UDP4000 <=> host vcan0\r\n");
    printf("  can0: classic 500k/SP87.5%%  heart=0x321@10Hz  echo 0x123->0x321\r\n");
    printf("  gw  : cannelloni v2 (5B hdr + canfd_frame[]), chan=%s\r\n", s_gw_full ? "CAN1/2/3" : "CAN1");
    printf("  ip  : %s\r\n", ipaddr_ntoa(netif_ip_addr4(app_lwip_netif())));
    printf("  cmds: gw on|off|stat|full|lite|peer <ip> <port>; echo on|off; hb on|off; send <id> <hex>; stat; loop; ip\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, winfo, show gateway config);

static void wstat(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    app_can_dump(&s_can0, "can0");
    app_can_gw_print();
    printf("[app] hb=%lu echo=%lu rx0=%lu verbose=%u echo_on=%u hb_on=%u\r\n",
           (unsigned long) s_hb_sent, (unsigned long) s_echo_cnt, (unsigned long) s_rx_cnt,
           (unsigned) s_verbose, (unsigned) s_echo, (unsigned) s_hb);
    printf("[app] peer=%s link=%s\r\n", app_can_gw_peer_str(), app_lwip_link_up() ? "UP" : "DOWN");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, wstat, show CAN0 + gateway statistics);

static void wgw(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: gw on|off|stat|full|lite|peer <ip> <port>\r\n");
        return;
    }
    if (strcmp(argv[1], "on") == 0) {
        app_can_gw_set_forward(true);
        printf("[gw] forwarding ON\r\n");
    } else if (strcmp(argv[1], "off") == 0) {
        app_can_gw_set_forward(false);
        printf("[gw] forwarding OFF\r\n");
    } else if (strcmp(argv[1], "stat") == 0) {
        app_can_gw_print();
        printf("[gw] peer=%s fwd=%u\r\n", app_can_gw_peer_str(), app_can_gw_forwarding() ? 1U : 0U);
    } else if (strcmp(argv[1], "full") == 0) {
        (void) gw_config(true);
    } else if (strcmp(argv[1], "lite") == 0) {
        (void) gw_config(false);
    } else if ((strcmp(argv[1], "peer") == 0) && (argc >= 4)) {
        uint32_t port = 0;
        if (bsp_parse_u32(argv[3], &port) && (app_can_gw_set_peer(argv[2], (uint16_t) port) == 0)) {
            printf("[gw] peer set to %s\r\n", app_can_gw_peer_str());
        } else {
            printf("[gw] bad peer\r\n");
        }
    } else {
        printf("usage: gw on|off|stat|full|lite|peer <ip> <port>\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gw, wgw, gateway control: gw on|off|stat|full|lite|peer);

static void wecho(int argc, char *argv[])
{
    if (argc >= 2) {
        s_echo = (strcmp(argv[1], "on") == 0);
    }
    printf("[app] echo %s (0x%03X -> 0x%03X)\r\n", s_echo ? "ON" : "OFF",
           (unsigned) ID_ECHO_REQ, (unsigned) ID_ECHO_RES);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 echo, wecho, echo on|off);

static void whb(int argc, char *argv[])
{
    if (argc >= 2) {
        s_hb = (strcmp(argv[1], "on") == 0);
    }
    printf("[app] heartbeat 0x%03X %s\r\n", (unsigned) ID_ECHO_RES, s_hb ? "ON" : "OFF");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 hb, whb, heartbeat on|off);

static void wquiet(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    s_verbose = false;
    printf("[app] verbose OFF (no per-frame print)\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 quiet, wquiet, stop per-frame printing);

static void wverbose(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    s_verbose = true;
    printf("[app] verbose ON\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 verbose, wverbose, enable per-frame printing);

static uint8_t hexval(char c)
{
    if ((c >= '0') && (c <= '9')) {
        return (uint8_t) (c - '0');
    }
    if ((c >= 'a') && (c <= 'f')) {
        return (uint8_t) (c - 'a' + 10);
    }
    if ((c >= 'A') && (c <= 'F')) {
        return (uint8_t) (c - 'A' + 10);
    }
    return 0xFFU;
}

static void wsend(int argc, char *argv[])
{
    uint32_t id = 0;
    app_can_frame_t f;
    const char *hex;
    uint8_t n = 0;

    if (argc < 2) {
        printf("usage: send <id> [hexpayload]   e.g. send 123 1122334455667788\r\n");
        return;
    }
    if (!bsp_parse_u32(argv[1], &id)) {
        /* 允许 0x 前缀 */
        id = (uint32_t) strtoul(argv[1], NULL, 16);
    }
    memset(&f, 0, sizeof(f));
    f.id = id & 0x7FFU;
    if (argc >= 3) {
        hex = argv[2];
        while ((hex[0] != '\0') && (hex[1] != '\0') && (n < 64U)) {
            uint8_t hi = hexval(hex[0]);
            uint8_t lo = hexval(hex[1]);
            if ((hi == 0xFFU) || (lo == 0xFFU)) {
                break;
            }
            f.data[n++] = (uint8_t) ((hi << 4) | lo);
            hex += 2;
        }
        f.len = n;
    }
    if (app_can_send(&s_can0, &f, 1000U) == 0) {
        printf("[can0] tx id=0x%03lX len=%u OK\r\n", (unsigned long) f.id, (unsigned) f.len);
    } else {
        printf("[can0] tx FAILED (FIFO full / bus off?)\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 send, wsend, send a frame on CAN0: send <id> <hex>);

static void wloop(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    int rc = app_can_loopback_test(BOARD_CAN3_BASE, APP_CAN_SPEED_CLASSIC_500K, 0x55AU);
    printf("[loop] CAN3 internal loopback %s rc=%d (%s)\r\n", (rc == 0) ? "PASS" : "FAIL", rc,
           app_can_strerror(rc));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 loop, wloop, CAN3 internal loopback self test);

static void wip(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    app_lwip_print();
    printf("[ip] raw netif: ip=%08lX mask=%08lX gw=%08lX hwlen=%u mac=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
           (unsigned long) netif_ip4_addr(app_lwip_netif())->addr,
           (unsigned long) netif_ip4_netmask(app_lwip_netif())->addr,
           (unsigned long) netif_ip4_gw(app_lwip_netif())->addr,
           (unsigned) app_lwip_netif()->hwaddr_len,
           app_lwip_netif()->hwaddr[0], app_lwip_netif()->hwaddr[1], app_lwip_netif()->hwaddr[2],
           app_lwip_netif()->hwaddr[3], app_lwip_netif()->hwaddr[4], app_lwip_netif()->hwaddr[5]);
}

/* 现场重建 CAN 节点并打印每一步的诊断（上电输出常被串口抓取错过） */
static void wcantest(int argc, char *argv[])
{
    app_can_t t;
    app_can_cfg_t cfg = APP_CAN_CFG_DEFAULT;
    int rc;

    (void) argc;
    (void) argv;
    /* 逐项定位 mcan_init 失败点：A=默认配置 B=低位时序(经典500k) C=baudrate API */
    {
        mcan_config_t mc;
        hpm_stat_t st;
        uint32_t clk;

        board_init_can(HPM_MCAN0);
        clk = board_init_can_clock(HPM_MCAN0);

        mcan_get_default_config(HPM_MCAN0, &mc);
        mc.mode = mcan_mode_normal;
        mc.enable_canfd = false;
        mcan_deinit(HPM_MCAN0);
        st = mcan_init(HPM_MCAN0, &mc, clk);
        printf("[ct] A default-cfg classic st=%d\r\n", (int) st);

        mcan_get_default_config(HPM_MCAN0, &mc);
        mc.mode = mcan_mode_normal;
        mc.enable_canfd = false;
        mc.use_lowlevel_timing_setting = false;
        mc.baudrate = 500000U;
        mc.can20_samplepoint_min = 750U;
        mc.can20_samplepoint_max = 900U;
        mcan_deinit(HPM_MCAN0);
        st = mcan_init(HPM_MCAN0, &mc, clk);
        printf("[ct] C baudrate-api 500k st=%d\r\n", (int) st);

        mcan_get_default_config(HPM_MCAN0, &mc);
        mc.mode = mcan_mode_normal;
        mc.enable_canfd = false;
        mc.use_lowlevel_timing_setting = true;
        mc.can_timing.prescaler = 1U;
        mc.can_timing.num_seg1 = 139U;
        mc.can_timing.num_seg2 = 20U;
        mc.can_timing.num_sjw = 4U;
        mcan_deinit(HPM_MCAN0);
        st = mcan_init(HPM_MCAN0, &mc, clk);
        printf("[ct] B lowlevel classic(seg1=139) st=%d\r\n", (int) st);

        mcan_get_default_config(HPM_MCAN0, &mc);
        mc.mode = mcan_mode_normal;
        mc.enable_canfd = false;
        mc.use_lowlevel_timing_setting = true;
        mc.can_timing.prescaler = 10U;
        mc.can_timing.num_seg1 = 13U;
        mc.can_timing.num_seg2 = 2U;
        mc.can_timing.num_sjw = 1U;
        mcan_deinit(HPM_MCAN0);
        st = mcan_init(HPM_MCAN0, &mc, clk);
        printf("[ct] D lowlevel classic(presc=10,tq=16) st=%d\r\n", (int) st);
    }

    printf("[ct] clocks: can0=%lu can1=%lu can2=%lu can3=%lu Hz\r\n",
           (unsigned long) board_init_can_clock(HPM_MCAN0),
           (unsigned long) board_init_can_clock(HPM_MCAN1),
           (unsigned long) board_init_can_clock(HPM_MCAN2),
           (unsigned long) board_init_can_clock(HPM_MCAN3));

    cfg.speed = APP_CAN_SPEED_CLASSIC_500K;
    rc = app_can_init(&t, BOARD_CAN0_BASE, &cfg);
    printf("[ct] init CAN0 classic normal rc=%d\r\n", rc);
    if (rc == 0) {
        app_can_frame_t f;
        memset(&f, 0, sizeof(f));
        f.id = 0x777;
        f.len = 8;
        printf("[ct] send rc=%d\r\n", app_can_send(&t, &f, 1000));
    }

    cfg.loopback = true;
    rc = app_can_init(&t, BOARD_CAN3_BASE, &cfg);
    printf("[ct] init CAN3 classic loopback rc=%d\r\n", rc);

    cfg.speed = APP_CAN_SPEED_FD_1M_2M;
    cfg.loopback = true;
    rc = app_can_init(&t, BOARD_CAN3_BASE, &cfg);
    printf("[ct] init CAN3 FD loopback rc=%d\r\n", rc);

    /* 把 CAN0 恢复成正常回显节点 */
    cfg = (app_can_cfg_t) APP_CAN_CFG_DEFAULT;
    (void) app_can_init(&s_can0, BOARD_CAN0_BASE, &cfg);
    printf("[ct] CAN0 restored\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 cantest, wcantest, rebuild CAN nodes with step-by-step diagnostics);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ip, wip, show lwIP netif + PHY status);

/* 重新打印结论行：hwrun 抓串口时可能错过上电瞬间的输出，用 'pass' 命令重新自证 */
static int s_rc_can0 = -99;
static int s_rc_loop = -99;
static int s_rc_ip = -99;
static int s_rc_gw = -99;

static void wpass(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("[pass] CAN0=%s CAN3-loop=%s lwip=%s link=%s gw=CAN1/UDP4000 rc=%d\r\n",
           (s_rc_can0 == 0) ? "OK" : "FAIL", (s_rc_loop == 0) ? "PASS" : "FAIL",
           (s_rc_ip <= 0) ? "OK" : "LINK_DOWN", app_lwip_link_up() ? "UP" : "DOWN", s_rc_gw);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 pass, wpass, print PASS/FAIL summary again);

/* ---------------- main ---------------- */

int main(void)
{
    app_can_cfg_t c0 = APP_CAN_CFG_DEFAULT;
    int rc_can0;
    int rc_loop;
    int rc_ip;
    int rc_gw;

    bsp_init();
    printf("\r\n[boot] can_gateway: cannelloni v2 CAN<->UDP gateway + CAN0 demo node\r\n");
    printf("[boot] build %s %s\r\n", __DATE__, __TIME__);

    c0.speed = APP_CAN_SPEED_CLASSIC_500K;
    rc_can0 = app_can_init(&s_can0, BOARD_CAN0_BASE, &c0);
    s_rc_can0 = rc_can0;
    printf("[can0] init %s rc=%d (%s)\r\n", (rc_can0 == 0) ? "OK" : "FAIL", rc_can0,
           app_can_strerror(rc_can0));

    /* 先用 CAN3 做内部环回自证（不占总线、不影响 CAN0/CAN1 网关链路） */
    rc_loop = app_can_loopback_test(BOARD_CAN3_BASE, APP_CAN_SPEED_CLASSIC_500K, 0x55AU);
    s_rc_loop = rc_loop;
    printf("[loop] CAN3 loopback %s rc=%d (%s)\r\n", (rc_loop == 0) ? "PASS" : "FAIL", rc_loop,
           app_can_strerror(rc_loop));

    rc_ip = app_lwip_init(NULL);
    s_rc_ip = rc_ip;
    printf("[lwip] init rc=%d link=%s\r\n", rc_ip, app_lwip_link_up() ? "UP" : "DOWN");

    rc_gw = gw_config(false);
    s_rc_gw = rc_gw;

    printf("\r\n[pass] CAN0=%s CAN3-loop=%s lwip=%s link=%s gw=CAN1/UDP4000 rc=%d\r\n",
           (rc_can0 == 0) ? "OK" : "FAIL", (rc_loop == 0) ? "PASS" : "FAIL",
           (rc_ip <= 0) ? "OK" : "LINK_DOWN", app_lwip_link_up() ? "UP" : "DOWN", rc_gw);
    if ((rc_can0 != 0) || (rc_ip > 0) || (rc_gw != 0)) {
        printf("[fail] one or more stages failed (细节看上一行)\r\n");
    }
    printf("cmds: gw on|off|stat|full|lite|peer <ip> <port>; echo on|off; hb on|off; send <id> <hex>; stat; loop; ip; cantest; quiet\r\n");

    {
        uint64_t next_led = 0;
        uint64_t next_alive = 0;
        while (1) {
            bsp_shell_poll();
            app_lwip_poll();
            app_can_gw_poll();
            can0_poll();
            hb_tick();
            /* 10 Hz 刷新 CAN0 状态灯（红=TX，白=RX），无 printf */
            if (app_can_now_us() >= next_led) {
                next_led = app_can_now_us() + 100000U;
                board_can_led_write(0U, true, (s_hb_sent & 1U) != 0U);
                board_can_led_write(0U, false, (s_rx_cnt & 1U) != 0U);
            }
            /* 5 s 一行存活/计数（长测证据；无输出即板子挂了） */
            if ((next_alive == 0U) || (app_can_now_us() >= next_alive)) {
                next_alive = app_can_now_us() + 5000000U;
                printf("[alive] t=%lu ms link=%s hb=%lu rx0=%lu echo=%lu\r\n",
                       (unsigned long) (app_can_now_us() / 1000U),
                       app_lwip_link_up() ? "UP" : "DOWN",
                       (unsigned long) s_hb_sent, (unsigned long) s_rx_cnt,
                       (unsigned long) s_echo_cnt);
            }
        }
    }
    return 0;
}