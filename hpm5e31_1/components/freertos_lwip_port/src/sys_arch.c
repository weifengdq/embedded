/*
 * sys_arch.c - lwIP 2.1.2 在 FreeRTOS 10.5.1（HPM5E31 RISC-V，mtime tick）上的最小 sys_arch 移植
 *
 * 契约来源：lwIP 2.1.x 官方 docs/sys_arch.txt + contrib/ports/freertos。
 * 与 SDK 样例移植（hpm_sdk/samples/lwip/ports/freertos/arch/sys_arch.c，650 行）的差别：
 *   - 只保留 lwIP 2.1.2 真正调用的接口（无 NETCONN_SEM_PER_THREAD、无 sys_check_core_locking）；
 *   - 每个原语都带统计，SYS_ARCH_PROTECT 额外测"单次最长关中断 cycle 数"（书稿要用实测数）；
 *   - 自带 vApplicationTickHook / malloc 失败 / 栈溢出钩子，空闲率由 tick 采样得到，不依赖
 *     configGENERATE_RUN_TIME_STATS（SDK 模板默认关闭，且 32 位 runtime counter 在 480 MHz 下 9 s 就回绕）。
 *
 * 映射表（FreeRTOS 10.5.1）：
 *   sys_sem_t   -> SemaphoreHandle_t（二值）
 *   sys_mutex_t -> SemaphoreHandle_t（递归互斥量，configUSE_RECURSIVE_MUTEXES=1）
 *   sys_mbox_t  -> QueueHandle_t（元素 void*，深度 = lwIP 的 size）
 *   sys_thread_t-> TaskHandle_t
 *   sys_protect -> taskENTER_CRITICAL/taskEXIT_CRITICAL（关中断，最坏 ~20 cycle）
 *
 * 作者：eth-stack（task-2）
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/stats.h"
#include "lwip/tcpip.h"

#include "freertos_lwip_port.h"

#if !NO_SYS

/* ------------------------------------------------------------------ */
/* 内部状态                                                            */
/* ------------------------------------------------------------------ */
static freertos_lwip_port_stats_t s_stats;

/* CPU 空闲采样（在 tick hook 里累加，1 ms 一次） */
static volatile uint32_t s_window_ticks;
static volatile uint32_t s_window_idle_ticks;
static volatile uint32_t s_last_idle_permille;
static volatile uint32_t s_last_window_ticks;

static volatile uint32_t s_malloc_fail;
static volatile uint32_t s_stack_ovf;

/* 统计里要用的 cycle 计数（RV32 的 rdcycle 只有 32 位，480 MHz 下 8.9 s 回绕，
   只用于测量单次临界区，不用于跨秒累计） */
static inline uint32_t port_rdcycle(void)
{
    uint32_t c;
    __asm volatile ("rdcycle %0" : "=r"(c));
    return c;
}

/* ------------------------------------------------------------------ */
/* 生命周期                                                            */
/* ------------------------------------------------------------------ */
void sys_init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_window_ticks = 0U;
    s_window_idle_ticks = 0U;
    s_last_idle_permille = 0U;
    s_last_window_ticks = 0U;
}

/* ------------------------------------------------------------------ */
/* 临界区保护（lwIP 的 SYS_ARCH_PROTECT）                               */
/* ------------------------------------------------------------------ */
sys_prot_t sys_arch_protect(void)
{
    uint32_t c = port_rdcycle();

    taskENTER_CRITICAL();
    s_stats.protect_enter++;
    return (sys_prot_t) c;
}

void sys_arch_unprotect(sys_prot_t pval)
{
    uint32_t c = port_rdcycle();
    uint32_t dt = c - (uint32_t) pval;

    /* 32 位 rdcycle 回绕：只统计差值合理的样本（< 1 ms @480 MHz = 480000 拍） */
    if (dt < 480000U) {
        s_stats.protect_total_cycles += dt;
        if (dt > s_stats.protect_max_cycles) {
            s_stats.protect_max_cycles = dt;
        }
    }
    taskEXIT_CRITICAL();
}

