/* shell_lin.c — letter-shell LIN master/slave test commands.
 *
 * Topology: LIN11 = commander (master), LIN1..LIN10 = responders (slaves),
 * all bus wires tied together. Every transaction is master-driven and
 * synchronous (see App/lin12.c); there is no background traffic.
 *
 *   linsend <id> [hexdata] [classic]  master broadcast + verify all slaves
 *   linreq <id> <slave> [len] [hex]   slave responds, master verifies
 *   linpair [rounds] [len]            master-broadcast loop over IDs
 *   linslv [rounds] [len]             each slave responds in turn, loop
 *   linstat                           counters + error flags + EN states
 *   lindump [ch|all]                  last verified frame per channel
 *   linbaud <rate>                    re-init all channels (9600/10417/19200)
 *   linslp <group|all> <0|1>          TLIN1024 EN (SLP_N): 1=normal, 0=sleep
 *   linwake                           all groups back to normal
 *   linerr <parity|cksum|timeout>     error-injection tests
 */
#include "shell.h"
#include "lin12.h"
#include "IfxPort.h"
#include "IfxAsclin_reg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

extern volatile uint32 g_TickCount_1ms;

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse even-length hex (dots/spaces skipped) into buf. Returns bytes or -1. */
static int parse_hexbytes(const char *s, uint8 *buf, int maxlen)
{
    int n = 0, hi = -1;
    for (; *s && n < maxlen; s++) {
        if (*s == '.' || *s == ' ' || *s == '_') continue;
        int v = hexval(*s);
        if (v < 0) return -1;
        if (hi < 0) hi = v;
        else { buf[n++] = (uint8)((hi << 4) | v); hi = -1; }
    }
    if (hi >= 0) return -1;
    if (*s) return -1;
    return n;
}

static int parse_id6(const char *s, uint8 *id6)
{
    char *end;
    unsigned long v = strtoul(s, &end, 0);
    if (*end != '\0' || v > 0x3Fu) return -1;
    *id6 = (uint8)v;
    return 0;
}

static int parse_ch(const char *s, linChannel *ch)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*end != '\0' || v < 1 || v > 10) return -1;
    *ch = (linChannel)v;
    return 0;
}

static void print_data(Shell *shell, const uint8 *d, int len)
{
    int i;
    for (i = 0; i < len; i++) shellPrint(shell, "%02X%s", d[i], (i + 1 < len) ? " " : "");
}

/* Wired buses (user 2026-09-15): A=(1,2,3) master LIN3, B=(4,5,6,7) master LIN7,
 * C=(8,9,10,11) master LIN11. LIN0 placeholder (shares UART0 pins). */
static const uint8 linBusA[] = {1, 2, 3};
static const uint8 linBusB[] = {4, 5, 6, 7};
static const uint8 linBusC[] = {8, 9, 10, 11};
static const uint8 linBusMasters[3] = {3, 7, 11};

static int bus_of(int ch)
{
    int i;
    for (i = 0; i < 3; i++) if (linBusA[i] == ch) return 0;
    for (i = 0; i < 4; i++) if (linBusB[i] == ch) return 1;
    for (i = 0; i < 4; i++) if (linBusC[i] == ch) return 2;
    return -1;
}

static int pair_partner(int ch)
{
    /* legacy helper: first other member on the same bus (for hints) */
    int b = bus_of(ch), i, n;
    const uint8 *m;
    if (b == 0) { m = linBusA; n = 3; }
    else if (b == 1) { m = linBusB; n = 4; }
    else if (b == 2) { m = linBusC; n = 4; }
    else return -1;
    for (i = 0; i < n; i++) {
        if (m[i] != ch) return m[i];
    }
    return -1;
}

/* Roles for a pairwise test: everyone slave except the header master. */
static void lin_roles_for(linChannel master)
{
    linChannel ch;
    for (ch = LIN1; ch <= LIN11; ch++) lin_set_role(ch, (ch == master) ? 1 : 0);
}

/* ---------- linsend ---------- */
static int cmd_linsend(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    linChannel txch;
    uint8 id6, data[8], classic = 0;
    int len = 8, i, rc, partner;
    long v;
    char *end;

    if (argc < 4) {
        shellPrint(shell, "Usage: linsend <txch 1..11> <rxch 1..11> <id 0..63> [hexdata] [classic 0|1]\r\n");
        shellPrint(shell, "  e.g. linsend 11 10 0x12 A0A1A2A3A4A5A6A7\r\n");
        shellPrint(shell, "  txch becomes master; both must share a bus (A:1-3 B:4-7 C:8-11).\r\n");
        return -1;
    }
    v = strtol(argv[1], &end, 10);
    if (*end != '\0' || v < 1 || v > 11) {
        shellPrint(shell, "Bad txch '%s'\r\n", argv[1]);
        return -1;
    }
    txch = (linChannel)v;
    v = strtol(argv[2], &end, 10);
    if (*end != '\0' || v < 1 || v > 11 || v == txch) {
        shellPrint(shell, "Bad rxch '%s'\r\n", argv[2]);
        return -1;
    }
    partner = (int)v;
    if (parse_id6(argv[3], &id6) != 0) {
        shellPrint(shell, "Bad id '%s' (0..63, hex ok)\r\n", argv[3]);
        return -1;
    }
    if (bus_of((int)txch) < 0 || bus_of((int)txch) != bus_of(partner)) {
        shellPrint(shell, "NOTE: LIN%d and LIN%d are on different buses; trying anyway.\r\n",
                   (int)txch, partner);
    }
    for (i = 0; i < 8; i++) data[i] = (uint8)(0xA0 + i);
    if (argc >= 5) {
        int n = parse_hexbytes(argv[4], data, 8);
        if (n < 0) {
            shellPrint(shell, "Bad hexdata '%s'\r\n", argv[4]);
            return -1;
        }
        len = (n == 0) ? 8 : n;
        if (argc >= 6) classic = (atoi(argv[5]) != 0) ? 1 : 0;
        else if (id6 >= 0x3C) classic = 1; /* 0x3C/0x3D default classic per LIN spec */
    } else {
        if (id6 >= 0x3C) classic = 1;
    }
    lin_roles_for(txch);
    rc = lin_xact_m2s(txch, (linChannel)partner, id6, data, (uint8)len, classic);
    shellPrint(shell, "linsend LIN%d->LIN%d id=0x%02X pid=0x%02X len=%d %s: %s\r\n",
               (int)txch, partner, id6, lin_pid(id6), len,
               classic ? "classic" : "enhanced", rc ? "FAIL" : "PASS");
    shellPrint(shell, "  data: ");
    print_data(shell, data, len);
    shellPrint(shell, "\r\n");
    return rc ? -1 : 0;
}

