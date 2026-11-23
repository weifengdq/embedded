/*
 * i2c2_i2c3_master_slave - 同一 MCU 双 I2C 实例主从实测 (I2C2 master <-> I2C3 slave)
 *
 * 接线（用户已用杜邦线接好）:
 *   I2C2_SCL PD02 <-> I2C3_SCL PD07
 *   I2C2_SDA PD03 <-> I2C3_SDA PD06
 *   注意: PD06/PD07 与 tp-timer 的 samples/qei/qei_gpio_sim 复用, 两个例程不能同时初始化.
 *
 * 做法: I2C3 作 slave(地址 0x50) + DMA(HDMA ch6); 地址命中中断里按方向武装 DMA,
 *       I2C2 作 master 调用 i2c_master_write/read 阻塞收发; 双向各自校验数据.
 * 命令: i2c_ms_info / i2c_ms_write [n] / i2c_ms_read [n] / i2c_ms_test [n] [khz] / i2c_ms_bench
 */
#include "bsp.h"
#include "hpm_i2c_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_dmav2_drv.h"
#include "hpm_dmamux_drv.h"
#include "pins/pinmux_core.h"

#define M_I2C       HPM_I2C2
#define S_I2C       HPM_I2C3
#define SLAVE_ADDR  0x50U
#define DMA         HPM_HDMA
#define DMA_CH      6U
#define DMA_MUX_CH  DMA_SOC_CHN_TO_DMAMUX_CHN(DMA, DMA_CH)
#define BUF_SIZE    256U

ATTR_PLACE_AT_NONCACHEABLE uint8_t s_m_tx[BUF_SIZE];   /* master 发送 */
ATTR_PLACE_AT_NONCACHEABLE uint8_t s_m_rx[BUF_SIZE];   /* master 接收 */
ATTR_PLACE_AT_NONCACHEABLE uint8_t s_s_tx[BUF_SIZE];   /* slave 预置发送 */
ATTR_PLACE_AT_NONCACHEABLE uint8_t s_s_rx[BUF_SIZE];   /* slave 接收 */

static uint32_t s_cpu_hz;
static uint32_t s_m_clk_hz;
static uint32_t s_s_clk_hz;
static uint32_t s_khz = 100U;
static uint32_t s_xfer_len = 16U;
static volatile uint32_t s_addr_hit;
static volatile uint32_t s_rx_done;
static volatile uint32_t s_tx_done;
static volatile uint32_t s_isr_cnt;
static volatile uint32_t s_scl_edges;
static volatile uint32_t s_m_isr, s_m_cmpl, s_m_addrhit;   /* master 侧中断计数 */
static volatile uint32_t s_s_cmpl;                         /* slave CMPL 计数 */
static volatile uint32_t s_sda_edges;

static hpm_stat_t slave_rx_dma_arm(uint32_t dst, uint32_t n)
{
    dma_handshake_config_t cfg;
    i2c_dma_disable(S_I2C);
    dma_default_handshake_config(DMA, &cfg);
    cfg.ch_index = DMA_CH;
    cfg.src = (uint32_t) &S_I2C->DATA;
    cfg.src_fixed = true;
    cfg.dst = dst;
    cfg.dst_fixed = false;
    cfg.data_width = DMA_TRANSFER_WIDTH_BYTE;
    cfg.size_in_byte = n;
    dmamux_config(HPM_DMAMUX, DMA_MUX_CH, HPM_DMA_SRC_I2C3, true);
    return dma_setup_handshake(DMA, &cfg, true);
}

