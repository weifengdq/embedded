/*
 * Spi.h —— AUTOSAR CP SPI Handler/Driver (模块 ID 83) 公共接口
 *
 * 参考：AUTOSAR_CP_SWS_SPIHandlerDriver (R25-11, 本地抽取文本
 *       autosar_txt/AUTOSAR_CP_SWS_SPIHandlerDriver.txt) §8.3 函数定义、
 *       HPM5E31 RM 第 "SPI" 章 (SPI_Type 寄存器)、例程 samples/mcal/spi_master_slave。
 *
 * 实现级别：LEVEL 2（异步 + 同步都可），EB/IB、Job/Sequence 链、8/16/32 bit、
 *           中断与 DMA（从机固定 DMA，主机异步中断模式用 DMA + END 中断）。
 *
 * 本头文件只包含 AUTOSAR 公共头，不含任何 HPM SDK 头（板级细节全在 Spi_PBcfg.c）。
 */
#ifndef SPI_H
#define SPI_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Mcal_ModuleId.h"
#include "Spi_Cfg.h"

/* ---- 版本（AUTOSAR SWS_BSWGeneral §5.2.2） ---- */
/* AUTOSAR 未分配 vendorID 时用 0xFFFF（与 mcal/common/Mcal_Version.h 的 MCAL_VENDOR_ID 一致） */
#define SPI_VENDOR_ID                   ((uint16)0xFFFFu)
#define SPI_AR_RELEASE_MAJOR_VERSION    ((uint8)MCAL_AR_RELEASE_MAJOR_VERSION)
#define SPI_AR_RELEASE_MINOR_VERSION    ((uint8)MCAL_AR_RELEASE_MINOR_VERSION)
#define SPI_AR_RELEASE_REVISION_VERSION ((uint8)MCAL_AR_RELEASE_REVISION_VERSION)
#define SPI_SW_MAJOR_VERSION            ((uint8)1u)
#define SPI_SW_MINOR_VERSION            ((uint8)0u)
#define SPI_SW_PATCH_VERSION            ((uint8)0u)

/* ---- API Service ID（SWS SPIHandlerDriver §8.3，Service ID [hex]） ---- */
#define SPI_API_ID_INIT                  ((uint8)0x00u)
#define SPI_API_ID_DEINIT                ((uint8)0x01u)
#define SPI_API_ID_WRITE_IB              ((uint8)0x02u)
#define SPI_API_ID_ASYNC_TRANSMIT        ((uint8)0x03u)
#define SPI_API_ID_READ_IB               ((uint8)0x04u)
#define SPI_API_ID_SETUP_EB              ((uint8)0x05u)
#define SPI_API_ID_GET_STATUS            ((uint8)0x06u)
#define SPI_API_ID_GET_JOB_RESULT        ((uint8)0x07u)
#define SPI_API_ID_GET_SEQUENCE_RESULT   ((uint8)0x08u)
#define SPI_API_ID_GET_VERSION_INFO      ((uint8)0x09u)
#define SPI_API_ID_SYNC_TRANSMIT         ((uint8)0x0Au)
#define SPI_API_ID_GET_HW_UNIT_STATUS    ((uint8)0x0Bu)
#define SPI_API_ID_CANCEL                ((uint8)0x0Cu)
#define SPI_API_ID_SET_ASYNC_MODE        ((uint8)0x0Du)
#define SPI_API_ID_MAIN_FUNCTION         ((uint8)0x10u)

/* ---- 开发错误（SWS SPIHandlerDriver §7.4.1） ---- */
#define SPI_E_PARAM_CHANNEL              ((uint8)0x0Au)
#define SPI_E_PARAM_JOB                  ((uint8)0x0Bu)
#define SPI_E_PARAM_SEQ                  ((uint8)0x0Cu)
#define SPI_E_PARAM_LENGTH               ((uint8)0x0Du)
#define SPI_E_PARAM_UNIT                 ((uint8)0x0Eu)
#define SPI_E_PARAM_POINTER              ((uint8)0x10u)
#define SPI_E_UNINIT                     ((uint8)0x1Au)
/* ---- 运行时错误（SWS SPIHandlerDriver §7.4.2） ---- */
#define SPI_E_SEQ_PENDING                ((uint8)0x2Au)
#define SPI_E_SEQ_IN_PROCESS             ((uint8)0x3Au)
#define SPI_E_ALREADY_INITIALIZED        ((uint8)0x4Au)

/* ---- SWS §8.2 数据类型 ---- */
typedef uint16 Spi_ChannelType;
typedef uint16 Spi_JobType;
typedef uint8  Spi_SequenceType;
typedef uint8  Spi_HWUnitType;
typedef uint8  Spi_DataBufferType;
typedef uint16 Spi_NumberOfDataType;

typedef enum {
    SPI_UNINIT = 0x00,
    SPI_IDLE   = 0x01,
    SPI_BUSY   = 0x02
} Spi_StatusType;

typedef enum {
    SPI_JOB_OK      = 0x00,
    SPI_JOB_PENDING = 0x01,
    SPI_JOB_FAILED  = 0x02,
    SPI_JOB_QUEUED  = 0x03
} Spi_JobResultType;

typedef enum {
    SPI_SEQ_OK       = 0x00,
    SPI_SEQ_PENDING  = 0x01,
    SPI_SEQ_FAILED   = 0x02,
    SPI_SEQ_CANCELED = 0x03
} Spi_SeqResultType;

