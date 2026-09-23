/* can12.c — TC397 12-channel MCMCAN driver (polling, single-core).
 * Ported from tc387 can_x12_gcc (ISR echo) + gs_udp_can_tc387/App/can12.c
 * (polling, MessageRAM layout), adapted to the tc397_can_x12 pin assignment.
 */
#include "can12.h"
#include "IfxPort.h"
#include "IfxCpu_Irq.h"
#include "IfxScuCcu.h"
#include <string.h>

/* g_can is ~70 KB zero-init: force NOBITS .bss (LSL *(.bss.*) -> NOLOAD,
 * zeroed by startup) instead of per-symbol PROGBITS data which would waste
 * 70 KB flash LMA copy. All fields are assigned at init + rxRing memset,
 * so behavior is deterministic either way. */
#if defined(__GNUC__)
#pragma section ".bss.g_can" aw
#endif
mcmcanType g_can[CAN_NUM];
#if defined(__GNUC__)
#pragma section
#endif

/* One physical MCMCAN module per group of 4 logical channels. */
static boolean g_canModuleInitialized[3] = {FALSE, FALSE, FALSE};

extern volatile uint32 g_TickCount_1ms;

/* Verified against IfxCan_PinMap_TC39xB_LFBGA292:
 * TXD00..03 = CAN0 nodes0-3, TXD10..13 = CAN1, TXD20..23 = CAN2. */
static const IfxCan_Can_Pins can_pins[CAN_NUM] = {
    {&IfxCan_TXD00_P34_1_OUT,  IfxPort_OutputMode_pushPull, &IfxCan_RXD00D_P33_12_IN, IfxPort_InputMode_pullUp, IfxPort_PadDriver_cmosAutomotiveSpeed4}, /* CAN0  */
    {&IfxCan_TXD01_P15_2_OUT,  IfxPort_OutputMode_pushPull, &IfxCan_RXD01D_P33_10_IN, IfxPort_InputMode_pullUp, IfxPort_PadDriver_cmosAutomotiveSpeed4}, /* CAN1  */
    {&IfxCan_TXD02_P32_5_OUT,  IfxPort_OutputMode_pushPull, &IfxCan_RXD02C_P32_6_IN,  IfxPort_InputMode_pullUp, IfxPort_PadDriver_cmosAutomotiveSpeed4}, /* CAN2  */
    {&IfxCan_TXD03_P32_3_OUT,  IfxPort_OutputMode_pushPull, &IfxCan_RXD03B_P32_2_IN,  IfxPort_InputMode_pullUp, IfxPort_PadDriver_cmosAutomotiveSpeed4}, /* CAN3  */
    {&IfxCan_TXD10_P00_0_OUT,  IfxPort_OutputMode_pushPull, &IfxCan_RXD10A_P00_1_IN,  IfxPort_InputMode_pullUp, IfxPort_PadDriver_cmosAutomotiveSpeed4}, /* CAN4  */
    {&IfxCan_TXD11_P23_6_OUT,  IfxPort_OutputMode_pushPull, &IfxCan_RXD11C_P23_7_IN,  IfxPort_InputMode_pullUp, IfxPort_PadDriver_cmosAutomotiveSpeed4}, /* CAN5  */
    {&IfxCan_TXD12_P23_2_OUT,  IfxPort_OutputMode_pushPull, &IfxCan_RXD12C_P23_3_IN,  IfxPort_InputMode_pullUp, IfxPort_PadDriver_cmosAutomotiveSpeed4}, /* CAN6  */
    {&IfxCan_TXD13_P33_4_OUT,  IfxPort_OutputMode_pushPull, &IfxCan_RXD13B_P33_5_IN,  IfxPort_InputMode_pullUp, IfxPort_PadDriver_cmosAutomotiveSpeed4}, /* CAN7  */
    {&IfxCan_TXD20_P10_6_OUT,  IfxPort_OutputMode_pushPull, &IfxCan_RXD20C_P34_2_IN,  IfxPort_InputMode_pullUp, IfxPort_PadDriver_cmosAutomotiveSpeed4}, /* CAN8  */
    {&IfxCan_TXD21_P00_2_OUT,  IfxPort_OutputMode_pushPull, &IfxCan_RXD21A_P00_3_IN,  IfxPort_InputMode_pullUp, IfxPort_PadDriver_cmosAutomotiveSpeed4}, /* CAN9  */
    {&IfxCan_TXD22_P22_8_OUT,  IfxPort_OutputMode_pushPull, &IfxCan_RXD22B_P32_7_IN,  IfxPort_InputMode_pullUp, IfxPort_PadDriver_cmosAutomotiveSpeed4}, /* CAN10 */
    {&IfxCan_TXD23_P22_10_OUT, IfxPort_OutputMode_pushPull, &IfxCan_RXD23E_P22_11_IN, IfxPort_InputMode_pullUp, IfxPort_PadDriver_cmosAutomotiveSpeed4}, /* CAN11 */
};

