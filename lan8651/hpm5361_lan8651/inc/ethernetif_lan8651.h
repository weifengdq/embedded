#ifndef ETHERNETIF_LAN8651_H
#define ETHERNETIF_LAN8651_H

#include <stdint.h>

#include "lwip/err.h"
#include "lwip/netif.h"

err_t lan8651_ethernetif_init(struct netif *netif);
void lan8651_ethernetif_input(struct netif *netif, uint32_t budget);

#endif