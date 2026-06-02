/*
 * Copyright (c) 2021-2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

/*---------------------------------------------------------------------*
 * Includes
 *---------------------------------------------------------------------*/
#include "common.h"
#include "utils.h"
#include "netinfo.h"
#include "netconf.h"
#include "sys_arch.h"
#include "ethernetif.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"

#ifndef IPERF_UDP_CLIENT_RATE
    #define IPERF_UDP_CLIENT_RATE (800 * 1024 * 1024)
#endif

#ifndef IPERF_CLIENT_AMOUNT
#define IPERF_CLIENT_AMOUNT (-1000) /* 10 seconds */
#endif

ATTR_PLACE_AT_FAST_RAM_NON_INIT volatile uint32_t g_lwiperf_restart_marker;

void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    g_lwiperf_restart_marker = 0xA5500001U;
    printf("assert failed: file=%s line=%d func=%s expr=%s\n",
           file != NULL ? file : "?",
           line,
           func != NULL ? func : "?",
           expr != NULL ? expr : "?");
    board_delay_ms(20);
    while (1) {
    }
}

void abort(void)
{
    g_lwiperf_restart_marker = 0xA5500002U;
    printf("abort()\n");
    board_delay_ms(20);
    while (1) {
    }
}

void _exit(int status)
{
    g_lwiperf_restart_marker = 0xA5500003U;
    printf("_exit(%d)\n", status);
    board_delay_ms(20);
    while (1) {
    }
}

long exception_handler(long cause, long epc)
{
    g_lwiperf_restart_marker = 0xA5500004U;
    printf("exception trap: cause=0x%08lX epc=0x%08lX mtval=0x%08lX\n",
           (unsigned long)cause,
           (unsigned long)epc,
           (unsigned long)read_csr(CSR_MTVAL));
    board_delay_ms(20);
    while (1) {
    }
}

static void
lwiperf_report(void *arg, enum lwiperf_report_type report_type,
  const ip_addr_t* local_addr, u16_t local_port, const ip_addr_t* remote_addr, u16_t remote_port,
  u32_t bytes_transferred, u32_t ms_duration, u32_t bandwidth_kbitpsec)
{
    ethernetif_debug_counters_t debug_counters;
    u32_t now_ms;
  LWIP_UNUSED_ARG(arg);
  LWIP_UNUSED_ARG(local_addr);
  LWIP_UNUSED_ARG(local_port);
    LWIP_UNUSED_ARG(remote_addr);

        ethernetif_get_debug_counters(&debug_counters);
        now_ms = sys_now();

    LWIP_PLATFORM_DIAG(("iperf report:\n"));
    LWIP_PLATFORM_DIAG(("type=%d\n", (int)report_type));
    LWIP_PLATFORM_DIAG(("remote_port=%d\n", (int)remote_port));
    LWIP_PLATFORM_DIAG(("total_bytes=%"U32_F"\n", bytes_transferred));
    LWIP_PLATFORM_DIAG(("duration_ms=%"U32_F"\n", ms_duration));
    LWIP_PLATFORM_DIAG(("kbits_per_s=%"U32_F"\n", bandwidth_kbitpsec));
        LWIP_PLATFORM_DIAG(("dbg_tx_frames=%"U32_F"\n", debug_counters.tx_frames));
        LWIP_PLATFORM_DIAG(("dbg_tx_bytes=%"U32_F"\n", debug_counters.tx_bytes));
        LWIP_PLATFORM_DIAG(("dbg_tx_busy=%"U32_F"\n", debug_counters.tx_busy));
        LWIP_PLATFORM_DIAG(("dbg_tx_errors=%"U32_F"\n", debug_counters.tx_errors));
        LWIP_PLATFORM_DIAG(("dbg_rx_frames=%"U32_F"\n", debug_counters.rx_frames));
        LWIP_PLATFORM_DIAG(("dbg_rx_bytes=%"U32_F"\n", debug_counters.rx_bytes));
        LWIP_PLATFORM_DIAG(("dbg_input_ok=%"U32_F"\n", debug_counters.input_ok));
        LWIP_PLATFORM_DIAG(("dbg_input_err=%"U32_F"\n", debug_counters.input_err));
        LWIP_PLATFORM_DIAG(("dbg_last_tx_age_ms=%"U32_F"\n", debug_counters.last_tx_ms == 0 ? 0 : now_ms - debug_counters.last_tx_ms));
        LWIP_PLATFORM_DIAG(("dbg_last_rx_age_ms=%"U32_F"\n", debug_counters.last_rx_ms == 0 ? 0 : now_ms - debug_counters.last_rx_ms));
}

