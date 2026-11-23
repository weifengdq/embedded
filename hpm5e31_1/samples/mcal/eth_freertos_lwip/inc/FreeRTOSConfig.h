/*
 * FreeRTOSConfig.h - eth_freertos_lwip 的 FreeRTOS 10.5.1 配置（HPM5E31，mtime tick）
 *
 * 关键选择（第 50 章"FreeRTOS 集成"节会解释为什么）：
 *   - configTICK_RATE_HZ = 1000：lwIP 的 sys_now() 直接就是 tick 数（portTICK_PERIOD_MS=1），
 *     代价是每秒 1000 次 tick 中断（实测 ~0.3% CPU）；
 *   - configMAX_PRIORITIES = 8：够摆"收包任务 > tcpip > 应用 > shell > 统计 > idle"；
 *   - configTOTAL_HEAP_SIZE = 96 KB：FreeRTOS 堆只放任务/队列/信号量，
 *     lwIP 自己还有 MEM_SIZE 24 KB 的堆（两套堆分开，便于各自观测水位）；
 *   - configCHECK_FOR_STACK_OVERFLOW = 2：溢出钩子由 components/freertos_lwip_port 提供；
 *   - configUSE_TICK_HOOK = 1：tick 里采"当前是不是 idle 任务"，得到 CPU 占用（1 ms 粒度）。
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "hpm_soc.h"

#undef ARRAY_SIZE

#if (portasmHAS_MTIME == 0)
#define configMTIME_BASE_ADDRESS                (0)
#define configMTIMECMP_BASE_ADDRESS             (0)
#else
#define configMTIME_BASE_ADDRESS                (HPM_MCHTMR_BASE)
#define configMTIMECMP_BASE_ADDRESS             (HPM_MCHTMR_BASE + 8UL)
#endif

#define configSTACK_DEPTH_TYPE                  uint32_t

#define configUSE_PREEMPTION                    1
#define configCPU_CLOCK_HZ                      ((uint32_t) 24000000) /* MCHTMR 固定 24 MHz */
#define configTICK_RATE_HZ                      ((TickType_t) 1000)
#define configMAX_PRIORITIES                    (8)
#define configMINIMAL_STACK_SIZE                (512)
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 0
#define configUSE_APPLICATION_TASK_TAG          0
#define configUSE_MUTEXES                       1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_RECURSIVE_MUTEXES             1

#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ((size_t) (96 * 1024))

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     1
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetHandle                  1

#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         2

#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                8
#define configTIMER_TASK_STACK_DEPTH            (1024)

#define configASSERT(x) if ((x) == 0) { taskDISABLE_INTERRUPTS(); __asm volatile("ebreak"); for (;;); }

#endif /* FREERTOS_CONFIG_H */
