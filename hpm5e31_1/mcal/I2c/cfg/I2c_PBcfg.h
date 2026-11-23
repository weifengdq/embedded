/*
 * I2c_PBcfg.h —— I2C post-build 配置声明
 */
#ifndef I2C_PBCFG_H
#define I2C_PBCFG_H

#include "I2c.h"

extern const I2C_ConfigType I2C_Config;
extern const I2C_ConfigType I2C_Config_100k;
extern const I2C_ConfigType I2C_Config_400k;
extern const I2C_ConfigType I2C_Config_1M;

/* 例程可覆盖的弱符号通知 */
void I2C_SeqEndNotification_Cb(void);

#define I2C_CH_I2C2_MASTER  ((I2C_ChannelType)0u)
#define I2C_CH_I2C3_TARGET  ((I2C_ChannelType)1u)

#define I2C_JOB_MASTER_TX   ((I2C_JobType)0u)
#define I2C_JOB_TARGET      ((I2C_JobType)1u)
#define I2C_JOB_MASTER_RX   ((I2C_JobType)2u)
#define I2C_JOB_MASTER_RDWR ((I2C_JobType)3u)

#define I2C_SEQ_WRITE       ((I2C_SequenceType)0u)  /* target 预备 -> master 写 */
#define I2C_SEQ_READ        ((I2C_SequenceType)1u)  /* target 预备 -> master 读 */
#define I2C_SEQ_RDWR        ((I2C_SequenceType)2u)  /* target 预备 -> master 写+读 */

#define I2C_TARGET_ADDR     ((I2C_AddressType)0x50u)

#endif /* I2C_PBCFG_H */
