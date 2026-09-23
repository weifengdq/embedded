/**********************************************************************************************************************
 * \file tlf35584.c
 * \brief TLF35584 driver: QSPI2 master (HW parity even) + WDI/ERR/SS1 GPIOs + helpers.
 *********************************************************************************************************************/
#include "tlf35584.h"
#include "IfxPort.h"
#include "IfxStm.h"
#include "IfxQspi_SpiMaster.h"
#include "IfxQspi_PinMap.h"
#include "ConfigurationIsr.h"

/* QSPI2 pins for this bench (LFBGA292, non-ADAS map):
 *  SCLK P15.8 alt3, MTSR(MOSI) P15.6 alt3, MRST(MISO) P15.7 RxSel_b, SLSO1(nCS) P14.2 alt3 */
#define TLF_SPI_BUFFER_SIZE 1u

typedef struct
{
    uint16 tx[TLF_SPI_BUFFER_SIZE];
    uint16 rx[TLF_SPI_BUFFER_SIZE];
} Tlf_Buffers;

static IfxQspi_SpiMaster         s_master;
static IfxQspi_SpiMaster_Channel s_channel;
static Tlf_Buffers               s_buf;

Tlf_State g_tlf;

/* FWD Table 26: question -> {RESP3, RESP2, RESP1, RESP0} */
static const uint8 s_fwdResp[16][4] = {
    {0xFF,0x0F,0xF0,0x00}, {0xB0,0x40,0xBF,0x4F},
    {0xE9,0x19,0xE6,0x16}, {0xA6,0x56,0xA9,0x59},
    {0x75,0x85,0x7A,0x8A}, {0x3A,0xCA,0x35,0xC5},
    {0x63,0x93,0x6C,0x9C}, {0x2C,0xDC,0x23,0xD3},
    {0xD2,0x22,0xDD,0x2D}, {0x9D,0x6D,0x92,0x62},
    {0xC4,0x34,0xCB,0x3B}, {0x8B,0x7B,0x84,0x74},
    {0x58,0xA8,0x57,0xA7}, {0x17,0xE7,0x18,0xE8},
    {0x4E,0xBE,0x41,0xB1}, {0x01,0xF1,0x0E,0xFE},
};

IFX_INTERRUPT(tlfTxISR, 0, ISR_PRIORITY_QSPI2_TX);
void tlfTxISR(void)
{
    IfxCpu_enableInterrupts();
    IfxQspi_SpiMaster_isrTransmit(&s_master);
}

IFX_INTERRUPT(tlfRxISR, 0, ISR_PRIORITY_QSPI2_RX);
void tlfRxISR(void)
{
    IfxCpu_enableInterrupts();
    IfxQspi_SpiMaster_isrReceive(&s_master);
}

IFX_INTERRUPT(tlfErISR, 0, ISR_PRIORITY_QSPI2_ER);
void tlfErISR(void)
{
    IfxCpu_enableInterrupts();
    IfxQspi_SpiMaster_isrError(&s_master);
}

static void Tlf_InitChannel(uint32 baud)
{
    IfxQspi_SpiMaster_ChannelConfig ch;
    IfxQspi_SpiMaster_initChannelConfig(&ch, &s_master);

    ch.ch.baudrate                  = (float32)baud;
    ch.ch.mode.dataWidth            = 15;   /* 15 data bits + 1 HW parity bit = 16 clocks */
    ch.ch.mode.csTrailDelay         = IfxQspi_SlsoTiming_2;
    ch.ch.mode.csInactiveDelay      = IfxQspi_SlsoTiming_2;
    ch.ch.mode.shiftClock           = IfxQspi_ShiftClock_shiftTransmitDataOnTrailingEdge;
    ch.ch.mode.parityCheck          = TRUE;
    ch.ch.mode.parityMode           = IfxQspi_ParityMode_even;

    ch.sls.output.pin    = &IfxQspi2_SLSO1_P14_2_OUT;
    ch.sls.output.mode   = IfxPort_OutputMode_pushPull;
    ch.sls.output.driver = IfxPort_PadDriver_cmosAutomotiveSpeed1;

    IfxQspi_SpiMaster_initChannel(&s_channel, &ch);
}

