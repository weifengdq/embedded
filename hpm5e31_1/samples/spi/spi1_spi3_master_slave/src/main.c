/*
 * spi1_spi3_master_slave - 同一 MCU 双 SPI 实例主从实测 (SPI1 master <-> SPI3 slave)
 *
 * 接线（用户已用杜邦线接好）:
 *   SPI1_nCS  PC11  <-> SPI3_nCS  PA27
 *   SPI1_SCLK PC10  <-> SPI3_SCLK PA26
 *   SPI1_MISO PC12  <-> SPI3_MISO PA28   (同名相连)
 *   SPI1_MOSI PC13  <-> SPI3_MOSI PA29   (同名相连)
 *
 * 做法: SPI3 作为 slave + DMA(HDMA ch4/5) 自主收发, SPI1 作为 master 用阻塞 spi_transfer 发时钟;
 *       一次传输同时完成双向数据交换, 然后校验: master_rx==slave_tx 且 slave_rx==master_tx.
 * 命令: spi_ms_info / spi_ms_test [bytes] [hz] / spi_ms_bench / spi_ms_read [bytes]
 */
#include "bsp.h"
#include "hpm_spi_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_iomux.h"
#include "hpm_dmav2_drv.h"
#include "hpm_dmamux_drv.h"
#include "pins/pinmux_core.h"

#define M_SPI       HPM_SPI1
#define S_SPI       HPM_SPI3
#define DMA         HPM_HDMA
#define DMAMUX      HPM_DMAMUX
#define TX_CH       4U
#define RX_CH       5U
#define TX_MUX_CH   DMA_SOC_CHN_TO_DMAMUX_CHN(DMA, TX_CH)
#define RX_MUX_CH   DMA_SOC_CHN_TO_DMAMUX_CHN(DMA, RX_CH)
#define BUF_SIZE    1024U

ATTR_PLACE_AT_NONCACHEABLE uint8_t s_m_tx[BUF_SIZE];
ATTR_PLACE_AT_NONCACHEABLE uint8_t s_m_rx[BUF_SIZE];
ATTR_PLACE_AT_NONCACHEABLE uint8_t s_s_tx[BUF_SIZE];
ATTR_PLACE_AT_NONCACHEABLE uint8_t s_s_rx[BUF_SIZE];

static uint32_t s_cpu_hz;
static uint32_t s_m_clk_hz;
static uint32_t s_s_clk_hz;
static uint32_t s_sclk = 1000000UL;
static hpm_stat_t s_m_timing_st;
static uint32_t s_m_div = 0xFFFFFFFFUL;
static volatile uint32_t s_cs_edges;
static spi_control_config_t s_c_master;
static spi_control_config_t s_c_slave;

static inline uint64_t cyc(void)
{
    return hpm_csr_get_core_mcycle();
}

static void delay_cycles(uint32_t n)
{
    uint64_t t0 = hpm_csr_get_core_mcycle();
    while ((hpm_csr_get_core_mcycle() - t0) < (uint64_t) n) {
    }
}

static hpm_stat_t dma_tx_trigger(uint32_t src, uint32_t size)
{
    dma_handshake_config_t cfg;
    dma_default_handshake_config(DMA, &cfg);
    cfg.ch_index = TX_CH;
    cfg.src = src;
    cfg.src_fixed = false;
    cfg.dst = (uint32_t) &S_SPI->DATA;
    cfg.dst_fixed = true;
    cfg.data_width = DMA_TRANSFER_WIDTH_BYTE;
    cfg.size_in_byte = size;
    dmamux_config(DMAMUX, TX_MUX_CH, HPM_DMA_SRC_SPI3_TX, true);
    return dma_setup_handshake(DMA, &cfg, true);
}

static hpm_stat_t dma_rx_trigger(uint32_t dst, uint32_t size)
{
    dma_handshake_config_t cfg;
    dma_default_handshake_config(DMA, &cfg);
    cfg.ch_index = RX_CH;
    cfg.src = (uint32_t) &S_SPI->DATA;
    cfg.src_fixed = true;
    cfg.dst = dst;
    cfg.dst_fixed = false;
    cfg.data_width = DMA_TRANSFER_WIDTH_BYTE;
    cfg.size_in_byte = size;
    dmamux_config(DMAMUX, RX_MUX_CH, HPM_DMA_SRC_SPI3_RX, true);
    return dma_setup_handshake(DMA, &cfg, true);
}

static bool dma_wait_tc(uint8_t ch, uint64_t max_cycles)
{
    uint64_t t0 = cyc();
    while (cyc() - t0 < max_cycles) {
        uint32_t st = dma_check_transfer_status(DMA, ch);
        if (st & (DMA_CHANNEL_STATUS_ERROR | DMA_CHANNEL_STATUS_ABORT)) { return false; }
        if (st & DMA_CHANNEL_STATUS_TC) { return true; }
    }
    return false;
}

