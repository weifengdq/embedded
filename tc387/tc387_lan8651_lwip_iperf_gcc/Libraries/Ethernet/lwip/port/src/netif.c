/**
 * \file netif.c
 * \brief LAN8651 based lwIP netif glue for TC387
 */

#include "lwip/opt.h"

#include "Ifx_Lwip.h"
#include "Ifx_Netif.h"
#include "ethernetif_lan8651.h"

#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "netif/etharp.h"

err_t ifx_netif_input(netif_t *netif)
{
    lan8651_ethernetif_input(netif, 32U);
    return ERR_OK;
}

err_t ifx_netif_init(netif_t *netif)
{
    lan8651_t *ethernetif;

    LWIP_ASSERT("netif != NULL", (netif != NULL));

    ethernetif = IfxLan8651_get();

    if (ethernetif == NULL_PTR)
    {
        return ERR_MEM;
    }

#if LWIP_NETIF_HOSTNAME
    netif->hostname = "lwip";
#endif

    netif->state = ethernetif;
    return lan8651_ethernetif_init(netif);
}
