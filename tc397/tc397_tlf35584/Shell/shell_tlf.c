/**********************************************************************************************************************
 * \file shell_tlf.c
 * \brief letter-shell 'tlf' command: full TLF35584 coverage over QSPI2.
 *********************************************************************************************************************/
#include "shell.h"
#include "tlf35584.h"
#include "IfxPort.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern volatile uint32 g_TickCount_1ms;

typedef struct { uint8 addr; const char *name; } Tlf_RegName;
static const Tlf_RegName s_regNames[] = {
    {0x00,"DEVCFG0"},{0x01,"DEVCFG1"},{0x02,"DEVCFG2"},{0x03,"PROTCFG"},
    {0x04,"SYSPCFG0"},{0x05,"SYSPCFG1"},{0x06,"WDCFG0"},{0x07,"WDCFG1"},
    {0x08,"FWDCFG"},{0x09,"WWDCFG0"},{0x0A,"WWDCFG1"},
    {0x0B,"RSYSPCFG0"},{0x0C,"RSYSPCFG1"},{0x0D,"RWDCFG0"},{0x0E,"RWDCFG1"},
    {0x0F,"RFWDCFG"},{0x10,"RWWDCFG0"},{0x11,"RWWDCFG1"},
    {0x12,"WKTIMCFG0"},{0x13,"WKTIMCFG1"},{0x14,"WKTIMCFG2"},
    {0x15,"DEVCTRL"},{0x16,"DEVCTRLN"},{0x17,"WWDSCMD"},{0x18,"FWDRSP"},{0x19,"FWDRSPSYNC"},
    {0x1A,"SYSFAIL"},{0x1B,"INITERR"},{0x1C,"IF"},{0x1D,"SYSSF"},{0x1E,"WKSF"},{0x1F,"SPISF"},
    {0x20,"MONSF0"},{0x21,"MONSF1"},{0x22,"MONSF2"},{0x23,"MONSF3"},
    {0x24,"OTFAIL"},{0x25,"OTWRNSF"},{0x26,"VMONSTAT"},{0x27,"DEVSTAT"},
    {0x28,"PROTSTAT"},{0x29,"WWDSTAT"},{0x2A,"FWDSTAT0"},{0x2B,"FWDSTAT1"},
    {0x2C,"ABIST_CTRL0"},{0x2D,"ABIST_CTRL1"},{0x2E,"ABIST_SEL0"},{0x2F,"ABIST_SEL1"},
    {0x30,"ABIST_SEL2"},{0x31,"BCK_FREQ_CHANGE"},{0x32,"BCK_FRE_SPREAD"},{0x33,"BCK_MAIN_CTRL"},
    {0x3F,"GTM"},
};

static const char *Tlf_RegNameOf(uint8 addr)
{
    uint32 i;
    for (i = 0; i < (uint32)(sizeof(s_regNames) / sizeof(s_regNames[0])); i++)
    {
        if (s_regNames[i].addr == addr) return s_regNames[i].name;
    }
    return "RESERVED";
}

static uint32 Tlf_ParseU32(const char *s, uint32 dflt)
{
    char *end = NULL_PTR;
    unsigned long v;
    if (s == NULL_PTR) return dflt;
    v = strtoul(s, &end, 0);
    if (end == s) return dflt;
    return (uint32)v;
}

static void Tlf_BusyWaitMs(uint32 ms)
{
    uint32 start = g_TickCount_1ms;
    while ((g_TickCount_1ms - start) < ms) { }
}

static void Tlf_PrintFlags(Shell *sh)
{
    shellPrint(sh, "SYSFAIL=0x%02X INITERR=0x%02X IF=0x%02X SYSSF=0x%02X WKSF=0x%02X SPISF=0x%02X\r\n",
        Tlf_Read(TLF_SYSFAIL), Tlf_Read(TLF_INITERR), Tlf_Read(TLF_IF),
        Tlf_Read(TLF_SYSSF), Tlf_Read(TLF_WKSF), Tlf_Read(TLF_SPISF));
    shellPrint(sh, "MONSF0=0x%02X MONSF1=0x%02X MONSF2=0x%02X MONSF3=0x%02X\r\n",
        Tlf_Read(TLF_MONSF0), Tlf_Read(TLF_MONSF1), Tlf_Read(TLF_MONSF2), Tlf_Read(TLF_MONSF3));
    shellPrint(sh, "OTFAIL=0x%02X OTWRNSF=0x%02X VMONSTAT=0x%02X DEVSTAT=0x%02X PROTSTAT=0x%02X\r\n",
        Tlf_Read(TLF_OTFAIL), Tlf_Read(TLF_OTWRNSF), Tlf_Read(TLF_VMONSTAT),
        Tlf_Read(TLF_DEVSTAT), Tlf_Read(TLF_PROTSTAT));
}

static void Tlf_PrintState(Shell *sh)
{
    uint8 dev = Tlf_Read(TLF_DEVSTAT);
    uint8 st  = (uint8)(dev & 0x07u);
    shellPrint(sh, "DEVSTAT=0x%02X STATE=%s TRK2=%u TRK1=%u COM=%u STBY=%u VREF=%u\r\n",
        dev, Tlf_StateName(st),
        (dev >> 7) & 1u, (dev >> 6) & 1u, (dev >> 5) & 1u, (dev >> 4) & 1u, (dev >> 3) & 1u);
    shellPrint(sh, "VMONSTAT=0x%02X (TRK2/1/VREF/COM/VCORE/STBY ready)\r\n", Tlf_Read(TLF_VMONSTAT));
    shellPrint(sh, "SS1(P33.9)=%u(%s) WDI(P14.3)=%u ERR(P33.8)=%u lock=%s baud=%lu xfer=%lu tout=%lu\r\n",
        Tlf_Ss1Level(), Tlf_Ss1Level() ? "high/normal" : "LOW/safe-state",
        Tlf_WdiLevel(), Tlf_ErrLevel(), Tlf_IsLocked() ? "locked" : "UNLOCKED",
        (unsigned long)g_tlf.baud, (unsigned long)g_tlf.xferCnt, (unsigned long)g_tlf.timeoutCnt);
}

