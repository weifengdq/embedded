/*
 * uart_dma - UART4 内部回环 + HDMA 收发: 吞吐/CPU 占用对比
 *
 * 硬件: hpm5e31_1, UART4 TX=PC16 RX=PC17, MCR.LOOP 内部自环, DMA=HDMA(片上 DMAV2)
 * 链路: 内存 --TX DMA--> UART4 THR --(loopback)--> RBR --RX DMA--> 内存
 * 命令: udma_info / udma_test [bytes] [baud] / udma_bench
 */
#include "bsp.h"
#include "hpm_uart_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_dmav2_drv.h"
#include "hpm_dmamux_drv.h"

#define T_UART      HPM_UART4
#define T_UART_CLK  clock_uart4
#define T_DMA       HPM_HDMA
#define T_DMAMUX    HPM_DMAMUX
#define TX_CH       2U
#define RX_CH       3U
#define TX_MUX_CH   DMA_SOC_CHN_TO_DMAMUX_CHN(T_DMA, TX_CH)
#define RX_MUX_CH   DMA_SOC_CHN_TO_DMAMUX_CHN(T_DMA, RX_CH)
#define BUF_SIZE    4096U

static uint32_t s_src_hz;
static uint32_t s_cpu_hz;
static uint32_t s_baud = 921600UL;

ATTR_PLACE_AT_NONCACHEABLE uint8_t s_tx[BUF_SIZE];
ATTR_PLACE_AT_NONCACHEABLE uint8_t s_rx[BUF_SIZE];

static inline uint64_t cyc(void)
{
    return hpm_csr_get_core_mcycle();
}

static hpm_stat_t uart_setup(uint32_t baud)
{
    uart_config_t cfg = {0};
    uart_default_config(T_UART, &cfg);
    cfg.src_freq_in_hz = s_src_hz;
    cfg.baudrate = baud;
    cfg.fifo_enable = true;
    cfg.rx_fifo_level = uart_rx_fifo_trg_not_empty;
    cfg.tx_fifo_level = uart_tx_fifo_trg_not_full;
    cfg.dma_enable = true;                    /* FCR.DMAE: 允许 FIFO 产生 DMA 请求 */
    cfg.modem_config.loop_back_en = true;
    hpm_stat_t st = uart_init(T_UART, &cfg);
    if (st == status_success) { s_baud = baud; }
    return st;
}

static bool dma_wait_tc(uint8_t ch, uint64_t max_cycles)
{
    uint64_t t0 = cyc();
    while (cyc() - t0 < max_cycles) {
        uint32_t st = dma_check_transfer_status(T_DMA, ch);
        if (st & (DMA_CHANNEL_STATUS_ERROR | DMA_CHANNEL_STATUS_ABORT)) { return false; }
        if (st & DMA_CHANNEL_STATUS_TC) { return true; }
    }
    return false;
}

