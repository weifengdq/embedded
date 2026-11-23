/*
 * gs_usb_proto.c - gs_usb 协议层实现（纯软件，无 USB/HPM 依赖）
 *
 * 所有结构布局与数值来自内核 7.0.0 gs_usb.c 的协议事实表（见 docs/gs_usb开发约定.md §3），
 * 本文件为按事实独立实现，不含任何 GPL 源码。
 */
#include <string.h>
#include "gs_usb.h"

/* ---------------- 能力常量 ---------------- */

void gs_usb_fill_bt_const(struct gs_device_bt_const *c)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    c->feature = GS_CAN_FEATURE_DEFAULT;
    c->fclk_can = 80000000U;      /* MCAN 外设时钟 80 MHz（board_init_can_clock 实测） */
    /* MOV: MCAN NBTP: NTSEG1 8bit(1..256), NTSEG2 7bit(1..128), NSJW 7bit(1..128), NBRP 9bit(1..512) */
    c->tseg1_min = 1U;
    c->tseg1_max = 256U;
    c->tseg2_min = 1U;
    c->tseg2_max = 128U;
    c->sjw_max = 128U;
    c->brp_min = 1U;
    c->brp_max = 512U;
    c->brp_inc = 1U;
}

void gs_usb_fill_bt_const_ext(struct gs_device_bt_const_ext *c)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    c->feature = GS_CAN_FEATURE_DEFAULT;
    c->fclk_can = 80000000U;
    c->tseg1_min = 1U;
    c->tseg1_max = 256U;
    c->tseg2_min = 1U;
    c->tseg2_max = 128U;
    c->sjw_max = 128U;
    c->brp_min = 1U;
    c->brp_max = 512U;
    c->brp_inc = 1U;
    /* MCAN DBTP: DTSEG1 5bit(1..32), DTSEG2 4bit(1..16), DSJW 4bit(1..16), DBRP 8bit(1..256) */
    c->dtseg1_min = 1U;
    c->dtseg1_max = 32U;
    c->dtseg2_min = 1U;
    c->dtseg2_max = 16U;
    c->dsjw_max = 16U;
    c->dbrp_min = 1U;
    c->dbrp_max = 256U;
    c->dbrp_inc = 1U;
}

void gs_usb_fill_device_config(struct gs_device_config *c)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    c->icount = (uint8_t) GS_USB_ICOUNT;
    c->sw_version = 0x00010000U;   /* 1.0 */
    c->hw_version = 0x00010000U;
}

/* ---------------- 位时序换算 ---------------- */

void gs_usb_bt_to_mcan(const struct gs_device_bittiming *bt, uint32_t *prescaler,
                       uint32_t *num_seg1, uint32_t *num_seg2, uint32_t *num_sjw)
{
    /* 内核 struct can_bittiming: prop_seg/phase_seg1/phase_seg2/sjw/brp（均为 TQ 数，1 基）
     * MCAN: NTSEG1 = prop_seg + phase_seg1 - 1；NTSEG2 = phase_seg2 - 1；NSJW = sjw - 1；
     *       NBRP = brp - 1；hpm_mcan_drv 的 num_seg1/num_seg2/num_sjw 是"寄存器值+1"，
     *       所以这里直接给 prop+phase1 / phase2 / sjw，prescaler 给 brp。 */
    uint32_t p = bt->brp ? bt->brp : 1U;
    uint32_t s1 = bt->prop_seg + bt->phase_seg1;
    uint32_t s2 = bt->phase_seg2 ? bt->phase_seg2 : 1U;
    uint32_t sjw = bt->sjw ? bt->sjw : 1U;

    if (s1 == 0U) {
        s1 = 1U;
    }
    if (prescaler != NULL) {
        *prescaler = p;
    }
    if (num_seg1 != NULL) {
        *num_seg1 = s1;
    }
    if (num_seg2 != NULL) {
        *num_seg2 = s2;
    }
    if (num_sjw != NULL) {
        *num_sjw = sjw;
    }
}

/* ---------------- DLC 换算 ---------------- */

uint8_t gs_usb_dlc_to_len(uint8_t dlc)
{
    static const uint8_t map[16] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
                                    8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U};
    return map[dlc & 0x0FU];
}

uint8_t gs_usb_len_to_dlc(uint8_t len)
{
    if (len <= 8U) {
        return len;
    }
    if (len <= 12U) {
        return 9U;
    }
    if (len <= 16U) {
        return 10U;
    }
    if (len <= 20U) {
        return 11U;
    }
    if (len <= 24U) {
        return 12U;
    }
    if (len <= 32U) {
        return 13U;
    }
    if (len <= 48U) {
        return 14U;
    }
    return 15U;
}

/* ---------------- 帧编解码 ---------------- */

uint8_t gs_usb_frame_flags(const struct gs_can_frame *f)
{
    uint8_t flags = 0U;
    if (f->fd) {
        flags |= GS_CAN_FLAG_FD;
    }
    if (f->fd && f->brs) {
        flags |= GS_CAN_FLAG_BRS;
    }
    if (f->fd && f->esi) {
        flags |= GS_CAN_FLAG_ESI;
    }
    return flags;
}

