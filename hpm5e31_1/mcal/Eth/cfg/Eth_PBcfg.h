/*
 * Eth_PBcfg.h —— Eth 驱动 post-build 配置（契约 §5.3）
 *
 * 本文件只做声明；板级参数（ENET 实例、引脚、时钟、PHY 地址、MAC、中断号、
 * 中断向量）全部在 Eth_PBcfg.c 里。
 */
#ifndef ETH_PBCFG_H
#define ETH_PBCFG_H

#include "Eth.h"

/*! post-build 配置实例：mcal_add(Eth) 后由 Eth_Init(&Eth_Config) 传入 */
extern const Eth_ConfigType Eth_Config;

/*! 板级初始化（Eth_PBcfg.c 实现）：引脚复用、PHY 复位、RGMII 时钟延迟、中断使能。
 *  驱动不直接调用 SDK/board 的板级函数，只调这个钩子（契约 §3 第 3 条）。 */
void Eth_BoardInit(uint8 CtrlIdx, void *BaseAddress);

/*! 板级中断使能/关闭（Eth_PBcfg.c 实现）：Eth_Init 在 DMA/MAC 配好后再打开 PLIC */
void Eth_BoardIrqEnable(uint8 CtrlIdx, void *BaseAddress, boolean Enable);

/*! 驱动侧中断分发入口（Eth.c 实现）：CtrlIdx = 0..ETH_CTRL_COUNT-1 */
void Eth_Isr_Handler(uint8 CtrlIdx);

/*! 中断向量入口（Eth_PBcfg.c 实现，SDK_DECLARE_EXT_ISR_M 绑定） */
void Eth_Isr_0(void);

#endif /* ETH_PBCFG_H */
