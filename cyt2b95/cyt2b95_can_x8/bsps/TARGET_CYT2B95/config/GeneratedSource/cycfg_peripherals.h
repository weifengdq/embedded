/*******************************************************************************
 * File Name: cycfg_peripherals.h
 *
 * Description:
 * Peripheral Hardware Block configuration
 * This file was automatically generated and should not be modified.
 * Configurator Backend 3.60.0
 * device-db 4.31.0.9165
 * mtb-pdl-cat1 3.18.0.43833
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

#if !defined(CYCFG_PERIPHERALS_H)
#define CYCFG_PERIPHERALS_H

#include "cycfg_notices.h"
#include "cy_canfd.h"
#include "cy_sysclk.h"
#include "cy_scb_uart.h"

#if defined (CY_USING_HAL)
#include "cyhal_hwmgr.h"
#include "cyhal.h"
#endif /* defined (CY_USING_HAL) */

#if defined (COMPONENT_MTB_HAL)
#include "mtb_hal.h"
#include "cycfg_clocks.h"
#include "mtb_hal_hw_types.h"
#endif /* defined (COMPONENT_MTB_HAL) */

#if defined (CY_USING_HAL_LITE)
#include "cyhal_hw_types.h"
#endif /* defined (CY_USING_HAL_LITE) */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#define canfd_0_chan_0_ENABLED 1U
#define canfd_0_chan_0_HW CANFD0
#define canfd_0_chan_0_CHANNEL CANFD0_CH0
#define canfd_0_chan_0_STD_ID_FILTER_ID_0 0
#define canfd_0_chan_0_EXT_ID_FILTER_ID_0 0
#define canfd_0_chan_0_DATA_0 0
#define canfd_0_chan_0_DATA_1 1
#define canfd_0_chan_0_DATA_2 2
#define canfd_0_chan_0_DATA_3 3
#define canfd_0_chan_0_DATA_4 4
#define canfd_0_chan_0_DATA_5 5
#define canfd_0_chan_0_DATA_6 6
#define canfd_0_chan_0_DATA_7 7
#define canfd_0_chan_0_DATA_8 8
#define canfd_0_chan_0_DATA_9 9
#define canfd_0_chan_0_DATA_10 10
#define canfd_0_chan_0_DATA_11 11
#define canfd_0_chan_0_DATA_12 12
#define canfd_0_chan_0_DATA_13 13
#define canfd_0_chan_0_DATA_14 14
#define canfd_0_chan_0_DATA_15 15
#define canfd_0_chan_0_IRQ_0 canfd_0_interrupts0_0_IRQn
#define canfd_0_chan_0_IRQ_1 canfd_0_interrupts1_0_IRQn
#define canfd_0_chan_0_CHANNEL_NUM 0U
#define scb_0_ENABLED 1U
#define scb_0_HW SCB0
#define scb_0_IRQ scb_0_interrupt_IRQn

