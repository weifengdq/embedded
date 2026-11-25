/*
 * j1939_port_mcan.c - J1939 端口层：HPM MCAN 驱动适配
 *
 * 250 kbit/s / 采样点 87.5%：MCAN 源时钟 80 MHz，prescaler=2，seg1=139，seg2=20
 * （1 bit = (1+139+20)*2 = 320 tq，80 MHz/320 = 250 kbit/s）。
 * 500 kbit/s：prescaler=1，其余相同（实验档）。
 *
 * 接收：默认 MCAN 全局过滤配置把"不匹配"的扩展帧收进 RXFIFO0，所以软件侧
 *       按 PGN/SA 过滤即可；硬件过滤的掩码算法见第 32 章"易错点"一节。
 */
#include "j1939_port.h"
#include "board.h"
#include "hpm_clock_drv.h"
#include "hpm_csr_drv.h"

static uint32_t s_cpu_mhz;

static uint32_t cpu_mhz(void)
{
    if (s_cpu_mhz == 0U) {
        s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
        if (s_cpu_mhz == 0U) {
            s_cpu_mhz = 1U; /* 防止除零 */
        }
    }
    return s_cpu_mhz;
}

uint32_t j1939_micros(void)
{
    return (uint32_t) (hpm_csr_get_core_mcycle() / (uint64_t) cpu_mhz());
}

uint32_t j1939_millis(void)
{
    return (uint32_t) (hpm_csr_get_core_mcycle() / ((uint64_t) cpu_mhz() * 1000ULL));
}

int j1939_bus_init(j1939_bus_t *bus, MCAN_Type *base, uint32_t *msg_buf, uint32_t bitrate)
{
    mcan_config_t cfg;
    if ((bus == NULL) || (base == NULL) || (msg_buf == NULL)) {
        return -1;
    }
    __builtin_memset(bus, 0, sizeof(*bus));
    bus->base = base;
    bus->msg_buf = msg_buf;

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
    {
        mcan_msg_buf_attr_t attr = {(uint32_t) msg_buf, MCAN_MSG_BUF_SIZE_IN_WORDS * 4U};
        mcan_set_msg_buf_attr(base, &attr);
    }
#endif

    mcan_get_default_config(base, &cfg);
    cfg.mode = mcan_mode_normal;
    cfg.enable_canfd = false;
    cfg.use_lowlevel_timing_setting = true;
    cfg.can_timing.prescaler = (bitrate <= 250000U) ? 2U : 1U;
    cfg.can_timing.num_seg1 = 139U;
    cfg.can_timing.num_seg2 = 20U;
    cfg.can_timing.num_sjw = 4U;

    board_init_can(base);
    uint32_t clk = board_init_can_clock(base); /* 必须先开时钟，否则 deinit 访问门控寄存器会挂死 */
    if (clk == 0U) {
        return -2;
    }
    mcan_deinit(base);
    if (mcan_init(base, &cfg, clk) != status_success) {
        return -3;
    }
    return 0;
}

int j1939_bus_send(j1939_bus_t *bus, const j1939_frame_t *f)
{
    mcan_tx_frame_t tx;
    if ((bus == NULL) || (f == NULL) || (f->len > 8U)) {
        return -1;
    }
    __builtin_memset(&tx, 0, sizeof(tx));
    tx.ext_id = f->id & J1939_ID_MASK;
    tx.use_ext_id = 1U;
    tx.rtr = 0U;
    tx.canfd_frame = 0U;
    tx.bitrate_switch = 0U;
    tx.dlc = f->len;
    for (uint8_t i = 0U; i < f->len; i++) {
        tx.data_8[i] = f->data[i];
    }
    if (mcan_transmit_blocking(bus->base, &tx) != status_success) {
        bus->tx_errors++;
        return -2;
    }
    bus->tx_frames++;
    return 0;
}

int j1939_bus_recv(j1939_bus_t *bus, j1939_frame_t *f)
{
    mcan_rx_message_t rx;
    if ((bus == NULL) || (f == NULL)) {
        return 0;
    }
    if (mcan_read_rxfifo(bus->base, 0, &rx) != status_success) {
        return 0;
    }
    if ((rx.use_ext_id == 0U) || (rx.canfd_frame != 0U) || (rx.rtr != 0U) || (rx.dlc > 8U)) {
        bus->dropped++;
        return 0;
    }
    f->id = rx.ext_id & J1939_ID_MASK;
    f->len = (uint8_t) rx.dlc;
    for (uint8_t i = 0U; i < f->len; i++) {
        f->data[i] = rx.data_8[i];
    }
    f->ts_us = j1939_micros();
    bus->rx_frames++;
    return 1;
}

void j1939_bus_stats(j1939_bus_t *bus, uint32_t *tx, uint32_t *rx, uint32_t *err)
{
    if (bus == NULL) {
        return;
    }
    if (tx != NULL) {
        *tx = bus->tx_frames;
    }
    if (rx != NULL) {
        *rx = bus->rx_frames;
    }
    if (err != NULL) {
        *err = bus->tx_errors;
    }
}
