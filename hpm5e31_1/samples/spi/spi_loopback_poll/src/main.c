/*
 * spi_loopback_poll - SPI1(PC10~PC13) 回环尝试 + SCLK 频率/模式/位宽实测
 *
 * 硬件: hpm5e31_1, SPI1 SCLK=PC10 MISO=PC12 MOSI=PC13 CS=PC11
 * 说明: HPM 的 IOC 只有"本 PAD 输出回送本 PAD 输入"(LOOP_BACK), 不能把 MOSI 送到 MISO,
 *       所以真正数据回环需要外部把 PC12(MISO) 与 PC13(MOSI) 短接(杜邦线/排针)。
 *       没有回环线时, 本例程仍能自证 SPI 控制器在正确工作:
 *         1) 用一次 N 字节传输的耗时反推实际 SCLK = N*8/t, 与配置值比对;
 *         2) 扫 CPOL/CPHA 四种模式、8/16/32 位宽, 确认时序配置生效;
 *         3) 扫描 1~80 MHz 求分频可得频率与误差(实测 SCK 上限).
 * 命令: spi_info / spi_loop / spi_sclk [N] / spi_sweep / spi_modes / spi_bits
 */
#include "bsp.h"
#include "hpm_spi_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_iomux.h"

#define T_SPI        HPM_SPI1
#define T_SPI_CLK    clock_spi1
#define T_SPI_PAD_SCLK IOC_PAD_PC10
#define T_SPI_PAD_MISO IOC_PAD_PC12
#define T_SPI_PAD_MOSI IOC_PAD_PC13
#define SPI_BUF_SIZE 4096U

ATTR_PLACE_AT_NONCACHEABLE uint8_t s_wbuf[SPI_BUF_SIZE];
ATTR_PLACE_AT_NONCACHEABLE uint8_t s_rbuf[SPI_BUF_SIZE];

static uint32_t s_src_hz;
static uint32_t s_cpu_hz;
static uint32_t s_sclk = 20000000UL;
static uint32_t s_cpol = 1U;   /* 1 = high idle */
static uint32_t s_cpha = 1U;   /* 1 = even edges */
static uint32_t s_bits = 8U;

static inline uint64_t cyc(void)
{
    return hpm_csr_get_core_mcycle();
}

static hpm_stat_t spi_setup(uint32_t sclk, uint32_t cpol, uint32_t cpha, uint32_t bits)
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
    format.common_config.data_len_in_bits = (uint8_t) bits;
    format.common_config.data_merge = false;
    format.common_config.mosi_bidir = false;
    format.common_config.lsb = false;
    format.common_config.mode = spi_master_mode;
    format.common_config.cpol = cpol ? spi_sclk_high_idle : spi_sclk_low_idle;
    format.common_config.cpha = cpha ? spi_sclk_sampling_even_clk_edges
                                     : spi_sclk_sampling_odd_clk_edges;
    spi_format_init(T_SPI, &format);
    s_sclk = sclk; s_cpol = cpol; s_cpha = cpha; s_bits = bits;
    return status_success;
}

/* 一次全双工传输 nbytes, 返回耗时周期; 顺带给出实测 SCLK */
static uint64_t spi_xfer(uint32_t nbytes)
{
    spi_control_config_t ctrl = {0};
    if (nbytes > SPI_BUF_SIZE) { nbytes = SPI_BUF_SIZE; }
    spi_master_get_default_control_config(&ctrl);
    ctrl.master_config.cmd_enable = false;
    ctrl.master_config.addr_enable = false;
    ctrl.common_config.trans_mode = spi_trans_write_read_together;
    ctrl.common_config.data_phase_fmt = spi_single_io_mode;

    uint64_t t0 = cyc();
    (void) spi_transfer(T_SPI, &ctrl, NULL, NULL, s_wbuf, nbytes, s_rbuf, nbytes);
    return cyc() - t0;
}

static uint32_t meas_sclk(uint32_t nbytes)
{
    uint64_t dt = spi_xfer(nbytes);
    if (dt == 0U) { return 0U; }
    /* 全双工: nbytes*8 个 SCLK 周期 */
    return (uint32_t) (((uint64_t) nbytes * 8ULL * s_cpu_hz) / dt);
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("spi_loopback_poll @ hpm5e31_1: SPI1 SCLK=PC10 MISO=PC12 MOSI=PC13 CS=PC11\r\n");
    printf("  src_clk=%lu Hz, cpu=%lu Hz, cfg: sclk=%lu cpol=%lu cpha=%lu bits=%lu\r\n",
           (unsigned long) s_src_hz, (unsigned long) s_cpu_hz,
           (unsigned long) s_sclk, (unsigned long) s_cpol,
           (unsigned long) s_cpha, (unsigned long) s_bits);
    printf("  note: 真正 MOSI->MISO 数据回环需要外部短接 PC12-PC13\r\n");
    printf("  usage: spi_info / spi_loop / spi_sclk [N] / spi_sweep / spi_modes / spi_bits\r\n");
}

