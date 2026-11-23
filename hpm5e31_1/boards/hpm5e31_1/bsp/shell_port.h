/*
 * letter-shell port for HPM5E31 / hpm5e31_1 board (UART0 console, IRQ driven RX)
 */
#ifndef SHELL_PORT_H
#define SHELL_PORT_H
#include "shell.h"
#ifdef __cplusplus
extern "C" {
#endif
void shell_port_init(Shell *shell, char *buffer, unsigned short size);
unsigned short shell_port_read(char *data, unsigned short len);
unsigned short shell_port_write(char *data, unsigned short len);
#ifdef __cplusplus
}
#endif
#endif
