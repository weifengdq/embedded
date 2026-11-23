/*
 * OPEN Alliance TC6 (10BASE-T1x MAC-PHY Serial Interface) - HPM MCU port
 *
 * Bare-metal port of the Linux oa_tc6.c framework.  No interrupts, no
 * threads: the caller drives control transfers and data transfers from the
 * main loop or from a GPIO interrupt handler.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <string.h>
#include "oa_tc6.h"
#include "board.h"
#include "hpm_clock_drv.h"
#include "hpm_gpio_drv.h"
#include "hpm_ioc_regs.h"
#include "hpm_csr_drv.h"
#include "hpm_spi.h"

/* SPI pad settings (PAD_CTL) applied to PA20..PA23; slow slew/speed by default */
/* slow slew / slow speed like the reference design, but keep the default drive
 * strength (DS=3): DS=0 disables the output driver on this SoC */
static uint32_t tc6_pad_ctl = (uint32_t) (IOC_PAD_PAD_CTL_SR_SET(0) | IOC_PAD_PAD_CTL_SPD_SET(0) |
                                          IOC_PAD_PAD_CTL_DS_SET(3));

/* ------------------------------------------------------------------ helpers */
static inline uint32_t be32_get(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3];
}

static inline void be32_put(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

/* odd parity over the 32 bit header (bit 0 excluded) */
static inline uint32_t oa_tc6_parity(uint32_t v)
{
    v ^= v >> 1;
    v ^= v >> 2;
    v = (v & 0x11111111U) * 0x11111111U;
    return !((v >> 28) & 1U);
}

static uint32_t oa_tc6_ctrl_header(uint32_t addr, uint8_t length, bool write)
{
    uint32_t h = 0;
    if (write) {
        h |= OA_TC6_CTRL_HDR_WRITE_NOT_READ;
    }
    h |= ((addr >> 16) & 0xFU) << 24;          /* memory map selector */
    h |= (addr & 0xFFFFU) << 8;                /* register address    */
    h |= ((uint32_t) (length - 1U) & 0x7FU) << 1;
    h |= oa_tc6_parity(h);
    return h;
}

static uint32_t oa_tc6_data_header(bool data_valid, bool start_valid, bool end_valid, uint8_t end_byte_offset)
{
    uint32_t h = OA_TC6_DATA_HDR_DATA_NOT_CTRL;
    if (data_valid) {
        h |= OA_TC6_DATA_HDR_DATA_VALID;
    }
    if (start_valid) {
        h |= OA_TC6_DATA_HDR_START_VALID;
    }
    if (end_valid) {
        h |= OA_TC6_DATA_HDR_END_VALID | OA_TC6_DATA_HDR_END_BYTE_OFFSET(end_byte_offset);
    }
    h |= oa_tc6_parity(h);
    return h;
}


/* --------------------------------------------------- bit-banged SPI fallback */
/*
 * Four SPI2 pads used as plain GPIO: SCLK PA20, CS_N PA21, SDO/MISO PA22,
 * SDI/MOSI PA23.  Mode 0: MOSI changes while SCLK is low, MISO is sampled just
 * after the rising edge (LAN8651 SDO changes on the falling edge,
 * t_SDV <= 21 ns, DS60001734E Table 9-9).
 */
#define BB_SCLK_PIN 20U
#define BB_CS_PIN   21U
#define BB_MISO_PIN 22U
#define BB_MOSI_PIN 23U

static void oa_tc6_cs_write(uint32_t level);

static inline void bb_pin_out(uint32_t pin, uint32_t level)
{
    if (level) {
        HPM_GPIO0->DO[GPIO_DO_GPIOA].SET = 1UL << pin;
    } else {
        HPM_GPIO0->DO[GPIO_DO_GPIOA].CLEAR = 1UL << pin;
    }
}

static inline uint32_t bb_pin_in(uint32_t pin)
{
    return (HPM_GPIO0->DI[GPIO_DI_GPIOA].VALUE >> pin) & 1UL;
}

static inline void bb_delay(oa_tc6_t *tc6)
{
    uint8_t n = tc6->bb_half_delay;
    while (n--) {
        __asm volatile("nop");
    }
}

static void bb_pins_gpio(void)
{
    HPM_IOC->PAD[IOC_PAD_PA20].FUNC_CTL = IOC_PA20_FUNC_CTL_GPIO_A_20;
    HPM_IOC->PAD[IOC_PAD_PA21].FUNC_CTL = IOC_PA21_FUNC_CTL_GPIO_A_21;
    HPM_IOC->PAD[IOC_PAD_PA22].FUNC_CTL = IOC_PA22_FUNC_CTL_GPIO_A_22;
    HPM_IOC->PAD[IOC_PAD_PA23].FUNC_CTL = IOC_PA23_FUNC_CTL_GPIO_A_23;
    HPM_IOC->PAD[IOC_PAD_PA22].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    gpio_set_pin_output_with_initial(HPM_GPIO0, GPIO_DO_GPIOA, BB_SCLK_PIN, 0);
    gpio_set_pin_output_with_initial(HPM_GPIO0, GPIO_DO_GPIOA, BB_CS_PIN, 1);
    gpio_set_pin_output_with_initial(HPM_GPIO0, GPIO_DO_GPIOA, BB_MOSI_PIN, 0);
    gpio_set_pin_input(HPM_GPIO0, GPIO_DI_GPIOA, BB_MISO_PIN);
}

static void bb_pins_spi(void)
{
    HPM_IOC->PAD[IOC_PAD_PA20].FUNC_CTL = IOC_PA20_FUNC_CTL_SPI2_SCLK | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
    HPM_IOC->PAD[IOC_PAD_PA22].FUNC_CTL = IOC_PA22_FUNC_CTL_SPI2_MISO;
    HPM_IOC->PAD[IOC_PAD_PA23].FUNC_CTL = IOC_PA23_FUNC_CTL_SPI2_MOSI;
    /* CS stays a GPIO (see pinmux_net1.c) */
    HPM_IOC->PAD[IOC_PAD_PA21].FUNC_CTL = IOC_PA21_FUNC_CTL_GPIO_A_21;
    oa_tc6_cs_write(1);
}

void oa_tc6_use_bitbang(oa_tc6_t *tc6, bool enable)
{
    tc6->use_bb = enable;
    if (enable) {
        if (tc6->bb_half_delay == 0U) {
            tc6->bb_half_delay = 10U;
        }
        bb_pins_gpio();
    } else {
        bb_pins_spi();
    }
}

void oa_tc6_set_bb_delay(oa_tc6_t *tc6, uint8_t half_delay_nops)
{
    tc6->bb_half_delay = half_delay_nops;
}

static int bb_transfer(oa_tc6_t *tc6, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    bb_pin_out(BB_CS_PIN, 0);
    bb_delay(tc6);
    for (uint32_t i = 0; i < len; i++) {
        uint8_t out = tx[i];
        uint8_t in = 0;
        for (int b = 7; b >= 0; b--) {
            bb_pin_out(BB_MOSI_PIN, (out >> b) & 1U);
            bb_delay(tc6);
            bb_pin_out(BB_SCLK_PIN, 1);
            bb_delay(tc6);
            in = (uint8_t) ((in << 1) | bb_pin_in(BB_MISO_PIN));
            bb_pin_out(BB_SCLK_PIN, 0);
            bb_delay(tc6);
        }
        rx[i] = in;
    }
    bb_pin_out(BB_CS_PIN, 1);
    bb_delay(tc6);
    return 0;
}

uint32_t oa_tc6_bb_measure_sclk(oa_tc6_t *tc6)
{
    static const uint8_t pattern[4] = {0x00, 0xFF, 0x00, 0xFF};
    uint8_t rx[4];
    uint64_t t0, t1;

    if (!tc6->use_bb) {
        return 0;
    }
    t0 = hpm_csr_get_core_mcycle();
    bb_transfer(tc6, pattern, rx, 4);
    t1 = hpm_csr_get_core_mcycle();
    if (t1 <= t0) {
        return 0;
    }
    /* 32 bits per 4 bytes */
    return (uint32_t) ((uint64_t) clock_get_frequency(clock_cpu0) * 32ULL / (t1 - t0));
}

/* ------------------------------------------------------------------- SPI IO */
/*
 * Hardware SPI transport, following the Microchip/Microchip-EVK reference
 * implementation (hpm5361_lan8651): HPM SDK high level SPI API + GPIO chip
 * select + explicit CS guard delays + slow pad slew/speed.
 *
 * Why the low level API (spi_master_timing_init/spi_format_init/spi_transfer)
 * was replaced: on hpm5e31_1 rev.1 the SPI2 controller produced no usable echo
 * on the LAN8651 control channel with that path (see chapter 10 U-10-1).  The
 * differences that matter for LAN8651 are:
 *   1. hpm_spi_initialize() also programs TRANSCTRL.SLVDATAONLY and leaves
 *      WRTRANCNT/RDTRANCNT to the transfer call (hpm_spi_transfer_init());
 *   2. hpm_spi_transmit_receive_blocking() writes the dummy CMD=0xFF that is
 *      required to start a master transfer even without a command phase;
 *   3. chip select is a plain GPIO, asserted around the whole frame with 1 us
 *      guard delays (LAN8651 requirement: one CS assertion per control frame);
 *   4. the SPI pads run with the slowest slew rate / speed (SR=0, SPD=0).
 */
void oa_tc6_set_pad_ctl(oa_tc6_t *tc6, uint32_t pad_ctl)
{
    static const uint8_t pads[4] = {IOC_PAD_PA20, IOC_PAD_PA21, IOC_PAD_PA22, IOC_PAD_PA23};
    tc6_pad_ctl = pad_ctl;
    for (int i = 0; i < 4; i++) {
        HPM_IOC->PAD[pads[i]].PAD_CTL = pad_ctl;
    }
    /* MISO keeps its pull-up so a missing driver is visible as 0xFF */
    HPM_IOC->PAD[IOC_PAD_PA22].PAD_CTL = pad_ctl | IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
    (void) tc6;
}

uint32_t oa_tc6_get_pad_ctl(void)
{
    return tc6_pad_ctl;
}

static void oa_tc6_cs_write(uint32_t level)
{
    gpio_write_pin(BOARD_LAN8651_CS_GPIO_CTRL, BOARD_LAN8651_CS_GPIO_INDEX,
                   BOARD_LAN8651_CS_GPIO_PIN, (uint8_t) level);
}

static void oa_tc6_cs_gpio_init(void)
{
    HPM_IOC->PAD[IOC_PAD_PA21].FUNC_CTL = IOC_PA21_FUNC_CTL_GPIO_A_21;
    oa_tc6_cs_write(1);
}

static int oa_tc6_spi_transfer(oa_tc6_t *tc6, uint8_t *tx, uint8_t *rx, uint32_t len)
{
    hpm_stat_t st;
    spi_control_config_t cfg;

    if (tc6->use_bb) {
        return bb_transfer(tc6, tx, rx, len);
    }
    if (tc6->use_ll_xfer) {
        /* FIFO aware low level transfer: the high level blocking helper loses
         * RX bytes on transfers longer than the RX FIFO (e.g. 16 register
         * block reads or multi chunk data frames). */
        oa_tc6_cs_write(0);
        board_delay_us(1);
        spi_master_get_default_control_config(&cfg);
        cfg.master_config.cmd_enable = false;
        cfg.master_config.addr_enable = false;
        cfg.common_config.trans_mode = spi_trans_write_read_together;
        cfg.common_config.cs_index = 0;              /* controller must not drive CS */
        st = spi_transfer(tc6->spi, &cfg, NULL, NULL, tx, len, rx, len);
        if (st == status_success) {
            st = spi_wait_for_idle_status(tc6->spi);
        }
        board_delay_us(1);
        oa_tc6_cs_write(1);
        board_delay_us(1);
        if (st != status_success) {
            tc6->stat_spi_err++;
            return -1;
        }
        return 0;
    }
    /* LAN8651 DS §5.1: one CS assertion per control frame, CS low for the whole
     * 12 byte frame.  The 1 us guard delays come from the reference project. */
    oa_tc6_cs_write(0);
    board_delay_us(1);
    st = hpm_spi_transmit_receive_blocking(tc6->spi, tx, rx, len, 100U);
    if (st == status_success) {
        st = spi_wait_for_idle_status(tc6->spi);
    }
    board_delay_us(1);
    oa_tc6_cs_write(1);
    board_delay_us(1);
    if (st != status_success) {
        tc6->stat_spi_err++;
        return -1;
    }
    return 0;
}

void oa_tc6_use_gpio_cs(oa_tc6_t *tc6, bool enable)
{
    tc6->gpio_cs = enable;
    if (enable) {
        oa_tc6_cs_gpio_init();
    }
}

hpm_stat_t oa_tc6_set_spi_mode(oa_tc6_t *tc6, uint8_t cpol, uint8_t cpha)
{
    spi_initialize_config_t init_config = {0};

    hpm_spi_get_default_init_config(&init_config);
    init_config.mode = spi_master_mode;
    init_config.io_mode = spi_single_io_mode;
    init_config.clk_polarity = cpol ? spi_sclk_high_idle : spi_sclk_low_idle;
    init_config.clk_phase = cpha ? spi_sclk_sampling_even_clk_edges : spi_sclk_sampling_odd_clk_edges;
    init_config.direction = spi_msb_first;
    init_config.data_len = 8;
    init_config.data_merge = false;
    if (hpm_spi_initialize(tc6->spi, &init_config) != status_success) {
        return status_fail;
    }
    return hpm_spi_set_sclk_frequency(tc6->spi, tc6->sclk_hz);
}

/* actual SCLK derived from the TIMING.SCLK_DIV field and the SPI2 source clock */
static uint32_t oa_tc6_spi_actual_sclk(SPI_Type *spi, uint32_t clk_src_hz)
{
    uint32_t div = (spi->TIMING & SPI_TIMING_SCLK_DIV_MASK) >> SPI_TIMING_SCLK_DIV_SHIFT;
    if ((div & 0x80U) != 0U) {
        return 0;   /* SCLK gated off */
    }
    return clk_src_hz / ((div + 1U) * 2U);
}

hpm_stat_t oa_tc6_spi_init(oa_tc6_t *tc6, SPI_Type *spi, uint32_t clk_src_hz, uint32_t sclk_hz)
{
    spi_initialize_config_t init_config = {0};

    memset(tc6, 0, sizeof(*tc6));
    tc6->spi = spi;
    tc6->sclk_hz = sclk_hz;
    tc6->gpio_cs = true;      /* GPIO chip select is the reference design choice */
    tc6->use_ll_xfer = true;  /* FIFO aware transfer path (needed for >8 byte frames) */

    /* slowest slew rate + slowest speed on all four SPI pads (reference design) */
    oa_tc6_set_pad_ctl(tc6, IOC_PAD_PAD_CTL_SR_SET(0) | IOC_PAD_PAD_CTL_SPD_SET(0));

    hpm_spi_get_default_init_config(&init_config);
    init_config.mode = spi_master_mode;
    init_config.io_mode = spi_single_io_mode;
    init_config.clk_polarity = spi_sclk_low_idle;              /* CPOL = 0 */
    init_config.clk_phase = spi_sclk_sampling_odd_clk_edges;   /* CPHA = 0 -> SPI mode 0 */
    init_config.direction = spi_msb_first;
    init_config.data_len = 8;
    init_config.data_merge = false;

    if (hpm_spi_initialize(spi, &init_config) != status_success) {
        return status_fail;
    }
    if (hpm_spi_set_sclk_frequency(spi, sclk_hz) != status_success) {
        return status_fail;
    }
    tc6->sclk_hz = oa_tc6_spi_actual_sclk(spi, clk_src_hz);

    /* CS as a plain GPIO, idle high (de-asserted) */
    oa_tc6_cs_gpio_init();
    /* make sure the SPI2 controller does not drive the hardware CS signal */
    spi->CTRL = (spi->CTRL & ~SPI_CTRL_CS_EN_MASK);
    /* hardware flow: keep the fast pads (DS=3) but slow slew/speed */
    oa_tc6_set_pad_ctl(tc6, IOC_PAD_PAD_CTL_SR_SET(0) | IOC_PAD_PAD_CTL_SPD_SET(0) | IOC_PAD_PAD_CTL_DS_SET(3));

    return status_success;
}

int oa_tc6_spi_transfer_raw(oa_tc6_t *tc6, uint8_t *tx, uint8_t *rx, uint32_t len)
{
    return oa_tc6_spi_transfer(tc6, tx, rx, len);
}

uint32_t oa_tc6_spi_get_sclk(oa_tc6_t *tc6, uint32_t clk_src_hz)
{
    if (tc6->use_bb) {
        return oa_tc6_bb_measure_sclk(tc6);
    }
    return oa_tc6_spi_actual_sclk(tc6->spi, clk_src_hz);
}


/* --------------------------------------------------------- register access */
int oa_tc6_read_regs(oa_tc6_t *tc6, uint32_t addr, uint32_t *value, uint8_t length)
{
    uint32_t hdr;
    uint32_t total;

    if ((length == 0U) || (length > OA_TC6_CTRL_MAX_REGISTERS)) {
        return -1;
    }
    hdr = oa_tc6_ctrl_header(addr, length, false);
    total = OA_TC6_CTRL_HEADER_SIZE + (uint32_t) length * OA_TC6_CTRL_REG_VALUE_SIZE + OA_TC6_CTRL_IGNORED_SIZE;

    be32_put(&tc6->ctrl_tx[0], hdr);
    memset(&tc6->ctrl_tx[OA_TC6_CTRL_HEADER_SIZE], 0, total - OA_TC6_CTRL_HEADER_SIZE);
    memset(tc6->ctrl_rx, 0, total);

    if (oa_tc6_spi_transfer(tc6, tc6->ctrl_tx, tc6->ctrl_rx, total) != 0) {
        return -2;
    }
    tc6->stat_ctrl_xfer++;
    /* rx layout: [4B ignored][4B echoed header][values] */
    if (be32_get(&tc6->ctrl_rx[OA_TC6_CTRL_IGNORED_SIZE]) != hdr) {
        tc6->stat_ctrl_err++;
        return -3;
    }
    for (uint8_t i = 0; i < length; i++) {
        value[i] = be32_get(&tc6->ctrl_rx[OA_TC6_CTRL_IGNORED_SIZE + OA_TC6_CTRL_HEADER_SIZE + (uint32_t) i * 4U]);
    }
    return 0;
}

int oa_tc6_write_regs(oa_tc6_t *tc6, uint32_t addr, const uint32_t *value, uint8_t length)
{
    uint32_t hdr;
    uint32_t total;

    if ((length == 0U) || (length > OA_TC6_CTRL_MAX_REGISTERS)) {
        return -1;
    }
    hdr = oa_tc6_ctrl_header(addr, length, true);
    total = OA_TC6_CTRL_HEADER_SIZE + (uint32_t) length * OA_TC6_CTRL_REG_VALUE_SIZE + OA_TC6_CTRL_IGNORED_SIZE;

    be32_put(&tc6->ctrl_tx[0], hdr);
    for (uint8_t i = 0; i < length; i++) {
        be32_put(&tc6->ctrl_tx[OA_TC6_CTRL_HEADER_SIZE + (uint32_t) i * 4U], value[i]);
    }
    memset(&tc6->ctrl_tx[total - OA_TC6_CTRL_IGNORED_SIZE], 0, OA_TC6_CTRL_IGNORED_SIZE);
    memset(tc6->ctrl_rx, 0, total);

    if (oa_tc6_spi_transfer(tc6, tc6->ctrl_tx, tc6->ctrl_rx, total) != 0) {
        return -2;
    }
    tc6->stat_ctrl_xfer++;
    /* the MAC-PHY echoes header + written values, offset by 4 ignored bytes */
    if (memcmp(tc6->ctrl_tx, &tc6->ctrl_rx[OA_TC6_CTRL_IGNORED_SIZE], total - OA_TC6_CTRL_IGNORED_SIZE) != 0) {
        tc6->stat_ctrl_err++;
        return -3;
    }
    return 0;
}

int oa_tc6_read_reg(oa_tc6_t *tc6, uint32_t addr, uint32_t *value)
{
    return oa_tc6_read_regs(tc6, addr, value, 1);
}

int oa_tc6_write_reg(oa_tc6_t *tc6, uint32_t addr, uint32_t value)
{
    return oa_tc6_write_regs(tc6, addr, &value, 1);
}

int oa_tc6_mmd_read(oa_tc6_t *tc6, uint8_t mms, uint16_t reg, uint16_t *value)
{
    uint32_t v = 0;
    int ret = oa_tc6_read_reg(tc6, ((uint32_t) mms << 16) | reg, &v);
    if (ret == 0) {
        *value = (uint16_t) v;
    }
    return ret;
}

int oa_tc6_mmd_write(oa_tc6_t *tc6, uint8_t mms, uint16_t reg, uint16_t value)
{
    return oa_tc6_write_reg(tc6, ((uint32_t) mms << 16) | reg, (uint32_t) value);
}

/*
 * The Clause 22 PHY registers are part of memory map selector 0, starting at
 * address 0xFF00 (LAN8650/1 DS60001734E 11.1 "MMS0 begins at 0xFF00"), so the
 * memory map selector field stays 0 and the address is 0xFF00 | (regnum & 0x1F).
 * (A previous version wrongly used MMS = 0xFF, which made every PHY access read
 * back as garbage.)
 */
int oa_tc6_c22_read(oa_tc6_t *tc6, uint8_t reg, uint16_t *value)
{
    uint32_t v = 0;
    int ret = oa_tc6_read_reg(tc6, OA_TC6_C22_REG_BASE | (uint32_t) (reg & 0x1FU), &v);
    if (ret == 0) {
        *value = (uint16_t) v;
    }
    return ret;
}

int oa_tc6_c22_write(oa_tc6_t *tc6, uint8_t reg, uint16_t value)
{
    return oa_tc6_write_reg(tc6, OA_TC6_C22_REG_BASE | (uint32_t) (reg & 0x1FU), (uint32_t) value);
}

/* ---------------------------------------------------------------- bring-up */
int oa_tc6_wait_reset_complete(oa_tc6_t *tc6, uint32_t timeout_ms)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < timeout_ms; i++) {
        if (oa_tc6_read_reg(tc6, OA_TC6_REG_STATUS0, &v) != 0) {
            board_delay_ms(1);
            continue;
        }
        if ((v & STATUS0_RESETC) != 0U) {
            tc6->stat_status0 = v;
            return 0;
        }
        board_delay_ms(1);
    }
    tc6->stat_status0 = v;
    return -1;
}

