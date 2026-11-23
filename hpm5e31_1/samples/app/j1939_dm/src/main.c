/*
 * j1939_dm - J1939-73 诊断报文实测：DM1 激活故障 / DM2 历史 / DM3 / DM11 清除
 *
 * 硬件：CAN0(PD00/PD01) 与 CAN1(PD05/PD04) 同一段 250 kbit/s 总线。
 *       CAN0 = 发动机 ECU SA=0x00（周期发 DM1，1 s）；
 *       CAN1 = 诊断测试仪 SA=0xF9（解码 DM1/DM2，发 DM3/DM11 清除请求）。
 *
 * 覆盖：
 *   1) DM1 故障注入：fault add <spn> <fmi> [oc] -> 下一秒 DM1 灯位/SPN/FMI/OC 变化；
 *   2) 灯状态时间线：记录 DM1 每次变化（MIL/RSL/AWL/PL、DTC），带时间戳打印；
 *   3) DM2 历史：清除激活 DTC 后，请求 DM2 仍能看到历史 DTC（>8 B，走 RTS/CTS）；
 *   4) DM11 清除激活、DM3 清除历史：分别验证灯熄灭与历史表清空。
 *
 * 现象：上电自动跑序列并打印 PASS/FAIL、灯状态时间线表、DM1/DM2 解码表。
 * shell: run | dm add <spn> <fmi> [oc] | dm clear | dm hist | dm timeline | stat | info
 */
#include <stdarg.h>
#include <string.h>
#include "bsp.h"
#include "app_j1939.h"
#include "j1939_port.h"
#include "ext/board_can.h"

#define SA_ECU    (0x00U)
#define SA_TESTER (0xF9U)

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_buf_ecu[MCAN_MSG_BUF_SIZE_IN_WORDS];
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_buf_tst[MCAN_MSG_BUF_SIZE_IN_WORDS];
#else
static uint32_t s_buf_ecu[MCAN_MSG_BUF_SIZE_IN_WORDS];
static uint32_t s_buf_tst[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static j1939_bus_t   s_bus_ecu;
static j1939_bus_t   s_bus_tst;
static j1939_stack_t s_ecu;
static j1939_stack_t s_tst;

static const uint64_t NAME_ECU = (0x000123ULL << 43) | (0x00001ULL << 32) | 0ULL;

typedef struct {
    uint8_t  lamp;
    uint8_t  mil, rsl, awl, pl;
    uint8_t  dtc_n;
    uint32_t spn[4];
    uint8_t  fmi[4];
    uint8_t  oc[4];
} dm_pdu_t;

typedef struct {
    uint32_t t_ms;
    uint8_t  lamp;
    uint8_t  dtc_n;
    uint32_t spn;
    uint8_t  fmi;
} timeline_t;

static struct {
    uint32_t t0_ms;
    dm_pdu_t dm1;
    dm_pdu_t dm2;
    uint32_t dm1_count;
    uint32_t dm2_count;
    bool     dm1_valid;
    bool     dm2_valid;
    timeline_t tl[16];
    uint8_t  tl_n;
    uint8_t  last_lamp;
    uint8_t  last_dtc_n;
    bool     have_last;
} g_dm;

static uint32_t g_pass, g_fail;

static void report(bool ok, const char *fmt, ...)
{
    printf(ok ? "[PASS] " : "[FAIL] ");
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
    }
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\r\n");
}

static void dm_decode(const uint8_t *d, uint16_t len, dm_pdu_t *out)
{
    __builtin_memset(out, 0, sizeof(*out));
    if (len < 2U) {
        return;
    }
    out->lamp = d[0];
    out->mil = (uint8_t) (d[0] & 0x03U);
    out->rsl = (uint8_t) ((d[0] >> 2) & 0x03U);
    out->awl = (uint8_t) ((d[0] >> 4) & 0x03U);
    out->pl = (uint8_t) ((d[0] >> 6) & 0x03U);
    uint16_t n = (uint16_t) ((len >= 2U) ? ((len - 2U) / 4U) : 0U);
    if (n > 4U) {
        n = 4U;
    }
    for (uint16_t i = 0U; i < n; i++) {
        const uint8_t *b = &d[2U + 4U * i];
        out->spn[i] = (uint32_t) (b[0] & 0x07U) | ((uint32_t) b[2] << 3) |
                      ((uint32_t) b[3] << 11) | ((uint32_t) ((b[1] >> 7) & 0x01U) << 19);
        out->fmi[i] = (uint8_t) (b[0] >> 3);
        out->oc[i] = (uint8_t) (b[1] & 0x7FU);
    }
    out->dtc_n = (uint8_t) n;
}

