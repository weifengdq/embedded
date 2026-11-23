/*
 * hpm5e31_1 board support package: console + letter-shell + common helpers
 */
#ifndef HPM_BSP_H
#define HPM_BSP_H

#include <stdio.h>
#include "board.h"
#include "hpm_debug_console.h"
#include "hpm_clock_drv.h"
#include "hpm_gpio_drv.h"
#include "hpm_uart_drv.h"
#include "shell.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_LOG(...)  printf(__VA_ARGS__)

void bsp_init(void);
void bsp_shell_poll(void);
void bsp_reboot(void);
uint32_t bsp_uptime_ms(void);
void bsp_gpio_output_init(uint8_t port, uint8_t pin);
void bsp_gpio_input_init(uint8_t port, uint8_t pin);
void bsp_hexdump(const void *addr, uint32_t len);
int bsp_parse_u32(const char *s, uint32_t *out);

#ifdef __cplusplus
}
#endif
#endif