static void spi_pins_init(void)
{
    init_spi1_master_pins();
    init_spi3_slave_pins();
}

static void spi_format_init_common(void)
{
    spi_timing_config_t timing = {0};
    spi_format_config_t format = {0};
    spi_control_config_t ctrl = {0};

    /* master timing: 1 MHz 起步 */
    spi_master_get_default_timing_config(&timing);
    timing.master_config.clk_src_freq_in_hz = s_m_clk_hz;
    timing.master_config.sclk_freq_in_hz = s_sclk;
    s_m_timing_st = spi_master_timing_init(M_SPI, &timing);
    s_m_div = SPI_TIMING_SCLK_DIV_GET(M_SPI->TIMING);
    if (s_m_timing_st != status_success) {
        printf("master timing init failed\r\n");
    }

    /* master format: mode 0 (CPOL=0, CPHA=0), 8 bit */
    spi_master_get_default_format_config(&format);
    format.common_config.data_len_in_bits = 8U;
    format.common_config.mode = spi_master_mode;
    format.common_config.cpol = spi_sclk_low_idle;
    format.common_config.cpha = spi_sclk_sampling_odd_clk_edges;
    spi_format_init(M_SPI, &format);

    spi_master_get_default_control_config(&ctrl);
    ctrl.master_config.cmd_enable = false;
    ctrl.master_config.addr_enable = false;
    ctrl.common_config.tx_dma_enable = false;
    ctrl.common_config.rx_dma_enable = false;
    ctrl.common_config.trans_mode = spi_trans_write_read_together;
    ctrl.common_config.data_phase_fmt = spi_single_io_mode;
    s_c_master = ctrl;

    /* slave format: 与 master 完全一致的 mode/位宽 */
    format.common_config.data_len_in_bits = 8U;
    format.common_config.data_merge = false;
    format.common_config.mosi_bidir = false;
    format.common_config.lsb = false;
    format.common_config.mode = spi_slave_mode;
    format.common_config.cpol = spi_sclk_low_idle;
    format.common_config.cpha = spi_sclk_sampling_odd_clk_edges;
    spi_format_init(S_SPI, &format);

    spi_slave_get_default_control_config(&ctrl);
    ctrl.slave_config.slave_data_only = true;
    ctrl.common_config.tx_dma_enable = true;
    ctrl.common_config.rx_dma_enable = true;
    ctrl.common_config.trans_mode = spi_trans_write_read_together;
    ctrl.common_config.data_phase_fmt = spi_single_io_mode;
    s_c_slave = ctrl;
}

/* 一次双向传输: master 发 A 收 B, slave 发 B 收 A */
static int one_transfer(uint32_t n, uint32_t *errors, uint64_t *cycles)
{
    if (n > BUF_SIZE) { n = BUF_SIZE; }
    /* slave TX DMA 多发 1 个字节(尾部 spare): 实测若只发 n 字节, 从机 TX FIFO 在首个时钟到来前会 underrun,
     * 导致数据整体错位 1 字节; 多发 1 字节即可稳定. */
    for (uint32_t i = 0; i < n; i++) {
        s_m_tx[i] = (uint8_t) (i * 7U + 0x31U);
        s_s_tx[i] = (uint8_t) (i * 11U + 0xA5U);
        s_m_rx[i] = 0U;
        s_s_rx[i] = 0U;
    }
    s_s_tx[n] = 0x5AU;

    /* 1) 先武装 slave 侧 DMA */
    (void) dma_check_transfer_status(DMA, TX_CH);
    (void) dma_check_transfer_status(DMA, RX_CH);
    if (dma_rx_trigger((uint32_t) s_s_rx, n) != status_success) { return -1; }
    if (dma_tx_trigger((uint32_t) s_s_tx, n + 1U) != status_success) { return -2; }
    if (spi_setup_dma_transfer(S_SPI, &s_c_slave, NULL, NULL, n, n) != status_success) { return -3; }

    /* 2) master 发时钟并同时收数据(阻塞) */
    uint64_t t0 = cyc();
    hpm_stat_t st = spi_transfer(M_SPI, &s_c_master, NULL, NULL, s_m_tx, n, s_m_rx, n);
    bool tx_ok = dma_wait_tc(TX_CH, 100000000ULL);
    bool rx_ok = dma_wait_tc(RX_CH, 100000000ULL);
    (void) spi_wait_for_idle_status(S_SPI);
    *cycles = cyc() - t0;
    if (st != status_success) { return -4; }
    if (!tx_ok || !rx_ok) { return -5; }

    uint32_t err = 0U;
    for (uint32_t i = 0; i < n; i++) {
        if (s_m_rx[i] != s_s_tx[i]) { err++; }       /* master 收到 slave 数据 */
        if (s_s_rx[i] != s_m_tx[i]) { err++; }        /* slave 收到 master 数据 */
    }
    *errors = err;
    if (err != 0U) {
        printf("  dbg m_tx=" );
        for (uint32_t k = 0U; k < 8U && k < n; k++) { printf(" %02X", s_m_tx[k]); }
        printf(" | m_rx=" );
        for (uint32_t k = 0U; k < 8U && k < n; k++) { printf(" %02X", s_m_rx[k]); }
        printf(" | s_tx=" );
        for (uint32_t k = 0U; k < 8U && k < n; k++) { printf(" %02X", s_s_tx[k]); }
        printf(" | s_rx=" );
        for (uint32_t k = 0U; k < 8U && k < n; k++) { printf(" %02X", s_s_rx[k]); }
        printf("\r\n  dbg SPI1.STATUS=0x%08lx SPI3.STATUS=0x%08lx SPI3.CTRL=0x%08lx\r\n",
               (unsigned long) M_SPI->STATUS, (unsigned long) S_SPI->STATUS,
               (unsigned long) S_SPI->CTRL);
    }
    return (err == 0U) ? 0 : -6;
}