static const char *lamp_name(uint8_t lamp)
{
    static char buf[32];
    buf[0] = '\0';
    if ((lamp & 0x03U) == 1U) {
        strcat(buf, "MIL ");
    }
    if ((lamp & 0x0CU) == 4U) {
        strcat(buf, "RSL ");
    }
    if ((lamp & 0x30U) == 0x10U) {
        strcat(buf, "AWL ");
    }
    if ((lamp & 0xC0U) == 0x40U) {
        strcat(buf, "PL ");
    }
    if (buf[0] == '\0') {
        strcpy(buf, "off");
    }
    return buf;
}

static void timeline_add(uint32_t now_ms)
{
    if (!g_dm.dm1_valid) {
        return;
    }
    if (g_dm.have_last && (g_dm.last_lamp == g_dm.dm1.lamp) &&
        (g_dm.last_dtc_n == g_dm.dm1.dtc_n)) {
        return;
    }
    if (g_dm.tl_n < 16U) {
        timeline_t *t = &g_dm.tl[g_dm.tl_n++];
        t->t_ms = now_ms - g_dm.t0_ms;
        t->lamp = g_dm.dm1.lamp;
        t->dtc_n = g_dm.dm1.dtc_n;
        t->spn = (g_dm.dm1.dtc_n > 0U) ? g_dm.dm1.spn[0] : 0U;
        t->fmi = (g_dm.dm1.dtc_n > 0U) ? g_dm.dm1.fmi[0] : 0U;
    }
    g_dm.last_lamp = g_dm.dm1.lamp;
    g_dm.last_dtc_n = g_dm.dm1.dtc_n;
    g_dm.have_last = true;
}

static void tst_rx_cb(j1939_stack_t *s, uint32_t pgn, uint8_t sa, uint8_t da,
                      const uint8_t *data, uint16_t len, void *user)
{
    (void) s;
    (void) da;
    (void) user;
    if (sa != SA_ECU) {
        return;
    }
    if (pgn == J1939_PGN_DM1) {
        g_dm.dm1_count++;
        dm_decode(data, len, &g_dm.dm1);
        g_dm.dm1_valid = true;
        timeline_add(j1939_millis());
    } else if (pgn == J1939_PGN_DM2) {
        g_dm.dm2_count++;
        dm_decode(data, len, &g_dm.dm2);
        g_dm.dm2_valid = true;
    }
}

static void pump_ms(uint32_t ms)
{
    uint32_t t0 = j1939_millis();
    while ((j1939_millis() - t0) < ms) {
        j1939_poll(&s_ecu);
        j1939_poll(&s_tst);
        bsp_shell_poll();
    }
}

static void print_dm_table(const char *tag, const dm_pdu_t *d)
{
    printf("  %s: lamp=0x%02X [%s] flash=%u dtc=%u\r\n", tag, d->lamp, lamp_name(d->lamp),
           0U, d->dtc_n);
    for (uint8_t i = 0U; i < d->dtc_n; i++) {
        printf("    DTC[%u] SPN=%-6u FMI=%-2u OC=%u\r\n", i, d->spn[i], d->fmi[i], d->oc[i]);
    }
}

static void print_timeline(void)
{
    printf("  ---- DM1 lamp timeline (t since test start) ----\r\n");
    printf("  | idx | t(ms) | lamp | MIL | RSL | AWL | PL | DTC | SPN | FMI |\r\n");
    for (uint8_t i = 0U; i < g_dm.tl_n; i++) {
        const timeline_t *t = &g_dm.tl[i];
        printf("  | %3u | %5u | 0x%02X |  %u  |  %u  |  %u  | %u  |  %u  | %3u |  %u  |\r\n",
               i, t->t_ms, t->lamp, (t->lamp & 3U), ((t->lamp >> 2) & 3U),
               ((t->lamp >> 4) & 3U), ((t->lamp >> 6) & 3U), t->dtc_n, t->spn, t->fmi);
    }
    (void) lamp_name(0);
}

static volatile bool s_dm_testing;

