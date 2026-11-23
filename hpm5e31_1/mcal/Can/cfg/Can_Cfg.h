/*
 * Can_Cfg.h —— CAN 驱动预编译配置（Pre-compile 阶段，契约 §5.1）
 *
 * 依据：AUTOSAR_CP_SWS_CANDriver §10 CanGeneral / CanConfigSet；
 *       对应 HPM5E31 MCAN0~3，板级映射见 Can_PBcfg.c。
 *
 * 改这里影响"编进固件的能力集合"，改板级参数请改 Can_PBcfg.c。
 */
#ifndef CAN_CFG_H
#define CAN_CFG_H

#include "Mcal_Cfg.h"

/* ---------------- 规模 ---------------- */
#define CAN_CONTROLLER_COUNT        (4U)  /*!< MCAN0~MCAN3，契约 §2 划给 Can 负责人 */
#define CAN_MAX_HTH                 (16U) /*!< 发送硬件对象（HTH）总数 = 4 控制器 x 4 */
#define CAN_MAX_HRH                 (8U)  /*!< 接收硬件对象（HRH）总数 */
#define CAN_MAX_FILTER_ELEMS        (16U) /*!< 单控制器单方向（std/ext）过滤器元素上限 */
#define CAN_MAX_BAUDRATE_CONFIGS    (20U) /*!< 单控制器波特率配置条目上限（含 5M 位时序扫描实验项） */

/* ---------------- 帧格式能力 ---------------- */
#define CAN_FD_SUPPORT              (STD_ON)  /*!< 支持 CAN FD（64 字节 / BRS / TDC） */
#define CAN_EXTENDED_ID_SUPPORT     (STD_ON)  /*!< 支持 29 位扩展 ID */
#define CAN_TX_BUF_PER_CONTROLLER   (4U)      /*!< 每控制器专用发送缓冲（=HTH 上限） */
#define CAN_MSG_BUF_WORDS           (640U)    /*!< 每控制器消息 RAM 字数（2560 B，SDK 默认） */

/* ---------------- 中断/轮询模型 ---------------- */
/*! 接收：1 = 中断驱动（ISR 里做 RxIndication），0 = MainFunction_Read 轮询 */
#define CAN_RX_PROCESSING_INTERRUPT (0U)
/*! 发送确认：1 = 中断驱动（ISR 里做 TxConfirmation），0 = MainFunction_Write 轮询 */
#define CAN_TX_CONFIRM_IN_INTERRUPT (0U)
/*! 外部中断优先级（RV32 CLINT/PLIC，数值越大优先级越高，范围 1..15） */
#define CAN_IRQ_PRIORITY            (3U)

/* ---------------- 开发错误检测 ---------------- */
#ifndef CAN_DEV_ERROR_DETECT
#define CAN_DEV_ERROR_DETECT        (MCAL_DEV_ERROR_DETECT)
#endif

/*! Can_EnableControllerInterrupts / DisableControllerInterrupts 的嵌套深度上限 */
#define CAN_INT_DISABLE_COUNT_MAX   (255U)

/* ---------------- 版本号（SWS_Can_00143 等） ---------------- */
#define CAN_AR_RELEASE_MAJOR_VERSION    (4U)
#define CAN_AR_RELEASE_MINOR_VERSION    (4U)
#define CAN_AR_RELEASE_REVISION_VERSION (0U)
#define CAN_SW_MAJOR_VERSION            (1U)
#define CAN_SW_MINOR_VERSION            (0U)
#define CAN_SW_PATCH_VERSION            (0U)
#define CAN_VENDOR_ID                   (0U) /*!< 本实现非 AUTOSAR 成员 vendor，填 0 */

/* ---------------- 可选 API 开关 ---------------- */
#define CAN_VERSION_INFO_API            (MCAL_VERSION_INFO_API) /*!< Can_GetVersionInfo */
#define CAN_WAKEUP_SUPPORT              (STD_OFF) /*!< M_CAN 无总线唤醒检测，见第 47 章 */

#endif /* CAN_CFG_H */
