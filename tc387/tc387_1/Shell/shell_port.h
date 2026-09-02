#ifndef __SHELL_PORT_H
#define __SHELL_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int Shell_Init(void);
void Shell_Process(void);
void Shell_PrintBanner(void);
int Shell_Exec(const char *cmd);

/* Called from UART ISR when a byte is received */
void Shell_RxPush(uint8_t ch);
void Shell_RxPushBulk(const uint8_t *data, uint16_t len);
extern volatile uint32_t gShellRxOverflow;

#ifdef __cplusplus
}
#endif

#endif
