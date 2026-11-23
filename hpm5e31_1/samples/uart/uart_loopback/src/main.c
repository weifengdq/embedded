/*
 * uart_loopback - UART4(PC16/PC17) 内部回环(MCR LOOP) 自验证:
 *                 波特率误差、数据完整性、FIFO 深度/溢出、回环吞吐
 *
 * 硬件: hpm5e31_1, UART4 TX=PC16 RX=PC17 (与 hpm5e00evk 一致), 不需要任何外部接线:
 *       打开 UART MCR 的 LOOP 位后, 控制器内部把 TX 接到 RX.
 * 控制台仍是 UART0(PA00/PA01, 921600), 不会被影响.
 * 命令: uart_info / uart_err / uart_selftest / uart_thru / uart_fifo / uart_baud
 */
#include "bsp.h"
#include "hpm_uart_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_iomux.h"

#define T_UART      HPM_UART4
#define T_UART_IRQ  IRQn_UART4
#define T_UART_CLK  clock_uart4
#define T_UART_PAD_TX IOC_PAD_PC16
#define T_UART_PAD_RX IOC_PAD_PC17

static uint32_t s_src_hz;
static uint32_t s_cpu_hz;
static uint32_t s_baud = 921600UL;
static uint8_t s_txbuf[4096];
static uint8_t s_rxbuf[4096];

static inline uint64_t cyc(void)
{
    return hpm_csr_get_core_mcycle();
}

static void cyc_to_ns(uint64_t c, uint32_t *ns, uint32_t *ns_frac)
{
    uint64_t v = c * 1000000000ULL / s_cpu_hz;
    *ns = (uint32_t) v;
    *ns_frac = (uint32_t) ((c * 100000000000ULL / s_cpu_hz) % 100ULL);
}

/* 不依赖 SDK 重试上限的阻塞收发 */
static inline void u_putc(uint8_t c)
{
    while (!(T_UART->LSR & UART_LSR_THRE_MASK)) {
    }
    T_UART->THR = UART_THR_THR_SET(c);
}

static inline int u_getc_spin(uint32_t spin)
{
    while (!(T_UART->LSR & UART_LSR_DR_MASK)) {
        if (spin-- == 0U) { return -1; }
    }
    return (int) (T_UART->RBR & UART_RBR_RBR_MASK);
}

static void u_wait_temt(void)
{
    while (!(T_UART->LSR & UART_LSR_TEMT_MASK)) {
    }
}

static void u_drain_rx(void)
{
    uint32_t guard = 100000U;
    while ((T_UART->LSR & UART_LSR_DR_MASK) && guard) {
        (void) T_UART->RBR;
        guard--;
    }
    /* 清 overrun/framing 等状态 */
    (void) T_UART->LSR;
}

static hpm_stat_t uart_setup(uint32_t baud, bool loopback)
{
    uart_config_t cfg = {0};
    uart_default_config(T_UART, &cfg);
    cfg.src_freq_in_hz = s_src_hz;
    cfg.baudrate = baud;
    cfg.fifo_enable = true;
    cfg.rx_fifo_level = uart_rx_fifo_trg_not_empty;
    cfg.tx_fifo_level = uart_tx_fifo_trg_not_full;
    cfg.modem_config.loop_back_en = loopback;
    hpm_stat_t st = uart_init(T_UART, &cfg);
    if (st == status_success) {
        s_baud = baud;
    }
    return st;
}

/* 从 OSCR/DLL/DLM 回读实际分频, 计算器件真实波特率 */
static uint32_t uart_reg_baud(UART_Type *u, uint32_t src_hz, uint32_t *osc_out, uint32_t *div_out)
{
    uint32_t lcr = u->LCR;
    uint32_t div, osc;
    u->LCR = lcr | UART_LCR_DLAB_MASK;
    div = ((uint32_t) UART_DLM_DLM_GET(u->DLM) << 8) | UART_DLL_DLL_GET(u->DLL);
    u->LCR = lcr;
    osc = UART_OSCR_OSC_GET(u->OSCR);
    if (osc_out) { *osc_out = osc; }
    if (div_out) { *div_out = div; }
    if ((osc == 0U) || (div == 0U) || (src_hz == 0U)) { return 0U; }
    return src_hz / (osc * div);
}

/* 用一次 TX 突发 + 等待 TEMT 精确测波特率(回环模式下 TX 同时被 RX 接收) */
static int measure_baud(uint32_t baud, uint32_t *meas, uint32_t *reg)
{
    if (uart_setup(baud, true) != status_success) { return -1; }
    u_drain_rx();
    uint32_t n = baud / 200UL;
    if (n < 256U) { n = 256U; }
    if (n > 4096U) { n = 4096U; }
    uint64_t t0 = cyc();
    for (uint32_t i = 0; i < n; i++) {
        u_putc(0x55U);
    }
    u_wait_temt();
    uint64_t dt = cyc() - t0;
    /* bits = n*10 (8N1); baud = bits / (dt/cpu) */
    *meas = (uint32_t) (((uint64_t) n * 10ULL * s_cpu_hz) / dt);
    *reg = uart_reg_baud(T_UART, s_src_hz, NULL, NULL);
    u_drain_rx();
    return 0;
}

