/*
 * usb_hid_keyboard - USB0 device (HID 键盘) 自动按键 + 主机侧枚举验证
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1), USB0 接主机
 *   控制台仍是 UART0 (/dev/ttyACM0); HID 设备在主机上不产生 ttyACM, 而是 input 设备:
 *   主机 dmesg/journalctl 可见 "hid-generic ... input,hidrawN: USB HID v1.01 Keyboard [HPMicro HID DEMO]"
 *   本示例每秒自动按一次 'a'(按下+释放), 主机侧可观察 input 事件。
 *
 * 现象: 见 README.md
 */
#include <stdio.h>
#include "bsp.h"
#include "usb_config.h"
#include "hpm_usb_drv.h"
#include "usbd_core.h"

extern void hid_keyboard_init(uint8_t busid, uint32_t reg_base);
extern int hid_keyboard_send_key(uint8_t usage, bool press);
extern volatile uint32_t g_hid_tx_cnt;

static void cmd_hidinfo(void)
{
    printf("USB0 HID keyboard @0x%08lx, IRQn=%d\r\n",
           (unsigned long) CONFIG_HPM_USBD_BASE, (int) CONFIG_HPM_USBD_IRQn);
    printf("  VID/PID   : 0x%04x/0x%04x  Product=HPMicro HID DEMO\r\n",
           (unsigned) USBD_VID, (unsigned) USBD_PID);
    printf("  configured: %d\r\n", usb_device_is_configured(0) ? 1 : 0);
    printf("  HID tx cnt: %lu (8 字节报告, 端点 0x81, 间隔 10 ms)\r\n",
           (unsigned long) g_hid_tx_cnt);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 hidinfo, cmd_hidinfo, USB HID keyboard status);

static void cmd_hidkey(int argc, char *argv[])
{
    uint32_t n = 1;
    if (argc >= 2 && (bsp_parse_u32(argv[1], &n) || n == 0 || n > 100)) {
        printf("usage: hidkey [1..100] (发送 'a' 按下+释放 n 次)\r\n");
        return;
    }
    if (!usb_device_is_configured(0)) {
        printf("USB 未配置, 主机还没枚举完成\r\n");
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        hid_keyboard_send_key(0x04, true);   /* HID usage 0x04 = 'a' */
        board_delay_ms(50);
        hid_keyboard_send_key(0x04, false);
        board_delay_ms(50);
    }
    printf("hidkey: 已发送 %lu 次 'a' 按下/释放, tx_cnt=%lu\r\n",
           (unsigned long) n, (unsigned long) g_hid_tx_cnt);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 hidkey, cmd_hidkey, send 'a' key press/release n times: hidkey [n]);

int main(void)
{
    bsp_init();

    printf("\r\n=== usb_hid_keyboard: USB0 HID keyboard device ===\r\n");
    board_init_usb((USB_Type *) CONFIG_HPM_USBD_BASE);
    usb_phy_using_internal_vbus((USB_Type *) CONFIG_HPM_USBD_BASE);
    intc_set_irq_priority(CONFIG_HPM_USBD_IRQn, 2);
    hid_keyboard_init(0, CONFIG_HPM_USBD_BASE);
    printf("hid_keyboard_init done, waiting for host enumeration...\r\n");
    printf("shell: hidinfo | hidkey [n]\r\n");

    uint32_t last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if (bsp_uptime_ms() - last >= 1000U) {
            last = bsp_uptime_ms();
            if (usb_device_is_configured(0)) {
                hid_keyboard_send_key(0x04, true);
                board_delay_ms(30);
                hid_keyboard_send_key(0x04, false);
            }
            printf("usb: configured=%d hid_tx=%lu\r\n",
                   usb_device_is_configured(0) ? 1 : 0, (unsigned long) g_hid_tx_cnt);
        }
    }
    return 0;
}
