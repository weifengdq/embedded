/**
 * @file  s2500_shell.c
 * @brief S2500 letter-shell 命令集
 *
 * 命令前缀统一为 s2500, 便于与网络命令 (ping/iperf/...) 区分。
 * 全部命令都直接读写芯片寄存器, 因此同一套命令可跑在任意 MCU 上。
 */
#include "s2500_shell.h"
#include "s2500_port.h"
#include "shell.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static Shell s_shell;
static char  s_shell_buf[512];
static s2500_dev_t *s_dev;

/* ------------------------------------------------------------------ */
/* shell 移植                                                         */
/* ------------------------------------------------------------------ */
static signed short sh_write(char *data, unsigned short len)
{
    s2500_port_uart_write(data, len);
    return (signed short)len;
}

static signed short sh_read(char *data, unsigned short len)
{
    int c = s2500_port_uart_getc();
    if (c < 0) {
        return 0;
    }
    data[0] = (char)c;
    (void)len;
    return 1;
}

void s2500_shell_init(s2500_dev_t *dev)
{
    s_dev = dev;
    shellInit(&s_shell, s_shell_buf, sizeof(s_shell_buf));
    s_shell.read = sh_read;
    s_shell.write = sh_write;
#if SHELL_USING_LOCK == 1
    s_shell.lock = NULL;
    s_shell.unlock = NULL;
#endif
}

void s2500_shell_poll(void)
{
    int c;
    int guard = 0;
    while ((c = s2500_port_uart_getc()) >= 0 && guard++ < 64) {
        shellHandler(&s_shell, (char)c);
    }
}

void s2500_shell_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        s2500_port_uart_write(buf, (unsigned int)n);
    }
}

#define P(fmt, ...) s2500_shell_printf(fmt, ##__VA_ARGS__)

/* ------------------------------------------------------------------ */
/* 工具函数                                                           */
/* ------------------------------------------------------------------ */
static int parse_u32(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;
    if (!s) return -1;
    v = strtoul(s, &end, 0);
    if (end == s) return -1;
    *out = (uint32_t)v;
    return 0;
}

static int parse_mac(const char *s, uint8_t mac[6])
{
    unsigned int b[6];
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return -1;
    }
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)b[i];
    return 0;
}

static const char *short_open_str(uint8_t v)
{
    switch (v) {
    case 0: return "OK (无故障)";
    case 1: return "OPEN / 终端缺失";
    case 2: return "SHORT / 多余终端";
    default: return "未知故障";
    }
}

