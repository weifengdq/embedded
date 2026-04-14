#ifndef APP_NETWORK_H
#define APP_NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lwip/netif.h"
#include "stm32f4xx_hal.h"

void App_Network_SetUart(UART_HandleTypeDef *huart);
void App_Network_Init(void);
void App_Network_OnLinkStatusChanged(struct netif *netif);
int debug_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif