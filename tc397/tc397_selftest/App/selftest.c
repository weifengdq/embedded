/**
 * \file selftest.c
 * \brief tc397_selftest - peripheral self-test framework and throughput bench.
 *
 * Design notes
 * ------------
 * Every check is an independent function `Selftest_Result t_xxx(Shell*, char*,
 * int)` that programs the peripheral through its normal driver API, evaluates
 * objective on-board criteria and returns PASS / FAIL / SKIP together with a
 * one-line evidence string. Nothing here needs a PC, so `selftest` is a genuine
 * factory test: it can be run at the end of the line and shows PASS or the list
 * of failed peripherals.
 *
 * The two Ethernet links are the exception: a link can only be proven against
 * a peer, so the automatic run reports their PHY/PLCA/MAC state and marks the
 * end-to-end throughput as SKIP, pointing at the README for the PC-side
 * iperf / ping numbers.
 *
 * Timing comes from the 100 MHz STM0 (1 tick = 10 ns) and the 1 ms tick
 * counter g_TickCount_1ms, both already used by the rest of the firmware.
 *
 * IMPORTANT: this code runs inside the letter-shell callback, i.e. inside the
 * main loop, so the CAN/FlexRay RX harvesting that normally happens in the
 * main loop has to be done explicitly here (can12_poll_channel, frd_poll).
 */
#include "shell.h"
#include "shell_port.h"
#include "selftest.h"
#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxStm.h"
#include "IfxPort.h"
#include "IfxScuCcu.h"
#include "IfxScu_reg.h"
#include "IfxGeth_reg.h"
#include "Dts/Dts/IfxDts_Dts.h"
#include "Dts/Std/IfxDts.h"
#include "Ifx_Lwip.h"
#include "UART_Logging.h"
#include "adc.h"
#include "can12.h"
#include "flexray_dual.h"
#include "tlf35584.h"
#include "lan8651.h"
#include "ff.h"
#include "diskio.h"
#include "mmc_sdmmc.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern volatile uint32 g_TickCount_1ms;
extern IfxAsclin_Asc g_asc;

/* GETH performance counters (defined in the lwIP port, see netif.c/Ifx_Lwip.c) */
extern volatile uint32 g_diag_rx_ok;
extern volatile uint32 g_diag_rx_err;
extern volatile uint32 g_diag_rx_nobuf;
extern volatile uint32 g_diag_tx_pkts;
extern volatile uint32 g_diag_rbu;
extern volatile uint32 g_prof_copy_ticks;
extern volatile uint32 g_prof_copy_cnt;
extern volatile uint32 g_prof_input_ticks;
extern volatile uint32 g_prof_input_cnt;
extern volatile uint32 g_prof_tx_ticks;
extern volatile uint32 g_prof_tx_cnt;
extern volatile uint32 g_prof_alloc_ticks;
extern volatile uint32 g_prof_busy_ticks;
extern volatile uint32 g_prof_idle_polls;
extern volatile uint32 g_prof_poll_gap_max;
extern volatile uint32 g_prof_poll_gap_1ms;
extern uint32 isrRxCount;
extern uint32 isrTxCount;

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

static uint32 st_stm_tick_us(void)
{
    uint32 f = (uint32)IfxStm_getFrequency(&MODULE_STM0);
    return (f == 0U) ? 100U : (f / 1000000U);
}

static uint32 st_now_us(void)
{
    return (uint32)IfxStm_getLower(&MODULE_STM0) / st_stm_tick_us();
}

static void st_wait_ms(Shell *sh, uint32 ms)
{
    uint32 start = g_TickCount_1ms;
    uint32 last  = start;
    while ((g_TickCount_1ms - start) < ms)
    {
        /* keep the protocol stacks alive while waiting */
        Ifx_Lwip_pollReceiveFlags();
        (void)can12_poll_all();
        (void)frd_poll();
        if (g_TickCount_1ms != last)
        {
            last = g_TickCount_1ms;
            UART_Poll();
        }
        (void)sh;
    }
}

/* ------------------------------------------------------------------ */
/* result registry                                                     */
/* ------------------------------------------------------------------ */

typedef Selftest_Result (*Selftest_Fn)(Shell *sh, char *detail, int n, boolean deep);

typedef struct
{
    const char *group;      /* short name shown in the table   */
    const char *key;        /* CLI selector                    */
    Selftest_Fn fn;
    boolean     needsPc;    /* cannot PASS without a host      */
} Selftest_Test;

static const Selftest_Test s_tests[] = {
    { "CORE/CLOCK",  "core", NULL, FALSE },
    { "UART0",       "uart", NULL, FALSE },
    { "ADC",         "adc",  NULL, FALSE },
    { "CAN x12",     "can",  NULL, FALSE },
    { "FLEXRAY",     "fr",   NULL, FALSE },
    { "TLF35584",    "tlf",  NULL, FALSE },
    { "SD CARD",     "sd",   NULL, FALSE },
    { "LAN8651 T1S", "t1s",  NULL, FALSE },
    { "GETH 1000T1", "geth", NULL, FALSE },
    { "NET (PC)",    "net",  NULL, TRUE  },
};

#define ST_TEST_COUNT ((int)(sizeof(s_tests) / sizeof(s_tests[0])))

static Selftest_Result s_res[ST_TEST_COUNT];
static char           s_det[ST_TEST_COUNT][112];

/* ------------------------------------------------------------------ */
/* 1. core / clock                                                     */
/* ------------------------------------------------------------------ */

static Selftest_Result t_core(Shell *sh, char *detail, int n, boolean deep)
{
    uint32 cpu = (uint32)IfxScuCcu_getCpuFrequency(IfxCpu_getCoreIndex());
    uint32 stm = (uint32)IfxStm_getFrequency(&MODULE_STM0);
    uint32 chip = SCU_CHIPID.U;

    (void)sh; (void)deep;
    snprintf(detail, n, "ChipID 0x%08lX CPU %lu MHz STM %lu MHz tick %lu ms",
             (unsigned long)chip,
             (unsigned long)(cpu / 1000000U), (unsigned long)(stm / 1000000U),
             (unsigned long)g_TickCount_1ms);

    if ((chip == 0U) || (cpu < 100000000U) || (stm != 100000000U))
    {
        return Selftest_Fail;
    }
    return Selftest_Pass;
}

/* ------------------------------------------------------------------ */
/* 2. UART console (baud-rate-verified transmit burst)                 */
/* ------------------------------------------------------------------ */

#define UART_BENCH_BYTES 64U