static hpm_stat_t slave_tx_dma_arm(uint32_t src, uint32_t n)
{
    dma_handshake_config_t cfg;
    i2c_dma_disable(S_I2C);
    dma_default_handshake_config(DMA, &cfg);
    cfg.ch_index = DMA_CH;
    cfg.src = src;
    cfg.src_fixed = false;
    cfg.dst = (uint32_t) &S_I2C->DATA;
    cfg.dst_fixed = true;
    cfg.data_width = DMA_TRANSFER_WIDTH_BYTE;
    cfg.size_in_byte = n;
    dmamux_config(HPM_DMAMUX, DMA_MUX_CH, HPM_DMA_SRC_I2C3, true);
    return dma_setup_handshake(DMA, &cfg, true);
}

/* 诊断用: GPIO_D 中断统计 SCL/SDA 边沿(I2C3 侧 PD07/PD06) */
SDK_DECLARE_EXT_ISR_M(IRQn_GPIO0_D, isr_gpio_d)
void isr_gpio_d(void)
{
    uint32_t fl = gpio_get_port_interrupt_flags(HPM_GPIO0, GPIO_DO_GPIOD);
    if (fl & (1UL << 7)) { s_scl_edges++; }
    if (fl & (1UL << 6)) { s_sda_edges++; }
    gpio_clear_port_interrupt_flags_with_mask(HPM_GPIO0, GPIO_DO_GPIOD, fl);
}

/* slave 中断: 地址命中时按方向武装 DMA; 完成时置标志 */
SDK_DECLARE_EXT_ISR_M(IRQn_I2C3, i2c_slave_isr)
void i2c_slave_isr(void)
{
    uint32_t st = i2c_get_status(S_I2C);
    s_isr_cnt++;
    if (st & I2C_EVENT_ADDRESS_HIT) {
        s_addr_hit++;
        if (I2C_DIR_SLAVE_READ == i2c_get_direction(S_I2C)) {
            /* master 写 -> slave 收 */
            (void) slave_rx_dma_arm((uint32_t) s_s_rx, s_xfer_len);
            (void) i2c_slave_dma_transfer(S_I2C, s_xfer_len);
        } else {
            /* master 读 -> slave 发 */
            (void) slave_tx_dma_arm((uint32_t) s_s_tx, s_xfer_len);
            (void) i2c_slave_dma_transfer(S_I2C, s_xfer_len);
        }
        i2c_clear_status(S_I2C, I2C_EVENT_ADDRESS_HIT);
    }
    if (st & I2C_EVENT_TRANSACTION_COMPLETE) {
        s_s_cmpl++;
        if (I2C_DIR_SLAVE_READ == i2c_get_direction(S_I2C)) {
            s_rx_done++;
        } else {
            s_tx_done++;
        }
        i2c_clear_status(S_I2C, I2C_EVENT_TRANSACTION_COMPLETE);
    }
}

static void i2c_pins_init(void)
{
    init_i2c2_master_pins();
    init_i2c3_slave_pins();
}

static const uint32_t s_khz_list[3] = { 100U, 400U, 1000U };

static const char *mode_name(uint32_t khz)
{
    return (khz == 100U) ? "100k" : ((khz == 400U) ? "400k" : "1M");
}

static hpm_stat_t i2c_master_setup(uint32_t khz)
{
    i2c_config_t cfg;
    cfg.is_10bit_addressing = false;
    cfg.i2c_mode = (khz <= 100U) ? i2c_mode_normal
                 : ((khz <= 400U) ? i2c_mode_fast : i2c_mode_fast_plus);
    s_khz = khz;
    return i2c_init_master(M_I2C, s_m_clk_hz, &cfg);
}

/* master 写 N 字节 -> slave 收, 校验 */
static int test_write(uint32_t n, uint32_t *errors, uint64_t *cycles)
{
    if (n > BUF_SIZE) { n = BUF_SIZE; }
    for (uint32_t i = 0; i < n; i++) {
        s_m_tx[i] = (uint8_t) (i * 13U + 0x21U);
        s_s_rx[i] = 0U;
    }
    s_xfer_len = n;
    s_rx_done = 0U;
    uint64_t t0 = hpm_csr_get_core_mcycle();
    hpm_stat_t st = i2c_master_write(M_I2C, SLAVE_ADDR, s_m_tx, n);
    uint32_t guard = 20000000U;
    while ((s_rx_done == 0U) && (guard-- != 0U)) {
    }
    *cycles = hpm_csr_get_core_mcycle() - t0;
    uint32_t err = 0U;
    for (uint32_t i = 0; i < n; i++) {
        if (s_s_rx[i] != s_m_tx[i]) { err++; }
    }
    *errors = err;
    if (st != status_success) { return -1; }
    if (err != 0U) { return -2; }
    return 0;
}

