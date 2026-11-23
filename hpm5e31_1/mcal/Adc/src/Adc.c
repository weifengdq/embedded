/*
 * Adc.c —— AUTOSAR CP ADC Driver（HPM5E31 / RV32，ADC16 寄存器级）
 *
 * 参考：AUTOSAR_CP_SWS_ADCDriver R21-11 §7.5 错误 / §8.3 函数 / §8.2 类型
 *       HPM5E31 RM：ADC16（CONV_CFG1 / ADC_CFG0 / BUF_CFG0 / ANA_CTRL0 / SAMPLE_CFG /
 *                   BUS_RESULT / ADC16_PARAMS / PRD_CFG），SYSCTL（ADCCLK / RESOURCE），IOC（模拟 PAD）
 * 例程：samples/mcal/adc_single（单次采样统计）、samples/mcal/adc_bench（连续组吞吐）
 *
 * 实现说明（与 SWS 的差异，章节 46 与 README 均会写明）：
 *   - 本驱动用 ADC16 "bus mode one-shot"：读 BUS_RESULT 触发一次转换并阻塞到 VALID；
 *     因此 Adc_StartGroupConversion 在内部完成第一轮转换（约 0.7 us），对外表现为"启动即完成"；
 *   - 连续组（ADC_CONV_MODE_CONTINUOUS）由 Adc_ReadGroup 轮询泵动：每次 ReadGroup 完成一轮转换，
 *     未调用 ReadGroup 时不会自行转换（SWS 的连续组通常配中断/硬件周期触发，本实现为轮询型，
 *     吞吐数据在 adc_bench 里按"每次 ReadGroup 一轮"实测，并在报告里标注口径）。
 */
#include "Adc.h"
#include "Adc_PBcfg.h"
#include "Det.h"
#include "hpm_soc.h"

#if (ADC_DEV_ERROR_DETECT == STD_ON)
#define ADC_DET(api, err)  MCAL_DET_REPORT(ADC_MODULE_ID, 0u, (api), (err))
#else
#define ADC_DET(api, err)  ((void)0)
#endif

#define ADC_SYSCTL_RESOURCE_ADC0 (295u)   /* SYSCTL_RESOURCE_ADC0，见 hpm_sysctl_regs.h */
#define ADC_RETRY_MAX            (16u)

typedef struct
{
    const Adc_GroupConfigType *cfg;
    Adc_ValueGroupType *buffer;       /* Adc_SetupResultBuffer 传入的应用缓冲 */
    Adc_ValueGroupType shadow[ADC_MAX_CHANNELS_PER_GROUP]; /* 最近一轮原始值 */
    Adc_StatusType status;
    Adc_StreamNumSampleType samples;  /* 已写入缓冲的采样轮数（每通道） */
    uint16 writeIdx;                  /* 流式写指针（轮） */
    boolean bufferSet;
    boolean notifEnabled;
    uint32 totalConversions;          /* 累计完成轮数（扩展统计用） */
} adc_group_state_t;

static const Adc_ConfigType *s_cfg = NULL_PTR;
static boolean s_initialized = FALSE;
static adc_group_state_t s_groups[ADC_GROUP_COUNT_CFG];

/* ---------------------------------------------------------------- 内部工具 */

static ADC16_Type *adc_hw(void)
{
    return (ADC16_Type *)s_cfg->adcBase;
}

static SYSCTL_Type *adc_sysctl(void)
{
    return (SYSCTL_Type *)s_cfg->sysctlBase;
}

static boolean adc_group_valid(Adc_GroupType g)
{
    return ((uint32)g < (uint32)s_cfg->groupCount) ? TRUE : FALSE;
}