/* ---------- linreq ---------- */
static int cmd_linreq(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint8 id6, data[8];
    linChannel mch = LIN11, sch = LIN10;
    int len = 8, i, rc;
    long v;
    char *end;

    if (argc < 4) {
        shellPrint(shell, "Usage: linreq <mch 1..11> <sch 1..10> <id 0..63> [len 1..8] [hexdata]\r\n");
        shellPrint(shell, "  e.g. linreq 11 10 0x20 8 1122334455667788\r\n");
        shellPrint(shell, "  mch sends header (master); sch responds; mch verifies.\r\n");
        return -1;
    }
    v = strtol(argv[1], &end, 10);
    if (*end != '\0' || v < 1 || v > 11) {
        shellPrint(shell, "Bad mch '%s'\r\n", argv[1]);
        return -1;
    }
    mch = (linChannel)v;
    if (parse_ch(argv[2], &sch) != 0) {
        shellPrint(shell, "Bad sch '%s' (1..10)\r\n", argv[2]);
        return -1;
    }
    if (parse_id6(argv[3], &id6) != 0) {
        shellPrint(shell, "Bad id '%s'\r\n", argv[3]);
        return -1;
    }
    if (argc >= 5) {
        len = atoi(argv[4]);
        if (len < 1) len = 1;
        if (len > 8) len = 8;
    }
    for (i = 0; i < 8; i++) data[i] = (uint8)(0x50 + id6 + i);
    if (argc >= 6) {
        int n = parse_hexbytes(argv[5], data, 8);
        if (n < 0) {
            shellPrint(shell, "Bad hexdata '%s'\r\n", argv[5]);
            return -1;
        }
        if (n > 0) len = n;
    }
    {
        uint8 classic = (id6 >= 0x3C) ? 1 : 0;
        lin_roles_for(mch);
        if (sch == mch) lin_set_role(sch, 0);
        rc = lin_xact_s2m(mch, sch, id6, data, (uint8)len, classic);
    }
    shellPrint(shell, "linreq LIN%d<-LIN%d id=0x%02X len=%d: %s\r\n",
               (int)mch, (int)sch, id6, len, rc ? "FAIL" : "PASS");
    shellPrint(shell, "  data: ");
    print_data(shell, data, len);
    shellPrint(shell, "\r\n");
    return rc ? -1 : 0;
}

/* ---------- linpair: per bus, master<->each slave both directions ---------- */
static int cmd_linpair(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    int rounds = 1, len = 8, r, b, i, fails = 0, total = 0;
    uint8 data[8];
    int k;

    if (argc >= 2) { rounds = atoi(argv[1]); if (rounds < 1) rounds = 1; if (rounds > 20) rounds = 20; }
    if (argc >= 3) { len = atoi(argv[2]); if (len < 1) len = 1; if (len > 8) len = 8; }
    shellPrint(shell, "linpair: %d round(s), len=%d, buses A(1-3,m3) B(4-7,m7) C(8-11,m11)\r\n",
               rounds, len);
    for (r = 0; r < rounds; r++) {
        shellPrint(shell, "round %d:\r\n", r + 1);
        for (b = 0; b < 3; b++) {
            const uint8 *mem;
            int n, Master = linBusMasters[b];
            if (b == 0) { mem = linBusA; n = 3; }
            else if (b == 1) { mem = linBusB; n = 4; }
            else { mem = linBusC; n = 4; }
            for (i = 0; i < n; i++) {
                linChannel slave = (linChannel)mem[i];
                uint8 idms = (uint8)(0x10 + b * 8 + i * 2), idsm = (uint8)(0x11 + b * 8 + i * 2);
                if ((int)slave == Master) continue;
                for (k = 0; k < len; k++) data[k] = (uint8)(0xA0 + r * 16 + b * 4 + i + k);
                lin_roles_for((linChannel)Master);
                total++;
                if (lin_xact_m2s((linChannel)Master, slave, idms, data, (uint8)len, 0) != 0) {
                    fails++;
                    shellPrint(shell, "  LIN%d->LIN%d id=0x%02X: FAIL\r\n", Master, (int)slave, idms);
                } else {
                    shellPrint(shell, "  LIN%d->LIN%d id=0x%02X: PASS\r\n", Master, (int)slave, idms);
                }
                total++;
                if (lin_xact_s2m((linChannel)Master, slave, idsm, data, (uint8)len, 0) != 0) {
                    fails++;
                    shellPrint(shell, "  LIN%d<-LIN%d id=0x%02X: FAIL\r\n", Master, (int)slave, idsm);
                } else {
                    shellPrint(shell, "  LIN%d<-LIN%d id=0x%02X: PASS\r\n", Master, (int)slave, idsm);
                }
            }
        }
    }
    shellPrint(shell, "linpair done: %s (%d/%d failed)\r\n",
               fails ? "FAIL" : "ALL PASS", fails, total);
    return fails ? -1 : 0;
}

/* ---------- linslv: slave-response direction per bus ---------- */
static int cmd_linslv(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    int rounds = 1, len = 8, r, b, i, fails = 0, total = 0;
    uint8 data[8];
    int k;

    if (argc >= 2) { rounds = atoi(argv[1]); if (rounds < 1) rounds = 1; if (rounds > 10) rounds = 10; }
    if (argc >= 3) { len = atoi(argv[2]); if (len < 1) len = 1; if (len > 8) len = 8; }
    shellPrint(shell, "linslv: %d round(s), len=%d, each slave responds to its bus master\r\n",
               rounds, len);
    for (r = 0; r < rounds; r++) {
        shellPrint(shell, "round %d:\r\n", r + 1);
        for (b = 0; b < 3; b++) {
            const uint8 *mem;
            int n, Master = linBusMasters[b];
            if (b == 0) { mem = linBusA; n = 3; }
            else if (b == 1) { mem = linBusB; n = 4; }
            else { mem = linBusC; n = 4; }
            for (i = 0; i < n; i++) {
                linChannel slave = (linChannel)mem[i];
                uint8 id6 = (uint8)(0x20 + b * 8 + i);
                if ((int)slave == Master) continue;
                for (k = 0; k < len; k++) data[k] = (uint8)(0x50 + r * 16 + b * 4 + i + k);
                lin_roles_for((linChannel)Master);
                total++;
                if (lin_xact_s2m((linChannel)Master, slave, id6, data, (uint8)len, 0) != 0) {
                    fails++;
                    shellPrint(shell, "  LIN%d<-LIN%d id=0x%02X: FAIL\r\n", Master, (int)slave, id6);
                } else {
                    shellPrint(shell, "  LIN%d<-LIN%d id=0x%02X: PASS\r\n", Master, (int)slave, id6);
                }
            }
        }
    }
    shellPrint(shell, "linslv done: %s (%d/%d failed)\r\n",
               fails ? "FAIL" : "ALL PASS", fails, total);
    return fails ? -1 : 0;
}

/* ---------- linrole ---------- */
static int cmd_linrole(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    long ch;
    char *end;

    if (argc < 3) {
        shellPrint(shell, "Usage: linrole <ch 1..11> <M|S>  (re-init channel as master/slave)\r\n");
        shellPrint(shell, "  Buses: A(1-3,m3) B(4-7,m7) C(8-11,m11); header side must be master.\r\n");
        return -1;
    }
    ch = strtol(argv[1], &end, 10);
    if (*end != '\0' || ch < 1 || ch > 11) {
        shellPrint(shell, "Bad channel '%s'\r\n", argv[1]);
        return -1;
    }
    if ((argv[2][0] != 'M' && argv[2][0] != 'm' && argv[2][0] != 'S' && argv[2][0] != 's') || argv[2][1] != '\0') {
        shellPrint(shell, "Bad role '%s' (M|S)\r\n", argv[2]);
        return -1;
    }
    lin_set_role((linChannel)ch, (argv[2][0] == 'M' || argv[2][0] == 'm') ? 1 : 0);
    shellPrint(shell, "linrole: LIN%ld now %s\r\n", ch,
               (argv[2][0] == 'M' || argv[2][0] == 'm') ? "master" : "slave");
    return 0;
}

/* ---------- linstat ---------- */
static int cmd_linstat(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    linChannel ch;
    (void)argc; (void)argv;
    shellPrint(shell, "baud=%.0f bps EN: g0(P02.8)=%d g1(P00.11)=%d g2(P00.10)=%d (1=normal)\r\n",
               (double)g_linBaud,
               (int)IfxPort_getPinState(&MODULE_P02, 8),
               (int)IfxPort_getPinState(&MODULE_P00, 11),
               (int)IfxPort_getPinState(&MODULE_P00, 10));
    shellPrint(shell, "ch(M) | txH/txR/rxH/rxR | hdrE/respE | par/ck/to/fe | last(id len ok)\r\n");
    for (ch = LIN0; ch < LIN_NUM; ch++) {
        linChState_t *s = &g_lin[ch];
        if (!s->used) {
            shellPrint(shell, "%2d(-) | placeholder (shares UART0 pins, not initialized)\r\n", (int)ch);
            continue;
        }
        shellPrint(shell, "%2d(%c) | %lu/%lu/%lu/%lu | %lu/%lu | %lu/%lu/%lu/%lu | id=0x%02X len=%d %s\r\n",
                   (int)ch, s->isMaster ? 'M' : 'S',
                   (unsigned long)s->txHdr, (unsigned long)s->txResp,
                   (unsigned long)s->rxHdr, (unsigned long)s->rxResp,
                   (unsigned long)s->hdrErr, (unsigned long)s->respErr,
                   (unsigned long)s->parityErr, (unsigned long)s->cksumErr,
                   (unsigned long)s->timeoutErr, (unsigned long)s->frameErr,
                   s->lastId, s->lastLen, s->lastOk ? "ok" : "-");
    }
    return 0;
}

/* ---------- lindump ---------- */
static int cmd_lindump(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    int first = 0, last = LIN_NUM - 1, i;

    if (argc >= 2) {
        if (strcmp(argv[1], "all") == 0) { first = 0; last = LIN_NUM - 1; }
        else {
            char *end;
            long v = strtol(argv[1], &end, 10);
            if (*end != '\0' || v < 0 || v > 11) {
                shellPrint(shell, "Usage: lindump [ch 0..11|all]\r\n");
                return -1;
            }
            first = last = (int)v;
        }
    }
    for (i = first; i <= last; i++) {
        linChState_t *s = &g_lin[i];
        int k;
        if (!s->used) {
            shellPrint(shell, "LIN%d: placeholder\r\n", i);
            continue;
        }
        shellPrint(shell, "[%lu] LIN%d%c id=0x%02X pid=0x%02X len=%d %s %s: ",
                   (unsigned long)s->lastTick, i, s->isMaster ? 'M' : 'S',
                   s->lastId, lin_pid(s->lastId), s->lastLen,
                   s->lastClassic ? "classic" : "enhanced",
                   s->lastDir ? "slv->mst" : "mst->slv");
        for (k = 0; k < s->lastLen; k++) shellPrint(shell, "%02X%s", s->lastData[k], (k + 1 < s->lastLen) ? " " : "");
        shellPrint(shell, " (%s)\r\n", s->lastOk ? "ok" : "stale/never");
    }
    return 0;
}

/* ---------- linbaud ---------- */
static int cmd_linbaud(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    long rate;
    char *end;

    if (argc < 2) {
        shellPrint(shell, "Usage: linbaud <2400|4800|9600|10417|19200>  (current %.0f)\r\n", (double)g_linBaud);
        return -1;
    }
    rate = strtol(argv[1], &end, 10);
    if (*end != '\0' || (rate != 2400 && rate != 4800 && rate != 9600 && rate != 10417 && rate != 19200)) {
        shellPrint(shell, "Bad rate '%s' (2400/4800/9600/10417/19200)\r\n", argv[1]);
        return -1;
    }
    lin12_init_all((float32)rate);
    shellPrint(shell, "linbaud: all LIN1..11 re-init at %ld bps. Try 'linpair 1 8'.\r\n", rate);
    return 0;
}

/* ---------- linslp / linwake ---------- */
static int cmd_linslp(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    int group = -1, normal = 1;

    if (argc < 3) {
        shellPrint(shell, "Usage: linslp <group 0..2|all> <0|1>  (1=normal H, 0=sleep L)\r\n");
        shellPrint(shell, "  group0=LIN0-3 P02.8, group1=LIN4-7 P00.11, group2=LIN8-11 P00.10\r\n");
        shellPrint(shell, "  e.g. linslp 2 0  (sleep LIN8-11 incl. master), then 'linpair' must FAIL\r\n");
        return -1;
    }
    if (strcmp(argv[1], "all") == 0) group = -1;
    else {
        group = atoi(argv[1]);
        if (group < 0 || group > 2) {
            shellPrint(shell, "Bad group '%s'\r\n", argv[1]);
            return -1;
        }
    }
    normal = (atoi(argv[2]) != 0) ? 1 : 0;
    if (group < 0) lin_xcvr_set_all((uint8)normal);
    else lin_xcvr_set_group((uint8)group, (uint8)normal);
    shellPrint(shell, "linslp: group %s -> %s. Run 'linstat' + 'linpair 1 8' to verify.\r\n",
               argv[1], normal ? "normal (H)" : "sleep (L)");
    return 0;
}

static int cmd_linwake(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    (void)argc; (void)argv;
    lin_xcvr_set_all(1);
    shellPrint(shell, "linwake: all EN high (normal). Run 'linpair 1 8'.\r\n");
    return 0;
}

/* ---------- lintgl ---------- */
static int cmd_lintgl(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    long ch, n = 10;
    char *end;

    if (argc < 2) {
        shellPrint(shell, "Usage: lintgl <txch 1..11> [n]  (toggle TX pin as GPIO, then LIN re-init)\r\n");
        shellPrint(shell, "  e.g. lintgl 11 10: P21.0 toggles 10x100ms; probe with meter/scope,\r\n");
        shellPrint(shell, "  then 'linpair' to re-test (counters cleared by re-init).\r\n");
        return -1;
    }
    ch = strtol(argv[1], &end, 10);
    if (*end != '\0' || ch < 1 || ch > 11) {
        shellPrint(shell, "Bad channel '%s' (1..11)\r\n", argv[1]);
        return -1;
    }
    if (argc >= 3) {
        n = strtol(argv[2], &end, 10);
        if (*end != '\0' || n < 1 || n > 50) n = 10;
    }
    shellPrint(shell, "lintgl: LIN%ld TX toggling %ld x 100ms... (probe now)\r\n", ch, n);
    lin_probe_tx_toggle((linChannel)ch, (uint8)n);
    shellPrint(shell, "lintgl done, LIN re-init at %.0f bps. RX levels:", (double)g_linBaud);
    {
        /* RX pin states need SFR-independent read: report via linstat instead */
        shellPrint(shell, " see 'linstat' EN + run 'linpair'.\r\n");
    }
    return 0;
}

/* ---------- linerr ---------- */
static int cmd_linerr(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint8 data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

    if (argc < 2) {
        shellPrint(shell, "Usage: linerr <parity|cksum|timeout>\r\n");
        shellPrint(shell, "  parity : master sends header with broken PID parity\r\n");
        shellPrint(shell, "  cksum  : master sends response with wrong checksum mode\r\n");
        shellPrint(shell, "  timeout: master header with response that never comes\r\n");
        return -1;
    }
    if (strcmp(argv[1], "parity") == 0) {
        uint8 good = lin_pid(0x12), bad = (uint8)(good ^ 0x40); /* flip P0 */
        uint8 pid = 0;
        uint32 pe0 = g_lin[LIN10].parityErr;
        int badSeen = 0;
        lin_roles_for(LIN11);
        lin_slave_arm_header(LIN10);
        shellPrint(shell, "linerr parity: LIN11 sends bad pid=0x%02X (good 0x%02X) to LIN10...\r\n", bad, good);
        if (lin_raw_master_header(bad) != 0) {
            shellPrint(shell, "  master header TX failed (bus ok? try 'linpair')\r\n");
            return -1;
        }
        if (lin_slave_poll_header(LIN10, &pid) == 0) {
            if (lin_pid_check(pid) != 0) badSeen++;
        }
        if (g_lin[LIN10].parityErr > pe0) badSeen++;
        shellPrint(shell, "  LIN10 parity indication: %s (see 'linstat' par column)\r\n",
                   badSeen ? "yes" : "NO");
        return badSeen ? 0 : -1;
    } else if (strcmp(argv[1], "cksum") == 0) {
        /* Pair (11->10): master sends classic while slave armed enhanced.
         * Slave must flag LIN checksum error (LC). */
        uint8 csData[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        uint32 cs0 = g_lin[LIN10].cksumErr;
        int fails;
        shellPrint(shell, "linerr cksum: LIN11->LIN10 id=0x12 master classic vs slave enhanced...\r\n");
        lin_roles_for(LIN11);
        fails = 0;
        {
            /* header (both sides parity-clean), then mismatched response */
            uint8 pid = lin_pid(0x12);
            uint8 rxPid = 0;
            lin_slave_arm_header(LIN10);
            if (lin_raw_master_header(pid) != 0) {
                shellPrint(shell, "  master header TX failed\r\n");
                return -1;
            }
            if (lin_slave_poll_header(LIN10, &rxPid) != 0 || rxPid != pid) {
                shellPrint(shell, "  LIN10 header miss\r\n");
                return -1;
            }
            /* slave armed enhanced(0), master sends classic(1) */
            lin_slave_arm_response(LIN10, 8, 0);
            if (lin_raw_master_response(csData, 8, 1) != 0) {
                shellPrint(shell, "  master response TX failed\r\n");
                return -1;
            }
            {
                uint8 rx[8];
                memset(rx, 0, sizeof(rx));
                if (lin_slave_poll_response(LIN10, rx, 8) == 0) {
                    shellPrint(shell, "  UNEXPECTED: slave accepted mismatched checksum\r\n");
                    fails = 1;
                }
            }
        }
        shellPrint(shell, "  LIN10 LC flag delta: %lu (expect >=1)\r\n",
                   (unsigned long)(g_lin[LIN10].cksumErr - cs0));
        return (fails == 0 && (g_lin[LIN10].cksumErr - cs0) >= 1) ? 0 : -1;
    } else if (strcmp(argv[1], "timeout") == 0) {
        uint8 pid = lin_pid(0x30);
        uint8 rx[8];
        memset(rx, 0, sizeof(rx));
        shellPrint(shell, "linerr timeout: header pid=0x%02X, nobody answers, master waits...\r\n", pid);
        if (lin_raw_master_header(pid) != 0) {
            shellPrint(shell, "  master header TX failed\r\n");
            return -1;
        }
        lin_master_arm_response(8, 0);
        if (lin_master_poll_response(rx, 8) == 0) {
            shellPrint(shell, "  UNEXPECTED: got a response (bus bridged?)\r\n");
            return -1;
        }
        shellPrint(shell, "  OK: response timeout flagged (see 'linstat' timeout column)\r\n");
        return 0;
    }
    shellPrint(shell, "Unknown linerr arg '%s'\r\n", argv[1]);
    return -1;
}

/* ---------- linbusact ---------- */
static int cmd_linbusact(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint8 pid = lin_pid(0x12);
    uint32 mask = 0;
    uint8 txLow = 0, txHigh = 0;
    int rc, i;

    if (argc >= 2) {
        char *end;
        unsigned long v = strtoul(argv[1], &end, 0);
        if (*end != '\0' || v > 0xFF) {
            shellPrint(shell, "Usage: linbusact [pid-hex]  (default PID for id 0x12)\r\n");
            return -1;
        }
        pid = (uint8)v;
    }
    shellPrint(shell, "linbusact: master sends header pid=0x%02X, sampling TX+RX pins...\r\n", pid);
    rc = lin_bus_activity(pid, &mask, &txLow, &txHigh);
    shellPrint(shell, "  THE=%s masterTX sawLow=%d sawHigh=%d\r\n",
               rc ? "FAIL" : "ok", txLow, txHigh);
    shellPrint(shell, "  slave RX saw-dominant mask=0x%03lX: ", (unsigned long)mask);
    for (i = 1; i <= 10; i++) shellPrint(shell, "%d", (mask & (1u << i)) ? 1 : 0);
    shellPrint(shell, " (LIN1..LIN10 order)\r\n");
    if (rc == 0 && txLow && txHigh && mask == 0) {
        shellPrint(shell, "  -> MCU TX toggles but NO slave sees it: check TLIN TXD trace / EN at chip / LIN bus wiring\r\n");
    } else if (rc == 0 && mask != 0) {
        shellPrint(shell, "  -> bus toggles; slaves' ASCLIN not decoding: check baud/ALTI/pin mux\r\n");
    } else if (!txLow || !txHigh) {
        shellPrint(shell, "  -> master TX pin stuck: check P21.0 ALT/mux\r\n");
    }
    return rc;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linsend, cmd_linsend, pair master test);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linreq, cmd_linreq, pair slave response test);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linrole, cmd_linrole, set master/slave role);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linpair, cmd_linpair, master->slaves loop);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linslv, cmd_linslv, slaves->master loop);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linstat, cmd_linstat, LIN status);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), lindump, cmd_lindump, last LIN frames);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linbaud, cmd_linbaud, LIN baudrate);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linslp, cmd_linslp, transceiver sleep);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linwake, cmd_linwake, transceiver wake);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), lintgl, cmd_lintgl, TX pin probe toggle);
