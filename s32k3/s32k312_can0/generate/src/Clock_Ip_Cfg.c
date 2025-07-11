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
*   used strictly in accordance with the applicable license terms. By expressly
*   accepting such terms or by downloading, installing, activating and/or otherwise
*   using the software, you are agreeing that you have read, and that you agree to
*   comply with and are bound by, such license terms. If you do not agree to be
*   bound by the applicable license terms, then you may not retain, install,
*   activate or otherwise use the software.
==================================================================================================*/

/**
*   @file       Clock_Ip_Cfg.c
*   @version    6.0.0
*
*   @brief   AUTOSAR Mcu - Post-Build(PB) configuration file code template.
*   @details Code template for Post-Build(PB) configuration file generation.
*
*   @addtogroup CLOCK_DRIVER_CONFIGURATION Clock Driver
*   @{
*/


#ifdef __cplusplus
extern "C"{
#endif


/*==================================================================================================
                                         INCLUDE FILES
 1) system and project includes
 2) needed interfaces from external units
 3) internal and external interfaces from this unit
==================================================================================================*/
#include "Clock_Ip_Cfg.h"
#include "Std_Types.h"
#include "Clock_Ip.h"
#include "Clock_Ip_Private.h"

/*==================================================================================================
*                              SOURCE FILE VERSION INFORMATION
==================================================================================================*/
#define CLOCK_IP_CFG_VENDOR_ID_C                      43
#define CLOCK_IP_CFG_AR_RELEASE_MAJOR_VERSION_C       4
#define CLOCK_IP_CFG_AR_RELEASE_MINOR_VERSION_C       7
#define CLOCK_IP_CFG_AR_RELEASE_REVISION_VERSION_C    0
#define CLOCK_IP_CFG_SW_MAJOR_VERSION_C               6
#define CLOCK_IP_CFG_SW_MINOR_VERSION_C               0
#define CLOCK_IP_CFG_SW_PATCH_VERSION_C               0

/*==================================================================================================
*                                     FILE VERSION CHECKS
==================================================================================================*/
/* Check if source file and Clock_Ip_Cfg.h file are of the same vendor */
#if (CLOCK_IP_CFG_VENDOR_ID_C != CLOCK_IP_CFG_VENDOR_ID)
    #error "Clock_Ip_Cfg.c and Clock_Ip_Cfg.h have different vendor ids"
#endif

/* Check if source file and Clock_Ip_Cfg.h file are of the same Autosar version */
#if ((CLOCK_IP_CFG_AR_RELEASE_MAJOR_VERSION_C != CLOCK_IP_CFG_AR_RELEASE_MAJOR_VERSION) || \
     (CLOCK_IP_CFG_AR_RELEASE_MINOR_VERSION_C != CLOCK_IP_CFG_AR_RELEASE_MINOR_VERSION) || \
     (CLOCK_IP_CFG_AR_RELEASE_REVISION_VERSION_C != CLOCK_IP_CFG_AR_RELEASE_REVISION_VERSION) \
    )
    #error "AutoSar Version Numbers of Clock_Ip_Cfg.c and Clock_Ip_Cfg.h are different"
#endif

/* Check if source file and Clock_Ip_Cfg.h file are of the same Software version */
#if ((CLOCK_IP_CFG_SW_MAJOR_VERSION_C != CLOCK_IP_CFG_SW_MAJOR_VERSION) || \
     (CLOCK_IP_CFG_SW_MINOR_VERSION_C != CLOCK_IP_CFG_SW_MINOR_VERSION) || \
     (CLOCK_IP_CFG_SW_PATCH_VERSION_C != CLOCK_IP_CFG_SW_PATCH_VERSION) \
    )
    #error "Software Version Numbers of Clock_Ip_Cfg.c and Clock_Ip_Cfg.h are different"
#endif

#ifndef DISABLE_MCAL_INTERMODULE_ASR_CHECK
/* Check if source file and Std_Types.h file are of the same Autosar version */
#if ((CLOCK_IP_CFG_AR_RELEASE_MAJOR_VERSION_C != STD_AR_RELEASE_MAJOR_VERSION) || \
     (CLOCK_IP_CFG_AR_RELEASE_MINOR_VERSION_C != STD_AR_RELEASE_MINOR_VERSION) \
    )
    #error "AutoSar Version Numbers of Clock_Ip_Cfg.c and Std_Types.h are different"
#endif
#endif    /* DISABLE_MCAL_INTERMODULE_ASR_CHECK */

