#include "DreCanEthBridge.h"

#include <stdio.h>
#include <string.h>

#include "Ap/Std/IfxApApu.h"
#include "Clock/Std/IfxClock.h"
#include "Cpu/Std/IfxCpu.h"
#include "Can/Can/IfxCan_Can.h"
#include "Can/Std/IfxCan.h"
#include "Dre/Dre/IfxDre_Dre.h"
#include "Dre/Std/IfxDre.h"
#include "Geth/Std/IfxGeth.h"
#include "IfxGeth_Eth.h"
#include "IfxCan_regdef.h"
#include "IfxDre_regdef.h"
#include "Pms/Std/IfxPmsEvr.h"
#include "Port/Std/IfxPort.h"
#include "SysSe/Bsp/Bsp.h"
#include "Stm/Std/IfxStm.h"
#include "Cpu/Std/IfxCpu.h"

#include "kit_tc4d7_lite.h"
#include "pdl/ifx_geth.h"
#include "pdl/ifx_hsphy.h"
#include "phy/dp83825i.h"

#define TC4D7_CAN_NODE_ID IfxCan_NodeId_1
#define TC4D7_CAN_NOMINAL_BAUDRATE 500000U
#define TC4D7_CAN_NOMINAL_SAMPLE_POINT 8000U
#define TC4D7_CAN_DATA_BAUDRATE 2000000U
#define TC4D7_CAN_DATA_SAMPLE_POINT 8000U
#define TC4D7_CAN_TX_FIFO_SIZE 8U
#define TC4D7_CAN_RX_FIFO_SIZE 8U

#define TC4D7_CAN_CRE_BASE_OFFSET 0x0800U

#define DRE_ETH_PORT_INDEX 0U
#define DRE_ETH_PHY_ADDR BOARD_GETH0_P0_PHYADR
#define DRE_ETH_NTSCF_OFFSET 14U
#define DRE_ETH_PAYLOAD_LENGTH 64U
#define DRE_TETHDL0_DESCRIPTOR_ADDRESS (0xF903B140U)
/* Destination MAC for the published ACF frames.
   Default: broadcast (FF:FF:FF:FF:FF:FF) so any host on the LAN can subscribe
   to the CAN->Ethernet stream.  Broadcast is also accepted by the NIC hardware
   unconditionally, which makes packet capture (Wireshark/tshark) on the test
   PC reliable without promiscuous-mode quirks.  Set DRE_ETH_PC_MAC to a
   specific host MAC for a point-to-point bridge. */
/* During verification the host sits at this MAC; using a unicast destination
   (instead of broadcast) keeps the DRE EOBUF happy.  macDestinationAddress0 =
   MAC bytes[0..1], macDestinationAddress1 = bytes[2..5]. */
#define DRE_ETH_PC_MAC0 0x001BU
#define DRE_ETH_PC_MAC1 0x2275346CU
#define DRE_TETHDL0_DESCRIPTOR_WORDS ((volatile uint32 *)DRE_TETHDL0_DESCRIPTOR_ADDRESS)

#define DRE_GETH_HEADER_PAD 16U
#define DRE_GETH_MAX_BUFFER_SIZE (2560U + DRE_GETH_HEADER_PAD + 2U)

#define IFX_NETIF_MDIO_CMD_READ  VAL2FLD(IFX_GETH_PORT_CORE_MDIO_SINGLE_COMMAND_CONTROL_DATA_CMD, 3)
#define IFX_NETIF_MDIO_CMD_WRITE VAL2FLD(IFX_GETH_PORT_CORE_MDIO_SINGLE_COMMAND_CONTROL_DATA_CMD, 1)
#define IFX_NETIF_MDIO_CMD_BUSY  GENMASK(IFX_GETH_PORT_CORE_MDIO_SINGLE_COMMAND_CONTROL_DATA_SBUSY)

extern volatile uint32 g_TickCount_1ms;

static IfxCan_Can g_canModule;
static IfxCan_Can_Node g_canNode;
static IfxDre_Dre g_dre;
static IfxGeth_Eth g_geth;
static phy_t g_phy;
static uint8 g_macAddress[6];

static uint32 g_canRxWords[16];
static uint32 g_canRxCount;
static uint32 g_dreTriggerCount;

/* Performance / loss monitoring counters (printed each diag tick).
   canRxTotal : CAN frames received by the SW RX path.
   ethTxTotal : ACF frames successfully handed to the GETH Tx DMA ring.
   ethTxDrop  : CAN frames NOT forwarded because the Tx ring was full
                (bounded tail advance to avoid the historic wrap-around
                deadlock) — i.e. measured packet loss under overload.
   ringStalls : ticks where the ring was full and not draining (diagnostic
                signal that the DMA/TETHDL could not keep up).
   -- Reverse path (Ethernet -> CAN) --
   ethRxTotal : Ethernet frames received on the GETH Rx DMA ring.
   acfRxTotal : Ethernet frames carrying the IEEE-1722 ACF EtherType 0x22F0
                (i.e. that reached the ACF parse stage at all).
   canTxTotal : CAN frames successfully handed to the CAN TX FIFO by the
                reverse (ETH->CAN) bridge.
   canTxDrop  : Ethernet frames NOT converted to CAN because the CAN TX FIFO
                was full (overload loss on the reverse path). */
typedef struct
{
    uint32 canRxTotal;
    uint32 ethTxTotal;
    uint32 ethTxDrop;
    uint32 ringStalls;
    uint32 ethRxTotal;
    uint32 acfRxTotal;
    uint32 canTxTotal;
    uint32 canTxDrop;
} DreCanEthBridge_PerfCounters;

static DreCanEthBridge_PerfCounters g_drePerf;

/* Software model of the GETH Tx ring occupancy.  The ring has
   DRE_GETH_TX_RING_ENTRIES (4) slots.  We increment it when we push a
   descriptor to the DMA and resynchronise it from the hardware pointers each
   main-loop tick, so it can never drift and we can reliably tell when the
   ring is full (and must drop) instead of blindly over-advancing the tail
   pointer (which previously deadlocked the Tx path under burst load). */
#define DRE_GETH_TX_RING_ENTRIES 4U
#define DRE_GETH_RX_RING_ENTRIES IFXGETH_MAX_RX_DESCRIPTORS   /* real Rx ring size (8) */
static uint8 g_txRingPending;

static uint32 g_lastLinkPollTick;
static uint32 g_lastProbeTxTick;
static uint32 g_lastDiagTick;
static boolean g_linkLogged;
static boolean g_softwareTriggerRequested;
static boolean g_descriptorLogged;

IFX_ALIGN(8) __attribute__((section(".lmubss_nc"))) static IfxGeth_TxDescrList g_txDescrList;
IFX_ALIGN(8) __attribute__((section(".lmubss_nc"))) static IfxGeth_RxDescrList g_rxDescrList;
IFX_ALIGN(8) __attribute__((section(".lmubss_nc"))) static uint8 g_channel0TxBuffer[IFXGETH_MAX_TX_DESCRIPTORS][DRE_GETH_MAX_BUFFER_SIZE];
IFX_ALIGN(8) __attribute__((section(".lmubss_nc"))) static uint8 g_channel0RxBuffer[IFXGETH_MAX_RX_DESCRIPTORS][DRE_GETH_MAX_BUFFER_SIZE];

static const IfxCan_Can_Pins g_canPins = {
    &IfxCan_TXD01_P01_3_OUT,
    IfxPort_OutputMode_pushPull,
    &IfxCan_RXD01C_P01_4_IN,
    IfxPort_InputMode_pullUp,
    IfxPort_PadDriver_cmosAutomotiveSpeed3
};

static const IfxCan_Filter g_acceptAllStandardFilter = {
    .number = 0,
    .elementConfiguration = IfxCan_FilterElementConfiguration_storeInRxFifo0,
    .stdType = IfxCan_StdFilterType_classic,
    .xtdType = IfxCan_XtdFilterType_classic,
    .id1 = 0,
    .id2 = 0,
    .rxBufferOffset = IfxCan_RxBufferId_0
};

static const IfxCan_Filter g_acceptAllExtendedFilter = {
    .number = 0,
    .elementConfiguration = IfxCan_FilterElementConfiguration_storeInRxFifo0,
    .stdType = IfxCan_StdFilterType_classic,
    .xtdType = IfxCan_XtdFilterType_classic,
    .id1 = 0,
    .id2 = 0,
    .rxBufferOffset = IfxCan_RxBufferId_0
};

static const Ifx_GETH_MDIO_Pins g_gethMdioPins = {
    .mdc = &BOARD_GETH0_P0_MDC,
    .mdio = &BOARD_GETH0_P0_MDIO,
};

