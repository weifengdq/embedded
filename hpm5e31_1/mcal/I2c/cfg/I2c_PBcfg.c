/*
 * I2c_PBcfg.c —— HPM5E31(hpm5e31_1) 板级 I2C post-build 配置
 *
 * 板级映射（docs/MCAL开发约定.md §2）：
 *   I2C2 控制器：SCL PD02 / SDA PD03；I2C3 目标：SCL PD07 / SDA PD06（杜邦线 SCL-SCL、SDA-SDA）
 *   I2C3 目标地址 0x50（7 bit）；目标模式收/发用 HDMA ch6 + I2C3 中断。
 *   注意：PD06/PD07 与 qei 例程复用，不能同时初始化。
 * 三套 post-build 配置集演示 100k/400k/1M（I2C_Init 的 ConfigPtr 选择）。
 */
#include "I2c_PBcfg.h"
#include "I2c.h"
#include "Compiler.h"

#include "hpm_soc.h"
#include "hpm_interrupt.h"
#include "hpm_i2c_drv.h"
#include "hpm_dmav2_drv.h"
#include "hpm_dmamux_src.h"
#include "board.h"
#include "pins/pinmux_core.h"

MCAL_WEAK void I2C_SeqEndNotification_Cb(void) { }

static uint32 i2c2_clock_enable(void) { return board_init_i2c_clock(HPM_I2C2); }
static uint32 i2c3_clock_enable(void) { return board_init_i2c_clock(HPM_I2C3); }

/* 中断向量 -> 驱动统一入口 */
SDK_DECLARE_EXT_ISR_M(IRQn_I2C3, i2c3_mcal_isr)
void i2c3_mcal_isr(void)
{
    I2C_Isr_Channel(I2C_CH_I2C3_TARGET);
}

static const I2C_JobConfigType I2C_Jobs[I2C_JOB_COUNT] = {
    { I2C_CH_I2C2_MASTER, I2C_TARGET_ADDR },   /* JOB0: 控制器写 */
    { I2C_CH_I2C3_TARGET, 0u },                /* JOB1: 目标预备（地址命中后按方向自动收发） */
    { I2C_CH_I2C2_MASTER, I2C_TARGET_ADDR },   /* JOB2: 控制器读 */
    { I2C_CH_I2C2_MASTER, I2C_TARGET_ADDR },   /* JOB3: 控制器写+读 */
};

static const I2C_SequenceConfigType I2C_Sequences[I2C_SEQUENCE_COUNT] = {
    { 2u, { I2C_JOB_TARGET, I2C_JOB_MASTER_TX },   I2C_SeqEndNotification_Cb },
    { 2u, { I2C_JOB_TARGET, I2C_JOB_MASTER_RX },   I2C_SeqEndNotification_Cb },
    { 2u, { I2C_JOB_TARGET, I2C_JOB_MASTER_RDWR }, I2C_SeqEndNotification_Cb },
};

/* 三套速度配置集 */
#define I2C_MK_CFG(name, khz)                                                     \
static const I2C_ChannelConfigType I2C_Channels_##name[I2C_CHANNEL_COUNT] = {      \
    {                                                                              \
        .Base = (void *)HPM_I2C2, .DmaBase = (void *)HPM_HDMA,                     \
        .DmamuxBase = (void *)HPM_DMAMUX,                                          \
        .PinInit = init_i2c2_master_pins, .ClockEnable = i2c2_clock_enable,          \
        .IsTarget = FALSE, .TargetAddress = 0u, .BaudrateKHz = (khz),               \
        .IrqNum = (uint16)IRQn_I2C2, .RxDmaChannel = 0u, .RxDmaSrc = 0u,            \
    },                                                                             \
    {                                                                              \
        .Base = (void *)HPM_I2C3, .DmaBase = (void *)HPM_HDMA,                      \
        .DmamuxBase = (void *)HPM_DMAMUX,                                           \
        .PinInit = init_i2c3_slave_pins, .ClockEnable = i2c3_clock_enable,           \
        .IsTarget = TRUE, .TargetAddress = I2C_TARGET_ADDR, .BaudrateKHz = (khz),    \
        .IrqNum = (uint16)IRQn_I2C3, .RxDmaChannel = 6u,                             \
        .RxDmaSrc = (uint8)HPM_DMA_SRC_I2C3,                                         \
    },                                                                             \
};                                                                                 \
const I2C_ConfigType I2C_Config_##name = {                                         \
    I2C_Channels_##name, I2C_Jobs, I2C_Sequences                                   \
};

I2C_MK_CFG(100k, 100u)
I2C_MK_CFG(400k, 400u)
I2C_MK_CFG(1M,   1000u)

const I2C_ConfigType I2C_Config = { I2C_Channels_100k, I2C_Jobs, I2C_Sequences };
