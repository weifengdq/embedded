/*
 * Gpt_Cfg.h —— GPT 预编译配置（SWS_Gpt §10.2.7 GptConfigurationOfOptApiServices）
 *
 * 全局开关 MCAL_DEV_ERROR_DETECT 在 mcal/common/Mcal_Cfg.h；本文件把它落到模块级，
 * 并裁剪可选 API（DeInit/TimeElapsed/TimeRemaining/VersionInfo/SetMode）。
 */
#ifndef GPT_CFG_H
#define GPT_CFG_H

#include "Mcal_Cfg.h"

/* ---- 通道数：与 Gpt_PBcfg.c 的 Gpt_ChannelConfig[] 长度一致 ---- */
#define GPT_NUM_CHANNELS            (2u)

/* ---- Dev Error Detect ---- */
#ifndef GPT_DEV_ERROR_DETECT
#define GPT_DEV_ERROR_DETECT        MCAL_DEV_ERROR_DETECT
#endif

/* ---- 可选 API 裁剪（SWS_Gpt §10.2.7） ---- */
#define GPT_DEINIT_API              STD_ON   /* GptDeinitApi              */
#define GPT_TIME_ELAPSED_API        STD_ON   /* GptTimeElapsedApi         */
#define GPT_TIME_REMAINING_API      STD_ON   /* GptTimeRemainingApi       */
#define GPT_VERSION_INFO_API        MCAL_VERSION_INFO_API /* GptVersionInfoApi */
#define GPT_SET_MODE_API            STD_ON   /* GptWakeupFunctionalityApi */
#define GPT_NOTIFICATION_SUPPORTED  STD_ON
#define GPT_PREDEF_TIMER_SUPPORTED  STD_ON

/* ---- 唤醒功能：本子集不实现（无 EcuM），见第 45 章"待确认项与风险" ---- */
#define GPT_WAKEUP_SUPPORTED        STD_OFF

/* ---- 中断优先级（NVIC） ---- */
#define GPT_IRQ_PRIORITY            (1u)

/* ---- 单次可装载最大 tick 数（32 位 GPTMR） ---- */
#define GPT_MAX_TICKS               (0xFFFFFFFFuL)

/* ---- 内部时钟换算（防御除零） ---- */
#define GPT_MIN_CLOCK_HZ            (1000000uL)

#endif /* GPT_CFG_H */
