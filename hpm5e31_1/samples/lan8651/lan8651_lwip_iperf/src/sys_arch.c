/*
 * sys_arch.c - minimal NO_SYS=1 sys_now() for the bare-metal HPM SDK port.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "bsp.h"

u32_t sys_now(void)
{
    return (u32_t) bsp_uptime_ms();
}
