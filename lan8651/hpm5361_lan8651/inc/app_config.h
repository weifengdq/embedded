#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "board.h"
#include "hpm_iomux.h"

#define APP_PROJECT_NAME                 "hpm5361_lan8651"

#define APP_IP_ADDR0                     192
#define APP_IP_ADDR1                     168
#define APP_IP_ADDR2                     1
#define APP_IP_ADDR3                     109

#define APP_NETMASK_ADDR0                255
#define APP_NETMASK_ADDR1                255
#define APP_NETMASK_ADDR2                255
#define APP_NETMASK_ADDR3                0

#define APP_GW_ADDR0                     192
#define APP_GW_ADDR1                     168
#define APP_GW_ADDR2                     1
#define APP_GW_ADDR3                     1

#define APP_UDP_ECHO_PORT                9U
#define APP_IPERF_PORT                   5009U

#define APP_LAN8651_PLCA_ENABLE          1U
#define APP_LAN8651_PLCA_NODE_ID         2U
#define APP_LAN8651_PLCA_NODE_COUNT      8U
#define APP_LAN8651_PLCA_BURST_COUNT     5U
#define APP_LAN8651_PLCA_BURST_TIMER     128U

#define APP_LAN8651_SPI_INIT_FREQ        1000000U
#define APP_LAN8651_SPI_RUNTIME_FREQ     20000000U
#define APP_LAN8651_SPI_DEBUG_BLOCKING   0U

#define APP_SYS_TICK_PERIOD_MS           1U
#define APP_STATUS_LED_PERIOD_MS         100U

#define APP_LAN8651_BRINGUP_PROFILE      0U

#if APP_LAN8651_BRINGUP_PROFILE
#define APP_LAN8651_ENABLE_DIAG_LOG      1U
#define APP_LAN8651_DIAG_LOG_PERIOD_MS   1000U
#define APP_LAN8651_FRAME_LOG_LIMIT      12U
#define APP_LAN8651_ENABLE_RX_OK_LOG     1U
#define APP_LAN8651_TX_CHUNK_LOG_LIMIT   8U
#define APP_LAN8651_RAW_SPI_LOG_ENABLE   0U
#else
#define APP_LAN8651_ENABLE_DIAG_LOG      0U
#define APP_LAN8651_DIAG_LOG_PERIOD_MS   5000U
#define APP_LAN8651_FRAME_LOG_LIMIT      4U
#define APP_LAN8651_ENABLE_RX_OK_LOG     0U
#define APP_LAN8651_TX_CHUNK_LOG_LIMIT   0U
#define APP_LAN8651_RAW_SPI_LOG_ENABLE   0U
#endif

#define APP_LAN8651_RESET_PAD            IOC_PAD_PA10
#define APP_LAN8651_IRQ_PAD              IOC_PAD_PA12
#define APP_LAN8651_RESET_FUNC           IOC_PA10_FUNC_CTL_GPIO_A_10
#define APP_LAN8651_IRQ_FUNC             IOC_PA12_FUNC_CTL_GPIO_A_12

#define APP_LAN8651_MAC0                 0x02U
#define APP_LAN8651_MAC1                 0x00U
#define APP_LAN8651_MAC2                 0x00U
#define APP_LAN8651_MAC3                 0x10U
#define APP_LAN8651_MAC4                 0xBAU
#define APP_LAN8651_MAC5                 0x6DU

#define APP_LAN8651_RX_POLL_BUDGET       32U

#endif