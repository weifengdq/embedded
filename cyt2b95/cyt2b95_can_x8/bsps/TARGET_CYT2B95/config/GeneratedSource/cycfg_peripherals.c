/*******************************************************************************
 * File Name: cycfg_peripherals.c
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

#include "cycfg_peripherals.h"

#define canfd_0_chan_0_STD_ID_FILTER_0 \
{\
    .sfid2 = 0U, \
    .sfid1 = 0U, \
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0, \
    .sft = CY_CANFD_SFT_CLASSIC_FILTER, \
 }
#define canfd_0_chan_0_EXT_ID_FILTER_0 \
{\
    .f0_f = &canfd_0_chan_0_extIdFilterF0Config_0, \
    .f1_f = &canfd_0_chan_0_extIdFilterF1Config_0, \
 }
#define canfd_0_chan_1_STD_ID_FILTER_0 \
{\
    .sfid2 = 0U, \
    .sfid1 = 0U, \
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0, \
    .sft = CY_CANFD_SFT_CLASSIC_FILTER, \
 }
#define canfd_0_chan_1_EXT_ID_FILTER_0 \
{\
    .f0_f = &canfd_0_chan_1_extIdFilterF0Config_0, \
    .f1_f = &canfd_0_chan_1_extIdFilterF1Config_0, \
 }
#define canfd_0_chan_2_STD_ID_FILTER_0 \
{\
    .sfid2 = 0U, \
    .sfid1 = 0U, \
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0, \
    .sft = CY_CANFD_SFT_CLASSIC_FILTER, \
 }
#define canfd_0_chan_2_EXT_ID_FILTER_0 \
{\
    .f0_f = &canfd_0_chan_2_extIdFilterF0Config_0, \
    .f1_f = &canfd_0_chan_2_extIdFilterF1Config_0, \
 }
#define canfd_0_chan_3_STD_ID_FILTER_0 \
{\
    .sfid2 = 0U, \
    .sfid1 = 0U, \
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0, \
    .sft = CY_CANFD_SFT_CLASSIC_FILTER, \
 }
#define canfd_0_chan_3_EXT_ID_FILTER_0 \
{\
    .f0_f = &canfd_0_chan_3_extIdFilterF0Config_0, \
    .f1_f = &canfd_0_chan_3_extIdFilterF1Config_0, \
 }
#define canfd_1_chan_0_STD_ID_FILTER_0 \
{\
    .sfid2 = 0U, \
    .sfid1 = 0U, \
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0, \
    .sft = CY_CANFD_SFT_CLASSIC_FILTER, \
 }
#define canfd_1_chan_0_EXT_ID_FILTER_0 \
{\
    .f0_f = &canfd_1_chan_0_extIdFilterF0Config_0, \
    .f1_f = &canfd_1_chan_0_extIdFilterF1Config_0, \
 }
#define canfd_1_chan_1_STD_ID_FILTER_0 \
{\
    .sfid2 = 0U, \
    .sfid1 = 0U, \
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0, \
    .sft = CY_CANFD_SFT_CLASSIC_FILTER, \
 }
#define canfd_1_chan_1_EXT_ID_FILTER_0 \
{\
    .f0_f = &canfd_1_chan_1_extIdFilterF0Config_0, \
    .f1_f = &canfd_1_chan_1_extIdFilterF1Config_0, \
 }
#define canfd_1_chan_2_STD_ID_FILTER_0 \
{\
    .sfid2 = 0U, \
    .sfid1 = 0U, \
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0, \
    .sft = CY_CANFD_SFT_CLASSIC_FILTER, \
 }
#define canfd_1_chan_2_EXT_ID_FILTER_0 \
{\
    .f0_f = &canfd_1_chan_2_extIdFilterF0Config_0, \
    .f1_f = &canfd_1_chan_2_extIdFilterF1Config_0, \
 }
#define canfd_1_chan_3_STD_ID_FILTER_0 \
{\
    .sfid2 = 0U, \
    .sfid1 = 0U, \
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0, \
    .sft = CY_CANFD_SFT_CLASSIC_FILTER, \
 }
#define canfd_1_chan_3_EXT_ID_FILTER_0 \
{\
    .f0_f = &canfd_1_chan_3_extIdFilterF0Config_0, \
    .f1_f = &canfd_1_chan_3_extIdFilterF1Config_0, \
 }

const cy_stc_canfd_bitrate_t canfd_0_chan_0_nominalBitrateConfig =
{
    .prescaler = 4U - 1U,
    .timeSegment1 = 15U - 1U,
    .timeSegment2 = 4U - 1U,
    .syncJumpWidth = 4U - 1U,
};
const cy_stc_canfd_bitrate_t canfd_0_chan_0_dataBitrateConfig =
{
    .prescaler = 2U - 1U,
    .timeSegment1 = 5U - 1U,
    .timeSegment2 = 2U - 1U,
    .syncJumpWidth = 2U - 1U,
};
const cy_stc_canfd_transceiver_delay_compensation_t canfd_0_chan_0_tdcConfig =
{
    .tdcEnabled = true,
    .tdcOffset = 6U,
    .tdcFilterWindow = 6U,
};
const cy_stc_id_filter_t canfd_0_chan_0_stdIdFilter_0 =
{
    .sfid2 = 0U,
    .sfid1 = 0U,
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0,
    .sft = CY_CANFD_SFT_CLASSIC_FILTER,
};
const cy_stc_id_filter_t canfd_0_chan_0_stdIdFilters[] =
{
    canfd_0_chan_0_STD_ID_FILTER_0, 
};
const cy_stc_canfd_sid_filter_config_t canfd_0_chan_0_sidFiltersConfig =
{
    .numberOfSIDFilters = 1U,
    .sidFilter = canfd_0_chan_0_stdIdFilters,
};
const cy_stc_canfd_f0_t canfd_0_chan_0_extIdFilterF0Config_0 =
{
    .efid1 = 0U,
    .efec = CY_CANFD_EFEC_STORE_RX_FIFO_0,
};
const cy_stc_canfd_f1_t canfd_0_chan_0_extIdFilterF1Config_0 =
{
    .efid2 = 0U,
    .eft = CY_CANFD_EFT_CLASSIC_FILTER,
};
const cy_stc_extid_filter_t canfd_0_chan_0_extIdFilter_0 =
{
    .f0_f = &canfd_0_chan_0_extIdFilterF0Config_0,
    .f1_f = &canfd_0_chan_0_extIdFilterF1Config_0,
};
const cy_stc_extid_filter_t canfd_0_chan_0_extIdFilters[] =
{
    canfd_0_chan_0_EXT_ID_FILTER_0, 
};
const cy_stc_canfd_extid_filter_config_t canfd_0_chan_0_extIdFiltersConfig =
{
    .numberOfEXTIDFilters = 1U,
    .extidFilter = (cy_stc_extid_filter_t*)&canfd_0_chan_0_extIdFilters,
    .extIDANDMask = 536870911UL,
};
const cy_stc_canfd_global_filter_config_t canfd_0_chan_0_globalFilterConfig =
{
    .nonMatchingFramesStandard = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .nonMatchingFramesExtended = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .rejectRemoteFramesStandard = false,
    .rejectRemoteFramesExtended = false,
};
const cy_en_canfd_fifo_config_t canfd_0_chan_0_rxFifo0Config =
{
    .mode = CY_CANFD_FIFO_MODE_OVERWRITE,
    .watermark = 0U,
    .numberOfFIFOElements = 16U,
    .topPointerLogicEnabled = true,
};
const cy_en_canfd_fifo_config_t canfd_0_chan_0_rxFifo1Config =
{
    .mode = CY_CANFD_FIFO_MODE_BLOCKING,
    .watermark = 0U,
    .numberOfFIFOElements = 0U,
    .topPointerLogicEnabled = false,
};
const cy_stc_canfd_config_t canfd_0_chan_0_config =
{
    .txCallback = NULL,
    .rxCallback = NULL,
    .errorCallback = NULL,
    .canFDMode = true,
    .bitrate = &canfd_0_chan_0_nominalBitrateConfig,
    .fastBitrate = &canfd_0_chan_0_dataBitrateConfig,
    .tdcConfig = &canfd_0_chan_0_tdcConfig,
    .sidFilterConfig = &canfd_0_chan_0_sidFiltersConfig,
    .extidFilterConfig = &canfd_0_chan_0_extIdFiltersConfig,
    .globalFilterConfig = &canfd_0_chan_0_globalFilterConfig,
    .rxBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO1DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .txBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0Config = &canfd_0_chan_0_rxFifo0Config,
    .rxFIFO1Config = &canfd_0_chan_0_rxFifo1Config,
    .noOfRxBuffers = 0U,
    .noOfTxBuffers = 32U,
    .messageRAMaddress = CY_CAN0MRAM_BASE + 0U,
    .messageRAMsize = 4096U,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_0 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_1 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_2 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_3 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_4 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_5 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_6 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_7 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_8 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_9 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_10 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_11 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_12 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_13 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_14 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_15 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_16 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_17 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_18 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_19 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_20 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_21 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_22 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_23 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_24 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_25 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_26 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_27 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_28 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_29 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_30 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_0_T0RegisterBuffer_31 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_0 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_1 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_2 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_3 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_4 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_5 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_6 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_7 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_8 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_9 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_10 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_11 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_12 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_13 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_14 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_15 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_16 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_17 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_18 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_19 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_20 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_21 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_22 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_23 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_24 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_25 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_26 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_27 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_28 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_29 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_30 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_0_T1RegisterBuffer_31 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
uint32_t canfd_0_chan_0_dataBuffer_0[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_1[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_2[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_3[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_4[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_5[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_6[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_7[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_8[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_9[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_10[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_11[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_12[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_13[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_14[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_15[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_16[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_17[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_18[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_19[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_20[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_21[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_22[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_23[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_24[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_25[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_26[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_27[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_28[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_29[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_30[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_0_dataBuffer_31[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_0 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_0,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_0,
    .data_area_f = canfd_0_chan_0_dataBuffer_0,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_1 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_1,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_1,
    .data_area_f = canfd_0_chan_0_dataBuffer_1,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_2 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_2,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_2,
    .data_area_f = canfd_0_chan_0_dataBuffer_2,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_3 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_3,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_3,
    .data_area_f = canfd_0_chan_0_dataBuffer_3,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_4 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_4,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_4,
    .data_area_f = canfd_0_chan_0_dataBuffer_4,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_5 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_5,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_5,
    .data_area_f = canfd_0_chan_0_dataBuffer_5,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_6 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_6,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_6,
    .data_area_f = canfd_0_chan_0_dataBuffer_6,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_7 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_7,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_7,
    .data_area_f = canfd_0_chan_0_dataBuffer_7,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_8 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_8,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_8,
    .data_area_f = canfd_0_chan_0_dataBuffer_8,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_9 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_9,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_9,
    .data_area_f = canfd_0_chan_0_dataBuffer_9,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_10 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_10,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_10,
    .data_area_f = canfd_0_chan_0_dataBuffer_10,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_11 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_11,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_11,
    .data_area_f = canfd_0_chan_0_dataBuffer_11,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_12 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_12,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_12,
    .data_area_f = canfd_0_chan_0_dataBuffer_12,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_13 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_13,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_13,
    .data_area_f = canfd_0_chan_0_dataBuffer_13,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_14 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_14,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_14,
    .data_area_f = canfd_0_chan_0_dataBuffer_14,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_15 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_15,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_15,
    .data_area_f = canfd_0_chan_0_dataBuffer_15,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_16 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_16,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_16,
    .data_area_f = canfd_0_chan_0_dataBuffer_16,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_17 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_17,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_17,
    .data_area_f = canfd_0_chan_0_dataBuffer_17,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_18 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_18,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_18,
    .data_area_f = canfd_0_chan_0_dataBuffer_18,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_19 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_19,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_19,
    .data_area_f = canfd_0_chan_0_dataBuffer_19,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_20 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_20,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_20,
    .data_area_f = canfd_0_chan_0_dataBuffer_20,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_21 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_21,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_21,
    .data_area_f = canfd_0_chan_0_dataBuffer_21,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_22 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_22,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_22,
    .data_area_f = canfd_0_chan_0_dataBuffer_22,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_23 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_23,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_23,
    .data_area_f = canfd_0_chan_0_dataBuffer_23,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_24 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_24,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_24,
    .data_area_f = canfd_0_chan_0_dataBuffer_24,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_25 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_25,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_25,
    .data_area_f = canfd_0_chan_0_dataBuffer_25,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_26 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_26,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_26,
    .data_area_f = canfd_0_chan_0_dataBuffer_26,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_27 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_27,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_27,
    .data_area_f = canfd_0_chan_0_dataBuffer_27,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_28 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_28,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_28,
    .data_area_f = canfd_0_chan_0_dataBuffer_28,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_29 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_29,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_29,
    .data_area_f = canfd_0_chan_0_dataBuffer_29,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_30 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_30,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_30,
    .data_area_f = canfd_0_chan_0_dataBuffer_30,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_0_txBuffer_31 =
{
    .t0_f = &canfd_0_chan_0_T0RegisterBuffer_31,
    .t1_f = &canfd_0_chan_0_T1RegisterBuffer_31,
    .data_area_f = canfd_0_chan_0_dataBuffer_31,
};

#if defined (CY_USING_HAL)
const cyhal_resource_inst_t canfd_0_chan_0_obj =
{
    .type = CYHAL_RSC_CAN,
    .block_num = 0U,
    .channel_num = 0U,
};
#endif /* defined (CY_USING_HAL) */

