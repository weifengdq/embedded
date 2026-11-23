/*
 * Gpt.c —— AUTOSAR GPT Driver 实现（HPM5E31 GPTMR）
 *
 * 参考：
 *   AUTOSAR_CP_SWS_GPTDriver §8.3（API 语义/状态机）、§8.3.4~8.3.14 DET 规则；
 *   HPM5E31 RM 第 15 章 GPTMR（CR/RLD/CMP/CNT 与状态位）；
 *   本书 docs/chapters/15-gptmr.md（GPTMR 寄存器级实测与坑）。
 *
 * 设计要点：
 *   1) 逻辑通道 → GPTMR 实例/通道的映射全部来自 Gpt_PBcfg.c，不在此硬编码；
 *   2) 连续模式下计数器 0..Value-1 自动回绕，GetTimeElapsed 直接读 CNT，
 *      GetTimeRemaining = Value - CNT，均无临界区（32 位 RV32 读原子）；
 *   3) one-shot 模式靠 reload 中断在目标时刻停表并置 EXPIRED，即使未使能
 *      notification 也开该中断（状态机需要），见第 45 章"坑 45-x"；
 *   4) Predef Timer 用 GPTMR0 CH3 自由运行计数器（10 ns 分辨率），
 *      1us/100us 时基在软件里做整数除法换算，避免占用额外硬件定时器。
 */
#include "Gpt.h"
#include "Gpt_PBcfg.h"
#include "Det.h"
#include "hpm_soc.h"
#include "hpm_clock_drv.h"
#include "hpm_gptmr_drv.h"
#include "hpm_interrupt.h"

/* ---- 驱动/通道模式状态 ---- */
#define GPT_STATE_UNINIT (0u)
#define GPT_STATE_NORMAL (1u)
#define GPT_STATE_SLEEP  (2u)

/* ---- 通道状态（SWS_Gpt §8.3 状态表：initialized/running/stopped/expired） ---- */
#define GPT_CH_STATE_INITIALIZED (0u)
#define GPT_CH_STATE_RUNNING     (1u)
#define GPT_CH_STATE_STOPPED     (2u)
#define GPT_CH_STATE_EXPIRED     (3u)

typedef struct
{
    uint8         state;          /* GPT_CH_STATE_xxx                    */
    boolean       notifyEnabled;  /* SWS_Gpt_00286/00287 存储属性         */
    Gpt_ValueType target;         /* StartTimer 传入的目标 tick 数        */
    Gpt_ValueType elapsedAtStop;  /* 停表时刻已计数值（stopped 时使用）   */
    Gpt_ValueType remainAtStop;   /* 停表时刻剩余值（stopped 时使用）     */
    uint32        clockHz;        /* 该通道 GPTMR 实例时钟（Hz）          */
} Gpt_ChannelRuntimeType;

/* ---- 模块内部状态 ---- */
static uint8                 s_mode = GPT_STATE_UNINIT;
static const Gpt_ConfigType *s_cfg = NULL_PTR;
static Gpt_ChannelRuntimeType s_ch[GPT_NUM_CHANNELS];
static uint32                s_predefClockHz;
static uint32                s_predefTicksPerUs;

/* ---- DET 便捷宏（instance 用逻辑通道号，便于 Det 计数回溯） ---- */
#if (GPT_DEV_ERROR_DETECT == STD_ON)
#define GPT_DET(api, err)                MCAL_DET_REPORT(GPT_MODULE_ID, 0u, (api), (err))
#define GPT_DET_CH(ch, api, err)         MCAL_DET_REPORT(GPT_MODULE_ID, (uint8)(ch), (api), (err))
#define GPT_RUNTIME_DET(api, err)        ((void)Det_ReportRuntimeError((uint16)GPT_MODULE_ID, 0u, (uint8)(api), (uint8)(err)))
#define GPT_RUNTIME_DET_CH(ch, api, err) ((void)Det_ReportRuntimeError((uint16)GPT_MODULE_ID, (uint8)(ch), (uint8)(api), (uint8)(err)))
#else
#define GPT_DET(api, err)                ((void)0)
#define GPT_DET_CH(ch, api, err)         ((void)0)
#define GPT_RUNTIME_DET(api, err)        ((void)0)
#define GPT_RUNTIME_DET_CH(ch, api, err) ((void)0)
#endif

