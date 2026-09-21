/* shell_flexray.c — letter-shell front-end for the dual-ERAY self-test driver.
 *
 * Commands (all share one handler, `fr` is the short alias):
 *   fr               : usage
 *   fr init          : blocking bring-up of FR0A+FR1A (coldstart, verbose)
 *   fr status        : POC/CCSV/SUCC1/EIR/SIR + counters for both nodes
 *   fr send <0|1> [hex16] : transmit one static frame (default: seq pattern)
 *   fr recv <0|1>    : show last valid received frame of a node
 *   fr test [n]      : automated FR0->FR1 + FR1->FR0 loopback check (default 5)
 *   fr regs          : raw register dump (SUCC1/CCSV/CCEV/EIR/SIR/TEST1/MHDS)
 */
#include "shell.h"
#include "shell_port.h"
#include "flexray_dual.h"
#include "IfxPort.h"
#include "IfxPort_reg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



static void fr_msleep(uint32_t ms)
{
    uint32_t s = g_TickCount_1ms;
    while ((g_TickCount_1ms - s) < ms) {}
}

static void fr_print_status(Shell *sh)
{
    for (int n = 0; n < 2; n++) {
        int poc = frd_node_poc(n);
        const FrdCounters *c = frd_counters(n);
        if (poc < 0) {
            shellPrint(sh, "%s: not prepared (run 'fr init')\r\n", frd_node_name(n));
            continue;
        }
        shellPrint(sh, "%s: POC=%d(%s) CCSV=0x%08lX SUCC1=0x%08lX EIR=0x%08lX SIR=0x%08lX\r\n",
                   frd_node_name(n), poc, frd_poc_name(poc),
                   (unsigned long)frd_node_ccsv(n), (unsigned long)frd_node_succ1(n),
                   (unsigned long)frd_node_eir(n), (unsigned long)frd_node_sir(n));
        if (c != NULL) {
            shellPrint(sh, "  txOk=%lu txBusy=%lu rxOk=%lu rxErr=%lu rxNull=%lu eirSeen=%lu lastEir=0x%08lX\r\n",
                       (unsigned long)c->txOk, (unsigned long)c->txBusy,
                       (unsigned long)c->rxOk, (unsigned long)c->rxErr, (unsigned long)c->rxNull,
                       (unsigned long)c->eirCount, (unsigned long)c->lastEir);
            shellPrint(sh, "  lastMbs=0x%08lX rejMbs=0x%08lX rejSlot=%u rejPlw=%u rejD0=0x%08lX\r\n",
                       (unsigned long)c->lastMbs, (unsigned long)c->lastRejMbs,
                       (unsigned)c->lastRejSlot, (unsigned)c->lastRejPayloadWords,
                       (unsigned long)c->lastRejData0);
        }
    }
}

static void fr_print_frame(Shell *sh, int node)
{
    FrdRxFrame f;
    if (frd_last_rx(node, &f) != 0) {
        shellPrint(sh, "%s: no valid frame yet\r\n", frd_node_name(node));
        return;
    }
    shellPrint(sh, "%s last RX: slot=%u cycle=%u len=%u MBS=0x%08lX data=",
               frd_node_name(node), (unsigned)f.slot, (unsigned)f.cycle,
               (unsigned)f.payloadLen, (unsigned long)f.mbs);
    for (int i = 0; i < f.payloadLen; i++) {
        shellPrint(sh, "%02X", f.payload[i]);
    }
    shellPrint(sh, "\r\n");
}