/* ------------------------------------------------------------------ */
/* 时间                                                                */
/* ------------------------------------------------------------------ */
u32_t sys_now(void)
{
    return (u32_t) (xTaskGetTickCount() * (TickType_t) portTICK_PERIOD_MS);
}

u32_t sys_jiffies(void)
{
    return (u32_t) xTaskGetTickCount();
}

/* sys_msleep() 由 lwIP 核心 src/core/sys.c 提供（NO_SYS=0 时），移植层不要再定义，
 * 否则与 liblwip 重复定义；这里只保留 sys_now/sys_jiffies。 */

/* ------------------------------------------------------------------ */
/* 信号量                                                              */
/* ------------------------------------------------------------------ */
err_t sys_sem_new(sys_sem_t *sem, u8_t initial_count)
{
    if (sem == NULL) {
        return ERR_ARG;
    }
    sem->sem = (void *) xSemaphoreCreateBinary();
    if (sem->sem == NULL) {
        SYS_STATS_INC(sem.err);
        return ERR_MEM;
    }
    if (initial_count != 0U) {
        (void) xSemaphoreGive((SemaphoreHandle_t) sem->sem);
    }
    s_stats.sem_new++;
    SYS_STATS_INC_USED(sem);
    return ERR_OK;
}

void sys_sem_signal(sys_sem_t *sem)
{
    if ((sem == NULL) || (sem->sem == NULL)) {
        return;
    }
    /* 队列满说明信号已被置位，对二值信号量属正常（lwIP 只要求"唤醒"） */
    if (xSemaphoreGive((SemaphoreHandle_t) sem->sem) != pdTRUE) {
        SYS_STATS_INC(sem.err);
    }
    s_stats.sem_signal++;
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout_ms)
{
    TickType_t ticks;
    BaseType_t ret;

    if ((sem == NULL) || (sem->sem == NULL)) {
        return SYS_ARCH_TIMEOUT;
    }
    s_stats.sem_wait++;
    if (timeout_ms == 0U) {
        ret = xSemaphoreTake((SemaphoreHandle_t) sem->sem, portMAX_DELAY);
    } else {
        ticks = (TickType_t) ((timeout_ms + (u32_t) portTICK_PERIOD_MS - 1U) / (u32_t) portTICK_PERIOD_MS);
        if (ticks == 0U) {
            ticks = 1U;
        }
        ret = xSemaphoreTake((SemaphoreHandle_t) sem->sem, ticks);
    }
    if (ret != pdTRUE) {
        s_stats.sem_wait_timeout++;
        return SYS_ARCH_TIMEOUT;
    }
    return 1U;
}

void sys_sem_free(sys_sem_t *sem)
{
    if ((sem == NULL) || (sem->sem == NULL)) {
        return;
    }
    vSemaphoreDelete((SemaphoreHandle_t) sem->sem);
    sem->sem = NULL;
    SYS_STATS_DEC(sem.used);
}

/* ------------------------------------------------------------------ */
/* 互斥量（LWIP_COMPAT_MUTEX=0 时由 lwIP 直接调用）                     */
/* ------------------------------------------------------------------ */
#if !LWIP_COMPAT_MUTEX
err_t sys_mutex_new(sys_mutex_t *mutex)
{
    if (mutex == NULL) {
        return ERR_ARG;
    }
    /* 用递归互斥量：lwIP 的 TCPIP 核心锁存在同一任务重入（tcpip_callback 内部再保护） */
    mutex->mut = (void *) xSemaphoreCreateRecursiveMutex();
    if (mutex->mut == NULL) {
        SYS_STATS_INC(mutex.err);
        return ERR_MEM;
    }
    s_stats.mutex_new++;
    SYS_STATS_INC_USED(mutex);
    return ERR_OK;
}