/* ------------------------------------------------------------------ */
/* s2500 主命令                                                       */
/* ------------------------------------------------------------------ */
static void cmd_info(void)
{
    s2500_dev_t *d = s_dev;
    uint32_t idver=0, phyid=0, cap=0, onip=0, onic=0, health=0, physts=0;
    uint32_t plca0=0, plca1=0, pstat=0, ext=0, ws1=0, cfg0=0, dio=0;

    s2500_read_reg(d, S2500_MMS_STD, S2500_REG_IDVER, &idver);
    s2500_read_reg(d, S2500_MMS_STD, S2500_REG_PHYID, &phyid);
    s2500_read_reg(d, S2500_MMS_STD, S2500_REG_STDCAP, &cap);
    s2500_read_reg(d, S2500_MMS_STD, S2500_REG_CONFIG0, &cfg0);
    s2500_read_reg(d, S2500_MMS_PLCA, S2500_REG_ONIPVER, &onip);
    s2500_read_reg(d, S2500_MMS_VENDOR, S2500_REG_ONICVER, &onic);
    s2500_read_reg(d, S2500_MMS_VENDOR, S2500_REG_NVM_HEALTH, &health);
    s2500_read_reg(d, S2500_MMS_STD, S2500_REG_PHY_STATUS, &physts);
    s2500_read_reg(d, S2500_MMS_PLCA, S2500_REG_PLCACTL0, &plca0);
    s2500_read_reg(d, S2500_MMS_PLCA, S2500_REG_PLCACTL1, &plca1);
    s2500_read_reg(d, S2500_MMS_PLCA, S2500_REG_PLCASTATUS, &pstat);
    s2500_read_reg(d, S2500_MMS_PLCA, S2500_REG_PLCAEXT, &ext);
    s2500_read_reg(d, S2500_MMS_PLCA, S2500_REG_WS_STATUS1, &ws1);
    s2500_read_reg(d, S2500_MMS_VENDOR, S2500_REG_DIOCFG0, &dio);

    P("\r\n=========== T30HM1TS2500 (S2500) ===========\r\n");
    P(" IDVER      : 0x%08X  (MAJVER=%u MINVER=%u)\r\n", idver, (idver >> 4) & 0xF, idver & 0xF);
    P(" PHYID      : 0x%08X  (OUI=0x%06X Model=%u REV=%u)\r\n", phyid,
      ((phyid >> 10) << 2), (phyid >> 4) & 0x3F, phyid & 0xF);
    P(" STDCAP     : 0x%08X  [CTC=%u FTC=%u AIDC=%u DPRAC=%u CPS_min=%u]\r\n", cap,
      (cap >> 7) & 1, (cap >> 6) & 1, (cap >> 5) & 1, (cap >> 8) & 1, cap & 7);
    P(" CONFIG0    : 0x%08X  [SYNC=%u ZARFE=%u CPS=%u TXCTE=%u RXCTE=%u FTSE=%u]\r\n", cfg0,
      (cfg0 >> 15) & 1, (cfg0 >> 12) & 1, cfg0 & 7, (cfg0 >> 9) & 1, (cfg0 >> 8) & 1, (cfg0 >> 7) & 1);
    P(" MAC        : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
      d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5]);
    P(" PHY STATUS : 0x%04X  link=%u speed10=%u\r\n", physts,
      (physts >> 2) & 1, (physts >> 11) & 1);
    P(" PLCA       : en=%u id=%u ncnt=%u PST=%u CRPS=%u prec=%u CREN=%u\r\n",
      (plca0 >> 15) & 1, plca1 & 0xFF, (plca1 >> 8) & 0xFF,
      (pstat >> 15) & 1, (ext >> 4) & 1, (ext >> 15) & 1, (ext >> 2) & 1);
    P(" ONIPVER    : 0x%04X  ONICVER=0x%08X  NVM_HEALTH=0x%08X\r\n", onip, onic, health);
    P(" DIOCFG0    : 0x%08X  DIO0.FN=%u DIO1.FN=%u\r\n", dio, (dio >> 1) & 0xF, (dio >> 9) & 0xF);
    P(" WS_STATUS1 : 0x%04X (POR_VCC=%u POR_VIO=%u RST_IN=%u TSD=%u WUT=%u WPIN=%u)\r\n",
      ws1, (ws1 >> 15) & 1, (ws1 >> 14) & 1, (ws1 >> 12) & 1, (ws1 >> 9) & 1,
      (ws1 >> 8) & 1, (ws1 >> 7) & 1);
    P("=============================================\r\n");
}

static void cmd_status(void)
{
    s2500_status_t st;
    uint32_t coll = 0, rjab = 0;
    if (s2500_read_status(s_dev, &st) != S2500_OK) {
        P("read status failed\r\n");
        return;
    }
    s2500_read_pcs_counters(s_dev, &coll, &rjab);
    P("link      : %s\r\n", st.link_up ? "UP" : "DOWN");
    P("plca      : %s id=%u ncnt=%u PST=%u CRPS=%u\r\n",
      st.plca_en ? "ON" : "OFF", st.plca_id, st.plca_ncnt, st.plca_pst, st.plca_crps);
    P("sqi       : %u/7  sqi+ = %u/255  (update %u/%u)\r\n",
      st.sqi, st.sqip, st.sqi_valid, st.sqip_valid);
    P("plca diag : 0x%X [RXINTO=%u UNEXPB=%u BCNBFTO=%u]\r\n", st.plca_diag,
      (st.plca_diag >> 2) & 1, (st.plca_diag >> 1) & 1, st.plca_diag & 1);
    P("pcs       : collisions=%lu remote_jabber=%lu (read-clear)\r\n",
      (unsigned long)coll, (unsigned long)rjab);
    P("drv stats : tx ok=%lu bytes=%lu chunks=%lu wait=%lu drop=%lu | rx ok=%lu bytes=%lu chunks=%lu fcs=%lu\r\n",
      (unsigned long)s_dev->stats.tx_frames, (unsigned long)s_dev->stats.tx_bytes,
      (unsigned long)s_dev->stats.tx_chunks, (unsigned long)s_dev->stats.tx_busy_waits,
      (unsigned long)s_dev->stats.tx_dropped,
      (unsigned long)s_dev->stats.rx_frames, (unsigned long)s_dev->stats.rx_bytes,
      (unsigned long)s_dev->stats.rx_chunks, (unsigned long)s_dev->stats.rx_fcs_err);
}

