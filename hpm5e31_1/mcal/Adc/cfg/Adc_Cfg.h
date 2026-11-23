/*
 * Adc_Cfg.h —— ADC 模块预编译配置
 *
 * 参考：AUTOSAR_CP_SWS_ADCDriver §10；例程 samples/mcal/adc_single、adc_bench
 */
#ifndef ADC_CFG_H
#define ADC_CFG_H

#include "Mcal_Cfg.h"

#ifndef ADC_DEV_ERROR_DETECT
#define ADC_DEV_ERROR_DETECT        MCAL_DEV_ERROR_DETECT
#endif

/* AdcDeInitApi（SWS_Adc_00228） */
#ifndef ADC_DEINIT_API
#define ADC_DEINIT_API              STD_ON
#endif

/* AdcEnableStartStopGroupApi（SWS_Adc_00259/00260） */
#ifndef ADC_ENABLE_START_STOP_GROUP_API
#define ADC_ENABLE_START_STOP_GROUP_API STD_ON
#endif

/* AdcReadGroupApi（SWS_Adc_00359） */
#ifndef ADC_READ_GROUP_API
#define ADC_READ_GROUP_API          STD_ON
#endif

/* AdcHwTriggerApi（SWS_Adc_00265） */
#ifndef ADC_HW_TRIGGER_API
#define ADC_HW_TRIGGER_API          STD_ON
#endif

/* AdcGrpNotifCapability（SWS_Adc_00100） */
#ifndef ADC_GRP_NOTIF_CAPABILITY
#define ADC_GRP_NOTIF_CAPABILITY    STD_ON
#endif

/* AdcVersionInfoApi */
#ifndef ADC_VERSION_INFO_API
#define ADC_VERSION_INFO_API        MCAL_VERSION_INFO_API
#endif

/* 结果对齐：右对齐（本驱动 BUS_RESULT.CHAN_RESULT 即右对齐 16 bit） */
#define ADC_RESULT_ALIGNMENT_RIGHT  (1u)

/* 本板组数（与 Adc_PBcfg.c 一致） */
#define ADC_GROUP_COUNT_CFG         (2u)
/* 单组最多通道数 */
#define ADC_MAX_CHANNELS_PER_GROUP  (4u)
/* ADC16 转换时钟上限（HPM5E31 ADC16_SOC_CONV_CLK_FREQ_MAX） */
#define ADC_CONV_CLK_FREQ_MAX       (50000000u)

#endif /* ADC_CFG_H */
