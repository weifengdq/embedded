/*
 * Spi_PBcfg.h —— SPI post-build 配置声明
 */
#ifndef SPI_PBCFG_H
#define SPI_PBCFG_H

#include "Spi.h"

/* post-build 配置集：1 MHz / 10 MHz（AUTOSAR 多配置集：Spi_Init 的 ConfigPtr 选择） */
extern const Spi_ConfigType Spi_Config;
extern const Spi_ConfigType Spi_Config_10M;

/* 例程可覆盖的弱符号通知函数（默认空实现，在 Spi_PBcfg.c） */
void Spi_JobEndNotification_HW(void);
void Spi_SeqEndNotification_Seq(void);

/* 便于例程按名字取 ID（值必须与 Spi_PBcfg.c 数组下标一致） */
#define SPI_HWUNIT_SPI1_MASTER   ((Spi_HWUnitType)0u)
#define SPI_HWUNIT_SPI3_SLAVE    ((Spi_HWUnitType)1u)

#define SPI_CH_MASTER_8BIT       ((Spi_ChannelType)0u)
#define SPI_CH_SLAVE_8BIT        ((Spi_ChannelType)1u)
#define SPI_CH_MASTER_16BIT      ((Spi_ChannelType)2u)
#define SPI_CH_SLAVE_16BIT       ((Spi_ChannelType)3u)
#define SPI_CH_MASTER_32BIT      ((Spi_ChannelType)4u)
#define SPI_CH_SLAVE_32BIT       ((Spi_ChannelType)5u)
#define SPI_CH_MASTER_IB         ((Spi_ChannelType)6u)
#define SPI_CH_SLAVE_IB          ((Spi_ChannelType)7u)

#define SPI_JOB_MASTER_8BIT      ((Spi_JobType)0u)
#define SPI_JOB_SLAVE_8BIT       ((Spi_JobType)1u)
#define SPI_JOB_MASTER_16BIT     ((Spi_JobType)2u)
#define SPI_JOB_SLAVE_16BIT      ((Spi_JobType)3u)
#define SPI_JOB_MASTER_32BIT     ((Spi_JobType)4u)
#define SPI_JOB_SLAVE_32BIT      ((Spi_JobType)5u)
#define SPI_JOB_MASTER_IB        ((Spi_JobType)6u)
#define SPI_JOB_SLAVE_IB         ((Spi_JobType)7u)

#define SPI_SEQ_MB8              ((Spi_SequenceType)0u)  /* slave arm -> master xfer, 8 bit  */
#define SPI_SEQ_MB16             ((Spi_SequenceType)1u)  /* 16 bit                          */
#define SPI_SEQ_MB32             ((Spi_SequenceType)2u)  /* 32 bit                          */
#define SPI_SEQ_IB8              ((Spi_SequenceType)3u)  /* IB 通道                          */
#define SPI_SEQ_ASYNC_MULTI      ((Spi_SequenceType)4u)  /* 多 Job 异步序列（4 个 Job）       */

#endif /* SPI_PBCFG_H */
