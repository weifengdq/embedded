/*==================================================================================================
* Project : RTD AUTOSAR 4.7
* Platform : CORTEXM
* Peripheral : S32K3XX
* Dependencies : none
*
* Autosar Version : 4.7.0
* Autosar Revision : ASR_REL_4_7_REV_0000
* Autosar Conf.Variant :
* SW Version : 6.0.0
* Build Version : S32K3_RTD_6_0_0_D2506_ASR_REL_4_7_REV_0000_20250610
*
* Copyright 2020 - 2025 NXP
*
* NXP Confidential and Proprietary. This software is owned or controlled by NXP and may only be 
*   used strictly in accordance with the applicable license terms.  By expressly 
*   accepting such terms or by downloading, installing, activating and/or otherwise 
*   using the software, you are agreeing that you have read, and that you agree to 
*   comply with and are bound by, such license terms.  If you do not agree to be 
*   bound by the applicable license terms, then you may not retain, install,
*   activate or otherwise use the software.
==================================================================================================*/

/**
*   @file OsIf_Timer_System
*   @version 6.0.0
*
*   @brief   BaseNXP - Implements the driver functionality.
*   @details Implements the APIs for System timer.
*
*   @addtogroup osif_drv
*   @{
*/

#ifdef __cplusplus
extern "C"{
#endif

/*==================================================================================================
*                                          INCLUDE FILES
* 1) system and project includes
* 2) needed interfaces from external units
* 3) internal and external interfaces from this unit
==================================================================================================*/
#include "OsIf.h"
#include "OsIf_Cfg.h"
#include "OsIf_Cfg_TypesDef.h"
#include "OsIf_Timer_System.h"

#if (OSIF_USE_SYSTEM_TIMER == STD_ON)

#if defined(USING_OS_AUTOSAROS)
#include "Os.h"
#elif defined(USING_OS_FREERTOS)
#include "FreeRTOS.h"
#elif defined(USING_OS_ZEPHYR)
#include <zephyr/kernel.h>
#else
    /* Baremetal, make sure USING_OS_BAREMETAL is defined */
#ifndef USING_OS_BAREMETAL
#define USING_OS_BAREMETAL
#endif /* ifndef USING_OS_BAREMETAL */
#endif /* defined(USING_OS_AUTOSAROS) */

/* Timer specific includes to define:
 - OsIf_Timer_System_Internal_Init
 - OsIf_Timer_System_Internal_GetCounter
 - OsIf_Timer_System_Internal_GetElapsed
 */
#if defined(OSIF_USE_SYSTICK)
#if (OSIF_USE_SYSTICK == STD_ON)
#include "OsIf_Timer_System_Internal_Systick.h"
#endif /* OSIF_USE_SYSTICK == STD_ON */
#endif /* defined(OSIF_USE_SYSTICK) */

#if defined(OSIF_USE_GENERICTIMER)
#if (OSIF_USE_GENERICTIMER == STD_ON)
#include "OsIf_Timer_System_Internal_GenericTimer.h"
#endif /* OSIF_USE_GENERICTIMER == STD_ON */
#endif /* defined(OSIF_USE_GENERICTIMER) */

/*==================================================================================================
*                                 SOURCE FILE VERSION INFORMATION
==================================================================================================*/
#define OSIF_TIMER_SYSTEM_VENDOR_ID_C                    43
#define OSIF_TIMER_SYSTEM_AR_RELEASE_MAJOR_VERSION_C     4
#define OSIF_TIMER_SYSTEM_AR_RELEASE_MINOR_VERSION_C     7
#define OSIF_TIMER_SYSTEM_AR_RELEASE_REVISION_VERSION_C  0
#define OSIF_TIMER_SYSTEM_SW_MAJOR_VERSION_C             6
#define OSIF_TIMER_SYSTEM_SW_MINOR_VERSION_C             0
#define OSIF_TIMER_SYSTEM_SW_PATCH_VERSION_C             0

/*==================================================================================================
*                                       FILE VERSION CHECKS
==================================================================================================*/
/* Check if OsIf_Timer_System.c file and OsIf.h file are of the same vendor */
#if (OSIF_TIMER_SYSTEM_VENDOR_ID_C != OSIF_VENDOR_ID)
    #error "OsIf_Timer_System.c and OsIf.h have different vendor ids"
#endif
/* Check if OsIf_Timer_System.c file and OsIf.h file are of the same Autosar version */
#if ((OSIF_TIMER_SYSTEM_AR_RELEASE_MAJOR_VERSION_C    != OSIF_AR_RELEASE_MAJOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_AR_RELEASE_MINOR_VERSION_C    != OSIF_AR_RELEASE_MINOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_AR_RELEASE_REVISION_VERSION_C != OSIF_AR_RELEASE_REVISION_VERSION))
    #error "AUTOSAR Version Numbers of OsIf_Timer_System.c and OsIf.h are different"
#endif
/* Check if OsIf_Timer_System.c file and OsIf.h file are of the same Software version */
#if ((OSIF_TIMER_SYSTEM_SW_MAJOR_VERSION_C != OSIF_SW_MAJOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_SW_MINOR_VERSION_C != OSIF_SW_MINOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_SW_PATCH_VERSION_C != OSIF_SW_PATCH_VERSION) \
    )
    #error "Software Version Numbers of OsIf_Timer_System.c and OsIf.h are different"
