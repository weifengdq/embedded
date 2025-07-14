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

/**
 *  @file FlexCAN_Ip.c
 *  @brief FlexCAN Driver File
 *  @addtogroup FlexCAN
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
#include "FlexCAN_Ip_Irq.h"
#include "FlexCAN_Ip_HwAccess.h"
#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
#include "Dma_Ip.h"
#endif

#if (STD_ON == FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE)
    #define USER_MODE_REG_PROT_ENABLED      (STD_ON)
    #include "RegLockMacros.h"
#endif /* (STD_ON == FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE) */

#include "SchM_Can_43_FLEXCAN.h"

#if ((defined (MCAL_ENABLE_FAULT_INJECTION)) || (defined (ERR_IPV_FLEXCAN_E050246)) || (defined (ERR_IPV_FLEXCAN_E050630)))
    #include "Mcal.h"
#endif
/*==================================================================================================
*                              SOURCE FILE VERSION INFORMATION
==================================================================================================*/
#define FLEXCAN_IP_VENDOR_ID_C                      43
#define FLEXCAN_IP_AR_RELEASE_MAJOR_VERSION_C       4
#define FLEXCAN_IP_AR_RELEASE_MINOR_VERSION_C       7
#define FLEXCAN_IP_AR_RELEASE_REVISION_VERSION_C    0
#define FLEXCAN_IP_SW_MAJOR_VERSION_C               6
#define FLEXCAN_IP_SW_MINOR_VERSION_C               0
#define FLEXCAN_IP_SW_PATCH_VERSION_C               0
/*==================================================================================================
*                                     FILE VERSION CHECKS
==================================================================================================*/
/* Check if current file and FlexCAN_Ip.h header file are of the same vendor */
#if (FLEXCAN_IP_VENDOR_ID_C != FLEXCAN_IP_VENDOR_ID_H)
    #error "FlexCAN_Ip.c and FlexCAN_Ip.h have different vendor ids"
#endif
/* Check if current file and FlexCAN_Ip.h header file are of the same Autosar version */
#if ((FLEXCAN_IP_AR_RELEASE_MAJOR_VERSION_C    != FLEXCAN_IP_AR_RELEASE_MAJOR_VERSION_H) || \
     (FLEXCAN_IP_AR_RELEASE_MINOR_VERSION_C    != FLEXCAN_IP_AR_RELEASE_MINOR_VERSION_H) || \
     (FLEXCAN_IP_AR_RELEASE_REVISION_VERSION_C != FLEXCAN_IP_AR_RELEASE_REVISION_VERSION_H) \
    )
    #error "AutoSar Version Numbers of FlexCAN_Ip.c and FlexCAN_Ip.h are different"
#endif
/* Check if current file and FlexCAN_Ip.h header file are of the same Software version */
#if ((FLEXCAN_IP_SW_MAJOR_VERSION_C != FLEXCAN_IP_SW_MAJOR_VERSION_H) || \
     (FLEXCAN_IP_SW_MINOR_VERSION_C != FLEXCAN_IP_SW_MINOR_VERSION_H) || \
     (FLEXCAN_IP_SW_PATCH_VERSION_C != FLEXCAN_IP_SW_PATCH_VERSION_H) \
    )
    #error "Software Version Numbers of FlexCAN_Ip.c and FlexCAN_Ip.h are different"
#endif

/* Check if current file and FlexCAN_Ip_Irq.h header file are of the same vendor */
#if (FLEXCAN_IP_VENDOR_ID_C != FLEXCAN_IP_IRQ_VENDOR_ID_H)
    #error "FlexCAN_Ip.c and FlexCAN_Ip_Irq.h have different vendor ids"
#endif
/* Check if current file and FlexCAN_Ip_Irq.h header file are of the same Autosar version */
#if ((FLEXCAN_IP_AR_RELEASE_MAJOR_VERSION_C    != FLEXCAN_IP_IRQ_AR_RELEASE_MAJOR_VERSION_H) || \
     (FLEXCAN_IP_AR_RELEASE_MINOR_VERSION_C    != FLEXCAN_IP_IRQ_AR_RELEASE_MINOR_VERSION_H) || \
     (FLEXCAN_IP_AR_RELEASE_REVISION_VERSION_C != FLEXCAN_IP_IRQ_AR_RELEASE_REVISION_VERSION_H) \
    )
    #error "AutoSar Version Numbers of FlexCAN_Ip.c and FlexCAN_Ip_Irq.h are different"
#endif
/* Check if current file and FlexCAN_Ip_Irq.h header file are of the same Software version */
#if ((FLEXCAN_IP_SW_MAJOR_VERSION_C != FLEXCAN_IP_IRQ_SW_MAJOR_VERSION_H) || \
     (FLEXCAN_IP_SW_MINOR_VERSION_C != FLEXCAN_IP_IRQ_SW_MINOR_VERSION_H) || \
     (FLEXCAN_IP_SW_PATCH_VERSION_C != FLEXCAN_IP_IRQ_SW_PATCH_VERSION_H) \
    )
    #error "Software Version Numbers of FlexCAN_Ip.c and FlexCAN_Ip_Irq.h are different"
#endif

/* Check if current file and FlexCAN_Ip_HwAccess.h header file are of the same vendor */
#if (FLEXCAN_IP_VENDOR_ID_C != FLEXCAN_IP_HWACCESS_VENDOR_ID_H)
    #error "FlexCAN_Ip.c and FlexCAN_Ip_HwAccess.h have different vendor ids"
#endif
/* Check if current file and FlexCAN_Ip_HwAccess.h header file are of the same Autosar version */
#if ((FLEXCAN_IP_AR_RELEASE_MAJOR_VERSION_C    != FLEXCAN_IP_HWACCESS_AR_RELEASE_MAJOR_VERSION_H) || \
     (FLEXCAN_IP_AR_RELEASE_MINOR_VERSION_C    != FLEXCAN_IP_HWACCESS_AR_RELEASE_MINOR_VERSION_H) || \
     (FLEXCAN_IP_AR_RELEASE_REVISION_VERSION_C != FLEXCAN_IP_HWACCESS_AR_RELEASE_REVISION_VERSION_H) \
    )
    #error "AutoSar Version Numbers of FlexCAN_Ip.c and FlexCAN_Ip_HwAccess.h are different"
#endif
/* Check if current file and FlexCAN_Ip_HwAccess.h header file are of the same Software version */
#if ((FLEXCAN_IP_SW_MAJOR_VERSION_C != FLEXCAN_IP_HWACCESS_SW_MAJOR_VERSION_H) || \
     (FLEXCAN_IP_SW_MINOR_VERSION_C != FLEXCAN_IP_HWACCESS_SW_MINOR_VERSION_H) || \
     (FLEXCAN_IP_SW_PATCH_VERSION_C != FLEXCAN_IP_HWACCESS_SW_PATCH_VERSION_H) \
    )
    #error "Software Version Numbers of FlexCAN_Ip.c and FlexCAN_Ip_HwAccess.h are different"
#endif

#ifndef DISABLE_MCAL_INTERMODULE_ASR_CHECK
    /* Check if current file and SchM_Can header file are of the same version */
    #if ((FLEXCAN_IP_AR_RELEASE_MAJOR_VERSION_C    != SCHM_CAN_43_FLEXCAN_AR_RELEASE_MAJOR_VERSION) || \
         (FLEXCAN_IP_AR_RELEASE_MINOR_VERSION_C     != SCHM_CAN_43_FLEXCAN_AR_RELEASE_MINOR_VERSION) \
        )
        #error "AutoSar Version Numbers of FlexCAN_Ip.c and SchM_Can_43_FLEXCAN.h are different"
    #endif
    /* Checks against current file and Dma_Ip.h */
    #if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
        #if ((FLEXCAN_IP_AR_RELEASE_MAJOR_VERSION_C    != DMA_IP_AR_RELEASE_MAJOR_VERSION) || \
             (FLEXCAN_IP_AR_RELEASE_MINOR_VERSION_C     != DMA_IP_AR_RELEASE_MINOR_VERSION) \
            )
            #error "AutoSar Version Numbers of FlexCAN_Ip.c and Dma_Ip.h are different"
        #endif
    #endif
    /* Checks against current file and RegLockMacros.h */
    #if (STD_ON == FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE)
        #if ((FLEXCAN_IP_AR_RELEASE_MAJOR_VERSION_C    != REGLOCKMACROS_AR_RELEASE_MAJOR_VERSION) || \
             (FLEXCAN_IP_AR_RELEASE_MINOR_VERSION_C     != REGLOCKMACROS_AR_RELEASE_MINOR_VERSION) \
            )
            #error "AutoSar Version Numbers of FlexCAN_Ip.c and RegLockMacros.h are different"
        #endif
    #endif /* (STD_ON == FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE) */
    /* Check if current file and Mcal.h header file are of the same Software version */
    #ifdef MCAL_ENABLE_FAULT_INJECTION
        #if ((FLEXCAN_IP_AR_RELEASE_MAJOR_VERSION_C    != MCAL_AR_RELEASE_MAJOR_VERSION) || \
             (FLEXCAN_IP_AR_RELEASE_MINOR_VERSION_C     != MCAL_AR_RELEASE_MINOR_VERSION) \
            )
            #error "AutoSar Version Numbers of FlexCAN_Ip.c and Mcal.h are different"
        #endif
    #endif
#endif
/*==================================================================================================
*                          LOCAL TYPEDEFS (STRUCTURES, UNIONS, ENUMS)
==================================================================================================*/

/*==================================================================================================
*                                       LOCAL MACROS
==================================================================================================*/
#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
    #if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
        #define FLEXCAN_IP_ENHANCE_TRASNFER_DIMENSION_LIST (13U)
    #endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)  */

    #define FLEXCAN_IP_TRASNFER_DIMENSION_LIST (10U)
#endif /*(FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)*/
/*==================================================================================================
*                                      LOCAL CONSTANTS
==================================================================================================*/

/*==================================================================================================
*                                      GLOBAL CONSTANTS
==================================================================================================*/

/*==================================================================================================
*                                      GLOBAL VARIABLES
==================================================================================================*/
#define CAN_43_FLEXCAN_START_SEC_CONST_UNSPECIFIED
#include "Can_43_FLEXCAN_MemMap.h"

/* Table of base addresses for CAN instances. */
static FLEXCAN_Type * const Flexcan_Ip_apxBase[] = FLEXCAN_IP_BASE_PTRS;

#define CAN_43_FLEXCAN_STOP_SEC_CONST_UNSPECIFIED
#include "Can_43_FLEXCAN_MemMap.h"

#if (FLEXCAN_IP_FEATURE_NO_CACHE_NEEDED == STD_ON)
#define CAN_43_FLEXCAN_START_SEC_VAR_CLEARED_UNSPECIFIED_NO_CACHEABLE
#else
#define CAN_43_FLEXCAN_START_SEC_VAR_CLEARED_UNSPECIFIED
#endif /* (FLEXCAN_IP_FEATURE_NO_CACHE_NEEDED == STD_ON) */
#include "Can_43_FLEXCAN_MemMap.h"

/* Pointer to runtime state structure.*/
static Flexcan_Ip_StateType * Flexcan_Ip_apxState[FLEXCAN_IP_INSTANCE_COUNT];

#if (FLEXCAN_IP_FEATURE_NO_CACHE_NEEDED == STD_ON)
#define CAN_43_FLEXCAN_STOP_SEC_VAR_CLEARED_UNSPECIFIED_NO_CACHEABLE
#else
#define CAN_43_FLEXCAN_STOP_SEC_VAR_CLEARED_UNSPECIFIED
#endif /* (FLEXCAN_IP_FEATURE_NO_CACHE_NEEDED == STD_ON) */
#include "Can_43_FLEXCAN_MemMap.h"


#define CAN_43_FLEXCAN_START_SEC_CODE
#include "Can_43_FLEXCAN_MemMap.h"
/*==================================================================================================
*                                   LOCAL FUNCTION PROTOTYPES
==================================================================================================*/
/* Init legacy, enhanced fifo if requested, if no fifo request, return success. */
static Flexcan_Ip_StatusType FlexCAN_InitRxFifo(FLEXCAN_Type * pBase, const Flexcan_Ip_ConfigType * pConfigData);

/* Init basically controller , this function is called in FlexCAN_Ip_Init only */
static Flexcan_Ip_StatusType FlexCAN_InitController(uint8 Instance, FLEXCAN_Type * pBase, const Flexcan_Ip_ConfigType * pConfigData);

static Flexcan_Ip_StatusType FlexCAN_InitCtroll(FLEXCAN_Type * pBase, const Flexcan_Ip_ConfigType * pConfigData);

static void FlexCAN_InitBaudrate(FLEXCAN_Type * pBase, const Flexcan_Ip_ConfigType * pConfigData);

static Flexcan_Ip_StatusType FlexCAN_StartRxMessageBufferData(uint8 Instance,
                                                              uint8 MbIdx,
                                                              Flexcan_Ip_MsgBuffType * Data,
                                                              boolean IsPolling
                                                             );

static Flexcan_Ip_StatusType FlexCAN_StartSendData(uint8 Instance,
                                                   uint8 MbIdx,
                                                   const Flexcan_Ip_DataInfoType * TxInfo,
                                                   uint32 MsgId,
                                                   const uint8 * MbData
                                                  );

static Flexcan_Ip_StatusType FlexCAN_StartRxMessageFifoData(uint8 Instance, Flexcan_Ip_MsgBuffType * Data);

static Flexcan_Ip_StatusType FlexCAN_ProccessLegacyRxFIFO(uint8 u8Instance, uint32 u32TimeoutMs);

static void FlexCAN_IRQHandlerRxMB(uint8 Instance, uint32 MbIdx);

static void FlexCAN_IRQHandlerTxMB(uint8 u8Instance, uint32 u32MbIdx);

static inline void FlexCAN_IRQHandlerRxFIFO(uint8 Instance, uint32 MbIdx);

#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
static void DMA_Can_Callback(uint8 Instance);
#endif

#if (STD_ON == FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE)
void FlexCAN_SetUserAccessAllowed(const FLEXCAN_Type * pBase);
void FlexCAN_ClrUserAccessAllowed(const FLEXCAN_Type * pBase);
#endif

static Flexcan_Ip_StatusType FlexCAN_AbortTxTransfer(uint8 u8Instance, uint8 MbIdx);
static Flexcan_Ip_StatusType FlexCAN_AbortRxTransfer(uint8 u8Instance, uint8 MbIdx);

static void FlexCAN_CompleteRxMessageFifoData(uint8 Instance);

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
static void FlexCAN_IRQHandlerEnhancedRxFIFO(uint8 Instance, uint32 IntType);
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_OFF)
static inline void FlexCAN_ProcessIRQHandlerEnhancedRxFIFO(uint8 u8Instance);
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_OFF) */ 

static void FlexCAN_CompleteRxMessageEnhancedFifoData(uint8 Instance);

#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
static void FlexCAN_ReadEnhancedRxFifoDma(uint8 Instance);
static void FlexCAN_CompleteRxMessageEnhancedFifoDataDma(uint8 Instance);
#endif

static Flexcan_Ip_StatusType FlexCAN_StartRxMessageEnhancedFifoData(uint8 Instance, Flexcan_Ip_MsgBuffType * Data);

static Flexcan_Ip_StatusType FlexCAN_ProccessEnhancedRxFifoBlocking(uint8 u8Instance, uint32 u32TimeoutMs);

static Flexcan_Ip_StatusType FlexCAN_ProccessEnhancedRxFifo(uint8 u8Instance, uint32 u32TimeoutMs);

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_ON)
static void FlexCAN_HandleEnhanceRxFIFO(uint8 Instance);
#endif
#endif /* FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON */
#if (FLEXCAN_IP_FEATURE_BUSOFF_ERROR_INTERRUPT_UNIFIED == STD_ON)
static void FlexCAN_ProcessIRQHandlerBusOffError(uint8 Instance, FLEXCAN_Type * pBase, const Flexcan_Ip_StateType * pState);
#endif /* (FLEXCAN_IP_FEATURE_BUSOFF_ERROR_INTERRUPT_UNIFIED) */
static uint32 FlexCAN_ErrorCallback(uint8 Instance, Flexcan_Ip_EventType event, uint32 u32ErrStatus, const Flexcan_Ip_StateType * pState);
static void FlexCAN_ClearAllIntStatusFlag
(
    FLEXCAN_Type * pBase,
    uint32 StartMbIdx,
    uint32 EndMbIdx
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_OFF)
    ,boolean bEnhancedFifoExisted
#endif
#endif
);
static void FlexCAN_ProcessIRQHandlerMsgBuffer
(
    uint8 Instance,
    const Flexcan_Ip_StateType * pState,
    uint32 MbIdx
);
/*==================================================================================================
*                                       LOCAL FUNCTIONS
==================================================================================================*/
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_InitRxFifo
 * Description   : Initialize fifo and dma if requested.
 *
 * This is not a public API as it is called from other driver functions.
 *END**************************************************************************/
static Flexcan_Ip_StatusType FlexCAN_InitRxFifo(FLEXCAN_Type * pBase, const Flexcan_Ip_ConfigType * pConfigData)
{
    Flexcan_Ip_StatusType eResult;

    /* Enable RxFIFO feature, if requested. This might fail if the FD mode is enabled. */
    if (pConfigData->is_rx_fifo_needed)
    {
        eResult = FlexCAN_EnableRxFifo(pBase, (uint32)pConfigData->num_id_filters);
    #if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
        if (FLEXCAN_STATUS_SUCCESS == eResult)
        {
            /* Enable DMA support for Rx FIFO transfer */
            if (FLEXCAN_RXFIFO_USING_DMA == pConfigData->transfer_type)
            {
                FlexCAN_SetRxFifoDMA(pBase, TRUE);
            }
            else
            {
                FlexCAN_SetRxFifoDMA(pBase, FALSE);
            }
        }
    #endif /* FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON */
    }
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
    /* Enable Enhanced RxFIFO feature, if requested.
    * This might fail if the current CAN instance does not support Enhaneced RxFIFO or the Rx FIFO is enabled. */
    else if (pConfigData->is_enhanced_rx_fifo_needed)
    {
    #if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
        if (FLEXCAN_RXFIFO_USING_DMA == pConfigData->transfer_type)
        {
            eResult = FlexCAN_EnableEnhancedRxFifo(pBase,
                                                   (uint32)pConfigData->num_enhanced_std_id_filters,
                                                   (uint32)pConfigData->num_enhanced_ext_id_filters,
                                                   (uint32)0U
                                                  ); /* for dma, each a frame received -> a minor loop */
        }
        else
    #endif /* FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */
        {
            eResult = FlexCAN_EnableEnhancedRxFifo(pBase,
                                                   (uint32)pConfigData->num_enhanced_std_id_filters,
                                                   (uint32)pConfigData->num_enhanced_ext_id_filters,
                                                   (uint32)pConfigData->num_enhanced_watermark
                                                  );
        }
    #if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
        if (FLEXCAN_STATUS_SUCCESS == eResult)
        {
            /* Enable DMA support for Enhanced FIFO transfer */
            if (FLEXCAN_RXFIFO_USING_DMA == pConfigData->transfer_type)
            {
                FlexCAN_SetRxFifoDMA(pBase, TRUE);
                FlexCAN_ConfigEnhancedRxFifoDMA(pBase, 20U); /* always transfer 80 bytes (DMALW = 19)*/
            }
            else
            {
                /* Clear Enhanced Rx FIFO status.*/
                FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_FRAME_AVAILABLE);
                FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_WATERMARK);
                FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_OVERFLOW);
                FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_UNDERFLOW);
                /* Clear the Enhanced RX FIFO engine */
                FlexCAN_ClearEnhancedRxFifoEngine(pBase);
                FlexCAN_SetRxFifoDMA(pBase, FALSE);
            }
        }
    #endif /* FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */
    }
#endif /* FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO */
    else
    {
        /* when both Legacy FIFO and Enhanced FIFO aren't enabled, the default return of execution is success. */
        eResult = FLEXCAN_STATUS_SUCCESS;
    }

    return eResult;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_InitCtroll
 * Description   : Initialize basically controller.
 *
 * This is not a public API as it is called from other driver functions.
 *END**************************************************************************/
