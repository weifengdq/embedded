/* can12.h — TC397 12-channel MCMCAN driver (polling, single-core).
 *
 * Pin assignment (board wiring, TC397XX LFBGA292):
 *   CAN0  TX P34.1  RX P33.12  (TCAN1043-Q1, EN P33.1 / nSTB P33.0 / nFAULT P10.3)
 *   CAN1  TX P15.2  RX P33.10  (TCAN1044-Q1, STB P21.5 shared with CAN2/CAN3)
 *   CAN2  TX P32.5  RX P32.6   (TCAN1044-Q1, STB P21.5)
 *   CAN3  TX P32.3  RX P32.2   (TCAN1044-Q1, STB P21.5)
 *   CAN4  TX P00.0  RX P00.1   (TCAN1044-Q1, STB P21.2 shared with CAN5/6/7)
 *   CAN5  TX P23.6  RX P23.7   (TCAN1044-Q1, STB P21.2)
 *   CAN6  TX P23.2  RX P23.3   (TCAN1044-Q1, STB P21.2)
 *   CAN7  TX P33.4  RX P33.5   (TCAN1044-Q1, STB P21.2)
 *   CAN8  TX P10.6  RX P34.2   (TCAN1044-Q1, STB P21.4 shared with CAN9/10/11)
 *   CAN9  TX P00.2  RX P00.3   (TCAN1044-Q1, STB P21.4)
 *   CAN10 TX P22.8  RX P32.7   (TCAN1044-Q1, STB P21.4)
 *   CAN11 TX P22.10 RX P22.11  (TCAN1044-Q1, STB P21.4)
 *
 * HW mapping: CAN0-3 -> MODULE_CAN0 nodes 0-3, CAN4-7 -> MODULE_CAN1,
 * CAN8-11 -> MODULE_CAN2. Message RAM 0xF0200000 + group*0x10000.
 * Default bit timing: 1 Mbps arb (80%) + 5 Mbps data (80%), CAN FD BRS.
 */
#ifndef CAN12_H
#define CAN12_H

#include "Ifx_Types.h"
#include "IfxCan_Can.h"
#include "IfxCan.h"
#include <stdint.h>

#define CAN_NUM          12
#define CAN_RX_FIFO_SIZE 32u
#define CAN_TX_FIFO_SIZE 16u

/* RX ring per channel (decoded frames for shell candump) */
#define CAN_RX_RING_DEPTH 64u

typedef enum {
    CAN0 = 0, CAN1, CAN2, CAN3, CAN4, CAN5, CAN6, CAN7,
    CAN8, CAN9, CAN10, CAN11
} canChannel;

/* Decoded RX frame (self-contained, no iLLD dependency for shell) */
typedef struct {
    uint32_t id;        /* 11-bit SFF or 29-bit EFF */
    uint8_t  ext;       /* 1 = extended frame */
    uint8_t  rtr;       /* 1 = remote frame (classic only) */
    uint8_t  fd;        /* 1 = CAN FD frame */
    uint8_t  brs;       /* 1 = bit-rate switched */
    uint8_t  dlc;       /* raw DLC code 0..15 */
    uint8_t  len;       /* payload bytes 0..64 */
    uint8_t  data[64];
    uint32_t tick;      /* g_TickCount_1ms at reception */
} can12_frame_t;

typedef struct {
    canChannel channel;
    IfxCan_Can_Config config;
    IfxCan_Can module;
    IfxCan_Can_Node node;
    IfxCan_Can_NodeConfig nodeConfig;
    IfxCan_Filter stdFilter;
    IfxCan_Filter extFilter;
    IfxCan_Message txMsg;
    IfxCan_Message rxMsg;
    uint8 rxData[64];
    uint8 txData[64];
    boolean busOff;
    uint32 rxCount;
    uint32 txCount;
    uint32 overflowCount;
    uint32 boCount;
    uint32 restartCount;
    /* RX ring for shell */
    can12_frame_t rxRing[CAN_RX_RING_DEPTH];
    uint32 rxHead;      /* written by poll */
    uint32 rxTail;      /* consumed by shell */
    uint32 rxDrop;      /* ring-full drops */
} mcmcanType;

extern mcmcanType g_can[CAN_NUM];

/* DLC helpers */
static inline uint8 can_dlc2len(uint8 dlc)
{
    static const uint8 m[16] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    return m[dlc & 0xFu];
}
/* payload length -> DLC code (classic: len<=8 identity; FD: rounded up) */
uint8 can_len2dlc(uint8 len);

/* Transceiver control (call before CAN init):
 * CAN0: EN P33.1=H, nSTB P33.0=H (normal); groups STB P21.x=L (normal).
 * nFAULT P10.3 is input (0 = fault). */
void xcvr_init(void);
uint8 xcvr_fault(void);   /* 1 = nFAULT asserted (low) */

/* Init all 12 channels: 1M arb 80% + 5M data 80% (auto bit-timing). */
void can12_init_all_1M_5M(void);
/* Init with explicit rates (0 = keep default 1M/5M). */
void can12_init_all(uint32 arbBaud, uint32 dataBaud);
void can12_init_channel_simple(canChannel ch, uint32 baud, float samplePoint,
                               uint32 fastBaud, float fastSamplePoint);

/* Poll: drain RX FIFO0 of one/all channels into rxRing. Returns frames. */
uint32_t can12_poll_channel(canChannel ch);
uint32_t can12_poll_all(void);

/* Send one frame. ext: 0=11-bit,1=29-bit. fd: 0=classic,1=FD. brs: FD BRS. */
IfxCan_Status can12_send(canChannel ch, uint32_t id, uint8 ext, uint8 rtr,
                         uint8 fd, uint8 brs, uint8 dlc, const uint8 *data);

/* Bus-off recovery (INIT toggle). Returns 1 if node was in bus-off. */
uint32_t can12_restart_channel(canChannel ch, boolean force);

/* Stats */
uint32_t can12_get_rx_count(canChannel ch);
uint32_t can12_get_tx_count(canChannel ch);
uint32_t can12_get_overflow(canChannel ch);

/* RX ring access (shell): peek oldest without consuming / consume one. */
uint32 can12_ring_pending(canChannel ch);
boolean can12_ring_pop(canChannel ch, can12_frame_t *out);

#endif /* CAN12_H */