static Selftest_Result t_uart(Shell *sh, char *detail, int n, boolean deep)
{
    static char  s_pat[UART_BENCH_BYTES];
    uint32 baud;
    uint32 t0, t1, us;
    Ifx_SizeT cnt;
    uint32 i;
    boolean ok;

    (void)sh;
    /* The baud rate is verified from the hardware, not from the config struct:
     * IfxAsclin_getShiftFrequency() derives it from BITCON/BRG exactly like the
     * receiver does. */
    baud = (uint32)(IfxAsclin_getShiftFrequency(&MODULE_ASCLIN0) + 0.5f);

    /* Short transmit burst (one line, CRLF terminated) as a TX smoke test. */
    for (i = 0; i + 2U < UART_BENCH_BYTES; i++)
    {
        s_pat[i] = (char)('0' + (int)(i % 10U));
    }
    s_pat[UART_BENCH_BYTES - 2U] = '\r';
    s_pat[UART_BENCH_BYTES - 1U] = '\n';

    t0  = st_now_us();
    cnt = UART_BENCH_BYTES;
    (void)IfxAsclin_Asc_write(&g_asc, (uint8 *)s_pat, &cnt, TIME_INFINITE);
    t1  = st_now_us();
    us  = t1 - t0;

    if (deep)
    {
        shellPrint(sh, "\r\n  uart: BITCON=0x%08lX BRG=0x%08lX FRAMECON=0x%08lX -> %lu baud\r\n",
                   (unsigned long)MODULE_ASCLIN0.BITCON.U,
                   (unsigned long)MODULE_ASCLIN0.BRG.U,
                   (unsigned long)MODULE_ASCLIN0.FRAMECON.U,
                   (unsigned long)baud);
        shellPrint(sh, "  uart: %lu B burst in %lu us (line rate 921600 would need %lu us), overrun %lu\r\n",
                   (unsigned long)UART_BENCH_BYTES, (unsigned long)us,
                   (unsigned long)(((uint64)UART_BENCH_BYTES * 10ULL * 1000000ULL) / 921600ULL),
                   (unsigned long)gShellRxOverflow);
    }

    snprintf(detail, n, "%lu baud 8N1 (BITCON 0x%08lX FRAMECON 0x%08lX), %lu B tx %lu us, overrun %lu",
             (unsigned long)baud,
             (unsigned long)MODULE_ASCLIN0.BITCON.U,
             (unsigned long)MODULE_ASCLIN0.FRAMECON.U,
             (unsigned long)UART_BENCH_BYTES, (unsigned long)us,
             (unsigned long)gShellRxOverflow);

    ok = (cnt == UART_BENCH_BYTES) && (gShellRxOverflow == 0U) &&
         (baud > 890000U) && (baud < 950000U);
    return ok ? Selftest_Pass : Selftest_Fail;
}

/* ------------------------------------------------------------------ */
/* 3. ADC (EVADC, AN0..AN47 with rail tolerances)                      */
/* ------------------------------------------------------------------ */

typedef struct
{
    uint8  an;
    const char *name;
    float  nominal;
    float  tol;
} StRail;

static const StRail s_rails[] = {
    { 16, "VUC",  3.31f, 0.35f },
    { 20, "3V3",  3.25f, 0.35f },
    { 21, "1V25", 1.24f, 0.15f },
    { 22, "0V9",  0.92f, 0.12f },
};

#define ST_RAIL_COUNT ((int)(sizeof(s_rails) / sizeof(s_rails[0])))

static Selftest_Result t_adc(Shell *sh, char *detail, int n, boolean deep)
{
    int sampled = 0, failed = 0, railsBad = 0, i, r;
    char railText[128];
    int pos = 0;

    (void)deep;
    railText[0] = '\0';

    for (i = 0; i < 48; i++)
    {
        uint16 raw = 0;
        float  v   = 0.0f;
        if (g_AdcAnTable[i].group == 0xFFu)
        {
            continue;   /* P40.x GPIO reuse, not an ADC channel on this board */
        }
        if (Adc_ReadAn((uint8)i, &raw, &v) == 0)
        {
            failed++;
        }
        else
        {
            sampled++;
        }
    }

    for (r = 0; r < ST_RAIL_COUNT; r++)
    {
        uint16 raw = 0;
        float  v   = 0.0f;
        float  d;
        if (Adc_ReadAn(s_rails[r].an, &raw, &v) == 0)
        {
            railsBad++;
            if (pos < (int)sizeof(railText) - 16)
            {
                pos += snprintf(&railText[pos], sizeof(railText) - (size_t)pos, "%s=NO-DATA ",
                                s_rails[r].name);
            }
            continue;
        }
        d = (v > s_rails[r].nominal) ? (v - s_rails[r].nominal) : (s_rails[r].nominal - v);
        if (d > s_rails[r].tol)
        {
            railsBad++;
        }
        if (pos < (int)sizeof(railText) - 24)
        {
            pos += snprintf(&railText[pos], sizeof(railText) - (size_t)pos, "%s=%.2f(%.2f) ",
                            s_rails[r].name, (double)v, (double)s_rails[r].nominal);
        }
    }

    if (deep)
    {
        shellPrint(sh, "\r\n  adc : %d channels sampled, %d read errors, %d rails out of range\r\n",
                   sampled, failed, railsBad);
        shellPrint(sh, "  adc : %s\r\n", railText);
    }

    snprintf(detail, n, "%d/48 ch ok, %d err, %d rail bad [%s]",
             sampled, failed, railsBad, railText);

    if ((failed == 0) && (railsBad == 0) && (sampled > 20))
    {
        return Selftest_Pass;
    }
    return Selftest_Fail;
}

/* ------------------------------------------------------------------ */
/* 4. CAN x12 (bidirectional wired-pair loopback)                      */
/* ------------------------------------------------------------------ */

static int st_can_pair(Shell *sh, canChannel tx, canChannel rx, uint32 id,
                       const uint8 *data, uint8 len, boolean deep)
{
    can12_frame_t f;
    uint32 attempt;
    uint8 dlc = can_len2dlc(len);

    /* The first frame on a pair is occasionally lost while the transceiver /
     * controller leaves its initial state (same transient the standalone
     * tc397_can_x12 'canpair' test documented).  Two attempts, and the detail
     * line reports how many were needed. */
    for (attempt = 0; attempt < 2U; attempt++)
    {
        uint32 t0;

        while (can12_ring_pop(rx, &f)) { }   /* flush stale frames */

        if (can12_send(tx, id, 0, 0, 1, 1, dlc, data) != IfxCan_Status_ok)
        {
            continue;
        }

        t0 = g_TickCount_1ms;
        while ((g_TickCount_1ms - t0) < 200U)
        {
            can12_poll_channel(rx);
            can12_poll_channel(tx);
            if (can12_ring_pending(rx))
            {
                break;
            }
        }

        if (!can12_ring_pop(rx, &f))
        {
            continue;
        }
        if ((f.id != id) || (f.len != len) || (memcmp(f.data, data, len) != 0))
        {
            shellPrint(sh, "    can%d->can%d MISMATCH (id=0x%lX len=%u)\r\n",
                       (int)tx, (int)rx, (unsigned long)f.id, (unsigned)f.len);
            return -1;
        }
        if (deep)
        {
            shellPrint(sh, "    can%d->can%d PASS (id=0x%lX len=%u FD+BRS%s)\r\n",
                       (int)tx, (int)rx, (unsigned long)id, (unsigned)len,
                       (attempt > 0U) ? ", retried" : "");
        }
        return (attempt == 0U) ? 0 : 1;   /* 1 = needed a retry */
    }

    shellPrint(sh, "    can%d->can%d TIMEOUT\r\n", (int)tx, (int)rx);
    return -1;
}