/* slave 预置 N 字节 -> master 读, 校验 */
static int test_read(uint32_t n, uint32_t *errors, uint64_t *cycles)
{
    if (n > BUF_SIZE) { n = BUF_SIZE; }
    for (uint32_t i = 0; i < n; i++) {
        s_s_tx[i] = (uint8_t) (i * 29U + 0x80U);
        s_m_rx[i] = 0U;
    }
    s_xfer_len = n;
    s_tx_done = 0U;
    uint64_t t0 = hpm_csr_get_core_mcycle();
    hpm_stat_t st = i2c_master_read(M_I2C, SLAVE_ADDR, s_m_rx, n);
    uint32_t guard = 20000000U;
    while ((s_tx_done == 0U) && (guard-- != 0U)) {
    }
    *cycles = hpm_csr_get_core_mcycle() - t0;
    uint32_t err = 0U;
    for (uint32_t i = 0; i < n; i++) {
        if (s_m_rx[i] != s_s_tx[i]) { err++; }
    }
    *errors = err;
    if (st != status_success) { return -1; }
    if (err != 0U) { return -2; }
    return 0;
}

static void report_row(const char *dir, uint32_t n, int rc, uint32_t err, uint64_t cycles)
{
    uint32_t ns = (uint32_t) (cycles * 1000000000ULL / s_cpu_hz);
    uint32_t kbps = (uint32_t) (((uint64_t) n * 8ULL * s_cpu_hz) / (cycles ? cycles : 1U) / 1000ULL);
    printf("%-6s %-4s n=%-3lu %s errors=%-2lu time=%lu.%02lu us bw=%lu kbps\r\n",
           mode_name(s_khz), dir, (unsigned long) n,
           (rc == 0 && err == 0U) ? "PASS" : "FAIL", (unsigned long) err,
           (unsigned long) (ns / 1000U), (unsigned long) ((ns % 1000U) / 10U),
           (unsigned long) kbps);
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("i2c2_i2c3_master_slave @ hpm5e31_1\r\n");
    printf("  I2C2 master: SCL=PD02 SDA=PD03, I2C3 slave: SCL=PD07 SDA=PD06 addr=0x%02lX\r\n",
           (unsigned long) SLAVE_ADDR);
    printf("  clk: i2c2=%lu Hz i2c3=%lu Hz cpu=%lu Hz, mode=%s\r\n",
           (unsigned long) s_m_clk_hz, (unsigned long) s_s_clk_hz,
           (unsigned long) s_cpu_hz, mode_name(s_khz));
    printf("  isr=%lu addr_hit=%lu rx_done=%lu tx_done=%lu, dma=HDMA ch%u\r\n",
           (unsigned long) s_isr_cnt, (unsigned long) s_addr_hit,
           (unsigned long) s_rx_done, (unsigned long) s_tx_done, DMA_CH);
    printf("  usage: i2c_ms_write [n] | i2c_ms_read [n] | i2c_ms_test [n] [khz] | i2c_ms_bench\r\n");
}

/* 引脚接线探测: PD02/PD03(I2C2) 与 PD06/PD07(I2C3) 切成 GPIO, 驱动-读回判断连接 */
static void slave_reinit(uint32_t addr)
{
    i2c_config_t c;
    c.is_10bit_addressing = false;
    c.i2c_mode = i2c_mode_normal;
    (void) i2c_init_slave(S_I2C, s_s_clk_hz, &c, (uint16_t) addr);
    i2c_enable_irq(S_I2C, I2C_EVENT_ADDRESS_HIT | I2C_EVENT_TRANSACTION_COMPLETE);
}