int oa_tc6_clear_status0(oa_tc6_t *tc6)
{
    uint32_t v = 0;
    if (oa_tc6_read_reg(tc6, OA_TC6_REG_STATUS0, &v) != 0) {
        return -1;
    }
    return oa_tc6_write_reg(tc6, OA_TC6_REG_STATUS0, v);
}

int oa_tc6_soft_reset(oa_tc6_t *tc6)
{
    if (oa_tc6_write_reg(tc6, OA_TC6_REG_RESET, RESET_SWRESET) != 0) {
        return -1;
    }
    return oa_tc6_wait_reset_complete(tc6, 1000);
}

int oa_tc6_unmask_error_interrupts(oa_tc6_t *tc6)
{
    uint32_t v = 0;
    const uint32_t mask = (STATUS0_TX_PROTOCOL_ERROR | STATUS0_TX_BUFFER_OVERFLOW_ERROR | \
                           STATUS0_TX_BUFFER_UNDERFLOW_ERROR | STATUS0_RX_BUFFER_OVERFLOW_ERROR | \
                           STATUS0_LOSS_OF_FRAME_ERROR | STATUS0_HEADER_ERROR | \
                           STATUS0_TX_FRAME_CHECK_SEQ_ERROR | STATUS0_CTRL_DATA_PROTECTION_ERROR);
    if (oa_tc6_read_reg(tc6, OA_TC6_REG_INT_MASK0, &v) != 0) {
        return -1;
    }
    v &= ~mask;   /* 0 = enabled */
    return oa_tc6_write_reg(tc6, OA_TC6_REG_INT_MASK0, v);
}

