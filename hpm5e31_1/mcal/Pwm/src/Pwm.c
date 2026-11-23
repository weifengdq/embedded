/*
 * Pwm.c —— AUTOSAR PWM Driver 实现（HPM5E31 PWM0 / PWMv2）
 *
 * 参考：
 *   AUTOSAR_CP_SWS_PWMDriver §8.3（API 语义）、§7.3.1 开发错误表；
 *   HPM5E31 RM 第 37 章 PWMv2：影子寄存器（§37.4.5 p.610）、
 *   计数器/输出/比较器映射（§37.4.8 p.613，表 14-4）；
 *   本书 docs/chapters/14-pwmv2-hrpwm.md（实测：CMP 事件依赖各计数器重载值、E00038）。
 *
 * 关键实现约定（第 45 章有表）：
 *   1) 一个 PWMv2 实例内所有计数器的重载值都绑定到同一个影子 0，并统一
 *      reload_update_on_shlk —— 否则 CMP4..CMP7 事件会被静默丢弃（ch14 坑 14-3）；
 *   2) 通道相位 = 配置里的 phasePermille，上升沿比较值 a = T*phase/1000，
 *      下降沿 b = a + 占空比 ticks（按周期回绕）；
 *   3) 0% / 100% 占空比用 b=a / b=T 两个特例实现，不依赖比较器边界行为；
 *   4) 通知：a>0 时上升沿用 CMP(a)，a==0 时上升沿就是重载事件，用重载中断。
 */
#include "Pwm.h"
#include "Pwm_PBcfg.h"
#include "Det.h"
#include "hpm_soc.h"
#include "hpm_iomux.h"
#include "hpm_clock_drv.h"
#include "hpm_pwmv2_drv.h"
#include "hpm_interrupt.h"

/* 影子寄存器 0 = 计数器重载值（与 Pwm_PBcfg.c 的 #define 一致） */
#define PWM_SHADOW_RELOAD (0u)

typedef struct
{
    uint32  period;      /* 当前周期（tick）              */
    uint32  a;           /* 上升沿比较值（tick）          */
    uint32  b;           /* 下降沿比较值（tick）          */
    uint16  duty;        /* 0..0x8000                     */
    uint8   channelClass;
    uint8   notifyType;  /* Pwm_EdgeNotificationType      */
    boolean notifyEnabled;
    boolean idleForced;
} Pwm_ChannelRuntimeType;

static boolean                    s_pwm_init = FALSE;
static const Pwm_ConfigType      *s_pwm_cfg = NULL_PTR;
static Pwm_ChannelRuntimeType     s_rt[PWM_NUM_CHANNELS];

#if (PWM_DEV_ERROR_DETECT == STD_ON)
#define PWM_DET(api, err)         MCAL_DET_REPORT(PWM_MODULE_ID, 0u, (api), (err))
#define PWM_DET_CH(ch, api, err)  MCAL_DET_REPORT(PWM_MODULE_ID, (uint8)(ch), (api), (err))
#else
#define PWM_DET(api, err)         ((void)0)
#define PWM_DET_CH(ch, api, err)  ((void)0)
#endif

#define PWM_BASE(pch) ((PWMV2_Type *)(uintptr_t)(pch)->base)

/* ---------- 内部工具 ---------- */
static boolean pwm_ch_valid(Pwm_ChannelType ch)
{
    if ((s_pwm_cfg == NULL_PTR) || (s_pwm_cfg->channels == NULL_PTR))
    {
        return FALSE;
    }
    return (ch < s_pwm_cfg->numChannels) ? TRUE : FALSE;
}

static uint32 pwm_duty_ticks(uint32 period, uint16 duty)
{
    uint32 ticks = (uint32)(((uint64)period * (uint64)duty) / (uint64)PWM_DUTY_100_PERCENT);
    return (ticks > period) ? period : ticks;
}

