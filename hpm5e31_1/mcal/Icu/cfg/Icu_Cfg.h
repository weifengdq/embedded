/*
 * Icu_Cfg.h —— ICU 预编译配置（SWS_Icu §10.2）
 */
#ifndef ICU_CFG_H
#define ICU_CFG_H

#include "Mcal_Cfg.h"

#define ICU_NUM_CHANNELS            (2u)

#ifndef ICU_DEV_ERROR_DETECT
#define ICU_DEV_ERROR_DETECT        MCAL_DEV_ERROR_DETECT
#endif

#define ICU_DEINIT_API              STD_ON
#define ICU_VERSION_INFO_API        MCAL_VERSION_INFO_API
#define ICU_SIGNAL_MEASUREMENT_API  STD_ON
#define ICU_TIMESTAMP_API           STD_ON
#define ICU_EDGE_DETECT_API         STD_ON
#define ICU_NOTIFICATION_SUPPORTED  STD_ON
#define ICU_WAKEUP_SUPPORTED        STD_OFF   /* 无 EcuM，本子集不做唤醒 */

#define ICU_IRQ_PRIORITY            (2u)

/* 时间戳缓冲上限（由配置 BufferSize 再截断） */
#define ICU_MAX_TIMESTAMP_BUFFER    (256u)

#endif /* ICU_CFG_H */