#define GPT_BASE(pch) ((GPTMR_Type *)(uintptr_t)(pch)->base)

/* ---------- 内部工具函数 ---------- */
static boolean gpt_is_initialized(void)
{
    return (s_mode != GPT_STATE_UNINIT) ? TRUE : FALSE;
}

static boolean gpt_channel_valid(Gpt_ChannelType Channel)
{
    if ((s_cfg == NULL_PTR) || (s_cfg->channels == NULL_PTR))
    {
        return FALSE;
    }
    return (Channel < s_cfg->numChannels) ? TRUE : FALSE;
}

/* 把通道硬件配置成 "计到 Value 个 tick 后回绕并在回绕时产生 RLD 中断" */
static void gpt_hw_program(Gpt_ChannelType Channel, Gpt_ValueType Value)
{
    const Gpt_ChannelConfigType *pch = &s_cfg->channels[Channel];
    GPTMR_Type *base = GPT_BASE(pch);
    gptmr_channel_config_t hwcfg;

    gptmr_stop_counter(base, pch->hwChannel);
    gptmr_channel_get_default_config(base, &hwcfg);
    hwcfg.mode = gptmr_work_mode_no_capture;
    hwcfg.reload = (uint32)Value;              /* 硬件 RLD = Value-1，一个周期 = Value tick */
    hwcfg.cmp[0] = 0xFFFFFFFFuL;
    hwcfg.cmp[1] = 0xFFFFFFFFuL;
    hwcfg.enable_cmp_output = false;           /* 纯定时，无输出 */
    hwcfg.cmp_initial_polarity_high = false;
    hwcfg.debug_mode = false;
    (void)gptmr_channel_config(base, pch->hwChannel, &hwcfg, false);
    gptmr_channel_reset_count(base, pch->hwChannel);
    gptmr_clear_status(base, GPTMR_CH_RLD_STAT_MASK(pch->hwChannel));
}

static void gpt_irq_enable(Gpt_ChannelType Channel)
{
    const Gpt_ChannelConfigType *pch = &s_cfg->channels[Channel];
    gptmr_enable_irq(GPT_BASE(pch), GPTMR_CH_RLD_IRQ_MASK(pch->hwChannel));
    intc_m_enable_irq_with_priority((uint32)pch->irq, GPT_IRQ_PRIORITY);
}

static void gpt_irq_disable(Gpt_ChannelType Channel)
{
    const Gpt_ChannelConfigType *pch = &s_cfg->channels[Channel];
    gptmr_disable_irq(GPT_BASE(pch), GPTMR_CH_RLD_IRQ_MASK(pch->hwChannel));
    gptmr_clear_status(GPT_BASE(pch), GPTMR_CH_RLD_STAT_MASK(pch->hwChannel));
}

/* 停表并记录 elapsed/remaining（SWS_Gpt_00297 / 00303：stopped 时返回停表瞬间值） */
static void gpt_hw_stop_and_latch(Gpt_ChannelType Channel)
{
    const Gpt_ChannelConfigType *pch = &s_cfg->channels[Channel];
    GPTMR_Type *base = GPT_BASE(pch);
    uint32 cnt = gptmr_channel_get_counter(base, pch->hwChannel, gptmr_counter_type_normal);

    gptmr_stop_counter(base, pch->hwChannel);
    s_ch[Channel].elapsedAtStop = (Gpt_ValueType)cnt;
    s_ch[Channel].remainAtStop = (cnt < s_ch[Channel].target) ? (Gpt_ValueType)(s_ch[Channel].target - cnt) : 0u;
    s_ch[Channel].state = GPT_CH_STATE_STOPPED;
}

/* ---------- SWS_Gpt §8.3.1 Gpt_GetVersionInfo（Service ID 0x00） ---------- */
void Gpt_GetVersionInfo(Std_VersionInfoType *VersionInfoPtr)
{
#if (GPT_VERSION_INFO_API == STD_ON)
    if (VersionInfoPtr == NULL_PTR)
    {
        GPT_DET(GPT_API_ID_GET_VERSION_INFO, GPT_E_PARAM_POINTER);
        return;
    }
    VersionInfoPtr->vendorID = GPT_VENDOR_ID;
    VersionInfoPtr->moduleID = (uint16)GPT_MODULE_ID;
    VersionInfoPtr->sw_major_version = (uint8)GPT_SW_MAJOR_VERSION;
    VersionInfoPtr->sw_minor_version = (uint8)GPT_SW_MINOR_VERSION;
    VersionInfoPtr->sw_patch_version = (uint8)GPT_SW_PATCH_VERSION;
#else
    (void)VersionInfoPtr;
    GPT_DET(GPT_API_ID_GET_VERSION_INFO, GPT_E_PARAM_POINTER);
#endif
}

