/*
 * usb_cdc_acm_echo - USB0 device (CDC ACM 虚拟串口) 回显
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1), USB0 接主机
 *   控制台仍是 UART0 (/dev/ttyACM0, CH343), USB CDC 会枚举成 /dev/ttyACM1/ACM2
 *   CherryUSB device stack + CDC ACM, 收到什么就回什么(含 ZLP 处理)
 *
 * 现象: 见 README.md
 */
#include <string.h>
#include "bsp.h"
#include "usb_config.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "hpm_usb_drv.h"

extern volatile bool dtr_enable;
extern volatile uint32_t g_cdc_echo_ticks;
extern void cdc_acm_init(uint8_t busid, uint32_t reg_base);
extern uint8_t write_buffer[2048];

static void cmd_usbinfo(void)
{
    printf("USB0 device @0x%08lx, IRQn=%d\r\n",
           (unsigned long) CONFIG_HPM_USBD_BASE, (int) CONFIG_HPM_USBD_IRQn);
    printf("  class     : CDC ACM (CherryUSB), VID=0x%04x PID=0x%04x\r\n",
           (unsigned) USBD_VID, (unsigned) USBD_PID);
    printf("  DTR       : %d (host opened the virtual COM port)\r\n", dtr_enable ? 1 : 0);
    printf("  ep mps    : in=%u out=%u\r\n", (unsigned) usbd_get_ep_mps(0, 0x81), (unsigned) usbd_get_ep_mps(0, 0x01));
    printf("  echo tick : %lu (每回显一包 +1)\r\n", (unsigned long) g_cdc_echo_ticks);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 usbinfo, cmd_usbinfo, USB CDC ACM status);

static void cmd_usbsend(int argc, char *argv[])
{
    uint32_t n = 64;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    if (n > sizeof(write_buffer)) {
        n = sizeof(write_buffer);
    }
    for (uint32_t i = 0; i < n; i++) {
        write_buffer[i] = (uint8_t) ('A' + (i % 26U));
    }
    write_buffer[n - 1] = '\n';
    if (usbd_ep_start_write(0, 0x81, write_buffer, n) != 0) {
        printf("usb send failed (DTR=%d)\r\n", dtr_enable ? 1 : 0);
        return;
    }
    printf("usb tx %lu bytes, echo_tick=%lu\r\n", (unsigned long) n,
           (unsigned long) g_cdc_echo_ticks);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 usbsend, cmd_usbsend, send N bytes to host: usbsend [n]);

int main(void)
{
    bsp_init();

    printf("\r\n=== usb_cdc_acm_echo: USB0 CDC ACM device ===\r\n");

    board_init_usb((USB_Type *) CONFIG_HPM_USBD_BASE);
    /* 最小系统板没有 VBUS 检测引脚: 用内部 VBUS 指示, 否则不会上拉 D+ 附着 */
    usb_phy_using_internal_vbus((USB_Type *) CONFIG_HPM_USBD_BASE);
    intc_set_irq_priority(CONFIG_HPM_USBD_IRQn, 2);
    cdc_acm_init(0, CONFIG_HPM_USBD_BASE);
    printf("cdc_acm_init done, waiting for host enumeration...\r\n");
    printf("shell: usbinfo | usbsend [n]\r\n");

    uint32_t last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if (bsp_uptime_ms() - last >= 2000U) {
            last = bsp_uptime_ms();
            printf("usb: dtr=%d echo_tick=%lu\r\n", dtr_enable ? 1 : 0,
                   (unsigned long) g_cdc_echo_ticks);
        }
    }
    return 0;
}
