/* flexray_dual.h — TC397 dual ERAY (FR0A on ERAY0 + FR1A on ERAY1) self-test driver.
 *
 * Two ERAY controllers share one FlexRay bus (A channel only, 10 Mbit/s).
 * FR0 (ERAY0): TXDA P02.0 / TXENA P02.4 / RXDA P02.1, key slot 11 (coldstart+sync)
 * FR1 (ERAY1): TXDA P14.10 / TXENA P14.9 / RXDA P14.8, key slot 12 (coldstart+sync)
 * Both nodes use the identical 10 Mbit/s static-segment cluster timing so they
 * can cold-start together and exchange frames without external equipment.
 */
#ifndef FLEXRAY_DUAL_H
#define FLEXRAY_DUAL_H

#include "Ifx_Types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRD_NODE0           0   /* FR0A = ERAY0, P02.0/P02.4/P02.1 */
#define FRD_NODE1           1   /* FR1A = ERAY1, P14.10/P14.9/P14.8 */
#define FRD_NODE_COUNT      2

#define FRD_SLOT_NODE0      11u /* static key slot transmitted by FR0 */
#define FRD_SLOT_NODE1      12u /* static key slot transmitted by FR1 */
#define FRD_PAYLOAD_WORDS   8u  /* 8 words = 16 bytes static payload */
#define FRD_PAYLOAD_BYTES   16u
#define FRD_CYCLE_CODE      1u  /* cycle filter used by TX and RX buffers */

/* Last received frame snapshot (per node). */
typedef struct
{
    uint8_t  valid;
    uint16_t slot;
    uint8_t  cycle;
    uint8_t  payloadLen;            /* bytes */
    uint8_t  payload[FRD_PAYLOAD_BYTES];
    uint32_t mbs;                   /* message buffer status at reception */
} FrdRxFrame;

/* Per-node counters. */
typedef struct
{
    uint32_t txOk;
    uint32_t txBusy;                /* POC not ready when send requested */
    uint32_t rxOk;
    uint32_t rxErr;                 /* VFRA set but error flags (SEOA/CEOA/SVOA/TCIA/MLST) */
    uint32_t rxNull;                /* null frames (payload header NFI) skipped */
    uint32_t eirCount;              /* error-interrupt snapshots seen */
    uint32_t lastEir;
    uint32_t lastMbs;               /* MBS of the most recent RX buffer hit */
    uint32_t lastRejMbs;            /* MBS of the most recent rejected frame */
    uint16_t lastRejSlot;           /* slot id of the most recent rejected frame */
    uint16_t lastRejPayloadWords;   /* payload length field of rejected frame */
    uint32_t lastRejData0;          /* first payload word of rejected frame */
} FrdCounters;

/* Bring-up trace for the last frd_startup() (per node). */
typedef struct
{
    int8_t   started;               /* 1 once boot of this node began */
    int      rc;                    /* 0 = reached RUN stage (phase A) */
    uint8_t  poc;                   /* POC snapshot at end of phase A */
    uint32_t succ1;
    uint32_t ccsv;
    uint32_t eir;
} FrdBootInfo;

void frd_prepare(void);                                   /* initModule + build configs (call once) */
int  frd_startup(int verbose);                            /* full POC sequence for both nodes; 0 = both NORMAL */
const FrdBootInfo *frd_boot_info(int node);
int  frd_node_poc(int node);                              /* POC state number, or -1 on bad node */
uint32_t frd_node_ccsv(int node);                         /* CCSV register snapshot */
uint32_t frd_node_succ1(int node);                        /* SUCC1 register snapshot */
uint32_t frd_node_eir(int node);                          /* EIR register snapshot */
uint32_t frd_node_sir(int node);                          /* SIR register snapshot */
uint32_t frd_node_ccev(int node);                         /* CCEV register snapshot */
uint32_t frd_node_test1(int node);                        /* TEST1 register snapshot */
uint32_t frd_node_mhds(int node);                         /* MHDS register snapshot */
uint32_t frd_node_acs(int node);                          /* ACS register snapshot */
uint32_t frd_node_fsr(int node);                          /* FSR register snapshot */
uint32_t frd_node_ndat(int node);                         /* NDAT1 new-data flags */
uint32_t frd_node_txrq(int node);                         /* TXRQ1 transmission request flags */
/* TX buffer 0 view (RDHS/MBS/MHDS snapshot for diagnostics). */
typedef struct
{
    uint32_t rdhs1;
    uint32_t rdhs2;
    uint32_t rdhs3;
    uint32_t mbs;
    uint32_t mhds;
    uint32_t data0;
    uint32_t data1;
} FrdTxView;

int  frd_send(int node, const uint8_t *bytes16);          /* NULL = auto incrementing pattern */
int frd_tx_view(int node, uint8_t bufIdx, FrdTxView *out);
int frd_setslot_rc(int node, int bufIdx);
int  frd_poll(void);                                      /* poll RX on both nodes; returns new frames */
int  frd_last_rx(int node, FrdRxFrame *out);
const FrdCounters *frd_counters(int node);
const char *frd_poc_name(int poc);
const char *frd_node_name(int node);
int  frd_is_ready(void);                                  /* 1 when both nodes NORMAL_ACTIVE/PASSIVE */

#ifdef __cplusplus
}
#endif

#endif /* FLEXRAY_DUAL_H */