static void Tlf_Usage(Shell *sh)
{
    shellPrint(sh, "Usage: tlf <sub> [args]  (QSPI2 P15.6/15.7/15.8 + nCS P14.2)\r\n");
    shellPrint(sh, " link      : rd DEVSTAT/PROTSTAT/GTM, check MISO MSB=1\r\n");
    shellPrint(sh, " state     : DEVSTAT/VMONSTAT/SS1/WDI/ERR/lock summary\r\n");
    shellPrint(sh, " dump [f [t]]        : dump regs (default 0x00-0x33 + 0x3F)\r\n");
    shellPrint(sh, " rd <a> [n]          : read n regs from addr (hex ok)\r\n");
    shellPrint(sh, " wr <a> <v>          : raw write reg (unprotected only)\r\n");
    shellPrint(sh, " unlock|lock|prot    : PROTCFG sequence / PROTSTAT+keys\r\n");
    shellPrint(sh, " goto <normal|sleep|standby|wake|init> [trk2 trk1 com vref]\r\n");
    shellPrint(sh, " flags               : SYSFAIL/INITERR/IF/SYSSF/WKSF/SPISF/MONSFx/OTx/VMON/DEV/PROT\r\n");
    shellPrint(sh, " clear <all|sysfail|initerr|if|syssf|wksf|spisf|monsf|ot>\r\n");
    shellPrint(sh, " devcfg [trdel]                 : show/set DEVCFG0.TRDEL(100us steps)\r\n");
    shellPrint(sh, " syscfg [ss2 errslp erren recrec rec] : show/set SYSPCFG1 (unlock+lock)\r\n");
    shellPrint(sh, " wdcfg [wwdethr wden fwden ts el cyc] : show/set WDCFG0 (unlock+lock)\r\n");
    shellPrint(sh, " wdcfg1 [slpen fwdethr]              : show/set WDCFG1\r\n");
    shellPrint(sh, " fwdcfg [hbt]        : show/set FWDCFG.WDHBTP (x50 wd cycles)\r\n");
    shellPrint(sh, " wwcfg [cw ow]       : show/set WWDCFG0.CW + WWDCFG1.OW (x50 wd cycles)\r\n");
    shellPrint(sh, " wwd trig [n] | status | auto <on [ms]|off>\r\n");
    shellPrint(sh, " fwd status|answer|resp <b>|sync <b>|bgauto <on [ms]|off>\r\n");
    shellPrint(sh, " wdi <high|low|toggle|pulse <ms>|auto <on [ms]|off>>\r\n");
    shellPrint(sh, " err <high|low|toggle|burst <n> <ms>|auto <on [ms]|off>>\r\n");
    shellPrint(sh, " ss                  : read SS1 (P33.9)\r\n");
    shellPrint(sh, " rails <com> <vref> [trk1] [trk2] : DEVCTRL+DEVCTRLN rail request\r\n");
    shellPrint(sh, " wktim [ms10|cyc]    : show/set 24-bit wake timer (WKTIMCFG2:0)\r\n");
    shellPrint(sh, " abist <start|sel <m> <v>|ctrl0 <v>|ctrl1 <v>|show>\r\n");
    shellPrint(sh, " buck [show]         : BCK_FREQ_CHANGE/FRE_SPREAD/MAIN_CTRL\r\n");
    shellPrint(sh, " baud <hz>           : re-init QSPI2 channel (100k-10M)\r\n");
    shellPrint(sh, " demo                : Infineon init (unlock, dis WWD+ERR, lock, rails, goto normal)\r\n");
}