/* ADC16 校准：与 SDK hpm_adc16_drv.c:adc16_do_calibration 的寄存器序列一致 */
static void adc_do_calibration(ADC16_Type *adc)
{
    uint32_t i, j;
    uint32_t clkDivTemp;
    uint32_t params[ADC16_SOC_PARAMS_LEN];
    int32_t param01;
    uint32_t param02, param32;
    uint64_t param64;
    uint32_t temp;

    clkDivTemp = ADC16_CONV_CFG1_CLOCK_DIVIDER_GET(adc->CONV_CFG1);
    adc->CONV_CFG1 = (adc->CONV_CFG1 & ~ADC16_CONV_CFG1_CLOCK_DIVIDER_MASK) |
                     ADC16_CONV_CFG1_CLOCK_DIVIDER_SET(1);
    adc->ANA_CTRL0 |= ADC16_ANA_CTRL0_ADC_CLK_ON_MASK;

    for (i = 0; i < ADC16_SOC_PARAMS_LEN; i++)
    {
        params[i] = 0u;
    }
    adc->ADC16_CONFIG0 |= ADC16_ADC16_CONFIG0_REG_EN_MASK | ADC16_ADC16_CONFIG0_BANDGAP_EN_MASK;
    adc->ADC16_CONFIG0 = (adc->ADC16_CONFIG0 & ~ADC16_ADC16_CONFIG0_CAL_AVG_CFG_MASK) |
                         ADC16_ADC16_CONFIG0_CAL_AVG_CFG_SET(5u);   /* 32 次平均 */
    adc->ADC_CFG0 |= ADC16_ADC_CFG0_ADC_AHB_EN_MASK | (1u << 2);
    adc->ANA_CTRL0 &= ~ADC16_ANA_CTRL0_ADC_CLK_ON_MASK;
    adc->CONV_CFG1 = (adc->CONV_CFG1 & ~ADC16_CONV_CFG1_CLOCK_DIVIDER_MASK) |
                     ADC16_CONV_CFG1_CLOCK_DIVIDER_SET(clkDivTemp);

    for (j = 0; j < 4u; j++)
    {
        adc->ANA_CTRL0 |= ADC16_ANA_CTRL0_STARTCAL_MASK;
        adc->ANA_CTRL0 &= ~ADC16_ANA_CTRL0_STARTCAL_MASK;
        while (ADC16_ANA_STATUS_CALON_GET(adc->ANA_STATUS) != 0u)
        {
            /* 等校准完成 */
        }
        for (i = 0; i < ADC16_SOC_PARAMS_LEN; i++)
        {
            params[i] += ADC16_ADC16_PARAMS_PARAM_VAL_GET(adc->ADC16_PARAMS[i]);
        }
    }

    params[ADC16_ADC16_PARAMS_ADC16_PARA33] -= 0x800u;
    param01 = (int32_t)params[ADC16_ADC16_PARAMS_ADC16_PARA32] -
              (int32_t)params[ADC16_ADC16_PARAMS_ADC16_PARA33];
    params[ADC16_ADC16_PARAMS_ADC16_PARA32] = params[ADC16_ADC16_PARAMS_ADC16_PARA00] -
                                              params[ADC16_ADC16_PARAMS_ADC16_PARA33];
    params[ADC16_ADC16_PARAMS_ADC16_PARA00] = 0u;
    for (i = 1u; i < (ADC16_SOC_PARAMS_LEN - 2u); i++)
    {
        params[i] = params[ADC16_ADC16_PARAMS_ADC16_PARA32] + params[i] -
                    params[ADC16_ADC16_PARAMS_ADC16_PARA33] + params[i - 1u];
    }
    param02 = (uint32_t)(param01 + (int32_t)params[ADC16_ADC16_PARAMS_ADC16_PARA31] +
                         (int32_t)params[ADC16_ADC16_PARAMS_ADC16_PARA32]) >> 6;
    param64 = (0x10000ull * (uint64_t)param02) / (uint64_t)(0x20000u - param02 / 2u);
    param32 = (uint32_t)param64;
    for (i = 0; i < ADC16_SOC_PARAMS_LEN; i++)
    {
        params[i] >>= 6;
    }

    adc->ANA_CTRL0 |= ADC16_ANA_CTRL0_ADC_CLK_ON_MASK;
    adc->CONV_CFG1 = (adc->CONV_CFG1 & ~ADC16_CONV_CFG1_CLOCK_DIVIDER_MASK) |
                     ADC16_CONV_CFG1_CLOCK_DIVIDER_SET(1);
    for (i = 0; i < ADC16_SOC_PARAMS_LEN; i++)
    {
        adc->ADC16_PARAMS[i] = (uint16_t)params[i];
    }

    temp = adc->ADC16_CONFIG0;
    temp &= ~(ADC16_ADC16_CONFIG0_CAL_AVG_CFG_MASK | ADC16_ADC16_CONFIG0_CONV_PARAM_MASK);
    temp |= ADC16_ADC16_CONFIG0_REG_EN_MASK | ADC16_ADC16_CONFIG0_BANDGAP_EN_MASK |
            ADC16_ADC16_CONFIG0_CAL_AVG_CFG_MASK | ADC16_ADC16_CONFIG0_CONV_PARAM_SET(param32);
    adc->ADC16_CONFIG0 = temp;

    adc->CONV_CFG1 = (adc->CONV_CFG1 & ~ADC16_CONV_CFG1_CLOCK_DIVIDER_MASK) |
                     ADC16_CONV_CFG1_CLOCK_DIVIDER_SET(clkDivTemp);
    adc->ANA_CTRL0 &= ~ADC16_ANA_CTRL0_ADC_CLK_ON_MASK;
}

