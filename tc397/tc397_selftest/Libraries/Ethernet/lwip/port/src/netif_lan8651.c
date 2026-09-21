/**
 * \file netif_lan8651.c
 * \brief Second lwIP netif for the LAN8651 10BASE-T1S transceiver (QSPI4).
 *
 * The combined tc397_selftest firmware runs one lwIP instance with two netifs:
 *
 *   en0  GETH + YT8011AN (1000BASE-T1)  192.168.0.100/24  -> netif.c (original port)
 *   t10  LAN8651        (10BASE-T1S)    192.168.1.100/24  -> this file
 *
 * This file only holds the glue: hardware bring-up, netif registration, the
 * link-state poll and the RX drain. The low-level frame path lives in
 * ethernetif_lan8651.c and the SPI/TC6 driver in Libraries/LAN8651.
 *
 * NOTE: the LAN8651 driver is interrupt (QSPI4 ISR) driven, so nothing in this
 * file may run with interrupts disabled. IfxLan8651_poll() is therefore called
 * from Ifx_Lwip_pollReceiveFlags() and NOT from Ifx_Lwip_pollTimerFlags()
 * (which runs the TCP timers inside a critical section).
 */
#include "lwip/opt.h"

#include "Ifx_Lwip.h"
#include "Ifx_Netif.h"
#include "ethernetif_lan8651.h"
#include "lan8651.h"
#include "Configuration.h"
#include "IfxPort.h"
#include "UART_Logging.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include <stdio.h>
#include <string.h>

/* g_lan8651 (the driver handle) is defined in Libraries/LAN8651/lan8651.c and
 * declared in Ifx_Lwip.h; only the netif lives here. */
netif_t g_Lan8651Netif;

static boolean s_lan8651LinkUp   = FALSE;
static boolean s_lan8651ArpSent  = FALSE;
static boolean s_lan8651HwReady  = FALSE;

/* ------------------------------------------------------------------ */
/* netif callbacks                                                     */
/* ------------------------------------------------------------------ */

err_t ifx_lan8651_netif_init(struct netif *netif)
{
    lan8651_t *dev = (lan8651_t *)netif->state;

    if (dev == NULL_PTR)
    {
        return ERR_ARG;
    }

#if LWIP_NETIF_HOSTNAME
    netif->hostname = BOARDNAME;
#endif

    /* Hand over to the LAN8651 ethernetif skeleton (it sets name t/1,
     * linkoutput, hwaddr from dev->mac_addr, ...). */
    return lan8651_ethernetif_init(netif);
}

/* ------------------------------------------------------------------ */
/* Hardware bring-up                                                   */
/* ------------------------------------------------------------------ */

/** \brief Reset + start the LAN8651 and apply the PLCA configuration.
 *  Returns TRUE when the chip answered and the MAC is enabled. */