void Tlf_Init(void)
{
    /* GPIOs first (safe levels before any TLF traffic) */
    /* WDI P14.3 output, idle low (TLF internal pull-down; trigger = 2x high samples + 2x low) */
    IfxPort_setPinMode(&MODULE_P14, 3, IfxPort_Mode_outputPushPullGeneral);
    IfxPort_setPinLow(&MODULE_P14, 3);
    /* ERR P33.8 output, idle high (= SMU FSP idle level) */
    IfxPort_setPinMode(&MODULE_P33, 8, IfxPort_Mode_outputPushPullGeneral);
    IfxPort_setPinHigh(&MODULE_P33, 8);
    /* SS1 P33.9 input with pull-up (TLF drives push-pull; low = safe state) */
    IfxPort_setPinMode(&MODULE_P33, 9, IfxPort_Mode_inputPullUp);

    /* QSPI2 master */
    {
        IfxQspi_SpiMaster_Config cfg;
        IfxQspi_SpiMaster_initModuleConfig(&cfg, &MODULE_QSPI2);
        cfg.mode = IfxQspi_Mode_master;

        const IfxQspi_SpiMaster_Pins pins = {
            &IfxQspi2_SCLK_P15_8_OUT, IfxPort_OutputMode_pushPull,
            &IfxQspi2_MTSR_P15_6_OUT, IfxPort_OutputMode_pushPull,
            &IfxQspi2_MRSTB_P15_7_IN, IfxPort_InputMode_pullDown,
            IfxPort_PadDriver_cmosAutomotiveSpeed3
        };
        cfg.pins = &pins;

        cfg.txPriority  = ISR_PRIORITY_QSPI2_TX;
        cfg.rxPriority  = ISR_PRIORITY_QSPI2_RX;
        cfg.erPriority  = ISR_PRIORITY_QSPI2_ER;
        cfg.isrProvider = IfxSrc_Tos_cpu0;

        IfxQspi_SpiMaster_initModule(&s_master, &cfg);
    }

    g_tlf.baud = TLF_QSPI_BAUD_DEFAULT;
    Tlf_InitChannel(g_tlf.baud);

    s_buf.tx[0] = 0;
    s_buf.rx[0] = 0;
    g_tlf.qspiOk     = TRUE;
    g_tlf.wwdAuto    = FALSE;
    g_tlf.wdiAuto    = FALSE;
    g_tlf.errAuto    = FALSE;
    g_tlf.fwdAuto    = FALSE;
    g_tlf.wwdPeriodMs = 10;
    g_tlf.wdiPeriodMs = 10;
    g_tlf.errPeriodMs = 5;
    g_tlf.fwdPeriodMs = 250;
    g_tlf.lastWwdTick = 0;
    g_tlf.lastWdiTick = 0;
    g_tlf.lastErrTick = 0;
    g_tlf.lastFwdTick = 0;
}

boolean Tlf_SetBaudrate(uint32 baud)
{
    if ((baud < 100000u) || (baud > 10000000u))
    {
        return FALSE;
    }
    Tlf_InitChannel(baud);
    g_tlf.baud = baud;
    return TRUE;
}

/* Pack 15-bit SW frame: bit14=CMD, bits13:8=ADDR, bits7:0=DATA (bit15=0, HW adds parity). */
#define TLF_PACK(cmd, addr, data) \
    ((uint16)((((uint16)(cmd) & 0x1u) << 14) | (((uint16)(addr) & 0x3Fu) << 8) | ((uint16)(data) & 0xFFu)))

uint16 Tlf_Transfer(uint8 cmd, uint8 addr, uint8 data, Tlf_Frame *frame)
{
    uint16 tx = TLF_PACK(cmd, addr, data);
    uint32 to;

    s_buf.tx[0] = tx;
    s_buf.rx[0] = 0;

    to = 0;
    while (IfxQspi_SpiMaster_getStatus(&s_channel) == IfxQspi_Status_busy)
    {
        if (++to > 200000u)
        {
            g_tlf.timeoutCnt++;
            if (frame != NULL_PTR)
            {
                frame->tx = tx; frame->rx = 0; frame->status = 0; frame->data = 0; frame->ok = FALSE;
            }
            return 0;
        }
    }

    (void)IfxQspi_SpiMaster_exchange(&s_channel, &s_buf.tx[0], &s_buf.rx[0], TLF_SPI_BUFFER_SIZE);

    to = 0;
    while (IfxQspi_SpiMaster_getStatus(&s_channel) == IfxQspi_Status_busy)
    {
        if (++to > 200000u)
        {
            g_tlf.timeoutCnt++;
            if (frame != NULL_PTR)
            {
                frame->tx = tx; frame->rx = s_buf.rx[0]; frame->status = 0; frame->data = 0; frame->ok = FALSE;
            }
            return s_buf.rx[0];
        }
    }

    {
        uint16 rx = s_buf.rx[0];
        g_tlf.xferCnt++;
        if (frame != NULL_PTR)
        {
            frame->tx     = tx;
            frame->rx     = rx;
            frame->status = (uint8)((rx >> 8) & 0x3Fu);
            frame->data   = (uint8)(rx & 0xFFu);
            frame->ok     = TRUE;
        }
        return rx;
    }
}

