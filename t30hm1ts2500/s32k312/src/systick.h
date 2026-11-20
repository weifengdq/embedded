#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

void SysTick_Init(uint32_t coreClockHz);
uint32_t SysTick_GetMs(void);
void SysTick_DelayMs(uint32_t ms);
void SysTick_Handler(void);

#endif
