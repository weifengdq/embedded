#ifndef DRE_CAN_ETH_BRIDGE_H
#define DRE_CAN_ETH_BRIDGE_H

#include "Ifx_Types.h"

void DreCanEthBridge_init(const uint8 *macAddress);
void DreCanEthBridge_poll(void);

#endif