uint8 Tlf_Read(uint8 addr)
{
    Tlf_Frame f;
    (void)Tlf_Transfer(0, addr & 0x3Fu, 0, &f);
    return f.data;
}

void Tlf_Write(uint8 addr, uint8 data)
{
    (void)Tlf_Transfer(1, addr & 0x3Fu, data, NULL_PTR);
}

void Tlf_Unlock(void)
{
    Tlf_Write(TLF_PROTCFG, TLF_UNLOCK_K1);
    Tlf_Write(TLF_PROTCFG, TLF_UNLOCK_K2);
    Tlf_Write(TLF_PROTCFG, TLF_UNLOCK_K3);
    Tlf_Write(TLF_PROTCFG, TLF_UNLOCK_K4);
}

void Tlf_Lock(void)
{
    uint32 start;
    Tlf_Write(TLF_PROTCFG, TLF_LOCK_K1);
    Tlf_Write(TLF_PROTCFG, TLF_LOCK_K2);
    Tlf_Write(TLF_PROTCFG, TLF_LOCK_K3);
    Tlf_Write(TLF_PROTCFG, TLF_LOCK_K4);
    /* Protected config is captured on LOCK; allow the TLF FSM to apply it
     * before any status readback (STM0 = 100MHz). */
    start = MODULE_STM0.TIM0.U;
    while ((MODULE_STM0.TIM0.U - start) < 200000u) { }
}

boolean Tlf_IsLocked(void)
{
    return (Tlf_Read(TLF_PROTSTAT) & 0x01u) ? TRUE : FALSE;
}

void Tlf_GotoState(uint8 state, uint8 trk2en, uint8 trk1en, uint8 comen, uint8 vrefen)
{
    uint8 dev = (uint8)(((trk2en & 0x1u) << 7) | ((trk1en & 0x1u) << 6) | ((comen & 0x1u) << 5) |
                         ((vrefen & 0x1u) << 3) | (state & 0x07u));
    Tlf_Write(TLF_DEVCTRL, dev);
    Tlf_Write(TLF_DEVCTRLN, (uint8)(~dev));
}

boolean Tlf_LinkOk(void)
{
    Tlf_Frame f;
    uint16 rx = Tlf_Transfer(0, TLF_DEVSTAT, 0, &f);
    return (boolean)(f.ok && (((rx >> 14) & 0x3u) == 0x1u));
}

/* ~1ms spin on STM0 (100MHz); STM0 is already running when Tlf_Init() returns. */
static void Tlf_SpinMs(uint32 ms)
{
    uint32 start = MODULE_STM0.TIM0.U;
    uint32 wait  = ms * 100000u;
    while ((MODULE_STM0.TIM0.U - start) < wait) { }
}

static boolean Tlf_GotoNormalOnce(void)
{
    uint8 dev = Tlf_Read(TLF_DEVSTAT);
    Tlf_GotoState(TLF_STATE_NORMAL,
                  (uint8)((dev >> 7) & 1u), (uint8)((dev >> 6) & 1u),
                  1, 1);  /* keep trackers, force COM+VREF on */
    Tlf_SpinMs(2);
    return (boolean)((Tlf_Read(TLF_DEVSTAT) & 0x07u) == TLF_STATE_NORMAL);
}