#endif

/* Check if OsIf_Timer_System.c file and OsIf_Cfg.h file are of the same vendor */
#if (OSIF_TIMER_SYSTEM_VENDOR_ID_C != OSIF_CFG_VENDOR_ID)
    #error "OsIf_Timer_System.c and OsIf_Cfg.h have different vendor ids"
#endif
/* Check if OsIf_Timer_System.c file and OsIf_Cfg.h file are of the same Autosar version */
#if ((OSIF_TIMER_SYSTEM_AR_RELEASE_MAJOR_VERSION_C    != OSIF_CFG_AR_RELEASE_MAJOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_AR_RELEASE_MINOR_VERSION_C    != OSIF_CFG_AR_RELEASE_MINOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_AR_RELEASE_REVISION_VERSION_C != OSIF_CFG_AR_RELEASE_REVISION_VERSION))
    #error "AUTOSAR Version Numbers of OsIf_Timer_System.c and OsIf_Cfg.h are different"
#endif
/* Check if OsIf_Timer_System.c file and OsIf_Cfg.h file are of the same Software version */
#if ((OSIF_TIMER_SYSTEM_SW_MAJOR_VERSION_C != OSIF_CFG_SW_MAJOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_SW_MINOR_VERSION_C != OSIF_CFG_SW_MINOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_SW_PATCH_VERSION_C != OSIF_CFG_SW_PATCH_VERSION) \
    )
    #error "Software Version Numbers of OsIf_Timer_System.c and OsIf_Cfg.h are different"
#endif

/* Check if OsIf_Timer_System.c file and OsIf_Cfg_TypesDef.h file are of the same vendor */
#if (OSIF_TIMER_SYSTEM_VENDOR_ID_C != OSIF_CFG_TYPESDEF_VENDOR_ID)
    #error "OsIf_Timer_System.c and OsIf_Cfg_TypesDef.h have different vendor ids"
#endif
/* Check if OsIf_Timer_System.c file and OsIf_Cfg_TypesDef.h file are of the same Autosar version */
#if ((OSIF_TIMER_SYSTEM_AR_RELEASE_MAJOR_VERSION_C    != OSIF_CFG_TYPESDEF_AR_RELEASE_MAJOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_AR_RELEASE_MINOR_VERSION_C    != OSIF_CFG_TYPESDEF_AR_RELEASE_MINOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_AR_RELEASE_REVISION_VERSION_C != OSIF_CFG_TYPESDEF_AR_RELEASE_REVISION_VERSION))
    #error "AUTOSAR Version Numbers of OsIf_Timer_System.c and OsIf_Cfg_TypesDef.h are different"
#endif
/* Check if OsIf_Timer_System.c file and OsIf_Cfg_TypesDef.h file are of the same Software version */
#if ((OSIF_TIMER_SYSTEM_SW_MAJOR_VERSION_C != OSIF_CFG_TYPESDEF_SW_MAJOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_SW_MINOR_VERSION_C != OSIF_CFG_TYPESDEF_SW_MINOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_SW_PATCH_VERSION_C != OSIF_CFG_TYPESDEF_SW_PATCH_VERSION) \
    )
    #error "Software Version Numbers of OsIf_Timer_System.c and OsIf_Cfg_TypesDef.h are different"
