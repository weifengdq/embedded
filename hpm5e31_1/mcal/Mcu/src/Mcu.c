/*
 * Mcu.c —— AUTOSAR CP MCU Driver（HPM5E31 / RV32，寄存器级）
 *
 * 参考：AUTOSAR_CP_SWS_MCUDriver R25-11
 *       §7.2.1 开发错误 / §8.3 函数定义 / §9 序列图
 *       HPM5E31 RM：PLLCTLV2（PLL 倍频器与后分频）、SYSCTL CLOCKx（时钟节点 mux/div）、
 *                   PPOR（RESET_FLAG 复位源、SOFTWARE_RESET 软复位）、MCHTMR（24 MHz 计时）
 * 例程：samples/mcal/mcu_clock_init、samples/mcal/mcu_reset_mode
 *
 * 代码规约：实例/引脚/频率全部来自 Mcu_PBcfg.c（Mcu_Config），本文件不出现 HPM_PPOR 之类实例名。
 */
#include "Mcu.h"
#include "Mcu_PBcfg.h"
#include "Det.h"
#include "hpm_soc.h"

#if (MCU_DEV_ERROR_DETECT == STD_ON)
#define MCU_DET(api, err)  MCAL_DET_REPORT(MCU_MODULE_ID, 0u, (api), (err))
#else
#define MCU_DET(api, err)  ((void)0)
#endif

/* PPOR RESET_FLAG 位定义（RM）：对应 Mcu_GetResetReason/GetResetRawValue 的映射 */
#define MCU_PPOR_FLAG_BROWNOUT   (0x00000001u)
#define MCU_PPOR_FLAG_TEMPERATURE (0x00000002u)
#define MCU_PPOR_FLAG_DEBUG      (0x00000010u)
#define MCU_PPOR_FLAG_JTAG_SOFT  (0x00000020u)
#define MCU_PPOR_FLAG_WDOG0      (0x00010000u)
#define MCU_PPOR_FLAG_WDOG1      (0x00020000u)
#define MCU_PPOR_FLAG_WDOG2      (0x00040000u)
#define MCU_PPOR_FLAG_WDOG3      (0x00080000u)
#define MCU_PPOR_FLAG_PMIC_WDOG  (0x01000000u)
#define MCU_PPOR_FLAG_JTAG_IEEE  (0x40000000u)
#define MCU_PPOR_FLAG_SOFTWARE   (0x80000000u)
#define MCU_PPOR_FLAG_ALL        (0xFFFFFFFFu)

/* 时钟源 -> (PLL 序号, PLL 后分频序号)，与 SYSCTL CLOCKx.MUX 编码一致 */
typedef struct
{
    uint8 pll;
    uint8 clk;
} mcu_clk_src_map_t;

static const mcu_clk_src_map_t s_src_map[8] =
{
    { 0xFFu, 0u },  /* 0: osc24m */
    { 0u, 0u },     /* 1: pll0_clk0 */
    { 0u, 1u },     /* 2: pll0_clk1 */
    { 1u, 0u },     /* 3: pll1_clk0 */
    { 1u, 1u },     /* 4: pll1_clk1 */
    { 1u, 2u },     /* 5: pll1_clk2 */
    { 2u, 0u },     /* 6: pll2_clk0 */
    { 2u, 1u },     /* 7: pll2_clk1 */
};

static const Mcu_ConfigType *s_cfg = NULL_PTR;
static boolean              s_initialized = FALSE;
static Mcu_RawResetType     s_resetRaw = 0u;
static Mcu_ClockType        s_activeClock = MCU_CLOCK_SETTING_0;
static boolean              s_pllDistributed = FALSE;
static Mcu_ModeType         s_mode = MCU_MODE_RUN;

/* ---------------------------------------------------------------- 内部工具 */

static SYSCTL_Type *mcu_sysctl(const Mcu_ConfigType *cfg)
{
    return (SYSCTL_Type *)cfg->sysctlBase;
}