void sys_mutex_lock(sys_mutex_t *mutex)
{
    if ((mutex == NULL) || (mutex->mut == NULL)) {
        return;
    }
    s_stats.mutex_lock++;
    (void) xSemaphoreTakeRecursive((SemaphoreHandle_t) mutex->mut, portMAX_DELAY);
}

void sys_mutex_unlock(sys_mutex_t *mutex)
{
    if ((mutex == NULL) || (mutex->mut == NULL)) {
        return;
    }
    (void) xSemaphoreGiveRecursive((SemaphoreHandle_t) mutex->mut);
}

void sys_mutex_free(sys_mutex_t *mutex)
{
    if ((mutex == NULL) || (mutex->mut == NULL)) {
        return;
    }
    vSemaphoreDelete((SemaphoreHandle_t) mutex->mut);
    mutex->mut = NULL;
    SYS_STATS_DEC(mutex.used);
}
#endif /* !LWIP_COMPAT_MUTEX */

/* ------------------------------------------------------------------ */
/* 邮箱（lwIP 的线程间消息队列，元素是 void*）                          */
/* ------------------------------------------------------------------ */
err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
    if (mbox == NULL) {
        return ERR_ARG;
    }
    if ((size <= 0) || (size > (int) SYS_LWIP_MBOX_SIZE)) {
        size = (int) SYS_LWIP_MBOX_SIZE;
    }
    mbox->mbx = (void *) xQueueCreate((UBaseType_t) size, sizeof(void *));
    if (mbox->mbx == NULL) {
        SYS_STATS_INC(mbox.err);
        return ERR_MEM;
    }
    s_stats.mbox_new++;
    SYS_STATS_INC_USED(mbox);
    return ERR_OK;
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
    if ((mbox == NULL) || (mbox->mbx == NULL)) {
        return;
    }
    if (xQueueSendToBack((QueueHandle_t) mbox->mbx, &msg, portMAX_DELAY) != pdTRUE) {
        s_stats.mbox_post_fail++;
        SYS_STATS_INC(mbox.err);
    } else {
        s_stats.mbox_post++;
    }
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
    if ((mbox == NULL) || (mbox->mbx == NULL)) {
        return ERR_ARG;
    }
    if (xQueueSendToBack((QueueHandle_t) mbox->mbx, &msg, 0) != pdTRUE) {
        s_stats.mbox_post_fail++;
        SYS_STATS_INC(mbox.err);
        return ERR_MEM;
    }
    s_stats.mbox_post++;
    return ERR_OK;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
    BaseType_t higher_woken = pdFALSE;

    if ((mbox == NULL) || (mbox->mbx == NULL)) {
        return ERR_ARG;
    }
    if (xQueueSendToBackFromISR((QueueHandle_t) mbox->mbx, &msg, &higher_woken) != pdTRUE) {
        s_stats.mbox_post_fail++;
        SYS_STATS_INC(mbox.err);
        return ERR_MEM;
    }
    s_stats.mbox_post++;
    return (higher_woken == pdTRUE) ? ERR_NEED_SCHED : ERR_OK;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout_ms)
{
    TickType_t ticks;
    BaseType_t ret;
    void *dummy = NULL;

    if (msg == NULL) {
        msg = &dummy;
    }
    if ((mbox == NULL) || (mbox->mbx == NULL)) {
        *msg = NULL;
        return SYS_ARCH_TIMEOUT;
    }
    s_stats.mbox_fetch++;
    if (timeout_ms == 0U) {
        ret = xQueueReceive((QueueHandle_t) mbox->mbx, msg, portMAX_DELAY);
    } else {
        ticks = (TickType_t) ((timeout_ms + (u32_t) portTICK_PERIOD_MS - 1U) / (u32_t) portTICK_PERIOD_MS);
        if (ticks == 0U) {
            ticks = 1U;
        }
        ret = xQueueReceive((QueueHandle_t) mbox->mbx, msg, ticks);
    }
    if (ret != pdTRUE) {
        *msg = NULL;
        return SYS_ARCH_TIMEOUT;
    }
    return 1U;
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
    void *dummy = NULL;

    if (msg == NULL) {
        msg = &dummy;
    }
    if ((mbox == NULL) || (mbox->mbx == NULL)) {
        *msg = NULL;
        return SYS_MBOX_EMPTY;
    }
    if (xQueueReceive((QueueHandle_t) mbox->mbx, msg, 0) != pdTRUE) {
        s_stats.mbox_tryfetch_empty++;
        *msg = NULL;
        return SYS_MBOX_EMPTY;
    }
    return 1U;
}

