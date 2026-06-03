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

#define APP_FATAL_SNAPSHOT_MAGIC 0x46545031UL

typedef struct app_fatal_snapshot {
    uint32_t magic;
    uint32_t boot_count;
    uint32_t reason;
    uint32_t line;
    uint32_t cause;
    uint32_t epc;
    uint32_t mtval;
    uint32_t mstatus;
} app_fatal_snapshot_t;

enum {
    APP_FATAL_REASON_ASSERT = 1U,
    APP_FATAL_REASON_ABORT = 2U,
    APP_FATAL_REASON_EXIT = 3U,
    APP_FATAL_REASON_EXCEPTION = 4U
};

__attribute__((section(".noncacheable.non_init"))) static volatile app_fatal_snapshot_t s_fatal_snapshot;

static void app_record_fatal(uint32_t reason, uint32_t line, uint32_t cause, uint32_t epc)
{
    s_fatal_snapshot.magic = APP_FATAL_SNAPSHOT_MAGIC;
    s_fatal_snapshot.reason = reason;
    s_fatal_snapshot.line = line;
    s_fatal_snapshot.cause = cause;
    s_fatal_snapshot.epc = epc;
    s_fatal_snapshot.mtval = (uint32_t) read_csr(CSR_MTVAL);
    s_fatal_snapshot.mstatus = (uint32_t) read_csr(CSR_MSTATUS);
}

static void app_dump_lwiperf_debug_events(void)
{
    uint32_t i;
    uint32_t head;

    if (g_lwiperf_debug_magic != LWIPERF_DEBUG_MAGIC) {
        return;
    }

    head = g_lwiperf_debug_head % LWIPERF_DEBUG_EVENT_RING_SIZE;
    printf("lwiperf dbg: magic=0x%08lX head=%lu seq=%lu\n",
           (unsigned long) g_lwiperf_debug_magic,
           (unsigned long) g_lwiperf_debug_head,
           (unsigned long) g_lwiperf_debug_seq);
    for (i = 0; i < LWIPERF_DEBUG_EVENT_RING_SIZE; i++) {
        uint32_t idx = (head + i) % LWIPERF_DEBUG_EVENT_RING_SIZE;
        const volatile lwiperf_debug_event_t *e = &g_lwiperf_debug_events[idx];
        if (e->seq == 0U && e->code == 0U && e->ts_ms == 0U) {
            continue;
        }
        printf("lwiperf evt[%lu]: seq=%lu ts=%lu code=%lu line=%lu a=0x%08lX b=0x%08lX c=0x%08lX d=0x%08lX\n",
               (unsigned long) idx,
               (unsigned long) e->seq,
               (unsigned long) e->ts_ms,
               (unsigned long) e->code,
               (unsigned long) e->line,
               (unsigned long) e->a,
               (unsigned long) e->b,
               (unsigned long) e->c,
               (unsigned long) e->d);
    }
}

static void app_dump_boot_diagnostics(void)
{
    if (s_fatal_snapshot.magic != APP_FATAL_SNAPSHOT_MAGIC) {
        s_fatal_snapshot.magic = APP_FATAL_SNAPSHOT_MAGIC;
        s_fatal_snapshot.boot_count = 0U;
        s_fatal_snapshot.reason = 0U;
        s_fatal_snapshot.line = 0U;
        s_fatal_snapshot.cause = 0U;
        s_fatal_snapshot.epc = 0U;
        s_fatal_snapshot.mtval = 0U;
        s_fatal_snapshot.mstatus = 0U;
    }

    s_fatal_snapshot.boot_count++;
    printf("boot counter: %lu\n", (unsigned long) s_fatal_snapshot.boot_count);

    if (s_fatal_snapshot.reason != 0U) {
        printf("last fatal: reason=%lu line=%lu cause=0x%08lX epc=0x%08lX mtval=0x%08lX mstatus=0x%08lX\n",
               (unsigned long) s_fatal_snapshot.reason,
               (unsigned long) s_fatal_snapshot.line,
               (unsigned long) s_fatal_snapshot.cause,
               (unsigned long) s_fatal_snapshot.epc,
               (unsigned long) s_fatal_snapshot.mtval,
               (unsigned long) s_fatal_snapshot.mstatus);
    }

    app_dump_lwiperf_debug_events();
}

#ifndef IPERF_UDP_CLIENT_RATE
    #define IPERF_UDP_CLIENT_RATE (100 * 1024 * 1024)
#endif

#ifndef IPERF_CLIENT_AMOUNT
#define IPERF_CLIENT_AMOUNT (-1000) /* 10 seconds */
#endif

void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    app_record_fatal(APP_FATAL_REASON_ASSERT, (uint32_t) line, 0U, (uint32_t) __builtin_return_address(0));
    printf("assert failed: file=%s line=%d func=%s expr=%s\n",
           file != NULL ? file : "?",
           line,
           func != NULL ? func : "?",
           expr != NULL ? expr : "?");
    while (1) {
    }
}

void abort(void)
{
    app_record_fatal(APP_FATAL_REASON_ABORT, 0U, 0U, (uint32_t) __builtin_return_address(0));
    printf("abort()\n");
    while (1) {
    }
}

void _exit(int status)
{
    app_record_fatal(APP_FATAL_REASON_EXIT, (uint32_t) status, 0U, (uint32_t) __builtin_return_address(0));
    printf("_exit(%d)\n", status);
    while (1) {
    }
}

long exception_handler(long cause, long epc)
{
    app_record_fatal(APP_FATAL_REASON_EXCEPTION, 0U, (uint32_t) cause, (uint32_t) epc);
    printf("exception trap: cause=0x%08lX epc=0x%08lX mtval=0x%08lX\n",
           (unsigned long)cause,
           (unsigned long)epc,
           (unsigned long)read_csr(CSR_MTVAL));
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
    app_dump_boot_diagnostics();

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
