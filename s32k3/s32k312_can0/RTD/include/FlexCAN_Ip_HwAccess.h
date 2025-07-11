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

#ifndef FLEXCAN_FLEXCAN_IP_HWACCESS_H_
#define FLEXCAN_FLEXCAN_IP_HWACCESS_H_

/**
 *  @file FlexCAN_Ip_HwAccess.h
 *
 *  @brief FlexCAN HardWare Access Header File
 *
 *  @addtogroup FlexCAN
 *  @{
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
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
#include "Devassert.h"
#endif

/*==================================================================================================
*                              SOURCE FILE VERSION INFORMATION
==================================================================================================*/
#define FLEXCAN_IP_HWACCESS_VENDOR_ID_H                      43
#define FLEXCAN_IP_HWACCESS_AR_RELEASE_MAJOR_VERSION_H       4
#define FLEXCAN_IP_HWACCESS_AR_RELEASE_MINOR_VERSION_H       7
#define FLEXCAN_IP_HWACCESS_AR_RELEASE_REVISION_VERSION_H    0
#define FLEXCAN_IP_HWACCESS_SW_MAJOR_VERSION_H               6
#define FLEXCAN_IP_HWACCESS_SW_MINOR_VERSION_H               0
#define FLEXCAN_IP_HWACCESS_SW_PATCH_VERSION_H               0
/*==================================================================================================
*                                     FILE VERSION CHECKS
==================================================================================================*/
/* Check if current file and FlexCAN_Ip header file are of the same vendor */
#if (FLEXCAN_IP_HWACCESS_VENDOR_ID_H != FLEXCAN_IP_VENDOR_ID_H)
    #error "FlexCAN_Ip_HwAccess.h and FlexCAN_Ip.h have different vendor ids"
#endif
/* Check if current file and FlexCAN_Ip header file are of the same Autosar version */
#if ((FLEXCAN_IP_HWACCESS_AR_RELEASE_MAJOR_VERSION_H    != FLEXCAN_IP_AR_RELEASE_MAJOR_VERSION_H) || \
     (FLEXCAN_IP_HWACCESS_AR_RELEASE_MINOR_VERSION_H    != FLEXCAN_IP_AR_RELEASE_MINOR_VERSION_H) || \
     (FLEXCAN_IP_HWACCESS_AR_RELEASE_REVISION_VERSION_H != FLEXCAN_IP_AR_RELEASE_REVISION_VERSION_H) \
    )
    #error "AutoSar Version Numbers of FlexCAN_Ip_HwAccess.h and FlexCAN_Ip.h are different"
#endif
/* Check if current file and FlexCAN_Ip header file are of the same Software version */
#if ((FLEXCAN_IP_HWACCESS_SW_MAJOR_VERSION_H != FLEXCAN_IP_SW_MAJOR_VERSION_H) || \
     (FLEXCAN_IP_HWACCESS_SW_MINOR_VERSION_H != FLEXCAN_IP_SW_MINOR_VERSION_H) || \
     (FLEXCAN_IP_HWACCESS_SW_PATCH_VERSION_H != FLEXCAN_IP_SW_PATCH_VERSION_H) \
    )
    #error "Software Version Numbers of FlexCAN_Ip_HwAccess.h and FlexCAN_Ip.h are different"
#endif

#ifndef DISABLE_MCAL_INTERMODULE_ASR_CHECK
    /* Check if current file and Devassert header file are of the same version */
    #if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
        #if ((FLEXCAN_IP_HWACCESS_AR_RELEASE_MAJOR_VERSION_H    !=  DEVASSERT_AR_RELEASE_MAJOR_VERSION) || \
             (FLEXCAN_IP_HWACCESS_AR_RELEASE_MINOR_VERSION_H     !=  DEVASSERT_AR_RELEASE_MINOR_VERSION) \
            )
            #error "AutoSar Version Numbers of FlexCAN_Ip_HwAccess.h and Devassert.h are different"
        #endif
    #endif
#endif
/*==================================================================================================
*                                          CONSTANTS
==================================================================================================*/

/*==================================================================================================
*                                      DEFINES AND MACROS
==================================================================================================*/
/* @brief Frames available in Rx FIFO flag shift */
#define FLEXCAN_IP_LEGACY_RXFIFO_FRAME_AVAILABLE             (5U)
/* @brief Rx FIFO warning flag shift */
#define FLEXCAN_IP_LEGACY_RXFIFO_WARNING                     (6U)
/* @brief Rx FIFO overflow flag shift */
#define FLEXCAN_IP_LEGACY_RXFIFO_OVERFLOW                    (7U)
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
/* @brief Frames available in Enhanced Rx FIFO flag shift */
#define FLEXCAN_IP_ENHANCED_RXFIFO_FRAME_AVAILABLE           (28U)
/* @brief Enhanced Rx FIFO Watermark Indication flag shift */
#define FLEXCAN_IP_ENHANCED_RXFIFO_WATERMARK                 (29U)
/* @brief Enhanced Rx FIFO Overflow  flag shift */
#define FLEXCAN_IP_ENHANCED_RXFIFO_OVERFLOW                  (30U)
/* @brief Enhanced Rx FIFO Underflow flag shift */
#define FLEXCAN_IP_ENHANCED_RXFIFO_UNDERFLOW                 (31U)
/*! @brief FlexCAN Enhanced Fifo Embedded RAM address offset */
#define FLEXCAN_IP_FEATURE_ENHANCED_FIFO_RAM_OFFSET          (0x00002000U)
/*! @brief FlexCAN Enhacend Fifo FilterDepth */
#define FLEXCAN_IP_ENHANCED_RXFIFO_FILTERDEPTH               (128U)
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON) */

/*! @brief FlexCAN Embedded RAM address offset */
#define FLEXCAN_IP_FEATURE_RAM_OFFSET                        (0x00000080U)

#if (STD_ON == FLEXCAN_IP_FEATURE_HAS_EXPANDABLE_MEMORY)
/*! @brief FlexCAN Expandable Embedded RAM address offset */
#define FLEXCAN_IP_FEATURE_EXP_RAM_OFFSET                    (0x00001000U)
#endif /* (STD_ON == FLEXCAN_IP_FEATURE_HAS_EXPANDABLE_MEMORY) */

#ifdef FLEXCAN_IP_FEATURE_HAS_FLTCONF_INT
#define FLEXCAN_IP_ALL_INT                                   (0xFB0006U)                 /*!< Masks for wakeup, error, bus off*/
#else
#define FLEXCAN_IP_ALL_INT                                   (0x3B0006U)                 /*!< Masks for wakeup, error, bus off*/
#endif

#if (FLEXCAN_IP_FEATURE_BUSOFF_ERROR_INTERRUPT_UNIFIED == STD_OFF)
#define FLEXCAN_IP_BUS_OFF_INT                               (0xB0004U)                  /*!< Masks for busOff, Tx/Rx Warning */
#define FLEXCAN_IP_ERROR_INT                                 (0x300002U)                 /*!< Masks for ErrorOvr, ErrorFast, Error */
#endif
#define FLEXCAN_IP_ESR1_FLTCONF_BUS_OFF                      (0x00000020U)

#if (FLEXCAN_IP_FEATURE_HAS_MEM_ERR_DET == STD_ON)
#if (FLEXCAN_IP_FEATURE_MEM_ERR_DET_ENABLED == STD_ON)
#define FLEXCAN_MECR_ERROR_INT_MASK         (FLEXCAN_MECR_HANCEI_MSK_MASK | FLEXCAN_MECR_FANCEI_MSK_MASK | FLEXCAN_MECR_CEI_MSK_MASK)
#define FLEXCAN_ERRSR_ERROR_FLAG_MASK       (FLEXCAN_ERRSR_HANCEIF_MASK | FLEXCAN_ERRSR_FANCEIF_MASK | FLEXCAN_ERRSR_CEIF_MASK)
#define FLEXCAN_ERRSR_OVERRUN_FLAG_MASK     (FLEXCAN_ERRSR_HANCEIOF_MASK | FLEXCAN_ERRSR_FANCEIOF_MASK | FLEXCAN_ERRSR_CEIOF_MASK)
#endif
#endif

/*! @brief FlexCAN Message Buffer field */
#define FLEXCAN_IP_ID_EXT_MASK                               (0x3FFFFU)
#define FLEXCAN_IP_ID_EXT_SHIFT                              (0U)
#define FLEXCAN_IP_ID_EXT_WIDTH                              (18U)

#define FLEXCAN_IP_ID_STD_MASK                               (0x1FFC0000U)
#define FLEXCAN_IP_ID_STD_SHIFT                              (18U)
#define FLEXCAN_IP_ID_STD_WIDTH                              (11U)

#define FLEXCAN_IP_ID_PRIO_MASK                              (0xE0000000U)
#define FLEXCAN_IP_ID_PRIO_SHIFT                             (29U)
#define FLEXCAN_IP_ID_PRIO_WIDTH                             (3U)

#define FLEXCAN_IP_CS_TIME_STAMP_MASK                        (0xFFFFU)
#define FLEXCAN_IP_CS_TIME_STAMP_SHIFT                       (0U)
#define FLEXCAN_IP_CS_TIME_STAMP_WIDTH                       (16U)

#define FLEXCAN_IP_CS_DLC_MASK                               (0xF0000U)
#define FLEXCAN_IP_CS_DLC_SHIFT                              (16U)
#define FLEXCAN_IP_CS_DLC_WIDTH                              (4U)

#define FLEXCAN_IP_CS_RTR_MASK                               (0x100000U)
#define FLEXCAN_IP_CS_RTR_SHIFT                              (20U)
#define FLEXCAN_IP_CS_RTR_WIDTH                              (1U)

#define FLEXCAN_IP_CS_IDE_MASK                               (0x200000U)
#define FLEXCAN_IP_CS_IDE_SHIFT                              (21U)
#define FLEXCAN_IP_CS_IDE_WIDTH                              (1U)

#define FLEXCAN_IP_CS_SRR_MASK                               (0x400000U)
#define FLEXCAN_IP_CS_SRR_SHIFT                              (22U)
#define FLEXCAN_IP_CS_SRR_WIDTH                              (1U)

#define FLEXCAN_IP_CS_CODE_MASK                              (0xF000000U)
#define FLEXCAN_IP_CS_CODE_SHIFT                             (24U)
#define FLEXCAN_IP_CS_CODE_WIDTH                             (4U)

#define FLEXCAN_IP_CS_IDHIT_MASK                             (0xFF800000U)
#define FLEXCAN_IP_CS_IDHIT_SHIFT                            (23U)
#define FLEXCAN_IP_CS_IDHIT_WIDTH                            (9U)

#define FLEXCAN_IP_MB_EDL_MASK                               (0x80000000U)
#define FLEXCAN_IP_MB_BRS_MASK                               (0x40000000U)

#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATAB_RTR_SHIFT      (31U)         /*!< FlexCAN RX FIFO ID filter*/
/*! format A&B RTR mask.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATAB_IDE_SHIFT      (30U)         /*!< FlexCAN RX FIFO ID filter*/
/*! format A&B IDE mask.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATB_RTR_SHIFT       (15U)         /*!< FlexCAN RX FIFO ID filter*/
/*! format B RTR-2 mask.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATB_IDE_SHIFT       (14U)         /*!< FlexCAN RX FIFO ID filter*/
/*! format B IDE-2 mask.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATA_EXT_MASK        (0x3FFFFFFFU) /*!< FlexCAN RX FIFO ID filter*/
/*! format A extended mask.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATA_EXT_SHIFT       (1U)          /*!< FlexCAN RX FIFO ID filter*/
/*! format A extended shift.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATA_STD_MASK        (0x3FF80000U) /*!< FlexCAN RX FIFO ID filter*/
/*! format A standard mask.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATA_STD_SHIFT       (19U)         /*!< FlexCAN RX FIFO ID filter*/
/*! format A standard shift.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATB_EXT_MASK        (0x1FFF8000U) /*!< FlexCAN RX FIFO ID filter*/
/*! format B extended mask1.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATB_EXT_SHIFT1      (16U)         /*!< FlexCAN RX FIFO ID filter*/
/*! format B extended shift 1.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATB_EXT_SHIFT2      (0U)          /*!< FlexCAN RX FIFO ID filter*/
/*! format B extended shift 2.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATB_STD_MASK        (0x7FFU)      /*!< FlexCAN RX FIFO ID filter*/
/*! format B standard mask.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATB_STD_SHIFT1      (19U)         /*!< FlexCAN RX FIFO ID filter*/
/*! format B standard shift1.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATB_STD_SHIFT2      (3U)          /*!< FlexCAN RX FIFO ID filter*/
/*! format B standard shift2.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATB_EXT_CMP_SHIFT   (15U)         /*!< FlexCAN RX FIFO ID filter*/
/*! format B extended compare shift.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATC_EXT_MASK        (0x1FE00000U) /*!< FlexCAN RX FIFO ID filter*/
/*! format C mask.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATC_STD_MASK        (0x7F8U)      /*!< FlexCAN RX FIFO ID filter*/
/*! format C mask.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATC_SHIFT1          (24U)         /*!< FlexCAN RX FIFO ID filter*/
/*! format C shift1.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATC_SHIFT2          (16U)         /*!< FlexCAN RX FIFO ID filter*/
/*! format C shift2.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATC_SHIFT3          (8U)          /*!< FlexCAN RX FIFO ID filter*/
/*! format C shift3.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATC_SHIFT4          (0U)          /*!< FlexCAN RX FIFO ID filter*/
/*! format C shift4.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATC_EXT_CMP_SHIFT   (21U)         /*!< FlexCAN RX FIFO ID filter*/
/*! format C extended compare shift.*/
#define FLEXCAN_IP_RX_FIFO_ID_FILTER_FORMATC_STD_CMP_SHIFT   (3U)          /*!< FlexCAN RX FIFO ID filter*/

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)