static PPOR_Type *mcu_ppor(const Mcu_ConfigType *cfg)
{
    return (PPOR_Type *)cfg->pporBase;
}

static MCHTMR_Type *mcu_mchtmr(const Mcu_ConfigType *cfg)
{
    return (MCHTMR_Type *)cfg->mchtmrBase;
}

/* PLL 输出频率：f = 24 MHz * (MFI + MFN/MFD) */
static uint32 mcu_pll_freq_hz(uint32 pllBase, uint8 pllIdx)
{
    PLLCTLV2_Type *pll = (PLLCTLV2_Type *)pllBase;
    uint32 mfi = PLLCTLV2_PLL_MFI_MFI_GET(pll->PLL[pllIdx].MFI);
    uint32 mfn = PLLCTLV2_PLL_MFN_MFN_GET(pll->PLL[pllIdx].MFN);
    uint32 mfd = PLLCTLV2_PLL_MFD_MFD_GET(pll->PLL[pllIdx].MFD);
    uint64 freq = (uint64)MCAL_BOARD_XTAL_HZ * (uint64)mfi;
    if (mfd != 0u)
    {
        freq += ((uint64)MCAL_BOARD_XTAL_HZ * (uint64)mfn) / (uint64)mfd;
    }
    return (uint32)freq;
}

/* PLL 后分频输出频率：分频比 = DIV/5 + 1 */
static uint32 mcu_pll_postdiv_hz(uint32 pllBase, uint8 pllIdx, uint8 clkIdx)
{
    PLLCTLV2_Type *pll = (PLLCTLV2_Type *)pllBase;
    uint32 raw = PLLCTLV2_PLL_DIV_DIV_GET(pll->PLL[pllIdx].DIV[clkIdx]);
    uint32 freq = mcu_pll_freq_hz(pllBase, pllIdx);
    return (uint32)(((uint64)freq * 5u) / ((uint64)raw + 5u));
}

static uint32 mcu_source_hz(uint32 pllBase, uint32 oscHz, uint8 src)
{
    uint32 freq;
    if (src == 0u)
    {
        freq = oscHz;
    }
    else if (src < 8u)
    {
        freq = mcu_pll_postdiv_hz(pllBase, s_src_map[src].pll, s_src_map[src].clk);
    }
    else
    {
        freq = 0u;
    }
    return freq;
}

static uint64 mcu_ticks(const Mcu_ConfigType *cfg)
{
    return mcu_mchtmr(cfg)->MTIME;
}

/* 等 PLL 锁定，超时按 mchtmr(24 MHz) 计时，避免死等 */
static boolean mcu_wait_pll_locked(const Mcu_ConfigType *cfg, const Mcu_PllConfigType *p, uint32 timeoutUs)
{
    PLLCTLV2_Type *pll = (PLLCTLV2_Type *)p->pllBase;
    uint64 start = mcu_ticks(cfg);
    uint64 limit = (uint64)timeoutUs * (uint64)(MCAL_BOARD_XTAL_HZ / 1000000u);
    boolean locked = FALSE;

    while (1)
    {
        uint32 mfi = pll->PLL[p->pllIndex].MFI;
        boolean enabled = ((mfi & PLLCTLV2_PLL_MFI_ENABLE_MASK) != 0u);
        boolean busy = ((mfi & PLLCTLV2_PLL_MFI_BUSY_MASK) != 0u);
        boolean resp = ((mfi & PLLCTLV2_PLL_MFI_RESPONSE_MASK) != 0u);
        if (enabled && (!busy) && resp)
        {
            locked = TRUE;
            break;
        }
        if ((mcu_ticks(cfg) - start) > limit)
        {
            break;
        }
    }
    return locked;
}

