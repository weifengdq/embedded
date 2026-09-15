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

/* ---------- linsend ---------- */
static int cmd_linsend(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint8 id6, data[8], classic = 0;
    int len = 8, i, fails;
    uint32 mask = 0;

    if (argc < 2) {
        shellPrint(shell, "Usage: linsend <id 0..63> [hexdata up to 8B] [classic 0|1]\r\n");
        shellPrint(shell, "  e.g. linsend 0x12 A0A1A2A3A4A5A6A7\r\n");
        shellPrint(shell, "  Master LIN11 broadcasts; LIN1..LIN10 verify header+response.\r\n");
        return -1;
    }
    if (parse_id6(argv[1], &id6) != 0) {
        shellPrint(shell, "Bad id '%s' (0..63, hex ok)\r\n", argv[1]);
        return -1;
    }
    for (i = 0; i < 8; i++) data[i] = (uint8)(0xA0 + i);
    if (argc >= 3) {
        int n = parse_hexbytes(argv[2], data, 8);
        if (n < 0) {
            shellPrint(shell, "Bad hexdata '%s'\r\n", argv[2]);
            return -1;
        }
        len = (n == 0) ? 8 : n;
        if (argc >= 4) classic = (atoi(argv[3]) != 0) ? 1 : 0;
    } else {
        /* IDs 0x3C/0x3D default to classic checksum per LIN spec */
        if (id6 >= 0x3C) classic = 1;
    }
    fails = lin_xact_master_tx(id6, data, (uint8)len, classic, &mask);
    shellPrint(shell, "linsend id=0x%02X pid=0x%02X len=%d %s: %s (%d fail(s), mask=0x%03lX)\r\n",
               id6, lin_pid(id6), len, classic ? "classic" : "enhanced",
               fails ? "FAIL" : "ALL PASS", fails, (unsigned long)mask);
    shellPrint(shell, "  data: ");
    print_data(shell, data, len);
    shellPrint(shell, "\r\n");
    return fails ? -1 : 0;
}

/* ---------- linreq ---------- */
static int cmd_linreq(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint8 id6, data[8];
    linChannel slave = LIN1;
    int len = 8, i, rc;
    uint32 snoop = 0;

    if (argc < 2) {
        shellPrint(shell, "Usage: linreq <id 0..63> <slave 1..10> [len 1..8] [hexdata]\r\n");
        shellPrint(shell, "  e.g. linreq 0x20 3 8 1122334455667788\r\n");
        shellPrint(shell, "  Master sends header; slave responds; master verifies.\r\n");
        return -1;
    }
    if (parse_id6(argv[1], &id6) != 0) {
        shellPrint(shell, "Bad id '%s'\r\n", argv[1]);
        return -1;
    }
    if (argc >= 3 && parse_ch(argv[2], &slave) != 0) {
        shellPrint(shell, "Bad slave '%s' (1..10)\r\n", argv[2]);
        return -1;
    }
    if (argc >= 4) {
        len = atoi(argv[3]);
        if (len < 1) len = 1;
        if (len > 8) len = 8;
    }
    for (i = 0; i < 8; i++) data[i] = (uint8)(0x50 + id6 + i);
    if (argc >= 5) {
        int n = parse_hexbytes(argv[4], data, 8);
        if (n < 0) {
            shellPrint(shell, "Bad hexdata '%s'\r\n", argv[4]);
            return -1;
        }
        if (n > 0) len = n;
    }
    {
        uint8 classic = (id6 >= 0x3C) ? 1 : 0;
        rc = lin_xact_slave_tx(id6, slave, data, (uint8)len, classic, &snoop);
    }
    shellPrint(shell, "linreq id=0x%02X slave=LIN%d len=%d: %s (snoop %lu/9)\r\n",
               id6, (int)slave, len, rc ? "FAIL" : "PASS", (unsigned long)snoop);
    shellPrint(shell, "  data: ");
    print_data(shell, data, len);
    shellPrint(shell, "\r\n");
    return rc ? -1 : 0;
}

/* ---------- linpair ---------- */
static int cmd_linpair(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    int rounds = 1, len = 8, r, k, fails = 0, total = 0;
    uint8 data[8];
    int i;

    if (argc >= 2) { rounds = atoi(argv[1]); if (rounds < 1) rounds = 1; if (rounds > 20) rounds = 20; }
    if (argc >= 3) { len = atoi(argv[2]); if (len < 1) len = 1; if (len > 8) len = 8; }
    shellPrint(shell, "linpair: %d round(s), len=%d, master LIN11 -> slaves LIN1..10\r\n", rounds, len);
    for (r = 0; r < rounds; r++) {
        shellPrint(shell, "round %d:\r\n", r + 1);
        for (k = 0; k < 10; k++) {
            uint8 id6 = (uint8)(0x10 + k);
            uint32 mask = 0;
            int f;
            for (i = 0; i < len; i++) data[i] = (uint8)(0xA0 + r * 16 + k * 2 + i);
            f = lin_xact_master_tx(id6, data, (uint8)len, 0, &mask);
            total++;
            if (f) {
                fails++;
                shellPrint(shell, "  id=0x%02X: FAIL (%d slave(s), mask=0x%03lX)\r\n",
                           id6, f, (unsigned long)mask);
            } else {
                shellPrint(shell, "  id=0x%02X: PASS (10/10)\r\n", id6);
            }
        }
    }
    shellPrint(shell, "linpair done: %s (%d/%d id(s) failed)\r\n",
               fails ? "FAIL" : "ALL PASS", fails, total);
    return fails ? -1 : 0;
}

