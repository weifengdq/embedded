/* shell_can.c — letter-shell CAN test commands (can-utils style).
 *
 * Ported subset of can-utils cansend/candump syntax to letter-shell:
 *   cansend <ch> <frame>   e.g. cansend 0 123#DEADBEEF
 *                              cansend 0 12345678#112233 (extended)
 *                              cansend 0 123#R / 123#R4 (remote)
 *                              cansend 0 123##1DEADBEEF (FD, flags bit0=BRS)
 *   candump [ch|all] [n]   dump buffered RX frames (default all, 20)
 *   canlive <on|off>        live-print every RX frame from main loop
 *   canstat [ch]            counters + TEC/REC + NBTP/DBTP + fMCAN
 *   canpair [rounds] [len]  paired loopback test over wired pairs
 *   canflood <ch> <n> [len] burst TX test
 *   canrst [ch|all]         bus-off recovery (INIT toggle)
 *   xcvr                    transceiver pin states + nFAULT
 */
#include "shell.h"
#include "shell_can.h"
#include "can12.h"
#include "IfxPort.h"
#include "IfxScuCcu.h"
#include "IfxCan_reg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

extern volatile uint32 g_TickCount_1ms;

static uint8 g_canlive = 0;

uint8 shell_can_live_enabled(void) { return g_canlive; }

/* ---------- frame parser (can-utils cansend subset) ---------- */
typedef struct {
    uint32_t id;
    uint8 ext, rtr, fd, brs;
    uint8 dlc, len;
    uint8 data[64];
} can_frame_req_t;

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse even-length hex (dots skipped) into buf. Returns bytes or -1. */
static int parse_hexbytes(const char *s, uint8 *buf, int maxlen)
{
    int n = 0, hi = -1;
    for (; *s && n < maxlen; s++) {
        if (*s == '.') continue;
        int v = hexval(*s);
        if (v < 0) return -1;
        if (hi < 0) hi = v;
        else { buf[n++] = (uint8)((hi << 4) | v); hi = -1; }
    }
    if (hi >= 0) return -1;   /* odd nibble */
    if (*s) return -1;        /* overflow */
    return n;
}

/* Parse "<id>#..." per cansend. Returns 0 on success. */
static int parse_can_frame(const char *s, can_frame_req_t *out)
{
    const char *hash;
    char idbuf[16];
    size_t idlen;
    unsigned long id;
    char *end;

    memset(out, 0, sizeof(*out));
    hash = strchr(s, '#');
    if (hash == NULL) return -1;
    idlen = (size_t)(hash - s);
    if (idlen == 0 || idlen > 8) return -1;
    if (idlen >= sizeof(idbuf)) return -1;
    memcpy(idbuf, s, idlen);
    idbuf[idlen] = '\0';
    id = strtoul(idbuf, &end, 16);
    if (*end != '\0') return -1;
    out->id = (uint32_t)id;
    /* Extended if >3 hex digits or value > 0x7FF */
    out->ext = (idlen > 3 || id > 0x7FFu) ? 1 : 0;
    if (!out->ext && id > 0x7FFu) return -1;
    if (out->ext && id > 0x1FFFFFFFu) return -1;

    s = hash + 1;
    if (*s == '#') {
        /* CAN FD: ##<flags><data> */
        int fl;
        int n;
        s++;
        fl = hexval(*s);
        if (fl < 0) return -1;
        s++;
        out->fd = 1;
        out->brs = (fl & 0x01) ? 1 : 0;
        n = parse_hexbytes(s, out->data, 64);
        if (n < 0) return -1;
        out->len = (uint8)n;
        out->dlc = can_len2dlc(out->len);
        return 0;
    }
    if (*s == 'R' || *s == 'r') {
        /* Remote: R[len] */
        s++;
        out->rtr = 1;
        if (*s) {
            char *e2;
            long dl = strtol(s, &e2, 10);
            if (*e2 != '\0' || dl < 0 || dl > 8) return -1;
            out->dlc = (uint8)dl;
        } else {
            out->dlc = 0;
        }
        out->len = 0;
        return 0;
    }
    /* Classic data (optional _dlc suffix ignored for derivation) */
    {
        char databuf[160];
        char *us = strchr(s, '_');
        size_t dl = us ? (size_t)(us - s) : strlen(s);
        int n;
        if (dl >= sizeof(databuf)) return -1;
        memcpy(databuf, s, dl);
        databuf[dl] = '\0';
        n = parse_hexbytes(databuf, out->data, 8);
        if (n < 0) return -1;
        out->len = (uint8)n;
        out->dlc = out->len;   /* classic: DLC == len */
        return 0;
    }
}