static Flexcan_Ip_StatusType FlexCAN_InitCtroll(FLEXCAN_Type * pBase, const Flexcan_Ip_ConfigType * pConfigData)
{
    Flexcan_Ip_StatusType eResult = FLEXCAN_STATUS_SUCCESS;

    /* Disable the self reception feature if FlexCAN is not in loopback mode. */
    if (pConfigData->flexcanMode != FLEXCAN_LOOPBACK_MODE)
    {
        FlexCAN_SetSelfReception(pBase, FALSE);
    }

    /* Init legacy fifo, enhanced fifo if requested. */
    eResult = FlexCAN_InitRxFifo(pBase, pConfigData);
    if (eResult != FLEXCAN_STATUS_SUCCESS)
    {
        /* To enter Disable Mode requires FreezMode first */
        (void)FlexCAN_EnterFreezeMode(pBase);
        (void)FlexCAN_Disable(pBase);
    }
    else
    {
    #if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
        /* Set payload size. */
        FlexCAN_SetPayloadSize(pBase, &pConfigData->payload);
    #endif /* FLEXCAN_IP_FEATURE_HAS_FD */

    #if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
        eResult = FlexCAN_SetMaxMsgBuffNum(pBase, pConfigData->max_num_mb);
        if (eResult != FLEXCAN_STATUS_SUCCESS)
        {
            /* To enter Disable Mode requires FreezMode first */
            (void)FlexCAN_EnterFreezeMode(pBase);
            (void)FlexCAN_Disable(pBase);
        }
    #else
        (void)FlexCAN_SetMaxMsgBuffNum(pBase, pConfigData->max_num_mb);
    #endif /* FLEXCAN_IP_DEV_ERROR_DETECT */
    }
    return eResult;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_InitController
 * Description   : Initialize basically controller.
 *
 * This is not a public API as it is called from other driver functions.
 *END**************************************************************************/
static Flexcan_Ip_StatusType FlexCAN_InitController(uint8 Instance, FLEXCAN_Type * pBase, const Flexcan_Ip_ConfigType * pConfigData)
{
    Flexcan_Ip_StatusType eResult = FLEXCAN_STATUS_SUCCESS;

    if (FlexCAN_IsEnabled(pBase))
    {
        /* To enter Disable Mode requires FreezMode first */
        eResult = FlexCAN_EnterFreezeMode(pBase);
        if (FLEXCAN_STATUS_SUCCESS == eResult)
        {
            eResult = FlexCAN_Disable(pBase);
        }
    }

    if (FLEXCAN_STATUS_SUCCESS == eResult)
    {
    #if (FLEXCAN_IP_FEATURE_HAS_PE_CLKSRC_SELECT == STD_ON)
        /* Select a source clock for the FlexCAN engine */
        FlexCAN_SetClkSrc(pBase, pConfigData->is_pe_clock);
    #endif
        /* Enable FlexCAN Module need to perform SoftReset & ClearRam */
        pBase->MCR &= ~FLEXCAN_MCR_MDIS_MASK;
        /* Initialize FLEXCAN device */
        eResult = FlexCAN_Init(pBase);
        if (eResult != FLEXCAN_STATUS_SUCCESS)
        {
            /* To enter Disable Mode requires FreezMode first */
            (void)FlexCAN_EnterFreezeMode(pBase);
            (void)FlexCAN_Disable(pBase);
        }
        else
        {
        #if (FLEXCAN_IP_FEATURE_HAS_MEM_ERR_DET == STD_ON)
            /* Disable the Protection again because is enabled by soft reset */
            FlexCAN_DisableMemErrorDetection(pBase);
        #endif

        #if defined(FLEXCAN_IP_FEATURE_FD_NOT_ALL_INSTANCES)
            if (TRUE == FlexCAN_IsFDAvailable(pBase))
        #endif
            {
                /* Enable/Disable FD and check FD was set as expected. Setting FD as enabled
                 * might fail if the current CAN instance does not support FD. */
                FlexCAN_SetFDEnabled(pBase, pConfigData->fd_enable, pConfigData->bitRateSwitch);
                /* No more required, I don't expect any other NPIs with and without Interfaces with FD support
                 * if (FLEXCAN_IsFDEnabled(pBase) != pConfigData->fd_enable)
                {
                    return FLEXCAN_STATUS_ERROR;
                }*/
            }
            /* configure depends on controller options. */
            FlexCAN_ConfigCtrlOptions(pBase, pConfigData->ctrlOptions);
            /* reset Imask buffers */
            FlexCAN_ResetImaskBuff(Instance);
            eResult = FlexCAN_InitCtroll(pBase, pConfigData);
        }
    }
    return eResult;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_InitBaudrate
 * Description   : Init baudrate for given controller.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static void FlexCAN_InitBaudrate(FLEXCAN_Type * pBase, const Flexcan_Ip_ConfigType * pConfigData)
{
    /* Enable the use of extended bit time definitions */
    FlexCAN_EnableExtCbt(pBase, pConfigData->fd_enable);

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
    if (pConfigData->enhCbtEnable)
    {
        /* Enable Enhanced CBT time segments */
        pBase->CTRL2 |= FLEXCAN_CTRL2_BTE_MASK;
        FlexCAN_SetEnhancedNominalTimeSegments(pBase, &pConfigData->bitrate);
#if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
        if (pConfigData->fd_enable)
        {
            FlexCAN_SetEnhancedDataTimeSegments(pBase, &pConfigData->bitrate_cbt);
        }
#endif
    }
    else
#endif    /* End of (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)  */
    {
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
        /* Disable Enhanced CBT time segments */
        pBase->CTRL2 &= ~FLEXCAN_CTRL2_BTE_MASK;
#endif
#if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
        /* Set bit rate. */
        if (pConfigData->fd_enable)
        {
            /* Write Normal bit time configuration to CBT register */
            FlexCAN_SetExtendedTimeSegments(pBase, &pConfigData->bitrate);
            /* Write Data bit time configuration to FDCBT register */
            FlexCAN_SetFDTimeSegments(pBase, &pConfigData->bitrate_cbt);
        }
        else
        {
            /* Write Normal bit time configuration to CTRL1 register */
            FlexCAN_SetTimeSegments(pBase, &pConfigData->bitrate);
        }
#endif
    }
}

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
/*FUNCTION**********************************************************************
 *
 * Function Name : FLEXCAN_CompleteRxMessageEnhancedFifoData
 * Description   : Finish up a receive by completing the process of receiving
 * data and disabling the interrupt.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static void FlexCAN_CompleteRxMessageEnhancedFifoData(uint8 Instance)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(FlexCAN_IsEnhancedRxFifoAvailable(pBase));
#endif /* (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON) */

    if (FALSE == pState->enhancedFifoOutput.isPolling)
    {
        /* Reset to default value to avoid re-enable when calling Ip_EnableInterrupt */
        pState->enhancedFifoOutput.isPolling = TRUE;
        /* Disable Enhanced RX FIFO interrupts*/
        FlexCAN_SetEnhancedRxFifoIntAll(pBase, FALSE);
    }
    else
    {
        /* avoid misra */
    }
    /* Clear enhanced rx fifo message*/
    pState->enhancedFifoOutput.pMBmessage = NULL_PTR;

    pState->enhancedFifoOutput.state = FLEXCAN_MB_IDLE;
}

#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_ReadEnhancedRxFifoDma
 * Description   : Update MB message after completing the process of receiving data.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/ 
static void FlexCAN_ReadEnhancedRxFifoDma(uint8 Instance)
{
    Flexcan_Ip_MsgBuffType * FifoMessage = NULL_PTR;
    uint32 u32MbCnt = 0U;
    uint8 Idx;
    uint8 IdHitOffset = 0U;
    uint32 * MsgData32 = NULL_PTR;
    uint8 FlexcanMbDlcValue = 0U;
    uint8 FlexcanDlcPayload = 0U;
    uint8 FlexcanRealPayload = 0U;

    FifoMessage = Flexcan_Ip_apxState[Instance]->enhancedFifoOutput.pMBmessage;

    for (u32MbCnt = 0U; u32MbCnt < Flexcan_Ip_apxState[Instance]->u32NumOfMbTransferByDMA; u32MbCnt++)
    {
        MsgData32 = &((uint32 *)((Flexcan_Ip_PtrSizeType)FifoMessage))[2U];
        /* Adjust the ID if it is not extended */
        if (0U == ((FifoMessage->cs) & FLEXCAN_IP_CS_IDE_MASK))
        {
            FifoMessage->msgId = FifoMessage->msgId >> FLEXCAN_IP_ID_STD_SHIFT;
        }

        /* Extract the data length */
        FlexcanMbDlcValue = (uint8)((FifoMessage->cs & FLEXCAN_IP_CS_DLC_MASK) >> FLEXCAN_IP_CS_DLC_SHIFT);
        FlexcanDlcPayload = FlexCAN_ComputePayloadSize(FlexcanMbDlcValue);
        FlexcanRealPayload = 0U;
        /* Extract the IDHIT and Time Stamp */
        if ((FifoMessage->cs & FLEXCAN_IP_CS_RTR_MASK) != 0U)
        {
            FlexcanRealPayload = 0U;
        }
        else
        {
            FlexcanRealPayload = FlexcanDlcPayload;
        }

        IdHitOffset = (FlexcanRealPayload >> 2U) + (((FlexcanRealPayload % 4U) != 0U) ? 1U : 0U);
#if (FLEXCAN_IP_FEATURE_HAS_HR_TIMER == STD_ON)
        /* Extract the Time Stamp */
        if (FLEXCAN_IsHRTimeStampEnabled(Flexcan_Ip_apxBase[Instance]))
        {
            FifoMessage->time_stamp = (uint32)(MsgData32[IdHitOffset + 1U]);
        }
        else
#endif /* FLEXCAN_IP_FEATURE_HAS_HR_TIMER */
        {
            FifoMessage->time_stamp = (uint32)((FifoMessage->cs & FLEXCAN_IP_CS_TIME_STAMP_MASK) >> FLEXCAN_IP_CS_TIME_STAMP_SHIFT);
        }

        /* Extract the IDHIT */
        FifoMessage->id_hit = (uint8)(((MsgData32[IdHitOffset]) & FLEXCAN_IP_ENHANCED_IDHIT_MASK) >> FLEXCAN_IP_ENHANCED_IDHIT_SHIFT);
        /* Extract the dataLen */
        FifoMessage->dataLen = FlexcanDlcPayload;
        /* Reverse the endianness */
        for (Idx = 0U; Idx < IdHitOffset; Idx++)
        {
            FLEXCAN_IP_SWAP_BYTES_IN_WORD(MsgData32[Idx], MsgData32[Idx]);
        }

        FifoMessage++;
    }    
}
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_CompleteRxMessageEnhancedFifoDataDma
 * Description   : Finish up a receive by completing the process of receiving
 * data and clear the DMA request.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static void FlexCAN_CompleteRxMessageEnhancedFifoDataDma(uint8 Instance)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];
    Dma_Ip_LogicChannelStatusType DmaStatus;

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(FlexCAN_IsEnhancedRxFifoAvailable(pBase));
#endif /* (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON) */

    if (FLEXCAN_RXFIFO_USING_DMA == pState->transferType)
    {
        DmaStatus.ChStateValue = DMA_IP_CH_ERROR_STATE;
        (void)Dma_Ip_GetLogicChannelStatus(pState->rxFifoDMAChannel, &DmaStatus);

        if (DMA_IP_CH_ERROR_STATE == DmaStatus.ChStateValue)
        {
            (void)Dma_Ip_SetLogicChannelCommand(pState->rxFifoDMAChannel, DMA_IP_CH_CLEAR_ERROR);
            pState->enhancedFifoOutput.state = FLEXCAN_MB_DMA_ERROR;
        }

        (void)Dma_Ip_SetLogicChannelCommand(pState->rxFifoDMAChannel, DMA_IP_CH_CLEAR_HARDWARE_REQUEST);

        if (pState->enhancedFifoOutput.state != FLEXCAN_MB_DMA_ERROR)
        {
            
            FlexCAN_ReadEnhancedRxFifoDma(Instance);
        }
        else
        {
            /* If Enhanced Rx FIFO has Pending Request that generated error,
             * the EnhancedRxFIFO need to be empty to activate DMA */
            FlexCAN_ClearOutputEnhanceFIFO(pBase);
        }
    }
    else
    {
        /* avoid misra */
    }

    /* Clear enhanced rx fifo message*/
    pState->enhancedFifoOutput.pMBmessage = NULL_PTR;

    if (pState->enhancedFifoOutput.state != FLEXCAN_MB_DMA_ERROR)
    {
        pState->enhancedFifoOutput.state = FLEXCAN_MB_IDLE;
        if ((pState->callback != NULL_PTR) && (FLEXCAN_RXFIFO_USING_DMA == pState->transferType))
        {
            pState->callback(Instance, FLEXCAN_EVENT_DMA_COMPLETE, FLEXCAN_IP_MB_ENHANCED_RXFIFO, pState);
        }
    }
    else
    {
        if (pState->callback != NULL_PTR)
        {
            pState->callback((uint8)Instance, FLEXCAN_EVENT_DMA_ERROR, FLEXCAN_IP_MB_ENHANCED_RXFIFO, pState);
        }
    }
}
#endif /* FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */

/*FUNCTION**********************************************************************
 *
 * Function Name : FLEXCAN_StartRxMessageEnhancedFifoData
 * Description   : Initiate (start) a receive by beginning the process of
 * receiving data and enabling the interrupt.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static Flexcan_Ip_StatusType FlexCAN_StartRxMessageEnhancedFifoData(uint8 Instance, Flexcan_Ip_MsgBuffType * Data)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];
    Flexcan_Ip_StatusType eResult = FLEXCAN_STATUS_SUCCESS;
#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
    Dma_Ip_ReturnType eDmaStatus;
    Dma_Ip_LogicChannelTransferListType axTransferList[FLEXCAN_IP_ENHANCE_TRASNFER_DIMENSION_LIST];
#endif /* (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON) */

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif /* (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON) */

    /* Start receiving fifo */
    if (FLEXCAN_MB_RX_BUSY == pState->enhancedFifoOutput.state)
    {
        eResult = FLEXCAN_STATUS_BUSY;
    }
    else
    {
        pState->enhancedFifoOutput.state = FLEXCAN_MB_RX_BUSY;
        /* This will get filled by the interrupt handler */
        pState->enhancedFifoOutput.pMBmessage = Data;
        if (FLEXCAN_RXFIFO_USING_INTERRUPTS == pState->transferType)
        {
            pState->enhancedFifoOutput.isPolling = FALSE;
            if (TRUE == pState->isIntActive)
            {
                /* Enable All Enhanced RX FIFO interrupts*/
                FlexCAN_SetEnhancedRxFifoIntAll(pBase, TRUE);
            }
        }
        if (FLEXCAN_RXFIFO_USING_POLLING == pState->transferType)
        {
            pState->enhancedFifoOutput.isPolling = TRUE;
        }

    #if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
        if (FLEXCAN_RXFIFO_USING_DMA == pState->transferType)
        {
            /* reset to default to avoid enabling interrupt */
            pState->enhancedFifoOutput.isPolling = TRUE;
            /* initialize Dma logic channel transfer list */
            axTransferList[0].Param = DMA_IP_CH_SET_SOURCE_ADDRESS;
            axTransferList[0].Value = ((Flexcan_Ip_PtrSizeType)pBase + (uint32)FLEXCAN_IP_FEATURE_ENHANCED_FIFO_RAM_OFFSET);

            axTransferList[1].Param = DMA_IP_CH_SET_SOURCE_SIGNED_OFFSET ;
            axTransferList[1].Value = 4;

            axTransferList[2].Param = DMA_IP_CH_SET_SOURCE_TRANSFER_SIZE;
            axTransferList[2].Value = DMA_IP_TRANSFER_SIZE_4_BYTE;

            axTransferList[3].Param = DMA_IP_CH_SET_DESTINATION_ADDRESS;
            axTransferList[3].Value = (Flexcan_Ip_PtrSizeType)(pState->enhancedFifoOutput.pMBmessage);

            axTransferList[4].Param = DMA_IP_CH_SET_DESTINATION_SIGNED_OFFSET;
            axTransferList[4].Value = 4;

            axTransferList[5].Param = DMA_IP_CH_SET_DESTINATION_TRANSFER_SIZE;
            axTransferList[5].Value = DMA_IP_TRANSFER_SIZE_4_BYTE;

            axTransferList[6].Param = DMA_IP_CH_SET_MINORLOOP_SIZE;
            axTransferList[6].Value = 80;

            axTransferList[7].Param = DMA_IP_CH_SET_MAJORLOOP_COUNT;
            axTransferList[7].Value = pState->u32NumOfMbTransferByDMA;

            axTransferList[8].Param = DMA_IP_CH_SET_CONTROL_EN_MAJOR_INTERRUPT;
            axTransferList[8].Value = 1;

            axTransferList[9].Param = DMA_IP_CH_SET_CONTROL_DIS_AUTO_REQUEST;
            axTransferList[9].Value = 1;

            axTransferList[10].Param = DMA_IP_CH_SET_MINORLOOP_EN_SRC_OFFSET;
            axTransferList[10].Value = 1; /* enable for src address: after each minor loop, jump back to output of enhance fifo */

            axTransferList[11].Param = DMA_IP_CH_SET_MINORLOOP_EN_DST_OFFSET;
            axTransferList[11].Value = 0; /* disable for dst address: after each minor loop: standing on next element of pMBmessage array */

            axTransferList[12].Param = DMA_IP_CH_SET_MINORLOOP_SIGNED_OFFSET;
            axTransferList[12].Value = (uint32)((sint32)(-80)); /* enable for src address: after each minor loop, jump back to output of enhance fifo */

            eDmaStatus = Dma_Ip_SetLogicChannelTransferList(pState->rxFifoDMAChannel, &axTransferList[0], FLEXCAN_IP_ENHANCE_TRASNFER_DIMENSION_LIST);

            if (eDmaStatus == DMA_IP_STATUS_SUCCESS)
            {
                eDmaStatus = Dma_Ip_SetLogicChannelCommand(pState->rxFifoDMAChannel, DMA_IP_CH_SET_HARDWARE_REQUEST);
            }
            if (eDmaStatus != DMA_IP_STATUS_SUCCESS)
            {
                pState->enhancedFifoOutput.state = FLEXCAN_MB_IDLE;
                eResult = FLEXCAN_STATUS_ERROR;
            }
        }
    #endif /* if FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */
    }
    return eResult;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_ProccessEnhancedRxFifoBlocking
 * Description   : This function will process the enhanced RxFIFO in blocking mode.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static Flexcan_Ip_StatusType FlexCAN_ProccessEnhancedRxFifoBlocking(uint8 u8Instance, uint32 u32TimeoutMs)
{
    Flexcan_Ip_StatusType eResult = FLEXCAN_STATUS_SUCCESS;
    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[u8Instance];
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
    uint32 TimeStart = 0U;
    uint32 TimeElapsed = 0U;
    uint32 Ms2Ticks = OsIf_MicrosToTicks((u32TimeoutMs * 1000U), FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
    uint32 u32IntType = 0U;
    
    TimeStart = OsIf_GetCounter(FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
    while (FLEXCAN_MB_RX_BUSY == pState->enhancedFifoOutput.state)
    {
        if (FLEXCAN_RXFIFO_USING_POLLING == pState->transferType)
        {
            for (u32IntType = FLEXCAN_IP_ENHANCED_RXFIFO_UNDERFLOW; \
                 u32IntType >= FLEXCAN_IP_ENHANCED_RXFIFO_FRAME_AVAILABLE; \
                 u32IntType--)
            {
                if (FlexCAN_GetEnhancedRxFIFOStatusFlag(pBase, u32IntType) != 0U)
                {
                    FlexCAN_IRQHandlerEnhancedRxFIFO(u8Instance, u32IntType);
                }
            }
        }

        TimeElapsed += OsIf_GetElapsed(&TimeStart, FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
        if (TimeElapsed >= Ms2Ticks)
        {
            eResult = FLEXCAN_STATUS_TIMEOUT;
            break;
        }
    }
    
    return eResult;
}
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_ProccessEnhancedRxFifo
 * Description   : This function will process the enhanced RxFIFO in blocking mode.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static Flexcan_Ip_StatusType FlexCAN_ProccessEnhancedRxFifo(uint8 u8Instance, uint32 u32TimeoutMs)
{
    Flexcan_Ip_StatusType eResult = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[u8Instance];
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
        
    /* Handle Poling method in Enhance Rx FiFo */
    eResult = FlexCAN_ProccessEnhancedRxFifoBlocking(u8Instance, u32TimeoutMs);

    if ((FLEXCAN_STATUS_TIMEOUT == eResult) && (FLEXCAN_RXFIFO_USING_POLLING != pState->transferType))
    {
        /* Disable Enhanced RX FIFO interrupts*/
        FlexCAN_SetEnhancedRxFifoIntAll(pBase, FALSE);
#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
        /* Check if transfer is done over DMA and stop transfer */
        if ((FLEXCAN_MB_RX_BUSY == pState->enhancedFifoOutput.state) && (FLEXCAN_RXFIFO_USING_DMA == pState->transferType))
        {
            (void)Dma_Ip_SetLogicChannelCommand(pState->rxFifoDMAChannel, DMA_IP_CH_CLEAR_HARDWARE_REQUEST);
            /* Reset Entire Enhance FIFO if timeout occurred */
            if (FLEXCAN_MB_RX_BUSY == pState->enhancedFifoOutput.state)
            {
            #if ((STD_ON == FLEXCAN_IP_ENABLE_USER_MODE_SUPPORT) && (STD_OFF == FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE))
                OsIf_Trusted_Call1param(FlexCAN_ClearOutputEnhanceFIFO, pBase);
            #else
                FlexCAN_ClearOutputEnhanceFIFO(pBase);
            #endif
            }
        }
#endif /* if FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */
    }

    switch (pState->enhancedFifoOutput.state)
    {
        case FLEXCAN_MB_RX_BUSY:
            pState->enhancedFifoOutput.state = FLEXCAN_MB_IDLE;
            break;
        case FLEXCAN_MB_IDLE:
            eResult = FLEXCAN_STATUS_SUCCESS;
            break;
#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
        case FLEXCAN_MB_DMA_ERROR:
            eResult = FLEXCAN_STATUS_ERROR;
            break;
#endif /* if FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */
        default:
            eResult = FLEXCAN_STATUS_ERROR;
            break;
    }

    return eResult;
}
#endif /* FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO */

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_ProccessLegacyRxFIFO
 * Description   : This function will process the enhanced RxFIFO in blocking mode.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static Flexcan_Ip_StatusType FlexCAN_ProccessLegacyRxFIFO(uint8 u8Instance, uint32 u32TimeoutMs)
{
    Flexcan_Ip_StatusType eResult = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[u8Instance];
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
    uint32 TimeStart = 0U;
    uint32 TimeElapsed = 0U;
    uint32 Ms2Ticks = OsIf_MicrosToTicks((u32TimeoutMs * 1000U), FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
    uint32 u32IntType = 0U;

        TimeStart = OsIf_GetCounter(FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);

        while (FLEXCAN_MB_RX_BUSY == pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state)
        {
            if (FLEXCAN_RXFIFO_USING_POLLING == pState->transferType)
            {
                for (u32IntType = FLEXCAN_IP_LEGACY_RXFIFO_OVERFLOW; \
                     u32IntType >= FLEXCAN_IP_LEGACY_RXFIFO_FRAME_AVAILABLE; \
                     u32IntType--)
                {
                    if (FlexCAN_GetMsgBuffIntStatusFlag(pBase, u32IntType) != 0U)
                    {
                        FlexCAN_IRQHandlerRxFIFO(u8Instance, u32IntType);
                    }
                }
            }

            TimeElapsed += OsIf_GetElapsed(&TimeStart, FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
            if (TimeElapsed >= Ms2Ticks)
            {
                eResult = FLEXCAN_STATUS_TIMEOUT;
                break;
            }
        }

        if ((FLEXCAN_STATUS_TIMEOUT == eResult) && (FLEXCAN_RXFIFO_USING_POLLING != pState->transferType))
        {
            /* Disable RX FIFO interrupts*/
            (void)FlexCAN_SetMsgBuffIntCmd(pBase, u8Instance, FLEXCAN_IP_LEGACY_RXFIFO_FRAME_AVAILABLE, FALSE, pState->isIntActive);
            (void)FlexCAN_SetMsgBuffIntCmd(pBase, u8Instance, FLEXCAN_IP_LEGACY_RXFIFO_WARNING, FALSE, pState->isIntActive);
            (void)FlexCAN_SetMsgBuffIntCmd(pBase, u8Instance, FLEXCAN_IP_LEGACY_RXFIFO_OVERFLOW, FALSE, pState->isIntActive);
#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
            /* Check if transfer is done over DMA and stop transfer */
            if ((FLEXCAN_MB_RX_BUSY == pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state) && (FLEXCAN_RXFIFO_USING_DMA == pState->transferType))
            {
                /* This function always return status success */
                (void)Dma_Ip_SetLogicChannelCommand(pState->rxFifoDMAChannel, DMA_IP_CH_CLEAR_HARDWARE_REQUEST);
            }
#endif
        }

        switch (pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state)
        {
            case FLEXCAN_MB_RX_BUSY:
                pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state = FLEXCAN_MB_IDLE;
                break;
            case FLEXCAN_MB_IDLE:
                eResult = FLEXCAN_STATUS_SUCCESS;
                break;
#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
            case FLEXCAN_MB_DMA_ERROR:
                eResult = FLEXCAN_STATUS_ERROR;
                break;
#endif /* if FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */
            default:
                eResult = FLEXCAN_STATUS_ERROR;
                break;
        }

    return eResult;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FLEXCAN_DRV_StartRxMessageBufferData
 * Description   : Initiate (start) a receive by beginning the process of
 * receiving data and enabling the interrupt.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static Flexcan_Ip_StatusType FlexCAN_StartRxMessageBufferData(uint8 Instance,
                                                              uint8 MbIdx,
                                                              Flexcan_Ip_MsgBuffType * Data,
                                                              boolean IsPolling
                                                             )
{
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
#endif
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);

    if (TRUE == FlexCAN_IsMbOutOfRange(pBase, MbIdx, pState->bIsLegacyFifoEn, pState->u32MaxMbNum))
    {
        Result = FLEXCAN_STATUS_BUFF_OUT_OF_RANGE;
    }
    else
    {
#endif
        /* Start receiving mailbox */
        if (pState->mbs[MbIdx].state != FLEXCAN_MB_IDLE)
        {
            Result = FLEXCAN_STATUS_BUSY;
        }
        else
        {
            pState->mbs[MbIdx].state = FLEXCAN_MB_RX_BUSY;
            pState->mbs[MbIdx].pMBmessage = Data;
            pState->mbs[MbIdx].isPolling = IsPolling;
        }
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    }
#endif
    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FLEXCAN_DRV_StartSendData
 * Description   : Initiate (start) a transmit by beginning the process of
 * sending data.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static Flexcan_Ip_StatusType FlexCAN_StartSendData(uint8 Instance,
                                                   uint8 MbIdx,
                                                   const Flexcan_Ip_DataInfoType * TxInfo,
                                                   uint32 MsgId,
                                                   const uint8 * MbData
                                                  )
{
    Flexcan_Ip_StatusType eResult = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_MsbuffCodeStatusType Cs;
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    volatile uint32 * pMbAddr = NULL_PTR;

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(TxInfo != NULL_PTR);
#if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
    /* Check if the Payload Size is smaller than the payload configured */
    DevAssert(((uint8)TxInfo->data_length) <= FlexCAN_GetMbPayloadSize(pBase, MbIdx));
#else
    DevAssert(((uint8)TxInfo->data_length) <= 8U);
#endif

    if (TRUE == FlexCAN_IsMbOutOfRange(pBase, MbIdx, pState->bIsLegacyFifoEn, pState->u32MaxMbNum))
    {
        eResult = FLEXCAN_STATUS_BUFF_OUT_OF_RANGE;
    }
    else
    {
#endif
        if (pState->mbs[MbIdx].state == FLEXCAN_MB_TX_BUSY)
        {
            eResult = FLEXCAN_STATUS_BUSY;
        }
        else
        {
            /* Clear message buffer flag */
            FlexCAN_ClearMsgBuffIntStatusFlag(pBase, MbIdx);

            pState->mbs[MbIdx].state = FLEXCAN_MB_TX_BUSY;
            pState->mbs[MbIdx].time_stamp = 0U;
            pState->mbs[MbIdx].isPolling = TxInfo->is_polling;
            pState->mbs[MbIdx].isRemote = TxInfo->is_remote;

            Cs.dataLen = TxInfo->data_length;

            Cs.msgIdType = TxInfo->msg_id_type;

        #if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
            Cs.fd_enable = TxInfo->fd_enable;
            Cs.fd_padding = TxInfo->fd_padding;
            Cs.enable_brs = TxInfo->enable_brs;
        #endif

            if (TxInfo->is_remote)
            {
                Cs.code = (uint32)FLEXCAN_TX_REMOTE;
            }
            else
            {
                Cs.code = (uint32)FLEXCAN_TX_DATA;
            }
            pMbAddr = FlexCAN_GetMsgBuffRegion(pBase, MbIdx);
            FlexCAN_SetTxMsgBuff(pMbAddr, &Cs, MsgId, MbData, FALSE);
        }
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    }
#endif
    return eResult;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FLEXCAN_StartRxMessageFifoData
 * Description   : Initiate (start) a receive by beginning the process of
 * receiving data and enabling the interrupt.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static Flexcan_Ip_StatusType FlexCAN_StartRxMessageFifoData(uint8 Instance, Flexcan_Ip_MsgBuffType * Data)
{

    FLEXCAN_Type * pBase = NULL_PTR;
    Flexcan_Ip_StateType * pState = NULL_PTR;
    Flexcan_Ip_StatusType eResult = FLEXCAN_STATUS_SUCCESS;
#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
    Dma_Ip_ReturnType eDmaStatus;
    Dma_Ip_LogicChannelTransferListType axTransferList[FLEXCAN_IP_TRASNFER_DIMENSION_LIST];
#endif /* (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON) */

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif

    pBase = Flexcan_Ip_apxBase[Instance];
    pState = Flexcan_Ip_apxState[Instance];

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    /* Check if RxFIFO feature is enabled */
    if (FALSE == pState->bIsLegacyFifoEn)
    {
        eResult = FLEXCAN_STATUS_ERROR;
    }
    else
    {
#endif

#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
    if (FLEXCAN_RXFIFO_USING_DMA == pState->transferType)
    {
        if (FLEXCAN_MB_DMA_ERROR == pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state)
        {
            /* Check if FIFO has Pending Request that generated error,
            * the RxFIFO need to be empty to activate DMA */
        #if ((STD_ON == FLEXCAN_IP_ENABLE_USER_MODE_SUPPORT) && (STD_OFF == FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE))
            OsIf_Trusted_Call1param(FlexCAN_ClearOutputLegacyFIFO, pBase);
        #else
            FlexCAN_ClearOutputLegacyFIFO(pBase);
        #endif
        }
    #ifdef MCAL_ENABLE_FAULT_INJECTION
        /* Fault injection point to test dma error event */
        MCAL_FAULT_INJECTION_POINT(CAN_FIP_0_DMA_ERROR_EVENT);
    #endif
    }
#endif /* FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */

    /* Start receiving fifo */
    if (pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state == FLEXCAN_MB_RX_BUSY)
    {
        eResult = FLEXCAN_STATUS_BUSY;
    }
    else
    {
        pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state = FLEXCAN_MB_RX_BUSY;
        if (FLEXCAN_RXFIFO_USING_POLLING == pState->transferType)
        {
            pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].isPolling = TRUE;
        }

        /* This will get filled by the interrupt handler */
        pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].pMBmessage = Data;

        if (FLEXCAN_RXFIFO_USING_INTERRUPTS == pState->transferType)
        {
            pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].isPolling = FALSE;
            /* Enable RX FIFO interrupts*/
            (void)FlexCAN_SetMsgBuffIntCmd(pBase, Instance, FLEXCAN_IP_LEGACY_RXFIFO_WARNING, TRUE, pState->isIntActive);
            (void)FlexCAN_SetMsgBuffIntCmd(pBase, Instance, FLEXCAN_IP_LEGACY_RXFIFO_OVERFLOW, TRUE, pState->isIntActive);
            (void)FlexCAN_SetMsgBuffIntCmd(pBase, Instance, FLEXCAN_IP_LEGACY_RXFIFO_FRAME_AVAILABLE, TRUE, pState->isIntActive);
        }
    #if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
        if (FLEXCAN_RXFIFO_USING_DMA == pState->transferType)
        {
            /* initialize Dma logic channel transfer list */
            axTransferList[0].Param = DMA_IP_CH_SET_SOURCE_ADDRESS;
            axTransferList[0].Value = ((Flexcan_Ip_PtrSizeType)pBase + (uint32)FLEXCAN_IP_FEATURE_RAM_OFFSET);

            axTransferList[1].Param = DMA_IP_CH_SET_SOURCE_SIGNED_OFFSET ;
            axTransferList[1].Value = 4;

            axTransferList[2].Param = DMA_IP_CH_SET_SOURCE_TRANSFER_SIZE;
            axTransferList[2].Value = DMA_IP_TRANSFER_SIZE_4_BYTE;

            axTransferList[3].Param = DMA_IP_CH_SET_DESTINATION_ADDRESS;
            axTransferList[3].Value = (Flexcan_Ip_PtrSizeType)(pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].pMBmessage);

            axTransferList[4].Param = DMA_IP_CH_SET_DESTINATION_SIGNED_OFFSET;
            axTransferList[4].Value = 4;

            axTransferList[5].Param = DMA_IP_CH_SET_DESTINATION_TRANSFER_SIZE;
            axTransferList[5].Value = DMA_IP_TRANSFER_SIZE_4_BYTE;

            axTransferList[6].Param = DMA_IP_CH_SET_MINORLOOP_SIZE;
            axTransferList[6].Value = 16;

            axTransferList[7].Param = DMA_IP_CH_SET_MAJORLOOP_COUNT;
            axTransferList[7].Value = 1;

            axTransferList[8].Param = DMA_IP_CH_SET_CONTROL_EN_MAJOR_INTERRUPT;
            axTransferList[8].Value = 1;

            axTransferList[9].Param = DMA_IP_CH_SET_CONTROL_DIS_AUTO_REQUEST;
            axTransferList[9].Value = 1;

            eDmaStatus = Dma_Ip_SetLogicChannelTransferList(pState->rxFifoDMAChannel, &axTransferList[0], FLEXCAN_IP_TRASNFER_DIMENSION_LIST);

            if (eDmaStatus == DMA_IP_STATUS_SUCCESS)
            {
                eDmaStatus = Dma_Ip_SetLogicChannelCommand(pState->rxFifoDMAChannel, DMA_IP_CH_SET_HARDWARE_REQUEST);
            }
            if (eDmaStatus != DMA_IP_STATUS_SUCCESS)
            {
                pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state = FLEXCAN_MB_IDLE;
                eResult = FLEXCAN_STATUS_ERROR;
            }
        }
    #endif /* if FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */
    }
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    }
#endif
    return eResult;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_IRQHandlerRxMB
 * Description   : Process IRQHandler in case of Rx MessageBuffer selection
 * for CAN interface.
 *
 * This is not a public API as it is called whenever an interrupt and receive
 * individual MB occurs
 *END**************************************************************************/
static void FlexCAN_IRQHandlerRxMB(uint8 Instance, uint32 MbIdx)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];
    Flexcan_Ip_MsgBuffType Data;
    boolean bCurrentIntStat = FALSE;

    /* If use pass NULL_PTR, they can get data in callback function by getting pState->mbs[MbIdx].pMBmessage  */
    if (NULL_PTR == pState->mbs[MbIdx].pMBmessage)
    {
        pState->mbs[MbIdx].pMBmessage = &Data;
    }

#if (defined (ERR_IPV_FLEXCAN_E050246) || defined (ERR_IPV_FLEXCAN_E050630))
    boolean bIsCriticalSectionNeeded = FALSE;

    /* Expectation: the sequence will not be interrupted when it already in interupt context */
    if (TRUE == pState->mbs[MbIdx].isPolling)
    {
    #if (defined (ERR_IPV_FLEXCAN_E050246) && defined (ERR_IPV_FLEXCAN_E050630))
        #if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
        if ((pState->bIsLegacyFifoEn) || (pState->bIsEnhancedFifoEn && (0U != (pBase->CTRL2 & FLEXCAN_CTRL2_TSTAMPCAP_MASK))))
        #else
        if (pState->bIsLegacyFifoEn)
        #endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON) */
    #elif (defined (ERR_IPV_FLEXCAN_E050630))
        #if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
        if ((pState->bIsLegacyFifoEn ||  pState->bIsEnhancedFifoEn) && (0U != (pBase->CTRL2 & FLEXCAN_CTRL2_TSTAMPCAP_MASK)))
        #else
        if ((pState->bIsLegacyFifoEn) && (0U != (pBase->CTRL2 & FLEXCAN_CTRL2_TSTAMPCAP_MASK)))
        #endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON) */
    #elif defined (ERR_IPV_FLEXCAN_E050246)
        if (pState->bIsLegacyFifoEn)
    #endif
        {
            bIsCriticalSectionNeeded = TRUE;
            /* Disable all IRQs */
            OsIf_SuspendAllInterrupts();
        }
    }
#endif /* (defined(ERR_IPV_FLEXCAN_E050246) || defined(ERR_IPV_FLEXCAN_E050630)) */

    /* Lock RX message buffer and RX FIFO*/
    FlexCAN_LockRxMsgBuff(pBase, MbIdx);

    /* Get RX MB field values*/
    FlexCAN_GetMsgBuff(pBase, MbIdx, pState->mbs[MbIdx].pMBmessage);

    FlexCAN_ClearMsgBuffIntStatusFlag(pBase, MbIdx);

#if defined (ERR_IPV_FLEXCAN_E050246)
    /* the CODE field is updated with an incorrect value when MBx is locked by software for more than 20 CAN bit times and FIFO enable.
    When the CODE field is corrupted, it's probably updated with any value that is invalid. Except EMPTY, FULL and OVERRUN other values can not make MB unlocked and move-in process. */
    if ((pState->bIsLegacyFifoEn) && \
    ((uint32)FLEXCAN_RX_FULL != ((pState->mbs[MbIdx].pMBmessage->cs & FLEXCAN_IP_CS_CODE_MASK) >> FLEXCAN_IP_CS_CODE_SHIFT)) && \
    ((uint32)FLEXCAN_RX_EMPTY != ((pState->mbs[MbIdx].pMBmessage->cs & FLEXCAN_IP_CS_CODE_MASK) >> FLEXCAN_IP_CS_CODE_SHIFT)) && \
    ((uint32)FLEXCAN_RX_OVERRUN != ((pState->mbs[MbIdx].pMBmessage->cs & FLEXCAN_IP_CS_CODE_MASK) >> FLEXCAN_IP_CS_CODE_SHIFT)))
    {
        /* Update the cs code for next sequence move in MB.
        A CPU write into the C/S word also unlocks the MB */
        volatile uint32 *FlexcanMb = FlexCAN_GetMsgBuffRegion(pBase, MbIdx);
        *FlexcanMb &= ~FLEXCAN_IP_CS_CODE_MASK;
        *FlexcanMb |= (((uint32)FLEXCAN_RX_EMPTY) << FLEXCAN_IP_CS_CODE_SHIFT) & FLEXCAN_IP_CS_CODE_MASK;
    }
    else
#endif
    {
        /* Unlock RX message buffer and RX FIFO*/
        FlexCAN_UnlockRxMsgBuff(pBase);
    }

#if (defined (ERR_IPV_FLEXCAN_E050246) || defined (ERR_IPV_FLEXCAN_E050630))
    /* To ensure that interrupts are resumed when they are suspended */
    if (TRUE == bIsCriticalSectionNeeded)
    {
        /* Enable all IRQs */
        OsIf_ResumeAllInterrupts();
    }
#endif /* (defined(ERR_IPV_FLEXCAN_E050246) || defined(ERR_IPV_FLEXCAN_E050630)) */

    pState->mbs[MbIdx].state = FLEXCAN_MB_IDLE;

    bCurrentIntStat = pState->mbs[MbIdx].isPolling;

    /* Invoke callback */
    if (pState->callback != NULL_PTR)
    {
        pState->callback(Instance, FLEXCAN_EVENT_RX_COMPLETE, MbIdx, pState);
    }

    if ((FLEXCAN_MB_IDLE == pState->mbs[MbIdx].state) && (FALSE == pState->mbs[MbIdx].isPolling))
    {
        /* callback is not called, need to reset to default value */
        pState->mbs[MbIdx].isPolling = TRUE;
        /* Disable the transmitter data register empty interrupt for case: mb is interrupt (it was not use in above callback with the same index) */
        (void)FlexCAN_SetMsgBuffIntCmd(pBase, Instance, MbIdx, FALSE, pState->isIntActive);
    }
    else if ((FALSE == bCurrentIntStat) && (TRUE == pState->mbs[MbIdx].isPolling))
    {
        /* Disable the transmitter data register empty interrupt for case: switch from interrupt to polling for the same MB (called in above callback with same mb index) */
        (void)FlexCAN_SetMsgBuffIntCmd(pBase, Instance, MbIdx, FALSE, pState->isIntActive);
    }
    else
    {
        /* Prevent misra */
        /* When processing type change from POLL->POLL or INTERRUPT -> INTERRUPT(this Mb is used continously in callback), no need to disable interrupt in the ISR */
    }
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_IRQHandlerTxMB
 * Description   : Process IRQHandler in case of Tx MessageBuffer selection
 * for CAN interface.
 * note: just using in interrupt mode
 * This is not a public API as it is called whenever an interrupt and receive
 * individual MB occurs
 *END**************************************************************************/
static void FlexCAN_IRQHandlerTxMB(uint8 u8Instance, uint32 u32MbIdx)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[u8Instance];
    Flexcan_Ip_MsgBuffType TxBuff;
    volatile const uint32 * FlexcanMb = FlexCAN_GetMsgBuffRegion(pBase, u32MbIdx);

    TxBuff.cs = *FlexcanMb;
    TxBuff.time_stamp = FlexCAN_GetMsgBuffTimestamp(pBase, u32MbIdx);
    FlexCAN_UnlockRxMsgBuff(pBase);
    {
        if (pState->mbs[u32MbIdx].isRemote)
        {
            pState->mbs[u32MbIdx].time_stamp = TxBuff.time_stamp;
            /* If the frame was a remote frame, clear the flag only if the response was
            * not received yet. If the response was received, leave the flag set in order
            * to be handled when the user calls FlexCAN_Ip_Receive. */
            if ((uint32)FLEXCAN_RX_EMPTY == ((TxBuff.cs & FLEXCAN_IP_CS_CODE_MASK) >> FLEXCAN_IP_CS_CODE_SHIFT))
            {
                /* Clear message buffer flag */
                FlexCAN_ClearMsgBuffIntStatusFlag(pBase, u32MbIdx);
            }
        }
        else
        {
            pState->mbs[u32MbIdx].time_stamp = TxBuff.time_stamp;
            /* Clear message buffer flag */
            FlexCAN_ClearMsgBuffIntStatusFlag(pBase, u32MbIdx);
        }

        pState->mbs[u32MbIdx].state = FLEXCAN_MB_IDLE;

        /* Invoke callback */
        if (pState->callback != NULL_PTR)
        {
            pState->callback(u8Instance, FLEXCAN_EVENT_TX_COMPLETE, u32MbIdx, pState);
        }
    }

    if ((FLEXCAN_MB_IDLE == pState->mbs[u32MbIdx].state)
       )
    {
        /* In case callback wasn't called.
        *  Or callback was called but current MB wasn't started new transmission anymore (other MB might be used)
        */
        /* Need to reset to default value */
        pState->mbs[u32MbIdx].isPolling = TRUE;
        /* Disable the transmitter data register empty interrupt */
        (void)FlexCAN_SetMsgBuffIntCmd(pBase, u8Instance, u32MbIdx, FALSE, pState->isIntActive);
    }
    else if (TRUE == pState->mbs[u32MbIdx].isPolling)
    {
        /* Disable the transmitter data register empty interrupt for case: switch from interrupt to polling for the same MB (called in above callback with same mb index) */
        (void)FlexCAN_SetMsgBuffIntCmd(pBase, u8Instance, u32MbIdx, FALSE, pState->isIntActive);
    }
    else
    {
        /* Prevent misra */
        /* When processing type change from POLL->POLL or INTERRUPT -> INTERRUPT(this Mb is used continously in callback), no need to disable interrupt in the ISR */
    }
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_IRQHandlerRxFIFO
 * Description   : Process IRQHandler in case of RxFIFO mode selection for CAN interface.
 *
 *END**************************************************************************/
static inline void FlexCAN_IRQHandlerRxFIFO(uint8 Instance, uint32 MbIdx)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];
    Flexcan_Ip_MsgBuffType Data;

    /* If use pass NULL_PTR, they can get data in callback function by getting pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].pMBmessage  */
    if (NULL_PTR == pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].pMBmessage)
    {
        pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].pMBmessage = &Data;
    }
    switch (MbIdx)
    {
        case FLEXCAN_IP_LEGACY_RXFIFO_FRAME_AVAILABLE:
            if (FLEXCAN_MB_RX_BUSY == pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state)
            {
                /* Get RX FIFO field values */
                FlexCAN_ReadRxFifo(pBase, pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].pMBmessage);

                FlexCAN_ClearMsgBuffIntStatusFlag(pBase, MbIdx);

                pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state = FLEXCAN_MB_IDLE;

                /* Invoke callback */
                if (pState->callback != NULL_PTR)
                {
                    pState->callback(Instance, FLEXCAN_EVENT_RXFIFO_COMPLETE, FLEXCAN_IP_MB_HANDLE_RXFIFO, pState);
                }

                if (FLEXCAN_MB_IDLE == pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state)
                {
                    /* reset to default value */
                    pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].isPolling = TRUE;
                    /* Complete receive data */
                    FlexCAN_CompleteRxMessageFifoData(Instance);
                }
            }

            break;
        case FLEXCAN_IP_LEGACY_RXFIFO_WARNING:
            FlexCAN_ClearMsgBuffIntStatusFlag(pBase, MbIdx);

            /* Invoke callback */
            if (pState->callback != NULL_PTR)
            {
                pState->callback(Instance, FLEXCAN_EVENT_RXFIFO_WARNING, FLEXCAN_IP_MB_HANDLE_RXFIFO, pState);
            }

            break;
        case FLEXCAN_IP_LEGACY_RXFIFO_OVERFLOW:
            FlexCAN_ClearMsgBuffIntStatusFlag(pBase, MbIdx);

            /* Invoke callback */
            if (pState->callback != NULL_PTR)
            {
                pState->callback(Instance, FLEXCAN_EVENT_RXFIFO_OVERFLOW, FLEXCAN_IP_MB_HANDLE_RXFIFO, pState);
            }

            break;
        default:
            /* Do Nothing */
            break;
    }
}

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_ON)
void FlexCAN_EnhancedRxFIFODataIRQHandler(uint8 u8Instance)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[u8Instance];
    uint32 u32IntType = 0U;

    if (NULL_PTR != pState)
    {
        /* Get the interrupts that are enabled and ready */
        for (u32IntType = FLEXCAN_IP_ENHANCED_RXFIFO_WATERMARK; u32IntType >= FLEXCAN_IP_ENHANCED_RXFIFO_FRAME_AVAILABLE; u32IntType--)
        {
            if ((uint8)0U != FlexCAN_GetEnhancedRxFIFOStatusFlag(pBase, u32IntType))
            {
                if ((uint8)0U != FlexCAN_GetEnhancedRxFIFOIntStatusFlag(pBase, u32IntType))
                {
                    FlexCAN_IRQHandlerEnhancedRxFIFO(u8Instance, u32IntType);
                }
            }
        }
    }
    else
    {
        /* Clear status interrupt flag */
        FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_WATERMARK);
        FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_FRAME_AVAILABLE);
    }
}
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_ON) */
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_OFF)
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_ProcessIRQHandlerEnhancedRxFIFO
 * Description   : Process IRQHandler in case of Enhanced RxFIFO mode selection for CAN interface.
 * note: just use in FlexCAN_IRQHandler
 *END**************************************************************************/
static inline void FlexCAN_ProcessIRQHandlerEnhancedRxFIFO(uint8 u8Instance)
{
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
    uint32 u32IntType = 0U;

    /* Get the interrupts that are enabled and ready */
    for (u32IntType = FLEXCAN_IP_ENHANCED_RXFIFO_UNDERFLOW; u32IntType >= FLEXCAN_IP_ENHANCED_RXFIFO_FRAME_AVAILABLE; u32IntType--)
    {
        if ((uint8)0U != FlexCAN_GetEnhancedRxFIFOStatusFlag(pBase, u32IntType))
        {
            if ((uint8)0U != FlexCAN_GetEnhancedRxFIFOIntStatusFlag(pBase, u32IntType))
            {
                FlexCAN_IRQHandlerEnhancedRxFIFO(u8Instance, u32IntType);
            }
        }
    }
}
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_OFF) */
/*FUNCTION**********************************************************************
 *
 * Function Name : FLEXCAN_IRQHandlerEnhancedRxFIFO
 * Description   : Process IRQHandler in case of Enhanced RxFIFO mode selection for CAN interface.
 *
 * Implements    : FLEXCAN_IRQHandlerEnyhancedRxFIFO_Activity
 *END**************************************************************************/
static void FlexCAN_IRQHandlerEnhancedRxFIFO(uint8 Instance, uint32 IntType)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];
    Flexcan_Ip_MsgBuffType Data;

    switch (IntType)
    {
        case FLEXCAN_IP_ENHANCED_RXFIFO_FRAME_AVAILABLE:
            if (FLEXCAN_MB_RX_BUSY == pState->enhancedFifoOutput.state)
            {
                /* If user pass NULL_PTR, they can get data in callback function by getting pState->enhancedFifoOutput.pMBmessage  */
                if (NULL_PTR == pState->enhancedFifoOutput.pMBmessage)
                {
                    pState->enhancedFifoOutput.pMBmessage = &Data;
                }

                /* Get Enhanced RX FIFO field values */
                FlexCAN_ReadEnhancedRxFifo(Instance, pState->enhancedFifoOutput.pMBmessage);

                FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, IntType);
                FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_WATERMARK);
                FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_OVERFLOW);
                pState->enhancedFifoOutput.state = FLEXCAN_MB_IDLE;

                /* Invoke callback */
                if (pState->callback != NULL_PTR)
                {
                    pState->callback(Instance, FLEXCAN_EVENT_ENHANCED_RXFIFO_COMPLETE, FLEXCAN_IP_MB_ENHANCED_RXFIFO, pState);
                }

                if (FLEXCAN_MB_IDLE == pState->enhancedFifoOutput.state)
                {
                    /* Complete receive data */
                    FlexCAN_CompleteRxMessageEnhancedFifoData(Instance);
                }
            }

            break;
        case FLEXCAN_IP_ENHANCED_RXFIFO_WATERMARK:
            FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, IntType);

            /* Invoke callback */
            if (pState->callback != NULL_PTR)
            {
                pState->callback(Instance, FLEXCAN_EVENT_ENHANCED_RXFIFO_WATERMARK, FLEXCAN_IP_MB_ENHANCED_RXFIFO, pState);
            }

            break;
        case FLEXCAN_IP_ENHANCED_RXFIFO_OVERFLOW:
            FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, IntType);

            /* Invoke callback */
            if (pState->callback != NULL_PTR)
            {
                pState->callback(Instance, FLEXCAN_EVENT_ENHANCED_RXFIFO_OVERFLOW, FLEXCAN_IP_MB_ENHANCED_RXFIFO, pState);
            }

            break;
        case FLEXCAN_IP_ENHANCED_RXFIFO_UNDERFLOW:
            FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, IntType);

            /* Invoke callback */
            if (pState->callback != NULL_PTR)
            {
                pState->callback(Instance, FLEXCAN_EVENT_ENHANCED_RXFIFO_UNDERFLOW, FLEXCAN_IP_MB_ENHANCED_RXFIFO, pState);
            }

            break;
        default:
            /* Do Nothing */
            break;
    }
}

