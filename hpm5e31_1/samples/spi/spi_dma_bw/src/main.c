/*
 * spi_dma_bw - SPI1 主机 DMA 收发带宽实测 (无外部从器件)
 *
 * 硬件: hpm5e31_1, SPI1 SCLK=PC10 MISO=PC12 MOSI=PC13 CS=PC11
 * 链路: 内存 --TX DMA--> SPI1.DATA(移位出去); SPI1.DATA(移入) --RX DMA--> 内存
 *       没有从器件时 MISO 静态电平, 但 SCLK 与 DMA 搬运照常进行, 所以带宽数据有效.
 * 命令: sdma_info / sdma_test [bytes] [sclk] / sdma_bench
 */
#include "bsp.h"
#include "hpm_spi_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_dmav2_drv.h"
#include "hpm_dmamux_drv.h"

#define T_SPI      HPM_SPI1
#define T_DMA      HPM_HDMA
#define T_DMAMUX   HPM_DMAMUX
#define TX_CH      0U
#define RX_CH      1U
#define TX_MUX_CH  DMA_SOC_CHN_TO_DMAMUX_CHN(T_DMA, TX_CH)
#define RX_MUX_CH  DMA_SOC_CHN_TO_DMAMUX_CHN(T_DMA, RX_CH)
#define BUF_SIZE   4096U

ATTR_PLACE_AT_NONCACHEABLE uint8_t s_tx[BUF_SIZE];
ATTR_PLACE_AT_NONCACHEABLE uint8_t s_rx[BUF_SIZE];

static uint32_t s_src_hz;
static uint32_t s_cpu_hz;
static uint32_t s_sclk = 20000000UL;

static inline uint64_t cyc(void)
{
    return hpm_csr_get_core_mcycle();
}

static hpm_stat_t spi_setup(uint32_t sclk)
{
    spi_timing_config_t timing = {0};
    spi_format_config_t format = {0};
    spi_master_get_default_timing_config(&timing);
    timing.master_config.clk_src_freq_in_hz = s_src_hz;
    timing.master_config.sclk_freq_in_hz = sclk;
    if (spi_master_timing_init(T_SPI, &timing) != status_success) {
        return status_fail;
    }
    spi_master_get_default_format_config(&format);
    format.common_config.data_len_in_bits = 8U;
    format.common_config.mode = spi_master_mode;
    format.common_config.cpol = spi_sclk_high_idle;
    format.common_config.cpha = spi_sclk_sampling_even_clk_edges;
    spi_format_init(T_SPI, &format);
    s_sclk = sclk;
    return status_success;
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

static int spi_dma_transfer(uint32_t nbytes, uint64_t *cycles)
{
    dma_handshake_config_t cfg;
    spi_control_config_t ctrl = {0};
    hpm_stat_t st;

    if (nbytes > BUF_SIZE) { nbytes = BUF_SIZE; }
    for (uint32_t i = 0; i < nbytes; i++) {
        s_tx[i] = (uint8_t) (i * 7U + 0x21U);
        s_rx[i] = 0U;
    }

    /* 1) 先配 control(含 tx/rx dma 使能), 再启动 DMA, 最后 spi_setup_dma_transfer 开始传输 */
    spi_master_get_default_control_config(&ctrl);
    ctrl.master_config.cmd_enable = false;
    ctrl.master_config.addr_enable = false;
    ctrl.common_config.tx_dma_enable = true;
    ctrl.common_config.rx_dma_enable = true;
    ctrl.common_config.trans_mode = spi_trans_write_read_together;
    ctrl.common_config.data_phase_fmt = spi_single_io_mode;

    /* TX DMA: 内存 -> SPI DATA */
    spi_disable_tx_dma(T_SPI);
    dma_default_handshake_config(T_DMA, &cfg);
    cfg.ch_index = TX_CH;
    cfg.src = (uint32_t) s_tx;
    cfg.src_fixed = false;
    cfg.dst = (uint32_t) &T_SPI->DATA;
    cfg.dst_fixed = true;
    cfg.data_width = DMA_TRANSFER_WIDTH_BYTE;
    cfg.size_in_byte = nbytes;
    dmamux_config(T_DMAMUX, TX_MUX_CH, BOARD_APP_SPI_TX_DMA, true);
    if (dma_setup_handshake(T_DMA, &cfg, true) != status_success) { return -1; }

    /* RX DMA: SPI DATA -> 内存 */
    dma_default_handshake_config(T_DMA, &cfg);
    cfg.ch_index = RX_CH;
    cfg.src = (uint32_t) &T_SPI->DATA;
    cfg.src_fixed = true;
    cfg.dst = (uint32_t) s_rx;
    cfg.dst_fixed = false;
    cfg.data_width = DMA_TRANSFER_WIDTH_BYTE;
    cfg.size_in_byte = nbytes;
    dmamux_config(T_DMAMUX, RX_MUX_CH, BOARD_APP_SPI_RX_DMA, true);
    if (dma_setup_handshake(T_DMA, &cfg, true) != status_success) { return -2; }

    /* 清掉可能残留的 DMA 状态 */
    (void) dma_check_transfer_status(T_DMA, TX_CH);
    (void) dma_check_transfer_status(T_DMA, RX_CH);

    uint64_t t0 = cyc();
    st = spi_setup_dma_transfer(T_SPI, &ctrl, NULL, NULL, nbytes, nbytes);
    if (st != status_success) { return -3; }
    bool tx_ok = dma_wait_tc(TX_CH, 100000000ULL);
    bool rx_ok = dma_wait_tc(RX_CH, 100000000ULL);
    (void) spi_wait_for_idle_status(T_SPI);
    *cycles = cyc() - t0;
    spi_disable_tx_dma(T_SPI);
    spi_disable_rx_dma(T_SPI);
    if (!tx_ok || !rx_ok) { return -4; }
    return 0;
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("spi_dma_bw @ hpm5e31_1: SPI1 + HDMA\r\n");
    printf("  src_clk=%lu Hz, cpu=%lu Hz, sclk=%lu Hz, dma ch%d(TX)/ch%d(RX)\r\n",
           (unsigned long) s_src_hz, (unsigned long) s_cpu_hz, (unsigned long) s_sclk,
           TX_CH, RX_CH);
    printf("  tx_req=0x%lx rx_req=0x%lx, DMAMUX TX=%lu RX=%lu\r\n",
           (unsigned long) BOARD_APP_SPI_TX_DMA, (unsigned long) BOARD_APP_SPI_RX_DMA,
           (unsigned long) TX_MUX_CH, (unsigned long) RX_MUX_CH);
    printf("  usage: sdma_test [bytes] [sclk_hz] / sdma_bench\r\n");
}

static void cmd_test(int argc, char *argv[])
{
    uint32_t n = 2048U, sclk = s_sclk;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &n); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &sclk); }
    if (spi_setup(sclk) != status_success) {
        printf("timing init failed for %lu\r\n", (unsigned long) sclk);
        return;
    }
    uint64_t dt = 0U;
    int rc = spi_dma_transfer(n, &dt);
    uint32_t kbs = (dt != 0U) ? (uint32_t) (((uint64_t) n * s_cpu_hz) / dt / 1000ULL) : 0U;
    uint32_t theory_kbs = sclk / 8U / 1000U;
    printf("sdma_test n=%lu sclk=%lu rc=%d time=%lu us bw=%lu KB/s theory=%lu KB/s eff=%lu%%\r\n",
           (unsigned long) n, (unsigned long) sclk, rc,
           (unsigned long) (dt * 1000000ULL / s_cpu_hz), (unsigned long) kbs,
           (unsigned long) theory_kbs,
           (unsigned long) ((theory_kbs != 0U) ? (kbs * 100U / theory_kbs) : 0U));
}

