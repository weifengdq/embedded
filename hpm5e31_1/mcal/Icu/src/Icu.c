/*
 * Icu.c —— AUTOSAR ICU Driver 实现（HPM5E31 GPTMR2 输入捕获 + TRGM）
 *
 * 参考：
 *   AUTOSAR_CP_SWS_ICUDriver §8.3（API 语义）、§7.3.1 开发错误表；
 *   HPM5E31 RM GPTMR 捕获：CAPPRD/CAPDTY（measure_width 模式）、CAPVALn（边沿捕获）；
 *   本书 docs/chapters/15-gptmr.md（测宽/测周期实测）、17-sdm-plb-trgm.md（TRGM 路由）。
 *
 * 实现映射（第 45 章有表）：
 *   ICU_MODE_SIGNAL_MEASUREMENT = GPTMR measure_width：硬件自动填周期+高电平时间；
 *   ICU_MODE_TIMESTAMP / EDGE_DETECT = GPTMR capture-at-edge：CAPVALn 存边沿时刻；
 *   边沿极性由 Icu_SetActivationCondition 直接映射到 GPTMR 捕获边沿模式；
 *   捕获输入不是芯片引脚直连，而是 引脚→TRGM_Pxx→TRGM→GPTMR2_CAPT_n（PBcfg 配）。
 */
#include "Icu.h"
#include "Icu_PBcfg.h"
#include "Det.h"
#include "hpm_soc.h"
#include "hpm_iomux.h"
#include "hpm_clock_drv.h"
#include "hpm_gptmr_drv.h"
#include "hpm_trgm_drv.h"
#include "hpm_interrupt.h"

#define ICU_STATE_UNINIT (0u)
#define ICU_STATE_INIT   (1u)
#define ICU_STATE_SLEEP  (2u)

typedef struct
{
    uint8   state;             /* 通道测量状态：0=停止 1=运行        */
    uint8   activation;        /* 当前边沿条件                       */
    uint8   measurementMode;   /* 当前生效的测量模式                 */
    boolean notifyEnabled;
    boolean timestampActive;
    volatile boolean edgeSeen; /* 自上次 GetInputState 以来有边沿    */
    volatile Icu_ValueType lastPeriod;
    volatile Icu_ValueType lastHigh;
    volatile Icu_ValueType lastEdge;
    volatile Icu_ValueType lastDelta;
    volatile Icu_EdgeNumberType edgeCount;
    Icu_ValueType *tsBuffer;
    uint16   tsSize;
    uint16   tsNotifyInterval;
    volatile Icu_IndexType tsIndex;
    volatile uint16 tsCount;
    uint32   clockHz;
} Icu_ChannelRuntimeType;

static uint8                 s_icu_mode = ICU_STATE_UNINIT;
static const Icu_ConfigType *s_icu_cfg = NULL_PTR;
static Icu_ChannelRuntimeType s_icu[ICU_NUM_CHANNELS];

#if (ICU_DEV_ERROR_DETECT == STD_ON)
#define ICU_DET(api, err)        MCAL_DET_REPORT(ICU_MODULE_ID, 0u, (api), (err))
#define ICU_DET_CH(ch, api, err) MCAL_DET_REPORT(ICU_MODULE_ID, (uint8)(ch), (api), (err))
#else
#define ICU_DET(api, err)        ((void)0)
#define ICU_DET_CH(ch, api, err) ((void)0)
#endif

#define ICU_BASE(pch) ((GPTMR_Type *)(uintptr_t)(pch)->base)

static boolean icu_ch_valid(Icu_ChannelType ch)
{
    if ((s_icu_cfg == NULL_PTR) || (s_icu_cfg->channels == NULL_PTR))
    {
        return FALSE;
    }
    return (ch < s_icu_cfg->numChannels) ? TRUE : FALSE;
}