uint8 can_len2dlc(uint8 len)
{
    if (len <= 8)  return len;
    if (len <= 12) return 9;
    if (len <= 16) return 10;
    if (len <= 20) return 11;
    if (len <= 24) return 12;
    if (len <= 32) return 13;
    if (len <= 48) return 14;
    return 15;
}

/* ---------- Transceivers ---------- */
void xcvr_init(void)
{
    /* TCAN1043 CAN0: EN=H + nSTB=H -> Normal mode (datasheet Table 8-3). */
    IfxPort_setPinModeOutput(&MODULE_P33, 1, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinHigh(&MODULE_P33, 1);
    IfxPort_setPinModeOutput(&MODULE_P33, 0, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinHigh(&MODULE_P33, 0);
    /* nFAULT P10.3: input, pull-up (transceiver pulls low on fault). */
    IfxPort_setPinModeInput(&MODULE_P10, 3, IfxPort_InputMode_pullUp);

    /* TCAN1044 groups: STB=L -> Normal mode (STB=H = standby). */
    IfxPort_setPinModeOutput(&MODULE_P21, 5, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinLow(&MODULE_P21, 5);   /* CAN1/2/3 */
    IfxPort_setPinModeOutput(&MODULE_P21, 2, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinLow(&MODULE_P21, 2);   /* CAN4/5/6/7 */
    IfxPort_setPinModeOutput(&MODULE_P21, 4, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinLow(&MODULE_P21, 4);   /* CAN8/9/10/11 */
}

uint8 xcvr_fault(void)
{
    /* nFAULT low = fault asserted */
    return (IfxPort_getPinState(&MODULE_P10, 3) == IfxPort_State_low) ? 1 : 0;
}

/* ---------- Init ---------- */
void can12_init_channel_simple(canChannel ch, uint32 baud, float samplePoint,
                               uint32 fastBaud, float fastSamplePoint)
{
    uint8 group = (ch < 4) ? 0 : (ch < 8) ? 1 : 2;
    Ifx_CAN *modulePtr = (group == 0) ? &MODULE_CAN0 : (group == 1) ? &MODULE_CAN1 : &MODULE_CAN2;
    mcmcanType *dev = &g_can[ch];

    if (!g_canModuleInitialized[group]) {
        IfxCan_Can_initModuleConfig(&dev->config, modulePtr);
        IfxCan_Can_initModule(&dev->module, &dev->config);
        g_canModuleInitialized[group] = TRUE;
    } else {
        dev->module = g_can[(group == 0) ? CAN0 : (group == 1) ? CAN4 : CAN8].module;
    }

    IfxCan_Can_initNodeConfig(&dev->nodeConfig, &dev->module);

    dev->channel = ch;
    dev->nodeConfig.nodeId = (IfxCan_NodeId)(ch % 4);
    dev->nodeConfig.clockSource = IfxCan_ClockSource_both;
    dev->nodeConfig.frame.type = IfxCan_FrameType_transmitAndReceive;
    dev->nodeConfig.frame.mode = IfxCan_FrameMode_fdLongAndFast;

    dev->nodeConfig.baudRate.baudrate = baud;
    dev->nodeConfig.baudRate.samplePoint = (uint16)(samplePoint * 10000.0f);
    dev->nodeConfig.baudRate.syncJumpWidth = (uint16)(10000 - dev->nodeConfig.baudRate.samplePoint);
    dev->nodeConfig.fastBaudRate.baudrate = fastBaud;
    dev->nodeConfig.fastBaudRate.samplePoint = (uint16)(fastSamplePoint * 10000.0f);
    dev->nodeConfig.fastBaudRate.syncJumpWidth = (uint16)(10000 - dev->nodeConfig.fastBaudRate.samplePoint);

    dev->nodeConfig.txConfig.txMode = IfxCan_TxMode_fifo;
    dev->nodeConfig.txConfig.dedicatedTxBuffersNumber = 0;
    dev->nodeConfig.txConfig.txFifoQueueSize = CAN_TX_FIFO_SIZE;
    dev->nodeConfig.txConfig.txBufferDataFieldSize = IfxCan_DataFieldSize_64;
    dev->nodeConfig.txConfig.txEventFifoSize = 0;

    dev->nodeConfig.filterConfig.messageIdLength = IfxCan_MessageIdLength_both;
    dev->nodeConfig.filterConfig.standardListSize = 32;
    dev->nodeConfig.filterConfig.extendedListSize = 32;
    dev->nodeConfig.filterConfig.rejectRemoteFramesWithStandardId = FALSE;
    dev->nodeConfig.filterConfig.rejectRemoteFramesWithExtendedId = FALSE;
    dev->nodeConfig.filterConfig.standardFilterForNonMatchingFrames = IfxCan_NonMatchingFrame_reject;
    dev->nodeConfig.filterConfig.extendedFilterForNonMatchingFrames = IfxCan_NonMatchingFrame_reject;

    dev->nodeConfig.rxConfig.rxMode = IfxCan_RxMode_fifo0;
    dev->nodeConfig.rxConfig.rxBufferDataFieldSize = IfxCan_DataFieldSize_64;
    dev->nodeConfig.rxConfig.rxFifo0DataFieldSize = IfxCan_DataFieldSize_64;
    dev->nodeConfig.rxConfig.rxFifo1DataFieldSize = IfxCan_DataFieldSize_64;
    dev->nodeConfig.rxConfig.rxFifo0OperatingMode = IfxCan_RxFifoMode_overwrite;
    dev->nodeConfig.rxConfig.rxFifo1OperatingMode = IfxCan_RxFifoMode_overwrite;
    dev->nodeConfig.rxConfig.rxFifo0WatermarkLevel = 0;
    dev->nodeConfig.rxConfig.rxFifo1WatermarkLevel = 0;
    dev->nodeConfig.rxConfig.rxFifo0Size = CAN_RX_FIFO_SIZE;
    dev->nodeConfig.rxConfig.rxFifo1Size = 0;

    /* Message RAM: 4 KB per node (same as tc387 can_x12, proven). */
    dev->nodeConfig.messageRAM.baseAddress = 0xF0200000u + (ch / 4) * 0x10000u;
    dev->nodeConfig.messageRAM.standardFilterListStartAddress = (ch % 4) * 0x1000u;
    dev->nodeConfig.messageRAM.extendedFilterListStartAddress = (ch % 4) * 0x1000u + 0x100u;
    dev->nodeConfig.messageRAM.rxFifo0StartAddress = (ch % 4) * 0x1000u + 0x200u;
    dev->nodeConfig.messageRAM.rxFifo1StartAddress = 0;
    dev->nodeConfig.messageRAM.rxBuffersStartAddress = 0;
    dev->nodeConfig.messageRAM.txEventFifoStartAddress = 0;
    dev->nodeConfig.messageRAM.txBuffersStartAddress = (ch % 4) * 0x1000u + 0x700u;

    /* Polling: no interrupts (RX drained in main loop). */
    dev->nodeConfig.interruptConfig.rxFifo0NewMessageEnabled = FALSE;
    dev->nodeConfig.interruptConfig.transmissionCompletedEnabled = FALSE;
    dev->nodeConfig.interruptConfig.busOffStatusEnabled = FALSE;

    dev->nodeConfig.pins = &can_pins[ch];
    dev->nodeConfig.busLoopbackEnabled = FALSE;
    dev->nodeConfig.calculateBitTimingValues = TRUE;

    IfxCan_Can_initNode(&dev->node, &dev->nodeConfig);

    /* TDC for data phase (same rule as tc387 reference). */
    IfxCan_Node_enableConfigurationChange(dev->node.node);
    IfxCan_Node_setTransceiverDelayCompensationOffset(
        dev->node.node, (uint8)(dev->node.node->DBTP.B.DTSEG1 + 2));
    IfxCan_Node_disableConfigurationChange(dev->node.node);

    /* Accept-all filters (classic range 0..0x7FF, extended full range). */
    dev->stdFilter.number = 0;
    dev->stdFilter.elementConfiguration = IfxCan_FilterElementConfiguration_storeInRxFifo0;
    dev->stdFilter.type = IfxCan_FilterType_classic;
    dev->stdFilter.id1 = 0;
    dev->stdFilter.id2 = 0;
    dev->stdFilter.rxBufferOffset = IfxCan_RxBufferId_0;
    IfxCan_Can_setStandardFilter(&dev->node, &dev->stdFilter);

    dev->extFilter.number = 0;
    dev->extFilter.elementConfiguration = IfxCan_FilterElementConfiguration_storeInRxFifo0;
    dev->extFilter.type = IfxCan_FilterType_classic;
    dev->extFilter.id1 = 0;
    dev->extFilter.id2 = 0;
    dev->extFilter.rxBufferOffset = IfxCan_RxBufferId_0;
    IfxCan_Can_setExtendedFilter(&dev->node, &dev->extFilter);

    /* Remote (RTR) frames must reach FIFO0 too. */
    IfxCan_Node_enableConfigurationChange(dev->node.node);
    IfxCan_Node_acceptRemoteFramesWithStandardId(dev->node.node);
    IfxCan_Node_acceptRemoteFrameswithExtendedId(dev->node.node);
    IfxCan_Node_disableConfigurationChange(dev->node.node);

    IfxCan_Can_initMessage(&dev->txMsg);
    IfxCan_Can_initMessage(&dev->rxMsg);

    dev->rxCount = 0; dev->txCount = 0; dev->overflowCount = 0; dev->busOff = FALSE;
    dev->boCount = 0; dev->restartCount = 0;
    dev->rxHead = 0; dev->rxTail = 0; dev->rxDrop = 0;
    memset(dev->rxRing, 0, sizeof(dev->rxRing));
}

void can12_init_all(uint32 arbBaud, uint32 dataBaud)
{
    if (arbBaud == 0)  arbBaud = 1000000u;
    if (dataBaud == 0) dataBaud = 5000000u;
    /* Warm-boot (SW reset) retains RAM: stale g_canModuleInitialized would
     * skip module init and reuse stale handles (hangs after SW reset).
     * Force full re-init. */
    g_canModuleInitialized[0] = FALSE;
    g_canModuleInitialized[1] = FALSE;
    g_canModuleInitialized[2] = FALSE;
    for (canChannel ch = CAN0; ch < CAN_NUM; ch++) {
        can12_init_channel_simple(ch, arbBaud, 0.8f, dataBaud, 0.8f);
    }
}

void can12_init_all_1M_5M(void)
{
    can12_init_all(1000000u, 5000000u);
}

/* ---------- Poll ---------- */
static void can12_push_ring(mcmcanType *dev, const can12_frame_t *f)
{
    uint32 used = dev->rxHead - dev->rxTail;
    if (used >= CAN_RX_RING_DEPTH) {
        dev->rxDrop++;
        dev->rxTail++;   /* overwrite oldest */
    }
    dev->rxRing[dev->rxHead % CAN_RX_RING_DEPTH] = *f;
    dev->rxHead++;
}

uint32_t can12_poll_channel(canChannel ch)
{
    if (ch >= CAN_NUM) return 0;
    mcmcanType *dev = &g_can[ch];
    uint32_t cnt = 0;

    /* Bus-off poll (interrupts disabled): auto-recover. */
    if (IfxCan_Node_getBusOffStatus(dev->node.node) == IfxCan_CanNodeBusOffErrorStatus_BusOffErr) {
        if (!dev->busOff) { dev->busOff = TRUE; dev->boCount++; }
        can12_restart_channel(ch, FALSE);
        return 0;
    }

    if (IfxCan_Can_getRxFifo0FillLevel(&dev->node) >= CAN_RX_FIFO_SIZE) {
        dev->overflowCount++;
    }

    while (IfxCan_Can_getRxFifo0FillLevel(&dev->node)) {
        /* iLLD readMessage() doesn't return R0.B.RTR: sample FIFO element
         * directly before the read acks it (same as gs_udp_can_tc387). */
        IfxCan_RxBufferId getIdx = IfxCan_Node_getRxFifo0GetIndex(dev->node.node);
        Ifx_CAN_RXMSG *rxEl = IfxCan_Node_getRxFifo0ElementAddress(
            dev->node.node, dev->node.messageRAM.baseAddress,
            dev->node.messageRAM.rxFifo0StartAddress, getIdx);
        boolean isRtr = (rxEl->R0.B.RTR != 0) ? TRUE : FALSE;

        dev->rxMsg.readFromRxFifo0 = TRUE;
        IfxCan_Can_readMessage(&dev->node, &dev->rxMsg, (uint32 *)dev->rxData);
        dev->rxMsg.remoteTransmitRequest = isRtr;

        can12_frame_t f;
        f.ext = (dev->rxMsg.messageIdLength == IfxCan_MessageIdLength_extended) ? 1 : 0;
        f.id  = dev->rxMsg.messageId & (f.ext ? 0x1FFFFFFFu : 0x7FFu);
        f.rtr = isRtr ? 1 : 0;
        f.fd  = (dev->rxMsg.frameMode == IfxCan_FrameMode_fdLong ||
                 dev->rxMsg.frameMode == IfxCan_FrameMode_fdLongAndFast) ? 1 : 0;
        f.brs = (dev->rxMsg.frameMode == IfxCan_FrameMode_fdLongAndFast) ? 1 : 0;
        f.dlc = dev->rxMsg.dataLengthCode & 0xF;
        f.len = f.rtr ? 0 : can_dlc2len(f.dlc);
        if (f.len > 64) f.len = 64;
        if (f.len) memcpy(f.data, dev->rxData, f.len);
        f.tick = g_TickCount_1ms;
        can12_push_ring(dev, &f);

        dev->rxCount++;
        cnt++;
    }
    return cnt;
}

uint32_t can12_poll_all(void)
{
    uint32_t total = 0;
    for (canChannel ch = CAN0; ch < CAN_NUM; ch++) {
        total += can12_poll_channel(ch);
    }
    return total;
}

/* ---------- Send ---------- */
IfxCan_Status can12_send(canChannel ch, uint32_t id, uint8 ext, uint8 rtr,
                         uint8 fd, uint8 brs, uint8 dlc, const uint8 *data)
{
    if (ch >= CAN_NUM) return IfxCan_Status_notSentBusy;
    mcmcanType *dev = &g_can[ch];
    if (IfxCan_Can_isTxFifoQueueFull(&dev->node)) return IfxCan_Status_notSentBusy;

    IfxCan_Can_initMessage(&dev->txMsg);
    dev->txMsg.messageId = ext ? (id & 0x1FFFFFFFu) : (id & 0x7FFu);
    dev->txMsg.messageIdLength = ext ? IfxCan_MessageIdLength_extended
                                     : IfxCan_MessageIdLength_standard;
    /* CAN FD has no remote frames. */
    dev->txMsg.remoteTransmitRequest = (rtr && !fd) ? TRUE : FALSE;
    if (fd) {
        dev->txMsg.frameMode = brs ? IfxCan_FrameMode_fdLongAndFast
                                   : IfxCan_FrameMode_fdLong;
    } else {
        dev->txMsg.frameMode = IfxCan_FrameMode_standard;
    }
    dev->txMsg.dataLengthCode = dlc & 0xF;
    dev->txMsg.bufferNumber = 0;
    dev->txMsg.storeInTxFifoQueue = TRUE;
    dev->txMsg.readFromRxFifo0 = FALSE;
    dev->txMsg.readFromRxFifo1 = FALSE;

    uint8 len = (rtr && !fd) ? 0 : can_dlc2len(dlc & 0xF);
    for (uint8 i = 0; i < len; i++) dev->txData[i] = data[i];

    IfxCan_Status st = IfxCan_Can_sendMessage(&dev->node, &dev->txMsg,
                                              (uint32 *)dev->txData);
    if (st == IfxCan_Status_ok) dev->txCount++;
    return st;
}

/* ---------- Bus-off recovery ---------- */
uint32_t can12_restart_channel(canChannel ch, boolean force)
{
    if (ch >= CAN_NUM) return 0;
    mcmcanType *dev = &g_can[ch];
    boolean inBo = (IfxCan_Node_getBusOffStatus(dev->node.node) ==
                    IfxCan_CanNodeBusOffErrorStatus_BusOffErr);
    if (!inBo && !force) return 0;
    /* M_CAN bus-off recovery: INIT toggle lets the node rejoin after
     * 129x11 recessive bits; also clears a stuck TX FIFO state. */
    IfxCan_Node_enableConfigurationChange(dev->node.node);
    IfxCan_Node_clearInterruptFlag(dev->node.node, IfxCan_Interrupt_busOffStatus);
    IfxCan_Node_disableConfigurationChange(dev->node.node);
    dev->busOff = FALSE;
    dev->restartCount++;
    return 1;
}

/* ---------- Stats / ring ---------- */
uint32_t can12_get_rx_count(canChannel ch) { return g_can[ch].rxCount; }
uint32_t can12_get_tx_count(canChannel ch) { return g_can[ch].txCount; }
uint32_t can12_get_overflow(canChannel ch) { return g_can[ch].overflowCount; }

uint32 can12_ring_pending(canChannel ch)
{
    if (ch >= CAN_NUM) return 0;
    return g_can[ch].rxHead - g_can[ch].rxTail;
}

boolean can12_ring_pop(canChannel ch, can12_frame_t *out)
{
    if (ch >= CAN_NUM || out == NULL) return FALSE;
    mcmcanType *dev = &g_can[ch];
    if (dev->rxTail == dev->rxHead) return FALSE;
    *out = dev->rxRing[dev->rxTail % CAN_RX_RING_DEPTH];
    dev->rxTail++;
    return TRUE;
}