/* 写 SYSCTL 时钟节点：mux + div（div 寄存器值 = 实际分频 - 1） */
static void mcu_write_clock_node(const Mcu_ClockNodeConfigType *node)
{
    SYSCTL_Type *sysctl = (SYSCTL_Type *)node->sysctlBase;
    uint32 value = sysctl->CLOCK[node->node];

    value &= ~(SYSCTL_CLOCK_MUX_MASK | SYSCTL_CLOCK_DIV_MASK);
    value |= SYSCTL_CLOCK_MUX_SET(node->src) | SYSCTL_CLOCK_DIV_SET(node->divMinusOne);
    sysctl->CLOCK[node->node] = value;

    /* LOC_BUSY 清零表示时钟节点切换完成（RM SYSCTL CLOCKx） */
    while ((sysctl->CLOCK[node->node] & SYSCTL_CLOCK_LOC_BUSY_MASK) != 0u)
    {
        /* 等待硬件完成 */
    }
}

/* 只有当目标值不同才写 PLL 寄存器：避免无谓的 PLL 重新锁定把 CPU 时钟打断 */
static Std_ReturnType mcu_apply_pll(const Mcu_ConfigType *cfg, const Mcu_PllConfigType *p)
{
    PLLCTLV2_Type *pll = (PLLCTLV2_Type *)p->pllBase;
    uint32 curMfi = PLLCTLV2_PLL_MFI_MFI_GET(pll->PLL[p->pllIndex].MFI);
    uint32 curMfn = PLLCTLV2_PLL_MFN_MFN_GET(pll->PLL[p->pllIndex].MFN);
    uint32 curMfd = PLLCTLV2_PLL_MFD_MFD_GET(pll->PLL[p->pllIndex].MFD);
    boolean pllEnabled = ((pll->PLL[p->pllIndex].MFI & PLLCTLV2_PLL_MFI_ENABLE_MASK) != 0u);

    if ((curMfi != (uint32)p->mfi) || (curMfn != p->mfn) || (curMfd != p->mfd) || (!pllEnabled))
    {
        /* RM：MFD 必须在 PLL 关闭时写；MFN 可在运行时写；写 MFI 触发 PLL 启动/重新锁定 */
        if ((curMfd != p->mfd) || (!pllEnabled))
        {
            pll->PLL[p->pllIndex].CONFIG &= ~PLLCTLV2_PLL_CONFIG_REFSEL_MASK; /* 0 = XTAL24M */
            pll->PLL[p->pllIndex].MFD = p->mfd;
        }
        pll->PLL[p->pllIndex].MFN = p->mfn;
        pll->PLL[p->pllIndex].MFI = (uint32)p->mfi;
    }

    if (!mcu_wait_pll_locked(cfg, p, p->lockTimeoutUs))
    {
        return E_NOT_OK;
    }

    /* 后分频：只有值变化才写（写 DIV 会触发后分频器重新同步） */
    if (PLLCTLV2_PLL_DIV_DIV_GET(pll->PLL[p->pllIndex].DIV[0]) != (uint32)p->clk0PostDiv)
    {
        pll->PLL[p->pllIndex].DIV[0] &= ~PLLCTLV2_PLL_DIV_DIV_MASK;
        pll->PLL[p->pllIndex].DIV[0] |= PLLCTLV2_PLL_DIV_DIV_SET(p->clk0PostDiv);
    }
    if (PLLCTLV2_PLL_DIV_DIV_GET(pll->PLL[p->pllIndex].DIV[1]) != (uint32)p->clk1PostDiv)
    {
        pll->PLL[p->pllIndex].DIV[1] &= ~PLLCTLV2_PLL_DIV_DIV_MASK;
        pll->PLL[p->pllIndex].DIV[1] |= PLLCTLV2_PLL_DIV_DIV_SET(p->clk1PostDiv);
    }
    return E_OK;
}

static void mcu_apply_clock_setting(const Mcu_ConfigType *cfg, Mcu_ClockType clockSetting)
{
    const Mcu_ClockSettingConfigType *cs = &cfg->clockSettings[clockSetting];
    uint8 i;

    for (i = 0u; i < cs->clockNodeCount; i++)
    {
        mcu_write_clock_node(&cs->clockNodes[i]);
    }
}

