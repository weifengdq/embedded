/**
 * \file IfxDma_cfg.h
 * \brief DMA on-chip implementation data
 * \ingroup IfxLld_Dma
 *
 * \version iLLD-TC4-v2.6.0
 * \copyright Copyright (c) 2026 Infineon Technologies AG. All rights reserved.
 *
 *
 *
 *                                 IMPORTANT NOTICE
 *
 * Infineon Technologies AG (Infineon) licenses this file to you under the
 * Infineon Automotive SW Lab License v2025-01 (IFASLL). You may not use
 * this file except in compliance with IFASLL.
 *
 * The full license text is contained in IFASLL202501.pdf delivered with this SW.
 * Unless required by applicable law or agreed to in writing, software distributed
 * under this license is distributed "AS IS" without any warranty or liability of any
 * kind and Infineon hereby expressly disclaims any warranties or representations,
 * whether express, implied, statutory or otherwise, including but not limited to
 * warranties of workmanship, merchantability, fitness for a particular purpose,
 * defects in the licensed items, or non-infringement of third parties'
 * intellectual property rights. See the full license text for the specific
 * language governing permissions and limitations under IFASLL.
 *
 *
 * \defgroup IfxLld_Dma DMA
 * \ingroup IfxLld
 * \defgroup IfxLld_Dma_Impl Implementation
 * \ingroup IfxLld_Dma
 * \defgroup IfxLld_Dma_Std Standard Driver
 * \ingroup IfxLld_Dma
 */

#ifndef IFXDMA_CFG_H
#define IFXDMA_CFG_H 1

/******************************************************************************/
/*----------------------------------Includes----------------------------------*/
/******************************************************************************/

#include "Ifx_Cfg.h"

#if defined DEVICE_TC4DX
#include "TC4Dx/IfxDma_cfg_TC4Dx.h"
#endif

#endif /* IFXDMA_CFG_H */
