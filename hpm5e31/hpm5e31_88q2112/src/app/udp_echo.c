/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include "udp_echo.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"

static void udp_echo_recv(void *arg, struct udp_pcb *pcb, struct pbuf *packet, const ip_addr_t *addr, u16_t port)
{
    struct pbuf *reply;

    (void) arg;

    if (packet == NULL) {
        return;
    }

    reply = pbuf_clone(PBUF_TRANSPORT, PBUF_RAM, packet);
    if (reply != NULL) {
        udp_sendto(pcb, reply, addr, port);
        pbuf_free(reply);
    }

    pbuf_free(packet);
}

void udp_echo_init(void)
{
    struct udp_pcb *pcb;

    pcb = udp_new();
    if (pcb == NULL) {
        return;
    }

    if (udp_bind(pcb, IP_ADDR_ANY, UDP_LOCAL_PORT) != ERR_OK) {
        udp_remove(pcb);
        return;
    }

    udp_recv(pcb, udp_echo_recv, NULL);
}
