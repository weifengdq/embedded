/*
 * gs_usb_candle - gs_usb / candleLight 兼容 4 路 CAN FD USB 适配器（HPM5E31）
 *
 * 目标：插到 Linux（内核 7.0.0 gs_usb.ko）直接出现 can0~can3，SocketCAN 工具可用。
 * 硬件：USB0(Pin67/68) → 主机；CAN0 PD00/PD01、CAN1 PD05/PD04、CAN2 PC08/PA09、CAN3 PC15/PC14；
 *       CAN0↔CAN1、CAN2↔CAN3 板内两两相连（可做本机自测）；终端电阻手动拨码（软件不实现）。
 *
 * 组件：components/gs_usb（协议层，纯软件）；本目录 usb_gs.c（USB 设备层）+ bridge.c（MCAN 桥接）。
 * 协议事实与防内核崩溃规则见 docs/gs_usb开发约定.md。
 */
#include <string.h>
#include "bsp.h"
#include "gs_usb.h"
#include "bridge.h"
#include "usb_gs.h"
#include "hpm_usb_drv.h"

static void cmd_usbinfo(int argc, char *argv[])
{
    uint32_t in_drop = 0U, out_drop = 0U, in_sent = 0U;
    uint32_t susp = 0U, resm = 0U, rst = 0U;

    (void) argc;
    (void) argv;
    gs_usb_usb_stats(&in_drop, &out_drop, &in_sent);
    gs_usb_usb_pm_stats(&susp, &resm, &rst);
    printf("[usb] pm: suspend=%lu resume=%lu reset=%lu sdis=%u\r\n",
           (unsigned long) susp, (unsigned long) resm, (unsigned long) rst, GS_USB_SET_SDIS);
    printf("[usb] configured=%u ep_in_mps=%u ep_out_mps=%u in_sent=%lu in_drop=%lu out_drop=%lu\r\n",
           gs_usb_usb_ready() ? 1U : 0U,
           (unsigned) usbd_get_ep_mps(0, GS_USB_EP_IN),
           (unsigned) usbd_get_ep_mps(0, GS_USB_EP_OUT),
           (unsigned long) in_sent, (unsigned long) in_drop, (unsigned long) out_drop);
    printf("[usb] VID:PID=%04X:%04X ch=%u icount=%u feature=0x%04X\r\n",
           (unsigned) GS_USB_VID, (unsigned) GS_USB_PID,
           (unsigned) GS_USB_CHANNELS, (unsigned) GS_USB_ICOUNT, (unsigned) GS_CAN_FEATURE_DEFAULT);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 usbinfo, cmd_usbinfo, show gs_usb device state);

static void cmd_gsstat(int argc, char *argv[])
{
    uint32_t rx = 0U, tx = 0U, drop = 0U, echo = 0U;
    uint32_t in_drop = 0U, out_drop = 0U, in_sent = 0U;

    (void) argc;
    (void) argv;
    gs_bridge_counters(&rx, &tx, &drop, &echo);
    gs_usb_usb_stats(&in_drop, &out_drop, &in_sent);
    printf("[gs] rx_frames=%lu tx_frames=%lu echo=%lu tx_drop=%lu tx_retry=%lu err_frames=%lu berr_mask=0x%lX\r\n",
           (unsigned long) rx, (unsigned long) tx, (unsigned long) echo,
           (unsigned long) drop, (unsigned long) gs_bridge_tx_retry(),
           (unsigned long) gs_bridge_err_frames(),
           (unsigned long) gs_bridge_berr_enable_mask());
    printf("[gs] usb: configured=%u in_sent=%lu in_drop=%lu out_drop=%lu\r\n",
           gs_usb_usb_ready() ? 1U : 0U,
           (unsigned long) in_sent, (unsigned long) in_drop, (unsigned long) out_drop);
    for (uint8_t ch = 0; ch < GS_USB_CHANNELS; ch++) {
        struct gs_device_state st;
        memset(&st, 0, sizeof(st));
        gs_bridge_can_get_state(ch, &st);
        printf("[gs] ch%u started=%u state=%lu tec=%lu rec=%lu\r\n", (unsigned) ch,
               gs_bridge_channel_started(ch) ? 1U : 0U,
               (unsigned long) st.state, (unsigned long) st.txerr, (unsigned long) st.rxerr);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 gsstat, cmd_gsstat, show gs_usb counters per channel);

static void cmd_ctrllog(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    gs_usb_ctrl_dump();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ctrllog, cmd_ctrllog, dump last gs_usb control requests);

int main(void)
{
    uint32_t last_ms;

    bsp_init();
    printf("\r\n=== gs_usb_candle: gs_usb/candleLight 4x CAN FD (HPM5E31) ===\r\n");
    printf("[gs] VID:PID=%04X:%04X feature=0x%04X(timestamp/FD/GET_STATE/BERR)\r\n",
           (unsigned) GS_USB_VID, (unsigned) GS_USB_PID, (unsigned) GS_CAN_FEATURE_DEFAULT);

    if (gs_bridge_init() != 0) {
        printf("[gs] CAN init FAILED\r\n");
    }
    gs_usb_usb_init();
    printf("[gs] usb device initialized, waiting for host enumeration...\r\n");

    last_ms = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        gs_bridge_poll();      /* CAN↔USB 帧搬运 */
        gs_usb_usb_poll();     /* IN/OUT 端点续传 */
        if (bsp_uptime_ms() - last_ms >= 5000U) {
            uint32_t rx = 0U, tx = 0U, drop = 0U, echo = 0U, in_drop = 0U, out_drop = 0U, in_sent = 0U;
            last_ms = bsp_uptime_ms();
            gs_bridge_counters(&rx, &tx, &drop, &echo);
            gs_usb_usb_stats(&in_drop, &out_drop, &in_sent);
            printf("[gs] usb=%u rx=%lu tx=%lu echo=%lu in_sent=%lu in_drop=%lu out_drop=%lu\r\n",
                   gs_usb_usb_ready() ? 1U : 0U,
                   (unsigned long) rx, (unsigned long) tx, (unsigned long) echo,
                   (unsigned long) in_sent, (unsigned long) in_drop, (unsigned long) out_drop);
        }
    }
    return 0;
}