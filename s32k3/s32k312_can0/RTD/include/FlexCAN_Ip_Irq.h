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

#ifndef FLEXCAN_IP_IRQ_H
#define FLEXCAN_IP_IRQ_H

/**
* @file FlexCAN_Ip_Irq.h
*
* @brief Interrupt Handlers for FlexCAN
* @details Contains the Interrupt Handlers Header file
*
* @addtogroup FlexCAN
* @{
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
#include "FlexCAN_Ip.h"
/*==================================================================================================
*                              SOURCE FILE VERSION INFORMATION
==================================================================================================*/
#define FLEXCAN_IP_IRQ_VENDOR_ID_H                      43
#define FLEXCAN_IP_IRQ_AR_RELEASE_MAJOR_VERSION_H       4
#define FLEXCAN_IP_IRQ_AR_RELEASE_MINOR_VERSION_H       7
#define FLEXCAN_IP_IRQ_AR_RELEASE_REVISION_VERSION_H    0
#define FLEXCAN_IP_IRQ_SW_MAJOR_VERSION_H               6
#define FLEXCAN_IP_IRQ_SW_MINOR_VERSION_H               0
#define FLEXCAN_IP_IRQ_SW_PATCH_VERSION_H               0
/*==================================================================================================
*                                     FILE VERSION CHECKS
==================================================================================================*/
/* Check if current file and FlexCAN_Ip_Irq header file are of the same vendor */
#if (FLEXCAN_IP_IRQ_VENDOR_ID_H != FLEXCAN_IP_VENDOR_ID_H)
    #error "FlexCAN_Ip_Irq.h and FlexCAN_Ip.h have different vendor ids"
#endif
/* Check if current file and FlexCAN_Ip_Irq header file are of the same Autosar version */
#if ((FLEXCAN_IP_IRQ_AR_RELEASE_MAJOR_VERSION_H    != FLEXCAN_IP_AR_RELEASE_MAJOR_VERSION_H) || \
     (FLEXCAN_IP_IRQ_AR_RELEASE_MINOR_VERSION_H    != FLEXCAN_IP_AR_RELEASE_MINOR_VERSION_H) || \
     (FLEXCAN_IP_IRQ_AR_RELEASE_REVISION_VERSION_H != FLEXCAN_IP_AR_RELEASE_REVISION_VERSION_H) \
    )
    #error "AutoSar Version Numbers of FlexCAN_Ip_Irq.h and FlexCAN_Ip.h are different"
#endif
/* Check if current file and FlexCAN_Ip_Irq header file are of the same Software version */
#if ((FLEXCAN_IP_IRQ_SW_MAJOR_VERSION_H != FLEXCAN_IP_SW_MAJOR_VERSION_H) || \
     (FLEXCAN_IP_IRQ_SW_MINOR_VERSION_H != FLEXCAN_IP_SW_MINOR_VERSION_H) || \
     (FLEXCAN_IP_IRQ_SW_PATCH_VERSION_H != FLEXCAN_IP_SW_PATCH_VERSION_H) \
    )
    #error "Software Version Numbers of FlexCAN_Ip_Irq.h and FlexCAN_Ip.h are different"
#endif

/*==================================================================================================
*                                          CONSTANTS
==================================================================================================*/

/*==================================================================================================
*                                      DEFINES AND MACROS
==================================================================================================*/

/*==================================================================================================
*                                    FUNCTION PROTOTYPES
==================================================================================================*/
#define CAN_43_FLEXCAN_START_SEC_CODE
#include "Can_43_FLEXCAN_MemMap.h"

/**
 * @brief Interrupt handler for FLEXCAN data
 * @details This handler process data from MB or FIFO
 * @param[in]   Instance                A FlexCAN instance number
 * @param[in]   StartMbIdx              Message Buffer index start
 * @param[in]   EndMbIdx                Message Buffer index end
 * @param[in]   bEnhancedFifoExisted    Specifies Enhanced Fifo is processed in current handler or not.
 *                                      Only exist if Enhanced Fifo and Message Buffer interrupts are unified
 */
void FlexCAN_IRQHandler
(
    uint8 Instance,
    uint32 StartMbIdx,
    uint32 EndMbIdx
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_OFF)
    ,boolean bEnhancedFifoExisted
#endif
#endif
);

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_ON)
/**
 * @brief Interrupt handler for Enhanced Fifo
 * @details This handler is for Enhanced Fifo interrupts separated
 * @param[in]   u8Instance    A FlexCAN instance number
 */
    void FlexCAN_EnhancedRxFIFODataIRQHandler(uint8 u8Instance);
#endif
#endif

#if (FLEXCAN_IP_FEATURE_BUSOFF_ERROR_INTERRUPT_UNIFIED == STD_ON)
/**
 * @brief Interrupt handler for Busoff and Error
 * @details This handler is for Busoff and Error interrupts unified
 * @param[in]   Instance    A FlexCAN instance number
 */
    void FlexCAN_Busoff_Error_IRQHandler(uint8 Instance);
#else
/**
 * @brief Interrupt handler for Busoff
 * @details This handler is for Busoff and Warning interrupts separated
 * @param[in]   Instance    A FlexCAN instance number
 */
    void FlexCAN_BusOff_IRQHandler(uint8 Instance);

/**
 * @brief Interrupt handler for Error
 * @details This handler is for Error interrupts separated
 * @param[in]   Instance    A FlexCAN instance number
 */
    void FlexCAN_Error_IRQHandler(uint8 Instance);
#endif

#if (FLEXCAN_IP_FEATURE_HAS_PRETENDED_NETWORKING == STD_ON)
/**
 * @brief Interrupt handler for Pretended Networking Wakeup
 * @details This handler verifies the event which caused the wake up
 * @param[in]   u8Instance    A FlexCAN instance number
 */
void FlexCAN_WakeUp_IRQHandler(uint8 u8Instance);
#endif

#if (FLEXCAN_IP_FEATURE_HAS_MEM_ERR_DET == STD_ON)
#if (FLEXCAN_IP_FEATURE_MEM_ERR_DET_ENABLED == STD_ON)
/**
 * @brief Interrupt handler for Memory Error Detection
 * @details This handler is for Memory Error interrupts
 * @param[in]   Instance    A FlexCAN instance number
 */
    void FlexCAN_MemError_IRQHandler(uint8 Instance);
#endif
#endif

#define CAN_43_FLEXCAN_STOP_SEC_CODE
#include "Can_43_FLEXCAN_MemMap.h"

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* FLEXCAN_IP_IRQ_H*/
