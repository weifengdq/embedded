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
*   @file OsIf_Software_Semaphore.h
*   @version 6.0.0
*
*   @brief   BaseNXP - Driver header file.
*   @details Specific driver header file.
*
*   @addtogroup osif_drv
*   @{
*/

#ifndef OSIF_SOFTWARE_SEMAPHORE_H
#define OSIF_SOFTWARE_SEMAPHORE_H

#ifdef __cplusplus
extern "C"{
#endif

/*==================================================================================================
*                                          INCLUDE FILES
* 1) system and project includes
* 2) needed interfaces from external units
* 3) internal and external interfaces from this unit
==================================================================================================*/
#include "StandardTypes.h"
#include "OsIf_Cfg.h"

/*==================================================================================================
*                                 SOURCE FILE VERSION INFORMATION
==================================================================================================*/
#define OSIF_SOFTWARE_SEMAPHORE_VENDOR_ID_H                    43
#define OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_MAJOR_VERSION_H     4
#define OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_MINOR_VERSION_H     7
#define OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_REVISION_VERSION_H  0
#define OSIF_SOFTWARE_SEMAPHORE_SW_MAJOR_VERSION_H             6
#define OSIF_SOFTWARE_SEMAPHORE_SW_MINOR_VERSION_H             0
#define OSIF_SOFTWARE_SEMAPHORE_SW_PATCH_VERSION_H             0

/*==================================================================================================
*                                       FILE VERSION CHECKS
==================================================================================================*/
/* Checks against StandardTypes.h */
#ifndef DISABLE_MCAL_INTERMODULE_ASR_CHECK
    #if ((OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_MAJOR_VERSION_H != STD_AR_RELEASE_MAJOR_VERSION) || \
         (OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_MINOR_VERSION_H != STD_AR_RELEASE_MINOR_VERSION))
        #error "AutoSar Version Numbers of OsIf_Software_Semaphore.h and StandardTypes.h are different"
    #endif
#endif

/* Checks against OsIf_Cfg.h */
#ifndef DISABLE_MCAL_INTERMODULE_ASR_CHECK
    #if ((OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_MAJOR_VERSION_H != OSIF_CFG_AR_RELEASE_MAJOR_VERSION) || \
         (OSIF_SOFTWARE_SEMAPHORE_AR_RELEASE_MINOR_VERSION_H != OSIF_CFG_AR_RELEASE_MINOR_VERSION))
        #error "AutoSar Version Numbers of OsIf_Software_Semaphore.h and OsIf_Cfg.h are different"
    #endif
#endif

/*==================================================================================================
*                                            CONSTANTS
==================================================================================================*/

/*==================================================================================================
*                                       DEFINES AND MACROS
==================================================================================================*/
#ifdef OSIF_SOFTWARE_SEMAPHORE_ENABLE
#if (OSIF_SOFTWARE_SEMAPHORE_ENABLE == STD_ON)

#define OSIF_SOFTWARE_SEMAPHORE_UNLOCKED_VALUE  0U

/*==================================================================================================
*                                              ENUMS
==================================================================================================*/

/*==================================================================================================
*                                  STRUCTURES AND OTHER TYPEDEFS
==================================================================================================*/

/*==================================================================================================
*                                  GLOBAL VARIABLE DECLARATIONS
==================================================================================================*/

/*==================================================================================================
*                                       FUNCTION PROTOTYPES
==================================================================================================*/
#define BASENXP_START_SEC_CODE
#include "BaseNXP_MemMap.h"

/*!
 * @brief Lock software semaphore
 *
 * This function locks software semaphore
 *
 * @param[in] Semaphore the pointer to software semaphore
 * @param[in] Lockval the lock value
 */
boolean OsIf_Software_Semaphore_Lock(const uint32 *Semaphore,
                                     uint32 Lockval
                                    );

/*!
 * @brief Unlock software semaphore
 *
 * This function unlocks software semaphore
 *
 * @param[in] Semaphore the pointer to software semaphore
 * @param[in] Lockval the lock value
 */
boolean OsIf_Software_Semaphore_Unlock(const uint32 *Semaphore,
                                       uint32 Lockval
                                      );

#define BASENXP_STOP_SEC_CODE
#include "BaseNXP_MemMap.h"

#endif /* #if (OSIF_SOFTWARE_SEMAPHORE_ENABLE == STD_ON) */
#endif /* #ifdef OSIF_SOFTWARE_SEMAPHORE_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* OSIF_SOFTWARE_SEMAPHORE_H */

/** @} */