static void cmd_bench(int argc, char *argv[])
{
    (void) argc; (void) argv;
    static const uint32_t sclks[] = { 1000000, 5000000, 10000000, 20000000, 40000000, 60000000 };
    printf("%10s %8s %10s %10s %10s %6s\r\n", "sclk", "bytes", "time_us", "KB/s", "theory", "eff%");
    for (uint32_t i = 0; i < sizeof(sclks) / sizeof(sclks[0]); i++) {
        if (spi_setup(sclks[i]) != status_success) {
            printf("%10lu   timing init failed\r\n", (unsigned long) sclks[i]);
            continue;
        }
        uint64_t dt = 0U;
        (void) spi_dma_transfer(2048U, &dt);
        uint32_t kbs = (dt != 0U) ? (uint32_t) (((uint64_t) 2048U * s_cpu_hz) / dt / 1000ULL) : 0U;
        uint32_t theory = sclks[i] / 8U / 1000U;
        printf("%10lu %8u %10lu %10lu %10lu %6lu\r\n", (unsigned long) sclks[i], 2048U,
               (unsigned long) (dt * 1000000ULL / s_cpu_hz), (unsigned long) kbs,
               (unsigned long) theory,
               (unsigned long) ((theory != 0U) ? (kbs * 100U / theory) : 0U));
    }
    (void) spi_setup(20000000UL);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sdma_info, cmd_info, spi1 dma: config info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sdma_test, cmd_test, spi1 dma: sdma_test [bytes] [sclk] single transfer);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sdma_bench, cmd_bench, spi1 dma: bandwidth table 1..60 MHz);

int main(void)
{
    bsp_init();
    s_src_hz = board_init_spi_clock(T_SPI);
    board_init_spi_pins(T_SPI);
    s_cpu_hz = clock_get_frequency(clock_cpu0);
    (void) spi_setup(20000000UL);

    printf("\r\n=== spi_dma_bw (SPI1 + HDMA, no slave) ===\r\n");
    cmd_info(0, NULL);
    cmd_test(0, NULL);
    cmd_bench(0, NULL);

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
