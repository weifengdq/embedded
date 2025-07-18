/*==================================================================================================
*   Project              : RTD AUTOSAR 4.7
*   Platform             : CORTEXM
*   Peripheral           : FLEXCAN
*   Dependencies         : 
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
*   used strictly in accordance with the applicable license terms. By expressly
*   accepting such terms or by downloading, installing, activating and/or otherwise
*   using the software, you are agreeing that you have read, and that you agree to
*   comply with and are bound by, such license terms. If you do not agree to be
*   bound by the applicable license terms, then you may not retain, install,
*   activate or otherwise use the software.
==================================================================================================*/
/***********************************************************************************************************************
 * This file was generated by the S32 Config Tools. Any manual edits made to this file
 * will be overwritten if the respective S32 Config Tools is used to update this file.
 **********************************************************************************************************************/

#ifndef FLEXCAN_IP_CFG_H_
#define FLEXCAN_IP_CFG_H_

/**
*   @file FlexCAN_Ip_Cfg.h
*
*   @addtogroup FlexCAN
*   @{
*/

#ifdef __cplusplus
extern "C"{
#endif

/*==================================================================================================
*                                        INCLUDE FILES
* 1) system and project includes
* 2) needed interfaces from external units
* 3) internal and external interfaces from this unit
==================================================================================================*/
#include "FlexCAN_Ip_Sa_PBcfg.h"
#include "Std_Types.h"
#include "Reg_eSys.h"
/*==================================================================================================
*                              SOURCE FILE VERSION INFORMATION
==================================================================================================*/
#define FLEXCAN_IP_CFG_VENDOR_ID_H                      43
#define FLEXCAN_IP_CFG_AR_RELEASE_MAJOR_VERSION_H       4
#define FLEXCAN_IP_CFG_AR_RELEASE_MINOR_VERSION_H       7
#define FLEXCAN_IP_CFG_AR_RELEASE_REVISION_VERSION_H    0
#define FLEXCAN_IP_CFG_SW_MAJOR_VERSION_H               6
#define FLEXCAN_IP_CFG_SW_MINOR_VERSION_H               0
#define FLEXCAN_IP_CFG_SW_PATCH_VERSION_H               0
/*==================================================================================================
*                                     FILE VERSION CHECKS
==================================================================================================*/
/* Checks against FlexCAN_Ip_Sa_PBcfg.h */
#if (FLEXCAN_IP_CFG_VENDOR_ID_H != FLEXCAN_IP_SA_VENDOR_ID_PBCFG_H)
    #error "FlexCAN_Ip_Cfg.h and FlexCAN_Ip_Sa_PBcfg.h have different vendor ids"
#endif
#if ((FLEXCAN_IP_CFG_AR_RELEASE_MAJOR_VERSION_H    != FLEXCAN_IP_SA_AR_RELEASE_MAJOR_VERSION_PBCFG_H) || \
     (FLEXCAN_IP_CFG_AR_RELEASE_MINOR_VERSION_H    != FLEXCAN_IP_SA_AR_RELEASE_MINOR_VERSION_PBCFG_H) || \
     (FLEXCAN_IP_CFG_AR_RELEASE_REVISION_VERSION_H != FLEXCAN_IP_SA_AR_RELEASE_REVISION_VERSION_PBCFG_H) \
    )
    #error "AutoSar Version Numbers of FlexCAN_Ip_Cfg.h and FlexCAN_Ip_Sa_PBcfg.h are different"
#endif
#if ((FLEXCAN_IP_CFG_SW_MAJOR_VERSION_H != FLEXCAN_IP_SA_SW_MAJOR_VERSION_PBCFG_H) || \
     (FLEXCAN_IP_CFG_SW_MINOR_VERSION_H != FLEXCAN_IP_SA_SW_MINOR_VERSION_PBCFG_H) || \
     (FLEXCAN_IP_CFG_SW_PATCH_VERSION_H != FLEXCAN_IP_SA_SW_PATCH_VERSION_PBCFG_H) \
    )
    #error "Software Version Numbers of FlexCAN_Ip_Cfg.h and FlexCAN_Ip_Sa_PBcfg.h are different"
#endif