static const char *wd_reason_str(uint8_t r)
{
    switch (r) {
    case S2500_WD_REASON_ID:    return "PLCA_CTL1.ID != 配置";
    case S2500_WD_REASON_NCNT:  return "PLCA_CTL1.NCNT != 配置";
    case S2500_WD_REASON_SYNC:  return "CONFIG0.SYNC 丢失";
    case S2500_WD_REASON_IDVER: return "IDVER != 0x11";
    default:                    return "-";
    }
}

static void cmd_wd(int argc, char **argv)
{
    s2500_wd_t *w = s2500_wd_state();
    if (argc >= 3) {
        if (!strcmp(argv[2], "on")) {
            s2500_wd_set_enabled(1);
            P("S2500 看门狗: on (周期 %u ms)\r\n", (unsigned)w->period_ms);
            return;
        }
        if (!strcmp(argv[2], "off")) {
            s2500_wd_set_enabled(0);
            P("S2500 看门狗: off\r\n");
            return;
        }
        if (!strcmp(argv[2], "reset")) {
            s2500_wd_reset();
            P("S2500 看门狗统计已清零\r\n");
            return;
        }
        P("usage: s2500 wd [on|off|reset]\r\n");
        return;
    }
    P("watchdog   : %s  period=%u ms\r\n",
      w->enabled ? "on" : "off", (unsigned)w->period_ms);
    P("checks     : %lu  detections=%lu  reinit ok=%lu fail=%lu  spi_err=%lu\r\n",
      (unsigned long)w->checks, (unsigned long)w->detections,
      (unsigned long)w->reinit_ok, (unsigned long)w->reinit_fail,
      (unsigned long)w->spi_errors);
    if (w->detections) {
        P("last reset : detect @%lu ms, recovered @%lu ms, reason=%s\r\n",
          (unsigned long)w->last_detect_ms, (unsigned long)w->last_recover_ms,
          wd_reason_str(w->last_reason));
    } else {
        P("last reset : -\r\n");
    }
}

static void cmd_counters(int clear)
{
    uint32_t c[35];
    if (s2500_read_counters(s_dev, c) != S2500_OK) {
        P("read counters failed\r\n");
        return;
    }
    /* 32 位平台没有 %llu: 64 位 octets 拆成高/低 32 位打印 */
    P("TX: octets=%lu%010lu frames=%lu bcast=%lu mcast=%lu\r\n",
      (unsigned long)c[1], (unsigned long)c[0], (unsigned long)c[2],
      (unsigned long)c[3], (unsigned long)c[4]);
    P("    64B=%lu 65-127=%lu 128-255=%lu 256-511=%lu 512-1023=%lu 1024+=%lu\r\n",
      (unsigned long)c[5], (unsigned long)c[6], (unsigned long)c[7],
      (unsigned long)c[8], (unsigned long)c[9], (unsigned long)c[10]);
    P("    underflow=%lu single_col=%lu multi_col=%lu excess_col=%lu deferred=%lu crs_err=%lu\r\n",
      (unsigned long)c[11], (unsigned long)c[12], (unsigned long)c[13],
      (unsigned long)c[14], (unsigned long)c[15], (unsigned long)c[16]);
    P("RX: octets=%lu%010lu frames=%lu bcast=%lu mcast=%lu\r\n",
      (unsigned long)c[18], (unsigned long)c[17], (unsigned long)c[19],
      (unsigned long)c[20], (unsigned long)c[21]);
    P("    64B=%lu 65-127=%lu 128-255=%lu 256-511=%lu 512-1023=%lu 1024+=%lu\r\n",
      (unsigned long)c[22], (unsigned long)c[23], (unsigned long)c[24],
      (unsigned long)c[25], (unsigned long)c[26], (unsigned long)c[27]);
    P("    runts=%lu toolong=%lu fcserr=%lu symbol=%lu align=%lu overflow=%lu filtered=%lu\r\n",
      (unsigned long)c[28], (unsigned long)c[29], (unsigned long)c[30],
      (unsigned long)c[31], (unsigned long)c[32], (unsigned long)c[33], (unsigned long)c[34]);
    if (clear) {
        s2500_clear_counters(s_dev);
        P("counters cleared\r\n");
    }
}

