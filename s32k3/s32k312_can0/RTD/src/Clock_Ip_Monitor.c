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
/**
*   @file       Clock_Ip_Monitor.c
*   @version    6.0.0
*
*   @brief   CLOCK driver implementations.
*   @details CLOCK driver implementations.
*
*   @addtogroup CLOCK_DRIVER Clock Ip Driver
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

#include "Clock_Ip_Private.h"
#include "SchM_Mcu.h"

/*==================================================================================================
                               SOURCE FILE VERSION INFORMATION
==================================================================================================*/
#define CLOCK_IP_MONITOR_VENDOR_ID_C                      43
#define CLOCK_IP_MONITOR_AR_RELEASE_MAJOR_VERSION_C       4
#define CLOCK_IP_MONITOR_AR_RELEASE_MINOR_VERSION_C       7
#define CLOCK_IP_MONITOR_AR_RELEASE_REVISION_VERSION_C    0
#define CLOCK_IP_MONITOR_SW_MAJOR_VERSION_C               6
#define CLOCK_IP_MONITOR_SW_MINOR_VERSION_C               0
#define CLOCK_IP_MONITOR_SW_PATCH_VERSION_C               0

/*==================================================================================================
*                                     FILE VERSION CHECKS
==================================================================================================*/
/* Check if Clock_Ip_Monitor.c file and Clock_Ip_Private.h file are of the same vendor */
#if (CLOCK_IP_MONITOR_VENDOR_ID_C != CLOCK_IP_PRIVATE_VENDOR_ID)
    #error "Clock_Ip_Monitor.c and Clock_Ip_Private.h have different vendor ids"
#endif

/* Check if Clock_Ip_Monitor.c file and Clock_Ip_Private.h file are of the same Autosar version */
#if ((CLOCK_IP_MONITOR_AR_RELEASE_MAJOR_VERSION_C != CLOCK_IP_PRIVATE_AR_RELEASE_MAJOR_VERSION) || \
     (CLOCK_IP_MONITOR_AR_RELEASE_MINOR_VERSION_C != CLOCK_IP_PRIVATE_AR_RELEASE_MINOR_VERSION) || \
     (CLOCK_IP_MONITOR_AR_RELEASE_REVISION_VERSION_C != CLOCK_IP_PRIVATE_AR_RELEASE_REVISION_VERSION) \
    )
    #error "AutoSar Version Numbers of Clock_Ip_Monitor.c and Clock_Ip_Private.h are different"
#endif

/* Check if Clock_Ip_Monitor.c file and Clock_Ip_Private.h file are of the same Software version */
#if ((CLOCK_IP_MONITOR_SW_MAJOR_VERSION_C != CLOCK_IP_PRIVATE_SW_MAJOR_VERSION) || \
     (CLOCK_IP_MONITOR_SW_MINOR_VERSION_C != CLOCK_IP_PRIVATE_SW_MINOR_VERSION) || \
     (CLOCK_IP_MONITOR_SW_PATCH_VERSION_C != CLOCK_IP_PRIVATE_SW_PATCH_VERSION) \
    )
    #error "Software Version Numbers of Clock_Ip_Monitor.c and Clock_Ip_Private.h are different"
#endif
#ifndef DISABLE_MCAL_INTERMODULE_ASR_CHECK
/* Check if Clock_Ip_Monitor.c file and SchM_Mcu.h file are of the same Autosar version */
#if ((CLOCK_IP_MONITOR_AR_RELEASE_MAJOR_VERSION_C != SCHM_MCU_AR_RELEASE_MAJOR_VERSION) || \
     (CLOCK_IP_MONITOR_AR_RELEASE_MINOR_VERSION_C != SCHM_MCU_AR_RELEASE_MINOR_VERSION))
    #error "AutoSar Version Numbers of Clock_Ip_Monitor.c and SchM_Mcu.h are different"
#endif
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

/* Clock start initialized section data */
#define MCU_START_SEC_VAR_CLEARED_32
#include "Mcu_MemMap.h"

#ifdef CLOCK_IP_CMU_FC_FCE_REF_CNT_LFREF_HFREF
#if !(defined(CLOCK_IP_REGISTER_VALUES_OPTIMIZATION) && (CLOCK_IP_REGISTER_VALUES_OPTIMIZATION == STD_ON))
static uint32 HashCmu[CLOCK_IP_CMUS_COUNT];
#endif
#endif



/* Clock stop initialized section data */
#define MCU_STOP_SEC_VAR_CLEARED_32
#include "Mcu_MemMap.h"