#endif /* FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO */

#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
/*FUNCTION**********************************************************************
 *
 * Function Name : DMA_Can_Callback
 * Description   : Callback function for DMA when using Rx Legacy FIFO and Enhanced FIFO.
 *END**************************************************************************/
static void DMA_Can_Callback(uint8 Instance)
{
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];

    if (TRUE == FlexCAN_IsEnhancedRxFifoEnabled(pBase))
    {
        FlexCAN_CompleteRxMessageEnhancedFifoDataDma(Instance);
    }
    else
#endif /* FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON */
    {
        FlexCAN_CompleteRxMessageFifoData(Instance);
    }
}
#endif /* FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON */

#if (STD_ON == FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE)
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_SetUserAccessAllowed
 * Description   : Sets the UAA bit in REG_PROT to make the instance accessible
 * in user mode.
 *
 * This is not a public API as it is called from other driver functions.
 *END**************************************************************************/
void FlexCAN_SetUserAccessAllowed(const FLEXCAN_Type * pBase)
{
    SET_USER_ACCESS_ALLOWED((uint32)pBase, FLEXCAN_PROT_MEM_U32);
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_ClrUserAccessAllowed
 * Description   : Clears the UAA bit in REG_PROT to make the instance accessible
 * in user mode.
 *
 * This is not a public API as it is called from other driver functions.
 *END**************************************************************************/
void FlexCAN_ClrUserAccessAllowed(const FLEXCAN_Type * pBase)
{
    CLR_USER_ACCESS_ALLOWED((uint32)pBase, FLEXCAN_PROT_MEM_U32);
}
#endif /* (STD_ON == FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE) */

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_AbortTxTransfer
 * Description   : Abort transfer for Tx buffer.
 *
 * This is not a public API as it is called from other driver functions.
 *END**************************************************************************/
static Flexcan_Ip_StatusType FlexCAN_AbortTxTransfer(uint8 u8Instance, uint8 MbIdx)
{
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;

    uint32 TimeStart = 0U;
    uint32 TimeElapsed = 0U;
    uint32 FlexcanMbConfig = 0;
    uint32 Us2Ticks = 0U;
    volatile uint32 * FlexcanMb = NULL_PTR;

    FlexcanMb = FlexCAN_GetMsgBuffRegion(pBase, MbIdx);
    FlexcanMbConfig = *FlexcanMb;

    {
        /* Reset the code and set to ABORT */
        FlexcanMbConfig &= (~FLEXCAN_IP_CS_CODE_MASK);
        FlexcanMbConfig |= (uint32)(((uint32)FLEXCAN_TX_ABORT & (uint32)0x1F) << (uint8)FLEXCAN_IP_CS_CODE_SHIFT) & (uint32)FLEXCAN_IP_CS_CODE_MASK;
        *FlexcanMb = FlexcanMbConfig;

        /* Wait to finish abort operation */
        Us2Ticks = OsIf_MicrosToTicks(FLEXCAN_IP_TIMEOUT_DURATION, FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
        TimeStart = OsIf_GetCounter(FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
        while (0U == FlexCAN_GetMsgBuffIntStatusFlag(pBase, MbIdx))
        {
            TimeElapsed += OsIf_GetElapsed(&TimeStart, FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
            if (TimeElapsed >= Us2Ticks)
            {
                Result = FLEXCAN_STATUS_TIMEOUT;
                break;
            }
        }
        if (Result != FLEXCAN_STATUS_TIMEOUT)
        {
            FlexcanMbConfig = *FlexcanMb;
            /* Check if the MBs have been safely Inactivated */
            if ((uint32)FLEXCAN_TX_INACTIVE == ((FlexcanMbConfig & FLEXCAN_IP_CS_CODE_MASK) >> FLEXCAN_IP_CS_CODE_SHIFT))
            {
                /* Transmission have occurred */
                Result = FLEXCAN_STATUS_NO_TRANSFER_IN_PROGRESS;
            }

            if ((uint32)FLEXCAN_TX_ABORT == ((FlexcanMbConfig & FLEXCAN_IP_CS_CODE_MASK) >> FLEXCAN_IP_CS_CODE_SHIFT))
            {
                /* Transmission have been aborted */
                Result = FLEXCAN_STATUS_SUCCESS;
            }
        }
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_AbortRxTransfer
 * Description   : Abort transfer for Rx normal or legacy fifo if enabled.
 *
 * This is not a public API as it is called from other driver functions.
 *END**************************************************************************/
static Flexcan_Ip_StatusType FlexCAN_AbortRxTransfer(uint8 u8Instance, uint8 MbIdx)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[u8Instance];
    uint8 Val1 = 0U;
    uint32 Val2 = 0U;
    uint32 FlexcanMbConfig = 0;
    volatile uint32 * FlexcanMb = NULL_PTR;
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;

    /* Check if fifo enabled */
    if (TRUE == pState->bIsLegacyFifoEn)
    {
        /* Get the number of RX FIFO Filters*/
        Val1 = (uint8)(((pBase->CTRL2) & FLEXCAN_CTRL2_RFFN_MASK) >> FLEXCAN_CTRL2_RFFN_SHIFT);
        /* Get the number if MBs occupied by RX FIFO and ID filter table*/
        /* the Rx FIFO occupies the memory space originally reserved for MB0-5*/
        /* Every number of RFFN means 8 number of RX FIFO filters*/
        /* and every 4 number of RX FIFO filters occupied one MB*/
        Val2 = RxFifoOcuppiedLastMsgBuff(Val1);
        if (MbIdx > Val2)
        {
            /* This operation is not allowed for MB that are part of RxFIFO */
            FlexcanMb = FlexCAN_GetMsgBuffRegion(pBase, MbIdx);
            FlexcanMbConfig = * FlexcanMb;
            /* Reset the code and unlock the MB */
            FlexcanMbConfig &= (uint32)(~FLEXCAN_IP_CS_CODE_MASK);
            FlexcanMbConfig |= (uint32)(((uint32)FLEXCAN_RX_INACTIVE & (uint32)0x1F) << (uint8)FLEXCAN_IP_CS_CODE_SHIFT) & (uint32)FLEXCAN_IP_CS_CODE_MASK;
            *FlexcanMb = FlexcanMbConfig;
            /* Reconfigure The MB as left by RxMBconfig */
            FlexcanMbConfig &= (~FLEXCAN_IP_CS_CODE_MASK);
            FlexcanMbConfig |= (uint32)(((uint32)FLEXCAN_RX_EMPTY & (uint32)0x1F) << (uint8)FLEXCAN_IP_CS_CODE_SHIFT) & (uint32)FLEXCAN_IP_CS_CODE_MASK;
            *FlexcanMb = FlexcanMbConfig;
        }
        if (FLEXCAN_IP_MB_HANDLE_RXFIFO == MbIdx)
        {
            FLEXCAN_ClearMsgBuffIntCmd(pBase, u8Instance, FLEXCAN_IP_LEGACY_RXFIFO_FRAME_AVAILABLE, pState->isIntActive);
        #if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
            if (FLEXCAN_RXFIFO_USING_DMA == pState->transferType)
            {
                (void)Dma_Ip_SetLogicChannelCommand(pState->rxFifoDMAChannel, DMA_IP_CH_CLEAR_HARDWARE_REQUEST);
                if (FLEXCAN_MB_RX_BUSY != pState->mbs[MbIdx].state)
                {
                    Result = FLEXCAN_STATUS_NO_TRANSFER_IN_PROGRESS;
                }
            }
        #endif
        }
    }
    else
    {
        /* This operation is not allowed for MB that are part of RxFIFO */
        FlexcanMb = FlexCAN_GetMsgBuffRegion(pBase, MbIdx);
        FlexcanMbConfig = * FlexcanMb;
        /* Reset the code and unlock the MB */
        FlexcanMbConfig &= (~FLEXCAN_IP_CS_CODE_MASK);
        FlexcanMbConfig |= (uint32)(((uint32)FLEXCAN_RX_INACTIVE & (uint32)0x1F) << (uint8)FLEXCAN_IP_CS_CODE_SHIFT) & (uint32)FLEXCAN_IP_CS_CODE_MASK;
        *FlexcanMb = FlexcanMbConfig;
        /* Reconfigure The MB as left by RxMBconfig */
        FlexcanMbConfig &= (~FLEXCAN_IP_CS_CODE_MASK);
        FlexcanMbConfig |= (uint32)(((uint32)FLEXCAN_RX_EMPTY & (uint32)0x1F) << (uint8)FLEXCAN_IP_CS_CODE_SHIFT) & (uint32)FLEXCAN_IP_CS_CODE_MASK;
        *FlexcanMb = FlexcanMbConfig;
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_CompleteRxMessageFifoData
 * Description   : Finish up a receive by completing the process of receiving
 * data and disabling the interrupt.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static void FlexCAN_CompleteRxMessageFifoData(uint8 Instance)
{

    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];
#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
    Dma_Ip_LogicChannelStatusType DmaStatus;
    Flexcan_Ip_MsgBuffType * FifoMessage = NULL_PTR;
    uint32 * MsgData32 = NULL_PTR;
#endif

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif

    if (FLEXCAN_RXFIFO_USING_INTERRUPTS == pState->transferType)
    {
        /* Disable RX FIFO interrupts*/
        (void)FlexCAN_SetMsgBuffIntCmd(pBase, Instance, FLEXCAN_IP_LEGACY_RXFIFO_FRAME_AVAILABLE, FALSE, pState->isIntActive);
        (void)FlexCAN_SetMsgBuffIntCmd(pBase, Instance, FLEXCAN_IP_LEGACY_RXFIFO_WARNING, FALSE, pState->isIntActive);
        (void)FlexCAN_SetMsgBuffIntCmd(pBase, Instance, FLEXCAN_IP_LEGACY_RXFIFO_OVERFLOW, FALSE, pState->isIntActive);
    }

#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
    else if (FLEXCAN_RXFIFO_USING_DMA == pState->transferType)
    {
        DmaStatus.ChStateValue = DMA_IP_CH_ERROR_STATE;
        (void)Dma_Ip_GetLogicChannelStatus(pState->rxFifoDMAChannel, &DmaStatus);

        if (DMA_IP_CH_ERROR_STATE == DmaStatus.ChStateValue)
        {
            (void)Dma_Ip_SetLogicChannelCommand(pState->rxFifoDMAChannel, DMA_IP_CH_CLEAR_ERROR);
            pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state = FLEXCAN_MB_DMA_ERROR;
        }

        (void)Dma_Ip_SetLogicChannelCommand(pState->rxFifoDMAChannel, DMA_IP_CH_CLEAR_HARDWARE_REQUEST);

        if (pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state != FLEXCAN_MB_DMA_ERROR)
        {
            FifoMessage = pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].pMBmessage;
            MsgData32 = (uint32 *)FifoMessage->data;

            /* Adjust the ID if it is not extended */
            if (0U == ((FifoMessage->cs) & FLEXCAN_IP_CS_IDE_MASK))
            {
                FifoMessage->msgId = FifoMessage->msgId >> FLEXCAN_IP_ID_STD_SHIFT;
            }

            /* Extract the data length */
            FifoMessage->dataLen = (uint8)((FifoMessage->cs & FLEXCAN_IP_CS_DLC_MASK) >> FLEXCAN_IP_CS_DLC_SHIFT);
            /* Extract the IDHIT */
            FifoMessage->id_hit = (uint8)((FifoMessage->cs & FLEXCAN_IP_CS_IDHIT_MASK) >> FLEXCAN_IP_CS_IDHIT_SHIFT);
            /* Extract the Time Stamp */
            FifoMessage->time_stamp = (uint32)((FifoMessage->cs & FLEXCAN_IP_CS_TIME_STAMP_MASK) >> FLEXCAN_IP_CS_TIME_STAMP_SHIFT);
            /* Reverse the endianness */
            FLEXCAN_IP_SWAP_BYTES_IN_WORD(MsgData32[0], MsgData32[0]);
            FLEXCAN_IP_SWAP_BYTES_IN_WORD(MsgData32[1], MsgData32[1]);
        }
    }
    else
    {
        /* do nothing when transferType is POLLING */
    }
#endif /* if FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */
    /* Clear fifo message*/
    pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].pMBmessage = NULL_PTR;

#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
    if (pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state != FLEXCAN_MB_DMA_ERROR)
    {
        pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state = FLEXCAN_MB_IDLE;
        if ((pState->callback != NULL_PTR) && (FLEXCAN_RXFIFO_USING_DMA == pState->transferType))
        {
            pState->callback(Instance, FLEXCAN_EVENT_DMA_COMPLETE, FLEXCAN_IP_MB_HANDLE_RXFIFO, pState);
        }
    }
    else
    {
        if (pState->callback != NULL_PTR)
        {
            pState->callback(Instance, FLEXCAN_EVENT_DMA_ERROR, FLEXCAN_IP_MB_HANDLE_RXFIFO, pState);
        }
    }

#else
    pState->mbs[FLEXCAN_IP_MB_HANDLE_RXFIFO].state = FLEXCAN_MB_IDLE;
#endif /* if FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */
}

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_ON)
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_HandleEnhanceRxFIFO
 * Description   : Checking the interrupts that are enabled and ready
 * for executing FlexCAN_IRQHandlerEnhancedRxFIFO function
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static void FlexCAN_HandleEnhanceRxFIFO(uint8 Instance)
{
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    uint32 u32IntType;

    if (TRUE == FlexCAN_IsEnhancedRxFifoAvailable(pBase))
    {
        /* Get the interrupts that are enabled and ready */
        for (u32IntType = (uint32)FLEXCAN_IP_ENHANCED_RXFIFO_UNDERFLOW; u32IntType >= (uint32)FLEXCAN_EVENT_ENHANCED_RXFIFO_OVERFLOW; u32IntType--)
        {
            if ((uint8)0U != FlexCAN_GetEnhancedRxFIFOStatusFlag(pBase, u32IntType))
            {
                if ((uint8)0U != FlexCAN_GetEnhancedRxFIFOIntStatusFlag(pBase, u32IntType))
                {
                    FlexCAN_IRQHandlerEnhancedRxFIFO(Instance, u32IntType);
                }
            }
        }
    }
}
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_ON) */
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON) */

#if (FLEXCAN_IP_FEATURE_BUSOFF_ERROR_INTERRUPT_UNIFIED == STD_ON)
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_ProcessIRQHandlerBusOffError
 * Description   : Checking the interrupts that are available and ready
 * for executing ErrorCallback function if this function is available.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static void FlexCAN_ProcessIRQHandlerBusOffError(uint8 Instance, FLEXCAN_Type * pBase, const Flexcan_Ip_StateType * pState)
{
    uint32 u32ErrStatus = 0U;
    
    
    /* Get error status to get value updated */
    u32ErrStatus = pBase->ESR1;

    /* Check spurious interrupt */
    if (((uint32)0U != (u32ErrStatus & ((uint32)FLEXCAN_ESR1_ERRINT_MASK))) && ((uint32)0U != (pBase->CTRL1 & ((uint32)FLEXCAN_CTRL1_ERRMSK_MASK))))
    {
        pBase->ESR1 = FLEXCAN_ESR1_ERRINT_MASK;
        /* Invoke callback */
        u32ErrStatus = FlexCAN_ErrorCallback(Instance, FLEXCAN_EVENT_ERROR, u32ErrStatus, pState);
    }

#if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
    /* Check if this is spurious interrupt */
    if (((uint32)0U != (u32ErrStatus & ((uint32)FLEXCAN_ESR1_ERRINT_FAST_MASK))) && ((uint32)0U != (pBase->CTRL2 & ((uint32)FLEXCAN_CTRL2_ERRMSK_FAST_MASK))))
    {
        pBase->ESR1 = FLEXCAN_ESR1_ERRINT_FAST_MASK;
        /* Invoke callback */
        u32ErrStatus = FlexCAN_ErrorCallback(Instance, FLEXCAN_EVENT_ERROR_FAST, u32ErrStatus, pState);
    }
#endif /* FLEXCAN_IP_FEATURE_HAS_FD */

    /* Check spurious interrupt */
    if (((uint32)0U != (u32ErrStatus & ((uint32)FLEXCAN_ESR1_TWRNINT_MASK))) && ((uint32)0U != (pBase->CTRL1 & ((uint32)FLEXCAN_CTRL1_TWRNMSK_MASK))))
    {
        pBase->ESR1 = FLEXCAN_ESR1_TWRNINT_MASK;
        /* Invoke callback */
        u32ErrStatus = FlexCAN_ErrorCallback(Instance, FLEXCAN_EVENT_TX_WARNING, u32ErrStatus, pState);
    }

    /* Check spurious interrupt */
    if (((uint32)0U != (u32ErrStatus & ((uint32)FLEXCAN_ESR1_RWRNINT_MASK))) && ((uint32)0U != (pBase->CTRL1 & ((uint32)FLEXCAN_CTRL1_RWRNMSK_MASK))))
    {
        pBase->ESR1 = FLEXCAN_ESR1_RWRNINT_MASK;
        /* Invoke callback */
        u32ErrStatus = FlexCAN_ErrorCallback(Instance, FLEXCAN_EVENT_RX_WARNING, u32ErrStatus, pState);
    }

    /* Check spurious interrupt */
    if (((uint32)0U != (u32ErrStatus & ((uint32)FLEXCAN_ESR1_BOFFINT_MASK))) && ((uint32)0U != (pBase->CTRL1 & ((uint32)FLEXCAN_CTRL1_BOFFMSK_MASK))))
    {
        pBase->ESR1 = FLEXCAN_ESR1_BOFFINT_MASK;
        /* Invoke callback */
        (void)FlexCAN_ErrorCallback(Instance, FLEXCAN_EVENT_BUSOFF, u32ErrStatus, pState);
    }
}
#endif /* (FLEXCAN_IP_FEATURE_BUSOFF_ERROR_INTERRUPT_UNIFIED) */
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_ErrorCallback
 * Description   : Call the callback function follow by event and return the status about ESR1 register.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static uint32 FlexCAN_ErrorCallback(uint8 Instance, Flexcan_Ip_EventType event, uint32 u32ErrStatus, const Flexcan_Ip_StateType * pState)
{
    uint32 u32Status = 0U;
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    
    /* Invoke callback */
    if (pState->error_callback != NULL_PTR)
    {
        pState->error_callback(Instance, event, u32ErrStatus, pState);
        /* Get error status to get value updated due to user may handle ESR1 register */
        u32Status = pBase->ESR1;
    }
    
    return u32Status;
}
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_ClearAllIntStatusFlag
 * Description   : Call clear all interrupt flags as MB flag, enhance fifo flag.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static void FlexCAN_ClearAllIntStatusFlag
(
    FLEXCAN_Type * pBase,
    uint32 StartMbIdx,
    uint32 EndMbIdx
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_OFF)
    ,boolean bEnhancedFifoExisted
#endif
#endif
)
{
    uint32 MbIdx = 0U;

    for (MbIdx = StartMbIdx; MbIdx <= EndMbIdx; MbIdx++)
    {
        /* clear the MB flag */
        FlexCAN_ClearMsgBuffIntStatusFlag(pBase, MbIdx);
    }
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_OFF)
    /* Check if Enhance fifo interrupt existed! */
    if (TRUE == bEnhancedFifoExisted)
    {
        FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_FRAME_AVAILABLE);
        FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_WATERMARK);
        FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_OVERFLOW);
        FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_UNDERFLOW);
    }
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_OFF) */
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON) */    
}
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_ProcessIRQHandlerMsgBuffer
 * Description   : Process Tx/Rx and Legacy Fifo interrupt and clear flag interrupt.
 * This is not a public API as it is called from other driver functions.
 *
 *END**************************************************************************/
static void FlexCAN_ProcessIRQHandlerMsgBuffer
(
    uint8 Instance,
    const Flexcan_Ip_StateType * pState,
    uint32 MbIdx
)
{
    uint32 u32MbHandle = 0U;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];

    u32MbHandle = MbIdx;

    if ((TRUE == pState->bIsLegacyFifoEn) && (MbIdx <= FLEXCAN_IP_LEGACY_RXFIFO_OVERFLOW))
    {
        FlexCAN_IRQHandlerRxFIFO(Instance, MbIdx);
        /* For legacy fifo, mb handler is FLEXCAN_IP_MB_HANDLE_RXFIFO (MB0) */
        u32MbHandle = (uint32)FLEXCAN_IP_MB_HANDLE_RXFIFO;
    }
    else
    {
        /* Check mailbox completed reception */
        if (FLEXCAN_MB_RX_BUSY == pState->mbs[u32MbHandle].state)
        {
            FlexCAN_IRQHandlerRxMB(Instance, MbIdx);
        }
    }

    /* Check mailbox completed transmission */
    if (FLEXCAN_MB_TX_BUSY == pState->mbs[u32MbHandle].state)
    {
        FlexCAN_IRQHandlerTxMB(Instance, MbIdx);
    }

    if ((FlexCAN_GetMsgBuffIntStatusFlag(pBase, MbIdx) != 0U) && \
        (FlexCAN_GetMsgBuffIntStatusImask(pBase, MbIdx) != 0U)
       )
    {
        if (pState->mbs[u32MbHandle].state == FLEXCAN_MB_IDLE)
        {
            /* In case of desynchronized status of the MB to avoid trapping in ISR
            * clear the MB flag */
            FlexCAN_ClearMsgBuffIntStatusFlag(pBase, MbIdx);
        }
    }
}
/*==================================================================================================
*                                       GLOBAL FUNCTIONS
==================================================================================================*/
/* implements FlexCAN_Ip_Init_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_Init_Privileged(uint8 Flexcan_Ip_u8Instance,
                                                 Flexcan_Ip_StateType * Flexcan_Ip_pState,
                                                 const Flexcan_Ip_ConfigType * Flexcan_Ip_pData
                                                )
{
    Flexcan_Ip_StatusType eResult = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Flexcan_Ip_u8Instance];
    uint32 Idx;

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Flexcan_Ip_u8Instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(Flexcan_Ip_pData != NULL_PTR);
#if defined(FLEXCAN_IP_FEATURE_FD_NOT_ALL_INSTANCES)
    /* Check if the instance support FD capability */
    if (TRUE == Flexcan_Ip_pData->fd_enable)
    {
        DevAssert(FlexCAN_IsFDAvailable(pBase) == Flexcan_Ip_pData->fd_enable);
    }
#endif
#endif

#if (STD_ON == FLEXCAN_IP_ENABLE_USER_MODE_SUPPORT)
    #if (STD_ON == FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE)
        /* Set the UAA bit in REG_PROT to make the instance accessible in user mode */
        if (Flexcan_Ip_u8Instance < FLEXCAN_IP_CTRL_REG_PROT_SUPPORT_U8)
        {
            OsIf_Trusted_Call1param(FlexCAN_SetUserAccessAllowed, pBase);
        }
    #endif
