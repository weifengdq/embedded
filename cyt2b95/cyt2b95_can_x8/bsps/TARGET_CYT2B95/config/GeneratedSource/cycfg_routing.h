/*******************************************************************************
 * File Name: cycfg_routing.h
 *
 * Description:
 * Establishes all necessary connections between hardware elements.
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

#if !defined(CYCFG_ROUTING_H)
#define CYCFG_ROUTING_H

#include "cycfg_notices.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#define ioss_0_port_0_pin_0_HSIOM P0_0_SCB0_UART_RX
#define ioss_0_port_0_pin_1_HSIOM P0_1_SCB0_UART_TX
#define ioss_0_port_0_pin_2_HSIOM P0_2_CANFD0_TTCAN_TX1
#define ioss_0_port_0_pin_3_HSIOM P0_3_CANFD0_TTCAN_RX1
#define ioss_0_port_2_pin_0_HSIOM P2_0_CANFD0_TTCAN_TX0
#define ioss_0_port_2_pin_1_HSIOM P2_1_CANFD0_TTCAN_RX0
#define ioss_0_port_3_pin_0_HSIOM P3_0_CANFD0_TTCAN_TX3
#define ioss_0_port_3_pin_1_HSIOM P3_1_CANFD0_TTCAN_RX3
#define ioss_0_port_6_pin_2_HSIOM P6_2_CANFD0_TTCAN_TX2
#define ioss_0_port_6_pin_3_HSIOM P6_3_CANFD0_TTCAN_RX2
#define ioss_0_port_14_pin_0_HSIOM P14_0_CANFD1_TTCAN_TX0
#define ioss_0_port_14_pin_1_HSIOM P14_1_CANFD1_TTCAN_RX0
#define ioss_0_port_17_pin_0_HSIOM P17_0_CANFD1_TTCAN_TX1
#define ioss_0_port_17_pin_1_HSIOM P17_1_CANFD1_TTCAN_RX1
#define ioss_0_port_18_pin_6_HSIOM P18_6_CANFD1_TTCAN_TX2
#define ioss_0_port_18_pin_7_HSIOM P18_7_CANFD1_TTCAN_RX2
#define ioss_0_port_19_pin_0_HSIOM P19_0_CANFD1_TTCAN_TX3
#define ioss_0_port_19_pin_1_HSIOM P19_1_CANFD1_TTCAN_RX3
#define ioss_0_port_21_pin_2_ANALOG P21_2_SRSS_ECO_IN
#define ioss_0_port_21_pin_3_ANALOG P21_3_SRSS_ECO_OUT
#define ioss_0_port_23_pin_4_HSIOM P23_4_CPUSS_SWJ_SWO_TDO
#define ioss_0_port_23_pin_5_HSIOM P23_5_CPUSS_SWJ_SWCLK_TCLK
#define ioss_0_port_23_pin_6_HSIOM P23_6_CPUSS_SWJ_SWDIO_TMS

static inline void init_cycfg_routing(void) {}

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* CYCFG_ROUTING_H */