/*==================================================================================================
*                                        GLOBAL VARIABLES
==================================================================================================*/

/*==================================================================================================
*                                    LOCAL FUNCTION PROTOTYPES
==================================================================================================*/
/* Clock start section code */
#define MCU_START_SEC_CODE

#include "Mcu_MemMap.h"


static void Clock_Ip_ClockMonitorEmpty(Clock_Ip_CmuConfigType const* Config);
static void Clock_Ip_ClockMonitorEmpty_Set( Clock_Ip_CmuConfigType const* Config,
                                            uint32 Index
                                           );
static void Clock_Ip_ClockMonitorEmpty_Disable(Clock_Ip_NameType Name);

#ifdef CLOCK_IP_CMU_FC_FCE_REF_CNT_LFREF_HFREF
static void Clock_Ip_ResetCmuFcFceRefCntLfrefHfref(Clock_Ip_CmuConfigType const* Config);
#if !(defined(CLOCK_IP_REGISTER_VALUES_OPTIMIZATION) && (CLOCK_IP_REGISTER_VALUES_OPTIMIZATION == STD_ON))
static void Clock_Ip_SetCmuFcFceRefCntLfrefHfref(Clock_Ip_CmuConfigType const* Config, uint32 Index);
#endif
static void Clock_Ip_DisableCmuFcFceRefCntLfrefHfref(Clock_Ip_NameType Name);
static void Clock_Ip_EnableCmuFcFceRefCntLfrefHfref(Clock_Ip_CmuConfigType const* Config);
#endif
#if (defined(CLOCK_IP_REGISTER_VALUES_OPTIMIZATION) && (CLOCK_IP_REGISTER_VALUES_OPTIMIZATION == STD_ON))
/* Set clock monitor via register value configuration */
static void Clock_Ip_SetClockMonitorRegisterValues(Clock_Ip_CmuConfigType const* Config, uint32 Index);
#endif





/* Clock stop section code */
#define MCU_STOP_SEC_CODE

#include "Mcu_MemMap.h"

/*==================================================================================================
*                                         LOCAL FUNCTIONS
==================================================================================================*/

/* Clock start section code */
#define MCU_START_SEC_CODE

#include "Mcu_MemMap.h"

static void Clock_Ip_ClockMonitorEmpty(Clock_Ip_CmuConfigType const* Config)
{
    (void)Config;
    /* No implementation */
}

static void Clock_Ip_ClockMonitorEmpty_Set( Clock_Ip_CmuConfigType const* Config,
                                            uint32 Index
                                           )
{
    (void)Config;
    (void)Index;
    /* No implementation */
}

static void Clock_Ip_ClockMonitorEmpty_Disable(Clock_Ip_NameType Name)
{
    (void)Name;
    /* No implementation */
}

#if (defined(CLOCK_IP_REGISTER_VALUES_OPTIMIZATION) && (CLOCK_IP_REGISTER_VALUES_OPTIMIZATION == STD_ON))
/* Set clock monitor via register value configuration */
static void Clock_Ip_SetClockMonitorRegisterValues( Clock_Ip_CmuConfigType const* Config,
                                                    uint32 Index
                                                   )
{
    (void)Index;

    if ((NULL_PTR != Config) && (Config->Enable != 0U))
    {
        Clock_Ip_WriteRegisterValues(&Config->Indexes);
    }
}

#endif

#ifdef CLOCK_IP_CMU_FC_FCE_REF_CNT_LFREF_HFREF
#define CLOCK_IP_CMU_FC_VAR                                     3U




