/* slave 预装数据, master 只读(发送 0x00 填充) */
static int read_from_slave(uint32_t n, uint32_t *errors, uint64_t *cycles)
{
    if (n > BUF_SIZE) { n = BUF_SIZE; }
    for (uint32_t i = 0; i < n; i++) {
        s_m_tx[i] = 0x00U;
        s_s_tx[i] = (uint8_t) (0x5AU ^ (i & 0xFFU));
        s_m_rx[i] = 0U;
        s_s_rx[i] = 0U;
    }
    s_s_tx[n] = 0x5AU;
    (void) dma_check_transfer_status(DMA, TX_CH);
    (void) dma_check_transfer_status(DMA, RX_CH);
    if (dma_rx_trigger((uint32_t) s_s_rx, n) != status_success) { return -1; }
    if (dma_tx_trigger((uint32_t) s_s_tx, n + 1U) != status_success) { return -2; }
    if (spi_setup_dma_transfer(S_SPI, &s_c_slave, NULL, NULL, n, n) != status_success) { return -3; }
    uint64_t t0 = cyc();
    (void) spi_transfer(M_SPI, &s_c_master, NULL, NULL, s_m_tx, n, s_m_rx, n);
    bool ok = dma_wait_tc(RX_CH, 100000000ULL) && dma_wait_tc(TX_CH, 100000000ULL);
    *cycles = cyc() - t0;
    uint32_t err = 0U;
    for (uint32_t i = 0; i < n; i++) {
        if (s_m_rx[i] != s_s_tx[i]) { err++; }
    }
    *errors = err;
    return (ok && err == 0U) ? 0 : -4;
}

static void report_row(const char *name, uint32_t n, int rc, uint32_t err, uint64_t cycles)
{
    uint32_t ns = (uint32_t) (cycles * 1000000000ULL / s_cpu_hz);
    uint32_t kbs = (uint32_t) (((uint64_t) n * s_cpu_hz) / (cycles ? cycles : 1U) / 1000ULL);
    printf("%-12s n=%-4lu %s errors=%-3lu time=%lu.%02lu us bw=%lu KB/s\r\n",
           name, (unsigned long) n, (rc == 0 && err == 0U) ? "PASS" : "FAIL",
           (unsigned long) err, (unsigned long) (ns / 1000U),
           (unsigned long) ((ns % 1000U) / 10U), (unsigned long) kbs);
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("spi1_spi3_master_slave @ hpm5e31_1\r\n");
    printf("  SPI1 master: SCLK PC10 CS PC11 MISO PC12 MOSI PC13 (mode 0, %lu Hz)\r\n",
           (unsigned long) s_sclk);
    printf("  SPI3 slave : SCLK PA26 CS PA27 MISO PA28 MOSI PA29, DMA HDMA ch%u/%u\r\n",
           TX_CH, RX_CH);
    printf("  src_clk: spi1=%lu Hz spi3=%lu Hz cpu=%lu Hz\r\n",
           (unsigned long) s_m_clk_hz, (unsigned long) s_s_clk_hz, (unsigned long) s_cpu_hz);
    printf("  usage: spi_ms_test [bytes] [hz] | spi_ms_bench | spi_ms_read [bytes]\r\n");
    printf("  NOTE: 若 spi_ms_wires 显示 PC13<->PA28 / PC12<->PA29，请互换 PA28/PA29 两根数据线(SCLK/CS 不动)\r\n");
}

/* 硬件接线探测: 把 4 根数据线临时切成 GPIO, 用"驱动-读回"判断谁连谁 */
SDK_DECLARE_EXT_ISR_M(IRQn_GPIO0_A, isr_gpio_a)
void isr_gpio_a(void)
{
    uint32_t fl = gpio_get_port_interrupt_flags(HPM_GPIO0, GPIO_DO_GPIOA);
    if (fl & (1UL << 27)) { s_cs_edges++; }
    gpio_clear_port_interrupt_flags_with_mask(HPM_GPIO0, GPIO_DO_GPIOA, fl);
}

