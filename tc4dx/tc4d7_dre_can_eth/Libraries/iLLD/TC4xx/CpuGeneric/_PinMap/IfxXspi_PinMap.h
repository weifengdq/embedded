/**
 * \file IfxXspi_PinMap.h
 * \brief XSPI  details
 * \ingroup IfxLld_Xspi
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
 * \defgroup IfxLld_Xspi_Pinmap Xspi Pinmap Structure
 * \ingroup IfxLld_Xspi
 */

#ifndef IFXXSPI_PINMAP_H
#define IFXXSPI_PINMAP_H 1

/******************************************************************************/
/*----------------------------------Includes----------------------------------*/
/******************************************************************************/

#include "Ifx_PinMap.h"
#include "IfxXspi_reg.h"
#include "Port/Std/IfxPort.h"

/******************************************************************************/
/*-----------------------------Data Structures--------------------------------*/
/******************************************************************************/

/** \brief
 */
typedef const struct
{
    Ifx_XSPI         *module;       /**< \brief Base address */
    IfxPort_Pin       pin;          /**< \brief Port pin */
    IfxPort_OutputIdx select;       /**< \brief Port control code */
} IfxXspi_Clk_Out;

/** \brief
 */
typedef const struct
{
    Ifx_XSPI         *module;       /**< \brief Base address */
    IfxPort_Pin       pin;          /**< \brief Port pin */
    IfxPort_OutputIdx select;       /**< \brief Port control code */
} IfxXspi_Cs_Out;

/** \brief
 */
typedef const struct
{
    Ifx_XSPI         *module;          /**< \brief Base address */
    IfxPort_Pin       pin;             /**< \brief Port pin */
    Ifx_RxSel         inSelect;        /**< \brief Input multiplexer value */
    IfxPort_OutputIdx outSelect;       /**< \brief Port control code */
} IfxXspi_Dm_InOut;

/** \brief
 */
typedef const struct
{
    Ifx_XSPI         *module;          /**< \brief Base address */
    IfxPort_Pin       pin;             /**< \brief Port pin */
    Ifx_RxSel         inSelect;        /**< \brief Input multiplexer value */
    IfxPort_OutputIdx outSelect;       /**< \brief Port control code */
} IfxXspi_Txd0_InOut;

/** \brief
 */
typedef const struct
{
    Ifx_XSPI         *module;          /**< \brief Base address */
    IfxPort_Pin       pin;             /**< \brief Port pin */
    Ifx_RxSel         inSelect;        /**< \brief Input multiplexer value */
    IfxPort_OutputIdx outSelect;       /**< \brief Port control code */
} IfxXspi_Txd1_InOut;

/** \brief
 */
typedef const struct
{
    Ifx_XSPI         *module;          /**< \brief Base address */
    IfxPort_Pin       pin;             /**< \brief Port pin */
    Ifx_RxSel         inSelect;        /**< \brief Input multiplexer value */
    IfxPort_OutputIdx outSelect;       /**< \brief Port control code */
} IfxXspi_Txd2_InOut;

/** \brief
 */
typedef const struct
{
    Ifx_XSPI         *module;          /**< \brief Base address */
    IfxPort_Pin       pin;             /**< \brief Port pin */
    Ifx_RxSel         inSelect;        /**< \brief Input multiplexer value */
    IfxPort_OutputIdx outSelect;       /**< \brief Port control code */
} IfxXspi_Txd3_InOut;

/** \brief
 */
typedef const struct
{
    Ifx_XSPI         *module;          /**< \brief Base address */
    IfxPort_Pin       pin;             /**< \brief Port pin */
    Ifx_RxSel         inSelect;        /**< \brief Input multiplexer value */
    IfxPort_OutputIdx outSelect;       /**< \brief Port control code */
} IfxXspi_Txd4_InOut;

/** \brief
 */
typedef const struct
{
    Ifx_XSPI         *module;          /**< \brief Base address */
    IfxPort_Pin       pin;             /**< \brief Port pin */
    Ifx_RxSel         inSelect;        /**< \brief Input multiplexer value */
    IfxPort_OutputIdx outSelect;       /**< \brief Port control code */
} IfxXspi_Txd5_InOut;

/** \brief
 */
typedef const struct
{
    Ifx_XSPI         *module;          /**< \brief Base address */
    IfxPort_Pin       pin;             /**< \brief Port pin */
    Ifx_RxSel         inSelect;        /**< \brief Input multiplexer value */
    IfxPort_OutputIdx outSelect;       /**< \brief Port control code */
} IfxXspi_Txd6_InOut;

/** \brief
 */
typedef const struct
{
    Ifx_XSPI         *module;          /**< \brief Base address */
    IfxPort_Pin       pin;             /**< \brief Port pin */
    Ifx_RxSel         inSelect;        /**< \brief Input multiplexer value */
    IfxPort_OutputIdx outSelect;       /**< \brief Port control code */
} IfxXspi_Txd7_InOut;

/******************************************************************************/
/*----------------------------------Includes----------------------------------*/
/******************************************************************************/

#if defined(DEVICE_TC4DX)
#if defined(IFX_PIN_PACKAGE_BGA436_COM)
#include "TC4Dx/IfxXspi_PinMap_TC4Dx_BGA436_COM.h"
#elif defined(IFX_PIN_PACKAGE_BGA292_COM)
#include "TC4Dx/IfxXspi_PinMap_TC4Dx_BGA292_COM.h"
#endif

#endif /* #if defined(DEVICE_TC4DX) */

#endif /* IFXXSPI_PINMAP_H */