/* 按测量模式/边沿条件配置 GPTMR 捕获通道（并清零计数器） */
static void icu_hw_program(Icu_ChannelType ch)
{
    const Icu_ChannelConfigType *pch = &s_icu_cfg->channels[ch];
    GPTMR_Type *base = ICU_BASE(pch);
    gptmr_channel_config_t hwcfg;

    gptmr_stop_counter(base, pch->hwChannel);
    gptmr_channel_get_default_config(base, &hwcfg);
    if (s_icu[ch].measurementMode == ICU_MODE_SIGNAL_MEASUREMENT)
    {
        hwcfg.mode = gptmr_work_mode_measure_width;
    }
    else
    {
        switch (s_icu[ch].activation)
        {
        case ICU_FALLING_EDGE:
            hwcfg.mode = gptmr_work_mode_capture_at_falling_edge;
            break;
        case ICU_BOTH_EDGES:
            hwcfg.mode = gptmr_work_mode_capture_at_both_edge;
            break;
        default:
            hwcfg.mode = gptmr_work_mode_capture_at_rising_edge;
            break;
        }
    }
    hwcfg.reload = 0xFFFFFFFFuL;
    hwcfg.cmp[0] = 0xFFFFFFFFuL;
    hwcfg.cmp[1] = 0xFFFFFFFFuL;
    hwcfg.enable_cmp_output = false;
    (void)gptmr_channel_config(base, pch->hwChannel, &hwcfg, false);
    gptmr_channel_reset_count(base, pch->hwChannel);
    gptmr_clear_status(base, GPTMR_CH_CAP_STAT_MASK(pch->hwChannel));
}

/* 引脚 + TRGM 路由（板级参数来自 PBcfg） */
static void icu_hw_route(Icu_ChannelType ch)
{
    const Icu_ChannelConfigType *pch = &s_icu_cfg->channels[ch];
    trgm_output_t tcfg;

    ((IOC_Type *)(uintptr_t)pch->iocBase)->PAD[pch->padIndex].FUNC_CTL = pch->padFunc;
    tcfg.invert = false;
    tcfg.type = trgm_output_same_as_input;
    tcfg.input = (uint8_t)pch->trgmInputSrc;
    trgm_output_config((TRGM_Type *)(uintptr_t)pch->trgmBase,
                       (uint8_t)pch->trgmOutputSrc, &tcfg);
}

static void icu_irq_enable(Icu_ChannelType ch)
{
    const Icu_ChannelConfigType *pch = &s_icu_cfg->channels[ch];
    gptmr_enable_irq(ICU_BASE(pch), GPTMR_CH_CAP_IRQ_MASK(pch->hwChannel));
    intc_m_enable_irq_with_priority((uint32)pch->irq, ICU_IRQ_PRIORITY);
}

static void icu_irq_disable(Icu_ChannelType ch)
{
    const Icu_ChannelConfigType *pch = &s_icu_cfg->channels[ch];
    gptmr_disable_irq(ICU_BASE(pch), GPTMR_CH_CAP_IRQ_MASK(pch->hwChannel));
    gptmr_clear_status(ICU_BASE(pch), GPTMR_CH_CAP_STAT_MASK(pch->hwChannel));
}

static void icu_start_measure(Icu_ChannelType ch)
{
    const Icu_ChannelConfigType *pch = &s_icu_cfg->channels[ch];
    icu_hw_program(ch);
    s_icu[ch].state = 1u;
    icu_irq_enable(ch);
    gptmr_start_counter(ICU_BASE(pch), pch->hwChannel);
}

static void icu_stop_measure(Icu_ChannelType ch)
{
    const Icu_ChannelConfigType *pch = &s_icu_cfg->channels[ch];
    gptmr_stop_counter(ICU_BASE(pch), pch->hwChannel);
    s_icu[ch].state = 0u;
    s_icu[ch].timestampActive = FALSE;
    icu_irq_disable(ch);
}

/* ---------- SWS_Icu §8.3.1 Icu_Init（Service ID 0x00） ---------- */
void Icu_Init(const Icu_ConfigType *ConfigPtr)
{
    const Icu_ConfigType *cfg;
    uint32 i;

    if (s_icu_mode != ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_INIT, ICU_E_ALREADY_INITIALIZED);
        return;
    }
    cfg = (ConfigPtr != NULL_PTR) ? ConfigPtr : (&Icu_Config);
    if ((cfg->channels == NULL_PTR) || (cfg->numChannels == 0u) || (cfg->numChannels > ICU_NUM_CHANNELS))
    {
        ICU_DET(ICU_API_ID_INIT, ICU_E_INIT_FAILED);
        return;
    }
    s_icu_cfg = cfg;

    for (i = 0u; i < cfg->numChannels; i++)
    {
        const Icu_ChannelConfigType *pch = &cfg->channels[i];

        clock_add_to_group((clock_name_t)pch->clockName, 0);
        s_icu[i].clockHz = clock_get_frequency((clock_name_t)pch->clockName);
        s_icu[i].state = 0u;
        s_icu[i].activation = pch->activation;
        s_icu[i].measurementMode = pch->measurementMode;
        s_icu[i].notifyEnabled = FALSE;
        s_icu[i].timestampActive = FALSE;
        s_icu[i].edgeSeen = FALSE;
        s_icu[i].lastPeriod = 0u;
        s_icu[i].lastHigh = 0u;
        s_icu[i].lastDelta = 0u;
        s_icu[i].lastEdge = 0u;
        s_icu[i].edgeCount = 0u;
        s_icu[i].tsBuffer = NULL_PTR;
        s_icu[i].tsSize = 0u;
        s_icu[i].tsIndex = 0u;
        s_icu[i].tsCount = 0u;

        icu_hw_route((Icu_ChannelType)i);
        icu_hw_program((Icu_ChannelType)i);
        icu_irq_disable((Icu_ChannelType)i);
    }
    s_icu_mode = ICU_STATE_INIT;
}