/* 用 GPIO 中断统计 master 的 CS 是否真的在翻转(PA27 临时当 GPIO 输入) */
/* 隔离测试 master 的 MISO 输入通路: PA28 当 GPIO 输出固定电平, master 发时钟读回 */
static void miso_probe_case(const char *tag)
{
    uint8_t tx[8] = { 0 };
    uint8_t rx[8] = { 0 };
    (void) spi_transfer(M_SPI, &s_c_master, NULL, NULL, tx, 8U, rx, 8U);
    printf("  %-22s m_rx=%02X %02X %02X %02X\r\n", tag, rx[0], rx[1], rx[2], rx[3]);
}

static void cmd_miso_probe(int argc, char *argv[])
{
    (void) argc; (void) argv;
    /* A: PC12=SPI1_MISO + 内部上拉, PA28 高阻 -> 若输入通路正常应读到 FF */
    init_spi1_master_pins();
    HPM_IOC->PAD[IOC_PAD_PC12].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    HPM_IOC->PAD[IOC_PAD_PA28].FUNC_CTL = IOC_PA28_FUNC_CTL_GPIO_A_28;
    HPM_IOC->PAD[IOC_PAD_PA28].PAD_CTL = 0;
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOA, 28U);
    delay_cycles(4000U);
    miso_probe_case("PC12 pullup/PA28 HiZ");
    /* B/C: PA28 推挽驱动 1 / 0 */
    gpio_set_pin_output(HPM_GPIO0, GPIO_DO_GPIOA, 28U);
    gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOA, 28U, 1U);
    delay_cycles(4000U);
    miso_probe_case("PC12 MISO/PA28=1");
    gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOA, 28U, 0U);
    delay_cycles(4000U);
    miso_probe_case("PC12 MISO/PA28=0");
    spi_pins_init();
}

static void cmd_cs_probe(int argc, char *argv[])
{
    (void) argc; (void) argv;
    HPM_IOC->PAD[IOC_PAD_PA27].FUNC_CTL = IOC_PA27_FUNC_CTL_GPIO_A_27;
    HPM_IOC->PAD[IOC_PAD_PA27].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOA, 27U);
    gpio_config_pin_interrupt(HPM_GPIO0, GPIO_DO_GPIOA, 27U, gpio_interrupt_trigger_edge_both);
    gpio_enable_pin_interrupt(HPM_GPIO0, GPIO_DO_GPIOA, 27U);
    intc_m_enable_irq_with_priority(IRQn_GPIO0_A, 1);
    s_cs_edges = 0U;
    uint8_t tx[4] = { 0x11U, 0x22U, 0x33U, 0x44U };
    uint8_t rx[4] = { 0 };
    (void) spi_transfer(M_SPI, &s_c_master, NULL, NULL, tx, 4U, rx, 4U);
    gpio_disable_pin_interrupt(HPM_GPIO0, GPIO_DO_GPIOA, 27U);
    printf("CS probe: PA27 edges=%lu (一次传输期望 >=2 即 选中+释放)\r\n",
           (unsigned long) s_cs_edges);
    spi_pins_init();
}