#define FLEXCAN_IP_ENHANCED_IDHIT_MASK                       (0x7FU)
#define FLEXCAN_IP_ENHANCED_IDHIT_SHIFT                      (0U)
#define FLEXCAN_IP_ENHANCED_IDHIT_WIDTH                      (7U)

#define FLEXCAN_IP_ENHANCED_RX_FIFO_ID_FILTER_FSCH_SHIFT     (30U)             /*!< FlexCAN Enhanced RX FIFO ID filter*/
/*! Standard & Extended FSCH shift.*/
#define FLEXCAN_IP_ENHANCED_RX_FIFO_ID_FILTER_STD_RTR2_SHIFT (27U)             /*!< FlexCAN Enhanced RX FIFO ID filter*/
/*! Standard RTR-2 shift.*/
#define FLEXCAN_IP_ENHANCED_RX_FIFO_ID_FILTER_STD_RTR1_SHIFT (11U)             /*!< FlexCAN Enhanced RX FIFO ID filter*/
/*! Standard RTR-1 shift.*/
#define FLEXCAN_IP_ENHANCED_RX_FIFO_ID_FILTER_EXT_RTR_SHIFT  (29U)             /*!< FlexCAN Enhanced RX FIFO ID filter*/
/*! Extended RTR shift.*/
#define FLEXCAN_IP_ENHANCED_RX_FIFO_ID_FILTER_STD_SHIFT2     (16U)             /*!< FlexCAN Enhanced RX FIFO ID filter*/
/*! Standard ID-2 shift.*/
#define FLEXCAN_IP_ENHANCED_RX_FIFO_ID_FILTER_STD_SHIFT1     (0U)              /*!< FlexCAN Enhanced RX FIFO ID filter*/
/*! Standard ID-1 shift.*/
#define FLEXCAN_IP_ENHANCED_RX_FIFO_ID_FILTER_STD_MASK       (0x7FFU)          /*!< FlexCAN Enhanced RX FIFO ID filter*/
/*! Standard ID mask.*/
#define FLEXCAN_IP_ENHANCED_RX_FIFO_ID_FILTER_EXT_SHIFT      (0U)              /*!< FlexCAN Enhanced RX FIFO ID filter*/
/*! Extended ID shift.*/
#define FLEXCAN_IP_ENHANCED_RX_FIFO_ID_FILTER_EXT_MASK       (0x1FFFFFFFU)     /*!< FlexCAN Enhanced RX FIFO ID filter*/
/*! Mask for enable all enhanced interrupts */
#define FLEXCAN_IP_ENHACED_RX_FIFO_ALL_INTERRUPT_MASK      (FLEXCAN_ERFIER_ERFUFWIE_MASK | FLEXCAN_ERFIER_ERFOVFIE_MASK | \
                                                            FLEXCAN_ERFIER_ERFWMIIE_MASK | FLEXCAN_ERFIER_ERFDAIE_MASK \
                                                           )

/*! Mask for disable all enhanced interrupts */
#define FLEXCAN_IP_ENHACED_RX_FIFO_NO_INTERRUPT_MASK         (0U)
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_On) */

/* This are for little endians cores and supporting rev32 asm instuction */
#define FLEXCAN_IP_SWAP_BYTES_IN_WORD_INDEX(index) (((index) & ~3U) + (3U - ((index) & 3U)))
#define FLEXCAN_IP_SWAP_BYTES_IN_WORD(a, b) FLEXCAN_IP_REV_BYTES_32(a, b)
#define FLEXCAN_IP_REV_BYTES_32(a, b) ((b) = (((a) & 0xFF000000U) >> 24U) | (((a) & 0xFF0000U) >> 8U) \
                                | (((a) & 0xFF00U) << 8U) | (((a) & 0xFFU) << 24U))

/*==================================================================================================
*                                             ENUMS
==================================================================================================*/
/*! @brief FlexCAN message buffer CODE for Rx buffers*/
enum
{
    FLEXCAN_RX_INACTIVE  = 0x0, /*!< MB is not active.*/
    FLEXCAN_RX_FULL      = 0x2, /*!< MB is full.*/
    FLEXCAN_RX_EMPTY     = 0x4, /*!< MB is active and empty.*/
    FLEXCAN_RX_OVERRUN   = 0x6, /*!< MB is overwritten into a full buffer.*/
    FLEXCAN_RX_BUSY      = 0x8, /*!< FlexCAN is updating the contents of the MB.*/
                                /*!  The CPU must not access the MB.*/
    FLEXCAN_RX_RANSWER   = 0xA, /*!< A frame was configured to recognize a Remote Request Frame*/
                                /*!  and transmit a Response Frame in return.*/
    FLEXCAN_RX_NOT_USED   = 0xF /*!< Not used*/
};

/*! @brief FlexCAN message buffer CODE FOR Tx buffers*/
enum
{
    FLEXCAN_TX_INACTIVE  = 0x08, /*!< MB is not active.*/
    FLEXCAN_TX_ABORT     = 0x09, /*!< MB is aborted.*/
    FLEXCAN_TX_DATA      = 0x0C, /*!< MB is a TX Data Frame(MB RTR must be 0).*/
    FLEXCAN_TX_REMOTE    = 0x1C, /*!< MB is a TX Remote Request Frame (MB RTR must be 1).*/
    FLEXCAN_TX_TANSWER   = 0x0E, /*!< MB is a TX Response Request Frame from.*/
                                 /*!  an incoming Remote Request Frame.*/
    FLEXCAN_TX_NOT_USED   = 0xF  /*!< Not used*/
};

/*! @brief FlexCAN error interrupt types
 */
typedef enum
{
    FLEXCAN_INT_RX_WARNING = FLEXCAN_CTRL1_RWRNMSK_MASK,     /*!< RX warning interrupt*/
    FLEXCAN_INT_TX_WARNING = FLEXCAN_CTRL1_TWRNMSK_MASK,     /*!< TX warning interrupt*/
    FLEXCAN_INT_ERR        = FLEXCAN_CTRL1_ERRMSK_MASK,      /*!< Error interrupt*/
    FLEXCAN_INT_ERR_FAST,                                    /*!< Error Fast interrupt*/
    FLEXCAN_INT_BUSOFF     = FLEXCAN_CTRL1_BOFFMSK_MASK,     /*!< Bus off interrupt*/
} Flexcan_Ip_ErrorIntMaskType;

#if (FLEXCAN_IP_FEATURE_HAS_MEM_ERR_DET == STD_ON)
#if (FLEXCAN_IP_FEATURE_MEM_ERR_DET_ENABLED == STD_ON)
/*! @brief FlexCAN memory error detection and correction interrupt types
 */
typedef enum
{
    FLEXCAN_INT_HOST_ACCESS_ERROR    = FLEXCAN_MECR_HANCEI_MSK_MASK,       /*!< Host Access with Non-Correctable Errors Interrupt */
    FLEXCAN_INT_FLEXCAN_ACCESS_ERROR = FLEXCAN_MECR_FANCEI_MSK_MASK,       /*!< FlexCAN Access with Non-Correctable Errors Interrupt */
    FLEXCAN_INT_CORRECTABLE_ERROR    = FLEXCAN_MECR_CEI_MSK_MASK,          /*!< Correctable Errors Interrupt */
    FLEXCAN_INT_ALL_ECC_ERROR        = FLEXCAN_MECR_ERROR_INT_MASK
} Flexcan_Ip_MemErrDetectionIntMaskType;

/*! @brief FlexCAN memory error injection types
 */
typedef enum
{
    FLEXCAN_INJECT_HOST_ACCESS_ERROR    = FLEXCAN_MECR_HAERRIE_MASK,       /*!< Host Access Error Injection */
    FLEXCAN_INJECT_FLEXCAN_ACCESS_ERROR = FLEXCAN_MECR_FAERRIE_MASK,       /*!< FlexCAN Access Error Injection. Apply error injection only to the 32-bit word */
    FLEXCAN_INJECT_EXTENDED_FLEXCAN_ACCESS_ERROR = (FLEXCAN_MECR_FAERRIE_MASK | FLEXCAN_MECR_EXTERRIE_MASK)       /*!< Extended Error Injection for FlexCAN Access. Apply error injection to the 64-bit word */
} Flexcan_Ip_MemErrInjectionMaskType;

#endif /* (FLEXCAN_IP_FEATURE_MEM_ERR_DET_ENABLED == STD_ON) */
#endif /* (FLEXCAN_IP_FEATURE_HAS_MEM_ERR_DET == STD_ON) */

/*==================================================================================================
*                                STRUCTURES AND OTHER TYPEDEFS
==================================================================================================*/
/*! @brief FlexCAN Message Buffer code and status for transmit and receive
 */
typedef struct
{
    uint32 code;                        /*!< MB code for TX or RX buffers.*/
                                        /*! Defined by flexcan_mb_code_rx_t and flexcan_mb_code_tx_t */
    Flexcan_Ip_MsgBuffIdType msgIdType; /*!< Type of message ID (standard or extended)*/
    uint32 dataLen;                     /*!< Length of Data in Bytes*/
    boolean fd_enable;
    uint8 fd_padding;
    boolean enable_brs;                   /* Enable bit rate switch*/
} Flexcan_Ip_MsbuffCodeStatusType;

/*==================================================================================================
*                                GLOBAL VARIABLE DECLARATIONS
==================================================================================================*/

/*==================================================================================================
*                                    FUNCTION PROTOTYPES
==================================================================================================*/
#define CAN_43_FLEXCAN_START_SEC_CODE
#include "Can_43_FLEXCAN_MemMap.h"


void FLEXCAN_ClearMsgBuffIntCmd(FLEXCAN_Type * pBase,
                                uint8 u8Instance,
                                uint32 MbIdx,
                                boolean bIsIntActive
                               );

void FlexCAN_SetErrIntCmd(FLEXCAN_Type * pBase,
                          Flexcan_Ip_ErrorIntMaskType ErrType,
                          boolean Enable
                         );

Flexcan_Ip_StatusType FlexCAN_EnterFreezeMode(FLEXCAN_Type * pBase);

/*!
 * @brief Sets the FlexCAN Rx FIFO fields.
 *
 * @param   pBase            The FlexCAN base address
 * @param   IdFormat         The format of the Rx FIFO ID Filter Table Elements
 * @param   IdFilterTable    The ID filter table elements which contain RTR bit,
 *                           IDE bit, and RX message ID.
 */
void FlexCAN_SetRxFifoFilter(FLEXCAN_Type * pBase,
                             Flexcan_Ip_RxFifoIdElementFormatType IdFormat,
                             const Flexcan_Ip_IdTableType * IdFilterTable
                            );

/*!
 * @brief Gets the FlexCAN Rx FIFO data.
 *
 * @param   pBase    The FlexCAN base address
 * @param   RxFifo   The FlexCAN receive FIFO data
 */
void FlexCAN_ReadRxFifo(const FLEXCAN_Type * pBase,
                        Flexcan_Ip_MsgBuffType * RxFifo
                       );

/*!
 * @brief Un freezes the FlexCAN module.
 *
 * @param   pBase     The FlexCAN base address
 * @return FLEXCAN_STATUS_SUCCESS successfully exit from freeze
 *         FLEXCAN_STATUS_TIMEOUT fail to exit from freeze
 */
Flexcan_Ip_StatusType FlexCAN_ExitFreezeMode(FLEXCAN_Type * pBase);

Flexcan_Ip_StatusType FlexCAN_Disable(FLEXCAN_Type * pBase);

Flexcan_Ip_StatusType FlexCAN_Enable(FLEXCAN_Type * pBase);

/*!
 * @brief Locks the FlexCAN Rx message buffer.
 *
 * @param   pBase        The FlexCAN base address
 * @param   MsgBuffIdx   Index of the message buffer
 *
 */
void FlexCAN_LockRxMsgBuff(const FLEXCAN_Type * pBase,
                           uint32 MsgBuffIdx
                          );

/*!
 * @brief Enables/Disables the FlexCAN Message Buffer interrupt.
 *
 * @param   pBase        The FlexCAN base address
 * @param   MsgBuffIdx   Index of the message buffer
 * @param   Enable       choose enable or disable
 * @return  FLEXCAN_STATUS_SUCCESS if successful;
 *          FLEXCAN_STATUS_CAN_BUFF_OUT_OF_RANGE if the index of the
 *          message buffer is invalid
 */
Flexcan_Ip_StatusType FlexCAN_SetMsgBuffIntCmd(FLEXCAN_Type * pBase,
                                               uint8 u8Instance,
                                               uint32 MsgBuffIdx,
                                               boolean Enable,
                                               boolean bIsIntActive
                                              );

/*!
 * @brief Disable all interrupts.
 *
 * @param   pBase       The FlexCAN base address
 */
void FlexCAN_DisableInterrupts(FLEXCAN_Type * pBase);

/*!
 * @brief Enable all interrupts configured.
 *
 * @param   pBase       The FlexCAN base address
 * @param   u8Instance  A FlexCAN instance number
 */
