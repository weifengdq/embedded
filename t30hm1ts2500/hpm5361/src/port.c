/**
 * @file  port.c
 * @brief common/s2500 移植层: 串口 / 复位
 */
#include "bsp.h"
#include "s2500_port.h"
#include "hpm_soc.h"
#include "hpm_ppor_drv.h"

int s2500_port_uart_getc(void)
{
    return bsp_uart_getc();
}

void s2500_port_uart_write(const char *data, unsigned int len)
{
    bsp_uart_write(data, len);
}

void s2500_port_reboot(void)
{
    ppor_sw_reset(HPM_PPOR, 0x10);   /* 写 0 会取消复位, 需 >2 */
    for (;;) {
    }
}
