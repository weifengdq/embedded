/* Minimal core_specific.h for S32K312 */
#ifndef CORE_SPECIFIC_H
#define CORE_SPECIFIC_H

#include "Std_Types.h"
#include "S32K312_SCB.h"
#include "S32K312_MPU.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Provide minimal definitions used by system.c */
#define ASM_KEYWORD(x) __asm volatile(x)
#define ASMV_KEYWORD(x) __asm volatile(x)
#define INLINE inline

#ifdef __cplusplus
}
#endif

#endif /* CORE_SPECIFIC_H */
