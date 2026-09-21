/**********************************************************************************************************************
 * \file tlf35584.h
 * \brief TLF35584 multi-voltage safety SBC driver for TC397 (QSPI2 + GPIO).
 *
 *  Wiring (bench):
 *   - SDI  = QSPI2 MOSI P15.6 (MTSR alt3)   SDO = QSPI2 MISO P15.7 (MRST RxSel_b)
 *   - SCL  = QSPI2 CLK  P15.8 (SCLK alt3)   SCS = QSPI2 nCS  P14.2 (SLSO1 alt3)
 *   - WDI  = P14.3 GPIO output (watchdog trigger input of TLF, internal pull-down)
 *   - SS1  = P33.9 GPIO input  (safe-state output of TLF, low = safe state)
 *   - ERR  = P33.8 GPIO output (bench bit-bang; production: SMU FSP0 -> IfxSmu_FSP0_P33_8_OUT)
 *   - ROT  -> nPORST, INT -> nESR1 (dedicated pins, no SW action; observe via SPI flags)
 *   - MPS  = high (Test Mode 1: INIT timer stopped, WD/ERR contribution to ROT blocked)
 *
 *  SPI frame (16 clocks, HW parity even, dataWidth 15):
 *   MOSI: CMD(1) + ADDR(6) + DATA(8) + PARITY(1, HW)
 *   MISO write: looped-back MOSI.  MISO read: 1'b1 + STATUS[5:0](=0) + DATA[7:0] + PARITY(1, HW checked)
 *
 *  Reference: Infineon AURIX_code_examples SPI_TLF_1_KIT_TC397_TFT (QSPI2 5MHz, HW parity even,
 *  shiftTransmitDataOnTrailingEdge) + TLF35584 DataSheet Rev2.0 Ch.11/12/13/15.
 *********************************************************************************************************************/
#ifndef TLF35584_H
#define TLF35584_H

#include "Ifx_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- TLF35584 register map (DataSheet Table 22) ---------- */
#define TLF_DEVCFG0     0x00u
#define TLF_DEVCFG1     0x01u
#define TLF_DEVCFG2     0x02u
#define TLF_PROTCFG     0x03u
#define TLF_SYSPCFG0    0x04u
#define TLF_SYSPCFG1    0x05u
#define TLF_WDCFG0      0x06u
#define TLF_WDCFG1      0x07u
#define TLF_FWDCFG      0x08u
#define TLF_WWDCFG0     0x09u
#define TLF_WWDCFG1     0x0Au
#define TLF_RSYSPCFG0   0x0Bu
#define TLF_RSYSPCFG1   0x0Cu
#define TLF_RWDCFG0     0x0Du
#define TLF_RWDCFG1     0x0Eu
#define TLF_RFWDCFG     0x0Fu
#define TLF_RWWDCFG0    0x10u
#define TLF_RWWDCFG1    0x11u
#define TLF_WKTIMCFG0   0x12u
#define TLF_WKTIMCFG1   0x13u
#define TLF_WKTIMCFG2   0x14u
#define TLF_DEVCTRL     0x15u
#define TLF_DEVCTRLN    0x16u
#define TLF_WWDSCMD     0x17u
#define TLF_FWDRSP      0x18u
#define TLF_FWDRSPSYNC  0x19u
#define TLF_SYSFAIL     0x1Au
#define TLF_INITERR     0x1Bu
#define TLF_IF          0x1Cu
#define TLF_SYSSF       0x1Du
#define TLF_WKSF        0x1Eu
#define TLF_SPISF       0x1Fu
#define TLF_MONSF0      0x20u
#define TLF_MONSF1      0x21u
#define TLF_MONSF2      0x22u
#define TLF_MONSF3      0x23u
#define TLF_OTFAIL      0x24u
#define TLF_OTWRNSF     0x25u
#define TLF_VMONSTAT    0x26u
#define TLF_DEVSTAT     0x27u
#define TLF_PROTSTAT    0x28u
#define TLF_WWDSTAT     0x29u
#define TLF_FWDSTAT0    0x2Au
#define TLF_FWDSTAT1    0x2Bu
#define TLF_ABIST_CTRL0 0x2Cu
#define TLF_ABIST_CTRL1 0x2Du
#define TLF_ABIST_SEL0  0x2Eu
#define TLF_ABIST_SEL1  0x2Fu
#define TLF_ABIST_SEL2  0x30u
#define TLF_BCK_FREQ_CHANGE 0x31u
#define TLF_BCK_FRE_SPREAD  0x32u
#define TLF_BCK_MAIN_CTRL   0x33u
#define TLF_GTM         0x3Fu

/* Protection keys */
#define TLF_UNLOCK_K1 0xABu
#define TLF_UNLOCK_K2 0xEFu
#define TLF_UNLOCK_K3 0x56u
#define TLF_UNLOCK_K4 0x12u
#define TLF_LOCK_K1   0xDFu
#define TLF_LOCK_K2   0x34u
#define TLF_LOCK_K3   0xBEu
#define TLF_LOCK_K4   0xCAu

/* DEVCTRL STATEREQ codes (write) / DEVSTAT STATE codes (read) */
#define TLF_STATE_NONE    0u
#define TLF_STATE_INIT    1u
#define TLF_STATE_NORMAL  2u
#define TLF_STATE_SLEEP   3u
#define TLF_STATE_STANDBY 4u
#define TLF_STATE_WAKE    5u

/* Default QSPI baudrate (Hz). Bench default 2 MHz: robust, well below 10 MHz max
 * (1.5 MHz max in SLEEP). Change at runtime via Tlf_SetBaudrate(). */
#define TLF_QSPI_BAUD_DEFAULT 2000000u

