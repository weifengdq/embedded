/*
 * Spi_Cfg.h —— SPI Handler/Driver 预编译配置（AUTOSAR pre-compile 阶段）
 *
 * 改这里会改变数据类型/静态 RAM 占用，需要重新编译；板级参数在 Spi_PBcfg.c。
 */
#ifndef SPI_CFG_H
#define SPI_CFG_H

#include "Mcal_Cfg.h"

/* 交付级别：LEVEL 2 = 同步 + 异步都支持（SWS SPIHandlerDriver §10.2.3 SpiLevelDelivered） */
#define SPI_LEVEL_DELIVERED          (2u)

/* 开发错误检测：跟随全局开关 */
#ifndef SPI_DEV_ERROR_DETECT
#define SPI_DEV_ERROR_DETECT         (MCAL_DEV_ERROR_DETECT)
#endif

#define SPI_VERSION_INFO_API         (MCAL_VERSION_INFO_API)
#define SPI_ASYNC_MODE_SUPPORT       STD_ON
#define SPI_CANCEL_API               STD_ON
#define SPI_HW_STATUS_API            STD_ON
#define SPI_INTERRUPT_SUPPORT        STD_ON
#define SPI_DMA_SUPPORT              STD_ON

/* 资源规模（与 Spi_PBcfg.c 的数组长度一致） */
#define SPI_HW_UNIT_COUNT            (2u)    /* SPI1 master + SPI3 slave；SPI2 留给 LAN8651 */
#define SPI_CHANNEL_COUNT            (8u)
#define SPI_JOB_COUNT                (8u)
#define SPI_SEQUENCE_COUNT           (5u)
#define SPI_MAX_CHANNEL_PER_JOB      (2u)
#define SPI_MAX_JOB_PER_SEQUENCE     (4u)

/* IB 内部缓冲：每个通道 IB 缓冲元素数（EB 通道不占） */
#define SPI_IB_BUFFER_SIZE           (256u)

/* 单次同步/异步传输的软件超时（轮询次数，防止总线挂死） */
#define SPI_TIMEOUT_POLLS            (2000000u)
/* 异步中断模式下 DMA 完成等待上限（Spi_MainFunction_Handling 轮询） */
#define SPI_DMA_WAIT_POLLS           (1000000u)

#endif /* SPI_CFG_H */
