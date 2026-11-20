#ifndef NVIC_H
#define NVIC_H

#include "Std_Types.h"
#include "S32K312_SCB.h"
#include "S32K312_NVIC.h"

#ifdef __cplusplus
extern "C" {
#endif

void NVIC_SetPriorityGrouping(uint32 PriorityGroup);
void NVIC_EnableIRQ(uint8 IRQn);
void NVIC_DisableIRQ(uint8 IRQn);
void NVIC_SetPriority(uint8 IRQn, uint8 priority);

#ifdef __cplusplus
}
#endif

#endif /* NVIC_H */