#endif /* STD_ON == FLEXCAN_IP_ENABLE_USER_MODE_SUPPORT */

    eResult = FlexCAN_InitController(Flexcan_Ip_u8Instance, pBase, Flexcan_Ip_pData);
    if (FLEXCAN_STATUS_SUCCESS == eResult)
    {
        /* Init Baudrate */
        FlexCAN_InitBaudrate(pBase, Flexcan_Ip_pData);
        /* Select mode */
        FlexCAN_SetOperationMode(pBase, Flexcan_Ip_pData->flexcanMode);

#if (STD_ON == FLEXCAN_IP_ENABLE_USER_MODE_SUPPORT)
    #if (STD_ON == FLEXCAN_IP_FEATURE_HAS_SUPV)
    #if defined(FLEXCAN_IP_FEATURE_SUPV_NOT_ALL_INSTANCES)
        /* Check if instance support Supervisor Mode. Clear MCR[SUPV] to switch User Mode */
        if (TRUE == FlexCAN_IsSupvModeAvailable(pBase))
    #endif
        {
            pBase->MCR = (pBase->MCR & ~FLEXCAN_MCR_SUPV_MASK) | FLEXCAN_MCR_SUPV(0U);
        }
    #endif
#endif /* STD_ON == FLEXCAN_IP_ENABLE_USER_MODE_SUPPORT */

#if (FLEXCAN_IP_FEATURE_HAS_TS_ENABLE == STD_ON)
        FlexCAN_ConfigTimestamp(Flexcan_Ip_u8Instance, pBase, (const Flexcan_Ip_TimeStampConfigType *)(&Flexcan_Ip_pData->time_stamp));
#endif   /* (FLEXCAN_IP_FEATURE_HAS_TS_ENABLE == STD_ON) */

        for (Idx = 0; Idx < (uint8)FLEXCAN_IP_FEATURE_MAX_MB_NUM; Idx++)
        {
            /* Check if blocking need to be any more present in sync\async discussions */
            /* Sync up isPolling status with hw (Imask), at the begining all Imask = 0 => isPolling = TRUE */
            Flexcan_Ip_pState->mbs[Idx].isPolling = TRUE;
            Flexcan_Ip_pState->mbs[Idx].pMBmessage = NULL_PTR;
            Flexcan_Ip_pState->mbs[Idx].state = FLEXCAN_MB_IDLE;
            Flexcan_Ip_pState->mbs[Idx].time_stamp = 0U;
        }
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
        /* Sync up isPolling status with hw (Imask), at the begining all Imask = 0 => isPolling = TRUE */
        Flexcan_Ip_pState->enhancedFifoOutput.isPolling = TRUE;
        Flexcan_Ip_pState->enhancedFifoOutput.state = FLEXCAN_MB_IDLE;
#endif

        Flexcan_Ip_pState->transferType = Flexcan_Ip_pData->transfer_type;
#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
        Flexcan_Ip_pState->rxFifoDMAChannel = Flexcan_Ip_pData->rxFifoDMAChannel;
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
        Flexcan_Ip_pState->u32NumOfMbTransferByDMA = Flexcan_Ip_pData->num_enhanced_watermark;
#endif /* FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO */
#endif /* FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */

        /* Clear Callbacks in case of autovariables garbage */
        Flexcan_Ip_pState->callback = Flexcan_Ip_pData->Callback;
        Flexcan_Ip_pState->callbackParam = NULL_PTR;
        Flexcan_Ip_pState->error_callback = Flexcan_Ip_pData->ErrorCallback;
        Flexcan_Ip_pState->errorCallbackParam = NULL_PTR;
        Flexcan_Ip_pState->bIsLegacyFifoEn = Flexcan_Ip_pData->is_rx_fifo_needed;
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
        Flexcan_Ip_pState->bIsEnhancedFifoEn = Flexcan_Ip_pData->is_enhanced_rx_fifo_needed;
#endif /* FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO */
        Flexcan_Ip_pState->u32MaxMbNum = Flexcan_Ip_pData->max_num_mb;
        Flexcan_Ip_pState->isIntActive = TRUE;
#if (FLEXCAN_IP_FEATURE_HAS_MEM_ERR_DET == STD_ON)
#if (FLEXCAN_IP_FEATURE_MEM_ERR_DET_ENABLED == STD_ON)
        Flexcan_Ip_pState->flexcanModeErrResponse = Flexcan_Ip_pData->flexcanModeErrResponse;
#endif
#endif
        /* Save runtime structure pointers so irq handler can point to the correct state structure */
        Flexcan_Ip_apxState[Flexcan_Ip_u8Instance] = Flexcan_Ip_pState;
    }
    return eResult;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_DRV_Send
 * Description   : This function sends a CAN frame using a configured message
 * buffer. The function returns immediately. If a callback is installed, it will
 * be invoked after the frame was sent.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_Send_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_Send(uint8 instance,
                                      uint8 mb_idx,
                                      const Flexcan_Ip_DataInfoType * tx_info,
                                      uint32 msg_id,
                                      const uint8 * mb_data
                                     )
{
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_ERROR;
#if (FLEXCAN_IP_MB_INTERRUPT_SUPPORT == STD_ON)
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[instance];
#else
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
#endif

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(tx_info != NULL_PTR);
#endif
    if (!FlexCAN_IsListenOnlyModeEnabled(pBase))
    {
        Result = FlexCAN_StartSendData(instance, mb_idx, tx_info, msg_id, mb_data);
#if (FLEXCAN_IP_MB_INTERRUPT_SUPPORT == STD_ON)
        if ((FLEXCAN_STATUS_SUCCESS ==  Result) && (FALSE == tx_info->is_polling))
        {
            /* Enable message buffer interrupt*/
            Result = FlexCAN_SetMsgBuffIntCmd(pBase, instance, mb_idx, TRUE, pState->isIntActive);
        }
#endif
    }
    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_ConfigMb
 * Description   : Configure a Rx message buffer.
 * This function will first check if RX FIFO is enabled. If RX FIFO is enabled,
 * the function will make sure if the MB requested is not occupied by RX FIFO
 * and ID filter table. Then this function will set up the message buffer fields,
 * configure the message buffer code for Rx message buffer as NOT_USED, enable
 * the Message Buffer interrupt, configure the message buffer code for Rx
 * message buffer as INACTIVE, copy user's buffer into the message buffer data
 * area, and configure the message buffer code for Rx message buffer as EMPTY.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_ConfigRxMb_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_ConfigRxMb(uint8 instance,
                                            uint8 mb_idx,
                                            const Flexcan_Ip_DataInfoType * rx_info,
                                            uint32 msg_id
                                           )
{
    Flexcan_Ip_StatusType eResult = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_MsbuffCodeStatusType Cs;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[instance];
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(rx_info != NULL_PTR);

    if (TRUE == FlexCAN_IsMbOutOfRange(pBase, mb_idx, pState->bIsLegacyFifoEn, pState->u32MaxMbNum))
    {
        eResult = FLEXCAN_STATUS_BUFF_OUT_OF_RANGE;
    }
    else
    {
#endif
        /* Clear the message buffer flag if previous remained triggered */
        FlexCAN_ClearMsgBuffIntStatusFlag(pBase, mb_idx);

        Cs.dataLen = rx_info->data_length;
        Cs.msgIdType = rx_info->msg_id_type;
    #if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
        Cs.fd_enable = rx_info->fd_enable;
    #endif

        /* Initialize rx mb*/
        Cs.code = (uint32)FLEXCAN_RX_NOT_USED;
        FlexCAN_SetRxMsgBuff(pBase, mb_idx, &Cs, msg_id);

        /* Initialize receive MB*/
        Cs.code = (uint32)FLEXCAN_RX_INACTIVE;
        FlexCAN_SetRxMsgBuff(pBase, mb_idx, &Cs, msg_id);

        /* Set up FlexCAN message buffer fields for receiving data*/
        Cs.code = (uint32)FLEXCAN_RX_EMPTY;
        FlexCAN_SetRxMsgBuff(pBase, mb_idx, &Cs, msg_id);
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    }
#endif
    return eResult;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_Receive
 * Description   : This function receives a CAN frame into a configured message
 * buffer. The function returns immediately. If a callback is installed, it will
 * be invoked after the frame was received and read into the specified buffer.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_Receive_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_Receive(uint8 instance,
                                         uint8 mb_idx,
                                         Flexcan_Ip_MsgBuffType * data,
                                         boolean isPolling
                                        )
{

    Flexcan_Ip_StatusType Result;
#if (FLEXCAN_IP_MB_INTERRUPT_SUPPORT == STD_ON)
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[instance];
#endif

    #if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
    #endif

    Result = FlexCAN_StartRxMessageBufferData(instance, mb_idx, data, isPolling);
#if (FLEXCAN_IP_MB_INTERRUPT_SUPPORT == STD_ON)
    if ((FLEXCAN_STATUS_SUCCESS == Result) && (FALSE == isPolling))
    {
        /* Enable MB interrupt*/
        Result = FlexCAN_SetMsgBuffIntCmd(pBase, instance, mb_idx, TRUE, pState->isIntActive);
    }
#endif
    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_ReceiveBlocking
 * Description   : This function receives a CAN frame into a configured message
 * buffer. The function blocks until either a frame was received, or the
 * specified timeout expired.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_ReceiveBlocking_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_ReceiveBlocking(uint8 instance,
                                                 uint8 mb_idx,
                                                 Flexcan_Ip_MsgBuffType * data,
                                                 boolean isPolling,
                                                 uint32 u32TimeoutMs
                                                )
{
    Flexcan_Ip_StatusType Result;
    uint32 TimeStart = 0U;
    uint32 TimeElapsed = 0U;
    uint32 Ms2Ticks = OsIf_MicrosToTicks((u32TimeoutMs * 1000U), FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[instance];
#if (FLEXCAN_IP_MB_INTERRUPT_SUPPORT == STD_ON)
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
#else
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
#endif

    Result = FlexCAN_StartRxMessageBufferData(instance, mb_idx, data, isPolling);
#if (FLEXCAN_IP_MB_INTERRUPT_SUPPORT == STD_ON)
    if ((FLEXCAN_STATUS_SUCCESS == Result) && (FALSE == isPolling))
    {
        /* Enable MB interrupt*/
        Result = FlexCAN_SetMsgBuffIntCmd(pBase, instance, mb_idx, TRUE, pState->isIntActive);
    }
#endif
    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        TimeStart = OsIf_GetCounter(FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
        while (FLEXCAN_MB_RX_BUSY == pState->mbs[mb_idx].state)
        {
            if (TRUE == isPolling)
            {
                if (FlexCAN_GetMsgBuffIntStatusFlag(pBase, mb_idx) != 0U)
                {
                    FlexCAN_IRQHandlerRxMB(instance, mb_idx);
                }
            }
            TimeElapsed += OsIf_GetElapsed(&TimeStart, FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
            if (TimeElapsed >= Ms2Ticks)
            {
                Result = FLEXCAN_STATUS_TIMEOUT;
                break;
            }
        }
    }

    if ((FLEXCAN_STATUS_TIMEOUT == Result) && (FALSE == isPolling))
    {
#if (FLEXCAN_IP_MB_INTERRUPT_SUPPORT == STD_ON)
        /* Disable Mb interrupt*/
       (void)FlexCAN_SetMsgBuffIntCmd(pBase, instance, mb_idx, FALSE, pState->isIntActive);
#endif
    }

    if ((FLEXCAN_STATUS_BUFF_OUT_OF_RANGE != Result) && (FLEXCAN_STATUS_BUSY != Result))
    {
        if ((FLEXCAN_MB_IDLE == pState->mbs[mb_idx].state))
        {
            Result = FLEXCAN_STATUS_SUCCESS;
        }
        else
        {
            pState->mbs[mb_idx].state = FLEXCAN_MB_IDLE;
            Result = FLEXCAN_STATUS_TIMEOUT;
        }
    }
    return Result;
}

#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
#if FLEXCAN_IP_INSTANCE_COUNT > 0U
/*FUNCTION**********************************************************************
 *
 * Function Name : DMA_Can_Callback0
 * Description   : Callback function for DMA when finish up a DMA major transfer 
 * at FlexCAN_0 peripheral.
 *
 *END**************************************************************************/
void DMA_Can_Callback0(void)
{ DMA_Can_Callback(0U); }
#endif

#if FLEXCAN_IP_INSTANCE_COUNT > 1U
/*FUNCTION**********************************************************************
 *
 * Function Name : DMA_Can_Callback1
 * Description   : Callback function for DMA when finish up a DMA major transfer 
 * at FlexCAN_1 peripheral.
 *
 *END**************************************************************************/
void DMA_Can_Callback1(void)
{ DMA_Can_Callback(1U); }
#endif

#if FLEXCAN_IP_INSTANCE_COUNT > 2U
/*FUNCTION**********************************************************************
 *
 * Function Name : DMA_Can_Callback2
 * Description   : Callback function for DMA when finish up a DMA major transfer 
 * at FlexCAN_2 peripheral.
 *
 *END**************************************************************************/
void DMA_Can_Callback2(void)
{ DMA_Can_Callback(2U); }
#endif

#if FLEXCAN_IP_INSTANCE_COUNT > 3U
/*FUNCTION**********************************************************************
 *
 * Function Name : DMA_Can_Callback3
 * Description   : Callback function for DMA when finish up a DMA major transfer 
 * at FlexCAN_3 peripheral.
 *
 *END**************************************************************************/
void DMA_Can_Callback3(void)
{ DMA_Can_Callback(3U); }
#endif

#if FLEXCAN_IP_INSTANCE_COUNT > 4U
/*FUNCTION**********************************************************************
 *
 * Function Name : DMA_Can_Callback4
 * Description   : Callback function for DMA when finish up a DMA major transfer 
 * at FlexCAN_4 peripheral.
 *
 *END**************************************************************************/
void DMA_Can_Callback4(void)
{ DMA_Can_Callback(4U); }
#endif

#if FLEXCAN_IP_INSTANCE_COUNT > 5U
/*FUNCTION**********************************************************************
 *
 * Function Name : DMA_Can_Callback5
 * Description   : Callback function for DMA when finish up a DMA major transfer 
 * at FlexCAN_5 peripheral.
 *
 *END**************************************************************************/
void DMA_Can_Callback5(void)
{ DMA_Can_Callback(5U); }
#endif

#if FLEXCAN_IP_INSTANCE_COUNT > 6U
/*FUNCTION**********************************************************************
 *
 * Function Name : DMA_Can_Callback6
 * Description   : Callback function for DMA when finish up a DMA major transfer 
 * at FlexCAN_6 peripheral.
 *
 *END**************************************************************************/
void DMA_Can_Callback6(void)
{ DMA_Can_Callback(6U); }
#endif

#if FLEXCAN_IP_INSTANCE_COUNT > 7U
/*FUNCTION**********************************************************************
 *
 * Function Name : DMA_Can_Callback7
 * Description   : Callback function for DMA when finish up a DMA major transfer 
 * at FlexCAN_7 peripheral.
 *
 *END**************************************************************************/
void DMA_Can_Callback7(void)
{ DMA_Can_Callback(7U); }
#endif

#if FLEXCAN_IP_INSTANCE_COUNT > 8U
/*FUNCTION**********************************************************************
 *
 * Function Name : DMA_Can_Callback8
 * Description   : Callback function for DMA when finish up a DMA major transfer 
 * at FlexCAN_8 peripheral.
 *
 *END**************************************************************************/
void DMA_Can_Callback8(void)
{ DMA_Can_Callback(8U); }
#endif

#if FLEXCAN_IP_INSTANCE_COUNT > 9U
/*FUNCTION**********************************************************************
 *
 * Function Name : DMA_Can_Callback9
 * Description   : Callback function for DMA when finish up a DMA major transfer 
 * at FlexCAN_9 peripheral.
 *
 *END**************************************************************************/
void DMA_Can_Callback9(void)
{ DMA_Can_Callback(9U); }
#endif

#if FLEXCAN_IP_INSTANCE_COUNT > 10U
/*FUNCTION**********************************************************************
 *
 * Function Name : DMA_Can_Callback10
 * Description   : Callback function for DMA when finish up a DMA major transfer 
 * at FlexCAN_10 peripheral.
 *
 *END**************************************************************************/
void DMA_Can_Callback10(void)
{ DMA_Can_Callback(10U); }
#endif

#if FLEXCAN_IP_INSTANCE_COUNT > 11U
/*FUNCTION**********************************************************************
 *
 * Function Name : DMA_Can_Callback11
 * Description   : Callback function for DMA when finish up a DMA major transfer 
 * at FlexCAN_11 peripheral.
 *
 *END**************************************************************************/
void DMA_Can_Callback11(void)
{ DMA_Can_Callback(11U); }
#endif

#endif /* FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON */

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_RxFifo
 * Description   : This function receives a CAN frame using the Rx FIFO or
 * Enhanced Rx FIFO (if available and enabled). If use Enhanced Rx FIFO, the size of
 * the data array will be considered the same as the configured FIFO watermark.
 * The function returns immediately. If a callback is installed, it will be invoked
 * after the frame was received and read into the specified buffer.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_RxFifo_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_RxFifo(uint8 instance, Flexcan_Ip_MsgBuffType * data)
{
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

    if (TRUE == FlexCAN_IsEnhancedRxFifoEnabled(pBase))
    {
        Result = FlexCAN_StartRxMessageEnhancedFifoData(instance, data);
    }
    else
#endif /* FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON */
    {
        Result = FlexCAN_StartRxMessageFifoData(instance, data);
    }
    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_RxFifoBlocking
 * Description   : This function receives a CAN frame using the Rx FIFO or
 * Enhanced Rx FIFO (if available and enabled). If use Enhanced Rx FIFO, the size of
 * the data array will be considered the same as the configured FIFO watermark.
 * The function blocks until either a frame was received, or the specified timeout expired.
 *
 *END**************************************************************************/

/* implements FlexCAN_Ip_RxFifoBlocking_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_RxFifoBlocking(uint8 instance, Flexcan_Ip_MsgBuffType *data, uint32 timeout)
{
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

    if (TRUE == FlexCAN_IsEnhancedRxFifoEnabled(pBase))
    {
        Result = FlexCAN_StartRxMessageEnhancedFifoData(instance, data);
        if (FLEXCAN_STATUS_SUCCESS == Result)
        {
            Result = FlexCAN_ProccessEnhancedRxFifo(instance, timeout);
        }
    }
    else
#endif /* FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON */
    {
        Result = FlexCAN_StartRxMessageFifoData(instance, data);
        if (FLEXCAN_STATUS_SUCCESS == Result)
        {
            Result = FlexCAN_ProccessLegacyRxFIFO(instance, timeout);
        }
    }
    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_ConfigRxFifo
 * Description   : Confgure RX FIFO ID filter table elements.
 * This function will confgure RX FIFO ID filter table elements, and enable RX
 * FIFO interrupts.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_ConfigRxFifo_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_ConfigRxFifo_Privileged(uint8 instance,
                                                         Flexcan_Ip_RxFifoIdElementFormatType id_format,
                                                         const Flexcan_Ip_IdTableType * id_filter_table
                                                        )
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert((id_filter_table != NULL_PTR) || (FLEXCAN_RX_FIFO_ID_FORMAT_D == id_format));
#endif
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    boolean Disabled = !FlexCAN_IsEnabled(pBase);
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#endif

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    Freeze = FlexCAN_IsFreezeMode(pBase);
    if ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result))
    {
        Result = FLEXCAN_STATUS_ERROR;
    }
#endif

    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        /* Initialize rx fifo*/
        FlexCAN_SetRxFifoFilter(pBase, id_format, id_filter_table);
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }

    return Result;
}

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_ConfigEnhancedRxFifo
 * Description   : Confgure Enhanced RX FIFO ID filter table elements.
 * This function will confgure Enhanced RX FIFO ID filter table elements, and enable Enhanced RX
 * FIFO interrupts.
 *END**************************************************************************/
/* implements FlexCAN_Ip_ConfigEnhancedRxFifo_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_ConfigEnhancedRxFifo_Privileged(uint8 instance, const Flexcan_Ip_EnhancedIdTableType * id_filter_table)
{
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    boolean Disabled = !FlexCAN_IsEnabled(pBase);
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#endif

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(FlexCAN_IsEnhancedRxFifoAvailable(pBase));
    DevAssert(id_filter_table != NULL_PTR);
#endif

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    Freeze = FlexCAN_IsFreezeMode(pBase);
    if ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result))
    {
        Result = FLEXCAN_STATUS_ERROR;
    }
#endif

    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        /* Initialize rx fifo*/
        FlexCAN_SetEnhancedRxFifoFilter(instance, id_filter_table);
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }

    return Result;
}
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON) */

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_ConfigRemoteResponseMb
 * Description   : Configures a transmit message buffer for remote frame
 * response. This function will first check if RX FIFO is enabled. If RX FIFO is
 * enabled, the function will make sure if the MB requested is not occupied by
 * the RX FIFO and ID filter table. Then this function will set up the message
 * buffer fields, configure the message buffer code for Tx buffer as RX_RANSWER,
 * and enable the Message Buffer interrupt if required by user.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_ConfigRemoteResponseMb_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_ConfigRemoteResponseMb(uint8 instance,
                                                        uint8 mb_idx,
                                                        const Flexcan_Ip_DataInfoType *tx_info,
                                                        uint32 msg_id,
                                                        const uint8 *mb_data
                                                       )
{
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_MsbuffCodeStatusType Cs;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    const Flexcan_Ip_StateType * const pState = Flexcan_Ip_apxState[instance];
    volatile uint32 * pMbAddr = NULL_PTR;

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(tx_info != NULL_PTR);
    /* Remote Request Store can't operate same time with automatic remote response */
    DevAssert(0U == (pBase->CTRL2 & FLEXCAN_CTRL2_RRS_MASK));
#if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
    /* Check if the Payload Size is smaller than the payload configured */
    DevAssert(((uint8)tx_info->data_length) <= FlexCAN_GetMbPayloadSize(pBase, mb_idx));
#else
    DevAssert(((uint8)tx_info->data_length) <= 8U);
#endif

    if (TRUE == FlexCAN_IsMbOutOfRange(pBase, mb_idx, pState->bIsLegacyFifoEn, pState->u32MaxMbNum))
    {
        Result = FLEXCAN_STATUS_BUFF_OUT_OF_RANGE;
    }
#endif
    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        /* Initialize transmit mb*/
        Cs.dataLen = tx_info->data_length;
        Cs.msgIdType = tx_info->msg_id_type;
        Cs.code = (uint32)FLEXCAN_RX_RANSWER;
#if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
        Cs.fd_enable = FALSE;
        Cs.enable_brs = FALSE;
        Cs.fd_padding = 0x00U;
#endif
        FlexCAN_ClearMsgBuffIntStatusFlag(pBase, mb_idx);
        pMbAddr = FlexCAN_GetMsgBuffRegion(pBase, mb_idx);
        FlexCAN_SetTxMsgBuff(pMbAddr, &Cs, msg_id, mb_data, tx_info->is_remote);
        if (FALSE == tx_info->is_polling)
        {
            /* Enable MB interrupt*/
            Result = FlexCAN_SetMsgBuffIntCmd(pBase, instance, mb_idx, TRUE, pState->isIntActive);
        }
    }
    return Result;
}
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_GetTransferStatus
 * Description   : This function returns whether the previous FLEXCAN receive is
 *                 completed.
 * When performing a non-blocking receive, the user can call this function to
 * ascertain the state of the current receive progress: in progress (or busy)
 * or complete (success). In case Enhanced Rx Fifo, mb_idx will be 255.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_GetTransferStatus_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_GetTransferStatus(uint8 instance, uint8 mb_idx)
{

    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[instance];
    Flexcan_Ip_StatusType Status;


#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
    #if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
    DevAssert((mb_idx < (uint8)FLEXCAN_IP_FEATURE_MAX_MB_NUM) || (FLEXCAN_IP_MB_ENHANCED_RXFIFO == mb_idx));
    #else
    DevAssert(mb_idx < (uint8)FLEXCAN_IP_FEATURE_MAX_MB_NUM);
    #endif
#endif

    if (mb_idx < (uint8)FLEXCAN_IP_FEATURE_MAX_MB_NUM)
    {
        if (FLEXCAN_MB_IDLE == pState->mbs[mb_idx].state)
        {
            Status = FLEXCAN_STATUS_SUCCESS;
        }
#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
        else if (FLEXCAN_MB_DMA_ERROR == pState->mbs[mb_idx].state)
        {
            Status = FLEXCAN_STATUS_ERROR;
        }
#endif
        else
        {
            Status = FLEXCAN_STATUS_BUSY;
        }
    }

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
    /* check in case of enhanced Rx fifo, mb_idx should be 255 */
    else if (FLEXCAN_IP_MB_ENHANCED_RXFIFO == mb_idx)
    {
        if (FLEXCAN_MB_IDLE == pState->enhancedFifoOutput.state)
        {
            Status = FLEXCAN_STATUS_SUCCESS;
        }

#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
        else if (FLEXCAN_MB_DMA_ERROR == pState->enhancedFifoOutput.state)
        {
            Status = FLEXCAN_STATUS_ERROR;
        }
#endif
        else
        {
            Status = FLEXCAN_STATUS_BUSY;
        }
    }
#endif /* FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO */

    else
    {
        Status = FLEXCAN_STATUS_ERROR;
    }

    return Status;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_IRQHandler
 * Description   : Interrupt handler for FLEXCAN.
 * This handler read data from MB or FIFO, and then clear the interrupt flags.
 * This is not a public API as it is called whenever an interrupt occurs.
 * Spurious interrupt implementation:
 * This handler will process only one Normal Buffers (tx or rx or legacy fifo)
 * and Enhance FIFO(if supported). The processing of spurious interrupt will be
 * done when there is no real interrupt(both IF and IE are set) at all.
 * If a spurious interrupt found:
 *  - for Rx (Rx normal, Legacy FIFO, Enhanced FIFO): Just clear IF and exit,
 *    the Buffer is still ready to receive a new Frame.
 *  - for Tx: Clear IF and reset the Buffer to default state to ready to use in next time.
 *END**************************************************************************/
/* implements CAN_X_MB_Y_ISR_Activity */
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
)
{
    boolean bInterruptValid = FALSE;
    uint32 MbIdx = 0;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(EndMbIdx < (uint8)FLEXCAN_IP_FEATURE_MAX_MB_NUM);
#endif
    /* Check if instance initialized */
    if (NULL_PTR != pState)
    {
        /* Get the MB index where the interrupt are enabled and ready to serve */
        bInterruptValid = FlexCAN_GetMsgBuffIntIndex(pBase, StartMbIdx, EndMbIdx, &MbIdx);

        /* Validate whether interrupt is valid or spurious */
        if (TRUE == bInterruptValid)
        {
            FlexCAN_ProcessIRQHandlerMsgBuffer(Instance, pState, MbIdx);
        }

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_OFF)
        if (TRUE == bEnhancedFifoExisted)
        {
            if ((TRUE == FlexCAN_IsEnhancedRxFifoEnabled(pBase)) && (FLEXCAN_RXFIFO_USING_INTERRUPTS == pState->transferType))
            {
                FlexCAN_ProcessIRQHandlerEnhancedRxFIFO(Instance);
            }
        }
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_OFF) */
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON) */
    }
    else
    {
        /* Clear all interrupt flags when driver is not initialized */
        /* Process spurious interrupt */
        FlexCAN_ClearAllIntStatusFlag(pBase,
                                      StartMbIdx,
                                      EndMbIdx
                                #if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
                                #if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_OFF)
                                      ,bEnhancedFifoExisted
                                #endif
                                #endif
                                     );
    }
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_ClearErrorStatus
 * Description   : Clears various error conditions detected in the reception and
 *                 transmission of a CAN frame.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_ClearErrorStatus_Activity */