static void Clock_Ip_DisableCmuFcFceRefCntLfrefHfref(Clock_Ip_NameType Name)
{
    const Clock_Ip_CmuInfoType * CmuInformation = &Clock_Ip_axCmuInfo[Clock_Ip_au8ClockFeatures[Name][CLOCK_IP_CMU_INDEX]];
    Clock_Ip_ClockMonitorType* const CmuFc    = CmuInformation->CmuInstance;

    boolean TimeoutOccurred = FALSE;
    uint32 StartTime;
    uint32 ElapsedTime;
    uint32 TimeoutTicks;
    uint32 FrequencyCheckStatus;

    /* Enter critical region*/
    SchM_Enter_Mcu_MCU_EXCLUSIVE_AREA_01();
    /* Only disable frequency check if it is enabled */
    if (CLOCK_IP_CMU_FREQUENCY_CHECK_ENABLED == (CmuFc->GCR & CMU_FC_GCR_FCE_MASK))
    {
        Clock_Ip_StartTimeout(&StartTime, &ElapsedTime, &TimeoutTicks, CLOCK_IP_TIMEOUT_VALUE_US);
        /* Wait for frequency check to be running. */
        do
        {
            FrequencyCheckStatus = (CmuFc->SR & CMU_FC_SR_RS_MASK);
            TimeoutOccurred = Clock_Ip_TimeoutExpired(&StartTime, &ElapsedTime, TimeoutTicks);
        }
        while ((CLOCK_IP_CMU_FREQUENCY_CHECK_STOPPED == FrequencyCheckStatus) && (!TimeoutOccurred));

        /* timeout notification */
        if (TimeoutOccurred)
        {
            /* Report timeout error */
            Clock_Ip_ReportClockErrors(CLOCK_IP_REPORT_TIMEOUT_ERROR, Name);
        }
        else
        {
            /* Disable frequency check */
            CmuFc->GCR &= ~(uint8)(CMU_FC_GCR_FCE_MASK);

            /* Disable interupts */
            CmuFc->IER &= ~(CMU_FC_IER_FLLIE_MASK | CMU_FC_IER_FHHIE_MASK | CMU_FC_IER_FLLAIE_MASK | CMU_FC_IER_FHHAIE_MASK);

            /* Clear flags */
            CmuFc->SR |= (CMU_FC_SR_FLL_MASK | CMU_FC_SR_FHH_MASK);

        }
    }

    /* Exit critical region. */
    SchM_Exit_Mcu_MCU_EXCLUSIVE_AREA_01();
}

/* Reset CMU register */
static void Clock_Ip_ResetCmuFcFceRefCntLfrefHfref(Clock_Ip_CmuConfigType const* Config)
{
    if (NULL_PTR != Config)
    {
        Clock_Ip_DisableCmuFcFceRefCntLfrefHfref(Config->Name);
    }
}