static void cmd_wires(int argc, char *argv[])
{
    (void) argc; (void) argv;
    /* GPIO 输入 + 上拉 */
    HPM_IOC->PAD[IOC_PAD_PC12].FUNC_CTL = IOC_PC12_FUNC_CTL_GPIO_C_12;
    HPM_IOC->PAD[IOC_PAD_PC13].FUNC_CTL = IOC_PC13_FUNC_CTL_GPIO_C_13;
    HPM_IOC->PAD[IOC_PAD_PA28].FUNC_CTL = IOC_PA28_FUNC_CTL_GPIO_A_28;
    HPM_IOC->PAD[IOC_PAD_PA29].FUNC_CTL = IOC_PA29_FUNC_CTL_GPIO_A_29;
    HPM_IOC->PAD[IOC_PAD_PC12].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    HPM_IOC->PAD[IOC_PAD_PC13].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    HPM_IOC->PAD[IOC_PAD_PA28].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    HPM_IOC->PAD[IOC_PAD_PA29].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOC, 12U);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOC, 13U);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOA, 28U);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOA, 29U);
    delay_cycles(2000U);
    printf("wire probe: idle PC12=%d PC13=%d PA28=%d PA29=%d (上拉应为 1)\r\n",
           gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOC, 12U), gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOC, 13U),
           gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOA, 28U), gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOA, 29U));
    gpio_set_pin_output(HPM_GPIO0, GPIO_DO_GPIOC, 13U);
    for (uint32_t v = 0U; v < 2U; v++) {
        gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOC, 13U, v);
        delay_cycles(4000U);
        printf("  drive PC13=%lu -> PA28=%d PA29=%d PC12=%d\r\n", (unsigned long) v,
               gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOA, 28U),
               gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOA, 29U),
               gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOC, 12U));
    }
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOC, 13U);
    HPM_IOC->PAD[IOC_PAD_PC13].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    gpio_set_pin_output(HPM_GPIO0, GPIO_DO_GPIOC, 12U);
    for (uint32_t v = 0U; v < 2U; v++) {
        gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOC, 12U, v);
        delay_cycles(4000U);
        printf("  drive PC12=%lu -> PA28=%d PA29=%d PC13=%d\r\n", (unsigned long) v,
               gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOA, 28U),
               gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOA, 29U),
               gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOC, 13U));
    }
    /* 反向测试: 从机侧(PA28/PA29/PA26/PA27)驱动, 主机侧(PC12/PC13/PC10/PC11)读 */
    HPM_IOC->PAD[IOC_PAD_PC10].FUNC_CTL = IOC_PC10_FUNC_CTL_GPIO_C_10;
    HPM_IOC->PAD[IOC_PAD_PC11].FUNC_CTL = IOC_PC11_FUNC_CTL_GPIO_C_11;
    HPM_IOC->PAD[IOC_PAD_PC12].FUNC_CTL = IOC_PC12_FUNC_CTL_GPIO_C_12;
    HPM_IOC->PAD[IOC_PAD_PC13].FUNC_CTL = IOC_PC13_FUNC_CTL_GPIO_C_13;
    HPM_IOC->PAD[IOC_PAD_PC10].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    HPM_IOC->PAD[IOC_PAD_PC11].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    HPM_IOC->PAD[IOC_PAD_PC12].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    HPM_IOC->PAD[IOC_PAD_PC13].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOC, 10U);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOC, 11U);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOC, 12U);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOC, 13U);
    {
        const uint32_t sp[4] = { IOC_PAD_PA28, IOC_PAD_PA29, IOC_PAD_PA26, IOC_PAD_PA27 };
        const uint32_t af[4] = { IOC_PA28_FUNC_CTL_GPIO_A_28, IOC_PA29_FUNC_CTL_GPIO_A_29, IOC_PA26_FUNC_CTL_GPIO_A_26, IOC_PA27_FUNC_CTL_GPIO_A_27 };
        const uint32_t sp_pin[4] = { 28U, 29U, 26U, 27U };
        const uint32_t mp_pin[4] = { 12U, 13U, 10U, 11U };
        const char *nm[4] = { "PA28->PC12", "PA29->PC13", "PA26->PC10", "PA27->PC11" };
        for (uint32_t i = 0U; i < 4U; i++) {
            HPM_IOC->PAD[sp[i]].FUNC_CTL = af[i];
            HPM_IOC->PAD[sp[i]].PAD_CTL = IOC_PAD_PAD_CTL_SR_SET(1) | IOC_PAD_PAD_CTL_SPD_SET(3);
            gpio_set_pin_output(HPM_GPIO0, GPIO_DO_GPIOA, (uint8_t) sp_pin[i]);
            uint32_t lo, hi;
            gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOA, (uint8_t) sp_pin[i], 0U);
            delay_cycles(4000U);
            lo = gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOC, (uint8_t) mp_pin[i]);
            gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOA, (uint8_t) sp_pin[i], 1U);
            delay_cycles(4000U);
            hi = gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOC, (uint8_t) mp_pin[i]);
            printf("  drive %s: 0->%lu 1->%lu %s\r\n", nm[i],
                   (unsigned long) lo, (unsigned long) hi,
                   ((lo == 0U) && (hi == 1U)) ? "OK" : "FAIL");
        }
    }
    /* 还原 SPI 引脚 */
    spi_pins_init();
    printf("wires: 请据此确认接线; 例程默认按 用户表格: PC12<-PA29 / PC13->PA28\r\n");
}

/* 分块轮询: slave 先把 TX FIFO 预装好(不用 TX DMA), master 再发时钟, 收完读 slave RX FIFO */
static int poll_xfer(uint32_t n, uint32_t *err, uint64_t *cycles)
{
    if (n > BUF_SIZE) { n = BUF_SIZE; }
    for (uint32_t i = 0; i < n; i++) {
        s_m_tx[i] = (uint8_t) (i * 7U + 0x31U);
        s_s_tx[i] = (uint8_t) (i * 11U + 0xA5U);
        s_m_rx[i] = 0U; s_s_rx[i] = 0U;
    }
    uint64_t t0 = cyc();
    uint32_t off = 0U;
    while (off < n) {
        uint32_t k = ((n - off) > 8U) ? 8U : (n - off);
        if (spi_control_init(S_SPI, &s_c_slave, k, k) != status_success) { return -1; }
        for (uint32_t i = 0; i < k; i++) {
            S_SPI->DATA = s_s_tx[off + i];
        }
        if (spi_transfer(M_SPI, &s_c_master, NULL, NULL, &s_m_tx[off], k, &s_m_rx[off], k) != status_success) { return -2; }
        for (uint32_t i = 0; i < k; i++) {
            uint32_t guard = 1000000U;
            while ((S_SPI->STATUS & SPI_STATUS_RXEMPTY_MASK) && (guard-- != 0U)) { }
            s_s_rx[off + i] = (uint8_t) (S_SPI->DATA & 0xFFU);
        }
        off += k;
    }
    *cycles = cyc() - t0;
    uint32_t e = 0U;
    for (uint32_t i = 0; i < n; i++) {
        if (s_m_rx[i] != s_s_tx[i]) { e++; }
        if (s_s_rx[i] != s_m_tx[i]) { e++; }
    }
    *err = e;
    return (e == 0U) ? 0 : -3;
}


