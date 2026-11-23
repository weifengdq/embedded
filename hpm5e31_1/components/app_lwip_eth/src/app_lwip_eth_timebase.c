/*
 * app_lwip_eth_timebase.c - 裸机（NO_SYS=1）的 lwIP 时基
 *
 * lwIP 在 NO_SYS=1 时只要求移植层提供 sys_now()（毫秒）。SDK 的
 * samples/lwip/ports/baremetal/arch/sys_arch.c 里那份实现绑着 SDK enet 样例的
 * common.h（板级 PHY/描述符一堆依赖），本组件不复制它，改用 8 行自己的实现：
 *   1 ms 硬件定时器（board_timer_create）累加一个 volatile 毫秒计数器。
 *
 * 注意：RTOS 版本（NO_SYS=0）不会编这个文件，sys_now() 由
 * components/freertos_lwip_port/src/sys_arch.c 用 xTaskGetTickCount() 提供。
 *
 * 作者：eth-stack（task-2）
 */
#include "lwip/opt.h"

#if NO_SYS

#include <stdint.h>

#include "board.h"
#include "lwip/sys.h"

static volatile uint32_t s_ms_tick;

static void app_lwip_eth_1ms_isr(void)
{
    s_ms_tick++;
}

void app_lwip_eth_timebase_start(void)
{
    s_ms_tick = 0U;
    board_timer_create(1U, app_lwip_eth_1ms_isr);
}

u32_t sys_now(void)
{
    return s_ms_tick;
}

#endif /* NO_SYS */