static Mcu_ResetType mcu_map_reset_reason(Mcu_RawResetType raw)
{
    Mcu_ResetType reason;

    if ((raw & MCU_PPOR_FLAG_SOFTWARE) != 0u)
    {
        reason = MCU_SW_RESET;
    }
    else if ((raw & (MCU_PPOR_FLAG_WDOG0 | MCU_PPOR_FLAG_WDOG1 | MCU_PPOR_FLAG_WDOG2 |
                     MCU_PPOR_FLAG_WDOG3 | MCU_PPOR_FLAG_PMIC_WDOG)) != 0u)
    {
        reason = MCU_WATCHDOG_RESET;
    }
    else if ((raw & (MCU_PPOR_FLAG_BROWNOUT | MCU_PPOR_FLAG_TEMPERATURE)) != 0u)
    {
        reason = MCU_POWER_ON_RESET;
    }
    else
    {
        /* 上电/调试器/未知来源：AUTOSAR 只定义 4 种，统一归到 UNDEFINED */
        reason = MCU_RESET_UNDEFINED;
    }
    return reason;
}

/* ---------------------------------------------------------------- API 实现 */

void Mcu_Init(const Mcu_ConfigType *ConfigPtr)
{
    if (ConfigPtr == NULL_PTR)
    {
        MCU_DET(MCU_API_ID_MCU_INIT, MCU_E_PARAM_CONFIG);
        return;
    }
    if ((ConfigPtr->clockSettings == NULL_PTR) || (ConfigPtr->clockSettingCount == 0u) ||
        (ConfigPtr->clockSettingCount > MCU_MAX_CLOCK_SETTINGS))
    {
        MCU_DET(MCU_API_ID_MCU_INIT, MCU_E_PARAM_CONFIG);
        return;
    }

    s_cfg = ConfigPtr;
    /* 复位源在 Init 时快照一次：SWS_Mcu_00158 要求多次调用返回值一致 */
    s_resetRaw = mcu_ppor(ConfigPtr)->RESET_FLAG;
    s_pllDistributed = FALSE;
    s_mode = MCU_MODE_RUN;
    s_activeClock = MCU_CLOCK_SETTING_0;
    s_initialized = TRUE;
}

Std_ReturnType Mcu_InitRamSection(uint16 RamSection)
{
    (void)RamSection;
    if (!s_initialized)
    {
        MCU_DET(MCU_API_ID_MCU_INITRAM_SECTION, MCU_E_UNINIT);
        return E_NOT_OK;
    }
    /* MCU_INIT_RAM_SECTION_API = STD_OFF：RAM 由启动代码清零，见 Mcu_Cfg.h 说明 */
    MCU_DET(MCU_API_ID_MCU_INITRAM_SECTION, MCU_E_PARAM_RAMSECTION);
    return E_NOT_OK;
}

Std_ReturnType Mcu_InitClock(Mcu_ClockType ClockSetting)
{
    const Mcu_ClockSettingConfigType *cs;

    if (!s_initialized)
    {
        MCU_DET(MCU_API_ID_MCU_INIT_CLOCK, MCU_E_UNINIT);
        return E_NOT_OK;
    }
    if ((uint32)ClockSetting >= (uint32)s_cfg->clockSettingCount)
    {
        MCU_DET(MCU_API_ID_MCU_INIT_CLOCK, MCU_E_PARAM_CLOCK);
        return E_NOT_OK;
    }

    cs = &s_cfg->clockSettings[ClockSetting];
    if ((cs->pll == NULL_PTR) || (cs->clockNodes == NULL_PTR) || (cs->clockNodeCount > MCU_MAX_CLOCK_NODES))
    {
        MCU_DET(MCU_API_ID_MCU_INIT_CLOCK, MCU_E_PARAM_CLOCK);
        return E_NOT_OK;
    }

    if (mcu_apply_pll(s_cfg, cs->pll) != E_OK)
    {
        MCU_DET(MCU_API_ID_MCU_INIT_CLOCK, MCU_E_INIT_FAILED);
        return E_NOT_OK;
    }

    mcu_apply_clock_setting(s_cfg, ClockSetting);
    s_activeClock = ClockSetting;
    return E_OK;
}

