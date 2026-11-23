/*
 * uart_irq_ring - UART4 内部回环 + RX 中断 + FIFO + 256B 环形缓冲
 *
 * 硬件: hpm5e31_1, UART4 TX=PC16 RX=PC17, MCR.LOOP=1 内部自环(无需接线).
 * 链路: TX --(内部回环)--> RX FIFO(32B) --RX中断--> 环形缓冲(256B) --> 主循环校验
 * 目的: 演示"中断 + FIFO 触发级 + 环形缓冲"三件套, 并量化 ISR 次数/丢字节/吞吐.
 * 命令: uart_ring_info / uart_ring_burst <n> [chunk] / uart_ring_stats / uart_ring_dump
 */
#include "bsp.h"
#include "hpm_uart_drv.h"
#include "hpm_clock_drv.h"

#define T_UART      HPM_UART4
#define T_UART_IRQ  IRQn_UART4
#define T_UART_CLK  clock_uart4
#define RING_SIZE   256U
#define RING_MASK   (RING_SIZE - 1U)

static uint32_t s_src_hz;
static uint32_t s_cpu_hz;
static uint32_t s_baud = 921600UL;

static volatile uint8_t  s_ring[RING_SIZE];
static volatile uint32_t s_rd, s_wr;
static volatile uint32_t s_isr_cnt, s_rx_bytes, s_ring_drop, s_fifo_overrun;

static inline uint64_t cyc(void)
{
    return hpm_csr_get_core_mcycle();
}

static uint32_t ring_count(void)
{
    return (s_wr - s_rd) & RING_MASK;
}

SDK_DECLARE_EXT_ISR_M(T_UART_IRQ, isr_uart)
void isr_uart(void)
{
    uint8_t id = uart_get_irq_id(T_UART);
    s_isr_cnt++;
    if (id == uart_intr_id_rx_data_avail) {
        while (uart_check_status(T_UART, uart_stat_data_ready)) {
            uint8_t c = uart_read_byte(T_UART);
            uint32_t nxt = (s_wr + 1U) & RING_MASK;
            if (nxt == s_rd) {
                s_ring_drop++;          /* 环形缓冲满: 丢字节 */
            } else {
                s_ring[s_wr] = c;
                s_wr = nxt;
            }
            s_rx_bytes++;
        }
        if (uart_check_status(T_UART, uart_stat_overrun_error)) {
            s_fifo_overrun++;           /* FIFO 溢出(读 LSR 时清除) */
        }
    }
}

static hpm_stat_t uart_setup(uint32_t baud)
{
    uart_config_t cfg = {0};
    uart_default_config(T_UART, &cfg);
    cfg.src_freq_in_hz = s_src_hz;
    cfg.baudrate = baud;
    cfg.fifo_enable = true;
    cfg.rx_fifo_level = uart_rx_fifo_trg_not_empty;   /* FIFO 里 >=1 字节触发中断 */
    cfg.tx_fifo_level = uart_tx_fifo_trg_not_full;
    cfg.modem_config.loop_back_en = true;
    hpm_stat_t st = uart_init(T_UART, &cfg);
    if (st == status_success) {
        s_baud = baud;
    }
    return st;
}

static inline void u_putc(uint8_t c)
{
    while (!(T_UART->LSR & UART_LSR_THRE_MASK)) {
    }
    T_UART->THR = UART_THR_THR_SET(c);
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("uart_irq_ring @ hpm5e31_1\r\n");
    printf("  UART4 TX=PC16 RX=PC17, internal MCR loopback, src_clk=%lu Hz\r\n",
           (unsigned long) s_src_hz);
    printf("  baud=%lu, FIFO=%u bytes, RX trigger=not_empty, ring=%u bytes\r\n",
           (unsigned long) s_baud, (unsigned) uart_get_fifo_size(T_UART), RING_SIZE);
    printf("  irq=%d priority=2, isr=%lu rx_bytes=%lu ring_drop=%lu fifo_overrun=%lu\r\n",
           (int) T_UART_IRQ, (unsigned long) s_isr_cnt, (unsigned long) s_rx_bytes,
           (unsigned long) s_ring_drop, (unsigned long) s_fifo_overrun);
}

static void cmd_stats(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("isr=%lu rx_bytes=%lu ring_level=%lu ring_drop=%lu fifo_overrun=%lu\r\n",
           (unsigned long) s_isr_cnt, (unsigned long) s_rx_bytes,
           (unsigned long) ring_count(), (unsigned long) s_ring_drop,
           (unsigned long) s_fifo_overrun);
}

static void cmd_dump(int argc, char *argv[])
{
    (void) argc; (void) argv;
    uint32_t n = ring_count();
    if (n > 32U) { n = 32U; }
    printf("ring level=%lu, first %lu bytes:", (unsigned long) ring_count(), (unsigned long) n);
    for (uint32_t i = 0; i < n; i++) {
        printf(" %02X", s_ring[(s_rd + i) & RING_MASK]);
    }
    printf("\r\n");
}

