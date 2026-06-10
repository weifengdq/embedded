/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <stdio.h>
#include "common.h"
#include "netinfo.h"
#include "netconf.h"
#include "ethernetif.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/apps/lwiperf.h"
#include "lwiperf_local.h"
#include "udp_echo.h"

static void lwiperf_report(void *arg,
                           enum lwiperf_report_type report_type,
                           const ip_addr_t *local_addr,
                           u16_t local_port,
                           const ip_addr_t *remote_addr,
                           u16_t remote_port,
                           u32_t bytes_transferred,
                           u32_t ms_duration,
                           u32_t bandwidth_kbitpsec)
{
    ethernetif_debug_counters_t counters = {0};

    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(local_addr);
    LWIP_UNUSED_ARG(local_port);
    LWIP_UNUSED_ARG(remote_addr);

    ethernetif_get_debug_counters(&counters);

    printf("iperf report:\n");
    printf("type=%d\n", (int) report_type);
    printf("remote_port=%u\n", (unsigned int) remote_port);
    printf("total_bytes=%lu\n", (unsigned long) bytes_transferred);
    printf("duration_ms=%lu\n", (unsigned long) ms_duration);
    printf("kbits_per_s=%lu\n", (unsigned long) bandwidth_kbitpsec);
    printf("eth tx_busy=%lu tx_err=%lu input_err=%lu input_ok=%lu\n",
           (unsigned long) counters.tx_busy,
           (unsigned long) counters.tx_errors,
           (unsigned long) counters.input_err,
           (unsigned long) counters.input_ok);
    printf("eth tx_frames=%lu rx_frames=%lu tx_bytes=%lu rx_bytes=%lu\n",
           (unsigned long) counters.tx_frames,
           (unsigned long) counters.rx_frames,
           (unsigned long) counters.tx_bytes,
           (unsigned long) counters.rx_bytes);
}

static void start_network_services(void)
{
    void *tcp_server;
    void *udp_server;

    ethernetif_reset_debug_counters();

    udp_echo_init();
    printf("UDP echo server started on port %u\n", (unsigned int) UDP_LOCAL_PORT);

    tcp_server = app_lwiperf_start_tcp_server_default(lwiperf_report, NULL);
    if (tcp_server == NULL) {
        printf("Failed to start TCP iperf server\n");
    } else {
        printf("TCP iperf server started on port %u\n", (unsigned int) LWIPERF_TCP_PORT_DEFAULT);
    }

    udp_server = app_lwiperf_start_udp_server(netif_ip_addr4(netif_default),
                                              LWIPERF_UDP_PORT_DEFAULT,
                                              lwiperf_report,
                                              NULL);
    if (udp_server == NULL) {
        printf("Failed to start UDP iperf server\n");
    } else {
        printf("UDP iperf server started on port %u\n", (unsigned int) LWIPERF_UDP_PORT_DEFAULT);
    }
}


int main(void)
{
    board_init();

#if defined(__ENABLE_ENET_RECEIVE_INTERRUPT) && __ENABLE_ENET_RECEIVE_INTERRUPT
    printf("This is an ethernet demo: RTL8211FI + lwIP (Interrupt Usage)\n");
#else
    printf("This is an ethernet demo: RTL8211FI + lwIP (Polling Usage)\n");
#endif
    printf("LwIP Version: %s\n", LWIP_VERSION_STRING);
    printf("PHY address candidate: %u\n", (unsigned int) RTL8211_ADDR);
    printf("Local IP:  %s\n", IP0_CONFIG_STR);
    printf("Host IP:   %s\n", REMOTE_IP0_CONFIG_STR);

    if (enet_init(ENET) == status_success) {
        lwip_init();
        netif_config(&gnetif);
        netif_show_ip_info(&gnetif);
        enet_services(&gnetif);
        start_network_services();
        board_timer_create(LWIP_APP_TIMER_INTERVAL, sys_timer_callback);

        printf("Ready:\n");
        printf("  ping %s\n", IP0_CONFIG_STR);
        printf("  UDP echo port: %u\n", (unsigned int) UDP_LOCAL_PORT);
        printf("  iperf TCP/UDP server port: %u\n", (unsigned int) LWIPERF_TCP_PORT_DEFAULT);

        while (1) {
            enet_common_handler(&gnetif);
        }
    }

    printf("Enet initialization fails !!!\n");
    while (1) {
    }

    return 0;
}