static Selftest_Result t_can(Shell *sh, char *detail, int n, boolean deep)
{
    static const uint8 pairs[6][2] = {{0,1},{2,3},{4,5},{6,7},{8,9},{10,11}};
    uint8 data[8];
    int p, fails = 0, ok = 0, retries = 0, i;

    for (i = 0; i < 8; i++)
    {
        data[i] = (uint8)(0xA0 + i);
    }

    for (p = 0; p < 6; p++)
    {
        uint32 id = 0x100u + (uint32)(p * 2u);
        int r;

        r = st_can_pair(sh, (canChannel)pairs[p][0], (canChannel)pairs[p][1], id,
                        data, 8, deep);
        if (r >= 0)
        {
            ok++;
            retries += r;
        }
        else
        {
            fails++;
        }
        st_wait_ms(sh, 2);

        r = st_can_pair(sh, (canChannel)pairs[p][1], (canChannel)pairs[p][0], id + 0x40u,
                        data, 8, deep);
        if (r >= 0)
        {
            ok++;
            retries += r;
        }
        else
        {
            fails++;
        }
        st_wait_ms(sh, 2);
    }

    {
        uint32 ovf = 0;
        for (p = 0; p < CAN_NUM; p++)
        {
            ovf += can12_get_overflow((canChannel)p);
        }
        snprintf(detail, n, "%d/12 links ok (%d fail, %d retried), FD+BRS 1M/5M, rx overflow %lu",
                 ok, fails, retries, (unsigned long)ovf);
        if (deep)
        {
            shellPrint(sh, "\r\n  can : %s\r\n", detail);
        }
        return (fails == 0) ? Selftest_Pass : Selftest_Fail;
    }
}

/* ------------------------------------------------------------------ */
/* 5. FlexRay (FR0A + FR1A, channel A looped)                          */
/* ------------------------------------------------------------------ */

static Selftest_Result t_fr(Shell *sh, char *detail, int n, boolean deep)
{
    uint8_t tx[16];
    int r, rounds = 3, fails = 0, okFrames = 0;
    int poc0, poc1;

    frd_prepare();
    if (frd_startup(deep ? 1 : 0) != 0)
    {
        poc0 = frd_node_poc(FRD_NODE0);
        poc1 = frd_node_poc(FRD_NODE1);
        snprintf(detail, n, "bring-up FAILED (POC FR0=%d FR1=%d)", poc0, poc1);
        shellPrint(sh, "    fr  : startup failed, POC FR0=%d FR1=%d\r\n", poc0, poc1);
        return Selftest_Fail;
    }

    for (r = 0; r < rounds; r++)
    {
        int dir, w;
        for (dir = 0; dir < 2; dir++)   /* 0: FR0->FR1 (slot 11), 1: FR1->FR0 (slot 12) */
        {
            int txNode = dir ? 1 : 0;
            int rxNode = dir ? 0 : 1;
            int slot   = dir ? 12 : 11;
            boolean got = FALSE;
            int i;

            for (i = 0; i < 16; i++)
            {
                tx[i] = (uint8_t)((uint8_t)(r * 32 + dir * 16 + i) ^ (uint8_t)(dir ? 0xA5 : 0x5A));
            }
            if (frd_send(txNode, tx) != 0)
            {
                fails++;
                shellPrint(sh, "    fr  : FR%d send failed\r\n", txNode);
                continue;
            }
            for (w = 0; w < 10; w++)
            {
                FrdRxFrame f;
                st_wait_ms(sh, 20);
                (void)frd_poll();
                if ((frd_last_rx(rxNode, &f) == 0) && (f.slot == (uint16_t)slot) &&
                    (memcmp(f.payload, tx, 16) == 0))
                {
                    got = TRUE;
                    break;
                }
            }
            if (got)
            {
                okFrames++;
                if (deep)
                {
                    shellPrint(sh, "    fr  : FR%d->FR%d slot%d OK (round %d)\r\n",
                               txNode, rxNode, slot, r);
                }
            }
            else
            {
                fails++;
                shellPrint(sh, "    fr  : FR%d->FR%d slot%d TIMEOUT/MISMATCH\r\n",
                           txNode, rxNode, slot);
            }
        }
    }

    poc0 = frd_node_poc(FRD_NODE0);
    poc1 = frd_node_poc(FRD_NODE1);
    snprintf(detail, n, "FR0A=%s FR1A=%s, %d/%d frames bidirectional (10Mbit, 5ms cycle)",
             frd_poc_name(poc0), frd_poc_name(poc1), okFrames, rounds * 2);

    if ((fails == 0) && (okFrames == rounds * 2) && (poc0 == 2 && poc1 == 2))
    {
        return Selftest_Pass;
    }
    return Selftest_Fail;
}

/* ------------------------------------------------------------------ */
/* 6. TLF35584 (safety SBC on QSPI2, MPS high = TestMode)              */
/* ------------------------------------------------------------------ */

static Selftest_Result t_tlf(Shell *sh, char *detail, int n, boolean deep)
{
    uint8 devstat, iflag, stat;
    boolean linkOk;
    uint8 state;

    (void)deep;
    linkOk = Tlf_LinkOk();
    devstat = Tlf_Read(TLF_DEVSTAT);
    iflag   = Tlf_Read(TLF_IF);
    stat    = Tlf_Read(TLF_PROTSTAT);
    state   = (uint8)(devstat & 0x07u);

    snprintf(detail, n, "%s, DEVSTAT 0x%02X (%s) SS1=%u IF=0x%02X PROTSTAT=0x%02X",
             linkOk ? "SPI link ok" : "SPI link FAIL",
             (unsigned)devstat, Tlf_StateName(state),
             (unsigned)Tlf_Ss1Level(), (unsigned)iflag, (unsigned)stat);
    if (deep)
    {
        shellPrint(sh, "\r\n  tlf : %s\r\n", detail);
    }

    if ((linkOk != FALSE) && (state == TLF_STATE_NORMAL))
    {
        return Selftest_Pass;
    }
    return Selftest_Fail;
}

