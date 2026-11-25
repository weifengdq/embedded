/*
 * bridge.c - gs_usb 协议帧 ↔ MCAN 4 通道桥接
 *
 * 数据流：
 *   CAN RX:  app_can_recv() → gs_usb_encode_frame(echo_id=0xffffffff, ts) → gs_usb_usb_send()
 *   USB TX:  gs_usb_usb 的 OUT 回调 → tx_ring_push() → 主循环 gs_usb_decode_tx() → app_can_send()
 *            → 成功后再发一帧同尺寸 echo（echo_id 原样 + 时间戳）
 *
 * 防崩：所有通道/DLC/echo_id 校验在 gs_usb_decode_tx() 内完成；本文件不再越界访问。
 */
#include <string.h>
#include "bridge.h"
#include "usb_gs.h"
#include "app_can.h"
#include "board.h"
#include "hpm_clock_drv.h"
#include "hpm_csr_drv.h"
#include "hpm_mcan_drv.h"

#define GS_BRIDGE_TX_RING       (16U)
#define GS_BRIDGE_RX_BURST      (16U)

typedef struct {
    uint8_t  channel;
    uint8_t  len;
    uint8_t  data[GS_USB_TX_FD_LEN];
} tx_item_t;

static app_can_t s_can[GS_USB_CHANNELS];
static bool s_started[GS_USB_CHANNELS];
static bool s_have_nom[GS_USB_CHANNELS];
static bool s_have_data[GS_USB_CHANNELS];
static struct gs_device_bittiming s_nom[GS_USB_CHANNELS];
static struct gs_device_bittiming s_data[GS_USB_CHANNELS];
static uint32_t s_mode_flags[GS_USB_CHANNELS];
static bool s_berr_en[GS_USB_CHANNELS];
static uint32_t s_identify_mode;
static uint8_t s_err_state[GS_USB_CHANNELS];     /* 0=error-active 1=warning 2=passive 3=bus-off */
static uint8_t s_restart_pending[GS_USB_CHANNELS];
static uint32_t s_err_frames;

static volatile uint32_t s_tx_head;
static volatile uint32_t s_tx_tail;
static tx_item_t s_tx_ring[GS_BRIDGE_TX_RING];
static volatile uint32_t s_tx_drop;

static volatile uint32_t s_rx_frames;
static volatile uint32_t s_tx_frames;
static volatile uint32_t s_echo_frames;

/* R8: MCAN TX FIFO 满时的退避次数（背压生效证据；不丢帧） */
static volatile uint32_t s_tx_retry;
/* R8: IN 端点暂时满时暂存的 echo（帧已上总线，不能重发，只能补发 echo） */
static uint8_t s_echo_pend[GS_USB_RX_FD_LEN];
static uint32_t s_echo_pend_len;

static MCAN_Type *const s_base[GS_USB_CHANNELS] = {HPM_MCAN0, HPM_MCAN1, HPM_MCAN2, HPM_MCAN3};

/* ---------------- 时间戳：mcycle(480 MHz) / 480 = 1 MHz ---------------- */

uint32_t gs_bridge_now_us(void)
{
    static uint32_t mhz;
    if (mhz == 0U) {
        uint32_t hz = (uint32_t) clock_get_frequency(clock_cpu0);
        mhz = (hz / 1000000U) ? (hz / 1000000U) : 1U;
    }
    return (uint32_t) (hpm_csr_get_core_mcycle() / (uint64_t) mhz);
}

uint32_t gs_bridge_ts_high(void)
{
    static uint32_t mhz;
    if (mhz == 0U) {
        uint32_t hz = (uint32_t) clock_get_frequency(clock_cpu0);
        mhz = (hz / 1000000U) ? (hz / 1000000U) : 1U;
    }
    return (uint32_t) ((hpm_csr_get_core_mcycle() / (uint64_t) mhz) >> 32);
}

/* ---------------- CAN 初始化 / 模式 ---------------- */