void FlexCAN_EnableInterrupts(FLEXCAN_Type * pBase, uint8 u8Instance);
/*!
 * @brief Sets the FlexCAN message buffer fields for transmitting.
 *
 * @param   pMbAddr      The Message buffer address
 * @param   Cs           CODE/status values (TX)
 * @param   MsgId        ID of the message to transmit
 * @param   MsgData      Bytes of the FlexCAN message
 * @param   IsRemote     Will set RTR remote Flag
 * @return  FLEXCAN_STATUS_SUCCESS if successful;
 *          FLEXCAN_STATUS_CAN_BUFF_OUT_OF_RANGE if the index of the
 *          message buffer is invalid
 */
void FlexCAN_SetTxMsgBuff(volatile uint32 * const pMbAddr,
                          const Flexcan_Ip_MsbuffCodeStatusType * Cs,
                          uint32 MsgId,
                          const uint8 * MsgData,
                          const boolean IsRemote
                         );

/*!
 * @brief Enables the Rx FIFO.
 *
 * @param   pBase           The FlexCAN base address
 * @param   NumOfFilters    The number of Rx FIFO filters
 * @return  The status of the operation
 * @retval  FLEXCAN_STATUS_SUCCESS RxFIFO was successfully enabled
 * @retval  FLEXCAN_STATUS_ERROR RxFIFO could not be enabled (e.g. the FD feature
 *          was enabled, and these two features are not compatible)
 */
Flexcan_Ip_StatusType FlexCAN_EnableRxFifo(FLEXCAN_Type * pBase, uint32 NumOfFilters);


/*!
 * @brief Sets  the maximum number of Message Buffers.
 *
 * @param   pBase             The FlexCAN base address
 * @param   MaxMsgBuffNum     Maximum number of message buffers
 * @return  FLEXCAN_STATUS_SUCCESS if successful;
 *          FLEXCAN_STATUS_BUFF_OUT_OF_RANGE if the index of the
 *          message buffer is invalid
 */
Flexcan_Ip_StatusType FlexCAN_SetMaxMsgBuffNum(FLEXCAN_Type * pBase, uint32 MaxMsgBuffNum);

/*!
 * @brief Sets the FlexCAN message buffer fields for receiving.
 *
 * @param   pBase       The FlexCAN base address
 * @param   MsgBuffIdx  Index of the message buffer
 * @param   Cs          CODE/status values (RX)
 * @param   MsgId       ID of the message to receive
 * @return  FLEXCAN_STATUS_SUCCESS if successful;
 *          FLEXCAN_STATUS_BUFF_OUT_OF_RANGE if the index of the
 *          message buffer is invalid
 */
void FlexCAN_SetRxMsgBuff(const FLEXCAN_Type * pBase,
                          uint32 MsgBuffIdx,
                          const Flexcan_Ip_MsbuffCodeStatusType * Cs,
                          uint32 MsgId
                         );

/*!
 * @brief Gets the message buffer timestamp value.
 *
 * @param   pBase         The FlexCAN base address
 * @param   MsgBuffIdx    Index of the message buffer
 * @return  value of timestamp for selected message buffer.
 */
uint32 FlexCAN_GetMsgBuffTimestamp(const FLEXCAN_Type * pBase, uint32 MsgBuffIdx);

/*!
 * @brief Gets the FlexCAN message buffer fields.
 *
 * @param   pBase         The FlexCAN base address
 * @param   MsgBuffIdx    Index of the message buffer
 * @param   MsgBuff       The fields of the message buffer
 */
void FlexCAN_GetMsgBuff(const FLEXCAN_Type * pBase,
                        uint32 MsgBuffIdx,
                        Flexcan_Ip_MsgBuffType * MsgBuff
                       );

#if (FLEXCAN_IP_FEATURE_HAS_SUPV == STD_ON)
#if defined(FLEXCAN_IP_FEATURE_SUPV_NOT_ALL_INSTANCES)
/*!
 * @brief Check if instance support Supervisor Mode MCR[SUPV].
 *
 * @param   pBase  The FlexCAN base address
 * @return  TRUE\FALSE if support Supervisor Mode MCR[SUPV].
 */
boolean FlexCAN_IsSupvModeAvailable(const FLEXCAN_Type * pBase);
#endif
#endif

#if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
#if defined(FLEXCAN_IP_FEATURE_FD_NOT_ALL_INSTANCES)
/*!
 * @brief Check if instance support FD.
 *
 * @param[in]   pBase  The FlexCAN base address
 * @return TRUE\FALSE if support FD.
 */
boolean FlexCAN_IsFDAvailable(const FLEXCAN_Type * pBase);
#endif
/*!
 * @brief Sets the payload size of the MBs.
 *
 * @param   pBase        The FlexCAN base address
 * @param   PayloadSize  The payload size
 */
void FlexCAN_SetPayloadSize(FLEXCAN_Type * pBase, const Flexcan_Ip_PayloadSizeType * PayloadSize);

/*!
 * @brief Check If mb index is out of range or not.
 *
 * @param   pBase                The FlexCAN base address
 * @param   u8MbIndex            MB index
 * @param   bIsLegacyFifoEn      Legacy fifo enabled or not
 * @param   u32MaxMbNum          Max mb number
 */
boolean FlexCAN_IsMbOutOfRange
(
    const FLEXCAN_Type * pBase,
    uint8 u8MbIndex,
    boolean bIsLegacyFifoEn,
    uint32 u32MaxMbNum
);

/*!
 * @brief Sets the FlexCAN RX FIFO global mask.
 *
 * @param[in]   pBase  The FlexCAN base address
 * @param[in]   Mask   Sets mask
 */
static inline void FlexCAN_SetRxFifoGlobalMask(FLEXCAN_Type * pBase, uint32 Mask)
{
    (pBase->RXFGMASK) = Mask;
}

/*!
 * @brief Enables/Disables the Transceiver Delay Compensation feature and sets
 * the Transceiver Delay Compensation Offset (offset value to be added to the
 * measured transceiver's loop delay in order to define the position of the
 * delayed comparison point when bit rate switching is active).
 *
 * @param   pBase  The FlexCAN base address
 * @param   Enable Enable/Disable Transceiver Delay Compensation
 * @param   Offset Transceiver Delay Compensation Offset
 */
static inline void FlexCAN_SetTDCOffset(FLEXCAN_Type * pBase,
                                        boolean Enable,
                                        uint8 Offset
                                       )
{
    uint32 Tmp;

    Tmp = pBase->FDCTRL;
    Tmp &= ~(FLEXCAN_FDCTRL_TDCEN_MASK | FLEXCAN_FDCTRL_TDCOFF_MASK);

    if (Enable)
    {
        Tmp = Tmp | FLEXCAN_FDCTRL_TDCEN_MASK;
        Tmp = Tmp | FLEXCAN_FDCTRL_TDCOFF(Offset);
    }

    pBase->FDCTRL = Tmp;
}

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
/*!
 * @brief Enables/Disables the Transceiver Delay Compensation feature and sets
 * the Transceiver Delay Compensation Offset (offset value to be added to the
 * measured transceiver's loop delay in order to define the position of the
 * delayed comparison point when bit rate switching is active).
 *
 * @param   pBase  The FlexCAN base address
 * @param   Enable Enable/Disable Transceiver Delay Compensation
 * @param   Offset Transceiver Delay Compensation Offset
 */
static inline void FlexCAN_SetEnhancedTDCOffset(FLEXCAN_Type * pBase,
                                                boolean Enable,
                                                uint8 Offset
                                               )
{
    uint32 Tmp;

    Tmp = pBase->ETDC;
    Tmp &= ~(FLEXCAN_ETDC_ETDCEN_MASK | FLEXCAN_ETDC_ETDCOFF_MASK);

    if (Enable)
    {
        Tmp = Tmp | FLEXCAN_ETDC_ETDCEN_MASK;
        Tmp = Tmp | FLEXCAN_ETDC_ETDCOFF(Offset);
    }

    pBase->ETDC = Tmp;
}
#endif
#endif /* FLEXCAN_IP_FEATURE_HAS_FD */

/*!
 * @brief Gets the payload size of the MBs.
 *
 * @param   pBase  The FlexCAN base address
 * @return  The payload size in bytes
 */
uint8 FlexCAN_GetMbPayloadSize(const FLEXCAN_Type * pBase, uint32 MaxMsgBuffNum);

/*!
 * @brief Initializes the FlexCAN controller.
 *
 * @param   pBase  The FlexCAN base address
 */
Flexcan_Ip_StatusType FlexCAN_Init(FLEXCAN_Type * pBase);

/*!
 * @brief Checks if the FlexCAN is enabled.
 *
 * @param   pBase   The FlexCAN base address
 * @return  TRUE if enabled; FALSE if disabled
 */
static inline boolean FlexCAN_IsEnabled(const FLEXCAN_Type * pBase)
{
    return (((pBase->MCR & FLEXCAN_MCR_MDIS_MASK) >> FLEXCAN_MCR_MDIS_SHIFT) != 0U) ? FALSE : TRUE;
}

/*!
 * @brief Enables/Disables Flexible Data rate (if supported).
 *
 * @param   pBase      The FlexCAN base address
 * @param   EnableFD   TRUE to enable; FALSE to disable FD mode
 * @param   EnableBRS  TRUE to enable; FALSE to disable bit rate switch
 */
static inline void FlexCAN_SetFDEnabled(FLEXCAN_Type * pBase,
                                        boolean EnableFD,
                                        boolean EnableBRS
                                       )
{
    pBase->MCR = (pBase->MCR & ~FLEXCAN_MCR_FDEN_MASK) | FLEXCAN_MCR_FDEN(EnableFD ? 1UL : 0UL);

    /* Enable BitRate Switch support from BRS_TX_MB field or ignore it */
    pBase->FDCTRL = (pBase->FDCTRL & ~FLEXCAN_FDCTRL_FDRATE_MASK) | FLEXCAN_FDCTRL_FDRATE(EnableBRS ? 1UL : 0UL);

    /* Disable Transmission Delay Compensation by default */
    pBase->FDCTRL &= ~(FLEXCAN_FDCTRL_TDCEN_MASK | FLEXCAN_FDCTRL_TDCOFF_MASK);
}

/*!
 * @brief Enables/Disables Listen Only Mode.
 *
 * @param   pBase             The FlexCAN base address
 * @param   EnableListenOnly  TRUE to enable; FALSE to disable
 */
static inline void FlexCAN_SetListenOnlyMode(FLEXCAN_Type * pBase, boolean EnableListenOnly)
{
    pBase->CTRL1 = (pBase->CTRL1 & ~FLEXCAN_CTRL1_LOM_MASK) | FLEXCAN_CTRL1_LOM(EnableListenOnly ? 1UL : 0UL);
}


#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)

/*!
 * @brief Clears the FIFO
 *
 * @param   pBase  The FlexCAN base address
 */
static inline void FlexCAN_ClearFIFO(FLEXCAN_Type * pBase)
{
    pBase->IFLAG1 = FLEXCAN_IFLAG1_BUF0I_MASK;
}


/*!
 * @brief Enables/Disables the DMA support for RxFIFO.
 *
 * @param   pBase  The FlexCAN base address
 * @param   Enable Enable/Disable DMA support
 */
static inline void FlexCAN_SetRxFifoDMA(FLEXCAN_Type * pBase, boolean Enable)
{
    pBase->MCR = (pBase->MCR & ~FLEXCAN_MCR_DMA_MASK) | FLEXCAN_MCR_DMA(Enable ? 1UL : 0UL);
}

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
/*!
 * @brief Resets Enhanced Rx FIFO engine and state.
 *
 * @param   pBase  The FlexCAN base address
 */
static inline void FlexCAN_ClearEnhancedRxFifoEngine(FLEXCAN_Type * pBase)
{
    pBase->ERFSR = pBase->ERFSR | FLEXCAN_ERFSR_ERFCLR_MASK;
}

/*!
 * @brief Clears the Enhanced Rx FIFO
 *
 * @param   pBase  The FlexCAN base address
 */
static inline void FlexCAN_ClearEnhancedFIFO(FLEXCAN_Type * pBase)
{
    pBase->ERFSR = FLEXCAN_ERFSR_ERFCLR_MASK;
}

/*!
 * @brief Configure the number of words to transfer for each Enhanced Rx FIFO data element in DMA mode.
 *
 * @param   pBase        The FlexCAN base address
 * @param   NumOfWords   The number of words to transfer
 */
static inline void FlexCAN_ConfigEnhancedRxFifoDMA(FLEXCAN_Type * pBase, uint32 NumOfWords)
{
    pBase->ERFCR = (pBase->ERFCR & (~FLEXCAN_ERFCR_DMALW_MASK)) | (((NumOfWords - 1u) << FLEXCAN_ERFCR_DMALW_SHIFT) & FLEXCAN_ERFCR_DMALW_MASK);
}

#endif /* FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO */
#endif /* if FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */

/*!
 * @brief Get The Max no of MBs allowed on CAN instance.
 *
 * @param   pBase    The FlexCAN base address
 * @return  The Max No of MBs on the CAN instance;
 */
uint32 FlexCAN_GetMaxMbNum(const FLEXCAN_Type * pBase);

/*!
 * @brief Unlocks the FlexCAN Rx message buffer.
 *
 * @param   pBase     The FlexCAN base address
 */
static inline void FlexCAN_UnlockRxMsgBuff(const FLEXCAN_Type * pBase)
{
    /* Unlock the mailbox by reading the free running timer */
    (void)pBase->TIMER;
}

/*!
 * @brief Clears the interrupt flag of the message buffers.
 *
 * @param   pBase        The FlexCAN base address
 * @param   MsgBuffIdx   Index of the message buffer
 */