/* ---------- linslv ---------- */
static int cmd_linslv(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    int rounds = 1, len = 8, r, s, fails = 0, total = 0;
    uint8 data[8];
    int i;

    if (argc >= 2) { rounds = atoi(argv[1]); if (rounds < 1) rounds = 1; if (rounds > 10) rounds = 10; }
    if (argc >= 3) { len = atoi(argv[2]); if (len < 1) len = 1; if (len > 8) len = 8; }
    shellPrint(shell, "linslv: %d round(s), len=%d, each LIN1..10 responds in turn\r\n", rounds, len);
    for (r = 0; r < rounds; r++) {
        shellPrint(shell, "round %d:\r\n", r + 1);
        for (s = 1; s <= 10; s++) {
            uint8 id6 = (uint8)(0x20 + s);
            uint32 snoop = 0;
            int rc;
            for (i = 0; i < len; i++) data[i] = (uint8)(0x50 + r * 16 + s + i);
            rc = lin_xact_slave_tx(id6, (linChannel)s, data, (uint8)len, 0, &snoop);
            total++;
            if (rc) {
                fails++;
                shellPrint(shell, "  LIN%d -> LIN11 id=0x%02X: FAIL\r\n", s, id6);
            } else {
                shellPrint(shell, "  LIN%d -> LIN11 id=0x%02X: PASS (snoop %lu/9)\r\n",
                           s, id6, (unsigned long)snoop);
            }
        }
    }
    shellPrint(shell, "linslv done: %s (%d/%d failed)\r\n",
               fails ? "FAIL" : "ALL PASS", fails, total);
    return fails ? -1 : 0;
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
        shellPrint(shell, "Usage: linbaud <9600|10417|19200>  (current %.0f)\r\n", (double)g_linBaud);
        return -1;
    }
    rate = strtol(argv[1], &end, 10);
    if (*end != '\0' || (rate != 9600 && rate != 10417 && rate != 19200)) {
        shellPrint(shell, "Bad rate '%s' (9600/10417/19200)\r\n", argv[1]);
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
        uint8 pid;
        int badSeen = 0;
        linChannel ch;
        for (ch = LIN1; ch <= LIN10; ch++) lin_slave_arm_header(ch);
        shellPrint(shell, "linerr parity: sending bad pid=0x%02X (good 0x%02X)...\r\n", bad, good);
        if (lin_raw_master_header(bad) != 0) {
            shellPrint(shell, "  master header TX failed (bus ok? try 'linpair')\r\n");
            return -1;
        }
        for (ch = LIN1; ch <= LIN10; ch++) {
            uint32 pe0 = g_lin[ch].parityErr;
            if (lin_slave_poll_header(ch, &pid) == 0) {
                if (lin_pid_check(pid) != 0) badSeen++;
            }
            if (g_lin[ch].parityErr > pe0) badSeen++;
        }
        shellPrint(shell, "  slaves with parity indication: %d/10 (expect 10; check 'linstat' par column)\r\n", badSeen);
        return 0;
    } else if (strcmp(argv[1], "cksum") == 0) {
        /* Master sends classic while slaves are armed enhanced for id 0x12.
         * All 10 slaves must flag LIN checksum error (LC). */
        uint32 cs0[LIN_NUM], mask = 0;
        linChannel ch;
        int csSeen = 0, fails;
        for (ch = LIN0; ch < LIN_NUM; ch++) cs0[ch] = g_lin[ch].cksumErr;
        shellPrint(shell, "linerr cksum: id=0x12 master classic vs slaves enhanced...\r\n");
        fails = lin_xact_master_tx_mode(0x12, data, 8, 1, 0, &mask);
        for (ch = LIN1; ch <= LIN10; ch++) {
            if (g_lin[ch].cksumErr > cs0[ch]) csSeen++;
        }
        shellPrint(shell, "  xact fails=%d mask=0x%03lX, slaves with LC flag: %d/10 (expect 10)\r\n",
                   fails, (unsigned long)mask, csSeen);
        return (csSeen == 10) ? 0 : -1;
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

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linsend, cmd_linsend, master broadcast test);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linreq, cmd_linreq, slave response test);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linpair, cmd_linpair, master->slaves loop);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linslv, cmd_linslv, slaves->master loop);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linstat, cmd_linstat, LIN status);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), lindump, cmd_lindump, last LIN frames);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linbaud, cmd_linbaud, LIN baudrate);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linslp, cmd_linslp, transceiver sleep);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linwake, cmd_linwake, transceiver wake);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linerr, cmd_linerr, error injection);
