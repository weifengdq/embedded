/*
 * vss_signals - COVESA VSS 信号映射例程（板端发 CAN 信号 -> 网关 -> 主机 vcan0 -> VSS 路径解码）
 *
 * 硬件: hpm5e31_1。CAN0 = VSS 信号源（app_can 周期发 3 帧），CAN1 = cannelloni 网关（app_can_gw）。
 *
 * VSS 信号映射（ID/起始位/长度/因子/偏移/单位/路径）见 host/vss/vss_decode.py 的 MAP；
 * 板端按同一张表发送：
 *   0x400  Vehicle.Speed (u16, 0.01 km/h) + Vehicle.Powertrain.CombustionEngine.Speed (u16, rpm)
 *   0x401  Vehicle.Powertrain.CombustionEngine.ECT (i8, -40 offset, °C) +
 *          Vehicle.Powertrain.Transmission.CurrentGear (u8) +
 *          Vehicle.Cabin.Door.Row1.DriverSide.IsOpen (u8, 0/1)
 *   0x402  Vehicle.Powertrain.TractionBattery.StateOfCharge.Current (u16, 0.1 %)
 *
 * 命令: vss on|off | rate <ms> | stat | gw on
 * 上电自动 vss on（100 ms 周期）并自动 gw on（网关模式）。
 */
#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include "hpm_csr_drv.h"
#include "app_can.h"
#include "app_lwip.h"
#include "app_can_gw.h"
#include "ext/board_can.h"

#define VSS_CAN0    BOARD_CAN0_BASE
#define ID_SPEED    0x400U
#define ID_ECT_GEAR 0x401U
#define ID_SOC      0x402U

static app_can_t s_can0;
static uint32_t s_rate_ms = 100U;
static uint32_t s_on = 1U;
static uint32_t s_sent;
static uint32_t s_tick;
static uint32_t s_cpu_mhz;

static uint32_t now_us(void) { return (uint32_t) (hpm_csr_get_core_mcycle() / (uint64_t) s_cpu_mhz); }

/* 简单确定性波形：速度 0..120 km/h、转速 800..6000 rpm、水温 70..100 °C、挡位 1..6、车门 1 s 周期开关 */
/* 三角波：period 个 tick 从 lo 升到 hi 再降回 lo（无 libm 依赖） */
static uint32_t tri(uint32_t t, uint32_t period, uint32_t lo, uint32_t hi)
{
    uint32_t half = period / 2U;
    uint32_t phase = t % period;
    uint32_t span = hi - lo;
    uint32_t v;
    if (half == 0U) {
        return lo;
    }
    v = (phase < half) ? (phase * span / half) : ((period - phase) * span / half);
    return lo + v;
}

static void vss_send(void)
{
    app_can_frame_t f;
    uint32_t t = s_tick;
    uint32_t speed_001 = tri(t, 200U, 0U, 12000U);          /* 0..120.00 km/h */
    uint32_t rpm = tri(t, 100U, 800U, 6000U);                /* 800..6000 rpm */
    int32_t ect = (int32_t) tri(t, 500U, 70U, 100U);         /* 70..100 °C */
    uint8_t gear = (uint8_t) ((t / 20U) % 6U) + 1U;
    uint8_t door = (uint8_t) ((t / 20U) & 1U);
    uint32_t soc_01pct = tri(t, 400U, 200U, 1000U);          /* 20.0..100.0 % */

    memset(&f, 0, sizeof(f));
    f.id = ID_SPEED;
    f.len = 4;
    f.data[0] = (uint8_t) (speed_001 & 0xFFU);
    f.data[1] = (uint8_t) ((speed_001 >> 8) & 0xFFU);
    f.data[2] = (uint8_t) (rpm & 0xFFU);
    f.data[3] = (uint8_t) ((rpm >> 8) & 0xFFU);
    (void) app_can_send(&s_can0, &f, 1000U);

    memset(&f, 0, sizeof(f));
    f.id = ID_ECT_GEAR;
    f.len = 3;
    f.data[0] = (uint8_t) (int8_t) (ect + 40); /* 偏移 -40 编码 */
    f.data[1] = gear;
    f.data[2] = door;
    (void) app_can_send(&s_can0, &f, 1000U);

    memset(&f, 0, sizeof(f));
    f.id = ID_SOC;
    f.len = 2;
    f.data[0] = (uint8_t) (soc_01pct & 0xFFU);
    f.data[1] = (uint8_t) ((soc_01pct >> 8) & 0xFFU);
    (void) app_can_send(&s_can0, &f, 1000U);

    s_sent += 3U;
    s_tick++;
}

