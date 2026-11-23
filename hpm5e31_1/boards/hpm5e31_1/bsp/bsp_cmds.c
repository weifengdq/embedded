/*
 * default letter-shell commands for every hpm5e31_1 sample
 */
#include <string.h>
#include "bsp.h"
#include "hpm_sysctl_drv.h"
#include "hpm_ppor_drv.h"
#include "hpm_csr_drv.h"

static void cmd_ver(void)
{
    printf("board       : %s\r\n", BOARD_NAME);
    printf("soc         : HPM5E31 (RV32-IMAFDCBP @ %lu Hz)\r\n",
           (unsigned long) clock_get_frequency(clock_cpu0));
    printf("sdk         : HPM SDK v1.13.0\r\n");
    printf("build       : " __DATE__ " " __TIME__ "\r\n");
    printf("letter-shell: 3.2.4\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN, ver, cmd_ver, show version info);

static void cmd_uptime(void)
{
    uint32_t ms = bsp_uptime_ms();
    printf("uptime: %lu.%03lu s\r\n", (unsigned long) (ms / 1000U), (unsigned long) (ms % 1000U));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN, uptime, cmd_uptime, show uptime);

static void cmd_reboot(void)
{
    printf("rebooting...\r\n");
    bsp_reboot();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN, reboot, cmd_reboot, software reset);

static void cmd_clk(void)
{
    printf("cpu0   : %lu Hz\r\n", (unsigned long) clock_get_frequency(clock_cpu0));
    printf("ahb0   : %lu Hz\r\n", (unsigned long) clock_get_frequency(clock_ahb0));
    printf("axif   : %lu Hz\r\n", (unsigned long) clock_get_frequency(clock_axif));
    printf("axis   : %lu Hz\r\n", (unsigned long) clock_get_frequency(clock_axis));
    printf("axic   : %lu Hz\r\n", (unsigned long) clock_get_frequency(clock_axic));
    printf("xpi0   : %lu Hz\r\n", (unsigned long) clock_get_frequency(clock_xpi0));
    printf("mchtmr : %lu Hz\r\n", (unsigned long) clock_get_frequency(clock_mchtmr0));
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN, clk, cmd_clk, print main clock tree);

static void cmd_pin(int argc, char *argv[])
{
    if (argc < 2) { printf("usage: pin <A..F><n> [0|1]\r\n"); return; }
    char port_ch = argv[1][0];
    if (port_ch >= 'a' && port_ch <= 'f') port_ch = (char) (port_ch - 32);
    if (port_ch < 'A' || port_ch > 'F') { printf("bad port\r\n"); return; }
    uint32_t pin;
    if (bsp_parse_u32(&argv[1][1], &pin) || pin > 31) { printf("bad pin\r\n"); return; }
    uint8_t port = (uint8_t) (port_ch - 'A');
    if (argc >= 3) {
        uint32_t v;
        if (bsp_parse_u32(argv[2], &v)) { printf("bad value\r\n"); return; }
        bsp_gpio_output_init(port, (uint8_t) pin);
        gpio_write_pin(HPM_GPIO0, (uint32_t) port, pin, v ? 1 : 0);
        printf("%c%lu <- %lu\r\n", port_ch, (unsigned long) pin, (unsigned long) v);
    } else {
        bsp_gpio_input_init(port, (uint8_t) pin);
        printf("%c%lu = %d\r\n", port_ch, (unsigned long) pin,
               gpio_read_pin(HPM_GPIO0, (uint32_t) port, pin));
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN, pin, cmd_pin, gpio read/write: pin <A..F><n> [0|1]);

static void cmd_reg(int argc, char *argv[])
{
    uint32_t addr;
    if (argc < 2 || bsp_parse_u32(argv[1], &addr)) { printf("usage: reg <addr> [value]\r\n"); return; }
    volatile uint32_t *p = (volatile uint32_t *) (uintptr_t) addr;
    if (argc >= 3) {
        uint32_t v;
        if (bsp_parse_u32(argv[2], &v)) { printf("bad value\r\n"); return; }
        *p = v;
        printf("[0x%08lx] <- 0x%08lx\r\n", (unsigned long) addr, (unsigned long) v);
    }
    printf("[0x%08lx] = 0x%08lx\r\n", (unsigned long) addr, (unsigned long) *p);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN, reg, cmd_reg, read/write 32-bit register: reg <addr> [value]);

static void cmd_mem(int argc, char *argv[])
{
    uint32_t addr, len = 64;
    if (argc < 2 || bsp_parse_u32(argv[1], &addr)) { printf("usage: mem <addr> [len]\r\n"); return; }
    if (argc >= 3) bsp_parse_u32(argv[2], &len);
    bsp_hexdump((const void *) (uintptr_t) addr, len);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN, mem, cmd_mem, hexdump memory: mem <addr> [len]);