int gs_bridge_init(void)
{
    static const app_can_cfg_t def = APP_CAN_CFG_DEFAULT;

    memset((void *) s_started, 0, sizeof(s_started));
    for (uint8_t ch = 0; ch < GS_USB_CHANNELS; ch++) {
        if (app_can_init(&s_can[ch], s_base[ch], &def) != 0) {
            printf("[gs] CAN%u init failed\r\n", (unsigned) ch);
            return -1;
        }
        s_started[ch] = true;   /* MCAN 已初始化（默认 500k 经典）；MODE START 时按内核位时序重配 */
    }
    printf("[gs] 4x MCAN initialized (default 500k classic)\r\n");
    return 0;
}

void gs_bridge_can_store_btim(uint8_t ch, bool data_phase, const struct gs_device_bittiming *bt)
{
    if ((ch >= GS_USB_CHANNELS) || (bt == NULL)) {
        return;
    }
    if (data_phase) {
        s_data[ch] = *bt;
        s_have_data[ch] = true;
    } else {
        s_nom[ch] = *bt;
        s_have_nom[ch] = true;
    }
}

static void apply_start(uint8_t ch, uint32_t flags)
{
    app_can_cfg_t cfg = APP_CAN_CFG_DEFAULT;
    bool fd = (flags & GS_CAN_MODE_FD) != 0U;

    cfg.loopback = (flags & GS_CAN_MODE_LOOP_BACK) != 0U;
    cfg.silent = (flags & GS_CAN_MODE_LISTEN_ONLY) != 0U;
    cfg.one_shot = (flags & GS_CAN_MODE_ONE_SHOT) != 0U;

    if (s_have_nom[ch]) {
        uint32_t presc = 1U, seg1 = 1U, seg2 = 1U, sjw = 1U;
        gs_usb_bt_to_mcan(&s_nom[ch], &presc, &seg1, &seg2, &sjw);
        cfg.use_custom_timing = true;
        cfg.fd = fd;
        cfg.timing.prescaler = (uint16_t) presc;
        cfg.timing.num_seg1 = (uint16_t) seg1;
        cfg.timing.num_seg2 = (uint16_t) seg2;
        cfg.timing.num_sjw = (uint16_t) sjw;
        if (fd && s_have_data[ch]) {
            uint32_t dpresc = 1U, dseg1 = 1U, dseg2 = 1U, dsjw = 1U;
            gs_usb_bt_to_mcan(&s_data[ch], &dpresc, &dseg1, &dseg2, &dsjw);
            cfg.data_timing.prescaler = (uint16_t) dpresc;
            cfg.data_timing.num_seg1 = (uint16_t) dseg1;
            cfg.data_timing.num_seg2 = (uint16_t) dseg2;
            cfg.data_timing.num_sjw = (uint16_t) dsjw;
            /* TDC：内核不传 SSP，固件按数据段 prescaler 内部计算（仅 1~2 分频有意义） */
            if ((dpresc >= 1U) && (dpresc <= 2U)) {
                uint32_t ssp = (1U + dseg1) * dpresc;
                cfg.tdc_enable = true;
                cfg.tdc_ssp_offset = (uint8_t) (ssp > 255U ? 255U : ssp);
                cfg.tdc_filter_window = 0U;
            }
        }
    } else {
        cfg.speed = fd ? APP_CAN_SPEED_FD_1M_2M : APP_CAN_SPEED_CLASSIC_500K;
    }

    if (app_can_init(&s_can[ch], s_base[ch], &cfg) != 0) {
        printf("[gs] CAN%u apply timing FAILED (fd=%u)\r\n", (unsigned) ch, fd ? 1U : 0U);
        s_started[ch] = false;
        return;
    }
    s_mode_flags[ch] = flags;
    if (s_err_state[ch] == 3U) {
        s_restart_pending[ch] = 1U;   /* 从 bus-off 重启：主循环补发 CAN_ERR_RESTARTED */
    }
    s_err_state[ch] = 0U;
    s_started[ch] = true;
    printf("[gs] CAN%u START fd=%u loop=%u listen=%u one_shot=%u\r\n",
           (unsigned) ch, fd ? 1U : 0U, (flags & GS_CAN_MODE_LOOP_BACK) ? 1U : 0U,
           (flags & GS_CAN_MODE_LISTEN_ONLY) ? 1U : 0U, (flags & GS_CAN_MODE_ONE_SHOT) ? 1U : 0U);
}