int oa_tc6_enable_data_transfer(oa_tc6_t *tc6)
{
    uint32_t v = 0;
    if (oa_tc6_read_reg(tc6, OA_TC6_REG_CONFIG0, &v) != 0) {
        return -1;
    }
    v |= CONFIG0_SYNC;
    return oa_tc6_write_reg(tc6, OA_TC6_REG_CONFIG0, v);
}

int oa_tc6_zarfe_enable(oa_tc6_t *tc6)
{
    uint32_t v = 0;
    if (oa_tc6_read_reg(tc6, OA_TC6_REG_CONFIG0, &v) != 0) {
        return -1;
    }
    v |= CONFIG0_ZARFE_ENABLE;
    return oa_tc6_write_reg(tc6, OA_TC6_REG_CONFIG0, v);
}

int oa_tc6_update_buffer_status(oa_tc6_t *tc6)
{
    uint32_t v = 0;
    if (oa_tc6_read_reg(tc6, OA_TC6_REG_BUFFER_STATUS, &v) != 0) {
        return -1;
    }
    tc6->tx_credits = (uint8_t) ((v >> BUFFER_STATUS_TX_CREDITS_SHIFT) & 0x1FU);
    tc6->rx_chunks_available = (uint8_t) ((v >> BUFFER_STATUS_RX_CHUNKS_SHIFT) & 0x1FU);
    return 0;
}

