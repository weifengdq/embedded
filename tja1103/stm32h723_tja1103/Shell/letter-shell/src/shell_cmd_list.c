/**
 * @file shell_cmd_list.c
 * @author Letter (NevermindZZT@gmail.com)
 * @brief shell cmd list
 * @version 3.0.0
 * @date 2020-01-17
 * 
 * @copyright (c) 2020 Letter
 * 
 */

#include "shell.h"

#if SHELL_USING_CMD_EXPORT != 1

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
#if SHELL_EXEC_UNDEF_FUNC == 1
extern int shellExecute(int argc, char *argv[]);
#endif

extern int cmd_info(int argc, char *argv[]);
extern int cmd_dump(int argc, char *argv[]);
extern int cmd_reset(int argc, char *argv[]);
extern int cmd_port(int argc, char *argv[]);
extern int cmd_master(int argc, char *argv[]);
extern int cmd_slave(int argc, char *argv[]);
extern int cmd_enable(int argc, char *argv[]);
extern int cmd_disable(int argc, char *argv[]);
extern int cmd_phyread(int argc, char *argv[]);
extern int cmd_phywrite(int argc, char *argv[]);
extern int cmd_vlan(int argc, char *argv[]);
extern int cmd_mirror(int argc, char *argv[]);
extern int cmd_ptp(int argc, char *argv[]);
extern int cmd_staticmac(int argc, char *argv[]);
extern int cmd_spiread(int argc, char *argv[]);
extern int cmd_spiwrite(int argc, char *argv[]);
extern int cmd_smiread(int argc, char *argv[]);
extern int cmd_smiwrite(int argc, char *argv[]);
extern int cmd_diagbus(int argc, char *argv[]);
extern int cmd_rstprobe(int argc, char *argv[]);
extern int cmd_spiprobe(int argc, char *argv[]);
extern int cmd_spispeed(int argc, char *argv[]);
extern int cmd_mib(int argc, char *argv[]);
extern int cmd_config(int argc, char *argv[]);

SHELL_AGENCY_FUNC(shellRun, shellGetCurrent(), (const char *)p1);


/**
 * @brief shell命令表
 * 
 */
const ShellCommand shellCommandList[] = 
{
    {.attr.value=SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_USER),
    .data.user.name = SHELL_DEFAULT_USER,
    .data.user.password = SHELL_DEFAULT_USER_PASSWORD,
    .data.user.desc = "default user"},
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
                   setVar, shellSetVar, set var),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0), 0x1B5B4100, shellUp, up),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0), 0x1B5B4200, shellDown, down),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_ENABLE_UNCHECKED,
                   0x1B5B4300, shellRight, right),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_ENABLE_UNCHECKED,
                   0x1B5B4400, shellLeft, left),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0), 0x09000000, shellTab, tab),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_ENABLE_UNCHECKED,
                   0x08000000, shellBackspace, backspace),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_ENABLE_UNCHECKED,
                   0x7F000000, shellDelete, delete),
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_ENABLE_UNCHECKED,
                   0x1B5B337E, shellDelete, delete),
#if SHELL_ENTER_LF == 1
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_ENABLE_UNCHECKED,
                   0x0A000000, shellEnter, enter),
#endif
#if SHELL_ENTER_CR == 1
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_ENABLE_UNCHECKED,
                   0x0D000000, shellEnter, enter),
#endif
#if SHELL_ENTER_CRLF == 1
    SHELL_KEY_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_ENABLE_UNCHECKED,
                   0x0D0A0000, shellEnter, enter),
#endif
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
                   help, shellHelp, show command info\r\nhelp [cmd]),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC)|SHELL_CMD_DISABLE_RETURN,
                   users, shellUsers, list all user),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC)|SHELL_CMD_DISABLE_RETURN,
                   cmds, shellCmds, list all cmd),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC)|SHELL_CMD_DISABLE_RETURN,
                   vars, shellVars, list all var),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC)|SHELL_CMD_DISABLE_RETURN,
                   keys, shellKeys, list all key),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC)|SHELL_CMD_DISABLE_RETURN,
                   clear, shellClear, clear console),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC)|SHELL_CMD_DISABLE_RETURN,
                   sh, SHELL_AGENCY_FUNC_NAME(shellRun), run command directly),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   info, cmd_info, show chip info),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   dump, cmd_dump, dump key registers),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   reset, cmd_reset, hardware reset lan9370),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   port, cmd_port, port <1-5>),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   master, cmd_master, master <1-4>),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   slave, cmd_slave, slave <1-4>),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   enable, cmd_enable, enable <1-5>),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   disable, cmd_disable, disable <1-5>),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   phyread, cmd_phyread, phyread <port> <reg>),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   phywrite, cmd_phywrite, phywrite <port> <reg> <value>),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   spiread, cmd_spiread, spiread <addr> [count]),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   spiwrite, cmd_spiwrite, spiwrite <addr> <value>),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   smiread, cmd_smiread, smiread <phy> <reg>),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   smiwrite, cmd_smiwrite, smiwrite <phy> <reg> <value>),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   diagbus, cmd_diagbus, diagnose SPI/SMI bus),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   rstprobe, cmd_rstprobe, reset timing probe),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   spiprobe, cmd_spiprobe, probe SPI mode0..3),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   spispeed, cmd_spispeed, set SPI prescaler),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   mib, cmd_mib, mib <1-5>),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   vlan, cmd_vlan, vlan on|off|set <port> <vid>|show),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   mirror, cmd_mirror, mirror <src> <dst|off>),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   ptp, cmd_ptp, ptp on|off|status|gptp on|off|status),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   config, cmd_config, config save|load|show|erase),
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                   staticmac, cmd_staticmac, staticmac list|flush),
#if SHELL_EXEC_UNDEF_FUNC == 1
    SHELL_CMD_ITEM(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN)|SHELL_CMD_DISABLE_RETURN,
                   exec, shellExecute, execute function undefined),
#endif
};


/**
 * @brief shell命令表大小
 * 
 */
const unsigned short shellCommandCount 
    = sizeof(shellCommandList) / sizeof(ShellCommand);

#endif