static int cmd_tlf(int argc, char *argv[])
{
    Shell *sh = shellGetCurrent();

    if (argc < 2)
    {
        Tlf_PrintState(sh);
        shellPrint(sh, "Try 'tlf help' for all subcommands.\r\n");
        return 0;
    }

    /* ---------- help ---------- */
    if (strcmp(argv[1], "help") == 0) { Tlf_Usage(sh); return 0; }

    /* ---------- link ---------- */
    if (strcmp(argv[1], "link") == 0 || strcmp(argv[1], "test") == 0)
    {
        Tlf_Frame f1, f2, f3;
        uint16 r1 = Tlf_Transfer(0, TLF_DEVSTAT, 0, &f1);
        uint16 r2 = Tlf_Transfer(0, TLF_PROTSTAT, 0, &f2);
        uint16 r3 = Tlf_Transfer(0, TLF_GTM, 0, &f3);
        shellPrint(sh, "DEVSTAT : rx=0x%04X msb=%u status=0x%02X data=0x%02X (%s)\r\n",
            r1, (r1 >> 14) & 1u, f1.status, f1.data, Tlf_StateName(f1.data & 0x07u));
        shellPrint(sh, "PROTSTAT: rx=0x%04X msb=%u status=0x%02X data=0x%02X (%s)\r\n",
            r2, (r2 >> 14) & 1u, f2.status, f2.data, (f2.data & 1u) ? "locked" : "UNLOCKED");
        shellPrint(sh, "GTM     : rx=0x%04X msb=%u status=0x%02X data=0x%02X (%s)\r\n",
            r3, (r3 >> 14) & 1u, f3.status, f3.data,
            (f3.data & 1u) ? "TEST mode (MPS=1)" : "normal mode (MPS=0)");
        if ((((r1 >> 14) & 1u) == 1u) && (((r2 >> 14) & 1u) == 1u) && f1.ok && f2.ok)
            shellPrint(sh, "link OK (MISO MSB=1, HW parity pass), baud=%lu\r\n", (unsigned long)g_tlf.baud);
        else
            shellPrint(sh, "link FAIL: check P15.6/15.7/15.8/P14.2 wiring + TLF supply\r\n");
        return 0;
    }

    /* ---------- state ---------- */
    if (strcmp(argv[1], "state") == 0 || strcmp(argv[1], "status") == 0)
    {
        Tlf_PrintState(sh);
        Tlf_PrintFlags(sh);
        return 0;
    }

    /* ---------- dump ---------- */
    if (strcmp(argv[1], "dump") == 0)
    {
        uint32 from = (argc >= 3) ? Tlf_ParseU32(argv[2], 0x00u) : 0x00u;
        uint32 to   = (argc >= 4) ? Tlf_ParseU32(argv[3], 0x33u) : 0x33u;
        uint32 a;
        if (from > 0x3Fu) from = 0x3Fu;
        if (to > 0x3Fu) to = 0x3Fu;
        for (a = from; a <= to; a++)
        {
            /* skip holes 0x34-0x3E for default full dump brevity? No: print all requested. */
            shellPrint(sh, "0x%02X %-14s = 0x%02X\r\n", a, Tlf_RegNameOf((uint8)a), Tlf_Read((uint8)a));
            if (a == to) break;
        }
        shellPrint(sh, "0x3F %-14s = 0x%02X\r\n", "GTM", Tlf_Read(TLF_GTM));
        return 0;
    }

    /* ---------- rd ---------- */
    if (strcmp(argv[1], "rd") == 0 || strcmp(argv[1], "read") == 0)
    {
        uint32 addr, n, i;
        if (argc < 3) { shellPrint(sh, "Usage: tlf rd <addr> [count]\r\n"); return -1; }
        addr = Tlf_ParseU32(argv[2], 0) & 0x3Fu;
        n    = (argc >= 4) ? Tlf_ParseU32(argv[3], 1) : 1;
        if (n == 0) n = 1;
        if (n > 64) n = 64;
        for (i = 0; i < n; i++)
        {
            Tlf_Frame f;
            uint16 rx = Tlf_Transfer(0, (uint8)((addr + i) & 0x3Fu), 0, &f);
            shellPrint(sh, "rd 0x%02X (%-12s): rx=0x%04X data=0x%02X %s\r\n",
                (addr + i) & 0x3Fu, Tlf_RegNameOf((uint8)((addr + i) & 0x3Fu)),
                rx, f.data, f.ok ? "" : "TIMEOUT");
        }
        return 0;
    }

    /* ---------- wr ---------- */
    if (strcmp(argv[1], "wr") == 0 || strcmp(argv[1], "write") == 0)
    {
        uint32 addr, val;
        Tlf_Frame f;
        uint16 rx;
        if (argc < 4) { shellPrint(sh, "Usage: tlf wr <addr> <val> (unprotected regs; protected need unlock+syscfg/wdcfg cmds)\r\n"); return -1; }
        addr = Tlf_ParseU32(argv[2], 0) & 0x3Fu;
        val  = Tlf_ParseU32(argv[3], 0) & 0xFFu;
        rx   = Tlf_Transfer(1, (uint8)addr, (uint8)val, &f);
        shellPrint(sh, "wr 0x%02X (%s) <= 0x%02X, loopback rx=0x%04X, readback=0x%02X\r\n",
            addr, Tlf_RegNameOf((uint8)addr), val, rx, Tlf_Read((uint8)addr));
        return 0;
    }

    /* ---------- unlock/lock/prot ---------- */
    if (strcmp(argv[1], "unlock") == 0)
    {
        Tlf_Unlock();
        shellPrint(sh, "unlock seq AB EF 56 12 sent; PROTSTAT=0x%02X (%s)\r\n",
            Tlf_Read(TLF_PROTSTAT), Tlf_IsLocked() ? "locked" : "UNLOCKED");
        return 0;
    }
    if (strcmp(argv[1], "lock") == 0)
    {
        Tlf_Lock();
        shellPrint(sh, "lock seq DF 34 BE CA sent; PROTSTAT=0x%02X (%s)\r\n",
            Tlf_Read(TLF_PROTSTAT), Tlf_IsLocked() ? "locked" : "UNLOCKED");
        return 0;
    }
    if (strcmp(argv[1], "prot") == 0)
    {
        uint8 p = Tlf_Read(TLF_PROTSTAT);
        shellPrint(sh, "PROTSTAT=0x%02X KEY1=%u KEY2=%u KEY3=%u KEY4=%u LOCK=%u(%s)\r\n", p,
            (p >> 4) & 1u, (p >> 5) & 1u, (p >> 6) & 1u, (p >> 7) & 1u,
            p & 1u, (p & 1u) ? "locked" : "UNLOCKED");
        return 0;
    }

    /* ---------- goto ---------- */
    if (strcmp(argv[1], "goto") == 0)
    {
        uint8 st = 0xFF, trk2 = 0, trk1 = 0, com = 0, vref = 0;
        uint8 cur;
        if (argc < 3) { shellPrint(sh, "Usage: tlf goto <normal|sleep|standby|wake|init> [trk2 trk1 com vref]\r\n"); return -1; }
        if (strcmp(argv[2], "normal") == 0) st = TLF_STATE_NORMAL;
        else if (strcmp(argv[2], "sleep") == 0) st = TLF_STATE_SLEEP;
        else if (strcmp(argv[2], "standby") == 0) st = TLF_STATE_STANDBY;
        else if (strcmp(argv[2], "wake") == 0) st = TLF_STATE_WAKE;
        else if (strcmp(argv[2], "init") == 0) st = TLF_STATE_INIT;
        else { shellPrint(sh, "Unknown state '%s'\r\n", argv[2]); return -1; }
        if (argc >= 7) { trk2 = (uint8)Tlf_ParseU32(argv[3], 0); trk1 = (uint8)Tlf_ParseU32(argv[4], 0);
                         com = (uint8)Tlf_ParseU32(argv[5], 0); vref = (uint8)Tlf_ParseU32(argv[6], 0); }
        else
        {   /* keep current rail enables: read DEVSTAT enable bits */
            cur = Tlf_Read(TLF_DEVSTAT);
            trk2 = (uint8)((cur >> 7) & 1u); trk1 = (uint8)((cur >> 6) & 1u);
            com = (uint8)((cur >> 5) & 1u); vref = (uint8)((cur >> 3) & 1u);
        }
        Tlf_GotoState(st, trk2, trk1, com, vref);
        Tlf_BusyWaitMs(2);
        shellPrint(sh, "goto %s (rails trk2=%u trk1=%u com=%u vref=%u); DEVSTAT=0x%02X (%s) SYSSF=0x%02X\r\n",
            argv[2], trk2, trk1, com, vref, Tlf_Read(TLF_DEVSTAT),
            Tlf_StateName(Tlf_Read(TLF_DEVSTAT) & 0x07u), Tlf_Read(TLF_SYSSF));
        shellPrint(sh, "Note: SLEEP/STANDBY need ENA/WAK handling + TRDEL; see README.\r\n");
        return 0;
    }

    /* ---------- flags ---------- */
    if (strcmp(argv[1], "flags") == 0)
    {
        Tlf_PrintFlags(sh);
        return 0;
    }

    /* ---------- clear ---------- */
    if (strcmp(argv[1], "clear") == 0 || strcmp(argv[1], "clr") == 0)
    {
        const char *t = (argc >= 3) ? argv[2] : "all";
        uint8 v = 0xFFu;
        if (strcmp(t, "all") == 0)
        {
            Tlf_Write(TLF_SYSFAIL, v); Tlf_Write(TLF_INITERR, v); Tlf_Write(TLF_IF, v);
            Tlf_Write(TLF_SYSSF, v); Tlf_Write(TLF_WKSF, v); Tlf_Write(TLF_SPISF, v);
            Tlf_Write(TLF_MONSF0, v); Tlf_Write(TLF_MONSF1, v); Tlf_Write(TLF_MONSF2, v);
            Tlf_Write(TLF_MONSF3, v); Tlf_Write(TLF_OTFAIL, v); Tlf_Write(TLF_OTWRNSF, v);
        }
        else if (strcmp(t, "sysfail") == 0) Tlf_Write(TLF_SYSFAIL, v);
        else if (strcmp(t, "initerr") == 0) Tlf_Write(TLF_INITERR, v);
        else if (strcmp(t, "if") == 0) Tlf_Write(TLF_IF, v);
        else if (strcmp(t, "syssf") == 0) Tlf_Write(TLF_SYSSF, v);
        else if (strcmp(t, "wksf") == 0) Tlf_Write(TLF_WKSF, v);
        else if (strcmp(t, "spisf") == 0) Tlf_Write(TLF_SPISF, v);
        else if (strcmp(t, "monsf") == 0) { Tlf_Write(TLF_MONSF0, v); Tlf_Write(TLF_MONSF1, v);
            Tlf_Write(TLF_MONSF2, v); Tlf_Write(TLF_MONSF3, v); }
        else if (strcmp(t, "ot") == 0) { Tlf_Write(TLF_OTFAIL, v); Tlf_Write(TLF_OTWRNSF, v); }
        else { shellPrint(sh, "Unknown clear target '%s'\r\n", t); return -1; }
        shellPrint(sh, "cleared %s (wrote 0xFF, rw1c). Flags now:\r\n", t);
        Tlf_PrintFlags(sh);
        return 0;
    }

    /* ---------- devcfg ---------- */
    if (strcmp(argv[1], "devcfg") == 0)
    {
        if (argc >= 3)
        {
            uint8 trdel = (uint8)(Tlf_ParseU32(argv[2], 8) & 0x0Fu);
            uint8 cur = (uint8)(Tlf_Read(TLF_DEVCFG0) & 0xF0u);
            Tlf_Write(TLF_DEVCFG0, (uint8)(cur | trdel));
        }
        {
            uint8 d0 = Tlf_Read(TLF_DEVCFG0), d1 = Tlf_Read(TLF_DEVCFG1), d2 = Tlf_Read(TLF_DEVCFG2);
            shellPrint(sh, "DEVCFG0=0x%02X (WKTIMEN=%u CYC=%s TRDEL=%u -> %uus)\r\n", d0,
                (d0 >> 7) & 1u, (d0 & 0x40u) ? "10ms" : "10us",
                d0 & 0x0Fu, (unsigned)((d0 & 0x0Fu) + 1u) * 100u);
            shellPrint(sh, "DEVCFG1=0x%02X (RESDEL=%u) DEVCFG2=0x%02X (EVC=%u STU=%u FRE=%s CMON=%u CTHR=%u ESYN=%u/%u)\r\n",
                d1, d1 & 0x07u, d2, (d2 >> 7) & 1u, (d2 >> 6) & 1u, (d2 & 0x20u) ? "high/2.2MHz" : "low",
                (d2 >> 4) & 1u, (d2 >> 2) & 3u, (d2 >> 1) & 1u, d2 & 1u);
        }
        return 0;
    }

    /* ---------- syscfg ---------- */
    if (strcmp(argv[1], "syscfg") == 0)
    {
        if (argc >= 7)
        {
            uint8 v = (uint8)(((Tlf_ParseU32(argv[2], 0) & 0x07u) << 5) |
                              ((Tlf_ParseU32(argv[3], 0) & 0x01u) << 4) |
                              ((Tlf_ParseU32(argv[4], 1) & 0x01u) << 3) |
                              ((Tlf_ParseU32(argv[5], 0) & 0x01u) << 2) |
                              (Tlf_ParseU32(argv[6], 0) & 0x03u));
            Tlf_Unlock();
            Tlf_Write(TLF_SYSPCFG1, v);
            Tlf_Lock();
            shellPrint(sh, "SYSPCFG1 <= 0x%02X; RSYSPCFG1=0x%02X\r\n", v, Tlf_Read(TLF_RSYSPCFG1));
        }
        shellPrint(sh, "SYSPCFG1 req=0x%02X status RSYSPCFG1=0x%02X (SS2DEL=%u ERRSLPEN=%u ERREN=%u RECEN=%u REC=%u)\r\n",
            Tlf_Read(TLF_SYSPCFG1), Tlf_Read(TLF_RSYSPCFG1),
            (Tlf_Read(TLF_RSYSPCFG1) >> 5) & 7u, (Tlf_Read(TLF_RSYSPCFG1) >> 4) & 1u,
            (Tlf_Read(TLF_RSYSPCFG1) >> 3) & 1u, (Tlf_Read(TLF_RSYSPCFG1) >> 2) & 1u,
            Tlf_Read(TLF_RSYSPCFG1) & 3u);
        shellPrint(sh, "Usage: tlf syscfg <ss2 0-4> <errslp 0/1> <erren 0/1> <recen 0/1> <rec 0-3>\r\n");
        return 0;
    }

    /* ---------- wdcfg ---------- */
    if (strcmp(argv[1], "wdcfg") == 0)
    {
        if (argc >= 7)
        {
            uint8 v = (uint8)(((Tlf_ParseU32(argv[2], 9) & 0x0Fu) << 4) |
                              ((Tlf_ParseU32(argv[3], 1) & 0x01u) << 3) |
                              ((Tlf_ParseU32(argv[4], 0) & 0x01u) << 2) |
                              ((Tlf_ParseU32(argv[5], 1) & 0x01u) << 1) |
                              (Tlf_ParseU32(argv[6], 1) & 0x01u));
            Tlf_Unlock();
            Tlf_Write(TLF_WDCFG0, v);
            Tlf_Lock();
            shellPrint(sh, "WDCFG0 <= 0x%02X; RWDCFG0=0x%02X\r\n", v, Tlf_Read(TLF_RWDCFG0));
        }
        {
            uint8 r = Tlf_Read(TLF_RWDCFG0);
            shellPrint(sh, "WDCFG0 req=0x%02X status RWDCFG0=0x%02X (ETHR=%u WWDEN=%u FWDEN=%u TSEL=%s CYC=%s)\r\n",
                Tlf_Read(TLF_WDCFG0), r, (r >> 4) & 0x0Fu, (r >> 3) & 1u, (r >> 2) & 1u,
                (r & 2u) ? "SPI" : "WDI-pin", (r & 1u) ? "1ms" : "0.1ms");
        }
        shellPrint(sh, "Usage: tlf wdcfg <wwdethr 0-15> <wwden 0/1> <fwden 0/1> <tsel 0=WDI/1=SPI> <cyc 0=0.1ms/1=1ms>\r\n");
        return 0;
    }
    if (strcmp(argv[1], "wdcfg1") == 0)
    {
        if (argc >= 4)
        {
            uint8 v = (uint8)(((Tlf_ParseU32(argv[2], 0) & 0x01u) << 4) | (Tlf_ParseU32(argv[3], 9) & 0x0Fu));
            Tlf_Unlock();
            Tlf_Write(TLF_WDCFG1, v);
            Tlf_Lock();
            shellPrint(sh, "WDCFG1 <= 0x%02X; RWDCFG1=0x%02X\r\n", v, Tlf_Read(TLF_RWDCFG1));
        }
        shellPrint(sh, "WDCFG1 req=0x%02X status=0x%02X (WDSLPEN=%u FWDETHR=%u)\r\n",
            Tlf_Read(TLF_WDCFG1), Tlf_Read(TLF_RWDCFG1),
            (Tlf_Read(TLF_RWDCFG1) >> 4) & 1u, Tlf_Read(TLF_RWDCFG1) & 0x0Fu);
        shellPrint(sh, "Usage: tlf wdcfg1 <wdsleepen 0/1> <fwdethr 0-15>\r\n");
        return 0;
    }

    /* ---------- fwdcfg ---------- */
    if (strcmp(argv[1], "fwdcfg") == 0)
    {
        if (argc >= 3)
        {
            uint8 v = (uint8)(Tlf_ParseU32(argv[2], 0x0B) & 0x1Fu);
            Tlf_Unlock();
            Tlf_Write(TLF_FWDCFG, v);
            Tlf_Lock();
            shellPrint(sh, "FWDCFG <= 0x%02X; RFWDCFG=0x%02X\r\n", v, Tlf_Read(TLF_RFWDCFG));
        }
        shellPrint(sh, "FWDCFG req=0x%02X status=0x%02X (WDHBTP=%u -> %u wd cycles)\r\n",
            Tlf_Read(TLF_FWDCFG), Tlf_Read(TLF_RFWDCFG),
            Tlf_Read(TLF_RFWDCFG) & 0x1Fu, (unsigned)((Tlf_Read(TLF_RFWDCFG) & 0x1Fu) + 1u) * 50u);
        shellPrint(sh, "Usage: tlf fwdcfg <hbt 0-31>\r\n");
        return 0;
    }

    /* ---------- wwcfg ---------- */
    if (strcmp(argv[1], "wwcfg") == 0)
    {
        if (argc >= 4)
        {
            uint8 cw = (uint8)(Tlf_ParseU32(argv[2], 6) & 0x1Fu);
            uint8 ow = (uint8)(Tlf_ParseU32(argv[3], 11) & 0x1Fu);
            Tlf_Unlock();
            Tlf_Write(TLF_WWDCFG0, cw);
            Tlf_Write(TLF_WWDCFG1, ow);
            Tlf_Lock();
            shellPrint(sh, "WWDCFG0(CW) <= 0x%02X; WWDCFG1(OW) <= 0x%02X\r\n", cw, ow);
        }
        {
            uint8 cyc = Tlf_Read(TLF_RWDCFG0) & 1u;
            uint32 unit = cyc ? 50u : 5u;  /* ms if 1ms cyc, else 0.1ms cyc: value*50 cycles */
            shellPrint(sh, "CW req=0x%02X status=0x%02X (%lu ms) OW req=0x%02X status=0x%02X (%lu ms) @%s\r\n",
                Tlf_Read(TLF_WWDCFG0), Tlf_Read(TLF_RWWDCFG0),
                (unsigned long)(Tlf_Read(TLF_RWWDCFG0) & 0x1Fu) * unit,
                Tlf_Read(TLF_WWDCFG1), Tlf_Read(TLF_RWWDCFG1),
                (unsigned long)(Tlf_Read(TLF_RWWDCFG1) & 0x1Fu) * unit,
                cyc ? "1ms-cyc" : "0.1ms-cyc");
        }
        shellPrint(sh, "Usage: tlf wwcfg <cw 0-31> <ow 0-31>  (x50 wd cycles each)\r\n");
        return 0;
    }

    /* ---------- wwd ---------- */
    if (strcmp(argv[1], "wwd") == 0)
    {
        const char *s = (argc >= 3) ? argv[2] : "status";
        if (strcmp(s, "trig") == 0 || strcmp(s, "feed") == 0 || strcmp(s, "kick") == 0)
        {
            uint32 n = (argc >= 4) ? Tlf_ParseU32(argv[3], 1) : 1;
            uint32 i;
            if (n > 1000) n = 1000;
            for (i = 0; i < n; i++)
            {
                uint8 ts = Tlf_WwdTrigger();
                if (n <= 8 || (i % 100) == 0)
                    shellPrint(sh, "feed %lu: TRIG_STATUS=%u WWDSTAT=%02X SYSSF=%02X\r\n",
                        (unsigned long)i, ts, Tlf_Read(TLF_WWDSTAT), Tlf_Read(TLF_SYSSF));
                if (n > 1) Tlf_BusyWaitMs(5);
            }
            return 0;
        }
        if (strcmp(s, "status") == 0)
        {
            uint8 r = Tlf_Read(TLF_RWDCFG0);
            shellPrint(sh, "WWDSTAT.WWDECNT=%u ETHR=%u EN=%u TSEL=%s feeds=%u auto=%s/%lums\r\n",
                Tlf_Read(TLF_WWDSTAT) & 0x0Fu, (r >> 4) & 0x0Fu, (r >> 3) & 1u,
                (r & 2u) ? "SPI" : "WDI", g_tlf.wwdFeeds,
                g_tlf.wwdAuto ? "on" : "off", (unsigned long)g_tlf.wwdPeriodMs);
            return 0;
        }
        if (strcmp(s, "auto") == 0)
        {
            const char *m = (argc >= 4) ? argv[3] : "off";
            if (strcmp(m, "on") == 0 || strcmp(m, "1") == 0)
            {
                g_tlf.wwdPeriodMs = (argc >= 5) ? Tlf_ParseU32(argv[4], 10) : 10;
                if (g_tlf.wwdPeriodMs < 1) g_tlf.wwdPeriodMs = 1;
                g_tlf.lastWwdTick = g_TickCount_1ms;
                g_tlf.wwdAuto = TRUE;
                shellPrint(sh, "WWD SPI auto-feed ON every %lu ms (background)\r\n", (unsigned long)g_tlf.wwdPeriodMs);
            }
            else { g_tlf.wwdAuto = FALSE; shellPrint(sh, "WWD SPI auto-feed OFF\r\n"); }
            return 0;
        }
        shellPrint(sh, "Usage: tlf wwd <trig [n]|status|auto <on [ms]|off>>\r\n");
        return -1;
    }

    /* ---------- fwd ---------- */
    if (strcmp(argv[1], "fwd") == 0)
    {
        const char *s = (argc >= 3) ? argv[2] : "status";
        if (strcmp(s, "status") == 0)
        {
            uint8 s0 = Tlf_Read(TLF_FWDSTAT0), s1 = Tlf_Read(TLF_FWDSTAT1);
            shellPrint(sh, "FWDSTAT0=0x%02X (RSPOK=%u RSPC=%u QUEST=0x%X) FWDSTAT1=0x%02X (FWDECNT=%u) seq=%s\r\n",
                s0, (s0 >> 6) & 1u, (s0 >> 4) & 3u, s0 & 0x0Fu, s1, s1 & 0x0Fu,
                Tlf_FwdBusy() ? "RUNNING" : "idle");
            shellPrint(sh, "RFWDCFG=0x%02X RWDCFG1=0x%02X (FWDEN=%u ETHR=%u)\r\n",
                Tlf_Read(TLF_RFWDCFG), Tlf_Read(TLF_RWDCFG1),
                (Tlf_Read(TLF_RWDCFG0) >> 2) & 1u, Tlf_Read(TLF_RWDCFG1) & 0x0Fu);
            return 0;
        }
        if (strcmp(s, "answer") == 0)
        {
            if (Tlf_FwdKick(FALSE, g_TickCount_1ms, 0))
                shellPrint(sh, "FWD answer sequence started (4 bytes @700ms, Table 26); check 'tlf fwd status' in ~4s\r\n");
            else
                shellPrint(sh, "FWD answer busy (sequencer active or RSPC!=3); check 'tlf fwd status'\r\n");
            return 0;
        }
        if (strcmp(s, "bgauto") == 0)
        {
            const char *m = (argc >= 4) ? argv[3] : "off";
            if (strcmp(m, "on") == 0 || strcmp(m, "1") == 0)
            {
                uint32 ms = (argc >= 5) ? Tlf_ParseU32(argv[4], 5000) : 5000;
                if (Tlf_FwdKick(TRUE, g_TickCount_1ms, ms))
                    shellPrint(sh, "FWD background auto-answer ON (full answer every >=4s, 1 byte/700ms)\r\n");
                else
                    shellPrint(sh, "FWD sequencer busy; try again after it completes\r\n");
            }
            else { Tlf_FwdRepeatStop(); shellPrint(sh, "FWD background auto-answer OFF (current sequence runs to completion)\r\n"); }
            return 0;
        }
        if (strcmp(s, "resp") == 0)
        {
            if (argc < 4) { shellPrint(sh, "Usage: tlf fwd resp <byte>\r\n"); return -1; }
            Tlf_Write(TLF_FWDRSP, (uint8)Tlf_ParseU32(argv[3], 0));
            shellPrint(sh, "FWDRSP written; FWDSTAT0=0x%02X\r\n", Tlf_Read(TLF_FWDSTAT0));
            return 0;
        }
        if (strcmp(s, "sync") == 0)
        {
            if (argc < 4) { shellPrint(sh, "Usage: tlf fwd sync <byte>\r\n"); return -1; }
            Tlf_Write(TLF_FWDRSPSYNC, (uint8)Tlf_ParseU32(argv[3], 0));
            shellPrint(sh, "FWDRSPSYNC written; FWDSTAT0=0x%02X FWDSTAT1=0x%02X\r\n",
                Tlf_Read(TLF_FWDSTAT0), Tlf_Read(TLF_FWDSTAT1));
            return 0;
        }
        shellPrint(sh, "Usage: tlf fwd <status|answer|resp <b>|sync <b>|bgauto <on [ms]|off>>\r\n");
        return -1;
    }

    /* ---------- wdi ---------- */
    if (strcmp(argv[1], "wdi") == 0)
    {
        const char *s = (argc >= 3) ? argv[2] : "";
        if (strcmp(s, "high") == 0 || strcmp(s, "1") == 0) { g_tlf.wdiAuto = FALSE; Tlf_WdiHigh(); }
        else if (strcmp(s, "low") == 0 || strcmp(s, "0") == 0) { g_tlf.wdiAuto = FALSE; Tlf_WdiLow(); }
        else if (strcmp(s, "toggle") == 0) { g_tlf.wdiAuto = FALSE; Tlf_WdiToggle(); }
        else if (strcmp(s, "pulse") == 0)
        {
            uint32 ms = (argc >= 4) ? Tlf_ParseU32(argv[3], 5) : 5;
            if (ms > 2000) ms = 2000;
            g_tlf.wdiAuto = FALSE;
            Tlf_WdiHigh(); Tlf_BusyWaitMs(ms); Tlf_WdiLow();
            shellPrint(sh, "WDI pulse %lu ms done (need 2-high+2-low samples; see README)\r\n", (unsigned long)ms);
        }
        else if (strcmp(s, "auto") == 0)
        {
            const char *m = (argc >= 4) ? argv[3] : "off";
            if (strcmp(m, "on") == 0 || strcmp(m, "1") == 0)
            {
                g_tlf.wdiPeriodMs = (argc >= 5) ? Tlf_ParseU32(argv[4], 10) : 10;
                if (g_tlf.wdiPeriodMs < 1) g_tlf.wdiPeriodMs = 1;
                g_tlf.lastWdiTick = g_TickCount_1ms;
                g_tlf.wdiAuto = TRUE;
                shellPrint(sh, "WDI auto-toggle ON every %lu ms\r\n", (unsigned long)g_tlf.wdiPeriodMs);
            }
            else { g_tlf.wdiAuto = FALSE; shellPrint(sh, "WDI auto OFF\r\n"); }
        }
        else { shellPrint(sh, "Usage: tlf wdi <high|low|toggle|pulse <ms>|auto <on [ms]|off>>\r\n"); return -1; }
        shellPrint(sh, "WDI(P14.3)=%u auto=%s\r\n", Tlf_WdiLevel(), g_tlf.wdiAuto ? "on" : "off");
        return 0;
    }

    /* ---------- err ---------- */
    if (strcmp(argv[1], "err") == 0)
    {
        const char *s = (argc >= 3) ? argv[2] : "";
        if (strcmp(s, "high") == 0 || strcmp(s, "1") == 0) { g_tlf.errAuto = FALSE; Tlf_ErrHigh(); }
        else if (strcmp(s, "low") == 0 || strcmp(s, "0") == 0) { g_tlf.errAuto = FALSE; Tlf_ErrLow(); }
        else if (strcmp(s, "toggle") == 0) { g_tlf.errAuto = FALSE; Tlf_ErrToggle(); }
        else if (strcmp(s, "burst") == 0)
        {
            uint32 n = (argc >= 4) ? Tlf_ParseU32(argv[3], 10) : 10;
            uint32 ms = (argc >= 5) ? Tlf_ParseU32(argv[4], 5) : 5;
            uint32 i;
            if (n > 500) n = 500;
            if (ms < 1) ms = 1;
            if (ms > 500) ms = 500;
            g_tlf.errAuto = FALSE;
            for (i = 0; i < n; i++) { Tlf_ErrToggle(); Tlf_BusyWaitMs(ms); }
            shellPrint(sh, "ERR burst %lu toggles x %lu ms done\r\n", (unsigned long)n, (unsigned long)ms);
        }
        else if (strcmp(s, "auto") == 0)
        {
            const char *m = (argc >= 4) ? argv[3] : "off";
            if (strcmp(m, "on") == 0 || strcmp(m, "1") == 0)
            {
                g_tlf.errPeriodMs = (argc >= 5) ? Tlf_ParseU32(argv[4], 5) : 5;
                if (g_tlf.errPeriodMs < 1) g_tlf.errPeriodMs = 1;
                g_tlf.lastErrTick = g_TickCount_1ms;
                g_tlf.errAuto = TRUE;
                shellPrint(sh, "ERR auto-toggle ON every %lu ms (mimics SMU FSP)\r\n", (unsigned long)g_tlf.errPeriodMs);
            }
            else { g_tlf.errAuto = FALSE; shellPrint(sh, "ERR auto OFF\r\n"); }
        }
        else { shellPrint(sh, "Usage: tlf err <high|low|toggle|burst <n> <ms>|auto <on [ms]|off>>\r\n"); return -1; }
        shellPrint(sh, "ERR(P33.8)=%u auto=%s; production: route SMU FSP0 (IfxSmu_FSP0_P33_8_OUT)\r\n",
            Tlf_ErrLevel(), g_tlf.errAuto ? "on" : "off");
        return 0;
    }

    /* ---------- ss ---------- */
    if (strcmp(argv[1], "ss") == 0)
    {
        shellPrint(sh, "SS1(P33.9)=%u (%s)\r\n", Tlf_Ss1Level(),
            Tlf_Ss1Level() ? "high: no safe state" : "LOW: SAFE STATE active");
        return 0;
    }

    /* ---------- rails ---------- */
    if (strcmp(argv[1], "rails") == 0)
    {
        uint8 com, vref, trk1 = 0, trk2 = 0;
        if (argc < 4) { shellPrint(sh, "Usage: tlf rails <com 0/1> <vref 0/1> [trk1 0/1] [trk2 0/1]\r\n"); return -1; }
        com = (uint8)Tlf_ParseU32(argv[2], 0); vref = (uint8)Tlf_ParseU32(argv[3], 0);
        if (argc >= 5) trk1 = (uint8)Tlf_ParseU32(argv[4], 0);
        if (argc >= 6) trk2 = (uint8)Tlf_ParseU32(argv[5], 0);
        {
            uint8 cur = (uint8)(Tlf_Read(TLF_DEVSTAT) & 0x07u);
            if (cur == TLF_STATE_NONE) cur = TLF_STATE_INIT;
            Tlf_GotoState(cur, trk2, trk1, com, vref);
        }
        shellPrint(sh, "rails req com=%u vref=%u trk1=%u trk2=%u; DEVSTAT=0x%02X VMONSTAT=0x%02X\r\n",
            com, vref, trk1, trk2, Tlf_Read(TLF_DEVSTAT), Tlf_Read(TLF_VMONSTAT));
        return 0;
    }

    /* ---------- wktim ---------- */
    if (strcmp(argv[1], "wktim") == 0)
    {
        if (argc >= 3)
        {
            uint32 v = Tlf_ParseU32(argv[2], 0) & 0xFFFFFFu;
            Tlf_Write(TLF_WKTIMCFG0, (uint8)(v & 0xFFu));
            Tlf_Write(TLF_WKTIMCFG1, (uint8)((v >> 8) & 0xFFu));
            Tlf_Write(TLF_WKTIMCFG2, (uint8)((v >> 16) & 0xFFu));
        }
        {
            uint32 v = (uint32)Tlf_Read(TLF_WKTIMCFG0) |
                       ((uint32)Tlf_Read(TLF_WKTIMCFG1) << 8) |
                       ((uint32)Tlf_Read(TLF_WKTIMCFG2) << 16);
            uint8 d0 = Tlf_Read(TLF_DEVCFG0);
            shellPrint(sh, "WKTIM=0x%06X (%lu %s) WKTIMEN=%u\r\n", v, (unsigned long)v,
                (d0 & 0x40u) ? "x10ms" : "x10us", (d0 >> 7) & 1u);
        }
        shellPrint(sh, "Usage: tlf wktim [cycles]  (unit = DEVCFG0.WKTIMCYC)\r\n");
        return 0;
    }

    /* ---------- abist ---------- */
    if (strcmp(argv[1], "abist") == 0)
    {
        const char *s = (argc >= 3) ? argv[2] : "show";
        if (strcmp(s, "show") == 0)
        {
            shellPrint(sh, "ABIST_CTRL0=0x%02X CTRL1=0x%02X SEL0=0x%02X SEL1=0x%02X SEL2=0x%02X\r\n",
                Tlf_Read(TLF_ABIST_CTRL0), Tlf_Read(TLF_ABIST_CTRL1), Tlf_Read(TLF_ABIST_SEL0),
                Tlf_Read(TLF_ABIST_SEL1), Tlf_Read(TLF_ABIST_SEL2));
            shellPrint(sh, "IF=0x%02X MONSF1=0x%02X MONSF2=0x%02X MONSF3=0x%02X\r\n",
                Tlf_Read(TLF_IF), Tlf_Read(TLF_MONSF1), Tlf_Read(TLF_MONSF2), Tlf_Read(TLF_MONSF3));
            return 0;
        }
        if (strcmp(s, "sel") == 0)
        {
            uint32 mask, val;
            if (argc < 5) { shellPrint(sh, "Usage: tlf abist sel <0|1|2> <val>\r\n"); return -1; }
            mask = Tlf_ParseU32(argv[3], 0); val = Tlf_ParseU32(argv[4], 0) & 0xFFu;
            if (mask == 0) Tlf_Write(TLF_ABIST_SEL0, (uint8)val);
            else if (mask == 1) Tlf_Write(TLF_ABIST_SEL1, (uint8)val);
            else if (mask == 2) Tlf_Write(TLF_ABIST_SEL2, (uint8)val);
            else { shellPrint(sh, "mask must be 0/1/2\r\n"); return -1; }
            shellPrint(sh, "ABIST_SEL%u <= 0x%02X\r\n", mask, val);
            return 0;
        }
        if (strcmp(s, "ctrl0") == 0)
        {
            if (argc < 4) { shellPrint(sh, "Usage: tlf abist ctrl0 <val> (bit0=START, bit1=PATH, bit2=SINGLE, bit3=INT)\r\n"); return -1; }
            Tlf_Write(TLF_ABIST_CTRL0, (uint8)Tlf_ParseU32(argv[3], 0));
            shellPrint(sh, "ABIST_CTRL0 <= 0x%02X; now=0x%02X\r\n", (uint8)Tlf_ParseU32(argv[3], 0), Tlf_Read(TLF_ABIST_CTRL0));
            return 0;
        }
        if (strcmp(s, "ctrl1") == 0)
        {
            if (argc < 4) { shellPrint(sh, "Usage: tlf abist ctrl1 <val>\r\n"); return -1; }
            Tlf_Write(TLF_ABIST_CTRL1, (uint8)Tlf_ParseU32(argv[3], 0));
            shellPrint(sh, "ABIST_CTRL1 <= 0x%02X; now=0x%02X\r\n", (uint8)Tlf_ParseU32(argv[3], 0), Tlf_Read(TLF_ABIST_CTRL1));
            return 0;
        }
        if (strcmp(s, "start") == 0)
        {
            uint8 c0 = (uint8)(Tlf_Read(TLF_ABIST_CTRL0) | 0x01u);
            Tlf_Write(TLF_ABIST_CTRL0, c0);
            Tlf_BusyWaitMs(50);
            shellPrint(sh, "ABIST started; CTRL0=0x%02X (STATUS=%u %s) IF=0x%02X\r\n",
                Tlf_Read(TLF_ABIST_CTRL0), (Tlf_Read(TLF_ABIST_CTRL0) >> 4) & 0x0Fu,
                (((Tlf_Read(TLF_ABIST_CTRL0) >> 4) & 0x0Fu) == 5u) ? "PASS" : "FAIL/CHECK-SEL",
                Tlf_Read(TLF_IF));
            return 0;
        }
        shellPrint(sh, "Usage: tlf abist <show|sel <m> <v>|ctrl0 <v>|ctrl1 <v>|start>\r\n");
        return -1;
    }

    /* ---------- buck ---------- */
    if (strcmp(argv[1], "buck") == 0)
    {
        if (argc >= 4 && strcmp(argv[2], "set") == 0)
        {
            /* tlf buck set <freq_change|spread|main> <val> */
            if (argc < 5) { shellPrint(sh, "Usage: tlf buck set <freq|spread|main> <val>\r\n"); return -1; }
            {
                uint8 v = (uint8)Tlf_ParseU32(argv[4], 0);
                if (strcmp(argv[3], "freq") == 0) Tlf_Write(TLF_BCK_FREQ_CHANGE, v);
                else if (strcmp(argv[3], "spread") == 0) Tlf_Write(TLF_BCK_FRE_SPREAD, v);
                else if (strcmp(argv[3], "main") == 0) Tlf_Write(TLF_BCK_MAIN_CTRL, v);
                else { shellPrint(sh, "target must be freq|spread|main\r\n"); return -1; }
            }
        }
        shellPrint(sh, "BCK_FREQ_CHANGE=0x%02X FRE_SPREAD=0x%02X MAIN_CTRL=0x%02X (FRE pin open=high/2.2MHz)\r\n",
            Tlf_Read(TLF_BCK_FREQ_CHANGE), Tlf_Read(TLF_BCK_FRE_SPREAD), Tlf_Read(TLF_BCK_MAIN_CTRL));
        return 0;
    }

    /* ---------- baud ---------- */
    if (strcmp(argv[1], "baud") == 0)
    {
        uint32 b;
        if (argc < 3) { shellPrint(sh, "baud=%lu Hz. Usage: tlf baud <100000-10000000>\r\n", (unsigned long)g_tlf.baud); return 0; }
        b = Tlf_ParseU32(argv[2], 0);
        if (Tlf_SetBaudrate(b)) shellPrint(sh, "QSPI2 baud=%lu Hz\r\n", (unsigned long)g_tlf.baud);
        else shellPrint(sh, "Invalid baud %lu (100k-10M; SLEEP max 1.5M)\r\n", (unsigned long)b);
        return 0;
    }

    /* ---------- demo (Infineon sequence) ---------- */
    if (strcmp(argv[1], "demo") == 0 || strcmp(argv[1], "init") == 0)
    {
        uint8 w, s1;
        shellPrint(sh, "demo: unlock...\r\n");
        Tlf_Unlock();
        shellPrint(sh, "PROTSTAT=0x%02X\r\n", Tlf_Read(TLF_PROTSTAT));
        /* disable WWD + FWD (both must be quiet for INIT->NORMAL) */
        w = Tlf_Read(TLF_RWDCFG0);
        shellPrint(sh, "RWDCFG0=0x%02X -> disable WWD+FWD...\r\n", w);
        Tlf_Unlock();
        Tlf_Write(TLF_WDCFG0, (uint8)(w & 0xF3u));
        /* disable ERR mon */
        s1 = Tlf_Read(TLF_RSYSPCFG1);
        shellPrint(sh, "RSYSPCFG1=0x%02X -> clear ERREN...\r\n", s1);
        Tlf_Write(TLF_SYSPCFG1, (uint8)(s1 & 0xF7u));
        Tlf_Lock();
        shellPrint(sh, "locked; RWDCFG0=0x%02X RSYSPCFG1=0x%02X\r\n",
            Tlf_Read(TLF_RWDCFG0), Tlf_Read(TLF_RSYSPCFG1));
        /* enable rails COM+VREF, keep state */
        {
            uint8 cur = (uint8)(Tlf_Read(TLF_DEVSTAT) & 0x07u);
            if (cur == TLF_STATE_NONE) cur = TLF_STATE_INIT;
            Tlf_GotoState(cur, 0, 0, 1, 1);
        }
        shellPrint(sh, "rails COM+VREF on; DEVSTAT=0x%02X VMONSTAT=0x%02X\r\n",
            Tlf_Read(TLF_DEVSTAT), Tlf_Read(TLF_VMONSTAT));
        /* clear + goto normal */
        Tlf_Write(TLF_SYSSF, 0xFFu);
        Tlf_BusyWaitMs(1);
        {
            uint8 cur = (uint8)(Tlf_Read(TLF_DEVSTAT) & 0x07u);
            uint8 dev = Tlf_Read(TLF_DEVSTAT);
            Tlf_GotoState(TLF_STATE_NORMAL, (uint8)((dev >> 7) & 1u), (uint8)((dev >> 6) & 1u),
                          1, 1);
            (void)cur;
        }
        Tlf_BusyWaitMs(2);
        shellPrint(sh, "goto NORMAL; DEVSTAT=0x%02X (%s) SYSSF=0x%02X SS1=%u\r\n",
            Tlf_Read(TLF_DEVSTAT), Tlf_StateName(Tlf_Read(TLF_DEVSTAT) & 0x07u),
            Tlf_Read(TLF_SYSSF), Tlf_Ss1Level());
        return 0;
    }

    /* ---------- gpio (raw port regs debug) ---------- */
    if (strcmp(argv[1], "gpio") == 0)
    {
        shellPrint(sh, "P14.OUT=0x%08lX IN=0x%08lX IOCR0=0x%08lX IOCR4=0x%08lX PDISC=0x%08lX\r\n",
            (unsigned long)MODULE_P14.OUT.U, (unsigned long)MODULE_P14.IN.U,
            (unsigned long)MODULE_P14.IOCR0.U, (unsigned long)MODULE_P14.IOCR4.U,
            (unsigned long)MODULE_P14.PDISC.U);
        shellPrint(sh, "P33.OUT=0x%08lX IN=0x%08lX IOCR8=0x%08lX PDISC=0x%08lX\r\n",
            (unsigned long)MODULE_P33.OUT.U, (unsigned long)MODULE_P33.IN.U,
            (unsigned long)MODULE_P33.IOCR8.U, (unsigned long)MODULE_P33.PDISC.U);
        shellPrint(sh, "P14.3 OUT=%lu IN=%lu P33.8 OUT=%lu IN=%lu P33.9 IN=%lu\r\n",
            (unsigned long)((MODULE_P14.OUT.U >> 3) & 1u), (unsigned long)((MODULE_P14.IN.U >> 3) & 1u),
            (unsigned long)((MODULE_P33.OUT.U >> 8) & 1u), (unsigned long)((MODULE_P33.IN.U >> 8) & 1u),
            (unsigned long)((MODULE_P33.IN.U >> 9) & 1u));
        return 0;
    }

    shellPrint(sh, "Unknown tlf subcommand '%s'. Try 'tlf help'.\r\n", argv[1]);
    return -1;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), tlf, cmd_tlf, TLF35584 SBC control);