/* ---------- SWS_Gpt §8.3.2 Gpt_Init（Service ID 0x01） ---------- */
void Gpt_Init(const Gpt_ConfigType *ConfigPtr)
{
    const Gpt_ConfigType *cfg;
    uint8 i;

    if (gpt_is_initialized() == TRUE)
    {
        GPT_DET(GPT_API_ID_INIT, GPT_E_ALREADY_INITIALIZED);
        return;
    }

    /* post-build 单配置集：ConfigPtr 为 NULL 时使用默认集 Gpt_Config（见 README/第 45 章） */
    cfg = (ConfigPtr != NULL_PTR) ? ConfigPtr : (&Gpt_Config);
    if ((cfg->channels == NULL_PTR) || (cfg->numChannels == 0u) || (cfg->numChannels > GPT_NUM_CHANNELS))
    {
        GPT_DET(GPT_API_ID_INIT, GPT_E_INIT_FAILED);
        return;
    }

    s_cfg = cfg;
    for (i = 0u; i < cfg->numChannels; i++)
    {
        const Gpt_ChannelConfigType *pch = &cfg->channels[i];
        GPTMR_Type *base;

        s_ch[i].state = GPT_CH_STATE_INITIALIZED;
        s_ch[i].notifyEnabled = FALSE;
        s_ch[i].target = 0u;
        s_ch[i].elapsedAtStop = 0u;
        s_ch[i].remainAtStop = 0u;

        clock_add_to_group((clock_name_t)pch->clockName, 0);
        s_ch[i].clockHz = clock_get_frequency((clock_name_t)pch->clockName);
        if ((s_ch[i].clockHz < GPT_MIN_CLOCK_HZ) || (pch->hwChannel > 3u))
        {
            GPT_DET(GPT_API_ID_INIT, GPT_E_INIT_FAILED);
            return;
        }

        base = GPT_BASE(pch);
        gptmr_stop_counter(base, pch->hwChannel);
        gpt_irq_disable(i);       /* SWS_Gpt_00107：Init 关闭所有 notification 中断 */
    }

    /* SWS_Gpt_00390：Init 启动所有使能的 Predef Timer，并从 0 开始计 */
    s_predefTicksPerUs = 0u;
    s_predefClockHz = 0u;
#if (GPT_PREDEF_TIMER_SUPPORTED == STD_ON)
    if (cfg->predefTimer != NULL_PTR)
    {
        const Gpt_PredefTimerConfigType *pp = cfg->predefTimer;
        GPTMR_Type *pbase = (GPTMR_Type *)(uintptr_t)pp->base;
        gptmr_channel_config_t hwcfg;
        uint32 perUs;

        clock_add_to_group((clock_name_t)pp->clockName, 0);
        s_predefClockHz = clock_get_frequency((clock_name_t)pp->clockName);
        perUs = (s_predefClockHz >= 1000000uL) ? (s_predefClockHz / 1000000uL) : 0u;
        if (perUs == 0u)
        {
            GPT_DET(GPT_API_ID_INIT, GPT_E_INIT_FAILED);
            return;
        }
        s_predefTicksPerUs = perUs;

        gptmr_stop_counter(pbase, pp->hwChannel);
        gptmr_channel_get_default_config(pbase, &hwcfg);
        hwcfg.mode = gptmr_work_mode_no_capture;
        hwcfg.reload = 0xFFFFFFFFuL;   /* 32 位自由运行，约 42.9 s 回绕一次 */
        hwcfg.cmp[0] = 0xFFFFFFFFuL;
        hwcfg.cmp[1] = 0xFFFFFFFFuL;
        hwcfg.enable_cmp_output = false;
        (void)gptmr_channel_config(pbase, pp->hwChannel, &hwcfg, false);
        gptmr_channel_reset_count(pbase, pp->hwChannel);
        gptmr_start_counter(pbase, pp->hwChannel);
    }
#endif

    s_mode = GPT_STATE_NORMAL;     /* SWS_Gpt_00339：Init 后等价于 SetMode(NORMAL) */
}

