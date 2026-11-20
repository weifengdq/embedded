/**
 * @file  bsp.c
 * @brief HPM5361 + T30HM1TS2500 板级初始化与 S2500 HAL
 *
 * 接线 (实测):
 *   CS_N = PA26 (SPI1, GPIO 软件片选)
 *   SCLK = PA27
 *   MISO = PA28
 *   MOSI = PA29
 *   IRQ_N= PA12 (GPIO 输入, 低有效)
 *   RST_N= PA10 (GPIO 输出, 低有效)
 *
 * 调试串口:
 *   - 板载 FT2232H channel B (/dev/ttyUSB1) 实测接 UART0 PA00(TX)/PA01(RX)
 *   - 同时把输出镜像到 UART2 PB08(TX)/PB09(RX), 便于按用户接线表外接串口
 *   - 两路都是 115200 8N1
 *
 * SPI1:
 *   - SCLK = 25 MHz (S2500 上限)。SPI1 时钟源 600 MHz (PLL0 clk1),
 *     SCLK_DIV=11 → 600/((11+1)*2) = 25.000 MHz 整数分频, 无取整误差。
 *     hpm_spi_set_sclk_frequency() 会在 >20MHz 时自动选源并复核, 见 bsp_spi_set_hz()。
 *   - 68 字节数据块事务走 HDMA (DMAMUX SPI1_TX/RX), 12 字节控制事务也走 DMA,
 *     但 CPU 仍负责搬移 68B 缓冲与 footer 校验; 相比阻塞式 FIFO 收发每事务
 *     CPU 周期下降 ~70% (实测见 README)。
 *   - DMA 缓冲必须放 AHB SRAM (0xF0400000, 系统内存), 核内 DLM 不是 DMA 目标。
 *
 * 注意: 这里不使用 SDK 的 board_init()/printf 控制台, 因为 SPI 时钟配置会
 *       改动共享时钟源, 导致 SDK console 的 uart_flush 卡死。串口在 SPI
 *       时钟配置完成后再初始化, 输出全部走 bsp_uart_write()。
 */
#include "bsp.h"
#include "board.h"                 /* HPM SDK board.h (clock/pmp/SPI pin helpers) */
#include "hpm_spi.h"
#include "hpm_gpio_drv.h"
#include "hpm_iomux.h"
#include "hpm_clock_drv.h"
#include "hpm_csr_drv.h"
#include "hpm_ppor_drv.h"
#include "hpm_uart_drv.h"
#include "hpm_mchtmr_drv.h"
#include "hpm_dmav2_drv.h"
#include "hpm_dmamux_drv.h"
#include "hpm_dmamux_src.h"
#include "s2500_app.h"
#include "s2500_regs.h"

#define BSP_RST_PAD   IOC_PAD_PA10
#define BSP_IRQ_PAD   IOC_PAD_PA12
#define BSP_CS_PAD    IOC_PAD_PA26

#define BSP_SPI_CLOCK_HZ  (25000000UL)
#define BSP_BAUDRATE      (115200UL)

/* DMA 通道分配 (HDMA 0/1, DMAMUX 0/1) */
#define BSP_DMA_CH_TX       (0U)
#define BSP_DMA_CH_RX       (1U)
#define BSP_DMAMUX_CH_TX    (0U)
#define BSP_DMAMUX_CH_RX    (1U)

/* CS 建立/保持时间 (ns)。S2500 只要求 SCLK 边缘对齐, 200ns 足够且远小于原来的 1us */
#define BSP_CS_DELAY_NS     (200U)

static uint32_t s_ms_div = 24000UL;     /* MCHTMR 每毫秒计数 */
static uint32_t s_cpu_hz = 480000000UL;
static bsp_spi_stats_t s_spi;
static int      s_dma_on;
static int      s_dma_ok;

/* DMA 缓冲: 必须位于系统内存 (AHB SRAM), 核内 DLM 不能作为 HDMA 源/目的 */
static uint8_t s_dma_tx[S2500_DATA_XFER_LEN + 8] __attribute__((section(".ahb_sram"), aligned(8)));
static uint8_t s_dma_rx[S2500_DATA_XFER_LEN + 8] __attribute__((section(".ahb_sram"), aligned(8)));