/* Parse up to 16 bytes of hex (32 hex chars, spaces allowed). Returns bytes. */
static int fr_parse_hex(const char *s, uint8_t *out16)
{
    int nibble = -1, count = 0;
    memset(out16, 0, 16);
    for (; *s != '\0'; s++) {
        int v = -1;
        if ((*s >= '0') && (*s <= '9')) {
            v = *s - '0';
        } else if ((*s >= 'a') && (*s <= 'f')) {
            v = *s - 'a' + 10;
        } else if ((*s >= 'A') && (*s <= 'F')) {
            v = *s - 'A' + 10;
        } else {
            continue;
        }
        if (nibble < 0) {
            nibble = v;
        } else {
            if (count < 16) {
                out16[count++] = (uint8_t)((nibble << 4) | v);
            }
            nibble = -1;
        }
    }
    return count;
}

static void fr_print_boot(Shell *sh)
{
    static const char *steps[] = {
        "OK", "HALT", "DEFAULT_CONFIG", "CONFIG", "READY",
        "WAKEUP", "WAKEUP_WAIT", "COLDSTART", "RUN", "NORMAL", "APPLY"
    };
    for (int n = 0; n < 2; n++) {
        const FrdBootInfo *bi = frd_boot_info(n);
        const char *step = " ?";
        int idx = (bi->rc < 0) ? -bi->rc : 0;
        if ((idx >= 0) && (idx <= 10)) {
            step = steps[idx];
        }
        shellPrint(sh, "%s boot: rc=%d@%s poc=%u(%s) SUCC1=0x%08lX CCSV=0x%08lX EIR=0x%08lX\r\n",
                   frd_node_name(n), bi->rc, step,
                   (unsigned)bi->poc, frd_poc_name(bi->poc),
                   (unsigned long)bi->succ1, (unsigned long)bi->ccsv,
                   (unsigned long)bi->eir);
    }
}

static int fr_selftest(Shell *sh, int rounds)
{
    int fails = 0;
    if (rounds <= 0) {
        rounds = 1;
    }
    if (rounds > 50) {
        rounds = 50;
    }

    shellPrint(sh, "FR selftest: bring-up ...\r\n");
    if (frd_startup(1) != 0) {
        shellPrint(sh, "FR selftest: bring-up FAILED\r\n");
        fr_print_boot(sh);
        return -1;
    }
    fr_print_boot(sh);
    shellPrint(sh, "FR selftest: both nodes NORMAL, %d rounds each direction\r\n", rounds);

    for (int r = 0; r < rounds; r++) {
        uint8_t tx[16], want;
        /* Direction A: FR0(slot11) -> FR1. Marker byte r, rest complement. */
        for (int i = 0; i < 16; i++) {
            tx[i] = (uint8_t)((r * 16 + i) & 0xFFU);
        }
        if (frd_send(0, tx) != 0) {
            shellPrint(sh, "[%d] FR0 send FAILED\r\n", r);
            fails++;
            continue;
        }
        want = 0;
        for (int w = 0; w < 10; w++) {
            fr_msleep(20);
            frd_poll();
            {
                FrdRxFrame f;
                if ((frd_last_rx(1, &f) == 0) && (f.slot == 11) &&
                    (memcmp(f.payload, tx, 16) == 0)) {
                    want = 1;
                    break;
                }
            }
        }
        if (want) {
            shellPrint(sh, "[%d] FR0->FR1 slot11 OK\r\n", r);
        } else {
            shellPrint(sh, "[%d] FR0->FR1 slot11 MISMATCH/TIMEOUT\r\n", r);
            fr_print_frame(sh, 1);
            fails++;
        }

        /* Direction B: FR1(slot12) -> FR0, bit-inverted pattern. */
        for (int i = 0; i < 16; i++) {
            tx[i] = (uint8_t)(~((r * 16 + i) & 0xFFU));
        }
        if (frd_send(1, tx) != 0) {
            shellPrint(sh, "[%d] FR1 send FAILED\r\n", r);
            fails++;
            continue;
        }
        want = 0;
        for (int w = 0; w < 10; w++) {
            fr_msleep(20);
            frd_poll();
            {
                FrdRxFrame f;
                if ((frd_last_rx(0, &f) == 0) && (f.slot == 12) &&
                    (memcmp(f.payload, tx, 16) == 0)) {
                    want = 1;
                    break;
                }
            }
        }
        if (want) {
            shellPrint(sh, "[%d] FR1->FR0 slot12 OK\r\n", r);
        } else {
            shellPrint(sh, "[%d] FR1->FR0 slot12 MISMATCH/TIMEOUT\r\n", r);
            fr_print_frame(sh, 0);
            fails++;
        }
    }

    shellPrint(sh, "FR selftest: %s (%d rounds, %d fails)\r\n",
               (fails == 0) ? "PASS" : "FAIL", rounds, fails);
    fr_print_status(sh);
    return (fails == 0) ? 0 : -2;
}