/* ---------- SWS_Icu §8.3.2 Icu_DeInit（Service ID 0x01） ---------- */
void Icu_DeInit(void)
{
    uint32 i;

    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_DEINIT, ICU_E_UNINIT);
        return;
    }
    for (i = 0u; i < s_icu_cfg->numChannels; i++)
    {
        icu_stop_measure((Icu_ChannelType)i);
        intc_m_disable_irq((uint32)s_icu_cfg->channels[i].irq);
    }
    s_icu_mode = ICU_STATE_UNINIT;
    s_icu_cfg = NULL_PTR;
}

/* ---------- SWS_Icu §8.3.3 Icu_SetMode（Service ID 0x02） ---------- */
void Icu_SetMode(Icu_ModeType Mode)
{
    uint32 i;

    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_SET_MODE, ICU_E_UNINIT);
        return;
    }
    if ((Mode != ICU_MODE_NORMAL) && (Mode != ICU_MODE_SLEEP))
    {
        ICU_DET(ICU_API_ID_SET_MODE, ICU_E_PARAM_MODE);
        return;
    }
    if (Mode == ICU_MODE_SLEEP)
    {
        for (i = 0u; i < s_icu_cfg->numChannels; i++)
        {
            if (s_icu[i].state != 0u)
            {
                icu_stop_measure((Icu_ChannelType)i);
            }
        }
        s_icu_mode = ICU_STATE_SLEEP;
    }
    else
    {
        s_icu_mode = ICU_STATE_INIT;
    }
}

/* ---------- SWS_Icu §8.3.6 Icu_SetActivationCondition（Service ID 0x05） ---------- */
void Icu_SetActivationCondition(Icu_ChannelType Channel, Icu_ActivationType Activation)
{
    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_SET_ACTIVATION, ICU_E_UNINIT);
        return;
    }
    if (icu_ch_valid(Channel) == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_SET_ACTIVATION, ICU_E_PARAM_CHANNEL);
        return;
    }
    if (Activation > ICU_BOTH_EDGES)
    {
        ICU_DET_CH(Channel, ICU_API_ID_SET_ACTIVATION, ICU_E_PARAM_ACTIVATION);
        return;
    }
    s_icu[Channel].activation = Activation;
    if (s_icu[Channel].state != 0u)
    {
        /* 运行中改边沿：重编程捕获模式（measure_width 模式本身同时测两个边沿，
         * 该模式下 Actication 只影响时间戳/通知路径，见第 45 章说明） */
        icu_hw_program(Channel);
        gptmr_start_counter(ICU_BASE(&s_icu_cfg->channels[Channel]),
                            s_icu_cfg->channels[Channel].hwChannel);
    }
}

/* ---------- SWS_Icu §8.3.7/8 Icu_DisableNotification / EnableNotification ---------- */
void Icu_DisableNotification(Icu_ChannelType Channel)
{
    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_DISABLE_NOTIFICATION, ICU_E_UNINIT);
        return;
    }
    if (icu_ch_valid(Channel) == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_DISABLE_NOTIFICATION, ICU_E_PARAM_CHANNEL);
        return;
    }
    s_icu[Channel].notifyEnabled = FALSE;
    if (s_icu[Channel].state == 0u)
    {
        icu_irq_disable(Channel);
    }
}

void Icu_EnableNotification(Icu_ChannelType Channel)
{
    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_ENABLE_NOTIFICATION, ICU_E_UNINIT);
        return;
    }
    if (icu_ch_valid(Channel) == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_ENABLE_NOTIFICATION, ICU_E_PARAM_CHANNEL);
        return;
    }
    s_icu[Channel].notifyEnabled = TRUE;
    icu_irq_enable(Channel);
}

