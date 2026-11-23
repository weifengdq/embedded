/*
 * app_xrce_platform.c —— Micro-XRCE-DDS Client 的时间钩子（裸机实现）
 *
 * 上游把 uxr_millis()/uxr_nanos() 放在 src/c/util/time.c，用 clock_gettime()/FreeRTOS tick。
 * 裸机 newlib 没有可用的 clock_gettime，因此 **不编译** 那份 time.c，改由本文件提供同名实现。
 * 时基用 RISC-V 的 mcycle CSR（HPM 的 mcycle 计数在 480 MHz CPU 时钟域，单调递增），
 * 只读 CSR，代价约十几个周期，可以放心在收发路径里调用。
 */
#include <stdint.h>
#include <stdbool.h>
#include "hpm_csr_drv.h"
#include "hpm_clock_drv.h"
#include <uxr/client/util/time.h>
#include "app_xrce.h"

/* CPU 主频（MHz）：只算一次；取不到时按板级配置 480 MHz 兜底。 */
static uint32_t s_mhz;

static uint32_t cpu_mhz(void)
{
    if (s_mhz == 0U) {
        uint32_t hz = clock_get_frequency(clock_cpu0);
        s_mhz = (hz != 0U) ? (hz / 1000000U) : 480U;
        if (s_mhz == 0U) {
            s_mhz = 480U;
        }
    }
    return s_mhz;
}

int64_t uxr_millis(void)
{
    uint64_t cyc = hpm_csr_get_core_mcycle();
    return (int64_t)(cyc / ((uint64_t)cpu_mhz() * 1000ULL));
}

int64_t uxr_nanos(void)
{
    uint64_t cyc = hpm_csr_get_core_mcycle();
    return (int64_t)((cyc * 1000ULL) / (uint64_t)cpu_mhz());
}

/* 给上层应用用的单调毫秒（与 uxr_millis 同一时基）。 */
uint32_t app_xrce_now_ms(void)
{
    return (uint32_t)uxr_millis();
}
