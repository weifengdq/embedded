/* flexray_dual.c — TC397 dual-ERAY FlexRay self-test driver (FR0A + FR1A, channel A).
 *
 * Approach: both ERAY0 and ERAY1 are configured as coldstart/sync nodes with
 * the same 10 Mbit/s cluster timing (static segment: 91 slots x 24 MT,
 * 16-byte static payload, 5 ms cycle of 3636 MT). FR0 owns static slot 11,
 * FR1 owns static slot 12, so no two transmitters collide. Each controller
 * carries 3 message buffers: buf0 = TX key slot, buf1/buf2 = RX slots 11/12.
 *
 * The POC bring-up is blocking with millisecond timeouts (driven by
 * g_TickCount_1ms from the STM tick) so shell commands get a deterministic
 * PASS/FAIL instead of a background state machine.
 */
#include "flexray_dual.h"
#include "Eray/Eray/IfxEray_Eray.h"
#include "Eray/Std/IfxEray.h"
#include "IfxPort.h"
#include <string.h>

extern volatile uint32 g_TickCount_1ms;

/* Per-node runtime: handles, configs, message-RAM images, RX snapshot. */
typedef struct
{
    IfxEray_Eray        ctrl;
    IfxEray_Eray_Config modCfg;
    IfxEray_Eray_NodeConfig nodeCfg;
    Ifx_ERAY           *sfr;
    IfxEray_Header      txHdr;
    IfxEray_SlotConfig  txSlot;
    uint32              txData[32];
    IfxEray_Header      rxHdr[2];
    IfxEray_SlotConfig  rxSlot[2];
    uint16              keySlot;
    uint32              txSeq;
    FrdRxFrame          lastRx;
    FrdCounters         cnt;
    uint8_t             prepared;
    /* Per-RX-buffer last-seen signature for change detection (no NDAT). */
    uint8_t             seenValid[2];
    uint16_t            seenSlot[2];
    uint8_t             seenCycle[2];
    uint8_t             seenLen[2];
    uint32              seenW0[2];
    uint32              seenMbs[2];
} FrdNode;

static FrdNode s_nodes[FRD_NODE_COUNT];

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

const char *frd_node_name(int node)
{
    return (node == FRD_NODE0) ? "FR0A(ERAY0)" : (node == FRD_NODE1) ? "FR1A(ERAY1)" : "?";
}

const char *frd_poc_name(int poc)
{
    switch (poc) {
    case 0:  return "DEFAULT_CONFIG";
    case 1:  return "READY";
    case 2:  return "NORMAL_ACTIVE";
    case 3:  return "NORMAL_PASSIVE";
    case 4:  return "HALT";
    case 5:  return "MONITOR";
    case 13: return "LOOPBACK";
    case 14: return "ASYNC";
    case 15: return "CONFIG";
    case 16: return "WAKEUP_STANDBY";
    case 17: return "WAKEUP_LISTEN";
    case 18: return "WAKEUP_SEND";
    case 19: return "WAKEUP_DETECT";
    case 32: return "STARTUP";
    case 33: return "COLDSTART_LISTEN";
    case 34: return "COLLISION_RES";
    case 35: return "CONSISTENCY_CHK";
    case 36: return "GAP";
    case 37: return "JOIN";
    case 38: return "INTEGRATION_CHK";
    case 39: return "INTEGRATION_LISTEN";
    case 40: return "INTEGRATION_CONSISTENCY";
    case 41: return "INIT_SCHEDULE";
    case 42: return "STARTUP_ABORTED";
    case 43: return "STARTUP_SUCCEED";
    default: return "?";
    }
}

static uint8_t frd_poc(FrdNode *n)
{
    return (uint8_t)IfxEray_Eray_getPocState(&n->ctrl);
}

/* SUCC1.CMD is protected: full-word write fenced by the LCK sequence.
 * Plain bit writes are silently ignored in CONFIG/READY, so every POC
 * command in this driver goes through these helpers. frd_try() reports
 * acceptance the same way the iLLD does (CMD reads back 0 = accepted).
 * All spins are bounded — a stuck PBSY must never hang the shell. */
static int frd_wait_pbsy_clear(FrdNode *n, uint32_t timeoutMs)
{
    uint32_t start = g_TickCount_1ms;
    while (n->sfr->SUCC1.B.PBSY != 0U) {
        if ((g_TickCount_1ms - start) >= timeoutMs) {
            return -1;
        }
    }
    return 0;
}

static int frd_issue(FrdNode *n, IfxEray_PocCommand cmd)
{
    Ifx_ERAY *sfr = n->sfr;
    Ifx_ERAY_SUCC1 succ1;
    if (frd_wait_pbsy_clear(n, 100U) != 0) {
        return -1;
    }
    succ1.U = sfr->SUCC1.U;
    succ1.B.CMD = cmd;
    sfr->LCK.B.CLK = 0xCEU;
    sfr->LCK.B.CLK = 0x31U;
    sfr->SUCC1.U = succ1.U;
    return 0;
}

static int frd_try(FrdNode *n, IfxEray_PocCommand cmd)
{
    if (frd_issue(n, cmd) != 0) {
        return -1;
    }
    /* SUCC1.CMD is sticky: it keeps showing the last issued command.
     * Accepted = the written value stuck (a blocked write reads back 0).
     * Wait for the POC unit to finish processing first. */
    if (frd_wait_pbsy_clear(n, 100U) != 0) {
        return -1;
    }
    return (n->sfr->SUCC1.B.CMD == (uint32_t)cmd) ? 0 : -1;
}

