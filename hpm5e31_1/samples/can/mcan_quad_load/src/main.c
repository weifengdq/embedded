/*
 * mcan_quad_load - 4 路 MCAN 并发满载 / 5M 数据段 / TSU 时间戳 / 采样点矩阵
 *
 * 硬件: hpm5e31_1，CAN0(PD00/PD01) <-> CAN1(PD05/PD04)、CAN2(PC08/PA09) <-> CAN3(PC15/PC14)
 *       两两同一段真实总线，各 120 欧；收发器 TCAN1044AV（上限 5 Mbit/s）。
 * 目的: (1) 4 路同时满负荷打流，测吞吐/丢帧/CPU 忙闲比与消息 RAM 占用；
 *       (2) 数据段 5M/8M 的吞吐与误码（8M 受收发器限制，用于给出实测边界）；
 *       (3) TSU 硬件时间戳：内部 AHB 时基与 PTPC 公共时基下 TX Event / RX 时间戳差值；
 *       (4) 500k/1M 仲裁 x 2M/5M 数据 x 采样点 x SJW/TDC 的位时序矩阵。
 * 命令: info | quad [n] [case] [links] | bi [n] [case] | one [n] [case]
 *       tsu [n] | ptp [n] | matrix [n] | dma
 *
 * links 语义: 1 = 单链路 CAN0->CAN1；2 = 一对总线两端都发（CAN0<->CAN1 双向）；
 *             4 = 两组总线两端都发（CAN0<->CAN1 + CAN2<->CAN3，4 个控制器全忙）
 */
#include "bsp.h"
#include "hpm_mcan_drv.h"
#include "hpm_ptpc_drv.h"
#include "hpm_clock_drv.h"
#include "ext/board_can.h"

#define LINK_MAX (4U)
#define TS_MAX   (256U)

typedef struct {
    const char *name;
    bool fd;
    uint16_t p_arb, s1_arb, s2_arb;
    uint16_t p_dat, s1_dat, s2_dat;
    uint16_t sjw_arb, sjw_dat;
    uint8_t dlc;
    uint8_t tdc;
} bus_case_t;

/* 80 MHz CAN 时钟；2M=40TQ、5M=16TQ、8M=10TQ */
static const bus_case_t s_cases[] = {
    {"fd-1M-2M-64B",   true,  1, 69, 10, 1, 29, 10, 2, 2, 15, 1},
    {"fd-1M-5M-64B",   true,  1, 69, 10, 1, 13,  2, 2, 2, 15, 1},
    {"fd-1M-8M-64B",   true,  1, 69, 10, 1,  7,  2, 2, 2, 15, 1},
    {"fd-500k-2M-64B", true,  1,139, 20, 1, 29, 10, 2, 2, 15, 1},
    {"can20-500k-8B",  false, 1,139, 20, 0,  0,  0, 2, 0,  8, 0},
    {"can20-1M-8B",    false, 1, 69, 10, 0,  0,  0, 2, 0,  8, 0},
};
#define CASE_COUNT (sizeof(s_cases) / sizeof(s_cases[0]))

#define TSU_OFF  (0U)
#define TSU_AHB  (1U)
#define TSU_PTPC (2U)

