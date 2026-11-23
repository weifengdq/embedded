/*
 * sys_arch.h - lwIP 2.1.2 的 FreeRTOS sys_arch 移植：类型与宏（HPM5E31 / RISC-V / FreeRTOS 10.5.1）
 *
 * 用途：让 lwIP 以 NO_SYS=0（netconn/tcpip 线程模型）跑在 FreeRTOS 上。
 * SDK v1.13.0 只在 samples/lwip/ports/freertos/ 里带了一份样例级移植（650 行，随样例复制），
 * middleware 里没有正式 sys_arch；本组件是书稿第 50 章用的"最小可讲清楚"的移植（约 530 行），
 * 并额外带 4 类可测点：
 *   1) sys 原语调用计数（sem/mbox/mutex/protect）；
 *   2) SYS_ARCH_PROTECT 进入次数、最大/累计关中断 cycle 数；
 *   3) 1 ms tick 采样的空闲率（CPU 占用 = 100% - 空闲率）；
 *   4) 任务栈水位（uxTaskGetStackHighWaterMark）。
 *
 * 设计约束（与 lwIP 官方 sys_arch 契约一致）：
 *  - 类型用 struct 包一层而不是裸 void*，保留一点类型安全，且不把 FreeRTOS 头暴露给 lwIP 源码；
 *  - FreeRTOS 对象只在本组件的 .c 里创建/删除，本头文件不 include FreeRTOS（lwIP 到处 include 本头）；
 *  - sys_arch_protect() 内部 taskENTER_CRITICAL()，返回值用于计"这次临界区开了多少个 cycle"，
 *    不当作中断状态恢复值 —— RV32 的 taskENTER_CRITICAL 不返回旧 mstatus，本移植用计数器做 sanity。
 *
 * 作者：eth-stack（task-2）
 */
#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

#include "lwip/opt.h"
#include "lwip/arch.h"

/** sys_mbox_new() 允许的最大深度（lwIP 侧用 TCPIP_MBOX_SIZE 等传入） */
#define SYS_LWIP_MBOX_SIZE              (32)
/** lwIP 的"永久等待"取值，与 lwIP 官方移植一致 */
#define SYS_LWIP_TIMEOUT_FOREVER        (0U)
/** _fromisr() 系列返回值：告诉调用方需要触发一次上下文切换 */
#define ERR_NEED_SCHED                  (123)

#if SYS_LIGHTWEIGHT_PROT
typedef u32_t sys_prot_t;
#endif

#if !LWIP_COMPAT_MUTEX
struct _sys_mut {
  void *mut;                      /*!< SemaphoreHandle_t（递归互斥量） */
};
typedef struct _sys_mut sys_mutex_t;
#define sys_mutex_valid_val(mutex)   ((mutex).mut != NULL)
#define sys_mutex_valid(mutex)       (((mutex) != NULL) && sys_mutex_valid_val(*(mutex)))
#define sys_mutex_set_invalid(mutex) ((mutex)->mut = NULL)
#endif /* !LWIP_COMPAT_MUTEX */

struct _sys_sem {
  void *sem;                      /*!< SemaphoreHandle_t（二值信号量） */
};
typedef struct _sys_sem sys_sem_t;
#define sys_sem_valid_val(sema)      ((sema).sem != NULL)
#define sys_sem_valid(sema)          (((sema) != NULL) && sys_sem_valid_val(*(sema)))
#define sys_sem_set_invalid(sema)    ((sema)->sem = NULL)

struct _sys_mbox {
  void *mbx;                      /*!< QueueHandle_t（深度 = 消息条数，元素 = void*） */
};
typedef struct _sys_mbox sys_mbox_t;
#define sys_mbox_valid_val(mbox)     ((mbox).mbx != NULL)
#define sys_mbox_valid(mbox)         (((mbox) != NULL) && sys_mbox_valid_val(*(mbox)))
#define sys_mbox_set_invalid(mbox)   ((mbox)->mbx = NULL)

struct _sys_thread {
  void *thread_handle;            /*!< TaskHandle_t */
};
typedef struct _sys_thread sys_thread_t;

#endif /* LWIP_ARCH_SYS_ARCH_H */
