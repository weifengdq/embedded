/*
 * app_can.c - CAN FD 节点组件实现（HPM5E31 MCAN + TCAN1044AV）
 *
 * 参考：samples/can/mcan_two_node/src/main.c（位时序/初始化顺序已上板验证）、
 *       samples/can/mcan_throughput、samples/can/mcan_filter。
 * 关键易错点：board_init_can_clock() 必须先于 mcan_deinit()/mcan_init()，否则访问被
 *         门控的 MCAN 寄存器会挂死（第 8 章易错点 1）。
 *
 * 作者：tp-infra
 */
#include <string.h>
#include <stdio.h>

#include "app_can.h"
#include "board.h"
#include "ext/board_can.h"

#include "hpm_clock_drv.h"
#include "hpm_csr_drv.h"

#define APP_CAN_MAX_FILTERS (8)

static uint32_t s_cpu_mhz;
static bool s_transceivers_on;

/*
 * HPM5E31 的 MCAN 消息 RAM 不在 MCAN IP 内部，而是共享 AHB SRAM（0xF0200000，32 KB）。
 * hpm_soc_feature.h 里 MCAN_SOC_MSG_BUF_IN_AHB_RAM=1，此时必须：
 *   1) 把消息缓冲区放到 .ahb_sram 段（ATTR_PLACE_AT(".ahb_sram")）；
 *   2) 在 mcan_init() 之前调用 mcan_set_msg_buf_attr(base, &attr) 告诉驱动基地址。
 * 漏掉任一步：mcan_init() 返回 status_invalid_argument(=2)，表现为"CANN 完全起不来"。
 * 4 个实例各 640 words(2560 B) = 10 KB，AHB SRAM 32 KB 够用。
 */
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
#define APP_CAN_MSG_BUF_IN_AHB (1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_msg_buf[APP_CAN_INST_COUNT][MCAN_MSG_BUF_SIZE_IN_WORDS];

static uint32_t can_inst_index(MCAN_Type *base)
{
    if (base == HPM_MCAN0) {
        return 0U;
    }
    if (base == HPM_MCAN1) {
        return 1U;
    }
    if (base == HPM_MCAN2) {
        return 2U;
    }
    return 3U;
}
#else
#define APP_CAN_MSG_BUF_IN_AHB (0)
#endif

/* ---------------- 时基 ---------------- */

static void ensure_cpu_mhz(void)
{
    if (s_cpu_mhz == 0U) {
        uint32_t hz = (uint32_t) clock_get_frequency(clock_cpu0);
        s_cpu_mhz = (hz == 0U) ? 480U : (hz / 1000000U);
        if (s_cpu_mhz == 0U) {
            s_cpu_mhz = 1U;
        }
    }
}

uint32_t app_can_cpu_mhz(void)
{
    ensure_cpu_mhz();
    return s_cpu_mhz;
}

uint64_t app_can_now_us(void)
{
    ensure_cpu_mhz();
    return (uint64_t) hpm_csr_get_core_mcycle() / (uint64_t) s_cpu_mhz;
}

/* ---------------- 长度 <-> DLC ---------------- */