static MCAN_Type *const s_bases[4] = {BOARD_CAN0_BASE, BOARD_CAN1_BASE,
                                      BOARD_CAN2_BASE, BOARD_CAN3_BASE};

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
ATTR_PLACE_AT(".ahb_sram") static uint32_t s_buf[4][MCAN_MSG_BUF_SIZE_IN_WORDS];
#else
static uint32_t s_buf[4][MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

static uint32_t s_cpu_mhz;
static uint32_t s_ahb_mhz;
static volatile uint32_t s_idle_sink;
static uint32_t s_cyc_per_idle_chunk;
static uint32_t s_last_cpf;
static uint32_t s_last_busy_permille;

static uint64_t cyc_now(void)
{
    return (uint64_t) hpm_csr_get_core_mcycle();
}

static uint32_t cyc_to_us(uint64_t c)
{
    return (uint32_t) (c / (uint64_t) s_cpu_mhz);
}

static inline void idle_chunk(void)
{
    for (uint32_t i = 0; i < 32U; i++) {
        s_idle_sink++;
    }
}

/* 空转校准：测出一次 idle_chunk 的周期数，用于由空转次数反推 CPU 忙闲比 */
static void idle_calibrate(void)
{
    uint32_t n = 0;
    uint64_t t0 = cyc_now();
    uint64_t budget = (uint64_t) s_cpu_mhz * 10000ULL; /* 10 ms */
    while ((cyc_now() - t0) < budget) {
        idle_chunk();
        n++;
    }
    uint64_t c = cyc_now() - t0;
    s_cyc_per_idle_chunk = (uint32_t) (c / (n ? n : 1U));
}

static hpm_stat_t node_init(MCAN_Type *base, uint32_t *buf, const bus_case_t *c,
                            uint8_t tsu_mode, bool evt_fifo)
{
    mcan_config_t cfg;
#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    mcan_msg_buf_attr_t attr = {(uint32_t) buf, MCAN_MSG_BUF_SIZE_IN_WORDS * 4U};
    mcan_set_msg_buf_attr(base, &attr);
#endif
    mcan_get_default_config(base, &cfg);
    cfg.mode = mcan_mode_normal;
    cfg.enable_canfd = c->fd;
    cfg.use_lowlevel_timing_setting = true;
    cfg.can_timing.prescaler = c->p_arb;
    cfg.can_timing.num_seg1 = c->s1_arb;
    cfg.can_timing.num_seg2 = c->s2_arb;
    cfg.can_timing.num_sjw = c->sjw_arb;
    if (c->fd) {
        cfg.canfd_timing.prescaler = c->p_dat;
        cfg.canfd_timing.num_seg1 = c->s1_dat;
        cfg.canfd_timing.num_seg2 = c->s2_dat;
        cfg.canfd_timing.num_sjw = c->sjw_dat;
        cfg.canfd_timing.enable_tdc = (c->tdc != 0U);
        cfg.enable_tdc = (c->tdc != 0U);
        if (c->tdc != 0U) {
            cfg.tdc_config.ssp_offset = (uint8_t) (c->s1_dat + 1U);
            cfg.tdc_config.filter_window_length = (uint8_t) (c->s1_dat + 1U);
        }
    }
    mcan_get_default_ram_config(base, &cfg.ram_config, c->fd);
    cfg.ram_config.enable_tx_evt_fifo = evt_fifo;
    if (evt_fifo) {
        cfg.ram_config.tx_evt_fifo_elem_count = 8U;
        cfg.ram_config.tx_evt_fifo_watermark = 0U;
    }
    if (tsu_mode != TSU_OFF) {
        cfg.use_timestamping_unit = true;
        cfg.timestamp_cfg.timestamp_selection = MCAN_TIMESTAMP_SEL_EXT_TS_VAL_USED;
        cfg.tsu_config.enable_tsu = true;
        /* 驱动 mcan_set_tsu() 校验 prescaler in [1,256]，传 0 会返回 status_invalid_argument(2)；
         * TSCFG.TBPRE = prescaler-1，本板 AHB 200 MHz 时 tick 约 5 ns */
        cfg.tsu_config.prescaler = 1U;
        cfg.tsu_config.capture_on_sof = true;
        cfg.tsu_config.enable_64bit_timestamp = (tsu_mode == TSU_PTPC);
        if (tsu_mode == TSU_PTPC) {
            cfg.tsu_config.use_ext_timebase = true;
            cfg.tsu_config.ext_timebase_src = MCAN_TSU_EXT_TIMEBASE_SRC_TBSEL_0;
            cfg.tsu_config.tbsel_option = MCAN_TSU_TBSEL_PTPC0;
        } else {
            cfg.tsu_config.use_ext_timebase = false;
        }
    }
    board_init_can(base);
    uint32_t clk = board_init_can_clock(base);
    mcan_deinit(base);
    return mcan_init(base, &cfg, clk);
}

/* 单向链路：tx 节点发、rx 节点收并校验 */
typedef struct {
    MCAN_Type *tx;
    MCAN_Type *rx;
    uint32_t *buf_tx;
    uint32_t *buf_rx;
    uint16_t id;
    uint32_t target;
    uint32_t submitted;
    uint32_t ok;
    uint32_t bad;
    uint32_t tx_full;
} link_t;

static void link_service(link_t *l, const bus_case_t *c, uint8_t *busy)
{
    mcan_tx_frame_t tx;
    uint32_t k = 0;
    uint32_t fi = 0;

    while ((l->submitted < l->target) && (k < 6U)) {
        __builtin_memset(&tx, 0, sizeof(tx));
        tx.std_id = l->id;
        tx.dlc = c->dlc;
        tx.canfd_frame = c->fd ? 1U : 0U;
        tx.bitrate_switch = c->fd ? 1U : 0U;
        tx.data_32[0] = l->submitted;
        tx.data_32[1] = 0x5AA55AA5U;
        if (mcan_transmit_via_txfifo_nonblocking(l->tx, &tx, &fi) != status_success) {
            l->tx_full++;
            break;
        }
        l->submitted++;
        k++;
        *busy = 1U;
    }
    mcan_rx_message_t rx;
    uint32_t drained = 0;
    while ((drained < 16U) && (mcan_read_rxfifo(l->rx, 0, &rx) == status_success)) {
        bool ok = (rx.std_id == l->id) && (rx.dlc == c->dlc) &&
                  ((rx.canfd_frame != 0U) == c->fd);
        if (ok) {
            l->ok++;
        } else {
            l->bad++;
        }
        drained++;
        *busy = 1U;
    }
}

typedef struct {
    uint32_t ok, bad, us, cpf, busy_permille, lec, tec, rec, lost;
    uint8_t timeout;
} run_stat_t;

static void run_links(uint32_t target, const bus_case_t *c, uint32_t nlinks,
                      bool verbose, run_stat_t *out)
{
    static const struct {
        uint8_t tx, rx;
        uint16_t id;
    } topo[LINK_MAX] = {{0, 1, 0x155U}, {1, 0, 0x2AAU}, {2, 3, 0x155U}, {3, 2, 0x2AAU}};
    link_t links[LINK_MAX];
    uint8_t inited = 0;
    hpm_stat_t st;

    __builtin_memset(out, 0, sizeof(*out));
    for (uint32_t i = 0; i < nlinks; i++) {
        uint8_t t = topo[i].tx, r = topo[i].rx;
        if (!(inited & (1U << t))) {
            st = node_init(s_bases[t], s_buf[t], c, TSU_OFF, false);
            if (st != status_success) {
                printf("[quad] node%u init failed st=%d\r\n", (unsigned) t, (int) st);
                return;
            }
            inited |= (uint8_t) (1U << t);
        }
        if (!(inited & (1U << r))) {
            st = node_init(s_bases[r], s_buf[r], c, TSU_OFF, false);
            if (st != status_success) {
                printf("[quad] node%u init failed st=%d\r\n", (unsigned) r, (int) st);
                return;
            }
            inited |= (uint8_t) (1U << r);
        }
        __builtin_memset(&links[i], 0, sizeof(links[i]));
        links[i].tx = s_bases[t];
        links[i].rx = s_bases[r];
        links[i].buf_tx = s_buf[t];
        links[i].buf_rx = s_buf[r];
        links[i].id = topo[i].id;
        links[i].target = target;
    }
    board_delay_us(100);

    uint64_t t0 = cyc_now();
    uint64_t idle = 0;
    uint8_t timeout = 0;
    bool running = true;
    while (running) {
        uint8_t busy = 0;
        for (uint32_t i = 0; i < nlinks; i++) {
            link_service(&links[i], c, &busy);
        }
        running = false;
        for (uint32_t i = 0; i < nlinks; i++) {
            if ((links[i].submitted < links[i].target) ||
                ((links[i].ok + links[i].bad) < links[i].target)) {
                running = true;
            }
        }
        if (busy == 0U) {
            idle_chunk();
            idle++;
        }
        if ((cyc_now() - t0) > ((uint64_t) s_cpu_mhz * 5000000ULL)) {
            timeout = 1;
            break;
        }
    }
    uint64_t total = cyc_now() - t0;
    uint32_t us = cyc_to_us(total);
    uint32_t idle_cyc = (uint32_t) ((idle * (uint64_t) s_cyc_per_idle_chunk) > total
                                        ? total
                                        : (idle * (uint64_t) s_cyc_per_idle_chunk));
    uint32_t busy_permille = (uint32_t) (((total - idle_cyc) * 1000ULL) / (total ? total : 1U));

    out->timeout = timeout;
    out->us = us;
    out->busy_permille = busy_permille;
    uint32_t total_ok = 0;
    for (uint32_t i = 0; i < nlinks; i++) {
        total_ok += links[i].ok;
        out->bad += links[i].bad;
        out->lost += (uint32_t) ((mcan_get_interrupt_flags(links[i].rx) & MCAN_IR_RF0L_MASK) != 0U);
        mcan_clear_interrupt_flags(links[i].rx, MCAN_IR_RF0L_MASK);
        mcan_protocol_status_t ps;
        mcan_error_count_t ec;
        mcan_get_protocol_status(links[i].rx, &ps);
        mcan_get_error_counter(links[i].rx, &ec);
        out->lec += ps.last_error_code;
        out->tec += ec.transmit_error_count;
        out->rec += ec.receive_error_count;
        if (verbose) {
            double fps = (double) links[i].ok * 1000000.0 / (double) (us ? us : 1U);
            double mbps = fps * (double) mcan_get_message_size_from_dlc(c->dlc) * 8.0 / 1000000.0;
            printf("  link%u can%u->can%u %-15s n=%u ok=%u bad=%u fps=%.0f payload=%.2fMbps "
                   "tx_full=%u\r\n",
                   (unsigned) i, (unsigned) topo[i].tx, (unsigned) topo[i].rx, c->name,
                   (unsigned) target, (unsigned) links[i].ok, (unsigned) links[i].bad, fps, mbps,
                   (unsigned) links[i].tx_full);
        }
    }
    out->ok = total_ok;
    out->cpf = total_ok ? (uint32_t) ((total - idle_cyc) / total_ok) : 0U;
    s_last_cpf = out->cpf;
    s_last_busy_permille = busy_permille;
    if (verbose) {
        printf("  QUAD_SUM links=%u case=%-15s n=%u ok=%u bad=%u t=%u us cpu_busy=%u.%u%% "
               "cyc/frame=%u lec=%u tec=%u rec=%u rxfifo_lost=%u timeout=%u\r\n",
               (unsigned) nlinks, c->name, (unsigned) target, (unsigned) out->ok,
               (unsigned) out->bad, (unsigned) us, (unsigned) (busy_permille / 10U),
               (unsigned) (busy_permille % 10U), (unsigned) out->cpf, (unsigned) out->lec,
               (unsigned) out->tec, (unsigned) out->rec, (unsigned) out->lost,
               (unsigned) timeout);
    }
}

/* ---------------------------------------------------------------- TSU 时间戳 */

static uint32_t s_ts_tx[TS_MAX];
static uint32_t s_ts_rx[TS_MAX];
static uint64_t s_ts_tx64[TS_MAX];
static uint64_t s_ts_rx64[TS_MAX];
static uint8_t s_ts_tx_ok[TS_MAX];
static uint8_t s_ts_rx_ok[TS_MAX];

static uint32_t ts_ticks_to_ns(uint32_t ticks)
{
    return (uint32_t) (((uint64_t) ticks * 1000000000ULL) /
                       ((uint64_t) s_ahb_mhz * 1000000ULL));
}

static uint64_t ts64_to_ns(uint64_t raw)
{
    uint32_t hi = (uint32_t) (raw >> 32);
    uint32_t lo = (uint32_t) raw;
    /* PTPC digital 模式输出 sec(高 32 位)+ns(低 32 位)；若不像则按纯 ns 处理 */
    if ((hi < 0x40000000U) && (lo < 1000000000U)) {
        return (uint64_t) hi * 1000000000ULL + lo;
    }
    return raw;
}

static void tsu_clear(void)
{
    for (uint32_t i = 0; i < TS_MAX; i++) {
        s_ts_tx[i] = 0;
        s_ts_rx[i] = 0;
        s_ts_tx64[i] = 0;
        s_ts_rx64[i] = 0;
        s_ts_tx_ok[i] = 0;
        s_ts_rx_ok[i] = 0;
    }
}

/* 收发同一批帧并收集 TX Event / RX 时间戳；失败返回 0 */
static bool tsu_collect(const bus_case_t *c, uint32_t n, uint8_t tsu_mode)
{
    uint32_t sent = 0, ntx = 0, nrx = 0;
    uint64_t t0 = cyc_now();

    tsu_clear();
    board_delay_us(100);
    while ((sent < n) || (ntx < n) || (nrx < n)) {
        while ((sent < n) && ((sent - ntx) < 4U)) {
            mcan_tx_frame_t tx;
            uint32_t fi = 0;
            __builtin_memset(&tx, 0, sizeof(tx));
            tx.std_id = 0x155U;
            tx.dlc = c->dlc;
            tx.canfd_frame = c->fd ? 1U : 0U;
            tx.bitrate_switch = c->fd ? 1U : 0U;
            tx.timestamp_capture_enable = 1U;
            tx.event_fifo_control = 1U;
            tx.message_marker_l = (uint8_t) (sent & 0xFFU);
            tx.data_32[0] = sent;
            if (mcan_transmit_via_txfifo_nonblocking(BOARD_CAN0_BASE, &tx, &fi) != status_success) {
                break;
            }
            sent++;
        }
        mcan_tx_event_fifo_elem_t evt;
        while (mcan_read_tx_evt_fifo(BOARD_CAN0_BASE, &evt) == status_success) {
            mcan_timestamp_value_t ts;
            if ((mcan_get_timestamp_from_tx_event(BOARD_CAN0_BASE, &evt, &ts) == status_success) &&
                (!ts.is_empty)) {
                uint32_t m = evt.message_marker;
                if (m < n) {
                    if (ts.is_64bit) {
                        s_ts_tx64[m] = ts.ts_64bit;
                    } else if (ts.is_32bit) {
                        s_ts_tx[m] = ts.ts_32bit;
                    } else if (ts.is_16bit) {
                        s_ts_tx[m] = ts.ts_16bit;
                    }
                    s_ts_tx_ok[m] = 1U;
                }
            }
            ntx++;
        }
        mcan_rx_message_t rx;
        while (mcan_read_rxfifo(BOARD_CAN1_BASE, 0, &rx) == status_success) {
            uint32_t seq = rx.data_32[0];
            mcan_timestamp_value_t ts;
            if ((seq < n) &&
                (mcan_get_timestamp_from_received_message(BOARD_CAN1_BASE, &rx, &ts) == status_success) &&
                (!ts.is_empty)) {
                if (ts.is_64bit) {
                    s_ts_rx64[seq] = ts.ts_64bit;
                } else if (ts.is_32bit) {
                    s_ts_rx[seq] = ts.ts_32bit;
                } else if (ts.is_16bit) {
                    s_ts_rx[seq] = ts.ts_16bit;
                }
                s_ts_rx_ok[seq] = 1U;
            }
            nrx++;
        }
        if ((cyc_now() - t0) > ((uint64_t) s_cpu_mhz * 5000000ULL)) {
            printf("[tsu] timeout sent=%u ntx=%u nrx=%u\r\n", (unsigned) sent, (unsigned) ntx,
                   (unsigned) nrx);
            return false;
        }
    }
    return true;
}

static void run_tsu(uint32_t n)
{
    const bus_case_t *c = &s_cases[0]; /* 1M 仲裁 / 2M 数据，64B */
    hpm_stat_t st;
    printf("\r\n=== TSU internal AHB timebase (TX Event vs RX, 32-bit) ===\r\n");
    st = node_init(BOARD_CAN0_BASE, s_buf[0], c, TSU_AHB, true);
    if (st != status_success) {
        printf("[tsu] CAN0 init failed st=%d\r\n", (int) st);
        return;
    }
    st = node_init(BOARD_CAN1_BASE, s_buf[1], c, TSU_AHB, true);
    if (st != status_success) {
        printf("[tsu] CAN1 init failed st=%d\r\n", (int) st);
        return;
    }
    if (!tsu_collect(c, n, TSU_AHB)) {
        return;
    }
    uint32_t valid = 0;
    uint64_t sum_tx = 0, sum_rx = 0, sum_d = 0;
    int64_t d_min = 0, d_max = 0;
    uint32_t iv_min = 0xFFFFFFFFU, iv_max = 0;
    uint32_t viol = 0;
    uint32_t prev_tx = 0, prev_rx = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!s_ts_tx_ok[i] || !s_ts_rx_ok[i]) {
            continue;
        }
        if (valid > 0U) {
            uint32_t iv_tx = s_ts_tx[i] - prev_tx;
            uint32_t iv_rx = s_ts_rx[i] - prev_rx;
            int64_t d = (int64_t) iv_rx - (int64_t) iv_tx;
            if (iv_tx == 0U) {
                viol++;
            }
            sum_tx += iv_tx;
            sum_rx += iv_rx;
            sum_d += (uint64_t) (d < 0 ? -d : d);
            if (d < d_min) {
                d_min = d;
            }
            if (d > d_max) {
                d_max = d;
            }
            if (iv_tx < iv_min) {
                iv_min = iv_tx;
            }
            if (iv_tx > iv_max) {
                iv_max = iv_tx;
            }
        }
        prev_tx = s_ts_tx[i];
        prev_rx = s_ts_rx[i];
        valid++;
    }
    if (valid < 3U) {
        printf("[tsu] too few valid timestamps valid=%u\r\n", (unsigned) valid);
        return;
    }
    uint32_t cnt = valid - 1U;
    printf("[tsu] ahb=%u MHz tick=%u.%03u ns n=%u valid=%u tx_evt_ok=%u rx_ok=%u\r\n",
           (unsigned) s_ahb_mhz, (unsigned) (ts_ticks_to_ns(1000U) / 1000U),
           (unsigned) (ts_ticks_to_ns(1000U) % 1000U), (unsigned) n, (unsigned) valid,
           (unsigned) s_ts_tx_ok[0], (unsigned) s_ts_rx_ok[0]);
    printf("[tsu] tx_interval min=%u mean=%u max=%u ns (n=%u, zero_iv=%u)\r\n",
           (unsigned) ts_ticks_to_ns(iv_min), (unsigned) ts_ticks_to_ns((uint32_t) (sum_tx / cnt)),
           (unsigned) ts_ticks_to_ns(iv_max), (unsigned) cnt, (unsigned) viol);
    printf("[tsu] rx_interval mean=%u ns ; |rx-tx| mean=%u ns max=%d ns\r\n",
           (unsigned) ts_ticks_to_ns((uint32_t) (sum_rx / cnt)),
           (unsigned) ts_ticks_to_ns((uint32_t) (sum_d / cnt)), (int) ts_ticks_to_ns((uint32_t) d_max));
    printf("[tsu] raw tx[0..1]=%08x %08x rx[0..1]=%08x %08x\r\n", (unsigned) s_ts_tx[0],
           (unsigned) s_ts_tx[1], (unsigned) s_ts_rx[0], (unsigned) s_ts_rx[1]);
}