int gs_bridge_can_mode(uint8_t ch, uint32_t mode, uint32_t flags)
{
    if (ch >= GS_USB_CHANNELS) {
        return -1;
    }
    if (mode == GS_CAN_MODE_START) {
        apply_start(ch, flags);
        return 0;
    }
    /* RESET：停止该通道收发（保留 MCAN 初始化，避免主机 down/up 时反复 deinit） */
    s_started[ch] = false;
    return 0;
}

void gs_bridge_can_get_state(uint8_t ch, struct gs_device_state *st)
{
    app_can_stats_t stats;

    if ((ch >= GS_USB_CHANNELS) || (st == NULL)) {
        return;
    }
    memset(&stats, 0, sizeof(stats));
    if (!s_started[ch]) {
        /* 未 START 的通道：内核约定返回 STOPPED + 0/0（不要返回 MCAN 残留计数） */
        st->state = GS_CAN_STATE_STOPPED;
        st->rxerr = 0U;
        st->txerr = 0U;
        return;
    }
    app_can_get_stats(&s_can[ch], &stats);
    st->state = gs_usb_map_state(stats.bus_off != 0U, stats.error_passive != 0U,
                                 (stats.tec >= 96U) || (stats.rec >= 96U), s_started[ch]);
    st->rxerr = stats.rec;
    st->txerr = stats.tec;
}

void gs_bridge_set_berr(uint8_t ch, bool enable)
{
    if (ch < GS_USB_CHANNELS) {
        s_berr_en[ch] = enable;
    }
}

void gs_bridge_set_identify(uint32_t mode)
{
    s_identify_mode = mode;
}

/* ---------------- 错误帧上报（CAN_ERR_*） ----------------
 *
 * 内核 7.0.0 gs_usb 驱动把 echo_id=0xFFFFFFFF 且 can_id 带 CAN_ERR_FLAG 的经典帧
 * 当错误帧处理：gs_update_state() 据此更新 netdev 的 ERROR-WARNING/PASSIVE/BUS-OFF
 * 状态与 can_stats，用户态 candump 也能看到。错误帧同样是 24 B（12 头 + 8 数据 +
 * 4 B 时间戳，本固件 advertise 了 HW_TIMESTAMP，短帧会被内核判 rx_length_errors）。
 *
 * 数据来源：MCAN PSR（状态位）+ ECR（TEC/REC）。只在主机 MODE flags 置了
 * BERR_REPORTING(bit12) 或历史 BREQ_BERR 置位时上报，与 candleLight 语义一致。
 */
static bool berr_enabled(uint8_t ch)
{
    return s_berr_en[ch] || ((s_mode_flags[ch] & GS_CAN_MODE_BERR_REPORTING) != 0U);
}

static void send_err_frame(uint8_t ch, uint32_t err_bits, const uint8_t data[8])
{
    struct gs_can_frame gf;
    uint8_t buf[GS_USB_RX_CLASSIC_LEN];
    struct gs_host_frame_header *hdr = (struct gs_host_frame_header *) buf;
    uint32_t n;

    memset(&gf, 0, sizeof(gf));
    gf.err = 1U;
    gf.id = err_bits & GS_CAN_EFF_MASK;
    gf.len = 8U;                      /* CAN_ERR_DLC */
    if (data != NULL) {
        memcpy(gf.data, data, 8U);
    }
    n = gs_usb_encode_frame(buf, GS_USB_ECHO_ID_RX, &gf, gs_bridge_now_us());
    hdr->channel = ch;
    if (gs_usb_usb_send(buf, n) == 0) {
        s_err_frames++;
    }
}