boolean IfxLan8651_initHw(const uint8 mac[6])
{
    uint32_t oaConfig0 = 0U;

    if (s_lan8651HwReady != FALSE)
    {
        return TRUE;
    }

    if (lan8651_init(&g_lan8651,
            &MODULE_QSPI4,
            LAN8651_RST_PORT, LAN8651_RST_PIN,
            LAN8651_INT_PORT, LAN8651_INT_PIN,
            mac) != kLan8651Status_Ok)
    {
        Ifx_Lwip_printf("LAN8651: init failed (QSPI4)");
        return FALSE;
    }

    if (lan8651_start(&g_lan8651) != kLan8651Status_Ok)
    {
        Ifx_Lwip_printf("LAN8651: start failed");
        return FALSE;
    }

    if (lan8651_read_reg(&g_lan8651, LAN8651_OA_CONFIG0, &oaConfig0) != kLan8651Status_Ok)
    {
        Ifx_Lwip_printf("LAN8651: register readback failed");
        return FALSE;
    }

    s_lan8651HwReady = TRUE;
    /* Print the bytes inline: passing a local string through %s used to come
     * out garbled while <stdio.h> was missing (implicit snprintf prototype). */
    Ifx_Lwip_printf("LAN8651: TC6 over QSPI4 %lu Hz, PLCA id=%u cnt=%u, MAC %02X:%02X:%02X:%02X:%02X:%02X",
                    (unsigned long)LAN8651_SPI_BAUDRATE,
                    (unsigned)LAN8651_PLCA_NODE_ID,
                    (unsigned)LAN8651_PLCA_NODE_COUNT,
                    (unsigned)mac[0], (unsigned)mac[1], (unsigned)mac[2],
                    (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
    return TRUE;
}

/** \brief Register the LAN8651 netif with lwIP and bring it up.
 *  Call after Ifx_Lwip_init_with_ip() (which runs lwip_init()). */
err_t IfxLan8651_netifAdd(ip_addr_t ipAddr, ip_addr_t netMask, ip_addr_t gateway)
{
    if (s_lan8651HwReady == FALSE)
    {
        return ERR_IF;
    }

    g_Lan8651Netif.state = &g_lan8651;

    if (netif_add(&g_Lan8651Netif, &ipAddr, &netMask, &gateway,
                  (void *)&g_lan8651, ifx_lan8651_netif_init, ethernet_input) == NULL_PTR)
    {
        return ERR_IF;
    }

    /* The GETH netif stays the default route; this one is reachable by subnet. */
    netif_set_up(&g_Lan8651Netif);

    /* No checksum offload on the LAN8651: let lwIP compute/verify in software.
     * (netif_add() sets NETIF_CHECKSUM_DISABLE_ALL, the GETH setting.) */
    NETIF_SET_CHECKSUM_CTRL(&g_Lan8651Netif, NETIF_CHECKSUM_ENABLE_ALL);

#if LAN8651_FORCE_LINK_UP
    netif_set_link_up(&g_Lan8651Netif);
    s_lan8651LinkUp = TRUE;
#endif

    Ifx_Lwip_printf("LAN8651: netif %c%c max up, IP %s", g_Lan8651Netif.name[0],
                    g_Lan8651Netif.name[1], ipaddr_ntoa(&ipAddr));
    return ERR_OK;
}

/* ------------------------------------------------------------------ */
/* Runtime polling                                                     */
/* ------------------------------------------------------------------ */

boolean IfxLan8651_linkUp(void)
{
    if (s_lan8651HwReady == FALSE)
    {
        return FALSE;
    }
    return lan8651_link_up(&g_lan8651);
}

/** \brief Poll the LAN8651 link state, drain its RX path into lwIP. */
void IfxLan8651_poll(void)
{
    if (s_lan8651HwReady == FALSE)
    {
        return;
    }

    /* ---- link state (100 ms cadence is plenty) ---- */
    {
        static uint32 s_lastLinkPoll = 0U;
        if ((g_TickCount_1ms - s_lastLinkPoll) >= 100U)
        {
            s_lastLinkPoll = g_TickCount_1ms;

            boolean linkUp = lan8651_link_up(&g_lan8651);

            if (linkUp != s_lan8651LinkUp)
            {
                s_lan8651LinkUp = linkUp;

                if (linkUp == FALSE)
                {
                    netif_set_link_down(&g_Lan8651Netif);
                    Ifx_Lwip_printf("LAN8651: link DOWN");
                }
                else
                {
                    netif_set_link_up(&g_Lan8651Netif);
                    Ifx_Lwip_printf("LAN8651: link UP");

                    if (s_lan8651ArpSent == FALSE)
                    {
                        s_lan8651ArpSent = TRUE;
                        (void)etharp_gratuitous(&g_Lan8651Netif);
                    }
                }
            }
        }
    }

    /* ---- RX drain ---- */
    if (netif_is_up(&g_Lan8651Netif))
    {
        lan8651_ethernetif_input(&g_Lan8651Netif, 32U);
    }
}