/* Bounded input-buffer spins (the iLLD originals spin forever). */
static int frd_wait_ibsyh_clear(FrdNode *n, uint32_t timeoutMs)
{
    uint32_t start = g_TickCount_1ms;
    while (IfxEray_getInputBufferBusyHostStatus(n->sfr) == TRUE) {
        if ((g_TickCount_1ms - start) >= timeoutMs) {
            return -1;
        }
    }
    return 0;
}

static int frd_wait_ibsys_clear(FrdNode *n, uint32_t timeoutMs)
{
    uint32_t start = g_TickCount_1ms;
    while (IfxEray_getInputBufferBusyShadowStatus(n->sfr) == TRUE) {
        if ((g_TickCount_1ms - start) >= timeoutMs) {
            return -1;
        }
    }
    return 0;
}

/* setSlot with bounded waits (same register sequence as the iLLD). */
static int frd_setslot(FrdNode *n, const IfxEray_Header *header, const uint32 *data,
                       const IfxEray_SlotConfig *slotConfig)
{
    Ifx_ERAY *sfr = n->sfr;
    if (frd_wait_ibsyh_clear(n, 100U) != 0) {
        return -1;
    }
    if (header != NULL_PTR) {
        Ifx_ERAY_WRHS1 wrhs1;
        Ifx_ERAY_WRHS2 wrhs2;
        wrhs1.U = 0;
        wrhs1.B.FID = header->frameId;
        wrhs1.B.CYC = header->cycleCode;
        wrhs1.B.CHA = header->channelAFiltered;
        wrhs1.B.CHB = header->channelBFiltered;
        wrhs1.B.CFG = header->bufferDirection;
        wrhs1.B.PPIT = header->transmitPayloadIndicatior;
        wrhs1.B.TXM = header->transmissionMode;
        wrhs1.B.MBI = header->bufferServiceEnabled;
        sfr->WRHS1.U = wrhs1.U;

        wrhs2.U = 0;
        if (header->bufferDirection == IfxEray_BufferDirection_transmit) {
            wrhs2.B.CRC = IfxEray_calcHeaderCrc(header->payloadLength, header->frameId,
                                                header->startupFrameIndicator,
                                                header->syncFrameIndicator);
        }
        wrhs2.B.PLC = header->payloadLength;
        sfr->WRHS2.U = wrhs2.U;
        sfr->WRHS3.U = header->dataPointer;
    }
    IfxEray_writeData(sfr, data, header->payloadLength);
    sfr->IBCM.B.LHSH = slotConfig->headerTransfered;
    sfr->IBCM.B.LDSH = slotConfig->dataTransfered;
    sfr->IBCM.B.STXRH = slotConfig->transferRequested;
    sfr->IBCR.B.IBRH = slotConfig->bufferIndex;
    if (frd_wait_ibsys_clear(n, 100U) != 0) {
        return -2;
    }
    if (frd_wait_ibsyh_clear(n, 100U) != 0) {
        return -3;
    }
    return 0;
}

static int frd_wait_poc(FrdNode *n, uint8_t want, uint32_t timeoutMs)
{
    uint32_t start = g_TickCount_1ms;
    while ((g_TickCount_1ms - start) < timeoutMs) {
        if (frd_poc(n) == want) {
            return 0;
        }
    }
    return -1;
}