/* ---------- linana: external-analyzer full-receive test ----------
 * Topology: LIN1..LIN11 tied together + LIN analyzer (external master,
 * 1 frame/s). All 11 channels are put to slave, then each round sniffs one
 * external frame (header+response). Known analyzer frames:
 *   ID 0x17 PID 0x97 classic  2B: 22 33
 *   ID 0x31 PID 0xB1 enhanced 8B: 11 22 33 44 55 66 77 88
 * Usage: linana [rounds 1..20] [timeoutMs 500..10000]
 * Each round prints PID/ID/len/mode, header mask, response mask, data
 * (from LIN1 as reference) and per-channel ok list. */
static int cmd_linana(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    int rounds = 4, r, passRounds = 0;
    uint32 timeoutMs = 3000;
    uint32 hdrTot[LIN_NUM], okTot[LIN_NUM];
    int i;

    if (argc >= 2) {
        rounds = atoi(argv[1]);
        if (rounds < 1) rounds = 1;
        if (rounds > 20) rounds = 20;
    }
    if (argc >= 3) {
        long t = atol(argv[2]);
        if (t < 500) t = 500;
        if (t > 10000) t = 10000;
        timeoutMs = (uint32)t;
    }
    for (i = 0; i < LIN_NUM; i++) { hdrTot[i] = 0; okTot[i] = 0; }
    shellPrint(shell, "linana: all LIN1..11 -> slave, sniff %d frame(s), timeout %lu ms\r\n",
               rounds, (unsigned long)timeoutMs);
    shellPrint(shell, "  expect ID 0x17 (2B classic 22 33) + ID 0x31 (8B enhanced 11..88), 1/s\r\n");
    lin_all_slave();
    for (r = 0; r < rounds; r++) {
        uint8 pid = 0, exp = 0, classic = 0;
        uint32 hdr = 0, resp = 0;
        uint8 data[12][10];
        uint8 rawLen[12];
        int rc, k, nhdr = 0, nok = 0;
        memset(data, 0, sizeof(data));
        memset(rawLen, 0, sizeof(rawLen));
        shellPrint(shell, "round %d: listening...\r\n", r + 1);
        rc = lin_sniff_ext_frame(&pid, &exp, &classic, &hdr, &resp,
                                 (uint8 (*)[10])data, rawLen, timeoutMs);
        for (i = 1; i <= 11; i++) {
            if (hdr & (1u << i)) { nhdr++; hdrTot[i]++; }
            if (resp & (1u << i)) { nok++; okTot[i]++; }
        }
        if (rc == -1) {
            shellPrint(shell, "  TIMEOUT: no external header in %lu ms (baud? try 'linbaud'; wiring? try 'lindomf')\r\n",
                       (unsigned long)timeoutMs);
            continue;
        }
        {
            int ref = -1;
            for (i = 1; i <= 11; i++) {
                if (resp & (1u << i)) { ref = i; break; }
            }
            if (ref < 0) {
                for (i = 1; i <= 11; i++) {
                    if (hdr & (1u << i)) { ref = i; break; }
                }
            }
            shellPrint(shell, "  %s pid=0x%02X id=0x%02X exp=%dB %s hdr=%d/11 resp=%d/11\r\n",
                       rc ? "PARTIAL" : "GOT", pid, pid & 0x3F, exp,
                       classic ? "classic" : "enhanced", nhdr, nok);
            shellPrint(shell, "  hdr : ");
            for (i = 1; i <= 11; i++) shellPrint(shell, "%d", (hdr & (1u << i)) ? 1 : 0);
            shellPrint(shell, "  resp: ");
            for (i = 1; i <= 11; i++) shellPrint(shell, "%d", (resp & (1u << i)) ? 1 : 0);
            shellPrint(shell, "\r\n");
            if (ref > 0) {
                shellPrint(shell, "  raw(LIN%d,%dB): ", ref, rawLen[ref]);
                for (k = 0; k < rawLen[ref] && k < 10; k++)
                    shellPrint(shell, "%02X%s", data[ref][k], (k + 1 < rawLen[ref]) ? " " : "");
                shellPrint(shell, "\r\n");
            }
            /* payload check against known analyzer frames (raw incl. checksum) */
            if ((pid & 0x3F) == 0x17) {
                uint8 expRaw[3] = {0x22, 0x33, 0x00};
                int refOk;
                expRaw[2] = lin_checksum(pid, expRaw, 2, 1);
                refOk = (ref > 0 && rawLen[ref] == 3 &&
                         data[ref][0] == 0x22 && data[ref][1] == 0x33 &&
                         data[ref][2] == expRaw[2]);
                shellPrint(shell, "  expect 22 33 %02X classic: %s\r\n",
                           expRaw[2], refOk ? "MATCH" : "MISMATCH");
                if (refOk && nok == 11) passRounds++;
            } else if ((pid & 0x3F) == 0x31) {
                uint8 expPay[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
                uint8 ck = lin_checksum(pid, expPay, 8, 0);
                int refOk = (ref > 0 && rawLen[ref] == 9);
                if (refOk) {
                    for (k = 0; k < 8; k++) {
                        if (data[ref][k] != expPay[k]) { refOk = 0; break; }
                    }
                    if (data[ref][8] != ck) refOk = 0;
                }
                shellPrint(shell, "  expect 11..88 %02X enhanced: %s\r\n",
                           ck, refOk ? "MATCH" : "MISMATCH");
                if (refOk && nok == 11) passRounds++;
            } else {
                shellPrint(shell, "  unknown ID (not 0x17/0x31): no payload check\r\n");
            }
        }
    }
    shellPrint(shell, "linana summary (hdr/resp hits per ch over %d rounds):\r\n", rounds);
    for (i = 1; i <= 11; i++) {
        shellPrint(shell, "  LIN%2d: hdr=%lu resp=%lu%s\r\n", i,
                   (unsigned long)hdrTot[i], (unsigned long)okTot[i],
                   (okTot[i] == (uint32)rounds) ? " FULL" : "");
    }
    shellPrint(shell, "linana done: %d/%d full-11 rounds. See 'linstat'/'lindump all'.\r\n",
               passRounds, rounds);
    return (passRounds > 0) ? 0 : -1;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linbusact, cmd_linbusact, bus activity probe);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linana, cmd_linana, analyzer full-receive test);

