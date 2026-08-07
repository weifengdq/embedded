/**
 * \file IfxCanxl_PinMap.h
 * \brief CANXL  details
 * \ingroup IfxLld_Canxl
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
 *
 * \defgroup IfxLld_Canxl_Pinmap Canxl Pinmap Structure
 * \ingroup IfxLld_Canxl
 */

#ifndef IFXCANXL_PINMAP_H
#define IFXCANXL_PINMAP_H 1

/******************************************************************************/
/*----------------------------------Includes----------------------------------*/
/******************************************************************************/

#include "_Impl/IfxCanxl_cfg.h"
#include "Ifx_PinMap.h"
#include "IfxCanxl_reg.h"
#include "Port/Std/IfxPort.h"

/******************************************************************************/
/*-----------------------------Data Structures--------------------------------*/
/******************************************************************************/

/** \brief
 */
typedef const struct
{
    Ifx_CANXL      *module;       /**< \brief Base address */
    IfxCanxl_NodeId nodeId;       /**< \brief Node ID */
    IfxPort_Pin     pin;          /**< \brief Port pin */
    Ifx_RxSel       select;       /**< \brief Input multiplexer value */
} IfxCanxl_Rxd_In;

/** \brief
 */
typedef const struct
{
    Ifx_CANXL        *module;       /**< \brief Base address */
    IfxCanxl_NodeId   nodeId;       /**< \brief Node ID */
    IfxPort_Pin       pin;          /**< \brief Port pin */
    IfxPort_OutputIdx select;       /**< \brief Port control code */
} IfxCanxl_Txd_Out;

/******************************************************************************/
/*----------------------------------Includes----------------------------------*/
/******************************************************************************/

#if defined(DEVICE_TC4DX)
#if defined(IFX_PIN_PACKAGE_BGA436_COM)
#include "TC4Dx/IfxCanxl_PinMap_TC4Dx_BGA436_COM.h"
#elif defined(IFX_PIN_PACKAGE_BGA292_COM)
#include "TC4Dx/IfxCanxl_PinMap_TC4Dx_BGA292_COM.h"
#endif

#endif /* #if defined(DEVICE_TC4DX) */

#endif /* IFXCANXL_PINMAP_H */