#if !(defined(CLOCK_IP_REGISTER_VALUES_OPTIMIZATION) && (CLOCK_IP_REGISTER_VALUES_OPTIMIZATION == STD_ON))
static void Clock_Ip_SetCmuFcFceRefCntLfrefHfref(Clock_Ip_CmuConfigType const* Config, uint32 Index)
{
    uint32 Cmp1;
    uint32 Cmp2;
    uint32 RefCount;

    uint32 ReferenceClk = 0U;
    uint32 MonitoredClk = 0U;
    uint32 BusClk = 0U;

    uint64 HfRef = 0UL;
    uint64 LfRef = 0UL;

    const Clock_Ip_CmuInfoType * CmuInformation;

    if ((NULL_PTR != Config) && (Config->Enable != 0U))
    {
        CmuInformation = &Clock_Ip_axCmuInfo[Clock_Ip_au8ClockFeatures[Config->Name][CLOCK_IP_CMU_INDEX]];
        /* Do not calculate cmu values if these values are already calculated and written in hw registers */
        if (HashCmu[Index] != ((((uint32)Config->MonitoredClockFrequency)  ^ ((uint32)Config->Name))))
        {
            HashCmu[Index] = ((((uint32)Config->MonitoredClockFrequency)  ^ ((uint32)Config->Name)));

            ReferenceClk = (*Clock_Ip_pxConfig->ConfiguredFrequencies)[Clock_Ip_FreqIds[CmuInformation->Reference]].ConfiguredFrequencyValue;
            BusClk       = (*Clock_Ip_pxConfig->ConfiguredFrequencies)[Clock_Ip_FreqIds[CmuInformation->Bus]].ConfiguredFrequencyValue;
            MonitoredClk = Config->MonitoredClockFrequency;

            #if (defined(CLOCK_IP_DEV_ERROR_DETECT) && (CLOCK_IP_DEV_ERROR_DETECT == STD_ON))
                CLOCK_IP_DEV_ASSERT((ReferenceClk != 0U) && (ReferenceClk < 65536000) && (BusClk != 0U) && (MonitoredClk != 0U) );
            #endif

            /* Avoid divide by zero */
            if ((ReferenceClk != 0U) && (BusClk != 0U) && (MonitoredClk != 0U))
            {
                /* Cmp1 = ceiling of (3 * fRef/ fBus) */
                Cmp1 = 1U + (uint32)(((3U * ReferenceClk) - 1U) / BusClk);
                /* Cmp2 = ceiling of (8 + (5 * fRef / fMonitor)) */
                Cmp2 = 9U + (uint32)(((5U * ReferenceClk) - 1U) / MonitoredClk);

                switch(Config->Latency)
                {
                    case CLOCK_IP_LOW_LATENCY_1US:
                    case CLOCK_IP_MEDIUM_LATENCY_10US:
                    case CLOCK_IP_HIGH_LATENCY_100US:
                    case CLOCK_IP_VERY_HIGH_LATENCY_1MS:
                    {
                        /* Example: FXOSC frequency is 40MHz, then counter is 40 for 1us latency, 400 for 10us latency, 4000 for 100us latency and 40000 for 1ms latency. */
                        /* If reference clock frequency is 40 MHz, and monitor latency is 1us (counter = 40000000Hz/1000000), 10us (counter = 40000000Hz/100000), 100us (counter = 40000000Hz/10000), 1ms (counter = 40000000Hz/1000) */
                        RefCount = ReferenceClk / (1000000U / (uint32)(Config->Latency));
                        RefCount = (((Cmp1 > Cmp2) ? Cmp1 : Cmp2) > RefCount) ? ((Cmp1 > Cmp2) ? Cmp1 : Cmp2) : RefCount;
                    }
                    break;
                    case CLOCK_IP_LOWEST_LATENCY:
                    default:
                    {
                        /* REF count = Max(cmp1,cmp2) */
                        RefCount = (Cmp1 > Cmp2) ? Cmp1 : Cmp2;
                    }
                    break;
                }

                /* Formula for Hfref
                   ceil((fMonitoredClk mul MonitoredClockPlusVariation mul RefCount) div
                        (fReferenceClk mul ReferenceClockMinusVariation))
                         + CLOCK_IP_CMU_FC_VAR
                 */
                HfRef = (((((uint64)MonitoredClk) * ((uint64)(10000UL + (uint64)(Config->Variation))) * ((uint64)RefCount)) - 1UL)       \
                /                                                                                                             \
                (((uint64)ReferenceClk) * ((uint64)(10000UL - (uint64)(CmuInformation->FreqRefVariation))))) + 1UL                       \
                + CLOCK_IP_CMU_FC_VAR;

                /* Formula for LfRef
                   floor((fMonitoredClk mul MonitoredClockMinusVariation mul RefCount) div
                        (fReferenceClk mul ReferenceClockPlusVariation))
                         - CLOCK_IP_CMU_FC_VAR
                 */
                LfRef = ((((uint64)MonitoredClk) * ((uint64)(10000UL - (uint64)(Config->Variation))) * ((uint64)RefCount))               \
                /                                                                                                             \
                (((uint64)ReferenceClk) * ((uint64)(10000UL + (uint64)(CmuInformation->FreqRefVariation)))))                             \
                - CLOCK_IP_CMU_FC_VAR;

                /*******************************   Write values in registers *************************************************/

                /* Set reference counter */
                ((Clock_Ip_ClockMonitorType*)CmuInformation->CmuInstance)->RCCR = RefCount;

                /* Set high limit. Do not program HFREF to a value greater than 0x00FFFFFC */
                ((Clock_Ip_ClockMonitorType*)CmuInformation->CmuInstance)->HTCR = (((uint32)HfRef) > (uint32)0xFFFFFC)? (uint32)0xFFFFFC : ((uint32)HfRef);

                /* Set low limit. Do not program LFREF to a value less than 0x00000003 */
                ((Clock_Ip_ClockMonitorType*)CmuInformation->CmuInstance)->LTCR = (((uint32)LfRef) < (uint32)3U)? (uint32)3U : ((uint32)LfRef);
            }
        }

        /* Enable/disable interrupts */
        ((Clock_Ip_ClockMonitorType*)CmuInformation->CmuInstance)->IER = Config->Interrupt;
    }
    else
    {
        (void)Index;
        (void)Cmp1;
        (void)Cmp2;
        (void)RefCount;
        (void)ReferenceClk;
        (void)MonitoredClk;
        (void)BusClk;
        (void)HfRef;
        (void)LfRef;
    }
}
#endif

static void Clock_Ip_EnableCmuFcFceRefCntLfrefHfref(Clock_Ip_CmuConfigType const* Config)
{
    const Clock_Ip_CmuInfoType *CmuInformation;
    Clock_Ip_ClockMonitorType* CmuFc;

    if (NULL_PTR != Config)
    {
        CmuInformation  = &Clock_Ip_axCmuInfo[Clock_Ip_au8ClockFeatures[Config->Name][CLOCK_IP_CMU_INDEX]];
        CmuFc           = CmuInformation->CmuInstance;

        /* Enable cmu */
        if (Config->Enable != 0U)
        {
            CmuFc->GCR |= (uint8)(CMU_FC_GCR_FCE_MASK);
        }
        else
        {
            CmuFc->GCR &= ~(uint8)(CMU_FC_GCR_FCE_MASK);
        }
    }
    else
    {
        (void)CmuInformation;
        (void)CmuFc;
    }
}
#endif




