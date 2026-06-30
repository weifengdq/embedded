#ifndef __SHELL_CFG_USER_H__
#define __SHELL_CFG_USER_H__

#include "main.h"

#define SHELL_TASK_WHILE            0
#define SHELL_USING_CMD_EXPORT      0
#define SHELL_HISTORY_MAX_NUMBER    10
#define SHELL_COMMAND_MAX_LENGTH    128
#define SHELL_PARAMETER_MAX_NUMBER  10
#define SHELL_SHOW_INFO             0
#define SHELL_CLS_WHEN_LOGIN        0
#define SHELL_GET_TICK()            HAL_GetTick()

#endif