/* Check if source file and Clock_Ip.h file are of the same vendor */
#if (CLOCK_IP_CFG_VENDOR_ID_C != CLOCK_IP_VENDOR_ID)
    #error "Clock_Ip_Cfg.c and Clock_Ip.h have different vendor ids"
#endif

/* Check if source file and Clock_Ip.h file are of the same Autosar version */
#if ((CLOCK_IP_CFG_AR_RELEASE_MAJOR_VERSION_C != CLOCK_IP_AR_RELEASE_MAJOR_VERSION) || \
     (CLOCK_IP_CFG_AR_RELEASE_MINOR_VERSION_C != CLOCK_IP_AR_RELEASE_MINOR_VERSION) || \
     (CLOCK_IP_CFG_AR_RELEASE_REVISION_VERSION_C != CLOCK_IP_AR_RELEASE_REVISION_VERSION) \
    )
    #error "AutoSar Version Numbers of Clock_Ip_Cfg.c and Clock_Ip.h are different"
#endif

/* Check if source file and Clock_Ip.h file are of the same Software version */
#if ((CLOCK_IP_CFG_SW_MAJOR_VERSION_C != CLOCK_IP_SW_MAJOR_VERSION) || \
     (CLOCK_IP_CFG_SW_MINOR_VERSION_C != CLOCK_IP_SW_MINOR_VERSION) || \
     (CLOCK_IP_CFG_SW_PATCH_VERSION_C != CLOCK_IP_SW_PATCH_VERSION) \
    )
    #error "Software Version Numbers of Clock_Ip_Cfg.c and Clock_Ip.h are different"
#endif

/* Check if source file and Clock_Ip_Private.h file are of the same vendor */
#if (CLOCK_IP_CFG_VENDOR_ID_C != CLOCK_IP_PRIVATE_VENDOR_ID)
    #error "Clock_Ip_Cfg.c and Clock_Ip_Private.h have different vendor ids"
#endif

/* Check if source file and Clock_Ip_Private.h file are of the same Autosar version */
#if ((CLOCK_IP_CFG_AR_RELEASE_MAJOR_VERSION_C != CLOCK_IP_PRIVATE_AR_RELEASE_MAJOR_VERSION) || \
     (CLOCK_IP_CFG_AR_RELEASE_MINOR_VERSION_C != CLOCK_IP_PRIVATE_AR_RELEASE_MINOR_VERSION) || \
     (CLOCK_IP_CFG_AR_RELEASE_REVISION_VERSION_C != CLOCK_IP_PRIVATE_AR_RELEASE_REVISION_VERSION) \
    )
    #error "AutoSar Version Numbers of Clock_Ip_Cfg.c and Clock_Ip_Private.h are different"
#endif

/* Check if source file and Clock_Ip_Private.h file are of the same Software version */
#if ((CLOCK_IP_CFG_SW_MAJOR_VERSION_C != CLOCK_IP_PRIVATE_SW_MAJOR_VERSION) || \
     (CLOCK_IP_CFG_SW_MINOR_VERSION_C != CLOCK_IP_PRIVATE_SW_MINOR_VERSION) || \
     (CLOCK_IP_CFG_SW_PATCH_VERSION_C != CLOCK_IP_PRIVATE_SW_PATCH_VERSION) \
    )
    #error "Software Version Numbers of Clock_Ip_Cfg.c and Clock_Ip_Private.h are different"
#endif

/*==================================================================================================
                          LOCAL TYPEDEFS (STRUCTURES, UNIONS, ENUMS)
==================================================================================================*/

/*==================================================================================================
                                        LOCAL MACROS
==================================================================================================*/

/*==================================================================================================
                                       LOCAL CONSTANTS
==================================================================================================*/

/*==================================================================================================
                                       LOCAL VARIABLES
==================================================================================================*/

/*==================================================================================================
                                       GLOBAL CONSTANTS
==================================================================================================*/

/*==================================================================================================
                                       GLOBAL VARIABLES
==================================================================================================*/


#define MCU_START_SEC_CONFIG_DATA_UNSPECIFIED
#include "Mcu_MemMap.h"



