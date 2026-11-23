/*
 * j1939_ecu - SAE J1939 发动机 ECU 模拟 + 测试仪（CAN0 <-> CAN1 板内双节点，Bench A）
 *
 * 硬件：hpm5e31_1，CAN0(PD00/PD01) 与 CAN1(PD05/PD04) 经 TCAN1044AV 连到同一段
 *       CAN 总线，板上 120 欧终端已接。CAN0 跑"发动机 ECU"（SA=0x00），
 *       CAN1 跑"诊断测试仪"（SA=0xF9），全部板内自测，无需上位机。
 *
 * 覆盖：
 *   1) 地址声明状态机：ECU 上电先用 SA=0xFE 请求 0xEE00，等 250 ms 后广播 NAME；
 *      测试仪校验 PGN/SA/NAME 与声明时延。
 *   2) 周期 PGN：EEC1 10 ms(P=3)、CCVS1 100 ms、ET1 1 s、DM1 1 s；测试仪统计
 *      到达间隔抖动（min/avg/max）。
 *   3) 请求/应答：测试仪发 PGN 0xEA00 请求 ET1/CCVS1（单播），ECU 立即回单播帧；
 *      测量端到端时延。
 *   4) DM1 故障注入：shell 命令 fault add <spn> <fmi> 后下一个 DM1 周期灯/SPN/FMI
 *      变化；测试仪解码灯状态与 DTC 表。
 *   5) DM2 历史：请求 0xFCB 得到历史 DTC（>8 字节，走 RTS/CTS 传输）。
 *   6) DM11 清除：测试仪请求 0xFED3，ECU 清激活 DTC，DM1 灯熄灭。
 *
 * 现象：上电自动跑完整序列并打印 PASS/FAIL 表；shell 可重跑：
 *   run | stat | fault add <spn> <fmi> | fault clear | dm1 | req <pgn_hex> | mon <ms> | info
 */
#include <stdarg.h>
#include <string.h>
#include "bsp.h"
#include "app_j1939.h"
#include "j1939_port.h"
#include "j1939_internal.h"
#include "ext/board_can.h"
#include "hpm_clock_drv.h"

#ifdef J1939_WITH_GW
#include "app_lwip.h"
#include "app_can.h"
#include "app_can_gw.h"
#endif

