/**
 * @file  sys_arch.c
 * @brief lwIP system architecture — NO_SYS=1  bare-metal implementation
 *        Only sys_now() is required.
 */
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "stm32f4xx_hal.h"

/* sys_now() returns milliseconds since boot — use HAL_GetTick() */
u32_t sys_now(void)
{
    return (u32_t)HAL_GetTick();
}