/* 单通道一次转换：bus mode 下读 BUS_RESULT 触发转换并阻塞到 VALID */
static boolean adc_convert_once(const Adc_GroupConfigType *g, Adc_ValueGroupType *raw)
{
    ADC16_Type *adc = adc_hw();
    uint32_t retry;
    uint32_t value = 0u;

    for (retry = 0u; retry < ADC_RETRY_MAX; retry++)
    {
        value = adc->BUS_RESULT[g->channel];
        if ((value & ADC16_BUS_RESULT_VALID_MASK) != 0u)
        {
            *raw = (Adc_ValueGroupType)ADC16_BUS_RESULT_CHAN_RESULT_GET(value);
            return TRUE;
        }
    }
    *raw = (Adc_ValueGroupType)ADC16_BUS_RESULT_CHAN_RESULT_GET(value);
    return FALSE;
}

/* 完成一轮转换（组内所有通道），结果写 shadow；返回是否全部成功 */
static boolean adc_round(const adc_group_state_t *st)
{
    uint8_t i;
    boolean ok = TRUE;

    for (i = 0u; i < st->cfg->channelCount; i++)
    {
        Adc_ValueGroupType raw = 0u;
        if (!adc_convert_once(st->cfg, &raw))
        {
            ok = FALSE;
        }
        s_groups[st->cfg->group].shadow[i] = raw;
    }
    return ok;
}

/* 一轮结果写入应用缓冲（单次访问：覆盖；流式：按轮写入） */
static void adc_store_round(adc_group_state_t *st)
{
    uint8_t i;
    uint16 base;

    if (!st->bufferSet)
    {
        return;
    }
    if (st->cfg->accessMode == (uint8)ADC_ACCESS_MODE_STREAMING)
    {
        base = (uint16)(st->writeIdx * st->cfg->channelCount);
        for (i = 0u; i < st->cfg->channelCount; i++)
        {
            st->buffer[base + i] = st->shadow[i];
        }
        st->writeIdx++;
        if (st->writeIdx >= st->cfg->streamSamples)
        {
            if (st->cfg->streamBufferMode == (uint8)ADC_STREAM_BUFFER_CIRCULAR)
            {
                st->writeIdx = 0u;
                st->samples = st->cfg->streamSamples;
            }
            else
            {
                st->samples = st->cfg->streamSamples;
                st->status = ADC_STREAM_COMPLETED;
            }
        }
        else
        {
            st->samples = (Adc_StreamNumSampleType)st->writeIdx;
        }
    }
    else
    {
        for (i = 0u; i < st->cfg->channelCount; i++)
        {
            st->buffer[i] = st->shadow[i];
        }
        st->samples = 1u;
    }
    st->totalConversions++;
}