/* ------------------------------------------------------------------ */
/* 7. SD card (SDMMC0 + FatFS, non-destructive read-only check plus a  */
/*    small write/verify/unlink cycle in a temp file)                  */
/* ------------------------------------------------------------------ */

#define SD_TEST_KB   32U

/* 32 KB staging buffer for the SD write/verify test.  Kept in LMU because
 * DSPR0 (240 KB) is nearly full in the combined firmware; disk_read/write
 * handle the cache maintenance for any buffer address.  The pragma is closed
 * again right after the buffer so that no const object (the shell command
 * table) accidentally inherits the default section under TriCore-GCC. */
#if defined(__TASKING__)
#pragma section farbss "lmubss"
#elif defined(__GNUC__)
#pragma section ".lmubss" aw
#endif
static uint32 s_sdBuf[SD_TEST_KB * 256U];   /* 32 KB, word aligned for ADMA2 */
#if defined(__TASKING__)
#pragma section farbss "bss_cpu0"
#elif defined(__GNUC__)
#pragma section
#endif

static Selftest_Result t_sd(Shell *sh, char *detail, int n, boolean deep)
{
    static FATFS fs;
    DSTATUS ds;
    FRESULT fr;
    DWORD   sectors = 0;
    int     csdVer = 0;
    boolean mounted = FALSE, writeOk = FALSE;
    uint32  mb = 0, rKBs = 0, wKBs = 0;

    ds = disk_initialize(0);
    if (ds & STA_NOINIT)
    {
        snprintf(detail, n, "disk_initialize=0x%02X STA_NOINIT (card missing/not responding)",
                 (unsigned)ds);
        return Selftest_Fail;
    }

    fr = f_mount(&fs, "", 1);
    if (fr != FR_OK)
    {
        snprintf(detail, n, "disk_initialize=0x%02X ok but f_mount=%d (no FAT volume / card wedged)",
                 (unsigned)ds, (int)fr);
        return Selftest_Fail;
    }
    mounted = TRUE;

    if (Sdmmc_GetCapacitySectors(&sectors, &csdVer) != 0)
    {
        (void)Sdmmc_ProbeCapacitySectors(&sectors, NULL);
    }
    mb = (uint32)(((uint64)sectors * 512ULL) / (1024ULL * 1024ULL));

    /* ---- write / read-back verify, 32 KB in one temp file ---- */
    {
        FIL f;
        UINT bw = 0, br = 0;
        uint32 i, t0, us;

        fr = f_open(&f, "0:/STEST.TMP", FA_CREATE_ALWAYS | FA_WRITE);
        if (fr == FR_OK)
        {
            for (i = 0; i < (SD_TEST_KB * 256U); i++)
            {
                s_sdBuf[i] = i ^ 0x5A5A1234U;
            }
            t0 = st_now_us();
            fr = f_write(&f, s_sdBuf, sizeof(s_sdBuf), &bw);
            us = st_now_us() - t0;
            (void)f_close(&f);
            if ((fr == FR_OK) && (bw == sizeof(s_sdBuf)))
            {
                wKBs = (us == 0U) ? 0U : (uint32)(((uint64)SD_TEST_KB * 1000000ULL) / us);

                fr = f_open(&f, "0:/STEST.TMP", FA_READ);
                if (fr == FR_OK)
                {
                    memset(s_sdBuf, 0, sizeof(s_sdBuf));
                    t0 = st_now_us();
                    fr = f_read(&f, s_sdBuf, sizeof(s_sdBuf), &br);
                    us = st_now_us() - t0;
                    (void)f_close(&f);
                    if ((fr == FR_OK) && (br == sizeof(s_sdBuf)))
                    {
                        for (i = 0; i < (SD_TEST_KB * 256U); i++)
                        {
                            if (s_sdBuf[i] != (i ^ 0x5A5A1234U))
                            {
                                break;
                            }
                        }
                        if (i == (SD_TEST_KB * 256U))
                        {
                            writeOk = TRUE;
                            rKBs = (us == 0U) ? 0U : (uint32)(((uint64)SD_TEST_KB * 1000000ULL) / us);
                        }
                    }
                }
            }
        }
        (void)f_unlink("0:/STEST.TMP");
    }

    if (deep)
    {
        shellPrint(sh, "\r\n  sd  : init=0x%02X mount=OK capacity=%lu MiB (%s)\r\n",
                   (unsigned)ds, (unsigned long)mb,
                   (csdVer > 0) ? "CSD" : "LBA probe");
shellPrint(sh, "  sd  : %lu KB write %lu KB/s read %lu KB/s verify=%s\r\n",
               (unsigned long)SD_TEST_KB, (unsigned long)wKBs, (unsigned long)rKBs,
               writeOk ? "OK" : "FAIL");
    }

    snprintf(detail, n, "SDHC %lu MiB FAT32, %luKB w %lu/ r %lu KB/s, verify %s",
             (unsigned long)mb, (unsigned long)SD_TEST_KB,
             (unsigned long)wKBs, (unsigned long)rKBs,
             writeOk ? "OK" : "FAIL");

    if (mounted && writeOk)
    {
        return Selftest_Pass;
    }
    return Selftest_Fail;
}

/* ------------------------------------------------------------------ */
/* 8. LAN8651 10BASE-T1S (QSPI4, PLCA)                                 */
/* ------------------------------------------------------------------ */

static Selftest_Result t_t1s(Shell *sh, char *detail, int n, boolean deep)
{
    uint32 oas = 0, c0 = 0, c1 = 0, bmsr = 0, ncr = 0, devid = 0, sts1 = 0;
    boolean link;
    boolean plcaEn, syncOk, phyLink, macTx;

    if (lan8651_read_reg(&g_lan8651, LAN8651_MISC_DEVID, &devid) != kLan8651Status_Ok)
    {
        snprintf(detail, n, "register read failed (QSPI4/TC6 not responding)");
        return Selftest_Fail;
    }
    (void)lan8651_read_reg(&g_lan8651, LAN8651_OA_STATUS0, &oas);
    (void)lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL0, &c0);
    (void)lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL1, &c1);
    (void)lan8651_read_reg(&g_lan8651, LAN8651_PHY_BMSR, &bmsr);
    (void)lan8651_read_reg(&g_lan8651, LAN8651_MAC_NCR, &ncr);
    (void)lan8651_read_reg(&g_lan8651, LAN8651_STS1, &sts1);

    link     = (lan8651_link_up(&g_lan8651) != false);
    plcaEn   = ((c0 & LAN8651_PLCA_CTRL0_EN) != 0U);
    syncOk   = ((oas & LAN8651_OA_STATUS0_SYNC) != 0U);
    phyLink  = ((bmsr & LAN8651_PHY_BMSR_LINK_STATUS) != 0U);
    macTx    = ((ncr & LAN8651_MAC_NCR_TXEN) != 0U);

    (void)deep;
    snprintf(detail, n, "DEVID 0x%08lX PLCA %s id=%lu cnt=%lu sync=%u phy_link=%u mac_tx=%u sts1=0x%04lX",
             (unsigned long)devid, plcaEn ? "on" : "OFF",
             (unsigned long)(c1 & 0xFFU), (unsigned long)((c1 >> 8) & 0xFFU),
             (unsigned)syncOk, (unsigned)phyLink, (unsigned)macTx,
             (unsigned long)(sts1 & 0xFFFFU));
    if (deep)
    {
        shellPrint(sh, "\r\n  t1s : %s\r\n", detail);
    }

    if (plcaEn && macTx && (link || phyLink))
    {
        return Selftest_Pass;
    }
    return Selftest_Fail;
}