#endif

/* Check if OsIf_Timer_System.c file and OsIf_Timer_System.h file are of the same vendor */
#if (OSIF_TIMER_SYSTEM_VENDOR_ID_C != OSIF_TIMER_SYSTEM_VENDOR_ID)
    #error "OsIf_Timer_System.c and OsIf_Timer_System.h have different vendor ids"
#endif
/* Check if OsIf_Timer_System.c file and OsIf_Timer_System.h file are of the same Autosar version */
#if ((OSIF_TIMER_SYSTEM_AR_RELEASE_MAJOR_VERSION_C    != OSIF_TIMER_SYSTEM_AR_RELEASE_MAJOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_AR_RELEASE_MINOR_VERSION_C    != OSIF_TIMER_SYSTEM_AR_RELEASE_MINOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_AR_RELEASE_REVISION_VERSION_C != OSIF_TIMER_SYSTEM_AR_RELEASE_REVISION_VERSION))
    #error "AUTOSAR Version Numbers of OsIf_Timer_System.c and OsIf_Timer_System.h are different"
#endif
/* Check if OsIf_Timer_System.c file and OsIf_Timer_System.h file are of the same Software version */
#if ((OSIF_TIMER_SYSTEM_SW_MAJOR_VERSION_C != OSIF_TIMER_SYSTEM_SW_MAJOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_SW_MINOR_VERSION_C != OSIF_TIMER_SYSTEM_SW_MINOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_SW_PATCH_VERSION_C != OSIF_TIMER_SYSTEM_SW_PATCH_VERSION) \
    )
    #error "Software Version Numbers of OsIf_Timer_System.c and OsIf_Timer_System.h are different"
#endif

#if defined(OSIF_USE_SYSTICK)
#if (OSIF_USE_SYSTICK == STD_ON)
/* Check if OsIf_Timer_System.c file and OsIf_Timer_System_Internal_Systick.h file are of the same vendor */
#if (OSIF_TIMER_SYSTEM_VENDOR_ID_C != OSIF_TIMER_SYSTEM_INTERNAL_SYSTICK_VENDOR_ID)
    #error "OsIf_Timer_System.c and OsIf_Timer_System_Internal_Systick.h have different vendor ids"
#endif
/* Check if OsIf_Timer_System.c file and OsIf_Timer_System_Internal_Systick.h file are of the same Autosar version */
#if ((OSIF_TIMER_SYSTEM_AR_RELEASE_MAJOR_VERSION_C    != OSIF_TIMER_SYSTEM_INTERNAL_SYSTICK_AR_RELEASE_MAJOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_AR_RELEASE_MINOR_VERSION_C    != OSIF_TIMER_SYSTEM_INTERNAL_SYSTICK_AR_RELEASE_MINOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_AR_RELEASE_REVISION_VERSION_C != OSIF_TIMER_SYSTEM_INTERNAL_SYSTICK_AR_RELEASE_REVISION_VERSION))
    #error "AUTOSAR Version Numbers of OsIf_Timer_System.c and OsIf_Timer_System_Internal_Systick.h are different"
#endif
/* Check if OsIf_Timer_System.c file and OsIf_Timer_System_Internal_Systick.h file are of the same Software version */
#if ((OSIF_TIMER_SYSTEM_SW_MAJOR_VERSION_C != OSIF_TIMER_SYSTEM_INTERNAL_SYSTICK_SW_MAJOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_SW_MINOR_VERSION_C != OSIF_TIMER_SYSTEM_INTERNAL_SYSTICK_SW_MINOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_SW_PATCH_VERSION_C != OSIF_TIMER_SYSTEM_INTERNAL_SYSTICK_SW_PATCH_VERSION) \
    )
    #error "Software Version Numbers of OsIf_Timer_System.c and OsIf_Timer_System_Internal_Systick.h are different"
#endif
#endif /* OSIF_USE_SYSTICK == STD_ON */
#endif /* defined(OSIF_USE_SYSTICK) */

