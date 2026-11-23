/*
 * Can_PBcfg.h —— CAN 驱动 post-build 配置（契约 §5.3）
 *
 * 本文件只做声明；所有板级参数（MCAN 实例、引脚、时钟、收发器、消息 RAM、
 * 位时序、过滤器、中断号、中断向量）都在 Can_PBcfg.c 里。
 *
 * 依据：AUTOSAR_CP_SWS_CANDriver §10（CanConfigSet / CanController /
 *       CanControllerBaudrateConfig / CanHardwareObject / CanFilterMask）。
 */
#ifndef CAN_PBCFG_H
#define CAN_PBCFG_H

#include "Can.h"

/*! post-build 配置实例：mcal_add(Can) 后由 Can_Init(&Can_Config) 传入 */
extern const Can_ConfigType Can_Config;

/*! 驱动侧中断分发入口（Can.c 实现）：ControllerId = 0..CAN_CONTROLLER_COUNT-1 */
void Can_Isr_Handler(uint8 ControllerId);

/*! 中断向量入口（Can_PBcfg.c 实现并用 SDK_DECLARE_EXT_ISR_M 绑定到 .isr_vector）
 *  命名遵循契约 §4 "ISR 回调命名 <Mod>_Isr_<instance>"。 */
void Can_Isr_0(void);
void Can_Isr_1(void);
void Can_Isr_2(void);
void Can_Isr_3(void);

#endif /* CAN_PBCFG_H */
