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
*   @file OsIf_Software_Semaphore.c
*   @version 6.0.0
*
*   @brief   BaseNXP - Implements the driver functionality.
*   @details Implements the software semaphore APIs. 
*
*   @addtogroup  osif_drv
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
#include "OsIf_Software_Semaphore.h"
#include "SchM_BaseNXP.h"
/*==================================================================================================
*                                 SOURCE FILE VERSION INFORMATION
==================================================================================================*/

#define OSIF_SOFTWARE_SEMAPHORE_VENDOR_ID_C                       43
#define OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_MAJOR_VERSION_C        4
#define OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_MINOR_VERSION_C        7
#define OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_REVISION_VERSION_C     0
#define OSIF_SOFTWARE_SEMAPHORE_SW_MAJOR_VERSION_C                6
#define OSIF_SOFTWARE_SEMAPHORE_SW_MINOR_VERSION_C                0
#define OSIF_SOFTWARE_SEMAPHORE_SW_PATCH_VERSION_C                0

/*==================================================================================================
*                                       FILE VERSION CHECKS
==================================================================================================*/
#ifndef DISABLE_MCAL_INTERMODULE_ASR_CHECK
    /* Check if current file and SchM_BaseNXP header file are of the same Autosar version */
    #if ((OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_MAJOR_VERSION_C != SCHM_BASENXP_AR_RELEASE_MAJOR_VERSION) || \
         (OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_MINOR_VERSION_C != SCHM_BASENXP_AR_RELEASE_MINOR_VERSION) \
        )
        #error "AutoSar Version Numbers of OsIf_Software_Semaphore.c and SchM_BaseNXP.h are different"
    #endif
#endif

/* Checks against OsIf_Software_Semaphore.h */
#if (OSIF_SOFTWARE_SEMAPHORE_VENDOR_ID_C != OSIF_SOFTWARE_SEMAPHORE_VENDOR_ID_H)
    #error "OsIf_Software_Semaphore.c and OsIf_Software_Semaphore.h have different vendor ids"
#endif
#if ((OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_MAJOR_VERSION_C    != OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_MAJOR_VERSION_H) || \
     (OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_MINOR_VERSION_C    != OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_MINOR_VERSION_H) || \
     (OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_REVISION_VERSION_C != OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_REVISION_VERSION_H))
    #error "AUTOSAR Version Numbers of OsIf_Software_Semaphore.c and OsIf_Software_Semaphore.h are different"
#endif
#if ((OSIF_SOFTWARE_SEMAPHORE_SW_MAJOR_VERSION_C != OSIF_SOFTWARE_SEMAPHORE_SW_MAJOR_VERSION_H) || \
     (OSIF_SOFTWARE_SEMAPHORE_SW_MINOR_VERSION_C != OSIF_SOFTWARE_SEMAPHORE_SW_MINOR_VERSION_H) || \
     (OSIF_SOFTWARE_SEMAPHORE_SW_PATCH_VERSION_C != OSIF_SOFTWARE_SEMAPHORE_SW_PATCH_VERSION_H) \
    )
    #error "Software Version Numbers of OsIf_Software_Semaphore.c and OsIf_Software_Semaphore.h are different"
#endif

/*==================================================================================================
*                           LOCAL TYPEDEFS (STRUCTURES, UNIONS, ENUMS)
==================================================================================================*/

/*==================================================================================================
*                                          LOCAL MACROS
==================================================================================================*/

/*==================================================================================================
*                                         LOCAL CONSTANTS
==================================================================================================*/

/*==================================================================================================
*                                         LOCAL VARIABLES
==================================================================================================*/

/*==================================================================================================
*                                        GLOBAL CONSTANTS
==================================================================================================*/

/*==================================================================================================
*                                        GLOBAL VARIABLES
==================================================================================================*/

/*==================================================================================================
*                                    LOCAL FUNCTION PROTOTYPES
==================================================================================================*/

/*==================================================================================================
*                                         LOCAL FUNCTIONS
==================================================================================================*/

/*==================================================================================================
*                                        GLOBAL FUNCTIONS
==================================================================================================*/
#ifdef OSIF_SOFTWARE_SEMAPHORE_ENABLE
#if (OSIF_SOFTWARE_SEMAPHORE_ENABLE == STD_ON)

#define BASENXP_START_SEC_CODE
#include "BaseNXP_MemMap.h"

#ifndef CPU_CORTEX_M0
/*FUNCTION**********************************************************************
 *
 * Function Name : OsIf_Software_Semaphore_Lock.
 * Description   : Lock semaphore.
 * @implements OsIf_Software_Semaphore_Lock_Activity
 *END**************************************************************************/
boolean OsIf_Software_Semaphore_Lock(const uint32 *Semaphore,
                                     uint32 Lockval
                                    )
{
    uint32 u32Result = 0U;
    /* Store lock value to shemaphore if it is unlock */
/*LDRA_NOANALYSIS*/

    SchM_Enter_BaseNXP_BASENXP_EXCLUSIVE_AREA_00();

    ASM_KEYWORD("dsb");
    ASM_KEYWORD("isb");
    ASM_KEYWORD (" LDREX %0, [%1] \n\t"
                 " CMP %0, #0 \n\t"
                 " IT EQ\n\t"
                 " STREXEQ %0, %2, [%1]"
                 : "=&r" (u32Result)
                 : "r" (Semaphore), "r" (Lockval));
    ASM_KEYWORD("dsb");
    ASM_KEYWORD("isb");

    SchM_Exit_BaseNXP_BASENXP_EXCLUSIVE_AREA_00();

/*LDRA_ANALYSIS*/
    return (u32Result == 0U);
}

/*FUNCTION**********************************************************************
 *
 * Function Name : OsIf_Software_Semaphore_Unlock.
 * Description   : Unlock semaphore.
 * @implements OsIf_Software_Semaphore_Unlock_Activity
 *END**************************************************************************/
boolean OsIf_Software_Semaphore_Unlock(const uint32 *Semaphore,
                                       uint32 Lockval
                                      )
{
    uint32 u32Result = 0U;
    uint32 u32UnlockValue = OSIF_SOFTWARE_SEMAPHORE_UNLOCKED_VALUE;
    /* Store unlock value to shemaphore if the lock value is matched */
/*LDRA_NOANALYSIS*/

    SchM_Enter_BaseNXP_BASENXP_EXCLUSIVE_AREA_01();

    ASM_KEYWORD("dsb");
    ASM_KEYWORD("isb");
    ASM_KEYWORD (" LDREX %0, [%2] \n\t"
                 " CMP %0, %3 \n\t"
                 " IT EQ\n\t"
                 " STREXEQ %0, %1, [%2]"
                 : "=&r" (u32Result)
                 : "r" (u32UnlockValue), "r" (Semaphore), "r" (Lockval));
    ASM_KEYWORD("dsb");
    ASM_KEYWORD("isb");

    SchM_Exit_BaseNXP_BASENXP_EXCLUSIVE_AREA_01();

/*LDRA_ANALYSIS*/
    return (u32Result == 0U);
}

#endif /* ifndef CPU_CORTEX_M0 */

#define BASENXP_STOP_SEC_CODE
#include "BaseNXP_MemMap.h"


#endif /* #if (OSIF_SOFTWARE_SEMAPHORE_ENABLE == STD_ON) */
#endif /* #ifdef OSIF_SOFTWARE_SEMAPHORE_ENABLE */

#ifdef __cplusplus
}
#endif

/** @} */