#ifndef DISABLE_MCAL_INTERMODULE_ASR_CHECK
    /* Check if current file and Std_Types header file are of the same version */
    #if ((FLEXCAN_IP_CFG_AR_RELEASE_MAJOR_VERSION_H   != STD_AR_RELEASE_MAJOR_VERSION) || \
         (FLEXCAN_IP_CFG_AR_RELEASE_MINOR_VERSION_H   != STD_AR_RELEASE_MINOR_VERSION) \
        )
        #error "AutoSar Version Numbers of FlexCAN_Ip_Cfg.h and Std_Types.h are different"
    #endif

    /* Check if current file and Reg_eSys header file are of the same version */
    #if ((FLEXCAN_IP_CFG_AR_RELEASE_MAJOR_VERSION_H   != REG_ESYS_AR_RELEASE_MAJOR_VERSION) || \
         (FLEXCAN_IP_CFG_AR_RELEASE_MINOR_VERSION_H   != REG_ESYS_AR_RELEASE_MINOR_VERSION) \
        )
        #error "AutoSar Version Numbers of FlexCAN_Ip_Cfg.h and Reg_eSys.h are different"
    #endif
#endif
/*==================================================================================================
*                                          CONSTANTS
==================================================================================================*/

/*==================================================================================================
*                                      DEFINES AND MACROS
==================================================================================================*/
#define FLEXCAN_IP_STATE_EXT \
   FLEXCAN_IP_SA_STATE_PB_CFG
/* External Structures generated by FlexCAN_Ip_PBCfg */
#define FLEXCAN_IP_CONFIG_EXT \
    FLEXCAN_IP_SA_PB_CFG

/*! @brief Enables / Disables user mode support */
#define FLEXCAN_IP_ENABLE_USER_MODE_SUPPORT    (STD_OFF)


#if ((STD_ON == FLEXCAN_IP_ENABLE_USER_MODE_SUPPORT) && defined(MCAL_FLEXCAN_REG_PROT_AVAILABLE))
    #if (STD_ON == MCAL_FLEXCAN_REG_PROT_AVAILABLE)
        #define FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE      (STD_ON)
    #else
        #define FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE      (STD_OFF)
    #endif
#else
    #define FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE      (STD_OFF)
#endif

#if (FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE == STD_ON)
    /*! @brief Use to check if the instances support UAA bit or not */
    #define FLEXCAN_IP_CTRL_REG_PROT_SUPPORT_U8    ((uint8)0U)
#endif

/* Time out value in uS */
#define FLEXCAN_IP_TIMEOUT_DURATION    (10000U)

/* This this will set the timer source for osif that will be used for timeout */
#define FLEXCAN_IP_SERVICE_TIMEOUT_TYPE    (OSIF_COUNTER_DUMMY)

/* @brief Maximum number of Message Buffers supported for payload size 8 for any of the CAN instances */
#define FLEXCAN_IP_FEATURE_MAX_MB_NUM    (64U)

/* @brief Maximum number of Message Buffers supported for payload size 8 for CAN0 */
#define FLEXCAN_IP_FEATURE_INSTANCE_0_MAX_MB_NUM    (64U)

/* @brief Maximum number of Message Buffers supported for payload size 8 for CAN1 */
#define FLEXCAN_IP_FEATURE_INSTANCE_1_MAX_MB_NUM    (64U)

/* @brief Maximum number of Message Buffers supported for payload size 8 for CAN2 */
#define FLEXCAN_IP_FEATURE_INSTANCE_2_MAX_MB_NUM    (64U)

/* @brief Maximum number of Message Buffers supported for payload size 8 for CAN3 */
#define FLEXCAN_IP_FEATURE_INSTANCE_3_MAX_MB_NUM    (32U)

/* @brief Maximum number of Message Buffers supported for payload size 8 for CAN4 */
#define FLEXCAN_IP_FEATURE_INSTANCE_4_MAX_MB_NUM    (32U)

/* @brief Maximum number of Message Buffers supported for payload size 8 for CAN5 */
#define FLEXCAN_IP_FEATURE_INSTANCE_5_MAX_MB_NUM    (32U)