/* ------------------------------------------------------------------ */
/* 9. GETH 1000BASE-T1 (YT8011AN PHY, RGMII)                           */
/* ------------------------------------------------------------------ */

static Selftest_Result t_geth(Shell *sh, char *detail, int n, boolean deep)
{
    Ifx_GETH_MAC_PHYIF_CONTROL_STATUS cs;
    uint32 lnksts, lnkspeed, lnkmod;

    (void)deep;
    cs.U = GETH_MAC_PHYIF_CONTROL_STATUS.U;
    lnksts   = cs.B.LNKSTS;
    lnkspeed = cs.B.LNKSPEED;
    lnkmod   = cs.B.LNKMOD;

    snprintf(detail, n, "MAC.PHYIF 0x%08lX link=%lu speed=%s duplex=%s netif en0 %s",
             (unsigned long)cs.U,
             (unsigned long)lnksts,
             (lnkspeed == 0U) ? "10M" : (lnkspeed == 1U) ? "100M" : "1000M",
             (lnkmod == 1U) ? "full" : "half",
             (g_Lwip.netif.flags & NETIF_FLAG_LINK_UP) ? "link-up" : "link-down");
    if (deep)
    {
        shellPrint(sh, "\r\n  geth: %s\r\n", detail);
    }

    if ((lnksts == 1U) && (lnkspeed == 2U) && (lnkmod == 1U))
    {
        return Selftest_Pass;
    }
    return Selftest_Fail;
}

/* ------------------------------------------------------------------ */
/* 10. NET (needs a PC on the other end of the cable)                  */
/* ------------------------------------------------------------------ */

static Selftest_Result t_net(Shell *sh, char *detail, int n, boolean deep)
{
    (void)sh; (void)deep;
    snprintf(detail, n,
             "needs PC: ping 192.168.0.100 (1000T1) / 192.168.1.100 (T1S); iperf TCP 5001 (see README)");
    return Selftest_Skip;
}

/* ------------------------------------------------------------------ */
/* runner                                                              */
/* ------------------------------------------------------------------ */

static Selftest_Fn st_fn_for(const char *key)
{
    if (strcmp(key, "core") == 0) return t_core;
    if (strcmp(key, "uart") == 0) return t_uart;
    if (strcmp(key, "adc")  == 0) return t_adc;
    if (strcmp(key, "can")  == 0) return t_can;
    if (strcmp(key, "fr")   == 0) return t_fr;
    if (strcmp(key, "tlf")  == 0) return t_tlf;
    if (strcmp(key, "sd")   == 0) return t_sd;
    if (strcmp(key, "t1s")  == 0) return t_t1s;
    if (strcmp(key, "geth") == 0) return t_geth;
    if (strcmp(key, "net")  == 0) return t_net;
    return NULL;
}

static const char *st_tag(Selftest_Result r)
{
    return (r == Selftest_Pass) ? "PASS" : (r == Selftest_Fail) ? "FAIL" : "SKIP";
}

static int st_run_one(Shell *sh, int idx, Selftest_Fn fn, boolean deep)
{
    Selftest_Result r;

    /* Run first, print afterwards: a test may emit its own evidence (deep mode
     * detail lines, the UART transmit burst), and that must not split the
     * result table. */
    r = fn(sh, s_det[idx], (int)sizeof(s_det[idx]), deep);
    s_res[idx] = r;
    shellPrint(sh, "%2d %-12s %s  %s\r\n", idx + 1, s_tests[idx].group, st_tag(r), s_det[idx]);
    return (r == Selftest_Fail) ? 1 : 0;
}

void Selftest_Run(Shell *sh, int deep)
{
    int i, pass = 0, fail = 0, skip = 0, failMask = 0;

    shellPrint(sh, "\r\n");
    shellPrint(sh, "==================== TC397 FACTORY SELF TEST ====================\r\n");
    shellPrint(sh, " FW " __DATE__ " " __TIME__
               "  ChipID 0x%08lX  uptime %lu ms\r\n",
               (unsigned long)SCU_CHIPID.U, (unsigned long)g_TickCount_1ms);
    shellPrint(sh, " #  PERIPHERAL   RESULT  EVIDENCE\r\n");
    shellPrint(sh, "-----------------------------------------------------------------\r\n");

    for (i = 0; i < ST_TEST_COUNT; i++)
    {
        Selftest_Fn fn = st_fn_for(s_tests[i].key);
        if (st_run_one(sh, i, fn, deep))
        {
            failMask |= (1 << i);
        }
    }

    shellPrint(sh, "-----------------------------------------------------------------\r\n");

    for (i = 0; i < ST_TEST_COUNT; i++)
    {
        switch (s_res[i])
        {
            case Selftest_Pass: pass++; break;
            case Selftest_Fail: fail++; break;
            default:            skip++; break;
        }
    }

    shellPrint(sh, "SUMMARY: %d PASS  %d FAIL  %d SKIP   -> %s\r\n",
               pass, fail, skip, (fail == 0) ? "OVERALL PASS" : "OVERALL FAIL");

    if (fail == 0)
    {
        shellPrint(sh, "FAILED PERIPHERALS: none\r\n");
    }
    else
    {
        shellPrint(sh, "FAILED PERIPHERALS:");
        for (i = 0; i < ST_TEST_COUNT; i++)
        {
            if (s_res[i] == Selftest_Fail)
            {
                shellPrint(sh, " %s", s_tests[i].group);
            }
        }
        shellPrint(sh, "\r\n");
        for (i = 0; i < ST_TEST_COUNT; i++)
        {
            if (s_res[i] == Selftest_Fail)
            {
                shellPrint(sh, "  * %-12s %s\r\n", s_tests[i].group, s_det[i]);
            }
        }
    }
    ((void)failMask);

    shellPrint(sh, "Network throughput needs a PC: see 'bench' and README section 'iperf'.\r\n");
    shellPrint(sh, "=================================================================\r\n");
}