/* ---------- SWS_Gpt §8.3.3 Gpt_DeInit（Service ID 0x02） ---------- */
void Gpt_DeInit(void)
{
    uint8 i;

#if (GPT_DEINIT_API == STD_ON)
    if (gpt_is_initialized() == FALSE)
    {
        GPT_DET(GPT_API_ID_DEINIT, GPT_E_UNINIT);
        return;
    }

    for (i = 0u; i < s_cfg->numChannels; i++)
    {
        const Gpt_ChannelConfigType *pch = &s_cfg->channels[i];

        /* SWS_Gpt_00234：有通道仍在跑时上报运行时错误 GPT_E_BUSY（仍完成反初始化） */
        if (s_ch[i].state == GPT_CH_STATE_RUNNING)
        {
            GPT_RUNTIME_DET_CH(i, GPT_API_ID_DEINIT, GPT_E_BUSY);
        }
        gptmr_stop_counter(GPT_BASE(pch), pch->hwChannel);
        gpt_irq_disable(i);
        intc_m_disable_irq((uint32)pch->irq);
        s_ch[i].state = GPT_CH_STATE_INITIALIZED;
        s_ch[i].notifyEnabled = FALSE;
    }

#if (GPT_PREDEF_TIMER_SUPPORTED == STD_ON)
    if (s_cfg->predefTimer != NULL_PTR)
    {
        gptmr_stop_counter((GPTMR_Type *)(uintptr_t)s_cfg->predefTimer->base,
                           s_cfg->predefTimer->hwChannel);
    }
#endif
    s_predefTicksPerUs = 0u;
    s_predefClockHz = 0u;
    s_cfg = NULL_PTR;
    s_mode = GPT_STATE_UNINIT;     /* SWS_Gpt_00363 */
#else
    (void)i;
    GPT_DET(GPT_API_ID_DEINIT, GPT_E_UNINIT);
#endif
}

/* ---------- SWS_Gpt §8.3.4 Gpt_GetTimeElapsed（Service ID 0x03） ---------- */
Gpt_ValueType Gpt_GetTimeElapsed(Gpt_ChannelType Channel)
{
    Gpt_ValueType ret = 0u;

#if (GPT_TIME_ELAPSED_API == STD_ON)
    const Gpt_ChannelConfigType *pch;

    if (gpt_is_initialized() == FALSE)
    {
        GPT_DET(GPT_API_ID_GET_TIME_ELAPSED, GPT_E_UNINIT);
        return 0u;
    }
    if (gpt_channel_valid(Channel) == FALSE)
    {
        GPT_DET_CH(Channel, GPT_API_ID_GET_TIME_ELAPSED, GPT_E_PARAM_CHANNEL);
        return 0u;
    }
    pch = &s_cfg->channels[Channel];

    switch (s_ch[Channel].state)
    {
    case GPT_CH_STATE_RUNNING:
        /* 连续模式：回绕后 CNT 即"距上次 recurrence 的已计数值"（SWS_Gpt_00361） */
        ret = (Gpt_ValueType)gptmr_channel_get_counter(GPT_BASE(pch), pch->hwChannel,
                                                      gptmr_counter_type_normal);
        break;
    case GPT_CH_STATE_STOPPED:
        ret = s_ch[Channel].elapsedAtStop;
        break;
    case GPT_CH_STATE_EXPIRED:
        ret = s_ch[Channel].target;   /* SWS_Gpt_00299：expired 返回目标值 */
        break;
    default: /* INITIALIZED：从未启动过，返回 0（SWS_Gpt_00295） */
        ret = 0u;
        break;
    }
#else
    (void)Channel;
    GPT_DET(GPT_API_ID_GET_TIME_ELAPSED, GPT_E_UNINIT);
#endif
    return ret;
}