static void adc_notify(adc_group_state_t *st)
{
    if ((st->notifEnabled) && (st->cfg->notification != NULL_PTR))
    {
        st->cfg->notification();
    }
}

/* ---------------------------------------------------------------- API 实现 */

void Adc_Init(const Adc_ConfigType *ConfigPtr)
{
    ADC16_Type *adc;
    SYSCTL_Type *sysctl;
    uint8_t i;
    uint32_t value;

    if (ConfigPtr == NULL_PTR)
    {
        ADC_DET(ADC_API_ID_INIT, ADC_E_PARAM_POINTER);
        return;
    }
    if (s_initialized)
    {
        /* SWS_Adc_00107：重复 Init 报 ADC_E_ALREADY_INITIALIZED 且不动作 */
        ADC_DET(ADC_API_ID_INIT, ADC_E_ALREADY_INITIALIZED);
        return;
    }

    s_cfg = ConfigPtr;
    adc = adc_hw();
    sysctl = adc_sysctl();

    /* 1) ADC 资源时钟常开（等价 BSP 的 clock_add_to_group，但只动 ADC0 这一个资源） */
    value = sysctl->RESOURCE[ADC_SYSCTL_RESOURCE_ADC0];
    value &= ~SYSCTL_RESOURCE_MODE_MASK;
    value |= SYSCTL_RESOURCE_MODE_SET(1u);   /* 1 = always on */
    sysctl->RESOURCE[ADC_SYSCTL_RESOURCE_ADC0] = value;

    /* 2) ADC 转换时钟源：ADCCLK.MUX = 0 -> ahb0（RM SYSCTL ADCCLK） */
    sysctl->ADCCLK[s_cfg->adcClockNode] = (sysctl->ADCCLK[s_cfg->adcClockNode] &
                                           ~SYSCTL_ADCCLK_MUX_MASK) |
                                          SYSCTL_ADCCLK_MUX_SET(s_cfg->adcClockSrc);

    /* 3) 模拟引脚：IOC PADx.FUNC_CTL.ANALOG = 1（由 Port 模块负责 I/O 属性，这里只切模拟通路） */
    for (i = 0u; i < s_cfg->groupCount; i++)
    {
        const Adc_GroupConfigType *g = &s_cfg->groups[i];
        ((IOC_Type *)s_cfg->iocBase)->PAD[g->padIndex].FUNC_CTL = IOC_PAD_FUNC_CTL_ANALOG_MASK;

        s_groups[i].cfg = g;
        s_groups[i].buffer = NULL_PTR;
        s_groups[i].status = ADC_IDLE;
        s_groups[i].samples = 0u;
        s_groups[i].writeIdx = 0u;
        s_groups[i].bufferSet = FALSE;
        s_groups[i].notifEnabled = FALSE;
        s_groups[i].totalConversions = 0u;
    }

    /* 4) ADC16 基础配置：分辨率（转换周期数）+ 时钟分频 */
    adc->INT_EN = 0u;
    adc->CONV_CFG1 = ADC16_CONV_CFG1_CONVERT_CLOCK_NUMBER_SET(s_cfg->groups[0].resolution) |
                     ADC16_CONV_CFG1_CLOCK_DIVIDER_SET((uint32_t)s_cfg->clockDivider - 1u);
    /* SEL_SYNC_AHB=1（时钟源是 ahb0），oneshot bus mode 不需要 AHB 请求 */
    adc->ADC_CFG0 = ADC16_ADC_CFG0_SEL_SYNC_AHB_SET(1u) |
                    ADC16_ADC_CFG0_ADC_AHB_EN_SET(0u) |
                    ADC16_ADC_CFG0_PORT3_REALTIME_SET(0u);
    /* WAIT_DIS=0：读 BUS_RESULT 阻塞到 VALID；BUS_MODE_EN=1：oneshot 总线模式（HPM5E31 必需） */
    adc->BUF_CFG0 = ADC16_BUF_CFG0_WAIT_DIS_SET(0u) | ADC16_BUF_CFG0_BUS_MODE_EN_MASK;

    /* 5) 转换窗口结束计数：COV_END_CNT = ADC16_SOC_MAX_CONV_CLK_NUM - 分辨率 + 1
     *    与 SDK adc16_init 一致；缺这一步转换结果会漂（第一次上板 mean 抖动 ±13000 LSB 即此原因） */
    adc->ANA_CTRL0 |= ADC16_ANA_CTRL0_ADC_CLK_ON_MASK;
    adc->ADC16_CONFIG1 &= ~ADC16_ADC16_CONFIG1_COV_END_CNT_MASK;
    adc->ADC16_CONFIG1 |= ADC16_ADC16_CONFIG1_COV_END_CNT_SET(
        (uint32_t)ADC16_SOC_MAX_CONV_CLK_NUM - (uint32_t)s_cfg->groups[0].resolution + 1u);
    adc->ANA_CTRL0 &= ~ADC16_ANA_CTRL0_ADC_CLK_ON_MASK;

    /* 6) 校准（16 bit 精度依赖这一步） */
    adc_do_calibration(adc);

    /* 7) 通道采样保持周期 + 周期模式阈值默认值 */
    for (i = 0u; i < s_cfg->groupCount; i++)
    {
        const Adc_GroupConfigType *g = &s_cfg->groups[i];
        adc->SAMPLE_CFG[g->channel] = ADC16_SAMPLE_CFG_SAMPLE_CLOCK_NUMBER_SHIFT_SET(0u) |
                                      ADC16_SAMPLE_CFG_SAMPLE_CLOCK_NUMBER_SET(g->samplingTime);
        adc->PRD_CFG[g->channel].PRD_THSHD_CFG = ADC16_PRD_CFG_PRD_THSHD_CFG_THSHDH_SET(0xFFFFu) |
                                                 ADC16_PRD_CFG_PRD_THSHD_CFG_THSHDL_SET(0u);
    }

    s_initialized = TRUE;
}