static void cmd_reg(int argc, char **argv)
{
    uint32_t mms, addr, val;
    if (argc < 4) {
        P("usage: s2500 reg <mms> <addr> [value]\r\n");
        return;
    }
    if (parse_u32(argv[2], &mms) || parse_u32(argv[3], &addr)) {
        P("bad number\r\n");
        return;
    }
    if (argc >= 5) {
        if (parse_u32(argv[4], &val)) { P("bad value\r\n"); return; }
        if (s2500_write_reg(s_dev, (uint8_t)mms, (uint16_t)addr, val) != S2500_OK) {
            P("write failed\r\n"); return;
        }
        P("W mms%lu[0x%04lX] <= 0x%08lX\r\n", (unsigned long)mms, (unsigned long)addr, (unsigned long)val);
    }
    if (s2500_read_reg(s_dev, (uint8_t)mms, (uint16_t)addr, &val) != S2500_OK) {
        P("read failed\r\n"); return;
    }
    P("R mms%lu[0x%04lX] = 0x%08lX (%lu)\r\n", (unsigned long)mms, (unsigned long)addr,
      (unsigned long)val, (unsigned long)val);
}

static void cmd_plca(int argc, char **argv)
{
    uint32_t id = s_dev->plca_id, ncnt = s_dev->plca_ncnt, totmr = 32;
    int en = -1, cr = -1, prec = -1, i;
    for (i = 2; i < argc - 1; i++) {
        uint32_t v;
        if (parse_u32(argv[i + 1], &v)) continue;
        if (!strcmp(argv[i], "id")) { id = v; }
        else if (!strcmp(argv[i], "ncnt")) { ncnt = v; }
        else if (!strcmp(argv[i], "totmr")) { totmr = v; }
        else if (!strcmp(argv[i], "en")) { en = (int)v; }
        else if (!strcmp(argv[i], "cr")) { cr = (int)v; }
        else if (!strcmp(argv[i], "prec")) { prec = (int)v; }
    }
    if (id > 255) id = 255;
    if (ncnt == 0 || ncnt > 255) ncnt = 8;
    if (en < 0) en = 1;      /* 配置时默认开启 */
    if (s2500_plca_configure(s_dev, (uint8_t)id, (uint8_t)ncnt, (uint8_t)totmr, en) != S2500_OK) {
        P("plca configure failed\r\n"); return;
    }
    if (cr >= 0) s2500_plca_redundancy(s_dev, cr);
    if (prec >= 0) s2500_plca_precedence(s_dev, prec);
    P("PLCA id=%lu ncnt=%lu totmr=%lu en=%d cr=%d prec=%d\r\n",
      (unsigned long)id, (unsigned long)ncnt, (unsigned long)totmr, en, cr, prec);
}

static void cmd_sqi(void)
{
    uint8_t sqi = 0, sqip = 0;
    uint32_t toid = 0;
    s2500_read_reg(s_dev, S2500_MMS_PLCA, S2500_REG_DCQ_TOID, &toid);
    s2500_read_sqi(s_dev, &sqi, &sqip);
    P("SQI=%u/7 SQI+=%u/255  (TOID=%lu, 255=全部流量)\r\n",
      sqi, sqip, (unsigned long)(toid & 0xFF));
}

static void cmd_hdd(void)
{
    s2500_hdd_result_t r;
    P("HDD 测量中 (链路需空闲, 建议先关闭 PLCA 协调者 BEACON)...\r\n");
    if (s2500_hdd_run(s_dev, &r, 3000) != S2500_OK) {
        P("HDD 超时/失败\r\n");
        return;
    }
    P("HDD valid=%u ready=%u SHORT_OPEN=%u (%s) 1W_SHORT=%u\r\n",
      r.valid, r.ready, r.short_open, short_open_str(r.short_open), r.one_wire);
}