/* ------------------------------------------------------------------ */
/* 片选 / 复位 / 中断                                                  */
/* ------------------------------------------------------------------ */
static inline void bsp_cs_low(void)
{
    board_write_spi_cs(BOARD_SPI_CS_PIN, 0);
}

static inline void bsp_cs_high(void)
{
    board_write_spi_cs(BOARD_SPI_CS_PIN, 1);
}

static inline uint32_t bsp_cycle(void)
{
    return (uint32_t)hpm_csr_get_core_mcycle();
}

static inline void bsp_delay_cycles(uint32_t c)
{
    uint32_t t0 = bsp_cycle();
    while ((bsp_cycle() - t0) < c) {
    }
}

static inline void bsp_delay_ns(uint32_t ns)
{
    bsp_delay_cycles((uint32_t)(((uint64_t)s_cpu_hz * (uint64_t)ns) / 1000000000ULL) + 1u);
}

static void bsp_gpio_init(void)
{
    HPM_IOC->PAD[BSP_RST_PAD].FUNC_CTL = IOC_PA10_FUNC_CTL_GPIO_A_10;
    HPM_IOC->PAD[BSP_IRQ_PAD].FUNC_CTL = IOC_PA12_FUNC_CTL_GPIO_A_12;

    gpio_set_pin_output_with_initial(HPM_GPIO0,
        GPIO_GET_PORT_INDEX(BSP_RST_PAD), GPIO_GET_PIN_INDEX(BSP_RST_PAD), 1);
    gpio_set_pin_input(HPM_GPIO0,
        GPIO_GET_PORT_INDEX(BSP_IRQ_PAD), GPIO_GET_PIN_INDEX(BSP_IRQ_PAD));
}

static void bsp_s2500_reset(void)
{
    gpio_write_pin(HPM_GPIO0, GPIO_GET_PORT_INDEX(BSP_RST_PAD),
                   GPIO_GET_PIN_INDEX(BSP_RST_PAD), 0);
    bsp_delay_us(5000);
    gpio_write_pin(HPM_GPIO0, GPIO_GET_PORT_INDEX(BSP_RST_PAD),
                   GPIO_GET_PIN_INDEX(BSP_RST_PAD), 1);
    bsp_delay_us(10000);
}

/* ------------------------------------------------------------------ */
/* SPI1: 模式 0, MSB first, CS_N 覆盖整个事务                          */
/* ------------------------------------------------------------------ */
uint32_t bsp_spi_src_hz(void)
{
    return clock_get_frequency(clock_spi1);
}

/* 由 TIMING.SCLK_DIV 反推真实 SCLK (0xFF = 直接使用时钟源) */
static uint32_t bsp_spi_actual_hz(void)
{
    uint32_t src = clock_get_frequency(clock_spi1);
    uint32_t div = HPM_SPI1->TIMING & SPI_TIMING_SCLK_DIV_MASK;

    if (div >= 0xFFu) {
        return src;
    }
    return src / ((div + 1u) * 2u);
}

static void bsp_dma_hw_init(void)
{
    dmamux_config(HPM_DMAMUX, BSP_DMAMUX_CH_TX, HPM_DMA_SRC_SPI1_TX, true);
    dmamux_config(HPM_DMAMUX, BSP_DMAMUX_CH_RX, HPM_DMA_SRC_SPI1_RX, true);
    dma_disable_channel(HPM_HDMA, BSP_DMA_CH_TX);
    dma_disable_channel(HPM_HDMA, BSP_DMA_CH_RX);
    dma_clear_transfer_status(HPM_HDMA, BSP_DMA_CH_TX);
    dma_clear_transfer_status(HPM_HDMA, BSP_DMA_CH_RX);
}

