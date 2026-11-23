/*
 * Port_Cfg.h —— Port 模块预编译配置（AUTOSAR ECUC 对应项见注释）
 *
 * 参考：AUTOSAR_CP_SWS_PortDriver §10；例程 samples/mcal/port_pin_config
 */
#ifndef PORT_CFG_H
#define PORT_CFG_H

#include "Mcal_Cfg.h"

#ifndef PORT_DEV_ERROR_DETECT
#define PORT_DEV_ERROR_DETECT       MCAL_DEV_ERROR_DETECT
#endif

/* PortSetPinDirectionApi（SWS_Port_00086） */
#ifndef PORT_SET_PIN_DIRECTION_API
#define PORT_SET_PIN_DIRECTION_API  STD_ON
#endif

/* PortSetPinModeApi（SWS_Port_00145） */
#ifndef PORT_SET_PIN_MODE_API
#define PORT_SET_PIN_MODE_API       STD_ON
#endif

/* PortVersionInfoApi（SWS_Port_00143） */
#ifndef PORT_VERSION_INFO_API
#define PORT_VERSION_INFO_API       MCAL_VERSION_INFO_API
#endif

/* 本板 post-build 引脚表长度（与 Port_PBcfg.c 一致，编译期自检） */
#define PORT_PIN_COUNT_CFG          (16u)

#endif /* PORT_CFG_H */