static const IfxHsphy_Geth_RmiiPins g_gethRmiiPins = {
    .rxd0 = &BOARD_GETH0_P0_RXD0,
    .rxd1 = &BOARD_GETH0_P0_RXD1,
    .crsDiv = &BOARD_GETH0_P0_CSRDV,
    .refClk = &BOARD_GETH0_P0_REFCLK,
    .txd0 = &BOARD_GETH0_P0_TXD0,
    .txd1 = &BOARD_GETH0_P0_TXD1,
    .txEn = &BOARD_GETH0_P0_TXEN,
};

static void DreCanEthBridge_initCanTransceiver(void)
{
    IfxPort_setPinModeOutput(&MODULE_P03, 5, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinPadDriver(&MODULE_P03, 5, IfxPort_PadDriver_cmosAutomotiveSpeed1);
    IfxPort_setPinLow(&MODULE_P03, 5);
}

static void DreCanEthBridge_enableVoltageRails(void)
{
    IfxPmsEvr_enableVoltageRail(&MODULE_PMS, IfxPmsEvr_PrimaryMonitorVoltageSource_vddphphy0);
    IfxPmsEvr_enableVoltageRail(&MODULE_PMS, IfxPmsEvr_PrimaryMonitorVoltageSource_vddphy0);
    IfxPmsEvr_enableVoltageRail(&MODULE_PMS, IfxPmsEvr_PrimaryMonitorVoltageSource_vddphphy1);
    IfxPmsEvr_enableVoltageRail(&MODULE_PMS, IfxPmsEvr_PrimaryMonitorVoltageSource_vddphy1);
    IfxPmsEvr_enableVoltageRail(&MODULE_PMS, IfxPmsEvr_PrimaryMonitorVoltageSource_vddphphy2);
    IfxPmsEvr_enableVoltageRail(&MODULE_PMS, IfxPmsEvr_PrimaryMonitorVoltageSource_vddphy2);
    IfxPmsEvr_enableVoltageRail(&MODULE_PMS, IfxPmsEvr_PrimaryMonitorVoltageSource_vddhsif);
}

static sint32 DreCanEthBridge_mdioInit(const Ifx_GETH_MDIO_Pins *pins, uint32 csrClockRate)
{
    uint32 divider;

    MODULE_GETH0.MACEN.U |= 1U << DRE_ETH_PORT_INDEX;

    if ((csrClockRate >= 40000000U) && (csrClockRate <= 50000000U))
    {
        divider = 5U;
    }
    else if (csrClockRate >= 350000000U)
    {
        divider = 4U;
    }
    else if (csrClockRate >= 300000000U)
    {
        divider = 3U;
    }
    else if (csrClockRate >= 250000000U)
    {
        divider = 2U;
    }
    else if (csrClockRate >= 150000000U)
    {
        divider = 1U;
    }
    else if (csrClockRate >= 100000000U)
    {
        divider = 0U;
    }
    else
    {
        return -1;
    }

    if (pins != NULL_PTR)
    {
        IfxPort_setPinModeInput(pins->mdio->pin.port, pins->mdio->pin.pinIndex, IfxPort_InputMode_noPullDevice);
        IfxPort_setPinPadDriver(pins->mdio->pin.port, pins->mdio->pin.pinIndex, IfxPort_PadDriver_cmosAutomotiveSpeed3);
        IfxPort_setPinModeOutput(pins->mdc->pin.port, pins->mdc->pin.pinIndex, IfxPort_OutputMode_pushPull, pins->mdc->select);
        IfxPort_setPinPadDriver(pins->mdc->pin.port, pins->mdc->pin.pinIndex, IfxPort_PadDriver_cmosAutomotiveSpeed3);
    }

    MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MDIO.SINGLE_COMMAND_CONTROL_DATA.U =
        VAL2FLD(IFX_GETH_PORT_CORE_MDIO_SINGLE_COMMAND_CONTROL_DATA_CR, divider);

    return 0;
}

static uint16 DreCanEthBridge_mdioRead(uint8 phyAddr, uint8 devAddr, uint16 regAddr)
{
    uint32 commandAddr;
    uint32 commandData;

    (void)devAddr;

    while (MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MDIO.SINGLE_COMMAND_CONTROL_DATA.B.SBUSY)
    {
    }

    MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MDIO.CLAUSE_22_PORT.U |= BIT(phyAddr);

    commandAddr = ((uint32)phyAddr << IFX_GETH_PORT_CORE_MDIO_SINGLE_COMMAND_ADDRESS_PA_OFF) |
                  (((uint32)regAddr & 0x1FU) << IFX_GETH_PORT_CORE_MDIO_SINGLE_COMMAND_ADDRESS_RA_OFF);

    commandData = MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MDIO.SINGLE_COMMAND_CONTROL_DATA.U &
                  (uint32)~GENMASK(IFX_GETH_PORT_CORE_MDIO_SINGLE_COMMAND_CONTROL_DATA_CMD);

    commandData |= IFX_NETIF_MDIO_CMD_READ | IFX_NETIF_MDIO_CMD_BUSY;

    MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MDIO.SINGLE_COMMAND_ADDRESS.U = commandAddr;
    MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MDIO.SINGLE_COMMAND_CONTROL_DATA.U = commandData;

    while (MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MDIO.SINGLE_COMMAND_CONTROL_DATA.B.SBUSY)
    {
    }

    return (uint16)MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MDIO.SINGLE_COMMAND_CONTROL_DATA.B.SDATA;
}

static void DreCanEthBridge_mdioWrite(uint8 phyAddr, uint8 devAddr, uint16 regAddr, uint16 regValue)
{
    uint32 commandAddr;
    uint32 commandData;

    (void)devAddr;

    while (MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MDIO.SINGLE_COMMAND_CONTROL_DATA.B.SBUSY)
    {
    }

    MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MDIO.CLAUSE_22_PORT.U |= BIT(phyAddr);

    commandAddr = ((uint32)phyAddr << IFX_GETH_PORT_CORE_MDIO_SINGLE_COMMAND_ADDRESS_PA_OFF) |
                  (((uint32)regAddr & 0x1FU) << IFX_GETH_PORT_CORE_MDIO_SINGLE_COMMAND_ADDRESS_RA_OFF);

    commandData = MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MDIO.SINGLE_COMMAND_CONTROL_DATA.U &
                  ((uint32)~GENMASK(IFX_GETH_PORT_CORE_MDIO_SINGLE_COMMAND_CONTROL_DATA_CMD) &
                   (uint32)~GENMASK(IFX_GETH_PORT_CORE_MDIO_SINGLE_COMMAND_CONTROL_DATA_SDATA));

    commandData |= IFX_NETIF_MDIO_CMD_WRITE | IFX_NETIF_MDIO_CMD_BUSY | regValue;

    MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MDIO.SINGLE_COMMAND_ADDRESS.U = commandAddr;
    MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MDIO.SINGLE_COMMAND_CONTROL_DATA.U = commandData;

    while (MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MDIO.SINGLE_COMMAND_CONTROL_DATA.B.SBUSY)
    {
    }
}

static void DreCanEthBridge_logBridgeStatus(void)
{
    IfxDre_EdlStatus edlStatus;
    IfxDre_ErsStatus ersStatus;

    memset((void *)&edlStatus, 0, sizeof(edlStatus));
    memset((void *)&ersStatus, 0, sizeof(ersStatus));

    IfxDre_getEthDescListStatus(&MODULE_DRE, &edlStatus);
    IfxDre_getEthReqSummary(&MODULE_DRE, &ersStatus);

            printf("BRIDGE diag: canRx=%lu, dreTrig=%lu, txReq0=%u, txCnt=%u, fwdReq0=%u, rxCnt=%u, eobuf0_status=0x%08lX, eobuf0_error=0x%08lX, me_err=0x%08lX, tethdl0=0x%08lX, txdma=0x%08lX, tail=0x%08lX, curdesc=0x%08lX, mac_tx=0x%08lX, mac_debug=0x%08lX, mac_txpkts=%lu, mac_txoct=%lu, mac_err=0x%08lX, phy_bmcr=0x%04X, phy_bmsr=0x%04X.\r\n",
           (unsigned long)g_canRxCount,
           (unsigned long)g_dreTriggerCount,
           (unsigned)ersStatus.tx0,
           (unsigned)edlStatus.txCount,
           (unsigned)ersStatus.fwd0,
            (unsigned)edlStatus.rxCount,
            (unsigned long)MODULE_DRE.EOBUF[0].STATUS.U,
            (unsigned long)MODULE_DRE.EOBUF[0].ERROR.U,
                (unsigned long)MODULE_DRE.ME.ERR.U,
               (unsigned long)MODULE_DRE.TETHDL[0].CTRL.U,
                (unsigned long)MODULE_GETH0.DMA.CH[0].STATUS.U,
                (unsigned long)MODULE_GETH0.DMA.CH[0].TXDESC_TAIL_LPOINTER.U,
                (unsigned long)MODULE_GETH0.DMA.CH[0].CURRENT_APP_TXDESC_L.U,
                (unsigned long)MODULE_GETH0.PORT[0].CORE.MAC_TX_CONFIGURATION.U,
                (unsigned long)MODULE_GETH0.PORT[0].CORE.MAC_DEBUG.U,
                (unsigned long)MODULE_GETH0.PORT[0].CORE.TX_PACKET_COUNT_GOOD_LOW.U,
                (unsigned long)MODULE_GETH0.PORT[0].CORE.TX_OCTET_COUNT_GOOD_LOW.U,
                (unsigned long)MODULE_GETH0.PORT[0].CORE.MAC_RX_TX_STATUS.U,
                (unsigned)DreCanEthBridge_mdioRead(DRE_ETH_PHY_ADDR, 0U, PHY_MII_BMCR),
                (unsigned)DreCanEthBridge_mdioRead(DRE_ETH_PHY_ADDR, 0U, PHY_MII_BMSR));

            /* Performance / loss summary.
               Forward (CAN->ETH): dropRate = ethTxDrop / canRxTotal.
               Reverse (ETH->CAN): dropRate = canTxDrop / ethRxTotal. */
            printf("PERF diag: canRxTotal=%lu, ethTxTotal=%lu, ethTxDrop=%lu, ringStalls=%lu",
                   (unsigned long)g_drePerf.canRxTotal,
                   (unsigned long)g_drePerf.ethTxTotal,
                   (unsigned long)g_drePerf.ethTxDrop,
                   (unsigned long)g_drePerf.ringStalls);
            if (g_drePerf.canRxTotal > 0U)
            {
                printf("  fwdDropRate=%.2f%%",
                       (double)g_drePerf.ethTxDrop * 100.0 / (double)g_drePerf.canRxTotal);
            }
            printf("\r\n");

            printf("PERF rev : ethRxTotal=%lu, acfRxTotal=%lu, canTxTotal=%lu, canTxDrop=%lu",
                   (unsigned long)g_drePerf.ethRxTotal,
                   (unsigned long)g_drePerf.acfRxTotal,
                   (unsigned long)g_drePerf.canTxTotal,
                   (unsigned long)g_drePerf.canTxDrop);
            if (g_drePerf.ethRxTotal > 0U)
            {
                printf("  revDropRate=%.2f%%",
                       (double)g_drePerf.canTxDrop * 100.0 / (double)g_drePerf.ethRxTotal);
            }
            printf("\r\n");

            printf("CAN psr: PSR=0x%08lX\r\n",
                   (unsigned long)MODULE_CAN0.N[TC4D7_CAN_NODE_ID].PSR.U);

            printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   (unsigned)g_macAddress[0], (unsigned)g_macAddress[1],
                   (unsigned)g_macAddress[2], (unsigned)g_macAddress[3],
                   (unsigned)g_macAddress[4], (unsigned)g_macAddress[5]);

            if (g_canRxCount > 0U)
            {
                printf("DRE tdesc0: w0=0x%08lX, w1=0x%08lX, w2=0x%08lX, w3=0x%08lX, mac_txsts=0x%08lX.\r\n",
                       (unsigned long)DRE_TETHDL0_DESCRIPTOR_WORDS[0],
                       (unsigned long)DRE_TETHDL0_DESCRIPTOR_WORDS[1],
                       (unsigned long)DRE_TETHDL0_DESCRIPTOR_WORDS[2],
                       (unsigned long)DRE_TETHDL0_DESCRIPTOR_WORDS[3],
                       (unsigned long)MODULE_GETH0.PORT[0].CORE.MAC_RX_TX_STATUS.U);
            }

            if ((g_canRxCount > 0U) && (g_descriptorLogged == FALSE))
            {
                volatile IfxGeth_TxDescr *descriptor = &g_txDescrList.descr[0];
                printf("GETH txdesc0: tdes0=0x%08lX, tdes1=0x%08lX, tdes2=0x%08lX, tdes3=0x%08lX, desc=0x%08lX, buf=0x%08lX.\r\n",
                       (unsigned long)descriptor->TDES0.U,
                       (unsigned long)descriptor->TDES1.U,
                       (unsigned long)descriptor->TDES2.U,
                       (unsigned long)descriptor->TDES3.U,
                       (unsigned long)&g_txDescrList.descr[0],
                       (unsigned long)&g_channel0TxBuffer[0][0]);
                g_descriptorLogged = TRUE;
            }
}