/* ------------------------------------------------------------ data transfer */
static void oa_tc6_rx_push(oa_tc6_t *tc6, const uint8_t *frame, uint16_t len)
{
    if (len == 0U) {
        return;
    }
    if (tc6->rx_q_cnt >= OA_TC6_RX_QUEUE_DEPTH) {
        /* queue full: drop the new frame, count it */
        tc6->stat_rx_dropped++;
        return;
    }
    if (len > OA_TC6_FRAME_MAX) {
        len = OA_TC6_FRAME_MAX;
    }
    memcpy(tc6->rx_q[tc6->rx_q_head], frame, len);
    tc6->rx_q_len[tc6->rx_q_head] = len;
    tc6->rx_q_head = (uint8_t) ((tc6->rx_q_head + 1U) % OA_TC6_RX_QUEUE_DEPTH);
    tc6->rx_q_cnt++;
    tc6->stat_rx_frames++;
    tc6->stat_rx_bytes += len;
}

static void oa_tc6_rx_append(oa_tc6_t *tc6, const uint8_t *data, uint16_t len)
{
    if (!tc6->rx_in_progress) {
        return;
    }
    if ((uint32_t) tc6->rx_frame_len + len > OA_TC6_FRAME_MAX) {
        tc6->rx_in_progress = false;
        tc6->rx_frame_len = 0;
        tc6->stat_rx_dropped++;
        return;
    }
    memcpy(&tc6->rx_frame[tc6->rx_frame_len], data, len);
    tc6->rx_frame_len = (uint16_t) (tc6->rx_frame_len + len);
}

