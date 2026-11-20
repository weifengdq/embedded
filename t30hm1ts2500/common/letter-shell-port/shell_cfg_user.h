/**
 * @file  shell_cfg_user.h
 * @brief letter-shell 3.2.x 裸机配置
 */
#ifndef SHELL_CFG_USER_H
#define SHELL_CFG_USER_H

#define SHELL_USING_COMPANION       0
#define SHELL_SUPPORT_END_LINE      1
/* 串口终端 (minicom/putty/s25.py) 默认发送 CR, 因此只启用 CR 触发 */
#define SHELL_ENTER_LF              0
#define SHELL_ENTER_CR              1
#define SHELL_ENTER_CRLF            0
#define SHELL_SCAN_BUFFER           0
#define SHELL_USING_LOCK            0
#define SHELL_MAX_NUMBER            1
#define SHELL_PARAMETER_MAX_NUMBER  8
#define SHELL_HISTORY_MAX_NUMBER    10
#define SHELL_PRINT_BUFFER          128
/* 注意: 不能与命令名 "s2500" 重名, 否则 shell 会把命令项当作用户项, 误判密码 */
#define SHELL_DEFAULT_USER          "root"
/* 默认无密码, 避免上电后停在 "Please input password:" */
#define SHELL_DEFAULT_USER_PASSWORD ""

#endif