#if defined(OSIF_USE_GENERICTIMER)
#if OSIF_USE_GENERICTIMER == STD_ON
/* Check if OsIf_Timer_System.c file and OsIf_Timer_System_Internal_GenericTimer.h file are of the same vendor */
#if (OSIF_TIMER_SYSTEM_VENDOR_ID_C != OSIF_TIMER_SYS_INTER_GENERICTIMER_VENDOR_ID)
    #error "OsIf_Timer_System.c and OsIf_Timer_System_Internal_GenericTimer.h have different vendor ids"
#endif
/* Check if OsIf_Timer_System.c file and OsIf_Timer_System_Internal_GenericTimer.h file are of the same Autosar version */
#if ((OSIF_TIMER_SYSTEM_AR_RELEASE_MAJOR_VERSION_C    != OSIF_TIMER_SYS_INTER_GENERICTIMER_AR_RELEASE_MAJOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_AR_RELEASE_MINOR_VERSION_C    != OSIF_TIMER_SYS_INTER_GENERICTIMER_AR_RELEASE_MINOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_AR_RELEASE_REVISION_VERSION_C != OSIF_TIMER_SYS_INTER_GENERICTIMER_AR_RELEASE_REVISION_VERSION))
    #error "AUTOSAR Version Numbers of OsIf_Timer_System.c and OsIf_Timer_System_Internal_GenericTimer.h are different"
#endif
/* Check if OsIf_Timer_System.c file and OsIf_Timer_System_Internal_GenericTimer.h file are of the same Software version */
#if ((OSIF_TIMER_SYSTEM_SW_MAJOR_VERSION_C != OSIF_TIMER_SYS_INTER_GENERICTIMER_SW_MAJOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_SW_MINOR_VERSION_C != OSIF_TIMER_SYS_INTER_GENERICTIMER_SW_MINOR_VERSION) || \
     (OSIF_TIMER_SYSTEM_SW_PATCH_VERSION_C != OSIF_TIMER_SYS_INTER_GENERICTIMER_SW_PATCH_VERSION) \
    )
    #error "Software Version Numbers of OsIf_Timer_System.c and OsIf_Timer_System_Internal_GenericTimer.h are different"
#endif
#endif /* OSIF_USE_GENERICTIMER == STD_ON */
#endif /* defined(OSIF_USE_GENERICTIMER) */

#if defined(USING_OS_AUTOSAROS)
/* Check if OsIf_Timer_System.c file and Os.h file are of the same Autosar version */
#ifndef DISABLE_MCAL_INTERMODULE_ASR_CHECK
    #if ((OSIF_TIMER_SYSTEM_AR_RELEASE_MAJOR_VERSION_C != OS_AR_RELEASE_MAJOR_VERSION) || \
         (OSIF_TIMER_SYSTEM_AR_RELEASE_MINOR_VERSION_C != OS_AR_RELEASE_MINOR_VERSION))
        #error "AutoSar Version Numbers of OsIf_Timer_System.c and Os.h are different"
    #endif
#endif /* DISABLE_MCAL_INTERMODULE_ASR_CHECK */
#endif /* defined(USING_OS_AUTOSAROS) */

/*==================================================================================================
*                           LOCAL TYPEDEFS (STRUCTURES, UNIONS, ENUMS)
==================================================================================================*/

/*==================================================================================================
*                                          LOCAL MACROS
==================================================================================================*/
#if (STD_ON == OSIF_ENABLE_USER_MODE_SUPPORT)
    #define Trusted_OsIf_Timer_System_Internal_Init(Freq)               OsIf_Trusted_Call1param(OsIf_Timer_System_Internal_Init, (Freq))
    #define Trusted_OsIf_Timer_System_Internal_GetCounter()             OsIf_Trusted_Call_Return(OsIf_Timer_System_Internal_GetCounter)
    #define Trusted_OsIf_Timer_System_Internal_GetElapsed(CurrentRef)   OsIf_Trusted_Call_Return1param(OsIf_Timer_System_Internal_GetElapsed, (CurrentRef))
    #define Trusted_k_cycle_get_32()                                    OsIf_Trusted_Call_Return(k_cycle_get_32)
