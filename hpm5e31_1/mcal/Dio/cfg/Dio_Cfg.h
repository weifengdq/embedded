/*
 * Dio_Cfg.h —— Dio 模块预编译配置
 *
 * 参考：AUTOSAR_CP_SWS_DIODriver §10；例程 samples/mcal/dio_led_button
 */
#ifndef DIO_CFG_H
#define DIO_CFG_H

#include "Mcal_Cfg.h"

#ifndef DIO_DEV_ERROR_DETECT
#define DIO_DEV_ERROR_DETECT        MCAL_DEV_ERROR_DETECT
#endif

/* DioFlipChannelApi（SWS 可选 API） */
#ifndef DIO_FLIP_CHANNEL_API
#define DIO_FLIP_CHANNEL_API        STD_ON
#endif

/* DioMaskedWritePortApi（SWS 可选 API） */
#ifndef DIO_MASKED_WRITE_PORT_API
#define DIO_MASKED_WRITE_PORT_API   STD_ON
#endif

/* DioVersionInfoApi */
#ifndef DIO_VERSION_INFO_API
#define DIO_VERSION_INFO_API        MCAL_VERSION_INFO_API
#endif

/* 本板通道数（与 Dio_PBcfg.c 一致，编译期自检） */
#define DIO_CHANNEL_COUNT_CFG       (16u)
/* 本板通道组个数 */
#define DIO_CHANNEL_GROUP_COUNT_CFG (3u)
/* GPIO0 支持的端口数（A..F） */
#define DIO_MAX_PORTS               (6u)

#endif /* DIO_CFG_H */
