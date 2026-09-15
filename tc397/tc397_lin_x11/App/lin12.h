/* lin12.h — TC397 LIN x11 driver (ASCLIN LIN, polling, single-core).
 *
 * Board wiring (TC397XX LFBGA292, 3x TLIN1024A-Q1 quad transceiver):
 *   LIN0  TX P14.0  RX P14.1   (ASCLIN0, SLP_N P02.8) -- PLACEHOLDER only:
 *           shares pins with UART0 debug console, never initialized/tested.
 *   LIN1  TX P15.4  RX P15.5   (ASCLIN1, SLP_N P02.8 shared with LIN0-3)
 *   LIN2  TX P10.5  RX P02.10  (ASCLIN2, SLP_N P02.8)
 *   LIN3  TX P20.0  RX P20.3   (ASCLIN3, SLP_N P02.8)
 *   LIN4  TX P00.9  RX P00.12  (ASCLIN4, SLP_N P00.11 shared with LIN4-7)
 *   LIN5  TX P00.7  RX P00.6   (ASCLIN5, SLP_N P00.11)
 *   LIN6  TX P23.5  RX P23.1   (ASCLIN6, SLP_N P00.11)
 *   LIN7  TX P22.1  RX P22.4   (ASCLIN7, SLP_N P00.11)
 *   LIN8  TX P34.5  RX P33.6   (ASCLIN8, SLP_N P00.10 shared with LIN8-11)
 *   LIN9  TX P01.7  RX P20.6   (ASCLIN9, SLP_N P00.10)
 *   LIN10 TX P00.8  RX P00.4   (ASCLIN10, SLP_N P00.10)
 *   LIN11 TX P21.0  RX P21.1   (ASCLIN11, SLP_N P00.10) -- LIN commander (master)
 *
 * LIN1..LIN10 are responders (slaves), LIN11 is the commander (master).
 * LIN1..LIN11 bus wires are tied together for master/slave loopback tests.
 * All channels use IfxAsclin_Lin in polling mode (no interrupts), default
 * 19200 bps, enhanced checksum (classic for 0x3C/0x3D or on request).
 * Pin symbols verified against IfxAsclin_PinMap_TC39xB_LFBGA292 (.h/.c):
 * RxSel letters map to ALTI 0..7 (a=0, b=1, ...); alti is derived from the
 * pin struct's select field so no hardcoded table can drift.
 */
#ifndef LIN12_H
#define LIN12_H

#include "Ifx_Types.h"
#include <stdint.h>

#define LIN_NUM        12
#define LIN_MASTER_CH  11   /* default master of bus C (raw helpers use it) */
#define LIN_DATA_MAX   8u

typedef enum {
    LIN0 = 0, LIN1, LIN2, LIN3, LIN4, LIN5, LIN6, LIN7,
    LIN8, LIN9, LIN10, LIN11
} linChannel;

/* Per-channel state + statistics. */
typedef struct {
    uint8  used;        /* 0 = placeholder (LIN0), 1 = initialized */
    uint8  isMaster;    /* 1 = commander (LIN11), 0 = responder */
    uint32 txHdr;       /* headers transmitted (master) */
    uint32 txResp;      /* responses transmitted */
    uint32 rxHdr;       /* headers received (slaves + snoops) */
    uint32 rxResp;      /* responses received */
    uint32 hdrErr;      /* header phase errors (timeout/parity/frame) */
    uint32 respErr;     /* response phase errors (timeout/checksum/frame) */
    uint32 parityErr;   /* LIN parity errors */
    uint32 cksumErr;    /* LIN checksum errors */
    uint32 timeoutErr;  /* header/response timeouts */
    uint32 frameErr;    /* framing / collision errors */
    /* Last verified frame (for lindump). */
    uint8  lastId;      /* 6-bit LIN ID */
    uint8  lastLen;
    uint8  lastData[LIN_DATA_MAX];
    uint8  lastClassic; /* checksum mode used */
    uint8  lastDir;     /* 0 = master-tx/slave-rx, 1 = slave-tx/master-rx */
    uint32 lastTick;
    uint8  lastOk;
} linChState_t;

extern linChState_t g_lin[LIN_NUM];
extern float32 g_linBaud;   /* current baudrate (all channels) */
extern uint32 g_linHoldIOC, g_linHoldIN, g_linHoldOUT;

/* TLIN1024 EN (= board SLP_N, high = normal, low = sleep):
 * group0 = LIN0-3 (P02.8), group1 = LIN4-7 (P00.11), group2 = LIN8-11 (P00.10). */
void lin_xcvr_init(void);
void lin_xcvr_set_group(uint8 group, uint8 normal);
void lin_xcvr_set_all(uint8 normal);

/* Init LIN1..LIN11 (LIN0 skipped): master = LIN11, rest slaves. Re-entrant
 * (safe to call again for linbaud / after SW reset). */
void lin12_init_all(float32 baud);
void lin12_init_all_19200(void);
/* Re-init a single channel with the given role (LIN0 rejected). Counters kept. */
int lin_set_role(linChannel ch, uint8 isMaster);
/* Pairwise transactions between an explicit master and one slave
 * (independent of LIN_MASTER_CH default; roles must be configured first,
 * e.g. via lin_set_role). m2s: master header+response, slave verifies.
 * s2m: master header, slave responds, master verifies. Returns 0 on PASS. */
int lin_xact_m2s(linChannel master, linChannel slave, uint8 id6,
                 const uint8 *data, uint8 len, uint8 classic);
int lin_xact_s2m(linChannel master, linChannel responder, uint8 id6,
                 const uint8 *data, uint8 len, uint8 classic);

