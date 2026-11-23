/*
 * app_isotp_port.c - SDK MCAN 直驱的 ISO-TP 端口
 */
#include "app_isotp_port.h"

#include <string.h>

#include "board.h"
#include "ext/board_can.h"
#include "hpm_mchtmr_drv.h"

static int app_isotp_port_index(const app_isotp_port_t *p)
{
    if (p->base == BOARD_CAN0_BASE) { return 0; }
    if (p->base == BOARD_CAN1_BASE) { return 1; }
    if (p->base == BOARD_CAN2_BASE) { return 2; }
    return 3;
}

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_mbuf[APP_ISOTP_PORT_MAX][MCAN_MSG_BUF_SIZE_IN_WORDS];
#define TP_PORT_MBUF(p) s_mbuf[app_isotp_port_index(p)]
#else
#define TP_PORT_MBUF(p) NULL
#endif

/* 位时序：80 MHz 外设时钟下的标准点（与 samples/can/mcan_two_node 实测一致） */
static void tp_bit_timing(app_isotp_port_t *p, mcan_config_t *cfg)
{
    uint32_t clk = p->clk_hz;
    uint32_t arb = p->nominal_bitrate;

    cfg->use_lowlevel_timing_setting = true;
    if ((clk == 80000000UL) && (arb == 500000UL)) {
        /* 500 kbit/s，80 MHz / 1 / 160 TQ：seg1=139 seg2=20，采样点 87.5% */
        cfg->can_timing.prescaler = 1U;
        cfg->can_timing.num_seg1 = 139U;
        cfg->can_timing.num_seg2 = 20U;
        cfg->can_timing.num_sjw = 4U;
    } else if ((clk == 80000000UL) && (arb == 1000000UL)) {
        /* 1 Mbit/s，presc=2 / 40 TQ：seg1=29 seg2=10，采样点 75% */
        cfg->can_timing.prescaler = 2U;
        cfg->can_timing.num_seg1 = 29U;
        cfg->can_timing.num_seg2 = 10U;
        cfg->can_timing.num_sjw = 2U;
    } else {
        uint32_t tq = clk / arb;
        uint32_t seg2;
        if (tq < 8u) { tq = 8u; }
        if (tq > 385u) { tq = 385u; }
        seg2 = tq / 5u;
        if (seg2 < 2u) { seg2 = 2u; }
        cfg->can_timing.prescaler = 1U;
        cfg->can_timing.num_seg1 = tq - 1u - seg2;
        cfg->can_timing.num_seg2 = seg2;
        cfg->can_timing.num_sjw = seg2 / 2u ? seg2 / 2u : 1u;
    }
    if (p->fd) {
        if ((clk == 80000000UL) && (p->data_bitrate == 2000000UL)) {
            /* 数据段 2 Mbit/s，presc=2 / 40 TQ：seg1=14 seg2=5，采样点 75% */
            cfg->canfd_timing.prescaler = 2U;
            cfg->canfd_timing.num_seg1 = 14U;
            cfg->canfd_timing.num_seg2 = 5U;
            cfg->canfd_timing.num_sjw = 2U;
        } else {
            uint32_t tq = clk / p->data_bitrate;
            uint32_t seg2;
            if (tq < 8u) { tq = 8u; }
            if (tq > 385u) { tq = 385u; }
            seg2 = tq / 5u;
            if (seg2 < 2u) { seg2 = 2u; }
            cfg->canfd_timing.prescaler = 1U;
            cfg->canfd_timing.num_seg1 = tq - 1u - seg2;
            cfg->canfd_timing.num_seg2 = seg2;
            cfg->canfd_timing.num_sjw = seg2 / 2u ? seg2 / 2u : 1u;
        }
        cfg->canfd_timing.enable_tdc = true;
        cfg->enable_tdc = true;
        cfg->tdc_config.ssp_offset = 15U;
        cfg->tdc_config.filter_window_length = 15U;
    }
}