/* Boot auto-init: run the INIT->NORMAL sequence (unlock, disable WWD+FWD+ERR,
 * enable rails, clear flags, goto NORMAL) automatically after Tlf_Init().
 * REQUIRED for MPS=0 normal mode: otherwise the INIT timer / WWD / ERR monitor
 * (all enabled by default) drive the TLF back to INIT and pulse ROT, so the
 * MCU resets in a loop. Set to 0 to keep Tlf_Init() fully passive (bench
 * TestMode debugging); the same sequence stays available as `tlf demo`. */
#ifndef TLF_AUTO_INIT
#define TLF_AUTO_INIT 1
#endif

/* Raw SPI transfer result */
typedef struct
{
    uint16 tx;      /* 15-bit frame driven on MOSI (cmd+addr+data, parity added by HW) */
    uint16 rx;      /* 15-bit frame sampled on MISO (MSB should be 1 on reads) */
    uint8  status;  /* MISO read status field S5..S0 (expected 0) */
    uint8  data;    /* MISO read data field = register content on reads */
    boolean ok;     /* FALSE on timeout */
} Tlf_Frame;

/* Driver state */
typedef struct
{
    boolean qspiOk;         /* QSPI2 master+channel init done */
    uint32  baud;           /* active baudrate */
    uint32  xferCnt;        /* total transfers */
    uint32  timeoutCnt;     /* busy timeouts */
    /* background auto service */
    boolean wwdAuto;        /* auto-feed window watchdog via SPI (WWDSCMD) */
    boolean wdiAuto;        /* auto-toggle WDI pin (for WDI trigger mode) */
    boolean errAuto;        /* auto-toggle ERR pin (for ERR monitor test) */
    boolean fwdAuto;        /* auto-answer functional watchdog (Table 26) */
    uint32  wwdPeriodMs;    /* WWD auto-feed period */
    uint32  wdiPeriodMs;    /* WDI toggle period (half period high/low) */
    uint32  errPeriodMs;    /* ERR toggle period */
    uint32  fwdPeriodMs;    /* FWD auto-answer period (must be < heartbeat) */
    uint32  lastWwdTick;
    uint32  lastWdiTick;
    uint32  lastErrTick;
    uint32  lastFwdTick;
    uint8   wwdFeeds;       /* feeds since last status query */
    uint8   wdiToggles;
    uint8   errToggles;
} Tlf_State;

extern Tlf_State g_tlf;

/* Init QSPI2 + GPIOs (WDI/ERR outputs, SS1 input). Safe: does NOT touch TLF config. */
void    Tlf_Init(void);
/* Re-init QSPI2 channel with a new baudrate (Hz). Returns TRUE on success. */
boolean Tlf_SetBaudrate(uint32 baud);
/* Raw 16-clock transfer. cmd: 0=read, 1=write. Returns rx word; fills *frame if non-NULL. */
uint16  Tlf_Transfer(uint8 cmd, uint8 addr, uint8 data, Tlf_Frame *frame);
/* Convenience: read / write register (write ignores rx data). */
uint8   Tlf_Read(uint8 addr);
void    Tlf_Write(uint8 addr, uint8 data);
/* Protected-register access */
void    Tlf_Unlock(void);
void    Tlf_Lock(void);
boolean Tlf_IsLocked(void);
/* State transition: writes DEVCTRL then bitwise-inverted DEVCTRLN (single API). */
void    Tlf_GotoState(uint8 state, uint8 trk2en, uint8 trk1en, uint8 comen, uint8 vrefen);
/* Window watchdog SPI trigger (reads TRIG_STATUS, writes inverted TRIG). Returns new TRIG_STATUS. */
uint8   Tlf_WwdTrigger(void);
/* Functional watchdog: start a paced answer sequence for the current question
 * (Table 26). Non-blocking: 4 bytes are emitted by Tlf_Background() at 700ms
 * intervals (empirical: the FWD FSM consumes ~1 byte per 600ms heartbeat;
 * back-to-back bytes are discarded). Returns TRUE if a sequence was started.
 * With repeat=TRUE the answer repeats every periodMs (min 4s, for FWD soak). */
boolean Tlf_FwdKick(boolean repeat, uint32 tickMs, uint32 periodMs);
boolean Tlf_FwdBusy(void);
void    Tlf_FwdRepeatStop(void);
/* SPI link check: TRUE if a DEVSTAT read returns with MISO MSB=1 and no timeout
 * (MISO has a pulldown, so an absent/floating TLF reads 0x0000). */
boolean Tlf_LinkOk(void);
/* Boot INIT->NORMAL sequence (quiet, no shell dependency):
 * unlock -> disable WWD+FWD -> disable ERR mon -> lock -> enable rails
 * (keep current enables, force COM+VREF on) -> clear all flags -> goto NORMAL.
 * Retries once. Returns TRUE if DEVSTAT reads NORMAL afterwards. */
boolean Tlf_AutoInit(void);
/* GPIO helpers */
void    Tlf_WdiHigh(void);
void    Tlf_WdiLow(void);
void    Tlf_WdiToggle(void);
uint8   Tlf_WdiLevel(void);
void    Tlf_ErrHigh(void);
void    Tlf_ErrLow(void);
void    Tlf_ErrToggle(void);
uint8   Tlf_ErrLevel(void);
uint8   Tlf_Ss1Level(void);   /* 0 = safe state active (low), 1 = normal (high) */
/* Call every main-loop iteration; handles wwdAuto/wdiAuto/errAuto using tickMs. */
void    Tlf_Background(uint32 tickMs);
/* Human-readable state name */
const char *Tlf_StateName(uint8 state);

#ifdef __cplusplus
}
#endif

#endif /* TLF35584_H */