static int parse_ch(const char *s, canChannel *ch)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*end != '\0' || v < 0 || v >= CAN_NUM) return -1;
    *ch = (canChannel)v;
    return 0;
}

/* Print one frame in cansend-compatible notation. */
static void print_frame(Shell *shell, canChannel ch, const can12_frame_t *f)
{
    int i;
    shellPrint(shell, "[%lu] can%d ", (unsigned long)f->tick, (int)ch);
    if (f->ext) shellPrint(shell, "%08lX", (unsigned long)f->id);
    else        shellPrint(shell, "%03lX", (unsigned long)f->id);
    if (f->fd) {
        shellPrint(shell, "##%X", f->brs ? 1 : 0);
    } else {
        shellPrint(shell, "#");
    }
    if (f->rtr && !f->fd) {
        shellPrint(shell, "R%d", f->dlc);
    } else {
        for (i = 0; i < f->len; i++) shellPrint(shell, "%02X", f->data[i]);
    }
    if (f->fd) shellPrint(shell, " (FD%s len=%d dlc=%d)", f->brs ? "+BRS" : "", f->len, f->dlc);
    shellPrint(shell, "\r\n");
}

/* Called from main loop when live mode is on: dump all pending rings. */
void shell_can_live_dump(void)
{
    Shell *shell;
    canChannel ch;
    can12_frame_t f;
    if (!g_canlive) return;
    shell = shellGetCurrent();
    if (shell == NULL) return;
    for (ch = CAN0; ch < CAN_NUM; ch++) {
        while (can12_ring_pop(ch, &f)) print_frame(shell, ch, &f);
    }
}

/* ---------- cansend ---------- */
static int cmd_cansend(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    canChannel ch;
    can_frame_req_t req;
    IfxCan_Status st;
    int retries = 0;

    if (argc != 3) {
        shellPrint(shell, "Usage: cansend <ch 0..11> <frame>\r\n");
        shellPrint(shell, "  classic : 123#DEADBEEF / 12345678#1122 (ext) / 123#R[Rlen]\r\n");
        shellPrint(shell, "  FD      : 123##<flags><data>  flags bit0=BRS, e.g. 123##1DEADBEEF\r\n");
        shellPrint(shell, "Wired pairs: 0-1 2-3 4-5 6-7 8-9 10-11\r\n");
        return -1;
    }
    if (parse_ch(argv[1], &ch) != 0) {
        shellPrint(shell, "Bad channel '%s' (0..11)\r\n", argv[1]);
        return -1;
    }
    if (parse_can_frame(argv[2], &req) != 0) {
        shellPrint(shell, "Bad frame '%s'\r\n", argv[2]);
        return -1;
    }
    do {
        st = can12_send(ch, req.id, req.ext, req.rtr, req.fd, req.brs, req.dlc, req.data);
        if (st == IfxCan_Status_ok) break;
        retries++;
    } while (retries < 1000);
    if (st != IfxCan_Status_ok) {
        shellPrint(shell, "cansend: TX FIFO full (busy)\r\n");
        return -1;
    }
    shellPrint(shell, "can%d ", (int)ch);
    {
        can12_frame_t f;
        memset(&f, 0, sizeof(f));
        f.id = req.id; f.ext = req.ext; f.rtr = req.rtr;
        f.fd = req.fd; f.brs = req.brs; f.dlc = req.dlc; f.len = req.len;
        memcpy(f.data, req.data, req.len);
        f.tick = g_TickCount_1ms;
        /* reuse printer without tick prefix duplication */
        shellPrint(shell, "sent: ");
        if (f.ext) shellPrint(shell, "%08lX", (unsigned long)f.id);
        else       shellPrint(shell, "%03lX", (unsigned long)f.id);
        if (f.fd) shellPrint(shell, "##%X", f.brs ? 1 : 0);
        else shellPrint(shell, "#");
        if (f.rtr && !f.fd) shellPrint(shell, "R%d", f.dlc);
        else { int i; for (i = 0; i < f.len; i++) shellPrint(shell, "%02X", f.data[i]); }
        shellPrint(shell, "\r\n");
    }
    return 0;
}

