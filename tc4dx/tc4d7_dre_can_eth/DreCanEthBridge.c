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

            printf("BRIDGE diag: canRx=%lu, dreTrig=%lu, txReq0=%u, txCnt=%u, fwdReq0=%u, rxCnt=%u, eobuf0_status=0x%08lX, eobuf0_error=0x%08lX, me_err=0x%08lX, tethdl0=0x%08lX, txdma=0x%08lX, tail=0x%08lX, curdesc=0x%08lX, mac_tx=0x%08lX, mac_txpkts=%lu, mac_txoct=%lu, mac_err=0x%08lX.\r\n",
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
                (unsigned long)MODULE_GETH0.PORT[0].CORE.TX_PACKET_COUNT_GOOD_LOW.U,
                (unsigned long)MODULE_GETH0.PORT[0].CORE.TX_OCTET_COUNT_GOOD_LOW.U,
                (unsigned long)MODULE_GETH0.PORT[0].CORE.MAC_RX_TX_STATUS.U);

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
    (void)DreCanEthBridge_mdioInit(NULL_PTR, gethClockRate);
    MODULE_GETH0.PORT[DRE_ETH_PORT_INDEX].CORE.MAC_PACKET_FILTER.U = 0U;
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
    creConfig.rxBuf0DreTriggerEnable = TRUE;
    creConfig.rxBuf1DreTriggerEnable = FALSE;
    IfxCan_Can_initCre(&g_canNode, &creConfig);

    while (IfxCan_Can_isNodeSynchronized(&g_canNode) == FALSE)
    {
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
    dreConfig.ethernetOutputBuffer0.macDestinationAddress0 = 0xFFFFU;
    dreConfig.ethernetOutputBuffer0.macDestinationAddress1 = 0xFFFFFFFFU;
    dreConfig.ethernetOutputBuffer0.macSourceAddress0 = DreCanEthBridge_packMac32(&macSource[0]);
    dreConfig.ethernetOutputBuffer0.macSourceAddress1 = DreCanEthBridge_packMac16(&macSource[4]);
    dreConfig.ethernetOutputBuffer0.tpId = 0U;
    dreConfig.ethernetOutputBuffer0.vlanTag = 0U;
    dreConfig.ethernetOutputBuffer0.avtpEtherType = 0x22F0U;
    dreConfig.ethernetOutputBuffer0.isStreamIdValid = TRUE;
    dreConfig.ethernetOutputBuffer0.ntscfSequenceNumber = 0U;
    dreConfig.ethernetOutputBuffer0.streamIdLower = DreCanEthBridge_packMac32(&macSource[2]);
    dreConfig.ethernetOutputBuffer0.streamIdHigher = DreCanEthBridge_packMac32(&macSource[0]);
    dreConfig.ethernetOutputBuffer0.triggerFillLevel = 1U;

    dreConfig.ethernetInputBuffer0.ntscfStartAddress = DRE_ETH_NTSCF_OFFSET;
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
    rxEthConfig.descriptorPointerConfigEnable = TRUE;
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

    memset((void *)&rxHostBuffer, 0, sizeof(rxHostBuffer));

    rxHostBuffer.UCRH.B.MODE = 0U;
    rxHostBuffer.UCRH.B.SID = IfxCan_DestinationId_Can0_Node1;
    rxHostBuffer.UCRH.B.DID = IfxCan_DestinationId_Ethernet1;

    rxHostBuffer.R0.B.ID = rxMessage->messageId;
    rxHostBuffer.R0.B.RTR = rxMessage->remoteTransmitRequest ? 1U : 0U;
    rxHostBuffer.R0.B.XTD = (rxMessage->messageIdLength == IfxCan_MessageIdLength_extended) ? 1U : 0U;
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

    IfxCan_Can_triggerDebugMessageToDre(&g_canNode, IfxCan_CreRxHostBufferIndex_0, &rxHostBuffer);
    g_dreTriggerCount++;
    g_softwareTriggerRequested = TRUE;
}

static void DreCanEthBridge_processCanRx(void)
{
    while (IfxCan_Can_getRxFifo0FillLevel(&g_canNode) > 0U)
    {
        IfxCan_Message rxMessage;

        IfxCan_Can_initMessage(&rxMessage);
        rxMessage.readFromRxFifo0 = TRUE;
        IfxCan_Can_readMessage(&g_canNode, &rxMessage, g_canRxWords);
        g_canRxCount++;
        DreCanEthBridge_triggerCanToEthernet(&rxMessage, g_canRxWords);
    }
}

static void DreCanEthBridge_serviceDreStatus(void)
{
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
    printf("ETH TX destination MAC: FF:FF:FF:FF:FF:FF, EtherType 0x22F0.\r\n");
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

    if (g_softwareTriggerRequested != FALSE)
    {
        if ((MODULE_DRE.EOBUF[0].STATUS.B.ACFL > 0U) && (MODULE_DRE.EREQ.B.TX0_REQ == 0U))
        {
            if (MODULE_DRE.EOBUF[0].STATUS.B.TTL != 0U)
            {
                MODULE_DRE.EOBUF[0].STATUS.B.TTL = 1U;
            }

            IfxDre_Dre_setSoftwareTrigger(&g_dre, 0U);
            g_softwareTriggerRequested = FALSE;
        }
    }

    DreCanEthBridge_serviceDreStatus();
}