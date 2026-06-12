/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <stdio.h>
#include "board.h"
#include "hpm_debug_console.h"

int main(void)
{
    int ch;

    board_init();

    printf("hello world from hpm5e31_hello_world\n");
    printf("uart0 ready: PA00(TX) / PA01(RX), 115200-8-N-1\n");

    while (1) {
        ch = getchar();
        if (ch == '\r') {
            ch = '\n';
        }
        putchar(ch);
    }

    return 0;
}