/* ---------- linpas: passive GPIO census (no ASCLIN, baud-independent) ---------- */
static int cmd_linpas(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32 ms = 3000, cnt[12];
    uint32 low = 0;
    int i;
    if (argc >= 2) {
        long v = atol(argv[1]);
        if (v < 100) v = 100;
        if (v > 10000) v = 10000;
        ms = (uint32)v;
    }
    shellPrint(shell, "linpas: sampling RX nets as GPIO for %lu ms (analyzer should send ~%lu frames)...\r\n",
               (unsigned long)ms, (unsigned long)(ms / 1000));
    lin_passive_census(ms, cnt, &low);
    shellPrint(shell, "  edges 1..11: ");
    for (i = 1; i <= 11; i++) shellPrint(shell, "%lu ", (unsigned long)cnt[i]);
    shellPrint(shell, "\r\n  everLow 1..11: ");
    for (i = 1; i <= 11; i++) shellPrint(shell, "%d", (low & (1u << i)) ? 1 : 0);
    shellPrint(shell, " (expect 40+ edges + all 1 if bus common with analyzer)\r\n");
    shellPrint(shell, "  idle now: ");
    {
        uint32 idle = lin_rx_levels();
        for (i = 1; i <= 11; i++) shellPrint(shell, "%d", (idle & (1u << i)) ? 1 : 0);
        shellPrint(shell, " (expect all 0; 1=dominant stuck)\r\n");
    }
    return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linpas, cmd_linpas, passive bus census);

/* ---------- linbb ---------- */
static int cmd_linbb(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    linChannel slave = LIN1;
    uint8 id6 = 0x12, pid, rxPid = 0;
    int rc;

    if (argc >= 2) {
        char *end;
        long v = strtol(argv[1], &end, 10);
        if (*end != '\0' || v < 1 || v > 11) {
            shellPrint(shell, "Usage: linbb <ch 1..11> [id 0..63]\r\n");
            return -1;
        }
        slave = (linChannel)v;
    }
    if (argc >= 3 && parse_id6(argv[2], &id6) != 0) {
        shellPrint(shell, "Bad id '%s'\r\n", argv[2]);
        return -1;
    }
    pid = lin_pid(id6);
    shellPrint(shell, "linbb: bit-bang header pid=0x%02X into LIN%d RX net...\r\n", pid, (int)slave);
    rc = lin_bb_header(slave, pid, &rxPid);
    if (rc == 0) {
        shellPrint(shell, "  PASS: slave latched RHE, pid=0x%02X match\r\n", rxPid);
    } else if (rc == -2) {
        shellPrint(shell, "  MISMATCH: RHE latched but pid=0x%02X (sent 0x%02X)\r\n", rxPid, pid);
    } else {
        shellPrint(shell, "  FAIL: no RHE (slave ASCLIN/pin/baud path broken)\r\n");
    }
    shellPrint(shell, "  (LIN re-initialized; counters cleared)\r\n");
    return rc;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linbb, cmd_linbb, bit-bang slave self-test);

/* ---------- linloop ---------- */
static int cmd_linloop(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint8 id6 = 0x12, data[8];
    int len = 8, i, rc;
    uint8 tre = 0;
    uint32 flags = 0;

    if (argc >= 2 && parse_id6(argv[1], &id6) != 0) {
        shellPrint(shell, "Usage: linloop [id 0..63]\r\n");
        return -1;
    }
    for (i = 0; i < 8; i++) data[i] = (uint8)(0xA0 + i);
    shellPrint(shell, "linloop: LIN11 TXD->TLIN->bus->TLIN->RXD self-reception...\r\n");
    rc = lin_master_loopback(id6, data, (uint8)len, 0, &tre, &flags);
    shellPrint(shell, "  TRE=%d FLAGS=0x%08lX ", tre, (unsigned long)flags);
    if (rc == 0) {
        shellPrint(shell, "  PASS: header + 8B response looped back intact\r\n");
    } else if (rc == -1) {
        shellPrint(shell, "  FAIL: no looped-back header (TXD/TLIN/bus/RXD chain broken)\r\n");
    } else if (rc == -2) {
        shellPrint(shell, "  FAIL: looped-back PID mismatch\r\n");
    } else if (rc == -4) {
        shellPrint(shell, "  PARTIAL: header looped back, response echo missing\r\n");
    } else {
        shellPrint(shell, "  FAIL: response echo mismatch (rc=%d)\r\n", rc);
    }
    return rc;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linloop, cmd_linloop, master analog loopback);

/* ---------- linresptst: response-on-bus test (no header) ---------- */
static int cmd_linresptst(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint8 data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint8 rx[8] = {0};
    int i, rc;

    (void)argc; (void)argv;
    shellPrint(shell, "linresptst: arm LIN1 for 8B response, master sends response (no header)...\r\n");
    lin_slave_arm_response(LIN1, 8, 0);
    if (lin_raw_master_response(data, 8, 0) != 0) {
        shellPrint(shell, "  master response TX failed\r\n");
        return -1;
    }
    rc = lin_slave_poll_response(LIN1, rx, 8);
    if (rc != 0) {
        shellPrint(shell, "  FAIL: LIN1 no RRE (response not on bus or LIN1 RXD path broken)\r\n");
        return -1;
    }
    for (i = 0; i < 8; i++) {
        if (rx[i] != data[i]) {
            shellPrint(shell, "  FAIL: data mismatch at byte %d\r\n", i);
            return -1;
        }
    }
    shellPrint(shell, "  PASS: LIN1 received master response intact\r\n");
    return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linresptst, cmd_linresptst, response on bus test);

/* ---------- lindom: dominant-hold bus commonality probe ---------- */
static void print_rxmask(Shell *shell, uint32 mask)
{
    int i;
    for (i = 1; i <= 11; i++) shellPrint(shell, "%d", (mask & (1u << i)) ? 1 : 0);
}

static int cmd_lindom(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    long ch;
    char *end;
    uint32 idle, early = 0, late = 0;

    if (argc < 2) {
        shellPrint(shell, "Usage: lindom <ch 1..11>  (hold TXD dominant, sample all RX nets)\r\n");
        shellPrint(shell, "  Which RX nets follow tells which chips share the LIN bus.\r\n");
        return -1;
    }
    ch = strtol(argv[1], &end, 10);
    if (*end != '\0' || ch < 1 || ch > 11) {
        shellPrint(shell, "Bad channel '%s'\r\n", argv[1]);
        return -1;
    }
    idle = lin_rx_levels();
    shellPrint(shell, "lindom LIN%ld: idle RX mask=", ch);
    print_rxmask(shell, idle);
    shellPrint(shell, " (expect all 0; 1=dominant!)\r\n");
    shellPrint(shell, "  holding TXD dominant... (LIN re-init afterwards)\r\n");
    lin_dominant_hold((linChannel)ch, &early, &late);
    shellPrint(shell, "  early(2ms) RX mask=");
    print_rxmask(shell, early);
    shellPrint(shell, " late(300ms) RX mask=");
    print_rxmask(shell, late);
    shellPrint(shell, "\r\n  (LIN re-initialized; counters cleared)\r\n");
    return 0;
}