#else
    #define Trusted_OsIf_Timer_System_Internal_Init(Freq)               OsIf_Timer_System_Internal_Init(Freq)
    #define Trusted_OsIf_Timer_System_Internal_GetCounter()             OsIf_Timer_System_Internal_GetCounter()
    #define Trusted_OsIf_Timer_System_Internal_GetElapsed(CurrentRef)   OsIf_Timer_System_Internal_GetElapsed(CurrentRef)
    #define Trusted_k_cycle_get_32()                                    k_cycle_get_32()
#endif

#if STD_ON == OSIF_ENABLE_MULTICORE_SUPPORT
    #define OsIfCoreID()        (OsIf_GetCoreID())
#else
    #define OsIfCoreID()        (0U)
#endif
/*==================================================================================================
*                                         LOCAL CONSTANTS
==================================================================================================*/

/*==================================================================================================
*                                         LOCAL VARIABLES
==================================================================================================*/
#if (STD_ON == OSIF_DEV_ERROR_DETECT)
#define BASENXP_START_SEC_VAR_CLEARED_BOOLEAN
#include "BaseNXP_MemMap.h"

static boolean OsIf_abMdlInit[OSIF_MAX_COREIDX_SUPPORTED];

#define BASENXP_STOP_SEC_VAR_CLEARED_BOOLEAN
#include "BaseNXP_MemMap.h"
#endif /* (STD_ON == OSIF_DEV_ERROR_DETECT) */

#if (defined(USING_OS_AUTOSAROS) || (STD_ON == OSIF_DEV_ERROR_DETECT))
#define BASENXP_START_SEC_VAR_CLEARED_UNSPECIFIED
#include "BaseNXP_MemMap.h"

static const OsIf_ConfigType *OsIf_apxInternalCfg[OSIF_MAX_COREIDX_SUPPORTED];

#define BASENXP_STOP_SEC_VAR_CLEARED_UNSPECIFIED
#include "BaseNXP_MemMap.h"
#endif /* (defined(USING_OS_AUTOSAROS) || (STD_ON == OSIF_DEV_ERROR_DETECT)) */

#define BASENXP_START_SEC_VAR_CLEARED_32
#include "BaseNXP_MemMap.h"

static uint32 OsIf_au32InternalFrequencies[OSIF_MAX_COREIDX_SUPPORTED];

#define BASENXP_STOP_SEC_VAR_CLEARED_32
#include "BaseNXP_MemMap.h"
/*==================================================================================================
*                                        GLOBAL CONSTANTS
==================================================================================================*/

/*==================================================================================================
*                                        GLOBAL VARIABLES
==================================================================================================*/
#define BASENXP_START_SEC_CONFIG_DATA_UNSPECIFIED
#include "BaseNXP_MemMap.h"

extern const OsIf_ConfigType *const OsIf_apxPredefinedConfig[OSIF_MAX_COREIDX_SUPPORTED];

#define BASENXP_STOP_SEC_CONFIG_DATA_UNSPECIFIED
#include "BaseNXP_MemMap.h"
/*==================================================================================================
*                                    LOCAL FUNCTION PROTOTYPES
==================================================================================================*/

/*==================================================================================================
*                                         LOCAL FUNCTIONS
==================================================================================================*/

/*==================================================================================================
*                                        GLOBAL FUNCTIONS
==================================================================================================*/
#define BASENXP_START_SEC_CODE
#include "BaseNXP_MemMap.h"

/*FUNCTION**********************************************************************
 *
 * Function Name : OsIf_Timer_System_Init.
 * Description   : Initialize system timer.
 * 
 *END**************************************************************************/