void Selftest_Status(Shell *sh)
{
    Ifx_GETH_MAC_PHYIF_CONTROL_STATUS cs;
    uint32 devstat, c0 = 0, c1 = 0;
    DSTATUS ds;
    DWORD sectors = 0;
    int csdVer = 0;
    int poc0, poc1;

    cs.U = GETH_MAC_PHYIF_CONTROL_STATUS.U;
    devstat = Tlf_Read(TLF_DEVSTAT);
    (void)lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL0, &c0);
    (void)lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL1, &c1);
    ds = disk_status(0);
    if ((ds & STA_NOINIT) == 0)
    {
        (void)Sdmmc_GetCapacitySectors(&sectors, &csdVer);
    }
    poc0 = frd_node_poc(FRD_NODE0);
    poc1 = frd_node_poc(FRD_NODE1);

    shellPrint(sh, "\r\n===== PERIPHERAL STATUS SUMMARY (live, no test) =====\r\n");
    shellPrint(sh, " MCU      : ChipID 0x%08lX  CPU %lu MHz  STM %lu MHz  temp %u C  uptime %lu ms\r\n",
               (unsigned long)SCU_CHIPID.U,
               (unsigned long)(IfxScuCcu_getCpuFrequency(IfxCpu_getCoreIndex()) / 1000000U),
               (unsigned long)(IfxStm_getFrequency(&MODULE_STM0) / 1000000U),
               (unsigned)(IfxDts_getTemperatureValue() / 100U),
               (unsigned long)g_TickCount_1ms);
    shellPrint(sh, " UART0    : ASCLIN0 921600 8N1, rx overrun %lu, shell %s\r\n",
               (unsigned long)gShellRxOverflow,
               Shell_HasPending() ? "rx pending" : "idle");
    shellPrint(sh, " ADC      : EVADC AN0..AN47, VREF 5.0V, 12 bit\r\n");
    shellPrint(sh, " CAN      : 12 nodes MCMCAN, 1M arb / 5M data FD+BRS, xcvr %s\r\n",
               xcvr_fault() ? "nFAULT ASSERTED" : "ok");
    shellPrint(sh, " FLEXRAY  : FR0A %s / FR1A %s\r\n",
               (poc0 < 0) ? "not prepared" : frd_poc_name(poc0),
               (poc1 < 0) ? "not prepared" : frd_poc_name(poc1));
    shellPrint(sh, " TLF35584 : DEVSTAT 0x%02X (%s) SS1=%u\r\n",
               (unsigned)devstat, Tlf_StateName((uint8)(devstat & 0x07u)),
               (unsigned)Tlf_Ss1Level());
    shellPrint(sh, " SD       : %s, capacity %lu MiB (%s)\r\n",
               (ds & STA_NOINIT) ? "not initialised ('sd init')" : "initialised",
               (unsigned long)(((uint64)sectors * 512ULL) / (1024ULL * 1024ULL)),
               (csdVer > 0) ? "CSD" : "LBA probe");
    shellPrint(sh, " LAN8651  : PLCA %s id=%lu cnt=%lu, link %s, irq %s\r\n",
               ((c0 & LAN8651_PLCA_CTRL0_EN) != 0U) ? "on" : "OFF",
               (unsigned long)(c1 & 0xFFU), (unsigned long)((c1 >> 8) & 0xFFU),
               IfxLan8651_linkUp() ? "UP" : "DOWN",
               lan8651_irq_asserted(&g_lan8651) ? "asserted" : "idle");
    shellPrint(sh, " GETH     : link %u speed %s duplex %s, netif en0 %s\r\n",
               (unsigned)cs.B.LNKSTS,
               (cs.B.LNKSPEED == 0U) ? "10M" : (cs.B.LNKSPEED == 1U) ? "100M" : "1000M",
               (cs.B.LNKMOD == 1U) ? "full" : "half",
               (g_Lwip.netif.flags & NETIF_FLAG_LINK_UP) ? "up" : "down");
    shellPrint(sh, " TRAFFIC  : geth rx_ok %lu rx_err %lu tx %lu | t1s netif %s\r\n",
               (unsigned long)g_diag_rx_ok, (unsigned long)g_diag_rx_err,
               (unsigned long)g_diag_tx_pkts,
               (netif_is_up(&g_Lan8651Netif)) ? "up" : "down");
    shellPrint(sh, "=====================================================\r\n");
}

/* ------------------------------------------------------------------ */
/* bench                                                               */
/* ------------------------------------------------------------------ */

static void bench_uart(Shell *sh)
{
    static char buf[4096];
    Ifx_SizeT cnt;
    uint32 t0, t1, us, i;
    uint32 baud;
    uint32 drainUs;

    for (i = 0; i < sizeof(buf); i++)
    {
        /* '\r' (no '\n') every 32 bytes keeps the whole burst on a single
         * terminal line instead of flooding the log with 4 KB of pattern. */
        buf[i] = ((i % 32U) == 31U) ? '\r' : (char)('0' + (int)(i % 10U));
    }
    baud = (uint32)(IfxAsclin_getShiftFrequency(&MODULE_ASCLIN0) + 0.5f);
    cnt = sizeof(buf);
    t0 = st_now_us();
    (void)IfxAsclin_Asc_write(&g_asc, (uint8 *)buf, &cnt, TIME_INFINITE);
    t1 = st_now_us();
    us = t1 - t0;

    /* The 1 KB software TX ring absorbs the first 1024 bytes without blocking,
     * so only the remaining bytes are paced by the line rate.  Comparing the
     * measured time against (N - 1024) bytes is the meaningful check; a full
     * (N) comparison is printed for reference. */
    drainUs = (uint32)((((uint64)sizeof(buf) - 1024ULL) * 10ULL * 1000000ULL) / 921600ULL);
    shellPrint(sh, "\r\n UART0  : %lu B in %lu us (%lu kbps effective); %lu B are paced by the\r\n",
               (unsigned long)sizeof(buf), (unsigned long)us,
               (unsigned long)(us ? (((uint64)sizeof(buf) * 10ULL * 1000ULL) / us) : 0ULL),
               (unsigned long)(sizeof(buf) - 1024U));
    shellPrint(sh, "         921600 baud line rate -> %lu us expected (full %lu B would be %lu us)\r\n",
               (unsigned long)drainUs,
               (unsigned long)sizeof(buf),
               (unsigned long)(((uint64)sizeof(buf) * 10ULL * 1000000ULL) / 921600ULL));
}

