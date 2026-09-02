#ifndef __SHELL_CFG_USER_H__
#define __SHELL_CFG_USER_H__

#include "Ifx_Types.h"

/* TC387 Shell configuration */
#define SHELL_TASK_WHILE            0
#define SHELL_USING_CMD_EXPORT      1
#define SHELL_USING_COMPANION       0
#define SHELL_SUPPORT_END_LINE      0
#define SHELL_HELP_LIST_USER        0
#define SHELL_HELP_LIST_VAR         0
#define SHELL_HELP_LIST_KEY         0
#define SHELL_HELP_SHOW_PERMISSION  0
#define SHELL_HISTORY_MAX_NUMBER    8
#define SHELL_COMMAND_MAX_LENGTH    256
#define SHELL_PARAMETER_MAX_NUMBER  8
#define SHELL_SHOW_INFO             0
#define SHELL_CLS_WHEN_LOGIN        0
#define SHELL_COMMAND_MAX_LEN       256
#define SHELL_PRINT_BUFFER          256
#define SHELL_GET_TICK()            g_TickCount_1ms

/* Provide tick for shell timeouts */
extern volatile uint32 g_TickCount_1ms;

#endif