/* 突发发送 n 字节(带自增图案), 由 RX 中断收进环形缓冲; 分批校验, 不丢字节 */
static void cmd_burst(int argc, char *argv[])
{
    uint32_t n = 128U, pass = 200U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &n); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &pass); }
    if (n > 4096U) { n = 4096U; }
    if (pass > (RING_SIZE - 2U)) { pass = RING_SIZE - 2U; }   /* 不超过环形缓冲容量 */
    if (pass == 0U) { pass = 200U; }

    uint32_t isr0 = s_isr_cnt, bytes0 = s_rx_bytes, drop0 = s_ring_drop;
    uint32_t err = 0U, verified = 0U;
    uint64_t t0 = cyc();
    uint32_t off = 0U;
    while (off < n) {
        uint32_t k = ((n - off) > pass) ? pass : (n - off);
        for (uint32_t i = 0; i < k; i++) {
            u_putc((uint8_t) ((off + i) * 3U + 0x11U));
        }
        /* 等本批全部进入环形缓冲(超时保护: 10 ms 量级) */
        uint32_t guard = 4000000U;
        while ((ring_count() < k) && guard) { guard--; }
        for (uint32_t i = 0; i < k; i++) {
            uint8_t expect = (uint8_t) ((off + i) * 3U + 0x11U);
            if (ring_count() == 0U) { err++; continue; }
            uint8_t c = s_ring[s_rd];
            s_rd = (s_rd + 1U) & RING_MASK;
            if (c != expect) { err++; }
            verified++;
        }
        off += k;
    }
    uint64_t dt = cyc() - t0;
    uint32_t ms_x100 = (uint32_t) (dt * 100000ULL / s_cpu_hz);
    printf("burst n=%lu pass=%lu: isr+%lu rx+%lu drop+%lu verified=%lu errors=%lu, %lu.%02lu ms, %lu KB/s\r\n",
           (unsigned long) n, (unsigned long) pass,
           (unsigned long) (s_isr_cnt - isr0), (unsigned long) (s_rx_bytes - bytes0),
           (unsigned long) (s_ring_drop - drop0), (unsigned long) verified, (unsigned long) err,
           (unsigned long) (ms_x100 / 100U), (unsigned long) (ms_x100 % 100U),
           (unsigned long) (((uint64_t) n * s_cpu_hz) / dt / 1000ULL));
}
/* ---------------- R5 新增: FIFO 触发级对比 / 1 MB 连续误码 ---------------- */

/* 触发级对比: p=0 not_empty / 1 gt_quarter / 2 gt_half / 3 gt_3quarter;
 * 每点发 128 B 内部回环, 统计 RX 中断次数 (同一份 ISR, 只改触发级). */
static void cmd_trig(int argc, char *argv[])
{
    uint32_t p = 0U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &p); }
    if (p > 3U) { p = 3U; }
    uart_config_t cfg = {0};
    uart_default_config(T_UART, &cfg);
    cfg.src_freq_in_hz = s_src_hz;
    cfg.baudrate = s_baud;
    cfg.fifo_enable = true;
    cfg.rx_fifo_level = (p == 0U) ? uart_rx_fifo_trg_not_empty :
                        (p == 1U) ? uart_rx_fifo_trg_gt_one_quarter :
                        (p == 2U) ? uart_rx_fifo_trg_gt_half : uart_rx_fifo_trg_gt_three_quarters;
    cfg.tx_fifo_level = uart_tx_fifo_trg_not_full;
    cfg.modem_config.loop_back_en = true;
    if (uart_init(T_UART, &cfg) != status_success) { printf("uart_init failed\r\n"); return; }
    uart_enable_irq(T_UART, uart_intr_rx_data_avail_or_timeout);
    uart_clear_rx_fifo(T_UART);
    s_rd = 0U; s_wr = 0U;
    s_isr_cnt = 0U; s_rx_bytes = 0U; s_ring_drop = 0U; s_fifo_overrun = 0U;

    const uint32_t n = 128U;
    uint64_t t0 = cyc();
    for (uint32_t i = 0U; i < n; i++) { u_putc((uint8_t) (i * 5U + 0x31U)); }
    uint32_t guard = 8000000U;
    while ((s_rx_bytes < n) && guard) { guard--; }
    uint64_t dt = cyc() - t0;
    /* 等最后一包超时中断收尾 */
    for (volatile uint32_t d = 0U; d < 20000U; d++) { }

    uint32_t err = 0U;
    for (uint32_t i = 0U; i < n; i++) {
        if (ring_count() == 0U) { err++; continue; }
        uint8_t c = s_ring[s_rd];
        s_rd = (s_rd + 1U) & RING_MASK;
        if (c != (uint8_t) (i * 5U + 0x31U)) { err++; }
    }
    uint32_t ms100 = (uint32_t) (dt * 100000ULL / s_cpu_hz);
    printf("trig=%lu (0=not_empty,2=gt_half,3=gt_3q) baud=%lu n=%lu: isr=%lu rx=%lu err=%lu %lu.%02lu ms\r\n",
           (unsigned long) p, (unsigned long) s_baud, (unsigned long) n,
           (unsigned long) s_isr_cnt, (unsigned long) s_rx_bytes, (unsigned long) err,
           (unsigned long) (ms100 / 100U), (unsigned long) (ms100 % 100U));
    printf("  => 每 %lu 字节一次中断\r\n", (unsigned long) (s_isr_cnt ? (n / s_isr_cnt) : 0U));
}