void OsIf_Timer_System_Init(void)
{
    uint32 u32CoreId = OsIfCoreID();

#if (STD_ON == OSIF_DEV_ERROR_DETECT)
#if (STD_ON == OSIF_ENABLE_MULTICORE_SUPPORT)
    if ((OSIF_MAX_COREIDX_SUPPORTED <= u32CoreId) || (NULL_PTR == OsIf_apxPredefinedConfig[u32CoreId]))
#else
    if (NULL_PTR == OsIf_apxPredefinedConfig[u32CoreId])
#endif /* (STD_ON == OSIF_ENABLE_MULTICORE_SUPPORT) */
    {
    #if defined(USING_OS_AUTOSAROS)
        (void)Det_ReportError(OSIF_MODULE_ID, OSIF_DRIVER_INSTANCE, OSIF_SID_INIT, OSIF_E_INV_CORE_IDX);
    #else
        OSIF_DEV_ASSERT(FALSE);
    #endif /* defined(USING_OS_AUTOSAROS) */
    }
    else
    {
        OsIf_abMdlInit[u32CoreId] = TRUE;
#endif /* (STD_ON == OSIF_DEV_ERROR_DETECT) */

#if (defined(USING_OS_AUTOSAROS) || (STD_ON == OSIF_DEV_ERROR_DETECT))
    OsIf_apxInternalCfg[u32CoreId] = OsIf_apxPredefinedConfig[u32CoreId];
#endif /* (defined(USING_OS_AUTOSAROS) || (STD_ON == OSIF_DEV_ERROR_DETECT)) */
#if (!defined(USING_OS_FREERTOS) && !defined(USING_OS_ZEPHYR))
    OsIf_au32InternalFrequencies[u32CoreId] = OsIf_apxPredefinedConfig[u32CoreId]->counterFrequency;
#endif /* (!defined(USING_OS_FREERTOS) && !defined(USING_OS_ZEPHYR)) */

#if defined(USING_OS_FREERTOS)
    /* FreeRTOS */
    OsIf_au32InternalFrequencies[u32CoreId] = configCPU_CLOCK_HZ;
#elif defined(USING_OS_ZEPHYR)
    /* ZephyrOS */
    OsIf_au32InternalFrequencies[u32CoreId] = sys_clock_hw_cycles_per_sec();
#elif defined(USING_OS_BAREMETAL)
    /* Baremetal */
    Trusted_OsIf_Timer_System_Internal_Init(OsIf_au32InternalFrequencies[u32CoreId]);
#endif
#if (STD_ON == OSIF_DEV_ERROR_DETECT)
    }
#endif /* (STD_ON == OSIF_DEV_ERROR_DETECT) */
}

/*FUNCTION**********************************************************************
 *
 * Function Name : OsIf_Timer_System_GetCounter.
 * Description   : Get counter value from system timer.
 * 
 *END**************************************************************************/
