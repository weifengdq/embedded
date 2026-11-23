/*
 * I2c.h —— AUTOSAR CP I2C Driver（模块 ID 113）公共接口
 *
 * 参考：AUTOSAR_CP_SWS_I2CDriver R25-11（draft，服务名 I2C_*，§8.3 函数定义；
 *       本地抽取文本 autosar_txt/AUTOSAR_CP_SWS_I2CDriver.txt）。
 *       注意：R4.x 老版 I2C 驱动（I2c_Init/I2c_Write/I2c_Read 语义）与本版差别很大，
 *       本实现按当前 SWS 的 Job/Sequence 模型，章节里列了新旧差异表。
 * 硬件：HPM5E31 I2C_Type（RM "I2C" 章：SETUP/ADDR/DATA/STATUS/INTEN/CMD/FIFOCTRL）。
 *
 * 目标（从机）模式用 I2C3 + HDMA ch6 自动收发；控制器（主机）模式用 I2C2 阻塞传输。
 * 本头文件不含任何 HPM SDK 头。
 */
#ifndef I2C_H
#define I2C_H

#include "Std_Types.h"
#include "Mcal_Cfg.h"
#include "Mcal_ModuleId.h"
#include "I2c_Cfg.h"

#define I2C_VENDOR_ID                   ((uint16)0xFFFFu)
#define I2C_AR_RELEASE_MAJOR_VERSION    ((uint8)MCAL_AR_RELEASE_MAJOR_VERSION)
#define I2C_AR_RELEASE_MINOR_VERSION    ((uint8)MCAL_AR_RELEASE_MINOR_VERSION)
#define I2C_AR_RELEASE_REVISION_VERSION ((uint8)MCAL_AR_RELEASE_REVISION_VERSION)
#define I2C_SW_MAJOR_VERSION            ((uint8)1u)
#define I2C_SW_MINOR_VERSION            ((uint8)0u)
#define I2C_SW_PATCH_VERSION            ((uint8)0u)

/* API Service ID（SWS I2CDriver §8.3 Service ID [hex]） */
#define I2C_API_ID_INIT                 ((uint8)0x00u)
#define I2C_API_ID_DEINIT               ((uint8)0x01u)
#define I2C_API_ID_SETUP_EB             ((uint8)0x02u)
#define I2C_API_ID_ASYNC_TRANSMIT       ((uint8)0x03u)
#define I2C_API_ID_SYNC_TRANSMIT        ((uint8)0x04u)
#define I2C_API_ID_GET_VERSION_INFO     ((uint8)0x07u)
#define I2C_API_ID_GET_SEQUENCE_RESULT  ((uint8)0x09u)
#define I2C_API_ID_GET_CHANNEL_STATUS   ((uint8)0x0Bu)
#define I2C_API_ID_MAIN_FUNCTION        ((uint8)0x0Cu)

/* 开发错误（SWS I2CDriver §7.3.1） */
#define I2C_E_PARAM_JOB                 ((uint8)0x00u)
#define I2C_E_PARAM_SEQUENCE            ((uint8)0x01u)
#define I2C_E_PARAM_POINTER             ((uint8)0x02u)
#define I2C_E_UNINIT                    ((uint8)0x03u)
#define I2C_E_WRONG_CONDITION           ((uint8)0x04u)
#define I2C_E_PARAM_UNIT                ((uint8)0x05u)
/* 运行时错误（SWS I2CDriver §7.3.2） */
#define I2C_E_NACK_RECEIVED             ((uint8)0x00u)
#define I2C_E_ARBITRATION_FAILURE       ((uint8)0x01u)
#define I2C_E_FIFO_HANDLING             ((uint8)0x02u)

/* ---- 数据类型（SWS §8.2） ---- */
typedef uint16 I2C_AddressType;         /* 7 bit: 0..0x7F；>0x7F 为 10 bit 扩展地址 */
typedef uint8  I2C_ChannelType;
typedef uint8  I2C_JobType;
typedef uint8  I2C_SequenceType;
typedef uint16 I2C_NumberOfDataType;
typedef uint8 *I2C_DataPtrType;

typedef enum {
    I2C_IDLE = 0x01,
    I2C_BUSY = 0x02
} I2C_ChannelStatusType;

typedef enum {
    I2C_SEQ_OK      = 0x00,
    I2C_SEQ_PENDING = 0x01,
    I2C_SEQ_QUEUED  = 0x02,
    I2C_SEQ_NACK    = 0x03,
    I2C_SEQ_FAILED  = 0x04
} I2C_SequenceResultType;

/* ---- 配置类型（post-build，实例在 I2c_PBcfg.c） ---- */
typedef struct {
    void    *Base;                  /* I2C_Type* */
    void    *DmaBase;
    void    *DmamuxBase;
    void   (*PinInit)(void);
    uint32 (*ClockEnable)(void);
    boolean  IsTarget;              /* TRUE: 目标(从机)模式 */
    I2C_AddressType TargetAddress;  /* 目标模式本机地址（7 bit） */
    uint32   BaudrateKHz;           /* 100 / 400 / 1000 */
    uint16   IrqNum;
    uint8    RxDmaChannel;
    uint8    RxDmaSrc;
} I2C_ChannelConfigType;

typedef struct {
    I2C_ChannelType Channel;
    I2C_AddressType NodeAddress;    /* 控制器模式下要访问的目标地址；0 = 由 SetupEB 覆盖 */
} I2C_JobConfigType;

typedef struct {
    uint8            JobCount;
    I2C_JobType      JobList[I2C_MAX_JOB_PER_SEQUENCE];
    void           (*SeqEndNotification)(void);
} I2C_SequenceConfigType;

typedef struct {
    const I2C_ChannelConfigType  *Channels;
    const I2C_JobConfigType      *Jobs;
    const I2C_SequenceConfigType *Sequences;
} I2C_ConfigType;

extern const I2C_ConfigType I2C_Config;

/* ---- SWS §8.3 API ---- */
void                      I2C_Init(const I2C_ConfigType *ConfigPtr);
void                      I2C_DeInit(void);
Std_ReturnType            I2C_SetupEB(I2C_JobType JobId, I2C_AddressType NodeAddress,
                                      const I2C_DataPtrType TxDataBufferPtr,
                                      I2C_DataPtrType RxDataBufferPtr,
                                      I2C_NumberOfDataType Length);
Std_ReturnType            I2C_AsyncTransmit(I2C_SequenceType SequenceId);
Std_ReturnType            I2C_SyncTransmit(I2C_SequenceType SequenceId);
void                      I2C_GetVersionInfo(Std_VersionInfoType *VersionInfo);
I2C_SequenceResultType    I2C_GetSequenceResult(I2C_SequenceType SequenceId);
I2C_ChannelStatusType     I2C_GetChannelStatus(I2C_ChannelType Channel);
void                      I2C_MainFunction(void);

/* PBcfg 绑定中断向量用 */
void                      I2C_Isr_Channel(I2C_ChannelType Channel);

/* 非 AUTOSAR 扩展：给例程/诊断用的运行时错误计数（SWS 里只有 Dem 上报） */
uint32                    I2C_GetRuntimeErrorCount(void);

#endif /* I2C_H */