/* 由 period/duty/phase 重算 a、b（相位按千分比，占空比按 0..0x8000 标度） */
static void pwm_recalc_edges(Pwm_ChannelType ch)
{
    const Pwm_ChannelConfigType *pch = &s_pwm_cfg->channels[ch];
    uint32 period = s_rt[ch].period;
    uint32 a = (uint32)(((uint64)period * (uint64)pch->phasePermille) / 1000ULL);
    uint32 b;
    uint32 dutyTicks;

    if (a >= period)
    {
        a = 0u;
    }
    s_rt[ch].a = a;

    if (s_rt[ch].duty == 0u)
    {
        s_rt[ch].b = a;                 /* 恒低：区间 [a,a) 为空 */
    }
    else if (s_rt[ch].duty >= PWM_DUTY_100_PERCENT)
    {
        s_rt[ch].a = 0u;                /* 恒高：整周期都在 [0,T) 内 */
        s_rt[ch].b = period;
    }
    else
    {
        dutyTicks = pwm_duty_ticks(period, s_rt[ch].duty);
        b = a + dutyTicks;
        if (b > period)
        {
            b -= period;
        }
        if (b == a)
        {
            b = (a == 0u) ? period : a; /* 极小占空比退化保护 */
            s_rt[ch].b = (s_rt[ch].duty > 0u) ? period : a;
        }
        else
        {
            s_rt[ch].b = b;
        }
    }
}

/* 把当前 period/a/b 写进影子寄存器并完成计数器/比较器绑定 */
static void pwm_program(Pwm_ChannelType ch)
{
    const Pwm_ChannelConfigType *pch = &s_pwm_cfg->channels[ch];
    PWMV2_Type *base = PWM_BASE(pch);
    uint32 i;

    pwmv2_shadow_register_unlock(base);
    pwmv2_set_shadow_val(base, PWM_SHADOW_RELOAD, s_rt[ch].period, 0u, false);
    pwmv2_set_shadow_val(base, pch->shadowA, s_rt[ch].a, 0u, false);
    pwmv2_set_shadow_val(base, pch->shadowB, s_rt[ch].b, 0u, false);

    /* 4 个计数器都指向影子 0：CMP 事件依赖对应计数器的重载值（RM §37.4.8 / 坑 14-3） */
    for (i = 0u; i < 4u; i++)
    {
        pwmv2_counter_select_data_offset_from_shadow_value(base, (pwm_counter_t)i, PWM_SHADOW_RELOAD);
        pwmv2_set_reload_update_time(base, (pwm_counter_t)i, pwm_reload_update_on_shlk);
        pwmv2_counter_burst_disable(base, (pwm_counter_t)i);
    }
    pwmv2_select_cmp_source(base, pch->cmpA, cmp_value_from_shadow_val, pch->shadowA);
    pwmv2_select_cmp_source(base, pch->cmpB, cmp_value_from_shadow_val, pch->shadowB);
    pwmv2_shadow_register_lock(base);
}

static void pwm_irq_apply(Pwm_ChannelType ch)
{
    const Pwm_ChannelConfigType *pch = &s_pwm_cfg->channels[ch];
    PWMV2_Type *base = PWM_BASE(pch);
    uint32 cmpA = 1UL << (uint32)pch->cmpA;
    uint32 cmpB = 1UL << (uint32)pch->cmpB;

    pwmv2_clear_cmp_irq_status(base, cmpA | cmpB);
    pwmv2_clear_reload_irq_status(base, 1UL << (uint32)pch->counter);
    if (s_rt[ch].notifyEnabled == FALSE)
    {
        (void)pwmv2_disable_cmp_irq(base, pch->cmpA);
        (void)pwmv2_disable_cmp_irq(base, pch->cmpB);
        pwmv2_disable_reload_irq(base, (pwm_counter_t)pch->counter);
        return;
    }
    if ((s_rt[ch].notifyType == PWM_RISING_EDGE) || (s_rt[ch].notifyType == PWM_BOTH_EDGES))
    {
        if (s_rt[ch].a > 0u)
        {
            (void)pwmv2_enable_cmp_irq(base, pch->cmpA);
        }
        else
        {
            pwmv2_enable_reload_irq(base, (pwm_counter_t)pch->counter);
        }
    }
    if ((s_rt[ch].notifyType == PWM_FALLING_EDGE) || (s_rt[ch].notifyType == PWM_BOTH_EDGES))
    {
        (void)pwmv2_enable_cmp_irq(base, pch->cmpB);
    }
    intc_m_enable_irq_with_priority((uint32)s_pwm_cfg->irq, PWM_IRQ_PRIORITY);
}