static int32_t ppm_err(uint32_t measured, uint32_t target)
{
    int64_t d = (int64_t) measured - (int64_t) target;
    return (int32_t) (d * 1000000LL / (int64_t) target);
}

/* ---- shell 命令 ---- */
static void cmd_info(int argc, char *argv[])
{
    (void) argc; (void) argv;
    uint32_t osc, div, baud;
    baud = uart_reg_baud(T_UART, s_src_hz, &osc, &div);
    printf("uart_loopback @ hpm5e31_1\r\n");
    printf("  UART4 TX=PC16 RX=PC17, irq=%d, 内部 MCR LOOP 回环\r\n", (int) IRQn_UART4);
    printf("  src_clk=%lu Hz, requested=%lu, OSCR=%lu, DIV=%lu -> reg_baud=%lu\r\n",
           (unsigned long) s_src_hz, (unsigned long) s_baud,
           (unsigned long) osc, (unsigned long) div, (unsigned long) baud);
    printf("  FIFO=%u bytes, cpu=%lu Hz\r\n", (unsigned) uart_get_fifo_size(T_UART),
           (unsigned long) s_cpu_hz);
    printf("  console: UART0 921600 (寄存器回读 %lu Hz 分频)\r\n",
           (unsigned long) uart_reg_baud((UART_Type *) BOARD_CONSOLE_UART_BASE,
                                         (uint32_t) clock_get_frequency(BOARD_CONSOLE_UART_CLK_NAME),
                                         NULL, NULL));
}

static void cmd_err(int argc, char *argv[])
{
    (void) argc; (void) argv;
    static const uint32_t bauds[] = { 9600, 38400, 115200, 460800, 921600,
                                      1500000, 2000000, 3000000, 4000000, 5000000 };
    uint32_t orig = s_baud;
    printf("baud rate accuracy: loopback TX burst + TEMT, bits=10/byte (8N1), src=%lu Hz\r\n",
           (unsigned long) s_src_hz);
    printf("%10s %10s %10s %8s %8s\r\n", "request", "reg_baud", "measured", "reg_err", "meas_err");
    for (uint32_t i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++) {
        uint32_t meas = 0U, reg = 0U;
        if (measure_baud(bauds[i], &meas, &reg) != 0) {
            printf("%10lu    (no suitable divider)\r\n", (unsigned long) bauds[i]);
            continue;
        }
        printf("%10lu %10lu %10lu %7ldp %7ldp\r\n",
               (unsigned long) bauds[i], (unsigned long) reg, (unsigned long) meas,
               (long) ppm_err(reg, bauds[i]), (long) ppm_err(meas, bauds[i]));
    }
    (void) uart_setup(orig, true);
    printf("(reg_err 由回读分频计算, meas_err 为实测; 1 ppm = 0.0001%%)\r\n");
}

static int loopback_check(uint32_t nbytes, uint32_t chunk, uint32_t *errors)
{
    uint32_t err = 0U;
    if (nbytes > sizeof(s_txbuf)) { nbytes = sizeof(s_txbuf); }
    for (uint32_t i = 0; i < nbytes; i++) {
        s_txbuf[i] = (uint8_t) (i * 7U + 0x5AU);
    }
    for (uint32_t off = 0; off < nbytes; off += chunk) {
        uint32_t k = ((nbytes - off) > chunk) ? chunk : (nbytes - off);
        for (uint32_t i = 0; i < k; i++) {
            u_putc(s_txbuf[off + i]);
        }
        for (uint32_t i = 0; i < k; i++) {
            int c = u_getc_spin(2000000UL);
            if (c < 0 || (uint8_t) c != s_txbuf[off + i]) { err++; }
        }
    }
    *errors = err;
    return 0;
}

static void cmd_selftest(int argc, char *argv[])
{
    uint32_t nbytes = 1024U, chunk = 16U, err = 0U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &nbytes); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &chunk); }
    if ((chunk == 0U) || (chunk > sizeof(s_txbuf))) { chunk = 16U; }
    if (uart_setup(s_baud, true) != status_success) {
        printf("uart setup failed\r\n");
        return;
    }
    u_drain_rx();
    uint64_t t0 = cyc();
    (void) loopback_check(nbytes, chunk, &err);
    uint64_t dt = cyc() - t0;
    uint32_t ns, nsf;
    cyc_to_ns(dt, &ns, &nsf);
    uint32_t kbs = (uint32_t) (((uint64_t) nbytes * s_cpu_hz) / dt / 1000ULL);
    printf("selftest baud=%lu bytes=%lu chunk=%lu errors=%lu time=%lu.%02lu ms throughput=%lu KB/s\r\n",
           (unsigned long) s_baud, (unsigned long) nbytes, (unsigned long) chunk,
           (unsigned long) err, (unsigned long) (ns / 1000000U),
           (unsigned long) ((ns % 1000000U) / 10000U), (unsigned long) kbs);
}

