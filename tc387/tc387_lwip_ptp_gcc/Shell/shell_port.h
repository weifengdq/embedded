#ifndef __SHELL_PORT_H
#define __SHELL_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

int  Shell_Init(void);
void Shell_Process(void);
void Shell_PrintBanner(void);

#ifdef __cplusplus
}
#endif

#endif
