/*
 * Mem_PBcfg.c —— Mem 板级 post-build 配置（HPM5E31 / hpm5e31_1）
 *
 * 依据：docs/MCAL开发约定.md §2/§5、AUTOSAR_CP_SWS_MemoryDriver §10.2。
 * 本文件是"改板只改这里"的唯一位置：存储器实例、基址、容量、擦写窗口、几何参数、
 * XPI 控制器基址与 ROM API 自动配置头全部来自板级宏（board.h）。
 *
 * 安全窗口：片内 1MB Flash 映射在 0x80000000..0x800FFFFF，应用镜像从 0x80000000
 * 起约 74~80KB（见 samples/flash/flash_rw_erase 的 map/etext 统计），因此把可擦写
 * 窗口限制在最后 64KB：0x800F0000..0x80100000。详见 README 与第 48 章。
 */
#include "Mem.h"
#include "Mem_Cfg.h"
#include "Mem_PBcfg.h"
#include "board.h"          /* BOARD_APP_XPI_NOR_CFG_OPT_* / HPM_XPI0 / HPM_L1C_CACHELINE_SIZE */
#include "hpm_romapi.h"     /* HPM_XPI0 基址（soc/HPM5E00/HPM5E31/hpm_romapi.h:26） */

/* ---- 板级常量：与 docs/MCAL开发约定.md §2 "片内 Flash" 一致 ---- */
#define MEM_CFG_FLASH_BASE          (0x80000000UL)  /* XPI0 存储器映射窗口起点 */
#define MEM_CFG_FLASH_SIZE          (1024UL * 1024UL)
#define MEM_CFG_SAFE_WINDOW_START   (0x800F0000UL)  /* 最后 64KB，留给 MCAL 例程 */
#define MEM_CFG_SAFE_WINDOW_END     (0x80100000UL)
#define MEM_CFG_SECTOR_SIZE         (4096UL)        /* XPI NOR 扇区 4KB */
#define MEM_CFG_PAGE_SIZE           (256UL)         /* XPI NOR 页 256B */
#define MEM_CFG_READ_CHUNK          (512UL)         /* 每次 MainFunction 读 512B */

/* ---- 实例表（单实例：片内 1MB XPI NOR） ---- */
const Mem_InstanceConfigType Mem_InstanceConfigTable[] = {
    /* baseAddress, size, eraseWindowStart, eraseWindowEnd, sectorSize, pageSize, readChunk, ecc */
    { MEM_CFG_FLASH_BASE, MEM_CFG_FLASH_SIZE,
      MEM_CFG_SAFE_WINDOW_START, MEM_CFG_SAFE_WINDOW_END,
      MEM_CFG_SECTOR_SIZE, MEM_CFG_PAGE_SIZE, MEM_CFG_READ_CHUNK, FALSE }
};

const uint8 Mem_InstanceConfigCount =
    (uint8)(sizeof(Mem_InstanceConfigTable) / sizeof(Mem_InstanceConfigTable[0]));

/* ---- post-build 配置实例 ---- */
const Mem_ConfigType Mem_Config = {
    /* numInstances      */ 1U,
    /* hwBaseAddress     */ (uint32)(uintptr_t)HPM_XPI0,
    /* hwCfgOptionHeader */ (uint32)BOARD_APP_XPI_NOR_CFG_OPT_HDR,
    /* hwCfgOption0      */ (uint32)BOARD_APP_XPI_NOR_CFG_OPT_OPT0,
    /* hwCfgOption1      */ (uint32)BOARD_APP_XPI_NOR_CFG_OPT_OPT1,
    /* instances         */ Mem_InstanceConfigTable
};