/* ---------------- R5 新增: 频率扫描 / 连续误码 / 时序余量 / 驱动强度 ---------------- */

/* SCLK = src / ((DIV+1)*2); DIV=0xFF 是"SCLK=源时钟"的特殊值 (hpm_spi_regs.h:943) */
static uint32_t sclk_from_div(uint32_t src, uint32_t div)
{
    if (div == 0xFFUL) { return src; }
    return src / ((div + 1UL) * 2UL);
}

/* 只重配 format/control, 不动 TIMING (供 cs2sclk/csht 扫描用) */
static void master_format_apply(void)
{
    spi_format_config_t f = {0};
    spi_master_get_default_format_config(&f);
    f.common_config.data_len_in_bits = 8U;
    f.common_config.mode = spi_master_mode;
    f.common_config.cpol = spi_sclk_low_idle;
    f.common_config.cpha = spi_sclk_sampling_odd_clk_edges;
    spi_format_init(M_SPI, &f);

    spi_control_config_t c = {0};
    spi_master_get_default_control_config(&c);
    c.master_config.cmd_enable = false;
    c.master_config.addr_enable = false;
    c.common_config.trans_mode = spi_trans_write_read_together;
    c.common_config.data_phase_fmt = spi_single_io_mode;
    s_c_master = c;
}

static void cmd_sweep(int argc, char *argv[])
{
    (void) argc; (void) argv;
    static const uint32_t hz_list[] = {
        1000000UL, 2000000UL, 4000000UL, 5000000UL, 8000000UL, 10000000UL,
        16000000UL, 20000000UL, 23000000UL, 25000000UL, 32000000UL,
        40000000UL, 50000000UL, 60000000UL, 64000000UL, 80000000UL
    };
    printf("sweep: src_clk=%lu Hz, 每点 512 B 全双工 (master 轮询 + slave DMA)\r\n",
           (unsigned long) s_m_clk_hz);
    printf("  %-10s %-7s %-5s %-11s %-12s %-7s %-5s\r\n",
           "request", "timing", "DIV", "SCLK(公式)", "实测Mbit/s", "比值‰", "误码");
    for (uint32_t i = 0U; i < (uint32_t) (sizeof(hz_list) / sizeof(hz_list[0])); i++) {
        s_sclk = hz_list[i];
        spi_format_init_common();
        uint32_t exp = sclk_from_div(s_m_clk_hz, s_m_div);
        uint32_t err = 0U;
        uint64_t dt = 0U;
        int rc = one_transfer(512U, &err, &dt);
        uint64_t mbps1000 = (dt != 0U) ? ((uint64_t) 512U * 8ULL * s_cpu_hz / dt / 1000ULL) : 0ULL;
        uint32_t ratio = (exp != 0U) ? (uint32_t) (mbps1000 * 1000ULL / ((uint64_t) exp / 1000ULL)) : 0U;
        printf("  %-10lu %-7s 0x%02lX  %-11lu %-12lu %-7lu %-5lu%s\r\n",
               (unsigned long) hz_list[i], (s_m_timing_st == status_success) ? "OK" : "FAIL",
               (unsigned long) s_m_div, (unsigned long) exp,
               (unsigned long) (mbps1000 / 1000ULL), (unsigned long) ratio,
               (unsigned long) err, (rc == 0) ? "" : " rc!");
    }
    s_sclk = 1000000UL;
    spi_format_init_common();
}

