#include <string.h>
#include "bsp.h"
#include "shell_port.h"
#include "hpm_ppor_drv.h"

#define BSP_SHELL_BUFFER_SIZE 1024U
static Shell s_shell;
static char s_shell_buf[BSP_SHELL_BUFFER_SIZE];

void bsp_init(void)
{
    board_init();
    shell_port_init(&s_shell, s_shell_buf, sizeof(s_shell_buf));
}

void bsp_shell_poll(void)
{
    shellTask(&s_shell);
}

void bsp_reboot(void)
{
    board_delay_ms(10);
    ppor_sw_reset(HPM_PPOR, 100);
}

uint32_t bsp_uptime_ms(void)
{
    /* 绝对时基：直接用 mcycle 换算，避免"每轮 <1ms 调用时整数截断为 0、时间冻结"的易错点
     * （2026-10-30 由 tp-xrce/tp-ptp 实测发现：旧实现按调用间隔累加，间隔不足 1ms 时截断成 0）。 */
    uint32_t cpu = clock_get_frequency(clock_cpu0);
    return (uint32_t) (hpm_csr_get_core_mcycle() * 1000ULL / (uint64_t) cpu);
}

void bsp_gpio_output_init(uint8_t port, uint8_t pin)
{
    gpio_set_pin_output(HPM_GPIO0, (uint32_t) port, pin);
}

void bsp_gpio_input_init(uint8_t port, uint8_t pin)
{
    gpio_set_pin_input(HPM_GPIO0, (uint32_t) port, pin);
}

void bsp_hexdump(const void *addr, uint32_t len)
{
    const uint8_t *p = (const uint8_t *) addr;
    for (uint32_t i = 0; i < len; i += 16) {
        printf("%08lx:", (unsigned long) (uintptr_t) (p + i));
        for (uint32_t j = 0; j < 16 && (i + j) < len; j++) {
            printf(" %02x", p[i + j]);
        }
        printf("\r\n");
    }
}

int bsp_parse_u32(const char *s, uint32_t *out)
{
    if (!s || !out) return -1;
    uint32_t v = 0; int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    if (*s == 0) return -1;
    while (*s) {
        char c = *s++;
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t) (c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t) (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t) (c - 'A' + 10);
        else return -1;
        v = v * (uint32_t) base + d;
    }
    *out = v;
    return 0;
}