/* ---------- candump ---------- */
static int cmd_candump(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    int ch_first = 0, ch_last = CAN_NUM - 1;
    int n = 20, shown = 0;
    canChannel ch;
    can12_frame_t f;

    if (argc >= 2) {
        if (strcmp(argv[1], "all") == 0) { ch_first = 0; ch_last = CAN_NUM - 1; }
        else {
            canChannel c;
            if (parse_ch(argv[1], &c) != 0) {
                shellPrint(shell, "Usage: candump [ch|all] [n]\r\n");
                return -1;
            }
            ch_first = ch_last = (int)c;
        }
    }
    if (argc >= 3) {
        n = atoi(argv[2]);
        if (n <= 0) n = 20;
        if (n > 256) n = 256;
    }
    for (ch = (canChannel)ch_first; ch <= (canChannel)ch_last && shown < n; ch++) {
        while (shown < n && can12_ring_pop(ch, &f)) {
            print_frame(shell, ch, &f);
            shown++;
        }
    }
    shellPrint(shell, "candump: %d frame(s)", shown);
    if (g_canlive) shellPrint(shell, " (live on: also printing in main loop)");
    shellPrint(shell, "\r\n");
    return 0;
}

static int cmd_canlive(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    if (argc >= 2 && (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "1") == 0)) {
        g_canlive = 1;
        shellPrint(shell, "canlive on: RX frames print as they arrive\r\n");
    } else if (argc >= 2 && (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "0") == 0)) {
        g_canlive = 0;
        shellPrint(shell, "canlive off\r\n");
    } else {
        shellPrint(shell, "canlive is %s. Usage: canlive <on|off>\r\n", g_canlive ? "on" : "off");
    }
    return 0;
}

/* ---------- canstat ---------- */
static int cmd_canstat(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    int ch_first = 0, ch_last = CAN_NUM - 1;
    canChannel ch;

    if (argc >= 2) {
        canChannel c;
        if (parse_ch(argv[1], &c) != 0) {
            shellPrint(shell, "Usage: canstat [ch]\r\n");
            return -1;
        }
        ch_first = ch_last = (int)c;
    }
    shellPrint(shell, "fMCAN=%.2f MHz nFAULT=%d live=%s\r\n",
               (double)IfxScuCcu_getMcanFrequency() / 1e6f,
               xcvr_fault(), g_canlive ? "on" : "off");
    shellPrint(shell, "ch | rx/tx/ovf/bo/rst | pend/drop | TEC REC BO | NBTP DBTP\r\n");
    for (ch = (canChannel)ch_first; ch <= (canChannel)ch_last; ch++) {
        mcmcanType *d = &g_can[ch];
        uint8 tec = d->node.node->ECR.B.TEC;
        uint8 rec = d->node.node->ECR.B.REC;
        uint32 psr = d->node.node->PSR.U;
        shellPrint(shell, "%2d | %lu/%lu/%lu/%lu/%lu | %lu/%lu | %u %u %d | 0x%08lX 0x%08lX\r\n",
                   (int)ch,
                   (unsigned long)d->rxCount, (unsigned long)d->txCount,
                   (unsigned long)d->overflowCount, (unsigned long)d->boCount,
                   (unsigned long)d->restartCount,
                   (unsigned long)can12_ring_pending(ch), (unsigned long)d->rxDrop,
                   tec, rec, d->busOff ? 1 : 0,
                   (unsigned long)d->node.node->NBTP.U,
                   (unsigned long)d->node.node->DBTP.U);
        (void)psr;
    }
    return 0;
}