static void cmd_ber(int argc, char *argv[])
{
    uint32_t mb = 1U, hz = 10000000UL;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &mb); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &hz); }
    if (mb == 0U) { mb = 1U; }
    if (mb > 8U) { mb = 8U; }
    s_sclk = hz;
    spi_format_init_common();
    uint64_t total = (uint64_t) mb * 1024ULL * 1024ULL;
    uint64_t done = 0ULL, err_bytes = 0ULL, xfers = 0ULL;
    uint32_t rc_err = 0U;
    uint64_t t0 = cyc();
    while (done < total) {
        uint32_t n = 1024U;
        if ((total - done) < (uint64_t) n) { n = (uint32_t) (total - done); }
        uint32_t e = 0U;
        uint64_t dt = 0U;
        if (one_transfer(n, &e, &dt) != 0 && e == 0U) { rc_err++; }
        err_bytes += e;
        done += n;
        xfers++;
    }
    uint64_t dt = cyc() - t0;
    uint64_t ms100 = dt * 100000ULL / s_cpu_hz;
    uint64_t kbs = (dt != 0U) ? (done * s_cpu_hz / dt / 1000ULL) : 0ULL;
    uint64_t bits = done * 16ULL;
    printf("ber: %lu MB @ %lu Hz: xfers=%lu err_bytes=%lu rc_err=%lu, %lu.%02lu ms, %lu KB/s\r\n",
           (unsigned long) mb, (unsigned long) hz, (unsigned long) xfers,
           (unsigned long) err_bytes, (unsigned long) rc_err,
           (unsigned long) (ms100 / 100ULL), (unsigned long) (ms100 % 100ULL),
           (unsigned long) kbs);
    if (err_bytes != 0ULL) {
        printf("  BER = %lu/%lu bit\r\n", (unsigned long) err_bytes, (unsigned long) bits);
    } else {
        printf("  BER: 0 错误 / %lu bit (双向)\r\n", (unsigned long) bits);
    }
    s_sclk = 1000000UL;
    spi_format_init_common();
}

static void cmd_guard(int argc, char *argv[])
{
    uint32_t hz = 20000000UL;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &hz); }
    if (hz < 100000U) { hz = 20000000UL; }
    static const uint8_t cs2_list[] = { 1U, 2U, 4U, 8U, 16U, 32U, 64U };
    static const uint8_t csht_list[] = { 0U, 1U, 2U, 4U };
    printf("guard sweep @ %lu Hz (每点 32 x 64 B 双向):\r\n", (unsigned long) hz);
    printf("  %-8s %-5s %-10s %-10s %s\r\n", "cs2sclk", "csht", "fail_xfer", "err_bytes", "结果");
    for (uint32_t a = 0U; a < sizeof(cs2_list); a++) {
        for (uint32_t b = 0U; b < sizeof(csht_list); b++) {
            spi_timing_config_t t = {0};
            spi_master_get_default_timing_config(&t);
            t.master_config.clk_src_freq_in_hz = s_m_clk_hz;
            t.master_config.sclk_freq_in_hz = hz;
            t.master_config.cs2sclk = cs2_list[a];
            t.master_config.csht = csht_list[b];
            s_sclk = hz;
            s_m_timing_st = spi_master_timing_init(M_SPI, &t);
            s_m_div = SPI_TIMING_SCLK_DIV_GET(M_SPI->TIMING);
            master_format_apply();
            uint32_t fails = 0U, errs = 0U;
            for (uint32_t k = 0U; k < 32U; k++) {
                uint32_t e = 0U;
                uint64_t dt = 0U;
                if (one_transfer(64U, &e, &dt) != 0) { fails++; }
                errs += e;
            }
            printf("  %-8u %-5u %-10lu %-10lu %s\r\n", cs2_list[a], csht_list[b],
                   (unsigned long) fails, (unsigned long) errs,
                   (fails == 0U && errs == 0U) ? "PASS" : "FAIL");
        }
    }
    s_sclk = 1000000UL;
    spi_format_init_common();
}

static void cmd_spd(int argc, char *argv[])
{
    uint32_t spd = 2U, hz = 20000000UL;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &spd); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &hz); }
    if (spd > 3U) { spd = 3U; }
    static const uint32_t pads[6] = { IOC_PAD_PC10, IOC_PAD_PC11, IOC_PAD_PC12,
                                      IOC_PAD_PC13, IOC_PAD_PA26, IOC_PAD_PA28 };
    for (uint32_t i = 0U; i < 6U; i++) {
        uint32_t v = HPM_IOC->PAD[pads[i]].PAD_CTL;
        v = (v & ~IOC_PAD_PAD_CTL_SPD_MASK) | IOC_PAD_PAD_CTL_SPD_SET(spd);
        HPM_IOC->PAD[pads[i]].PAD_CTL = v;
    }
    s_sclk = hz;
    spi_format_init_common();
    uint32_t fails = 0U, errs = 0U;
    for (uint32_t k = 0U; k < 200U; k++) {
        uint32_t e = 0U;
        uint64_t dt = 0U;
        if (one_transfer(64U, &e, &dt) != 0) { fails++; }
        errs += e;
    }
    printf("spd=%lu @ %lu Hz: 200 x 64 B -> fail=%lu err_bytes=%lu\r\n",
           (unsigned long) spd, (unsigned long) hz, (unsigned long) fails, (unsigned long) errs);
    spi_pins_init();
    s_sclk = 1000000UL;
    spi_format_init_common();
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_ms_sweep, cmd_sweep, spi master/slave: freq sweep + TIMING readback);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_ms_ber, cmd_ber, spi master/slave: spi_ms_ber [MB] [hz] continuous BER);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_ms_guard, cmd_guard, spi master/slave: cs2sclk/csht guard sweep);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_ms_spd, cmd_spd, spi master/slave: pad drive strength sweep);