static void cmd_td(int argc, char **argv)
{
    s2500_td_result_t req, res;
    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    req.dm_dur_ms = 8;
    if (argc >= 3) {
        if (!strcmp(argv[2], "ref")) { req.auto_mode = 1; req.dist_mr = 1; }
        else if (!strcmp(argv[2], "meas")) { req.auto_mode = 1; req.dist_mr = 0; }
        else if (!strcmp(argv[2], "dly")) { req.auto_mode = 0; req.delay_only = 1; }
        else { P("usage: s2500 td <ref|meas|dly> [dur_ms]\r\n"); return; }
    } else {
        P("usage: s2500 td <ref|meas|dly> [dur_ms]\r\n");
        return;
    }
    if (argc >= 4) {
        uint32_t d;
        if (!parse_u32(argv[3], &d) && d >= 1 && d <= 16) req.dm_dur_ms = (uint8_t)d;
    }
    {
        /* 拓扑发现期间会改写 PLCA/TD 寄存器, 临时关闭看门狗避免误判 */
        int wd_prev = s2500_wd_enabled();
        int rc;
        s2500_wd_set_enabled(0);
        rc = s2500_td_run(s_dev, &req, &res, (uint32_t)(req.dm_dur_ms * 3 + 4000));
        s2500_wd_set_enabled(wd_prev);
        if (rc != S2500_OK) {
            P("TD failed/timeout\r\n"); return;
        }
    }
    P("TD: dur=%u ms dist_mr=%lu dly_mr=%lu mndly_mr=%lu mndly_dur=%u err(dm=%u dly=%u auto=%u)\r\n",
      res.dm_dur_ms, (unsigned long)res.dist_mr, (unsigned long)res.dly_mr,
      (unsigned long)res.mndly_mr, res.mndly_dur_ms, res.dm_err, res.dlym_err, res.auto_err);
}

static void cmd_time(int argc, char **argv)
{
    uint32_t sec = 0, nsec = 0;
    if (argc >= 4 && !strcmp(argv[2], "set")) {
        if (parse_u32(argv[3], &sec) || (argc >= 5 && parse_u32(argv[4], &nsec))) {
            P("bad number\r\n"); return;
        }
        s2500_time_set(s_dev, sec, nsec);
        P("time set to %lu.%09lu\r\n", (unsigned long)sec, (unsigned long)nsec);
        return;
    }
    s2500_time_read(s_dev, &sec, &nsec);
    P("PTP time: %lu.%09lu s\r\n", (unsigned long)sec, (unsigned long)nsec);
}

static void cmd_ts(int argc, char **argv)
{
    uint64_t ts = 0;
    uint8_t idx = 0;
    if (argc < 3) { P("usage: s2500 ts <a|b|c>\r\n"); return; }
    if (!strcmp(argv[2], "tx")) {
        /* TC6 发送时间戳实测: 打开 FTSE, 用带 TSC 的帧发送, 等 TTSC 可用后读回 */
        uint8_t frame[1600];
        uint32_t len = 60, i, flags, c0 = 0, st = 0;
        uint8_t ti = 0;
        int rc;
        if (argc >= 4) ti = (uint8_t)((argv[3][0] == 'b') ? 1 : (argv[3][0] == 'c') ? 2 : 0);
        if (argc >= 5) (void)parse_u32(argv[4], &len);
        if (len < 60 || len > 1500) len = 60;
        flags = (ti == 1) ? S2500_XMIT_TS_B : (ti == 2) ? S2500_XMIT_TS_C : S2500_XMIT_TS_A;
        s2500_read_reg(s_dev, S2500_MMS_STD, S2500_REG_CONFIG0, &c0);
        s2500_write_reg(s_dev, S2500_MMS_STD, S2500_REG_CONFIG0, c0 | S2500_CONFIG0_FTSE);
        /* 清三个 TTSC 可用标志 (写 1 清) */
        s2500_write_reg(s_dev, S2500_MMS_STD, S2500_REG_STATUS0,
                        S2500_STATUS0_TTSCAA | S2500_STATUS0_TTSCAB | S2500_STATUS0_TTSCAC);
        memset(frame, 0xFF, 6);
        memcpy(&frame[6], s_dev->mac, 6);
        frame[12] = 0x88; frame[13] = 0xB5;
        for (i = 14; i < len; i++) frame[i] = (uint8_t)i;
        rc = s2500_send(s_dev, frame, (uint16_t)len, flags);
        for (i = 0; i < 100; i++) {          /* 最多等 100 ms */
            s2500_read_reg(s_dev, S2500_MMS_STD, S2500_REG_STATUS0, &st);
            if (st & (S2500_STATUS0_TTSCAA << ti)) break;
            if (s_dev->hal->delay_us) s_dev->hal->delay_us(s_dev->hal->ctx, 1000);
        }
        s2500_ttsc_read(s_dev, ti, &ts);
        s2500_write_reg(s_dev, S2500_MMS_STD, S2500_REG_CONFIG0, c0);
        P("ts tx: rc=%d len=%lu tsc=%lu status0=0x%08lX avail=%lu TTSC%c=0x%08lX%08lX sec=%lu ns=%lu\r\n",
          rc, (unsigned long)len, (unsigned long)ti, (unsigned long)st,
          (unsigned long)((st >> (8 + ti)) & 1), 'A' + ti,
          (unsigned long)(uint32_t)(ts >> 32), (unsigned long)(uint32_t)ts,
          (unsigned long)(ts >> 32), (unsigned long)(ts & 0x3FFFFFFFu));
        return;
    }
    idx = (uint8_t)((argv[2][0] == 'b') ? 1 : (argv[2][0] == 'c') ? 2 : 0);
    if (s2500_ttsc_read(s_dev, idx, &ts) != S2500_OK) { P("read failed\r\n"); return; }
    /* newlib-nano 的 printf 不支持 %llX, 拆成两个 32 位打印 */
    P("TTSC%c = 0x%08lX%08lX  sec=%lu ns=%lu\r\n", 'A' + idx,
      (unsigned long)(uint32_t)(ts >> 32), (unsigned long)(uint32_t)ts,
      (unsigned long)(ts >> 32), (unsigned long)(ts & 0x3FFFFFFFu));
}