/* ---------- SWS_Icu §8.3.9 Icu_GetInputState（Service ID 0x08） ---------- */
Icu_InputStateType Icu_GetInputState(Icu_ChannelType Channel)
{
    Icu_InputStateType st = ICU_IDLE;

    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_GET_INPUT_STATE, ICU_E_UNINIT);
        return ICU_IDLE;
    }
    if (icu_ch_valid(Channel) == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_GET_INPUT_STATE, ICU_E_PARAM_CHANNEL);
        return ICU_IDLE;
    }
    if (s_icu[Channel].edgeSeen == TRUE)
    {
        s_icu[Channel].edgeSeen = FALSE;
        st = ICU_ACTIVE;
    }
    return st;
}

/* ---------- SWS_Icu §8.3.13 Icu_EnableEdgeDetection（Service ID 0x16） ---------- */
void Icu_EnableEdgeDetection(Icu_ChannelType Channel)
{
    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_ENABLE_EDGE_DETECTION, ICU_E_UNINIT);
        return;
    }
    if (icu_ch_valid(Channel) == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_ENABLE_EDGE_DETECTION, ICU_E_PARAM_CHANNEL);
        return;
    }
    if (s_icu[Channel].measurementMode == ICU_MODE_SIGNAL_MEASUREMENT)
    {
        s_icu[Channel].measurementMode = ICU_MODE_SIGNAL_EDGE_DETECT;
    }
    icu_start_measure(Channel);
}

/* ---------- SWS_Icu §8.3.14 Icu_DisableEdgeDetection（Service ID 0x17） ---------- */
void Icu_DisableEdgeDetection(Icu_ChannelType Channel)
{
    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_DISABLE_EDGE_DETECTION, ICU_E_UNINIT);
        return;
    }
    if (icu_ch_valid(Channel) == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_DISABLE_EDGE_DETECTION, ICU_E_PARAM_CHANNEL);
        return;
    }
    icu_stop_measure(Channel);
}

/* ---------- SWS_Icu §8.3.10 Icu_StartSignalMeasurement（Service ID 0x13） ---------- */
void Icu_StartSignalMeasurement(Icu_ChannelType Channel)
{
    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_START_SIGNAL_MEASUREMENT, ICU_E_UNINIT);
        return;
    }
    if (icu_ch_valid(Channel) == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_START_SIGNAL_MEASUREMENT, ICU_E_PARAM_CHANNEL);
        return;
    }
    s_icu[Channel].measurementMode = ICU_MODE_SIGNAL_MEASUREMENT;
    icu_start_measure(Channel);
}

/* ---------- SWS_Icu §8.3.15 Icu_StopSignalMeasurement（Service ID 0x14） ---------- */
void Icu_StopSignalMeasurement(Icu_ChannelType Channel)
{
    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_STOP_SIGNAL_MEASUREMENT, ICU_E_UNINIT);
        return;
    }
    if (icu_ch_valid(Channel) == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_STOP_SIGNAL_MEASUREMENT, ICU_E_PARAM_CHANNEL);
        return;
    }
    icu_stop_measure(Channel);
}

/* ---------- SWS_Icu §8.3.9 Icu_StartTimestamp（Service ID 0x09） ---------- */
void Icu_StartTimestamp(Icu_ChannelType Channel, Icu_ValueType *BufferPtr, uint16 BufferSize, uint16 NotifyInterval)
{
    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_START_TIMESTAMP, ICU_E_UNINIT);
        return;
    }
    if (icu_ch_valid(Channel) == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_START_TIMESTAMP, ICU_E_PARAM_CHANNEL);
        return;
    }
    if (BufferPtr == NULL_PTR)
    {
        ICU_DET_CH(Channel, ICU_API_ID_START_TIMESTAMP, ICU_E_PARAM_POINTER);
        return;
    }
    if ((BufferSize == 0u) || (BufferSize > ICU_MAX_TIMESTAMP_BUFFER))
    {
        ICU_DET_CH(Channel, ICU_API_ID_START_TIMESTAMP, ICU_E_PARAM_BUFFER_SIZE);
        return;
    }
    if ((NotifyInterval == 0u) || (NotifyInterval > BufferSize))
    {
        ICU_DET_CH(Channel, ICU_API_ID_START_TIMESTAMP, ICU_E_PARAM_NOTIFY_INTERVAL);
        return;
    }
    if (s_icu[Channel].state != 0u)
    {
        icu_stop_measure(Channel);
    }
    s_icu[Channel].measurementMode = ICU_MODE_TIMESTAMP;
    s_icu[Channel].tsBuffer = BufferPtr;
    s_icu[Channel].tsSize = BufferSize;
    s_icu[Channel].tsNotifyInterval = NotifyInterval;
    s_icu[Channel].tsIndex = 0u;
    s_icu[Channel].tsCount = 0u;
    s_icu[Channel].timestampActive = TRUE;
    icu_start_measure(Channel);
}

