/*
 * Spi_PBcfg.c —— HPM5E31(hpm5e31_1) 板级 SPI post-build 配置
 *
 * 板级映射（与 docs/MCAL开发约定.md §2 一致，改板只改本文件）：
 *   SPI1 主：SCLK PC10 / CS PC11 / MISO PC12 / MOSI PC13
 *   SPI3 从：SCLK PA26 / CS PA27 / MISO PA28 / MOSI PA29   （杜邦线 1:1 对连）
 *   SPI2 留给 LAN8651(10BASE-T1S)，本模块不初始化。
 * 参考：HPM5E31 RM "SPI" 章；samples/spi/spi1_spi3_master_slave（寄存器级已验证的接线与时序）。
 *
 * DMA 通道分配（勿与其它 MCAL 模块冲突，见 docs/MCAL开发约定.md §2）：
 *   SPI1 主(异步中断模式) TX=HDMA ch2 / RX=HDMA ch3
 *   SPI3 从(DMA 收发)      TX=HDMA ch4 / RX=HDMA ch5
 */
#include "Spi_PBcfg.h"
#include "Spi.h"
#include "Compiler.h"

#include "hpm_soc.h"
#include "hpm_interrupt.h"
#include "hpm_spi_drv.h"
#include "hpm_dmav2_drv.h"
#include "hpm_dmamux_src.h"
#include "board.h"
#include "pins/pinmux_core.h"

/* ---------- 弱符号通知：例程用强符号覆盖即可，不需要改本文件 ---------- */
MCAL_WEAK void Spi_JobEndNotification_HW(void)  { }
MCAL_WEAK void Spi_SeqEndNotification_Seq(void) { }

/* ---------- 中断入口：中断向量 -> 驱动统一入口 Spi_Isr_HWUnit() ---------- */
SDK_DECLARE_EXT_ISR_M(IRQn_SPI1, spi1_mcal_isr)
void spi1_mcal_isr(void)
{
    Spi_Isr_HWUnit(SPI_HWUNIT_SPI1_MASTER);
}

SDK_DECLARE_EXT_ISR_M(IRQn_SPI3, spi3_mcal_isr)
void spi3_mcal_isr(void)
{
    Spi_Isr_HWUnit(SPI_HWUNIT_SPI3_SLAVE);
}

/* ---------- 板级时钟：返回 SPI 源时钟 Hz，同时打开外设时钟门 ---------- */
static uint32 spi1_clock_enable(void) { return board_init_spi_clock(HPM_SPI1); }
static uint32 spi3_clock_enable(void) { return board_init_spi_clock(HPM_SPI3); }

/* ---------- 通道表 ---------- */
static const Spi_ChannelConfigType Spi_Channels[SPI_CHANNEL_COUNT] = {
    /* CH0  SPI1 主 8bit EB */
    { SPI_DATA_WIDTH_8,  FALSE, 0u },
    /* CH1  SPI3 从 8bit EB */
    { SPI_DATA_WIDTH_8,  FALSE, 0u },
    /* CH2  SPI1 主 16bit EB */
    { SPI_DATA_WIDTH_16, FALSE, 0u },
    /* CH3  SPI3 从 16bit EB */
    { SPI_DATA_WIDTH_16, FALSE, 0u },
    /* CH4  SPI1 主 32bit EB */
    { SPI_DATA_WIDTH_32, FALSE, 0u },
    /* CH5  SPI3 从 32bit EB */
    { SPI_DATA_WIDTH_32, FALSE, 0u },
    /* CH6  SPI1 主 8bit IB（驱动内部缓冲，Demonstrate IB） */
    { SPI_DATA_WIDTH_8,  TRUE,  64u },
    /* CH7  SPI3 从 8bit IB */
    { SPI_DATA_WIDTH_8,  TRUE,  64u },
};

/* ---------- Job 表 ---------- */
static const Spi_JobConfigType Spi_Jobs[SPI_JOB_COUNT] = {
    { SPI_HWUNIT_SPI1_MASTER, SPI_ASYNC_MODE_INTERRUPT, 0u, 1u, { SPI_CH_MASTER_8BIT },  Spi_JobEndNotification_HW },
    { SPI_HWUNIT_SPI3_SLAVE,  SPI_ASYNC_MODE_POLLING,   0u, 1u, { SPI_CH_SLAVE_8BIT  },  Spi_JobEndNotification_HW },
    { SPI_HWUNIT_SPI1_MASTER, SPI_ASYNC_MODE_INTERRUPT, 0u, 1u, { SPI_CH_MASTER_16BIT }, Spi_JobEndNotification_HW },
    { SPI_HWUNIT_SPI3_SLAVE,  SPI_ASYNC_MODE_POLLING,   0u, 1u, { SPI_CH_SLAVE_16BIT  }, Spi_JobEndNotification_HW },
    { SPI_HWUNIT_SPI1_MASTER, SPI_ASYNC_MODE_POLLING,   0u, 1u, { SPI_CH_MASTER_32BIT }, Spi_JobEndNotification_HW },
    { SPI_HWUNIT_SPI3_SLAVE,  SPI_ASYNC_MODE_POLLING,   0u, 1u, { SPI_CH_SLAVE_32BIT  }, Spi_JobEndNotification_HW },
    { SPI_HWUNIT_SPI1_MASTER, SPI_ASYNC_MODE_POLLING,   0u, 1u, { SPI_CH_MASTER_IB    }, Spi_JobEndNotification_HW },
    { SPI_HWUNIT_SPI3_SLAVE,  SPI_ASYNC_MODE_POLLING,   0u, 1u, { SPI_CH_SLAVE_IB     }, Spi_JobEndNotification_HW },
};