static bool select_mode(struct netif *netif, bool *server_mode, bool *tcp, enum lwiperf_client_type *client_type)
{
    char code;

    if (!netif_is_link_up(netif)) {
        return false;
    }

    printf("\n");
    printf("1: TCP Server Mode\n");
    printf("2: TCP Client Mode\n");
    printf("3: UDP Server Mode\n");
    printf("4: UDP Client Mode\n");
    printf("Please enter one of modes above (e.g. 1 or 2 ...): ");
    code = getchar();
    printf("%c\n", code);

    switch (code)
    {
        case '1':
            *server_mode = true;
            *tcp         = true;
            *client_type = LWIPERF_CLIENT;
            break;

        case '2':
            *server_mode = false;
            *tcp         = true;
            *client_type = LWIPERF_CLIENT;
            break;

        case '3':
            *server_mode = true;
            *tcp         = false;
            *client_type = LWIPERF_CLIENT;
            break;

        case '4':
            *server_mode = false;
            *tcp         = false;
            *client_type = LWIPERF_CLIENT;
            break;

        default:
            return false;
    }

    return true;
}

void *start_iperf(void)
{
    bool server = false;
    bool tcp = false;
    enum lwiperf_client_type client_type;
    void *session;
    ip_addr_t remote_addr;
    uint8_t cmd_str_buff[20];

    if (!select_mode(&gnetif, &server, &tcp, &client_type)) {
        return NULL;
    }

    ethernetif_reset_debug_counters();

    if (server) {
        if (tcp) {
            printf("Starting TCP server...\n");
            session = lwiperf_start_tcp_server_default(lwiperf_report, NULL);
        } else {
            printf("Starting UDP server...\n");
            session = lwiperf_start_udp_server(netif_ip_addr4(netif_default), LWIPERF_UDP_PORT_DEFAULT,
                                               lwiperf_report, NULL);
        }
    } else {
        while (!fetch_ip_addr_from_serial_terminal(0, cmd_str_buff, sizeof(cmd_str_buff))) {

        }

        ip4addr_aton((char *)cmd_str_buff, &remote_addr);
        printf("Remote IP parsed.\n");

        if (tcp) {
            printf("Calling lwiperf_start_tcp_client_default...\n");
            session = lwiperf_start_tcp_client_default(&remote_addr, lwiperf_report, NULL);
        } else {
            printf("Calling lwiperf_start_udp_client...\n");
            session = lwiperf_start_udp_client(netif_ip_addr4(netif_default), LWIPERF_UDP_PORT_DEFAULT,
                                               &remote_addr, LWIPERF_UDP_PORT_DEFAULT, client_type,
                                               IPERF_CLIENT_AMOUNT, IPERF_UDP_CLIENT_RATE, 0,
                                               lwiperf_report, NULL);
        }
    }

    if (session == NULL) {
        printf("Failed to start iperf session.\n");
    } else {
        printf("Iperf session started.\n");
    }

    return session;
}

void iperf(void)
{
    static void *session = NULL;

#if defined(LWIP_DHCP) && LWIP_DHCP
    if (netif_dhcp_data(&gnetif)->state == DHCP_STATE_BOUND) {
#endif
        if (session == NULL) {
            session = start_iperf();
        } else {
            if (console_try_receive_byte() == ' ') {
                lwiperf_abort(session);
                session = NULL;
            }
        }

        lwiperf_poll_udp_client();
#if defined(LWIP_DHCP) && LWIP_DHCP
    }
#endif
}

/*---------------------------------------------------------------------*
 * Main
/ *---------------------------------------------------------------------*/
int main(void)
{
    /* Initialize BSP */
    board_init();

    #if defined(__ENABLE_ENET_RECEIVE_INTERRUPT) && __ENABLE_ENET_RECEIVE_INTERRUPT
    printf("This is an ethernet demo: Iperf (Interrupt Usage)\n");
    #else
    printf("This is an ethernet demo: Iperf (Polling Usage)\n");
    #endif

    printf("LwIP Version: %s\n", LWIP_VERSION_STRING);
    #if defined(MAC_TO_MAC_ROLE_B) && MAC_TO_MAC_ROLE_B
    printf("Board Role: B\n");
    #else
    printf("Board Role: A\n");
    #endif
    printf("Local IP:  %s\n", IP0_CONFIG_STR);
    printf("Peer IP:   %s\n", REMOTE_IP0_CONFIG_STR);

    /* Initialize GPIOs, clock, MAC(DMA) and PHY */
    if (enet_init(ENET) == 0) {
        /* Initialize lwIP stack */
        lwip_init();

        /* Initialize network interface */
        netif_config(&gnetif);

        /* Start services */
        enet_services(&gnetif);

        /* Start a board timer */
        board_timer_create(LWIP_APP_TIMER_INTERVAL, sys_timer_callback);

        while (1) {
            enet_common_handler(&gnetif);
            iperf();
        }
    } else {
        printf("Enet initialization fails !!!\n");
        while (1) {

        }
    }

    return 0;
}