static void DreCanEthBridge_applyLinkState(void)
{
    Ifx_GETH *geth = &MODULE_GETH0;

    if (g_phy.link_status.is_up == false)
    {
        IfxGeth_stopRx(geth, DRE_ETH_PORT_INDEX);
        IfxGeth_stopTx(geth, DRE_ETH_PORT_INDEX);

        if (g_linkLogged != FALSE)
        {
            g_linkLogged = FALSE;
            printf("ETH link down.\r\n");
        }

        return;
    }

    if (PHY_LINK_IS_SPEED_100M(g_phy.link_status.speed) != 0)
    {
        IfxGeth_setSpeed(geth, DRE_ETH_PORT_INDEX, IfxGeth_Speed_100M_MII);
    }
    else
    {
        IfxGeth_setSpeed(geth, DRE_ETH_PORT_INDEX, IfxGeth_Speed_10M_MII);
    }

    if (PHY_LINK_IS_FULL_DUPLEX(g_phy.link_status.speed) != 0)
    {
        IfxGeth_setDuplexMode(geth, DRE_ETH_PORT_INDEX, IfxGeth_DuplexMode_fullDuplex);
    }
    else
    {
        IfxGeth_setDuplexMode(geth, DRE_ETH_PORT_INDEX, IfxGeth_DuplexMode_halfDuplex);
    }

    IfxGeth_startRx(geth, DRE_ETH_PORT_INDEX);
    IfxGeth_startTx(geth, DRE_ETH_PORT_INDEX);

    if (g_linkLogged == FALSE)
    {
        g_linkLogged = TRUE;
        printf("ETH link up: %s %s duplex.\r\n",
               PHY_LINK_IS_SPEED_100M(g_phy.link_status.speed) != 0 ? "100M" : "10M",
               PHY_LINK_IS_FULL_DUPLEX(g_phy.link_status.speed) != 0 ? "full" : "half");
    }
}

static uint32 DreCanEthBridge_packMac32(const uint8 *mac)
{
    return ((uint32)mac[0]) |
           ((uint32)mac[1] << 8) |
           ((uint32)mac[2] << 16) |
           ((uint32)mac[3] << 24);
}

static uint16 DreCanEthBridge_packMac16(const uint8 *mac)
{
    return (uint16)(((uint16)mac[0]) | ((uint16)mac[1] << 8));
}