/* ---------- canpair: wired-pair loopback test ---------- */
static uint32 wait_ms(uint32 ms)
{
    uint32 start = g_TickCount_1ms;
    while ((g_TickCount_1ms - start) < ms) { }
    return g_TickCount_1ms - start;
}

static int pair_one(Shell *shell, canChannel tx, canChannel rx,
                    uint32 id, const uint8 *data, uint8 len, uint8 fd, uint8 brs)
{
    can12_frame_t f;
    uint32 t0;
    uint8 dlc = fd ? can_len2dlc(len) : len;
    /* flush stale frames on the receiver */
    while (can12_ring_pop(rx, &f)) { }
    if (can12_send(tx, id, 0, 0, fd, brs, dlc, data) != IfxCan_Status_ok) {
        shellPrint(shell, "  can%d->can%d: TX busy FAIL\r\n", (int)tx, (int)rx);
        return -1;
    }
    t0 = g_TickCount_1ms;
    while ((g_TickCount_1ms - t0) < 200u) {
        can12_poll_channel(rx);
        can12_poll_channel(tx);
        if (can12_ring_pending(rx)) break;
    }
    if (!can12_ring_pop(rx, &f)) {
        shellPrint(shell, "  can%d->can%d: TIMEOUT FAIL\r\n", (int)tx, (int)rx);
        return -1;
    }
    if (f.id != id || f.len != len || memcmp(f.data, data, len) != 0) {
        shellPrint(shell, "  can%d->can%d: MISMATCH FAIL (got id=%lX len=%d)\r\n",
                   (int)tx, (int)rx, (unsigned long)f.id, f.len);
        return -1;
    }
    shellPrint(shell, "  can%d->can%d: PASS (id=%lX len=%d %s)\r\n",
               (int)tx, (int)rx, (unsigned long)id, len, fd ? (brs ? "FD+BRS" : "FD") : "CC");
    return 0;
}

static int cmd_canpair(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    int rounds = 1, len = 8, fd = 1, r, p;
    int fails = 0;
    static const uint8 pairs[6][2] = {{0,1},{2,3},{4,5},{6,7},{8,9},{10,11}};
    uint8 data[64];
    int i;

    if (argc >= 2) { rounds = atoi(argv[1]); if (rounds < 1) rounds = 1; if (rounds > 20) rounds = 20; }
    if (argc >= 3) { len = atoi(argv[2]); if (len < 1) len = 1; if (len > 64) len = 64; }
    if (argc >= 4) { fd = atoi(argv[3]) ? 1 : 0; }
    for (i = 0; i < (int)sizeof(data); i++) data[i] = (uint8)(0xA0 + i);

    shellPrint(shell, "canpair: %d round(s), len=%d %s, pairs 0-1..10-11\r\n",
               rounds, len, fd ? "FD+BRS" : "classic");
    for (r = 0; r < rounds; r++) {
        shellPrint(shell, "round %d:\r\n", r + 1);
        for (p = 0; p < 6; p++) {
            uint32 id = 0x100u + (uint32)(p * 2);
            uint8 plen = (uint8)len;
            uint8 usefd = (uint8)fd;
            uint8 usebrs = (uint8)fd;
            if (!usefd && plen > 8) plen = 8;
            if (pair_one(shell, (canChannel)pairs[p][0], (canChannel)pairs[p][1],
                         id, data, plen, usefd, usebrs) != 0) fails++;
            wait_ms(5);
            if (pair_one(shell, (canChannel)pairs[p][1], (canChannel)pairs[p][0],
                         id + 0x40u, data, plen, usefd, usebrs) != 0) fails++;
            wait_ms(5);
        }
    }
    shellPrint(shell, "canpair done: %s (%d fail(s))\r\n", fails ? "FAIL" : "ALL PASS", fails);
    return fails ? -1 : 0;
}