static void bsp_spi_init(void)
{
    spi_initialize_config_t cfg;

    board_init_spi_clock(HPM_SPI1);
    board_init_spi_pins_with_gpio_as_cs(HPM_SPI1);
    bsp_cs_high();

    hpm_spi_get_default_init_config(&cfg);
    cfg.mode         = spi_master_mode;
    cfg.io_mode      = spi_single_io_mode;
    cfg.clk_polarity = spi_sclk_low_idle;
    cfg.clk_phase    = spi_sclk_sampling_odd_clk_edges;
    cfg.direction    = spi_msb_first;
    cfg.data_len     = 8;
    cfg.data_merge   = false;

    (void)hpm_spi_initialize(HPM_SPI1, &cfg);
    (void)bsp_spi_set_hz(BSP_SPI_CLOCK_HZ);

    bsp_dma_hw_init();
    s_dma_on = 1;                       /* 先打开, 由 bsp_spi_dma_selftest() 判定 */
    s_dma_ok = 0;
    s_spi.dma = 1;
}

int bsp_spi_set_hz(uint32_t hz)
{
    if (hz == 0) {
        hz = BSP_SPI_CLOCK_HZ;
    }
    if (hpm_spi_set_sclk_frequency(HPM_SPI1, hz) != status_success) {
        return -1;
    }
    s_spi.sclk_hz = bsp_spi_actual_hz();
    s_spi.src_hz  = bsp_spi_src_hz();
    return 0;
}

/* ------------------------------------------------------------------ */
/* 数据事务: 阻塞式 FIFO / DMA 两条路径                                */
/* ------------------------------------------------------------------ */
static int bsp_spi_xfer_blocking(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    hpm_stat_t st = hpm_spi_transmit_receive_blocking(HPM_SPI1, (uint8_t *)tx, rx, len, 100);
    if (st == status_success) {
        st = spi_wait_for_idle_status(HPM_SPI1);
    }
    return (st == status_success) ? 0 : -1;
}

static int bsp_spi_xfer_dma(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    dma_handshake_config_t cfg;
    uint32_t w0;

    if (len > sizeof(s_dma_tx)) {
        return -1;
    }
    memcpy(s_dma_tx, tx, len);
    memset(s_dma_rx, 0, sizeof(s_dma_rx));

    /* TX: 源递增, 目的固定 = SPI DATA */
    spi_disable_tx_dma(HPM_SPI1);
    dma_default_handshake_config(HPM_HDMA, &cfg);
    cfg.ch_index     = BSP_DMA_CH_TX;
    cfg.dst          = (uint32_t)&HPM_SPI1->DATA;
    cfg.dst_fixed    = true;
    cfg.src          = (uint32_t)s_dma_tx;
    cfg.src_fixed    = false;
    cfg.data_width   = DMA_TRANSFER_WIDTH_BYTE;
    cfg.size_in_byte = len;
    if (dma_setup_handshake(HPM_HDMA, &cfg, true) != status_success) {
        return -1;
    }

    /* RX: 源固定 = SPI DATA, 目的递增 */
    spi_disable_rx_dma(HPM_SPI1);
    dma_default_handshake_config(HPM_HDMA, &cfg);
    cfg.ch_index     = BSP_DMA_CH_RX;
    cfg.dst          = (uint32_t)s_dma_rx;
    cfg.dst_fixed    = false;
    cfg.src          = (uint32_t)&HPM_SPI1->DATA;
    cfg.src_fixed    = true;
    cfg.data_width   = DMA_TRANSFER_WIDTH_BYTE;
    cfg.size_in_byte = len;
    if (dma_setup_handshake(HPM_HDMA, &cfg, true) != status_success) {
        return -1;
    }

    /* 启动 SPI (使能 TX/RX DMA 请求并写 CMD 触发) */
    if (hpm_spi_transmit_receive_setup_dma(HPM_SPI1, len) != status_success) {
        return -1;
    }

    w0 = bsp_cycle();
    while (dma_channel_is_enable(HPM_HDMA, BSP_DMA_CH_TX) ||
           dma_channel_is_enable(HPM_HDMA, BSP_DMA_CH_RX)) {
    }
    while (spi_is_active(HPM_SPI1)) {
    }
    s_spi.wait_cyc += (uint64_t)(bsp_cycle() - w0);

    dma_clear_transfer_status(HPM_HDMA, BSP_DMA_CH_TX);
    dma_clear_transfer_status(HPM_HDMA, BSP_DMA_CH_RX);
    memcpy(rx, s_dma_rx, len);
    return 0;
}