static inline void FlexCAN_ClearMsgBuffIntStatusFlag(FLEXCAN_Type * pBase, uint32 MsgBuffIdx)
{
    uint32 Flag = ((uint32)1U << (MsgBuffIdx % 32U));

    /* Clear the corresponding message buffer interrupt flag*/
    if (MsgBuffIdx < 32U)
    {
        (pBase->IFLAG1) = (Flag);
    }

#if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 32U
    else if (MsgBuffIdx < 64U)
    {
        (pBase->IFLAG2) = (Flag);
    }
#if FLEXCAN_IP_FEATURE_MAX_MB_NUM <= 64U
    else
    {
        /* Required Rule 15.7, no 'else' at end of 'if ... else if' chain */
    }
#endif
#endif /* if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 32U */
#if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 64U
    else if (MsgBuffIdx < 96U)
    {
        (pBase->IFLAG3) = (Flag);
    }
#if FLEXCAN_IP_FEATURE_MAX_MB_NUM <= 96U
    else
    {
        /* Required Rule 15.7, no 'else' at end of 'if ... else if' chain */
    }
#endif
#endif /* if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 64U */
#if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 96U
    else
    {
        (pBase->IFLAG4) = (Flag);
    }
#endif
}

/*!
 * @brief Get the interrupt flag of the message buffers.
 *
 * @param   pBase       The FlexCAN base address
 * @param   MsgBuffIdx  Index of the message buffer
 * @return  The value of interrupt flag of the message buffer.
 */
static inline uint8 FlexCAN_GetMsgBuffIntStatusFlag(const FLEXCAN_Type * pBase, uint32 MsgBuffIdx)
{
    uint32 Flag = 0U;

    if (MsgBuffIdx < 32U)
    {
        Flag = ((pBase->IFLAG1 & ((uint32)1U << (MsgBuffIdx % 32U))) >> (MsgBuffIdx % 32U));
    }

#if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 32U
    else if (MsgBuffIdx < 64U)
    {
        Flag = ((pBase->IFLAG2 & ((uint32)1U << (MsgBuffIdx % 32U))) >> (MsgBuffIdx % 32U));
    }
#if FLEXCAN_IP_FEATURE_MAX_MB_NUM <= 64U
    else
    {
        /* Required Rule 15.7, no 'else' at end of 'if ... else if' chain */
    }
#endif /* FLEXCAN_IP_FEATURE_MAX_MB_NUM <= 64U */
#endif /* FLEXCAN_IP_FEATURE_MAX_MB_NUM > 32U */

#if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 64U
    else if (MsgBuffIdx < 96U)
    {
        Flag = ((pBase->IFLAG3 & ((uint32)1U << (MsgBuffIdx % 32U))) >> (MsgBuffIdx % 32U));
    }
#if FLEXCAN_IP_FEATURE_MAX_MB_NUM <= 96U
    else
    {
        /* Required Rule 15.7, no 'else' at end of 'if ... else if' chain */
    }
#endif /* FLEXCAN_IP_FEATURE_MAX_MB_NUM <= 96U */
#endif /* FLEXCAN_IP_FEATURE_MAX_MB_NUM > 64U */

#if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 96U
    else
    {
        Flag = ((pBase->IFLAG4 & ((uint32)1U << (MsgBuffIdx % 32U))) >> (MsgBuffIdx % 32U));
    }
#endif

    return (uint8)Flag;
}

/*!
 * @brief Get the interrupt Imask of the message buffers.
 *
 * @param   pBase       The FlexCAN base address
 * @param   MsgBuffIdx  Index of the message buffer
 * @return  The value of interrupt Imask of the message buffer.
 */
static inline uint8 FlexCAN_GetMsgBuffIntStatusImask(const FLEXCAN_Type * pBase, uint32 MsgBuffIdx)
{
    uint32 u32Imask = 0U;

    if (MsgBuffIdx < 32U)
    {
        u32Imask = ((pBase->IMASK1 & ((uint32)1U << (MsgBuffIdx % 32U))) >> (MsgBuffIdx % 32U));
    }

#if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 32U
    else if (MsgBuffIdx < 64U)
    {
        u32Imask = ((pBase->IMASK2 & ((uint32)1U << (MsgBuffIdx % 32U))) >> (MsgBuffIdx % 32U));
    }
#if FLEXCAN_IP_FEATURE_MAX_MB_NUM <= 64U
    else
    {
        /* Required Rule 15.7, no 'else' at end of 'if ... else if' chain */
    }
#endif /* FLEXCAN_IP_FEATURE_MAX_MB_NUM <= 64U */
#endif /* FLEXCAN_IP_FEATURE_MAX_MB_NUM > 32U */

#if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 64U
    else if (MsgBuffIdx < 96U)
    {
        u32Imask = ((pBase->IMASK3 & ((uint32)1U << (MsgBuffIdx % 32U))) >> (MsgBuffIdx % 32U));
    }
#if FLEXCAN_IP_FEATURE_MAX_MB_NUM <= 96U
    else
    {
        /* Required Rule 15.7, no 'else' at end of 'if ... else if' chain */
    }
#endif /* FLEXCAN_IP_FEATURE_MAX_MB_NUM <= 96U */
#endif /* FLEXCAN_IP_FEATURE_MAX_MB_NUM > 64U */

#if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 96U
    else
    {
        u32Imask = ((pBase->IMASK4 & ((uint32)1U << (MsgBuffIdx % 32U))) >> (MsgBuffIdx % 32U));
    }
#endif

    return (uint8)u32Imask;
}


#if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)

/*!
 * @brief Sets the FlexCAN time segments for setting up bit rate for FD BRS.
 *
 * @param   pBase      The FlexCAN base address
 * @param   TimeSeg    FlexCAN time segments, which need to be set for the bit rate.
 */
static inline void FlexCAN_SetFDTimeSegments(FLEXCAN_Type * pBase, const Flexcan_Ip_TimeSegmentType * TimeSeg)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(TimeSeg != NULL_PTR);
#endif
    /* Set FlexCAN time segments*/
    (pBase->FDCBT) = ((pBase->FDCBT) & ~((FLEXCAN_FDCBT_FPROPSEG_MASK | FLEXCAN_FDCBT_FPSEG2_MASK |
                                        FLEXCAN_FDCBT_FPSEG1_MASK | FLEXCAN_FDCBT_FPRESDIV_MASK
                                       ) | FLEXCAN_FDCBT_FRJW_MASK
                                      )
                    );

    (pBase->FDCBT) = ((pBase->FDCBT) | (FLEXCAN_FDCBT_FPROPSEG(TimeSeg->propSeg) |
                                      FLEXCAN_FDCBT_FPSEG2(TimeSeg->phaseSeg2) |
                                      FLEXCAN_FDCBT_FPSEG1(TimeSeg->phaseSeg1) |
                                      FLEXCAN_FDCBT_FPRESDIV(TimeSeg->preDivider) |
                                      FLEXCAN_FDCBT_FRJW(TimeSeg->rJumpwidth)
                                     )
                    );
}

#endif /* FLEXCAN_IP_FEATURE_HAS_FD */
/*!
 * @brief Sets the FlexCAN time segments for setting up bit rate.
 *
 * @param   pBase      The FlexCAN base address
 * @param   TimeSeg    FlexCAN time segments, which need to be set for the bit rate.
 */
static inline void FlexCAN_SetTimeSegments(FLEXCAN_Type * pBase, const Flexcan_Ip_TimeSegmentType * TimeSeg)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(TimeSeg != NULL_PTR);
#endif
    (pBase->CTRL1) = ((pBase->CTRL1) & ~((FLEXCAN_CTRL1_PROPSEG_MASK | FLEXCAN_CTRL1_PSEG2_MASK |
                                        FLEXCAN_CTRL1_PSEG1_MASK | FLEXCAN_CTRL1_PRESDIV_MASK
                                       ) | FLEXCAN_CTRL1_RJW_MASK
                                      )
                    );

    (pBase->CTRL1) = ((pBase->CTRL1) | (FLEXCAN_CTRL1_PROPSEG(TimeSeg->propSeg) |
                                      FLEXCAN_CTRL1_PSEG2(TimeSeg->phaseSeg2) |
                                      FLEXCAN_CTRL1_PSEG1(TimeSeg->phaseSeg1) |
                                      FLEXCAN_CTRL1_PRESDIV(TimeSeg->preDivider) |
                                      FLEXCAN_CTRL1_RJW(TimeSeg->rJumpwidth)
                                     )
                    );
}

/*!
 * @brief Sets the FlexCAN extended time segments for setting up bit rate.
 *
 * @param   pBase      The FlexCAN base address
 * @param   TimeSeg    FlexCAN time segments, which need to be set for the bit rate.
 */
static inline void FlexCAN_SetExtendedTimeSegments(FLEXCAN_Type * pBase, const Flexcan_Ip_TimeSegmentType * TimeSeg)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(TimeSeg != NULL_PTR);
#endif
    /* If extended bit time definitions are enabled, use CBT register */
    (pBase->CBT) = ((pBase->CBT) & ~((FLEXCAN_CBT_EPROPSEG_MASK | FLEXCAN_CBT_EPSEG2_MASK |
                                    FLEXCAN_CBT_EPSEG1_MASK | FLEXCAN_CBT_EPRESDIV_MASK
                                   ) | FLEXCAN_CBT_ERJW_MASK
                                  )
                  );

    (pBase->CBT) = ((pBase->CBT) | (FLEXCAN_CBT_EPROPSEG(TimeSeg->propSeg) |
                                  FLEXCAN_CBT_EPSEG2(TimeSeg->phaseSeg2) |
                                  FLEXCAN_CBT_EPSEG1(TimeSeg->phaseSeg1) |
                                  FLEXCAN_CBT_EPRESDIV(TimeSeg->preDivider) |
                                  FLEXCAN_CBT_ERJW(TimeSeg->rJumpwidth)
                                 )
                  );
}

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
/*!
 * @brief Sets the FlexCAN Enhanced time segments for setting up nominal bit rate.
 *
 * @param   pBase      The FlexCAN base address
 * @param   TimeSeg    FlexCAN time segments, which need to be set for the bit rate.
 */
static inline void FlexCAN_SetEnhancedNominalTimeSegments(FLEXCAN_Type * pBase, const Flexcan_Ip_TimeSegmentType * TimeSeg)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
    DevAssert(TimeSeg != NULL_PTR);
#endif
    (pBase->ENCBT) = ((pBase->ENCBT) & ~(FLEXCAN_ENCBT_NTSEG1_MASK | FLEXCAN_ENCBT_NTSEG2_MASK | FLEXCAN_ENCBT_NRJW_MASK));

    (pBase->ENCBT) = ((pBase->ENCBT) |(FLEXCAN_ENCBT_NTSEG1(TimeSeg->phaseSeg1 + TimeSeg->propSeg + 1U) |
                                     FLEXCAN_ENCBT_NTSEG2(TimeSeg->phaseSeg2) |
                                     FLEXCAN_ENCBT_NRJW(TimeSeg->rJumpwidth)
                                    )
                    );
    (pBase->EPRS) = (pBase->EPRS & ~FLEXCAN_EPRS_ENPRESDIV_MASK);
    (pBase->EPRS) |= FLEXCAN_EPRS_ENPRESDIV(TimeSeg->preDivider);
}

/*!
 * @brief Get the FlexCAN Enhanced time segments for nominal bit rate.
 *
 * @param   pBase      The FlexCAN base address
 * @param   TimeSeg    FlexCAN time segments, which need to be set for the bit rate.
 */
static inline void FlexCAN_GetEnhancedNominalTimeSegments(const FLEXCAN_Type * pBase, Flexcan_Ip_TimeSegmentType * TimeSeg)
{
    TimeSeg->propSeg = 0;
    TimeSeg->preDivider = ((pBase->EPRS & FLEXCAN_EPRS_ENPRESDIV_MASK) >> FLEXCAN_EPRS_ENPRESDIV_SHIFT);
    TimeSeg->phaseSeg1 = ((pBase->ENCBT & FLEXCAN_ENCBT_NTSEG1_MASK) >> FLEXCAN_ENCBT_NTSEG1_SHIFT);
    TimeSeg->phaseSeg2 = ((pBase->ENCBT & FLEXCAN_ENCBT_NTSEG2_MASK) >> FLEXCAN_ENCBT_NTSEG2_SHIFT);
    TimeSeg->rJumpwidth = ((pBase->ENCBT & FLEXCAN_ENCBT_NRJW_MASK) >> FLEXCAN_ENCBT_NRJW_SHIFT);
}

/*!
 * @brief Sets the FlexCAN Enhanced time segments for setting up data bit rate.
 *
 * @param   pBase      The FlexCAN base address
 * @param   TimeSeg    FlexCAN time segments, which need to be set for the bit rate.
 */
static inline void FlexCAN_SetEnhancedDataTimeSegments(FLEXCAN_Type * pBase, const Flexcan_Ip_TimeSegmentType * TimeSeg)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
DevAssert(TimeSeg != NULL_PTR);
#endif
(pBase->EDCBT) = ((pBase->EDCBT) & ~(FLEXCAN_EDCBT_DTSEG1_MASK | FLEXCAN_EDCBT_DTSEG2_MASK | FLEXCAN_EDCBT_DRJW_MASK));

(pBase->EDCBT) = ((pBase->EDCBT) | (FLEXCAN_EDCBT_DTSEG1(TimeSeg->phaseSeg1 + TimeSeg->propSeg) |
                                  FLEXCAN_EDCBT_DTSEG2(TimeSeg->phaseSeg2) |
                                  FLEXCAN_EDCBT_DRJW(TimeSeg->rJumpwidth)
                                 )
                );