static void bench_can(Shell *sh)
{
    uint8 data[64];
    can12_frame_t f;
    uint32 i, frames = 0, busy = 0, t0, ms;
    uint32 ovfBefore = 0, ovfAfter = 0;
    int c;

    for (i = 0; i < sizeof(data); i++)
    {
        data[i] = (uint8)i;
    }
    for (c = 0; c < CAN_NUM; c++)
    {
        ovfBefore += can12_get_overflow((canChannel)c);
    }

    /* CAN0 -> CAN1 wired pair, 64-byte FD frames with BRS.  The TX FIFO holds
     * only 16 entries and the RX FIFO 32, so the loop keeps the receiver
     * drained and retries while the TX queue reports busy; it runs for a fixed
     * 500 ms window so the result is a sustained rate, not a burst. */
    t0 = g_TickCount_1ms;
    while ((g_TickCount_1ms - t0) < 500U)
    {
        if (can12_send(CAN0, 0x200U + (frames & 0xFu), 0, 0, 1, 1, can_len2dlc(64), data) == IfxCan_Status_ok)
        {
            frames++;
        }
        else
        {
            busy++;
        }
        can12_poll_channel(CAN1);
        while (can12_ring_pop(CAN1, &f)) { }
    }
    ms = g_TickCount_1ms - t0;
    for (c = 0; c < CAN_NUM; c++)
    {
        ovfAfter += can12_get_overflow((canChannel)c);
    }

    shellPrint(sh, " CAN0->1 : %lu frames x64B FD+BRS in %lu ms = %lu frame/s (~%lu kbit/s payload),\r\n",
               (unsigned long)frames, (unsigned long)ms,
               (unsigned long)(ms ? (frames * 1000UL / ms) : 0UL),
               (unsigned long)(ms ? (frames * 64UL * 8UL / ms) : 0UL));
    shellPrint(sh, "           TX-queue busy retries %lu, RX overflow %lu (1 Mbit arb / 5 Mbit data)\r\n",
               (unsigned long)busy, (unsigned long)(ovfAfter - ovfBefore));
}

static void bench_fr(Shell *sh)
{
    uint8_t tx[16];
    uint32 i, sent = 0, t0, ms;
    const FrdCounters *c;

    frd_prepare();     /* safe/no-op when already prepared; required before
                        * frd_is_ready()/frd_send() on a cold board */
    if (!frd_is_ready())
    {
        shellPrint(sh, " FLEXRAY: not running - run 'fr init' or 'selftest fr' first\r\n");
        return;
    }
    for (i = 0; i < 16; i++)
    {
        tx[i] = (uint8_t)i;
    }
    t0 = g_TickCount_1ms;
    for (i = 0; i < 200U; i++)
    {
        if (frd_send(FRD_NODE0, tx) == 0)
        {
            sent++;
        }
        st_wait_ms(sh, 5);
        (void)frd_poll();
    }
    ms = g_TickCount_1ms - t0;
    c = frd_counters(FRD_NODE1);
    shellPrint(sh, " FLEXRAY: %lu frames x16B in %lu ms = %lu frame/s sustained\r\n",
               (unsigned long)sent, (unsigned long)ms,
               (unsigned long)(ms ? (sent * 1000UL / ms) : 0UL));
    shellPrint(sh, "          (static slot 11 occurs once per 5 ms cycle -> 200 frame/s per slot;\r\n");
    shellPrint(sh, "           the 91 static slots of the 10 Mbit/s cluster carry up to ~18 kframe/s)\r\n");
    if (c != NULL)
    {
        shellPrint(sh, "          FR1 rxOk=%lu rxErr=%lu rxNull=%lu\r\n",
                   (unsigned long)c->rxOk, (unsigned long)c->rxErr, (unsigned long)c->rxNull);
    }
}

static void bench_sd(Shell *sh, uint32 sizeMB)
{
    static FATFS fs;
    FIL f;
    FRESULT fr;
    UINT bw = 0, br = 0;
    uint32 total = 0, remain, t0, wMs, rMs, i;
    boolean verifyOk = TRUE;

    if (sizeMB == 0U)
    {
        sizeMB = 2U;
    }
    if (sizeMB > 16U)
    {
        sizeMB = 16U;
    }
    if (f_mount(&fs, "", 1) != FR_OK)
    {
        shellPrint(sh, " SD     : not mounted (insert card / run 'sd init')\r\n");
        return;
    }

    fr = f_open(&f, "0:/SBENCH.TMP", FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        shellPrint(sh, " SD     : f_open(w) -> %d\r\n", (int)fr);
        return;
    }
    for (i = 0; i < (sizeof(s_sdBuf) / sizeof(s_sdBuf[0])); i++)
    {
        s_sdBuf[i] = i ^ 0x1234ABCDU;
    }
    remain = sizeMB * 1024U * 1024U;
    t0 = g_TickCount_1ms;
    while (remain > 0U)
    {
        UINT chunk = (remain > sizeof(s_sdBuf)) ? (UINT)sizeof(s_sdBuf) : (UINT)remain;
        if ((f_write(&f, s_sdBuf, chunk, &bw) != FR_OK) || (bw != chunk))
        {
            break;
        }
        total += bw;
        remain -= bw;
    }
    (void)f_close(&f);
    wMs = g_TickCount_1ms - t0;

    if (remain != 0U)
    {
        shellPrint(sh, " SD     : write FAILED after %lu KB\r\n", (unsigned long)(total / 1024U));
        (void)f_unlink("0:/SBENCH.TMP");
        return;
    }

    fr = f_open(&f, "0:/SBENCH.TMP", FA_READ);
    if (fr != FR_OK)
    {
        shellPrint(sh, " SD     : f_open(r) -> %d\r\n", (int)fr);
        (void)f_unlink("0:/SBENCH.TMP");
        return;
    }
    {
        uint32 done = 0;
        t0 = g_TickCount_1ms;
        while (done < total)
        {
            UINT chunk = ((total - done) > sizeof(s_sdBuf)) ? (UINT)sizeof(s_sdBuf) : (UINT)(total - done);
            uint32 k;
            if ((f_read(&f, s_sdBuf, chunk, &br) != FR_OK) || (br != chunk))
            {
                break;
            }
            for (k = 0; k < (br / 4U); k++)
            {
                if (s_sdBuf[k] != (k ^ 0x1234ABCDU))
                {
                    verifyOk = FALSE;
                    break;
                }
            }
            done += br;
            if (!verifyOk)
            {
                break;
            }
        }
        rMs = g_TickCount_1ms - t0;
    }
    (void)f_close(&f);
    (void)f_unlink("0:/SBENCH.TMP");

    shellPrint(sh, " SD     : %lu MB write %lu ms = %lu KB/s | read %lu ms = %lu KB/s | verify %s\r\n",
               (unsigned long)(total / (1024U * 1024U)),
               (unsigned long)wMs, (unsigned long)(wMs ? ((total / 1024U) * 1000U / wMs) : 0U),
               (unsigned long)rMs, (unsigned long)(rMs ? ((total / 1024U) * 1000U / rMs) : 0U),
               verifyOk ? "OK" : "FAIL");
}

