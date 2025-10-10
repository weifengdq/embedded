/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the CE234615 - pdl-systick-led
*              for ModusToolbox.
*
* Related Document: See README.md
*
*
*******************************************************************************
* Copyright 2022, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

#include "cy_pdl.h"
#include "cybsp.h"

/*******************************************************************************
* Macros
********************************************************************************/
#define SYSTICK_RELOAD_VAL   (10000000UL)
#define SYSTICK_PERIOD_TIMES (10u)

/*******************************************************************************
* Function Prototypes
********************************************************************************/
static void toggle_led_on_systick_handler(void);

/*******************************************************************************
* Global Variables
********************************************************************************/
uint32_t counterTick = 0;

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This main achieve the systick timer interrupt function. Toggle user led when generate
* the systick interrupt up to 10 times.
*
* Return: int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* Initialize the device and board peripherals */
    result = cybsp_init() ;
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    __enable_irq();

    /*Initialize the User LED*/
    Cy_GPIO_Pin_FastInit(P5_0_PORT, P5_0_NUM, CY_GPIO_DM_STRONG, 1u, P5_0_GPIO);

    /*Initialize the systick*/
    Cy_SysTick_Init(CY_SYSTICK_CLOCK_SOURCE_CLK_CPU, SYSTICK_RELOAD_VAL);

    /*Set Systick interrupt callback*/
    Cy_SysTick_SetCallback(0, toggle_led_on_systick_handler);

    /*Enable Systick and the Systick interrupt*/
    Cy_SysTick_Enable();


    for (;;)
    {
        /*toggle user led*/
        if(counterTick > SYSTICK_PERIOD_TIMES)
        {
            counterTick = 0;
            Cy_GPIO_Inv(P5_0_PORT, P5_0_NUM);
        }
    }
}

/*******************************************************************************
* Function Name: toggle_led_on_systick_handler
********************************************************************************
*
*  Summary:
*  Systick interrupt handler
*
*  Parameters:
*  None
*
*  Return:
*  None
*
**********************************************************************************/
void toggle_led_on_systick_handler(void)
{
    counterTick++;
}

/* [] END OF FILE */