static int bsp_spi_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    uint32_t t0 = bsp_cycle();
    int rc;

    (void)ctx;

    bsp_cs_low();
    bsp_delay_ns(BSP_CS_DELAY_NS);

    rc = (s_dma_on && len <= sizeof(s_dma_tx))
             ? bsp_spi_xfer_dma(tx, rx, len)
             : bsp_spi_xfer_blocking(tx, rx, len);

    bsp_delay_ns(BSP_CS_DELAY_NS);
    bsp_cs_high();

    {
        /* cyc 用 64 位累加: 480 MHz 下 32 位周期计数器 8.95 s 就回绕,
         * 回绕后的"平均周期/事务"会变成假数字。 */
        uint32_t d = bsp_cycle() - t0;
        s_spi.xfers++;
        s_spi.bytes = (uint32_t)(s_spi.bytes + len);
        s_spi.cyc  += (uint64_t)d;
    }
    return rc;
}

void bsp_spi_set_dma(int on)
{
    s_dma_on = on ? 1 : 0;
    s_spi.dma = (uint8_t)s_dma_on;
}

int bsp_spi_dma(void)
{
    return s_dma_on;
}

void bsp_spi_get_stats(bsp_spi_stats_t *st)
{
    if (st) {
        *st = s_spi;
        st->dma_ok = (uint8_t)s_dma_ok;
        st->dma    = (uint8_t)s_dma_on;
    }
}

void bsp_spi_reset_stats(void)
{
    uint32_t sclk = s_spi.sclk_hz, src = s_spi.src_hz;

    memset(&s_spi, 0, sizeof(s_spi));
    s_spi.sclk_hz = sclk;
    s_spi.src_hz  = src;
    s_spi.dma     = (uint8_t)s_dma_on;
}

/**
 * @brief DMA 自检: 用驱动读同一个寄存器, 比较阻塞与 DMA 两条路径的结果
 *
 * 失败时自动回退到阻塞式收发并打印告警 (便于现场定位 DMA/内存配置问题)。
 */