static const Clock_Ip_IrcoscConfigType Clock_Ip_IrcoscConfigurations_0[CLOCK_IP_CONFIGURED_IRCOSCS_0_NO] = {

    #if CLOCK_IP_CONFIGURED_IRCOSCS_0_NO > 0U
    {
        FIRC_CLK,                   /* name */
        1U,                     /* Always enabled. */
        0U,                     /* Enable regulator */
        CLOCK_IP_SUPPORTS_48MHZ_FREQUENCY,                     /* Ircosc range */
        0U,                     /* Ircosc enable in VLP mode */
        0U,                     /* Ircosc enable in STOP mode */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_IRCOSCS_0_NO > 1U
    {
        FIRC_STANDBY_CLK,                   /* name */
        0U,                     /* Disabled in standby mode. */
        0U,                     /* Enable regulator */
        0U,                     /* Ircosc range */
        0U,                     /* Ircosc enable in VLP mode */
        0U,                     /* Ircosc enable in STOP mode */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_IRCOSCS_0_NO > 2U
    {
        SIRC_STANDBY_CLK,                   /* name */
        0U,                     /* Disabled in standby mode. */
        0U,                     /* Enable regulator */
        0U,                     /* Ircosc range */
        0U,                     /* Ircosc enable in VLP mode */
        0U,                     /* Ircosc enable in STOP mode */
    },
    #endif
};


static const Clock_Ip_XoscConfigType Clock_Ip_XoscConfigurations_0[CLOCK_IP_CONFIGURED_XOSCS_0_NO] = {

    #if CLOCK_IP_CONFIGURED_XOSCS_0_NO > 0U
    {
        FXOSC_CLK,              /* name */
        16000000U,              /* frequency */ 
        1U,                     /* enable */
        49U,                   /* startupDelay */
        0U,                     /* bypassOption: Xosc use crystal */ 
        1U,                     /* Comparator is enabled */ 
        12U,                    /* TransConductance */
        0U,                     /* Gain value */
        0U,                     /* Monitor type */
        0U,                     /* Automatic level controller */
        0U,                     /* Level Shifter Current */
        0U,                     /* Comparator Current */
    },
    #endif
};


static const Clock_Ip_PllConfigType Clock_Ip_PllConfigurations_0[CLOCK_IP_CONFIGURED_PLLS_0_NO] = {

    #if CLOCK_IP_CONFIGURED_PLLS_0_NO > 0U
    {
        PLL_CLK,                /* name */
        1U,                     /* enable */
        FXOSC_CLK,                     /* inputReference */
        0U,                     /* Bypass */
        2U,                      /* predivider */
        0U,                      /* multiplier */
        0U,                      /* postdivider */
        0U,                     /* numeratorFracLoopDiv */
        120U,                   /* mulFactorDiv */
        1U,                     /* ModulationBypass */
        1U,                     /* Modulation type: Spread spectrum modulation bypassed */ 
        0U,                     /* modulationPeriod */
        0U,                     /* incrementStep */
        0U,                     /* sigmaDelta */
        0U,                     /* ditherControl */
        0U,                     /* ditherControlValue */
        0U,                     /* Monitor type */
        {
            0U,
            0U,
            0U,
        },
        0U,                     /* SoftwareDisable */
    },
    #endif
};


static const Clock_Ip_SelectorConfigType Clock_Ip_SelectorConfigurations_0[CLOCK_IP_CONFIGURED_SELECTORS_0_NO] = {

    #if CLOCK_IP_CONFIGURED_SELECTORS_0_NO > 0U
    {
        SCS_CLK,                    /* Clock name associated to selector */
        PLL_PHI0_CLK,                    /* Name of the selected input source */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_SELECTORS_0_NO > 1U
    {
        CLKOUT_RUN_CLK,                    /* Clock name associated to selector */
        FXOSC_CLK,                    /* Name of the selected input source */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_SELECTORS_0_NO > 2U
    {
        CLKOUT_STANDBY_CLK,                    /* Clock name associated to selector */
        FIRC_CLK,                    /* Name of the selected input source */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_SELECTORS_0_NO > 3U
    {
        FLEXCANA_CLK,                    /* Clock name associated to selector */
        AIPS_PLAT_CLK,                    /* Name of the selected input source */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_SELECTORS_0_NO > 4U
    {
        FLEXCANB_CLK,                    /* Clock name associated to selector */
        AIPS_PLAT_CLK,                    /* Name of the selected input source */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_SELECTORS_0_NO > 5U
    {
        RTC_CLK,                    /* Clock name associated to selector */
        FIRC_CLK,                    /* Name of the selected input source */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_SELECTORS_0_NO > 6U
    {
        STMA_CLK,                    /* Clock name associated to selector */
        FIRC_CLK,                    /* Name of the selected input source */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_SELECTORS_0_NO > 7U
    {
        TRACE_CLK,                    /* Clock name associated to selector */
        FIRC_CLK,                    /* Name of the selected input source */
    },
    #endif
};


static const Clock_Ip_DividerConfigType Clock_Ip_DividerConfigurations_0[CLOCK_IP_CONFIGURED_DIVIDERS_0_NO] = {

    #if CLOCK_IP_CONFIGURED_DIVIDERS_0_NO > 0U
    {
        PLL_POSTDIV_CLK,                    /* name */
        4U,                              /* value */
        {
            0U,
        }
    },
    #endif


    #if CLOCK_IP_CONFIGURED_DIVIDERS_0_NO > 1U
    {
        PLL_PHI0_CLK,                    /* name */
        2U,                              /* value */
        {
            0U,
        }
    },
    #endif

    #if CLOCK_IP_CONFIGURED_DIVIDERS_0_NO > 2U
    {
        PLL_PHI1_CLK,                    /* name */
        5U,                              /* value */
        {
            0U,
        }
    },
    #endif

    #if CLOCK_IP_CONFIGURED_DIVIDERS_0_NO > 3U
    {
        CORE_CLK,                    /* name */
        1U,                              /* value */
        {
            0U,
        }
    },
    #endif

    #if CLOCK_IP_CONFIGURED_DIVIDERS_0_NO > 4U
    {
        AIPS_PLAT_CLK,                    /* name */
        2U,                              /* value */
        {
            0U,
        }
    },
    #endif

    #if CLOCK_IP_CONFIGURED_DIVIDERS_0_NO > 5U
    {
        AIPS_SLOW_CLK,                    /* name */
        4U,                              /* value */
        {
            0U,
        }
    },
    #endif

    #if CLOCK_IP_CONFIGURED_DIVIDERS_0_NO > 6U
    {
        HSE_CLK,                    /* name */
        2U,                              /* value */
        {
            0U,
        }
    },
    #endif

    #if CLOCK_IP_CONFIGURED_DIVIDERS_0_NO > 7U
    {
        DCM_CLK,                    /* name */
        4U,                              /* value */
        {
            0U,
        }
    },
    #endif

    #if CLOCK_IP_CONFIGURED_DIVIDERS_0_NO > 8U
    {
        CLKOUT_RUN_CLK,                    /* name */
        2U,                              /* value */
        {
            0U,
        }
    },
    #endif

    #if CLOCK_IP_CONFIGURED_DIVIDERS_0_NO > 9U
    {
        CLKOUT_STANDBY_CLK,                    /* name */
        2U,                              /* value */
        {
            0U,
        }
    },
    #endif

    #if CLOCK_IP_CONFIGURED_DIVIDERS_0_NO > 10U
    {
        FLEXCANA_CLK,                    /* name */
        1U,                              /* value */
        {
            0U,
        }
    },
    #endif

    #if CLOCK_IP_CONFIGURED_DIVIDERS_0_NO > 11U
    {
        FLEXCANB_CLK,                    /* name */
        1U,                              /* value */
        {
            0U,
        }
    },
    #endif

    #if CLOCK_IP_CONFIGURED_DIVIDERS_0_NO > 12U
    {
        STMA_CLK,                    /* name */
        1U,                              /* value */
        {
            0U,
        }
    },
    #endif

    #if CLOCK_IP_CONFIGURED_DIVIDERS_0_NO > 13U
    {
        TRACE_CLK,                    /* name */
        1U,                              /* value */
        {
            0U,
        }
    },
    #endif
};


static const Clock_Ip_DividerTriggerConfigType Clock_Ip_DividerTriggerConfigurations_0[CLOCK_IP_CONFIGURED_DIVIDER_TRIGGERS_0_NO] = {

    #if CLOCK_IP_CONFIGURED_DIVIDER_TRIGGERS_0_NO > 0U
    {
        CORE_CLK,          /* divider name */
        COMMON_TRIGGER_DIVIDER_UPDATE,          /* trigger value */
        CORE_CLK,          /* input source name */
    },
    #endif
};


static const Clock_Ip_GateConfigType Clock_Ip_GatesConfigurations_0[CLOCK_IP_CONFIGURED_GATES_0_NO] = {

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 0U
    {
        ADC0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 1U
    {
        ADC1_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 2U
    {
        BCTU0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 3U
    {
        CMP0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 4U
    {
        CMP1_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 5U
    {
        CRC0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 6U
    {
        DMAMUX0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 7U
    {
        DMAMUX1_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 8U
    {
        EDMA0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 9U
    {
        EDMA0_TCD0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 10U
    {
        EDMA0_TCD1_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 11U
    {
        EDMA0_TCD2_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 12U
    {
        EDMA0_TCD3_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 13U
    {
        EDMA0_TCD4_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 14U
    {
        EDMA0_TCD5_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 15U
    {
        EDMA0_TCD6_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 16U
    {
        EDMA0_TCD7_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 17U
    {
        EDMA0_TCD8_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 18U
    {
        EDMA0_TCD9_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 19U
    {
        EDMA0_TCD10_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 20U
    {
        EDMA0_TCD11_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 21U
    {
        EIM_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 22U
    {
        EMIOS0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 23U
    {
        EMIOS1_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 24U
    {
        ERM0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 25U
    {
        FLEXCAN0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 26U
    {
        FLEXCAN1_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 27U
    {
        FLEXCAN2_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 28U
    {
        FLEXCAN3_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 29U
    {
        FLEXCAN4_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 30U
    {
        FLEXCAN5_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 31U
    {
        FLEXIO0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 32U
    {
        INTM_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 33U
    {
        LCU0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 34U
    {
        LCU1_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 35U
    {
        LPI2C0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 36U
    {
        LPI2C1_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 37U
    {
        LPSPI0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 38U
    {
        LPSPI1_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 39U
    {
        LPSPI2_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 40U
    {
        LPSPI3_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 41U
    {
        LPUART0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 42U
    {
        LPUART1_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 43U
    {
        LPUART2_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 44U
    {
        LPUART3_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 45U
    {
        LPUART4_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 46U
    {
        LPUART5_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 47U
    {
        LPUART6_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 48U
    {
        LPUART7_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 49U
    {
        MSCM_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 50U
    {
        PIT0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 51U
    {
        PIT1_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 52U
    {
        RTC0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 53U
    {
        SIUL2_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 54U
    {
        STCU0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 55U
    {
        STM0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 56U
    {
        SWT0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 57U
    {
        TEMPSENSE_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 58U
    {
        TRGMUX0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 59U
    {
        TSENSE0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif

    #if CLOCK_IP_CONFIGURED_GATES_0_NO > 60U
    {
        WKPU0_CLK,                    /* name */
        1U,                           /* enable */
    },
    #endif
};


static const Clock_Ip_CmuConfigType Clock_Ip_CmuConfigurations_0[CLOCK_IP_CONFIGURED_CMUS_0_NO] = {

    #if CLOCK_IP_CONFIGURED_CMUS_0_NO > 0U
    {
        FXOSC_CLK,            /* Clock name associated to clock monitor. */
        0U,                   /*Enable/disable clock monitor CMU_FC_0 */
        (                                           /* IER for CMU_FC_0 */
            CMU_FC_IER_FLLIE(0U) |
            CMU_FC_IER_FHHIE(0U) |
            CMU_FC_IER_FLLAIE(0U) |
            CMU_FC_IER_FHHAIE(0U)
        ),
        CLOCK_IP_MAXIMUM_5_PERCENTAGE_DEVIATION,            /* Monitor maximum allowed frequency variation. */
        CLOCK_IP_LOW_LATENCY_1US,            /* Latency of clock monitor reaction. */
        16000000U,
        {
                0U,          /* Start index in register values array */
                0U,          /* End index in register values array */
        },
    },
    #endif
    #if CLOCK_IP_CONFIGURED_CMUS_0_NO > 0U
    {
        CORE_CLK,            /* Clock name associated to clock monitor. */
        0U,                   /*Enable/disable clock monitor CMU_FC_3 */
        (                                           /* IER for CMU_FC_3 */
            CMU_FC_IER_FLLIE(0U) |
            CMU_FC_IER_FHHIE(0U) |
            CMU_FC_IER_FLLAIE(0U) |
            CMU_FC_IER_FHHAIE(0U)
        ),
        CLOCK_IP_MAXIMUM_5_PERCENTAGE_DEVIATION,            /* Monitor maximum allowed frequency variation. */
        CLOCK_IP_LOW_LATENCY_1US,            /* Latency of clock monitor reaction. */
        120000000U,
        {
                0U,          /* Start index in register values array */
                0U,          /* End index in register values array */
        },
    },
    #endif
    #if CLOCK_IP_CONFIGURED_CMUS_0_NO > 0U
    {
        AIPS_PLAT_CLK,            /* Clock name associated to clock monitor. */
        0U,                   /*Enable/disable clock monitor CMU_FC_4 */
        (                                           /* IER for CMU_FC_4 */
            CMU_FC_IER_FLLIE(0U) |
            CMU_FC_IER_FHHIE(0U) |
            CMU_FC_IER_FLLAIE(0U) |
            CMU_FC_IER_FHHAIE(0U)
        ),
        CLOCK_IP_MAXIMUM_5_PERCENTAGE_DEVIATION,            /* Monitor maximum allowed frequency variation. */
        CLOCK_IP_LOW_LATENCY_1US,            /* Latency of clock monitor reaction. */
        60000000U,
        {
                0U,          /* Start index in register values array */
                0U,          /* End index in register values array */
        },
    },
    #endif
};


static const Clock_Ip_ConfiguredFrequencyType Clock_Ip_ConfiguredFrequencyConfigurations_0[CLOCK_IP_CONFIGURED_FREQUENCIES_COUNT] = {

    {
        CLOCK_IS_OFF,
        0U,
    },
    #if CLOCK_IP_CONFIGURED_FREQUENCIES_COUNT > 1U
    {
        FIRC_CLK,
        48000000U,
    },
    #endif
    #if CLOCK_IP_CONFIGURED_FREQUENCIES_COUNT > 2U
    {
        FXOSC_CLK,
        16000000U,
    },
    #endif
    #if CLOCK_IP_CONFIGURED_FREQUENCIES_COUNT > 3U
    {
        CORE_CLK,
        120000000U,
    },
    #endif
    #if CLOCK_IP_CONFIGURED_FREQUENCIES_COUNT > 4U
    {
        AIPS_PLAT_CLK,
        60000000U,
    },
    #endif
    #if CLOCK_IP_CONFIGURED_FREQUENCIES_COUNT > 5U
    {
        AIPS_SLOW_CLK,
        30000000U,
    },
    #endif
};


/* *************************************************************************
 * Configuration structure for Clock Configuration 
 * ************************************************************************* */
const Clock_Ip_ClockConfigType Clock_Ip_aClockConfig[1U] = {

    /*! @brief User Configuration structure clock_Cfg_0 */

    {
        0U,                          /* clkConfigId */
        0U,               /* Clock Partition ID */
        (NULL_PTR),               /* Register data if register value optimization is enabled */

        3U,                       /* ircoscsCount */
        1U,                       /* xoscsCount */
        1U,                       /* pllsCount */
        8U,                       /* selectorsCount */
        14U,                       /* dividersCount */
        1U,                       /* dividerTriggersCount */
        0U,                       /* fracDivsCount */
        0U,                       /* extClksCount */
        61U,                       /* gatesCount */
        0U,                       /* pcfsCount */
        3U,                       /* cmusCount */
        6U,                       /* configureFrequenciesCount */

        (&Clock_Ip_IrcoscConfigurations_0),                    /* Ircosc configurations */
        (&Clock_Ip_XoscConfigurations_0),                      /* Xosc configurations */
        (&Clock_Ip_PllConfigurations_0),                       /* Pll configurations */
        (&Clock_Ip_SelectorConfigurations_0),                  /* Selectors configurations */
        (&Clock_Ip_DividerConfigurations_0),                   /* dividers configurations */
        (&Clock_Ip_DividerTriggerConfigurations_0),            /* dividerTriggers configurations */
        (NULL_PTR),                       /* fracDivs configurations */
        (NULL_PTR),                       /* extClks configurations */
        (&Clock_Ip_GatesConfigurations_0),                     /* gates configurations */
        (NULL_PTR),                       /* pcfs configurations */
        (&Clock_Ip_CmuConfigurations_0),                       /* cmus configurations */
        (&Clock_Ip_ConfiguredFrequencyConfigurations_0),       /* configureFrequencies configurations */
    },
};


#define MCU_STOP_SEC_CONFIG_DATA_UNSPECIFIED
#include "Mcu_MemMap.h"


/*==================================================================================================
                                   LOCAL FUNCTION PROTOTYPES
==================================================================================================*/

/*==================================================================================================
                                       LOCAL FUNCTIONS
==================================================================================================*/

/*==================================================================================================
                                       GLOBAL FUNCTIONS
==================================================================================================*/

#ifdef __cplusplus
}
#endif

/** @} */


