/*
 * dma_uart_rx_idle - UART4 + DMA 收帧 + 空闲(IDLE)中断定帧界, 实测丢包/延迟
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1), UART4 引脚 PC16(TXD)/PC17(RXD)
 *   自测方式: 打开 UART 的 modem loopback(MCR.LOOP) 做内部 TX->RX 回环(无需外部接线);
 *             若芯片/封装不支持内部回环, 需要把 PC16 与 PC17 用杜邦线短接(README 有说明)。
 *   每帧: 先挂 RX DMA(handshake, 长度=本帧长度) -> 阻塞发送 -> 等 RX IDLE 中断
 *         -> 校验数据, 统计"帧丢失数"与"发送开始到空闲中断的延迟"。
 *
 * 现象: 见 README.md
 */
#include <string.h>
#include "bsp.h"
#include "hpm_uart_drv.h"
#include "hpm_dmav2_drv.h"
#include "hpm_dmamux_drv.h"
#include "hpm_mchtmr_drv.h"

#define APP_UART            BOARD_APP_UART_BASE
#define APP_UART_CLK        BOARD_APP_UART_CLK_NAME
#define APP_UART_IRQ        BOARD_APP_UART_IRQ
#define APP_UART_BAUD       (115200UL)
#define APP_UART_RX_DMA_SRC BOARD_APP_UART_RX_DMA_REQ

#define APP_DMA             HPM_HDMA
#define APP_DMAMUX          HPM_DMAMUX
#define APP_RX_CH           (0U)
#define APP_RX_DMAMUX_CH    DMA_SOC_CHN_TO_DMAMUX_CHN(APP_DMA, APP_RX_CH)
#define APP_TX_CH           (1U)
#define APP_TX_DMAMUX_CH    DMA_SOC_CHN_TO_DMAMUX_CHN(APP_DMA, APP_TX_CH)
#define APP_UART_TX_DMA_SRC BOARD_APP_UART_TX_DMA_REQ
#define APP_FRAME_MAX       (64U)

ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT(4) static uint8_t s_tx_buf[APP_FRAME_MAX];
ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT(4) static uint8_t s_rx_buf[APP_FRAME_MAX];
ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT(4) static uint8_t s_tx_exp[1024];
ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT(4) static uint8_t s_poll_buf[APP_FRAME_MAX];
ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT(4) static uint8_t s_rx_big[1024];

static uint32_t s_tmr_hz;
static volatile uint32_t s_idle_cnt;
static volatile uint32_t s_idle_tick;
static volatile bool s_idle_seen;
static volatile bool s_loopback_ok;

SDK_DECLARE_EXT_ISR_M(APP_UART_IRQ, isr_uart_idle)
void isr_uart_idle(void)
{
    if (uart_is_rxline_idle(APP_UART)) {
        uart_clear_rxline_idle_flag(APP_UART);
        s_idle_tick = mchtmr_get_count(HPM_MCHTMR);
        s_idle_seen = true;
        s_idle_cnt++;
    }
}

static hpm_stat_t uart_setup(void)
{
    uart_config_t cfg = {0};
    board_init_uart(APP_UART);
    uart_default_config(APP_UART, &cfg);
    cfg.src_freq_in_hz = clock_get_frequency(APP_UART_CLK);
    cfg.baudrate = APP_UART_BAUD;
    cfg.fifo_enable = true;
    cfg.dma_enable = true;
    cfg.tx_fifo_level = uart_tx_fifo_trg_not_full;
    cfg.rx_fifo_level = uart_rx_fifo_trg_not_empty;
    cfg.rxidle_config.detect_enable = true;
    cfg.rxidle_config.detect_irq_enable = true;
    cfg.rxidle_config.idle_cond = uart_rxline_idle_cond_rxline_logic_one;
    cfg.rxidle_config.threshold = 10;
    return uart_init(APP_UART, &cfg);
}


