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

/*********************************************************************************************************************/
/*------------------------------------LAN8651 10BASE-T1S (QSPI4, TC6 over SPI)--------------------------------------*/
/*********************************************************************************************************************/
/* TC397XX 292pin: MOSI P22.0 / nCS P22.2 (SLSO3) / SCLK P22.3 / MISO P33.13,
 * nRST P23.4 (GPIO out), nINT P33.7 (GPIO in, pull-up). */
#define LAN8651_RST_PORT             (&MODULE_P23)
#define LAN8651_RST_PIN              (4)
#define LAN8651_INT_PORT             (&MODULE_P33)
#define LAN8651_INT_PIN              (7)

#define LAN8651_SPI_BAUDRATE         (20000000U)
#define LAN8651_FORCE_LINK_UP        (1U)
#define LAN8651_PLCA_ENABLE          (1U)
#define LAN8651_PLCA_NODE_ID         (1U)
#define LAN8651_PLCA_NODE_COUNT      (8U)

#define LAN8651_IP_ADDR0             (192)
#define LAN8651_IP_ADDR1             (168)
#define LAN8651_IP_ADDR2             (1)
#define LAN8651_IP_ADDR3             (100)

#define LAN8651_NETMASK0             (255)
#define LAN8651_NETMASK1             (255)
#define LAN8651_NETMASK2             (255)
#define LAN8651_NETMASK3             (0)

#define LAN8651_GATEWAY0             (192)
#define LAN8651_GATEWAY1             (168)
#define LAN8651_GATEWAY2             (1)
#define LAN8651_GATEWAY3             (1)

#define LAN8651_MAC0                 (0x02U)
#define LAN8651_MAC1                 (0x00U)
#define LAN8651_MAC2                 (0x00U)
#define LAN8651_MAC3                 (0x10U)
#define LAN8651_MAC4                 (0xBAU)
#define LAN8651_MAC5                 (0x5EU)

#endif