/* ---------- SWS_Icu §8.3.9 Icu_StopTimestamp（Service ID 0x0A） ---------- */
void Icu_StopTimestamp(Icu_ChannelType Channel)
{
    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_STOP_TIMESTAMP, ICU_E_UNINIT);
        return;
    }
    if (icu_ch_valid(Channel) == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_STOP_TIMESTAMP, ICU_E_PARAM_CHANNEL);
        return;
    }
    if (s_icu[Channel].timestampActive == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_STOP_TIMESTAMP, ICU_E_NOT_STARTED);
        return;
    }
    icu_stop_measure(Channel);
}

/* ---------- Icu_GetTimestampIndex（SWS 可选，本实现提供） ---------- */
Icu_IndexType Icu_GetTimestampIndex(Icu_ChannelType Channel)
{
    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_GET_TIMESTAMP_INDEX, ICU_E_UNINIT);
        return 0u;
    }
    if (icu_ch_valid(Channel) == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_GET_TIMESTAMP_INDEX, ICU_E_PARAM_CHANNEL);
        return 0u;
    }
    return s_icu[Channel].tsIndex;
}

/* ---------- SWS_Icu §8.3.11 Icu_GetTimeElapsed（Service ID 0x10） ---------- */
Icu_ValueType Icu_GetTimeElapsed(Icu_ChannelType Channel)
{
    Icu_ValueType ret = 0u;
    uint32 p; uint32 h;

    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_GET_TIME_ELAPSED, ICU_E_UNINIT);
        return 0u;
    }
    if (icu_ch_valid(Channel) == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_GET_TIME_ELAPSED, ICU_E_PARAM_CHANNEL);
        return 0u;
    }
    if (s_icu[Channel].measurementMode == ICU_MODE_TIMESTAMP)
    {
        return s_icu[Channel].lastDelta;
    }
    p = s_icu[Channel].lastPeriod;
    h = s_icu[Channel].lastHigh;
    switch (s_icu_cfg->channels[Channel].signalProperty)
    {
    case ICU_HIGH_TIME:
        ret = (Icu_ValueType)h;
        break;
    case ICU_LOW_TIME:
        ret = (p > h) ? (Icu_ValueType)(p - h) : 0u;
        break;
    default: /* ICU_PERIOD_TIME / ICU_DUTY_CYCLE */
        ret = (Icu_ValueType)p;
        break;
    }
    return ret;
}

/* ---------- SWS_Icu §8.3.12 Icu_GetDutyCycleValues（Service ID 0x11） ---------- */
void Icu_GetDutyCycleValues(Icu_ChannelType Channel, Icu_DutyCycleType *DutyCycleValues)
{
    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_GET_DUTY_CYCLE_VALUES, ICU_E_UNINIT);
        return;
    }
    if (icu_ch_valid(Channel) == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_GET_DUTY_CYCLE_VALUES, ICU_E_PARAM_CHANNEL);
        return;
    }
    if (DutyCycleValues == NULL_PTR)
    {
        ICU_DET_CH(Channel, ICU_API_ID_GET_DUTY_CYCLE_VALUES, ICU_E_PARAM_POINTER);
        return;
    }
    DutyCycleValues->ActiveTime = s_icu[Channel].lastHigh;
    DutyCycleValues->PeriodTime = s_icu[Channel].lastPeriod;
}

/* ---------- SWS_Icu §8.3.16 Icu_GetVersionInfo（Service ID 0x12） ---------- */
void Icu_GetVersionInfo(Std_VersionInfoType *versioninfo)
{
#if (ICU_VERSION_INFO_API == STD_ON)
    if (versioninfo == NULL_PTR)
    {
        ICU_DET(ICU_API_ID_GET_VERSION_INFO, ICU_E_PARAM_VINFO);
        return;
    }
    versioninfo->vendorID = ICU_VENDOR_ID;
    versioninfo->moduleID = (uint16)ICU_MODULE_ID;
    versioninfo->sw_major_version = (uint8)ICU_SW_MAJOR_VERSION;
    versioninfo->sw_minor_version = (uint8)ICU_SW_MINOR_VERSION;
    versioninfo->sw_patch_version = (uint8)ICU_SW_PATCH_VERSION;
#else
    (void)versioninfo;
    ICU_DET(ICU_API_ID_GET_VERSION_INFO, ICU_E_PARAM_VINFO);
#endif
}

