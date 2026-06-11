/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef UDP_ECHO_H
#define UDP_ECHO_H

#include "common_cfg.h"

#ifndef UDP_LOCAL_PORT
#define UDP_LOCAL_PORT (APP_UDP_ECHO_PORT)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

void udp_echo_init(void);

#if defined(__cplusplus)
}
#endif

#endif
