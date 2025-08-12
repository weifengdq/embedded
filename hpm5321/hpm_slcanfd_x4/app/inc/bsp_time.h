#ifndef BSP_TIME_H
#define BSP_TIME_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "hpm_csr_drv.h"

#define TICKS_PER_SECOND 480000000UL
#define TICKS_PER_MS (TICKS_PER_SECOND / 1000)
#define TICKS_PER_US (TICKS_PER_SECOND / 1000000)

#if defined(__cplusplus)
extern "C" {
#endif

// 基础时间函数
inline uint64_t bsp_get_ticks(void)
{
    return hpm_csr_get_core_cycle();
}

inline uint64_t bsp_get_ns(void)
{
    return (bsp_get_ticks() * 1000000000UL) / TICKS_PER_SECOND;
}

inline uint64_t bsp_get_us(void)
{
    return bsp_get_ticks() / TICKS_PER_US;
}

inline uint64_t bsp_get_ms(void)
{
    return bsp_get_ticks() / TICKS_PER_MS;
}

inline void bsp_delay_ticks(uint64_t ticks)
{
    uint64_t start = bsp_get_ticks();
    while ((bsp_get_ticks() - start) < ticks) {
        // Wait
    }
}

inline void bsp_delay_us(uint32_t us)
{
    bsp_delay_ticks((uint64_t) us * TICKS_PER_US);
}

inline void bsp_delay_ms(uint32_t ms)
{
    bsp_delay_ticks((uint64_t) ms * TICKS_PER_MS);
}

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* BSP_TIME_H */