/* 1) 回环尝试: 打开 MOSI PAD 的 LOOP_BACK, 看 RX 是否等于 TX */
static void cmd_loop(int argc, char *argv[])
{
    (void) argc; (void) argv;
    spi_setup(5000000UL, 1U, 1U, 8U);
    for (uint32_t i = 0; i < 8U; i++) { s_wbuf[i] = (uint8_t) (0xA0U + i); s_rbuf[i] = 0U; }

    /* 先不打开回送: 读到的应是 MISO 引脚的静态电平 */
    spi_xfer(8U);
    printf("no-loopback rx: %02X %02X %02X %02X %02X %02X %02X %02X (MOSI 数据 %02X..%02X)\r\n",
           s_rbuf[0], s_rbuf[1], s_rbuf[2], s_rbuf[3], s_rbuf[4], s_rbuf[5], s_rbuf[6], s_rbuf[7],
           s_wbuf[0], s_wbuf[7]);

    /* 打开 MOSI PAD 的 LOOP_BACK(本 PAD 输出回送本 PAD 输入), 再试一次 */
    HPM_IOC->PAD[T_SPI_PAD_MOSI].FUNC_CTL = IOC_PC13_FUNC_CTL_SPI1_MOSI
        | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
    for (uint32_t i = 0; i < 8U; i++) { s_rbuf[i] = 0U; }
    spi_xfer(8U);
    printf("mosi-loopback rx: %02X %02X %02X %02X %02X %02X %02X %02X -> %s\r\n",
           s_rbuf[0], s_rbuf[1], s_rbuf[2], s_rbuf[3], s_rbuf[4], s_rbuf[5], s_rbuf[6], s_rbuf[7],
           (s_rbuf[0] == s_wbuf[0]) ? "MATCH" : "MISMATCH(需外部短接 MISO-MOSI)");
    /* 还原 */
    HPM_IOC->PAD[T_SPI_PAD_MOSI].FUNC_CTL = IOC_PC13_FUNC_CTL_SPI1_MOSI;
}

/* 2) 单点: 实测当前配置下的 SCLK */
static void cmd_sclk(int argc, char *argv[])
{
    uint32_t n = 1024U, cfg = s_sclk;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &n); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &cfg); }
    if (n > SPI_BUF_SIZE) { n = SPI_BUF_SIZE; }
    if (spi_setup(cfg, s_cpol, s_cpha, s_bits) != status_success) {
        printf("timing init failed for %lu\r\n", (unsigned long) cfg);
        return;
    }
    uint64_t dt = spi_xfer(n);
    uint32_t meas = (dt != 0U) ? (uint32_t) (((uint64_t) n * 8ULL * s_cpu_hz) / dt) : 0U;
    int32_t err = (int32_t) (((int64_t) meas - (int64_t) cfg) * 1000000LL / (int64_t) cfg);
    printf("spi_sclk cfg=%lu measured=%lu err=%ld ppm, n=%lu time=%lu.%02lu us\r\n",
           (unsigned long) cfg, (unsigned long) meas, (long) err, (unsigned long) n,
           (unsigned long) (dt * 1000000ULL / s_cpu_hz),
           (unsigned long) ((dt * 100000000ULL / s_cpu_hz) % 100ULL));
}

/* 3) 频率扫描: 1~80 MHz, 看分频后的实际 SCLK */
static void cmd_sweep(int argc, char *argv[])
{
    (void) argc; (void) argv;
    static const uint32_t freqs[] = { 100000, 1000000, 5000000, 10000000, 20000000,
                                      25000000, 40000000, 50000000, 60000000, 80000000 };
    printf("%10s %12s %10s %8s\r\n", "request", "measured", "ratio", "err_ppm");
    for (uint32_t i = 0; i < sizeof(freqs) / sizeof(freqs[0]); i++) {
        if (spi_setup(freqs[i], 1U, 1U, 8U) != status_success) {
            printf("%10lu   timing init failed\r\n", (unsigned long) freqs[i]);
            continue;
        }
        uint32_t meas = meas_sclk(1024U);
        int32_t err = (int32_t) (((int64_t) meas - (int64_t) freqs[i]) * 1000000LL / (int64_t) freqs[i]);
        printf("%10lu %12lu %9lu.%02lu %8ld\r\n", (unsigned long) freqs[i],
               (unsigned long) meas,
               (unsigned long) (meas / freqs[i]),
               (unsigned long) (((uint64_t) meas * 100ULL / freqs[i]) % 100ULL),
               (long) err);
    }
    (void) spi_setup(20000000UL, 1U, 1U, 8U);
}