void FlexCAN_Ip_ClearErrorStatus(uint8 instance, uint32 error)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif

    pBase->ESR1 = error;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_GetErrorStatus
 * Description   : Reports various error conditions detected in the reception and
 *                 transmission of a CAN frame and some general status of the device.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_GetErrorStatus_Activity */
uint32 FlexCAN_Ip_GetErrorStatus(uint8 instance)
{

    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    return (uint32)(pBase->ESR1);
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_GetControllerTxErrorCounter
 * Description   : Reports Transmit error counter for all errors detected in
 *                 transmitted messages.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_GetControllerTxErrorCounter_Activity */
uint8 FlexCAN_Ip_GetControllerTxErrorCounter(uint8 instance)
{

    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    return (uint8)((pBase->ECR & FLEXCAN_ECR_TXERRCNT_MASK) >> FLEXCAN_ECR_TXERRCNT_SHIFT);
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_GetControllerRxErrorCounter
 * Description   : Reports Receive error counter for all errors detected in
 *                 received messages.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_GetControllerRxErrorCounter_Activity */
uint8 FlexCAN_Ip_GetControllerRxErrorCounter(uint8 instance)
{

    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    return (uint8)((pBase->ECR & FLEXCAN_ECR_RXERRCNT_MASK) >> FLEXCAN_ECR_RXERRCNT_SHIFT);
}


#if (FLEXCAN_IP_FEATURE_BUSOFF_ERROR_INTERRUPT_UNIFIED == STD_ON)
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Busoff_Error_IRQHandler
 * Description   : BusOff and Error interrupt handler for FLEXCAN.
 * This is not a public API as it is called whenever an interrupt occurs.
 *
 *END**************************************************************************/
/* implements  CAN_X_BUSOFF_ERROR_ISR_Activity */
void FlexCAN_Busoff_Error_IRQHandler(uint8 Instance)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif

    /* Check if the instance initialized */
    if (NULL_PTR != pState)
    {
        FlexCAN_ProcessIRQHandlerBusOffError(Instance, pBase, pState);
#if (FLEXCAN_IP_FEATURE_HAS_PRETENDED_NETWORKING == STD_ON)
#if (FLEXCAN_IP_FEATURE_PRETENDED_NETWORKING_INTERRUPT_UNIFIED== STD_ON)
        if (FLEXCAN_MCR_PNET_EN_MASK == (pBase->MCR & FLEXCAN_MCR_PNET_EN_MASK))
        {
            FlexCAN_WakeUp_IRQHandler(Instance);
        }
#endif /* (FLEXCAN_IP_FEATURE_PRETENDED_NETWORKING_INTERRUPT_UNIFIED == STD_ON) */
#endif /* (FLEXCAN_IP_FEATURE_HAS_PRETENDED_NETWORKING == STD_ON) */

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_ON)
    FlexCAN_HandleEnhanceRxFIFO(Instance);
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_ON) */
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON) */
    }
    else
    {
        /* Clear all error interrupt flags */
        pBase->ESR1 = FLEXCAN_IP_ALL_INT;
#if (FLEXCAN_IP_FEATURE_HAS_PRETENDED_NETWORKING == STD_ON)
#if (FLEXCAN_IP_FEATURE_PRETENDED_NETWORKING_INTERRUPT_UNIFIED== STD_ON)
        /* Clear spurious Interrupt */
        FlexCAN_ClearWTOF(pBase);
        FlexCAN_ClearWUMF(pBase);
#endif /* (FLEXCAN_IP_FEATURE_PRETENDED_NETWORKING_INTERRUPT_UNIFIED == STD_ON) */
#endif /* (FLEXCAN_IP_FEATURE_HAS_PRETENDED_NETWORKING == STD_ON) */

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_ON)
        if (TRUE == FlexCAN_IsEnhancedRxFifoAvailable(pBase))
        {
            FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_UNDERFLOW);
            FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, (uint32)FLEXCAN_EVENT_ENHANCED_RXFIFO_OVERFLOW);
        }
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO_INT_SEPARATED == STD_ON) */
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON) */
    }
}
#else /* (FLEXCAN_IP_FEATURE_BUSOFF_ERROR_INTERRUPT_UNIFIED == STD_OFF) */
/*FUNCTION**********************************************************************
 *
 * Function Name : FLEXCAN_Error_IRQHandler
 * Description   : Error interrupt handler for FLEXCAN.
 * This is not a public API as it is called whenever an interrupt occurs.
 *
 *END**************************************************************************/
/* implements  CAN_X_ERROR_ISR_Activity */
void FlexCAN_Error_IRQHandler(uint8 Instance)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];
    uint32 u32ErrStatus = 0U;

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif

    /* Check if the instance initialized */
    if (NULL_PTR != pState)
    {
        /* Get error status to get value updated */
        u32ErrStatus = pBase->ESR1;

        /* Check spurious interrupt */
        if (((uint32)0U != (u32ErrStatus & ((uint32)FLEXCAN_ESR1_ERRINT_MASK))) && ((uint32)0U != (pBase->CTRL1 & ((uint32)FLEXCAN_CTRL1_ERRMSK_MASK))))
        {
            pBase->ESR1 = FLEXCAN_ESR1_ERRINT_MASK;
            /* Invoke callback */
            #if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
            u32ErrStatus = FlexCAN_ErrorCallback(Instance, FLEXCAN_EVENT_ERROR, u32ErrStatus, pState);
            #else
            (void)FlexCAN_ErrorCallback(Instance, FLEXCAN_EVENT_ERROR, u32ErrStatus, pState);
            #endif /* FLEXCAN_IP_FEATURE_HAS_FD */
        }

    #if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
        /* Check if this is spurious interrupt */
        if (((uint32)0U != (u32ErrStatus & ((uint32)FLEXCAN_ESR1_ERRINT_FAST_MASK))) && ((uint32)0U != (pBase->CTRL2 & ((uint32)FLEXCAN_CTRL2_ERRMSK_FAST_MASK))))
        {
            pBase->ESR1 = FLEXCAN_ESR1_ERRINT_FAST_MASK;
            /* Invoke callback */
            (void)FlexCAN_ErrorCallback(Instance, FLEXCAN_EVENT_ERROR_FAST, u32ErrStatus, pState);
        }
    #endif /* FLEXCAN_IP_FEATURE_HAS_FD */
    }
    else
    {
        (pBase->ESR1) = FLEXCAN_IP_ERROR_INT;
    }
}


/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_BusOff_IRQHandler
 * Description   : BusOff and Tx/Rx Warning interrupt handler for FLEXCAN.
 * This handler only provides a error status report and invokes the user callback,
 * and then clears the interrupt flags.
 * This is not a public API as it is called whenever an interrupt occurs.
 *
 *END**************************************************************************/
/* implements CAN_X_BUSOFF_ISR_Activity */
void FlexCAN_BusOff_IRQHandler(uint8 Instance)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];
    uint32 u32ErrStatus = 0U;

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif

    /* Check If Driver initialized */
    if (NULL_PTR != pState)
    {
        /* Get error status to get value updated */
        u32ErrStatus = pBase->ESR1;

        /* Check spurious interrupt */
        if (((uint32)0U != (u32ErrStatus & ((uint32)FLEXCAN_ESR1_TWRNINT_MASK))) && (0U != (pBase->CTRL1 & ((uint32)FLEXCAN_CTRL1_TWRNMSK_MASK))))
        {
            pBase->ESR1 = FLEXCAN_ESR1_TWRNINT_MASK;
            /* Invoke callback */
            u32ErrStatus = FlexCAN_ErrorCallback(Instance, FLEXCAN_EVENT_TX_WARNING, u32ErrStatus, pState);
        }

        /* Check spurious interrupt */
        if (((uint32)0U != (u32ErrStatus & ((uint32)FLEXCAN_ESR1_RWRNINT_MASK))) && (0U != (pBase->CTRL1 & ((uint32)FLEXCAN_CTRL1_RWRNMSK_MASK))))
        {
            pBase->ESR1 = FLEXCAN_ESR1_RWRNINT_MASK;
            /* Invoke callback */
            u32ErrStatus = FlexCAN_ErrorCallback(Instance, FLEXCAN_EVENT_RX_WARNING, u32ErrStatus, pState);
        }

        /* Check spurious interrupt */
        if (((uint32)0U != (u32ErrStatus & ((uint32)FLEXCAN_ESR1_BOFFINT_MASK))) && ((uint32)0U != (pBase->CTRL1 & ((uint32)FLEXCAN_CTRL1_BOFFMSK_MASK))))
        {
            pBase->ESR1 = FLEXCAN_ESR1_BOFFINT_MASK;
            /* Invoke callback */
            (void)FlexCAN_ErrorCallback(Instance, FLEXCAN_EVENT_BUSOFF, u32ErrStatus, pState);
        }
    }
    else
    {
        pBase->ESR1 = FLEXCAN_IP_BUS_OFF_INT;
    }
}
#endif /* (FLEXCAN_IP_FEATURE_BUSOFF_ERROR_INTERRUPT_UNIFIED) */
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SendBlocking
 * Description   : This function sends a CAN frame using a configured message
 * buffer. The function blocks until either the frame was sent, or the specified
 * timeout expired.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_SendBlocking_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SendBlocking(uint8 instance,
                                              uint8 mb_idx,
                                              const Flexcan_Ip_DataInfoType * tx_info,
                                              uint32 msg_id,
                                              const uint8 * mb_data,
                                              uint32 timeout_ms
                                             )
{
    Flexcan_Ip_StatusType Result;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[instance];

    uint32 TimeStart = 0U;
    uint32 TimeElapsed = 0U;
    uint32 Ms2Ticks = OsIf_MicrosToTicks((timeout_ms * 1000U), FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
    uint32 Us2Ticks = 0U;
    uint32 FlexcanMbConfig = 0;
    volatile uint32 * FlexcanMb = FlexCAN_GetMsgBuffRegion(pBase, mb_idx);

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(tx_info != NULL_PTR);
#endif

    Result = FlexCAN_StartSendData(instance, mb_idx, tx_info, msg_id, mb_data);

    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        TimeStart = OsIf_GetCounter(FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
        while (FlexCAN_GetMsgBuffIntStatusFlag(pBase, mb_idx) != 1U)
        {
            TimeElapsed += OsIf_GetElapsed(&TimeStart, FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
            if (TimeElapsed >= Ms2Ticks)
            {
                Result = FLEXCAN_STATUS_TIMEOUT;
                break;
            }
        }

        if ((FLEXCAN_STATUS_TIMEOUT == Result) && (pState->mbs[mb_idx].state == FLEXCAN_MB_TX_BUSY))
        {
            /* Clear message buffer flag */
            FlexCAN_ClearMsgBuffIntStatusFlag(pBase, mb_idx);
            FlexcanMbConfig = *FlexcanMb;
            /* Reset the code and set to ABORT */
            FlexcanMbConfig &= (uint32)(~FLEXCAN_IP_CS_CODE_MASK);
            FlexcanMbConfig |= ((uint32)(((uint32)FLEXCAN_TX_ABORT & (uint32)0x1F) << (uint8)FLEXCAN_IP_CS_CODE_SHIFT) & (uint32)FLEXCAN_IP_CS_CODE_MASK);
            *FlexcanMb = FlexcanMbConfig;

            /* Wait to finish abort operation */
            Us2Ticks = OsIf_MicrosToTicks(FLEXCAN_IP_TIMEOUT_DURATION, FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
            TimeStart = OsIf_GetCounter(FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
            TimeElapsed = 0U;
            while (0U == FlexCAN_GetMsgBuffIntStatusFlag(pBase, mb_idx))
            {
                TimeElapsed += OsIf_GetElapsed(&TimeStart, FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
                if (TimeElapsed >= Us2Ticks)
                {
                    break;
                }
            }
        }

        FlexcanMbConfig = *FlexcanMb;
        FlexCAN_UnlockRxMsgBuff(pBase);

        /* Check if the MBs have been safely Inactivated */
        if ((uint32)FLEXCAN_TX_INACTIVE == ((FlexcanMbConfig & FLEXCAN_IP_CS_CODE_MASK) >> FLEXCAN_IP_CS_CODE_SHIFT))
        {
            /* Transmission have occurred either as normal or during aborting */
            Result = FLEXCAN_STATUS_SUCCESS;
        }

        if ((uint32)FLEXCAN_TX_ABORT == ((FlexcanMbConfig & FLEXCAN_IP_CS_CODE_MASK) >> FLEXCAN_IP_CS_CODE_SHIFT))
        {
            {
                /* Transmission have been aborted by SW when timeout occurred */
                Result = FLEXCAN_STATUS_TIMEOUT;
            }
        }

        if (TRUE == pState->mbs[mb_idx].isRemote)
        {
            /* If the frame was a remote frame, clear the flag only if the response was
            * not received yet. If the response was received, leave the flag set in order
            * to be handled when the user calls FlexCAN_Ip_Receive. */
            if ((uint32)FLEXCAN_RX_EMPTY == ((FlexcanMbConfig & FLEXCAN_IP_CS_CODE_MASK) >> FLEXCAN_IP_CS_CODE_SHIFT))
            {
                /* Clear message buffer flag */
                FlexCAN_ClearMsgBuffIntStatusFlag(pBase, mb_idx);
            }
        }
        else
        {
            /* Clear message buffer flag */
            FlexCAN_ClearMsgBuffIntStatusFlag(pBase, mb_idx);
        }
        /* Reset MB's state to be ready for next transmission */
        pState->mbs[mb_idx].state = FLEXCAN_MB_IDLE;
    }
    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetRxMbGlobalMask
 * Description   : Set Rx Message Buffer global mask as the mask value provided.
 *
 *END**************************************************************************/

/* implements FlexCAN_Ip_SetRxMbGlobalMask_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetRxMbGlobalMask_Privileged(uint8 instance, uint32 mask)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#endif
    boolean Disabled = !FlexCAN_IsEnabled(pBase);

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    Freeze = FlexCAN_IsFreezeMode(pBase);

    if ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result))
    {
        Result = FLEXCAN_STATUS_ERROR;
    }
#endif
    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        FlexCAN_SetRxMsgBuffGlobalMask(pBase, mask);
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }
    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_EnterFreezeMode
 * Description   : Enter Driver In freeze Mode.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_EnterFreezeMode_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_EnterFreezeMode_Privileged(uint8 instance)
{
        FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

        return FlexCAN_EnterFreezeMode(pBase);
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_ExitFreezeMode
 * Description   : Exit Driver from freeze Mode.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_ExitFreezeMode_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_ExitFreezeMode_Privileged(uint8 instance)
{
        FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

        return FlexCAN_ExitFreezeMode(pBase);
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetRxIndividualMask
 * Description   : Set Rx individual mask as absolute value provided by mask parameter
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_SetRxIndividualMask_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetRxIndividualMask_Privileged(uint8 instance, uint8 mb_idx, uint32 mask)
{
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#endif

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
    if ((mb_idx > (((pBase->MCR) & FLEXCAN_MCR_MAXMB_MASK) >> FLEXCAN_MCR_MAXMB_SHIFT)) || (mb_idx >= FLEXCAN_RXIMR_COUNT))
    {
        Result = FLEXCAN_STATUS_BUFF_OUT_OF_RANGE;
    }
    else
    {
#endif

    boolean Disabled = !FlexCAN_IsEnabled(pBase);

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    Freeze = FlexCAN_IsFreezeMode(pBase);

    if ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result))
    {
        Result = FLEXCAN_STATUS_ERROR;
    }
#endif
    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        FlexCAN_SetRxIndividualMask(pBase, mb_idx, mask);
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    }
#endif
    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetRxFifoGlobalMask
 * Description   : Set RxFifo Global Mask.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_SetRxFifoGlobalMask_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetRxFifoGlobalMask_Privileged(uint8 instance, uint32 mask)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    boolean Disabled = !FlexCAN_IsEnabled(pBase);
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#endif

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    Freeze = FlexCAN_IsFreezeMode(pBase);
    if ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result))
    {
        Result = FLEXCAN_STATUS_ERROR;
    }
#endif

    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        FlexCAN_SetRxFifoGlobalMask(pBase, mask);
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_Deinit
 * Description   : Shutdown a FlexCAN module.
 * This function will disable all FlexCAN interrupts, and disable the FlexCAN.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_Deinit_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_Deinit_Privileged(uint8 instance)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    Flexcan_Ip_StatusType Result;

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif

    /* Enter Freeze Mode Required before to enter Disabled Mode */
    Result = FlexCAN_EnterFreezeMode(pBase);
    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        /* Reset registers */
        FlexCAN_SetRegDefaultVal(pBase);
        /* wait for disable */
        (void)FlexCAN_Disable(pBase);
        /* Clear state pointer that is checked by FLEXCAN_DRV_Init */
        Flexcan_Ip_apxState[instance] = NULL_PTR;

    #if (STD_ON == FLEXCAN_IP_SET_USER_ACCESS_ALLOWED_AVAILABLE)
        /* Ignore if the instance doesn't support REG_PROT */
        if (instance < FLEXCAN_IP_CTRL_REG_PROT_SUPPORT_U8)
        {
            /* Clear the UAA bit in REG_PROT to reset the elevation requirement */
            OsIf_Trusted_Call1param(FlexCAN_ClrUserAccessAllowed, pBase);
        }
    #endif
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_MainFunctionRead
 * Description   : Process received Rx MessageBuffer or RxFifo.
 * This function read the messages received as pulling or if the Interrupts are disabled.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_MainFunctionRead_Activity */
void FlexCAN_Ip_MainFunctionRead(uint8 instance, uint8 mb_idx)
{
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[instance];

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
    #if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
    DevAssert((mb_idx < (uint8)FLEXCAN_IP_FEATURE_MAX_MB_NUM) || (FLEXCAN_IP_MB_ENHANCED_RXFIFO == mb_idx));
    #else
    DevAssert(mb_idx < (uint8)FLEXCAN_IP_FEATURE_MAX_MB_NUM);
    #endif
#endif

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
    if ((uint8)FLEXCAN_IP_MB_ENHANCED_RXFIFO == mb_idx)
    {
        if (TRUE == FlexCAN_IsEnhancedRxFifoEnabled(pBase))
        {
            if (FlexCAN_GetEnhancedRxFIFOStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_FRAME_AVAILABLE) != 0U)
            {
                FlexCAN_IRQHandlerEnhancedRxFIFO(instance, FLEXCAN_IP_ENHANCED_RXFIFO_FRAME_AVAILABLE);
            }
        }
    }
    else
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON) */
    {
        if ((TRUE == pState->bIsLegacyFifoEn) && (mb_idx <= FLEXCAN_IP_LEGACY_RXFIFO_OVERFLOW))
        {
            /* just process available legacy fifo event only */
            if ((uint8)FLEXCAN_IP_MB_HANDLE_RXFIFO == mb_idx)
            {
                if (FlexCAN_GetMsgBuffIntStatusFlag(pBase, FLEXCAN_IP_LEGACY_RXFIFO_FRAME_AVAILABLE) != 0U)
                {
                    FlexCAN_IRQHandlerRxFIFO(instance, FLEXCAN_IP_LEGACY_RXFIFO_FRAME_AVAILABLE);
                }
            }
        }
        else
        {
            if (FlexCAN_GetMsgBuffIntStatusFlag(pBase, mb_idx) != 0U)
            {
                /* Check mailbox completed reception */
                if (FLEXCAN_MB_RX_BUSY == pState->mbs[mb_idx].state)
                {
                    FlexCAN_IRQHandlerRxMB(instance, mb_idx);
                }
            }
        }
    }
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_MainFunctionBusOff
 * Description   : Check if BusOff occurred
 * This function check the bus off event.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_MainFunctionBusOff_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_MainFunctionBusOff_Privileged(uint8 instance)
{
    Flexcan_Ip_StatusType eRetVal = FLEXCAN_STATUS_ERROR;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[instance];
    uint32 u32ErrStatus = 0U;

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif

    /* Get error status */
    u32ErrStatus = pBase->ESR1;

    if (0U != (u32ErrStatus & FLEXCAN_ESR1_BOFFINT_MASK))
    {
        /* Invoke callback */
        (void)FlexCAN_ErrorCallback(instance, FLEXCAN_EVENT_BUSOFF, u32ErrStatus, pState);
        /* Clear BusOff Status Flag */
        pBase->ESR1 = FLEXCAN_ESR1_BOFFINT_MASK;
        eRetVal = FLEXCAN_STATUS_SUCCESS;
    }
    return eRetVal;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_MainFunctionWrite
 * Description   : Process transmitted Tx MessageBuffer
 * This function check the message if have been sent.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_MainFunctionWrite_Activity */
void FlexCAN_Ip_MainFunctionWrite(uint8 instance, uint8 mb_idx)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[instance];
    Flexcan_Ip_MsgBuffType TxBuff;
    volatile const uint32 * FlexcanMb = FlexCAN_GetMsgBuffRegion(pBase, mb_idx);

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif

    if (FlexCAN_GetMsgBuffIntStatusFlag(pBase, mb_idx) != 0U)
    {
        if (FLEXCAN_MB_TX_BUSY == pState->mbs[mb_idx].state)
        {
            TxBuff.cs = *FlexcanMb;
            TxBuff.time_stamp = FlexCAN_GetMsgBuffTimestamp(pBase, mb_idx);
            FlexCAN_UnlockRxMsgBuff(pBase);
            {
                if (pState->mbs[mb_idx].isRemote)
                {
                    pState->mbs[mb_idx].time_stamp = TxBuff.time_stamp;
                    /* If the frame was a remote frame, clear the flag only if the response was
                    * not received yet. If the response was received, leave the flag set in order
                    * to be handled when the user calls FlexCAN_Ip_Receive. */
                    if ((uint32)FLEXCAN_RX_EMPTY == ((TxBuff.cs & FLEXCAN_IP_CS_CODE_MASK) >> FLEXCAN_IP_CS_CODE_SHIFT))
                    {
                        /* Clear message buffer flag */
                        FlexCAN_ClearMsgBuffIntStatusFlag(pBase, mb_idx);
                    }
                }
                else
                {
                    pState->mbs[mb_idx].time_stamp = TxBuff.time_stamp;
                    /* Clear message buffer flag */
                    FlexCAN_ClearMsgBuffIntStatusFlag(pBase, mb_idx);
                }

                pState->mbs[mb_idx].state = FLEXCAN_MB_IDLE;

                /* Invoke callback */
                if (pState->callback != NULL_PTR)
                {
                    pState->callback(instance, FLEXCAN_EVENT_TX_COMPLETE, mb_idx, pState);
                }
            }
        }
    }
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_GetStopMode
 * Description   : Check if the FlexCAN instance is STOPPED.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_GetStopMode_Activity */
boolean FlexCAN_Ip_GetStopMode_Privileged(uint8 instance)
{
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

    return ((FLEXCAN_MCR_LPMACK_MASK == (pBase->MCR & FLEXCAN_MCR_LPMACK_MASK)) ? TRUE : FALSE);
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_GetStartMode
 * Description   : Check if the FlexCAN instance is STARTED.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_GetStartMode_Activity */
boolean FlexCAN_Ip_GetStartMode_Privileged(uint8 instance)
{
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

    return ((0U == (pBase->MCR & (FLEXCAN_MCR_LPMACK_MASK | FLEXCAN_MCR_FRZACK_MASK))) ? TRUE : FALSE);
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetStartMode
 * Description   : Set the FlexCAN instance in START mode.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_SetStartMode_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetStartMode_Privileged(uint8 instance)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

    /* Start critical section: implementation depends on integrator */
    SchM_Enter_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_07();
    /* Enable Flexcan Module */
    pBase->MCR &= ~FLEXCAN_MCR_MDIS_MASK;
    /* End critical section: implementation depends on integrator */
    SchM_Exit_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_07();

    return (FlexCAN_ExitFreezeMode(pBase));
}


/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetStopMode
 * Description   : Set the FlexCAN instance in STOP mode.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_SetStopMode_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetStopMode_Privileged(uint8 instance)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    Flexcan_Ip_StatusType Status;

    Status = FlexCAN_EnterFreezeMode(pBase);
    if (FLEXCAN_STATUS_SUCCESS == Status)
    {
        /* TODO: deactivate all pending tx, this should be move to IPW */

        /* TODO:Clear all flag */

        /* TODO: reset MB status */

        /* TODO: disable all interrupt */

        Status = FlexCAN_Disable(pBase);
    }
    return Status;
}


/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetRxMaskType
 * Description   : Set RX masking type.
 * This function will set RX masking type as RX global mask or RX individual
 * mask.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_SetRxMaskType_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetRxMaskType_Privileged(uint8 instance, Flexcan_Ip_RxMaskType type)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    boolean Disabled = !FlexCAN_IsEnabled(pBase);
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#endif

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    Freeze = FlexCAN_IsFreezeMode(pBase);

    if ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result))
    {
        Result = FLEXCAN_STATUS_ERROR;
    }
#endif

    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        /* Start critical section: implementation depends on integrator */
        SchM_Enter_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_08();
        FlexCAN_SetRxMaskType(pBase, type);
        /* End critical section: implementation depends on integrator */
        SchM_Exit_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_08();
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetBitrate
 * Description   : Set FlexCAN baudrate.
 * This function will set up all the time segment values for classical frames or the
 * extended time segments. Those time segment values are passed in by the user and
 * are based on the required baudrate.
 *
 *END**************************************************************************/

/* implements  FlexCAN_Ip_SetBitrate_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetBitrate_Privileged(uint8 instance, const Flexcan_Ip_TimeSegmentType * bitrate, boolean enhExt)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(bitrate != NULL_PTR);
#endif
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    /* Check if the FlexCAN is enabled or not */
    boolean Disabled = ((pBase->MCR & FLEXCAN_MCR_MDIS_MASK) != 0U) ? TRUE : FALSE;
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#endif

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_OFF)
    (void)enhExt;
#endif
    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    /* Check if controller is in freeze mode or not */
    Freeze = ((pBase->MCR & (FLEXCAN_MCR_FRZACK_MASK)) != 0U)? TRUE : FALSE;

    if ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result))
    {
        Result = FLEXCAN_STATUS_ERROR;
    }
#endif

    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        /* Start critical section: implementation depends on integrator */
        SchM_Enter_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_14();
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
        /* Enable the use of extended bit time definitions */
        FlexCAN_EnhCbtEnable(pBase, enhExt);

        if (TRUE == enhExt)
        {
            /* Set extended time segments*/
            FlexCAN_SetEnhancedNominalTimeSegments(pBase, bitrate);
        }
        else
#endif
        {
            if (TRUE == FlexCAN_IsExCbtEnabled(pBase))
            {
                FlexCAN_SetExtendedTimeSegments(pBase, bitrate);
            }
            else
            {
                FlexCAN_SetTimeSegments(pBase, bitrate);
            }
        }
        /* End critical section: implementation depends on integrator */
        SchM_Exit_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_14();
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_GetBitrate
 * Description   : Get FlexCAN Baudrate.
 * This function will be return the current bit rate settings for classical frames
 * or the arbitration phase of FD frames.
 *
 *END**************************************************************************/
 /* implements   FlexCAN_Ip_GetBitrate_Activity */
boolean FlexCAN_Ip_GetBitrate(uint8 instance, Flexcan_Ip_TimeSegmentType * bitrate)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(bitrate != NULL_PTR);
#endif
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    boolean EnhCbt = FALSE;

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
    EnhCbt = FlexCAN_IsEnhCbtEnabled(pBase);
    if (TRUE == EnhCbt)
    {
        FlexCAN_GetEnhancedNominalTimeSegments(pBase, bitrate);
    }
    else
#endif
    {
        if (TRUE == FlexCAN_IsExCbtEnabled(pBase))
        {
            /* Get the Extended time segments*/
            FlexCAN_GetExtendedTimeSegments(pBase, bitrate);
        }
        else
        {
            /* Get the time segments*/
            FlexCAN_GetTimeSegments(pBase, bitrate);
        }
    }
    return EnhCbt;
}

