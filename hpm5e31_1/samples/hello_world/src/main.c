/*
 * hpm5e31_1 bring-up: hello world + clock summary
 */
#include "board.h"
#include "hpm_debug_console.h"
#include "hpm_clock_drv.h"

int main(void)
{
    board_init();
    printf("\r\n[hpm5e31_1] hello world (%s board)\r\n", BOARD_NAME);
    printf("cpu0 : %lu Hz\r\n", (unsigned long)clock_get_frequency(clock_cpu0));
    printf("ahb  : %lu Hz\r\n", (unsigned long)clock_get_frequency(clock_ahb0));
    printf("xpi0 : %lu Hz\r\n", (unsigned long)clock_get_frequency(clock_xpi0));
    uint32_t n = 0;
    while (1) {
        board_delay_ms(1000);
        n++;
        printf("tick %lu, uptime %lu ms\r\n", (unsigned long)n, (unsigned long)(n * 1000));
    }
    return 0;
}
