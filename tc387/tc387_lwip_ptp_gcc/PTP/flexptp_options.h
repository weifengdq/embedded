#ifndef FLEXPTP_OPTIONS_H_
#define FLEXPTP_OPTIONS_H_

#define FLEXPTP_OSLESS
#define PTP_ADDEND_INTERFACE
#define LWIP 1

#ifdef __GNUC__
#include <limits.h>
#include <sys/_types.h>
#ifndef _SSIZE_T_DECLARED
typedef _ssize_t ssize_t;
#define _SSIZE_T_DECLARED
#endif
#ifndef SSIZE_MAX
#define SSIZE_MAX LONG_MAX
#endif
#endif

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lwip/igmp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "flexptp_port.h"
#include "flexptp/servo/pid_controller.h"

#ifndef __PRI64_PREFIX
#define __PRI64_PREFIX "l"
#endif

#ifndef ONOFF
#define ONOFF(str) ((!strcmp((str), "on")) ? 1 : ((!strcmp((str), "off")) ? 0 : -1))
#endif

#ifndef LOCK_TCPIP_CORE
#define LOCK_TCPIP_CORE() ((void)0)
#endif

#ifndef UNLOCK_TCPIP_CORE
#define UNLOCK_TCPIP_CORE() ((void)0)
#endif

#define MSG(...) flexptp_printf(__VA_ARGS__)
#define FLEXPTP_SNPRINTF(...) snprintf(__VA_ARGS__)
#define CLILOG(en, ...)     \
    do                      \
    {                       \
        if (en)             \
        {                   \
            MSG(__VA_ARGS__); \
        }                   \
    } while (0)

#define FLEXPTP_OSLESS_LOCK flexptp_osless_lock

#define PTP_HW_INIT(increment, addend) ptphw_init((increment), (addend))
#define PTP_SET_CLOCK(seconds, nanoseconds) flexptp_ptp_set_clock((seconds), (nanoseconds))
#define PTP_SET_ADDEND(addend) flexptp_ptp_set_addend((addend))
#define PTP_HW_GET_TIME(pt) ptphw_gettime((pt))

#define PTP_MAIN_OSCILLATOR_FREQ_HZ (150000000UL)
#define PTP_INCREMENT_NSEC (7UL)

#define PTP_SERVO_INIT() pid_ctrl_init()
#define PTP_SERVO_DEINIT() pid_ctrl_deinit()
#define PTP_SERVO_RESET() pid_ctrl_reset()
#define PTP_SERVO_RUN(dt, aux) pid_ctrl_run((dt), (aux))

#define CMD_FUNCTION(name) int name(const char **ppArgs, uint8_t argc)
#define CLI_REG_CMD(cmd_hintline, n_cmd, n_min_arg, cb) \
    flexptp_cli_register_command((cmd_hintline), (n_cmd), (n_min_arg), (cb))
#define CLI_REMOVE_CMD(id) flexptp_cli_remove_command((id))

#define PTP_USER_EVENT_CALLBACK flexptp_user_event_cb

#define PTP_ENABLE_MASTER_OPERATION (1)
#define PTP_CLOCK_PRIORITY1 (248)
#define PTP_CLOCK_PRIORITY2 (248)

#define FLEXPTP_LWIP_BIND_ANY_FOR_MULTICAST 1

#endif