/* UART4 TX 用 DMA 发送一帧(handshake), 返回 DMA 完成耗时 us(失败返回 0) */
static uint32_t uart_dma_send(const uint8_t *buf, uint32_t len)
{
    dma_handshake_config_t cfg;
    dmamux_config(APP_DMAMUX, APP_TX_DMAMUX_CH, APP_UART_TX_DMA_SRC, true);
    dma_default_handshake_config(APP_DMA, &cfg);
    cfg.ch_index = APP_TX_CH;
    cfg.src = (uint32_t) buf;
    cfg.dst = (uint32_t) &APP_UART->THR;
    cfg.dst_fixed = true;
    cfg.src_fixed = false;
    cfg.data_width = DMA_TRANSFER_WIDTH_BYTE;
    cfg.size_in_byte = len;
    cfg.interrupt_mask = 0;
    if (dma_setup_handshake(APP_DMA, &cfg, true) != status_success) {
        return 0;
    }
    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    while (!(dma_check_transfer_status(APP_DMA, APP_TX_CH) & DMA_CHANNEL_STATUS_TC)) {
        if ((mchtmr_get_count(HPM_MCHTMR) - t0) > s_tmr_hz / 100U) {
            dma_disable_channel(APP_DMA, APP_TX_CH);
            return 0;
        }
    }
    return (uint32_t) (((uint64_t) (mchtmr_get_count(HPM_MCHTMR) - t0) * 1000000ULL) / s_tmr_hz);
}

static hpm_stat_t rx_dma_start(uint32_t len)
{
    dma_channel_config_t ch = {0};

    /* 清掉 FIFO 里可能残留的字节与空闲标志, 否则 DMA 可能读到伪数据 */
    while (uart_check_status(APP_UART, uart_stat_data_ready)) {
        (void) APP_UART->RBR;
    }
    uart_clear_rxline_idle_flag(APP_UART);

    dmamux_config(APP_DMAMUX, APP_RX_DMAMUX_CH, APP_UART_RX_DMA_SRC, true);

    dma_default_channel_config(APP_DMA, &ch);
    ch.src_addr = (uint32_t) &APP_UART->RBR;
    ch.src_width = DMA_TRANSFER_WIDTH_BYTE;
    ch.src_addr_ctrl = DMA_ADDRESS_CONTROL_FIXED;
    ch.src_mode = DMA_HANDSHAKE_MODE_HANDSHAKE;
    ch.src_burst_size = DMA_NUM_TRANSFER_PER_BURST_1T;
    ch.dst_addr = (uint32_t) s_rx_buf;
    ch.dst_width = DMA_TRANSFER_WIDTH_BYTE;
    ch.dst_addr_ctrl = DMA_ADDRESS_CONTROL_INCREMENT;
    ch.dst_mode = DMA_HANDSHAKE_MODE_NORMAL;
    ch.size_in_byte = len;
    return dma_setup_channel(APP_DMA, APP_RX_CH, &ch, true);
}

static bool loopback_probe(void)
{
    uint8_t tx = 0x5A, rx = 0;
    uart_modem_enable_loopback(APP_UART);
    uart_send_data(APP_UART, &tx, 1);
    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    while ((mchtmr_get_count(HPM_MCHTMR) - t0) < s_tmr_hz / 200U) {
        if (uart_check_status(APP_UART, uart_stat_data_ready)) {
            uart_receive_data(APP_UART, &rx, 1);
            break;
        }
    }
    return (rx == tx);
}

static void cmd_uartinfo(void)
{
    printf("UART4 @0x%08lx baud=%lu, TX=PC16 RX=PC17\r\n",
           (unsigned long) APP_UART, (unsigned long) APP_UART_BAUD);
    printf("  RX DMA: HDMA ch%u (DMAMUX ch%u), idle threshold=10 bit\r\n",
           (unsigned) APP_RX_CH, (unsigned) APP_RX_DMAMUX_CH);
    printf("  internal loopback: %s\r\n", s_loopback_ok ? "OK" : "FAILED/unknown");
    printf("  idle irq total: %lu\r\n", (unsigned long) s_idle_cnt);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uartinfo, cmd_uartinfo, UART4 + DMA + idle detection summary);

