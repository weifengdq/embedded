/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * HPM5E31 RMII DP83848 lwIP application
 * Features:
 *   - Static IP: 192.168.0.100
 *   - UDP echo server (port 7)
 *   - iperf TCP/UDP server and client
 */

#include "common.h"
#include "utils.h"
#include "netconf.h"
#include "sys_arch.h"
#include "ethernetif.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/udp.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"

/* -----------------------------------------------------------------------
 * iperf configuration
 * ----------------------------------------------------------------------- */
#ifndef IPERF_UDP_CLIENT_RATE
#define IPERF_UDP_CLIENT_RATE (10 * 1024 * 1024)  /* 10 Mbps default */
#endif

#ifndef IPERF_CLIENT_AMOUNT
#define IPERF_CLIENT_AMOUNT (-1000)  /* 10 seconds */
#endif

/* -----------------------------------------------------------------------
 * UDP echo server (RFC 862, port 7)
 * ----------------------------------------------------------------------- */
#define UDP_ECHO_PORT  7

static void udp_echo_recv(void *arg, struct udp_pcb *pcb,
                          struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    if (p != NULL) {
        udp_sendto(pcb, p, addr, port);
        pbuf_free(p);
    }
}

static void udp_echo_init(void)
{
    struct udp_pcb *pcb = udp_new();
    if (pcb == NULL) {
        printf("UDP echo: failed to allocate PCB\n");
        return;
    }
    err_t err = udp_bind(pcb, IP_ADDR_ANY, UDP_ECHO_PORT);
    if (err != ERR_OK) {
        printf("UDP echo: bind failed %d\n", (int)err);
        udp_remove(pcb);
        return;
    }
    udp_recv(pcb, udp_echo_recv, NULL);
    printf("UDP echo server started on port %d\n", UDP_ECHO_PORT);
}

/* -----------------------------------------------------------------------
 * iperf
 * ----------------------------------------------------------------------- */
static void lwiperf_report(void *arg, enum lwiperf_report_type report_type,
    const ip_addr_t *local_addr, u16_t local_port,
    const ip_addr_t *remote_addr, u16_t remote_port,
    u32_t bytes_transferred, u32_t ms_duration, u32_t bandwidth_kbitpsec)
{
    LWIP_UNUSED_ARG(arg);
    LWIP_UNUSED_ARG(local_addr);
    LWIP_UNUSED_ARG(local_port);

    printf("iperf report: type=%d, remote: %s:%d, bytes: %"U32_F
           ", ms: %"U32_F", kbits/s: %"U32_F"\n",
           (int)report_type, ipaddr_ntoa(remote_addr), (int)remote_port,
           bytes_transferred, ms_duration, bandwidth_kbitpsec);
}

static bool s_menu_shown = false;

static void show_menu(void)
{
    printf("\n--- iperf mode ---\n");
    printf("1: TCP Server  2: TCP Client\n");
    printf("3: UDP Server  4: UDP Client\n");
    printf("Space: stop current session\n");
}

static void iperf(void)
{
    static void *session = NULL;
    int ch = console_try_receive_byte();

    /* show menu once after link comes up */
    if (!s_menu_shown && netif_is_link_up(&gnetif)) {
        s_menu_shown = true;
        show_menu();
    }

    if (ch < 0) {
        if (session != NULL) {
            lwiperf_poll_udp_client();
        }
        return;
    }

    if (ch == ' ' && session != NULL) {
        lwiperf_abort(session);
        session = NULL;
        printf("iperf stopped.\n");
        show_menu();
        return;
    }

    if (session != NULL) {
        lwiperf_poll_udp_client();
        return;
    }

    ip_addr_t remote_addr;
    uint8_t cmd_str_buff[20];

    switch (ch) {
    case '1':
        session = lwiperf_start_tcp_server_default(lwiperf_report, NULL);
        printf("TCP server started (port 5001)\n");
        break;
    case '2':
        printf("Enter remote IP: ");
        while (!fetch_ip_addr_from_serial_terminal(0, cmd_str_buff, sizeof(cmd_str_buff))) {}
        ip4addr_aton((char *)cmd_str_buff, &remote_addr);
        session = lwiperf_start_tcp_client_default(&remote_addr, lwiperf_report, NULL);
        printf("TCP client started\n");
        break;
    case '3':
        session = lwiperf_start_udp_server(netif_ip_addr4(netif_default),
                      LWIPERF_UDP_PORT_DEFAULT, lwiperf_report, NULL);
        printf("UDP server started (port %d)\n", LWIPERF_UDP_PORT_DEFAULT);
        break;
    case '4':
        printf("Enter remote IP: ");
        while (!fetch_ip_addr_from_serial_terminal(0, cmd_str_buff, sizeof(cmd_str_buff))) {}
        ip4addr_aton((char *)cmd_str_buff, &remote_addr);
        session = lwiperf_start_udp_client(
                      netif_ip_addr4(netif_default),
                      LWIPERF_UDP_PORT_DEFAULT,
                      &remote_addr, LWIPERF_UDP_PORT_DEFAULT,
                      LWIPERF_CLIENT, IPERF_CLIENT_AMOUNT,
                      IPERF_UDP_CLIENT_RATE, 0,
                      lwiperf_report, NULL);
        printf("UDP client started\n");
        break;
    default:
        break;
    }
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */
int main(void)
{
    board_init();

    printf("HPM5E31 RMII DP83848 lwIP demo\n");
    printf("LwIP version: %s\n", LWIP_VERSION_STRING);
    printf("Static IP: %s\n", HPM_STRINGIFY(IP0_CONFIG));

    if (enet_init(ENET) != status_success) {
        printf("Ethernet init failed!\n");
        while (1) {}
    }

    lwip_init();
    netif_config(&gnetif);
    enet_services(&gnetif);

    udp_echo_init();

    board_timer_create(LWIP_APP_TIMER_INTERVAL, sys_timer_callback);

    printf("Ready. Ping 192.168.0.100. UDP echo on port 7.\n");
    printf("Press 1-4 to start iperf once link is up.\n");

    while (1) {
        enet_common_handler(&gnetif);
        iperf();
    }

    return 0;
}