#if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_ClearTDCFail
 * Description   : This function clear the TDC Fail flag.
 *
 *END**************************************************************************/
/* implements   FlexCAN_Ip_ClearTDCFail_Activity */
void FlexCAN_Ip_ClearTDCFail(uint8 u8Instance)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(u8Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];

    /* Start critical section: implementation depends on integrator */
    SchM_Enter_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_09();
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
    /* Check if enhaced CBT is Enabled */
    if (TRUE == FlexCAN_IsEnhCbtEnabled(pBase))
    {
        pBase->ETDC |=  FLEXCAN_ETDC_ETDCFAIL_MASK;
    }
    else
#endif
    {
        pBase->FDCTRL |= FLEXCAN_FDCTRL_TDCFAIL_MASK;
    }
    /* End critical section: implementation depends on integrator */
    SchM_Exit_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_09();
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_GetTDCFail
 * Description   : This function get the value of the TDC Fail flag.
 *
 *END**************************************************************************/

/* implements    FlexCAN_Ip_GetTDCFail_Activity */
boolean FlexCAN_Ip_GetTDCFail(uint8 u8Instance)
{
    boolean Value=FALSE;
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(u8Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
    /* Check if enhaced CBT is Enabled */
    if (TRUE == FlexCAN_IsEnhCbtEnabled(pBase))
    {
        Value = ((pBase->ETDC & FLEXCAN_ETDC_ETDCFAIL_MASK) == FLEXCAN_ETDC_ETDCFAIL_MASK) ? TRUE : FALSE;
    }
    else
#endif
    {
        Value = ((pBase->FDCTRL & FLEXCAN_FDCTRL_TDCFAIL_MASK) == FLEXCAN_FDCTRL_TDCFAIL_MASK) ? TRUE : FALSE;
    }
    return Value;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_GetTDCValue
 * Description   : This function get the value of the Transceiver Delay Compensation.
 *
 *END**************************************************************************/

/* implements FlexCAN_Ip_GetTDCValue_Activity */
uint8 FlexCAN_Ip_GetTDCValue(uint8 u8Instance)
{
    uint8 Value = 0;
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(u8Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
    /* Check if enhaced CBT is Enabled */
    if (TRUE == FlexCAN_IsEnhCbtEnabled(pBase))
    {
        Value = (uint8)((pBase->ETDC& FLEXCAN_ETDC_ETDCVAL_MASK) >> FLEXCAN_ETDC_ETDCVAL_SHIFT);
    }
    else
#endif
    {
        Value = (uint8)((pBase->FDCTRL & FLEXCAN_FDCTRL_TDCVAL_MASK) >> FLEXCAN_FDCTRL_TDCVAL_SHIFT);
    }
    return Value;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetBitrateCbt
 * Description   : Set FlexCAN bitrate.
 * This function will set up all the time segment values for the data phase of
 * FD frames. Those time segment values are passed in by the user and are based
 * on the required baudrate.
 *
 *END**************************************************************************/

/* implements  FlexCAN_Ip_SetBitrateCbt_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetBitrateCbt_Privileged(uint8 instance, const Flexcan_Ip_TimeSegmentType * bitrate, boolean bitRateSwitch)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(bitrate != NULL_PTR);
#endif
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    boolean FdEnable = FlexCAN_IsFDEnabled(pBase);
    /* Check if the FlexCAN is enabled or not */
    boolean Disabled = ((pBase->MCR & FLEXCAN_MCR_MDIS_MASK) != 0U) ? TRUE : FALSE;
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#endif

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)

    /* Check if controller is in freeze mode or not */
    Freeze = ((pBase->MCR & (FLEXCAN_MCR_FRZACK_MASK)) != 0U)? TRUE : FALSE;

    if ((FALSE == FdEnable) || ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result)))
    {
        Result = FLEXCAN_STATUS_ERROR;
    }
#endif

    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
        boolean enhCbt = FlexCAN_IsEnhCbtEnabled(pBase);
#endif
        /* Start critical section: implementation depends on integrator */
        SchM_Enter_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_15();
        FlexCAN_SetFDEnabled(pBase, FdEnable, bitRateSwitch);
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
        if (TRUE == enhCbt)
        {
            FlexCAN_SetEnhancedDataTimeSegments(pBase, bitrate);
        }
        else
#endif
        {
            /* Set time segments*/
            FlexCAN_SetFDTimeSegments(pBase, bitrate);
        }
        /* End critical section: implementation depends on integrator */
        SchM_Exit_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_15();
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_GetBitrateFD
 * Description   : Get FlexCAN baudrate.
 * This function will be return the current bit rate settings for the data phase
 * of FD frames.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_GetBitrateFD_Activity */
boolean FlexCAN_Ip_GetBitrateFD(uint8 instance, Flexcan_Ip_TimeSegmentType * bitrate)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(bitrate != NULL_PTR);
#endif
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    boolean EnhCbt = FALSE;

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
    EnhCbt = FlexCAN_IsEnhCbtEnabled(pBase);

    if (TRUE == EnhCbt)
    {
        FlexCAN_GetEnhancedDataTimeSegments(pBase, bitrate);
    }
    else
#endif
    {
        /* Get the time segments*/
        FlexCAN_GetFDTimeSegments(pBase, bitrate);
    }
    return EnhCbt;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetTDCOffset
 * Description   : Enables/Disables the Transceiver Delay Compensation feature and sets
 * the Transceiver Delay Compensation Offset.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_SetTDCOffset_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetTDCOffset_Privileged(uint8 instance, boolean enable, uint8 offset)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    boolean Disabled = !FlexCAN_IsEnabled(pBase);
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#if defined(FLEXCAN_IP_FEATURE_FD_NOT_ALL_INSTANCES)
    /* Check if the instance support FD capability */
    DevAssert(TRUE == FlexCAN_IsFDAvailable(pBase));
#endif
#endif

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    Freeze = FlexCAN_IsFreezeMode(pBase);

    if ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result))
    {
        Result = FLEXCAN_STATUS_ERROR;
    }
#endif

    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        /* Check if enhaced CBT is Enabled */
        SchM_Enter_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_16();
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
        /* End critical section: implementation depends on integrator */
        if (FLEXCAN_CTRL2_BTE_MASK == (pBase->CTRL2 & FLEXCAN_CTRL2_BTE_MASK))
        {
            FlexCAN_SetEnhancedTDCOffset(pBase, enable, offset);
        }
        else
#endif
        {
            /* Enable/Disable TDC and set the TDC Offset */
            FlexCAN_SetTDCOffset(pBase, enable, offset);
        }
        SchM_Exit_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_16();
        /* Check if enhaced CBT is Enabled */
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }
    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetTxArbitrationStartDelay
 * Description   : Set FlexCAN Tx Arbitration Start Delay.
 * This function will set how many CAN bits the Tx arbitration process start point can
 * be delayed from the first bit of CRC field on CAN bus.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_SetTxArbitrationStartDelay_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetTxArbitrationStartDelay_Privileged(uint8 instance,  uint8 value)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    boolean Disabled = !FlexCAN_IsEnabled(pBase);
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#endif

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    Freeze = FlexCAN_IsFreezeMode(pBase);

    if ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result))
    {
        Result = FLEXCAN_STATUS_ERROR;
    }
#endif

    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        /* Start critical section: implementation depends on integrator */
        SchM_Enter_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_17();
        FlexCAN_SetTxArbitrationStartDelay(pBase, value);
        /* End critical section: implementation depends on integrator */
        SchM_Exit_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_17();
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_GetBuffStatusFlag
 * Description   : Get FlexCAN Message Buffer Status Flag for an operation.
 * In case of a complete operation this flag is set.
 * In case msgBuff is 255 will return Enhanced Overflow Status Flag.
 *END**************************************************************************/
/* implements FlexCAN_Ip_GetBuffStatusFlag_Activity */
boolean FlexCAN_Ip_GetBuffStatusFlag(uint8 instance, uint8 msgBuffIdx)
{
    boolean ReturnResult;
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
    if (FLEXCAN_IP_MB_ENHANCED_RXFIFO == msgBuffIdx)
    {
        ReturnResult = ((1U == FlexCAN_GetEnhancedRxFIFOStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_OVERFLOW)) ? TRUE : FALSE);
    }
    else
#endif
    {
        ReturnResult = ((1U == FlexCAN_GetMsgBuffIntStatusFlag(pBase, msgBuffIdx)) ? TRUE : FALSE);
    }
    return ReturnResult;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_ClearBuffStatusFlag
 * Description   : Clear FlexCAN Message Buffer Status Flag.
 * In case msgBuff is 255 will clear Enhanced Overflow Status Flag.
 *END**************************************************************************/
/* implements FlexCAN_Ip_ClearBuffStatusFlag_Activity */
void FlexCAN_Ip_ClearBuffStatusFlag(uint8 instance, uint8 msgBuffIdx)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
    if (FLEXCAN_IP_MB_ENHANCED_RXFIFO == msgBuffIdx)
    {
        FlexCAN_ClearEnhancedRxFifoIntStatusFlag(pBase, FLEXCAN_IP_ENHANCED_RXFIFO_OVERFLOW);
    }
    else
#endif
    {
        FlexCAN_ClearMsgBuffIntStatusFlag(pBase, msgBuffIdx);
    }
}

#endif

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_EnableInterrupts
 * Description   : Enable all mb interrupts configured.
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_EnableInterrupts_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_EnableInterrupts_Privileged(uint8 u8Instance)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_ERROR;
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[u8Instance];

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(u8Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif

    if (TRUE == FlexCAN_IsEnabled(pBase))
    {
        FlexCAN_EnableInterrupts(pBase, u8Instance);
    #if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
        if (FALSE == pState->enhancedFifoOutput.isPolling)
        {   /* Check if Enabled Enhanced Fifo */
            if (TRUE == FlexCAN_IsEnhancedRxFifoEnabled(pBase))
            {
                FlexCAN_SetEnhancedRxFifoIntAll(pBase, TRUE);
            }
        }
    #endif
        pState->isIntActive = TRUE;
        Result = FLEXCAN_STATUS_SUCCESS;
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_DisableInterrupts
 * Description   : Enable all interrupts configured.
 *
 *END**************************************************************************/
 /* implements FlexCAN_Ip_DisableInterrupts_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_DisableInterrupts_Privileged(uint8 u8Instance)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_ERROR;
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[u8Instance];

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(u8Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif

    if (TRUE == FlexCAN_IsEnabled(pBase))
    {
        FlexCAN_DisableInterrupts(pBase);
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
        /* Check if Enabled Enhanced Fifo */
        if (TRUE == FlexCAN_IsEnhancedRxFifoEnabled(pBase))
        {
            FlexCAN_SetEnhancedRxFifoIntAll(pBase, FALSE);
        }
#endif
        pState->isIntActive = FALSE;
        Result = FLEXCAN_STATUS_SUCCESS;
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetErrorInt
 * Description   : Enable\Disable Error or BusOff Interrupt
 *
 *END**************************************************************************/
/* implements FlexCAN_Ip_SetErrorInt_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetErrorInt_Privileged(uint8 u8Instance, Flexcan_Ip_ErrorIntType type, boolean enable)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(u8Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
    boolean Disabled = !FlexCAN_IsEnabled(pBase);

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }

    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        switch (type)
        {
            case FLEXCAN_IP_INT_BUSOFF:
            {
                FlexCAN_SetErrIntCmd(pBase, FLEXCAN_INT_BUSOFF, enable);
                break;
            }
            case FLEXCAN_IP_INT_ERR:
            {
                FlexCAN_SetErrIntCmd(pBase, FLEXCAN_INT_ERR, enable);
                break;
            }
            case FLEXCAN_IP_INT_ERR_FAST :
            {
                FlexCAN_SetErrIntCmd(pBase, FLEXCAN_INT_ERR_FAST, enable);
                break;
            }
            case FLEXCAN_IP_INT_RX_WARNING :
            {
                #if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
                    if (TRUE != FlexCAN_IsFreezeMode(pBase))
                    {
                        Result = FLEXCAN_STATUS_ERROR;
                    }else
                #endif
                {
                    FlexCAN_SetErrIntCmd(pBase, FLEXCAN_INT_RX_WARNING, enable);
                }
                break;
            }
            case FLEXCAN_IP_INT_TX_WARNING :
            {
                #if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
                    if (TRUE != FlexCAN_IsFreezeMode(pBase))
                    {
                        Result = FLEXCAN_STATUS_ERROR;
                    }else
                #endif
                {
                    FlexCAN_SetErrIntCmd(pBase, FLEXCAN_INT_TX_WARNING, enable);
                }
                break;
            }
            default:
            {
                #if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
                    DevAssert(FALSE);
                    /* Should not get here */
                #endif
                break;
            }
        }
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_AbortTransfer
 * Description   : This function shuts down the FLEXCAN by disabling interrupts and
 *                 the transmitter/receiver.
 * This function disables the FLEXCAN interrupts, disables the transmitter and
 * receiver.
 *
 *END**************************************************************************/
/* implements    FlexCAN_Ip_AbortTransfer_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_AbortTransfer(uint8 u8Instance, uint8 mb_idx)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(u8Instance < FLEXCAN_IP_INSTANCE_COUNT);
    #if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
    DevAssert((mb_idx < (uint8)FLEXCAN_IP_FEATURE_MAX_MB_NUM) || (FLEXCAN_IP_MB_ENHANCED_RXFIFO == mb_idx));
    #else
    DevAssert(mb_idx < (uint8)FLEXCAN_IP_FEATURE_MAX_MB_NUM);
    #endif
#endif
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[u8Instance];
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;

    if (mb_idx < (uint8)FLEXCAN_IP_FEATURE_MAX_MB_NUM)
    {
        if ((FLEXCAN_MB_IDLE == pState->mbs[mb_idx].state)
        #if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
            || (FLEXCAN_MB_DMA_ERROR == pState->mbs[mb_idx].state)
        #endif
           )
        {
            Result = FLEXCAN_STATUS_NO_TRANSFER_IN_PROGRESS;
        }
        else
        {
            /* Disable message buffer interrupt */
            FLEXCAN_ClearMsgBuffIntCmd(pBase, u8Instance, mb_idx, pState->isIntActive);
            if (FLEXCAN_MB_RX_BUSY == pState->mbs[mb_idx].state)
            {
                Result = FlexCAN_AbortRxTransfer(u8Instance, mb_idx);
            }
            if (FLEXCAN_MB_TX_BUSY == pState->mbs[mb_idx].state)
            {
                Result = FlexCAN_AbortTxTransfer(u8Instance, mb_idx);
                /* Clear message buffer status flag */
                FlexCAN_ClearMsgBuffIntStatusFlag(pBase, mb_idx);
            }
            pState->mbs[mb_idx].state = FLEXCAN_MB_IDLE;
        }
    }
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
    else
    {
        /* Check if transfer is done over DMA and stop transfer */
        if ((FLEXCAN_MB_RX_BUSY == pState->enhancedFifoOutput.state) && (FLEXCAN_RXFIFO_USING_DMA == pState->transferType))
        {
            (void)Dma_Ip_SetLogicChannelCommand(pState->rxFifoDMAChannel, DMA_IP_CH_CLEAR_HARDWARE_REQUEST);
            if (FLEXCAN_MB_RX_BUSY == pState->enhancedFifoOutput.state)
            {
                pState->enhancedFifoOutput.state = FLEXCAN_MB_IDLE;
            }
            else
            {
                Result = FLEXCAN_STATUS_NO_TRANSFER_IN_PROGRESS;
            }
        }
        else
        {
            Result = FLEXCAN_STATUS_NO_TRANSFER_IN_PROGRESS;
        }
    }
#endif /* if FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */
#endif /* if FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO */

    return Result;
}

/* implements    FlexCAN_Ip_SetRxMb14Mask_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetRxMb14Mask_Privileged(uint8 instance, uint32 mask)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#endif
    boolean Disabled = !FlexCAN_IsEnabled(pBase);

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    Freeze = FlexCAN_IsFreezeMode(pBase);

    if ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result))
    {
        Result = FLEXCAN_STATUS_ERROR;
    }
#endif
    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        pBase->RX14MASK = mask;
    }
    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }

    return Result;
}

/* implements    FlexCAN_Ip_SetRxMb15Mask_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetRxMb15Mask_Privileged(uint8 instance, uint32 mask)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#endif
    boolean Disabled = !FlexCAN_IsEnabled(pBase);

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    Freeze = FlexCAN_IsFreezeMode(pBase);

    if ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result))
    {
        Result = FLEXCAN_STATUS_ERROR;
    }
#endif
    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        pBase->RX15MASK = mask;
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetListenOnlyMode
 * Description   : Set FlexCAN Listen Only.
 * This function will enable or disable Listen Only mode.
 *
 *END**************************************************************************/
/* implements  FlexCAN_Ip_SetListenOnlyMode_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetListenOnlyMode_Privileged(uint8 instance, const boolean enable)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    boolean Disabled = !FlexCAN_IsEnabled(pBase);
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#endif

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    Freeze = FlexCAN_IsFreezeMode(pBase);

    if ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result))
    {
        Result = FLEXCAN_STATUS_ERROR;
    }
#endif

    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        /* Start critical section: implementation depends on integrator */
        SchM_Enter_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_10();
        FlexCAN_SetListenOnlyMode(pBase, enable);
        /* End critical section: implementation depends on integrator */
        SchM_Exit_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_10();
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_GetListenOnlyMode
 * Description   : Check if Listen Only mode is ENABLE.
 *
 *END**************************************************************************/
/* implements  FlexCAN_Ip_GetListenOnlyMode_Activity */
boolean FlexCAN_Ip_GetListenOnlyMode(uint8 instance)
{
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];

    return FlexCAN_IsListenOnlyModeEnabled(pBase);
}