/* ---------- SWS_Gpt §8.3.5 Gpt_GetTimeRemaining（Service ID 0x04） ---------- */
Gpt_ValueType Gpt_GetTimeRemaining(Gpt_ChannelType Channel)
{
    Gpt_ValueType ret = 0u;

#if (GPT_TIME_REMAINING_API == STD_ON)
    const Gpt_ChannelConfigType *pch;
    uint32 cnt;

    if (gpt_is_initialized() == FALSE)
    {
        GPT_DET(GPT_API_ID_GET_TIME_REMAINING, GPT_E_UNINIT);
        return 0u;
    }
    if (gpt_channel_valid(Channel) == FALSE)
    {
        GPT_DET_CH(Channel, GPT_API_ID_GET_TIME_REMAINING, GPT_E_PARAM_CHANNEL);
        return 0u;
    }
    pch = &s_cfg->channels[Channel];

    switch (s_ch[Channel].state)
    {
    case GPT_CH_STATE_RUNNING:
        cnt = gptmr_channel_get_counter(GPT_BASE(pch), pch->hwChannel, gptmr_counter_type_normal);
        ret = (cnt < s_ch[Channel].target) ? (Gpt_ValueType)(s_ch[Channel].target - cnt) : 0u;
        break;
    case GPT_CH_STATE_STOPPED:
        ret = s_ch[Channel].remainAtStop;
        break;
    default: /* INITIALIZED / EXPIRED：SWS_Gpt_00301 / 00305 返回 0 */
        ret = 0u;
        break;
    }
#else
    (void)Channel;
    GPT_DET(GPT_API_ID_GET_TIME_REMAINING, GPT_E_UNINIT);
#endif
    return ret;
}

/* ---------- SWS_Gpt §8.3.6 Gpt_StartTimer（Service ID 0x05） ---------- */
void Gpt_StartTimer(Gpt_ChannelType Channel, Gpt_ValueType Value)
{
    const Gpt_ChannelConfigType *pch;

    if (gpt_is_initialized() == FALSE)
    {
        GPT_DET(GPT_API_ID_START_TIMER, GPT_E_UNINIT);
        return;
    }
    if (gpt_channel_valid(Channel) == FALSE)
    {
        GPT_DET_CH(Channel, GPT_API_ID_START_TIMER, GPT_E_PARAM_CHANNEL);
        return;
    }
    if ((Value == 0u) || (Value > GPT_MAX_TICKS))
    {
        GPT_DET_CH(Channel, GPT_API_ID_START_TIMER, GPT_E_PARAM_VALUE);
        return;
    }
    if (s_ch[Channel].state == GPT_CH_STATE_RUNNING)
    {
        GPT_RUNTIME_DET_CH(Channel, GPT_API_ID_START_TIMER, GPT_E_BUSY);
        return;
    }

    pch = &s_cfg->channels[Channel];
    s_ch[Channel].target = Value;
    gpt_hw_program(Channel, Value);

    /* one-shot 必须开 RLD 中断才能在目标时刻停表（即使用户未使能 notification） */
    if ((pch->mode == GPT_CHANNEL_MODE_ONESHOT) || (s_ch[Channel].notifyEnabled == TRUE))
    {
        gpt_irq_enable(Channel);
    }
    s_ch[Channel].state = GPT_CH_STATE_RUNNING;   /* SWS_Gpt_00364 */
    gptmr_start_counter(GPT_BASE(pch), pch->hwChannel);
}

/* ---------- SWS_Gpt §8.3.7 Gpt_StopTimer（Service ID 0x06） ---------- */
void Gpt_StopTimer(Gpt_ChannelType Channel)
{
    if (gpt_is_initialized() == FALSE)
    {
        GPT_DET(GPT_API_ID_STOP_TIMER, GPT_E_UNINIT);
        return;
    }
    if (gpt_channel_valid(Channel) == FALSE)
    {
        GPT_DET_CH(Channel, GPT_API_ID_STOP_TIMER, GPT_E_PARAM_CHANNEL);
        return;
    }
    /* SWS_Gpt_00344：initialized/stopped/expired 状态下调用"无动作、无 DET" */
    if (s_ch[Channel].state != GPT_CH_STATE_RUNNING)
    {
        return;
    }
    gpt_hw_stop_and_latch(Channel);
}

/* ---------- SWS_Gpt §8.3.8 Gpt_EnableNotification（Service ID 0x07） ---------- */
void Gpt_EnableNotification(Gpt_ChannelType Channel)
{
    if (gpt_is_initialized() == FALSE)
    {
        GPT_DET(GPT_API_ID_ENABLE_NOTIFICATION, GPT_E_UNINIT);
        return;
    }
    if (gpt_channel_valid(Channel) == FALSE)
    {
        GPT_DET_CH(Channel, GPT_API_ID_ENABLE_NOTIFICATION, GPT_E_PARAM_CHANNEL);
        return;
    }
    s_ch[Channel].notifyEnabled = TRUE;
    gpt_irq_enable(Channel);
}

