/*==================================================================================================
*   Project              : RTD AUTOSAR 4.7
*   Platform             : CORTEXM
*   Peripheral           : 
*   Dependencies         : none
*
*   Autosar Version      : 4.7.0
*   Autosar Revision     : ASR_REL_4_7_REV_0000
*   Autosar Conf.Variant :
*   SW Version           : 6.0.0
*   Build Version        : S32K3_RTD_6_0_0_D2506_ASR_REL_4_7_REV_0000_20250610
*
*   Copyright 2020 - 2025 NXP
*
*   NXP Confidential and Proprietary. This software is owned or controlled by NXP and may only be 
*   used strictly in accordance with the applicable license terms.  By expressly 
*   accepting such terms or by downloading, installing, activating and/or otherwise 
*   using the software, you are agreeing that you have read, and that you agree to 
*   comply with and are bound by, such license terms.  If you do not agree to be 
*   bound by the applicable license terms, then you may not retain, install,
*   activate or otherwise use the software.
==================================================================================================*/

#ifndef INTCTRL_IP_CFG_H_
#define INTCTRL_IP_CFG_H_

/*==================================================================================================
                                         INCLUDE FILES
==================================================================================================*/
#include "IntCtrl_Ip_TypesDef.h"

/*==================================================================================================
*                              SOURCE FILE VERSION INFORMATION
==================================================================================================*/
#define CDD_PLATFORM_INTCTRL_IP_CFG_VENDOR_ID                          43
#define CDD_PLATFORM_INTCTRL_IP_CFG_AR_RELEASE_MAJOR_VERSION           4
#define CDD_PLATFORM_INTCTRL_IP_CFG_AR_RELEASE_MINOR_VERSION           7
#define CDD_PLATFORM_INTCTRL_IP_CFG_AR_RELEASE_REVISION_VERSION        0
#define CDD_PLATFORM_INTCTRL_IP_CFG_SW_MAJOR_VERSION                   6
#define CDD_PLATFORM_INTCTRL_IP_CFG_SW_MINOR_VERSION                   0
#define CDD_PLATFORM_INTCTRL_IP_CFG_SW_PATCH_VERSION                   0

/*==================================================================================================
                                      FILE VERSION CHECKS
==================================================================================================*/
/* Check if current file and IntCtrl_Ip_TypesDef header file are of the same vendor */
#if (CDD_PLATFORM_INTCTRL_IP_CFG_VENDOR_ID != CDD_PLATFORM_INTCTRL_IP_TYPESDEF_VENDOR_ID)
    #error "IntCtrl_Ip_Cfg.h and IntCtrl_Ip_TypesDef.h have different vendor ids"
#endif
/* Check if current file and IntCtrl_Ip_TypesDef header file are of the same Autosar version */
#if ((CDD_PLATFORM_INTCTRL_IP_CFG_AR_RELEASE_MAJOR_VERSION    != CDD_PLATFORM_INTCTRL_IP_TYPESDEF_AR_RELEASE_MAJOR_VERSION) || \
     (CDD_PLATFORM_INTCTRL_IP_CFG_AR_RELEASE_MINOR_VERSION    != CDD_PLATFORM_INTCTRL_IP_TYPESDEF_AR_RELEASE_MINOR_VERSION) || \
     (CDD_PLATFORM_INTCTRL_IP_CFG_AR_RELEASE_REVISION_VERSION != CDD_PLATFORM_INTCTRL_IP_TYPESDEF_AR_RELEASE_REVISION_VERSION) \
    )
    #error "AutoSar Version Numbers of IntCtrl_Ip_Cfg.h and IntCtrl_Ip_TypesDef.h are different"
#endif
/* Check if current file and Fls header file are of the same Software version */
#if ((CDD_PLATFORM_INTCTRL_IP_CFG_SW_MAJOR_VERSION != CDD_PLATFORM_INTCTRL_IP_TYPESDEF_SW_MAJOR_VERSION) || \
     (CDD_PLATFORM_INTCTRL_IP_CFG_SW_MINOR_VERSION != CDD_PLATFORM_INTCTRL_IP_TYPESDEF_SW_MINOR_VERSION) || \
     (CDD_PLATFORM_INTCTRL_IP_CFG_SW_PATCH_VERSION != CDD_PLATFORM_INTCTRL_IP_TYPESDEF_SW_PATCH_VERSION) \
    )
    #error "Software Version Numbers of IntCtrl_Ip_Cfg.h and IntCtrl_Ip_TypesDef.h are different"
#endif

/*==================================================================================================
                                       GLOBAL VARIABLES
==================================================================================================*/
#define PLATFORM_START_SEC_CONFIG_DATA_UNSPECIFIED
#include "Platform_MemMap.h"
/* Declaration of the configuration structure #1 for Interrupt Controller (core related) */
extern const IntCtrl_Ip_CtrlConfigType IntCtrlConfig_0;
/* Declaration of interrupt handlers 
 * NOTE: For application specific handlers, these functions must be implemented in the application code!
 */
extern void CAN0_ORED_IRQHandler(void);
extern void CAN0_ORED_0_31_MB_IRQHandler(void);
extern void CAN1_ORED_IRQHandler(void);
extern void CAN1_ORED_0_31_MB_IRQHandler(void);
extern void CAN2_ORED_IRQHandler(void);
extern void CAN2_ORED_0_31_MB_IRQHandler(void);
extern void CAN3_ORED_IRQHandler(void);
extern void CAN3_ORED_0_31_MB_IRQHandler(void);
extern void CAN4_ORED_IRQHandler(void);
extern void CAN4_ORED_0_31_MB_IRQHandler(void);
extern void CAN5_ORED_IRQHandler(void);
extern void CAN5_ORED_0_31_MB_IRQHandler(void);
#define PLATFORM_STOP_SEC_CONFIG_DATA_UNSPECIFIED
#include "Platform_MemMap.h"

#endif /* INTCTRL_IP_CFG_H_ */