static void oa_tc6_rx_finish(oa_tc6_t *tc6, const uint8_t *data, uint16_t len)
{
    oa_tc6_rx_append(tc6, data, len);
    if (tc6->rx_in_progress) {
        oa_tc6_rx_push(tc6, tc6->rx_frame, tc6->rx_frame_len);
        tc6->rx_in_progress = false;
        tc6->rx_frame_len = 0;
    }
}

static void oa_tc6_process_rx_payload(oa_tc6_t *tc6, const uint8_t *chunk, uint32_t footer)
{
    uint8_t start_off = (uint8_t) (OA_TC6_DATA_FTR_START_WORD_OFFSET(footer) * 4U);
    uint8_t end_off = (uint8_t) OA_TC6_DATA_FTR_END_BYTE_OFFSET(footer);
    bool start_valid = (footer & OA_TC6_DATA_FTR_START_VALID) != 0U;
    bool end_valid = (footer & OA_TC6_DATA_FTR_END_VALID) != 0U;

    if (start_off > OA_TC6_CHUNK_PAYLOAD_SIZE) {
        start_off = OA_TC6_CHUNK_PAYLOAD_SIZE;
    }
    if (end_off >= OA_TC6_CHUNK_PAYLOAD_SIZE) {
        end_off = OA_TC6_CHUNK_PAYLOAD_SIZE - 1U;
    }

    /* whole frame inside this chunk */
    if (start_valid && end_valid && (start_off <= end_off) && !tc6->rx_in_progress) {
        oa_tc6_rx_push(tc6, &chunk[start_off], (uint16_t) (end_off - start_off + 1U));
        return;
    }
    /* end of the previous frame and start of the next one in the same chunk */
    if (start_valid && end_valid && (start_off > end_off)) {
        if (tc6->rx_in_progress) {
            oa_tc6_rx_finish(tc6, chunk, (uint16_t) (end_off + 1U));
        }
        tc6->rx_in_progress = true;
        tc6->rx_frame_len = 0;
        oa_tc6_rx_append(tc6, &chunk[start_off], (uint16_t) (OA_TC6_CHUNK_PAYLOAD_SIZE - start_off));
        return;
    }
    /* frame start */
    if (start_valid && !end_valid) {
        if (tc6->rx_in_progress) {
            tc6->stat_rx_dropped++;
        }
        tc6->rx_in_progress = true;
        tc6->rx_frame_len = 0;
        oa_tc6_rx_append(tc6, &chunk[start_off], (uint16_t) (OA_TC6_CHUNK_PAYLOAD_SIZE - start_off));
        return;
    }
    /* frame end */
    if (!start_valid && end_valid) {
        oa_tc6_rx_finish(tc6, chunk, (uint16_t) (end_off + 1U));
        return;
    }
    /* middle of a frame */
    if (tc6->rx_in_progress) {
        oa_tc6_rx_append(tc6, chunk, OA_TC6_CHUNK_PAYLOAD_SIZE);
    }
}