/* ---------- lindomf: fast dominant probe with TX self-read ---------- */
static int cmd_lindomf(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    long ch;
    char *end;
    uint8 selfLow = 0;
    uint32 rx = 0;

    if (argc < 2) {
        shellPrint(shell, "Usage: lindomf <ch 1..11>\r\n");
        return -1;
    }
    ch = strtol(argv[1], &end, 10);
    if (*end != '\0' || ch < 1 || ch > 11) {
        shellPrint(shell, "Bad channel '%s'\r\n", argv[1]);
        return -1;
    }
    lin_dominant_fast((linChannel)ch, &selfLow, &rx);
    shellPrint(shell, "lindomf LIN%ld: txSelfLow=%d rxMask=0x%03lX: ", ch, selfLow, (unsigned long)rx);
    print_rxmask(shell, rx);
    shellPrint(shell, "\r\n  mid-hold IOCR0=0x%08lX IN=0x%08lX OUT=0x%08lX\r\n",
               (unsigned long)g_linHoldIOC, (unsigned long)g_linHoldIN, (unsigned long)g_linHoldOUT);
    shellPrint(shell, "  (LIN re-initialized; counters cleared)\r\n");
    return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), lindom, cmd_lindom, dominant bus probe);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), lindomf, cmd_lindomf, fast dominant probe);

/* ---------- linact: AC edge census during a real header ---------- */
static int cmd_linact(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    long ch = 11;
    char *end;
    uint32 cnt[LIN_NUM];
    int i;

    if (argc >= 2) {
        ch = strtol(argv[1], &end, 10);
        if (*end != '\0' || ch < 1 || ch > 11) {
            shellPrint(shell, "Usage: linact [master 1..11]\r\n");
            return -1;
        }
    }
    lin_roles_for((linChannel)ch);
    lin_ac_census((linChannel)ch, lin_pid(0x12), cnt);
    shellPrint(shell, "linact LIN%ld header: RX edge counts 1..11: ", ch);
    for (i = 1; i <= 11; i++) shellPrint(shell, "%lu ", (unsigned long)cnt[i]);
    shellPrint(shell, "\r\n  (expect ~20+ on wired partners, 0 elsewhere)\r\n");
    return 0;
}

/* ---------- linreg: pin/mode forensics ---------- */
static int cmd_linreg(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    (void)argc; (void)argv;
    shellPrint(shell, "P21_IOCR0=0x%08lX IN=0x%08lX OUT=0x%08lX\r\n",
               (unsigned long)MODULE_P21.IOCR0.U,
               (unsigned long)MODULE_P21.IN.U,
               (unsigned long)MODULE_P21.OUT.U);
    shellPrint(shell, "P15_IOCR0/1=0x%08lX/0x%08lX IN=0x%08lX\r\n",
               (unsigned long)MODULE_P15.IOCR0.U,
               (unsigned long)MODULE_P15.IOCR4.U,
               (unsigned long)MODULE_P15.IN.U);
    shellPrint(shell, "P14_IOCR0=0x%08lX IN=0x%08lX (UART ref)\r\n",
               (unsigned long)MODULE_P14.IOCR0.U,
               (unsigned long)MODULE_P14.IN.U);
    shellPrint(shell, "P00_IOCR4=0x%08lX IOCR8=0x%08lX IN=0x%08lX (exp: P00.7/alt2=0x90 P00.8/alt3=0x98 P00.9/alt5=0xA8)\r\n",
               (unsigned long)MODULE_P00.IOCR4.U,
               (unsigned long)MODULE_P00.IOCR8.U,
               (unsigned long)MODULE_P00.IN.U);
    shellPrint(shell, "P10_IOCR4=0x%08lX (exp: P10.5/alt2=0x90)\r\n",
               (unsigned long)MODULE_P10.IOCR4.U);
    shellPrint(shell, "ASC11: IOCR=0x%08lX FLAGS=0x%08lX FEN=0x%08lX FRAMECON=0x%08lX LINCON=0x%08lX DATCON=0x%08lX BRG=0x%08lX BITCON=0x%08lX\r\n",
               (unsigned long)MODULE_ASCLIN11.IOCR.U,
               (unsigned long)MODULE_ASCLIN11.FLAGS.U,
               (unsigned long)MODULE_ASCLIN11.FLAGSENABLE.U,
               (unsigned long)MODULE_ASCLIN11.FRAMECON.U,
               (unsigned long)MODULE_ASCLIN11.LIN.CON.U,
               (unsigned long)MODULE_ASCLIN11.DATCON.U,
               (unsigned long)MODULE_ASCLIN11.BRG.U,
               (unsigned long)MODULE_ASCLIN11.BITCON.U);
    shellPrint(shell, "ASC1 : IOCR=0x%08lX FLAGS=0x%08lX FEN=0x%08lX FRAMECON=0x%08lX LINCON=0x%08lX\r\n",
               (unsigned long)MODULE_ASCLIN1.IOCR.U,
               (unsigned long)MODULE_ASCLIN1.FLAGS.U,
               (unsigned long)MODULE_ASCLIN1.FLAGSENABLE.U,
               (unsigned long)MODULE_ASCLIN1.FRAMECON.U,
               (unsigned long)MODULE_ASCLIN1.LIN.CON.U);
    {
        /* all-module MS bits + g_lin role mirror */
        uint32 ms = 0;
        int i;
        ms |= ((MODULE_ASCLIN1.LIN.CON.B.MS & 1u) << 1);
        ms |= ((MODULE_ASCLIN2.LIN.CON.B.MS & 1u) << 2);
        ms |= ((MODULE_ASCLIN3.LIN.CON.B.MS & 1u) << 3);
        ms |= ((MODULE_ASCLIN4.LIN.CON.B.MS & 1u) << 4);
        ms |= ((MODULE_ASCLIN5.LIN.CON.B.MS & 1u) << 5);
        ms |= ((MODULE_ASCLIN6.LIN.CON.B.MS & 1u) << 6);
        ms |= ((MODULE_ASCLIN7.LIN.CON.B.MS & 1u) << 7);
        ms |= ((MODULE_ASCLIN8.LIN.CON.B.MS & 1u) << 8);
        ms |= ((MODULE_ASCLIN9.LIN.CON.B.MS & 1u) << 9);
        ms |= ((MODULE_ASCLIN10.LIN.CON.B.MS & 1u) << 10);
        ms |= ((MODULE_ASCLIN11.LIN.CON.B.MS & 1u) << 11);
        shellPrint(shell, "MS bits11..1: ");
        for (i = 11; i >= 1; i--) shellPrint(shell, "%d", (ms >> i) & 1u);
        shellPrint(shell, " (1=master)  sw roles: ");
        for (i = 11; i >= 1; i--) shellPrint(shell, "%d", g_lin[i].isMaster ? 1 : 0);
        shellPrint(shell, "\r\n");
    }
    {
        /* TX-path regs for suspect (4/5/10) vs reference (11) modules */
        shellPrint(shell, "ASC4 : CLC=0x%08lX TXFIFO=0x%08lX BRG=0x%08lX BITCON=0x%08lX DATCON=0x%08lX LINCON=0x%08lX\r\n",
                   (unsigned long)MODULE_ASCLIN4.CLC.U,
                   (unsigned long)MODULE_ASCLIN4.TXFIFOCON.U,
                   (unsigned long)MODULE_ASCLIN4.BRG.U,
                   (unsigned long)MODULE_ASCLIN4.BITCON.U,
                   (unsigned long)MODULE_ASCLIN4.DATCON.U,
                   (unsigned long)MODULE_ASCLIN4.LIN.CON.U);
        shellPrint(shell, "ASC5 : CLC=0x%08lX TXFIFO=0x%08lX BRG=0x%08lX BITCON=0x%08lX DATCON=0x%08lX LINCON=0x%08lX\r\n",
                   (unsigned long)MODULE_ASCLIN5.CLC.U,
                   (unsigned long)MODULE_ASCLIN5.TXFIFOCON.U,
                   (unsigned long)MODULE_ASCLIN5.BRG.U,
                   (unsigned long)MODULE_ASCLIN5.BITCON.U,
                   (unsigned long)MODULE_ASCLIN5.DATCON.U,
                   (unsigned long)MODULE_ASCLIN5.LIN.CON.U);
        shellPrint(shell, "ASC10: CLC=0x%08lX TXFIFO=0x%08lX BRG=0x%08lX BITCON=0x%08lX DATCON=0x%08lX LINCON=0x%08lX\r\n",
                   (unsigned long)MODULE_ASCLIN10.CLC.U,
                   (unsigned long)MODULE_ASCLIN10.TXFIFOCON.U,
                   (unsigned long)MODULE_ASCLIN10.BRG.U,
                   (unsigned long)MODULE_ASCLIN10.BITCON.U,
                   (unsigned long)MODULE_ASCLIN10.DATCON.U,
                   (unsigned long)MODULE_ASCLIN10.LIN.CON.U);
        shellPrint(shell, "FEN 4/5/10: 0x%08lX/0x%08lX/0x%08lX  FLAGS 4/5/10: 0x%08lX/0x%08lX/0x%08lX\r\n",
                   (unsigned long)MODULE_ASCLIN4.FLAGSENABLE.U,
                   (unsigned long)MODULE_ASCLIN5.FLAGSENABLE.U,
                   (unsigned long)MODULE_ASCLIN10.FLAGSENABLE.U,
                   (unsigned long)MODULE_ASCLIN4.FLAGS.U,
                   (unsigned long)MODULE_ASCLIN5.FLAGS.U,
                   (unsigned long)MODULE_ASCLIN10.FLAGS.U);
        shellPrint(shell, "CSR 4/5/10/11: 0x%08lX/0x%08lX/0x%08lX/0x%08lX  FRAMECON 4/5/10: 0x%08lX/0x%08lX/0x%08lX\r\n",
                   (unsigned long)MODULE_ASCLIN4.CSR.U,
                   (unsigned long)MODULE_ASCLIN5.CSR.U,
                   (unsigned long)MODULE_ASCLIN10.CSR.U,
                   (unsigned long)MODULE_ASCLIN11.CSR.U,
                   (unsigned long)MODULE_ASCLIN4.FRAMECON.U,
                   (unsigned long)MODULE_ASCLIN5.FRAMECON.U,
                   (unsigned long)MODULE_ASCLIN10.FRAMECON.U);
    }
    return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linreg, cmd_linreg, register dump);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linact, cmd_linact, AC edge census);