#define SA_ECU    (0x00U)
#define SA_TESTER (0xF9U)

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_buf_can0[MCAN_MSG_BUF_SIZE_IN_WORDS];
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_buf_can1[MCAN_MSG_BUF_SIZE_IN_WORDS];
#else
static uint32_t s_buf_can0[MCAN_MSG_BUF_SIZE_IN_WORDS];
static uint32_t s_buf_can1[MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static j1939_bus_t   s_bus_ecu;
static j1939_bus_t   s_bus_tester;
static j1939_stack_t s_ecu;
static j1939_stack_t s_tester;

/* ---------------- 发动机信号（ECU 侧） ---------------- */
static struct {
    uint16_t rpm_x8;      /* 0.125 rpm/bit，6000 => 750 rpm */
    uint8_t  throttle_pct;
    int16_t  coolant_c;
    int16_t  oil_temp_c;
    uint16_t oil_kpa;
    uint16_t speed_x256;  /* km/h * 256 */
    uint16_t fuel_rate_x20; /* 0.05 L/h per bit */
} g_sig = {6000U, 20U, 85, 90, 350U, (uint16_t) (36.5 * 256), 120U};

static const uint64_t NAME_TESTER =
    (0x000456ULL << 43) | (0x00002ULL << 32) | (0U << 29) | (0U << 24) | (0x00ULL << 16) |
    (0ULL << 15) | (0ULL << 8) | (0ULL << 4) | (0ULL << 1) | 0ULL;

const uint64_t NAME_ECU =
    (0x000123ULL << 43) | /* Identity Number 21 bit */
    (0x00001ULL << 32) |  /* Manufacturer Code 11 bit */
    (0U << 29) | (0U << 24) | (0x00ULL << 16) | (0ULL << 15) |
    (0ULL << 8) | (0ULL << 4) | (0ULL << 1) | 0ULL; /* 固定地址，不支持任意地址 */

/* ---------------- 周期 PGN 填充回调 ---------------- */

static void fill_eec1(j1939_stack_t *s, uint8_t *buf, uint16_t *len, void *user)
{
    (void) s;
    (void) user;
    buf[0] = 0x00U;                       /* 扭矩模式 */
    buf[1] = (uint8_t) (125 + g_sig.throttle_pct); /* 驾驶员需求扭矩 1%/bit, -125 */
    buf[2] = (uint8_t) (125 + g_sig.throttle_pct); /* 实际扭矩 */
    buf[3] = (uint8_t) (g_sig.rpm_x8 & 0xFFU);     /* 转速 0.125 rpm/bit */
    buf[4] = (uint8_t) (g_sig.rpm_x8 >> 8);
    buf[5] = 0x00U;                       /* 控制源地址 */
    buf[6] = 0x00U;                       /* 起动模式 */
    buf[7] = (uint8_t) (125 + g_sig.throttle_pct);
    *len = 8U;
}

static void fill_et1(j1939_stack_t *s, uint8_t *buf, uint16_t *len, void *user)
{
    (void) s;
    (void) user;
    buf[0] = (uint8_t) (g_sig.coolant_c + 40);  /* 冷却液温度 1 degC/bit, -40 */
    buf[1] = (uint8_t) (30 + 40);               /* 燃油温度 */
    buf[2] = (uint8_t) (g_sig.oil_temp_c + 40); /* 机油温度 */
    buf[3] = (uint8_t) (g_sig.oil_kpa / 4U);    /* 机油压力 4 kPa/bit */
    buf[4] = 0x00U;
    buf[5] = 0x00U;                             /* 冷却液压力 2 kPa/bit */
    buf[6] = 0x00U;
    buf[7] = 0xFFU;                             /* 冷却液液位不可用 */
    *len = 8U;
}

static void fill_ccvs1(j1939_stack_t *s, uint8_t *buf, uint16_t *len, void *user)
{
    (void) s;
    (void) user;
    buf[0] = (uint8_t) (g_sig.speed_x256 & 0xFFU); /* 车轮车速 1/256 km/h/bit */
    buf[1] = (uint8_t) (g_sig.speed_x256 >> 8);
    buf[2] = 0x03U; /* 离合器开关：3 = 不可用 */
    buf[3] = 0x00U; /* 制动开关：0 = 未踩 */
    buf[4] = 0x00U;
    buf[5] = 0x00U;
    buf[6] = 0xFFU;
    buf[7] = 0xFFU;
    *len = 8U;
}

static void fill_lfe(j1939_stack_t *s, uint8_t *buf, uint16_t *len, void *user)
{
    (void) s;
    (void) user;
    buf[0] = (uint8_t) (g_sig.fuel_rate_x20 & 0xFFU); /* 燃油消耗率 0.05 L/h/bit */
    buf[1] = (uint8_t) (g_sig.fuel_rate_x20 >> 8);
    buf[2] = 0xFFU;
    buf[3] = 0xFFU;
    buf[4] = 0xFFU;
    buf[5] = 0xFFU;
    buf[6] = 0xFFU;
    buf[7] = 0xFFU;
    *len = 8U;
}

/* ---------------- 测试仪（接收/统计） ---------------- */

#define PGN_IDX_EEC1  0
#define PGN_IDX_CCVS1 1
#define PGN_IDX_ET1   2
#define PGN_IDX_DM1   3

typedef struct {
    uint32_t count;
    uint32_t first_us;
    uint32_t last_us;
    uint32_t min_gap_us;
    uint32_t max_gap_us;
    uint32_t sum_gap_us;
} pgn_stat_t;

typedef struct {
    uint8_t  lamp;   /* 原始灯状态字节 */
    uint8_t  mil, rsl, awl, pl;
    uint8_t  dtc_n;
    uint32_t spn[4];
    uint8_t  fmi[4];
    uint8_t  oc[4];
} dm_decode_t;

static struct {
    uint32_t boot_us;
    bool     claim_seen;
    bool     claim_name_ok;
    uint8_t  claim_sa;
    uint64_t claim_name;
    uint32_t claim_us;
    pgn_stat_t stat[4];
    uint32_t dm1_count;
    dm_decode_t dm1;
    dm_decode_t dm2;
    uint32_t dm2_count;
    bool     req_pending;
    uint32_t req_pgn;
    uint32_t req_sent_us;
    uint32_t req_ok;
    uint32_t req_lat_min;
    uint32_t req_lat_max;
    uint32_t req_lat_sum;
    uint32_t unknown_pgn;
    uint32_t last_unknown_pgn;
    bool     monitor;         /* mon 命令：逐帧打印 */
    uint32_t mon_frames;
    uint32_t rx_total;
    uint32_t dbg_printed;
} g_test;

static const char *pgn_name(uint32_t pgn)
{
    switch (pgn) {
    case J1939_PGN_EEC1: return "EEC1";
    case J1939_PGN_ET1: return "ET1";
    case J1939_PGN_CCVS1: return "CCVS1";
    case J1939_PGN_LFE: return "LFE";
    case J1939_PGN_DM1: return "DM1";
    case J1939_PGN_DM2: return "DM2";
    case J1939_PGN_DM3: return "DM3";
    case J1939_PGN_DM11: return "DM11";
    case J1939_PGN_REQUEST: return "REQUEST";
    case J1939_PGN_ACK: return "ACK";
    case J1939_PGN_ADDRESS_CLAIMED: return "ADDR_CLAIM";
    case J1939_PGN_TP_CM: return "TP.CM";
    case J1939_PGN_TP_DT: return "TP.DT";
    default: return "?";
    }
}

static uint64_t rd_le64(const uint8_t *p)
{
    uint64_t v = 0ULL;
    for (uint8_t i = 0U; i < 8U; i++) {
        v |= ((uint64_t) p[i]) << (8U * i);
    }
    return v;
}

static void dm_decode(const uint8_t *d, uint16_t len, dm_decode_t *out)
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

static void tester_record_gap(uint32_t idx, uint32_t now_us)
{
    pgn_stat_t *st = &g_test.stat[idx];
    st->count++;
    if (st->count == 1U) {
        st->first_us = now_us;
    } else {
        uint32_t gap = now_us - st->last_us;
        if ((st->min_gap_us == 0U) || (gap < st->min_gap_us)) {
            st->min_gap_us = gap;
        }
        if (gap > st->max_gap_us) {
            st->max_gap_us = gap;
        }
        st->sum_gap_us += gap;
    }
    st->last_us = now_us;
}

static void tester_rx_cb(j1939_stack_t *s, uint32_t pgn, uint8_t sa, uint8_t da,
                         const uint8_t *data, uint16_t len, void *user)
{
    uint32_t now_us = j1939_micros();
    (void) s;
    (void) user;
    g_test.rx_total++;
    if (sa != SA_ECU) {
        return; /* 只统计 ECU 发出的报文 */
    }
    if (g_test.monitor) {
        g_test.mon_frames++;
        printf("[%7u us] %-11s PGN=0x%05X SA=0x%02X DA=0x%02X DLC=%u:",
               now_us - g_test.boot_us, pgn_name(pgn), (unsigned) pgn, sa, da, len);
        for (uint16_t i = 0U; i < len; i++) {
            printf(" %02X", data[i]);
        }
        printf("\r\n");
    }

    switch (pgn) {
    case J1939_PGN_ADDRESS_CLAIMED:
        if (len == 8U) {
            g_test.claim_seen = true;
            g_test.claim_sa = sa;
            g_test.claim_name = rd_le64(data);
            g_test.claim_name_ok = (g_test.claim_name == NAME_ECU);
            g_test.claim_us = now_us - g_test.boot_us;
        }
        break;
    case J1939_PGN_EEC1:
        tester_record_gap(PGN_IDX_EEC1, now_us);
        break;
    case J1939_PGN_CCVS1:
        tester_record_gap(PGN_IDX_CCVS1, now_us);
        break;
    case J1939_PGN_ET1:
        tester_record_gap(PGN_IDX_ET1, now_us);
        break;
    case J1939_PGN_DM1:
        g_test.dm1_count++;
        dm_decode(data, len, &g_test.dm1);
        break;
    case J1939_PGN_DM2:
        g_test.dm2_count++;
        dm_decode(data, len, &g_test.dm2);
        break;
    default:
        g_test.unknown_pgn++;
        g_test.last_unknown_pgn = pgn;
        break;
    }

    /* 请求-应答时延：PDU2 的应答仍是广播（PDU2 没有 DA 字段），
     * 所以用"请求后 80 ms 内到达同一 PGN"判定为应答。 */
    if (g_test.req_pending && (pgn == g_test.req_pgn) &&
        ((now_us - g_test.req_sent_us) < 80000U)) {
        uint32_t lat = now_us - g_test.req_sent_us;
        g_test.req_pending = false;
        g_test.req_ok++;
        if ((g_test.req_lat_min == 0U) || (lat < g_test.req_lat_min)) {
            g_test.req_lat_min = lat;
        }
        if (lat > g_test.req_lat_max) {
            g_test.req_lat_max = lat;
        }
        g_test.req_lat_sum += lat;
    }
}

static void pump_us(uint32_t us)
{
    uint32_t t0 = j1939_micros();
    while ((j1939_micros() - t0) < us) {
        j1939_poll(&s_ecu);
        j1939_poll(&s_tester);
        bsp_shell_poll();
    }
}

/* ---------------- 自测序列 ---------------- */

static uint32_t s_pass, s_fail;

static void report(bool ok, const char *fmt, ...)
{
    printf(ok ? "[PASS] " : "[FAIL] ");
    if (ok) {
        s_pass++;
    } else {
        s_fail++;
    }
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\r\n");
}

static void tester_request(uint32_t pgn)
{
    g_test.req_pending = true;
    g_test.req_pgn = pgn;
    g_test.req_sent_us = j1939_micros();
    (void) j1939_request(&s_tester, pgn, SA_ECU);
}

static volatile bool s_testing;

static void run_tests(void)
{
    /* 重入保护：自动跑的过程中 shell 收到 run 时不能嵌套（会重置统计、打乱断言） */
    if (s_testing) {
        printf("[busy] self-test already running, command ignored\r\n");
        return;
    }
    s_testing = true;
    printf("\r\n=== j1939_ecu auto test (CAN0=ECU SA=0x00, CAN1=TESTER SA=0xF9) ===\r\n");
    __builtin_memset(&g_test, 0, sizeof(g_test));
    g_test.boot_us = j1939_micros();
    s_pass = 0;
    s_fail = 0;

    /* 地址声明只在首次上电做一次；再次 run 时补发一次广播，测试仪才能重新看到 NAME */
    if (s_ecu.addr_state == J1939_ADDR_CLAIMED) {
        j1939_send_address_claim(&s_ecu, 0xFFU); /* 再次 run：补发一次广播声明 */
    }

    /* 1) 地址声明：上电 -> 请求 0xEE00 -> 250 ms 后广播 NAME */
    pump_us(700000U);
    report(g_test.claim_seen && g_test.claim_name_ok && (g_test.claim_sa == SA_ECU),
           "addr claim: SA=0x%02X NAME=%08X%08X seen_at=%u us (250 ms wait + tx)",
           g_test.claim_sa, (uint32_t) (g_test.claim_name >> 32),
           (uint32_t) (g_test.claim_name & 0xFFFFFFFFU), g_test.claim_us);

    /* 2) 周期 PGN 与抖动：跑 1.5 s */
    for (uint8_t i = 0U; i < 4U; i++) {
        __builtin_memset(&g_test.stat[i], 0, sizeof(pgn_stat_t));
    }
    g_test.dm1_count = 0;
    pump_us(1500000U);

    {
        pgn_stat_t *st = &g_test.stat[PGN_IDX_EEC1];
        uint32_t avg = (st->count > 1U) ? (st->sum_gap_us / (st->count - 1U)) : 0U;
        report(st->count >= 120U && (st->max_gap_us < 20000U),
               "periodic EEC1(10ms): n=%u gap min/avg/max = %u/%u/%u us",
               st->count, st->min_gap_us, avg, st->max_gap_us);
        pgn_stat_t *c = &g_test.stat[PGN_IDX_CCVS1];
        report(c->count >= 12U, "periodic CCVS1(100ms): n=%u", c->count);
        pgn_stat_t *e = &g_test.stat[PGN_IDX_ET1];
        report(e->count >= 1U, "periodic ET1(1s): n=%u", e->count);
        report(g_test.dm1_count >= 1U, "periodic DM1(1s): n=%u lamp=0x%02X",
               g_test.dm1_count, g_test.dm1.lamp);
    }

    /* 3) 请求/应答（单播）：ET1、CCVS1 各请求 2 次 */
    g_test.req_ok = 0U;
    g_test.req_lat_min = 0U;
    g_test.req_lat_max = 0U;
    g_test.req_lat_sum = 0U;
    int nreq = 0;
    for (uint8_t k = 0U; k < 2U; k++) {
        tester_request(J1939_PGN_ET1);
        pump_us(60000U);
        nreq++;
        tester_request(J1939_PGN_LFE); /* 默认不周期发送，只能由请求触发 */
        pump_us(60000U);
        nreq++;
    }
    {
        uint32_t avg = (g_test.req_ok != 0U) ? (g_test.req_lat_sum / g_test.req_ok) : 0U;
        report((g_test.req_ok == (uint32_t) nreq) && (avg < 10000U),
               "request/response: %u/%u answered, latency min/avg/max = %u/%u/%u us",
               g_test.req_ok, nreq, g_test.req_lat_min, avg, g_test.req_lat_max);
    }

    /* 4) 故障注入 -> DM1 灯与 DTC */
    (void) j1939_dm_add_fault(&s_ecu, 100U, 4U, 1U);  /* SPN100 机油压力 FMI4 */
    (void) j1939_dm_add_fault(&s_ecu, 190U, 1U, 2U);  /* SPN190 超速 FMI1 */
    pump_us(1200000U);
    {
        bool lamp_ok = (g_test.dm1.mil == 1U) || (g_test.dm1.awl == 1U);
        bool dtc_ok = (g_test.dm1.dtc_n >= 1U) && (g_test.dm1.spn[0] == 100U) &&
                      (g_test.dm1.fmi[0] == 4U);
        report(lamp_ok && dtc_ok,
               "DM1 fault: lamp=0x%02X MIL=%u RSL=%u AWL=%u PL=%u SPN=%u FMI=%u OC=%u",
               g_test.dm1.lamp, g_test.dm1.mil, g_test.dm1.rsl, g_test.dm1.awl,
               g_test.dm1.pl, g_test.dm1.spn[0], g_test.dm1.fmi[0], g_test.dm1.oc[0]);
    }

    /* 5) DM2 历史（>8 字节，走 RTS/CTS） */
    tester_request(J1939_PGN_DM2);
    pump_us(500000U);
    report(g_test.dm2_count >= 1U && g_test.dm2.dtc_n >= 2U,
           "DM2 history via RTS/CTS: frames=%u dtc=%u (SPN %u/%u)",
           g_test.dm2_count, g_test.dm2.dtc_n, g_test.dm2.spn[0], g_test.dm2.spn[1]);

    /* 6) DM11 清除激活 DTC -> DM1 灯灭 */
    (void) j1939_request(&s_tester, J1939_PGN_DM11, SA_ECU);
    pump_us(1200000U);
    {
        bool cleared = (g_test.dm1.dtc_n == 0U) && (g_test.dm1.lamp == 0x00U);
        report(cleared, "DM11 clear: lamp=0x%02X dtc=%u (expect 0x00/0)",
               g_test.dm1.lamp, g_test.dm1.dtc_n);
    }

    s_testing = false;
    printf("\r\n=== RESULT: %s (%u PASS / %u FAIL) ===\r\n",
           (s_fail == 0U) ? "PASS" : "FAIL", s_pass, s_fail);
    printf("bus0: tx=%u rx=%u err=%u | bus1: tx=%u rx=%u err=%u\r\n",
           s_bus_ecu.tx_frames, s_bus_ecu.rx_frames, s_bus_ecu.tx_errors,
           s_bus_tester.tx_frames, s_bus_tester.rx_frames, s_bus_tester.tx_errors);
}

/* ---------------- shell 命令 ---------------- */

static void cmd_run(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    run_tests();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 run, cmd_run, run full J1939 self test);


/* ---------------- U-32-3 地址冲突/换址 ------------------------------------
 * CAN0 与 CAN1 挂同一段总线（板上互连），两个节点用相同 preferred_sa、不同 NAME
 * 同时做地址声明：NAME 大的一方必须让出地址（可动态换址时换到 0x81；不支持时退 0xFE）。
 * 两相：phase1 双方 arbitrary_address_capable=1（输者换址）；phase2 输者不支持动态换址（退 0xFE）。
 */
static uint64_t mk_name(uint32_t identity, bool arb)
{
    j1939_name_t n;
    __builtin_memset(&n, 0, sizeof(n));
    n.identity = identity & 0x1FFFFFu;
    n.manufacturer = 0x123u;
    n.function = 0x01u;              /* 发动机相关 */
    n.industry_group = 0x01u;        /* 道路车辆 */
    n.arbitrary_address_capable = arb;
    return j1939_name_encode(&n);
}

static void conflict_one(bool loser_arb)
{
    j1939_cfg_t cfg;
    uint64_t name_win = mk_name(0x10000u, true);    /* 小 NAME = 赢 */
    uint64_t name_lose = mk_name(0x20000u, loser_arb); /* 大 NAME = 输 */

    __builtin_memset(&cfg, 0, sizeof(cfg));
    cfg.auto_claim = true;
    cfg.preferred_sa = 0x80u;
    cfg.name = name_lose;             /* CAN0 节点故意输 */
    j1939_init(&s_ecu, &s_bus_ecu, &cfg);

    __builtin_memset(&cfg, 0, sizeof(cfg));
    cfg.auto_claim = true;
    cfg.preferred_sa = 0x80u;
    cfg.name = name_win;
    j1939_init(&s_tester, &s_bus_tester, &cfg);

    uint32_t t0 = j1939_millis();
    while (j1939_millis() - t0 < 2500u) {
        j1939_poll(&s_ecu);
        j1939_poll(&s_tester);
    }
    printf("  phase%d(输者arb=%d): A(CAN0,name=%08lX%08lX) sa=0x%02X state=%u | "
           "B(CAN1,name=%08lX%08lX) sa=0x%02X state=%u | claims_rx A=%u B=%u\r\n",
           loser_arb ? 1 : 2, (int) loser_arb,
           (unsigned long) (name_lose >> 32), (unsigned long) (name_lose & 0xFFFFFFFFu),
           s_ecu.sa, (unsigned) s_ecu.addr_state,
           (unsigned long) (name_win >> 32), (unsigned long) (name_win & 0xFFFFFFFFu),
           s_tester.sa, (unsigned) s_tester.addr_state,
           s_ecu.stats.addr_claims_rx, s_tester.stats.addr_claims_rx);
}

static void cmd_conflict(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("\r\n[j1939] addr conflict test: 两节点 preferred_sa=0x80，NAME 0x10000<0x20000\r\n");
    conflict_one(true);       /* 输者支持动态换址 -> 应换到 0x81 */
    board_delay_ms(600);
    conflict_one(false);      /* 输者不支持动态换址 -> 应退 0xFE / CANNOT_CLAIM */
    board_delay_ms(600);

    /* 恢复默认配置，方便继续跑 run */
    j1939_cfg_t cfg;
    __builtin_memset(&cfg, 0, sizeof(cfg));
    cfg.auto_claim = true;
    cfg.preferred_sa = SA_ECU;
    cfg.name = NAME_ECU;
    j1939_init(&s_ecu, &s_bus_ecu, &cfg);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_EEC1, 10U, 3U, fill_eec1, NULL);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_CCVS1, 100U, 6U, fill_ccvs1, NULL);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_ET1, 1000U, 6U, fill_et1, NULL);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_LFE, 1000U, 6U, fill_lfe, NULL);
    (void) j1939_sched_set_enabled(&s_ecu, J1939_PGN_LFE, false);
    j1939_dm_init(&s_ecu);
    __builtin_memset(&cfg, 0, sizeof(cfg));
    cfg.auto_claim = false;
    cfg.preferred_sa = SA_TESTER;
    cfg.name = NAME_TESTER;
    j1939_init(&s_tester, &s_bus_tester, &cfg);
    j1939_set_rx_cb(&s_tester, tester_rx_cb, NULL);
    printf("[j1939] conflict test done, 默认配置已恢复\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 conflict, cmd_conflict, address claim conflict test (U-32-3));

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("j1939_ecu: CAN0=ECU SA=0x%02X, CAN1=TESTER SA=0x%02X, 250 kbit/s, SP=87.5%%\r\n",
           SA_ECU, SA_TESTER);
    printf("NAME=0x%08X%08X  addr_state=%u\r\n", (uint32_t) (NAME_ECU >> 32),
           (uint32_t) (NAME_ECU & 0xFFFFFFFFU), (unsigned) s_ecu.addr_state);
    printf("periodic: EEC1 10ms P3, CCVS1 100ms P6, ET1 1s P6, DM1 1s P6\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, show J1939 node config);

static void cmd_stat(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    j1939_stats_t *st = &s_ecu.stats;
    printf("ECU : sa=0x%02X state=%u rx=%u tx=%u err=%u req=%u/%u claim_rx=%u\r\n",
           s_ecu.sa, (unsigned) s_ecu.addr_state, st->rx_frames, st->tx_frames,
           st->tx_errors, st->req_answered, st->req_rx, st->addr_claims_rx);
    j1939_stats_t *tt = &s_tester.stats;
    printf("TEST: sa=0x%02X rx=%u tx=%u err=%u tp_bam_rx=%u tp_bam_tx=%u tp_rts_rx=%u\r\n",
           s_tester.sa, tt->rx_frames, tt->tx_frames, tt->tx_errors, tt->tp_bam_rx,
           tt->tp_bam_tx, tt->tp_rts_rx);
    printf("ECU tp: bam_tx=%u rts_tx=%u done_rx=%u done_tx=%u bytes_rx=%u bytes_tx=%u "
           "gap_min/max=%u/%u us timeout=%u err=%u\r\n",
           st->tp_bam_tx, st->tp_rts_tx, st->tp_done_rx, st->tp_done_tx,
           st->tp_bytes_rx, st->tp_bytes_tx, st->tp_gap_min_us, st->tp_gap_max_us,
           st->tp_timeouts, st->tp_errors);
    printf("TESTER: rx_total=%u rx_frames=%u unknown_pgn=%u last=0x%05X claim_seen=%u\r\n",
           g_test.rx_total, tt->rx_frames, g_test.unknown_pgn, (unsigned) g_test.last_unknown_pgn,
           (unsigned) g_test.claim_seen);
    printf("req latency min/avg/max = %u/%u/%u us (n=%u)\r\n", st->req_rsp_min_us,
           (st->req_rsp_n != 0U) ? (st->req_rsp_sum_us / st->req_rsp_n) : 0U,
           st->req_rsp_max_us, st->req_rsp_n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stat, dump J1939 stack stats);

static void cmd_fault(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: fault add <spn> <fmi> [oc] | fault clear | fault list\r\n");
        return;
    }
    if (strcmp(argv[1], "add") == 0) {
        uint32_t spn = 0U, fmi = 0U, oc = 1U;
        if (argc >= 4) {
            bsp_parse_u32(argv[2], &spn);
            bsp_parse_u32(argv[3], &fmi);
        }
        if (argc >= 5) {
            bsp_parse_u32(argv[4], &oc);
        }
        if (j1939_dm_add_fault(&s_ecu, spn, (uint8_t) fmi, (uint8_t) oc) == 0) {
            printf("fault added: SPN=%u FMI=%u OC=%u -> next DM1 (1 s)\r\n",
                   (unsigned) spn, (unsigned) fmi, (unsigned) oc);
        } else {
            printf("fault table full (max %u)\r\n", J1939_DTC_MAX);
        }
    } else if (strcmp(argv[1], "clear") == 0) {
        j1939_dm_clear_active(&s_ecu);
        printf("active DTC cleared (DM1 lamps off); history kept for DM2\r\n");
    } else if (strcmp(argv[1], "list") == 0) {
        printf("active DTC: %u\r\n", s_ecu.dtc_count);
        for (uint8_t i = 0U; i < s_ecu.dtc_count; i++) {
            printf("  [%u] SPN=%u FMI=%u OC=%u lamp=%u\r\n", i, s_ecu.dtc[i].spn,
                   s_ecu.dtc[i].fmi, s_ecu.dtc[i].oc, s_ecu.dtc[i].lamp);
        }
        printf("history DTC: %u\r\n", s_ecu.dtc_hist_count);
    } else {
        printf("unknown subcommand\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 fault, cmd_fault, inject/clear DM1 faults: fault add <spn> <fmi> [oc]|clear|list);

static void cmd_dm1(int argc, char *argv[])
{
    uint8_t buf[64];
    (void) argc;
    (void) argv;
    uint16_t n = j1939_dm_build(&s_ecu, J1939_PGN_DM1, buf);
    printf("DM1 len=%u:", n);
    for (uint16_t i = 0U; i < n; i++) {
        printf(" %02X", buf[i]);
    }
    dm_decode_t d;
    dm_decode(buf, n, &d);
    printf("  lamp=0x%02X MIL=%u RSL=%u AWL=%u PL=%u dtc=%u", d.lamp, d.mil, d.rsl, d.awl,
           d.pl, d.dtc_n);
    if (d.dtc_n > 0U) {
        printf(" SPN=%u FMI=%u OC=%u", d.spn[0], d.fmi[0], d.oc[0]);
    }
    printf("\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dm1, cmd_dm1, encode and decode current DM1);

static void cmd_req(int argc, char *argv[])
{
    uint32_t pgn = 0U;
    if (argc < 2) {
        printf("usage: req <pgn_hex>\r\n");
        return;
    }
    bsp_parse_u32(argv[1], &pgn);
    tester_request(pgn);
    uint32_t t0 = j1939_micros();
    while (g_test.req_pending && ((j1939_micros() - t0) < 200000U)) {
        j1939_poll(&s_ecu);
        j1939_poll(&s_tester);
        bsp_shell_poll();
    }
    if (g_test.req_pending) {
        g_test.req_pending = false;
        printf("request PGN 0x%X: TIMEOUT (200 ms)\r\n", (unsigned) pgn);
    } else {
        printf("request PGN 0x%X answered, last latency=%u us\r\n", (unsigned) pgn,
               g_test.req_lat_max);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 req, cmd_req, request a PGN from ECU: req <pgn_hex>);

static void cmd_mon(int argc, char *argv[])
{
    uint32_t ms = 1000U;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &ms);
    }
    if (ms > 5000U) {
        ms = 5000U; /* 串口打印慢，限 5 s */
    }
    printf("monitor %u ms (print each ECU frame decoded)\r\n", (unsigned) ms);
    g_test.monitor = true;
    g_test.mon_frames = 0U;
    g_test.boot_us = j1939_micros();
    pump_us(ms * 1000U);
    g_test.monitor = false;
    printf("monitor done: %u frames printed\r\n", g_test.mon_frames);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mon, cmd_mon, monitor bus for N ms);
#ifdef J1939_WITH_GW
/* ---------------- Bench B 网关命令 ---------------- */

static void cmd_gw(int argc, char *argv[])
{
    const char *sub = (argc >= 2) ? argv[1] : "stat";
    if (strcmp(sub, "on") == 0) {
        app_can_gw_set_forward(true);
        printf("[gw] forwarding ON, peer=%s\r\n", app_can_gw_peer_str());
    } else if (strcmp(sub, "off") == 0) {
        app_can_gw_set_forward(false);
        printf("[gw] forwarding OFF\r\n");
    } else {
        app_can_gw_print();
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gw, cmd_gw, cannelloni gateway: gw on|off|stat);
#endif

/* ---------------- 主流程 ---------------- */

int main(void)
{
    bsp_init();
#ifdef J1939_WITH_GW
    printf("\r\n[boot] j1939_ecu GW: CAN0=ECU SA=0x00 @500k, CAN1=app_can_gw UDP4000\r\n");
    board_can_leds_init();
    board_can_init_pins();
    board_can_transceivers_enable();

    int lwrc = app_lwip_init(NULL);
    printf("[gw] lwip init rc=%d\r\n", lwrc);

    app_can_gw_cfg_t gwcfg;
    __builtin_memset(&gwcfg, 0, sizeof(gwcfg));
    gwcfg.local_ip = NULL;
    gwcfg.peer_ip = NULL;
    gwcfg.peer_port = 0;
    gwcfg.bus_speed = APP_CAN_SPEED_CLASSIC_500K; /* app_can 只提供 500k / FD 档位 */
    gwcfg.max_frames_per_pkt = 16U;
    gwcfg.ch_count = 1;
    gwcfg.ch[0].can_ch = 1U;
    gwcfg.ch[0].udp_port = 4000U;
    gwcfg.ch[0].enabled = true;
    int gwrc = (lwrc >= 0) ? app_can_gw_init(&gwcfg) : -100;
    printf("[gw] init rc=%d peer=%s\r\n", gwrc, app_can_gw_peer_str());

    if (j1939_bus_init(&s_bus_ecu, BOARD_CAN0_BASE, s_buf_can0, 500000U) != 0) {
        printf("[FAIL] CAN0 init\r\n");
    }
    j1939_cfg_t cfg;
    __builtin_memset(&cfg, 0, sizeof(cfg));
    cfg.auto_claim = true;
    cfg.preferred_sa = SA_ECU;
    cfg.name = NAME_ECU;
    j1939_init(&s_ecu, &s_bus_ecu, &cfg);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_EEC1, 10U, 3U, fill_eec1, NULL);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_CCVS1, 100U, 6U, fill_ccvs1, NULL);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_ET1, 1000U, 6U, fill_et1, NULL);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_LFE, 1000U, 6U, fill_lfe, NULL);
    (void) j1939_sched_set_enabled(&s_ecu, J1939_PGN_LFE, false);
    j1939_dm_init(&s_ecu);
    (void) j1939_dm_add_fault(&s_ecu, 100U, 4U, 1U); /* 给上位机一个可见的 DM1 */

    uint32_t t0 = j1939_millis();
    while ((j1939_millis() - t0) < 1500U) {
        bsp_shell_poll();
        app_lwip_poll();
        app_can_gw_poll();
        j1939_poll(&s_ecu);
    }
    printf("[pass] j1939 gw ready: claim_sa=0x%02X eec1=%u bus=500k udp=4000 lwip=%d gw=%d\r\n",
           s_ecu.sa, s_ecu.sched[0].sent, lwrc, gwrc);
    printf("type 'gw on|off|stat|fault add <spn> <fmi>|dm1|stat|info'\r\n");
    while (1) {
        bsp_shell_poll();
        app_lwip_poll();
        app_can_gw_poll();
        j1939_poll(&s_ecu);
    }
#else
    printf("\r\n[boot] j1939_ecu: CAN0=ECU SA=0x00, CAN1=TESTER SA=0xF9, 250 kbit/s\r\n");
    board_can_leds_init();
    board_can_init_pins();
    board_can_transceivers_enable();

    if (j1939_bus_init(&s_bus_ecu, BOARD_CAN0_BASE, s_buf_can0, 250000U) != 0) {
        printf("[FAIL] CAN0 init\r\n");
    }
    if (j1939_bus_init(&s_bus_tester, BOARD_CAN1_BASE, s_buf_can1, 250000U) != 0) {
        printf("[FAIL] CAN1 init\r\n");
    }

    j1939_cfg_t cfg;
    __builtin_memset(&cfg, 0, sizeof(cfg));
    cfg.auto_claim = true;
    cfg.preferred_sa = SA_ECU;
    cfg.name = NAME_ECU;
    j1939_init(&s_ecu, &s_bus_ecu, &cfg);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_EEC1, 10U, 3U, fill_eec1, NULL);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_CCVS1, 100U, 6U, fill_ccvs1, NULL);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_ET1, 1000U, 6U, fill_et1, NULL);
    (void) j1939_sched_add(&s_ecu, J1939_PGN_LFE, 1000U, 6U, fill_lfe, NULL);
    (void) j1939_sched_set_enabled(&s_ecu, J1939_PGN_LFE, false); /* 可选 PGN 默认关 */
    j1939_dm_init(&s_ecu);

    __builtin_memset(&cfg, 0, sizeof(cfg));
    cfg.auto_claim = false;
    cfg.preferred_sa = SA_TESTER;
    cfg.name = NAME_TESTER;
    j1939_init(&s_tester, &s_bus_tester, &cfg);
    j1939_set_rx_cb(&s_tester, tester_rx_cb, NULL);

    run_tests();
    printf("type 'run|stat|fault add <spn> <fmi>|fault clear|dm1|req <pgn>|mon <ms>|info'\r\n");
    while (1) {
        bsp_shell_poll();
        j1939_poll(&s_ecu);
        j1939_poll(&s_tester);
    }
#endif
    return 0;
}