/* ---------- canflood ---------- */
static int cmd_canflood(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    canChannel ch;
    int count, len = 8, i, sent = 0, busy = 0;
    uint8 data[64];
    uint32 t0, dt;

    if (argc < 3) {
        shellPrint(shell, "Usage: canflood <ch> <count> [len 1..64] [fd 0|1]\r\n");
        return -1;
    }
    if (parse_ch(argv[1], &ch) != 0) { shellPrint(shell, "Bad channel\r\n"); return -1; }
    count = atoi(argv[2]);
    if (count < 1) count = 1;
    if (count > 100000) count = 100000;
    if (argc >= 4) { len = atoi(argv[3]); if (len < 1) len = 1; if (len > 64) len = 64; }
    {
        int fd = (argc >= 5) ? (atoi(argv[4]) ? 1 : 0) : (len > 8 ? 1 : 0);
        uint8 usefd = (uint8)fd, usebrs = (uint8)fd;
        uint8 dlc = usefd ? can_len2dlc((uint8)len) : (uint8)len;
        if (!usefd && len > 8) len = 8;
        for (i = 0; i < (int)sizeof(data); i++) data[i] = (uint8)i;
        t0 = g_TickCount_1ms;
        for (i = 0; i < count; i++) {
            if (can12_send(ch, 0x123u, 0, 0, usefd, usebrs, dlc, data) == IfxCan_Status_ok) sent++;
            else busy++;
        }
        dt = g_TickCount_1ms - t0;
        shellPrint(shell, "canflood can%d: %d/%d queued (%d busy) in %lu ms\r\n",
                   (int)ch, sent, count, busy, (unsigned long)dt);
    }
    return 0;
}

/* ---------- canrst / xcvr ---------- */
static int cmd_canrst(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    int ch_first = 0, ch_last = CAN_NUM - 1, n = 0;
    canChannel ch;

    if (argc >= 2 && strcmp(argv[1], "all") != 0) {
        canChannel c;
        if (parse_ch(argv[1], &c) != 0) {
            shellPrint(shell, "Usage: canrst [ch|all]\r\n");
            return -1;
        }
        ch_first = ch_last = (int)c;
    }
    for (ch = (canChannel)ch_first; ch <= (canChannel)ch_last; ch++) {
        n += (int)can12_restart_channel(ch, TRUE);
    }
    shellPrint(shell, "canrst: restarted %d node(s)\r\n", n);
    return 0;
}

static int cmd_xcvr(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    (void)argc; (void)argv;
    shellPrint(shell, "CAN0 TCAN1043: EN P33.1=%d nSTB P33.0=%d nFAULT P10.3=%d (%s)\r\n",
               (int)IfxPort_getPinState(&MODULE_P33, 1),
               (int)IfxPort_getPinState(&MODULE_P33, 0),
               (int)IfxPort_getPinState(&MODULE_P10, 3),
               xcvr_fault() ? "FAULT" : "ok");
    shellPrint(shell, "TCAN1044 STB: P21.5(CAN1-3)=%d P21.2(CAN4-7)=%d P21.4(CAN8-11)=%d (0=normal)\r\n",
               (int)IfxPort_getPinState(&MODULE_P21, 5),
               (int)IfxPort_getPinState(&MODULE_P21, 2),
               (int)IfxPort_getPinState(&MODULE_P21, 4));
    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), cansend, cmd_cansend, cansend ch frame);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), candump, cmd_candump, candump [ch] [n]);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), canlive, cmd_canlive, live RX print);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), canstat, cmd_canstat, CAN status);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), canpair, cmd_canpair, pair loopback test);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), canflood, cmd_canflood, burst TX test);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), canrst, cmd_canrst, restart node);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), xcvr, cmd_xcvr, transceiver state);
