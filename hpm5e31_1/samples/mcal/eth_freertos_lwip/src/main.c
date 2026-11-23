/*
 * eth_freertos_lwip —— FreeRTOS 10.5.1 + lwIP 2.1.2（NO_SYS=0）+ AUTOSAR MCAL Eth
 *
 * 任务划分（优先级见 configMAX_PRIORITIES=8）：
 *   6  ethrx  收包任务：循环 Eth_GetRxBuffer -> netif->input(tcpip_input)，独占 Rx 方向
 *   5  tcpip  lwIP 核心线程（lwipopts TCPIP_THREAD_PRIO），Tx 方向（linkoutput）在它里面跑
 *   4  app    初始化 + 自检（跑完自己删掉）
 *   2  ethmon 每 5 s：CPU 占用、netif/PHY、吞吐计数、任务栈水位
 *   1  shell  letter-shell 命令轮询
 *   0  IDLE
 *
 * 串口命令：
 *   eth ...                  组件自带（见 components/app_lwip_eth）
 *   ethprio <n>              改收包任务优先级（0..7），配合 eth ping 测优先级对 RTT 的影响
 *   ethload <prio> <ms>      起一个 <prio> 优先级的忙任务压 CPU，<ms> 后自动退出
 *   ethload stop             提前停掉忙任务
 *   ethrt                    打印 CPU 占用 / 任务栈水位 / 网络统计
 *
 * 设计取舍（第 50 章"FreeRTOS 集成"节）：
 *   - 收包任务不做协议处理，只把 pbuf 丢给 tcpip_input，保证 Rx 环不被协议栈耗时拖住；
 *   - Tx 只在 tcpip 线程发生，Rx 只在 ethrx 任务发生，两个上下文对 Eth 驱动的访问由
 *     app_lwip_eth_hw_mcal.c 里的 FreeRTOS 互斥量串行化；
 *   - 无包时 ethrx 连续 64 次空转后 vTaskDelay(1)，有包时连续排空不睡（1 Gbit/s 下
 *     16 个 Rx 缓冲不到 1 ms 就会溢出）。
 */
#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include "board.h"
#include "FreeRTOS.h"
#include "task.h"

#include "Eth.h"
#include "Eth_PBcfg.h"
#include "EthTrcv.h"
#include "EthTrcv_PBcfg.h"

#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"

#include "app_lwip_eth.h"
#include "freertos_lwip_port.h"

#define ETH_CTRL_IDX        (0U)

#define PRIO_ETH_RX         (6U)
#define PRIO_APP            (4U)
#define PRIO_MON            (2U)
#define PRIO_SHELL          (1U)

#define STACK_ETH_RX        (2048U)
#define STACK_APP           (4096U)
#define STACK_MON           (2048U)
#define STACK_SHELL         (2048U)

static TaskHandle_t s_rx_handle;
static TaskHandle_t s_hog_handle;
static volatile bool s_hog_stop = true;
static volatile uint32_t s_hog_loops;

/* ------------------------------------------------------------------ */
/* 收包任务：把组件里的 app_lwip_eth_rx_task 包一层，只为拿到任务句柄 */
/* ------------------------------------------------------------------ */
static void eth_rx_task(void *arg)
{
    s_rx_handle = xTaskGetCurrentTaskHandle();
    app_lwip_eth_rx_task(arg);
}

