/*
 * plca_demo.h - shared PLCA master/slave demo logic for the two LAN8651
 * PLCA samples (lan8651_plca_master / lan8651_plca_slave).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef LAN8651_PLCA_DEMO_H
#define LAN8651_PLCA_DEMO_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    PLCA_ROLE_COORDINATOR = 0,   /* node 0: sends BEACON */
    PLCA_ROLE_FOLLOWER = 1       /* node 1: follows the BEACON */
} plca_demo_role_t;

/**
 * @brief bring the LAN8651 up (transport auto-detect) and configure PLCA.
 * @param role   coordinator (node 0) or follower (node 1)
 * @param node_cnt  total node count on the segment (must match the peer)
 * @param to_tmr    TO timer in bit times
 * @param burst_cnt burst count (0 = disabled)
 * @param burst_tmr burst timer
 * @return 0 on success
 */
int  plca_demo_init(plca_demo_role_t role, uint8_t node_cnt, uint8_t to_tmr,
                    uint8_t burst_cnt, uint8_t burst_tmr);

/** print PLCA configuration/status (with PST), PHY STS1 and TC6 statistics */
void plca_demo_status(void);

/** run the demo main loop forever (polls RX, echoes frames, prints a heartbeat) */
void plca_demo_run(void);

#endif /* LAN8651_PLCA_DEMO_H */