(pBase->EPRS) = (pBase->EPRS & ~FLEXCAN_EPRS_EDPRESDIV_MASK);
(pBase->EPRS) |= FLEXCAN_EPRS_EDPRESDIV(TimeSeg->preDivider);
}
/*!
 * @brief Get the FlexCAN Enhanced time segments.
 *
 * @param   pBase      The FlexCAN base address
 * @param   TimeSeg    FlexCAN time segments read for bit rate
 */
static inline void FlexCAN_GetEnhancedDataTimeSegments(const FLEXCAN_Type * pBase, Flexcan_Ip_TimeSegmentType * TimeSeg)
{
#if (FLEXCAN_IP_DEV_ERROR_DETECT == STD_ON)
DevAssert(TimeSeg != NULL_PTR);
#endif
TimeSeg->propSeg = 0U;
TimeSeg->phaseSeg1 = ((pBase->EDCBT & FLEXCAN_EDCBT_DTSEG1_MASK) >> FLEXCAN_EDCBT_DTSEG1_SHIFT);
TimeSeg->phaseSeg2 = ((pBase->EDCBT & FLEXCAN_EDCBT_DTSEG2_MASK) >> FLEXCAN_EDCBT_DTSEG2_SHIFT);
TimeSeg->rJumpwidth = ((pBase->EDCBT & FLEXCAN_EDCBT_DRJW_MASK) >> FLEXCAN_EDCBT_DRJW_SHIFT);
TimeSeg->preDivider = ((pBase->EPRS & FLEXCAN_EPRS_EDPRESDIV_MASK) >> FLEXCAN_EPRS_EDPRESDIV_SHIFT);
}
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON) */

/*!
 * @brief Gets the FlexCAN extended time segments used for setting up bit rate.
 *
 * @param[in]   pBase       The FlexCAN base address
 * @param[out]   TimeSeg    FlexCAN time segments read for bit rate
 */
static inline void FlexCAN_GetExtendedTimeSegments(const FLEXCAN_Type * pBase, Flexcan_Ip_TimeSegmentType * TimeSeg)
{
    TimeSeg->preDivider = ((pBase->CBT) & FLEXCAN_CBT_EPRESDIV_MASK) >> FLEXCAN_CBT_EPRESDIV_SHIFT;
    TimeSeg->propSeg = ((pBase->CBT) & FLEXCAN_CBT_EPROPSEG_MASK) >> FLEXCAN_CBT_EPROPSEG_SHIFT;
    TimeSeg->phaseSeg1 = ((pBase->CBT) & FLEXCAN_CBT_EPSEG1_MASK) >> FLEXCAN_CBT_EPSEG1_SHIFT;
    TimeSeg->phaseSeg2 = ((pBase->CBT) & FLEXCAN_CBT_EPSEG2_MASK) >> FLEXCAN_CBT_EPSEG2_SHIFT;
    TimeSeg->rJumpwidth = ((pBase->CBT) & FLEXCAN_CBT_ERJW_MASK) >> FLEXCAN_CBT_ERJW_SHIFT;
}

/*!
 * @brief Gets the FlexCAN time segments to calculate the bit rate.
 *
 * @param[in]   pBase      The FlexCAN base address
 * @param[out]  TimeSeg    FlexCAN time segments read for bit rate
 */
static inline void FlexCAN_GetTimeSegments(const FLEXCAN_Type * pBase, Flexcan_Ip_TimeSegmentType * TimeSeg)
{
    TimeSeg->preDivider = ((pBase->CTRL1) & FLEXCAN_CTRL1_PRESDIV_MASK) >> FLEXCAN_CTRL1_PRESDIV_SHIFT;
    TimeSeg->propSeg = ((pBase->CTRL1) & FLEXCAN_CTRL1_PROPSEG_MASK) >> FLEXCAN_CTRL1_PROPSEG_SHIFT;
    TimeSeg->phaseSeg1 = ((pBase->CTRL1) & FLEXCAN_CTRL1_PSEG1_MASK) >> FLEXCAN_CTRL1_PSEG1_SHIFT;
    TimeSeg->phaseSeg2 = ((pBase->CTRL1) & FLEXCAN_CTRL1_PSEG2_MASK) >> FLEXCAN_CTRL1_PSEG2_SHIFT;
    TimeSeg->rJumpwidth = ((pBase->CTRL1) & FLEXCAN_CTRL1_RJW_MASK) >> FLEXCAN_CTRL1_RJW_SHIFT;
}

#if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
/*!
 * @brief Gets the  FlexCAN time segments for FD BRS to calculate the bit rate.
 *
 * @param   pBase      The FlexCAN base address
 * @param   TimeSeg    FlexCAN time segments read for bit rate
 */
static inline void FlexCAN_GetFDTimeSegments(const FLEXCAN_Type * pBase, Flexcan_Ip_TimeSegmentType * TimeSeg)
{
    TimeSeg->preDivider = ((pBase->FDCBT) & FLEXCAN_FDCBT_FPRESDIV_MASK) >> FLEXCAN_FDCBT_FPRESDIV_SHIFT;
    TimeSeg->propSeg = ((pBase->FDCBT) & FLEXCAN_FDCBT_FPROPSEG_MASK) >> FLEXCAN_FDCBT_FPROPSEG_SHIFT;
    TimeSeg->phaseSeg1 = ((pBase->FDCBT) & FLEXCAN_FDCBT_FPSEG1_MASK) >> FLEXCAN_FDCBT_FPSEG1_SHIFT;
    TimeSeg->phaseSeg2 = ((pBase->FDCBT) & FLEXCAN_FDCBT_FPSEG2_MASK) >> FLEXCAN_FDCBT_FPSEG2_SHIFT;
    TimeSeg->rJumpwidth = ((pBase->FDCBT) & FLEXCAN_FDCBT_FRJW_MASK) >> FLEXCAN_FDCBT_FRJW_SHIFT;
}

#endif /* if FLEXCAN_IP_FEATURE_HAS_FD */

/*!
 * @brief Checks if the Extended Time Segment are enabled.
 *
 * @param   pBase    The FlexCAN base address
 * @return  TRUE if enabled; FALSE if disabled
 */
static inline boolean FlexCAN_IsExCbtEnabled(const FLEXCAN_Type * pBase)
{
    return (0U == ((pBase->CBT & FLEXCAN_CBT_BTF_MASK) >> FLEXCAN_CBT_BTF_SHIFT)) ? FALSE : TRUE;
}

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
/*!
 * @brief Checks if the Enhanced Time Segment are enabled.
 *
 * @param   pBase    The FlexCAN base address
 * @return  TRUE if enabled; FALSE if disabled
 */
static inline boolean FlexCAN_IsEnhCbtEnabled(const FLEXCAN_Type * pBase)
{
    return (0U == ((pBase->CTRL2 & FLEXCAN_CTRL2_BTE_MASK) >> FLEXCAN_CTRL2_BTE_SHIFT)) ? FALSE : TRUE;
}

/*!
 * @brief Set the Enhanced Time Segment are enabled or disabled.
 *
 * @param   pBase     The FlexCAN base address
 * @param   EnableCBT Enable/Disable use of Enhanced Time Segments
 */
static inline void FlexCAN_EnhCbtEnable(FLEXCAN_Type * pBase, boolean EnableCBT)
{   /* Enable the use of extended bit time definitions */
    pBase->CTRL2 = (pBase->CTRL2 & ~FLEXCAN_CTRL2_BTE_MASK) | FLEXCAN_CTRL2_BTE(EnableCBT ? 1UL : 0UL);
}
#endif
/*!
 * @brief Set the Extended Time Segment are enabled or disabled.
 *
 * @param   pBase     The FlexCAN base address
 * @param   EnableCBT Enable/Disable use of Extent Time Segments
 */
static inline void FlexCAN_EnableExtCbt(FLEXCAN_Type * pBase, boolean EnableCBT)
{   /* Enable the use of extended bit time definitions */
    pBase->CBT = (pBase->CBT & ~FLEXCAN_CBT_BTF_MASK) | FLEXCAN_CBT_BTF(EnableCBT ? 1UL : 0UL);
}

/*!
 * @brief Set operation mode.
 *
 * @param   pBase  The FlexCAN base address
 * @param   Mode   Set an operation mode
 */
void FlexCAN_SetOperationMode(FLEXCAN_Type * pBase, Flexcan_Ip_ModesType Mode);


/*!
 * @brief Enables/Disables the Self Reception feature.
 *
 * If enabled, FlexCAN is allowed to receive frames transmitted by itself.
 *
 * @param   pBase  The FlexCAN base address
 * @param   Enable Enable/Disable Self Reception
 */
static inline void FlexCAN_SetSelfReception(FLEXCAN_Type * pBase, boolean Enable)
{
    pBase->MCR = (pBase->MCR & ~FLEXCAN_MCR_SRXDIS_MASK) | FLEXCAN_MCR_SRXDIS(Enable ? 0UL : 1UL);
}

/*!
 * @brief Checks if the Flexible Data rate feature is enabled.
 *
 * @param   pBase    The FlexCAN base address
 * @return  TRUE if enabled; FALSE if disabled
 */
static inline boolean FlexCAN_IsFDEnabled(const FLEXCAN_Type * pBase)
{
    return ((pBase->MCR & FLEXCAN_MCR_FDEN_MASK) >> FLEXCAN_MCR_FDEN_SHIFT) != 0U;
}

/*!
 * @brief Checks if the listen only mode is enabled.
 *
 * @param   pBase    The FlexCAN base address
 * @return  TRUE if enabled; FALSE if disabled
 */
static inline boolean FlexCAN_IsListenOnlyModeEnabled(const FLEXCAN_Type * pBase)
{
    return (((pBase->CTRL1 & (FLEXCAN_CTRL1_LOM_MASK)) != 0U) ? TRUE : FALSE);
}


/*!
 * @brief Return last Message Buffer Occupied By RxFIFO
 *
 * @param   FilterCnt    Number of Configured RxFIFO Filters
 * @return  number of last MB occupied by RxFIFO
 */
static inline uint32 RxFifoOcuppiedLastMsgBuff(uint8 FilterCnt)
{
    return 5U + (((((uint32)FilterCnt) + 1U) * 8U) / 4U);
}

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
/*!
 * @brief Computes the maximum payload size (in bytes), given a DLC
 *
 * @param   DlcValue    DLC code from the MB memory.
 * @return  payload size (in bytes)
 */
uint8 FlexCAN_ComputePayloadSize(uint8 DlcValue);
#endif /*(FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON) */

/*!
 * @brief Sets the FlexCAN message buffer fields for transmitting.
 *
 * @param   pBase        The FlexCAN base address
 * @param   MsgBuffIdx   Index of the message buffer
 * @return  Pointer to the beginning of the MBs space address
 */
volatile uint32 * FlexCAN_GetMsgBuffRegion(const FLEXCAN_Type * pBase, uint32 MsgBuffIdx);

#if (FLEXCAN_IP_FEATURE_SWITCHINGISOMODE == STD_ON)
/*!
 * @brief Enables/Disables FD frame compatible with ISO-FD Frame ISO 11898-1 (2003)
 *
 * The CAN FD protocol has been improved to increase the failure detection capability that was in the original CAN FD protocol,
 * which is also called non-ISO CAN FD, by CAN in Automation (CiA). A three-bit stuff counter and a parity bit have been introduced
 * in the improved CAN FD protocol, now called ISO CAN FD. The CRC calculation has also been modified. All these improvements
 * make the ISO CAN FD protocol incompatible with the non-FD CAN FD protocol.
 *
 * @param   pBase  The FlexCAN base address
 * @param   Enable Enable/Disable ISO FD Compatible mode.
 */
static inline void FlexCAN_SetIsoCan(FLEXCAN_Type * pBase, boolean Enable)
{
    pBase->CTRL2 = (pBase->CTRL2 & ~FLEXCAN_CTRL2_ISOCANFDEN_MASK) | FLEXCAN_CTRL2_ISOCANFDEN(Enable ? 1UL : 0UL);
}
#endif
/*!
 * @brief This bit controls the comparison of IDE and RTR bits within Rx mailbox filters with their corresponding bits
 * in the incoming frame by the matching process. This bit does not affect matching for Legacy Rx FIFO or Enhanced Rx FIFO.
 *
 * @param   pBase  The FlexCAN base address
 * @param   Enable Enables/Disables the comparison of both Rx mailbox filter’s IDE and RTR bit with their corresponding
 * bits within the incoming frame.
 */
static inline void FlexCAN_SetEntireFrameArbitrationFieldComparison(FLEXCAN_Type * pBase, boolean Enable)
{
    pBase->CTRL2 = (pBase->CTRL2 & ~FLEXCAN_CTRL2_EACEN_MASK) | FLEXCAN_CTRL2_EACEN(Enable ? 1UL : 0UL);
}
#if (FLEXCAN_IP_FEATURE_PROTOCOLEXCEPTION == STD_ON)
/*!
 * @brief Enables/Disable the protocol exception feature
 *
 * @param   pBase  The FlexCAN base address
 * @param   Enable Enables/Disable the protocol exception feature.
 */
static inline void FlexCAN_SetProtocolException(FLEXCAN_Type * pBase, boolean Enable)
{
    pBase->CTRL2 = (pBase->CTRL2 & ~FLEXCAN_CTRL2_PREXCEN_MASK) | FLEXCAN_CTRL2_PREXCEN(Enable ? 1UL : 0UL);
}
#endif
/*!
 * @brief If this bit is asserted a remote request frame is submitted to a matching process and stored in the
 * corresponding message buffer in the same fashion as a data frame. No automatic remote response frame will be generated.
 *
 * @param   pBase  The FlexCAN base address
 * @param   Enable Enable/Disable Remote Request Storing.
 */