/* ---------- SWS_Pwm §8.3.1 Pwm_Init（Service ID 0x00） ---------- */
void Pwm_Init(const Pwm_ConfigType *ConfigPtr)
{
    const Pwm_ConfigType *cfg;
    uint32 i;

    if (s_pwm_init == TRUE)
    {
        PWM_DET(PWM_API_ID_INIT, PWM_E_ALREADY_INITIALIZED);
        return;
    }
    cfg = (ConfigPtr != NULL_PTR) ? ConfigPtr : (&Pwm_Config);
    if ((cfg->channels == NULL_PTR) || (cfg->numChannels == 0u) || (cfg->numChannels > PWM_NUM_CHANNELS))
    {
        PWM_DET(PWM_API_ID_INIT, PWM_E_INIT_FAILED);
        return;
    }
    s_pwm_cfg = cfg;

    for (i = 0u; i < cfg->numChannels; i++)
    {
        const Pwm_ChannelConfigType *pch = &cfg->channels[i];

        clock_add_to_group((clock_name_t)pch->clockName, 0);
        /* 引脚复用与电气属性（板级参数来自 PBcfg） */
        {
            IOC_Type *ioc = (IOC_Type *)(uintptr_t)pch->iocBase;
            ioc->PAD[pch->padIndex].FUNC_CTL = pch->padFunc;
            if (pch->padCtl != 0u)
            {
                ioc->PAD[pch->padIndex].PAD_CTL = pch->padCtl;
            }
        }

        s_rt[i].period = (pch->defaultPeriod > PWM_MAX_PERIOD_TICKS) ? PWM_MAX_PERIOD_TICKS : pch->defaultPeriod;
        s_rt[i].duty = pch->defaultDuty;
        s_rt[i].channelClass = pch->channelClass;
        s_rt[i].notifyType = PWM_RISING_EDGE;
        s_rt[i].notifyEnabled = FALSE;
        s_rt[i].idleForced = FALSE;
        pwm_recalc_edges((Pwm_ChannelType)i);
    }

    /* 硬件初始化：先复位 PWMv2，再写影子/绑定，最后开输出 */
    pwmv2_deinit(PWM_BASE(&cfg->channels[0]));
    for (i = 0u; i < cfg->numChannels; i++)
    {
        pwm_program((Pwm_ChannelType)i);
    }
    for (i = 0u; i < cfg->numChannels; i++)
    {
        const Pwm_ChannelConfigType *pch = &cfg->channels[i];
        PWMV2_Type *base = PWM_BASE(pch);

        pwmv2_disable_four_cmp(base, (pwm_channel_t)pch->hwChannel);
        pwmv2_disable_pair_mode(base, (pwm_channel_t)pch->hwChannel);
        pwmv2_channel_enable_output(base, (pwm_channel_t)pch->hwChannel);
        pwmv2_enable_counter(base, (pwm_counter_t)pch->counter);
        pwmv2_start_pwm_output(base, (pwm_counter_t)pch->counter);
        pwm_irq_apply((Pwm_ChannelType)i);
    }
    s_pwm_init = TRUE;
}

/* ---------- SWS_Pwm §8.3.2 Pwm_DeInit（Service ID 0x01） ---------- */
void Pwm_DeInit(void)
{
    uint32 i;

    if (s_pwm_init == FALSE)
    {
        PWM_DET(PWM_API_ID_DEINIT, PWM_E_UNINIT);
        return;
    }
    for (i = 0u; i < s_pwm_cfg->numChannels; i++)
    {
        const Pwm_ChannelConfigType *pch = &s_pwm_cfg->channels[i];
        PWMV2_Type *base = PWM_BASE(pch);

        s_rt[i].notifyEnabled = FALSE;
        pwm_irq_apply((Pwm_ChannelType)i);
        pwmv2_channel_disable_output(base, (pwm_channel_t)pch->hwChannel);
        /* SDK 无 pwmv2_stop_pwm_output()，用关计数器实现"停止输出"（RM §37.4.9） */
        pwmv2_disable_counter(base, (pwm_counter_t)pch->counter);
    }
    pwmv2_deinit(PWM_BASE(&s_pwm_cfg->channels[0]));
    intc_m_disable_irq((uint32)s_pwm_cfg->irq);
    s_pwm_init = FALSE;
    s_pwm_cfg = NULL_PTR;
}

/* ---------- SWS_Pwm §8.3.3 Pwm_SetDutyCycle（Service ID 0x02） ---------- */
void Pwm_SetDutyCycle(Pwm_ChannelType ChannelNumber, uint16 DutyCycle)
{
    if (s_pwm_init == FALSE)
    {
        PWM_DET(PWM_API_ID_SET_DUTY_CYCLE, PWM_E_UNINIT);
        return;
    }
    if (pwm_ch_valid(ChannelNumber) == FALSE)
    {
        PWM_DET_CH(ChannelNumber, PWM_API_ID_SET_DUTY_CYCLE, PWM_E_PARAM_CHANNEL);
        return;
    }
    if (DutyCycle > PWM_DUTY_100_PERCENT)
    {
        PWM_DET_CH(ChannelNumber, PWM_API_ID_SET_DUTY_CYCLE, DET_E_PARAM_VALUE);
        return;
    }
    if (s_rt[ChannelNumber].idleForced == TRUE)
    {
        const Pwm_ChannelConfigType *pch = &s_pwm_cfg->channels[ChannelNumber];
        pwmv2_disable_force_by_software(PWM_BASE(pch), (pwm_channel_t)pch->hwChannel);
        s_rt[ChannelNumber].idleForced = FALSE;
    }
    s_rt[ChannelNumber].duty = DutyCycle;
    pwm_recalc_edges(ChannelNumber);
    pwm_program(ChannelNumber);
}

/* ---------- SWS_Pwm §8.3.4 Pwm_SetPeriodAndDuty（Service ID 0x03） ---------- */
void Pwm_SetPeriodAndDuty(Pwm_ChannelType ChannelNumber, Pwm_PeriodType Period, uint16 DutyCycle)
{
    const Pwm_ChannelConfigType *pch;
    uint32 i;

    if (s_pwm_init == FALSE)
    {
        PWM_DET(PWM_API_ID_SET_PERIOD_AND_DUTY, PWM_E_UNINIT);
        return;
    }
    if (pwm_ch_valid(ChannelNumber) == FALSE)
    {
        PWM_DET_CH(ChannelNumber, PWM_API_ID_SET_PERIOD_AND_DUTY, PWM_E_PARAM_CHANNEL);
        return;
    }
    if ((Period == 0u) || (Period > PWM_MAX_PERIOD_TICKS) || (DutyCycle > PWM_DUTY_100_PERCENT))
    {
        PWM_DET_CH(ChannelNumber, PWM_API_ID_SET_PERIOD_AND_DUTY, DET_E_PARAM_VALUE);
        return;
    }
    if (s_rt[ChannelNumber].channelClass == PWM_FIXED_PERIOD)
    {
        PWM_DET_CH(ChannelNumber, PWM_API_ID_SET_PERIOD_AND_DUTY, PWM_E_PERIOD_UNCHANGEABLE);
        return;
    }
    pch = &s_pwm_cfg->channels[ChannelNumber];

    /* 同一计数器上的通道共享周期：全部一起改（本配置 2 个通道都在计数器 1） */
    for (i = 0u; i < s_pwm_cfg->numChannels; i++)
    {
        if (s_pwm_cfg->channels[i].counter == pch->counter)
        {
            s_rt[i].period = Period;
            pwm_recalc_edges((Pwm_ChannelType)i);
        }
    }
    s_rt[ChannelNumber].duty = DutyCycle;
    pwm_recalc_edges(ChannelNumber);
    for (i = 0u; i < s_pwm_cfg->numChannels; i++)
    {
        if (s_pwm_cfg->channels[i].counter == pch->counter)
        {
            pwm_program((Pwm_ChannelType)i);
        }
    }
}

/* ---------- SWS_Pwm §8.3.5 Pwm_SetOutputToIdle（Service ID 0x04） ---------- */
void Pwm_SetOutputToIdle(Pwm_ChannelType ChannelNumber)
{
    const Pwm_ChannelConfigType *pch;
    pwm_force_mode_t mode;

    if (s_pwm_init == FALSE)
    {
        PWM_DET(PWM_API_ID_SET_OUTPUT_TO_IDLE, PWM_E_UNINIT);
        return;
    }
    if (pwm_ch_valid(ChannelNumber) == FALSE)
    {
        PWM_DET_CH(ChannelNumber, PWM_API_ID_SET_OUTPUT_TO_IDLE, PWM_E_PARAM_CHANNEL);
        return;
    }
    pch = &s_pwm_cfg->channels[ChannelNumber];
    mode = (pch->idleState == PWM_HIGH) ? pwm_force_output_1 : pwm_force_output_0;
    pwmv2_force_output(PWM_BASE(pch), (pwm_channel_t)pch->hwChannel, mode, false);
    pwmv2_enable_force_by_software(PWM_BASE(pch), (pwm_channel_t)pch->hwChannel);
    s_rt[ChannelNumber].idleForced = TRUE;
}

