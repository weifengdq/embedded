/**
 * \file Ifx_PinMap.h
 * \brief Pinmap configuration file.
 *
 * \version iLLD-TC4-v2.6.0
 * \copyright Copyright (c) 2026 Infineon Technologies AG. All rights reserved.
 *
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
 */

#ifndef IFX_PINAMP_H
#define IFX_PINMAP_H 1

/******************************************************************************/
/*----------------------------------Includes----------------------------------*/
/******************************************************************************/

#include "Ifx_Cfg.h"

/******************************************************************************/
/*-----------------------------------Macros-----------------------------------*/
/******************************************************************************/

#if defined(DEVICE_TC4DX)
/** \brief Default Pin Package for TC4Dx 
 */
#if !defined(IFX_PIN_PACKAGE_BGA436_COM) && !defined(IFX_PIN_PACKAGE_BGA292_COM)
#define IFX_PIN_PACKAGE_BGA436_COM
#endif

#else
#error "TC4xx device configuration is missing or incorrect. Please define a valid TC4xx device macro in Ifx_Cfg.h."
#endif /* #if defined(DEVICE_TC4DX) */

#endif /* IFX_PINMAP_H */