uint32 OsIf_Timer_System_GetCounter(void)
{
    uint32 u32Counter = 0U;
    uint32 u32CoreId = OsIfCoreID();
#if defined(USING_OS_AUTOSAROS)
    StatusType Status;
    TickType value;
#endif /* defined(USING_OS_AUTOSAROS) */

#if (STD_ON == OSIF_DEV_ERROR_DETECT)
    if (TRUE != OsIf_abMdlInit[u32CoreId])
    {
    #if defined(USING_OS_AUTOSAROS)
        (void)Det_ReportError(OSIF_MODULE_ID, OSIF_DRIVER_INSTANCE, OSIF_SID_GETCOUNTER, OSIF_E_UNINIT);
    #else
        OSIF_DEV_ASSERT(FALSE);
    #endif /* defined(USING_OS_AUTOSAROS) */
    }
#if (STD_ON == OSIF_ENABLE_MULTICORE_SUPPORT)
    else if ((OSIF_MAX_COREIDX_SUPPORTED <= u32CoreId) || (NULL_PTR == OsIf_apxInternalCfg[u32CoreId]))
#else
    else if (NULL_PTR == OsIf_apxInternalCfg[u32CoreId])
#endif /* (STD_ON == OSIF_ENABLE_MULTICORE_SUPPORT) */
    {
    #if defined(USING_OS_AUTOSAROS)
        (void)Det_ReportError(OSIF_MODULE_ID, OSIF_DRIVER_INSTANCE, OSIF_SID_GETCOUNTER, OSIF_E_INV_CORE_IDX);
    #else
        OSIF_DEV_ASSERT(FALSE);
    #endif /* defined(USING_OS_AUTOSAROS) */
    }
    else
#endif /* (STD_ON == OSIF_DEV_ERROR_DETECT) */
    {
#if defined(USING_OS_AUTOSAROS)
        Status = GetCounterValue(OsIf_apxInternalCfg[u32CoreId]->counterId, &value);
        OSIF_DEV_ASSERT(Status == E_OK);
        u32Counter = (uint32)value;
#elif defined(USING_OS_ZEPHYR)
        /* ZephyrOS */
        (void)u32CoreId;
        u32Counter = Trusted_k_cycle_get_32();
#elif defined(USING_OS_FREERTOS) || defined(USING_OS_BAREMETAL)
        /* FreeRTOS and Baremetal*/
        (void)u32CoreId;
        u32Counter = Trusted_OsIf_Timer_System_Internal_GetCounter();
#endif
    }

    return u32Counter;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : OsIf_Timer_System_GetElapsed.
 * Description   : Get elapsed value from system timer.
 * 
 *END**************************************************************************/
uint32 OsIf_Timer_System_GetElapsed(uint32 * const CurrentRef)
{
    uint32 u32Elapsed = 0U;
    uint32 u32CoreId = OsIfCoreID();
#if defined(USING_OS_AUTOSAROS)
    TickType ElapsedTickType;
    StatusType Status;
#elif defined(USING_OS_ZEPHYR)
    uint32 u32CurrentVal;
#endif

#if (STD_ON == OSIF_DEV_ERROR_DETECT)
    if (TRUE != OsIf_abMdlInit[u32CoreId])
    {
    #if defined(USING_OS_AUTOSAROS)
        (void)Det_ReportError(OSIF_MODULE_ID, OSIF_DRIVER_INSTANCE, OSIF_SID_GETELAPSED, OSIF_E_UNINIT);
    #else
        OSIF_DEV_ASSERT(FALSE);
    #endif /* defined(USING_OS_AUTOSAROS) */
    }
#if (STD_ON == OSIF_ENABLE_MULTICORE_SUPPORT)
    else if ((OSIF_MAX_COREIDX_SUPPORTED  <= u32CoreId) || (NULL_PTR == OsIf_apxInternalCfg[u32CoreId]))
#else
    else if (NULL_PTR == OsIf_apxInternalCfg[u32CoreId])
#endif /* (STD_ON == OSIF_ENABLE_MULTICORE_SUPPORT) */
    {
    #if defined(USING_OS_AUTOSAROS)
        (void)Det_ReportError(OSIF_MODULE_ID, OSIF_DRIVER_INSTANCE, OSIF_SID_GETELAPSED, OSIF_E_INV_CORE_IDX);
    #else
        OSIF_DEV_ASSERT(FALSE);
    #endif /* defined(USING_OS_AUTOSAROS) */
    }
    else
#endif /* (STD_ON == OSIF_DEV_ERROR_DETECT) */
    {
#if defined(USING_OS_AUTOSAROS)
        Status = GetElapsedValue(OsIf_apxInternalCfg[u32CoreId]->counterId, (TickType*)CurrentRef, &ElapsedTickType);
        OSIF_DEV_ASSERT(Status == E_OK);
        u32Elapsed = (uint32)ElapsedTickType;
#elif defined(USING_OS_ZEPHYR)
        /* No need for additional logic. The hardware clock is represented as a 32-bit counter */
        u32CurrentVal = Trusted_k_cycle_get_32();
        u32Elapsed = u32CurrentVal - *CurrentRef;
        *CurrentRef = u32CurrentVal;
        (void)u32CoreId;
#elif defined(USING_OS_FREERTOS) || defined(USING_OS_BAREMETAL)
        /* FreeRTOS and Baremetal*/
        (void)u32CoreId;
        u32Elapsed = Trusted_OsIf_Timer_System_Internal_GetElapsed(CurrentRef);
#endif
    }

    return u32Elapsed;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : OsIf_Timer_System_SetTimerFrequency.
 * Description   : Set system timer frequency.
 * 
 *END**************************************************************************/
void OsIf_Timer_System_SetTimerFrequency(uint32 Freq)
{
    uint32 u32CoreId = OsIfCoreID();

#if (STD_ON == OSIF_DEV_ERROR_DETECT)
    if (TRUE != OsIf_abMdlInit[u32CoreId])
    {
    #if defined(USING_OS_AUTOSAROS)
        (void)Det_ReportError(OSIF_MODULE_ID, OSIF_DRIVER_INSTANCE, OSIF_SID_SETTIMERFREQ, OSIF_E_UNINIT);
    #else
        OSIF_DEV_ASSERT(FALSE);
    #endif /* defined(USING_OS_AUTOSAROS) */
    }
#if (STD_ON == OSIF_ENABLE_MULTICORE_SUPPORT)
    else if ((OSIF_MAX_COREIDX_SUPPORTED <= u32CoreId) || (NULL_PTR == OsIf_apxInternalCfg[u32CoreId]))
#else
    else if (NULL_PTR == OsIf_apxInternalCfg[u32CoreId])
#endif /* (STD_ON == OSIF_ENABLE_MULTICORE_SUPPORT) */
    {
    #if defined(USING_OS_AUTOSAROS)
        (void)Det_ReportError(OSIF_MODULE_ID, OSIF_DRIVER_INSTANCE, OSIF_SID_SETTIMERFREQ, OSIF_E_INV_CORE_IDX);
    #else
        OSIF_DEV_ASSERT(FALSE);
    #endif /* defined(USING_OS_AUTOSAROS) */
    }
    else
#endif /* (STD_ON == OSIF_DEV_ERROR_DETECT)  */
    {
#if defined(USING_OS_AUTOSAROS)
        (void)u32CoreId;
        (void)Freq;
#elif defined(USING_OS_ZEPHYR)
        (void)u32CoreId;
        (void)Freq;
        /* As of 2.6.0: "The frequency of this counter is required to be steady over time" */
#elif defined(USING_OS_FREERTOS) || defined(USING_OS_BAREMETAL)
        /* FreeRTOS and Baremetal*/
        OsIf_au32InternalFrequencies[u32CoreId] = Freq;
#endif
    }
}

/*FUNCTION**********************************************************************
 *
 * Function Name : OsIf_Timer_System_MicrosToTicks.
 * Description   : Convert micro second to ticks based on system timer frequency.
 * 
 *END**************************************************************************/
uint32 OsIf_Timer_System_MicrosToTicks(uint32 Micros)
{
    uint64 u64Interim;
    uint32 u32Ticks = 0U;
    uint32 u32CoreId = OsIfCoreID();

#if (STD_ON == OSIF_DEV_ERROR_DETECT)
    if (TRUE != OsIf_abMdlInit[u32CoreId])
    {
    #if defined(USING_OS_AUTOSAROS)
        (void)Det_ReportError(OSIF_MODULE_ID, OSIF_DRIVER_INSTANCE, OSIF_SID_US2TICKS, OSIF_E_UNINIT);
    #else
        OSIF_DEV_ASSERT(FALSE);
    #endif /* defined(USING_OS_AUTOSAROS) */
    }
#if (STD_ON == OSIF_ENABLE_MULTICORE_SUPPORT)
    else if ((OSIF_MAX_COREIDX_SUPPORTED <= u32CoreId) || (NULL_PTR == OsIf_apxInternalCfg[u32CoreId]))
#else
    else if (NULL_PTR == OsIf_apxInternalCfg[u32CoreId])
#endif
    {
    #if defined(USING_OS_AUTOSAROS)
        (void)Det_ReportError(OSIF_MODULE_ID, OSIF_DRIVER_INSTANCE, OSIF_SID_US2TICKS, OSIF_E_INV_CORE_IDX);
    #else
        OSIF_DEV_ASSERT(FALSE);
    #endif /* defined(USING_OS_AUTOSAROS) */
    }
    else
#endif /* (STD_ON == OSIF_DEV_ERROR_DETECT)  */
    {
        u64Interim = Micros * (uint64)OsIf_au32InternalFrequencies[u32CoreId];
        u64Interim /= 1000000u;
        /* check that computed value fits in 32 bits */
        OSIF_DEV_ASSERT(u64Interim <= 0xFFFFFFFFu);
        u32Ticks = (uint32)(u64Interim & 0xFFFFFFFFu);
    }

    return u32Ticks;
}

#define BASENXP_STOP_SEC_CODE
#include "BaseNXP_MemMap.h"

#endif /* (OSIF_USE_SYSTEM_TIMER == STD_ON) */

#ifdef __cplusplus
}
#endif

/** @} */