/* ---------- Sequence 表 ---------- */
static const Spi_SequenceConfigType Spi_Sequences[SPI_SEQUENCE_COUNT] = {
    /* SEQ0: 8bit 双向 —— 先武装从机(SPI3)，再让主机(SPI1)发时钟 */
    { 2u, { SPI_JOB_SLAVE_8BIT, SPI_JOB_MASTER_8BIT }, FALSE, Spi_SeqEndNotification_Seq },
    /* SEQ1: 16bit 双向 */
    { 2u, { SPI_JOB_SLAVE_16BIT, SPI_JOB_MASTER_16BIT }, FALSE, Spi_SeqEndNotification_Seq },
    /* SEQ2: 32bit 双向 */
    { 2u, { SPI_JOB_SLAVE_32BIT, SPI_JOB_MASTER_32BIT }, FALSE, Spi_SeqEndNotification_Seq },
    /* SEQ3: IB 通道 8bit 双向 */
    { 2u, { SPI_JOB_SLAVE_IB, SPI_JOB_MASTER_IB }, FALSE, Spi_SeqEndNotification_Seq },
    /* SEQ4: 多 Job 异步序列（8bit 一对 + 16bit 一对），给 spi_async_job 例程用 */
    { 4u, { SPI_JOB_SLAVE_8BIT, SPI_JOB_MASTER_8BIT, SPI_JOB_SLAVE_16BIT, SPI_JOB_MASTER_16BIT },
      FALSE, Spi_SeqEndNotification_Seq },
};

/* ---------- HW Unit 表：同一套实例，三种 SCLK 配置集 ---------- */

/* 1 MHz：默认，长杜邦线最稳 */
static const Spi_HWUnitConfigType Spi_HwUnits_1M[SPI_HW_UNIT_COUNT] = {
    {   /* SPI1 master */
        .Base = (void *)HPM_SPI1,
        .DmaBase = (void *)HPM_HDMA,
        .DmamuxBase = (void *)HPM_DMAMUX,
        .PinInit = init_spi1_master_pins,
        .ClockEnable = spi1_clock_enable,
        .IsMaster = TRUE,
        .Cpol = 0u, .Cpha = 0u, .CsToSclk = 3u, .CsHold = 1u,
        .SclkFreqHz = 1000000u,
        .IrqNum = (uint16)IRQn_SPI1,
        .TxDmaChannel = 2u, .RxDmaChannel = 3u,
        .TxDmaSrc = (uint8)HPM_DMA_SRC_SPI1_TX, .RxDmaSrc = (uint8)HPM_DMA_SRC_SPI1_RX,
        .ExtraTxElements = 0u,
    },
    {   /* SPI3 slave */
        .Base = (void *)HPM_SPI3,
        .DmaBase = (void *)HPM_HDMA,
        .DmamuxBase = (void *)HPM_DMAMUX,
        .PinInit = init_spi3_slave_pins,
        .ClockEnable = spi3_clock_enable,
        .IsMaster = FALSE,
        .Cpol = 0u, .Cpha = 0u, .CsToSclk = 0u, .CsHold = 0u,
        .SclkFreqHz = 1000000u,
        .IrqNum = (uint16)IRQn_SPI3,
        .TxDmaChannel = 4u, .RxDmaChannel = 5u,
        .TxDmaSrc = (uint8)HPM_DMA_SRC_SPI3_TX, .RxDmaSrc = (uint8)HPM_DMA_SRC_SPI3_RX,
        .ExtraTxElements = 1u,   /* HPM 从机 TX FIFO 在首个时钟前欠载，多发 1 个哑元补偿 */
    },
};

/* 10 MHz */
static const Spi_HWUnitConfigType Spi_HwUnits_10M[SPI_HW_UNIT_COUNT] = {
    {
        .Base = (void *)HPM_SPI1, .DmaBase = (void *)HPM_HDMA, .DmamuxBase = (void *)HPM_DMAMUX,
        .PinInit = init_spi1_master_pins, .ClockEnable = spi1_clock_enable,
        .IsMaster = TRUE, .Cpol = 0u, .Cpha = 0u, .CsToSclk = 3u, .CsHold = 1u,
        .SclkFreqHz = 10000000u, .IrqNum = (uint16)IRQn_SPI1,
        .TxDmaChannel = 2u, .RxDmaChannel = 3u,
        .TxDmaSrc = (uint8)HPM_DMA_SRC_SPI1_TX, .RxDmaSrc = (uint8)HPM_DMA_SRC_SPI1_RX,
        .ExtraTxElements = 0u,
    },
    {
        .Base = (void *)HPM_SPI3, .DmaBase = (void *)HPM_HDMA, .DmamuxBase = (void *)HPM_DMAMUX,
        .PinInit = init_spi3_slave_pins, .ClockEnable = spi3_clock_enable,
        .IsMaster = FALSE, .Cpol = 0u, .Cpha = 0u, .CsToSclk = 0u, .CsHold = 0u,
        .SclkFreqHz = 10000000u, .IrqNum = (uint16)IRQn_SPI3,
        .TxDmaChannel = 4u, .RxDmaChannel = 5u,
        .TxDmaSrc = (uint8)HPM_DMA_SRC_SPI3_TX, .RxDmaSrc = (uint8)HPM_DMA_SRC_SPI3_RX,
        .ExtraTxElements = 1u,
    },
};

/* post-build 配置实例：例程按需 Spi_Init(&Spi_Config) / Spi_Init(&Spi_Config_10M) */
const Spi_ConfigType Spi_Config = {
    Spi_Channels, Spi_Jobs, Spi_Sequences, Spi_HwUnits_1M
};
const Spi_ConfigType Spi_Config_10M = {
    Spi_Channels, Spi_Jobs, Spi_Sequences, Spi_HwUnits_10M
};