static void poll_can_error_state(void)
{
    for (uint8_t ch = 0; ch < GS_USB_CHANNELS; ch++) {
        mcan_protocol_status_t ps;
        mcan_error_count_t ec;
        uint8_t st;
        uint8_t data[8];
        uint32_t bits;

        if (!s_started[ch]) {
            s_err_state[ch] = 0U;
            s_restart_pending[ch] = 0U;
            continue;
        }
        if (s_restart_pending[ch] != 0U) {
            /* MODE START 已把 MCAN 重新初始化；RESTARTED 在主循环补发（控制回调在中断上下文，
             * 不在这里入队，避免和主循环抢 IN 环） */
            s_restart_pending[ch] = 0U;
            if (berr_enabled(ch)) {
                memset(data, 0, sizeof(data));
                send_err_frame(ch, GS_CAN_ERR_RESTARTED | GS_CAN_ERR_CNT, data);
            }
        }

        memset(&ps, 0, sizeof(ps));
        memset(&ec, 0, sizeof(ec));
        (void) mcan_get_protocol_status(s_can[ch].base, &ps);
        (void) mcan_get_error_counter(s_can[ch].base, &ec);

        st = ps.in_bus_off_state ? 3U : (ps.in_error_passive_state ? 2U
                                        : (ps.in_warning_state ? 1U : 0U));
        if (st == s_err_state[ch]) {
            continue;
        }
        s_err_state[ch] = st;
        if (!berr_enabled(ch)) {
            continue;
        }
        memset(data, 0, sizeof(data));
        data[6] = ec.transmit_error_count;
        data[7] = ec.receive_error_count;
        if (st == 3U) {
            bits = GS_CAN_ERR_BUSOFF | GS_CAN_ERR_CNT;
        } else {
            bits = GS_CAN_ERR_CRTL | GS_CAN_ERR_CNT;
            if (st == 2U) {
                if (ec.receive_error_passive) {
                    data[1] |= GS_CAN_ERR_CRTL_RX_PASSIVE;
                }
                if (ec.transmit_error_count >= 128U) {
                    data[1] |= GS_CAN_ERR_CRTL_TX_PASSIVE;
                }
            } else if (st == 1U) {
                if (ec.receive_error_count >= 96U) {
                    data[1] |= GS_CAN_ERR_CRTL_RX_WARNING;
                }
                if (ec.transmit_error_count >= 96U) {
                    data[1] |= GS_CAN_ERR_CRTL_TX_WARNING;
                }
            }
            /* 回到 error-active：data[1]=0，内核据此把 state 置回 ERROR-ACTIVE */
        }
        send_err_frame(ch, bits, data);
    }
}

/* ---------------- USB → CAN（主机 TX） ---------------- */

void gs_bridge_tx_from_host(const uint8_t *data, uint32_t len)
{
    uint32_t next = (s_tx_head + 1U) % GS_BRIDGE_TX_RING;

    if ((data == NULL) || (len > GS_USB_TX_FD_LEN) || (next == s_tx_tail)) {
        s_tx_drop++;
        return;
    }
    s_tx_ring[s_tx_head].channel = 0U;                 /* 解析时再填 */
    s_tx_ring[s_tx_head].len = (uint8_t) len;
    memcpy(s_tx_ring[s_tx_head].data, data, len);
    s_tx_head = next;
}

/* ---------------- CAN → USB（RX + echo） ---------------- */

static void drain_can_rx(void)
{
    for (uint8_t ch = 0; ch < GS_USB_CHANNELS; ch++) {
        uint32_t burst = GS_BRIDGE_RX_BURST;
        app_can_frame_t f;

        if (!s_started[ch]) {
            continue;
        }
        while ((burst-- > 0U) && (app_can_recv(&s_can[ch], &f, 0U) == 0)) {
            struct gs_can_frame gf;
            uint8_t buf[GS_USB_RX_FD_LEN];
            uint32_t n;
            struct gs_host_frame_header *hdr = (struct gs_host_frame_header *) buf;

            memset(&gf, 0, sizeof(gf));
            gf.id = f.id;
            gf.ext = f.ext;
            gf.rtr = f.rtr;
            gf.fd = f.fd;
            gf.brs = f.brs;
            gf.len = f.len;
            memcpy(gf.data, f.data, f.len > 64U ? 64U : f.len);

            n = gs_usb_encode_frame(buf, GS_USB_ECHO_ID_RX, &gf, gs_bridge_now_us());
            hdr->channel = ch;
            if (gs_usb_usb_send(buf, n) == 0) {
                s_rx_frames++;
            }
        }
    }
}