static void run_dm_test(void)
{
    if (s_dm_testing) {
        printf("[busy] dm test already running, command ignored\r\n");
        return;
    }
    s_dm_testing = true;
    printf("\r\n=== j1939_dm auto test (CAN0=ECU DM1 1s, CAN1=TESTER) ===\r\n");
    __builtin_memset(&g_dm, 0, sizeof(g_dm));
    g_dm.t0_ms = j1939_millis();
    g_pass = 0U;
    g_fail = 0U;

    j1939_dm_clear_active(&s_ecu);
    j1939_dm_clear_history(&s_ecu);
    pump_ms(2300U); /* 至少两个 DM1 周期，等灯全灭基线 */
    report(g_dm.dm1_valid && (g_dm.dm1.lamp == 0U) && (g_dm.dm1.dtc_n == 0U),
           "baseline DM1: lamp=0x%02X dtc=%u (expect 0x00/0)", g_dm.dm1.lamp, g_dm.dm1.dtc_n);

    /* 1) 第 1 个故障：SPN 100 FMI 4 -> AWL 黄灯 */
    (void) j1939_dm_add_fault(&s_ecu, 100U, 4U, 1U);
    pump_ms(1300U);
    report((g_dm.dm1.lamp & 0x30U) == 0x10U && g_dm.dm1.dtc_n == 1U &&
           g_dm.dm1.spn[0] == 100U && g_dm.dm1.fmi[0] == 4U,
           "fault#1 SPN100/FMI4 -> lamp=0x%02X AWL=%u SPN=%u FMI=%u",
           g_dm.dm1.lamp, (g_dm.dm1.lamp >> 4) & 3U, g_dm.dm1.spn[0], g_dm.dm1.fmi[0]);

    /* 2) 第 2 个故障：SPN 190 FMI 1 -> MIL 红灯也亮 */
    (void) j1939_dm_add_fault(&s_ecu, 190U, 1U, 2U);
    pump_ms(1300U);
    report((g_dm.dm1.lamp & 0x03U) == 1U && (g_dm.dm1.lamp & 0x30U) == 0x10U,
           "fault#2 SPN190/FMI1 -> lamp=0x%02X MIL=%u AWL=%u",
           g_dm.dm1.lamp, g_dm.dm1.lamp & 3U, (g_dm.dm1.lamp >> 4) & 3U);

    /* 3) DM11 清除激活 DTC -> 灯灭 */
    (void) j1939_request(&s_tst, J1939_PGN_DM11, SA_ECU);
    pump_ms(1500U);
    report((g_dm.dm1.lamp == 0U) && (g_dm.dm1.dtc_n == 0U),
           "DM11 clear active: lamp=0x%02X dtc=%u (expect 0x00/0)",
           g_dm.dm1.lamp, g_dm.dm1.dtc_n);

    /* 4) DM2 历史：2 个 DTC，>8 字节走 RTS/CTS */
    g_dm.dm2_count = 0U;
    g_dm.dm2_valid = false;
    (void) j1939_request(&s_tst, J1939_PGN_DM2, SA_ECU);
    pump_ms(600U);
    report(g_dm.dm2_valid && (g_dm.dm2.dtc_n == 2U) &&
           ((g_dm.dm2.spn[0] == 100U) || (g_dm.dm2.spn[1] == 100U)),
           "DM2 history: frames=%u dtc=%u (SPN %u/%u via RTS/CTS)",
           g_dm.dm2_count, g_dm.dm2.dtc_n, g_dm.dm2.spn[0], g_dm.dm2.spn[1]);

    /* 5) DM3 清除历史 -> DM2 空 */
    (void) j1939_request(&s_tst, J1939_PGN_DM3, SA_ECU);
    pump_ms(300U);
    g_dm.dm2_valid = false;
    (void) j1939_request(&s_tst, J1939_PGN_DM2, SA_ECU);
    pump_ms(600U);
    report(g_dm.dm2_valid && (g_dm.dm2.dtc_n == 0U),
           "DM3 clear history: DM2 dtc=%u lamp=0x%02X (expect 0/0x00)",
           g_dm.dm2.dtc_n, g_dm.dm2.lamp);

    print_timeline();
    print_dm_table("DM1 last", &g_dm.dm1);
    print_dm_table("DM2 last", &g_dm.dm2);
    bool tl_ok = (g_dm.tl_n >= 4U);
    report(tl_ok, "lamp timeline entries=%u (expect >=4: off->AWL->MIL+AWL->off)",
           g_dm.tl_n);

    s_dm_testing = false;
    printf("\r\n=== RESULT: %s (%u PASS / %u FAIL) ===\r\n",
           (g_fail == 0U) ? "PASS" : "FAIL", g_pass, g_fail);
    printf("bus0: tx=%u rx=%u | bus1: tx=%u rx=%u | ecu dm1_tx=%u tester dm1_rx=%u\r\n",
           s_bus_ecu.tx_frames, s_bus_ecu.rx_frames, s_bus_tst.tx_frames,
           s_bus_tst.rx_frames, s_ecu.sched[0].sent, g_dm.dm1_count);
}

/* ---------------- shell ---------------- */

static void cmd_run(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    run_dm_test();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 run, cmd_run, run DM1/DM2/DM3/DM11 self test);