/* 一次 DMA 回环传输: 先备 RX, 再启动 TX, 等两个通道 TC */
static int udma_transfer(uint32_t nbytes, uint32_t *errors, uint64_t *cycles)
{
    if (nbytes > BUF_SIZE) { nbytes = BUF_SIZE; }
    dma_handshake_config_t cfg;

    for (uint32_t i = 0; i < nbytes; i++) {
        s_tx[i] = (uint8_t) (i * 5U + 0x3CU);
        s_rx[i] = 0U;
    }
    /* RX: RBR(固定) -> 内存 */
    dma_default_handshake_config(T_DMA, &cfg);
    cfg.ch_index = RX_CH;
    cfg.src = (uint32_t) &T_UART->RBR;
    cfg.src_fixed = true;
    cfg.dst = (uint32_t) s_rx;
    cfg.dst_fixed = false;
    cfg.data_width = DMA_TRANSFER_WIDTH_BYTE;
    cfg.size_in_byte = nbytes;
    dmamux_config(T_DMAMUX, RX_MUX_CH, BOARD_APP_UART_RX_DMA_REQ, true);
    if (dma_setup_handshake(T_DMA, &cfg, true) != status_success) { return -1; }

    /* TX: 内存 -> THR(固定) */
    dma_default_handshake_config(T_DMA, &cfg);
    cfg.ch_index = TX_CH;
    cfg.src = (uint32_t) s_tx;
    cfg.src_fixed = false;
    cfg.dst = (uint32_t) &T_UART->THR;
    cfg.dst_fixed = true;
    cfg.data_width = DMA_TRANSFER_WIDTH_BYTE;
    cfg.size_in_byte = nbytes;
    dmamux_config(T_DMAMUX, TX_MUX_CH, BOARD_APP_UART_TX_DMA_REQ, true);

    uint64_t t0 = cyc();
    if (dma_setup_handshake(T_DMA, &cfg, true) != status_success) { return -2; }
    bool tx_ok = dma_wait_tc(TX_CH, 200000000ULL);
    bool rx_ok = dma_wait_tc(RX_CH, 200000000ULL);
    *cycles = cyc() - t0;

    uint32_t err = 0U;
    for (uint32_t i = 0; i < nbytes; i++) {
        if (s_rx[i] != s_tx[i]) { err++; }
    }
    *errors = err;
    if (!tx_ok || !rx_ok) { return -3; }
    return (err == 0U) ? 0 : -4;
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("uart_dma @ hpm5e31_1: UART4 loopback + HDMA\r\n");
    printf("  src_clk=%lu Hz, baud=%lu, dma=HDMA ch%d(TX)/ch%d(RX), dmamux=%lu/%lu\r\n",
           (unsigned long) s_src_hz, (unsigned long) s_baud, TX_CH, RX_CH,
           (unsigned long) TX_MUX_CH, (unsigned long) RX_MUX_CH);
    printf("  tx_req=0x%lx rx_req=0x%lx, dma_support=%s\r\n",
           (unsigned long) BOARD_APP_UART_TX_DMA_REQ, (unsigned long) BOARD_APP_UART_RX_DMA_REQ,
           "yes");
}

static void cmd_test(int argc, char *argv[])
{
    uint32_t n = 1024U, baud = s_baud;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &n); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &baud); }
    if (uart_setup(baud) != status_success) {
        printf("no suitable divider for %lu\r\n", (unsigned long) baud);
        return;
    }
    uint32_t err = 0U;
    uint64_t dt = 0U;
    int rc = udma_transfer(n, &err, &dt);
    printf("udma_test n=%lu baud=%lu rc=%d errors=%lu time=%lu us throughput=%lu KB/s\r\n",
           (unsigned long) n, (unsigned long) baud, rc, (unsigned long) err,
           (unsigned long) (dt * 1000000ULL / s_cpu_hz),
           (unsigned long) (((uint64_t) n * s_cpu_hz) / (dt ? dt : 1U) / 1000ULL));
}

static void cmd_bench(int argc, char *argv[])
{
    (void) argc; (void) argv;
    static const uint32_t bauds[] = { 115200, 460800, 921600, 2000000, 3000000 };
    printf("%9s %8s %10s %10s %8s\r\n", "baud", "bytes", "time_us", "KB/s", "err");
    for (uint32_t i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++) {
        if (uart_setup(bauds[i]) != status_success) {
            printf("%9lu   no suitable divider\r\n", (unsigned long) bauds[i]);
            continue;
        }
        uint32_t err = 0U;
        uint64_t dt = 0U;
        (void) udma_transfer(2048U, &err, &dt);
        printf("%9lu %8u %10lu %10lu %8lu\r\n", (unsigned long) bauds[i], 2048U,
               (unsigned long) (dt * 1000000ULL / s_cpu_hz),
               (unsigned long) (((uint64_t) 2048U * s_cpu_hz) / (dt ? dt : 1U) / 1000ULL),
               (unsigned long) err);
    }
    (void) uart_setup(921600UL);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 udma_info, cmd_info, uart4 dma: config info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 udma_test, cmd_test, uart4 dma: udma_test [bytes] [baud] one dma loopback);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 udma_bench, cmd_bench, uart4 dma: throughput table over baud rates);

int main(void)
{
    bsp_init();
    board_init_uart(HPM_UART4);
    s_src_hz = clock_get_frequency(T_UART_CLK);
    s_cpu_hz = clock_get_frequency(clock_cpu0);

    printf("\r\n=== uart_dma (UART4 loopback + HDMA) ===\r\n");
    if (uart_setup(921600UL) != status_success) {
        printf("uart_init failed\r\n");
    }
    cmd_info(0, NULL);
    cmd_test(0, NULL);
    cmd_bench(0, NULL);

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