/* 探测从机地址约定: 分别用 0x50(7bit) / 0xA0(8bit) / 0x28 配 slave, 每次扫 0x08..0x77 */
/* SCL/SDA 边沿诊断: 把 I2C3 侧两脚切成 GPIO 输入+上拉, 中断计数, 再让 master 发一帧 */
static void cmd_scl_probe(int argc, char *argv[])
{
    (void) argc; (void) argv;
    HPM_IOC->PAD[IOC_PAD_PD06].FUNC_CTL = IOC_PD06_FUNC_CTL_GPIO_D_06;
    HPM_IOC->PAD[IOC_PAD_PD07].FUNC_CTL = IOC_PD07_FUNC_CTL_GPIO_D_07;
    HPM_IOC->PAD[IOC_PAD_PD06].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    HPM_IOC->PAD[IOC_PAD_PD07].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOD, 6U);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOD, 7U);
    gpio_config_pin_interrupt(HPM_GPIO0, GPIO_DO_GPIOD, 6U, gpio_interrupt_trigger_edge_both);
    gpio_config_pin_interrupt(HPM_GPIO0, GPIO_DO_GPIOD, 7U, gpio_interrupt_trigger_edge_both);
    gpio_enable_pin_interrupt(HPM_GPIO0, GPIO_DO_GPIOD, 6U);
    gpio_enable_pin_interrupt(HPM_GPIO0, GPIO_DO_GPIOD, 7U);
    intc_m_enable_irq_with_priority(IRQn_GPIO0_D, 1);
    s_scl_edges = 0U; s_sda_edges = 0U;
    uint8_t b = 0x00U;
    (void) i2c_master_write(M_I2C, SLAVE_ADDR, &b, 1U);
    gpio_disable_pin_interrupt(HPM_GPIO0, GPIO_DO_GPIOD, 6U);
    gpio_disable_pin_interrupt(HPM_GPIO0, GPIO_DO_GPIOD, 7U);
    printf("edge probe: SCL edges=%lu SDA edges=%lu (master 一帧 1 字节, 期望 SCL≈18+)\r\n",
           (unsigned long) s_scl_edges, (unsigned long) s_sda_edges);
    i2c_pins_init();
    slave_reinit(0x50U);
    (void) i2c_master_setup(s_khz);
}

static void cmd_probe_addr(int argc, char *argv[])
{
    (void) argc; (void) argv;
    static const uint32_t cand[3] = { 0x50U, 0xA0U, 0x28U };
    printf("slave regs: SETUP=0x%08lx ADDR=0x%08lx, master SETUP=0x%08lx\r\n",
           (unsigned long) S_I2C->SETUP, (unsigned long) S_I2C->ADDR, (unsigned long) M_I2C->SETUP);
    for (uint32_t k = 0U; k < 3U; k++) {
        slave_reinit(cand[k]);
        printf("slave ADDR set 0x%02lX -> scan ACKs:", (unsigned long) cand[k]);
        uint32_t found = 0U;
        for (uint32_t a = 0x08U; a <= 0x77U; a++) {
            uint8_t dummy = 0U;
            if (i2c_master_write(M_I2C, (uint16_t) a, &dummy, 0U) == status_success) {
                printf(" 0x%02lX", (unsigned long) a);
                found++;
            }
        }
        printf(" (found=%lu)\r\n", (unsigned long) found);
    }
    slave_reinit(0x50U);
}