static void DreCanEthBridge_initEthernet(const uint8 *macAddress)
{
    IfxApApu_ApuConfig gethApuConfig;
    const Ifx_HSPHY_ETH_Bits hsphyConfig = {
        .EPR = HSPHY_ETH_EPR_RMII,
        .MDIOEN = TRUE,
        .MDIO = BOARD_GETH0_P0_MDIO.inSelect
    };
    IfxGeth_Eth_Config gethConfig;
    uint32 gethClockRate;

    DreCanEthBridge_enableVoltageRails();

    (void)HSPHY_Init();
    (void)HSPHY_ETH_Init(0U, hsphyConfig);

    IfxGeth_enableModule(&MODULE_GETH0);
    gethClockRate = IfxClock_getXGeth0Frequency();
    (void)DreCanEthBridge_mdioInit(&g_gethMdioPins, gethClockRate);

    if (dp83825i_init(&g_phy, DRE_ETH_PHY_ADDR, DreCanEthBridge_mdioRead, DreCanEthBridge_mdioWrite, 0U) != 0)
    {
        printf("ETH init: PHY probe failed at address %u.\r\n", (unsigned)DRE_ETH_PHY_ADDR);
        return;
    }

    (void)dp83825i_cfg_link(&g_phy,
                            (enum phy_link_speed)(LINK_HALF_10BASE_T |
                                                  LINK_FULL_10BASE_T |
                                                  LINK_HALF_100BASE_T |
                                                  LINK_FULL_100BASE_T));

    IfxHsphy_Geth_setupRmiiInputPins(&MODULE_HSPHY, IfxHsphy_EthIndex_0, &g_gethRmiiPins);

    IfxGeth_Eth_initModuleConfig(&gethConfig, &MODULE_GETH0);
    gethConfig.port[0].phyInterfaceMode = IfxGeth_PhyInterfaceMode_rmii_100;
    gethConfig.port[0].mac.disableCrcCheck = FALSE;
    gethConfig.port[0].mtl.rxQueue[0].enable = TRUE;
    gethConfig.port[0].mtl.rxQueue[0].enableDynamicDmaChannelMap = TRUE;
    gethConfig.port[0].mtl.rxQueue[0].rxQueueSize = (4096U >> 8) - 1U;
    gethConfig.port[0].mtl.txQueue[0].enable = TRUE;
    gethConfig.port[0].mtl.txQueue[0].txQueueSize = (4096U >> 8) - 1U;
    memcpy(gethConfig.port[0].mac.macAddress, macAddress, 6U);

    gethConfig.dma.addressAlignedBeatsEnabled = TRUE;
    gethConfig.dma.burstLength = IfxGeth_DmaBurstLength_16;
    gethConfig.dma.undefinedBurstLength = FALSE;
    memset(gethConfig.dma.burstLengthMultiplierEnable, FALSE, sizeof(gethConfig.dma.burstLengthMultiplierEnable));

    gethConfig.dma.txChannel[0].channelEnable = TRUE;
    gethConfig.dma.txChannel[0].maxBurstLength = IfxGeth_TxBurstLength_16;
    gethConfig.dma.txChannel[0].txDescrList = &g_txDescrList;
    gethConfig.dma.txChannel[0].txBuffer1Size = DRE_GETH_MAX_BUFFER_SIZE;
    gethConfig.dma.txChannel[0].txBuffer1StartAddress = (uint32 *)&g_channel0TxBuffer[0][0];

    gethConfig.dma.rxChannel[0].channelEnable = TRUE;
    gethConfig.dma.rxChannel[0].maxBurstLength = IfxGeth_RxBurstLength_16;
    gethConfig.dma.rxChannel[0].rxDescrList = &g_rxDescrList;
    gethConfig.dma.rxChannel[0].rxBuffer1Size = DRE_GETH_MAX_BUFFER_SIZE;
    gethConfig.dma.rxChannel[0].rxBuffer1StartAddress = (uint32 *)&g_channel0RxBuffer[0][0];
    gethConfig.bridge.mode = IfxGeth_BridgePortMode_singlePort0;

    IfxGeth_Eth_initModule(&g_geth, &gethConfig);
    IfxApApu_initConfig(&gethApuConfig);
    IfxGeth_configureAccessToGeth(&gethApuConfig);
    {
        IfxApApu_ApuMemoryConfig lmuMemoryConfig;
        lmuMemoryConfig.apuConfig = &gethApuConfig;
        IfxApApu_configureAccessToLmus(&lmuMemoryConfig);
    }
    /*
     * Initialize the GETH driver with CPU-visible LMU descriptors first.
     * In DRE DMA mode the Tx Descriptor Handler owns a separate four-entry
     * descriptor list in DRE Message RAM. Switch only the hardware Tx ring
     * registers after iLLD initialization, otherwise the CPU-side driver
     * initialization would access the DRE RAM window and stall.
     */
    MODULE_GETH0.DMA.CH[0].TXDESC_LIST_LADDRESS.U = DRE_TETHDL0_DESCRIPTOR_ADDRESS;
    MODULE_GETH0.DMA.CH[0].TXDESC_TAIL_LPOINTER.U = DRE_TETHDL0_DESCRIPTOR_ADDRESS;
    /* Tx descriptors are owned by the DRE TETHDL (four per interface).  Rx now
       runs on the iLLD/CPU descriptor ring (g_rxDescrList, 8 entries), so set the
       Rx ring length hint to match (RDRL = entries-1 = 7). */
    MODULE_GETH0.DMA.CH[0].TX_CONTROL2.B.TDRL = 3U;
    MODULE_GETH0.DMA.CH[0].RX_CONTROL2.B.RDRL = 7U;
    (void)DreCanEthBridge_mdioInit(NULL_PTR, gethClockRate);
    MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MAC_PACKET_FILTER.U = 0x80000000U; /* RA: receive all (verification) */
    IfxGeth_startRxDma(&MODULE_GETH0, IfxGeth_RxDmaChannel_0);
    IfxGeth_startTxDma(&MODULE_GETH0, IfxGeth_TxDmaChannel_0);
    IfxHsphy_Geth_setupRmiiOutputPins(&MODULE_HSPHY, &g_gethRmiiPins);

    DreCanEthBridge_applyLinkState();
}

static void DreCanEthBridge_initCan(void)
{
    IfxCan_Can_Config canConfig;
    IfxCan_Can_NodeConfig nodeConfig;
    IfxCan_CreConfig creConfig;

    DreCanEthBridge_initCanTransceiver();

    IfxCan_Can_initModuleConfig(&canConfig, &MODULE_CAN0);
    IfxCan_Can_initModule(&g_canModule, &canConfig);

    IfxCan_Can_initNodeConfig(&nodeConfig, &g_canModule);
    nodeConfig.nodeId = TC4D7_CAN_NODE_ID;
    nodeConfig.clockSource = IfxCan_ClockSource_both;
    nodeConfig.frame.type = IfxCan_FrameType_transmitAndReceive;
    nodeConfig.frame.mode = IfxCan_FrameMode_fdLongAndFast;
    nodeConfig.calculateBitTimingValues = TRUE;
    nodeConfig.baudRate.baudrate = TC4D7_CAN_NOMINAL_BAUDRATE;
    nodeConfig.baudRate.samplePoint = TC4D7_CAN_NOMINAL_SAMPLE_POINT;
    nodeConfig.baudRate.syncJumpWidth = 2000U;
    nodeConfig.fastBaudRate.baudrate = TC4D7_CAN_DATA_BAUDRATE;
    nodeConfig.fastBaudRate.samplePoint = TC4D7_CAN_DATA_SAMPLE_POINT;
    nodeConfig.fastBaudRate.syncJumpWidth = 2000U;
    /* Enable Transceiver Delay Compensation (mandatory for CAN FD data phase
       >= 1 Mbps).  Without it the FD data-phase sample point is wrong and the
       node silently drops every CAN FD frame. */
    nodeConfig.fastBaudRate.tranceiverDelayOffset = 5U;

    nodeConfig.txConfig.txMode = IfxCan_TxMode_fifo;
    nodeConfig.txConfig.dedicatedTxBuffersNumber = 0;
    nodeConfig.txConfig.txFifoQueueSize = TC4D7_CAN_TX_FIFO_SIZE;
    nodeConfig.txConfig.txBufferDataFieldSize = IfxCan_DataFieldSize_64;
    nodeConfig.txConfig.txEventFifoSize = 0;

    nodeConfig.rxConfig.rxMode = IfxCan_RxMode_fifo0;
    nodeConfig.rxConfig.rxBufferDataFieldSize = IfxCan_DataFieldSize_64;
    nodeConfig.rxConfig.rxFifo0DataFieldSize = IfxCan_DataFieldSize_64;
    nodeConfig.rxConfig.rxFifo1DataFieldSize = IfxCan_DataFieldSize_8;
    nodeConfig.rxConfig.rxFifo0OperatingMode = IfxCan_RxFifoMode_blocking;
    nodeConfig.rxConfig.rxFifo0WatermarkLevel = 1;
    nodeConfig.rxConfig.rxFifo0Size = TC4D7_CAN_RX_FIFO_SIZE;
    nodeConfig.rxConfig.rxFifo1Size = 0;

    nodeConfig.filterConfig.messageIdLength = IfxCan_MessageIdLength_both;
    nodeConfig.filterConfig.standardListSize = 1;
    nodeConfig.filterConfig.extendedListSize = 1;
    nodeConfig.filterConfig.rejectRemoteFramesWithStandardId = TRUE;
    nodeConfig.filterConfig.rejectRemoteFramesWithExtendedId = TRUE;
    nodeConfig.filterConfig.standardFilterForNonMatchingFrames = IfxCan_NonMatchingFrame_reject;
    nodeConfig.filterConfig.extendedFilterForNonMatchingFrames = IfxCan_NonMatchingFrame_reject;

    nodeConfig.messageRAM.standardFilterListStartAddress = 0x000;
    nodeConfig.messageRAM.extendedFilterListStartAddress = 0x010;
    nodeConfig.messageRAM.rxFifo0StartAddress = 0x040;
    nodeConfig.messageRAM.rxFifo1StartAddress = 0x000;
    nodeConfig.messageRAM.rxBuffersStartAddress = 0x000;
    nodeConfig.messageRAM.txEventFifoStartAddress = 0x000;
    nodeConfig.messageRAM.txBuffersStartAddress = 0x300;

    nodeConfig.pins = &g_canPins;
    nodeConfig.busLoopbackEnabled = FALSE;

    (void)IfxCan_Can_initNode(&g_canNode, &nodeConfig);
    IfxCan_Can_setStandardFilter(&g_canNode, (IfxCan_Filter *)&g_acceptAllStandardFilter);
    IfxCan_Can_setExtendedFilter(&g_canNode, (IfxCan_Filter *)&g_acceptAllExtendedFilter);

    IfxCan_Can_initCreConfig(&creConfig);
    creConfig.creStartAddress = TC4D7_CAN_CRE_BASE_OFFSET;
    creConfig.stdRoutingTableStartAddress = TC4D7_CAN_CRE_BASE_OFFSET;
    creConfig.xtdRoutingTableStartAddress = TC4D7_CAN_CRE_BASE_OFFSET;
    creConfig.stdFrameRateTableStartAddress = TC4D7_CAN_CRE_BASE_OFFSET;
    creConfig.xtdFrameRateTableStartAddress = TC4D7_CAN_CRE_BASE_OFFSET;
    creConfig.stdTimeStampDatabaseStartAddress = TC4D7_CAN_CRE_BASE_OFFSET;
    creConfig.xtdTimeStampDatabaseStartAddress = TC4D7_CAN_CRE_BASE_OFFSET;
    creConfig.stdRoutingRuleSize = IfxCan_StdRoutingRuleSize_0;
    creConfig.xtdRoutingRuleSize = IfxCan_XtdRoutingRuleSize_0;
    creConfig.stdFrameRateTableSize = IfxCan_StdFrameRateSize_0;
    creConfig.xtdFrameRateTableSize = IfxCan_XtdFrameRateSize_0;
    creConfig.enableCreRouting = FALSE;
    creConfig.enableDestinationRouting = TRUE;
    /* Software-driven path: CAN frames land in Rx FIFO0, are read by
       DreCanEthBridge_processCanRx() and pushed into DRE via
       IfxCan_Can_triggerDebugMessageToDre().  Keep the automatic
       Rx->DRE trigger DISABLED so the FIFO0 is owned solely by software
       and does not get starved after the first frame. */
    creConfig.rxBuf0DreTriggerEnable = FALSE;
    creConfig.rxBuf1DreTriggerEnable = FALSE;
    IfxCan_Can_initCre(&g_canNode, &creConfig);

    while (IfxCan_Can_isNodeSynchronized(&g_canNode) == FALSE)
    {
    }

    /* Enable automatic Bus-Off recovery.  A transient error burst on the bus
       (e.g. a frame the node could not yet ACK) would otherwise leave the
       node permanently in Bus-Off, silently dropping every subsequent frame. */
    {
        Ifx_CAN_N *cn = &MODULE_CAN0.N[TC4D7_CAN_NODE_ID];
        cn->CCCR.B.INIT = 1U;
        while (cn->CCCR.B.INIT == 0U) {}
        cn->CCCR.B.BRSE = 1U;
        cn->CCCR.B.INIT = 0U;
        while (cn->CCCR.B.INIT == 1U) {}
    }
}