static void cmd_loop(int argc, char **argv)
{
    uint32_t mode = 0;
    if (argc >= 3) parse_u32(argv[2], &mode);
    s2500_loopback(s_dev, (uint8_t)mode);
    P("loopback mode=%lu (0=off 1=PCS 2=PHY)\r\n", (unsigned long)mode);
}

static void cmd_led(int argc, char **argv)
{
    uint32_t fn0 = S2500_DIOFN_LED_TXRX, fn1 = S2500_DIOFN_LED_PLCASTS;
    if (argc >= 3) {
        if (!strcmp(argv[2], "off")) {
            s2500_led_config(s_dev, S2500_DIOFN_DISABLE, S2500_DIOFN_DISABLE);
            P("LEDs off\r\n"); return;
        }
        if (parse_u32(argv[2], &fn0)) { P("bad fn\r\n"); return; }
    }
    if (argc >= 4) parse_u32(argv[3], &fn1);
    s2500_led_config(s_dev, (uint8_t)fn0, (uint8_t)fn1);
    P("DIO0(ACT)=FN%lu DIO1(LINK)=FN%lu\r\n", (unsigned long)fn0, (unsigned long)fn1);
}

static void cmd_sleep(int argc, char **argv)
{
    int wd_prev = s2500_wd_enabled();
    /* Sleep 期间芯片不响应 SPI, 关闭看门狗避免误判; 命令结束后恢复 */
    s2500_wd_set_enabled(0);
    if (argc >= 3 && !strcmp(argv[2], "wake")) {
        s2500_send_wake_tone(s_dev);
        s2500_wd_set_enabled(wd_prev);
        P("wake tone sent (需 LCTL=1)\r\n");
        return;
    }
    P("进入 Sleep (LPREQ)...\r\n");
    s2500_sleep(s_dev);
    s2500_wd_set_enabled(wd_prev);
}

static void cmd_analog(int argc, char **argv)
{
    uint32_t amp = 0, edth = 3, edthcd = 0xB, slw = 1, clko = 0, cmc = 0;
    if (argc >= 3) parse_u32(argv[2], &amp);
    if (argc >= 4) parse_u32(argv[3], &edth);
    if (argc >= 5) parse_u32(argv[4], &edthcd);
    if (argc >= 6) parse_u32(argv[5], &slw);
    if (argc >= 7) parse_u32(argv[6], &clko);
    if (argc >= 8) parse_u32(argv[7], &cmc);
    if (s2500_analog_config(s_dev, (uint8_t)amp, (uint8_t)edth, (uint8_t)edthcd,
                            (uint8_t)slw, (uint8_t)clko, (uint8_t)cmc) != S2500_OK) {
        P("write failed\r\n"); return;
    }
    P("TWEAKS2 updated: tx_amp=%lu edth=%lu edthcd=%lu tx_slw=%lu clko=%lu cmc=%lu\r\n",
      (unsigned long)amp, (unsigned long)edth, (unsigned long)edthcd,
      (unsigned long)slw, (unsigned long)clko, (unsigned long)cmc);
}