/* ---------- SWS_Pwm §8.3.6 Pwm_GetOutputState（Service ID 0x05） ---------- */
Pwm_OutputStateType Pwm_GetOutputState(Pwm_ChannelType ChannelNumber)
{
    const Pwm_ChannelConfigType *pch;
    uint32 cnt;
    uint32 a;
    uint32 b;
    boolean high;

    if (s_pwm_init == FALSE)
    {
        PWM_DET(PWM_API_ID_GET_OUTPUT_STATE, PWM_E_UNINIT);
        return PWM_LOW;
    }
    if (pwm_ch_valid(ChannelNumber) == FALSE)
    {
        PWM_DET_CH(ChannelNumber, PWM_API_ID_GET_OUTPUT_STATE, PWM_E_PARAM_CHANNEL);
        return PWM_LOW;
    }
    if (s_rt[ChannelNumber].idleForced == TRUE)
    {
        return (s_pwm_cfg->channels[ChannelNumber].idleState == PWM_HIGH) ? PWM_HIGH : PWM_LOW;
    }
    pch = &s_pwm_cfg->channels[ChannelNumber];
    /* PWMv2 的 CNT_VAL 是 24.8 定点（整数在 bit31:8） */
    cnt = pwmv2_get_counter_value(PWM_BASE(pch), (pwm_counter_t)pch->counter) >> 8;
    a = s_rt[ChannelNumber].a;
    b = s_rt[ChannelNumber].b;

    if (a < b)
    {
        high = ((cnt >= a) && (cnt < b)) ? TRUE : FALSE;
    }
    else if (a > b)
    {
        high = ((cnt >= a) || (cnt < b)) ? TRUE : FALSE;
    }
    else
    {
        high = FALSE;   /* a==b：0% 占空比（恒低） */
    }
    return (high == TRUE) ? PWM_HIGH : PWM_LOW;
}

/* ---------- SWS_Pwm §8.3.7 Pwm_DisableNotification（Service ID 0x06） ---------- */
void Pwm_DisableNotification(Pwm_ChannelType ChannelNumber)
{
    if (s_pwm_init == FALSE)
    {
        PWM_DET(PWM_API_ID_DISABLE_NOTIFICATION, PWM_E_UNINIT);
        return;
    }
    if (pwm_ch_valid(ChannelNumber) == FALSE)
    {
        PWM_DET_CH(ChannelNumber, PWM_API_ID_DISABLE_NOTIFICATION, PWM_E_PARAM_CHANNEL);
        return;
    }
    s_rt[ChannelNumber].notifyEnabled = FALSE;
    pwm_irq_apply(ChannelNumber);
}

/* ---------- SWS_Pwm §8.3.8 Pwm_EnableNotification（Service ID 0x07） ---------- */
void Pwm_EnableNotification(Pwm_ChannelType ChannelNumber, Pwm_EdgeNotificationType Notification)
{
    if (s_pwm_init == FALSE)
    {
        PWM_DET(PWM_API_ID_ENABLE_NOTIFICATION, PWM_E_UNINIT);
        return;
    }
    if (pwm_ch_valid(ChannelNumber) == FALSE)
    {
        PWM_DET_CH(ChannelNumber, PWM_API_ID_ENABLE_NOTIFICATION, PWM_E_PARAM_CHANNEL);
        return;
    }
    if (Notification > PWM_BOTH_EDGES)
    {
        PWM_DET_CH(ChannelNumber, PWM_API_ID_ENABLE_NOTIFICATION, DET_E_PARAM_VALUE);
        return;
    }
    s_rt[ChannelNumber].notifyType = Notification;
    s_rt[ChannelNumber].notifyEnabled = TRUE;
    pwm_irq_apply(ChannelNumber);
}