boolean Tlf_AutoInit(void)
{
    uint8 w, s1;
    int attempt;

    if (!Tlf_LinkOk())
    {
        return FALSE;  /* no TLF on the bus: stay passive, don't hang boot */
    }
    if ((Tlf_Read(TLF_DEVSTAT) & 0x07u) == TLF_STATE_NORMAL)
    {
        return TRUE;  /* already there (e.g. warm reboot with config retained) */
    }

    Tlf_Unlock();
    /* Quiet both watchdogs (either one blocks INIT->NORMAL when noisy). */
    w = Tlf_Read(TLF_RWDCFG0);
    Tlf_Write(TLF_WDCFG0, (uint8)(w & 0xF3u));  /* clear WWDEN + FWDEN */
    /* Disable ERR monitor (bench ERR is static; production uses SMU FSP). */
    s1 = Tlf_Read(TLF_RSYSPCFG1);
    Tlf_Write(TLF_SYSPCFG1, (uint8)(s1 & 0xF7u));  /* clear ERREN */
    Tlf_Lock();  /* applies config + 2ms settle */

    /* Rails: keep current enables, force COM+VREF on (prevents EVR UV alarm). */
    {
        uint8 cur = (uint8)(Tlf_Read(TLF_DEVSTAT) & 0x07u);
        if (cur == TLF_STATE_NONE)
        {
            cur = TLF_STATE_INIT;
        }
        {
            uint8 dev = Tlf_Read(TLF_DEVSTAT);
            Tlf_GotoState(cur, (uint8)((dev >> 7) & 1u), (uint8)((dev >> 6) & 1u), 1, 1);
        }
    }

    /* Clear all rw1c flags, then go NORMAL (one retry). */
    for (attempt = 0; attempt < 2; attempt++)
    {
        Tlf_Write(TLF_SYSFAIL, 0xFFu);
        Tlf_Write(TLF_INITERR, 0xFFu);
        Tlf_Write(TLF_IF, 0xFFu);
        Tlf_Write(TLF_SYSSF, 0xFFu);
        Tlf_Write(TLF_WKSF, 0xFFu);
        Tlf_Write(TLF_SPISF, 0xFFu);
        Tlf_Write(TLF_MONSF0, 0xFFu);
        Tlf_Write(TLF_MONSF1, 0xFFu);
        Tlf_Write(TLF_MONSF2, 0xFFu);
        Tlf_Write(TLF_MONSF3, 0xFFu);
        Tlf_Write(TLF_OTFAIL, 0xFFu);
        Tlf_Write(TLF_OTWRNSF, 0xFFu);
        if (Tlf_GotoNormalOnce())
        {
            return TRUE;
        }
    }
    return FALSE;
}

uint8 Tlf_WwdTrigger(void)
{
    uint8 cur  = Tlf_Read(TLF_WWDSCMD);
    uint8 trig = (uint8)(((cur >> 7) & 0x01u) ^ 0x01u);  /* write inverted TRIG_STATUS */
    Tlf_Write(TLF_WWDSCMD, trig);
    g_tlf.wwdFeeds++;
    return (uint8)((Tlf_Read(TLF_WWDSCMD) >> 7) & 0x01u);
}

/* FWD paced answer sequencer (see header). The FWD FSM consumes at most one
 * response byte per heartbeat period: inter-byte gap = heartbeat + 100ms
 * (empirical: 750ms gaps work @600ms heartbeat; 150ms gaps don't). */
static uint32 Tlf_FwdHeartbeatMs(void)
{
    uint8 hbt = (uint8)(Tlf_Read(TLF_RFWDCFG) & 0x1Fu);
    uint8 cyc = (uint8)(Tlf_Read(TLF_RWDCFG0) & 0x01u);
    uint32 cycMs_x10 = cyc ? 10u : 1u;  /* 1ms or 0.1ms per wd cycle */
    return (((uint32)hbt + 1u) * 50u * cycMs_x10 + 9u) / 10u;  /* ms, rounded */
}

static struct
{
    boolean active;
    boolean repeat;
    uint8   quest;
    sint32  idx;          /* next byte index: 3..1 -> FWDRSP, 0 -> FWDRSPSYNC, -1 done */
    uint32  nextTick;
    uint32  gapMs;
    uint32  periodMs;
    uint32  restartTick;
} s_fwdSeq = {FALSE, FALSE, 0, -1, 0, 700u, 5000u, 0};

boolean Tlf_FwdKick(boolean repeat, uint32 tickMs, uint32 periodMs)
{
    uint8 st0;
    if (s_fwdSeq.active)
    {
        return FALSE;
    }
    st0 = Tlf_Read(TLF_FWDSTAT0);
    if (((st0 >> 4) & 0x03u) != 0x03u)
    {
        return FALSE;  /* a sequence is already pending in silicon */
    }
    s_fwdSeq.quest  = (uint8)(st0 & 0x0Fu);
    s_fwdSeq.idx    = 3;
    s_fwdSeq.repeat = repeat;
    {
        uint32 gap = Tlf_FwdHeartbeatMs() + 100u;
        if (gap < 700u)
        {
            gap = 700u;
        }
        if (gap > 5000u)
        {
            gap = 5000u;
        }
        s_fwdSeq.gapMs = gap;
    }
    if (periodMs < (4u * s_fwdSeq.gapMs + 1000u))
    {
        periodMs = 4u * s_fwdSeq.gapMs + 1000u;  /* full answer + margin */
    }
    s_fwdSeq.periodMs = periodMs;
    s_fwdSeq.nextTick = tickMs;
    s_fwdSeq.active   = TRUE;
    return TRUE;
}