static void cmd_dm(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: dm add <spn> <fmi> [oc] | clear | hist | timeline | list\r\n");
        return;
    }
    if (strcmp(argv[1], "add") == 0) {
        uint32_t spn = 100U, fmi = 4U, oc = 1U;
        if (argc >= 4) {
            bsp_parse_u32(argv[2], &spn);
            bsp_parse_u32(argv[3], &fmi);
        }
        if (argc >= 5) {
            bsp_parse_u32(argv[4], &oc);
        }
        if (j1939_dm_add_fault(&s_ecu, spn, (uint8_t) fmi, (uint8_t) oc) == 0) {
            printf("DTC added: SPN=%u FMI=%u OC=%u; DM1 updates within 1 s\r\n",
                   (unsigned) spn, (unsigned) fmi, (unsigned) oc);
        } else {
            printf("DTC table full\r\n");
        }
    } else if (strcmp(argv[1], "clear") == 0) {
        j1939_dm_clear_active(&s_ecu);
        printf("active DTC cleared, DM1 lamps off within 1 s\r\n");
    } else if (strcmp(argv[1], "hist") == 0) {
        (void) j1939_request(&s_tst, J1939_PGN_DM2, SA_ECU);
        pump_ms(500U);
        print_dm_table("DM2", &g_dm.dm2);
    } else if (strcmp(argv[1], "timeline") == 0) {
        print_timeline();
    } else if (strcmp(argv[1], "list") == 0) {
        printf("active=%u history=%u\r\n", s_ecu.dtc_count, s_ecu.dtc_hist_count);
        for (uint8_t i = 0U; i < s_ecu.dtc_count; i++) {
            printf("  active[%u] SPN=%u FMI=%u OC=%u lamp=%u\r\n", i, s_ecu.dtc[i].spn,
                   s_ecu.dtc[i].fmi, s_ecu.dtc[i].oc, s_ecu.dtc[i].lamp);
        }
        for (uint8_t i = 0U; i < s_ecu.dtc_hist_count; i++) {
            printf("  hist[%u]   SPN=%u FMI=%u OC=%u\r\n", i, s_ecu.dtc_hist[i].spn,
                   s_ecu.dtc_hist[i].fmi, s_ecu.dtc_hist[i].oc);
        }
    } else {
        printf("unknown subcommand\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dm, cmd_dm, DM1/DM2 operations: dm add|clear|hist|timeline|list);

static void cmd_stat(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("ECU: sa=0x%02X state=%u dm1_sched_sent=%u active=%u hist=%u clears=%u\r\n",
           s_ecu.sa, (unsigned) s_ecu.addr_state, s_ecu.sched[0].sent, s_ecu.dtc_count,
           s_ecu.dtc_hist_count, s_ecu.stats.dm_clears);
    printf("TEST: dm1_rx=%u dm2_rx=%u rx_frames=%u tx_frames=%u\r\n", g_dm.dm1_count,
           g_dm.dm2_count, s_tst.stats.rx_frames, s_tst.stats.tx_frames);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, dump DM stats);

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("j1939_dm: DM1=PGN 0xFECA (1 s), DM2=0xFECB (on request), DM3=0xFECC, DM11=0xFED3\r\n");
    printf("lamp byte: MIL[1:0] RSL[3:2] AWL[5:4] PL[7:6] 2bit each (0 off/1 on/2 err/3 n.a.)\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, show DM config);

int main(void)
{
    j1939_cfg_t cfg;
    bsp_init();
    printf("\r\n[boot] j1939_dm: CAN0=ECU SA=0x00, CAN1=TESTER SA=0xF9 @250k\r\n");
    board_can_leds_init();
    board_can_init_pins();
    board_can_transceivers_enable();
    if (j1939_bus_init(&s_bus_ecu, BOARD_CAN0_BASE, s_buf_ecu, 250000U) != 0) {
        printf("[FAIL] CAN0 init\r\n");
    }
    if (j1939_bus_init(&s_bus_tst, BOARD_CAN1_BASE, s_buf_tst, 250000U) != 0) {
        printf("[FAIL] CAN1 init\r\n");
    }
    __builtin_memset(&cfg, 0, sizeof(cfg));
    cfg.auto_claim = true;
    cfg.preferred_sa = SA_ECU;
    cfg.name = NAME_ECU;
    j1939_init(&s_ecu, &s_bus_ecu, &cfg);
    j1939_dm_init(&s_ecu);
    __builtin_memset(&cfg, 0, sizeof(cfg));
    cfg.auto_claim = false;
    cfg.preferred_sa = SA_TESTER;
    j1939_init(&s_tst, &s_bus_tst, &cfg);
    j1939_set_rx_cb(&s_tst, tst_rx_cb, NULL);

    run_dm_test();
    printf("type 'run|dm add <spn> <fmi>|dm clear|dm hist|dm timeline|stat|info'\r\n");
    while (1) {
        bsp_shell_poll();
        j1939_poll(&s_ecu);
        j1939_poll(&s_tst);
    }
    return 0;
}