void Selftest_Bench(Shell *sh, int argc, char *argv[])
{
    uint32 sizeMB = 2U;

    if ((argc >= 2) && (strcmp(argv[1], "uart") == 0))
    {
        bench_uart(sh);
        return;
    }
    if ((argc >= 2) && (strcmp(argv[1], "can") == 0))
    {
        bench_can(sh);
        return;
    }
    if ((argc >= 2) && (strcmp(argv[1], "fr") == 0))
    {
        bench_fr(sh);
        return;
    }
    if ((argc >= 2) && (strcmp(argv[1], "sd") == 0))
    {
        if (argc >= 3)
        {
            sizeMB = (uint32)atoi(argv[2]);
        }
        bench_sd(sh, sizeMB);
        return;
    }

    shellPrint(sh, "\r\n=================== ON-BOARD PERFORMANCE ===================\r\n");
    bench_uart(sh);
    bench_can(sh);
    bench_fr(sh);
    bench_sd(sh, sizeMB);
    shellPrint(sh, " ETH    : end-to-end needs the PC side, use the iperf commands in README:\r\n");
    shellPrint(sh, "          10BASE-T1S : tools\\iperf.exe -c 192.168.1.100 -p 5001 -t 15 -w 32K -M 1024\r\n");
    shellPrint(sh, "          1000BASE-T1: tools\\iperf.exe -c 192.168.0.100 -p 5001 -t 15 -w 16K\r\n");
    shellPrint(sh, "          reference measured values: 8.66 Mbps (T1S) / 359 Mbps (1000T1)\r\n");
    shellPrint(sh, "===========================================================\r\n");
}

/* ------------------------------------------------------------------ */
/* UDP statistics service (port 5002) + sink (5003)                    */
/* ------------------------------------------------------------------ */

static void diag_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                          const ip_addr_t *addr, u16_t port)
{
    char msg[512];
    int  n;

    LWIP_UNUSED_ARG(arg);

    n = snprintf(msg, sizeof(msg),
        "uptime=%lu isrRx=%lu isrTx=%lu rx_ok=%lu rx_err=%lu rx_nobuf=%lu tx=%lu rbu=%lu "
        "copy_t=%lu copy_n=%lu input_t=%lu input_n=%lu tx_t=%lu tx_n=%lu "
        "gap_max=%lu gap1ms=%lu busy_t=%lu idle_p=%lu alloc_t=%lu "
        "hw_rxgb=%lu hw_crc=%lu hw_fifo_ovf=%lu dma_miss=%lu sysbus=0x%08lX rxctl=0x%08lX "
        "fgeth=%lu t1s_rx_ovf=%lu\n",
        (unsigned long)g_TickCount_1ms,
        (unsigned long)isrRxCount, (unsigned long)isrTxCount,
        (unsigned long)g_diag_rx_ok, (unsigned long)g_diag_rx_err,
        (unsigned long)g_diag_rx_nobuf, (unsigned long)g_diag_tx_pkts,
        (unsigned long)g_diag_rbu,
        (unsigned long)g_prof_copy_ticks, (unsigned long)g_prof_copy_cnt,
        (unsigned long)g_prof_input_ticks, (unsigned long)g_prof_input_cnt,
        (unsigned long)g_prof_tx_ticks, (unsigned long)g_prof_tx_cnt,
        (unsigned long)g_prof_poll_gap_max, (unsigned long)g_prof_poll_gap_1ms,
        (unsigned long)g_prof_busy_ticks, (unsigned long)g_prof_idle_polls,
        (unsigned long)g_prof_alloc_ticks,
        (unsigned long)GETH_RX_PACKETS_COUNT_GOOD_BAD.U,
        (unsigned long)GETH_RX_CRC_ERROR_PACKETS.U,
        (unsigned long)GETH_RX_FIFO_OVERFLOW_PACKETS.U,
        (unsigned long)GETH_DMA_CH0_MISS_FRAME_CNT.U,
        (unsigned long)GETH_DMA_SYSBUS_MODE.U,
        (unsigned long)GETH_DMA_CH0_RX_CONTROL.U,
        (unsigned long)(IfxScuCcu_getSriFrequency() /
                        (MODULE_SCU.CCUCON5.B.GETHDIV ? MODULE_SCU.CCUCON5.B.GETHDIV : 1u)),
        (unsigned long)gShellRxOverflow);

    if (p != NULL)
    {
        /* first byte of the payload commands a reset of the counters */
        if ((p->len > 0U) && (((const char *)p->payload)[0] == 'z'))
        {
            g_diag_rx_ok = 0; g_diag_rx_err = 0; g_diag_rx_nobuf = 0; g_diag_tx_pkts = 0;
            g_prof_copy_ticks = 0; g_prof_copy_cnt = 0;
            g_prof_input_ticks = 0; g_prof_input_cnt = 0;
            g_prof_tx_ticks = 0; g_prof_tx_cnt = 0;
            g_prof_busy_ticks = 0; g_prof_idle_polls = 0; g_prof_alloc_ticks = 0;
        }
        pbuf_free(p);
    }

    if (n > 0)
    {
        struct pbuf *r = pbuf_alloc(PBUF_TRANSPORT, (u16_t)n, PBUF_RAM);
        if (r != NULL)
        {
            memcpy(r->payload, msg, (size_t)n);
            (void)udp_sendto(pcb, r, addr, port);
            pbuf_free(r);
        }
    }
}

static void diag_sink_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                           const ip_addr_t *addr, u16_t port)
{
    LWIP_UNUSED_ARG(arg); LWIP_UNUSED_ARG(pcb);
    LWIP_UNUSED_ARG(addr); LWIP_UNUSED_ARG(port);
    if (p != NULL)
    {
        pbuf_free(p);
    }
}

void Selftest_DiagUdpInit(void)
{
    struct udp_pcb *pcb = udp_new();

    if (pcb != NULL)
    {
        (void)udp_bind(pcb, IP_ANY_TYPE, 5002);
        udp_recv(pcb, diag_udp_recv, NULL);
    }

    pcb = udp_new();
    if (pcb != NULL)
    {
        (void)udp_bind(pcb, IP_ANY_TYPE, 5003);
        udp_recv(pcb, diag_sink_recv, NULL);
    }
}