/* ---------- SWS_Pwm §8.3.9 Pwm_GetVersionInfo（Service ID 0x08） ---------- */
void Pwm_GetVersionInfo(Std_VersionInfoType *versioninfo)
{
#if (PWM_VERSION_INFO_API == STD_ON)
    if (versioninfo == NULL_PTR)
    {
        PWM_DET(PWM_API_ID_GET_VERSION_INFO, PWM_E_PARAM_POINTER);
        return;
    }
    versioninfo->vendorID = PWM_VENDOR_ID;
    versioninfo->moduleID = (uint16)PWM_MODULE_ID;
    versioninfo->sw_major_version = (uint8)PWM_SW_MAJOR_VERSION;
    versioninfo->sw_minor_version = (uint8)PWM_SW_MINOR_VERSION;
    versioninfo->sw_patch_version = (uint8)PWM_SW_PATCH_VERSION;
#else
    (void)versioninfo;
    PWM_DET(PWM_API_ID_GET_VERSION_INFO, PWM_E_PARAM_POINTER);
#endif
}

/* ---------- 扩展：Pwm_SetChannelClass（任务书要求；SWS_PWM R25-11 §8.3 无此服务） ---------- */
Std_ReturnType Pwm_SetChannelClass(Pwm_ChannelType ChannelNumber, Pwm_ChannelClassType ChannelClass)
{
    if (s_pwm_init == FALSE)
    {
        PWM_DET(PWM_API_ID_SET_CHANNEL_CLASS, PWM_E_UNINIT);
        return E_NOT_OK;
    }
    if (pwm_ch_valid(ChannelNumber) == FALSE)
    {
        PWM_DET_CH(ChannelNumber, PWM_API_ID_SET_CHANNEL_CLASS, PWM_E_PARAM_CHANNEL);
        return E_NOT_OK;
    }
    if (ChannelClass > PWM_FIXED_PERIOD)
    {
        /* PWM_FIXED_PERIOD_SHIFTED 需要硬件移相锁存，本驱动未实现 */
        PWM_DET_CH(ChannelNumber, PWM_API_ID_SET_CHANNEL_CLASS, DET_E_PARAM_VALUE);
        return E_NOT_OK;
    }
    s_rt[ChannelNumber].channelClass = ChannelClass;
    return E_OK;
}

/* ---------- ISR 分发（由 Pwm_PBcfg.c 的向量桩调用） ----------
 * ISR 只做：清状态位 → 判定边沿类型 → 调通知（契约 §4：ISR 不 printf/不浮点）
 */
void Pwm_IsrHandler(void)
{
    uint32 i;

    if ((s_pwm_init == FALSE) || (s_pwm_cfg == NULL_PTR))
    {
        return;
    }
    for (i = 0u; i < s_pwm_cfg->numChannels; i++)
    {
        const Pwm_ChannelConfigType *pch = &s_pwm_cfg->channels[i];
        PWMV2_Type *base = PWM_BASE(pch);
        uint32 stC = pwmv2_get_cmp_irq_status(base);
        uint32 stR = pwmv2_get_reload_irq_status(base);
        boolean fire = FALSE;

        if (s_rt[i].notifyEnabled == FALSE)
        {
            continue;
        }
        if ((s_rt[i].notifyType == PWM_RISING_EDGE) || (s_rt[i].notifyType == PWM_BOTH_EDGES))
        {
            if (s_rt[i].a > 0u)
            {
                if ((stC & (1UL << (uint32)pch->cmpA)) != 0u)
                {
                    pwmv2_clear_cmp_irq_status(base, 1UL << (uint32)pch->cmpA);
                    fire = TRUE;
                }
            }
            else
            {
                if ((stR & (1UL << (uint32)pch->counter)) != 0u)
                {
                    pwmv2_clear_reload_irq_status(base, 1UL << (uint32)pch->counter);
                    fire = TRUE;
                }
            }
        }
        if ((s_rt[i].notifyType == PWM_FALLING_EDGE) || (s_rt[i].notifyType == PWM_BOTH_EDGES))
        {
            if ((stC & (1UL << (uint32)pch->cmpB)) != 0u)
            {
                pwmv2_clear_cmp_irq_status(base, 1UL << (uint32)pch->cmpB);
                fire = TRUE;
            }
        }
        if ((fire == TRUE) && (pch->notification != NULL_PTR))
        {
            pch->notification();
        }
    }
}