int app_isotp_port_init(app_isotp_port_t *p, const app_isotp_port_cfg_t *cfg)
{
    mcan_config_t mcfg;
    hpm_stat_t st;

    if ((p == NULL) || (cfg == NULL) || (cfg->base == NULL)) {
        return APP_ISOTP_ERR_ARG;
    }
    memset(p, 0, sizeof(*p));
    p->base = cfg->base;
    p->fd = cfg->fd;
    p->brs = cfg->brs && cfg->fd;
    p->nominal_bitrate = cfg->nominal_bitrate ? cfg->nominal_bitrate : 500000UL;
    p->data_bitrate = cfg->data_bitrate ? cfg->data_bitrate : 2000000UL;

    board_init_can(p->base);
    p->clk_hz = board_init_can_clock(p->base);   /* 必须先开时钟，否则 mcan_deinit 挂死 */
    if (p->clk_hz == 0u) {
        return APP_ISOTP_ERR_LINK;
    }
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    {
        mcan_msg_buf_attr_t attr = {(uint32_t) (uintptr_t) TP_PORT_MBUF(p),
                                    MCAN_MSG_BUF_SIZE_IN_WORDS * 4U};
        mcan_set_msg_buf_attr(p->base, &attr);
    }
#endif
    mcan_get_default_config(p->base, &mcfg);
    mcfg.mode = mcan_mode_normal;
    mcfg.enable_canfd = p->fd;
    if (p->fd) {
        mcan_get_default_ram_config(p->base, &mcfg.ram_config, true);
    }
    tp_bit_timing(p, &mcfg);

    mcan_deinit(p->base);
    st = mcan_init(p->base, &mcfg, p->clk_hz);
    if (st != status_success) {
        return APP_ISOTP_ERR_LINK;
    }
    return APP_ISOTP_OK;
}

/* MCAN 的 dlc 字段是 DLC 编码（0..15），不是字节数：9=12B, 10=16B, ... 15=64B */
static const uint8_t k_dlc_bytes[16] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u,
                                        8u, 12u, 16u, 20u, 24u, 32u, 48u, 64u};

static uint8_t tp_len_to_dlc(uint8_t len)
{
    for (uint8_t i = 0u; i < 16u; i++) {
        if (len <= k_dlc_bytes[i]) {
            return i;
        }
    }
    return 15u;
}

int app_isotp_port_send(app_isotp_port_t *p, const app_isotp_frame_t *f)
{
    mcan_tx_frame_t tx;

    memset(&tx, 0, sizeof(tx));
    if (f->ext) {
        tx.ext_id = f->id;
        tx.use_ext_id = 1U;
    } else {
        tx.std_id = (uint16_t) f->id;
        tx.use_ext_id = 0U;
    }
    tx.rtr = 0U;
    tx.canfd_frame = f->fd ? 1U : 0U;
    tx.bitrate_switch = (f->fd && p->brs) ? 1U : 0U;
    if (f->len > 64u) {
        return APP_ISOTP_ERR_ARG;
    }
    tx.dlc = tp_len_to_dlc(f->len);
    memcpy(tx.data_8, f->data, f->len);
    if (mcan_transmit_blocking(p->base, &tx) != status_success) {
        p->tx_err++;
        return APP_ISOTP_ERR_LINK;
    }
    p->tx_cnt++;
    return APP_ISOTP_OK;
}

int app_isotp_port_recv(app_isotp_port_t *p, app_isotp_frame_t *f)
{
    mcan_rx_message_t rx;

    if (mcan_read_rxfifo(p->base, 0, &rx) != status_success) {
        return 0;
    }
    if (rx.dlc > 15u) {
        p->rx_drop++;
        return 0;
    }
    f->id = rx.use_ext_id ? rx.ext_id : (uint32_t) rx.std_id;
    f->ext = rx.use_ext_id ? true : false;
    f->fd = rx.canfd_frame ? true : false;
    f->brs = rx.bitrate_switch ? true : false;
    f->len = k_dlc_bytes[rx.dlc];
    memcpy(f->data, rx.data_8, f->len);
    p->rx_cnt++;
    return 1;
}

uint64_t app_isotp_port_now_us(void)
{
    return (uint64_t) mchtmr_get_count(HPM_MCHTMR) / 24u;   /* MCHTMR 24 MHz -> 1 us */
}

static int tp_link_tx(void *ctx, const app_isotp_frame_t *f)
{
    return app_isotp_port_send((app_isotp_port_t *) ctx, f);
}

static uint64_t tp_link_now(void *ctx)
{
    (void) ctx;
    return app_isotp_port_now_us();
}

app_isotp_link_t app_isotp_port_link(app_isotp_port_t *p)
{
    app_isotp_link_t link;

    link.ctx = p;
    link.tx_frame = tp_link_tx;
    link.now_us = tp_link_now;
    return link;
}

int app_isotp_port_pump(app_isotp_port_t *p, app_isotp_t *const *tps, uint32_t n)
{
    app_isotp_frame_t f;
    int frames = 0;

    while (app_isotp_port_recv(p, &f)) {
        frames++;
        for (uint32_t i = 0; i < n; i++) {
            if (tps[i] == NULL) {
                continue;
            }
            if (app_isotp_on_frame(tps[i], &f)) {
                break;
            }
        }
    }
    return frames;
}