#if (FLEXCAN_IP_FEATURE_HAS_TS_ENABLE == STD_ON)
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_ConfigTimeStamp_Privileged
 * Description   : Timestamp configuration
 *
 *END**************************************************************************/
/* implements  FlexCAN_Ip_ConfigTimeStamp_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_ConfigTimeStamp_Privileged(uint8 instance, const Flexcan_Ip_TimeStampConfigType * time_stamp)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[instance];
    boolean Disabled = !FlexCAN_IsEnabled(pBase);
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#endif

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    Freeze = FlexCAN_IsFreezeMode(pBase);

    if ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result))
    {
        Result = FLEXCAN_STATUS_ERROR;
    }
#endif

    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        FlexCAN_ConfigTimestamp(instance, pBase, time_stamp);
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }

    return Result;
}
#endif /* FLEXCAN_IP_FEATURE_HAS_TS_ENABLE */

#if (FLEXCAN_IP_FEATURE_HAS_PRETENDED_NETWORKING == STD_ON)
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_ConfigPN
 * Description   : Configures Pretended Networking settings.
 *
 *END**************************************************************************/
/* implements  FlexCAN_Ip_ConfigPN_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_ConfigPN_Privileged(uint8 u8Instance,
                          boolean bEnable,
                          const Flexcan_Ip_PnConfigType * pPnConfig)
{
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
    boolean Disabled = !FlexCAN_IsEnabled(pBase);
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    boolean Freeze = FALSE;
#endif

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(u8Instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert((FALSE == bEnable) || (pPnConfig != NULL_PTR));
#endif

    if (0U == u8Instance)
    {
        if (TRUE == Disabled)
        {
            Result = FlexCAN_Enable(pBase);
        }

    #if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
        Freeze = FlexCAN_IsFreezeMode(pBase);

        if ((FALSE == Freeze) && (FLEXCAN_STATUS_SUCCESS == Result))
        {
            Result = FLEXCAN_STATUS_ERROR;
        }
    #endif

        if (FLEXCAN_STATUS_SUCCESS == Result)
        {
            if (bEnable)
            {
                FlexCAN_ConfigPN(pBase, pPnConfig);
            }
            SchM_Enter_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_12();
            FlexCAN_SetPN(pBase, bEnable);
            SchM_Exit_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_12();
        }

        if (TRUE == Disabled)
        {
            Status = FlexCAN_Disable(pBase);
            if (FLEXCAN_STATUS_SUCCESS != Status)
            {
                Result = Status;
            }
        }
    }
    else
    {
        Result = FLEXCAN_STATUS_ERROR;
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_GetWMB
 * Description   : Extracts one of the frames which triggered the wake up event.
 *
 *END**************************************************************************/
/* implements  FlexCAN_Ip_GetWMB_Activity */
void FlexCAN_Ip_GetWMB(uint8 u8Instance,
                        uint8 u8WmbIndex,
                        Flexcan_Ip_MsgBuffType * pWmb)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(u8Instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(pWmb != NULL_PTR);
    DevAssert(0U == u8Instance);
#endif
    const FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
    uint32 * pTmp, pWmbData;

    pTmp = (uint32 *)&pWmb->data[0];
    pWmbData = pBase->WMB[u8WmbIndex].WMBn_D03;
    FLEXCAN_IP_SWAP_BYTES_IN_WORD(pWmbData, *pTmp);

    pTmp = (uint32 *)&pWmb->data[4];
    pWmbData = pBase->WMB[u8WmbIndex].WMBn_D47;
    FLEXCAN_IP_SWAP_BYTES_IN_WORD(pWmbData, *pTmp);

    pWmb->cs = pBase->WMB[u8WmbIndex].WMBn_CS;

    if ((pWmb->cs & FLEXCAN_IP_CS_IDE_MASK) != 0U)
    {
        pWmb->msgId = pBase->WMB[u8WmbIndex].WMBn_ID;
    }
    else
    {
        pWmb->msgId = pBase->WMB[u8WmbIndex].WMBn_ID >> FLEXCAN_IP_ID_STD_SHIFT;
    }

    pWmb->dataLen = (uint8)((pWmb->cs & FLEXCAN_IP_CS_DLC_MASK) >> 16U);
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_WakeUp_IRQHandler
 * Description   : Wake up handler for FLEXCAN.
 * This handler verifies the event which caused the wake up and invokes the
 * user callback, if configured.
 * This is not a public API as it is called whenever an wake up event occurs.
 * If no any events processed, this function will return FALSE
 *
 *END**************************************************************************/
void FlexCAN_WakeUp_IRQHandler(uint8 u8Instance)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(u8Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[u8Instance];
    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[u8Instance];

    if (NULL_PTR != pState)
    {
        if (FlexCAN_GetWTOF(pBase) != 0U)
        {
            FlexCAN_ClearWTOF(pBase);
            /* Invoke callback */
            if ((0U != FlexCAN_GetWTOIE(pBase)) && (pState->callback != NULL_PTR))
            {
                pState->callback(u8Instance, FLEXCAN_EVENT_WAKEUP_TIMEOUT, 0U, pState);
            }
        }

        if (FlexCAN_GetWUMF(pBase) != 0U)
        {
            FlexCAN_ClearWUMF(pBase);
            /* Invoke callback */
            if ((0U != FlexCAN_GetWUMIE(pBase)) && (pState->callback != NULL_PTR))
            {
                pState->callback(u8Instance, FLEXCAN_EVENT_WAKEUP_MATCH, 0U, pState);
            }
        }
    }
    else
    {
        FlexCAN_ClearWTOF(pBase);
        FlexCAN_ClearWUMF(pBase);
    }
}

#endif /* FLEXCAN_IP_FEATURE_HAS_PRETENDED_NETWORKING */

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_ManualBusOffRecovery
 * Description   : Recover manually from bus-off if possible.
 *
 *END**************************************************************************/
/* implements  FlexCAN_Ip_ManualBusOffRecovery_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_ManualBusOffRecovery(uint8 Instance)
{
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    uint32 TimeStart = 0U;
    uint32 TimeElapsed = 0U;
    uint32 Us2Ticks = OsIf_MicrosToTicks(FLEXCAN_IP_TIMEOUT_DURATION, FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
    Flexcan_Ip_StatusType RetVal = FLEXCAN_STATUS_ERROR;

#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif

    /* Recover from bus-off when Automatic recovering from Bus Off state disabled. */
    if ((pBase->CTRL1 & FLEXCAN_CTRL1_BOFFREC_MASK) != 0U)
    {
        RetVal = FLEXCAN_STATUS_SUCCESS;
        /* return success if the controller is not in bus-off */
        if ((pBase->ESR1 & FLEXCAN_IP_ESR1_FLTCONF_BUS_OFF) != 0U)
        {
            SchM_Enter_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_20();
            /* negate to recover from bus-off */
            pBase->CTRL1 &= ~FLEXCAN_CTRL1_BOFFREC_MASK;
            /* re-assert to disable bus-off auto reocvery */
            pBase->CTRL1 |= FLEXCAN_CTRL1_BOFFREC_MASK;
            SchM_Exit_Can_43_FLEXCAN_CAN_EXCLUSIVE_AREA_20();
            /* Wait till exit bus-off */
            TimeStart = OsIf_GetCounter(FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);

            while ((pBase->ESR1 & FLEXCAN_IP_ESR1_FLTCONF_BUS_OFF) != 0U)
            {
                TimeElapsed += OsIf_GetElapsed(&TimeStart, FLEXCAN_IP_SERVICE_TIMEOUT_TYPE);
                if (TimeElapsed >= Us2Ticks)
                {
                    RetVal = FLEXCAN_STATUS_TIMEOUT;
                    break;
                }
            }
        }
    }

    return RetVal;
}

#if (FLEXCAN_IP_FEATURE_HAS_MEM_ERR_DET == STD_ON)
#if (FLEXCAN_IP_FEATURE_MEM_ERR_DET_ENABLED == STD_ON)
/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_MemError_IRQHandler
 * Description   : Memory error detection and correction interrupt handler for FLEXCAN.
 * This handler only provides a error status report and invokes the user callback,
 * and then clears the interrupt flags.
 * This is not a public API as it is called whenever an interrupt occurs.
 *
 *END**************************************************************************/
/* implements  CAN_X_MEMERROR_ISR_Activity */
void FlexCAN_MemError_IRQHandler(uint8 Instance)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];
    uint32 u32ErrStatus;

    /* Check if FlexCAN instance is already initialized */
    if (NULL_PTR != pState)
    {
        /* Get all error status value */
        u32ErrStatus = pBase->ERRSR;
        /* Check spurious interrupt */
        if (((uint32)0U != (u32ErrStatus & ((uint32)FLEXCAN_ERRSR_ERROR_FLAG_MASK))) && (0U != (pBase->MECR & ((uint32)FLEXCAN_MECR_ERROR_INT_MASK))))
        {
            /* store error status */
            pState->u32MemErrorStatus = u32ErrStatus;
            /* clear all error interrupt and error overrun flags */
            pBase->ERRSR = (FLEXCAN_ERRSR_ERROR_FLAG_MASK | FLEXCAN_ERRSR_OVERRUN_FLAG_MASK);
            /* Invoke callback */
            if (pState->error_callback != NULL_PTR)
            {
                pState->error_callback(Instance, FLEXCAN_EVENT_MEM_ERROR_DETECT, u32ErrStatus, pState);
            }
        }
    }
    else
    {
        /* clear all error interrupt and error overrun flags */
        pBase->ERRSR = (FLEXCAN_ERRSR_ERROR_FLAG_MASK | FLEXCAN_ERRSR_OVERRUN_FLAG_MASK);
    }
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetMemErrorDetection
 * Description   : Set Memory Error Detection and Correction in FlexCAN memory.
 * Note: All FlexCAN memory must be initialized first by calling FlexCAN_Ip_Init.
 *
 *END**************************************************************************/
/* implements  FlexCAN_Ip_SetMemErrorDetection_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetMemErrorDetection(uint8 Instance, boolean IsEnable)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    const Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];
    Flexcan_Ip_StatusType RetVal = FLEXCAN_STATUS_ERROR;
    boolean IsFreezeMode;

    /* Check if FlexCAN instance is already initialized.
    This guarantees the FlexCAN memory is ready to update the parity bits in memory properly
    and memory error detection and correction is reliable */
    if (NULL_PTR != pState)
    {

        if (TRUE == IsEnable)
        {
            /* check the response when non-correctable error detected */
            if (FLEXCAN_FREEZE_MODE == pState->flexcanModeErrResponse)
            {
                IsFreezeMode = TRUE;
            }
            else
            {
                IsFreezeMode = FALSE;
            }
            FlexCAN_EnableMemErrorDetection(pBase, IsFreezeMode);
        }
        else
        {
            FlexCAN_DisableMemErrorDetection(pBase);
        }
        RetVal = FLEXCAN_STATUS_SUCCESS;
    }

    return RetVal;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetMemErrorDetectionInt
 * Description   : Set Memory Error Detection and Correction interrupt for corresponding error source.
 *
 *END**************************************************************************/
/* implements  FlexCAN_Ip_SetMemErrorDetectionInt_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetMemErrorDetectionInt_Privileged(uint8 Instance,
                                                                    Flexcan_Ip_ErrorDetectionType ErrorType,
                                                                    boolean IsEnable)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    Flexcan_Ip_StatusType Result = FLEXCAN_STATUS_SUCCESS;
    Flexcan_Ip_StatusType Status = FLEXCAN_STATUS_SUCCESS;
    boolean Disabled = !FlexCAN_IsEnabled(pBase);
    boolean bErrCfgEnabled, bErrDetEnabled;

    if (TRUE == Disabled)
    {
        Result = FlexCAN_Enable(pBase);
    }

    if (FLEXCAN_STATUS_SUCCESS == Result)
    {
        /* check if error correction configuration register write and error configuration register write enable */
        bErrCfgEnabled = FlexCAN_HasMemErrorConfigureEnable(pBase);
        /* check if error detection and correction mechanism is enabled */
        bErrDetEnabled = FlexCAN_IsMemErrorDetectionEnabled(pBase);
        if (bErrCfgEnabled && bErrDetEnabled)
        {
            switch (ErrorType)
            {
                case FLEXCAN_HOST_ACCESS_ERROR:
                    FlexCAN_SetMemErrorDetectionIntCmd(pBase, FLEXCAN_INT_HOST_ACCESS_ERROR, IsEnable);
                    break;
                case FLEXCAN_FLEXCAN_ACCESS_ERROR:
                    FlexCAN_SetMemErrorDetectionIntCmd(pBase, FLEXCAN_INT_FLEXCAN_ACCESS_ERROR, IsEnable);
                    break;
                case FLEXCAN_CORRECTABLE_ERROR:
                    FlexCAN_SetMemErrorDetectionIntCmd(pBase, FLEXCAN_INT_CORRECTABLE_ERROR, IsEnable);
                    break;
                case FLEXCAN_ALL_ECC_ERROR:
                    FlexCAN_SetMemErrorDetectionIntCmd(pBase, FLEXCAN_INT_ALL_ECC_ERROR, IsEnable);
                    break;
                default:
                    /* nothing */
                    #if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
                        DevAssert(FALSE);
                    #endif
                    break;
            }
        }
        else
        {
            Result = FLEXCAN_STATUS_ERROR;
        }
    }

    if (TRUE == Disabled)
    {
        Status = FlexCAN_Disable(pBase);
        if (FLEXCAN_STATUS_SUCCESS != Status)
        {
            Result = Status;
        }
    }

    return Result;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_SetMemErrorInjection
 * Description   : Set Memory Error Injection for corresponding error source.
 *
 *END**************************************************************************/
/* implements  FlexCAN_Ip_SetMemErrorInjection_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_SetMemErrorInjection(uint8 Instance,
                                                      Flexcan_Ip_MemErrorInjectionType * MemErrorInjection,
                                                      boolean IsEnable)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(MemErrorInjection != NULL_PTR);
#endif
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    Flexcan_Ip_StatusType RetVal = FLEXCAN_STATUS_SUCCESS;
    boolean bErrCfgEnabled, bErrDetEnabled;

    /* check if error correction configuration register write and error configuration register write enable */
    bErrCfgEnabled = FlexCAN_HasMemErrorConfigureEnable(pBase);
    /* check if error detection and correction mechanism is enabled */
    bErrDetEnabled = FlexCAN_IsMemErrorDetectionEnabled(pBase);
    if (bErrCfgEnabled && bErrDetEnabled)
    {
        if(TRUE == IsEnable)
        {
            /* disable all error injection is required before setting error information */
            FlexCAN_SetMemErrorInjectionCmd(pBase, FLEXCAN_INJECT_HOST_ACCESS_ERROR, FALSE);
            FlexCAN_SetMemErrorInjectionCmd(pBase, FLEXCAN_INJECT_FLEXCAN_ACCESS_ERROR, FALSE);
            FlexCAN_SetMemErrorInjectionCmd(pBase, FLEXCAN_INJECT_EXTENDED_FLEXCAN_ACCESS_ERROR, FALSE);
            /* set error injection information */
            FlexCAN_SetMemErrorInjectionInfo(pBase,
                                            MemErrorInjection->u32InjectionAddr,
                                            MemErrorInjection->u32InjectionData,
                                            MemErrorInjection->u32InjectionParity);
            /* enable specific error injection */
            switch (MemErrorInjection->eErrorInjection)
            {
                case FLEXCAN_HOST_ACCESS_ERROR_INJECTION:
                    FlexCAN_SetMemErrorInjectionCmd(pBase, FLEXCAN_INJECT_HOST_ACCESS_ERROR, TRUE);
                    break;
                case FLEXCAN_FLEXCAN_ACCESS_ERROR_INJECTION:
                    FlexCAN_SetMemErrorInjectionCmd(pBase, FLEXCAN_INJECT_FLEXCAN_ACCESS_ERROR, TRUE);
                    break;
                case FLEXCAN_EXTENDED_FLEXCAN_ACCESS_ERROR_INJECTION:
                    FlexCAN_SetMemErrorInjectionCmd(pBase, FLEXCAN_INJECT_EXTENDED_FLEXCAN_ACCESS_ERROR, TRUE);
                    break;
                default:
                    /* nothing */
                    #if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
                        DevAssert(FALSE);
                    #endif
                    break;
            }
        }
        else
        {
            (void)MemErrorInjection->u32InjectionAddr;
            (void)MemErrorInjection->u32InjectionData;
            (void)MemErrorInjection->u32InjectionParity;

            switch (MemErrorInjection->eErrorInjection)
            {
                case FLEXCAN_HOST_ACCESS_ERROR_INJECTION:
                    FlexCAN_SetMemErrorInjectionCmd(pBase, FLEXCAN_INJECT_HOST_ACCESS_ERROR, FALSE);
                    break;
                case FLEXCAN_FLEXCAN_ACCESS_ERROR_INJECTION:
                    FlexCAN_SetMemErrorInjectionCmd(pBase, FLEXCAN_INJECT_FLEXCAN_ACCESS_ERROR, FALSE);
                    break;
                case FLEXCAN_EXTENDED_FLEXCAN_ACCESS_ERROR_INJECTION:
                    FlexCAN_SetMemErrorInjectionCmd(pBase, FLEXCAN_INJECT_EXTENDED_FLEXCAN_ACCESS_ERROR, FALSE);
                    break;
                default:
                    /* nothing */
                    #if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
                        DevAssert(FALSE);
                    #endif
                    break;
            }
        }
    }
    else
    {
        RetVal = FLEXCAN_STATUS_ERROR;
    }

    return RetVal;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_MainFunctionMemErrorDetection
 * Description   : Check the memory error occurred and detected by FlexCAN hardware.
 *
 *END**************************************************************************/
/* implements  FlexCAN_Ip_MainFunctionMemErrorDetection_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_MainFunctionMemErrorDetection(uint8 Instance)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
#endif
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];
    Flexcan_Ip_StatusType RetVal = FLEXCAN_STATUS_ERROR;
    uint32 u32ErrStatus;
    boolean bErrDetEnabled;

    /* check if error detection and correction mechanism is enabled */
    bErrDetEnabled = FlexCAN_IsMemErrorDetectionEnabled(pBase);
    if (TRUE == bErrDetEnabled)
    {
        /* Get all error status value */
        u32ErrStatus = pBase->ERRSR;
        /* Check if memory error occurred and detected */
        if ((uint32)0U != u32ErrStatus)
        {
        #if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
            if (NULL_PTR != pState)
        #endif
            {
                /* store error status */
                pState->u32MemErrorStatus = u32ErrStatus;
                /* clear all error interrupt and error overrun flags */
                pBase->ERRSR = (FLEXCAN_ERRSR_ERROR_FLAG_MASK | FLEXCAN_ERRSR_OVERRUN_FLAG_MASK);
                /* Invoke callback */
                (void)FlexCAN_ErrorCallback(Instance, FLEXCAN_EVENT_MEM_ERROR_DETECT, u32ErrStatus, pState);

                RetVal = FLEXCAN_STATUS_SUCCESS;
            }
        }
    }

    return RetVal;
}

/*FUNCTION**********************************************************************
 *
 * Function Name : FlexCAN_Ip_ReadMemErrorReport
 * Description   : Read Memory Error Report.
 *
 *END**************************************************************************/
/* implements  FlexCAN_Ip_ReadMemErrorReport_Activity */
Flexcan_Ip_StatusType FlexCAN_Ip_ReadMemErrorReport(uint8 Instance, Flexcan_Ip_MemErrorReportType * ErrorReport)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(Instance < FLEXCAN_IP_INSTANCE_COUNT);
    DevAssert(ErrorReport != NULL_PTR);
#endif
    FLEXCAN_Type * pBase = Flexcan_Ip_apxBase[Instance];
    Flexcan_Ip_StateType * pState = Flexcan_Ip_apxState[Instance];
    Flexcan_Ip_StatusType RetVal = FLEXCAN_STATUS_ERROR;
    uint32 u32ErrStatus;
    uint32 ReportAddr, ReportData, ReportSyndrome;

    if (NULL_PTR != pState)
    {
        /* check if error was detected */
        u32ErrStatus = pState->u32MemErrorStatus;
        if ((uint32)0U != u32ErrStatus)
        {
            /* Disable error report to assure coherence on the consecutive register reads */
            /* todo: analyze if need to disable ECC instead of only disable error report */
            pBase->MECR |= FLEXCAN_MECR_RERRDIS(1);
            /* read reported error data */
            FlexCAN_ReadReportError(pBase, &ReportAddr, &ReportData, &ReportSyndrome);
            /* Re-enable error report */
            /* todo: in case disable ECC above then enable it again */
            pBase->MECR &= ~FLEXCAN_MECR_RERRDIS_MASK;

            /* starting extract error information. */
            /* the user pointer that point to where the error infor will be stored is passed to driver state infor.
               the application can get error infor from user pointer or via state.pMemErrorReport */
            pState->pMemErrorReport = ErrorReport;

            /* get error overrun flags */
            pState->pMemErrorReport->u8ErrOvrIndication = (uint8)(u32ErrStatus & FLEXCAN_ERRSR_OVERRUN_FLAG_MASK);
            /* extract non-correctable error type */
            if ((uint32)0U != (ReportAddr & FLEXCAN_RERRAR_NCE_MASK))
            {
                if (((uint32)4U == ((ReportAddr & FLEXCAN_RERRAR_SAID_MASK) >> FLEXCAN_RERRAR_SAID_SHIFT)) && ((uint32)0U != (u32ErrStatus & FLEXCAN_ERRSR_HANCEIF_MASK)))
                {
                    pState->pMemErrorReport->errReportType = FLEXCAN_HOST_ACCESS_ERROR;
                    /* extract Source of memory access */
                    /* SAID = 4 Move-out host access */
                    pState->pMemErrorReport->u8ErrSrcIndication = (uint8)4U;
                }
                else if (((uint32)4U > ((ReportAddr & FLEXCAN_RERRAR_SAID_MASK) >> FLEXCAN_RERRAR_SAID_SHIFT)) && ((uint32)0U != (u32ErrStatus & FLEXCAN_ERRSR_FANCEIF_MASK)))
                {
                    pState->pMemErrorReport->errReportType = FLEXCAN_FLEXCAN_ACCESS_ERROR;
                    /* extract Source of memory access */
                    /* SAID = 0 Move-out FlexCAN access
                              1 Move-in
                              2 TX arbitration
                              3 RX matching */
                    pState->pMemErrorReport->u8ErrSrcIndication = (uint8)((ReportAddr & FLEXCAN_RERRAR_SAID_MASK) >> FLEXCAN_RERRAR_SAID_SHIFT);
                }
                else
                {
                    /* nothing */
                }
            }
            else
            {
                /* extract correctable error type */
                if ((uint32)0U != (u32ErrStatus & FLEXCAN_ERRSR_CEIF_MASK))
                {
                    pState->pMemErrorReport->errReportType = FLEXCAN_CORRECTABLE_ERROR;
                    /* extract Source of memory access */
                    pState->pMemErrorReport->u8ErrSrcIndication = (uint8)((ReportAddr & FLEXCAN_RERRAR_SAID_MASK) >> FLEXCAN_RERRAR_SAID_SHIFT);
                }
            }
            /* extract error address */
            pState->pMemErrorReport->u16ReportAddr = (uint16)(ReportAddr & FLEXCAN_RERRAR_ERRADDR_MASK);
            /* get error data */
            pState->pMemErrorReport->u32ReportData = ReportData;
            /* get error syndrome */
            pState->pMemErrorReport->u32ReportSyndrome = ReportSyndrome;

            /* clear error Status storage to prepare for the next handling */
            pState->u32MemErrorStatus = 0U;

            RetVal = FLEXCAN_STATUS_SUCCESS;
        }
    }

    return RetVal;
}

#endif /* (FLEXCAN_IP_FEATURE_MEM_ERR_DET_ENABLED == STD_ON) */
#endif /* (FLEXCAN_IP_FEATURE_HAS_MEM_ERR_DET == STD_ON) */

#define CAN_43_FLEXCAN_STOP_SEC_CODE
#include "Can_43_FLEXCAN_MemMap.h"

#ifdef __cplusplus
}
#endif

/** @} */