/* ------------------------------------------------------------------ */
/* CPU 压力任务（优先级实验用）                                        */
/* ------------------------------------------------------------------ */
static void hog_task(void *arg)
{
    (void) arg;
    while (!s_hog_stop) {
        s_hog_loops++;
        __asm volatile ("nop");
    }
    s_hog_handle = NULL;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/* 统计任务                                                            */
/* ------------------------------------------------------------------ */
static void mon_task(void *arg)
{
    (void) arg;
    for (;;) {
        freertos_lwip_cpu_load_t load;
        uint32_t idle;

        vTaskDelay(pdMS_TO_TICKS(5000));
        idle = freertos_lwip_cpu_load_take(&load);
        printf("[rtos] cpu: idle=%lu.%lu%% busyload=%lu.%lu%% window=%lu ticks | heap_free=%u\r\n",
               (unsigned long) (idle / 10U), (unsigned long) (idle % 10U),
               (unsigned long) ((1000U - idle) / 10U), (unsigned long) ((1000U - idle) % 10U),
               (unsigned long) load.window_ticks, (unsigned) xPortGetFreeHeapSize());
        freertos_lwip_port_stats_print();
        app_lwip_eth_print_stats();
        freertos_lwip_port_print_task_stack();
    }
}

/* ------------------------------------------------------------------ */
/* shell 轮询任务（letter-shell 是非阻塞的，10 ms 轮一次足够）           */
/* ------------------------------------------------------------------ */
static void shell_task(void *arg)
{
    (void) arg;
    for (;;) {
        bsp_shell_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ------------------------------------------------------------------ */
/* 初始化任务                                                          */
/* ------------------------------------------------------------------ */
static void app_task(void *arg)
{
    Std_VersionInfoType ver;
    int rc;

    (void) arg;
    printf("\r\n=== mcal_eth_freertos_lwip: FreeRTOS %s + lwIP %s + MCAL Eth ===\r\n",
           tskKERNEL_VERSION_NUMBER, LWIP_VERSION_STRING);
    Eth_GetVersionInfo(&ver);
    printf("[init] Eth MCAL v%u.%u.%u\r\n", (unsigned) ver.sw_major_version,
           (unsigned) ver.sw_minor_version, (unsigned) ver.sw_patch_version);
#ifdef FREERTOS_LWIP_PORT_CUSTOM
    printf("[init] lwIP sys_arch: components/freertos_lwip_port (自写移植)\r\n");
#endif

    /* 1) MCAL Eth + EthTrcv（顺序：Eth_Init → EthTrcv_Init → SetControllerMode） */
    Eth_Init(&Eth_Config);
    EthTrcv_Init(&EthTrcv_Config);
    if (Eth_SetControllerMode(ETH_CTRL_IDX, ETH_MODE_ACTIVE) != E_OK) {
        printf("[init] Eth_SetControllerMode(ACTIVE) FAILED\r\n");
        vTaskDelete(NULL);
    }
    /* 2) lwIP tcpip 线程（必须在 netif_add 之前） */
    tcpip_init(NULL, NULL);
    /* 3) netif 适配层 */
    rc = app_lwip_eth_init(NULL);
    printf("[init] app_lwip_eth_init rc=%d (0=link up, 1=link down)\r\n", rc);

    /* 4) 收包任务（优先级 6，高于 tcpip 的 5：进中断/收包优先） */
    xTaskCreate(eth_rx_task, "ethrx", STACK_ETH_RX, NULL, PRIO_ETH_RX, &s_rx_handle);

    /* 5) 上电自证：ping 网关 5 次 */
    if (rc == 0) {
        app_lwip_eth_ping_result_t pr;
        (void) app_lwip_eth_ping(APP_LWIP_ETH_DEFAULT_GW, 5U, 200U, &pr);
        printf("[selftest] ping gw: %lu/%lu rtt min/avg/max=%lu/%lu/%lu us jitter=%lu us\r\n",
               (unsigned long) pr.recv, (unsigned long) pr.sent,
               (unsigned long) pr.rtt_min_us, (unsigned long) pr.rtt_avg_us,
               (unsigned long) pr.rtt_max_us, (unsigned long) pr.rtt_jitter_us);
    }
    printf("[run] cmds: eth | eth ping <ip> [n] [ms] | eth iserver [tcp|udp] | eth iclient <ip> |\r\n"
           "            ethprio <0..7> | ethload <prio> <ms> | ethload stop | ethrt\r\n");
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/* 调试命令（只在本例程里有，组件里不放）                               */
/* ------------------------------------------------------------------ */
/* 延时命令：脚本化测试用 */
static void cmd_sleep(int argc, char *argv[])
{
    uint32_t ms = 1000;
    if (argc >= 2) {
        (void) bsp_parse_u32(argv[1], &ms);
    }
    printf("[sys] sleep %lu ms\r\n", (unsigned long) ms);
    vTaskDelay(pdMS_TO_TICKS(ms));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sleep, cmd_sleep, delay N ms: sleep <ms>);

static void cmd_ethprio(int argc, char *argv[])
{
    uint32_t prio = PRIO_ETH_RX;

    if (argc < 2) {
        printf("usage: ethprio <0..%u>\r\n", (unsigned) (configMAX_PRIORITIES - 1));
        return;
    }
    (void) bsp_parse_u32(argv[1], &prio);
    if ((prio >= configMAX_PRIORITIES) || (s_rx_handle == NULL)) {
        printf("[rtos] bad prio or rx task not ready\r\n");
        return;
    }
    vTaskPrioritySet(s_rx_handle, (UBaseType_t) prio);
    printf("[rtos] ethrx priority -> %lu\r\n", (unsigned long) prio);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ethprio, cmd_ethprio, set eth rx task priority: ethprio <0..7>);

static void cmd_ethload(int argc, char *argv[])
{
    uint32_t prio;
    uint32_t ms;

    if (argc < 2) {
        printf("usage: ethload <prio> <ms> | ethload stop\r\n");
        return;
    }
    if (strcmp(argv[1], "stop") == 0) {
        s_hog_stop = true;
        printf("[rtos] load task stopping\r\n");
        return;
    }
    prio = PRIO_SHELL;
    ms = 10000U;
    (void) bsp_parse_u32(argv[1], &prio);
    if (argc >= 3) {
        (void) bsp_parse_u32(argv[2], &ms);
    }
    if (prio >= configMAX_PRIORITIES) {
        printf("[rtos] bad prio\r\n");
        return;
    }
    s_hog_stop = true;
    vTaskDelay(pdMS_TO_TICKS(20));
    s_hog_stop = false;
    s_hog_loops = 0U;
    if (xTaskCreate(hog_task, "hog", 1024, NULL, (UBaseType_t) prio, &s_hog_handle) != pdPASS) {
        printf("[rtos] load task create FAILED\r\n");
        return;
    }
    printf("[rtos] load task prio=%lu, will stop in %lu ms (ethload stop 可提前停)\r\n",
           (unsigned long) prio, (unsigned long) ms);
    vTaskDelay(pdMS_TO_TICKS(ms));
    s_hog_stop = true;
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ethload, cmd_ethload, CPU load task: ethload <prio> <ms> | ethload stop);

static void cmd_ethrt(int argc, char *argv[])
{
    freertos_lwip_cpu_load_t load;
    uint32_t idle;

    (void) argc;
    (void) argv;
    idle = freertos_lwip_cpu_load_take(&load);
    printf("[rtos] cpu: idle=%lu.%lu%% | heap_free=%u bytes | malloc_fail=%lu stack_ovf=%lu\r\n",
           (unsigned long) (idle / 10U), (unsigned long) (idle % 10U),
           (unsigned) xPortGetFreeHeapSize(),
           (unsigned long) freertos_lwip_port_malloc_fail_count(),
           (unsigned long) freertos_lwip_port_stack_overflow_count());
    freertos_lwip_port_stats_print();
    app_lwip_eth_print_stats();
    freertos_lwip_port_print_task_stack();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ethrt, cmd_ethrt, print RTOS cpu load / stack watermarks / net stats);

int main(void)
{
    bsp_init();
    printf("[boot] FreeRTOS + lwIP + MCAL Eth bring-up\r\n");

    xTaskCreate(app_task, "app", STACK_APP, NULL, PRIO_APP, NULL);
    xTaskCreate(mon_task, "ethmon", STACK_MON, NULL, PRIO_MON, NULL);
    xTaskCreate(shell_task, "shell", STACK_SHELL, NULL, PRIO_SHELL, NULL);
    vTaskStartScheduler();
    for (;;) {
    }
    return 0;
}