/* ---------- SWS_Gpt §8.3.9 Gpt_DisableNotification（Service ID 0x08） ---------- */
void Gpt_DisableNotification(Gpt_ChannelType Channel)
{
    const Gpt_ChannelConfigType *pch;

    if (gpt_is_initialized() == FALSE)
    {
        GPT_DET(GPT_API_ID_DISABLE_NOTIFICATION, GPT_E_UNINIT);
        return;
    }
    if (gpt_channel_valid(Channel) == FALSE)
    {
        GPT_DET_CH(Channel, GPT_API_ID_DISABLE_NOTIFICATION, GPT_E_PARAM_CHANNEL);
        return;
    }
    s_ch[Channel].notifyEnabled = FALSE;
    pch = &s_cfg->channels[Channel];
    /* one-shot 通道仍需 RLD 中断驱动状态机，只关通知不关中断 */
    if (pch->mode != GPT_CHANNEL_MODE_ONESHOT)
    {
        gpt_irq_disable(Channel);
    }
}

/* ---------- SWS_Gpt §8.3.10 Gpt_SetMode（Service ID 0x09） ---------- */
void Gpt_SetMode(Gpt_ModeType Mode)
{
    uint8 i;

    if (gpt_is_initialized() == FALSE)
    {
        GPT_DET(GPT_API_ID_SET_MODE, GPT_E_UNINIT);
        return;
    }
    if ((Mode != GPT_MODE_NORMAL) && (Mode != GPT_MODE_SLEEP))
    {
        GPT_DET(GPT_API_ID_SET_MODE, GPT_E_PARAM_MODE);
        return;
    }

    if (Mode == GPT_MODE_SLEEP)
    {
        for (i = 0u; i < s_cfg->numChannels; i++)
        {
            /* SWS_Gpt_00164：sleep 下非唤醒通道一律停表并置 stopped */
            if (s_ch[i].state == GPT_CH_STATE_RUNNING)
            {
                gpt_hw_stop_and_latch(i);
                gpt_irq_disable(i);
            }
        }
#if (GPT_PREDEF_TIMER_SUPPORTED == STD_ON)
        if (s_cfg->predefTimer != NULL_PTR)
        {
            gptmr_stop_counter((GPTMR_Type *)(uintptr_t)s_cfg->predefTimer->base,
                               s_cfg->predefTimer->hwChannel);   /* SWS_Gpt_00393 */
        }
#endif
        s_mode = GPT_STATE_SLEEP;
    }
    else
    {
        s_mode = GPT_STATE_NORMAL;
        /* SWS_Gpt_00392：从 sleep 回 normal 时 Predef Timer 重新从 0 启动 */
#if (GPT_PREDEF_TIMER_SUPPORTED == STD_ON)
        if ((s_cfg->predefTimer != NULL_PTR) && (s_predefTicksPerUs != 0u))
        {
            GPTMR_Type *pbase = (GPTMR_Type *)(uintptr_t)s_cfg->predefTimer->base;
            gptmr_channel_config_t hwcfg;

            gptmr_stop_counter(pbase, s_cfg->predefTimer->hwChannel);
            gptmr_channel_get_default_config(pbase, &hwcfg);
            hwcfg.mode = gptmr_work_mode_no_capture;
            hwcfg.reload = 0xFFFFFFFFuL;
            hwcfg.cmp[0] = 0xFFFFFFFFuL;
            hwcfg.cmp[1] = 0xFFFFFFFFuL;
            hwcfg.enable_cmp_output = false;
            (void)gptmr_channel_config(pbase, s_cfg->predefTimer->hwChannel, &hwcfg, false);
            gptmr_channel_reset_count(pbase, s_cfg->predefTimer->hwChannel);
            gptmr_start_counter(pbase, s_cfg->predefTimer->hwChannel);
        }
#endif
        /* SWS_Gpt_00165：回 normal 不自动重启被 sleep 停掉的通道 */
    }
}