Std_ReturnType Mcu_DistributePllClock(void)
{
    if (!s_initialized)
    {
        MCU_DET(MCU_API_ID_MCU_DISTRIBUTE_PLL_CLOCK, MCU_E_UNINIT);
        return E_NOT_OK;
    }
    if (s_cfg->clockSettings[s_activeClock].pll == NULL_PTR)
    {
        MCU_DET(MCU_API_ID_MCU_DISTRIBUTE_PLL_CLOCK, MCU_E_PARAM_CONFIG);
        return E_NOT_OK;
    }
    if (Mcu_GetPllStatus() != MCU_PLL_LOCKED)
    {
        /* SWS_Mcu_00142：PLL 未锁定立刻返回 E_NOT_OK */
        MCU_DET(MCU_API_ID_MCU_DISTRIBUTE_PLL_CLOCK, MCU_E_PLL_NOT_LOCKED);
        return E_NOT_OK;
    }

    /*
     * HPM5E31 的"PLL 时钟分配"由 SYSCTL 时钟节点 MUX 完成（没有单独的 PLL 使能门）。
     * Mcu_InitClock 已写入节点；这里再幂等确认一次 CPU 节点指向 PLL，并记录已分配。
     */
    mcu_apply_clock_setting(s_cfg, s_activeClock);
    s_pllDistributed = TRUE;
    return E_OK;
}

Mcu_PllStatusType Mcu_GetPllStatus(void)
{
    PLLCTLV2_Type *pll;
    const Mcu_PllConfigType *p;
    uint32 mfi;

    if (!s_initialized)
    {
        /* SWS_Mcu_00132：Mcu_Init 之前返回 UNDEFINED */
        MCU_DET(MCU_API_ID_MCU_GET_PLL_STATUS, MCU_E_UNINIT);
        return MCU_PLL_STATUS_UNDEFINED;
    }

    p = s_cfg->clockSettings[s_activeClock].pll;
    if (p == NULL_PTR)
    {
        return MCU_PLL_STATUS_UNDEFINED;
    }
    pll = (PLLCTLV2_Type *)p->pllBase;
    mfi = pll->PLL[p->pllIndex].MFI;

    if (((mfi & PLLCTLV2_PLL_MFI_ENABLE_MASK) == 0u) || ((mfi & PLLCTLV2_PLL_MFI_BUSY_MASK) != 0u))
    {
        return MCU_PLL_UNLOCKED;
    }
    if ((mfi & PLLCTLV2_PLL_MFI_RESPONSE_MASK) == 0u)
    {
        return MCU_PLL_UNLOCKED;
    }
    return MCU_PLL_LOCKED;
}

Mcu_ResetType Mcu_GetResetReason(void)
{
    if (!s_initialized)
    {
        /* SWS_Mcu_00133：Init 之前返回 MCU_RESET_UNDEFINED */
        MCU_DET(MCU_API_ID_MCU_GET_RESET_REASON, MCU_E_UNINIT);
        return MCU_RESET_UNDEFINED;
    }
    return mcu_map_reset_reason(s_resetRaw);
}

Mcu_RawResetType Mcu_GetResetRawValue(void)
{
    if (!s_initialized)
    {
        /* SWS_Mcu_00135：Init 之前返回非 0 且不对应有效复位标志的值 */
        MCU_DET(MCU_API_ID_MCU_GET_RESET_RAW_VALUE, MCU_E_UNINIT);
        return MCU_PPOR_FLAG_ALL;
    }
    return s_resetRaw;
}