const cy_stc_canfd_bitrate_t canfd_0_chan_1_nominalBitrateConfig =
{
    .prescaler = 4U - 1U,
    .timeSegment1 = 15U - 1U,
    .timeSegment2 = 4U - 1U,
    .syncJumpWidth = 4U - 1U,
};
const cy_stc_canfd_bitrate_t canfd_0_chan_1_dataBitrateConfig =
{
    .prescaler = 2U - 1U,
    .timeSegment1 = 5U - 1U,
    .timeSegment2 = 2U - 1U,
    .syncJumpWidth = 2U - 1U,
};
const cy_stc_canfd_transceiver_delay_compensation_t canfd_0_chan_1_tdcConfig =
{
    .tdcEnabled = true,
    .tdcOffset = 6U,
    .tdcFilterWindow = 6U,
};
const cy_stc_id_filter_t canfd_0_chan_1_stdIdFilter_0 =
{
    .sfid2 = 0U,
    .sfid1 = 0U,
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0,
    .sft = CY_CANFD_SFT_CLASSIC_FILTER,
};
const cy_stc_id_filter_t canfd_0_chan_1_stdIdFilters[] =
{
    canfd_0_chan_1_STD_ID_FILTER_0, 
};
const cy_stc_canfd_sid_filter_config_t canfd_0_chan_1_sidFiltersConfig =
{
    .numberOfSIDFilters = 1U,
    .sidFilter = canfd_0_chan_1_stdIdFilters,
};
const cy_stc_canfd_f0_t canfd_0_chan_1_extIdFilterF0Config_0 =
{
    .efid1 = 0U,
    .efec = CY_CANFD_EFEC_STORE_RX_FIFO_0,
};
const cy_stc_canfd_f1_t canfd_0_chan_1_extIdFilterF1Config_0 =
{
    .efid2 = 0U,
    .eft = CY_CANFD_EFT_CLASSIC_FILTER,
};
const cy_stc_extid_filter_t canfd_0_chan_1_extIdFilter_0 =
{
    .f0_f = &canfd_0_chan_1_extIdFilterF0Config_0,
    .f1_f = &canfd_0_chan_1_extIdFilterF1Config_0,
};
const cy_stc_extid_filter_t canfd_0_chan_1_extIdFilters[] =
{
    canfd_0_chan_1_EXT_ID_FILTER_0, 
};
const cy_stc_canfd_extid_filter_config_t canfd_0_chan_1_extIdFiltersConfig =
{
    .numberOfEXTIDFilters = 1U,
    .extidFilter = (cy_stc_extid_filter_t*)&canfd_0_chan_1_extIdFilters,
    .extIDANDMask = 536870911UL,
};
const cy_stc_canfd_global_filter_config_t canfd_0_chan_1_globalFilterConfig =
{
    .nonMatchingFramesStandard = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .nonMatchingFramesExtended = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .rejectRemoteFramesStandard = false,
    .rejectRemoteFramesExtended = false,
};
const cy_en_canfd_fifo_config_t canfd_0_chan_1_rxFifo0Config =
{
    .mode = CY_CANFD_FIFO_MODE_OVERWRITE,
    .watermark = 0U,
    .numberOfFIFOElements = 16U,
    .topPointerLogicEnabled = true,
};
const cy_en_canfd_fifo_config_t canfd_0_chan_1_rxFifo1Config =
{
    .mode = CY_CANFD_FIFO_MODE_BLOCKING,
    .watermark = 0U,
    .numberOfFIFOElements = 0U,
    .topPointerLogicEnabled = false,
};
const cy_stc_canfd_config_t canfd_0_chan_1_config =
{
    .txCallback = NULL,
    .rxCallback = NULL,
    .errorCallback = NULL,
    .canFDMode = true,
    .bitrate = &canfd_0_chan_1_nominalBitrateConfig,
    .fastBitrate = &canfd_0_chan_1_dataBitrateConfig,
    .tdcConfig = &canfd_0_chan_1_tdcConfig,
    .sidFilterConfig = &canfd_0_chan_1_sidFiltersConfig,
    .extidFilterConfig = &canfd_0_chan_1_extIdFiltersConfig,
    .globalFilterConfig = &canfd_0_chan_1_globalFilterConfig,
    .rxBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO1DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .txBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0Config = &canfd_0_chan_1_rxFifo0Config,
    .rxFIFO1Config = &canfd_0_chan_1_rxFifo1Config,
    .noOfRxBuffers = 0U,
    .noOfTxBuffers = 32U,
    .messageRAMaddress = CY_CAN0MRAM_BASE + 4096U,
    .messageRAMsize = 4096U,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_0 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_1 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_2 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_3 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_4 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_5 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_6 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_7 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_8 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_9 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_10 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_11 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_12 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_13 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_14 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_15 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_16 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_17 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_18 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_19 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_20 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_21 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_22 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_23 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_24 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_25 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_26 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_27 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_28 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_29 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_30 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_1_T0RegisterBuffer_31 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_0 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_1 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_2 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_3 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_4 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_5 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_6 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_7 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_8 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_9 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_10 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_11 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_12 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_13 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_14 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_15 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_16 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_17 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_18 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_19 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_20 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_21 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_22 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_23 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_24 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_25 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_26 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_27 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_28 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_29 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_30 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_1_T1RegisterBuffer_31 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
uint32_t canfd_0_chan_1_dataBuffer_0[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_1[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_2[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_3[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_4[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_5[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_6[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_7[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_8[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_9[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_10[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_11[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_12[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_13[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_14[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_15[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_16[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_17[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_18[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_19[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_20[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_21[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_22[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_23[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_24[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_25[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_26[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_27[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_28[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_29[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_30[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_1_dataBuffer_31[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_0 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_0,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_0,
    .data_area_f = canfd_0_chan_1_dataBuffer_0,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_1 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_1,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_1,
    .data_area_f = canfd_0_chan_1_dataBuffer_1,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_2 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_2,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_2,
    .data_area_f = canfd_0_chan_1_dataBuffer_2,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_3 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_3,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_3,
    .data_area_f = canfd_0_chan_1_dataBuffer_3,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_4 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_4,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_4,
    .data_area_f = canfd_0_chan_1_dataBuffer_4,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_5 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_5,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_5,
    .data_area_f = canfd_0_chan_1_dataBuffer_5,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_6 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_6,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_6,
    .data_area_f = canfd_0_chan_1_dataBuffer_6,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_7 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_7,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_7,
    .data_area_f = canfd_0_chan_1_dataBuffer_7,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_8 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_8,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_8,
    .data_area_f = canfd_0_chan_1_dataBuffer_8,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_9 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_9,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_9,
    .data_area_f = canfd_0_chan_1_dataBuffer_9,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_10 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_10,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_10,
    .data_area_f = canfd_0_chan_1_dataBuffer_10,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_11 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_11,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_11,
    .data_area_f = canfd_0_chan_1_dataBuffer_11,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_12 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_12,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_12,
    .data_area_f = canfd_0_chan_1_dataBuffer_12,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_13 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_13,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_13,
    .data_area_f = canfd_0_chan_1_dataBuffer_13,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_14 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_14,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_14,
    .data_area_f = canfd_0_chan_1_dataBuffer_14,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_15 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_15,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_15,
    .data_area_f = canfd_0_chan_1_dataBuffer_15,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_16 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_16,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_16,
    .data_area_f = canfd_0_chan_1_dataBuffer_16,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_17 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_17,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_17,
    .data_area_f = canfd_0_chan_1_dataBuffer_17,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_18 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_18,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_18,
    .data_area_f = canfd_0_chan_1_dataBuffer_18,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_19 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_19,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_19,
    .data_area_f = canfd_0_chan_1_dataBuffer_19,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_20 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_20,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_20,
    .data_area_f = canfd_0_chan_1_dataBuffer_20,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_21 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_21,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_21,
    .data_area_f = canfd_0_chan_1_dataBuffer_21,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_22 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_22,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_22,
    .data_area_f = canfd_0_chan_1_dataBuffer_22,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_23 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_23,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_23,
    .data_area_f = canfd_0_chan_1_dataBuffer_23,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_24 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_24,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_24,
    .data_area_f = canfd_0_chan_1_dataBuffer_24,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_25 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_25,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_25,
    .data_area_f = canfd_0_chan_1_dataBuffer_25,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_26 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_26,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_26,
    .data_area_f = canfd_0_chan_1_dataBuffer_26,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_27 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_27,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_27,
    .data_area_f = canfd_0_chan_1_dataBuffer_27,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_28 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_28,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_28,
    .data_area_f = canfd_0_chan_1_dataBuffer_28,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_29 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_29,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_29,
    .data_area_f = canfd_0_chan_1_dataBuffer_29,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_30 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_30,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_30,
    .data_area_f = canfd_0_chan_1_dataBuffer_30,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_1_txBuffer_31 =
{
    .t0_f = &canfd_0_chan_1_T0RegisterBuffer_31,
    .t1_f = &canfd_0_chan_1_T1RegisterBuffer_31,
    .data_area_f = canfd_0_chan_1_dataBuffer_31,
};

#if defined (CY_USING_HAL)
const cyhal_resource_inst_t canfd_0_chan_1_obj =
{
    .type = CYHAL_RSC_CAN,
    .block_num = 0U,
    .channel_num = 1U,
};
#endif /* defined (CY_USING_HAL) */

const cy_stc_canfd_bitrate_t canfd_0_chan_2_nominalBitrateConfig =
{
    .prescaler = 4U - 1U,
    .timeSegment1 = 15U - 1U,
    .timeSegment2 = 4U - 1U,
    .syncJumpWidth = 4U - 1U,
};
const cy_stc_canfd_bitrate_t canfd_0_chan_2_dataBitrateConfig =
{
    .prescaler = 2U - 1U,
    .timeSegment1 = 5U - 1U,
    .timeSegment2 = 2U - 1U,
    .syncJumpWidth = 2U - 1U,
};
const cy_stc_canfd_transceiver_delay_compensation_t canfd_0_chan_2_tdcConfig =
{
    .tdcEnabled = true,
    .tdcOffset = 6U,
    .tdcFilterWindow = 6U,
};
const cy_stc_id_filter_t canfd_0_chan_2_stdIdFilter_0 =
{
    .sfid2 = 0U,
    .sfid1 = 0U,
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0,
    .sft = CY_CANFD_SFT_CLASSIC_FILTER,
};
const cy_stc_id_filter_t canfd_0_chan_2_stdIdFilters[] =
{
    canfd_0_chan_2_STD_ID_FILTER_0, 
};
const cy_stc_canfd_sid_filter_config_t canfd_0_chan_2_sidFiltersConfig =
{
    .numberOfSIDFilters = 1U,
    .sidFilter = canfd_0_chan_2_stdIdFilters,
};
const cy_stc_canfd_f0_t canfd_0_chan_2_extIdFilterF0Config_0 =
{
    .efid1 = 0U,
    .efec = CY_CANFD_EFEC_STORE_RX_FIFO_0,
};
const cy_stc_canfd_f1_t canfd_0_chan_2_extIdFilterF1Config_0 =
{
    .efid2 = 0U,
    .eft = CY_CANFD_EFT_CLASSIC_FILTER,
};
const cy_stc_extid_filter_t canfd_0_chan_2_extIdFilter_0 =
{
    .f0_f = &canfd_0_chan_2_extIdFilterF0Config_0,
    .f1_f = &canfd_0_chan_2_extIdFilterF1Config_0,
};
const cy_stc_extid_filter_t canfd_0_chan_2_extIdFilters[] =
{
    canfd_0_chan_2_EXT_ID_FILTER_0, 
};
const cy_stc_canfd_extid_filter_config_t canfd_0_chan_2_extIdFiltersConfig =
{
    .numberOfEXTIDFilters = 1U,
    .extidFilter = (cy_stc_extid_filter_t*)&canfd_0_chan_2_extIdFilters,
    .extIDANDMask = 536870911UL,
};
const cy_stc_canfd_global_filter_config_t canfd_0_chan_2_globalFilterConfig =
{
    .nonMatchingFramesStandard = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .nonMatchingFramesExtended = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .rejectRemoteFramesStandard = false,
    .rejectRemoteFramesExtended = false,
};
const cy_en_canfd_fifo_config_t canfd_0_chan_2_rxFifo0Config =
{
    .mode = CY_CANFD_FIFO_MODE_OVERWRITE,
    .watermark = 0U,
    .numberOfFIFOElements = 16U,
    .topPointerLogicEnabled = true,
};
const cy_en_canfd_fifo_config_t canfd_0_chan_2_rxFifo1Config =
{
    .mode = CY_CANFD_FIFO_MODE_BLOCKING,
    .watermark = 0U,
    .numberOfFIFOElements = 0U,
    .topPointerLogicEnabled = false,
};
const cy_stc_canfd_config_t canfd_0_chan_2_config =
{
    .txCallback = NULL,
    .rxCallback = NULL,
    .errorCallback = NULL,
    .canFDMode = true,
    .bitrate = &canfd_0_chan_2_nominalBitrateConfig,
    .fastBitrate = &canfd_0_chan_2_dataBitrateConfig,
    .tdcConfig = &canfd_0_chan_2_tdcConfig,
    .sidFilterConfig = &canfd_0_chan_2_sidFiltersConfig,
    .extidFilterConfig = &canfd_0_chan_2_extIdFiltersConfig,
    .globalFilterConfig = &canfd_0_chan_2_globalFilterConfig,
    .rxBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO1DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .txBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0Config = &canfd_0_chan_2_rxFifo0Config,
    .rxFIFO1Config = &canfd_0_chan_2_rxFifo1Config,
    .noOfRxBuffers = 0U,
    .noOfTxBuffers = 32U,
    .messageRAMaddress = CY_CAN0MRAM_BASE + 8192U,
    .messageRAMsize = 4096U,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_0 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_1 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_2 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_3 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_4 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_5 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_6 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_7 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_8 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_9 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_10 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_11 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_12 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_13 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_14 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_15 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_16 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_17 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_18 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_19 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_20 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_21 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_22 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_23 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_24 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_25 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_26 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_27 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_28 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_29 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_30 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_2_T0RegisterBuffer_31 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_0 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_1 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_2 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_3 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_4 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_5 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_6 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_7 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_8 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_9 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_10 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_11 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_12 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_13 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_14 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_15 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_16 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_17 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_18 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_19 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_20 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_21 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_22 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_23 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_24 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_25 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_26 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_27 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_28 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_29 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_30 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_2_T1RegisterBuffer_31 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
uint32_t canfd_0_chan_2_dataBuffer_0[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_1[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_2[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_3[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_4[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_5[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_6[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_7[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_8[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_9[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_10[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_11[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_12[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_13[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_14[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_15[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_16[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_17[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_18[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_19[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_20[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_21[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_22[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_23[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_24[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_25[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_26[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_27[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_28[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_29[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_30[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_2_dataBuffer_31[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_0 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_0,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_0,
    .data_area_f = canfd_0_chan_2_dataBuffer_0,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_1 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_1,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_1,
    .data_area_f = canfd_0_chan_2_dataBuffer_1,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_2 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_2,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_2,
    .data_area_f = canfd_0_chan_2_dataBuffer_2,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_3 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_3,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_3,
    .data_area_f = canfd_0_chan_2_dataBuffer_3,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_4 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_4,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_4,
    .data_area_f = canfd_0_chan_2_dataBuffer_4,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_5 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_5,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_5,
    .data_area_f = canfd_0_chan_2_dataBuffer_5,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_6 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_6,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_6,
    .data_area_f = canfd_0_chan_2_dataBuffer_6,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_7 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_7,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_7,
    .data_area_f = canfd_0_chan_2_dataBuffer_7,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_8 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_8,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_8,
    .data_area_f = canfd_0_chan_2_dataBuffer_8,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_9 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_9,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_9,
    .data_area_f = canfd_0_chan_2_dataBuffer_9,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_10 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_10,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_10,
    .data_area_f = canfd_0_chan_2_dataBuffer_10,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_11 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_11,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_11,
    .data_area_f = canfd_0_chan_2_dataBuffer_11,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_12 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_12,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_12,
    .data_area_f = canfd_0_chan_2_dataBuffer_12,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_13 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_13,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_13,
    .data_area_f = canfd_0_chan_2_dataBuffer_13,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_14 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_14,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_14,
    .data_area_f = canfd_0_chan_2_dataBuffer_14,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_15 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_15,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_15,
    .data_area_f = canfd_0_chan_2_dataBuffer_15,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_16 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_16,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_16,
    .data_area_f = canfd_0_chan_2_dataBuffer_16,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_17 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_17,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_17,
    .data_area_f = canfd_0_chan_2_dataBuffer_17,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_18 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_18,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_18,
    .data_area_f = canfd_0_chan_2_dataBuffer_18,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_19 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_19,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_19,
    .data_area_f = canfd_0_chan_2_dataBuffer_19,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_20 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_20,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_20,
    .data_area_f = canfd_0_chan_2_dataBuffer_20,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_21 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_21,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_21,
    .data_area_f = canfd_0_chan_2_dataBuffer_21,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_22 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_22,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_22,
    .data_area_f = canfd_0_chan_2_dataBuffer_22,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_23 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_23,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_23,
    .data_area_f = canfd_0_chan_2_dataBuffer_23,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_24 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_24,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_24,
    .data_area_f = canfd_0_chan_2_dataBuffer_24,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_25 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_25,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_25,
    .data_area_f = canfd_0_chan_2_dataBuffer_25,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_26 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_26,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_26,
    .data_area_f = canfd_0_chan_2_dataBuffer_26,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_27 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_27,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_27,
    .data_area_f = canfd_0_chan_2_dataBuffer_27,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_28 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_28,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_28,
    .data_area_f = canfd_0_chan_2_dataBuffer_28,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_29 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_29,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_29,
    .data_area_f = canfd_0_chan_2_dataBuffer_29,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_30 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_30,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_30,
    .data_area_f = canfd_0_chan_2_dataBuffer_30,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_2_txBuffer_31 =
{
    .t0_f = &canfd_0_chan_2_T0RegisterBuffer_31,
    .t1_f = &canfd_0_chan_2_T1RegisterBuffer_31,
    .data_area_f = canfd_0_chan_2_dataBuffer_31,
};

#if defined (CY_USING_HAL)
const cyhal_resource_inst_t canfd_0_chan_2_obj =
{
    .type = CYHAL_RSC_CAN,
    .block_num = 0U,
    .channel_num = 2U,
};
#endif /* defined (CY_USING_HAL) */

const cy_stc_canfd_bitrate_t canfd_0_chan_3_nominalBitrateConfig =
{
    .prescaler = 4U - 1U,
    .timeSegment1 = 15U - 1U,
    .timeSegment2 = 4U - 1U,
    .syncJumpWidth = 4U - 1U,
};
const cy_stc_canfd_bitrate_t canfd_0_chan_3_dataBitrateConfig =
{
    .prescaler = 2U - 1U,
    .timeSegment1 = 5U - 1U,
    .timeSegment2 = 2U - 1U,
    .syncJumpWidth = 2U - 1U,
};
const cy_stc_canfd_transceiver_delay_compensation_t canfd_0_chan_3_tdcConfig =
{
    .tdcEnabled = true,
    .tdcOffset = 6U,
    .tdcFilterWindow = 6U,
};
const cy_stc_id_filter_t canfd_0_chan_3_stdIdFilter_0 =
{
    .sfid2 = 0U,
    .sfid1 = 0U,
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0,
    .sft = CY_CANFD_SFT_CLASSIC_FILTER,
};
const cy_stc_id_filter_t canfd_0_chan_3_stdIdFilters[] =
{
    canfd_0_chan_3_STD_ID_FILTER_0, 
};
const cy_stc_canfd_sid_filter_config_t canfd_0_chan_3_sidFiltersConfig =
{
    .numberOfSIDFilters = 1U,
    .sidFilter = canfd_0_chan_3_stdIdFilters,
};
const cy_stc_canfd_f0_t canfd_0_chan_3_extIdFilterF0Config_0 =
{
    .efid1 = 0U,
    .efec = CY_CANFD_EFEC_STORE_RX_FIFO_0,
};
const cy_stc_canfd_f1_t canfd_0_chan_3_extIdFilterF1Config_0 =
{
    .efid2 = 0U,
    .eft = CY_CANFD_EFT_CLASSIC_FILTER,
};
const cy_stc_extid_filter_t canfd_0_chan_3_extIdFilter_0 =
{
    .f0_f = &canfd_0_chan_3_extIdFilterF0Config_0,
    .f1_f = &canfd_0_chan_3_extIdFilterF1Config_0,
};
const cy_stc_extid_filter_t canfd_0_chan_3_extIdFilters[] =
{
    canfd_0_chan_3_EXT_ID_FILTER_0, 
};
const cy_stc_canfd_extid_filter_config_t canfd_0_chan_3_extIdFiltersConfig =
{
    .numberOfEXTIDFilters = 1U,
    .extidFilter = (cy_stc_extid_filter_t*)&canfd_0_chan_3_extIdFilters,
    .extIDANDMask = 536870911UL,
};
const cy_stc_canfd_global_filter_config_t canfd_0_chan_3_globalFilterConfig =
{
    .nonMatchingFramesStandard = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .nonMatchingFramesExtended = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .rejectRemoteFramesStandard = false,
    .rejectRemoteFramesExtended = false,
};
const cy_en_canfd_fifo_config_t canfd_0_chan_3_rxFifo0Config =
{
    .mode = CY_CANFD_FIFO_MODE_OVERWRITE,
    .watermark = 0U,
    .numberOfFIFOElements = 16U,
    .topPointerLogicEnabled = true,
};
const cy_en_canfd_fifo_config_t canfd_0_chan_3_rxFifo1Config =
{
    .mode = CY_CANFD_FIFO_MODE_BLOCKING,
    .watermark = 0U,
    .numberOfFIFOElements = 0U,
    .topPointerLogicEnabled = false,
};
const cy_stc_canfd_config_t canfd_0_chan_3_config =
{
    .txCallback = NULL,
    .rxCallback = NULL,
    .errorCallback = NULL,
    .canFDMode = true,
    .bitrate = &canfd_0_chan_3_nominalBitrateConfig,
    .fastBitrate = &canfd_0_chan_3_dataBitrateConfig,
    .tdcConfig = &canfd_0_chan_3_tdcConfig,
    .sidFilterConfig = &canfd_0_chan_3_sidFiltersConfig,
    .extidFilterConfig = &canfd_0_chan_3_extIdFiltersConfig,
    .globalFilterConfig = &canfd_0_chan_3_globalFilterConfig,
    .rxBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO1DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .txBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0Config = &canfd_0_chan_3_rxFifo0Config,
    .rxFIFO1Config = &canfd_0_chan_3_rxFifo1Config,
    .noOfRxBuffers = 0U,
    .noOfTxBuffers = 32U,
    .messageRAMaddress = CY_CAN0MRAM_BASE + 12288U,
    .messageRAMsize = 4096U,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_0 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_1 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_2 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_3 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_4 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_5 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_6 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_7 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_8 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_9 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_10 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_11 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_12 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_13 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_14 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_15 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_16 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_17 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_18 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_19 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_20 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_21 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_22 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_23 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_24 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_25 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_26 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_27 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_28 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_29 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_30 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_0_chan_3_T0RegisterBuffer_31 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_0 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_1 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_2 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_3 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_4 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_5 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_6 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_7 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_8 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_9 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_10 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_11 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_12 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_13 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_14 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_15 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_16 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_17 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_18 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_19 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_20 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_21 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_22 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_23 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_24 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_25 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_26 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_27 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_28 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_29 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_30 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_0_chan_3_T1RegisterBuffer_31 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
uint32_t canfd_0_chan_3_dataBuffer_0[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_1[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_2[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_3[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_4[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_5[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_6[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_7[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_8[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_9[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_10[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_11[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_12[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_13[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_14[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_15[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_16[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_17[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_18[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_19[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_20[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_21[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_22[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_23[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_24[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_25[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_26[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_27[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_28[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_29[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_30[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_0_chan_3_dataBuffer_31[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_0 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_0,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_0,
    .data_area_f = canfd_0_chan_3_dataBuffer_0,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_1 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_1,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_1,
    .data_area_f = canfd_0_chan_3_dataBuffer_1,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_2 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_2,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_2,
    .data_area_f = canfd_0_chan_3_dataBuffer_2,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_3 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_3,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_3,
    .data_area_f = canfd_0_chan_3_dataBuffer_3,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_4 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_4,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_4,
    .data_area_f = canfd_0_chan_3_dataBuffer_4,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_5 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_5,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_5,
    .data_area_f = canfd_0_chan_3_dataBuffer_5,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_6 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_6,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_6,
    .data_area_f = canfd_0_chan_3_dataBuffer_6,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_7 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_7,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_7,
    .data_area_f = canfd_0_chan_3_dataBuffer_7,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_8 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_8,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_8,
    .data_area_f = canfd_0_chan_3_dataBuffer_8,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_9 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_9,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_9,
    .data_area_f = canfd_0_chan_3_dataBuffer_9,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_10 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_10,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_10,
    .data_area_f = canfd_0_chan_3_dataBuffer_10,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_11 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_11,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_11,
    .data_area_f = canfd_0_chan_3_dataBuffer_11,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_12 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_12,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_12,
    .data_area_f = canfd_0_chan_3_dataBuffer_12,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_13 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_13,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_13,
    .data_area_f = canfd_0_chan_3_dataBuffer_13,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_14 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_14,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_14,
    .data_area_f = canfd_0_chan_3_dataBuffer_14,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_15 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_15,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_15,
    .data_area_f = canfd_0_chan_3_dataBuffer_15,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_16 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_16,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_16,
    .data_area_f = canfd_0_chan_3_dataBuffer_16,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_17 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_17,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_17,
    .data_area_f = canfd_0_chan_3_dataBuffer_17,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_18 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_18,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_18,
    .data_area_f = canfd_0_chan_3_dataBuffer_18,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_19 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_19,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_19,
    .data_area_f = canfd_0_chan_3_dataBuffer_19,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_20 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_20,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_20,
    .data_area_f = canfd_0_chan_3_dataBuffer_20,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_21 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_21,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_21,
    .data_area_f = canfd_0_chan_3_dataBuffer_21,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_22 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_22,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_22,
    .data_area_f = canfd_0_chan_3_dataBuffer_22,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_23 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_23,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_23,
    .data_area_f = canfd_0_chan_3_dataBuffer_23,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_24 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_24,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_24,
    .data_area_f = canfd_0_chan_3_dataBuffer_24,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_25 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_25,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_25,
    .data_area_f = canfd_0_chan_3_dataBuffer_25,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_26 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_26,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_26,
    .data_area_f = canfd_0_chan_3_dataBuffer_26,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_27 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_27,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_27,
    .data_area_f = canfd_0_chan_3_dataBuffer_27,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_28 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_28,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_28,
    .data_area_f = canfd_0_chan_3_dataBuffer_28,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_29 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_29,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_29,
    .data_area_f = canfd_0_chan_3_dataBuffer_29,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_30 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_30,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_30,
    .data_area_f = canfd_0_chan_3_dataBuffer_30,
};
cy_stc_canfd_tx_buffer_t canfd_0_chan_3_txBuffer_31 =
{
    .t0_f = &canfd_0_chan_3_T0RegisterBuffer_31,
    .t1_f = &canfd_0_chan_3_T1RegisterBuffer_31,
    .data_area_f = canfd_0_chan_3_dataBuffer_31,
};

#if defined (CY_USING_HAL)
const cyhal_resource_inst_t canfd_0_chan_3_obj =
{
    .type = CYHAL_RSC_CAN,
    .block_num = 0U,
    .channel_num = 3U,
};
#endif /* defined (CY_USING_HAL) */

const cy_stc_canfd_bitrate_t canfd_1_chan_0_nominalBitrateConfig =
{
    .prescaler = 4U - 1U,
    .timeSegment1 = 15U - 1U,
    .timeSegment2 = 4U - 1U,
    .syncJumpWidth = 4U - 1U,
};
const cy_stc_canfd_bitrate_t canfd_1_chan_0_dataBitrateConfig =
{
    .prescaler = 2U - 1U,
    .timeSegment1 = 5U - 1U,
    .timeSegment2 = 2U - 1U,
    .syncJumpWidth = 2U - 1U,
};
const cy_stc_canfd_transceiver_delay_compensation_t canfd_1_chan_0_tdcConfig =
{
    .tdcEnabled = true,
    .tdcOffset = 6U,
    .tdcFilterWindow = 6U,
};
const cy_stc_id_filter_t canfd_1_chan_0_stdIdFilter_0 =
{
    .sfid2 = 0U,
    .sfid1 = 0U,
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0,
    .sft = CY_CANFD_SFT_CLASSIC_FILTER,
};
const cy_stc_id_filter_t canfd_1_chan_0_stdIdFilters[] =
{
    canfd_1_chan_0_STD_ID_FILTER_0, 
};
const cy_stc_canfd_sid_filter_config_t canfd_1_chan_0_sidFiltersConfig =
{
    .numberOfSIDFilters = 1U,
    .sidFilter = canfd_1_chan_0_stdIdFilters,
};
const cy_stc_canfd_f0_t canfd_1_chan_0_extIdFilterF0Config_0 =
{
    .efid1 = 0U,
    .efec = CY_CANFD_EFEC_STORE_RX_FIFO_0,
};
const cy_stc_canfd_f1_t canfd_1_chan_0_extIdFilterF1Config_0 =
{
    .efid2 = 0U,
    .eft = CY_CANFD_EFT_CLASSIC_FILTER,
};
const cy_stc_extid_filter_t canfd_1_chan_0_extIdFilter_0 =
{
    .f0_f = &canfd_1_chan_0_extIdFilterF0Config_0,
    .f1_f = &canfd_1_chan_0_extIdFilterF1Config_0,
};
const cy_stc_extid_filter_t canfd_1_chan_0_extIdFilters[] =
{
    canfd_1_chan_0_EXT_ID_FILTER_0, 
};
const cy_stc_canfd_extid_filter_config_t canfd_1_chan_0_extIdFiltersConfig =
{
    .numberOfEXTIDFilters = 1U,
    .extidFilter = (cy_stc_extid_filter_t*)&canfd_1_chan_0_extIdFilters,
    .extIDANDMask = 536870911UL,
};
const cy_stc_canfd_global_filter_config_t canfd_1_chan_0_globalFilterConfig =
{
    .nonMatchingFramesStandard = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .nonMatchingFramesExtended = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .rejectRemoteFramesStandard = false,
    .rejectRemoteFramesExtended = false,
};
const cy_en_canfd_fifo_config_t canfd_1_chan_0_rxFifo0Config =
{
    .mode = CY_CANFD_FIFO_MODE_OVERWRITE,
    .watermark = 0U,
    .numberOfFIFOElements = 16U,
    .topPointerLogicEnabled = true,
};
const cy_en_canfd_fifo_config_t canfd_1_chan_0_rxFifo1Config =
{
    .mode = CY_CANFD_FIFO_MODE_BLOCKING,
    .watermark = 0U,
    .numberOfFIFOElements = 0U,
    .topPointerLogicEnabled = false,
};
const cy_stc_canfd_config_t canfd_1_chan_0_config =
{
    .txCallback = NULL,
    .rxCallback = NULL,
    .errorCallback = NULL,
    .canFDMode = true,
    .bitrate = &canfd_1_chan_0_nominalBitrateConfig,
    .fastBitrate = &canfd_1_chan_0_dataBitrateConfig,
    .tdcConfig = &canfd_1_chan_0_tdcConfig,
    .sidFilterConfig = &canfd_1_chan_0_sidFiltersConfig,
    .extidFilterConfig = &canfd_1_chan_0_extIdFiltersConfig,
    .globalFilterConfig = &canfd_1_chan_0_globalFilterConfig,
    .rxBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO1DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .txBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0Config = &canfd_1_chan_0_rxFifo0Config,
    .rxFIFO1Config = &canfd_1_chan_0_rxFifo1Config,
    .noOfRxBuffers = 0U,
    .noOfTxBuffers = 32U,
    .messageRAMaddress = CY_CAN1MRAM_BASE + 0U,
    .messageRAMsize = 4096U,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_0 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_1 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_2 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_3 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_4 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_5 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_6 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_7 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_8 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_9 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_10 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_11 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_12 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_13 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_14 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_15 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_16 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_17 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_18 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_19 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_20 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_21 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_22 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_23 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_24 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_25 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_26 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_27 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_28 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_29 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_30 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_0_T0RegisterBuffer_31 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_0 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_1 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_2 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_3 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_4 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_5 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_6 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_7 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_8 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_9 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_10 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_11 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_12 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_13 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_14 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_15 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_16 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_17 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_18 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_19 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_20 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_21 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_22 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_23 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_24 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_25 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_26 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_27 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_28 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_29 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_30 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_0_T1RegisterBuffer_31 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
uint32_t canfd_1_chan_0_dataBuffer_0[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_1[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_2[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_3[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_4[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_5[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_6[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_7[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_8[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_9[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_10[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_11[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_12[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_13[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_14[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_15[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_16[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_17[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_18[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_19[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_20[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_21[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_22[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_23[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_24[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_25[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_26[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_27[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_28[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_29[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_30[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_0_dataBuffer_31[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_0 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_0,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_0,
    .data_area_f = canfd_1_chan_0_dataBuffer_0,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_1 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_1,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_1,
    .data_area_f = canfd_1_chan_0_dataBuffer_1,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_2 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_2,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_2,
    .data_area_f = canfd_1_chan_0_dataBuffer_2,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_3 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_3,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_3,
    .data_area_f = canfd_1_chan_0_dataBuffer_3,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_4 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_4,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_4,
    .data_area_f = canfd_1_chan_0_dataBuffer_4,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_5 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_5,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_5,
    .data_area_f = canfd_1_chan_0_dataBuffer_5,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_6 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_6,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_6,
    .data_area_f = canfd_1_chan_0_dataBuffer_6,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_7 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_7,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_7,
    .data_area_f = canfd_1_chan_0_dataBuffer_7,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_8 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_8,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_8,
    .data_area_f = canfd_1_chan_0_dataBuffer_8,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_9 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_9,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_9,
    .data_area_f = canfd_1_chan_0_dataBuffer_9,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_10 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_10,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_10,
    .data_area_f = canfd_1_chan_0_dataBuffer_10,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_11 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_11,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_11,
    .data_area_f = canfd_1_chan_0_dataBuffer_11,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_12 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_12,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_12,
    .data_area_f = canfd_1_chan_0_dataBuffer_12,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_13 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_13,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_13,
    .data_area_f = canfd_1_chan_0_dataBuffer_13,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_14 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_14,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_14,
    .data_area_f = canfd_1_chan_0_dataBuffer_14,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_15 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_15,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_15,
    .data_area_f = canfd_1_chan_0_dataBuffer_15,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_16 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_16,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_16,
    .data_area_f = canfd_1_chan_0_dataBuffer_16,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_17 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_17,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_17,
    .data_area_f = canfd_1_chan_0_dataBuffer_17,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_18 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_18,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_18,
    .data_area_f = canfd_1_chan_0_dataBuffer_18,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_19 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_19,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_19,
    .data_area_f = canfd_1_chan_0_dataBuffer_19,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_20 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_20,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_20,
    .data_area_f = canfd_1_chan_0_dataBuffer_20,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_21 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_21,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_21,
    .data_area_f = canfd_1_chan_0_dataBuffer_21,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_22 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_22,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_22,
    .data_area_f = canfd_1_chan_0_dataBuffer_22,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_23 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_23,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_23,
    .data_area_f = canfd_1_chan_0_dataBuffer_23,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_24 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_24,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_24,
    .data_area_f = canfd_1_chan_0_dataBuffer_24,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_25 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_25,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_25,
    .data_area_f = canfd_1_chan_0_dataBuffer_25,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_26 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_26,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_26,
    .data_area_f = canfd_1_chan_0_dataBuffer_26,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_27 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_27,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_27,
    .data_area_f = canfd_1_chan_0_dataBuffer_27,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_28 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_28,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_28,
    .data_area_f = canfd_1_chan_0_dataBuffer_28,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_29 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_29,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_29,
    .data_area_f = canfd_1_chan_0_dataBuffer_29,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_30 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_30,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_30,
    .data_area_f = canfd_1_chan_0_dataBuffer_30,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_0_txBuffer_31 =
{
    .t0_f = &canfd_1_chan_0_T0RegisterBuffer_31,
    .t1_f = &canfd_1_chan_0_T1RegisterBuffer_31,
    .data_area_f = canfd_1_chan_0_dataBuffer_31,
};

#if defined (CY_USING_HAL)
const cyhal_resource_inst_t canfd_1_chan_0_obj =
{
    .type = CYHAL_RSC_CAN,
    .block_num = 1U,
    .channel_num = 0U,
};
#endif /* defined (CY_USING_HAL) */

const cy_stc_canfd_bitrate_t canfd_1_chan_1_nominalBitrateConfig =
{
    .prescaler = 4U - 1U,
    .timeSegment1 = 15U - 1U,
    .timeSegment2 = 4U - 1U,
    .syncJumpWidth = 4U - 1U,
};
const cy_stc_canfd_bitrate_t canfd_1_chan_1_dataBitrateConfig =
{
    .prescaler = 2U - 1U,
    .timeSegment1 = 5U - 1U,
    .timeSegment2 = 2U - 1U,
    .syncJumpWidth = 2U - 1U,
};
const cy_stc_canfd_transceiver_delay_compensation_t canfd_1_chan_1_tdcConfig =
{
    .tdcEnabled = true,
    .tdcOffset = 6U,
    .tdcFilterWindow = 6U,
};
const cy_stc_id_filter_t canfd_1_chan_1_stdIdFilter_0 =
{
    .sfid2 = 0U,
    .sfid1 = 0U,
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0,
    .sft = CY_CANFD_SFT_CLASSIC_FILTER,
};
const cy_stc_id_filter_t canfd_1_chan_1_stdIdFilters[] =
{
    canfd_1_chan_1_STD_ID_FILTER_0, 
};
const cy_stc_canfd_sid_filter_config_t canfd_1_chan_1_sidFiltersConfig =
{
    .numberOfSIDFilters = 1U,
    .sidFilter = canfd_1_chan_1_stdIdFilters,
};
const cy_stc_canfd_f0_t canfd_1_chan_1_extIdFilterF0Config_0 =
{
    .efid1 = 0U,
    .efec = CY_CANFD_EFEC_STORE_RX_FIFO_0,
};
const cy_stc_canfd_f1_t canfd_1_chan_1_extIdFilterF1Config_0 =
{
    .efid2 = 0U,
    .eft = CY_CANFD_EFT_CLASSIC_FILTER,
};
const cy_stc_extid_filter_t canfd_1_chan_1_extIdFilter_0 =
{
    .f0_f = &canfd_1_chan_1_extIdFilterF0Config_0,
    .f1_f = &canfd_1_chan_1_extIdFilterF1Config_0,
};
const cy_stc_extid_filter_t canfd_1_chan_1_extIdFilters[] =
{
    canfd_1_chan_1_EXT_ID_FILTER_0, 
};
const cy_stc_canfd_extid_filter_config_t canfd_1_chan_1_extIdFiltersConfig =
{
    .numberOfEXTIDFilters = 1U,
    .extidFilter = (cy_stc_extid_filter_t*)&canfd_1_chan_1_extIdFilters,
    .extIDANDMask = 536870911UL,
};
const cy_stc_canfd_global_filter_config_t canfd_1_chan_1_globalFilterConfig =
{
    .nonMatchingFramesStandard = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .nonMatchingFramesExtended = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .rejectRemoteFramesStandard = false,
    .rejectRemoteFramesExtended = false,
};
const cy_en_canfd_fifo_config_t canfd_1_chan_1_rxFifo0Config =
{
    .mode = CY_CANFD_FIFO_MODE_OVERWRITE,
    .watermark = 0U,
    .numberOfFIFOElements = 16U,
    .topPointerLogicEnabled = true,
};
const cy_en_canfd_fifo_config_t canfd_1_chan_1_rxFifo1Config =
{
    .mode = CY_CANFD_FIFO_MODE_BLOCKING,
    .watermark = 0U,
    .numberOfFIFOElements = 0U,
    .topPointerLogicEnabled = false,
};
const cy_stc_canfd_config_t canfd_1_chan_1_config =
{
    .txCallback = NULL,
    .rxCallback = NULL,
    .errorCallback = NULL,
    .canFDMode = true,
    .bitrate = &canfd_1_chan_1_nominalBitrateConfig,
    .fastBitrate = &canfd_1_chan_1_dataBitrateConfig,
    .tdcConfig = &canfd_1_chan_1_tdcConfig,
    .sidFilterConfig = &canfd_1_chan_1_sidFiltersConfig,
    .extidFilterConfig = &canfd_1_chan_1_extIdFiltersConfig,
    .globalFilterConfig = &canfd_1_chan_1_globalFilterConfig,
    .rxBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO1DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .txBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0Config = &canfd_1_chan_1_rxFifo0Config,
    .rxFIFO1Config = &canfd_1_chan_1_rxFifo1Config,
    .noOfRxBuffers = 0U,
    .noOfTxBuffers = 32U,
    .messageRAMaddress = CY_CAN1MRAM_BASE + 4096U,
    .messageRAMsize = 4096U,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_0 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_1 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_2 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_3 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_4 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_5 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_6 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_7 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_8 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_9 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_10 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_11 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_12 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_13 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_14 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_15 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_16 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_17 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_18 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_19 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_20 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_21 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_22 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_23 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_24 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_25 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_26 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_27 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_28 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_29 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_30 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_1_T0RegisterBuffer_31 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_0 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_1 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_2 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_3 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_4 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_5 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_6 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_7 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_8 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_9 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_10 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_11 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_12 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_13 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_14 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_15 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_16 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_17 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_18 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_19 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_20 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_21 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_22 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_23 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_24 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_25 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_26 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_27 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_28 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_29 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_30 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_1_T1RegisterBuffer_31 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
uint32_t canfd_1_chan_1_dataBuffer_0[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_1[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_2[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_3[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_4[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_5[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_6[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_7[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_8[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_9[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_10[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_11[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_12[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_13[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_14[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_15[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_16[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_17[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_18[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_19[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_20[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_21[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_22[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_23[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_24[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_25[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_26[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_27[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_28[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_29[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_30[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_1_dataBuffer_31[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_0 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_0,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_0,
    .data_area_f = canfd_1_chan_1_dataBuffer_0,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_1 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_1,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_1,
    .data_area_f = canfd_1_chan_1_dataBuffer_1,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_2 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_2,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_2,
    .data_area_f = canfd_1_chan_1_dataBuffer_2,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_3 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_3,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_3,
    .data_area_f = canfd_1_chan_1_dataBuffer_3,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_4 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_4,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_4,
    .data_area_f = canfd_1_chan_1_dataBuffer_4,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_5 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_5,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_5,
    .data_area_f = canfd_1_chan_1_dataBuffer_5,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_6 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_6,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_6,
    .data_area_f = canfd_1_chan_1_dataBuffer_6,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_7 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_7,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_7,
    .data_area_f = canfd_1_chan_1_dataBuffer_7,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_8 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_8,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_8,
    .data_area_f = canfd_1_chan_1_dataBuffer_8,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_9 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_9,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_9,
    .data_area_f = canfd_1_chan_1_dataBuffer_9,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_10 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_10,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_10,
    .data_area_f = canfd_1_chan_1_dataBuffer_10,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_11 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_11,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_11,
    .data_area_f = canfd_1_chan_1_dataBuffer_11,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_12 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_12,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_12,
    .data_area_f = canfd_1_chan_1_dataBuffer_12,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_13 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_13,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_13,
    .data_area_f = canfd_1_chan_1_dataBuffer_13,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_14 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_14,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_14,
    .data_area_f = canfd_1_chan_1_dataBuffer_14,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_15 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_15,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_15,
    .data_area_f = canfd_1_chan_1_dataBuffer_15,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_16 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_16,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_16,
    .data_area_f = canfd_1_chan_1_dataBuffer_16,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_17 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_17,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_17,
    .data_area_f = canfd_1_chan_1_dataBuffer_17,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_18 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_18,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_18,
    .data_area_f = canfd_1_chan_1_dataBuffer_18,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_19 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_19,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_19,
    .data_area_f = canfd_1_chan_1_dataBuffer_19,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_20 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_20,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_20,
    .data_area_f = canfd_1_chan_1_dataBuffer_20,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_21 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_21,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_21,
    .data_area_f = canfd_1_chan_1_dataBuffer_21,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_22 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_22,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_22,
    .data_area_f = canfd_1_chan_1_dataBuffer_22,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_23 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_23,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_23,
    .data_area_f = canfd_1_chan_1_dataBuffer_23,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_24 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_24,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_24,
    .data_area_f = canfd_1_chan_1_dataBuffer_24,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_25 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_25,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_25,
    .data_area_f = canfd_1_chan_1_dataBuffer_25,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_26 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_26,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_26,
    .data_area_f = canfd_1_chan_1_dataBuffer_26,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_27 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_27,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_27,
    .data_area_f = canfd_1_chan_1_dataBuffer_27,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_28 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_28,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_28,
    .data_area_f = canfd_1_chan_1_dataBuffer_28,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_29 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_29,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_29,
    .data_area_f = canfd_1_chan_1_dataBuffer_29,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_30 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_30,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_30,
    .data_area_f = canfd_1_chan_1_dataBuffer_30,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_1_txBuffer_31 =
{
    .t0_f = &canfd_1_chan_1_T0RegisterBuffer_31,
    .t1_f = &canfd_1_chan_1_T1RegisterBuffer_31,
    .data_area_f = canfd_1_chan_1_dataBuffer_31,
};

#if defined (CY_USING_HAL)
const cyhal_resource_inst_t canfd_1_chan_1_obj =
{
    .type = CYHAL_RSC_CAN,
    .block_num = 1U,
    .channel_num = 1U,
};
#endif /* defined (CY_USING_HAL) */

const cy_stc_canfd_bitrate_t canfd_1_chan_2_nominalBitrateConfig =
{
    .prescaler = 4U - 1U,
    .timeSegment1 = 15U - 1U,
    .timeSegment2 = 4U - 1U,
    .syncJumpWidth = 4U - 1U,
};
const cy_stc_canfd_bitrate_t canfd_1_chan_2_dataBitrateConfig =
{
    .prescaler = 2U - 1U,
    .timeSegment1 = 5U - 1U,
    .timeSegment2 = 2U - 1U,
    .syncJumpWidth = 2U - 1U,
};
const cy_stc_canfd_transceiver_delay_compensation_t canfd_1_chan_2_tdcConfig =
{
    .tdcEnabled = true,
    .tdcOffset = 6U,
    .tdcFilterWindow = 6U,
};
const cy_stc_id_filter_t canfd_1_chan_2_stdIdFilter_0 =
{
    .sfid2 = 0U,
    .sfid1 = 0U,
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0,
    .sft = CY_CANFD_SFT_CLASSIC_FILTER,
};
const cy_stc_id_filter_t canfd_1_chan_2_stdIdFilters[] =
{
    canfd_1_chan_2_STD_ID_FILTER_0, 
};
const cy_stc_canfd_sid_filter_config_t canfd_1_chan_2_sidFiltersConfig =
{
    .numberOfSIDFilters = 1U,
    .sidFilter = canfd_1_chan_2_stdIdFilters,
};
const cy_stc_canfd_f0_t canfd_1_chan_2_extIdFilterF0Config_0 =
{
    .efid1 = 0U,
    .efec = CY_CANFD_EFEC_STORE_RX_FIFO_0,
};
const cy_stc_canfd_f1_t canfd_1_chan_2_extIdFilterF1Config_0 =
{
    .efid2 = 0U,
    .eft = CY_CANFD_EFT_CLASSIC_FILTER,
};
const cy_stc_extid_filter_t canfd_1_chan_2_extIdFilter_0 =
{
    .f0_f = &canfd_1_chan_2_extIdFilterF0Config_0,
    .f1_f = &canfd_1_chan_2_extIdFilterF1Config_0,
};
const cy_stc_extid_filter_t canfd_1_chan_2_extIdFilters[] =
{
    canfd_1_chan_2_EXT_ID_FILTER_0, 
};
const cy_stc_canfd_extid_filter_config_t canfd_1_chan_2_extIdFiltersConfig =
{
    .numberOfEXTIDFilters = 1U,
    .extidFilter = (cy_stc_extid_filter_t*)&canfd_1_chan_2_extIdFilters,
    .extIDANDMask = 536870911UL,
};
const cy_stc_canfd_global_filter_config_t canfd_1_chan_2_globalFilterConfig =
{
    .nonMatchingFramesStandard = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .nonMatchingFramesExtended = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .rejectRemoteFramesStandard = false,
    .rejectRemoteFramesExtended = false,
};
const cy_en_canfd_fifo_config_t canfd_1_chan_2_rxFifo0Config =
{
    .mode = CY_CANFD_FIFO_MODE_OVERWRITE,
    .watermark = 0U,
    .numberOfFIFOElements = 16U,
    .topPointerLogicEnabled = true,
};
const cy_en_canfd_fifo_config_t canfd_1_chan_2_rxFifo1Config =
{
    .mode = CY_CANFD_FIFO_MODE_BLOCKING,
    .watermark = 0U,
    .numberOfFIFOElements = 0U,
    .topPointerLogicEnabled = false,
};
const cy_stc_canfd_config_t canfd_1_chan_2_config =
{
    .txCallback = NULL,
    .rxCallback = NULL,
    .errorCallback = NULL,
    .canFDMode = true,
    .bitrate = &canfd_1_chan_2_nominalBitrateConfig,
    .fastBitrate = &canfd_1_chan_2_dataBitrateConfig,
    .tdcConfig = &canfd_1_chan_2_tdcConfig,
    .sidFilterConfig = &canfd_1_chan_2_sidFiltersConfig,
    .extidFilterConfig = &canfd_1_chan_2_extIdFiltersConfig,
    .globalFilterConfig = &canfd_1_chan_2_globalFilterConfig,
    .rxBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO1DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .txBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0Config = &canfd_1_chan_2_rxFifo0Config,
    .rxFIFO1Config = &canfd_1_chan_2_rxFifo1Config,
    .noOfRxBuffers = 0U,
    .noOfTxBuffers = 32U,
    .messageRAMaddress = CY_CAN1MRAM_BASE + 0U,
    .messageRAMsize = 8192U,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_0 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_1 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_2 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_3 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_4 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_5 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_6 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_7 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_8 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_9 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_10 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_11 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_12 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_13 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_14 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_15 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_16 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_17 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_18 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_19 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_20 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_21 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_22 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_23 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_24 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_25 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_26 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_27 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_28 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_29 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_30 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_2_T0RegisterBuffer_31 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_0 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_1 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_2 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_3 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_4 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_5 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_6 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_7 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_8 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_9 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_10 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_11 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_12 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_13 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_14 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_15 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_16 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_17 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_18 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_19 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_20 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_21 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_22 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_23 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_24 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_25 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_26 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_27 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_28 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_29 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_30 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_2_T1RegisterBuffer_31 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
uint32_t canfd_1_chan_2_dataBuffer_0[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_1[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_2[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_3[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_4[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_5[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_6[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_7[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_8[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_9[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_10[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_11[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_12[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_13[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_14[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_15[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_16[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_17[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_18[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_19[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_20[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_21[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_22[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_23[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_24[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_25[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_26[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_27[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_28[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_29[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_30[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_2_dataBuffer_31[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_0 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_0,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_0,
    .data_area_f = canfd_1_chan_2_dataBuffer_0,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_1 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_1,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_1,
    .data_area_f = canfd_1_chan_2_dataBuffer_1,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_2 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_2,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_2,
    .data_area_f = canfd_1_chan_2_dataBuffer_2,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_3 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_3,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_3,
    .data_area_f = canfd_1_chan_2_dataBuffer_3,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_4 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_4,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_4,
    .data_area_f = canfd_1_chan_2_dataBuffer_4,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_5 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_5,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_5,
    .data_area_f = canfd_1_chan_2_dataBuffer_5,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_6 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_6,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_6,
    .data_area_f = canfd_1_chan_2_dataBuffer_6,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_7 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_7,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_7,
    .data_area_f = canfd_1_chan_2_dataBuffer_7,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_8 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_8,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_8,
    .data_area_f = canfd_1_chan_2_dataBuffer_8,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_9 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_9,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_9,
    .data_area_f = canfd_1_chan_2_dataBuffer_9,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_10 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_10,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_10,
    .data_area_f = canfd_1_chan_2_dataBuffer_10,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_11 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_11,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_11,
    .data_area_f = canfd_1_chan_2_dataBuffer_11,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_12 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_12,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_12,
    .data_area_f = canfd_1_chan_2_dataBuffer_12,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_13 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_13,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_13,
    .data_area_f = canfd_1_chan_2_dataBuffer_13,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_14 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_14,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_14,
    .data_area_f = canfd_1_chan_2_dataBuffer_14,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_15 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_15,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_15,
    .data_area_f = canfd_1_chan_2_dataBuffer_15,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_16 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_16,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_16,
    .data_area_f = canfd_1_chan_2_dataBuffer_16,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_17 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_17,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_17,
    .data_area_f = canfd_1_chan_2_dataBuffer_17,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_18 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_18,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_18,
    .data_area_f = canfd_1_chan_2_dataBuffer_18,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_19 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_19,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_19,
    .data_area_f = canfd_1_chan_2_dataBuffer_19,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_20 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_20,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_20,
    .data_area_f = canfd_1_chan_2_dataBuffer_20,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_21 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_21,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_21,
    .data_area_f = canfd_1_chan_2_dataBuffer_21,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_22 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_22,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_22,
    .data_area_f = canfd_1_chan_2_dataBuffer_22,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_23 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_23,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_23,
    .data_area_f = canfd_1_chan_2_dataBuffer_23,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_24 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_24,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_24,
    .data_area_f = canfd_1_chan_2_dataBuffer_24,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_25 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_25,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_25,
    .data_area_f = canfd_1_chan_2_dataBuffer_25,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_26 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_26,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_26,
    .data_area_f = canfd_1_chan_2_dataBuffer_26,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_27 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_27,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_27,
    .data_area_f = canfd_1_chan_2_dataBuffer_27,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_28 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_28,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_28,
    .data_area_f = canfd_1_chan_2_dataBuffer_28,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_29 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_29,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_29,
    .data_area_f = canfd_1_chan_2_dataBuffer_29,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_30 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_30,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_30,
    .data_area_f = canfd_1_chan_2_dataBuffer_30,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_2_txBuffer_31 =
{
    .t0_f = &canfd_1_chan_2_T0RegisterBuffer_31,
    .t1_f = &canfd_1_chan_2_T1RegisterBuffer_31,
    .data_area_f = canfd_1_chan_2_dataBuffer_31,
};

#if defined (CY_USING_HAL)
const cyhal_resource_inst_t canfd_1_chan_2_obj =
{
    .type = CYHAL_RSC_CAN,
    .block_num = 1U,
    .channel_num = 2U,
};
#endif /* defined (CY_USING_HAL) */

const cy_stc_canfd_bitrate_t canfd_1_chan_3_nominalBitrateConfig =
{
    .prescaler = 4U - 1U,
    .timeSegment1 = 15U - 1U,
    .timeSegment2 = 4U - 1U,
    .syncJumpWidth = 4U - 1U,
};
const cy_stc_canfd_bitrate_t canfd_1_chan_3_dataBitrateConfig =
{
    .prescaler = 2U - 1U,
    .timeSegment1 = 5U - 1U,
    .timeSegment2 = 2U - 1U,
    .syncJumpWidth = 2U - 1U,
};
const cy_stc_canfd_transceiver_delay_compensation_t canfd_1_chan_3_tdcConfig =
{
    .tdcEnabled = true,
    .tdcOffset = 6U,
    .tdcFilterWindow = 6U,
};
const cy_stc_id_filter_t canfd_1_chan_3_stdIdFilter_0 =
{
    .sfid2 = 0U,
    .sfid1 = 0U,
    .sfec = CY_CANFD_SFEC_STORE_RX_FIFO_0,
    .sft = CY_CANFD_SFT_CLASSIC_FILTER,
};
const cy_stc_id_filter_t canfd_1_chan_3_stdIdFilters[] =
{
    canfd_1_chan_3_STD_ID_FILTER_0, 
};
const cy_stc_canfd_sid_filter_config_t canfd_1_chan_3_sidFiltersConfig =
{
    .numberOfSIDFilters = 1U,
    .sidFilter = canfd_1_chan_3_stdIdFilters,
};
const cy_stc_canfd_f0_t canfd_1_chan_3_extIdFilterF0Config_0 =
{
    .efid1 = 0U,
    .efec = CY_CANFD_EFEC_STORE_RX_FIFO_0,
};
const cy_stc_canfd_f1_t canfd_1_chan_3_extIdFilterF1Config_0 =
{
    .efid2 = 0U,
    .eft = CY_CANFD_EFT_CLASSIC_FILTER,
};
const cy_stc_extid_filter_t canfd_1_chan_3_extIdFilter_0 =
{
    .f0_f = &canfd_1_chan_3_extIdFilterF0Config_0,
    .f1_f = &canfd_1_chan_3_extIdFilterF1Config_0,
};
const cy_stc_extid_filter_t canfd_1_chan_3_extIdFilters[] =
{
    canfd_1_chan_3_EXT_ID_FILTER_0, 
};
const cy_stc_canfd_extid_filter_config_t canfd_1_chan_3_extIdFiltersConfig =
{
    .numberOfEXTIDFilters = 1U,
    .extidFilter = (cy_stc_extid_filter_t*)&canfd_1_chan_3_extIdFilters,
    .extIDANDMask = 536870911UL,
};
const cy_stc_canfd_global_filter_config_t canfd_1_chan_3_globalFilterConfig =
{
    .nonMatchingFramesStandard = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .nonMatchingFramesExtended = CY_CANFD_ACCEPT_IN_RXFIFO_0,
    .rejectRemoteFramesStandard = false,
    .rejectRemoteFramesExtended = false,
};
const cy_en_canfd_fifo_config_t canfd_1_chan_3_rxFifo0Config =
{
    .mode = CY_CANFD_FIFO_MODE_OVERWRITE,
    .watermark = 0U,
    .numberOfFIFOElements = 16U,
    .topPointerLogicEnabled = true,
};
const cy_en_canfd_fifo_config_t canfd_1_chan_3_rxFifo1Config =
{
    .mode = CY_CANFD_FIFO_MODE_BLOCKING,
    .watermark = 0U,
    .numberOfFIFOElements = 0U,
    .topPointerLogicEnabled = false,
};
const cy_stc_canfd_config_t canfd_1_chan_3_config =
{
    .txCallback = NULL,
    .rxCallback = NULL,
    .errorCallback = NULL,
    .canFDMode = true,
    .bitrate = &canfd_1_chan_3_nominalBitrateConfig,
    .fastBitrate = &canfd_1_chan_3_dataBitrateConfig,
    .tdcConfig = &canfd_1_chan_3_tdcConfig,
    .sidFilterConfig = &canfd_1_chan_3_sidFiltersConfig,
    .extidFilterConfig = &canfd_1_chan_3_extIdFiltersConfig,
    .globalFilterConfig = &canfd_1_chan_3_globalFilterConfig,
    .rxBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO1DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0DataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .txBufferDataSize = CY_CANFD_BUFFER_DATA_SIZE_64,
    .rxFIFO0Config = &canfd_1_chan_3_rxFifo0Config,
    .rxFIFO1Config = &canfd_1_chan_3_rxFifo1Config,
    .noOfRxBuffers = 0U,
    .noOfTxBuffers = 32U,
    .messageRAMaddress = CY_CAN1MRAM_BASE + 12288U,
    .messageRAMsize = 4096U,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_0 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_1 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_2 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_3 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_4 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_5 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_6 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_7 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_8 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_9 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_10 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_11 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_12 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_13 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_14 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_15 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_16 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_17 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_18 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_19 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_20 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_21 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_22 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_23 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_24 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_25 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_26 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_27 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_28 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_29 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_30 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t0_t canfd_1_chan_3_T0RegisterBuffer_31 =
{
    .id = 0U,
    .rtr = CY_CANFD_RTR_DATA_FRAME,
    .xtd = CY_CANFD_XTD_STANDARD_ID,
    .esi = CY_CANFD_ESI_ERROR_ACTIVE,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_0 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_1 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_2 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_3 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_4 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_5 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_6 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_7 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_8 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_9 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_10 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_11 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_12 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_13 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_14 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_15 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_16 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_17 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_18 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_19 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_20 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_21 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_22 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_23 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_24 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_25 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_26 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_27 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_28 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_29 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_30 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
cy_stc_canfd_t1_t canfd_1_chan_3_T1RegisterBuffer_31 =
{
    .dlc = 0U,
    .brs = false,
    .fdf = CY_CANFD_FDF_STANDARD_FRAME,
    .efc = false,
    .mm = 0U,
};
uint32_t canfd_1_chan_3_dataBuffer_0[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_1[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_2[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_3[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_4[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_5[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_6[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_7[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_8[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_9[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_10[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_11[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_12[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_13[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_14[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_15[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_16[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_17[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_18[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_19[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_20[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_21[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_22[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_23[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_24[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_25[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_26[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_27[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_28[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_29[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_30[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
uint32_t canfd_1_chan_3_dataBuffer_31[] =
{
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
    0U, 
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_0 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_0,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_0,
    .data_area_f = canfd_1_chan_3_dataBuffer_0,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_1 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_1,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_1,
    .data_area_f = canfd_1_chan_3_dataBuffer_1,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_2 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_2,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_2,
    .data_area_f = canfd_1_chan_3_dataBuffer_2,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_3 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_3,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_3,
    .data_area_f = canfd_1_chan_3_dataBuffer_3,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_4 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_4,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_4,
    .data_area_f = canfd_1_chan_3_dataBuffer_4,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_5 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_5,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_5,
    .data_area_f = canfd_1_chan_3_dataBuffer_5,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_6 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_6,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_6,
    .data_area_f = canfd_1_chan_3_dataBuffer_6,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_7 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_7,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_7,
    .data_area_f = canfd_1_chan_3_dataBuffer_7,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_8 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_8,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_8,
    .data_area_f = canfd_1_chan_3_dataBuffer_8,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_9 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_9,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_9,
    .data_area_f = canfd_1_chan_3_dataBuffer_9,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_10 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_10,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_10,
    .data_area_f = canfd_1_chan_3_dataBuffer_10,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_11 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_11,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_11,
    .data_area_f = canfd_1_chan_3_dataBuffer_11,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_12 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_12,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_12,
    .data_area_f = canfd_1_chan_3_dataBuffer_12,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_13 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_13,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_13,
    .data_area_f = canfd_1_chan_3_dataBuffer_13,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_14 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_14,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_14,
    .data_area_f = canfd_1_chan_3_dataBuffer_14,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_15 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_15,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_15,
    .data_area_f = canfd_1_chan_3_dataBuffer_15,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_16 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_16,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_16,
    .data_area_f = canfd_1_chan_3_dataBuffer_16,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_17 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_17,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_17,
    .data_area_f = canfd_1_chan_3_dataBuffer_17,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_18 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_18,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_18,
    .data_area_f = canfd_1_chan_3_dataBuffer_18,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_19 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_19,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_19,
    .data_area_f = canfd_1_chan_3_dataBuffer_19,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_20 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_20,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_20,
    .data_area_f = canfd_1_chan_3_dataBuffer_20,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_21 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_21,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_21,
    .data_area_f = canfd_1_chan_3_dataBuffer_21,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_22 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_22,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_22,
    .data_area_f = canfd_1_chan_3_dataBuffer_22,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_23 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_23,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_23,
    .data_area_f = canfd_1_chan_3_dataBuffer_23,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_24 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_24,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_24,
    .data_area_f = canfd_1_chan_3_dataBuffer_24,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_25 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_25,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_25,
    .data_area_f = canfd_1_chan_3_dataBuffer_25,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_26 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_26,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_26,
    .data_area_f = canfd_1_chan_3_dataBuffer_26,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_27 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_27,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_27,
    .data_area_f = canfd_1_chan_3_dataBuffer_27,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_28 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_28,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_28,
    .data_area_f = canfd_1_chan_3_dataBuffer_28,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_29 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_29,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_29,
    .data_area_f = canfd_1_chan_3_dataBuffer_29,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_30 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_30,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_30,
    .data_area_f = canfd_1_chan_3_dataBuffer_30,
};
cy_stc_canfd_tx_buffer_t canfd_1_chan_3_txBuffer_31 =
{
    .t0_f = &canfd_1_chan_3_T0RegisterBuffer_31,
    .t1_f = &canfd_1_chan_3_T1RegisterBuffer_31,
    .data_area_f = canfd_1_chan_3_dataBuffer_31,
};

#if defined (CY_USING_HAL)
const cyhal_resource_inst_t canfd_1_chan_3_obj =
{
    .type = CYHAL_RSC_CAN,
    .block_num = 1U,
    .channel_num = 3U,
};
#endif /* defined (CY_USING_HAL) */

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
    .rxFifoTriggerLevel = 63UL,
    .rxFifoIntEnableMask = 0UL,
    .txFifoTriggerLevel = 63UL,
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
    .tx_pin = 1,
#if defined (COMPONENT_MW_ASYNC_TRANSFER)
    .rts_pin = 0xFF,
#endif /* defined (COMPONENT_MW_ASYNC_TRANSFER) */
    .tx_port = 0,
#if defined (COMPONENT_MW_ASYNC_TRANSFER)
    .rts_port = 0xFF,
    .rts_enable = 0UL,
#endif /* defined (COMPONENT_MW_ASYNC_TRANSFER) */
};
#endif /* defined (COMPONENT_MTB_HAL) && (MTB_HAL_DRIVER_AVAILABLE_UART) */

void init_cycfg_peripherals(void)
{
#if defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE)
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_CANFD0_PERI_NR , CY_MMIO_CANFD0_GROUP_NR, CY_MMIO_CANFD0_SLAVE_NR, CY_MMIO_CANFD0_CLK_HF_NR);
#endif /* defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE) */
    Cy_SysClk_PeriPclkAssignDivider(PCLK_CANFD0_CLOCK_CAN0, CY_SYSCLK_DIV_8_BIT, 0U);
#if defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE)
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_CANFD0_PERI_NR , CY_MMIO_CANFD0_GROUP_NR, CY_MMIO_CANFD0_SLAVE_NR, CY_MMIO_CANFD0_CLK_HF_NR);
#endif /* defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE) */
    Cy_SysClk_PeriPclkAssignDivider(PCLK_CANFD0_CLOCK_CAN1, CY_SYSCLK_DIV_8_BIT, 0U);
#if defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE)
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_CANFD0_PERI_NR , CY_MMIO_CANFD0_GROUP_NR, CY_MMIO_CANFD0_SLAVE_NR, CY_MMIO_CANFD0_CLK_HF_NR);
#endif /* defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE) */
    Cy_SysClk_PeriPclkAssignDivider(PCLK_CANFD0_CLOCK_CAN2, CY_SYSCLK_DIV_8_BIT, 0U);
#if defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE)
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_CANFD0_PERI_NR , CY_MMIO_CANFD0_GROUP_NR, CY_MMIO_CANFD0_SLAVE_NR, CY_MMIO_CANFD0_CLK_HF_NR);
#endif /* defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE) */
    Cy_SysClk_PeriPclkAssignDivider(PCLK_CANFD0_CLOCK_CAN3, CY_SYSCLK_DIV_8_BIT, 0U);
#if defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE)
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_CANFD0_PERI_NR , CY_MMIO_CANFD0_GROUP_NR, CY_MMIO_CANFD0_SLAVE_NR, CY_MMIO_CANFD0_CLK_HF_NR);
#endif /* defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE) */
    Cy_SysClk_PeriPclkAssignDivider(PCLK_CANFD1_CLOCK_CAN0, CY_SYSCLK_DIV_8_BIT, 1U);
#if defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE)
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_CANFD0_PERI_NR , CY_MMIO_CANFD0_GROUP_NR, CY_MMIO_CANFD0_SLAVE_NR, CY_MMIO_CANFD0_CLK_HF_NR);
#endif /* defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE) */
    Cy_SysClk_PeriPclkAssignDivider(PCLK_CANFD1_CLOCK_CAN1, CY_SYSCLK_DIV_8_BIT, 1U);
#if defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE)
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_CANFD0_PERI_NR , CY_MMIO_CANFD0_GROUP_NR, CY_MMIO_CANFD0_SLAVE_NR, CY_MMIO_CANFD0_CLK_HF_NR);
#endif /* defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE) */
    Cy_SysClk_PeriPclkAssignDivider(PCLK_CANFD1_CLOCK_CAN2, CY_SYSCLK_DIV_8_BIT, 1U);
#if defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE)
    Cy_SysClk_PeriGroupSlaveInit(CY_MMIO_CANFD0_PERI_NR , CY_MMIO_CANFD0_GROUP_NR, CY_MMIO_CANFD0_SLAVE_NR, CY_MMIO_CANFD0_CLK_HF_NR);
#endif /* defined (CY_DEVICE_CONFIGURATOR_IP_ENABLE_FEATURE) */
    Cy_SysClk_PeriPclkAssignDivider(PCLK_CANFD1_CLOCK_CAN3, CY_SYSCLK_DIV_8_BIT, 1U);
    Cy_SysClk_PeriPclkAssignDivider(PCLK_SCB0_CLOCK, CY_SYSCLK_DIV_24_5_BIT, 0U);
}
void reserve_cycfg_peripherals(void)
{
#if defined (CY_USING_HAL)
    cyhal_hwmgr_reserve(&canfd_0_chan_0_obj);
    cyhal_hwmgr_reserve(&canfd_0_chan_1_obj);
    cyhal_hwmgr_reserve(&canfd_0_chan_2_obj);
    cyhal_hwmgr_reserve(&canfd_0_chan_3_obj);
    cyhal_hwmgr_reserve(&canfd_1_chan_0_obj);
    cyhal_hwmgr_reserve(&canfd_1_chan_1_obj);
    cyhal_hwmgr_reserve(&canfd_1_chan_2_obj);
    cyhal_hwmgr_reserve(&canfd_1_chan_3_obj);
    cyhal_hwmgr_reserve(&scb_0_obj);
#endif /* defined (CY_USING_HAL) */
}