/* ---------- SWS_Gpt §8.3.14 Gpt_GetPredefTimerValue（Service ID 0x0D） ---------- */
Std_ReturnType Gpt_GetPredefTimerValue(Gpt_PredefTimerType PredefTimer, uint32 *TimeValuePtr)
{
    uint32 cnt;
    uint32 val;

    if (PredefTimer > GPT_PREDEF_TIMER_100US_32BIT)
    {
        GPT_DET(GPT_API_ID_GET_PREDEF_TIMER_VALUE, GPT_E_PARAM_PREDEF_TIMER);
        return E_NOT_OK;
    }
    if (TimeValuePtr == NULL_PTR)
    {
        GPT_DET(GPT_API_ID_GET_PREDEF_TIMER_VALUE, GPT_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    if (gpt_is_initialized() == FALSE)
    {
        GPT_DET(GPT_API_ID_GET_PREDEF_TIMER_VALUE, GPT_E_UNINIT);
        return E_NOT_OK;
    }
    if (s_mode == GPT_STATE_SLEEP)
    {
        /* SWS_Gpt_00401：sleep 模式上报运行时错误 GPT_E_MODE 并返回 E_NOT_OK */
        GPT_RUNTIME_DET(GPT_API_ID_GET_PREDEF_TIMER_VALUE, GPT_E_MODE);
        return E_NOT_OK;
    }
#if (GPT_PREDEF_TIMER_SUPPORTED == STD_ON)
    if ((s_cfg->predefTimer == NULL_PTR) || (s_predefTicksPerUs == 0u))
    {
        GPT_DET(GPT_API_ID_GET_PREDEF_TIMER_VALUE, GPT_E_PARAM_PREDEF_TIMER);
        return E_NOT_OK;
    }

    cnt = gptmr_channel_get_counter((GPTMR_Type *)(uintptr_t)s_cfg->predefTimer->base,
                                    s_cfg->predefTimer->hwChannel, gptmr_counter_type_normal);
    switch (PredefTimer)
    {
    case GPT_PREDEF_TIMER_1US_16BIT:
        val = (cnt / s_predefTicksPerUs) & 0x0000FFFFuL;
        break;
    case GPT_PREDEF_TIMER_1US_24BIT:
        val = (cnt / s_predefTicksPerUs) & 0x00FFFFFFuL;
        break;
    case GPT_PREDEF_TIMER_1US_32BIT:
        val = cnt / s_predefTicksPerUs;
        break;
    default: /* GPT_PREDEF_TIMER_100US_32BIT */
        val = cnt / (s_predefTicksPerUs * 100uL);
        break;
    }
    *TimeValuePtr = val;
#else
    *TimeValuePtr = 0u;
    GPT_DET(GPT_API_ID_GET_PREDEF_TIMER_VALUE, GPT_E_PARAM_PREDEF_TIMER);
    return E_NOT_OK;
#endif
    return E_OK;
}

/* ---------- ISR 分发（由 Gpt_PBcfg.c 的向量桩调用） ----------
 * ISR 只做：清状态位 → 状态推进 → 调通知。不做 printf/浮点（契约 §4）。
 */
void Gpt_IsrHandler(Gpt_ChannelType Channel)
{
    const Gpt_ChannelConfigType *pch;
    GPTMR_Type *base;
    uint32 mask;

    if ((s_cfg == NULL_PTR) || (Channel >= s_cfg->numChannels))
    {
        return;
    }
    pch = &s_cfg->channels[Channel];
    base = GPT_BASE(pch);
    mask = (uint32)GPTMR_CH_RLD_STAT_MASK(pch->hwChannel);
    if (gptmr_check_status(base, mask) == false)
    {
        return;
    }
    gptmr_clear_status(base, mask);

    if (pch->mode == GPT_CHANNEL_MODE_ONESHOT)
    {
        /* 目标时刻到：停表、置 expired（SWS_Gpt_00299 / 00305 依赖该状态） */
        gptmr_stop_counter(base, pch->hwChannel);
        s_ch[Channel].elapsedAtStop = s_ch[Channel].target;
        s_ch[Channel].remainAtStop = 0u;
        s_ch[Channel].state = GPT_CH_STATE_EXPIRED;
    }

    if ((s_ch[Channel].notifyEnabled == TRUE) && (pch->notification != NULL_PTR))
    {
        pch->notification();
    }
}