static int cmd_flexray(int argc, char *argv[])
{
    /* NOTE: the letter-shell copy used by tc397_selftest exposes
     * shellGetCurrent(); the standalone tc397_flexray project used Shell_Get(). */
    Shell *sh = shellGetCurrent();
    if (sh == NULL) {
        return -1;
    }

    if (argc < 2) {
        shellPrint(sh, "Usage: flexray <init|status|send|recv|test|regs>\r\n");
        shellPrint(sh, "  fr init            - bring up FR0A+FR1A (blocking, verbose)\r\n");
        shellPrint(sh, "  fr status          - POC/regs + counters of both nodes\r\n");
        shellPrint(sh, "  fr send <0|1> [hex]- TX one frame (default seq pattern)\r\n");
        shellPrint(sh, "  fr recv <0|1>      - show last RX frame of node\r\n");
        shellPrint(sh, "  fr test [n]        - auto loopback both directions (def 5)\r\n");
        shellPrint(sh, "  fr regs            - raw register dump\r\n");
        shellPrint(sh, "Nodes: 0=FR0A(ERAY0 P02.0/02.4/02.1 slot11), 1=FR1A(ERAY1 P14.10/14.9/14.8 slot12)\r\n");
        return 0;
    }

    if (strcmp(argv[1], "init") == 0) {
        int rc;
        shellPrint(sh, "FR bring-up start ...\r\n");
        rc = frd_startup(1);
        shellPrint(sh, "FR bring-up %s (rc=%d)\r\n", (rc == 0) ? "OK" : "FAIL", rc);
        fr_print_boot(sh);
        fr_print_status(sh);
        return rc;
    }
    if (strcmp(argv[1], "status") == 0) {
        frd_poll();
        fr_print_status(sh);
        return 0;
    }
    if (strcmp(argv[1], "send") == 0) {
        int node = (argc >= 3) ? atoi(argv[2]) : -1;
        int rc;
        if ((node != 0) && (node != 1)) {
            shellPrint(sh, "Usage: fr send <0|1> [hex16bytes]\r\n");
            return -1;
        }
        if (argc >= 4) {
            uint8_t bytes[16];
            int n = fr_parse_hex(argv[3], bytes);
            shellPrint(sh, "FR send node %d, %d bytes%s\r\n", node, n,
                       (n != 16) ? " (padded with 00)" : "");
            rc = frd_send(node, bytes);
        } else {
            rc = frd_send(node, NULL);
            shellPrint(sh, "FR send node %d seq-pattern rc=%d TXRQ1=0x%08lX\r\n",
                       node, rc, (unsigned long)frd_node_txrq(node));
        }
        if (rc == -2) {
            shellPrint(sh, "FR not NORMAL yet, run 'fr init' first\r\n");
        }
        return rc;
    }
    if (strcmp(argv[1], "recv") == 0) {
        int node = (argc >= 3) ? atoi(argv[2]) : -1;
        if ((node != 0) && (node != 1)) {
            shellPrint(sh, "Usage: fr recv <0|1>\r\n");
            return -1;
        }
        frd_poll();
        fr_print_frame(sh, node);
        return 0;
    }
    if (strcmp(argv[1], "test") == 0) {
        int n = (argc >= 3) ? atoi(argv[2]) : 5;
        return fr_selftest(sh, n);
    }
    if (strcmp(argv[1], "regs") == 0) {
        for (int node = 0; node < 2; node++) {
            if (frd_node_poc(node) < 0) {
                shellPrint(sh, "%s: not prepared\r\n", frd_node_name(node));
                continue;
            }
            shellPrint(sh, "%s: SUCC1=0x%08lX CCSV=0x%08lX CCEV=0x%08lX EIR=0x%08lX SIR=0x%08lX TEST1=0x%08lX MHDS=0x%08lX ACS=0x%08lX FSR=0x%08lX\r\n",
                       frd_node_name(node),
                       (unsigned long)frd_node_succ1(node), (unsigned long)frd_node_ccsv(node),
                       (unsigned long)frd_node_ccev(node), (unsigned long)frd_node_eir(node),
                       (unsigned long)frd_node_sir(node), (unsigned long)frd_node_test1(node),
                       (unsigned long)frd_node_mhds(node), (unsigned long)frd_node_acs(node),
                       (unsigned long)frd_node_fsr(node));
            shellPrint(sh, "  NDAT1=0x%08lX TXRQ1=0x%08lX MHDS=0x%08lX\r\n",
                       (unsigned long)frd_node_ndat(node), (unsigned long)frd_node_txrq(node),
                       (unsigned long)frd_node_mhds(node));
        }
        return 0;
    }
    if (strcmp(argv[1], "pins") == 0) {
        /* Pin mux + live levels. Expect:
         * FR0: P02.0=ALT6 out PP(TXDA), P02.4=ALT6 out PP(TXENA), P02.1=input(RXDA)
         * FR1: P14.10=ALT7 out PP(TXDA), P14.9=ALT7 out PP(TXENA), P14.8=input(RXDA) */
        uint32_t p02iocr0 = MODULE_P02.IOCR0.U;
        uint32_t p14iocr8 = MODULE_P14.IOCR8.U;
        uint32_t p02in = MODULE_P02.IN.U;
        uint32_t p02seen = 0, p14seen = 0;
        shellPrint(sh, "P02_IOCR0=0x%08lX P14_IOCR8=0x%08lX P02_IN=0x%08lX P14_IN=0x%08lX\r\n",
                   (unsigned long)p02iocr0, (unsigned long)p14iocr8,
                   (unsigned long)MODULE_P02.IN.U, (unsigned long)MODULE_P14.IN.U);
        for (int k = 0; k < 20000; k++) {
            p02seen |= MODULE_P02.IN.U;
            p14seen |= MODULE_P14.IN.U;
        }
        (void)p02in;
        shellPrint(sh, "P02 sticky-high mask=0x%08lX (bit0=TXD0 bit1=RXD0 bit4=TXEN0)\r\n", (unsigned long)p02seen);
        shellPrint(sh, "P14 sticky-high mask=0x%08lX (bit8=RXD1 bit9=TXEN1 bit10=TXD1)\r\n", (unsigned long)p14seen);
        shellPrint(sh, "P02.0=%lu P02.1=%lu P02.4=%lu P14.8=%lu P14.9=%lu P14.10=%lu\r\n",
                   (unsigned long)((p02seen >> 0) & 1U),
                   (unsigned long)((p02seen >> 1) & 1U), (unsigned long)((p02seen >> 4) & 1U),
                   (unsigned long)((p14seen >> 8) & 1U), (unsigned long)((p14seen >> 9) & 1U),
                   (unsigned long)((p14seen >> 10) & 1U));
        return 0;
    }
    if (strcmp(argv[1], "txview") == 0) {
        int node = (argc >= 3) ? atoi(argv[2]) : -1;
        int bi = (argc >= 4) ? atoi(argv[3]) : 0;
        FrdTxView v;
        if (((node != 0) && (node != 1)) || (bi < 0) || (bi > 2) ||
            (frd_tx_view(node, (uint8_t)bi, &v) != 0)) {
            shellPrint(sh, "Usage: fr txview <0|1> [buf 0..2]\r\n");
            return -1;
        }
        shellPrint(sh, "%s buf%d: RDHS1=0x%08lX RDHS2=0x%08lX RDHS3=0x%08lX MBS=0x%08lX MHDS=0x%08lX D0=0x%08lX D1=0x%08lX\r\n",
                   frd_node_name(node), bi, (unsigned long)v.rdhs1, (unsigned long)v.rdhs2,
                   (unsigned long)v.rdhs3, (unsigned long)v.mbs, (unsigned long)v.mhds,
                   (unsigned long)v.data0, (unsigned long)v.data1);
        shellPrint(sh, "  FID=%lu CYC=%lu CHA=%lu CHB=%lu CFG=%lu PPIT=%lu TXM=%lu MBI=%lu PLC=%lu CRC=0x%03lX DP=0x%04lX\r\n",
                   (unsigned long)(v.rdhs1 & 0x7FFU), (unsigned long)((v.rdhs1 >> 16) & 0x7FU),
                   (unsigned long)((v.rdhs1 >> 24) & 1U), (unsigned long)((v.rdhs1 >> 25) & 1U),
                   (unsigned long)((v.rdhs1 >> 26) & 1U), (unsigned long)((v.rdhs1 >> 27) & 1U),
                   (unsigned long)((v.rdhs1 >> 28) & 1U), (unsigned long)((v.rdhs1 >> 29) & 1U),
                   (unsigned long)((v.rdhs2 >> 16) & 0x7FU), (unsigned long)(v.rdhs2 & 0x7FFU),
                   (unsigned long)(v.rdhs3 & 0xFFFFU));
        shellPrint(sh, "  apply setslot rc: buf0=%d buf1=%d buf2=%d\r\n",
                   frd_setslot_rc(node, 0), frd_setslot_rc(node, 1), frd_setslot_rc(node, 2));
        return 0;
    }
    if (strcmp(argv[1], "txact") == 0) {
        /* Poll TEST1 across a full ~5ms cycle and accumulate activity:
         * AOA=8 (any activity), RXA=16, TXA=18, TXENA=20; CERA=27:24 sticky. */
        for (int node = 0; node < 2; node++) {
            uint32_t acc = 0;
            uint32_t cera0, cerb0, cera1, cerb1;
            if (frd_node_poc(node) < 0) {
                shellPrint(sh, "%s: not prepared\r\n", frd_node_name(node));
                continue;
            }
            cera0 = (frd_node_test1(node) >> 24) & 0xFU;
            cerb0 = (frd_node_test1(node) >> 28) & 0xFU;
            for (int k = 0; k < 20000; k++) {
                acc |= frd_node_test1(node);
            }
            cera1 = (frd_node_test1(node) >> 24) & 0xFU;
            cerb1 = (frd_node_test1(node) >> 28) & 0xFU;
            shellPrint(sh, "%s: AOA=%lu RXA=%lu TXA=%lu TXENA=%lu CERA %lu->%lu CERB %lu->%lu ACC=0x%08lX\r\n",
                       frd_node_name(node),
                       (unsigned long)((acc >> 8) & 1U), (unsigned long)((acc >> 16) & 1U),
                       (unsigned long)((acc >> 18) & 1U), (unsigned long)((acc >> 20) & 1U),
                       (unsigned long)cera0, (unsigned long)cera1,
                       (unsigned long)cerb0, (unsigned long)cerb1, (unsigned long)acc);
        }
        return 0;
    }

    shellPrint(sh, "Unknown flexray arg '%s'\r\n", argv[1]);
    return -1;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), flexray, cmd_flexray, FlexRay dual-ERAY selftest);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), fr, cmd_flexray, fr alias);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), eray, cmd_flexray, eray alias);