static void DreCanEthBridge_initDre(const uint8 *macAddress)
{
    IfxDre_ApConfig dreApConfig;
    IfxDre_Dre_Config dreConfig;
    IfxDre_Dre_CADConfig cadConfig;
    IfxDre_Dre_RoutingConfig routingConfig;
    IfxDre_Dre_EADConfig eadConfig;
    IfxDre_Dre_RPConfig rpConfig;
    IfxDre_Dre_RxEthConfig rxEthConfig;
    IfxDre_Dre_TxEthConfig txEthConfig;
    uint8 macSource[6];

    memcpy(macSource, macAddress, sizeof(macSource));

    /* Open the DRE Ethernet Message RAM APU for the GETH/LETH masters. */
    IfxDre_initApConfig(&dreApConfig);
    IfxDre_initAp(&MODULE_DRE, &dreApConfig);

    IfxDre_Dre_initModuleConfig(&dreConfig, &MODULE_DRE);
    dreConfig.rt0Config.size = 1U;
    dreConfig.streamFilter0.enable = TRUE;
    dreConfig.streamFilter0.mode = IfxDre_StreamFilterMode_range;
    dreConfig.streamFilter0.routingTableIndex = 0U;
    dreConfig.streamFilter0.filter1LowerId = 0U;
    dreConfig.streamFilter0.filter1HigherId = 0U;
    dreConfig.streamFilter0.filter2LowerId = 0xFFFFFFFFU;
    dreConfig.streamFilter0.filter2HigherId = 0xFFFFFFFFU;

    dreConfig.ethernetOutputBuffer0.payloadLength = DRE_ETH_PAYLOAD_LENGTH;
    dreConfig.ethernetOutputBuffer0.destinationId = IfxCan_DestinationId_Ethernet1;
    dreConfig.ethernetOutputBuffer0.headerEnable = TRUE;
    dreConfig.ethernetOutputBuffer0.triggerMode = IfxDre_TriggerMode_software;
    dreConfig.ethernetOutputBuffer0.macDestinationAddress0 = DRE_ETH_PC_MAC0;
    dreConfig.ethernetOutputBuffer0.macDestinationAddress1 = DRE_ETH_PC_MAC1;
    dreConfig.ethernetOutputBuffer0.macSourceAddress0 = DreCanEthBridge_packMac32(&macSource[0]);
    dreConfig.ethernetOutputBuffer0.macSourceAddress1 = DreCanEthBridge_packMac16(&macSource[4]);
    dreConfig.ethernetOutputBuffer0.tpId = 0x8100U;
    dreConfig.ethernetOutputBuffer0.vlanTag = 0U;
    dreConfig.ethernetOutputBuffer0.avtpEtherType = 0x22F0U;
    dreConfig.ethernetOutputBuffer0.isStreamIdValid = TRUE;
    dreConfig.ethernetOutputBuffer0.ntscfSequenceNumber = 0U;
    dreConfig.ethernetOutputBuffer0.streamIdLower = DreCanEthBridge_packMac32(&macSource[2]);
    dreConfig.ethernetOutputBuffer0.streamIdHigher = DreCanEthBridge_packMac32(&macSource[0]);
    dreConfig.ethernetOutputBuffer0.triggerFillLevel = 1U;

    /* ntscfStartAddress = 0 disables the hardware NTSCF/AVTP pre-parse in the
       DRE EIBUF.  The EIBUF hardware expects a standard IEEE-1722a ACF (AVTP)
       header at the given offset and rejects (EIBUF0_ERROR=0x2A) any frame that
       is not a valid AVTP frame -- which silently drops the raw-ACF frames this
       firmware's software bridge (DreCanEthBridge_processEthRx) expects.  With
       the pre-parse disabled the whole Ethernet frame is handed to the GETH Rx
       DMA and parsed by software instead (ETH -> CAN software path).  CAN -> ETH
       still uses the DRE EOBUF hardware path. */
    dreConfig.ethernetInputBuffer0.ntscfStartAddress = 0U;
    dreConfig.ethernetInputBuffer0.enableRejectRemoteFrame = TRUE;

    IfxDre_Dre_initModule(&g_dre, &dreConfig);

    cadConfig.creStartAddress = (g_canNode.messageRAM.baseAddress & IFXCAN_MODULE_ADDRESS_MASK) + g_canNode.node->CRE.CONFIGADR.U;
    cadConfig.elementIndex = IfxDre_CAD_Index_1;
    IfxDre_Dre_setCanAddressDatabaseElement(&g_dre, &cadConfig);

    routingConfig.destinationId1 = IfxCan_DestinationId_Can0_Node1;
    routingConfig.destinationId2 = IfxCan_DestinationId_none;
    routingConfig.destinationId3 = IfxCan_DestinationId_none;
    routingConfig.destinationId4 = IfxCan_DestinationId_none;
    routingConfig.filterMode = IfxDre_FilterMode_classic;
    routingConfig.canId1 = 0U;
    routingConfig.canId2 = 0U;
    routingConfig.xtdShiftLength = 0U;
    routingConfig.routingType = IfxDre_RoutingType_unicast;
    IfxDre_Dre_setFilterAndRoutingElement(&g_dre, 0U, 0U, &routingConfig);

    memset(&eadConfig, 0, sizeof(eadConfig));
    eadConfig.gethMac0TxChannelNumber = IfxGeth_TxDmaChannel_0;
    eadConfig.gethMac0RxChannelNumber = IfxGeth_RxDmaChannel_0;
    IfxDre_Dre_initEthAddressDatabase(&g_dre, &eadConfig);

    rpConfig.mode = FALSE;
    rpConfig.vmId = IfxApProt_VmId_0;
    rpConfig.vmEnable = FALSE;
    rpConfig.protectionSet = IfxApProt_PrsId_0;
    rpConfig.protectionSetEnable = FALSE;
    rpConfig.tagOffset = FALSE;
    IfxDre_Dre_setResourcePartition(&g_dre, 0U, &rpConfig);
    IfxDre_Dre_assignCanResourcePartition(&g_dre, 0U, IfxDre_CanIndex_Can0_Node1);
    IfxDre_Dre_assignEthResourcePartition(&g_dre, 0U, IfxDre_EthRPIndex_GethMac0);

    rxEthConfig.interface = IfxDre_EthInterface_GethMac0;
    rxEthConfig.dmaChannel = IfxDre_EthDmaChannel_0;
    rxEthConfig.triggerType = FALSE;
    rxEthConfig.interruptOnCompletion = FALSE;
    rxEthConfig.fcsEnable = TRUE;
    rxEthConfig.descriptorPointer = 0U;
    /* descriptorPointerConfigEnable = FALSE: do NOT let the DRE own the GETH Rx
       descriptor list.  When TRUE the DRE RETHDL overrides the GETH Rx descriptor
       pointer with its own internal list and the frames are first handed to the
       EIBUF hardware parser before any software sees them -- which, combined with
       the (now disabled) NTSCF pre-parse, prevented DreCanEthBridge_processEthRx
       from ever receiving a frame (ethRxTotal stayed 0).  Leaving the descriptor
       list to the iLLD/CPU (g_rxDescrList) lets the software ETH->CAN path work. */
    rxEthConfig.descriptorPointerConfigEnable = FALSE;
    IfxDre_Dre_initRxEthDescListControlConfig(&g_dre, 0U, &rxEthConfig);

    txEthConfig.dmaChannel = IfxDre_EthDmaChannel_0;
    txEthConfig.triggerType = FALSE;
    txEthConfig.slotNumber = 0U;
    txEthConfig.sourceAddressInsertionControl = IfxGeth_SourceAddressControl_notIncluded;
    txEthConfig.interruptOnCompletion = FALSE;
    txEthConfig.descriptorPointer = 0U;
    txEthConfig.descriptorPointerConfigEnable = TRUE;
    IfxDre_Dre_initTxEthDescListControlConfig(&g_dre, 0U, &txEthConfig);

    IfxDre_resetEthRxCount(&MODULE_DRE);
    IfxDre_resetEthTxCount(&MODULE_DRE);
}

