/*
 * usb_gs.h - gs_usb USB 设备层（CherryUSB vendor class）内部接口
 */
#ifndef GS_USB_USB_H
#define GS_USB_USB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A/B：是否设置 USB0 USBMODE.SDIS（roboparty 参考实现设了该位） */
#ifndef GS_USB_SET_SDIS
#define GS_USB_SET_SDIS 1
#endif

void gs_usb_usb_init(void);
bool gs_usb_usb_ready(void);
int  gs_usb_usb_send(const uint8_t *frame, uint32_t len);   /* 0 = 入队成功 */
void gs_usb_usb_poll(void);
void gs_usb_usb_stats(uint32_t *in_drop, uint32_t *out_drop, uint32_t *in_sent);
void gs_usb_ctrl_dump(void);
void gs_usb_usb_pm_stats(uint32_t *suspend, uint32_t *resume, uint32_t *reset);

#ifdef __cplusplus
}
#endif
#endif