void Mcu_PerformReset(void)
{
    if (!s_initialized)
    {
        MCU_DET(MCU_API_ID_MCU_PERFORM_RESET, MCU_E_UNINIT);
        return;
    }
    /* RM PPOR SOFTWARE_RESET：计数值按 24 MHz 递减，减到 2 时触发软复位 */
    mcu_ppor(s_cfg)->SOFTWARE_RESET = (uint32)s_cfg->resetDelayTicks;
    for (;;)
    {
        /* 等待芯片复位；正常情况下不会返回 */
        __asm__ volatile("nop");
    }
}

void Mcu_SetMode(Mcu_ModeType McuMode)
{
    if (!s_initialized)
    {
        MCU_DET(MCU_API_ID_MCU_SET_MODE, MCU_E_UNINIT);
        return;
    }
    if ((uint32)McuMode >= (uint32)s_cfg->modeCount)
    {
        MCU_DET(MCU_API_ID_MCU_SET_MODE, MCU_E_PARAM_MODE);
        return;
    }

    switch (McuMode)
    {
    case MCU_MODE_RUN:
        s_mode = MCU_MODE_RUN;
        break;

    case MCU_MODE_SLEEP:
        /*
         * HPM5E31 低功耗：这里只演示 CPU 级 WFI（内核停时钟，外设照常）。
         * 唤醒源由调用者先配置（本仓库 Mcu 例程不演示 SLEEP，见章节 44 "已知限制"）。
         */
        s_mode = MCU_MODE_SLEEP;
        __asm__ volatile("wfi");
        s_mode = MCU_MODE_RUN;
        break;

    case MCU_MODE_RESET:
        Mcu_PerformReset();
        break;

    default:
        MCU_DET(MCU_API_ID_MCU_SET_MODE, MCU_E_PARAM_MODE);
        break;
    }
}

void Mcu_GetVersionInfo(Std_VersionInfoType *versioninfo)
{
    if (versioninfo == NULL_PTR)
    {
        MCU_DET(MCU_API_ID_MCU_GET_VERSION_INFO, MCU_E_PARAM_POINTER);
        return;
    }
    versioninfo->vendorID = MCU_VENDOR_ID;
    versioninfo->moduleID = MCU_MODULE_ID;
    versioninfo->sw_major_version = MCU_SW_MAJOR_VERSION;
    versioninfo->sw_minor_version = MCU_SW_MINOR_VERSION;
    versioninfo->sw_patch_version = MCU_SW_PATCH_VERSION;
}

Mcu_RamStateType Mcu_GetRamState(void)
{
    if (!s_initialized)
    {
        MCU_DET(MCU_API_ID_MCU_GET_RAM_STATE, MCU_E_UNINIT);
        return MCU_RAMSTATE_INVALID;
    }
    /* MCU_GET_RAM_STATE_API = STD_OFF：HPM5E31 无 RAM 内容有效标志 */
    return MCU_RAMSTATE_INVALID;
}

/* ---------------------------------------------------------------- 扩展 API */

uint32 Mcu_GetCoreClockHz(void)
{
    if (!s_initialized)
    {
        return 0u;
    }
    return Mcu_GetClockNodeHz(s_cfg->coreClockNode);
}

uint32 Mcu_GetClockNodeHz(uint8 node)
{
    SYSCTL_Type *sysctl;
    uint32 reg;
    uint32 srcFreq;
    uint32 div;
    uint32 mux;

    if (!s_initialized)
    {
        return 0u;
    }
    sysctl = mcu_sysctl(s_cfg);
    reg = sysctl->CLOCK[node];
    mux = SYSCTL_CLOCK_MUX_GET(reg);
    div = 1u + SYSCTL_CLOCK_DIV_GET(reg);
    srcFreq = mcu_source_hz(s_cfg->clockSettings[0].pll->pllBase, s_cfg->oscFreqHz, (uint8)mux);
    return srcFreq / div;
}

void Mcu_ClearResetFlags(void)
{
    if (!s_initialized)
    {
        return;
    }
    mcu_ppor(s_cfg)->RESET_FLAG = MCU_PPOR_FLAG_ALL; /* W1C */
    s_resetRaw = 0u;
}

boolean Mcu_IsInitialized(void)
{
    return s_initialized;
}