static void oa_tc6_process_footer(oa_tc6_t *tc6, uint32_t footer)
{
    tc6->tx_credits = (uint8_t) OA_TC6_DATA_FTR_TX_CREDITS(footer);
    tc6->rx_chunks_available = (uint8_t) OA_TC6_DATA_FTR_RX_CHUNKS(footer);

    if ((footer & OA_TC6_DATA_FTR_EXTENDED_STS) != 0U) {
        uint32_t st = 0;
        if (oa_tc6_read_reg(tc6, OA_TC6_REG_STATUS0, &st) == 0) {
            tc6->stat_status0 = st;
            tc6->stat_ext_status++;
            (void) oa_tc6_write_reg(tc6, OA_TC6_REG_STATUS0, st);  /* clear */
        }
    }
    if ((footer & OA_TC6_DATA_FTR_RXD_HEADER_BAD) != 0U) {
        tc6->stat_hdr_bad++;
    }
    if ((footer & OA_TC6_DATA_FTR_CONFIG_SYNC) == 0U) {
        tc6->stat_cfg_sync_lost++;
    }
}

/**
 * One SPI data transfer.  data_tx[] already holds tx_chunks fully built chunks,
 * the remaining chunk slots are filled with empty (data invalid) headers so the
 * MAC-PHY can hand back pending receive chunks.
 */