void Adc_DeInit(void)
{
    ADC16_Type *adc;
    SYSCTL_Type *sysctl;
    uint32_t value;
    uint8_t i;

    if (!s_initialized)
    {
        ADC_DET(ADC_API_ID_DEINIT, ADC_E_UNINIT);
        return;
    }
    adc = adc_hw();
    adc->INT_EN = 0u;
    adc->BUF_CFG0 = 0u;
    for (i = 0u; i < s_cfg->groupCount; i++)
    {
        s_groups[i].status = ADC_IDLE;
        s_groups[i].bufferSet = FALSE;
        s_groups[i].notifEnabled = FALSE;
        s_groups[i].samples = 0u;
        s_groups[i].writeIdx = 0u;
    }
    /* 关 ADC0 资源时钟（MODE=2 always off） */
    sysctl = adc_sysctl();
    value = sysctl->RESOURCE[ADC_SYSCTL_RESOURCE_ADC0];
    value &= ~SYSCTL_RESOURCE_MODE_MASK;
    value |= SYSCTL_RESOURCE_MODE_SET(2u);
    sysctl->RESOURCE[ADC_SYSCTL_RESOURCE_ADC0] = value;
    s_initialized = FALSE;
}

Std_ReturnType Adc_SetupResultBuffer(Adc_GroupType Group, Adc_ValueGroupType *DataBufferPtr)
{
    if (!s_initialized)
    {
        ADC_DET(ADC_API_ID_SETUP_RESULT_BUFFER, ADC_E_UNINIT);
        return E_NOT_OK;
    }
    if (!adc_group_valid(Group))
    {
        ADC_DET(ADC_API_ID_SETUP_RESULT_BUFFER, ADC_E_PARAM_GROUP);
        return E_NOT_OK;
    }
    if (DataBufferPtr == NULL_PTR)
    {
        ADC_DET(ADC_API_ID_SETUP_RESULT_BUFFER, ADC_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    if (s_groups[Group].status != ADC_IDLE)
    {
        /* SWS_Adc_00433：组不在 IDLE 时报 ADC_E_BUSY（运行时错误） */
        ADC_DET(ADC_API_ID_SETUP_RESULT_BUFFER, ADC_E_BUSY);
        return E_NOT_OK;
    }
    s_groups[Group].buffer = DataBufferPtr;
    s_groups[Group].bufferSet = TRUE;
    s_groups[Group].writeIdx = 0u;
    s_groups[Group].samples = 0u;
    return E_OK;
}

void Adc_StartGroupConversion(Adc_GroupType Group)
{
    adc_group_state_t *st;

    if (!s_initialized)
    {
        ADC_DET(ADC_API_ID_START_GROUP_CONVERSION, ADC_E_UNINIT);
        return;
    }
    if (!adc_group_valid(Group))
    {
        ADC_DET(ADC_API_ID_START_GROUP_CONVERSION, ADC_E_PARAM_GROUP);
        return;
    }
    st = &s_groups[Group];
    if (st->cfg->triggerSource != (uint8)ADC_TRIGG_SRC_SW)
    {
        ADC_DET(ADC_API_ID_START_GROUP_CONVERSION, ADC_E_WRONG_TRIGG_SRC);
        return;
    }
    if (!st->bufferSet)
    {
        ADC_DET(ADC_API_ID_START_GROUP_CONVERSION, ADC_E_BUFFER_UNINIT);
        return;
    }
    if (st->status == ADC_BUSY)
    {
        ADC_DET(ADC_API_ID_START_GROUP_CONVERSION, ADC_E_BUSY);
        return;
    }

    /* 复位流式指针（SWS_Adc_00431） */
    st->writeIdx = 0u;
    st->samples = 0u;
    st->status = ADC_BUSY;

    /* 第一轮转换（bus mode 下约 0.7 us，阻塞；连续组的后续轮由 ReadGroup 泵动） */
    (void)adc_round(st);
    adc_store_round(st);
    if (st->status != ADC_STREAM_COMPLETED)
    {
        st->status = ADC_COMPLETED;
    }
    adc_notify(st);
}

void Adc_StopGroupConversion(Adc_GroupType Group)
{
    adc_group_state_t *st;

    if (!s_initialized)
    {
        ADC_DET(ADC_API_ID_STOP_GROUP_CONVERSION, ADC_E_UNINIT);
        return;
    }
    if (!adc_group_valid(Group))
    {
        ADC_DET(ADC_API_ID_STOP_GROUP_CONVERSION, ADC_E_PARAM_GROUP);
        return;
    }
    st = &s_groups[Group];
    if (st->cfg->triggerSource != (uint8)ADC_TRIGG_SRC_SW)
    {
        ADC_DET(ADC_API_ID_STOP_GROUP_CONVERSION, ADC_E_WRONG_TRIGG_SRC);
        return;
    }
    if (st->status == ADC_IDLE)
    {
        /* SWS_Adc_00241：IDLE 下调 Stop 报运行时错误 ADC_E_IDLE */
        ADC_DET(ADC_API_ID_STOP_GROUP_CONVERSION, ADC_E_IDLE);
        return;
    }
    /* SWS_Adc_00155：Stop 自动关组通知 */
    st->notifEnabled = FALSE;
    st->status = ADC_IDLE;
}

Std_ReturnType Adc_ReadGroup(Adc_GroupType Group, Adc_ValueGroupType *DataBufferPtr)
{
    adc_group_state_t *st;
    uint8_t i;

    if (!s_initialized)
    {
        ADC_DET(ADC_API_ID_READ_GROUP, ADC_E_UNINIT);
        return E_NOT_OK;
    }
    if (!adc_group_valid(Group))
    {
        ADC_DET(ADC_API_ID_READ_GROUP, ADC_E_PARAM_GROUP);
        return E_NOT_OK;
    }
    if (DataBufferPtr == NULL_PTR)
    {
        ADC_DET(ADC_API_ID_READ_GROUP, ADC_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    st = &s_groups[Group];
    if ((!st->bufferSet) || (st->samples == 0u))
    {
        /* 没有可用结果：IDLE 且未启动过 */
        ADC_DET(ADC_API_ID_READ_GROUP, ADC_E_IDLE);
        return E_NOT_OK;
    }

    for (i = 0u; i < st->cfg->channelCount; i++)
    {
        DataBufferPtr[i] = st->shadow[i];
    }

    if (st->cfg->convMode == (uint8)ADC_CONV_MODE_CONTINUOUS)
    {
        /* 连续组：每次 ReadGroup 完成下一轮（轮询泵动），状态回到 COMPLETED */
        (void)adc_round(st);
        adc_store_round(st);
        if (st->status != ADC_STREAM_COMPLETED)
        {
            st->status = ADC_COMPLETED;
        }
        adc_notify(st);
    }
    else
    {
        /* 单次组：结果被读走后回到 IDLE（SWS_Adc_00330 的单次访问语义） */
        st->status = ADC_IDLE;
    }
    return E_OK;
}

void Adc_EnableHardwareTrigger(Adc_GroupType Group)
{
    if (!s_initialized)
    {
        ADC_DET(ADC_API_ID_ENABLE_HW_TRIGGER, ADC_E_UNINIT);
        return;
    }
    if (!adc_group_valid(Group))
    {
        ADC_DET(ADC_API_ID_ENABLE_HW_TRIGGER, ADC_E_PARAM_GROUP);
        return;
    }
    /* 本板两组都是软件触发：按 SWS_Adc_00136 报 ADC_E_WRONG_TRIGG_SRC */
    if (s_groups[Group].cfg->triggerSource != (uint8)ADC_TRIGG_SRC_HW)
    {
        ADC_DET(ADC_API_ID_ENABLE_HW_TRIGGER, ADC_E_WRONG_TRIGG_SRC);
        return;
    }
    /* 无硬件触发组配置，不会走到这里；若将来配置：设置硬件触发使能位（实现路径） */
}

void Adc_DisableHardwareTrigger(Adc_GroupType Group)
{
    if (!s_initialized)
    {
        ADC_DET(ADC_API_ID_DISABLE_HW_TRIGGER, ADC_E_UNINIT);
        return;
    }
    if (!adc_group_valid(Group))
    {
        ADC_DET(ADC_API_ID_DISABLE_HW_TRIGGER, ADC_E_PARAM_GROUP);
        return;
    }
    if (s_groups[Group].cfg->triggerSource != (uint8)ADC_TRIGG_SRC_HW)
    {
        ADC_DET(ADC_API_ID_DISABLE_HW_TRIGGER, ADC_E_WRONG_TRIGG_SRC);
        return;
    }
}

void Adc_EnableGroupNotification(Adc_GroupType Group)
{
    if (!s_initialized)
    {
        ADC_DET(ADC_API_ID_ENABLE_GROUP_NOTIFICATION, ADC_E_UNINIT);
        return;
    }
    if (!adc_group_valid(Group))
    {
        ADC_DET(ADC_API_ID_ENABLE_GROUP_NOTIFICATION, ADC_E_PARAM_GROUP);
        return;
    }
    if (s_groups[Group].cfg->notification == NULL_PTR)
    {
        ADC_DET(ADC_API_ID_ENABLE_GROUP_NOTIFICATION, ADC_E_NOTIF_CAPABILITY);
        return;
    }
    s_groups[Group].notifEnabled = TRUE;
}

void Adc_DisableGroupNotification(Adc_GroupType Group)
{
    if (!s_initialized)
    {
        ADC_DET(ADC_API_ID_DISABLE_GROUP_NOTIFICATION, ADC_E_UNINIT);
        return;
    }
    if (!adc_group_valid(Group))
    {
        ADC_DET(ADC_API_ID_DISABLE_GROUP_NOTIFICATION, ADC_E_PARAM_GROUP);
        return;
    }
    if (s_groups[Group].cfg->notification == NULL_PTR)
    {
        ADC_DET(ADC_API_ID_DISABLE_GROUP_NOTIFICATION, ADC_E_NOTIF_CAPABILITY);
        return;
    }
    s_groups[Group].notifEnabled = FALSE;
}

Adc_StatusType Adc_GetGroupStatus(Adc_GroupType Group)
{
    if (!s_initialized)
    {
        ADC_DET(ADC_API_ID_GET_GROUP_STATUS, ADC_E_UNINIT);
        return ADC_IDLE;
    }
    if (!adc_group_valid(Group))
    {
        ADC_DET(ADC_API_ID_GET_GROUP_STATUS, ADC_E_PARAM_GROUP);
        return ADC_IDLE;
    }
    return s_groups[Group].status;
}

Adc_StreamNumSampleType Adc_GetStreamLastPointer(Adc_GroupType Group, Adc_ValueGroupType **PtrToSamplePtr)
{
    adc_group_state_t *st;
    Adc_StreamNumSampleType valid;

    if (PtrToSamplePtr != NULL_PTR)
    {
        *PtrToSamplePtr = NULL_PTR;
    }
    if (!s_initialized)
    {
        ADC_DET(ADC_API_ID_GET_STREAM_LAST_POINTER, ADC_E_UNINIT);
        return 0u;
    }
    if (!adc_group_valid(Group))
    {
        ADC_DET(ADC_API_ID_GET_STREAM_LAST_POINTER, ADC_E_PARAM_GROUP);
        return 0u;
    }
    if (PtrToSamplePtr == NULL_PTR)
    {
        ADC_DET(ADC_API_ID_GET_STREAM_LAST_POINTER, ADC_E_PARAM_POINTER);
        return 0u;
    }
    st = &s_groups[Group];
    if ((st->status == ADC_BUSY) || (st->status == ADC_IDLE) || (!st->bufferSet) || (st->samples == 0u))
    {
        return 0u;   /* SWS_Adc_00216：BUSY 时返回 0 且指针为 NULL */
    }
    valid = st->samples;
    if (st->cfg->accessMode == (uint8)ADC_ACCESS_MODE_STREAMING)
    {
        uint16 last = (st->writeIdx == 0u) ? (uint16)(st->cfg->streamSamples - 1u)
                                           : (uint16)(st->writeIdx - 1u);
        *PtrToSamplePtr = &st->buffer[(uint16)(last * st->cfg->channelCount)];
    }
    else
    {
        *PtrToSamplePtr = &st->buffer[0];
    }

    if (st->cfg->convMode == (uint8)ADC_CONV_MODE_CONTINUOUS)
    {
        (void)adc_round(st);
        adc_store_round(st);
        st->status = (st->status == ADC_STREAM_COMPLETED) ? ADC_STREAM_COMPLETED : ADC_COMPLETED;
        adc_notify(st);
    }
    else
    {
        st->status = ADC_IDLE;
    }
    return valid;
}

void Adc_GetVersionInfo(Std_VersionInfoType *versioninfo)
{
    if (versioninfo == NULL_PTR)
    {
        ADC_DET(ADC_API_ID_GET_VERSION_INFO, ADC_E_PARAM_POINTER);
        return;
    }
    versioninfo->vendorID = ADC_VENDOR_ID;
    versioninfo->moduleID = ADC_MODULE_ID;
    versioninfo->sw_major_version = ADC_SW_MAJOR_VERSION;
    versioninfo->sw_minor_version = ADC_SW_MINOR_VERSION;
    versioninfo->sw_patch_version = ADC_SW_PATCH_VERSION;
}

/* ---------------------------------------------------------------- 扩展 API */

uint32 Adc_GetConvClockHz(void)
{
    if (!s_initialized)
    {
        return 0u;
    }
    return s_cfg->adcBusHz / (uint32)s_cfg->clockDivider;
}

uint32 Adc_GetGroupSampleCount(Adc_GroupType Group)
{
    if ((!s_initialized) || (!adc_group_valid(Group)))
    {
        return 0u;
    }
    return s_groups[Group].totalConversions;
}

boolean Adc_IsInitialized(void)
{
    return s_initialized;
}