static void cmd_mac(int argc, char **argv)
{
    uint8_t mac[6];
    if (argc >= 3) {
        if (parse_mac(argv[2], mac)) { P("bad mac\r\n"); return; }
        s2500_set_mac(s_dev, mac);
        P("MAC set\r\n");
        return;
    }
    P("MAC=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
      s_dev->mac[0], s_dev->mac[1], s_dev->mac[2], s_dev->mac[3], s_dev->mac[4], s_dev->mac[5]);
}

static void cmd_irq(void)
{
    uint32_t v = 0;
    s2500_read_irq_status(s_dev, &v);
    P("PHYIRQS=0x%08lX [LSI=%lu PSI=%lu LJI=%lu RJI=%lu PRI=%lu PCI=%lu CRDI=%lu]\r\n",
      (unsigned long)v, (unsigned long)(v & 1), (unsigned long)((v >> 1) & 1),
      (unsigned long)((v >> 2) & 1), (unsigned long)((v >> 3) & 1),
      (unsigned long)((v >> 4) & 1), (unsigned long)((v >> 5) & 1),
      (unsigned long)((v >> 6) & 1));
}

static void cmd_rxdump(void)
{
    static uint8_t f[1600];
    uint16_t len = 0;
    int rc = s2500_recv(s_dev, f, &len, NULL);
    if (rc != S2500_OK) {
        P("no frame (rc=%d)\r\n", rc);
        return;
    }
    P("rx len=%u\r\n", len);
    for (int i = 0; i < len && i < 64; i++) {
        P("%02X%s", f[i], ((i & 15) == 15) ? "\r\n" : " ");
    }
    P("\r\n");
}

/* 执行一次数据块事务并解码 footer, 用于调试 OA-SPI 时序 */
static void cmd_lbdump(int argc, char **argv)
{
    uint32_t n = 1, i;
    if (argc >= 3) parse_u32(argv[2], &n);
    for (i = 0; i < n; i++) {
        int rc = s2500_raw_block_dump(s_dev, 1);
        if (rc != S2500_OK) {
            P("block %lu: read failed (%d)\r\n", (unsigned long)i, rc);
            break;
        }
    }
}

static void cmd_bench(int argc, char **argv)
{
    static uint8_t frame[1514];
    uint32_t count = 100, len = 1000, i;
    if (argc >= 4 && !strcmp(argv[2], "tx")) {
        parse_u32(argv[3], &count);
        if (argc >= 5) parse_u32(argv[4], &len);
        if (len < 60 || len > 1500) { P("len 60..1500\r\n"); return; }
        memset(frame, 0x5A, sizeof(frame));
        /* 目的地址: 广播; 源地址: 本机; 类型 0x88B5 (实验以太网类型) */
        memset(frame, 0xFF, 6);
        memcpy(&frame[6], s_dev->mac, 6);
        frame[12] = 0x88; frame[13] = 0xB5;
        uint32_t t0 = s_dev->hal->millis ? s_dev->hal->millis(s_dev->hal->ctx) : 0;
        uint32_t ok = 0;
        for (i = 0; i < count; i++) {
            if (s2500_send(s_dev, frame, (uint16_t)len, 0) == S2500_OK) ok++;
        }
        uint32_t t1 = s_dev->hal->millis ? s_dev->hal->millis(s_dev->hal->ctx) : 0;
        uint32_t dt = t1 - t0;
        /* 避免 %f: newlib-nano 默认不链接浮点 printf */
        uint32_t fps = dt ? (ok * 1000u) / dt : 0;
        uint32_t kbps = dt ? (ok * (uint32_t)len * 8u) / dt : 0;   /* bit/ms = kbit/s */
        P("bench tx: %lu/%lu frames len=%lu dt=%lu ms => %lu fps, %lu kbit/s\r\n",
          (unsigned long)ok, (unsigned long)count, (unsigned long)len, (unsigned long)dt,
          (unsigned long)fps, (unsigned long)kbps);
        return;
    }
    P("usage: s2500 bench tx <count> [len]\r\n");
}