/* ---------- 非 AUTOSAR 扩展：原始边沿时间戳 ---------- */
Std_ReturnType Icu_GetLastEdgeTimestamp(Icu_ChannelType Channel, Icu_ValueType *TimestampPtr)
{
    if (s_icu_mode == ICU_STATE_UNINIT)
    {
        ICU_DET(ICU_API_ID_GET_TIME_ELAPSED, ICU_E_UNINIT);
        return E_NOT_OK;
    }
    if (icu_ch_valid(Channel) == FALSE)
    {
        ICU_DET_CH(Channel, ICU_API_ID_GET_TIME_ELAPSED, ICU_E_PARAM_CHANNEL);
        return E_NOT_OK;
    }
    if (TimestampPtr == NULL_PTR)
    {
        ICU_DET_CH(Channel, ICU_API_ID_GET_TIME_ELAPSED, ICU_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    *TimestampPtr = s_icu[Channel].lastEdge;
    return E_OK;
}

/* ---------- ISR 分发（由 Icu_PBcfg.c 的向量桩调用） ----------
 * ISR 只做：清状态 → 读硬件捕获结果 → 推进时间戳缓冲 → 调通知。
 */
void Icu_IsrHandler(void)
{
    uint32 i;

    if ((s_icu_mode == ICU_STATE_UNINIT) || (s_icu_cfg == NULL_PTR))
    {
        return;
    }
    for (i = 0u; i < s_icu_cfg->numChannels; i++)
    {
        const Icu_ChannelConfigType *pch = &s_icu_cfg->channels[i];
        GPTMR_Type *base = ICU_BASE(pch);
        uint32 mask = (uint32)GPTMR_CH_CAP_STAT_MASK(pch->hwChannel);

        if (gptmr_check_status(base, mask) == false)
        {
            continue;
        }
        gptmr_clear_status(base, mask);
        s_icu[i].edgeSeen = TRUE;
        s_icu[i].edgeCount++;

        if (s_icu[i].measurementMode == ICU_MODE_SIGNAL_MEASUREMENT)
        {
            uint32 period = gptmr_channel_get_counter(base, pch->hwChannel,
                                                     gptmr_counter_type_measured_period);
            uint32 high = gptmr_channel_get_counter(base, pch->hwChannel,
                                                    gptmr_counter_type_measured_duty_cycle);
            if (period != 0u)
            {
                s_icu[i].lastPeriod = (Icu_ValueType)period;
                s_icu[i].lastHigh = (Icu_ValueType)high;
            }
        }
        else
        {
            uint32 edge = gptmr_channel_get_counter(base, pch->hwChannel,
                                                    (s_icu[i].activation == ICU_FALLING_EDGE) ?
                                                    gptmr_counter_type_falling_edge :
                                                    gptmr_counter_type_rising_edge);
            Icu_ValueType prev = s_icu[i].lastEdge;
            s_icu[i].lastDelta = (Icu_ValueType)(edge - prev);
            s_icu[i].lastEdge = (Icu_ValueType)edge;
            s_icu[i].lastPeriod = (Icu_ValueType)(edge - prev);
            if (s_icu[i].timestampActive == TRUE)
            {
                if (s_icu[i].tsCount < s_icu[i].tsSize)
                {
                    s_icu[i].tsBuffer[(uint32)s_icu[i].tsIndex] = (Icu_ValueType)edge;
                    s_icu[i].tsCount++;
                    s_icu[i].tsIndex++;
                    if (s_icu[i].tsIndex >= s_icu[i].tsSize)
                    {
                        s_icu[i].tsIndex = 0u;
                        if (pch->timestampBufferType == ICU_LINEAR_BUFFER)
                        {
                            s_icu[i].timestampActive = FALSE;
                        }
                    }
                    if ((s_icu[i].tsNotifyInterval != 0u) &&
                        ((s_icu[i].tsCount % s_icu[i].tsNotifyInterval) == 0u) &&
                        (pch->timestampNotification != NULL_PTR))
                    {
                        pch->timestampNotification();
                    }
                }
            }
        }
        if ((s_icu[i].notifyEnabled == TRUE) && (pch->signalNotification != NULL_PTR))
        {
            pch->signalNotification();
        }
    }
}