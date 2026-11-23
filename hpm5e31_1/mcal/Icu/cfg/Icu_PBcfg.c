/*
 * Icu_PBcfg.c —— ICU post-build 配置实例 + 板级 glue（ISR 向量桩）
 *
 * 引脚依据 soc/HPM5E00/HPM5E31/hpm_iomux.h：
 *   PD06: ALT17 = TRGM_P_06     PD07: ALT17 = TRGM_P_07
 * TRGM 依据 soc/HPM5E00/HPM5E31/hpm_trgmmux_src.h：
 *   HPM_TRGM0_INPUT_SRC_TRGM0_P06/P07  ->  HPM_TRGM0_OUTPUT_SRC_GPTMR2_CAPT_2/3
 */
#include "Icu.h"
#include "Icu_PBcfg.h"
#include "hpm_soc.h"
#include "hpm_iomux.h"
#include "hpm_clock_drv.h"
#include "hpm_gptmr_drv.h"
#include "hpm_trgm_drv.h"
#include "hpm_interrupt.h"

/* 中断向量桩（SDK 要求 IRQn 字面量） */
SDK_DECLARE_EXT_ISR_M(IRQn_GPTMR2, gptmr2_isr_stub)
void gptmr2_isr_stub(void)
{
    Icu_IsrHandler();
}

MCAL_WEAK void Icu_SignalNotification_0(void) { }
MCAL_WEAK void Icu_SignalNotification_1(void) { }
MCAL_WEAK void Icu_TimestampNotification_0(void) { }
MCAL_WEAK void Icu_TimestampNotification_1(void) { }

static const Icu_ChannelConfigType Icu_ChannelConfig[] = {
    /* ICU_CHANNEL_0: GPTMR2 CH2 <- TRGM0_P06 <- PD06 */
    {
        (uint32)(uintptr_t)HPM_GPTMR2,
        (uint32)IRQn_GPTMR2,
        (uint32)clock_gptmr2,
        (uint32)(uintptr_t)HPM_TRGM0,
        (uint32)HPM_TRGM0_INPUT_SRC_TRGM0_P06,
        (uint32)HPM_TRGM0_OUTPUT_SRC_GPTMR2_CAPT_2,
        (uint32)(uintptr_t)HPM_IOC,
        (uint32)IOC_PAD_PD06,
        (uint32)IOC_PD06_FUNC_CTL_TRGM_P_06,
        2u,
        ICU_MODE_SIGNAL_MEASUREMENT,
        ICU_DUTY_CYCLE,
        ICU_RISING_EDGE,
        ICU_LINEAR_BUFFER,
        &Icu_SignalNotification_0,
        &Icu_TimestampNotification_0
    },
    /* ICU_CHANNEL_1: GPTMR2 CH3 <- TRGM0_P07 <- PD07 */
    {
        (uint32)(uintptr_t)HPM_GPTMR2,
        (uint32)IRQn_GPTMR2,
        (uint32)clock_gptmr2,
        (uint32)(uintptr_t)HPM_TRGM0,
        (uint32)HPM_TRGM0_INPUT_SRC_TRGM0_P07,
        (uint32)HPM_TRGM0_OUTPUT_SRC_GPTMR2_CAPT_3,
        (uint32)(uintptr_t)HPM_IOC,
        (uint32)IOC_PAD_PD07,
        (uint32)IOC_PD07_FUNC_CTL_TRGM_P_07,
        3u,
        ICU_MODE_SIGNAL_MEASUREMENT,
        ICU_DUTY_CYCLE,
        ICU_RISING_EDGE,
        ICU_LINEAR_BUFFER,
        &Icu_SignalNotification_1,
        &Icu_TimestampNotification_1
    },
};

const Icu_ConfigType Icu_Config = {
    (uint8)ICU_NUM_CHANNELS,
    Icu_ChannelConfig
};