/* @brief Array of maximum number of Message Buffers supported for payload size 8 for all the CAN instances */
#define FLEXCAN_IP_FEATURE_MAX_MB_NUM_ARRAY { \
                        FLEXCAN_IP_FEATURE_INSTANCE_0_MAX_MB_NUM, \
                        FLEXCAN_IP_FEATURE_INSTANCE_1_MAX_MB_NUM, \
                        FLEXCAN_IP_FEATURE_INSTANCE_2_MAX_MB_NUM, \
                        FLEXCAN_IP_FEATURE_INSTANCE_3_MAX_MB_NUM, \
                        FLEXCAN_IP_FEATURE_INSTANCE_4_MAX_MB_NUM, \
                        FLEXCAN_IP_FEATURE_INSTANCE_5_MAX_MB_NUM \
}
/* @brief Has DMA enable (bit field MCR[DMA]). */
#define FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE    (STD_OFF)
/* @brief Has Supervisor Mode MCR[SUPV] */
#define FLEXCAN_IP_FEATURE_HAS_SUPV    (STD_ON)
/* @brief Has Flexible Data Rate */
#define FLEXCAN_IP_FEATURE_HAS_FD    (STD_ON)
/* @bried FlexCAN has Detection And Correction of Memory Errors */
#define FLEXCAN_IP_FEATURE_HAS_MEM_ERR_DET    (STD_ON)
/* @bried FlexCAN is enabled Detection And Correction of Memory Errors */
#define FLEXCAN_IP_FEATURE_MEM_ERR_DET_ENABLED (STD_OFF)
/* @brief Has FlexCAN Enhanced Rx FIFO mode */
#define FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO    (STD_ON)
/* @brief Enable/Disable the Enhanced Rx FIFO IdHit Callout */
#ifndef FLEXCAN_IP_ENABLE_IDHIT_CALLOUT
    #define FLEXCAN_IP_ENABLE_IDHIT_CALLOUT    (STD_OFF)
#endif
/* @brief Has FlexCAN expandable memory */
#define FLEXCAN_IP_FEATURE_HAS_EXPANDABLE_MEMORY    (STD_OFF)
/* @brief Has FlexCAN Timestamp enabled */
#define FLEXCAN_IP_FEATURE_HAS_TS_ENABLE    (STD_OFF)
/* @brief Has FlexCAN High Resolution Timer for Time stamp CAN Message */
#define FLEXCAN_IP_FEATURE_HAS_HR_TIMER    (STD_OFF)
/* @brief Time base source selection for Time stamp CAN Message */
#if (FLEXCAN_IP_FEATURE_HAS_HR_TIMER == STD_ON)
    #define FLEXCAN_HRTIMERSRC_STM0_VALUE    (0U)
    #define FLEXCAN_IP_HR_TIMESTAMP_SRC_SELECTABLE (STD_OFF)
#endif /* (FLEXCAN_IP_FEATURE_HAS_HR_TIMER == STD_ON) */
/* @brief specifies if the Enhanced Fifo has separated ISR */
#define FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED    (STD_OFF)
/* @brief Specifies if FlexCAN has PE clock selection */
#define FLEXCAN_IP_FEATURE_HAS_PE_CLKSRC_SELECT    (STD_OFF)
/* @brief Enable use of Enhanced CBT time segments and ENTDC */
#define FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT    (STD_ON)
/* @brief Has FD Iso Option Mode  */
#define FLEXCAN_IP_FEATURE_SWITCHINGISOMODE   (STD_ON)
/* @brief Has Protocol exception Mode */
#define FLEXCAN_IP_FEATURE_PROTOCOLEXCEPTION  (STD_ON)
/* @brief Has Edge filter Feature */
#define FLEXCAN_IP_FEATURE_EDGEFILTER         (STD_ON)
/* @brief Define if global variables need to be placed in non-cache area or not */
#define FLEXCAN_IP_FEATURE_NO_CACHE_NEEDED       (STD_OFF)
/* @brief Has Pretending Network Feature */
#define FLEXCAN_IP_FEATURE_HAS_PRETENDED_NETWORKING    (STD_OFF)
/* @brief Defines if interface will enable any of interrupts */
#define FLEXCAN_IP_MB_INTERRUPT_SUPPORT    (STD_ON)
/* @brief Specifies if support separated interrupt handlers for Busoff and Error events. */
#define FLEXCAN_IP_FEATURE_BUSOFF_ERROR_INTERRUPT_UNIFIED    (STD_ON)

/* @brief Defines the No Of Message Buffers Partions Suppport MBDSR regions */
#define FLEXCAN_IP_FEATURE_MBDSR_COUNT    (2U)
/* @brief Enable Development Error Detection */
#define FLEXCAN_IP_DEV_ERROR_DETECT    (STD_ON)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FLEXCAN_IP_CFG_H_ */