static inline void FlexCAN_SetRemoteReqStore(FLEXCAN_Type * pBase, boolean Enable)
{
    pBase->CTRL2 = (pBase->CTRL2 & ~FLEXCAN_CTRL2_RRS_MASK) | FLEXCAN_CTRL2_RRS(Enable ? 1UL : 0UL);
}
/*!
 * @brief Enable/Disable Automatic recovering from Bus Off state
 *
 * @param   pBase  The FlexCAN base address
 * @param   Enable Enable/Disable Automatic recovering from Bus Off state.
 */
static inline void FlexCAN_SetBusOffAutorecovery(FLEXCAN_Type * pBase, boolean Enable)
{
    pBase->CTRL1 = (pBase->CTRL1 & ~FLEXCAN_CTRL1_BOFFREC_MASK) | FLEXCAN_CTRL1_BOFFREC(Enable ? 0UL : 1UL);
}
#if (FLEXCAN_IP_FEATURE_EDGEFILTER == STD_ON)
/*!
 * @brief Enable/Disable the edge filter used during the bus integration state.
 *
 * @param   pBase  The FlexCAN base address
 * @param   Enable Enable/Disable the edge filter used during the bus integration state.
 */
static inline void FlexCAN_SetEdgeFilter(FLEXCAN_Type * pBase, boolean Enable)
{
    pBase->CTRL2 = (pBase->CTRL2 & ~FLEXCAN_CTRL2_EDFLTDIS_MASK) | FLEXCAN_CTRL2_EDFLTDIS(Enable ? 0UL : 1UL);
}
#endif
/*!
 * @brief Enable/Disable the sampling mode of CAN bits at the Rx input.
 *
 * @param   pBase  The FlexCAN base address
 * @param   Enable Enable/Disable the sampling mode of CAN bits at the Rx input.
 */
static inline void FlexCAN_CanBitSampling(FLEXCAN_Type * pBase, boolean Enable)
{
    pBase->CTRL1 = (pBase->CTRL1 & ~FLEXCAN_CTRL1_SMP_MASK) | FLEXCAN_CTRL1_SMP(Enable ? 1UL : 0UL);
}
#if (FLEXCAN_IP_FEATURE_HAS_PE_CLKSRC_SELECT == STD_ON)
/*!
 * @brief Specifies if The CAN engine clock source is the oscillator clock or peripheral clock.
 *
 * @param   pBase  The FlexCAN base address
 * @param   Enable Specifies if The CAN engine clock source is the oscillator clock(FALSE) or peripheral clock(TRUE).
 */
static inline void FlexCAN_SetClkSrc(FLEXCAN_Type * pBase, boolean Enable)
{
    pBase->CTRL1 = (pBase->CTRL1 & ~FLEXCAN_CTRL1_CLKSRC_MASK) | FLEXCAN_CTRL1_CLKSRC(Enable ? 1UL : 0UL);
}
#endif

/*!
 * @brief Enables/Disable the Lowest Buffer Transmitted First feature
 *
 * @param   pBase  The FlexCAN base address
 * @param   Enable Enables/Disable the Lowest Buffer Transmitted First feature.
 */
static inline void FlexCAN_SetLowestBufferTransmittedFirst(FLEXCAN_Type * pBase, boolean Enable)
{
    pBase->CTRL1 = (pBase->CTRL1 & ~FLEXCAN_CTRL1_LBUF_MASK) | FLEXCAN_CTRL1_LBUF(Enable ? 1UL : 0UL);
}

/*!
 * @brief Get the MB index where the interrupt are enabled and ready to serve
 *
 * @param[in]   pBase         The FlexCAN base address
 * @param[in]   StartMbIdx    Start index of message buffers
 * @param[in]   EndMbIdx      End index of message buffers
 * @param[out]  pMbIdx        Index of the message buffer where the interrupt are enabled and ready to serve
 * @return  TRUE if valid interrupt occurred else return FALSE
 */
static inline boolean FlexCAN_GetMsgBuffIntIndex(const FLEXCAN_Type * pBase,
                                                 uint32 StartMbIdx,
                                                 uint32 EndMbIdx,
                                                 uint32 * pMbIdx
                                                )
{
    uint32 Flag = 0U;
    uint32 Mask = 0U;
    uint32 SubStartMbIdx = FLEXCAN_IP_FEATURE_MAX_MB_NUM;
    uint32 SubEndMbIdx = EndMbIdx;
    boolean RetVal = FALSE;

    while (SubStartMbIdx > StartMbIdx)
    {
        if (SubEndMbIdx < 32U)
        {
            SubStartMbIdx = 0U;
            Mask = pBase->IMASK1 & FLEXCAN_IMASK1_BUF31TO0M_MASK;
            Flag = pBase->IFLAG1 & Mask;
        }
    #if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 32U
        else if (SubEndMbIdx < 64U)
        {
            SubStartMbIdx = 32U;
            Mask = pBase->IMASK2 & FLEXCAN_IMASK2_BUF63TO32M_MASK;
            Flag = pBase->IFLAG2 & Mask;
        }
    #endif /* if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 32U */
    #if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 64U
        else if (SubEndMbIdx < 96U)
        {
            SubStartMbIdx = 64U;
            Mask = pBase->IMASK3 & FLEXCAN_IMASK3_BUF95TO64M_MASK;
            Flag = pBase->IFLAG3 & Mask;
        }
    #endif /* if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 64U */
    #if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 96U
        else if (SubEndMbIdx < 128U)
        {
            SubStartMbIdx = 96U;
            Mask = pBase->IMASK4 & FLEXCAN_IMASK4_BUF127TO96M_MASK;
            Flag = pBase->IFLAG4 & Mask;
        }
    #endif /* if FLEXCAN_IP_FEATURE_MAX_MB_NUM > 96U */
        else
        {
            /* nothing */
        }

        if (SubStartMbIdx < StartMbIdx)
        {
            SubStartMbIdx = StartMbIdx;
        }

        if (Flag != 0U)
        {
            for (; SubEndMbIdx >= SubStartMbIdx; SubEndMbIdx--)
            {
                if ((Flag >> (SubEndMbIdx % 32U)) != 0U)
                {
                    *pMbIdx = SubEndMbIdx;
                    RetVal = TRUE;
                    break;
                }
            }
            if (TRUE == RetVal)
            {
                break;
            }
        }
        else
        {
            SubEndMbIdx = (SubStartMbIdx > 0U) ? (SubStartMbIdx - 1U) : 0U;
        }
    }
    return RetVal;
}

/*!
 * @brief Sets the FlexCAN Rx Message Buffer global mask.
 *
 * @param   pBase  The FlexCAN base address
 * @param   Mask   Mask Value
 */
static inline void FlexCAN_SetRxMsgBuffGlobalMask(FLEXCAN_Type * pBase, uint32 Mask)
{
    (pBase->RXMGMASK) = Mask;
}

/*!
 * @brief Sets the FlexCAN Rx individual mask for ID filtering in the Rx Message Buffers and the Rx FIFO.
 *
 * @param   pBase       The FlexCAN base address
 * @param   MsgBuffIdx  Index of the message buffer/filter
 * @param   Mask        Individual mask
 */
static inline void FlexCAN_SetRxIndividualMask(FLEXCAN_Type * pBase,
                                               uint32 MsgBuffIdx,
                                               uint32 Mask
                                              )
{
    pBase->RXIMR[MsgBuffIdx] = Mask;
}

/*!
 * @brief Check if controller is in freeze mode or not.
 *
 * @param   pBase  The FlexCAN base address
 * @return  TRUE if controller is in freeze mode
 *          FALSE if controller is not in freeze mode
 */
static inline boolean FlexCAN_IsFreezeMode(const FLEXCAN_Type * pBase)
{
    return (((pBase->MCR & (FLEXCAN_MCR_FRZACK_MASK)) != 0U)? TRUE : FALSE);
}

/*!
 * @brief Set Tx arbitration start delay.
 *
 * @param   pBase  The FlexCAN base address
 * @param   Tasd   The Tx arbitration start delay value
 */
static inline void FlexCAN_SetTxArbitrationStartDelay(FLEXCAN_Type * pBase, uint8 Tasd)
{
    pBase->CTRL2 = (pBase->CTRL2 & ~FLEXCAN_CTRL2_TASD_MASK) | FLEXCAN_CTRL2_TASD(Tasd);
}

/*!
 * @brief Sets the Rx masking type.
 *
 * @param   pBase  The FlexCAN base address
 * @param   Type   The FlexCAN Rx mask type
 */
static inline void FlexCAN_SetRxMaskType(FLEXCAN_Type * pBase, Flexcan_Ip_RxMaskType Type)
{
    /* Set RX masking type (RX global mask or RX individual mask)*/
    if (FLEXCAN_RX_MASK_GLOBAL == Type)
    {
        /* Enable Global RX masking */
        pBase->MCR = (pBase->MCR & ~FLEXCAN_MCR_IRMQ_MASK) | FLEXCAN_MCR_IRMQ(0U);
    }
    else
    {
        /* Enable Individual Rx Masking and Queue */
        pBase->MCR = (pBase->MCR & ~FLEXCAN_MCR_IRMQ_MASK) | FLEXCAN_MCR_IRMQ(1U);
    }
}

/*!
 * @brief Checks if Legacy Rx FIFO is enabled.
 *
 * @param   pBase     The FlexCAN base address
 * @return  Legacy Fifo status (true = enabled / false = disabled)
 */
static inline boolean FlexCAN_IsRxFifoEnabled(const FLEXCAN_Type * pBase)
{
    return ((((pBase->MCR & FLEXCAN_MCR_RFEN_MASK) >> FLEXCAN_MCR_RFEN_SHIFT) != 0U) ? (TRUE): (FALSE));
}

#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)

/*FUNCTION**********************************************************************
 *
 * Function Name : FLEXCAN_SetEnhancedRxFifoIntAll
 * Description   : Enable/Disable All(Underflow, Overflow, Watermark and frame available)
 *                 interrupts for Enhanced Rx FIFO.
 *
 *END**************************************************************************/
static inline void FlexCAN_SetEnhancedRxFifoIntAll(FLEXCAN_Type * pBase, boolean Enable)
{
    if (TRUE == Enable)
    {
        (pBase->ERFIER) = (uint32)(FLEXCAN_IP_ENHACED_RX_FIFO_ALL_INTERRUPT_MASK);
    }
    else
    {
        (pBase->ERFIER) = (uint32)(FLEXCAN_IP_ENHACED_RX_FIFO_NO_INTERRUPT_MASK);
    }
}

/*!
 * @brief Gets the individual FlexCAN Enhanced Rx FIFO flag.
 *
 * @param   pBase       The FlexCAN base address
 * @param   IntFlag     Index of the Enhanced Rx FIFO flag
 * @return  the individual Enhanced Rx FIFO flag (0 and 1 are the flag value)
 */
static inline uint8 FlexCAN_GetEnhancedRxFIFOStatusFlag(const FLEXCAN_Type * pBase, uint32 IntFlag)
{
    return (uint8)((pBase->ERFSR & ((uint32)1U << ((uint8)IntFlag & (uint8)0x1F))) >> ((uint8)IntFlag & (uint8)0x1F));
}

/*!
 * @brief Clears the interrupt flag of the Enhanced Rx FIFO.
 *
 * @param   pBase     The FlexCAN base address
 * @param   IntFlag   Index of the Enhanced Rx FIFO interrupt flag
 */
static inline void FlexCAN_ClearEnhancedRxFifoIntStatusFlag(FLEXCAN_Type * pBase, uint32 IntFlag)
{
    (pBase->ERFSR) = (uint32)1U << IntFlag;
}

/*!
 * @brief Gets the individual FlexCAN Enhanced Rx FIFO interrupt flag.
 *
 * @param   pBase       The FlexCAN base address
 * @param   IntFlag     Index of the Enhanced Rx FIFO interrupt flag
 * @return  the individual Enhanced Rx FIFO interrupt flag (0 and 1 are the flag value)
 */
static inline uint8 FlexCAN_GetEnhancedRxFIFOIntStatusFlag(const FLEXCAN_Type * pBase, uint32 IntFlag)
{
    return (uint8)((pBase->ERFIER & ((uint32)1U << ((uint8)IntFlag & (uint8)0x1F))) >> ((uint8)IntFlag & (uint8)0x1F));
}
/*!
 * @brief Checks if FlexCAN has Enhanced Rx FIFO.
 *
 * @param   pBase  The FlexCAN base address
 * @return  EnhancedRxFifo status (TRUE = available / FALSE = unavailable)
 */
boolean FlexCAN_IsEnhancedRxFifoAvailable(const FLEXCAN_Type * pBase);

/*!
 * @brief Checks if Enhanced Rx FIFO is enabled.
 *
 * @param   pBase     The FlexCAN base address
 * @return  EnhancedRxFifo status (true = enabled / false = disabled)
 */
boolean FlexCAN_IsEnhancedRxFifoEnabled(const FLEXCAN_Type * pBase);

Flexcan_Ip_StatusType FlexCAN_EnableEnhancedRxFifo(FLEXCAN_Type * pBase,
                                                   uint32 NumOfStdIDFilters,
                                                   uint32 NumOfExtIDFilters,
                                                   uint32 NumOfWatermark
                                                  );