/* Wait until POC leaves the listed states is overkill here; simple ANY-wait. */
static int frd_wait_poc_any(FrdNode *n, const uint8_t *wants, int nWant, uint32_t timeoutMs)
{
    uint32_t start = g_TickCount_1ms;
    while ((g_TickCount_1ms - start) < timeoutMs) {
        uint8_t p = frd_poc(n);
        for (int i = 0; i < nWant; i++) {
            if (p == wants[i]) {
                return 0;
            }
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Cluster timing: proven 10 Mbit/s static profile (16 B payload).    */
/* ------------------------------------------------------------------ */

static void frd_fill_cluster(FrdNode *n)
{
    IfxEray_Eray_ControllerConfig *c = &n->nodeCfg.controllerConfig;

    c->networkVectorLength = 0U;
    c->latestTransmissionStart = 249U;

    c->prtc1Control.transmissionStartTime = 9U;
    c->prtc1Control.collisionAvoidanceDuration = 87U;
    c->prtc1Control.strobePosition = IfxEray_StrobePosition_5;
    c->prtc1Control.baudrate = IfxEray_Baudrate_10;
    c->prtc1Control.receiveWakeupTestDuration = 301U;
    c->prtc1Control.transmitWakeupRepetitions = 2U;

    c->prtc2Control.receiveWakeupIdleTime = 59U;
    c->prtc2Control.receiveWakeupLowTime = 50U;
    c->prtc2Control.transmitWakeupIdleTime = 180U;
    c->prtc2Control.transmitWakeupLowTime = 60U;

    /* Both nodes are coldstart+sync capable so either order of frd_startup
     * converges: the first node to finish wakeup cold-starts the cluster,
     * the other integrates. */
    c->succ1Config.startupFrameTransmitted = TRUE;
    c->succ1Config.syncFrameTransmitted = TRUE;
    c->succ1Config.maxColdStartAttempts = 8U;
    c->succ1Config.numberOfCyclePairsForActive = 0U;
    c->succ1Config.wakeupPatternChannel = IfxEray_WakeupChannel_a;
    c->succ1Config.transmissionSlotMode = IfxEray_TransmissionSlotMode_all;
    c->succ1Config.clockSyncErrorHalt = FALSE;
    c->succ1Config.channelASymbolTransmitted = FALSE;
    c->succ1Config.channelBSymbolTransmitted = FALSE;
    c->succ1Config.channelAConnectedNode = TRUE;
    c->succ1Config.channelBConnectedNode = FALSE;   /* B not populated */

    c->succ2Config.listenTimeOut = 401202U;
    c->succ2Config.listenTimeOutNoise = IfxEray_ListenTimeOutNoise_2;

    c->succ3Config.clockCorrectionCyclesPassive = 2U;
    c->succ3Config.clockCorrectionCyclesHalt = 2U;

    c->gtuConfig.gtu01Config.microticksPerCycle = 200000U;
    c->gtuConfig.gtu02Config.macroticksPerCycle = 3636U;
    c->gtuConfig.gtu02Config.maxSyncFrames = 15U;
    c->gtuConfig.gtu03Config.channelAMicrotickInitialOffset = 6U;
    c->gtuConfig.gtu03Config.channelBMicrotickInitialOffset = 6U;
    c->gtuConfig.gtu03Config.channelAMacrotickInitialOffset = 3U;
    c->gtuConfig.gtu03Config.channelBMacrotickInitialOffset = 3U;
    c->gtuConfig.gtu04Config.networkStartIdleTime = 3629U;
    c->gtuConfig.gtu04Config.correctionOffset = 3632U;
    c->gtuConfig.gtu05Config.channelAReceptionDelay = 4U;
    c->gtuConfig.gtu05Config.channelBReceptionDelay = 4U;
    c->gtuConfig.gtu05Config.clusterDrift = 2U;
    c->gtuConfig.gtu05Config.decodingCorrection = 52U;
    c->gtuConfig.gtu06Config.acceptedStartupDeviation = 220U;
    c->gtuConfig.gtu06Config.maxDriftOffset = 601U;
    c->gtuConfig.gtu07Config.staticSlotLength = 24U;
    c->gtuConfig.gtu07Config.staticSlotsCount = 91U;
    c->gtuConfig.gtu08Config.dynamicSlotLength = 5U;
    c->gtuConfig.gtu08Config.dynamicSlotCount = 289U;
    c->gtuConfig.gtu09Config.idleDynamicSlots = IfxEray_IdleDynamicSlots_1;
    c->gtuConfig.gtu09Config.staticActionPoint = 2U;
    c->gtuConfig.gtu09Config.dynamicActionPoint = 2U;
    c->gtuConfig.gtu10Config.maxOffsetCorrection = 127U;
    c->gtuConfig.gtu10Config.maxRateCorrection = 601U;
    c->gtuConfig.gtu11Config.externalOffsetCorrection = IfxEray_ExternalOffsetCorrection_0;
    c->gtuConfig.gtu11Config.externalRateCorrection = IfxEray_ExternalRateCorrection_0;
    c->gtuConfig.gtu11Config.externalOffset = IfxEray_ExternalOffset_noCorrection;
    c->gtuConfig.gtu11Config.externalRate = IfxEray_ExternalRate_noCorrection;

    c->staticFramepayload = FRD_PAYLOAD_WORDS;
}

static void frd_fill_message_ram(FrdNode *n)
{
    IfxEray_Eray_MessageRAMConfig *m = &n->nodeCfg.messageRAMConfig;
    const uint16_t rxSlots[2] = { FRD_SLOT_NODE0, FRD_SLOT_NODE1 };

    m->firstDynamicBuffer = 3U;         /* buffers 0..2 are static */
    m->numberOfMessageBuffers = 3U;
    m->fifoBufferStartIndex = 3U;
    m->fifoDepth = 0U;
    m->fifoConfigured = FALSE;
    m->bufferReconfigEnabled = TRUE;
    m->receiveChannel = IfxEray_ReceiveChannel_a;  /* unused without FIFO */
    m->rejectedFrameId = 0U;
    m->filteredCycleNumber = 0U;
    m->staticFifoDisabled = TRUE;
    m->fifoNullFramesRejected = FALSE;
    m->frameIdFilter = 0x7FFU;

    /* buf0: TX key slot, continuous, channel A, startup+sync frame. */
    memset(&n->txHdr, 0, sizeof(n->txHdr));
    n->txHdr.frameId = n->keySlot;
    n->txHdr.cycleCode = FRD_CYCLE_CODE;
    n->txHdr.channelAFiltered = TRUE;
    n->txHdr.channelBFiltered = FALSE;
    n->txHdr.bufferDirection = IfxEray_BufferDirection_transmit;
    n->txHdr.transmitPayloadIndicatior = FALSE;
    n->txHdr.transmissionMode = IfxEray_TransmissionMode_continuous;
    n->txHdr.bufferServiceEnabled = TRUE;
    n->txHdr.payloadLength = FRD_PAYLOAD_WORDS;
    n->txHdr.dataPointer = 0x30U;
    n->txHdr.startupFrameIndicator = TRUE;
    n->txHdr.syncFrameIndicator = TRUE;
    memset(&n->txSlot, 0, sizeof(n->txSlot));
    n->txSlot.headerTransfered = TRUE;
    n->txSlot.dataTransfered = TRUE;
    n->txSlot.transferRequested = FALSE;
    n->txSlot.bufferIndex = 0U;
    memset(n->txData, 0, sizeof(n->txData));

    m->header[0] = &n->txHdr;
    m->slotControl[0] = &n->txSlot;
    m->data[0] = n->txData;

    /* buf1/buf2: RX slots 11/12, channel A. */
    for (int i = 0; i < 2; i++) {
        memset(&n->rxHdr[i], 0, sizeof(n->rxHdr[i]));
        n->rxHdr[i].frameId = rxSlots[i];
        n->rxHdr[i].cycleCode = FRD_CYCLE_CODE;
        n->rxHdr[i].channelAFiltered = TRUE;
        n->rxHdr[i].channelBFiltered = FALSE;
        n->rxHdr[i].bufferDirection = IfxEray_BufferDirection_receive;
        n->rxHdr[i].transmitPayloadIndicatior = FALSE;
        n->rxHdr[i].transmissionMode = IfxEray_TransmissionMode_continuous;
        n->rxHdr[i].bufferServiceEnabled = TRUE;
        n->rxHdr[i].payloadLength = FRD_PAYLOAD_WORDS;
        n->rxHdr[i].dataPointer = (uint16)(0x40U + (uint16)i * 0x10U);
        n->rxHdr[i].startupFrameIndicator = FALSE;
        n->rxHdr[i].syncFrameIndicator = FALSE;

        memset(&n->rxSlot[i], 0, sizeof(n->rxSlot[i]));
        n->rxSlot[i].headerTransfered = TRUE;
        n->rxSlot[i].dataTransfered = FALSE;
        n->rxSlot[i].transferRequested = FALSE;
        n->rxSlot[i].bufferIndex = (uint8)(1U + (uint8)i);

        m->header[1 + i] = &n->rxHdr[i];
        m->slotControl[1 + i] = &n->rxSlot[i];
        m->data[1 + i] = NULL_PTR;
    }
}

/* Program all controller registers + message buffers. Must run in CONFIG. */
static int8_t s_setslot_rc[FRD_NODE_COUNT][4];

static int frd_apply(FrdNode *n)
{
    Ifx_ERAY *sfr = n->sfr;
    const IfxEray_Eray_ControllerConfig *c = &n->nodeCfg.controllerConfig;
    const IfxEray_Eray_MessageRAMConfig *m = &n->nodeCfg.messageRAMConfig;

    IfxEray_enableInterruptLines(sfr);
    IfxEray_setAutoDelayBuffers(sfr);

    IfxEray_setFirstDynamicBuffer(sfr, m->firstDynamicBuffer);
    IfxEray_setMessageBufferCount(sfr, m->numberOfMessageBuffers);
    IfxEray_setFifoBufferStartIndex(sfr, (uint8)m->fifoBufferStartIndex);
    IfxEray_setBufferReconfigSecure(sfr, (m->bufferReconfigEnabled == TRUE) ? 0U : 2U);
    for (uint32_t i = 0; i < m->numberOfMessageBuffers; i++) {
        if (m->header[i] == NULL_PTR) {
            break;
        }
        {
            int rc = frd_setslot(n, m->header[i], m->data[i], m->slotControl[i]);
            int ni = (int)(n - s_nodes);
            if ((ni >= 0) && (ni < FRD_NODE_COUNT) && (i < 4U)) {
                s_setslot_rc[ni][i] = (int8_t)rc;
            }
            if (rc != 0) {
                return -10;
            }
        }
    }

    IfxEray_setTransmittedFrames(sfr, c->succ1Config.startupFrameTransmitted,
                                 c->succ1Config.syncFrameTransmitted);
    IfxEray_setMaxColdStartAttempts(sfr, c->succ1Config.maxColdStartAttempts);
    IfxEray_setActiveCyclePairs(sfr, c->succ1Config.numberOfCyclePairsForActive);
    IfxEray_setWakeupPatternChannel(sfr, c->succ1Config.wakeupPatternChannel);
    IfxEray_setTransmissionSlotMode(sfr, c->succ1Config.transmissionSlotMode);
    IfxEray_setClockSynchErrorHalt(sfr, c->succ1Config.clockSyncErrorHalt);
    IfxEray_setSymbolChannels(sfr, c->succ1Config.channelASymbolTransmitted,
                              c->succ1Config.channelBSymbolTransmitted);
    IfxEray_setNodeChannels(sfr, TRUE, FALSE);
    IfxEray_setListenTimeOuts(sfr, c->succ2Config.listenTimeOut, c->succ2Config.listenTimeOutNoise);
    IfxEray_setClockCorrectionCycles(sfr, c->succ3Config.clockCorrectionCyclesPassive,
                                     c->succ3Config.clockCorrectionCyclesHalt);
    IfxEray_setNetworkVectorLength(sfr, c->networkVectorLength);
    IfxEray_setTransmissionStartTime(sfr, (uint8)c->prtc1Control.transmissionStartTime);
    IfxEray_setCollisionAvoidanceDuration(sfr, c->prtc1Control.collisionAvoidanceDuration);
    IfxEray_setStrobePosition(sfr, c->prtc1Control.strobePosition);
    IfxEray_setBaudrate(sfr, c->prtc1Control.baudrate);
    IfxEray_setReceiveWakeupTimes(sfr, c->prtc1Control.receiveWakeupTestDuration,
                                  c->prtc2Control.receiveWakeupIdleTime,
                                  c->prtc2Control.receiveWakeupLowTime);
    IfxEray_setTransmitWakeupTimes(sfr, c->prtc1Control.transmitWakeupRepetitions,
                                   c->prtc2Control.transmitWakeupIdleTime,
                                   c->prtc2Control.transmitWakeupLowTime);
    IfxEray_setMessageHandlerConfigurations(sfr, c->staticFramepayload, c->latestTransmissionStart);
    IfxEray_setCycleDurationMicroticks(sfr, c->gtuConfig.gtu01Config.microticksPerCycle);
    IfxEray_setCycleDurationMacroticks(sfr, c->gtuConfig.gtu02Config.macroticksPerCycle);
    IfxEray_setMaxSynchFrames(sfr, (IfxEray_MaxSynchFrames)c->gtuConfig.gtu02Config.maxSyncFrames);
    IfxEray_setChannelAInitialOffsets(sfr, c->gtuConfig.gtu03Config.channelAMicrotickInitialOffset,
                                      c->gtuConfig.gtu03Config.channelAMacrotickInitialOffset);
    IfxEray_setChannelBInitialOffsets(sfr, c->gtuConfig.gtu03Config.channelBMicrotickInitialOffset,
                                      c->gtuConfig.gtu03Config.channelBMacrotickInitialOffset);
    IfxEray_setNetworkStartIdleTime(sfr, c->gtuConfig.gtu04Config.networkStartIdleTime);
    IfxEray_setOffsetCorrection(sfr, c->gtuConfig.gtu04Config.correctionOffset);
    IfxEray_setChannelsReceiveDelay(sfr, c->gtuConfig.gtu05Config.channelAReceptionDelay,
                                    c->gtuConfig.gtu05Config.channelBReceptionDelay);
    IfxEray_setDecodingCorrectionValue(sfr, c->gtuConfig.gtu05Config.decodingCorrection);
    IfxEray_setClusterDriftValues(sfr, c->gtuConfig.gtu05Config.clusterDrift,
                                  c->gtuConfig.gtu06Config.maxDriftOffset);
    IfxEray_setClusterStartupDeviation(sfr, c->gtuConfig.gtu06Config.acceptedStartupDeviation);
    IfxEray_setStaticSlots(sfr, c->gtuConfig.gtu07Config.staticSlotLength,
                           c->gtuConfig.gtu07Config.staticSlotsCount);
    IfxEray_setDynamicSlots(sfr, c->gtuConfig.gtu08Config.dynamicSlotLength,
                            c->gtuConfig.gtu08Config.dynamicSlotCount,
                            c->gtuConfig.gtu09Config.idleDynamicSlots);
    IfxEray_setSlotActionPoints(sfr, c->gtuConfig.gtu09Config.staticActionPoint,
                                c->gtuConfig.gtu09Config.dynamicActionPoint);
    IfxEray_setMaxCorrectionValues(sfr, c->gtuConfig.gtu10Config.maxOffsetCorrection,
                                   c->gtuConfig.gtu10Config.maxRateCorrection);
    IfxEray_setExternalCorrectionControl(sfr, c->gtuConfig.gtu11Config.externalOffset,
                                         c->gtuConfig.gtu11Config.externalRate);
    IfxEray_setExternalCorrectionValues(sfr, c->gtuConfig.gtu11Config.externalOffsetCorrection,
                                        c->gtuConfig.gtu11Config.externalRateCorrection);

    /* Transceiver pins: FR0A on P02.x (ERAY0), FR1A on P14.x (ERAY1). */
    if (n == &s_nodes[FRD_NODE0]) {
        IfxEray_initRxPinWithPadLevel(&IfxEray0_RXDA2_P02_1_IN, IfxPort_InputMode_noPullDevice,
                                      IfxPort_PadDriver_cmosAutomotiveSpeed3);
        IfxEray_initTxPin(&IfxEray0_TXDA_P02_0_OUT, IfxPort_OutputMode_pushPull,
                          IfxPort_PadDriver_cmosAutomotiveSpeed3);
        IfxEray_initTxEnPin(&IfxEray0_TXENA_P02_4_OUT, IfxPort_OutputMode_pushPull,
                            IfxPort_PadDriver_cmosAutomotiveSpeed3);
    } else {
        IfxEray_initRxPinWithPadLevel(&IfxEray1_RXDA0_P14_8_IN, IfxPort_InputMode_noPullDevice,
                                      IfxPort_PadDriver_cmosAutomotiveSpeed3);
        IfxEray_initTxPin(&IfxEray1_TXDA_P14_10_OUT, IfxPort_OutputMode_pushPull,
                          IfxPort_PadDriver_cmosAutomotiveSpeed3);
        IfxEray_initTxEnPin(&IfxEray1_TXENA_P14_9_OUT, IfxPort_OutputMode_pushPull,
                            IfxPort_PadDriver_cmosAutomotiveSpeed3);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void frd_prepare(void)
{
    for (int i = 0; i < FRD_NODE_COUNT; i++) {
        FrdNode *n = &s_nodes[i];
        if (n->prepared) {
            continue;
        }
        memset(n, 0, sizeof(*n));
        n->keySlot = (i == FRD_NODE0) ? FRD_SLOT_NODE0 : FRD_SLOT_NODE1;
        if (i == FRD_NODE0) {
            IfxEray_Eray_initModuleConfig(&n->modCfg, &MODULE_ERAY0);
        } else {
            IfxEray_Eray_initModuleConfig(&n->modCfg, &MODULE_ERAY1);
        }
        IfxEray_Eray_initModule(&n->ctrl, &n->modCfg);
        n->sfr = n->ctrl.eray;
        IfxEray_Eray_Node_initConfig(&n->nodeCfg);
        frd_fill_cluster(n);
        frd_fill_message_ram(n);
        n->prepared = 1;
    }
}

static FrdBootInfo s_boot[FRD_NODE_COUNT];

/* Phase A: one node from any state to RUN (STARTUP/integration).
 * Does NOT wait for NORMAL — the partner node must be started too before
 * either can synchronize (FlexRay needs >= 2 coldstart nodes). */
static int frd_boot_phase_a(FrdNode *n, int idx)
{
    FrdBootInfo *bi = &s_boot[idx];
    uint8_t p = frd_poc(n);

    memset(bi, 0, sizeof(*bi));
    bi->started = 1;

    /* 1) HALT */
    if (p != (uint8_t)IfxEray_PocState_halt) {
        IfxEray_clearAllFlags(n->sfr);
        if ((frd_issue(n, IfxEray_PocCommand_freeze) != 0) ||
            (frd_wait_poc(n, (uint8_t)IfxEray_PocState_halt, 300U) != 0)) {
            bi->rc = -1;
            goto snap;
        }
    }
    /* 2) DEFAULT_CONFIG */
    if ((frd_issue(n, IfxEray_PocCommand_config) != 0) ||
        (frd_wait_poc(n, (uint8_t)IfxEray_PocState_defaultConfig, 300U) != 0)) {
        bi->rc = -2;
        goto snap;
    }
    /* 3) CONFIG + apply cluster/message-RAM/pins */
    if ((frd_issue(n, IfxEray_PocCommand_config) != 0) ||
        (frd_wait_poc(n, (uint8_t)IfxEray_PocState_config, 300U) != 0)) {
        bi->rc = -3;
        goto snap;
    }
    if (frd_apply(n) != 0) {
        bi->rc = -10;
        goto snap;
    }

    /* 4) READY */
    IfxEray_clearAllFlags(n->sfr);
    if ((frd_issue(n, IfxEray_PocCommand_ready) != 0) ||
        (frd_wait_poc(n, (uint8_t)IfxEray_PocState_ready, 300U) != 0)) {
        bi->rc = -4;
        goto snap;
    }
    /* 5) WAKEUP (LCK-protected; the plain iLLD helper is silently ignored).
     * Accept LISTEN/SEND/DETECT; a quiet bus may also drop straight back to
     * READY after the pattern — that is fine, coldstart follows anyway. */
    IfxEray_clearAllFlags(n->sfr);
    if (frd_try(n, IfxEray_PocCommand_wakeup) != 0) {
        bi->rc = -5;
        goto snap;
    }
    {
        static const uint8_t wakeStates[] = { 1, 16, 17, 18, 19 };
        if (frd_wait_poc_any(n, wakeStates, 5, 1500U) != 0) {
            bi->rc = -6;
            goto snap;
        }
    }
    /* 6) ALLOW_COLDSTART then RUN (both LCK-protected). */
    IfxEray_clearAllFlags(n->sfr);
    if (frd_try(n, IfxEray_PocCommand_coldStart) != 0) {
        bi->rc = -7;
        goto snap;
    }
    {
        uint32_t start = g_TickCount_1ms;
        int ok = -1;
        while ((g_TickCount_1ms - start) < 500U) {
            if (frd_try(n, IfxEray_PocCommand_run) == 0) {
                ok = 0;
                break;
            }
        }
        if (ok != 0) {
            bi->rc = -8;
            goto snap;
        }
    }
    bi->rc = 0;
snap:
    bi->poc = frd_poc(n);
    bi->succ1 = n->sfr->SUCC1.U;
    bi->ccsv = n->sfr->CCSV.U;
    bi->eir = n->sfr->EIR.U;
    return bi->rc;
}

const FrdBootInfo *frd_boot_info(int node)
{
    if ((node < 0) || (node >= FRD_NODE_COUNT)) {
        return NULL_PTR;
    }
    return &s_boot[node];
}

int frd_setslot_rc(int node, int bufIdx)
{
    if ((node < 0) || (node >= FRD_NODE_COUNT) || (bufIdx < 0) || (bufIdx > 3)) {
        return -99;
    }
    return (int)s_setslot_rc[node][bufIdx];
}

int frd_startup(int verbose)
{
    uint32_t start;
    (void)verbose;

    frd_prepare();

    /* Phase A: bring each node to RUN (no NORMAL wait yet). */
    for (int i = 0; i < FRD_NODE_COUNT; i++) {
        if (frd_boot_phase_a(&s_nodes[i], i) != 0) {
            return s_boot[i].rc;
        }
    }

    /* Phase B: joint wait — both nodes synchronize together. */
    start = g_TickCount_1ms;
    while ((g_TickCount_1ms - start) < 12000U) {
        int allNormal = 1;
        for (int i = 0; i < FRD_NODE_COUNT; i++) {
            uint8_t p = frd_poc(&s_nodes[i]);
            if ((p != 2U) && (p != 3U)) {
                allNormal = 0;
                break;
            }
        }
        if (allNormal) {
            break;
        }
    }
    for (int i = 0; i < FRD_NODE_COUNT; i++) {
        s_boot[i].poc = frd_poc(&s_nodes[i]);
        s_boot[i].succ1 = s_nodes[i].sfr->SUCC1.U;
        s_boot[i].ccsv = s_nodes[i].sfr->CCSV.U;
        s_boot[i].eir = s_nodes[i].sfr->EIR.U;
        if ((s_boot[i].poc != 2U) && (s_boot[i].poc != 3U)) {
            s_boot[i].rc = -9;
            return -9;
        }
        s_nodes[i].cnt.lastEir = 0U;
        (void)IfxEray_Eray_setPocAllSlots(&s_nodes[i].ctrl);
    }
    return 0;
}

int frd_is_ready(void)
{
    for (int i = 0; i < FRD_NODE_COUNT; i++) {
        uint8_t p;
        /* NOTE: frd_poc() dereferences s_nodes[i].ctrl.eray, which is only
         * valid after frd_prepare().  Calling this before the first
         * frd_prepare() used to trap (data access to address 0) and left the
         * whole MCU silent - hit by `bench` (bench_fr) in tc397_selftest.
         * Report "not ready" instead of faulting. */
        if (!s_nodes[i].prepared) {
            return 0;
        }
        p = frd_poc(&s_nodes[i]);
        if ((p != 2U) && (p != 3U)) {
            return 0;
        }
    }
    return 1;
}

int frd_node_poc(int node)
{
    if ((node < 0) || (node >= FRD_NODE_COUNT) || (!s_nodes[node].prepared)) {
        return -1;
    }
    return (int)frd_poc(&s_nodes[node]);
}

uint32_t frd_node_ccsv(int node)
{
    if ((node < 0) || (node >= FRD_NODE_COUNT) || (!s_nodes[node].prepared)) {
        return 0;
    }
    return s_nodes[node].sfr->CCSV.U;
}

uint32_t frd_node_succ1(int node)
{
    if ((node < 0) || (node >= FRD_NODE_COUNT) || (!s_nodes[node].prepared)) {
        return 0;
    }
    return s_nodes[node].sfr->SUCC1.U;
}

uint32_t frd_node_eir(int node)
{
    if ((node < 0) || (node >= FRD_NODE_COUNT) || (!s_nodes[node].prepared)) {
        return 0;
    }
    return s_nodes[node].sfr->EIR.U;
}

uint32_t frd_node_sir(int node)
{
    if ((node < 0) || (node >= FRD_NODE_COUNT) || (!s_nodes[node].prepared)) {
        return 0;
    }
    return s_nodes[node].sfr->SIR.U;
}

static uint32_t frd_reg(int node, uint32_t regSel)
{
    if ((node < 0) || (node >= FRD_NODE_COUNT) || (!s_nodes[node].prepared)) {
        return 0;
    }
    switch (regSel) {
    case 0: return s_nodes[node].sfr->CCEV.U;
    case 1: return s_nodes[node].sfr->TEST1.U;
    case 2: return s_nodes[node].sfr->MHDS.U;
    case 3: return s_nodes[node].sfr->ACS.U;
    case 4: return s_nodes[node].sfr->FSR.U;
    default: return 0;
    }
}

uint32_t frd_node_ccev(int node)  { return frd_reg(node, 0); }
uint32_t frd_node_test1(int node) { return frd_reg(node, 1); }
uint32_t frd_node_mhds(int node)  { return frd_reg(node, 2); }
uint32_t frd_node_acs(int node)   { return frd_reg(node, 3); }
uint32_t frd_node_fsr(int node)   { return frd_reg(node, 4); }

uint32_t frd_node_ndat(int node)
{
    if ((node < 0) || (node >= FRD_NODE_COUNT) || (!s_nodes[node].prepared)) {
        return 0;
    }
    return s_nodes[node].sfr->NDAT1.U;
}

uint32_t frd_node_txrq(int node)
{
    if ((node < 0) || (node >= FRD_NODE_COUNT) || (!s_nodes[node].prepared)) {
        return 0;
    }
    return s_nodes[node].sfr->TXRQ1.U;
}

int frd_send(int node, const uint8_t *bytes16)
{
    FrdNode *n;
    uint32 words[4];
    uint8_t p;

    if ((node < 0) || (node >= FRD_NODE_COUNT) || (!s_nodes[node].prepared)) {
        return -1;
    }
    n = &s_nodes[node];
    p = frd_poc(n);
    if ((p != 2U) && (p != 3U)) {
        n->cnt.txBusy++;
        return -2;
    }

    if (bytes16 != NULL) {
        memcpy(words, bytes16, sizeof(words));
    } else {
        /* Default pattern: sequence counter + incrementing bytes. */
        uint32_t seq = n->txSeq;
        uint8_t *b = (uint8_t *)words;
        for (int i = 0; i < FRD_PAYLOAD_BYTES; i++) {
            b[i] = (uint8_t)((seq + (uint32_t)i) & 0xFFU);
        }
    }

    /* Data-only refresh (LHSH=0): header transfer (LHSH=1) outside CONFIG
     * is IIBA-illegal, so steady-state sends only refresh data + TXR. */
    if (frd_wait_ibsyh_clear(n, 100U) != 0) {
        return -4;
    }
    IfxEray_writeData(n->sfr, words, FRD_PAYLOAD_WORDS);
    IfxEray_sendHeader(n->sfr, FALSE);
    IfxEray_sendData(n->sfr, TRUE);
    IfxEray_setTransmitRequest(n->sfr, TRUE);
    IfxEray_setTxBufferNumber(n->sfr, 0U);
    if (frd_wait_ibsys_clear(n, 100U) != 0) {
        return -4;
    }
    if (frd_wait_ibsyh_clear(n, 100U) != 0) {
        return -4;
    }

    if ((n->sfr->EIR.B.IIBA != 0U) || (n->sfr->EIR.B.IOBA != 0U)) {
        return -3;
    }
    n->cnt.txOk++;
    n->txSeq++;
    return 0;
}

int frd_tx_view(int node, uint8_t bufIdx, FrdTxView *out)
{
    FrdNode *n;
    IfxEray_Eray_ReceiveControl rx;
    Ifx_ERAY *sfr;
    uint32_t tmo;

    if ((node < 0) || (node >= FRD_NODE_COUNT) || (!s_nodes[node].prepared) || (out == NULL) ||
        (bufIdx > 2U)) {
        return -1;
    }
    n = &s_nodes[node];
    sfr = n->sfr;

    tmo = g_TickCount_1ms;
    while (IfxEray_getOutputBufferBusyShadowStatus(sfr) == TRUE) {
        if ((g_TickCount_1ms - tmo) >= 100U) {
            return -2;
        }
    }
    rx.headerReceived = TRUE;
    rx.dataReceived = TRUE;
    rx.receiveRequested = TRUE;
    rx.swapRequested = TRUE;
    rx.bufferIndex = bufIdx;
    IfxEray_Eray_receiveFrame(&n->ctrl, &rx);
    /* The VIEW swap starts another shadow transfer — wait for it, else RDHS
     * still holds the previous buffer (stale read, e.g. zeros on 1st view). */
    tmo = g_TickCount_1ms;
    while (IfxEray_getOutputBufferBusyShadowStatus(sfr) == TRUE) {
        if ((g_TickCount_1ms - tmo) >= 100U) {
            return -3;
        }
    }
    out->rdhs1 = sfr->RDHS1.U;
    out->rdhs2 = sfr->RDHS2.U;
    out->rdhs3 = sfr->RDHS3.U;
    out->mbs = sfr->MBS.U;
    out->mhds = sfr->MHDS.U;
    out->data0 = sfr->RDDS_1S[0].U;
    out->data1 = sfr->RDDS_1S[1].U;
    return 0;
}
/* Poll RX buffers 1/2 of one node; returns number of new valid frames.
 * Deliberately NOT gated on NDAT (its clear semantics are unreliable for
 * polling): every poll VIEWs the buffers and counts only CONTENT CHANGES
 * (slot/cycle/length/first-word/MBS) so repeats are never double-counted. */
static int frd_poll_node(FrdNode *n)
{
    int got = 0;
    for (uint8_t buf = 1U; buf <= 2U; buf++) {
        {
            IfxEray_Eray_ReceiveControl rx;
            IfxEray_Eray_ReceivedFrame frame;
            Ifx_ERAY_MBS mbs;
            uint8_t bi = (uint8_t)(buf - 1U);
            uint16_t nbytes;
            uint8_t changed;

            rx.headerReceived = TRUE;
            rx.dataReceived = TRUE;
            rx.receiveRequested = TRUE;
            rx.swapRequested = TRUE;
            rx.bufferIndex = buf;
            IfxEray_Eray_receiveFrame(&n->ctrl, &rx);
            /* Wait out the VIEW swap or the frame read is stale. */
            {
                uint32_t tmo = g_TickCount_1ms;
                while (IfxEray_getOutputBufferBusyShadowStatus(n->sfr) == TRUE) {
                    if ((g_TickCount_1ms - tmo) >= 100U) {
                        break;
                    }
                }
            }
            memset(&frame, 0, sizeof(frame));
            IfxEray_Eray_readFrame(&n->ctrl, &frame, 64U);
            mbs.U = n->sfr->MBS.U;
            n->cnt.lastMbs = mbs.U;

            if (mbs.B.VFRA == 0U) {
                continue;
            }
            nbytes = (uint16_t)frame.header.payloadLength * 2U;
            if (nbytes > FRD_PAYLOAD_BYTES) {
                nbytes = FRD_PAYLOAD_BYTES;
            }
            /* Change detection against the per-buffer snapshot. */
            changed = (uint8_t)((n->seenValid[bi] == 0U) ||
                                (n->seenSlot[bi] != frame.header.frameId) ||
                                (n->seenCycle[bi] != frame.header.cycleNumber) ||
                                (n->seenLen[bi] != (uint8_t)nbytes) ||
                                (n->seenW0[bi] != frame.data[0]) ||
                                (n->seenMbs[bi] != mbs.U));
            n->seenValid[bi] = 1U;
            n->seenSlot[bi] = frame.header.frameId;
            n->seenCycle[bi] = frame.header.cycleNumber;
            n->seenLen[bi] = (uint8_t)nbytes;
            n->seenW0[bi] = frame.data[0];
            n->seenMbs[bi] = mbs.U;
            if (!changed) {
                continue;
            }
            /* NOTE: the MBS error bits are recorded for stats. MLST (message
             * lost = overwritten before host read) is BENIGN for content
             * checks and must NOT skip the frame; only true line errors
             * (SEOA/CEOA/SVOA/TCIA) reject. NFI alone decides null/data.
             * The physical layer is verified clean (CERA/CERB stay 0). */
            if ((mbs.B.SEOA != 0U) || (mbs.B.CEOA != 0U) || (mbs.B.SVOA != 0U) ||
                (mbs.B.TCIA != 0U)) {
                n->cnt.rxErr++;
                n->cnt.lastRejMbs = mbs.U;
                n->cnt.lastRejSlot = frame.header.frameId;
                n->cnt.lastRejPayloadWords = frame.header.payloadLength;
                n->cnt.lastRejData0 = frame.data[0];
                continue;
            }
            /* MLST (overwritten before host read) is benign: current content
             * is still the latest frame, keep processing it. Record it for
             * stats like true errors (without rejecting). */
            if (mbs.B.MLST != 0U) {
                n->cnt.lastRejMbs = mbs.U;
                n->cnt.lastRejSlot = frame.header.frameId;
                n->cnt.lastRejPayloadWords = frame.header.payloadLength;
                n->cnt.lastRejData0 = frame.data[0];
            }
            /* NFI polarity (iLLD doc): nullFrameIndicator==1 means a DATA
             * frame was received, ==0 means NULL. (MBS.NFIS has the
             * opposite sense: 1=NULL.) */
            if (frame.header.nullFrameIndicator == 0U) {
                n->cnt.rxNull++;
                continue;
            }
            {
                n->lastRx.valid = 1;
                n->lastRx.slot = frame.header.frameId;
                n->lastRx.cycle = frame.header.cycleNumber;
                n->lastRx.payloadLen = (uint8_t)nbytes;
                memcpy(n->lastRx.payload, (const void *)frame.data, nbytes);
                n->lastRx.mbs = mbs.U;
                n->cnt.rxOk++;
                got++;
            }
        }
    }
    {
        Ifx_ERAY_EIR eir = IfxEray_Eray_getErrorInterrupts(&n->ctrl);
        if (eir.U != 0U) {
            n->cnt.eirCount++;
            n->cnt.lastEir = eir.U;
            IfxEray_clearAllFlags(n->sfr);
        }
    }
    return got;
}

int frd_poll(void)
{
    int got = 0;
    for (int i = 0; i < FRD_NODE_COUNT; i++) {
        if (!s_nodes[i].prepared) {
            continue;
        }
        got += frd_poll_node(&s_nodes[i]);
    }
    return got;
}

int frd_last_rx(int node, FrdRxFrame *out)
{
    if ((node < 0) || (node >= FRD_NODE_COUNT) || (out == NULL)) {
        return -1;
    }
    memcpy(out, &s_nodes[node].lastRx, sizeof(*out));
    return (s_nodes[node].lastRx.valid != 0U) ? 0 : -2;
}

const FrdCounters *frd_counters(int node)
{
    if ((node < 0) || (node >= FRD_NODE_COUNT)) {
        return NULL_PTR;
    }
    return &s_nodes[node].cnt;
}
