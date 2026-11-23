/*
 * Mem_PBcfg.h —— Mem post-build 配置声明（AUTOSAR Post-build 配置阶段）
 *
 * 实例表与 <Mod>_Config 常量都在 Mem_PBcfg.c 里，本头文件只暴露符号。
 */
#ifndef MEM_PBCFG_H
#define MEM_PBCFG_H

#include "Mem.h"

/* 实例表（板级映射，改板只改 Mem_PBcfg.c） */
extern const Mem_InstanceConfigType Mem_InstanceConfigTable[];
extern const uint8                 Mem_InstanceConfigCount;

/* post-build 配置实例（SWS_Mem §5 约定：const <Mod>_ConfigType <Mod>_Config） */
extern const Mem_ConfigType Mem_Config;

#endif /* MEM_PBCFG_H */