static void cmd_thru(int argc, char *argv[])
{
    uint32_t nbytes = 4096U, baud = s_baud;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &nbytes); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &baud); }
    if (nbytes > sizeof(s_txbuf)) { nbytes = sizeof(s_txbuf); }
    if (uart_setup(baud, true) != status_success) {
        printf("no suitable divider for %lu\r\n", (unsigned long) baud);
        return;
    }
    u_drain_rx();
    uint32_t err = 0U;
    uint64_t t0 = cyc();
    (void) loopback_check(nbytes, 32U, &err);
    uint64_t dt = cyc() - t0;
    uint32_t bps = (uint32_t) (((uint64_t) nbytes * s_cpu_hz) / dt);
    uint32_t kbs = bps / 1000U;
    uint32_t theory = baud / 10U / 1000U;
    printf("thru baud=%lu bytes=%lu errors=%lu measured=%lu KB/s theoretical=%lu KB/s efficiency=%lu%%\r\n",
           (unsigned long) baud, (unsigned long) nbytes, (unsigned long) err,
           (unsigned long) kbs, (unsigned long) theory,
           (unsigned long) ((theory != 0U) ? (kbs * 100U / theory) : 0U));
    (void) uart_setup(s_baud, true);
}

static void cmd_fifo(int argc, char *argv[])
{
    (void) argc; (void) argv;
    if (uart_setup(s_baud, true) != status_success) { return; }
    u_drain_rx();
    uint32_t total = uart_get_fifo_size(T_UART) * 3U;
    if (total > 256U) { total = 256U; }
    for (uint32_t i = 0; i < total; i++) {
        u_putc((uint8_t) i);
    }
    u_wait_temt();
    /* 只读回 FIFO 中现存的数据个数 */
    uint32_t got = 0U;
    while (T_UART->LSR & UART_LSR_DR_MASK) {
        (void) T_UART->RBR;
        got++;
        if (got > 4096U) { break; }
    }
    uint32_t lsr = T_UART->LSR;
    printf("fifo: sent=%lu without reading, fifo_size=%u, received=%lu, overrun=%d, framing=%d\r\n",
           (unsigned long) total, (unsigned) uart_get_fifo_size(T_UART),
           (unsigned long) got,
           (lsr & UART_LSR_OE_MASK) ? 1 : 0, (lsr & UART_LSR_FE_MASK) ? 1 : 0);
    u_drain_rx();
}

static void cmd_baud(int argc, char *argv[])
{
    uint32_t baud = s_baud;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &baud); }
    uint32_t meas = 0U, reg = 0U;
    if (measure_baud(baud, &meas, &reg) != 0) {
        printf("no suitable divider for %lu (src=%lu)\r\n",
               (unsigned long) baud, (unsigned long) s_src_hz);
        return;
    }
    printf("baud=%lu reg_baud=%lu measured=%lu reg_err=%ld ppm meas_err=%ld ppm\r\n",
           (unsigned long) baud, (unsigned long) reg, (unsigned long) meas,
           (long) ppm_err(reg, baud), (long) ppm_err(meas, baud));
    (void) uart_setup(baud, true);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uart_info, cmd_info, uart4: pin/clock/fifo info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uart_err, cmd_err, uart4: baud rate error table);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uart_selftest, cmd_selftest, uart4: uart_selftest [bytes] [chunk] loopback verify);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uart_thru, cmd_thru, uart4: uart_thru [bytes] [baud] loopback throughput);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uart_fifo, cmd_fifo, uart4: fifo depth / overflow demo);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 uart_baud, cmd_baud, uart4: uart_baud <rate> switch and verify);

int main(void)
{
    bsp_init();
    board_init_uart(HPM_UART4);              /* PC16/PC17 复用 + UART4 时钟 */
    s_src_hz = clock_get_frequency(T_UART_CLK);
    s_cpu_hz = clock_get_frequency(clock_cpu0);

    printf("\r\n=== uart_loopback (UART4 internal loopback, no wire needed) ===\r\n");
    printf("uart4 src_clk=%lu Hz, cpu=%lu Hz\r\n",
           (unsigned long) s_src_hz, (unsigned long) s_cpu_hz);
    if (uart_setup(921600UL, true) != status_success) {
        printf("uart_init failed!\r\n");
        while (1) { bsp_shell_poll(); }
    }
    printf("loopback enabled: MCR.LOOP=1, TX->RX inside controller\r\n");

    cmd_info(0, NULL);
    cmd_selftest(0, NULL);
    cmd_fifo(0, NULL);
    cmd_thru(0, NULL);
    cmd_err(0, NULL);
    printf("try 'uart_baud 3000000' or 'uart_thru 4096 3000000'\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
