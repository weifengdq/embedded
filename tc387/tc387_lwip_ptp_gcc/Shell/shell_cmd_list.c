/**
 * @file    shell_cmd_list.c
 * @brief   Manual letter-shell command table for TC387 PTP/gPTP
 */

#include "shell.h"
#include "PTP/flexptp_app.h"
#include "PTP/flexptp_port.h"
#include "lwip/netif.h"

#include <stdio.h>

extern int shellSetVar(char *name, int value);
extern void shellUp(Shell *shell);
extern void shellDown(Shell *shell);
extern void shellRight(Shell *shell);
extern void shellLeft(Shell *shell);
extern void shellTab(Shell *shell);
extern void shellBackspace(Shell *shell);
extern void shellDelete(Shell *shell);
extern void shellEnter(Shell *shell);
extern void shellHelp(int argc, char *argv[]);
extern void shellUsers(void);
extern void shellCmds(void);
extern void shellVars(void);
extern void shellKeys(void);
extern void shellClear(void);

static int cmd_ptp(int argc, char *argv[])
{
    return FlexPTP_ShellPtp(argc, argv);
}

static int cmd_time(int argc, char *argv[])
{
    return FlexPTP_ShellTime(argc, argv);
}

static int cmd_gptp(int argc, char *argv[])
{
    char *localArgv[3];

    (void)argc;
    (void)argv;

    localArgv[0] = "ptp";
    localArgv[1] = "gptp";
    localArgv[2] = NULL_PTR;
    return FlexPTP_ShellPtp(2, localArgv);
}

static int cmd_pps(int argc, char *argv[])
{
    char *localArgv[5];
    int forwardedArgc;
    int index;

    localArgv[0] = "ptp";
    localArgv[1] = "pps";

    forwardedArgc = 2;
    for (index = 1; (index < argc) && (forwardedArgc < 4); ++index)
    {
        localArgv[forwardedArgc++] = argv[index];
    }

    localArgv[forwardedArgc] = NULL_PTR;
    return FlexPTP_ShellPtp(forwardedArgc, localArgv);
}

static int cmd_info(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    extern struct netif *netif_default;
    if (netif_default != NULL)
    {
        printf("IP:  192.168.0.100\r\n");
        printf("MAC: DE:AD:BE:EF:FE:ED\r\n");
        printf("Link: %s\r\n", netif_is_link_up(netif_default) ? "up" : "down");
    }

    FlexPTP_PrintStatus();
    return 0;
}

static int cmd_uptime(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    extern volatile uint32_t g_TickCount_1ms;
    uint32_t sec = g_TickCount_1ms / 1000U;
    printf("Uptime: %lu.%03lu s\r\n",
           (unsigned long)sec,
           (unsigned long)(g_TickCount_1ms % 1000U));
    return 0;
}

static int cmd_diag(int argc, char *argv[])
{
    TimestampU ts;

    (void)argc;
    (void)argv;

    ptphw_gettime(&ts);
    printf("diag:\r\n");
    printf("  time      : %lu s  %lu ns\r\n", (unsigned long)ts.sec, (unsigned long)ts.nanosec);
    printf("  rx_hook    : %lu\r\n", (unsigned long)g_ptp_rx_hook_called);
    printf("  rx_type    : %lu\r\n", (unsigned long)g_ptp_rx_ethertype_match);
    printf("  rx_mac     : %lu\r\n", (unsigned long)g_ptp_rx_mac_match);
    printf("  rx_enqueue : %lu\r\n", (unsigned long)g_ptp_rx_enqueued);
    printf("  tx_attempt : %lu\r\n", (unsigned long)g_ptp_tx_attempts);
    printf("  tx_sent    : %lu\r\n", (unsigned long)g_ptp_tx_sent);
    return 0;
}

const ShellCommand shellCommandList[] =
{
    {.attr.value = SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_USER),
     .data.user.name = SHELL_DEFAULT_USER,
     .data.user.password = SHELL_DEFAULT_USER_PASSWORD,
     .data.user.desc = "default user"},

    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
                   setVar, shellSetVar, set var),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0), 0x1B5B4100, shellUp, up),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0), 0x1B5B4200, shellDown, down),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_ENABLE_UNCHECKED,
                   0x1B5B4300, shellRight, right),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_ENABLE_UNCHECKED,
                   0x1B5B4400, shellLeft, left),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0), 0x09000000, shellTab, tab),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_ENABLE_UNCHECKED,
                   0x08000000, shellBackspace, backspace),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_ENABLE_UNCHECKED,
                   0x7F000000, shellDelete, delete),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_ENABLE_UNCHECKED,
                   0x1B5B337E, shellDelete, delete),
#if SHELL_ENTER_LF == 1
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_ENABLE_UNCHECKED,
                   0x0A000000, shellEnter, enter),
#endif
#if SHELL_ENTER_CR == 1
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_ENABLE_UNCHECKED,
                   0x0D000000, shellEnter, enter),
#endif
#if SHELL_ENTER_CRLF == 1
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_ENABLE_UNCHECKED,
                   0x0D0A0000, shellEnter, enter),
#endif

    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                   help, shellHelp, show command info\r\nhelp [cmd]),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC) | SHELL_CMD_DISABLE_RETURN,
                   users, shellUsers, list all user),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC) | SHELL_CMD_DISABLE_RETURN,
                   cmds, shellCmds, list all cmd),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC) | SHELL_CMD_DISABLE_RETURN,
                   vars, shellVars, list all var),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC) | SHELL_CMD_DISABLE_RETURN,
                   keys, shellKeys, list all key),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC) | SHELL_CMD_DISABLE_RETURN,
                   clear, shellClear, clear console),

    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   ptp, cmd_ptp, ptp on|gptp|off|status|help),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   gptp, cmd_gptp, shortcut: start gPTP profile),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   pps, cmd_pps, shortcut: pps on|off|freq N|duty N),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   time, cmd_time, time ns|utc|local|+H|-H|offset N),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   info, cmd_info, show network + PTP info),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   uptime, cmd_uptime, show MCU uptime),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   diag, cmd_diag, show PTP counters and timestamp state),
};

const unsigned short shellCommandCount = sizeof(shellCommandList) / sizeof(ShellCommand);