static void run_ptp(uint32_t n)
{
    const bus_case_t *c = &s_cases[0];
    hpm_stat_t st;
    printf("\r\n=== TSU + PTPC0 external timebase (64-bit, common timebase) ===\r\n");
    clock_add_to_group(clock_ptpc, 0);
    ptpc_config_t pc;
    ptpc_get_default_config(HPM_PTPC, &pc);
    pc.src_frequency = clock_get_frequency(clock_ptpc);
    pc.ns_rollover_mode = ptpc_ns_counter_rollover_digital;
    pc.coarse_increment = false;
    pc.capture_keep = false;
    st = ptpc_init(HPM_PTPC, 0, &pc);
    printf("[ptp] ptpc_init st=%d src=%u Hz\r\n", (int) st, (unsigned) pc.src_frequency);
    if (st != status_success) {
        return;
    }
    ptpc_init_timer(HPM_PTPC, 0);
    st = ptpc_set_timer_output(HPM_PTPC, 0, false);
    printf("[ptp] ptpc0->MCAN0 st=%d\r\n", (int) st);
    st = ptpc_set_timer_output(HPM_PTPC, 1, false);
    printf("[ptp] ptpc0->MCAN1 st=%d\r\n", (int) st);
    st = node_init(BOARD_CAN0_BASE, s_buf[0], c, TSU_PTPC, true);
    printf("[ptp] CAN0 tsu init st=%d\r\n", (int) st);
    if (st != status_success) {
        return;
    }
    st = node_init(BOARD_CAN1_BASE, s_buf[1], c, TSU_PTPC, true);
    printf("[ptp] CAN1 tsu init st=%d\r\n", (int) st);
    if (st != status_success) {
        return;
    }
    if (!tsu_collect(c, n, TSU_PTPC)) {
        return;
    }
    uint32_t valid = 0;
    int64_t d_min = 0, d_max = 0;
    uint64_t sum_d = 0, sum_abs = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!s_ts_tx_ok[i] || !s_ts_rx_ok[i]) {
            continue;
        }
        uint64_t tx_ns = ts64_to_ns(s_ts_tx64[i]);
        uint64_t rx_ns = ts64_to_ns(s_ts_rx64[i]);
        int64_t d = (int64_t) (rx_ns - tx_ns);
        if (valid == 0U) {
            d_min = d;
            d_max = d;
        } else {
            if (d < d_min) {
                d_min = d;
            }
            if (d > d_max) {
                d_max = d;
            }
        }
        sum_d += (uint64_t) d;
        sum_abs += (uint64_t) (d < 0 ? -d : d);
        valid++;
    }
    if (valid == 0U) {
        printf("[ptp] no valid timestamps\r\n");
        return;
    }
    printf("[ptp] valid=%u frame_delta(rx-tx) min=%d mean=%d max=%d ns |mean_abs|=%u ns\r\n",
           (unsigned) valid, (int) d_min, (int) (sum_d / valid), (int) d_max,
           (unsigned) (sum_abs / valid));
    printf("[ptp] raw64 tx[0]=%08x%08x rx[0]=%08x%08x -> ns tx=%u rx=%u\r\n",
           (unsigned) (s_ts_tx64[0] >> 32), (unsigned) (s_ts_tx64[0] & 0xFFFFFFFFU),
           (unsigned) (s_ts_rx64[0] >> 32), (unsigned) (s_ts_rx64[0] & 0xFFFFFFFFU),
           (unsigned) ts64_to_ns(s_ts_tx64[0]), (unsigned) ts64_to_ns(s_ts_rx64[0]));
}

