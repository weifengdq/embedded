/*
 * I2c_Cfg.h —— I2C Driver 预编译配置
 */
#ifndef I2C_CFG_H
#define I2C_CFG_H

#include "Mcal_Cfg.h"

#ifndef I2C_DEV_ERROR_DETECT
#define I2C_DEV_ERROR_DETECT        (MCAL_DEV_ERROR_DETECT)
#endif
#define I2C_VERSION_INFO_API        (MCAL_VERSION_INFO_API)

/* 资源规模：与 I2c_PBcfg.c 数组一致 */
#define I2C_CHANNEL_COUNT           (2u)   /* I2C2 控制器 + I2C3 目标 */
#define I2C_JOB_COUNT               (4u)
#define I2C_SEQUENCE_COUNT          (3u)
#define I2C_MAX_JOB_PER_SEQUENCE    (2u)
#define I2C_MAX_DATA_LENGTH         (256u)

/* 阻塞传输的软件超时（轮询次数） */
#define I2C_TIMEOUT_POLLS           (2000000u)

#endif /* I2C_CFG_H */
