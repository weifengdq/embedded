/*
 * Mcu_PBcfg.c —— MCU post-build 配置实例（HPM5E31 / hpm5e31_1）
 *
 * 数据来源：boards/hpm5e31_1/clock.c（init_board_clock_source）与 board.h 的 BOARD_CPU_FREQ；
 *           HPM5E31 手册 PLLCTLV2 / SYSCTL / PPOR 章节。
 * 改板原则：只在"板级映射表"里改地址与频率，驱动源代码不出现任何实例名/引脚名。
 */
#include "Mcu_PBcfg.h"
#include "hpm_soc.h"
#include "hpm_pllctlv2_drv.h"   /* pllctlv2_div_2p0 等后分频枚举 */

/* ---- PLL 设置 0：PLL0 = 24 MHz x 40 = 960 MHz，clk0 /2 = 480 MHz，clk1 /2.4 = 400 MHz ---- */
static const Mcu_PllConfigType Mcu_PllConfig_0 =
{
    .iocBase      = (uint32)HPM_IOC_BASE,
    .pllBase      = (uint32)HPM_PLLCTLV2_BASE,
    .pllIndex     = (uint16)PLLCTLV2_PLL_PLL0,
    .mfi          = (uint16)40u,          /* 24 MHz * 40 = 960 MHz */
    .mfn          = 0u,
    .mfd          = 240000000u,           /* PLLCTLV2 默认 MFD，与 SDK 一致 */
    .clk0PostDiv  = (uint8)pllctlv2_div_2p0,   /* /2.0 -> 480 MHz */
    .clk1PostDiv  = (uint8)pllctlv2_div_2p4,   /* /2.4 -> 400 MHz */
    .reserved0    = 0u,
    .lockTimeoutUs = 1000u
};

/* 设置 0 的时钟节点：CPU0 = pll0_clk0 / 1 = 480 MHz；MCHTMR0 = osc24m / 1 = 24 MHz */
static const Mcu_ClockNodeConfigType Mcu_ClockNodes_0[] =
{
    { .sysctlBase = (uint32)HPM_SYSCTL_BASE, .node = MCU_PBCFG_NODE_CPU0,
      .src = MCU_PBCFG_SRC_PLL0_CLK0, .divMinusOne = 0u, .reserved0 = 0u },
    { .sysctlBase = (uint32)HPM_SYSCTL_BASE, .node = MCU_PBCFG_NODE_MCHTMR0,
      .src = MCU_PBCFG_SRC_OSC24M,    .divMinusOne = 0u, .reserved0 = 0u },
};

/* ---- 设置 1：PLL 不变，CPU0 节点再 /2 -> 240 MHz（用于实测主频随配置变化） ---- */
static const Mcu_PllConfigType Mcu_PllConfig_1 =
{
    .iocBase      = (uint32)HPM_IOC_BASE,
    .pllBase      = (uint32)HPM_PLLCTLV2_BASE,
    .pllIndex     = (uint16)PLLCTLV2_PLL_PLL0,
    .mfi          = (uint16)40u,
    .mfn          = 0u,
    .mfd          = 240000000u,
    .clk0PostDiv  = (uint8)pllctlv2_div_2p0,
    .clk1PostDiv  = (uint8)pllctlv2_div_2p4,
    .reserved0    = 0u,
    .lockTimeoutUs = 1000u
};

static const Mcu_ClockNodeConfigType Mcu_ClockNodes_1[] =
{
    { .sysctlBase = (uint32)HPM_SYSCTL_BASE, .node = MCU_PBCFG_NODE_CPU0,
      .src = MCU_PBCFG_SRC_PLL0_CLK0, .divMinusOne = 1u, .reserved0 = 0u }, /* /(1+1) = 240 MHz */
    { .sysctlBase = (uint32)HPM_SYSCTL_BASE, .node = MCU_PBCFG_NODE_MCHTMR0,
      .src = MCU_PBCFG_SRC_OSC24M,    .divMinusOne = 0u, .reserved0 = 0u },
};

static const Mcu_ClockSettingConfigType Mcu_ClockSettings[] =
{
    { .pll = &Mcu_PllConfig_0, .clockNodes = Mcu_ClockNodes_0,
      .clockNodeCount = (uint8)(sizeof(Mcu_ClockNodes_0) / sizeof(Mcu_ClockNodes_0[0])), .reserved0 = 0u },
    { .pll = &Mcu_PllConfig_1, .clockNodes = Mcu_ClockNodes_1,
      .clockNodeCount = (uint8)(sizeof(Mcu_ClockNodes_1) / sizeof(Mcu_ClockNodes_1[0])), .reserved0 = 0u },
};

/* post-build 配置实例：Mcu_Init(&Mcu_Config) */
const Mcu_ConfigType Mcu_Config =
{
    .pporBase        = (uint32)HPM_PPOR_BASE,
    .sysctlBase      = (uint32)HPM_SYSCTL_BASE,
    .mchtmrBase      = (uint32)HPM_MCHTMR_BASE,
    .oscFreqHz       = 24000000u,
    .coreClockNode   = MCU_PBCFG_NODE_CPU0,
    .resetDelayTicks = 100u,      /* 100 / 24 MHz ≈ 4.2 us 后触发软件复位 */
    .modeCount       = (uint8)MCU_MODE_COUNT,
    .reserved0       = 0u,
    .clockSettings   = Mcu_ClockSettings,
    .clockSettingCount = (uint8)(sizeof(Mcu_ClockSettings) / sizeof(Mcu_ClockSettings[0])),
    .reserved1       = { 0u, 0u, 0u }
};