static uint8_t len_to_dlc(uint8_t len)
{
    static const uint8_t map[9] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    if (len <= 8U) {
        return map[len];
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

static uint8_t dlc_to_len(uint8_t dlc)
{
    static const uint8_t map[16] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
                                    8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U};
    return map[dlc & 0x0FU];
}

/* ---------------- 初始化 ---------------- */

static hpm_stat_t node_init(MCAN_Type *base, const app_can_cfg_t *cfg, uint32_t *clk_out)
{
    mcan_config_t mc;
    static mcan_filter_elem_t std_elems[APP_CAN_MAX_FILTERS];
    static mcan_filter_elem_t ext_elems[APP_CAN_MAX_FILTERS];
    uint32_t n_std = 0U;
    uint32_t n_ext = 0U;

#if APP_CAN_MSG_BUF_IN_AHB
    /* 必须在 mcan_get_default_config/mcan_init 之前登记消息缓冲（AHB SRAM） */
    {
        mcan_msg_buf_attr_t attr;
        attr.ram_base = (uint32_t) s_msg_buf[can_inst_index(base)];
        attr.ram_size = MCAN_MSG_BUF_SIZE_IN_WORDS * 4U;
        mcan_set_msg_buf_attr(base, &attr);
    }
#endif
#if APP_CAN_MSG_BUF_IN_AHB
    /* 必须在 mcan_get_default_config/mcan_init 之前登记消息缓冲（AHB SRAM） */
    {
        mcan_msg_buf_attr_t attr;
        attr.ram_base = (uint32_t) s_msg_buf[can_inst_index(base)];
        attr.ram_size = MCAN_MSG_BUF_SIZE_IN_WORDS * 4U;
        mcan_set_msg_buf_attr(base, &attr);
    }
#endif
    mcan_get_default_config(base, &mc);
    mc.mode = cfg->loopback ? mcan_mode_loopback_internal : mcan_mode_normal;
    mc.enable_restricted_operation_mode = cfg->silent;
    mc.disable_auto_retransmission = cfg->one_shot;   /* gs_usb ONE_SHOT：失败不重传 */
    mc.enable_canfd = cfg->use_custom_timing ? cfg->fd : (cfg->speed == APP_CAN_SPEED_FD_1M_2M);

    if (cfg->use_custom_timing) {
        /* 上层给的是 1 基位时序（与 MCAN 寄存器语义一致），直接填 */
        mc.use_lowlevel_timing_setting = true;
        mc.can_timing.prescaler = cfg->timing.prescaler;
        mc.can_timing.num_seg1 = cfg->timing.num_seg1;
        mc.can_timing.num_seg2 = cfg->timing.num_seg2;
        mc.can_timing.num_sjw = cfg->timing.num_sjw;
        if (cfg->fd) {
            mc.canfd_timing.prescaler = cfg->data_timing.prescaler;
            mc.canfd_timing.num_seg1 = cfg->data_timing.num_seg1;
            mc.canfd_timing.num_seg2 = cfg->data_timing.num_seg2;
            mc.canfd_timing.num_sjw = cfg->data_timing.num_sjw;
            mc.canfd_timing.enable_tdc = cfg->tdc_enable;
            mc.enable_tdc = cfg->tdc_enable;
            mc.tdc_config.ssp_offset = cfg->tdc_ssp_offset;
            mc.tdc_config.filter_window_length = cfg->tdc_filter_window;
            mcan_get_default_ram_config(base, &mc.ram_config, true);
        }
    } else if (cfg->speed == APP_CAN_SPEED_CLASSIC_500K) {
        /* 经典帧：仲裁段 500 kbit/s，采样点 87.5%（80 MHz / 1 / 160TQ，seg1=139 seg2=20） */
        mc.use_lowlevel_timing_setting = true;
        mc.can_timing.prescaler = 1U;
        mc.can_timing.num_seg1 = 139U;
        mc.can_timing.num_seg2 = 20U;
        mc.can_timing.num_sjw = 4U;
    } else {
        /* FD：仲裁 1 Mbit/s（75%）、数据 2 Mbit/s（75%）、TDC 开（SSP=15，滤波窗 15） */
        mc.use_lowlevel_timing_setting = true;
        mc.can_timing.prescaler = 2U;
        mc.can_timing.num_seg1 = 29U;
        mc.can_timing.num_seg2 = 10U;
        mc.can_timing.num_sjw = 2U;
        mc.canfd_timing.prescaler = 2U;
        mc.canfd_timing.num_seg1 = 14U;
        mc.canfd_timing.num_seg2 = 5U;
        mc.canfd_timing.num_sjw = 2U;
        mc.canfd_timing.enable_tdc = true;
        mc.enable_tdc = true;
        mc.tdc_config.ssp_offset = 15U;
        mc.tdc_config.filter_window_length = 15U;
        mcan_get_default_ram_config(base, &mc.ram_config, true);
    }

    /* 过滤器：显式给定时安装；否则沿用 SDK 默认（mask=0 全收进 RXFIFO0） */
    if ((cfg->filters != NULL) && (cfg->filter_count > 0)) {
        int n = cfg->filter_count;
        if (n > APP_CAN_MAX_FILTERS) {
            n = APP_CAN_MAX_FILTERS;
        }
        for (int i = 0; i < n; i++) {
            mcan_filter_elem_t e;
            memset(&e, 0, sizeof(e));
            e.filter_type = MCAN_FILTER_TYPE_CLASSIC_FILTER;
            e.filter_config = MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO0_IF_MATCH;
            e.sync_message = 0U;
            e.filter_id = cfg->filters[i].id;
            e.filter_mask = cfg->filters[i].mask;
            if (cfg->filters[i].ext) {
                e.can_id_type = MCAN_CAN_ID_TYPE_EXTENDED;
                if (n_ext < APP_CAN_MAX_FILTERS) {
                    ext_elems[n_ext++] = e;
                }
            } else {
                e.can_id_type = MCAN_CAN_ID_TYPE_STANDARD;
                if (n_std < APP_CAN_MAX_FILTERS) {
                    std_elems[n_std++] = e;
                }
            }
        }
        mc.all_filters_config.global_filter_config.accept_non_matching_std_frame_option =
            MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_REJECT;
        mc.all_filters_config.global_filter_config.accept_non_matching_ext_frame_option =
            MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_REJECT;
        mc.all_filters_config.std_id_filter_list.filter_elem_list = std_elems;
        mc.all_filters_config.std_id_filter_list.mcan_filter_elem_count = n_std;
        mc.all_filters_config.ext_id_filter_list.filter_elem_list = ext_elems;
        mc.all_filters_config.ext_id_filter_list.mcan_filter_elem_count = n_ext;
    }

    board_init_can(base);
    uint32_t clk = board_init_can_clock(base);
    if (clk_out != NULL) {
        *clk_out = clk;
    }
    if (clk == 0U) {
        printf("[app_can] clock=0 for MCAN base=%p (board_init_can_clock failed)\r\n", (void *) base);
        return status_invalid_argument;
    }
    mcan_deinit(base);
    {
        hpm_stat_t st = mcan_init(base, &mc, clk);
        if (st != status_success) {
            printf("[app_can] mcan_init FAILED base=%p clk=%lu st=%d mode=%d fd=%d\r\n",
                   (void *) base, (unsigned long) clk, (int) st, (int) mc.mode,
                   (int) mc.enable_canfd);
        }
        return st;
    }
}

int app_can_init(app_can_t *c, MCAN_Type *base, const app_can_cfg_t *cfg)
{
    app_can_cfg_t def = APP_CAN_CFG_DEFAULT;
    hpm_stat_t st;

    if ((c == NULL) || (base == NULL)) {
        return -1;
    }
    if (cfg == NULL) {
        cfg = &def;
    }
    memset(c, 0, sizeof(*c));
    c->base = base;
    c->speed = cfg->speed;
    c->loopback = cfg->loopback;

    /* 4 颗 TCAN1044AV 共用 PE03 STB，上电后只需使能一次 */
    if (!s_transceivers_on) {
        board_can_transceivers_enable();
        s_transceivers_on = true;
    }

    st = node_init(base, cfg, &c->src_clk_hz);
    if (st != status_success) {
        return -3;
    }
    c->inited = true;
    return 0;
}

/* ---------------- 发送 ---------------- */

int app_can_send(app_can_t *c, const app_can_frame_t *f, uint32_t timeout_us)
{
    mcan_tx_frame_t tx;
    uint32_t slot = 0U;
    uint64_t t0;
    uint8_t len;

    if ((c == NULL) || (f == NULL) || !c->inited) {
        return 1;
    }
    len = f->len;
    if (len > APP_CAN_MAX_DATA_LEN) {
        len = APP_CAN_MAX_DATA_LEN;
    }
    if (!f->fd && (len > 8U)) {
        len = 8U;
    }

    memset(&tx, 0, sizeof(tx));
    if (f->ext) {
        tx.use_ext_id = 1U;
        tx.ext_id = f->id & 0x1FFFFFFFUL;
    } else {
        tx.std_id = (uint16_t) (f->id & 0x7FFU);
    }
    tx.rtr = f->rtr ? 1U : 0U;
    tx.canfd_frame = f->fd ? 1U : 0U;
    tx.bitrate_switch = (f->fd && f->brs) ? 1U : 0U;
    tx.dlc = len_to_dlc(len);
    if (len > 0U) {
        memcpy(tx.data_8, f->data, len);
    }

    t0 = app_can_now_us();
    for (;;) {
        if (mcan_transmit_via_txfifo_nonblocking(c->base, &tx, &slot) == status_success) {
            c->stats.tx_frames++;
            return 0;
        }
        if ((timeout_us == 0U) || ((app_can_now_us() - t0) >= (uint64_t) timeout_us)) {
            c->stats.tx_fail++;
            return 1;
        }
    }
}

/* ---------------- 接收 ---------------- */

int app_can_recv(app_can_t *c, app_can_frame_t *f, uint32_t timeout_us)
{
    mcan_rx_message_t rx;
    uint64_t t0;

    if ((c == NULL) || (f == NULL) || !c->inited) {
        return 1;
    }
    t0 = app_can_now_us();
    for (;;) {
        hpm_stat_t st = mcan_read_rxfifo(c->base, 0U, &rx);
        if (st != status_success) {
            st = mcan_read_rxfifo(c->base, 1U, &rx);
        }
        if (st == status_success) {
            uint8_t len = dlc_to_len(rx.dlc);
            memset(f, 0, sizeof(*f));
            if (rx.use_ext_id) {
                f->ext = 1U;
                f->id = rx.ext_id & 0x1FFFFFFFUL;
            } else {
                f->id = (uint32_t) (rx.std_id & 0x7FFU);
            }
            f->rtr = rx.rtr ? 1U : 0U;
            f->fd = rx.canfd_frame ? 1U : 0U;
            f->brs = rx.bitrate_switch ? 1U : 0U;
            f->len = len;
            if (len > 0U) {
                memcpy(f->data, rx.data_8, len);
            }
            f->ts_us = app_can_now_us();
            c->stats.rx_frames++;
            return 0;
        }
        if ((timeout_us == 0U) || ((app_can_now_us() - t0) >= (uint64_t) timeout_us)) {
            return 1;
        }
    }
}

/* ---------------- 运行期过滤器 ---------------- */

void app_can_set_filter(app_can_t *c, const app_can_filter_t *flt, int n)
{
    static mcan_filter_elem_t std_elems[APP_CAN_MAX_FILTERS];
    static mcan_filter_elem_t ext_elems[APP_CAN_MAX_FILTERS];
    mcan_all_filters_config_t fc;
    uint32_t n_std = 0U;
    uint32_t n_ext = 0U;
    int cnt = (flt == NULL) ? 0 : n;

    if ((c == NULL) || !c->inited) {
        return;
    }
    if (cnt > APP_CAN_MAX_FILTERS) {
        cnt = APP_CAN_MAX_FILTERS;
    }
    memset(&fc, 0, sizeof(fc));
    fc.ext_id_mask = 0x1FFFFFFFUL;
    fc.global_filter_config.accept_non_matching_std_frame_option = MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;
    fc.global_filter_config.accept_non_matching_ext_frame_option = MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;
    for (int i = 0; i < cnt; i++) {
        mcan_filter_elem_t e;
        memset(&e, 0, sizeof(e));
        e.filter_type = MCAN_FILTER_TYPE_CLASSIC_FILTER;
        e.filter_config = MCAN_FILTER_ELEM_CFG_STORE_IN_RX_FIFO0_IF_MATCH;
        e.filter_id = flt[i].id;
        e.filter_mask = flt[i].mask;
        if (flt[i].ext) {
            e.can_id_type = MCAN_CAN_ID_TYPE_EXTENDED;
            ext_elems[n_ext++] = e;
        } else {
            e.can_id_type = MCAN_CAN_ID_TYPE_STANDARD;
            std_elems[n_std++] = e;
        }
    }
    fc.std_id_filter_list.filter_elem_list = std_elems;
    fc.std_id_filter_list.mcan_filter_elem_count = n_std;
    fc.ext_id_filter_list.filter_elem_list = ext_elems;
    fc.ext_id_filter_list.mcan_filter_elem_count = n_ext;

    /* 过滤器只能在初始化模式 + CCE=1 下写；重配期间可能丢 1~2 帧，业务侧自行留窗口 */
    mcan_enter_init_mode(c->base);
    mcan_enable_write_to_prot_config_registers(c->base);
    (void) mcan_config_all_filters(c->base, &fc);
    mcan_enter_normal_mode(c->base);
}

/* ---------------- 统计 ---------------- */

void app_can_get_stats(app_can_t *c, app_can_stats_t *st)
{
    mcan_protocol_status_t ps;
    mcan_error_count_t ec;
    uint8_t was_off;
    uint8_t was_pas;

    if ((c == NULL) || (st == NULL)) {
        return;
    }
    was_off = c->stats.bus_off;
    was_pas = c->stats.error_passive;

    memset(&ps, 0, sizeof(ps));
    memset(&ec, 0, sizeof(ec));
    if (c->inited) {
        (void) mcan_get_protocol_status(c->base, &ps);
        mcan_get_error_counter(c->base, &ec);
    }
    c->stats.last_error_code = ps.last_error_code;
    c->stats.tec = ec.transmit_error_count;
    c->stats.rec = ec.receive_error_count;
    c->stats.bus_off = mcan_is_in_busoff_state(c->base) ? 1U : 0U;
    c->stats.error_passive = ps.in_error_passive_state ? 1U : 0U;
    if (c->stats.bus_off && !was_off) {
        c->stats.busoff_count++;
    }
    if (c->stats.error_passive && !was_pas) {
        c->stats.error_passive_count++;
    }
    *st = c->stats;
}

/* ---------------- 便利 API ---------------- */

int app_can_loopback_test(MCAN_Type *base, app_can_speed_t speed, uint32_t id)
{
    app_can_t tmp;
    app_can_cfg_t cfg = APP_CAN_CFG_DEFAULT;
    app_can_frame_t f;
    int rc;

    cfg.speed = speed;
    cfg.loopback = true;
    {
        int irc = app_can_init(&tmp, base, &cfg);
        if (irc != 0) {
            printf("[app_can] loopback init failed rc=%d\r\n", irc);
            return (irc == -3) ? -3 : -1;
        }
    }
    memset(&f, 0, sizeof(f));
    f.id = id & 0x7FFU;
    f.len = 8U;
    for (uint8_t i = 0; i < 8U; i++) {
        f.data[i] = (uint8_t) (0xA5U + i);
    }
    if (app_can_send(&tmp, &f, 1000U) != 0) {
        return -2;
    }
    if (app_can_recv(&tmp, &f, 2000U) != 0) {
        return -3;
    }
    rc = 0;
    if ((f.id != (id & 0x7FFU)) || (f.len != 8U)) {
        rc = -4;
    }
    for (uint8_t i = 0; (rc == 0) && (i < 8U); i++) {
        if (f.data[i] != (uint8_t) (0xA5U + i)) {
            rc = -4;
        }
    }
    return rc;
}

const char *app_can_strerror(int rc)
{
    switch (rc) {
    case 0:  return "OK";
    case -1: return "bad argument";
    case -2: return "no CAN clock (board_init_can_clock failed)";
    case -3: return "mcan_init failed";
    case -4: return "loopback payload mismatch";
    case -5: return "loopback init failed (mcan_init)";
    default: return "unknown";
    }
}

const char *app_can_speed_str(app_can_speed_t speed)
{
    return (speed == APP_CAN_SPEED_FD_1M_2M) ? "FD arb1M/data2M/SP75%/TDC"
                                             : "classic 500k/SP87.5%";
}

void app_can_dump(app_can_t *c, const char *tag)
{
    app_can_stats_t st;
    if ((c == NULL) || (c->base == NULL)) {
        return;
    }
    app_can_get_stats(c, &st);
    printf("[can%s] %s act=%s tx=%lu rx=%lu fail=%lu bo_cnt=%lu lec=%lu tec=%u rec=%u bo=%u ep=%u\r\n",
           (c->base == HPM_MCAN0) ? "0" : (c->base == HPM_MCAN1) ? "1"
             : (c->base == HPM_MCAN2) ? "2" : "3",
           (tag != NULL) ? tag : "",
           (st.bus_off ? "busoff" : (st.error_passive ? "errpassive" : "normal")),
           (unsigned long) st.tx_frames, (unsigned long) st.rx_frames,
           (unsigned long) st.tx_fail, (unsigned long) st.busoff_count,
           (unsigned long) st.last_error_code, (unsigned) st.tec, (unsigned) st.rec,
           (unsigned) st.bus_off, (unsigned) st.error_passive);
}