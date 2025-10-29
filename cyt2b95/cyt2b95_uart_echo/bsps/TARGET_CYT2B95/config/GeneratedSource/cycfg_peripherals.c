/*******************************************************************************
 * File Name: cycfg_peripherals.c
 *
 * Description:
 * Peripheral Hardware Block configuration
 * This file was automatically generated and should not be modified.
 * Configurator Backend 3.60.0
 * device-db 4.29.0.9102
 * mtb-pdl-cat1 3.17.0.43048
 *
 *******************************************************************************
 * Copyright 2025 Cypress Semiconductor Corporation (an Infineon company) or
 * an affiliate of Cypress Semiconductor Corporation.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ******************************************************************************/

#include "cycfg_peripherals.h"

const cy_stc_scb_uart_config_t scb_0_config =
{
    .uartMode = CY_SCB_UART_STANDARD,
    .enableMutliProcessorMode = false,
    .smartCardRetryOnNack = false,
    .irdaInvertRx = false,
    .irdaEnableLowPowerReceiver = false,
    .oversample = 8,
    .enableMsbFirst = false,
    .dataWidth = 8UL,
    .parity = CY_SCB_UART_PARITY_NONE,
    .stopBits = CY_SCB_UART_STOP_BITS_1,
    .enableInputFilter = false,
    .breakWidth = 11UL,
    .dropOnFrameError = false,
    .dropOnParityError = false,
    .breaklevel = false,
    .receiverAddress = 0x0UL,
    .receiverAddressMask = 0x0UL,
    .acceptAddrInFifo = false,
    .enableCts = false,
    .ctsPolarity = CY_SCB_UART_ACTIVE_LOW,
    .rtsRxFifoLevel = 0UL,
    .rtsPolarity = CY_SCB_UART_ACTIVE_LOW,
    .rxFifoTriggerLevel = 0UL,
    .rxFifoIntEnableMask = 0UL,
    .txFifoTriggerLevel = 0UL,
    .txFifoIntEnableMask = 0UL,
};

#if defined (CY_USING_HAL) || defined(CY_USING_HAL_LITE)
const cyhal_resource_inst_t scb_0_obj =
{
    .type = CYHAL_RSC_SCB,
    .block_num = 0U,
    .channel_num = 0U,
};
#endif /* defined (CY_USING_HAL) || defined(CY_USING_HAL_LITE) */

#if defined(CY_USING_HAL_LITE) || defined (CY_USING_HAL)
const cyhal_clock_t scb_0_clock =
{
    .block = CYHAL_CLOCK_BLOCK_PERIPHERAL_24_5BIT,
    .channel = 0,
#if defined (CY_USING_HAL)
    .reserved = false,
    .funcs = NULL,
#endif /* defined (CY_USING_HAL) */
};
#endif /* defined(CY_USING_HAL_LITE) || defined (CY_USING_HAL) */

#if defined (CY_USING_HAL) || defined(CY_USING_HAL_LITE)
const cyhal_uart_configurator_t scb_0_hal_config =
{
    .resource = &scb_0_obj,
    .config = &scb_0_config,
    .clock = &scb_0_clock,
#if defined (CY_USING_HAL)
    .gpios = {.pin_tx = P0_1, .pin_rts = NC, .pin_cts = NC},
#endif /* defined (CY_USING_HAL) */
};
#endif /* defined (CY_USING_HAL) || defined(CY_USING_HAL_LITE) */

#if defined (COMPONENT_MTB_HAL)
const mtb_hal_peri_div_t scb_0_clock_ref =
{
    .clk_dst = (en_clk_dst_t)peri_0_div_24_5_0_GRP_NUM,
    .div_type = peri_0_div_24_5_0_HW,
    .div_num = peri_0_div_24_5_0_NUM,
};
const mtb_hal_clock_t scb_0_hal_clock =
{
    .clock_ref = &scb_0_clock_ref,
    .interface = &mtb_hal_clock_peri_interface,
};
#endif /* defined (COMPONENT_MTB_HAL) */

#if defined (COMPONENT_MTB_HAL) && (MTB_HAL_DRIVER_AVAILABLE_UART)
const mtb_hal_uart_configurator_t scb_0_hal_config =
{
    .base = scb_0_HW,
    .clock = &scb_0_hal_clock,
#if defined (COMPONENT_MW_ASYNC_TRANSFER)
    .rts_enable = 0UL,
#endif /* defined (COMPONENT_MW_ASYNC_TRANSFER) */
};
#endif /* defined (COMPONENT_MTB_HAL) && (MTB_HAL_DRIVER_AVAILABLE_UART) */

void init_cycfg_peripherals(void)
{
    Cy_SysClk_PeriPclkAssignDivider(PCLK_SCB0_CLOCK, CY_SYSCLK_DIV_24_5_BIT, 0U);
}
void reserve_cycfg_peripherals(void)
{
#if defined (CY_USING_HAL)
    cyhal_hwmgr_reserve(&scb_0_obj);
#endif /* defined (CY_USING_HAL) */
}