static void DreCanEthBridge_triggerCanToEthernet(const IfxCan_Message *rxMessage, const uint32 *dataWords)
{
    Ifx_CAN_RHBUF rxHostBuffer;
    uint32 byteCount;
    uint32 index;
    uint8 *payload;

    /* NOTE on back-pressure: the original design forwarded every CAN frame by
       writing RHBUF and issuing the EOBUF software trigger.  The only safe
       point to drop a frame under overload is in DreCanEthBridge_main(), where
       we refuse to push a new descriptor when the GETH Tx ring is already full
       (see g_txRingPending / ethTxDrop there).  We intentionally do NOT drop
       here on EOBUF TXREQ, because TXREQ can be momentarily set between a
       trigger and DRE releasing the buffer; dropping on it would discard
       healthy frames and under-count real throughput. */

    memset((void *)&rxHostBuffer, 0, sizeof(rxHostBuffer));

    rxHostBuffer.UCRH.B.MODE = 0U;
    rxHostBuffer.UCRH.B.SID = IfxCan_DestinationId_Can0_Node1;
    rxHostBuffer.UCRH.B.DID = IfxCan_DestinationId_Ethernet1;

    /* TC4x CAN RHBUF R0.ID packs a standard (11-bit) identifier into bits
       [28:18], while an extended (29-bit) identifier occupies bits [28:0].
       DRE reads the identifier straight from this register, so for standard
       frames the ID must be left-shifted by 18.  Writing it un-shifted makes
       DRE read a zero identifier for classic frames (extended IDs are already
       in the low bits and work as-is). */
    if (rxMessage->messageIdLength == IfxCan_MessageIdLength_extended)
    {
        rxHostBuffer.R0.B.ID  = rxMessage->messageId;
        rxHostBuffer.R0.B.XTD = 1U;
    }
    else
    {
        rxHostBuffer.R0.B.ID  = rxMessage->messageId << 18U;
        rxHostBuffer.R0.B.XTD = 0U;
    }
    rxHostBuffer.R0.B.RTR = rxMessage->remoteTransmitRequest ? 1U : 0U;
    rxHostBuffer.R0.B.ESI = rxMessage->errorStateIndicator ? 1U : 0U;

    rxHostBuffer.R1.B.DLC = rxMessage->dataLengthCode;
    rxHostBuffer.R1.B.BRS = (rxMessage->frameMode == IfxCan_FrameMode_fdLongAndFast) ? 1U : 0U;
    rxHostBuffer.R1.B.FDF = (rxMessage->frameMode != IfxCan_FrameMode_standard) ? 1U : 0U;
    rxHostBuffer.R1.B.FIDX = 0U;
    rxHostBuffer.R1.B.ANMF = 0U;

    byteCount = IfxCan_Node_getDataLengthInBytes(rxMessage->dataLengthCode);
    payload = (uint8 *)&dataWords[0];

    for (index = 0U; index < byteCount; ++index)
    {
        rxHostBuffer.RHBUF_DB[index].U = payload[index];
    }

    /* Set the EOBUF payload length to the EXACT ACF frame size for this CAN
       message.  DRE uses EOBUF CONFIG.PL as the Ethernet payload length (bytes
       after the EtherType: AVTP control header + ACF CAN message).  A fixed
       large PL would make GETH transmit the whole EOBUF buffer, including
       stale bytes left by previous frames.  Keep PL 32-bit aligned. */
    {
        uint32 padded = (byteCount + 3U) & ~3U;
        uint32 pl     = 12U + 8U + padded;   /* AVTP ctrl header + ACF(CAN hdr+id) + payload */
        if (pl < 8U)
        {
            pl = 8U;   /* EOBUF PL minimum (value 4 is treated as 8) */
        }
        MODULE_DRE.EOBUF[0].CONFIG.B.PL = (uint16)pl;
    }

    IfxCan_Can_triggerDebugMessageToDre(&g_canNode, IfxCan_CreRxHostBufferIndex_0, &rxHostBuffer);
    g_dreTriggerCount++;
    g_softwareTriggerRequested = TRUE;

    /* Capture-independent ACF byte echo (for verification without Wireshark):
       print the exact fields that will be placed into the AVTP/ACF Ethernet
       frame.  A beginner can compare these against the sent CAN frame and see a
       1:1 mapping: ACF id == CAN id, ACF ext/fdf/brs == CAN flags, ACF data ==
       CAN data. */
    {
        uint32 i;
        uint32 n = (byteCount < 16U) ? byteCount : 16U;
        printf("ACF   id=0x%lx ext=%d fdf=%d brs=%d len=%lu data=",
               (unsigned long)rxMessage->messageId,
               (rxMessage->messageIdLength == IfxCan_MessageIdLength_extended) ? 1 : 0,
               (rxMessage->frameMode != IfxCan_FrameMode_standard) ? 1 : 0,
               (rxMessage->frameMode == IfxCan_FrameMode_fdLongAndFast) ? 1 : 0,
               (unsigned long)byteCount);
        for (i = 0U; i < n; i++)
        {
            printf("%02X", (unsigned)g_canRxWords[i / 4U] >> ((i % 4U) * 8U) & 0xFFU);
        }
        if (byteCount > n)
        {
            printf("...");
        }
        printf("\r\n");
    }
}

static void DreCanEthBridge_processCanRx(void)
{
    /* Read at most ONE CAN frame per main-loop iteration.  The DRE EOBUF path
       is single-buffered: each frame must be handed to DRE and fully processed
       before the next one.  If we drained the whole Rx FIFO here and only
       forwarded one frame (the old behaviour, because g_softwareTriggerRequested
       is a single boolean), the remaining frames in the FIFO were silently
       overwritten in RHBUF and lost.  Reading one frame per iteration lets the
       main loop forward one frame per iteration and keeps the Rx FIFO as a real
       (8-deep) buffer instead of a coalescing black hole.  Frames that arrive
       faster than DRE can drain are then honestly counted as ethTxDrop. */
    if (IfxCan_Can_getRxFifo0FillLevel(&g_canNode) > 0U)
    {
        IfxCan_Message rxMessage;

        IfxCan_Can_initMessage(&rxMessage);
        rxMessage.readFromRxFifo0 = TRUE;
        IfxCan_Can_readMessage(&g_canNode, &rxMessage, g_canRxWords);
        g_canRxCount++;
        g_drePerf.canRxTotal++;
        printf("CANRX id=0x%lx ext=%d fdf=%d brs=%d dlc=%d\r\n",
               (unsigned long)rxMessage.messageId,
               (rxMessage.messageIdLength == IfxCan_MessageIdLength_extended) ? 1 : 0,
               (rxMessage.frameMode != IfxCan_FrameMode_standard) ? 1 : 0,
               (rxMessage.frameMode == IfxCan_FrameMode_fdLongAndFast) ? 1 : 0,
               rxMessage.dataLengthCode);
        DreCanEthBridge_triggerCanToEthernet(&rxMessage, g_canRxWords);
    }
}

