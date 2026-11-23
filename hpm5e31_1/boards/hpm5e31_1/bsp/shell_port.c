#include "board.h"
#include "hpm_uart_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_interrupt.h"
#include "shell.h"
#include "shell_port.h"

#define SHELL_RX_RING_SIZE 512U
static volatile uint8_t s_rx_ring[SHELL_RX_RING_SIZE];
static volatile uint16_t s_rx_head, s_rx_tail;

SDK_DECLARE_EXT_ISR_M(BOARD_CONSOLE_UART_IRQ, shell_uart_isr)
void shell_uart_isr(void)
{
    UART_Type *base = (UART_Type *) BOARD_CONSOLE_UART_BASE;
    uint8_t c;
    while (uart_check_status(base, uart_stat_data_ready)) {
        c = uart_read_byte(base);
        uint16_t next = (uint16_t)((s_rx_head + 1U) % SHELL_RX_RING_SIZE);
        if (next != s_rx_tail) {
            s_rx_ring[s_rx_head] = c;
            s_rx_head = next;
        }
    }
}

unsigned short shell_port_read(char *data, unsigned short len)
{
    unsigned short n = 0;
    while ((n < len) && (s_rx_tail != s_rx_head)) {
        data[n++] = (char) s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % SHELL_RX_RING_SIZE);
    }
    return n;
}

unsigned short shell_port_write(char *data, unsigned short len)
{
    uart_send_data((UART_Type *) BOARD_CONSOLE_UART_BASE, (uint8_t *) data, (uint8_t) len);
    return len;
}

void shell_port_init(Shell *shell, char *buffer, unsigned short size)
{
    shell->read = shell_port_read;
    shell->write = shell_port_write;
    shellInit(shell, buffer, size);
    uart_enable_irq((UART_Type *) BOARD_CONSOLE_UART_BASE, uart_intr_rx_data_avail_or_timeout);
    intc_m_enable_irq_with_priority(BOARD_CONSOLE_UART_IRQ, 2);
}