/* ---------- linpulse: RX pulse timing during a real header ---------- */
static int cmd_linpulse(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    long tx = 11, rx = 10;
    char *end;
    uint32 stamps[64];
    uint32 n, i;

    if (argc >= 2) {
        tx = strtol(argv[1], &end, 10);
        if (*end != '\0' || tx < 1 || tx > 11) {
            shellPrint(shell, "Usage: linpulse <txch> [rxch]\r\n");
            return -1;
        }
    }
    if (argc >= 3) {
        rx = strtol(argv[2], &end, 10);
        if (*end != '\0' || rx < 1 || rx > 11) {
            shellPrint(shell, "Usage: linpulse <txch> [rxch]\r\n");
            return -1;
        }
    }
    lin_roles_for((linChannel)tx);
    n = lin_pulse_capture((linChannel)tx, (linChannel)rx, lin_pid(0x12), stamps);
    shellPrint(shell, "linpulse TX=LIN%ld RX=LIN%ld: %lu edges, intervals_us:", tx, rx, (unsigned long)n);
    for (i = 1; i < n && i < 40; i++) {
        uint32 dt = (stamps[i] - stamps[i - 1]) / 100u; /* 100 MHz -> us */
        shellPrint(shell, " %lu", (unsigned long)dt);
    }
    shellPrint(shell, "\r\n  (19200: bit=52us; break~680us; sync 0x55 alternating 52us)\r\n");
    return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linpulse, cmd_linpulse, RX pulse timing);

/* ---------- linforen: raw header + RHE-only poll + FLAGS snapshot ---------- */
static int cmd_linforen(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    long tx = 11, rx = 10;
    char *end;
    uint8 rxPid = 0;
    uint32 flags = 0;
    int rc;

    if (argc >= 2) {
        tx = strtol(argv[1], &end, 10);
        if (*end != '\0' || tx < 1 || tx > 11) {
            shellPrint(shell, "Usage: linforen [txch] [rxch]\r\n");
            return -1;
        }
    }
    if (argc >= 3) {
        rx = strtol(argv[2], &end, 10);
        if (*end != '\0' || rx < 1 || rx > 11) {
            shellPrint(shell, "Usage: linforen [txch] [rxch]\r\n");
            return -1;
        }
    }
    lin_roles_for((linChannel)tx);
    rc = lin_rawhdr_forensic((linChannel)tx, (linChannel)rx, lin_pid(0x12), &rxPid, &flags);
    shellPrint(shell, "linforen TX=LIN%ld RX=LIN%ld: RHE=%s pid=0x%02X FLAGS=0x%08lX\r\n",
               tx, rx, rc ? "MISS" : "HIT", rxPid, (unsigned long)flags);
    shellPrint(shell, "  bit: 0=THE 1=TRE 2=RHE 3=RRE 5=FED 6=RED 16=PE 17=TC 18=FE 19=HT 20=RT 21=BD 22=LP 23=LA 24=LC 25=CE 26=RFO 30=TFO\r\n");
    return rc;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linforen, cmd_linforen, forensic header test);

/* ---------- lintrace: FLAGS/TXFIFO time series after THRQS ---------- */
static int cmd_lintrace(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    long ch = 10;
    char *end;
    uint32 fl[60], ff[60];
    int i;

    if (argc >= 2) {
        ch = strtol(argv[1], &end, 10);
        if (*end != '\0' || ch < 1 || ch > 11) {
            shellPrint(shell, "Usage: lintrace [master 1..11]\r\n");
            return -1;
        }
    }
    lin_roles_for((linChannel)ch);
    lin_tx_trace((linChannel)ch, lin_pid(0x12), fl, ff);
    shellPrint(shell, "lintrace LIN%ld (~100us/sample, FLAGS/TXFIFO-fill):\r\n", ch);
    for (i = 0; i < 60; i += 2) {
        shellPrint(shell, "  %02d: F=0x%08lX FIFO=0x%08lX\r\n", i,
                   (unsigned long)fl[i], (unsigned long)ff[i]);
        if (i % 10 == 8) shellPrint(shell, "\r\n");
    }
    return 0;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), lintrace, cmd_lintrace, TX flag trace);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linerr, cmd_linerr, error injection);
