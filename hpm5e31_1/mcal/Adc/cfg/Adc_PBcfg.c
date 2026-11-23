/*
 * Adc_PBcfg.c —— ADC post-build 配置实例（HPM5E31 / hpm5e31_1）
 *
 * 板级知识只有三样：ADC 实例、通道<->引脚、时钟。改板只改本文件。
 * 例程：samples/mcal/adc_single、samples/mcal/adc_bench
 */
#include "Adc_PBcfg.h"
#include "hpm_soc.h"

/* 例程用的两个组：0=单次，1=连续（同一通道，交替使用，不同时启动） */
static const Adc_GroupConfigType Adc_Groups[ADC_GROUP_COUNT_CFG] =
{
    {
        .group = ADC_GROUP_SINGLE,
        .channel = ADC_PBCFG_CH_PF26,
        .triggerSource = (uint8)ADC_TRIGG_SRC_SW,
        .convMode = (uint8)ADC_CONV_MODE_ONESHOT,
        .accessMode = (uint8)ADC_ACCESS_MODE_SINGLE,
        .resolution = (uint8)16u,
        .streamBufferMode = (uint8)ADC_STREAM_BUFFER_LINEAR,
        .channelCount = 1u,
        .samplingTime = 20u,
        .streamSamples = ADC_PBCFG_STREAM_SAMPLES,
        .padIndex = (uint16)IOC_PAD_PF26,
        .notification = NULL_PTR
    },
    {
        .group = ADC_GROUP_CONTINUOUS,
        .channel = ADC_PBCFG_CH_PF26,
        .triggerSource = (uint8)ADC_TRIGG_SRC_SW,
        .convMode = (uint8)ADC_CONV_MODE_CONTINUOUS,
        .accessMode = (uint8)ADC_ACCESS_MODE_SINGLE,
        .resolution = (uint8)16u,
        .streamBufferMode = (uint8)ADC_STREAM_BUFFER_LINEAR,
        .channelCount = 1u,
        .samplingTime = 20u,
        .streamSamples = ADC_PBCFG_STREAM_SAMPLES,
        .padIndex = (uint16)IOC_PAD_PF26,
        .notification = NULL_PTR
    },
};

const Adc_ConfigType Adc_Config =
{
    .adcBase = (uint32)HPM_ADC0_BASE,
    .sysctlBase = (uint32)HPM_SYSCTL_BASE,
    .iocBase = (uint32)HPM_IOC_BASE,
    .adcClockNode = (uint16)0u,
    .adcClockSrc = (uint16)0u,       /* 0 = ahb0（SYSCTL.ADCCLK.MUX） */
    .adcBusHz = 200000000u,          /* ahb0 = pll1_clk0 / 2，实测 200 MHz */
    .clockDivider = (uint8)4u,       /* 200 MHz / 4 = 50 MHz */
    .groupCount = (uint8)ADC_GROUP_COUNT_CFG,
    .reserved0 = 0u,
    .groups = Adc_Groups
};