static void cmd_wires(int argc, char *argv[])
{
    (void) argc; (void) argv;
    const uint32_t pads[4] = { IOC_PAD_PD02, IOC_PAD_PD03, IOC_PAD_PD06, IOC_PAD_PD07 };
    for (uint32_t i = 0U; i < 4U; i++) {
        HPM_IOC->PAD[pads[i]].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    }
    HPM_IOC->PAD[IOC_PAD_PD02].FUNC_CTL = IOC_PD02_FUNC_CTL_GPIO_D_02;
    HPM_IOC->PAD[IOC_PAD_PD03].FUNC_CTL = IOC_PD03_FUNC_CTL_GPIO_D_03;
    HPM_IOC->PAD[IOC_PAD_PD06].FUNC_CTL = IOC_PD06_FUNC_CTL_GPIO_D_06;
    HPM_IOC->PAD[IOC_PAD_PD07].FUNC_CTL = IOC_PD07_FUNC_CTL_GPIO_D_07;
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOD, 2U);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOD, 3U);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOD, 6U);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOD, 7U);
    for (volatile uint32_t d = 0U; d < 10000U; d++) { }
    printf("i2c wire probe idle: PD02=%d PD03=%d PD06=%d PD07=%d (上拉=1)\r\n",
           gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 2U), gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 3U),
           gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 6U), gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 7U));
    gpio_set_pin_output(HPM_GPIO0, GPIO_DO_GPIOD, 2U);
    for (uint32_t v = 0U; v < 2U; v++) {
        gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOD, 2U, v);
        for (volatile uint32_t d = 0U; d < 10000U; d++) { }
        printf("  drive PD02=%lu -> PD07=%d PD06=%d PD03=%d\r\n", (unsigned long) v,
               gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 7U),
               gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 6U),
               gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 3U));
    }
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOD, 2U);
    HPM_IOC->PAD[IOC_PAD_PD02].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    gpio_set_pin_output(HPM_GPIO0, GPIO_DO_GPIOD, 3U);
    for (uint32_t v = 0U; v < 2U; v++) {
        gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOD, 3U, v);
        for (volatile uint32_t d = 0U; d < 10000U; d++) { }
        printf("  drive PD03=%lu -> PD06=%d PD07=%d PD02=%d\r\n", (unsigned long) v,
               gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 6U),
               gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 7U),
               gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 2U));
    }
    i2c_pins_init();
    printf("i2c wires: 期望 SCL(PD02)<->SCL(PD07), SDA(PD03)<->SDA(PD06)\r\n");
}

static void cmd_write(int argc, char *argv[])
{
    uint32_t n = 16U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &n); }
    if (n == 0U) { n = 16U; }
    uint32_t err = 0U;
    uint64_t dt = 0U;
    int rc = test_write(n, &err, &dt);
    report_row("wr", n, rc, err, dt);
}

static void cmd_read(int argc, char *argv[])
{
    uint32_t n = 16U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &n); }
    if (n == 0U) { n = 16U; }
    uint32_t err = 0U;
    uint64_t dt = 0U;
    int rc = test_read(n, &err, &dt);
    report_row("rd", n, rc, err, dt);
}

static void cmd_test(int argc, char *argv[])
{
    uint32_t n = 16U, khz = s_khz;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &n); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &khz); }
    if (n == 0U) { n = 16U; }
    if (i2c_master_setup(khz) != status_success) {
        printf("master setup failed for %lu kHz\r\n", (unsigned long) khz);
        return;
    }
    uint32_t err = 0U;
    uint64_t dt = 0U;
    int rc = test_write(n, &err, &dt);
    report_row("wr", n, rc, err, dt);
    rc = test_read(n, &err, &dt);
    report_row("rd", n, rc, err, dt);
}

static void cmd_bench(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("I2C2<->I2C3 master/slave bench (24 B write+read per mode):\r\n");
    for (uint32_t i = 0; i < 3U; i++) {
        if (i2c_master_setup(s_khz_list[i]) != status_success) {
            printf("%s setup failed\r\n", mode_name(s_khz_list[i]));
            continue;
        }
        uint32_t err = 0U;
        uint64_t dt = 0U;
        int rc = test_write(24U, &err, &dt);
        report_row("wr", 24U, rc, err, dt);
        rc = test_read(24U, &err, &dt);
        report_row("rd", 24U, rc, err, dt);
    }
    (void) i2c_master_setup(100U);
}
/* ---------------- R5 新增: 纯地址事务 / 连续误码 / 开漏 DI 语义 ---------------- */