void sys_mbox_free(sys_mbox_t *mbox)
{
    void *msg;
    uint32_t drained = 0U;

    if ((mbox == NULL) || (mbox->mbx == NULL)) {
        return;
    }
    /* 把还没取走的消息排干，避免残留指针；lwIP 保证调用前已自己释放消息体 */
    while (xQueueReceive((QueueHandle_t) mbox->mbx, &msg, 0) == pdTRUE) {
        drained++;
    }
    if (drained != 0U) {
        printf("[lwip-port] warn: mbox freed with %lu queued msg\r\n", (unsigned long) drained);
    }
    vQueueDelete((QueueHandle_t) mbox->mbx);
    mbox->mbx = NULL;
    SYS_STATS_DEC(mbox.used);
}

/* ------------------------------------------------------------------ */
/* 线程                                                                */
/* ------------------------------------------------------------------ */
sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio)
{
    TaskHandle_t handle = NULL;
    sys_thread_t st;
    BaseType_t ret;
    uint32_t words;

    st.thread_handle = NULL;
    if ((thread == NULL) || (stacksize <= 0)) {
        return st;
    }
    /* lwIP 传的是字节数（TCPIP_THREAD_STACKSIZE），FreeRTOS 要的是 StackType_t 个数 */
    words = ((uint32_t) stacksize + 3U) / 4U;
    if (words < (uint32_t) configMINIMAL_STACK_SIZE) {
        words = (uint32_t) configMINIMAL_STACK_SIZE;
    }
    if (prio < 0) {
        prio = 1;
    }
    if (prio >= (int) configMAX_PRIORITIES) {
        prio = (int) configMAX_PRIORITIES - 1;
    }
    ret = xTaskCreate((TaskFunction_t) thread, (name != NULL) ? name : "lwip",
                      (configSTACK_DEPTH_TYPE) words, arg, (UBaseType_t) prio, &handle);
    if (ret != pdPASS) {
        printf("[lwip-port] sys_thread_new(%s) FAILED (words=%lu prio=%d)\r\n",
               (name != NULL) ? name : "?", (unsigned long) words, prio);
        return st;
    }
    s_stats.thread_new++;
    st.thread_handle = (void *) handle;
    return st;
}

/* ------------------------------------------------------------------ */
/* 统计 / CPU 占用 / 钩子                                               */
/* ------------------------------------------------------------------ */
void freertos_lwip_port_stats_get(freertos_lwip_port_stats_t *out)
{
    if (out != NULL) {
        *out = s_stats;
    }
}

void freertos_lwip_port_stats_reset(void)
{
    taskENTER_CRITICAL();
    memset(&s_stats, 0, sizeof(s_stats));
    taskEXIT_CRITICAL();
}