boolean Tlf_FwdBusy(void)
{
    return s_fwdSeq.active;
}

static void Tlf_FwdSeqStep(uint32 tickMs)
{
    if (s_fwdSeq.active)
    {
        if (((sint32)(tickMs - s_fwdSeq.nextTick)) >= 0)
        {
            if (s_fwdSeq.idx >= 1)
            {
                Tlf_Write(TLF_FWDRSP, s_fwdResp[s_fwdSeq.quest][s_fwdSeq.idx]);
            }
            else
            {
                Tlf_Write(TLF_FWDRSPSYNC, s_fwdResp[s_fwdSeq.quest][0]);
            }
            s_fwdSeq.idx--;
            s_fwdSeq.nextTick = tickMs + s_fwdSeq.gapMs;
            if (s_fwdSeq.idx < 0)
            {
                s_fwdSeq.active = FALSE;
                s_fwdSeq.restartTick = tickMs + s_fwdSeq.periodMs;
            }
        }
    }
    else if (s_fwdSeq.repeat)
    {
        if (((sint32)(tickMs - s_fwdSeq.restartTick)) >= 0)
        {
            (void)Tlf_FwdKick(TRUE, tickMs, s_fwdSeq.periodMs);
        }
    }
}

void Tlf_FwdRepeatStop(void)
{
    s_fwdSeq.repeat = FALSE;  /* current sequence (if any) runs to completion */
}

void Tlf_WdiHigh(void)   { IfxPort_setPinHigh(&MODULE_P14, 3); }
void Tlf_WdiLow(void)    { IfxPort_setPinLow(&MODULE_P14, 3); }
void Tlf_WdiToggle(void) { IfxPort_togglePin(&MODULE_P14, 3); }
uint8 Tlf_WdiLevel(void) { return IfxPort_getPinState(&MODULE_P14, 3) ? 1u : 0u; }

void Tlf_ErrHigh(void)   { IfxPort_setPinHigh(&MODULE_P33, 8); }
void Tlf_ErrLow(void)    { IfxPort_setPinLow(&MODULE_P33, 8); }
void Tlf_ErrToggle(void) { IfxPort_togglePin(&MODULE_P33, 8); }
uint8 Tlf_ErrLevel(void) { return IfxPort_getPinState(&MODULE_P33, 8) ? 1u : 0u; }

uint8 Tlf_Ss1Level(void) { return IfxPort_getPinState(&MODULE_P33, 9) ? 1u : 0u; }

void Tlf_Background(uint32 tickMs)
{
    if (g_tlf.wwdAuto && ((tickMs - g_tlf.lastWwdTick) >= g_tlf.wwdPeriodMs))
    {
        g_tlf.lastWwdTick = tickMs;
        (void)Tlf_WwdTrigger();
    }
    if (g_tlf.wdiAuto && ((tickMs - g_tlf.lastWdiTick) >= g_tlf.wdiPeriodMs))
    {
        g_tlf.lastWdiTick = tickMs;
        Tlf_WdiToggle();
        g_tlf.wdiToggles++;
    }
    if (g_tlf.errAuto && ((tickMs - g_tlf.lastErrTick) >= g_tlf.errPeriodMs))
    {
        g_tlf.lastErrTick = tickMs;
        Tlf_ErrToggle();
        g_tlf.errToggles++;
    }
    Tlf_FwdSeqStep(tickMs);  /* paced FWD answers (one-shot or repeat) */
}

const char *Tlf_StateName(uint8 state)
{
    switch (state & 0x07u)
    {
        case TLF_STATE_NONE:    return "NONE/POR";
        case TLF_STATE_INIT:    return "INIT";
        case TLF_STATE_NORMAL:  return "NORMAL";
        case TLF_STATE_SLEEP:   return "SLEEP";
        case TLF_STATE_STANDBY: return "STANDBY";
        case TLF_STATE_WAKE:    return "WAKE";
        default:                return "RESERVED";
    }
}