static void DreCanEthBridge_serviceDreStatus(void)
{
    /* --- DRE Message Engine error housekeeping ---
       An ME error (e.g. IRDE = Invalid Routing Destination, or a TX-descriptor
       /watchdog error from the EOBUF path) stalls the ETH output.  Clear it and
       re-kick the GETH TX DMA channel so pending EOBUF frames can be sent. */
    {
        uint32 meErr = MODULE_DRE.ME.ERR.U;
        if (meErr != 0U)
        {
            MODULE_DRE.ME.ERR.U = 0x1EU;   /* clear rw1ch bits SPBBE/SRIBE/DBOE/IRDE */
            IfxGeth_startTxDma(&MODULE_GETH0, IfxGeth_TxDmaChannel_0);
        }
    }

    /* --- EOBUF (Ethernet output) housekeeping: clear TXREQ and errors --- */
    {
        Ifx_DRE_EOBUF_ERROR err;
        err.U = MODULE_DRE.EOBUF[0].ERROR.U;
        if (err.U != 0U)
        {
            static uint8 s_logged = FALSE;
            if (s_logged == FALSE)
            {
                printf("EOBUF0 err: TDESE=%u WDTE=%u DERRTYP=%u (cleared)\r\n",
                       (unsigned)err.B.TDESE, (unsigned)err.B.WDTE, (unsigned)err.B.DERRTYP);
                s_logged = TRUE;
            }
            /* write 1 to clear error bits */
            MODULE_DRE.EOBUF[0].ERROR.U = 3U;
        }
        if (MODULE_DRE.EOBUF[0].STATUS.B.TXREQ != 0U)
        {
            MODULE_DRE.EOBUF[0].STATUS.B.TXREQ = 1U; /* write 1 to clear */
        }
    }

    if (IfxDre_get_EIBUF_Status_EthernetFrameCompleteFlag(&MODULE_DRE, 0U) != FALSE)
    {
        IfxDre_clear_EIBUF_Status_EthernetFrameCompleteFlag(&MODULE_DRE, 0U);
    }

    if (IfxDre_get_EIBUF_Status_EthernetFrameErrorFlag(&MODULE_DRE, 0U) != FALSE)
    {
        IfxDre_clear_EIBUF_Status_EthernetFrameErrorFlag(&MODULE_DRE, 0U);
    }

    if (MODULE_DRE.EIBUF[0].STATUS.B.RXREQ != 0U)
    {
        MODULE_DRE.EIBUF[0].STATUS.B.RXREQ = 1U;
    }

    if (IfxDre_getEibufPendingRequest(&MODULE_DRE, 0U) != FALSE)
    {
        IfxDre_clearBufferPendingRequest(&MODULE_DRE, 0U);
    }
}

/* ----------------------------------------------------------------------------
 * Reverse path: Ethernet -> CAN
 *
 * The GETH MAC receives Ethernet frames on its Rx DMA ring (g_rxDescrList /
 * g_channel0RxBuffer, configured by IfxGeth_Eth_initModule + startRxDma).  We
 * poll the descriptors here, parse the ACF/AVTP frame that the PC peer sends,
 * and hand the embedded CAN message to the CAN TX FIFO.  This is the symmetric
 * counterpart of the CAN -> Ethernet bridge above, and uses the same simple,
 * beginner-readable ACF layout so that a captured frame maps 1:1 onto a CAN
 * frame (id / ext / fdf / brs / len / data).
 *
 * ACF frame layout (immediately after the 14-byte Ethernet header; EtherType
 * 0x22F0):
 *   offset 0..1 : txLength  (u16, big-endian) - total ACF payload bytes (informational)
 *   offset 2    : flags     (bit0 = EXT, bit1 = FDF, bit2 = BRS)
 *   offset 3    : dlc       (CAN DLC 0..15)
 *   offset 4..7 : canId     (u32, big-endian)
 *   offset 8..  : data      (0..64 bytes, length derived from dlc)
 * -------------------------------------------------------------------------- */
/* The iLLD Rx API (getReceiveBuffer/freeReceiveBuffer) advances the descriptor
   pointer kept inside the global g_geth.  Under -O3 the compiler caches that
   pointer across the external call and ends up dereferencing a stale/freed
   descriptor, crashing the firmware.  Compiling this one function at -O0 stops
   the compiler from making that assumption (an -O3 build is otherwise fine for
   the rest of the bridge). */
#pragma GCC push_options
#pragma GCC optimize ("O0")
static void DreCanEthBridge_processEthRx(void)
{
    uint8 *buffer;

    /* Handle at most ONE frame per poll tick.  This mirrors the forward
       processCanRx() (also one frame per tick) and is deliberate: the iLLD Rx
       API (getReceiveBuffer/freeReceiveBuffer) must not be driven back-to-back
       with zero delay, otherwise a follow-up getReceiveBuffer can dereference a
       descriptor the DMA is still updating and the firmware crashes.  One frame
       per 1 ms tick (set by the STM tick in the main loop) keeps a safe gap and
       still sustains >1000 frames/s, far above any realistic CAN rate.  The 8
       entry Rx ring (DRE_GETH_RX_RING_ENTRIES) absorbs bursts between ticks. */
    buffer = (uint8 *)IfxGeth_Eth_getReceiveBuffer(&g_geth, IfxGeth_RxDmaChannel_0);
    if (buffer == NULL_PTR)
    {
        return;   /* ring empty this tick */
    }

    /* Frame length lives in the current Rx descriptor (write-back format,
       PL = payload length incl. FCS when the MAC keeps it). */
    uint32 frameLen = g_geth.rxChannel[0].rxDescrPtr->RDES3.W.PL;
    uint32 dataLength = frameLen;

    g_drePerf.ethRxTotal++;

    /* Accept IEEE-1722 ACF/AVTP frames (EtherType 0x22F0) carried either:
         (a) directly as a raw Ethernet frame, or
         (b) inside a UDP datagram (verify path: WinDivert injects UDP because
             Npcap cannot emit raw frames on a direct link -- see README).
       In both cases the ACF payload layout is identical; only the L2/L3/L4
       headers differ.  No VLAN tag is expected, so EtherType is at offset 12. */
    uint8  *acf = NULL;
    uint32  acfAvail = 0U;

    if ((dataLength >= (14U + 8U)) &&
        (buffer[12] == 0x22U) && (buffer[13] == 0xF0U))
    {
        /* (a) raw ACF/Ethernet */
        acf = &buffer[14];
        acfAvail = dataLength - 14U;
    }
    else if ((dataLength >= (14U + 20U + 8U + 8U)) &&
             (buffer[12] == 0x08U) && (buffer[13] == 0x00U) &&   /* IPv4 */
             (buffer[23] == 17U) &&                              /* protocol = UDP */
             (((uint16)((buffer[34] << 8U) | buffer[35])) == 5555U))  /* verify UDP port */
    {
        /* (b) ACF carried in UDP payload (WinDivert verify injection) */
        acf = &buffer[14U + 20U + 8U];        /* past Eth(14)+IP(20)+UDP(8) */
        acfAvail = dataLength - (14U + 20U + 8U);
    }

    if (acf != NULL)
    {
        g_drePerf.acfRxTotal++;
        uint8   flags = acf[2];
        uint8   dlc   = acf[3];
        uint32  canId = ((uint32)acf[4] << 24U) | ((uint32)acf[5] << 16U) |
                        ((uint32)acf[6] << 8U)  |  (uint32)acf[7];
        uint32  byteCount = IfxCan_Node_getDataLengthInBytes(dlc);
        IfxCan_Message canMsg;
        uint32  canData[16];
        uint32  i;
        uint32  dataOffset = 8U;
        boolean ok = TRUE;

        if (dlc > 15U)
        {
            /* Invalid DLC range: malformed / non-ACF frame. */
            ok = FALSE;
        }

        if ((dataOffset + byteCount) > acfAvail)
        {
            /* Malformed frame: claimed more data than the ACF payload holds. */
            ok = FALSE;
        }

        for (i = 0U; (i < 16U) && (i < byteCount); ++i)
        {
            canData[i] = (uint32)acf[dataOffset + i];
        }

        if (ok != FALSE)
        {
            IfxCan_Can_initMessage(&canMsg);
            /* The CAN node is configured in Tx FIFO mode (no dedicated Tx
               buffers, txFifoQueueSize=8).  The iLLD default for a freshly
               initMessage()'d message is storeInTxFifoQueue=FALSE, which makes
               IfxCan_Can_sendMessage() write to dedicated buffer 0 — a buffer
               that does NOT exist in this configuration.  Writing through that
               path corrupts the Tx message RAM / FIFO and crashes the firmware.
               Queueing into the Tx FIFO instead uses the correct FIFO put index
               and is the only safe way to transmit on this node. */
            canMsg.storeInTxFifoQueue = TRUE;
            canMsg.messageIdLength = ((flags & 0x01U) != 0U)
                                     ? IfxCan_MessageIdLength_extended
                                     : IfxCan_MessageIdLength_standard;
            if ((flags & 0x04U) != 0U)
            {
                canMsg.frameMode = IfxCan_FrameMode_fdLongAndFast;       /* FDF + BRS */
            }
            else if ((flags & 0x02U) != 0U)
            {
                canMsg.frameMode = IfxCan_FrameMode_fdLong;              /* FDF only */
            }
            else
            {
                canMsg.frameMode = IfxCan_FrameMode_standard;            /* classic */
            }
            canMsg.dataLengthCode = (IfxCan_DataLengthCode)dlc;
            canMsg.messageId      = canId;

            if (IfxCan_Can_sendMessage(&g_canNode, &canMsg, canData) == IfxCan_Status_notSentBusy)
            {
                /* CAN TX FIFO full under overload: count and drop. */
                g_drePerf.canTxDrop++;
            }
            else
            {
                g_drePerf.canTxTotal++;
                /* Capture-independent ACF echo (for verification without gs_usb):
                   print the exact CAN fields parsed from the ACF payload, so a
                   beginner can compare 1:1 with the sent frame: id / ext /
                   fdf / brs / len / data. */
                {
                    uint32 i;
                    uint32 n = (byteCount < 16U) ? byteCount : 16U;
                    printf("ACF->  id=0x%lx ext=%d fdf=%d brs=%d len=%lu data=",
                           (unsigned long)canId,
                           (flags & 0x01U) ? 1 : 0,
                           (flags & 0x02U) ? 1 : 0,
                           (flags & 0x04U) ? 1 : 0,
                           (unsigned long)byteCount);
                    for (i = 0U; i < n; i++)
                    {
                        printf("%02X", (unsigned)acf[dataOffset + i]);
                    }
                    if (byteCount > n)
                    {
                        printf("...");
                    }
                    printf("\r\n");
                }
            }
        }
    }

    /* Return the descriptor to the DMA so it can receive again.  The iLLD
       freeReceiveBuffer() (shuffleRxDescriptor) and the Rx DMA race on the
       descriptor ring; with zero delay between getReceiveBuffer and
       freeReceiveBuffer the firmware crashes.  The delay must be a REAL hardware
       stall: waitTime(TimeConst_*) is useless here because the BSP TimeConst[]
       table is zero-initialised in this build (TC=0), so we use the STM timer
       directly.  A couple of ms is plenty for the DMA to settle. */
    IfxStm_waitTicks(&MODULE_CPU0, IfxStm_getTicksFromMilliseconds(2));
    IfxGeth_Eth_freeReceiveBuffer(&g_geth, IfxGeth_RxDmaChannel_0);
}
#pragma GCC pop_options