static int oa_tc6_data_transfer(oa_tc6_t *tc6, uint16_t tx_chunks)
{
    uint16_t n = tx_chunks;
    uint32_t bytes;
    uint32_t empty_hdr;

    if (tc6->rx_chunks_available > n) {
        n = tc6->rx_chunks_available;
    }
    if (n > OA_TC6_MAX_CHUNKS) {
        n = OA_TC6_MAX_CHUNKS;
    }
    if (n == 0U) {
        n = 1U;    /* an empty chunk still returns buffer status in the footer */
        tx_chunks = 0;
        tc6->stat_empty_xfer++;
    }
    empty_hdr = oa_tc6_data_header(false, false, false, 0);
    for (uint16_t i = tx_chunks; i < n; i++) {
        be32_put(&tc6->data_tx[(uint32_t) i * OA_TC6_CHUNK_SIZE], empty_hdr);
    }
    bytes = (uint32_t) n * OA_TC6_CHUNK_SIZE;

    if (oa_tc6_spi_transfer(tc6, tc6->data_tx, tc6->data_rx, bytes) != 0) {
        return -1;
    }
    tc6->stat_data_xfer++;

    for (uint16_t i = 0; i < n; i++) {
        uint32_t footer = be32_get(&tc6->data_rx[(uint32_t) i * OA_TC6_CHUNK_SIZE + OA_TC6_CHUNK_PAYLOAD_SIZE]);
        oa_tc6_process_footer(tc6, footer);
        if ((footer & OA_TC6_DATA_FTR_DATA_VALID) != 0U) {
            oa_tc6_process_rx_payload(tc6, &tc6->data_rx[(uint32_t) i * OA_TC6_CHUNK_SIZE], footer);
        }
    }
    return 0;
}

