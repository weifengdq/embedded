/* Minimal system.h for S32K312 */
#ifndef SYSTEM_H
#define SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Std_Types.h"

void SystemInit(void);
void default_interrupt_routine(void);

#ifdef MCAL_ENABLE_USER_MODE_SUPPORT
uint32 Sys_GoToSupervisor(void);
uint32 Sys_GoToUser(void);
uint32 Sys_GoToUser_Return(uint32 sw, uint32 val);
void Sys_SuspendInterrupts(void);
void Sys_ResumeInterrupts(void);
#endif

uint8 Sys_GetCoreID(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_H */