/* ---------------------------------------------------------------- 位时序矩阵 */

typedef struct {
    bus_case_t c;
    char label[48];
} mcase_t;

static mcase_t s_mc[64];
static uint32_t s_mc_count;

static void build_matrix(void)
{
    static const struct {
        uint16_t rate;
        uint16_t total;
        uint16_t s1[3];
    } sp_tbl[] = {
        {500U, 160U, {119U, 127U, 139U}},
        {1000U, 80U, {59U, 63U, 69U}},
        {2000U, 40U, {29U, 31U, 34U}},
        {5000U, 16U, {11U, 12U, 13U}},
    };
    static const uint16_t arb_rates[2] = {500U, 1000U};
    static const uint16_t dat_rates[2] = {2000U, 5000U};
    static const uint16_t sjws[3] = {1U, 2U, 4U};

    s_mc_count = 0;
    for (uint32_t ai = 0; ai < 2U; ai++) {
        for (uint32_t di = 0; di < 2U; di++) {
            uint32_t ia = (arb_rates[ai] == 500U) ? 0U : 1U;
            uint32_t id = (dat_rates[di] == 2000U) ? 2U : 3U;
            for (uint32_t si = 0; si < 3U; si++) {
                for (uint32_t td = 0; td < 2U; td++) {
                    uint32_t sjn = (td == 0U) ? 1U : 3U;
                    for (uint32_t sj = 0; sj < sjn; sj++) {
                        mcase_t *m = &s_mc[s_mc_count];
                        uint16_t s1a = sp_tbl[ia].s1[si];
                        uint16_t s1d = sp_tbl[id].s1[si];
                        __builtin_memset(&m->c, 0, sizeof(m->c));
                        m->c.fd = true;
                        m->c.p_arb = 1U;
                        m->c.s1_arb = s1a;
                        m->c.s2_arb = (uint16_t) (sp_tbl[ia].total - 1U - s1a);
                        m->c.p_dat = 1U;
                        m->c.s1_dat = s1d;
                        m->c.s2_dat = (uint16_t) (sp_tbl[id].total - 1U - s1d);
                        m->c.sjw_arb = sjws[sj];
                        m->c.sjw_dat = sjws[sj];
                        m->c.dlc = 15U;
                        m->c.tdc = (td == 0U) ? 0U : 1U;
                        m->c.name = m->label;
                        __builtin_snprintf(m->label, sizeof(m->label), "a%u-d%u-sp%u-sjw%u-tdc%u",
                                           (unsigned) arb_rates[ai], (unsigned) dat_rates[di],
                                           (unsigned) ((uint32_t) (s1a + 1U) * 1000U /
                                                       (uint32_t) (sp_tbl[ia].total)),
                                           (unsigned) sjws[sj], (unsigned) m->c.tdc);
                        s_mc_count++;
                    }
                }
            }
        }
    }
}

