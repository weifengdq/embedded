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
#define LIN_MASTER_CH  11
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

/* TLIN1024 EN (= board SLP_N, high = normal, low = sleep):
 * group0 = LIN0-3 (P02.8), group1 = LIN4-7 (P00.11), group2 = LIN8-11 (P00.10). */
void lin_xcvr_init(void);
void lin_xcvr_set_group(uint8 group, uint8 normal);
void lin_xcvr_set_all(uint8 normal);

/* Init LIN1..LIN11 (LIN0 skipped): master = LIN11, rest slaves. Re-entrant
 * (safe to call again for linbaud / after SW reset). */
void lin12_init_all(float32 baud);
void lin12_init_all_19200(void);

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
/* Master response arm/poll (for timeout test: expect -1 when nobody answers). */
void lin_master_arm_response(uint8 len, uint8 classic);
int lin_master_poll_response(uint8 *data, uint8 len);

#endif /* LIN12_H */