uint32_t gs_usb_encode_frame(uint8_t *out, uint32_t echo_id, const struct gs_can_frame *f,
                             uint32_t ts_us)
{
    struct gs_host_frame_header hdr;
    uint32_t data_len;
    uint32_t out_len;

    if ((out == NULL) || (f == NULL)) {
        return 0U;
    }
    data_len = gs_usb_dlc_to_len(gs_usb_len_to_dlc(f->len));
    out_len = f->fd ? GS_USB_RX_FD_LEN : GS_USB_RX_CLASSIC_LEN;

    memset(out, 0, out_len);

    hdr.echo_id = echo_id;
    hdr.can_id = f->id & GS_CAN_EFF_MASK;
    if (f->ext) {
        hdr.can_id |= GS_CAN_EFF_FLAG;
    }
    if (f->rtr) {
        hdr.can_id |= GS_CAN_RTR_FLAG;
    }
    if (f->err) {
        hdr.can_id |= GS_CAN_ERR_FLAG;   /* 错误帧：类别位在 f->id 低 29 位里 */
    }
    hdr.can_dlc = gs_usb_len_to_dlc((uint8_t) (f->rtr ? f->len : data_len));
    hdr.channel = 0U;               /* 调用者按通道填 */
    hdr.flags = gs_usb_frame_flags(f);
    hdr.reserved = 0U;
    memcpy(out, &hdr, sizeof(hdr));

    if (!f->rtr && (data_len > 0U)) {
        memcpy(out + GS_USB_FRAME_HDR_LEN, f->data, data_len);
    }
    /* 时间戳固定在"最大数据长度"之后：经典 12+8，FD 12+64 */
    {
        uint32_t ts_off = GS_USB_FRAME_HDR_LEN + (f->fd ? 64U : 8U);
        memcpy(out + ts_off, &ts_us, sizeof(ts_us));
    }
    return out_len;
}

int gs_usb_decode_tx(const uint8_t *in, uint32_t len, uint32_t *echo_id,
                     struct gs_can_frame *f, uint8_t *channel, uint8_t *frame_flags)
{
    struct gs_host_frame_header hdr;
    bool fd;
    uint8_t data_len;

    if ((in == NULL) || (f == NULL) || (len < GS_USB_FRAME_HDR_LEN)) {
        return -1;
    }
    memcpy(&hdr, in, sizeof(hdr));

    /* 主机侧 TX 尺寸只有 20（经典）/76（FD）；内核按模式固定，不携带时间戳 */
    if ((len != GS_USB_TX_CLASSIC_LEN) && (len != GS_USB_TX_FD_LEN)) {
        return -2;
    }
    fd = (len == GS_USB_TX_FD_LEN) || ((hdr.flags & GS_CAN_FLAG_FD) != 0U);
    if ((hdr.channel >= GS_USB_CHANNELS) || (echo_id == NULL)) {
        return -3;
    }
    if (hdr.can_dlc > 15U) {
        return -4;
    }
    if (!fd && (hdr.can_dlc > 8U)) {
        return -5;
    }
    *echo_id = hdr.echo_id;
    if (hdr.echo_id >= GS_USB_MAX_ECHO_ID) {
        return -6;
    }
    if (channel != NULL) {
        *channel = hdr.channel;
    }
    if (frame_flags != NULL) {
        *frame_flags = hdr.flags;
    }

    memset(f, 0, sizeof(*f));
    f->id = hdr.can_id & GS_CAN_EFF_MASK;
    f->ext = (hdr.can_id & GS_CAN_EFF_FLAG) ? 1U : 0U;
    f->rtr = (hdr.can_id & GS_CAN_RTR_FLAG) ? 1U : 0U;
    f->fd = fd ? 1U : 0U;
    f->brs = ((hdr.flags & GS_CAN_FLAG_BRS) && fd) ? 1U : 0U;
    f->esi = ((hdr.flags & GS_CAN_FLAG_ESI) && fd) ? 1U : 0U;
    data_len = gs_usb_dlc_to_len(hdr.can_dlc);
    f->len = f->rtr ? (uint8_t) hdr.can_dlc : data_len;
    if (!f->rtr && (data_len > 0U)) {
        memcpy(f->data, in + GS_USB_FRAME_HDR_LEN, data_len);
    }
    return 0;
}

uint32_t gs_usb_map_state(bool bus_off, bool error_passive, bool error_warning, bool started)
{
    if (!started) {
        return GS_CAN_STATE_STOPPED;
    }
    if (bus_off) {
        return GS_CAN_STATE_BUS_OFF;
    }
    if (error_passive) {
        return GS_CAN_STATE_ERROR_PASSIVE;
    }
    if (error_warning) {
        return GS_CAN_STATE_ERROR_WARNING;
    }
    return GS_CAN_STATE_ERROR_ACTIVE;
}
