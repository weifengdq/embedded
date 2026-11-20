#include <stdint.h>

/* Provide stub implementations for SchM exclusive area MSR reading.
   RTD's SchM expects these to be defined as assembly functions that read PRIMASK. */

uint32_t Mcu_schm_read_msr(void)
{
    uint32_t ret;
    __asm volatile("mrs %0, primask" : "=r"(ret) :: "memory");
    return ret;
}
uint32_t Uart_schm_read_msr(void)
{
    uint32_t ret;
    __asm volatile("mrs %0, primask" : "=r"(ret) :: "memory");
    return ret;
}
uint32_t Dio_schm_read_msr(void)
{
    uint32_t ret;
    __asm volatile("mrs %0, primask" : "=r"(ret) :: "memory");
    return ret;
}
uint32_t Port_schm_read_msr(void)
{
    uint32_t ret;
    __asm volatile("mrs %0, primask" : "=r"(ret) :: "memory");
    return ret;
}
uint32_t BaseNXP_schm_read_msr(void)
{
    uint32_t ret;
    __asm volatile("mrs %0, primask" : "=r"(ret) :: "memory");
    return ret;
}

/* Also handle generic SchM read */
uint32_t SchM_schm_read_msr(void)
{
    uint32_t ret;
    __asm volatile("mrs %0, primask" : "=r"(ret) :: "memory");
    return ret;
}