static void cmd_selftest(void)
{
    uint32_t idver = 0, phyid = 0, physts = 0;
    int rc;
    s2500_status_t st;
    s2500_hdd_result_t hdd;

    P("\r\n===== S2500 上电自检 =====\r\n");
    rc = s2500_read_reg(s_dev, S2500_MMS_STD, S2500_REG_IDVER, &idver);
    P("[1] SPI/寄存器   : %s (IDVER=0x%08lX)\r\n",
      (rc == S2500_OK && idver == 0x11) ? "PASS" : "FAIL", (unsigned long)idver);
    s2500_read_reg(s_dev, S2500_MMS_STD, S2500_REG_PHYID, &phyid);
    P("[2] PHY ID       : 0x%08lX  MAC=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
      (unsigned long)phyid, s_dev->mac[0], s_dev->mac[1], s_dev->mac[2],
      s_dev->mac[3], s_dev->mac[4], s_dev->mac[5]);

    {
        s2500_lb_result_t lb;
        rc = s2500_loopback_test_ex(s_dev, 1, 300, &lb);
        P("[3] PCS 环回收发 : %s (rxlen=%u head=%02X%02X%02X%02X)\r\n",
          rc == S2500_OK ? "PASS" : (rc == S2500_ERR_TIMEOUT ? "TIMEOUT" : "FAIL"),
          lb.rxlen, lb.head[0], lb.head[1], lb.head[2], lb.head[3]);
        rc = s2500_loopback_test_ex(s_dev, 2, 300, &lb);
        P("[4] PHY 环回收发 : %s (rxlen=%u head=%02X%02X%02X%02X)\r\n",
          rc == S2500_OK ? "PASS" : (rc == S2500_ERR_TIMEOUT ? "TIMEOUT" : "FAIL"),
          lb.rxlen, lb.head[0], lb.head[1], lb.head[2], lb.head[3]);
    }

    s2500_read_reg(s_dev, S2500_MMS_STD, S2500_REG_PHY_STATUS, &physts);
    P("[5] PHY Status   : 0x%04lX link=%lu\r\n", (unsigned long)physts,
      (unsigned long)((physts >> 2) & 1));

    if (s2500_read_status(s_dev, &st) == S2500_OK) {
        P("[6] SQI/SQI+     : %u/7, %u/255\r\n", st.sqi, st.sqip);
    }
    rc = s2500_hdd_run(s_dev, &hdd, 2000);
    if (rc == S2500_OK) {
        P("[7] HDD          : valid=%u SHORT_OPEN=%u (%s) 1W=%u\r\n",
          hdd.valid, hdd.short_open, short_open_str(hdd.short_open), hdd.one_wire);
    } else {
        P("[7] HDD          : 超时/不可用\r\n");
    }
    P("===== 自检结束 =====\r\n");
}

static int s2500_cmd(int argc, char *argv[])
{
    if (argc < 2) {
        P("s2500 <info|selftest|status|wd|counters|reg|plca|sqi|hdd|td|time|ts|loop|led|sleep|analog|mac|irq|bench>\r\n");
        return 0;
    }
    if (!strcmp(argv[1], "info")) cmd_info();
    else if (!strcmp(argv[1], "selftest")) cmd_selftest();
    else if (!strcmp(argv[1], "status")) cmd_status();
    else if (!strcmp(argv[1], "wd")) cmd_wd(argc, argv);
    else if (!strcmp(argv[1], "counters")) cmd_counters(argc >= 3 && !strcmp(argv[2], "clear"));
    else if (!strcmp(argv[1], "reg")) cmd_reg(argc, argv);
    else if (!strcmp(argv[1], "plca")) cmd_plca(argc, argv);
    else if (!strcmp(argv[1], "sqi")) cmd_sqi();
    else if (!strcmp(argv[1], "hdd")) cmd_hdd();
    else if (!strcmp(argv[1], "td")) cmd_td(argc, argv);
    else if (!strcmp(argv[1], "time")) cmd_time(argc, argv);
    else if (!strcmp(argv[1], "ts")) cmd_ts(argc, argv);
    else if (!strcmp(argv[1], "loop")) cmd_loop(argc, argv);
    else if (!strcmp(argv[1], "led")) cmd_led(argc, argv);
    else if (!strcmp(argv[1], "sleep")) cmd_sleep(argc, argv);
    else if (!strcmp(argv[1], "analog")) cmd_analog(argc, argv);
    else if (!strcmp(argv[1], "mac")) cmd_mac(argc, argv);
    else if (!strcmp(argv[1], "irq")) cmd_irq();
    else if (!strcmp(argv[1], "bench")) cmd_bench(argc, argv);
    else if (!strcmp(argv[1], "rxdump")) cmd_rxdump();
    else if (!strcmp(argv[1], "lbdump")) cmd_lbdump(argc, argv);
    else P("unknown subcommand: %s\r\n", argv[1]);
    return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 s2500, s2500_cmd, S2500 寄存器/PLCA/诊断/时间戳 命令集);