/* 连续误码: 内部回环发 n KB, 逐批校验, 统计错误字节与吞吐 */
static void cmd_ber(int argc, char *argv[])
{
    uint32_t kb = 1024U, baud = 3000000UL;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &kb); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &baud); }
    if (kb == 0U) { kb = 1024U; }
    if (kb > 8192U) { kb = 8192U; }
    if (uart_setup(baud) != status_success) {
        printf("uart_setup(%lu) failed\r\n", (unsigned long) baud);
        return;
    }
    uart_enable_irq(T_UART, uart_intr_rx_data_avail_or_timeout);
    uart_clear_rx_fifo(T_UART);
    s_rd = 0U; s_wr = 0U;
    s_isr_cnt = 0U; s_rx_bytes = 0U; s_ring_drop = 0U; s_fifo_overrun = 0U;

    uint32_t total = kb * 1024U;
    uint32_t done = 0U, err = 0U, verified = 0U;
    uint64_t t0 = cyc();
    while (done < total) {
        uint32_t k = 200U;
        if ((total - done) < k) { k = total - done; }
        for (uint32_t i = 0U; i < k; i++) {
            u_putc((uint8_t) ((done + i) * 7U + 0x5AU));
        }
        uint32_t guard = 8000000U;
        while ((ring_count() < k) && guard) { guard--; }
        for (uint32_t i = 0U; i < k; i++) {
            uint8_t expect = (uint8_t) ((done + i) * 7U + 0x5AU);
            if (ring_count() == 0U) { err++; continue; }
            uint8_t c = s_ring[s_rd];
            s_rd = (s_rd + 1U) & RING_MASK;
            if (c != expect) { err++; }
            verified++;
        }
        done += k;
    }
    uint64_t dt = cyc() - t0;
    uint32_t ms = (uint32_t) (dt * 1000ULL / s_cpu_hz);
    printf("ber: %lu KB @ %lu baud (实际 %lu): verified=%lu err=%lu drop=%lu overrun=%lu isr=%lu\r\n",
           (unsigned long) kb, (unsigned long) baud, (unsigned long) s_baud,
           (unsigned long) verified, (unsigned long) err, (unsigned long) s_ring_drop,
           (unsigned long) s_fifo_overrun, (unsigned long) s_isr_cnt);
    printf("  %lu ms, %lu KB/s, BER %s\r\n", (unsigned long) ms,
           (unsigned long) (((uint64_t) total * s_cpu_hz) / (dt ? dt : 1U) / 1000ULL),
           (err == 0U) ? "0 错误" : "有错误");
    (void) uart_setup(921600UL);
    uart_enable_irq(T_UART, uart_intr_rx_data_avail_or_timeout);
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uart_ring_trig, cmd_trig, uart4 irq: uart_ring_trig [0..3] fifo trigger compare);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uart_ring_ber, cmd_ber, uart4 irq: uart_ring_ber [KB] [baud] continuous BER);

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uart_ring_info, cmd_info, uart4 irq: config info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uart_ring_burst, cmd_burst, uart4 irq: uart_ring_burst <n> [chunk] burst + verify);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uart_ring_stats, cmd_stats, uart4 irq: isr/bytes/drop counters);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uart_ring_dump, cmd_dump, uart4 irq: dump up to 32 bytes in ring);

int main(void)
{
    bsp_init();
    board_init_uart(HPM_UART4);
    s_src_hz = clock_get_frequency(T_UART_CLK);
    s_cpu_hz = clock_get_frequency(clock_cpu0);
    s_rd = 0U; s_wr = 0U;

    printf("\r\n=== uart_irq_ring (UART4 loopback + RX irq + FIFO + ring) ===\r\n");
    if (uart_setup(921600UL) != status_success) {
        printf("uart_init failed\r\n");
    }
    uart_enable_irq(T_UART, uart_intr_rx_data_avail_or_timeout);
    intc_m_enable_irq_with_priority(T_UART_IRQ, 2);

    cmd_info(0, NULL);
    cmd_burst(0, NULL);       /* 上电自证: 128 字节突发 + 中断收取 + 校验 */
    cmd_stats(0, NULL);

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
