/*
 * Copyright (c) 2025 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */


#ifndef NETINFO_H
#define NETINFO_H

#if defined(MAC_TO_MAC_ROLE_B) && MAC_TO_MAC_ROLE_B
#define MAC_TO_MAC_LOCAL_MAC 02:00:5E:31:00:61
#define MAC_TO_MAC_LOCAL_IP 192.168.10.61
#define MAC_TO_MAC_REMOTE_IP 192.168.10.13
#define MAC_TO_MAC_LOCAL_IP_STR "192.168.10.61"
#define MAC_TO_MAC_REMOTE_IP_STR "192.168.10.13"
#else
#define MAC_TO_MAC_LOCAL_MAC 02:00:5E:31:00:13
#define MAC_TO_MAC_LOCAL_IP 192.168.10.13
#define MAC_TO_MAC_REMOTE_IP 192.168.10.61
#define MAC_TO_MAC_LOCAL_IP_STR "192.168.10.13"
#define MAC_TO_MAC_REMOTE_IP_STR "192.168.10.61"
#endif

/* MAC ADDRESS */
#ifndef MAC0_CONFIG
#define MAC0_CONFIG MAC_TO_MAC_LOCAL_MAC
#endif

#ifndef MAC1_CONFIG
#define MAC1_CONFIG 98:2c:bc:b1:9f:37
#endif

/* Static IP ADDRESS */
#ifndef IP0_CONFIG
#define IP0_CONFIG MAC_TO_MAC_LOCAL_IP
#endif

#ifndef IP1_CONFIG
#define IP1_CONFIG 192.168.200.10
#endif

/* Netmask */
#ifndef NETMASK0_CONFIG
#define NETMASK0_CONFIG 255.255.255.0
#endif

#ifndef NETMASK1_CONFIG
#define NETMASK1_CONFIG 255.255.255.0
#endif

/* Gateway Address */
#ifndef GW0_CONFIG
#define GW0_CONFIG 0.0.0.0
#endif

#ifndef GW1_CONFIG
#define GW1_CONFIG 192.168.200.1
#endif

/* Remote IP Address */
#ifndef REMOTE_IP0_CONFIG
#define REMOTE_IP0_CONFIG MAC_TO_MAC_REMOTE_IP
#endif

#ifndef IP0_CONFIG_STR
#define IP0_CONFIG_STR MAC_TO_MAC_LOCAL_IP_STR
#endif

#ifndef REMOTE_IP0_CONFIG_STR
#define REMOTE_IP0_CONFIG_STR MAC_TO_MAC_REMOTE_IP_STR
#endif

#ifndef REMOTE_IP1_CONFIG
#define REMOTE_IP1_CONFIG 192.168.200.5
#endif

#endif  /* NETINFO_H */