int oa_tc6_poll_rx(oa_tc6_t *tc6)
{
    /* A data transfer is the only way to learn how many RX chunks the MAC-PHY
     * holds: the count comes back in the footer of a transfer.  Gating this on
     * tc6->rx_chunks_available deadlocks pure-receive traffic (no TX event would
     * refresh the count).  Callers throttle the rate; see oa_tc6_netif_poll(). */
    return oa_tc6_data_transfer(tc6, 0);
}

int oa_tc6_send_frame(oa_tc6_t *tc6, const uint8_t *frame, uint16_t len)
{
    uint16_t off = 0;
    uint32_t spin = 0;

    if ((len < 14U) || (len > 1518U)) {
        return -1;
    }
    while (off < len) {
        uint16_t remain;
        uint16_t need;
        uint16_t n;

        if (tc6->tx_credits == 0U) {
            if (oa_tc6_data_transfer(tc6, 0) != 0) {
                return -2;
            }
            if (tc6->tx_credits == 0U) {
                if (++spin > 20000U) {
                    return -3;   /* MAC-PHY does not hand out tx credits */
                }
                continue;
            }
            spin = 0;
        }
        remain = (uint16_t) (len - off);
        need = (uint16_t) ((remain + OA_TC6_CHUNK_PAYLOAD_SIZE - 1U) / OA_TC6_CHUNK_PAYLOAD_SIZE);
        n = (need < tc6->tx_credits) ? need : tc6->tx_credits;
        if (n > OA_TC6_MAX_CHUNKS) {
            n = OA_TC6_MAX_CHUNKS;
        }
        for (uint16_t i = 0; i < n; i++) {
            uint8_t *chunk = &tc6->data_tx[(uint32_t) i * OA_TC6_CHUNK_SIZE];
            uint16_t clen = (remain > OA_TC6_CHUNK_PAYLOAD_SIZE) ? OA_TC6_CHUNK_PAYLOAD_SIZE : remain;
            bool start_valid = (i == 0U) && (off == 0U) ;
            bool end_valid = ((uint32_t) off + clen == len);
            be32_put(chunk, oa_tc6_data_header(true, start_valid, end_valid,
                                               end_valid ? (uint8_t) (clen - 1U) : 0U));
            memcpy(&chunk[OA_TC6_DATA_HEADER_SIZE], &frame[off], clen);
            if (clen < OA_TC6_CHUNK_PAYLOAD_SIZE) {
                memset(&chunk[OA_TC6_DATA_HEADER_SIZE + clen], 0, OA_TC6_CHUNK_PAYLOAD_SIZE - clen);
            }
            off = (uint16_t) (off + clen);
            remain = (uint16_t) (remain - clen);
        }
        tc6->tx_credits = (uint8_t) (tc6->tx_credits - n);
        if (oa_tc6_data_transfer(tc6, n) != 0) {
            return -2;
        }
    }
    tc6->stat_tx_frames++;
    tc6->stat_tx_bytes += len;
    return 0;
}

uint16_t oa_tc6_rx_pop(oa_tc6_t *tc6, uint8_t *buf, uint16_t buf_size)
{
    uint16_t len;

    if (tc6->rx_q_cnt == 0U) {
        return 0;
    }
    len = tc6->rx_q_len[tc6->rx_q_tail];
    if (len > buf_size) {
        len = buf_size;
    }
    memcpy(buf, tc6->rx_q[tc6->rx_q_tail], len);
    tc6->rx_q_tail = (uint8_t) ((tc6->rx_q_tail + 1U) % OA_TC6_RX_QUEUE_DEPTH);
    tc6->rx_q_cnt--;
    return len;
}