/* LIN protected identifier (PID) from 6-bit ID (parity per LIN spec). */
uint8 lin_pid(uint8 id6);
uint8 lin_id6(uint8 pid);   /* strip parity (no check) */
sint8 lin_pid_check(uint8 pid); /* 0 = parity ok, -1 = parity error */

/* Full transactions (synchronous, tick-timeout guarded):
 * master-tx: arm slaves -> master header -> slaves read header ->
 *            arm slaves for response -> master response -> slaves read+verify.
 * Returns number of slave failures (0 = all 10 slaves verified).
 * respOut: optional per-slave ok bitmap (bit i = LINi ok), may be NULL. */
int lin_xact_master_tx(uint8 id6, const uint8 *data, uint8 len, uint8 classic,
                       uint32 *respOut);
/* slave-tx: master header -> designated slave sends response -> master +
 * other slaves receive. Returns 0 on master-verified, -1 on failure.
 * snooped: optional count of other slaves that verified the frame. */
int lin_xact_slave_tx(uint8 id6, linChannel slave, const uint8 *data, uint8 len,
                      uint8 classic, uint32 *snooped);
/* Checksum-mismatch variant: master sends with masterClassic while slaves are
 * armed with slaveClassic. Returns slave failures; cksumErr counters show LC. */
int lin_xact_master_tx_mode(uint8 id6, const uint8 *data, uint8 len,
                            uint8 masterClassic, uint8 slaveClassic, uint32 *okMask);

/* Raw primitives (tick-timeout guarded, for linerr tests):
 * master sends header byte as-is (no parity fixup). */
int lin_raw_master_header(uint8 pid);
int lin_raw_master_response(const uint8 *data, uint8 len, uint8 classic);
/* Slave header arm/poll (for parity/timeout tests). Returns 0 + pid. */
void lin_slave_arm_header(linChannel ch);
int lin_slave_poll_header(linChannel ch, uint8 *pid);
/* Slave response arm/poll (header-independent, for bus tests). */
void lin_slave_arm_response(linChannel ch, uint8 len, uint8 classic);
int lin_slave_poll_response(linChannel ch, uint8 *data, uint8 len);
/* Master response arm/poll (for timeout test: expect -1 when nobody answers). */
void lin_master_arm_response(uint8 len, uint8 classic);
int lin_master_poll_response(uint8 *data, uint8 len);
/* Probe helper: toggle a LIN TX pin as GPIO n times, then full LIN re-init
 * (restores pin mux + ASCLIN config; counters are cleared). */
void lin_probe_tx_toggle(linChannel ch, uint8 n);
/* Bus-activity probe: master sends one header (raw PID) while sampling the
 * master TX pin and all slave RX pins as GPIO. sawMask bit i = LINi RX saw
 * dominant (low) at least once during the header. Returns 0 if THE set. */
int lin_bus_activity(uint8 pid, uint32 *sawMask, uint8 *txSawLow, uint8 *txSawHigh);
/* Bit-bang self-test: STM-timed LIN header (break + 0x55 + PID) driven as
 * GPIO onto one slave's RX net, then check the slave ASCLIN latched RHE with
 * matching PID. Proves slave pin/ALTI/baud/config without any transceiver.
 * Returns 0 + rxPid on success. LIN is fully re-initialized afterwards. */
int lin_bb_header(linChannel slave, uint8 pid, uint8 *rxPid);
/* Master analog loopback: ASCLIN11 transmits a header+response while its own
 * RX stays armed, so the frame must travel MCU TXD -> TLIN -> LIN bus ->
 * TLIN -> MCU RXD to be received. Returns 0 on header+response both looped
 * back with matching bytes. Debug outs: treSeen, FLAGS snapshot at exit. */
int lin_master_loopback(uint8 id6, const uint8 *data, uint8 len, uint8 classic,
                        uint8 *treSeen, uint32 *flagSnap);
/* RX level snapshot: bit i = LINi RX pin currently low (dominant). */
uint32 lin_rx_levels(void);
/* Dominant-hold probe: drive one channel's TXD net dominant (GPIO low) for
 * holdMs, sampling all RX nets early (2 ms, must follow if bus common) and
 * late (300 ms, TLIN DTO may have released the bus). Full LIN re-init after.
 * earlyMask/lateMask use lin_rx_levels() encoding. */
void lin_dominant_hold(linChannel ch, uint32 *earlyMask, uint32 *lateMask);
/* Fast dominant probe: drive TXD low, STM-sample (~5 ms window) the driven TX
 * pin itself (txSelfLow=1 if ever read back low) and all RX nets. */
void lin_dominant_fast(linChannel ch, uint8 *txSelfLow, uint32 *rxMask);
/* AC census: master sends one raw header while fast-sampling every RX net;
 * edgeCnt[i] = transitions seen on LINi RX during the header window. */
void lin_ac_census(linChannel master, uint8 pid, uint32 edgeCnt[LIN_NUM]);
/* Pulse capture: master sends raw header; record STM timestamps (100 MHz) of
 * up to 64 transitions on one RX net. Caller prints deltas. */
uint32 lin_pulse_capture(linChannel master, linChannel rxch, uint8 pid,
                         uint32 stamps[64]);
/* Raw header + RHE-only poll (no error-break): reports RHE status and FLAGS
 * snapshot for forensics. */
int lin_rawhdr_forensic(linChannel master, linChannel slave, uint8 pid,
                        uint8 *rxPid, uint32 *flagSnap);
/* TX trace: issue header TX, sample (FLAGS, TXFIFOCON) ~every 100us x 60. */
void lin_tx_trace(linChannel master, uint8 pid, uint32 flagsLog[60],
                  uint32 fifoLog[60]);

#endif /* LIN12_H */
