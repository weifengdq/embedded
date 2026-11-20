#include "systick.h"
#include "S32K312_SYSTICK.h"
#include "S32K312_SCB.h"
#include <stdint.h>

static volatile uint32_t g_ms = 0;

void SysTick_Init(uint32_t coreClockHz)
{
    uint32_t reload = (coreClockHz / 1000U) - 1U;
    if (reload > 0xFFFFFFU) reload = 0xFFFFFFU;
    S32_SysTick->RVR = reload;
    S32_SysTick->CVR = 0;
    S32_SysTick->CSRr = S32_SysTick_CSR_ENABLE_MASK | S32_SysTick_CSR_TICKINT_MASK | S32_SysTick_CSR_CLKSOURCE_MASK;
    /* Set SysTick priority */
    S32_SCB->SHPR3 = (S32_SCB->SHPR3 & ~0xFF000000UL) | (0xF0UL << 24);
}

uint32_t SysTick_GetMs(void)
{
    return g_ms;
}

void SysTick_DelayMs(uint32_t ms)
{
    uint32_t start = g_ms;
    while ((g_ms - start) < ms) {
        __asm volatile("nop");
    }
}

void SysTick_Handler(void)
{
    g_ms++;
}