typedef enum {
    SPI_ASYNC_MODE_INTERRUPT = 0x00,
    SPI_ASYNC_MODE_POLLING   = 0x01
} Spi_AsyncModeType;

/* 数据宽度：SWS SpiChannel 的 SpiDataWidth，本实现按 8/16/32 bit 主/从都能跑 */
typedef enum {
    SPI_DATA_WIDTH_8  = 8,
    SPI_DATA_WIDTH_16 = 16,
    SPI_DATA_WIDTH_32 = 32
} Spi_DataWidthType;

/* ---- 配置类型（post-build，实例在 Spi_PBcfg.c） ---- */
typedef struct {
    Spi_DataWidthType DataWidth;        /* 每个数据元素位宽 */
    boolean           UseInternalBuffer;/* TRUE: IB，驱动内部缓冲；FALSE: EB，用户缓冲 */
    uint16            DefaultLength;    /* IB 缓冲元素数（EB 由 Spi_SetupEB 的 Length 决定） */
} Spi_ChannelConfigType;

typedef struct {
    Spi_HWUnitType    HwUnit;           /* 本 Job 跑在哪个 SPI 实例上 */
    Spi_AsyncModeType AsyncMode;        /* 异步时的完成方式（中断 / 主函数轮询） */
    uint32            SclkFreqHz;       /* 本 Job 的 SCLK；0 = 用 HWUnit 默认值（SpiBaudrate） */
    uint8             ChannelCount;
    Spi_ChannelType   ChannelList[SPI_MAX_CHANNEL_PER_JOB];
    void            (*JobEndNotification)(void);  /* 可 NULL */
} Spi_JobConfigType;

typedef struct {
    uint8           JobCount;
    Spi_JobType     JobList[SPI_MAX_JOB_PER_SEQUENCE];
    boolean         Interruptible;
    void          (*SeqEndNotification)(void);    /* 可 NULL */
} Spi_SequenceConfigType;

/* 板级/实例级配置：引脚、时钟、DMA、IRQ 全在这里，驱动源码里没有硬编码 */
typedef struct {
    void    *Base;                 /* SPI_Type* */
    void    *DmaBase;              /* DMA_Type*（HPM_HDMA） */
    void    *DmamuxBase;           /* DMAMUX_Type* */
    void   (*PinInit)(void);       /* 板级引脚初始化（在 Spi_Init 里调用） */
    uint32 (*ClockEnable)(void);   /* 打开外设时钟并返回源时钟 Hz（在 Spi_Init 里调用） */
    boolean  IsMaster;
    uint8    Cpol;                 /* 0: 空闲低；1: 空闲高 */
    uint8    Cpha;                 /* 0: 第一个边沿采样；1: 第二个边沿采样 */
    uint8    CsToSclk;             /* CS 到 SCLK 的建立时间（TIMING.CS2SCLK 档位） */
    uint8    CsHold;               /* SCLK 到 CS 的保持时间（TIMING.CSHT 档位） */
    uint32   SclkFreqHz;           /* 默认 SCLK Hz */
    uint16   IrqNum;               /* 中断号（异步中断模式用） */
    uint8    TxDmaChannel;         /* HDMA 通道号 */
    uint8    RxDmaChannel;
    uint8    TxDmaSrc;             /* DMAMUX 源选择（HPM_DMA_SRC_xxx） */
    uint8    RxDmaSrc;
    uint8    ExtraTxElements;      /* HPM 从机 TX FIFO 提前欠载补偿（多发几个哑元） */
} Spi_HWUnitConfigType;

typedef struct {
    const Spi_ChannelConfigType  *Channels;
    const Spi_JobConfigType      *Jobs;
    const Spi_SequenceConfigType *Sequences;
    const Spi_HWUnitConfigType   *HWUnits;
} Spi_ConfigType;

extern const Spi_ConfigType Spi_Config;

/* ---- SWS §8.3 API ---- */
void             Spi_Init(const Spi_ConfigType *ConfigPtr);
Std_ReturnType   Spi_DeInit(void);
Std_ReturnType   Spi_WriteIB(Spi_ChannelType Channel, const Spi_DataBufferType *DataBufferPtr);
Std_ReturnType   Spi_ReadIB(Spi_ChannelType Channel, Spi_DataBufferType *DataBufferPointer);
Std_ReturnType   Spi_SetupEB(Spi_ChannelType Channel, const Spi_DataBufferType *SrcDataBufferPtr,
                             Spi_DataBufferType *DesDataBufferPtr, Spi_NumberOfDataType Length);
Std_ReturnType   Spi_AsyncTransmit(Spi_SequenceType Sequence);
Std_ReturnType   Spi_SyncTransmit(Spi_SequenceType Sequence);
Spi_StatusType   Spi_GetStatus(void);
Spi_JobResultType Spi_GetJobResult(Spi_JobType Job);
Spi_SeqResultType Spi_GetSequenceResult(Spi_SequenceType Sequence);
void             Spi_GetVersionInfo(Std_VersionInfoType *versioninfo);
Spi_StatusType   Spi_GetHWUnitStatus(Spi_HWUnitType HWUnit);
void             Spi_Cancel(Spi_SequenceType Sequence);
Std_ReturnType   Spi_SetAsyncMode(Spi_AsyncModeType Mode);
void             Spi_MainFunction_Handling(void);

/* 驱动 -> PBcfg 的 ISR 入口（PBcfg.c 用 SDK_DECLARE_EXT_ISR_M 绑定中断向量） */
void             Spi_Isr_HWUnit(Spi_HWUnitType HWUnit);

#endif /* SPI_H */