/* 4) 四种 CPOL/CPHA 模式: 传输耗时应当一致 */
static void cmd_modes(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("mode table: SCLK=20MHz, 1024 bytes full-duplex\r\n");
    printf("%4s %4s %5s %10s %12s\r\n", "mode", "CPOL", "CPHA", "time_us", "measured_SCLK");
    for (uint32_t m = 0U; m < 4U; m++) {
        uint32_t cpol = (m & 2U) ? 1U : 0U;
        uint32_t cpha = (m & 1U) ? 1U : 0U;
        if (spi_setup(20000000UL, cpol, cpha, 8U) != status_success) {
            printf("%4lu  timing init failed\r\n", (unsigned long) m);
            continue;
        }
        uint64_t dt = spi_xfer(1024U);
        uint32_t meas = (dt != 0U) ? (uint32_t) (((uint64_t) 1024U * 8ULL * s_cpu_hz) / dt) : 0U;
        printf("%4lu %4lu %5lu %10lu %12lu\r\n", (unsigned long) m, (unsigned long) cpol,
               (unsigned long) cpha, (unsigned long) (dt * 1000000ULL / s_cpu_hz),
               (unsigned long) meas);
    }
    (void) spi_setup(20000000UL, 1U, 1U, 8U);
}

/* 5) 位宽: 8/16/32 bit 每次传输的字数不同, 但总比特数一致 */
static void cmd_bits(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("data width table: SCLK=20MHz, 2048 bytes total\r\n");
    printf("%8s %10s %10s %12s\r\n", "bits", "words", "time_us", "measured_SCLK");
    for (uint32_t b = 0U; b < 3U; b++) {
        uint32_t bits = (b == 0U) ? 8U : ((b == 1U) ? 16U : 32U);
        uint32_t bytes = bits / 8U;
        if (spi_setup(20000000UL, 1U, 1U, bits) != status_success) {
            printf("%8lu  format init failed\r\n", (unsigned long) bits);
            continue;
        }
        uint64_t dt = spi_xfer(2048U / bytes);
        uint32_t meas = (dt != 0U) ? (uint32_t) (((uint64_t) 2048U * 8ULL * s_cpu_hz) / dt) : 0U;
        printf("%8lu %10lu %10lu %12lu\r\n", (unsigned long) bits,
               (unsigned long) (2048U / bytes),
               (unsigned long) (dt * 1000000ULL / s_cpu_hz), (unsigned long) meas);
    }
    (void) spi_setup(20000000UL, 1U, 1U, 8U);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_info, cmd_info, spi1: pin/clock/config info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_loop, cmd_loop, spi1: MOSI/MISO loopback attempt);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_sclk, cmd_sclk, spi1: spi_sclk [N] [cfg_hz] measured SCLK);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_sweep, cmd_sweep, spi1: 100k..80M SCLK sweep table);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_modes, cmd_modes, spi1: CPOL/CPHA 4 modes table);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_bits, cmd_bits, spi1: 8/16/32-bit width table);

int main(void)
{
    bsp_init();
    s_src_hz = board_init_spi_clock(T_SPI);
    board_init_spi_pins(T_SPI);
    s_cpu_hz = clock_get_frequency(clock_cpu0);
    (void) spi_setup(20000000UL, 1U, 1U, 8U);

    printf("\r\n=== spi_loopback_poll (SPI1, no external slave) ===\r\n");
    printf("spi1 src_clk=%lu Hz, cpu=%lu Hz\r\n",
           (unsigned long) s_src_hz, (unsigned long) s_cpu_hz);
    cmd_info(0, NULL);
    cmd_loop(0, NULL);
    cmd_sclk(0, NULL);
    cmd_modes(0, NULL);
    cmd_bits(0, NULL);
    cmd_sweep(0, NULL);
    printf("type 'spi_sclk 1024 20000000' to re-measure\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
