/*
 * Gpt_PBcfg.c —— GPT post-build 配置实例 + 板级 glue（ISR 向量桩）
 *
 * 板级资源（与 docs/MCAL开发约定.md §2 一致）：
 *   GPTMR0 CH0  : GPT_CHANNEL_0，1 ms 周期 tick
 *   GPTMR1 CH0  : GPT_CHANNEL_1，one-shot / 时间戳
 *   GPTMR0 CH3  : Predef Timer 自由运行时基（不产生中断）
 *   GPTMR3      : BSP 的 board_delay_us 占用，本文件不配置
 *
 * 为什么 ISR 向量桩放在这里：HPM SDK 的 SDK_DECLARE_EXT_ISR_M(irq_num, isr)
 * 用 ## 拼接 irq_num 生成向量表符号，参数必须是字面量 IRQn_xxx 标记；
 * 驱动 Gpt.c 不出现实例名，只暴露 Gpt_IsrHandler(Channel) 供桩调用。
 */
#include "Gpt.h"
#include "Gpt_PBcfg.h"
#include "hpm_soc.h"
#include "hpm_clock_drv.h"
#include "hpm_gptmr_drv.h"
#include "hpm_interrupt.h"

/* ---------- 中断向量桩（每实例一个入口，再按逻辑通道分发） ---------- */
SDK_DECLARE_EXT_ISR_M(IRQn_GPTMR0, gptmr0_isr_stub)
void gptmr0_isr_stub(void)
{
    Gpt_IsrHandler(GPT_CHANNEL_0);
}

SDK_DECLARE_EXT_ISR_M(IRQn_GPTMR1, gptmr1_isr_stub)
void gptmr1_isr_stub(void)
{
    Gpt_IsrHandler(GPT_CHANNEL_1);
}

/* ---------- 通知回调默认实现（弱符号，应用可覆盖） ---------- */
MCAL_WEAK void Gpt_Notification_0(void)
{
}

MCAL_WEAK void Gpt_Notification_1(void)
{
}

/* ---------- 通道配置表 ---------- */
static const Gpt_ChannelConfigType Gpt_ChannelConfig[] = {
    /* GPT_CHANNEL_0: GPTMR0 CH0，连续模式，1 ms tick 例程使用 */
    {
        (uint32)(uintptr_t)HPM_GPTMR0,
        (uint32)IRQn_GPTMR0,
        (uint32)clock_gptmr0,
        0u,
        GPT_CHANNEL_MODE_CONTINUOUS,
        &Gpt_Notification_0
    },
    /* GPT_CHANNEL_1: GPTMR1 CH0，one-shot 模式 */
    {
        (uint32)(uintptr_t)HPM_GPTMR1,
        (uint32)IRQn_GPTMR1,
        (uint32)clock_gptmr1,
        0u,
        GPT_CHANNEL_MODE_ONESHOT,
        &Gpt_Notification_1
    },
};

/* ---------- Predef Timer：GPTMR0 CH3 自由运行 ---------- */
static const Gpt_PredefTimerConfigType Gpt_PredefTimerConfig = {
    (uint32)(uintptr_t)HPM_GPTMR0,
    (uint32)clock_gptmr0,
    3u,     /* GPTMR0 CH3（CH0 已给 GPT_CHANNEL_0） */
    100u,   /* 名义值：100 MHz / 1 MHz = 100 ticks/µs，运行时由驱动按实测时钟重算 */
};

const Gpt_ConfigType Gpt_Config = {
    (uint8)GPT_NUM_CHANNELS,
    Gpt_ChannelConfig,
    &Gpt_PredefTimerConfig
};