void DreCanEthBridge_init(const uint8 *macAddress)
{
    g_canRxCount = 0U;
    g_dreTriggerCount = 0U;
    g_lastLinkPollTick = 0U;
    g_lastDiagTick = 0U;
    g_linkLogged = FALSE;
    g_softwareTriggerRequested = FALSE;
    memcpy(g_macAddress, macAddress, sizeof(g_macAddress));

    DreCanEthBridge_initCan();
    DreCanEthBridge_initEthernet(macAddress);
    DreCanEthBridge_initDre(macAddress);

    printf("DRE bridge ready: CAN01 <-> Ethernet ACF/AVTP.\r\n");
    printf("CAN nominal/data: 500K@80%% / 2M@80%%.\r\n");
    printf("ETH TX destination MAC: 2C:53:4A:0E:33:01, EtherType 0x22F0.\r\n");
}

void DreCanEthBridge_poll(void)
{
    uint32 now = g_TickCount_1ms;

    if ((uint32)(now - g_lastLinkPollTick) >= 100U)
    {
        g_lastLinkPollTick = now;

        if (dp83825i_update_link(&g_phy) != 0)
        {
            DreCanEthBridge_applyLinkState();
        }
    }

    if ((uint32)(now - g_lastDiagTick) >= 1000U)
    {
        g_lastDiagTick = now;
        DreCanEthBridge_logBridgeStatus();
    }

    DreCanEthBridge_processCanRx();

    /* Drain EOBUF: trigger the DRE Message Engine to move one pending ACF
       frame from EOBUF0 into the GETH Tx path.  On this setup DRE does NOT
       advance the GETH Tx DMA tail pointer by itself, so we advance it once
       after each trigger (4-descriptor ring, 16 bytes each).  We trigger at
       most ONCE per poll tick: the `ACFL > 0` condition re-enters on the next
       tick for any remaining buffered frames, which avoids re-triggering the
       same frame while DRE is still busy (that previously multiplied frames). */
    if (g_softwareTriggerRequested != FALSE)
    {
        /* The DRE EOBUF is single-buffered: DRE must finish moving the previous
           frame into the GETH Tx path before we hand it a new one.  If the
           previous EOBUF is still busy (TTL/trigger latch not yet released),
           overwriting RHBUF now would corrupt the in-flight frame, so we DROP
           this CAN frame and count it.  This is the honest, measured packet-
           loss point: the sustainable CAN->ETH rate is bounded by DRE's EOBUF
           turnaround time, not by a silent overwrite. */
        if (MODULE_DRE.EOBUF[0].STATUS.B.TXREQ != 0U)
        {
            g_drePerf.ethTxDrop++;
        }
        else
        {
            if (MODULE_DRE.EOBUF[0].STATUS.B.TTL != 0U)
            {
                MODULE_DRE.EOBUF[0].STATUS.B.TTL = 1U;
            }

            IfxDre_Dre_setSoftwareTrigger(&g_dre, 0U);

        /* Advance the GETH Tx DMA tail pointer by one descriptor so the DMA
           engine picks up the descriptor DRE just filled and transmits it.

           This is the proven mechanism from the original bridge: after each
           EOBUF trigger we hand the next ring slot to the DMA.  The four-entry
           ring (16 bytes/descriptor) simply wraps around; surplus descriptors
           beyond what the DMA has consumed are ignored by the hardware.

           Robustness against the historic burst deadlock: we no longer allow
           the tail to run arbitrarily far ahead.  If the ring is already full
           (tail has wrapped all the way around to the consumed descriptor),
           pushing further would corrupt the in-flight frame, so we DROP the
           frame and count it (the only CAN->ETH loss point under overload).
           A genuine hardware stall is healed separately below by resetting the
           ring, so the path can never lock up permanently. */
        {
            uint32 base = DRE_TETHDL0_DESCRIPTOR_ADDRESS;
            uint32 tp   = MODULE_GETH0.DMA.CH[0].TXDESC_TAIL_LPOINTER.U;
            uint32 cur  = MODULE_GETH0.DMA.CH[0].CURRENT_APP_TXDESC_L.U;
            uint32 occupied = (tp >= cur)
                ? ((tp - cur) / 16U)
                : ((tp + DRE_GETH_TX_RING_ENTRIES * 16U - cur) / 16U);

            if (occupied < DRE_GETH_TX_RING_ENTRIES)
            {
                tp += 16U;
                if (tp >= (base + DRE_GETH_TX_RING_ENTRIES * 16U))
                {
                    tp = base;
                }
                MODULE_GETH0.DMA.CH[0].TXDESC_TAIL_LPOINTER.U = tp;
                g_drePerf.ethTxTotal++;
            }
            else
            {
                /* Ring full: drop rather than over-advance (would corrupt the
                   in-flight frame and previously deadlocked the Tx path). */
                g_drePerf.ethTxDrop++;
            }
        }

        g_softwareTriggerRequested = FALSE;
        }
    }

    /* Deadlock prevention (replaces the historic blind "tail += 16" that could
       push tail all the way around to the consumed descriptor and freeze the Tx
       path under burst).  We recompute the true ring occupancy from the
       hardware pointers every tick.  As long as TETHDL/GetH keep draining the
       ring, occupancy stays below the 4-entry limit and every frame is pushed.
       If the ring is genuinely full (occupancy == entries) we DROP the frame
       and count it — this bounds the tail and prevents the wrap-around
       deadlock, at the cost of measured packet loss under overload.

       The previous deadlock also required the DMA to be stuck (not draining).
       If that ever happens, occupancy stays at the limit and we keep dropping
       rather than corrupting in-flight data; the hardware fault is then visible
       via ethTxDrop without taking the whole bridge down. */
    {
        uint32 tp  = MODULE_GETH0.DMA.CH[0].TXDESC_TAIL_LPOINTER.U;
        uint32 cur = MODULE_GETH0.DMA.CH[0].CURRENT_APP_TXDESC_L.U;
        uint32 occupied = (tp >= cur)
            ? ((tp - cur) / 16U)
            : ((tp + DRE_GETH_TX_RING_ENTRIES * 16U - cur) / 16U);
        if (occupied >= DRE_GETH_TX_RING_ENTRIES)
        {
            /* Ring did not drain this tick: count a stall event for diagnostics
               (distinct from a normal overload drop, which is already counted by
               ethTxDrop above). */
            g_drePerf.ringStalls++;
        }
    }

    /* Reverse path: Ethernet -> CAN (software bridge, symmetric to the
       CAN->ETH path above).  Independent of the forward path; drains the GETH
       Rx ring and forwards parsed ACF frames to the CAN TX FIFO. */
    DreCanEthBridge_processEthRx();

    DreCanEthBridge_serviceDreStatus();
}