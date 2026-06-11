/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef APP_LWIPERF_LOCAL_H
#define APP_LWIPERF_LOCAL_H

#include "lwip/apps/lwiperf.h"

#if defined(__cplusplus)
extern "C" {
#endif

void *app_lwiperf_start_tcp_server(const ip_addr_t *local_addr, u16_t local_port,
                                   lwiperf_report_fn report_fn, void *report_arg);
void *app_lwiperf_start_tcp_server_default(lwiperf_report_fn report_fn, void *report_arg);
void *app_lwiperf_start_tcp_client(const ip_addr_t *remote_addr, u16_t remote_port,
                                   enum lwiperf_client_type type,
                                   lwiperf_report_fn report_fn, void *report_arg);
void *app_lwiperf_start_tcp_client_default(const ip_addr_t *remote_addr,
                                           lwiperf_report_fn report_fn, void *report_arg);
void *app_lwiperf_start_udp_server(const ip_addr_t *local_addr, u16_t local_port,
                                   lwiperf_report_fn report_fn, void *report_arg);
void *app_lwiperf_start_udp_client(const ip_addr_t *local_addr, u16_t local_port,
                                   const ip_addr_t *remote_addr, u16_t remote_port,
                                   enum lwiperf_client_type type, int amount, s32_t rate, u8_t tos,
                                   lwiperf_report_fn report_fn, void *report_arg);
void app_lwiperf_poll_udp_client(void);
void app_lwiperf_abort(void *lwiperf_session);

#if defined(__cplusplus)
}
#endif

#endif /* APP_LWIPERF_LOCAL_H */
