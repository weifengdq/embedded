/**
 * \file IfxAp_cfg.c
 * \brief AP on-chip implementation data
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
 */

/******************************************************************************/
/*----------------------------------Includes----------------------------------*/
/******************************************************************************/

#include "IfxAp_cfg.h"

/******************************************************************************/
/*-----------------------Exported Variables/Constants-------------------------*/
/******************************************************************************/
#if defined(DEVICE_TC4DX)


IFX_CONST IfxAp_memoryRegion IfxAp_dlmuRegions[IFXAP_NUM_DLMU] = {
    {(void *)&MODULE_CPU0, (void *)0xB0000000, (void *)0xB0080000},    /**< DLMU0 */
    {(void *)&MODULE_CPU1, (void *)0xB0080000, (void *)0xB0100000},    /**< DLMU1 */
    {(void *)&MODULE_CPU2, (void *)0xB0100000, (void *)0xB0180000},    /**< DLMU2 */
    {(void *)&MODULE_CPU3, (void *)0xB0180000, (void *)0xB0200000},    /**< DLMU3 */
    {(void *)&MODULE_CPU4, (void *)0xB0200000, (void *)0xB0280000},    /**< DLMU4 */
    {(void *)&MODULE_CPU5, (void *)0xB0280000, (void *)0xB0300000},    /**< DLMU5 */
};
#endif /* #if defined(DEVICE_TC4DX) */

#if defined(DEVICE_TC4DX)

IFX_CONST IfxAp_memoryRegion IfxAp_dsprRegions[IFXAP_NUM_DSPR] = {
    {(void *)&MODULE_CPU0, (void *)0x70000000, (void *)0x7003C000},    /**< DSPR0 */
    {(void *)&MODULE_CPU1, (void *)0x60000000, (void *)0x6003C000},    /**< DSPR1 */
    {(void *)&MODULE_CPU2, (void *)0x50000000, (void *)0x5003C000},    /**< DSPR2 */
    {(void *)&MODULE_CPU3, (void *)0x40000000, (void *)0x4003C000},    /**< DSPR3 */
    {(void *)&MODULE_CPU4, (void *)0x30000000, (void *)0x3003C000},    /**< DSPR4 */
    {(void *)&MODULE_CPU5, (void *)0x20000000, (void *)0x2003C000},    /**< DSPR5 */
};
#endif /* #if defined(DEVICE_TC4DX) */

#if defined(DEVICE_TC4DX)
IFX_CONST IfxAp_memoryRegion IfxAp_lmuRegions[IFXAP_NUM_LMU] = {
    {(void *)&MODULE_LMU0, (void *)0xB0400000, (void *)0xB0480000},    /**< LMU0 */
    {(void *)&MODULE_LMU1, (void *)0xB0480000, (void *)0xB0500000},    /**< LMU1 */
    {(void *)&MODULE_LMU2, (void *)0xB0500000, (void *)0xB0580000},    /**< LMU2 */
    {(void *)&MODULE_LMU3, (void *)0xB0580000, (void *)0xB0600000},    /**< LMU3 */
    {(void *)&MODULE_LMU4, (void *)0xB0600000, (void *)0xB0680000},    /**< LMU4 */
    {(void *)&MODULE_LMU5, (void *)0xB0680000, (void *)0xB0700000},    /**< LMU5 */
    {(void *)&MODULE_LMU6, (void *)0xB0700000, (void *)0xB0780000},    /**< LMU6 */
    {(void *)&MODULE_LMU7, (void *)0xB0780000, (void *)0xB0800000},    /**< LMU7 */
    {(void *)&MODULE_LMU8, (void *)0xB0800000, (void *)0xB0880000},    /**< LMU8 */
    {(void *)&MODULE_LMU9, (void *)0xB0880000, (void *)0xB0900000},    /**< LMU9 */
};
#endif /* #if defined(DEVICE_TC4DX) */

#if defined(DEVICE_TC4DX)
IFX_CONST IfxAp_memoryRegion IfxAp_psprRegions[IFXAP_NUM_PSPR] = {
    {(void *)&MODULE_CPU0, (void *)0x70100000, (void *)0x70110000},    /**< PSPR0 */
    {(void *)&MODULE_CPU1, (void *)0x60100000, (void *)0x60110000},    /**< PSPR1 */
    {(void *)&MODULE_CPU2, (void *)0x50100000, (void *)0x50110000},    /**< PSPR2 */
    {(void *)&MODULE_CPU3, (void *)0x40100000, (void *)0x40110000},    /**< PSPR3 */
    {(void *)&MODULE_CPU4, (void *)0x30100000, (void *)0x30110000},    /**< PSPR4 */
    {(void *)&MODULE_CPU5, (void *)0x20100000, (void *)0x20110000},    /**< PSPR5 */
};
#endif /* #if defined(DEVICE_TC4DX) */