static int gw_start(void)
{
    app_can_gw_cfg_t gw;
    int rc_ip = app_lwip_init(NULL);
    int rc_gw;

    printf("[gw] lwip rc=%d link=%s\r\n", rc_ip, app_lwip_link_up() ? "UP" : "DOWN");
    memset(&gw, 0, sizeof(gw));
    gw.local_ip = "192.168.2.100";
    /* 纯发送场景必须显式指定对端：否则 peer 要等主机先发一包才能学到 */
    gw.peer_ip = "192.168.2.1";
    gw.peer_port = 4000U;
    gw.bus_speed = APP_CAN_SPEED_CLASSIC_500K;
    gw.max_frames_per_pkt = APP_CAN_GW_MAX_FRAMES_PER_PKT;
    gw.ch_count = 1;
    gw.ch[0].can_ch = 1U;
    gw.ch[0].udp_port = 4000U;
    gw.ch[0].enabled = true;
    rc_gw = app_can_gw_init(&gw);
    printf("[gw] CAN1 <-> UDP4000 rc=%d\r\n", rc_gw);
    if (rc_gw == 0) {
        app_can_gw_set_forward(true);
    }
    return rc_gw;
}

static void cmd_vss(int argc, char *argv[])
{
    const char *a = (argc >= 2) ? argv[1] : NULL;
    if ((a != NULL) && (strcmp(a, "on") == 0)) {
        s_on = 1U;
        printf("vss: ON rate=%u ms (3 frames/tick)\r\n", (unsigned) s_rate_ms);
    } else if ((a != NULL) && (strcmp(a, "off") == 0)) {
        s_on = 0U;
        printf("vss: OFF sent=%u\r\n", (unsigned) s_sent);
    } else {
        printf("usage: vss on|off\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 vss, cmd_vss, VSS signal generator: vss on|off);

static void cmd_rate(int argc, char *argv[])
{
    uint32_t ms = 100U;
    if (argc >= 2) {
        (void) bsp_parse_u32(argv[1], &ms);
    }
    if (ms >= 10U && ms <= 1000U) {
        s_rate_ms = ms;
    }
    printf("rate=%u ms\r\n", (unsigned) s_rate_ms);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 rate, cmd_rate, set VSS frame period: rate <ms>);

static void cmd_vstat(void)
{
    printf("vss: %s rate=%u ms sent=%u tick=%u uptime=%u ms\r\n", s_on ? "ON" : "OFF", (unsigned) s_rate_ms,
           (unsigned) s_sent, (unsigned) s_tick, (unsigned) bsp_uptime_ms());
    {
        app_can_stats_t st;
        app_can_get_stats(&s_can0, &st);
        printf("can0: tx=%u tx_fail=%u rx=%u busoff=%u tec=%u rec=%u\r\n", (unsigned) st.tx_frames,
               (unsigned) st.tx_fail, (unsigned) st.rx_frames, (unsigned) st.busoff_count, (unsigned) st.tec,
               (unsigned) st.rec);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 vstat, cmd_vstat, VSS signal statistics);

int main(void)
{
    int rc_can;
    app_can_cfg_t cfg;
    uint32_t last_ms = 0U;
    uint32_t t_end;
    int rc_gw;

    board_init();
    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    if (s_cpu_mhz == 0U) {
        s_cpu_mhz = 480U;
    }
    cfg = (app_can_cfg_t) APP_CAN_CFG_DEFAULT;
    cfg.speed = APP_CAN_SPEED_CLASSIC_500K;
    rc_can = app_can_init(&s_can0, VSS_CAN0, &cfg);
    printf("[app] vss_signals ready: CAN0 rc=%d (0x400/0x401/0x402 @ %u ms)\r\n", rc_can,
           (unsigned) s_rate_ms);

    rc_gw = gw_start();
    printf("[app] gw rc=%d, host: candump vcan0 应看到 0x400/0x401/0x402\r\n", rc_gw);

    /* 自检：发 10 帧供上位机采样 */
    app_can_frame_t f;
    memset(&f, 0, sizeof(f));
    f.id = ID_SPEED;
    f.len = 4;
    f.data[0] = 0x10U;
    f.data[1] = 0x27U; /* 100.00 km/h */
    f.data[2] = 0x20U;
    f.data[3] = 0x4EU; /* 20000 rpm -> 超范围，用于验证解码器范围检查 */
    (void) app_can_send(&s_can0, &f, 1000U);
    printf("[app] 0x400 自检帧已发（100.00 km/h / 20000 rpm）\r\n");

    t_end = bsp_uptime_ms() + 1200U;
    while ((int32_t) (bsp_uptime_ms() - t_end) < 0) {
        bsp_shell_poll();
        app_lwip_poll();
        app_can_gw_poll();
        if ((bsp_uptime_ms() - last_ms) >= s_rate_ms) {
            last_ms = bsp_uptime_ms();
            if (s_on != 0U) {
                vss_send();
            }
        }
    }
    printf("[auto] 1.2 s 发送完成: ");
    cmd_vstat();

    while (1) {
        bsp_shell_poll();
        app_lwip_poll();
        app_can_gw_poll();
        if ((bsp_uptime_ms() - last_ms) >= s_rate_ms) {
            last_ms = bsp_uptime_ms();
            if (s_on != 0U) {
                vss_send();
            }
        }
    }
}
