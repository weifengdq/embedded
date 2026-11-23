/*
 * <sample name> - <一句话说明>
 *
 * 硬件: hpm5e31_1
 * 现象: 见 README.md
 */
#include "bsp.h"

static void cmd_info(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("sample: %s\r\n", "TODO");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, show sample info);

int main(void)
{
    bsp_init();
    printf("\r\n[sample] start\r\n");
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