SDK_DECLARE_EXT_ISR_M(IRQn_I2C2, i2c_master_isr)
void i2c_master_isr(void)
{
    uint32_t st = i2c_get_status(M_I2C);
    s_m_isr++;
    if (st & I2C_EVENT_ADDRESS_HIT) { s_m_addrhit++; }
    if (st & I2C_EVENT_TRANSACTION_COMPLETE) { s_m_cmpl++; }
    i2c_clear_status(M_I2C, st);
}

/* 纯地址事务: master 只发 START+ADDR(+R/W)+STOP, 无 DATA 相.
 * 观察: slave 是否进 ADDRESS_HIT / CMPL 中断, master 自己是否产生 CMPL. */
static void cmd_addr_only(int argc, char *argv[])
{
    (void) argc; (void) argv;
    i2c_enable_irq(M_I2C, I2C_EVENT_ADDRESS_HIT | I2C_EVENT_TRANSACTION_COMPLETE);
    intc_m_enable_irq_with_priority(IRQn_I2C2, 2);
    uint32_t a0 = s_addr_hit, c0 = s_s_cmpl, m0 = s_m_cmpl, n0 = s_m_addrhit, i0 = s_isr_cnt;
    printf("addr-only (n=0):\r\n");
    hpm_stat_t st = i2c_master_write(M_I2C, SLAVE_ADDR, NULL, 0U);
    for (volatile uint32_t d = 0U; d < 200000U; d++) { }
    printf("  master i2c_master_write(n=0) -> %s\r\n", (st == status_success) ? "success" : "FAIL");
    printf("  slave : addr_hit+%lu cmpl+%lu isr+%lu\r\n",
           (unsigned long) (s_addr_hit - a0), (unsigned long) (s_s_cmpl - c0),
           (unsigned long) (s_isr_cnt - i0));
    printf("  master: addr_hit+%lu cmpl+%lu isr+%lu\r\n",
           (unsigned long) (s_m_addrhit - n0), (unsigned long) (s_m_cmpl - m0),
           (unsigned long) (s_m_isr - 0U) - 0U);
    printf("  master regs: STATUS=0x%08lx SETUP=0x%08lx INTREN=0x%08lx\r\n",
           (unsigned long) M_I2C->STATUS, (unsigned long) M_I2C->SETUP,
           (unsigned long) M_I2C->INTEN);

    /* 对照: 带 1 字节 DATA 相 */
    uint8_t b = 0x5AU;
    a0 = s_addr_hit; c0 = s_s_cmpl; m0 = s_m_cmpl; n0 = s_m_addrhit; i0 = s_isr_cnt;
    st = i2c_master_write(M_I2C, SLAVE_ADDR, &b, 1U);
    for (volatile uint32_t d = 0U; d < 200000U; d++) { }
    printf("addr-only (n=1 对照): -> %s\r\n", (st == status_success) ? "success" : "FAIL");
    printf("  slave : addr_hit+%lu cmpl+%lu isr+%lu\r\n",
           (unsigned long) (s_addr_hit - a0), (unsigned long) (s_s_cmpl - c0),
           (unsigned long) (s_isr_cnt - i0));
    printf("  master: addr_hit+%lu cmpl+%lu\r\n",
           (unsigned long) (s_m_addrhit - n0), (unsigned long) (s_m_cmpl - m0));
    i2c_disable_irq(M_I2C, I2C_EVENT_ADDRESS_HIT | I2C_EVENT_TRANSACTION_COMPLETE);
}

