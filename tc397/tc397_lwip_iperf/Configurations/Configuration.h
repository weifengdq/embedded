/**********************************************************************************************************************
 * \file Configuration.h
 * \copyright Copyright (C) Infineon Technologies AG 2019
 *********************************************************************************************************************/

#ifndef CONFIGURATION_H
#define CONFIGURATION_H
/*********************************************************************************************************************/
/*-----------------------------------------------------Includes------------------------------------------------------*/
/*********************************************************************************************************************/
#include "Ifx_Cfg.h"
#include <_PinMap/IfxGeth_PinMap.h>
#include "ConfigurationIsr.h"

/*********************************************************************************************************************/
/*------------------------------------------------------Macros-------------------------------------------------------*/
/*********************************************************************************************************************/
/* Pins for YT8011AN RGMII connection (TC397XX 292pin, P11.x + P12.x).
 * Symbol names verified against TC39xB LFBGA292 PinMap. P11.5 = GREFCLK 125MHz active crystal. */
#define ETH_GREFCLK_PIN             IfxGeth_GREFCLK_P11_5_IN
#define ETH_RXCTL_PIN               IfxGeth_RXCTLA_P11_11_IN
#define ETH_RXCLK_PIN               IfxGeth_RXCLKA_P11_12_IN
#define ETH_RXD0_PIN                IfxGeth_RXD0A_P11_10_IN
#define ETH_RXD1_PIN                IfxGeth_RXD1A_P11_9_IN
#define ETH_RXD2_PIN                IfxGeth_RXD2A_P11_8_IN
#define ETH_RXD3_PIN                IfxGeth_RXD3A_P11_7_IN
#define ETH_MDC_PIN                 IfxGeth_MDC_P12_0_OUT
#define ETH_MDIO_PIN                IfxGeth_MDIO_P12_1_INOUT
#define ETH_TXD0_PIN                IfxGeth_TXD0_P11_3_OUT
#define ETH_TXD1_PIN                IfxGeth_TXD1_P11_2_OUT
#define ETH_TXD2_PIN                IfxGeth_TXD2_P11_1_OUT
#define ETH_TXD3_PIN                IfxGeth_TXD3_P11_0_OUT
#define ETH_TXCTL_PIN               IfxGeth_TXCTL_P11_6_OUT
#define ETH_TXCLK_PIN               IfxGeth_TXCLK_P11_4_OUT

/* YT8011AN board control pins */
#define YT8011_NRST_PORT            (&MODULE_P20)
#define YT8011_NRST_PIN             1
#define YT8011_NINT_PORT            (&MODULE_P10)
#define YT8011_NINT_PIN             8

#define IFX_CFG_STM_TICKS_PER_MS    (100000)                /* Value of the system timer in ticks per millisecond   */

#define CPU_WHICH_SERVICE_ETHERNET  0                       /* Define the CPU which services the Ethernet           */

#endif
