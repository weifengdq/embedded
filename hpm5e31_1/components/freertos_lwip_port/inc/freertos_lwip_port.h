/*
 * freertos_lwip_port.h - 自写 FreeRTOS + lwIP 移植的对外接口（统计、CPU 占用、栈水位）
 *
 * 只在应用代码里 include；lwIP 源码只看 arch/sys_arch.h。
 *
 * 作者：eth-stack（task-2）
 */
#ifndef FREERTOS_LWIP_PORT_H
#define FREERTOS_LWIP_PORT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** sys 原语与临界区统计（只增不减，freertos_lwip_port_stats_reset() 清零） */
typedef struct freertos_lwip_port_stats_struct {
    uint32_t sem_new;               /*!< sys_sem_new 次数 */
    uint32_t sem_wait;              /*!< sys_arch_sem_wait 次数（含超时） */
    uint32_t sem_wait_timeout;      /*!< sys_arch_sem_wait 超时次数 */
    uint32_t sem_signal;            /*!< sys_sem_signal 次数 */
    uint32_t mbox_new;              /*!< sys_mbox_new 次数 */
    uint32_t mbox_post;             /*!< sys_mbox_post / trypost 成功次数 */
    uint32_t mbox_post_fail;        /*!< sys_mbox_post / trypost 失败（队列满）次数 */
    uint32_t mbox_fetch;            /*!< sys_arch_mbox_fetch 次数（含超时） */
    uint32_t mbox_tryfetch_empty;   /*!< tryfetch 空队列次数 */
    uint32_t mutex_new;             /*!< sys_mutex_new 次数 */
    uint32_t mutex_lock;            /*!< sys_mutex_lock 次数 */
    uint32_t protect_enter;         /*!< SYS_ARCH_PROTECT 进入次数（lwIP 热路径） */
    uint32_t protect_max_cycles;    /*!< 单次临界区最长的 CPU cycle 数 */
    uint64_t protect_total_cycles;  /*!< 临界区累计 CPU cycle 数 */
    uint32_t thread_new;            /*!< sys_thread_new 成功次数 */
} freertos_lwip_port_stats_t;

/** CPU 占用采样（tick hook 里做，1 ms 粒度） */
typedef struct freertos_lwip_cpu_load_struct {
    uint32_t idle_permille;         /*!< 空闲率 ×1000（900 = 90.0% 空闲） */
    uint32_t window_ticks;          /*!< 本次统计窗口的 tick 数 */
    uint32_t idle_ticks;            /*!< 窗口内采到 idle 任务的 tick 数 */
} freertos_lwip_cpu_load_t;

/** 取全部 sys 原语统计的副本 */
void freertos_lwip_port_stats_get(freertos_lwip_port_stats_t *out);

/** 清零统计（不影响 FreeRTOS 对象） */
void freertos_lwip_port_stats_reset(void);

/** 打印统计到串口（一行 sem/mbox，一行 mutex/protect） */
void freertos_lwip_port_stats_print(void);

/**
 * @brief 取上一个窗口的 CPU 占用并重开窗口
 * @param out 采样结果（可为 NULL）
 * @return 空闲率 ×1000（0..1000），1000 = 全程空闲
 */
uint32_t freertos_lwip_cpu_load_take(freertos_lwip_cpu_load_t *out);

/** 打印所有任务的栈水位（单位：字；剩余字数，越小越危险） */
void freertos_lwip_port_print_task_stack(void);

/** 已发生的 malloc 失败次数（configUSE_MALLOC_FAILED_HOOK） */
uint32_t freertos_lwip_port_malloc_fail_count(void);

/** 已发生的栈溢出次数（configCHECK_FOR_STACK_OVERFLOW>=2） */
uint32_t freertos_lwip_port_stack_overflow_count(void);

/** tick hook 实现（本组件提供 vApplicationTickHook，应用不要再定义） */
void freertos_lwip_port_tick_hook(void);

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_LWIP_PORT_H */