/* 连续误码率: n 次 24 B 写 + 24 B 读, 统计失败次数与错误字节数 */
static void cmd_ber(int argc, char *argv[])
{
    uint32_t cnt = 1000U, khz = 1000U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &cnt); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &khz); }
    if (cnt == 0U) { cnt = 1000U; }
    if (cnt > 20000U) { cnt = 20000U; }
    if (i2c_master_setup(khz) != status_success) {
        printf("master setup failed for %lu kHz\r\n", (unsigned long) khz);
        return;
    }
    uint32_t fail = 0U, err_bytes = 0U, err_xfer = 0U;
    uint64_t total = 0ULL;
    uint64_t t0 = hpm_csr_get_core_mcycle();
    for (uint32_t k = 0U; k < cnt; k++) {
        uint32_t e = 0U;
        uint64_t dt = 0U;
        int rc = test_write(24U, &e, &dt);
        if (rc != 0) { fail++; }
        if (e != 0U) { err_xfer++; }
        err_bytes += e;
        rc = test_read(24U, &e, &dt);
        if (rc != 0) { fail++; }
        if (e != 0U) { err_xfer++; }
        err_bytes += e;
        total += 48ULL;
    }
    uint64_t dt = hpm_csr_get_core_mcycle() - t0;
    uint64_t ms100 = dt * 100000ULL / s_cpu_hz;
    printf("ber: %lu 次 x (24 B 写 + 24 B 读) @ %lu kHz\r\n",
           (unsigned long) cnt, (unsigned long) khz);
    printf("  total=%lu B fail=%lu err_xfer=%lu err_bytes=%lu, %lu.%02lu ms, %lu B/s\r\n",
           (unsigned long) total, (unsigned long) fail, (unsigned long) err_xfer,
           (unsigned long) err_bytes, (unsigned long) (ms100 / 100ULL),
           (unsigned long) (ms100 % 100ULL),
           (unsigned long) (total * s_cpu_hz / (dt ? dt : 1U)));
    printf("  => %s\r\n", (fail == 0U && err_bytes == 0U) ? "PASS (0 错误)" : "FAIL");
    (void) i2c_master_setup(100U);
}