int bsp_spi_dma_selftest(void)
{
    s2500_dev_t *dev = s2500_app_dev();
    uint32_t v_blk = 0, v_dma = 0;
    int i, ok = 0;

    if (!dev) {
        return 0;
    }

    for (i = 0; i < 3 && !ok; i++) {
        v_blk = 0;
        v_dma = 0;
        bsp_spi_set_dma(0);
        if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_IDVER, &v_blk) != S2500_OK) {
            continue;
        }
        bsp_spi_set_dma(1);
        if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_IDVER, &v_dma) != S2500_OK) {
            continue;
        }
        if (v_dma == v_blk && v_dma == 0x11u) {
            ok = 1;
        }
    }

    s_dma_ok = ok;
    bsp_spi_set_dma(ok ? 1 : 0);
    if (!ok) {
        bsp_puts("[SPI1] DMA 自检失败 (阻塞=0x.../DMA=0x...), 已回退到阻塞式收发\r\n");
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* 时间基准                                                            */
/* ------------------------------------------------------------------ */
void bsp_delay_us(uint32_t us)
{
    clock_cpu_delay_us(us);
}

uint32_t bsp_cpu_hz(void)
{
    return s_cpu_hz;
}

/**
 * @brief 毫秒时基
 *
 * 用 MCHTMR (24MHz, 64 位) 而不是 mcycle: mcycle 是 32 位, 480MHz 下 8.95s
 * 就回绕一次, 会让 lwIP 的 sys_now() 在 9 秒内"倒退", 造成 TCP 定时器异常。
 */
uint32_t bsp_millis(void)
{
    return (uint32_t)(mchtmr_get_count(HPM_MCHTMR) / (uint64_t)s_ms_div);
}

/* ------------------------------------------------------------------ */
/* 调试串口: UART0 (FT2232H) + UART2 (PB08/PB09) 双路输出              */
/* ------------------------------------------------------------------ */
static void bsp_uart_init_one(UART_Type *ptr)
{
    uart_config_t cfg;

    uart_default_config(ptr, &cfg);
    cfg.src_freq_in_hz = board_init_uart_clock(ptr);
    cfg.baudrate = BSP_BAUDRATE;
    (void)uart_init(ptr, &cfg);
}

static void bsp_uart_init(void)
{
    /* UART0: PA00/PA01 (板载 FT2232H channel B, 实测调试口) */
    HPM_IOC->PAD[IOC_PAD_PA00].FUNC_CTL = IOC_PA00_FUNC_CTL_UART0_TXD;
    HPM_IOC->PAD[IOC_PAD_PA01].FUNC_CTL = IOC_PA01_FUNC_CTL_UART0_RXD;
    HPM_IOC->PAD[IOC_PAD_PA01].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    bsp_uart_init_one(HPM_UART0);

    /* UART2: PB08/PB09 (用户接线表, 作为镜像输出/备用输入) */
    HPM_IOC->PAD[IOC_PAD_PB08].FUNC_CTL = IOC_PB08_FUNC_CTL_UART2_TXD;
    HPM_IOC->PAD[IOC_PAD_PB09].FUNC_CTL = IOC_PB09_FUNC_CTL_UART2_RXD;
    HPM_IOC->PAD[IOC_PAD_PB09].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    bsp_uart_init_one(HPM_UART2);
}

int bsp_uart_getc(void)
{
    uint8_t c = 0;

    if (uart_check_status(HPM_UART0, uart_stat_data_ready)) {
        (void)uart_receive_byte(HPM_UART0, &c);
        return (int)c;
    }
    if (uart_check_status(HPM_UART2, uart_stat_data_ready)) {
        (void)uart_receive_byte(HPM_UART2, &c);
        return (int)c;
    }
    return -1;
}

void bsp_uart_write(const char *data, unsigned int len)
{
    unsigned int i;

    for (i = 0; i < len; i++) {
        (void)uart_send_byte(HPM_UART0, (uint8_t)data[i]);
        /* UART2 仅做非阻塞镜像: 未接/时钟未就绪时不能卡住主串口 */
        if (uart_check_status(HPM_UART2, uart_stat_tx_slot_avail)) {
            (void)uart_send_byte(HPM_UART2, (uint8_t)data[i]);
        }
    }
}

void bsp_puts(const char *s)
{
    unsigned int n = 0;

    while (s[n] != '\0') {
        n++;
    }
    bsp_uart_write(s, n);
}

/* ------------------------------------------------------------------ */
/* S2500 HAL                                                           */
/* ------------------------------------------------------------------ */
static void bsp_hal_delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    bsp_delay_us(us);
}

static uint32_t bsp_hal_millis(void *ctx)
{
    (void)ctx;
    return bsp_millis();
}

static int bsp_hal_irq_level(void *ctx)
{
    (void)ctx;
    /* IRQ_N 低有效: 返回 1 表示有中断 */
    return gpio_read_pin(HPM_GPIO0, GPIO_GET_PORT_INDEX(BSP_IRQ_PAD),
                         GPIO_GET_PIN_INDEX(BSP_IRQ_PAD)) ? 0 : 1;
}

static const s2500_hal_t s_hal = {
    .ctx       = NULL,
    .spi_xfer  = bsp_spi_xfer,
    .delay_us  = bsp_hal_delay_us,
    .millis    = bsp_hal_millis,
    .irq_level = bsp_hal_irq_level,
    .log       = NULL,
};

const s2500_hal_t *s2500_board_hal(void)
{
    return &s_hal;
}

void s2500_board_init(void)
{
    board_init_clock();
    board_init_pmp();

    s_cpu_hz = clock_get_frequency(clock_cpu0);
    if (s_cpu_hz == 0UL) {
        s_cpu_hz = 480000000UL;
    }
    s_ms_div = clock_get_frequency(clock_mchtmr0) / 1000UL;
    if (s_ms_div == 0UL) {
        s_ms_div = 24000UL;
    }

    bsp_gpio_init();
    bsp_spi_init();
    bsp_uart_init();          /* 放在 SPI 时钟配置之后, 避免时钟源被改 */
    bsp_s2500_reset();
}