static void drain_host_tx(void)
{
    /* R8 修复：先把上一轮没能入 IN 环的 echo 补发出去。
     * 丢掉 echo 与丢掉 TX 帧同罪——内核靠 echo 回收 TX URB（GS_MAX_TX_URBS=10），
     * 漏 10 个就永久停 TX 队列。 */
    if (s_echo_pend_len != 0U) {
        if (gs_usb_usb_send(s_echo_pend, s_echo_pend_len) != 0) {
            return;
        }
        s_echo_pend_len = 0U;
    }

    while (s_tx_tail != s_tx_head) {
        uint32_t next = (s_tx_tail + 1U) % GS_BRIDGE_TX_RING;
        tx_item_t *item = &s_tx_ring[s_tx_tail];
        struct gs_can_frame f;
        uint32_t echo_id = 0U;
        uint8_t channel = 0U;
        uint8_t fflags = 0U;
        app_can_frame_t af;
        int rc = gs_usb_decode_tx(item->data, item->len, &echo_id, &f, &channel, &fflags);

        if (rc != 0) {
            /* 主机发来非法帧：不计入队尾重试（否则会永久堵队），丢弃并计数 */
            s_tx_tail = next;
            s_tx_drop++;
            continue;
        }
        if ((channel >= GS_USB_CHANNELS) || !s_started[channel]) {
            s_tx_tail = next;
            s_tx_drop++;
            continue;
        }

        memset(&af, 0, sizeof(af));
        af.id = f.id;
        af.ext = f.ext;
        af.rtr = f.rtr;
        af.fd = f.fd;
        af.brs = f.brs;
        af.len = f.len;
        memcpy(af.data, f.data, f.len > 64U ? 64U : f.len);

        if (app_can_send(&s_can[channel], &af, 0U) != 0) {
            /* MCAN TX FIFO 满（非阻塞发送返回失败）：**保留队尾，下一轮 poll 重试**。
             * R8 实测易错点：旧代码在这里直接丢帧且不回 echo，主机满速发送（cangen -g 0）
             * 到 494 帧时 10 个 TX URB 全部泄漏、netdev TX 队列永久 stop
             * （cansend 报 "No buffer space available"、tx_packets 不再增长）。
             * candleLight 的正确行为是背压：主机本来最多只有 10 帧在途，16 深环足够。 */
            s_tx_retry++;
            break;
        }
        s_tx_tail = next;
        s_tx_frames++;
        {
            /* echo：同尺寸（经典 24 / FD 80）+ 时间戳，echo_id 原样 <10 */
            uint8_t echo[GS_USB_RX_FD_LEN];
            struct gs_host_frame_header *hdr = (struct gs_host_frame_header *) echo;
            uint32_t n = gs_usb_encode_frame(echo, echo_id, &f, gs_bridge_now_us());

            hdr->channel = channel;
            if (gs_usb_usb_send(echo, n) == 0) {
                s_echo_frames++;
            } else {
                /* IN 环暂时满：帧已经在总线上了，echo 暂存、下一轮补发（不能重发帧本身） */
                memcpy(s_echo_pend, echo, n);
                s_echo_pend_len = n;
                s_echo_frames++;
                break;
            }
        }
    }
}

void gs_bridge_poll(void)
{
    poll_can_error_state();
    drain_can_rx();
    drain_host_tx();
}

void gs_bridge_counters(uint32_t *rx, uint32_t *tx, uint32_t *drop, uint32_t *echo)
{
    if (rx != NULL) {
        *rx = s_rx_frames;
    }
    if (tx != NULL) {
        *tx = s_tx_frames;
    }
    if (drop != NULL) {
        *drop = s_tx_drop;
    }
    if (echo != NULL) {
        *echo = s_echo_frames;
    }
}

uint32_t gs_bridge_tx_retry(void)
{
    return s_tx_retry;
}

bool gs_bridge_channel_started(uint8_t ch)
{
    return (ch < GS_USB_CHANNELS) ? s_started[ch] : false;
}

uint32_t gs_bridge_err_frames(void)
{
    return s_err_frames;
}

uint32_t gs_bridge_berr_enable_mask(void)
{
    uint32_t m = 0U;
    for (uint8_t i = 0; i < GS_USB_CHANNELS; i++) {
        if (s_berr_en[i]) {
            m |= (1UL << i);
        }
    }
    return m;
}