/* 开漏输出时 DI 读到的是什么: 借 I2C2<->I2C3 的杜邦线, 一端驱动、一端读 */
static void cmd_od_probe(int argc, char *argv[])
{
    (void) argc; (void) argv;
    const uint32_t pads[4] = { IOC_PAD_PD02, IOC_PAD_PD03, IOC_PAD_PD06, IOC_PAD_PD07 };
    for (uint32_t i = 0U; i < 4U; i++) {
        HPM_IOC->PAD[pads[i]].FUNC_CTL = (i == 0U) ? IOC_PD02_FUNC_CTL_GPIO_D_02 :
                                       (i == 1U) ? IOC_PD03_FUNC_CTL_GPIO_D_03 :
                                       (i == 2U) ? IOC_PD06_FUNC_CTL_GPIO_D_06 :
                                                   IOC_PD07_FUNC_CTL_GPIO_D_07;
        HPM_IOC->PAD[pads[i]].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    }
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOD, 6U);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOD, 7U);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOD, 2U);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOD, 3U);

    /* 用例 A: PD02 推挽输出 0 -> 对端 PD07 读 */
    gpio_set_pin_output(HPM_GPIO0, GPIO_DO_GPIOD, 2U);
    gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOD, 2U, 0U);
    for (volatile uint32_t d = 0U; d < 20000U; d++) { }
    printf("A pushpull PD02=0: PD07=%d PD02_di=%d\r\n",
           gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 7U),
           gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 2U));

    /* 用例 B: PD02 开漏输出 0 (OD=1) -> 对端 PD07 读, 同时看自己 DI */
    HPM_IOC->PAD[IOC_PAD_PD02].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1) |
                                         IOC_PAD_PAD_CTL_OD_SET(1);
    gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOD, 2U, 0U);
    for (volatile uint32_t d = 0U; d < 20000U; d++) { }
    printf("B opendrain PD02=0: PD07=%d PD02_di=%d\r\n",
           gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 7U),
           gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 2U));

    /* 用例 C: PD02 开漏释放 -> 对端 PD07 读 (应被上拉拉高) */
    gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOD, 2U, 1U);
    for (volatile uint32_t d = 0U; d < 20000U; d++) { }
    printf("C opendrain PD02=1: PD07=%d PD02_di=%d\r\n",
           gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 7U),
           gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 2U));

    /* 用例 D: 反向 —— PD07 开漏输出 0, 读 PD02 */
    HPM_IOC->PAD[IOC_PAD_PD07].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1) |
                                         IOC_PAD_PAD_CTL_OD_SET(1);
    gpio_set_pin_output(HPM_GPIO0, GPIO_DO_GPIOD, 7U);
    gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOD, 7U, 0U);
    for (volatile uint32_t d = 0U; d < 20000U; d++) { }
    printf("D opendrain PD07=0: PD02=%d PD07_di=%d\r\n",
           gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 2U),
           gpio_read_pin(HPM_GPIO0, GPIO_DO_GPIOD, 7U));
    i2c_pins_init();
    slave_reinit(0x50U);
    (void) i2c_master_setup(s_khz);
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_ms_addr_only, cmd_addr_only, i2c master/slave: pure-address transaction counters);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_ms_ber, cmd_ber, i2c master/slave: i2c_ms_ber [count] [khz] BER);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_ms_od_probe, cmd_od_probe, i2c master/slave: open-drain DI semantics probe);

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_ms_info, cmd_info, i2c master/slave: wiring/config info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_ms_write, cmd_write, i2c master/slave: master write N bytes -> slave verify);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_ms_read, cmd_read, i2c master/slave: slave preload -> master read verify);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_ms_test, cmd_test, i2c master/slave: i2c_ms_test [n] [khz]);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_ms_bench, cmd_bench, i2c master/slave: 100k/400k/1M bench);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_ms_wires, cmd_wires, i2c master/slave: probe user wiring with GPIO);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_ms_probe_addr, cmd_probe_addr, i2c master/slave: probe slave address convention);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_ms_scl_probe, cmd_scl_probe, i2c master/slave: count SCL/SDA edges via GPIO irq);

int main(void)
{
    bsp_init();
    s_cpu_hz = clock_get_frequency(clock_cpu0);
    i2c_pins_init();
    s_m_clk_hz = board_init_i2c_clock(M_I2C);
    s_s_clk_hz = board_init_i2c_clock(S_I2C);
    i2c_config_t scfg;
    scfg.is_10bit_addressing = false;
    scfg.i2c_mode = i2c_mode_normal;
    if (i2c_init_slave(S_I2C, s_s_clk_hz, &scfg, SLAVE_ADDR) != status_success) {
        printf("i2c_init_slave failed\r\n");
    }
    i2c_enable_irq(S_I2C, I2C_EVENT_ADDRESS_HIT | I2C_EVENT_TRANSACTION_COMPLETE);
    intc_m_enable_irq_with_priority(IRQn_I2C3, 2);
    if (i2c_master_setup(100U) != status_success) {
        printf("i2c_init_master failed\r\n");
    }

    printf("\r\n=== i2c2_i2c3_master_slave (I2C2 master <-> I2C3 slave, user-wired) ===\r\n");
    cmd_info(0, NULL);
    uint32_t err = 0U;
    uint64_t dt = 0U;
    int rc = test_write(16U, &err, &dt);
    report_row("wr", 16U, rc, err, dt);
    rc = test_read(16U, &err, &dt);
    report_row("rd", 16U, rc, err, dt);
    cmd_bench(0, NULL);
    printf("type i2c_ms_test 32 400 / i2c_ms_bench\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