void FlexCAN_SetEnhancedRxFifoFilter(const uint8 u8Instance, const Flexcan_Ip_EnhancedIdTableType * IdFilterTable);

#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
/*!
 * @brief Clear Enhance fifo data.
 *
 * @param   pBase  The FlexCAN base address
 * @return  void
 */
void FlexCAN_ClearOutputEnhanceFIFO(FLEXCAN_Type * pBase);
#endif /* (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON) */

void FlexCAN_ReadEnhancedRxFifo(const uint8 u8Instance, Flexcan_Ip_MsgBuffType * RxFifo);
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON) */
#if (FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE == STD_ON)
/*!
 * @brief Clear Legacy fifo data.
 *
 * @param   pBase  The FlexCAN base address
 * @return  void
 */
void FlexCAN_ClearOutputLegacyFIFO(FLEXCAN_Type * pBase);
#endif /* FLEXCAN_IP_FEATURE_HAS_DMA_ENABLE */

#if (FLEXCAN_IP_FEATURE_HAS_TS_ENABLE == STD_ON)
/*!
 * @brief Configures the timestamp feature.
 *
 * @param   Instance FlexCAN instance
 * @param   pBase    The FlexCAN base address.
 * @param   Config   The timestamp configuration structure.
 */
void FlexCAN_ConfigTimestamp(uint8 Instance, FLEXCAN_Type * pBase, const Flexcan_Ip_TimeStampConfigType * Config);

#if (FLEXCAN_IP_FEATURE_HAS_HR_TIMER == STD_ON)
/*!
 * @brief Checks if High Resolution Time Stamp is enabled.
 *
 * @param   pBase     The FlexCAN base address
 * @return  HRTimeStamp status (true = enabled / false = disabled)
 */
static inline boolean FLEXCAN_IsHRTimeStampEnabled(const FLEXCAN_Type * pBase)
{
    return ((((pBase->CTRL2 & FLEXCAN_CTRL2_TSTAMPCAP_MASK) >> FLEXCAN_CTRL2_TSTAMPCAP_SHIFT) != 0U) ? TRUE : FALSE);
}
#endif /*(FLEXCAN_IP_FEATURE_HAS_HR_TIMER == STD_ON) */
#endif /* #if (FLEXCAN_IP_FEATURE_HAS_TS_ENABLE == STD_ON) */
/*!
 * @brief configure controller depending on options.
 *
 * @param   pBase          The FlexCAN base address.
 * @param   u32Options     Controller Options.
 */
void FlexCAN_ConfigCtrlOptions(FLEXCAN_Type * pBase, uint32 u32Options);

/*!
 * @brief Will set Flexcan Peripheral Register to default val.
 *
 * @param   pBase    The FlexCAN base address
 */
static inline void FlexCAN_SetRegDefaultVal(FLEXCAN_Type * pBase)
{
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON)
    if (TRUE == FlexCAN_IsEnhancedRxFifoAvailable(pBase))
    {
        pBase->ERFSR = FLEXCAN_IP_ERFSR_DEFAULT_VALUE_U32;
        pBase->ERFIER = FLEXCAN_IP_ERFIER_DEFAULT_VALUE_U32;
        pBase->ERFCR = FLEXCAN_IP_ERFCR_DEFAULT_VALUE_U32;
    }
#endif /* (FLEXCAN_IP_FEATURE_HAS_ENHANCED_RX_FIFO == STD_ON) */
#if (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON)
#if defined(FLEXCAN_IP_FEATURE_FD_NOT_ALL_INSTANCES)
    if (TRUE == FlexCAN_IsFDAvailable(pBase))
#endif
    {
        pBase->FDCBT = FLEXCAN_IP_FDCBT_DEFAULT_VALUE_U32;
        pBase->FDCTRL = FLEXCAN_IP_FDCTRL_DEFAULT_VALUE_U32;
    }
#endif /* (FLEXCAN_IP_FEATURE_HAS_FD == STD_ON) */
#if (FLEXCAN_IP_FEATURE_HAS_MEM_ERR_DET == STD_ON)
    pBase->ERRSR = FLEXCAN_IP_ERRSR_DEFAULT_VALUE_U32;
    pBase->ERRIPPR = FLEXCAN_IP_ERRIPPR_DEFAULT_VALUE_U32;
    pBase->ERRIDPR = FLEXCAN_IP_ERRIDPR_DEFAULT_VALUE_U32;
    pBase->ERRIAR = FLEXCAN_IP_ERRIAR_DEFAULT_VALUE_U32;
    /* Enable write of MECR register */
    pBase->CTRL2 = (pBase->CTRL2 & ~FLEXCAN_CTRL2_ECRWRE_MASK) | FLEXCAN_CTRL2_ECRWRE(1);
    /* Enable write of MECR */
    pBase->MECR = (pBase->MECR & ~FLEXCAN_MECR_ECRWRDIS_MASK) | FLEXCAN_MECR_ECRWRDIS(0);
    /* set Default value */
    pBase->MECR = FLEXCAN_IP_MECR_DEFAULT_VALUE_U32;
    /* disable write of MECR */
    pBase->MECR = (pBase->MECR & ~FLEXCAN_MECR_ECRWRDIS_MASK) | FLEXCAN_MECR_ECRWRDIS(1);
    /* Disable write of MECR */
    pBase->CTRL2 = (pBase->CTRL2 & ~FLEXCAN_CTRL2_ECRWRE_MASK) | FLEXCAN_CTRL2_ECRWRE(0);
#endif /* (FLEXCAN_IP_FEATURE_HAS_MEM_ERR_DET == STD_ON) */
#if (FLEXCAN_IP_FEATURE_MAX_MB_NUM > 96U)
    if (FlexCAN_GetMaxMbNum(pBase) > 96U)
    {
        pBase->IFLAG4 = FLEXCAN_IP_IFLAG_DEFAULT_VALUE_U32;
        pBase->IMASK4 = FLEXCAN_IP_IMASK_DEFAULT_VALUE_U32;
    }
#endif /* (FLEXCAN_IP_FEATURE_MAX_MB_NUM > 96U) */
#if (FLEXCAN_IP_FEATURE_MAX_MB_NUM > 64U)
    if (FlexCAN_GetMaxMbNum(pBase) > 64U)
    {
        pBase->IFLAG3 = FLEXCAN_IP_IFLAG_DEFAULT_VALUE_U32;
        pBase->IMASK3 = FLEXCAN_IP_IMASK_DEFAULT_VALUE_U32;
    }
#endif /* (FLEXCAN_IP_FEATURE_MAX_MB_NUM > 64U) */
#if (FLEXCAN_IP_FEATURE_MAX_MB_NUM > 32U)
    if (FlexCAN_GetMaxMbNum(pBase) > 32U)
    {
        pBase->IFLAG2 = FLEXCAN_IP_IFLAG_DEFAULT_VALUE_U32;
        pBase->IMASK2 = FLEXCAN_IP_IMASK_DEFAULT_VALUE_U32;
    }
#endif /* (FLEXCAN_IP_FEATURE_MAX_MB_NUM > 32U) */
    pBase->IFLAG1 = FLEXCAN_IP_IFLAG_DEFAULT_VALUE_U32;
    pBase->IMASK1 = FLEXCAN_IP_IMASK_DEFAULT_VALUE_U32;
    pBase->CBT = FLEXCAN_IP_CBT_DEFAULT_VALUE_U32;
    pBase->CTRL2 = FLEXCAN_IP_CTRL2_DEFAULT_VALUE_U32;
    pBase->ESR1 = FLEXCAN_IP_ESR1_DEFAULT_VALUE_U32;
    pBase->ECR = FLEXCAN_IP_ECR_DEFAULT_VALUE_U32;
    pBase->TIMER = FLEXCAN_IP_TIMER_DEFAULT_VALUE_U32;
    pBase->CTRL1 = FLEXCAN_IP_CTRL1_DEFAULT_VALUE_U32;
#if (FLEXCAN_IP_FEATURE_HAS_ENHANCE_CBT == STD_ON)
    pBase->EPRS  = FLEXCAN_IP_EPRS_DEFAULT_VALUE_U32;
    pBase->ENCBT = FLEXCAN_IP_ENCBT_DEFAULT_VALUE_U32;
    pBase->EDCBT = FLEXCAN_IP_EDCBT_DEFAULT_VALUE_U32;
    pBase->ETDC  = FLEXCAN_IP_ETDC_DEFAULT_VALUE_U32;
#endif
#ifdef FLEXCAN_IP_FEATURE_HAS_FLTCONF_INT
    pBase->FLTCONF_IE = FLEXCAN_IP_FLTCONF_IE_DEFAULT_VALUE_U32;
#endif
    pBase->MCR = FLEXCAN_IP_MCR_DEFAULT_VALUE_U32;
}

#if (FLEXCAN_IP_FEATURE_HAS_PRETENDED_NETWORKING == STD_ON)
/*!
 * @brief Configures Pretended Networking mode filtering selection.
 *
 * @param   pBase       The FlexCAN base address
 * @param   pPnConfig   The pretended networking configuration
 *
 */
static inline void FlexCAN_SetPNFilteringSelection(FLEXCAN_Type * pBase, const Flexcan_Ip_PnConfigType * pPnConfig)
{
    uint32 u32Tmp;

    u32Tmp = pBase->CTRL1_PN;
    u32Tmp &= ~(FLEXCAN_CTRL1_PN_WTOF_MSK_MASK |
             FLEXCAN_CTRL1_PN_WUMF_MSK_MASK |
             FLEXCAN_CTRL1_PN_NMATCH_MASK |
             FLEXCAN_CTRL1_PN_PLFS_MASK |
             FLEXCAN_CTRL1_PN_IDFS_MASK |
             FLEXCAN_CTRL1_PN_FCS_MASK);
    u32Tmp |= FLEXCAN_CTRL1_PN_WTOF_MSK(pPnConfig->bWakeUpTimeout ? 1UL : 0UL);
    u32Tmp |= FLEXCAN_CTRL1_PN_WUMF_MSK(pPnConfig->bWakeUpMatch ? 1UL : 0UL);
    u32Tmp |= FLEXCAN_CTRL1_PN_NMATCH(pPnConfig->u16NumMatches);
    u32Tmp |= FLEXCAN_CTRL1_PN_FCS(pPnConfig->eFilterComb);
    u32Tmp |= FLEXCAN_CTRL1_PN_IDFS(pPnConfig->eIdFilterType);
    u32Tmp |= FLEXCAN_CTRL1_PN_PLFS(pPnConfig->ePayloadFilterType);
    pBase->CTRL1_PN = u32Tmp;
}

/*!
 * @brief Set PN timeout value.
 *
 * @param   pBase            The FlexCAN base address
 * @param   u16TimeoutValue  timeout for no message matching
 */
static inline void FlexCAN_SetPNTimeoutValue(FLEXCAN_Type * pBase, uint16 u16TimeoutValue)
{
    pBase->CTRL2_PN = (pBase->CTRL2_PN & ~FLEXCAN_CTRL2_PN_MATCHTO_MASK) | FLEXCAN_CTRL2_PN_MATCHTO(u16TimeoutValue);
}

/*!
 * @brief Configures the Pretended Networking ID Filter 1.
 *
 * @param   pBase     The FlexCAN base address
 * @param   IdFilter  The ID Filter configuration
 */
static inline void FlexCAN_SetPNIdFilter1(FLEXCAN_Type * pBase, Flexcan_Ip_PnIdFilterType IdFilter)
{
    uint32 u32Tmp;

    u32Tmp = pBase->FLT_ID1;
    u32Tmp &= ~(FLEXCAN_FLT_ID1_FLT_IDE_MASK | FLEXCAN_FLT_ID1_FLT_RTR_MASK | FLEXCAN_FLT_ID1_FLT_ID1_MASK);
    u32Tmp |= FLEXCAN_FLT_ID1_FLT_IDE(IdFilter.bExtendedId ? 1UL : 0UL);
    u32Tmp |= FLEXCAN_FLT_ID1_FLT_RTR(IdFilter.bRemoteFrame ? 1UL : 0UL);
    if (IdFilter.bExtendedId)
    {
        u32Tmp |= FLEXCAN_FLT_ID1_FLT_ID1(IdFilter.u32Id);
    }
    else
    {
        u32Tmp |= FLEXCAN_FLT_ID1_FLT_ID1(IdFilter.u32Id << FLEXCAN_IP_ID_STD_SHIFT);
    }

    pBase->FLT_ID1 = u32Tmp;
}

/*!
 * @brief Configures the Pretended Networking ID Filter 2 Check IDE&RTR.
 *
 * @param   pBase  The FlexCAN base address

 */
static inline void FlexCAN_SetPNIdFilter2Check(FLEXCAN_Type * pBase)
{
    pBase->FLT_ID2_IDMASK |= FLEXCAN_FLT_ID2_IDMASK_IDE_MSK_MASK | FLEXCAN_FLT_ID2_IDMASK_RTR_MSK_MASK;
}
/*!
 * @brief Configures the Pretended Networking ID Filter 2.
 *
 * @param   pBase      The FlexCAN base address
 * @param   pPnConfig  The pretended networking configuration
 */