void freertos_lwip_port_stats_print(void)
{
    printf("[lwip-port] sem new=%lu wait=%lu(timeout %lu) signal=%lu | mbox new=%lu post=%lu(fail %lu) fetch=%lu(empty %lu)\r\n",
           (unsigned long) s_stats.sem_new, (unsigned long) s_stats.sem_wait,
           (unsigned long) s_stats.sem_wait_timeout, (unsigned long) s_stats.sem_signal,
           (unsigned long) s_stats.mbox_new, (unsigned long) s_stats.mbox_post,
           (unsigned long) s_stats.mbox_post_fail, (unsigned long) s_stats.mbox_fetch,
           (unsigned long) s_stats.mbox_tryfetch_empty);
    printf("[lwip-port] mutex new=%lu lock=%lu | protect enter=%lu max=%lu cyc avg=%lu.%02lu cyc | thread new=%lu\r\n",
           (unsigned long) s_stats.mutex_new, (unsigned long) s_stats.mutex_lock,
           (unsigned long) s_stats.protect_enter, (unsigned long) s_stats.protect_max_cycles,
           (unsigned long) ((s_stats.protect_enter != 0U) ?
                            (s_stats.protect_total_cycles / s_stats.protect_enter) : 0U),
           (unsigned long) ((s_stats.protect_enter != 0U) ?
                            ((s_stats.protect_total_cycles % s_stats.protect_enter) * 100U / s_stats.protect_enter) : 0U),
           (unsigned long) s_stats.thread_new);
}

uint32_t freertos_lwip_cpu_load_take(freertos_lwip_cpu_load_t *out)
{
    uint32_t total;
    uint32_t idle;
    uint32_t permille;

    taskENTER_CRITICAL();
    total = s_window_ticks;
    idle = s_window_idle_ticks;
    s_window_ticks = 0U;
    s_window_idle_ticks = 0U;
    taskEXIT_CRITICAL();

    permille = (total == 0U) ? 0U : (uint32_t) (((uint64_t) idle * 1000U) / total);
    s_last_idle_permille = permille;
    s_last_window_ticks = total;

    if (out != NULL) {
        out->idle_permille = permille;
        out->window_ticks = total;
        out->idle_ticks = idle;
    }
    return permille;
}

void freertos_lwip_port_print_task_stack(void)
{
    static TaskStatus_t tasks[12];
    UBaseType_t n;
    UBaseType_t i;

    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        printf("[lwip-port] scheduler not running\r\n");
        return;
    }
    n = uxTaskGetSystemState(tasks, (UBaseType_t) (sizeof(tasks) / sizeof(tasks[0])), NULL);
    printf("[lwip-port] %lu tasks (stack high-water mark, unit=word, larger=safer):\r\n", (unsigned long) n);
    for (i = 0U; i < n; i++) {
        printf("  %-12s prio=%lu state=%lu stack_hwm=%lu\r\n",
               tasks[i].pcTaskName, (unsigned long) tasks[i].uxCurrentPriority,
               (unsigned long) tasks[i].eCurrentState,
               (unsigned long) tasks[i].usStackHighWaterMark);
    }
}

uint32_t freertos_lwip_port_malloc_fail_count(void)
{
    return s_malloc_fail;
}

uint32_t freertos_lwip_port_stack_overflow_count(void)
{
    return s_stack_ovf;
}

void freertos_lwip_port_tick_hook(void)
{
    ++s_window_ticks;
    if (xTaskGetCurrentTaskHandle() == xTaskGetIdleTaskHandle()) {
        ++s_window_idle_ticks;
    }
}

/* ------------------------------------------------------------------ */
/* FreeRTOS 钩子（由本组件提供，应用不要再定义同名函数）                 */
/* ------------------------------------------------------------------ */
void vApplicationTickHook(void)
{
    freertos_lwip_port_tick_hook();
}

void vApplicationMallocFailedHook(void)
{
    ++s_malloc_fail;
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void) xTask;
    ++s_stack_ovf;
    taskDISABLE_INTERRUPTS();
    printf("[lwip-port] STACK OVERFLOW in task %s\r\n", (pcTaskName != NULL) ? pcTaskName : "?");
    for (;;) {
    }
}

#endif /* !NO_SYS */