static void cmd_uarttest(int argc, char *argv[])
{
    uint32_t n = 20;
    if (argc >= 2 && (bsp_parse_u32(argv[1], &n) || n < 1 || n > 100)) {
        printf("usage: uarttest [1..100]\r\n");
        return;
    }
    printf("uarttest: %lu 帧, 长度 1..%lu 变化, 帧间 1 ms 空闲\r\n",
           (unsigned long) n, (unsigned long) APP_FRAME_MAX);

    uint32_t frames = 0, idle_ok = 0, poll_ok = 0, dma_ok = 0, timeouts = 0;
    uint64_t lat_sum = 0, tx_sum = 0;
    uint32_t lat_min = 0xFFFFFFFFUL, lat_max = 0, tx_max = 0;
    uint32_t ticks_per_ms = s_tmr_hz / 1000U;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t len = 1U + ((i * 7U) % (APP_FRAME_MAX - 1U));
        for (uint32_t k = 0; k < len; k++) {
            s_tx_buf[k] = (uint8_t) (i + k);
            s_rx_buf[k] = 0;
            s_poll_buf[k] = 0;
        }
        frames++;
        s_idle_seen = false;
        if (rx_dma_start(len) != status_success) {
            printf("  dma start failed at frame %lu\r\n", (unsigned long) i);
            break;
        }
        uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
        uint32_t tx_us = uart_dma_send(s_tx_buf, len);
        if (tx_us) {
            tx_sum += tx_us;
            if (tx_us > tx_max) tx_max = tx_us;
        }
        /* 等 RX 空闲中断(帧界), 最多 10 ms */
        while (!s_idle_seen && (mchtmr_get_count(HPM_MCHTMR) - t0) < ticks_per_ms * 10U) {
        }
        if (s_idle_seen) {
            idle_ok++;
            uint32_t lat = (uint32_t) (((uint64_t) (s_idle_tick - t0) * 1000000ULL) / s_tmr_hz);
            if (lat < lat_min) lat_min = lat;
            if (lat > lat_max) lat_max = lat;
            lat_sum += lat;
        } else {
            timeouts++;
            dma_disable_channel(APP_DMA, APP_RX_CH);
        }
        /* 对照: UART FIFO 里剩余的字节用轮询读出来, 判断 DMA 是否搬错 */
        uint32_t polled = 0;
        while (polled < len && uart_check_status(APP_UART, uart_stat_data_ready)) {
            s_poll_buf[polled++] = (uint8_t) APP_UART->RBR;
        }
        if ((polled == len) && (memcmp(s_tx_buf, s_poll_buf, len) == 0)) {
            poll_ok++;
        }
        if (memcmp(s_tx_buf, s_rx_buf, len) == 0) {
            dma_ok++;
        }
        board_delay_ms(1);
    }

    printf("  result: frames=%lu idle_frame=%lu timeout=%lu\r\n",
           (unsigned long) frames, (unsigned long) idle_ok, (unsigned long) timeouts);
    printf("  data: poll_rx_ok=%lu/%lu, dma_rx_ok=%lu/%lu (DMA RX 异常见 README 坑 1)\r\n",
           (unsigned long) poll_ok, (unsigned long) frames,
           (unsigned long) dma_ok, (unsigned long) frames);
    printf("  loss=%.0f ppm (按 idle 帧界计), tx->idle latency min=%lu us avg=%lu us max=%lu us\r\n",
           frames ? (1000000.0 * (double) (frames - idle_ok) / (double) frames) : 0.0,
           (unsigned long) lat_min, (unsigned long) (idle_ok ? (lat_sum / idle_ok) : 0),
           (unsigned long) lat_max);
    printf("  tx_dma: avg=%lu us max=%lu us, idle irq total=%lu\r\n",
           (unsigned long) (frames ? (tx_sum / frames) : 0), (unsigned long) tx_max,
           (unsigned long) s_idle_cnt);
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uarttest, cmd_uarttest, framed loopback test with DMA + idle IRQ: uarttest [n]);

int main(void)
{
    bsp_init();
    s_tmr_hz = clock_get_frequency(clock_mchtmr0);

    printf("\r\n=== dma_uart_rx_idle: UART4 RX DMA + idle interrupt ===\r\n");
    if (uart_setup() != status_success) {
        printf("uart init failed\r\n");
    }
    intc_m_enable_irq_with_priority(APP_UART_IRQ, 2);
    s_loopback_ok = loopback_probe();
    while (uart_check_status(APP_UART, uart_stat_data_ready)) {
        (void) APP_UART->RBR;
    }
    printf("internal loopback: %s\r\n", s_loopback_ok ? "OK" : "FAILED (需外部短接 PC16-PC17)");
    cmd_uartinfo();
    printf("shell: uartinfo | uarttest [n]\r\n");

    uint32_t last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if (bsp_uptime_ms() - last >= 5000U) {
            last = bsp_uptime_ms();
            printf("uart4: idle_irq=%lu\r\n", (unsigned long) s_idle_cnt);
        }
    }
    return 0;
}