extern void CAN_RxMsgCallback(bool rxFIFOMsg, uint8_t msgBufOrRxFIFONum, cy_stc_canfd_rx_buffer_t* basemsg);
extern const cy_stc_canfd_bitrate_t canfd_0_chan_0_nominalBitrateConfig;
extern const cy_stc_canfd_bitrate_t canfd_0_chan_0_dataBitrateConfig;
extern const cy_stc_canfd_transceiver_delay_compensation_t canfd_0_chan_0_tdcConfig;
extern const cy_stc_id_filter_t canfd_0_chan_0_stdIdFilter_0;
extern const cy_stc_id_filter_t canfd_0_chan_0_stdIdFilters[];
extern const cy_stc_canfd_sid_filter_config_t canfd_0_chan_0_sidFiltersConfig;
extern const cy_stc_canfd_f0_t canfd_0_chan_0_extIdFilterF0Config_0;
extern const cy_stc_canfd_f1_t canfd_0_chan_0_extIdFilterF1Config_0;
extern const cy_stc_extid_filter_t canfd_0_chan_0_extIdFilter_0;
extern const cy_stc_extid_filter_t canfd_0_chan_0_extIdFilters[];
extern const cy_stc_canfd_extid_filter_config_t canfd_0_chan_0_extIdFiltersConfig;
extern const cy_stc_canfd_global_filter_config_t canfd_0_chan_0_globalFilterConfig;
extern const cy_en_canfd_fifo_config_t canfd_0_chan_0_rxFifo0Config;
extern const cy_en_canfd_fifo_config_t canfd_0_chan_0_rxFifo1Config;
extern const cy_stc_canfd_config_t canfd_0_chan_0_config;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_0;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_1;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_2;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_3;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_4;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_5;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_6;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_7;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_8;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_9;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_10;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_11;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_12;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_13;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_14;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_15;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_16;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_17;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_18;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_19;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_20;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_21;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_22;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_23;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_24;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_25;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_26;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_27;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_28;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_29;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_30;
extern cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_31;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_0;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_1;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_2;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_3;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_4;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_5;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_6;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_7;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_8;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_9;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_10;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_11;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_12;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_13;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_14;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_15;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_16;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_17;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_18;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_19;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_20;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_21;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_22;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_23;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_24;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_25;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_26;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_27;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_28;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_29;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_30;
extern cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_31;
extern uint32_t canfd_0_chan_0_dataBuffer_0[];
extern uint32_t canfd_0_chan_0_dataBuffer_1[];
extern uint32_t canfd_0_chan_0_dataBuffer_2[];
extern uint32_t canfd_0_chan_0_dataBuffer_3[];
extern uint32_t canfd_0_chan_0_dataBuffer_4[];
extern uint32_t canfd_0_chan_0_dataBuffer_5[];
extern uint32_t canfd_0_chan_0_dataBuffer_6[];
extern uint32_t canfd_0_chan_0_dataBuffer_7[];
extern uint32_t canfd_0_chan_0_dataBuffer_8[];
extern uint32_t canfd_0_chan_0_dataBuffer_9[];
extern uint32_t canfd_0_chan_0_dataBuffer_10[];
extern uint32_t canfd_0_chan_0_dataBuffer_11[];
extern uint32_t canfd_0_chan_0_dataBuffer_12[];
extern uint32_t canfd_0_chan_0_dataBuffer_13[];
extern uint32_t canfd_0_chan_0_dataBuffer_14[];
extern uint32_t canfd_0_chan_0_dataBuffer_15[];
extern uint32_t canfd_0_chan_0_dataBuffer_16[];
extern uint32_t canfd_0_chan_0_dataBuffer_17[];
extern uint32_t canfd_0_chan_0_dataBuffer_18[];
extern uint32_t canfd_0_chan_0_dataBuffer_19[];
extern uint32_t canfd_0_chan_0_dataBuffer_20[];
extern uint32_t canfd_0_chan_0_dataBuffer_21[];
extern uint32_t canfd_0_chan_0_dataBuffer_22[];
extern uint32_t canfd_0_chan_0_dataBuffer_23[];
extern uint32_t canfd_0_chan_0_dataBuffer_24[];
extern uint32_t canfd_0_chan_0_dataBuffer_25[];
extern uint32_t canfd_0_chan_0_dataBuffer_26[];
extern uint32_t canfd_0_chan_0_dataBuffer_27[];
extern uint32_t canfd_0_chan_0_dataBuffer_28[];
extern uint32_t canfd_0_chan_0_dataBuffer_29[];
extern uint32_t canfd_0_chan_0_dataBuffer_30[];
extern uint32_t canfd_0_chan_0_dataBuffer_31[];
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_0;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_1;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_2;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_3;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_4;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_5;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_6;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_7;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_8;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_9;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_10;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_11;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_12;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_13;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_14;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_15;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_16;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_17;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_18;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_19;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_20;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_21;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_22;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_23;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_24;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_25;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_26;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_27;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_28;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_29;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_30;
extern cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_31;

#if defined (CY_USING_HAL)
extern const cyhal_resource_inst_t canfd_0_chan_0_obj;
#endif /* defined (CY_USING_HAL) */

extern const cy_stc_scb_uart_config_t scb_0_config;

#if defined (CY_USING_HAL) || defined(CY_USING_HAL_LITE)
extern const cyhal_resource_inst_t scb_0_obj;
#endif /* defined (CY_USING_HAL) || defined(CY_USING_HAL_LITE) */

#if defined(CY_USING_HAL_LITE) || defined (CY_USING_HAL)
extern const cyhal_clock_t scb_0_clock;
#endif /* defined(CY_USING_HAL_LITE) || defined (CY_USING_HAL) */

#if defined (CY_USING_HAL) || defined(CY_USING_HAL_LITE)
extern const cyhal_uart_configurator_t scb_0_hal_config;
#endif /* defined (CY_USING_HAL) || defined(CY_USING_HAL_LITE) */

#if defined (COMPONENT_MTB_HAL)
extern const mtb_hal_peri_div_t scb_0_clock_ref;
extern const mtb_hal_clock_t scb_0_hal_clock;
#endif /* defined (COMPONENT_MTB_HAL) */

#if defined (COMPONENT_MTB_HAL) && (MTB_HAL_DRIVER_AVAILABLE_UART)
extern const mtb_hal_uart_configurator_t scb_0_hal_config;
#endif /* defined (COMPONENT_MTB_HAL) && (MTB_HAL_DRIVER_AVAILABLE_UART) */

void init_cycfg_peripherals(void);
void reserve_cycfg_peripherals(void);

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* CYCFG_PERIPHERALS_H */