static void cmd_poll(int argc, char *argv[])
{
    uint32_t n = 8U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &n); }
    if (n == 0U) { n = 8U; }
    uint32_t err = 0U; uint64_t dt = 0U;
    int rc = poll_xfer(n, &err, &dt);
    report_row("poll", n, rc, err, dt);
    printf("  m_rx[0..3]=%02X %02X %02X %02X s_tx[0..3]=%02X %02X %02X %02X\r\n",
           s_m_rx[0], s_m_rx[1], s_m_rx[2], s_m_rx[3],
           s_s_tx[0], s_s_tx[1], s_s_tx[2], s_s_tx[3]);
}

static void cmd_dbg(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("  M_SPI TRANSFMT=0x%08lx CTRL=0x%08lx TRANSCTRL=0x%08lx STATUS=0x%08lx\r\n",
           (unsigned long) M_SPI->TRANSFMT, (unsigned long) M_SPI->CTRL,
           (unsigned long) M_SPI->TRANSCTRL, (unsigned long) M_SPI->STATUS);
    printf("  S_SPI TRANSFMT=0x%08lx CTRL=0x%08lx TRANSCTRL=0x%08lx STATUS=0x%08lx\r\n",
           (unsigned long) S_SPI->TRANSFMT, (unsigned long) S_SPI->CTRL,
           (unsigned long) S_SPI->TRANSCTRL, (unsigned long) S_SPI->STATUS);
    printf("  PA28.FUNC_CTL=0x%08lx PAD_CTL=0x%08lx  PA27.FUNC_CTL=0x%08lx\r\n",
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PA28].FUNC_CTL, (unsigned long) HPM_IOC->PAD[IOC_PAD_PA28].PAD_CTL,
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PA27].FUNC_CTL);
}

static void cmd_test(int argc, char *argv[])
{
    uint32_t n = 64U, hz = s_sclk;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &n); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &hz); }
    if (n == 0U) { n = 64U; }
    if (hz < 100000U) { hz = 100000U; }
    s_sclk = hz;
    spi_format_init_common();
    uint32_t err = 0U;
    uint64_t dt = 0U;
    int rc = one_transfer(n, &err, &dt);
    report_row("bidir", n, rc, err, dt);
}

static void cmd_bench(int argc, char *argv[])
{
    (void) argc; (void) argv;
    static const uint32_t hz_list[4] = { 1000000U, 5000000U, 10000000U, 20000000U };
    printf("spi master/slave bidirectional bench (n=128, mode0):\r\n");
    for (uint32_t i = 0; i < 4U; i++) {
        s_sclk = hz_list[i];
        spi_format_init_common();
        uint32_t err = 0U;
        uint64_t dt = 0U;
        int rc = one_transfer(128U, &err, &dt);
        char nm[20];
        (void) nm;
        printf("  hz=%-9lu ", (unsigned long) hz_list[i]);
        report_row("bench", 128U, rc, err, dt);
    }
    s_sclk = 1000000U;
    spi_format_init_common();
}

static void cmd_read(int argc, char *argv[])
{
    uint32_t n = 32U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &n); }
    if (n == 0U) { n = 32U; }
    uint32_t err = 0U;
    uint64_t dt = 0U;
    int rc = read_from_slave(n, &err, &dt);
    report_row("slave->mst", n, rc, err, dt);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_ms_info, cmd_info, spi master/slave: wiring and config info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_ms_test, cmd_test, spi master/slave: spi_ms_test [bytes] [hz]);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_ms_bench, cmd_bench, spi master/slave: 1/5/10/20MHz bench);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_ms_read, cmd_read, spi master/slave: slave preload -> master read);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_ms_wires, cmd_wires, spi master/slave: probe user wiring with GPIO);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_ms_poll, cmd_poll, spi master/slave: chunked polling test);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_ms_dbg, cmd_dbg, spi master/slave: dump registers);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_ms_cs_probe, cmd_cs_probe, spi master/slave: count CS edges via GPIO irq);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi_ms_miso_probe, cmd_miso_probe, spi master/slave: isolate master MISO path);

int main(void)
{
    bsp_init();
    s_cpu_hz = clock_get_frequency(clock_cpu0);
    spi_pins_init();
    s_m_clk_hz = board_init_spi_clock(M_SPI);
    s_s_clk_hz = board_init_spi_clock(S_SPI);
    spi_format_init_common();

    printf("\r\n=== spi1_spi3_master_slave (SPI1 master <-> SPI3 slave, user-wired) ===\r\n");
    cmd_info(0, NULL);
    uint32_t err = 0U;
    uint64_t dt = 0U;
    int rc = one_transfer(64U, &err, &dt);
    report_row("bidir64", 64U, rc, err, dt);
    cmd_bench(0, NULL);
    printf("type spi_ms_test 128 20000000 / spi_ms_read 32\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
