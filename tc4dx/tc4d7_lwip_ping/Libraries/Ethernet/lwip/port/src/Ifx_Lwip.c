/**
 * \file Ifx_Lwip.c
 * \brief Source file of the simplified lwIP port glue for TC4D7
 */

#include <Ifx_Types.h>
#include <Cpu/Std/IfxCpu.h>

#include <string.h>

#include "IfxGeth_Eth.h"
#include "Ifx_Lwip.h"
#include "Ifx_Netif.h"

#define IFX_LWIP_TIMER_TICK_MS      (1U)
#define IFX_LWIP_ARP_PERIOD         (ARP_TMR_INTERVAL / IFX_LWIP_TIMER_TICK_MS)
#define IFX_LWIP_TCP_FAST_PERIOD    (TCP_FAST_INTERVAL / IFX_LWIP_TIMER_TICK_MS)
#define IFX_LWIP_TCP_SLOW_PERIOD    (TCP_SLOW_INTERVAL / IFX_LWIP_TIMER_TICK_MS)
#define IFX_LWIP_LINK_PERIOD        (100U / IFX_LWIP_TIMER_TICK_MS)

#define IFX_LWIP_FLAG_ARP           (1U << 1)
#define IFX_LWIP_FLAG_TCP_FAST      (1U << 2)
#define IFX_LWIP_FLAG_TCP_SLOW      (1U << 3)
#define IFX_LWIP_FLAG_LINK          (1U << 4)

volatile uint32 g_TickCount_1ms;
Ifx_Lwip g_Lwip;
IfxGeth_Eth g_IfxGeth;
IFX_ALIGN(8) uint8 channel0TxBuffer1[IFXGETH_MAX_TX_DESCRIPTORS][IFXGETH_MAX_TX_BUFFER_SIZE];
IFX_ALIGN(8) uint8 channel0RxBuffer1[IFXGETH_MAX_RX_DESCRIPTORS][IFXGETH_MAX_RX_BUFFER_SIZE];

#define Ifx_Lwip_timerIncr(var, period, flag) \
    do                                        \
    {                                         \
        (var) += 1U;                          \
        if ((var) >= (period))                \
        {                                     \
            (var) = 0U;                       \
            timerFlags |= (flag);             \
        }                                     \
    } while (0)

void Ifx_Lwip_onTimerTick(void)
{
    Ifx_Lwip *lwip = &g_Lwip;
    uint16 timerFlags = lwip->timerFlags;

    Ifx_Lwip_timerIncr(lwip->timer.arp, IFX_LWIP_ARP_PERIOD, IFX_LWIP_FLAG_ARP);
    Ifx_Lwip_timerIncr(lwip->timer.tcp_fast, IFX_LWIP_TCP_FAST_PERIOD, IFX_LWIP_FLAG_TCP_FAST);
    Ifx_Lwip_timerIncr(lwip->timer.tcp_slow, IFX_LWIP_TCP_SLOW_PERIOD, IFX_LWIP_FLAG_TCP_SLOW);
    Ifx_Lwip_timerIncr(lwip->timer.link, IFX_LWIP_LINK_PERIOD, IFX_LWIP_FLAG_LINK);

    lwip->timerFlags = timerFlags;
}

void Ifx_Lwip_pollTimerFlags(void)
{
    Ifx_Lwip *lwip = &g_Lwip;
    uint16 timerFlags;
    boolean interruptState = IfxCpu_disableInterrupts();

    timerFlags = lwip->timerFlags;
    lwip->timerFlags = 0U;

    IfxCpu_restoreInterrupts(interruptState);

    if ((timerFlags & IFX_LWIP_FLAG_TCP_FAST) != 0U)
    {
        tcp_fasttmr();
    }

    if ((timerFlags & IFX_LWIP_FLAG_TCP_SLOW) != 0U)
    {
        tcp_slowtmr();
    }

    if ((timerFlags & IFX_LWIP_FLAG_ARP) != 0U)
    {
        etharp_tmr();
    }

    if ((timerFlags & IFX_LWIP_FLAG_LINK) != 0U)
    {
        ifx_netif_update_link(&g_Lwip.netif);
    }
}

void Ifx_Lwip_pollReceiveFlags(void)
{
    (void)ifx_netif_input(&g_Lwip.netif);
}

static void Ifx_Lwip_initCommon(eth_addr_t ethAddr, const ip_addr_t *ipAddr, const ip_addr_t *netMask, const ip_addr_t *gateway)
{
    memset(&g_Lwip, 0, sizeof(g_Lwip));

    lwip_init();

    g_Lwip.eth_addr = ethAddr;
    (void)netif_add(&g_Lwip.netif, ipAddr, netMask, gateway, NULL_PTR, ifx_netif_init, ethernet_input);
    netif_set_default(&g_Lwip.netif);

#if LWIP_NETIF_HOSTNAME
    g_Lwip.netif.hostname = BOARDNAME;
#endif

    netif_set_up(&g_Lwip.netif);
}

void Ifx_Lwip_init(eth_addr_t ethAddr)
{
    ip_addr_t ipAddr = IPADDR4_INIT_BYTES(0, 0, 0, 0);
    ip_addr_t netMask = IPADDR4_INIT_BYTES(255, 0, 0, 0);
    ip_addr_t gateway = IPADDR4_INIT_BYTES(0, 0, 0, 0);

    Ifx_Lwip_initCommon(ethAddr, &ipAddr, &netMask, &gateway);
}

void Ifx_Lwip_init_with_ip(eth_addr_t ethAddr, ip_addr_t ipAddr, ip_addr_t netMask, ip_addr_t gateway)
{
    Ifx_Lwip_initCommon(ethAddr, &ipAddr, &netMask, &gateway);
}

u32_t sys_now(void)
{
    return g_TickCount_1ms;
}