static void run_matrix(uint32_t n)
{
    build_matrix();
    printf("\r\n=== timing matrix: arb 500k/1M x data 2M/5M x SP x SJW x TDC ===\r\n");
    printf("[m] note: 5M 数据段用 16 TQ，SP 只能取 75/81.25/87.5%%（表中为实际值）\r\n");
    for (uint32_t i = 0; i < s_mc_count; i++) {
        run_stat_t st;
        run_links(n, &s_mc[i].c, 1U, false, &st);
        uint32_t sp_arb = ((uint32_t) (1U + s_mc[i].c.s1_arb) * 1000U) /
                          (uint32_t) (1U + s_mc[i].c.s1_arb + s_mc[i].c.s2_arb);
        uint32_t sp_dat = ((uint32_t) (1U + s_mc[i].c.s1_dat) * 1000U) /
                          (uint32_t) (1U + s_mc[i].c.s1_dat + s_mc[i].c.s2_dat);
        double fps = (double) st.ok * 1000000.0 / (double) (st.us ? st.us : 1U);
        printf("[m] %-22s sp_arb=%u.%u%% sp_dat=%u.%u%% n=%u ok=%u bad=%u fps=%.0f "
               "lec=%u tec=%u rec=%u t=%u us timeout=%u\r\n", s_mc[i].label,
               (unsigned) (sp_arb / 10U), (unsigned) (sp_arb % 10U), (unsigned) (sp_dat / 10U),
               (unsigned) (sp_dat % 10U), (unsigned) n, (unsigned) st.ok, (unsigned) st.bad, fps,
               (unsigned) st.lec, (unsigned) st.tec, (unsigned) st.rec, (unsigned) st.us,
               (unsigned) st.timeout);
    }
    printf("[m] MATRIX SUMMARY cases=%u\r\n", (unsigned) s_mc_count);
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("sample: mcan_quad_load (4x MCAN concurrent / 5M / TSU / timing matrix)\r\n");
    printf("boards: CAN0<->CAN1, CAN2<->CAN3 两两同一总线 + 120 ohm，TCAN1044AV 上限 5 Mbit/s\r\n");
    printf("clock : cpu=%u MHz ahb=%u MHz can=80 MHz ; msg_ram 4x%u B / 32768 B\r\n",
           (unsigned) s_cpu_mhz, (unsigned) s_ahb_mhz,
           (unsigned) (MCAN_MSG_BUF_SIZE_IN_WORDS * 4U));
    for (uint32_t i = 0; i < CASE_COUNT; i++) {
        printf("  case[%u] %-15s fd=%u dlc=%u\r\n", (unsigned) i, s_cases[i].name,
               (unsigned) s_cases[i].fd, (unsigned) s_cases[i].dlc);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 info, cmd_info, show quad load sample info);

static void cmd_quad(int argc, char *argv[])
{
    uint32_t n = 2000, ci = 0, links = 2;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    if (argc >= 3) {
        bsp_parse_u32(argv[2], &ci);
    }
    if (argc >= 4) {
        bsp_parse_u32(argv[3], &links);
    }
    if ((ci >= CASE_COUNT) || (links < 1U) || (links > 4U)) {
        printf("usage: quad [n] [case 0..%u] [links 1|2|4]\r\n", (unsigned) (CASE_COUNT - 1U));
        return;
    }
    printf("\r\n=== 4CH concurrent one-way (links=%u) case=%s n=%u ===\r\n", (unsigned) links,
           s_cases[ci].name, (unsigned) n);
    run_stat_t st;
    run_links(n, &s_cases[ci], links, true, &st);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 quad, cmd_quad, 4ch concurrent load: quad [n] [case] [links]);

static void cmd_one(int argc, char *argv[])
{
    uint32_t n = 2000, ci = 0;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    if (argc >= 3) {
        bsp_parse_u32(argv[2], &ci);
    }
    if (ci >= CASE_COUNT) {
        printf("case out of range 0..%u\r\n", (unsigned) (CASE_COUNT - 1U));
        return;
    }
    printf("\r\n=== single link case=%s n=%u ===\r\n", s_cases[ci].name, (unsigned) n);
    run_stat_t st;
    run_links(n, &s_cases[ci], 1U, true, &st);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 one, cmd_one, single link: one [n] [case]);

static void cmd_bi(int argc, char *argv[])
{
    uint32_t n = 2000, ci = 0;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    if (argc >= 3) {
        bsp_parse_u32(argv[2], &ci);
    }
    if (ci >= CASE_COUNT) {
        printf("case out of range 0..%u\r\n", (unsigned) (CASE_COUNT - 1U));
        return;
    }
    printf("\r\n=== 4CH bidirectional (both ends of each pair TX, arbitration shared) case=%s ===\r\n",
           s_cases[ci].name);
    run_stat_t st;
    run_links(n, &s_cases[ci], 4U, true, &st);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 bi, cmd_bi, 4ch bidirectional: bi [n] [case]);

static void cmd_tsu(int argc, char *argv[])
{
    uint32_t n = 200;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    if (n > TS_MAX) {
        n = TS_MAX;
    }
    run_tsu(n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 tsu, cmd_tsu, TSU AHB timebase timestamps: tsu [n]);

static void cmd_ptp(int argc, char *argv[])
{
    uint32_t n = 200;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    if (n > TS_MAX) {
        n = TS_MAX;
    }
    run_ptp(n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ptp, cmd_ptp, TSU+PTPC timestamps: ptp [n]);

static void cmd_matrix(int argc, char *argv[])
{
    uint32_t n = 200;
    if (argc >= 2) {
        bsp_parse_u32(argv[1], &n);
    }
    run_matrix(n);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 matrix, cmd_matrix, bit timing matrix: matrix [n]);

static void cmd_dma(int argc, char *argv[])
{
    (void) argc;
    (void) argv;
    printf("\r\n=== MCAN DMA 评估（当前实现：RX FIFO0 轮询）===\r\n");
    printf("  SDK 驱动 hpm_mcan_drv.h 无 DMA 接口（无 dma 字段/函数），MCAN 的 DMA 要应用层自己接\n");
    printf("  实测轮询成本：上一次并发测试 cyc/frame=%u，4 路满载主循环忙闲比 = %u.%u%%\n",
           (unsigned) s_last_cpf, (unsigned) (s_last_busy_permille / 10U),
           (unsigned) (s_last_busy_permille % 10U));
    printf("  方案A（RX）：RXFIFO0 水位=1 -> 中断里只置标志，主循环把 FIFO 元素(72B/64B 帧)\n");
    printf("    用 HDMA 链式搬运到环形缓冲；DMA 每帧 1 次 72B 传输，CPU 只处理指针\n");
    printf("  方案B（TX）：CPU 写 TX FIFO，DMA 仅做数据搬运（收益小，瓶颈在总线）\n");
    printf("  限制：MCAN 消息 RAM 在 AHB SRAM，DMA 源地址固定、需要 4 字节对齐与 32 位宽度；\n");
    printf("        4 路各 1 条 DMA 通道，HPM5E31 的 HDMA 通道数与 TRGM 触发需按 RM §51 核对\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dma, cmd_dma, MCAN DMA evaluation and plan);

int main(void)
{
    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    s_ahb_mhz = clock_get_frequency(clock_ahb0) / 1000000U;
    printf("\r\n[boot] mcan_quad_load\r\n");
    board_can_leds_init();
    board_can_init_pins();
    board_can_transceivers_enable();
    printf("[cfg] cpu=%u MHz ahb=%u MHz msg_ram_per_node=%u B total=%u B\r\n", (unsigned) s_cpu_mhz,
           (unsigned) s_ahb_mhz, (unsigned) (MCAN_MSG_BUF_SIZE_IN_WORDS * 4U),
           (unsigned) (4U * MCAN_MSG_BUF_SIZE_IN_WORDS * 4U));
    idle_calibrate();
    printf("[cfg] idle_chunk=%u cyc\r\n", (unsigned) s_cyc_per_idle_chunk);

    run_stat_t st;
    printf("\r\n=== 4CH concurrent one-way (links=2) case=%s n=2000 ===\r\n", s_cases[0].name);
    run_links(2000U, &s_cases[0], 2U, true, &st);
    printf("\r\n=== 4CH concurrent one-way (links=2) case=%s n=2000 ===\r\n", s_cases[1].name);
    run_links(2000U, &s_cases[1], 2U, true, &st);
    printf("\r\n=== 4CH bidirectional (links=4) case=%s n=2000 ===\r\n", s_cases[0].name);
    run_links(2000U, &s_cases[0], 4U, true, &st);
    printf("done. commands: info | quad [n] [case] [links] | bi [n] [case] | one [n] [case] | "
           "tsu [n] | ptp [n] | matrix [n] | dma\r\n");
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
