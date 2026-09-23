/**********************************************************************************************************************
 * \file Configuration.h
 * \copyright Copyright (C) Infineon Technologies AG 2019
 *********************************************************************************************************************/

#ifndef CONFIGURATION_H
#define CONFIGURATION_H
#include "Ifx_Cfg.h"
#include "ConfigurationIsr.h"

#define IFX_CFG_STM_TICKS_PER_MS    (100000)                /* 100MHz STM, 100k ticks per ms */

#define CPU_WHICH_SERVICE_ETHERNET  0                       /* CPU0 services LAN8651 + lwIP */

/* LAN8651 (10BASE-T1S over TC6 SPI4) configuration.
 * TC397XX 292pin: MOSI P22.0 / nCS P22.2 (SLSO3) / SCLK P22.3 / MISO P33.13,
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