/*==================================================================================================
*                                        GLOBAL FUNCTIONS
==================================================================================================*/


#ifdef CLOCK_IP_CMU_FC_FCE_REF_CNT_LFREF_HFREF
#if CLOCK_IP_CMU_INSTANCES_ARRAY_SIZE > 0U
/**
* @brief        This function clear the CMU interrupt flag from CMU module.
* @details      Called by RGM ISR routine when a user notification for CMU FCCU events is configured
*
* @return       void
*
* @implements Clock_Ip_CMU_ClockFailInt_Activity
*
*/
void Clock_Ip_CMU_ClockFailInt(void)
{
    uint32 CmuIerValue;
    uint32 CmuIsrValue = 0U;
    uint32 IndexCmu;

    for (IndexCmu = 0U; IndexCmu < CLOCK_IP_CMU_INSTANCES_ARRAY_SIZE; IndexCmu++)
    {
        if(NULL_PTR != Clock_Ip_apxCmu[IndexCmu])
        {
            /* Read flags */
            CmuIsrValue = Clock_Ip_apxCmu[IndexCmu]->SR & CLOCK_IP_CMU_ISR_MASK;
            /* Check whether driver is initialized */
            if (NULL_PTR != Clock_Ip_pxConfig){
                /* Read interrupt enable */
                CmuIerValue = Clock_Ip_apxCmu[IndexCmu]->IER & CLOCK_IP_CMU_ISR_MASK;
                /* Filter all interrupts that are not enabled from cmuIsrValue */
                CmuIsrValue = CmuIsrValue & CmuIerValue;

                /* If at least one interrupt has been triggered */
                if (CmuIsrValue != 0U)
                {
                    /* Clear status flag */
                    Clock_Ip_apxCmu[IndexCmu]->SR = CmuIsrValue;
#ifdef CLOCK_IP_CMU_FCCU_NOTIFICATION
                    CLOCK_IP_CMU_FCCU_NOTIFICATION(Clock_Ip_aeCmuNames[IndexCmu]);
#else
                    Clock_Ip_ReportClockErrors(CLOCK_IP_CMU_ERROR, Clock_Ip_aeCmuNames[IndexCmu]);
#endif
                }
            }
            else{
                /* Clear status flag */
                Clock_Ip_apxCmu[IndexCmu]->SR = CmuIsrValue;
            }
        }
    }
}
#endif
#endif


/* Clock stop section code */
#define MCU_STOP_SEC_CODE

#include "Mcu_MemMap.h"

/*==================================================================================================
*                                        GLOBAL CONSTANTS
==================================================================================================*/

/* Clock start constant section data */
#define MCU_START_SEC_CONST_UNSPECIFIED

#include "Mcu_MemMap.h"

const Clock_Ip_ClockMonitorCallbackType Clock_Ip_axCmuCallbacks[CLOCK_IP_CMU_CALLBACKS_COUNT] =
{
    {
        &Clock_Ip_ClockMonitorEmpty,                /* Reset */
        &Clock_Ip_ClockMonitorEmpty_Set,            /* Set */
        &Clock_Ip_ClockMonitorEmpty_Disable,        /* Disable */
        &Clock_Ip_ClockMonitorEmpty,                /* Enable */
    },
#ifdef CLOCK_IP_CMU_FC_FCE_REF_CNT_LFREF_HFREF
    {
        &Clock_Ip_ResetCmuFcFceRefCntLfrefHfref,          /* Reset */
#if (defined(CLOCK_IP_REGISTER_VALUES_OPTIMIZATION) && (CLOCK_IP_REGISTER_VALUES_OPTIMIZATION == STD_ON))
        &Clock_Ip_SetClockMonitorRegisterValues,          /* Set */
#else
        &Clock_Ip_SetCmuFcFceRefCntLfrefHfref,            /* Set */
#endif
        &Clock_Ip_DisableCmuFcFceRefCntLfrefHfref,        /* Disable */
        &Clock_Ip_EnableCmuFcFceRefCntLfrefHfref,         /* Enable */
    },
#endif


};

/* Clock stop constant section data */
#define MCU_STOP_SEC_CONST_UNSPECIFIED

#include "Mcu_MemMap.h"


#ifdef __cplusplus
}
#endif

/** @} */
