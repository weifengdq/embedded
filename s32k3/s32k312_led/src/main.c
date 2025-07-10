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
*   @file main.c
*
*   @addtogroup main_module main module documentation
*   @{
*/

/* Including necessary configuration files. */
#include "Mcal.h"

volatile int exit_code = 0;
/* User includes */
#include "Siul2_Dio_Ip.h"
#include "Siul2_Port_Ip.h"

/*!
  \brief The main function for the project.
  \details The startup initialization sequence is the following:
 * - startup asm routine
 * - main()
*/
int main(void)
{
    /* Write your code here */
	Siul2_Port_Ip_Init(
	      NUM_OF_CONFIGURED_PINS_PortContainer_0_BOARD_InitPeripherals,
	      g_pin_mux_InitConfigArr_PortContainer_0_BOARD_InitPeripherals);

    for(;;)
    {
    	Siul2_Dio_Ip_WritePin(LED1_PORT, LED1_PIN, 0);
    	for(volatile int i = 0; i < 1000000; i++);
    	Siul2_Dio_Ip_WritePin(LED1_PORT, LED1_PIN, 1);
    	for(volatile int i = 0; i < 1000000; i++);

        if(exit_code != 0)
        {
            break;
        }
    }
    return exit_code;
}

/** @} */