static inline void FlexCAN_SetPNIdFilter2(FLEXCAN_Type * pBase, const Flexcan_Ip_PnConfigType * pPnConfig)
{
    uint32 u32Tmp;

    u32Tmp = pBase->FLT_ID2_IDMASK;
    u32Tmp &= ~(FLEXCAN_FLT_ID2_IDMASK_IDE_MSK_MASK | FLEXCAN_FLT_ID2_IDMASK_RTR_MSK_MASK | FLEXCAN_FLT_ID2_IDMASK_FLT_ID2_IDMASK_MASK);
    u32Tmp |= FLEXCAN_FLT_ID2_IDMASK_IDE_MSK(pPnConfig->idFilter2.bExtendedId ? 1UL : 0UL);
    u32Tmp |= FLEXCAN_FLT_ID2_IDMASK_RTR_MSK(pPnConfig->idFilter2.bRemoteFrame ? 1UL : 0UL);
    /* Check if idFilter1 is extended and apply accordingly mask */
    if (pPnConfig->idFilter1.bExtendedId)
    {
        u32Tmp |= FLEXCAN_FLT_ID2_IDMASK_FLT_ID2_IDMASK(pPnConfig->idFilter2.u32Id);
    }
    else
    {
        u32Tmp |= FLEXCAN_FLT_ID2_IDMASK_FLT_ID2_IDMASK(pPnConfig->idFilter2.u32Id << FLEXCAN_IP_ID_STD_SHIFT);
    }

    pBase->FLT_ID2_IDMASK = u32Tmp;
}


/*!
 * @brief Set PN DLC Filter.
 *
 * @param   pBase      The FlexCAN base address
 * @param   u8DlcLow   DLC low value
 * @param   u8DlcHigh  DLC high value
 */
static inline void FlexCAN_SetPNDlcFilter(FLEXCAN_Type * pBase,
                                          uint8 u8DlcLow,
                                          uint8 u8DlcHigh
                                         )
{
    uint32 Tmp;

    Tmp = pBase->FLT_DLC;
    Tmp &= ~(FLEXCAN_FLT_DLC_FLT_DLC_LO_MASK | FLEXCAN_FLT_DLC_FLT_DLC_HI_MASK);
    Tmp |= FLEXCAN_FLT_DLC_FLT_DLC_HI(u8DlcHigh);
    Tmp |= FLEXCAN_FLT_DLC_FLT_DLC_LO(u8DlcLow);
    pBase->FLT_DLC = Tmp;
}

/*!
 * @brief Set PN Payload High Filter 1.
 *
 * @param   pBase     The FlexCAN Base address
 * @param   pPayload  message Payload filter
 */
static inline void FlexCAN_SetPNPayloadHighFilter1(FLEXCAN_Type * pBase, const uint8 * pPayload)
{
    pBase->PL1_HI = FLEXCAN_PL1_HI_Data_byte_4(pPayload[4]) |
                   FLEXCAN_PL1_HI_Data_byte_5(pPayload[5]) |
                   FLEXCAN_PL1_HI_Data_byte_6(pPayload[6]) |
                   FLEXCAN_PL1_HI_Data_byte_7(pPayload[7]);
}

/*!
 * @brief Set PN Payload Low Filter 1.
 *
 * @param   pBase     The FlexCAN Base address
 * @param   pPayload  message Payload filter
 */
static inline void FlexCAN_SetPNPayloadLowFilter1(FLEXCAN_Type * pBase, const uint8 * pPayload)
{
    pBase->PL1_LO = FLEXCAN_PL1_LO_Data_byte_0(pPayload[0]) |
                   FLEXCAN_PL1_LO_Data_byte_1(pPayload[1]) |
                   FLEXCAN_PL1_LO_Data_byte_2(pPayload[2]) |
                   FLEXCAN_PL1_LO_Data_byte_3(pPayload[3]);
}

/*!
 * @brief Set PN Payload High Filter 2.
 *
 * @param   pBase     The FlexCAN Base address
 * @param   pPayload  message Payload filter
 */
static inline void FlexCAN_SetPNPayloadHighFilter2(FLEXCAN_Type * pBase, const uint8 * pPayload)
{
    pBase->PL2_PLMASK_HI = FLEXCAN_PL2_PLMASK_HI_Data_byte_4(pPayload[4]) |
                          FLEXCAN_PL2_PLMASK_HI_Data_byte_5(pPayload[5]) |
                          FLEXCAN_PL2_PLMASK_HI_Data_byte_6(pPayload[6]) |
                          FLEXCAN_PL2_PLMASK_HI_Data_byte_7(pPayload[7]);
}

/*!
 * @brief Set PN Payload Low Filter 2.
 *
 * @param   pBase     The FlexCAN Base address
 * @param   pPayload  message Payload filter
 */
static inline void FlexCAN_SetPNPayloadLowFilter2(FLEXCAN_Type * pBase, const uint8 * pPayload)
{
    pBase->PL2_PLMASK_LO = FLEXCAN_PL2_PLMASK_LO_Data_byte_0(pPayload[0]) |
                          FLEXCAN_PL2_PLMASK_LO_Data_byte_1(pPayload[1]) |
                          FLEXCAN_PL2_PLMASK_LO_Data_byte_2(pPayload[2]) |
                          FLEXCAN_PL2_PLMASK_LO_Data_byte_3(pPayload[3]);
}

/*!
 * @brief Configures the Pretended Networking mode.
 *
 * @param   pBase      The FlexCAN Base address
 * @param   pPnConfig  The pretended networking configuration
 */
void FlexCAN_ConfigPN(FLEXCAN_Type * pBase, const Flexcan_Ip_PnConfigType * pPnConfig);

/*!
 * @brief Enables/Disables the Pretended Networking mode.
 *
 * @param   pBase   The FlexCAN Base address
 * @param   Enable  Enable/Disable Pretending Networking
 */
static inline void FlexCAN_SetPN(FLEXCAN_Type * pBase, boolean Enable)
{
    pBase->MCR = (pBase->MCR & ~FLEXCAN_MCR_PNET_EN_MASK) | FLEXCAN_MCR_PNET_EN(Enable ? 1UL : 0UL);
}

/*!
 * @brief Checks if the Pretended Networking mode is enabled/disabled.
 *
 * @param   pBase  The FlexCAN Base address
 * @return  false if Pretended Networking mode is disabled;
 *          true if Pretended Networking mode is enabled
 */
static inline boolean FlexCAN_IsPNEnabled(const FLEXCAN_Type * pBase)
{
    return ((pBase->MCR & FLEXCAN_MCR_PNET_EN_MASK) >> FLEXCAN_MCR_PNET_EN_SHIFT) != 0U;
}

/*!
 * @brief Gets the Wake Up by Timeout Flag Bit.
 *
 * @param   pBase  The FlexCAN Base address
 * @return  the Wake Up by Timeout Flag Bit
 */
static inline uint8 FlexCAN_GetWTOF(const FLEXCAN_Type * pBase)
{
    return (uint8)((pBase->WU_MTC & FLEXCAN_WU_MTC_WTOF_MASK) >> FLEXCAN_WU_MTC_WTOF_SHIFT);
}

/*!
 * @brief Gets the Wake Up Timeout interrupt enable Bit.
 *
 * @param   pBase  The FlexCAN Base address
 * @return  the Wake Up Timeout interrupt enable Bit
 */
static inline uint8 FlexCAN_GetWTOIE(const FLEXCAN_Type * pBase)
{
    return (uint8)((pBase->CTRL1_PN & FLEXCAN_CTRL1_PN_WTOF_MSK_MASK) >> FLEXCAN_CTRL1_PN_WTOF_MSK_SHIFT);
}

/*!
 * @brief Clears the Wake Up by Timeout Flag Bit.
 *
 * @param   pBase  The FlexCAN Base address
 */
static inline void FlexCAN_ClearWTOF(FLEXCAN_Type * pBase)
{
    pBase->WU_MTC = FLEXCAN_WU_MTC_WTOF_MASK;
}

/*!
 * @brief Gets the Wake Up by Match Flag Bit.
 *
 * @param   pBase  The FlexCAN Base address
 * @return  the Wake Up by Match Flag Bit
 */
static inline uint8 FlexCAN_GetWUMF(const FLEXCAN_Type * pBase)
{
    return (uint8)((pBase->WU_MTC & FLEXCAN_WU_MTC_WUMF_MASK) >> FLEXCAN_WU_MTC_WUMF_SHIFT);
}

/*!
 * @brief Gets the Wake Up Match IE Bit.
 *
 * @param   pBase  The FlexCAN Base address
 * @return  the Wake Up Match IE Bit
 */
static inline uint8 FlexCAN_GetWUMIE(const FLEXCAN_Type * pBase)
{
    return (uint8)((pBase->CTRL1_PN & FLEXCAN_CTRL1_PN_WUMF_MSK_MASK) >> FLEXCAN_CTRL1_PN_WUMF_MSK_SHIFT);
}

/*!
 * @brief Clears the Wake Up by Match Flag Bit.
 *
 * @param   pBase  The FlexCAN Base address
 */
static inline void FlexCAN_ClearWUMF(FLEXCAN_Type * pBase)
{
    pBase->WU_MTC = FLEXCAN_WU_MTC_WUMF_MASK;
}

#endif /* FLEXCAN_IP_FEATURE_HAS_PRETENDED_NETWORKING */

/*!
 * @brief  Reset Imask Buffers.
 *
 * @param   Instance  The FlexCAN instance
 */
void FlexCAN_ResetImaskBuff(uint8 Instance);


#if (FLEXCAN_IP_FEATURE_HAS_MEM_ERR_DET == STD_ON)
#if (FLEXCAN_IP_FEATURE_MEM_ERR_DET_ENABLED == STD_ON)
/*!
 * @brief Check if error correction configuration register write and error configuration register write Enable.
 *
 * @param   pBase  The FlexCAN base address
 */
static inline boolean FlexCAN_HasMemErrorConfigureEnable(FLEXCAN_Type * pBase)
{
 return (((pBase->CTRL2 & FLEXCAN_CTRL2_ECRWRE_MASK) >> FLEXCAN_CTRL2_ECRWRE_SHIFT) != 0U) &&
        (((pBase->MECR & FLEXCAN_MECR_ECRWRDIS_MASK) >> FLEXCAN_MECR_ECRWRDIS_SHIFT) != 1U) ? TRUE : FALSE;
}

/*!
 * @brief Check if Memory Error Detection and Correction mechanism is enabled.
 *
 * @param   pBase  The FlexCAN base address
 */
static inline boolean FlexCAN_IsMemErrorDetectionEnabled(FLEXCAN_Type * pBase)
{
    return (((pBase->MECR & FLEXCAN_MECR_ECCDIS_MASK) >> FLEXCAN_MECR_ECCDIS_SHIFT) != 0U) ? FALSE : TRUE;
}

/*!
 * @brief Read Memory Error Report.
 *
 * @param   pBase           The FlexCAN base address
 * @param   ReportAddr      store report address
 * @param   ReportData      store report data
 * @param   ReportSyndrome  store report syndrome
 */
static inline void FlexCAN_ReadReportError(FLEXCAN_Type * pBase, uint32 * ReportAddr, uint32 * ReportData, uint32 * ReportSyndrome)
{
    *ReportAddr = pBase->RERRAR;
    *ReportData = pBase->RERRDR;
    *ReportSyndrome = pBase->RERRSYNR;
}

/*!
 * @brief Enable Memory Error Detection and Correction in FlexCAN memory.
 *
 * @param   pBase         The FlexCAN base address
 * @param   IsFreezeMode  The response when non-correctable error detected.
 *                        True: put in freeze Mode, False: keep normal Mode.
 */
void FlexCAN_EnableMemErrorDetection(FLEXCAN_Type * pBase, boolean IsFreezeMode);

/*!
 * @brief Set Memory Error Detection and Correction interrupts.
 *
 * @param   pBase          The FlexCAN base address
 * @param   ErrorMaskType  The Memory Error Detection and Correction interrupt Mask
 * @param   IsEnable       Enable/disable interrupt
 */
void FlexCAN_SetMemErrorDetectionIntCmd(FLEXCAN_Type * pBase,
                                        Flexcan_Ip_MemErrDetectionIntMaskType ErrorMaskType,
                                        boolean IsEnable);

/*!
 * @brief Set Memory Error Injection info.
 *
 * @param   pBase               The FlexCAN base address
 * @param   u32InjectionAddr    Error Injection Address
 * @param   u32InjectionData    Error Injection Data Pattern
 * @param   u32InjectionParity  Error Injection Parity Pattern 
 */
void FlexCAN_SetMemErrorInjectionInfo(FLEXCAN_Type * pBase,
                                      uint32 u32InjectionAddr,
                                      uint32 u32InjectionData,
                                      uint32 u32InjectionParity);

/*!
 * @brief Enable/disable Memory Error Injection.
 *
 * @param   pBase          The FlexCAN base address
 * @param   ErrorMaskType  The Memory Error Injection Mask
 * @param   IsEnable       Enable/disable injection
 */
void FlexCAN_SetMemErrorInjectionCmd(FLEXCAN_Type * pBase,
                                     Flexcan_Ip_MemErrInjectionMaskType ErrorMaskType,
                                     boolean IsEnable);

#endif /* (FLEXCAN_IP_FEATURE_MEM_ERR_DET_ENABLED == STD_ON) */

/*!
 * @brief Disable Memory Error Detection and Correction in FlexCAN memory.
 *
 * @param   pBase  The FlexCAN base address
 */
void FlexCAN_DisableMemErrorDetection(FLEXCAN_Type * pBase);

#endif /* (FLEXCAN_IP_FEATURE_HAS_MEM_ERR_DET == STD_ON) */

#define CAN_43_FLEXCAN_STOP_SEC_CODE
#include "Can_43_FLEXCAN_MemMap.h"

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* FLEXCAN_FLEXCAN_IP_HWACCESS_H_ */
