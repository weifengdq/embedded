/**
 * @file  lan8651.c
 * @brief LAN8651 10BASE-T1S MAC-PHY driver — OPEN Alliance TC6 SPI protocol
 *        Ported / adapted from Espressif ESP-IDF lan865x driver
 */
#include "lan8651.h"
#include "app_config.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Debug printf — routed through UART in main.c                      */
/* ------------------------------------------------------------------ */
extern int debug_printf(const char *fmt, ...);
#define DBG(...)  debug_printf(__VA_ARGS__)

#define DBG_IF(enabled, ...)    do { if ((enabled) != 0) DBG(__VA_ARGS__); } while (0)

volatile uint32_t g_lan8651_stage;
volatile uint32_t g_lan8651_detail;
volatile uint32_t g_lan8651_ctrl_echo_errors;
volatile uint32_t g_lan8651_ctrl_header_bad_errors;
volatile uint32_t g_lan8651_tx_header_bad_errors;
volatile uint32_t g_lan8651_tx_frame_drop_errors;
volatile uint32_t g_lan8651_rx_footer_parity_errors;
volatile uint32_t g_lan8651_rx_header_bad_errors;
volatile uint32_t g_lan8651_rx_frame_drop_errors;
volatile uint32_t g_lan8651_rx_unexpected_swo_errors;
volatile uint32_t g_lan8651_last_ctrl_echo;
volatile uint32_t g_lan8651_last_tx_ftr;
volatile uint32_t g_lan8651_last_rx_ftr;
volatile lan8651_rx_diag_t g_lan8651_rx_diag __attribute__((used));

static uint32_t g_ctrl_echo_log_count;
static uint32_t g_tx_footer_log_count;
static uint32_t g_rx_footer_log_count;

void LAN8651_ResetRxDiag(void)
{
    uint32_t next_session_seq = g_lan8651_rx_diag.session_seq + 1U;
    memset((void *)&g_lan8651_rx_diag, 0, sizeof(g_lan8651_rx_diag));
    g_lan8651_rx_diag.magic = 0x4C364458u;
    g_lan8651_rx_diag.session_seq = next_session_seq;
}

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */
static inline uint32_t bswap32(uint32_t v)
{
    return __builtin_bswap32(v);
}

static void log_limited_word(const char *tag, uint32_t *counter, uint32_t word)
{
#if !LAN8651_LOG_ERROR_DETAIL
    (void)tag;
    (void)counter;
    (void)word;
    return;
#else
    if (*counter < 12U) {
        DBG("%s 0x%08lX\r\n", tag, (unsigned long)word);
    }
    (*counter)++;
#endif
}

/** Compute parity for TC6 header / footer (odd parity over bits [31:1]) */
static bool tc6_parity(uint32_t v)
{
    v >>= 1;
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return !(v & 1U);
}

static inline void cs_low(lan8651_t *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
}

static inline void cs_high(lan8651_t *dev)
{
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
}

static inline void tc6_cs_delay(void)
{
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    __NOP();
}

/** Full-duplex SPI transfer with manual CS */
static HAL_StatusTypeDef spi_xfer(lan8651_t *dev, uint16_t len)
{
    uint32_t start_tick;

    cs_low(dev);
    tc6_cs_delay();

    HAL_StatusTypeDef st;
    if (len >= LAN8651_TX_BLOCK_SIZE) {
        st = HAL_SPI_TransmitReceive_DMA(dev->hspi, dev->tx_buf, dev->rx_buf, len);
        if (st == HAL_OK) {
            start_tick = HAL_GetTick();
            while (HAL_SPI_GetState(dev->hspi) != HAL_SPI_STATE_READY) {
                if ((HAL_GetTick() - start_tick) > 100U) {
                    HAL_SPI_Abort(dev->hspi);
                    st = HAL_TIMEOUT;
                    break;
                }
            }
            if (st == HAL_OK && dev->hspi->ErrorCode != HAL_SPI_ERROR_NONE) {
                st = HAL_ERROR;
            }
        }
    } else {
        st = HAL_SPI_TransmitReceive(dev->hspi, dev->tx_buf, dev->rx_buf, len, 100);
    }

    tc6_cs_delay();
    cs_high(dev);
    tc6_cs_delay();
    return st;
}

/* ================================================================== */
/*  Control transaction — register read / write                       */
/* ================================================================== */
static HAL_StatusTypeDef ctrl_transaction(lan8651_t *dev, bool write,
                                          uint8_t mms, uint16_t addr,
                                          uint32_t *value)
{
    /* Build control header (native / little-endian bit-field equivalent) */
    uint32_t hdr = 0;
    /* DNC=0 (control), AID=1 */
    hdr |= CTRL_HDR_AID;
    hdr |= ((uint32_t)mms  & 0x0F) << CTRL_HDR_MMS_SHIFT;
    hdr |= ((uint32_t)addr & 0xFFFF) << CTRL_HDR_ADDR_SHIFT;
    hdr |= (0U) << CTRL_HDR_LEN_SHIFT;   /* len = 0 means 1 register */
    if (write) hdr |= CTRL_HDR_WR;
    hdr |= tc6_parity(hdr) ? 1U : 0U;    /* parity in bit 0 */

    /*  TX layout: [header_be32(4)] [data_be32(4) if write] [pad…]
     *  RX layout: [dummy(4)] [echo_header(4)] [data_be32(4) if read]
     *  Total length = 4 (header) + 4 (dummy/echo) + 4 (data) = 12
     */
    uint16_t xfer_len = 12;
    memset(dev->tx_buf, 0, xfer_len);
    memset(dev->rx_buf, 0, xfer_len);

    /* Place header in big-endian */
    uint32_t hdr_be = bswap32(hdr);
    memcpy(&dev->tx_buf[0], &hdr_be, 4);

    if (write) {
        uint32_t data_be = bswap32(*value);
        memcpy(&dev->tx_buf[4], &data_be, 4);
    }

    HAL_StatusTypeDef st = spi_xfer(dev, xfer_len);
    if (st != HAL_OK) {
        g_lan8651_stage = 0xE001U;
        g_lan8651_detail = ((uint32_t)mms << 24) | ((uint32_t)addr << 8) | (write ? 1U : 0U);
        return st;
    }

    /* Check echoed header */
    uint32_t echo_be;
    memcpy(&echo_be, &dev->rx_buf[4], 4);
    uint32_t echo = bswap32(echo_be);

    /* Validate parity */
    bool p = tc6_parity(echo);
    if (p != (echo & 1U)) {
        g_lan8651_stage = 0xE002U;
        g_lan8651_detail = echo;
        g_lan8651_ctrl_echo_errors++;
        g_lan8651_last_ctrl_echo = echo;
        log_limited_word("LAN8651: ctrl echo parity error echo=", &g_ctrl_echo_log_count, echo);
    }
    /* Check HDRB (bit 30) */
    if (echo & (1U << 30)) {
        g_lan8651_stage = 0xE003U;
        g_lan8651_detail = echo;
        g_lan8651_ctrl_header_bad_errors++;
        g_lan8651_last_ctrl_echo = echo;
        log_limited_word("LAN8651: ctrl header bad echo=", &g_ctrl_echo_log_count, echo);
    }

    if (!write) {
        uint32_t data_be2;
        memcpy(&data_be2, &dev->rx_buf[8], 4);
        *value = bswap32(data_be2);
    }
    return HAL_OK;
}

HAL_StatusTypeDef LAN8651_ReadReg(lan8651_t *dev, uint8_t mms, uint16_t addr, uint32_t *value)
{
    return ctrl_transaction(dev, false, mms, addr, value);
}

HAL_StatusTypeDef LAN8651_WriteReg(lan8651_t *dev, uint8_t mms, uint16_t addr, uint32_t value)
{
    return ctrl_transaction(dev, true, mms, addr, &value);
}

static HAL_StatusTypeDef set_reg_bits(lan8651_t *dev, uint8_t mms, uint16_t addr, uint32_t bits)
{
    uint32_t v;
    HAL_StatusTypeDef st = LAN8651_ReadReg(dev, mms, addr, &v);
    if (st != HAL_OK) return st;
    v |= bits;
    return LAN8651_WriteReg(dev, mms, addr, v);
}

/* ================================================================== */
/*  Data transactions — TC6 frame TX / RX                             */
/* ================================================================== */

/** Build a 32-bit TX data header from individual fields */
static uint32_t build_tx_header(bool dv, bool sv, uint8_t swo,
                                bool ev, uint8_t ebo, bool norx, bool seq)
{
    uint32_t h = TX_HDR_DNC;  /* always data */
    if (seq)  h |= TX_HDR_SEQ;
    if (dv)   h |= TX_HDR_DV;
    if (sv)   h |= TX_HDR_SV;
    if (ev)   h |= TX_HDR_EV;
    if (norx) h |= TX_HDR_NORX;
    h |= ((uint32_t)(swo & 0x0F)) << TX_HDR_SWO_SHIFT;
    h |= ((uint32_t)(ebo & 0x3F)) << TX_HDR_EBO_SHIFT;
    h |= tc6_parity(h) ? 1U : 0U;
    return h;
}

static uint32_t build_data_header(bool dv, bool sv, uint8_t swo,
                                  bool ev, uint8_t ebo, bool norx)
{
    return build_tx_header(dv, sv, swo, ev, ebo, norx, false);
}

#if LAN8651_LOG_TX_STATUS
static void trace_tx_status_snapshot(lan8651_t *dev, const char *tag, uint16_t len)
{
    uint32_t mac_tsr = 0;
    uint32_t mac_nsr = 0;
    uint32_t mac_rsr = 0;
    uint32_t oa_bufsts = 0;
    uint32_t plca_sts = 0;
    bool ok = true;

    if (LAN8651_ReadReg(dev, LAN8651_MMS_MAC, MAC_TSR_REG, &mac_tsr) != HAL_OK) ok = false;
    if (LAN8651_ReadReg(dev, LAN8651_MMS_MAC, MAC_NSR_REG, &mac_nsr) != HAL_OK) ok = false;
    if (LAN8651_ReadReg(dev, LAN8651_MMS_MAC, MAC_RSR_REG, &mac_rsr) != HAL_OK) ok = false;
    if (LAN8651_ReadReg(dev, LAN8651_MMS_OA, OA_BUFSTS_REG, &oa_bufsts) != HAL_OK) ok = false;
    if (LAN8651_ReadReg(dev, LAN8651_MMS_PHY_VENDOR, PLCA_STS_REG, &plca_sts) != HAL_OK) ok = false;

    if (!ok) {
        DBG("LAN8651: TX status[%s] len=%u read failed\r\n", tag, len);
        return;
    }

    DBG("LAN8651: TX status[%s] len=%u TSR=0x%03lX txgo=%u txcomp=%u tfc=%u col=%u und=%u hresp=%u NSR=0x%02lX idle=%u RSR=0x%02lX rec=%u BUFSTS=0x%04lX txc=%u rba=%u PLCA=0x%04lX pst=%u\r\n",
        tag,
        len,
        (unsigned long)mac_tsr,
        (unsigned)((mac_tsr & MAC_TSR_TXGO) != 0U),
        (unsigned)((mac_tsr & MAC_TSR_TXCOMP) != 0U),
        (unsigned)((mac_tsr & MAC_TSR_TFC) != 0U),
        (unsigned)((mac_tsr & MAC_TSR_COL) != 0U),
        (unsigned)((mac_tsr & MAC_TSR_UND) != 0U),
        (unsigned)((mac_tsr & MAC_TSR_HRESP) != 0U),
        (unsigned long)mac_nsr,
        (unsigned)((mac_nsr & MAC_NSR_IDLE) != 0U),
        (unsigned long)mac_rsr,
        (unsigned)((mac_rsr & MAC_RSR_REC) != 0U),
        (unsigned long)oa_bufsts,
        (unsigned)((oa_bufsts & OA_BUFSTS_TXC_MASK) >> OA_BUFSTS_TXC_SHIFT),
        (unsigned)((oa_bufsts & OA_BUFSTS_RBA_MASK) >> OA_BUFSTS_RBA_SHIFT),
        (unsigned long)plca_sts,
        (unsigned)((plca_sts & PLCA_STS_PST) != 0U));
}
    #endif

HAL_StatusTypeDef LAN8651_Transmit(lan8651_t *dev, const uint8_t *frame, uint16_t len)
{
    if (len == 0 || len > LAN8651_MAX_FRAME_SIZE) return HAL_ERROR;

    int chunks = (len + LAN8651_DATA_BLOCK_SIZE - 1) / LAN8651_DATA_BLOCK_SIZE;

#if LAN8651_LOG_TX_STATUS
    trace_tx_status_snapshot(dev, "pre", len);
#endif

    for (int i = 0; i < chunks; i++) {
        bool is_first = (i == 0);
        bool is_last  = (i == chunks - 1);
        uint16_t offset = i * LAN8651_DATA_BLOCK_SIZE;
        uint16_t remaining = len - offset;
        uint16_t copy_len = is_last ? remaining : LAN8651_DATA_BLOCK_SIZE;

        uint8_t ebo = is_last ? (uint8_t)((len - 1) % LAN8651_DATA_BLOCK_SIZE) : 0;
        uint32_t hdr = build_data_header(true, is_first, 0, is_last, ebo, true);

        /* Fill TX buffer: [header(4)] [payload(64)] = 68 bytes */
        uint32_t hdr_be = bswap32(hdr);
        memcpy(&dev->tx_buf[0], &hdr_be, 4);
        memset(&dev->tx_buf[4], 0, LAN8651_DATA_BLOCK_SIZE);
        memcpy(&dev->tx_buf[4], frame + offset, copy_len);

        memset(dev->rx_buf, 0, LAN8651_TX_BLOCK_SIZE);

        HAL_StatusTypeDef st = spi_xfer(dev, LAN8651_TX_BLOCK_SIZE);
        if (st != HAL_OK) return st;

        /* Parse RX footer (last 4 bytes of the 68-byte RX block) */
        uint32_t ftr_be;
        memcpy(&ftr_be, &dev->rx_buf[LAN8651_DATA_BLOCK_SIZE], 4);
        uint32_t ftr = bswap32(ftr_be);

        DBG_IF(LAN8651_LOG_TX_TRACE,
               "LAN8651: TX chunk[%d/%d] hdr=0x%08lX ftr=0x%08lX txc=%u rca=%u\r\n",
               i + 1,
               chunks,
               (unsigned long)hdr,
               (unsigned long)ftr,
               (unsigned)(((ftr & RX_FTR_TXC_MASK) >> RX_FTR_TXC_SHIFT)),
               (unsigned)(((ftr & RX_FTR_RBA_MASK) >> RX_FTR_RBA_SHIFT)));

        if (ftr & RX_FTR_HDRB) {
            g_lan8651_tx_header_bad_errors++;
            g_lan8651_last_tx_ftr = ftr;
            log_limited_word("LAN8651: TX header bad ftr=", &g_tx_footer_log_count, ftr);
            return HAL_ERROR;
        }
        if (ftr & RX_FTR_FD) {
            g_lan8651_tx_frame_drop_errors++;
            g_lan8651_last_tx_ftr = ftr;
            log_limited_word("LAN8651: TX frame dropped ftr=", &g_tx_footer_log_count, ftr);
            return HAL_ERROR;
        }
    }

#if LAN8651_LOG_TX_STATUS
    trace_tx_status_snapshot(dev, "post0", len);
    HAL_Delay(1);
    trace_tx_status_snapshot(dev, "post1", len);
    HAL_Delay(1);
    trace_tx_status_snapshot(dev, "post2", len);
#endif

    return HAL_OK;
}

HAL_StatusTypeDef LAN8651_Receive(lan8651_t *dev, uint8_t *frame, uint16_t *len)
{
    *len = 0;
    dev->rx_frame_len = 0;
    bool start_found = false;
    uint32_t blocks_this_frame = 0U;

    g_lan8651_rx_diag.magic = 0x4C364458u;
    g_lan8651_rx_diag.receive_calls++;

    if (dev->rx_pending_frame_len > 0U) {
        memcpy(dev->rx_frame, dev->rx_pending_frame, dev->rx_pending_frame_len);
        dev->rx_frame_len = dev->rx_pending_frame_len;
        dev->rx_pending_frame_len = 0U;
        start_found = true;
    }

    /* Read blocks until we get a complete frame or no more data */
    for (;;) {
        /* Send an empty data header to clock out RX data */
        uint32_t hdr = build_data_header(false, false, 0, false, 0, false);
        uint32_t hdr_be = bswap32(hdr);
        memset(dev->tx_buf, 0, LAN8651_TX_BLOCK_SIZE);
        memcpy(&dev->tx_buf[0], &hdr_be, 4);
        memset(dev->rx_buf, 0, LAN8651_TX_BLOCK_SIZE);

        HAL_StatusTypeDef st = spi_xfer(dev, LAN8651_TX_BLOCK_SIZE);
        if (st != HAL_OK) return st;
    g_lan8651_rx_diag.blocks_total++;
    blocks_this_frame++;

        /* Footer is at the end of the 68-byte RX block */
        uint32_t ftr_be;
        memcpy(&ftr_be, &dev->rx_buf[LAN8651_DATA_BLOCK_SIZE], 4);
        uint32_t ftr = bswap32(ftr_be);

        bool p = tc6_parity(ftr);
        if (p != (ftr & 1U)) {
            g_lan8651_rx_footer_parity_errors++;
            g_lan8651_last_rx_ftr = ftr;
            log_limited_word("LAN8651: RX footer parity error ftr=", &g_rx_footer_log_count, ftr);
            return HAL_ERROR;
        }
        if (ftr & RX_FTR_HDRB) {
            g_lan8651_rx_header_bad_errors++;
            g_lan8651_last_rx_ftr = ftr;
            log_limited_word("LAN8651: RX header bad ftr=", &g_rx_footer_log_count, ftr);
            return HAL_ERROR;
        }
        if (ftr & RX_FTR_FD) {
            g_lan8651_rx_frame_drop_errors++;
            g_lan8651_last_rx_ftr = ftr;
            log_limited_word("LAN8651: RX frame dropped ftr=", &g_rx_footer_log_count, ftr);
            return HAL_ERROR;
        }

        bool dv = !!(ftr & RX_FTR_DV);
        bool sv = !!(ftr & RX_FTR_SV);
        bool ev = !!(ftr & RX_FTR_EV);
        /* Footer bits[28:24] report available RX chunks for the host to read. */
        uint8_t rx_chunks_available = (uint8_t)((ftr & RX_FTR_RBA_MASK) >> RX_FTR_RBA_SHIFT);
        g_lan8651_rx_diag.last_rba = rx_chunks_available;
        uint8_t swo = (uint8_t)((ftr & RX_FTR_SWO_MASK) >> RX_FTR_SWO_SHIFT);
        uint8_t sbo = sv ? (uint8_t)(swo * 4U) : 0U;
        uint8_t ebo = ev ? (uint8_t)(((ftr & RX_FTR_EBO_MASK) >> RX_FTR_EBO_SHIFT) + 1U)
                         : LAN8651_DATA_BLOCK_SIZE;
        bool two_frames = (ebo <= sbo);

        if (!dv) {
            if (rx_chunks_available != 0U) {
                g_lan8651_rx_diag.no_dv_with_rca++;
                DBG_IF(LAN8651_LOG_RX_EMPTY,
                       "LAN8651: RX no DV but RCA=%u ftr=0x%08lX\r\n",
                       rx_chunks_available,
                       (unsigned long)ftr);
                continue;
            }
            g_lan8651_rx_diag.empty_polls++;
            break;  /* No more data */
        }

        g_lan8651_rx_diag.dv_blocks++;

        if (!start_found) {
            if (!sv) {
                continue;  /* Skip until frame start */
            }

            if (two_frames) {
                uint16_t copy_len = (uint16_t)(LAN8651_DATA_BLOCK_SIZE - sbo);
                if (copy_len == 0U || copy_len > LAN8651_MAX_FRAME_SIZE) {
                    DBG("LAN8651: RX invalid two-frame start ftr=0x%08lX\r\n", (unsigned long)ftr);
                    return HAL_ERROR;
                }

                memcpy(dev->rx_frame, &dev->rx_buf[sbo], copy_len);
                dev->rx_frame_len = copy_len;
                start_found = true;
                continue;
            }

            if (sbo != 0U) {
                g_lan8651_rx_unexpected_swo_errors++;
                g_lan8651_last_rx_ftr = ftr;
                log_limited_word("LAN8651: RX unexpected SWO ftr=", &g_rx_footer_log_count, ftr);
                continue;
            }

            start_found = true;
        }

        uint16_t copy_offset = 0U;
        uint16_t copy_len;
        if (two_frames) {
            copy_len = ebo;
        } else {
            copy_offset = sbo;
            copy_len = (uint16_t)(ebo - sbo);
        }

        if (dev->rx_frame_len + copy_len > LAN8651_MAX_FRAME_SIZE) {
            DBG("LAN8651: RX frame too large\r\n");
            return HAL_ERROR;
        }

        memcpy(dev->rx_frame + dev->rx_frame_len, &dev->rx_buf[copy_offset], copy_len);
        dev->rx_frame_len += copy_len;

        if (two_frames && sv) {
            uint16_t pending_len = (uint16_t)(LAN8651_DATA_BLOCK_SIZE - sbo);
            if (pending_len == 0U || pending_len > LAN8651_MAX_FRAME_SIZE) {
                DBG("LAN8651: RX invalid pending frame start ftr=0x%08lX\r\n", (unsigned long)ftr);
                return HAL_ERROR;
            }

            memcpy(dev->rx_pending_frame, &dev->rx_buf[sbo], pending_len);
            dev->rx_pending_frame_len = pending_len;
        }

        if (two_frames || ev) {
            break;  /* Current frame complete */
        }
    }

    if (dev->rx_frame_len > 0) {
        memcpy(frame, dev->rx_frame, dev->rx_frame_len);
        *len = dev->rx_frame_len;
        g_lan8651_rx_diag.frames_returned++;
        g_lan8651_rx_diag.frame_bytes_total += dev->rx_frame_len;
        g_lan8651_rx_diag.last_frame_len = dev->rx_frame_len;
        if (dev->rx_frame_len > g_lan8651_rx_diag.max_frame_len) {
            g_lan8651_rx_diag.max_frame_len = dev->rx_frame_len;
        }
        g_lan8651_rx_diag.last_blocks_per_frame = blocks_this_frame;
        if (blocks_this_frame > g_lan8651_rx_diag.max_blocks_per_frame) {
            g_lan8651_rx_diag.max_blocks_per_frame = blocks_this_frame;
        }
        return HAL_OK;
    }
    return HAL_ERROR;  /* No frame */
}

uint8_t LAN8651_GetRxBlocksAvailable(lan8651_t *dev)
{
    uint32_t bufsts = 0;
    if (LAN8651_ReadReg(dev, LAN8651_MMS_OA, OA_BUFSTS_REG, &bufsts) != HAL_OK)
        return 0;
    return (uint8_t)((bufsts & OA_BUFSTS_RBA_MASK) >> OA_BUFSTS_RBA_SHIFT);
}

/* ================================================================== */
/*  Indirect register read (for AN1760 calibration values)            */
/* ================================================================== */
static HAL_StatusTypeDef indirect_read(lan8651_t *dev, uint8_t addr, uint8_t mask, uint8_t *val)
{
    HAL_StatusTypeDef st;
    st = LAN8651_WriteReg(dev, LAN8651_MMS_PHY_VENDOR, 0x00D8, addr);
    if (st != HAL_OK) return st;
    st = LAN8651_WriteReg(dev, LAN8651_MMS_PHY_VENDOR, 0x00DA, 0x02);
    if (st != HAL_OK) return st;
    uint32_t rv = 0;
    st = LAN8651_ReadReg(dev, LAN8651_MMS_PHY_VENDOR, 0x00D9, &rv);
    if (st != HAL_OK) return st;
    *val = (uint8_t)(rv & mask);
    return HAL_OK;
}

static HAL_StatusTypeDef read_reg16(lan8651_t *dev, uint8_t mms, uint16_t addr, uint16_t *value)
{
    uint32_t raw = 0;
    HAL_StatusTypeDef st = LAN8651_ReadReg(dev, mms, addr, &raw);
    if (st != HAL_OK) return st;
    *value = (uint16_t)(raw & 0xFFFFU);
    return HAL_OK;
}

static HAL_StatusTypeDef configure_plca(lan8651_t *dev)
{
    HAL_StatusTypeDef st;
    uint32_t plca_ctrl1 = ((uint32_t)PLCA_NODE_COUNT_DEFAULT << PLCA_CTRL1_NCNT_SHIFT) |
                          PLCA_LOCAL_ID_DEFAULT;

    st = LAN8651_WriteReg(dev, LAN8651_MMS_PHY_VENDOR, COL_DET_CTRL0_REG, COL_DET_CTRL0_PLCA);
    if (st != HAL_OK) return st;

    st = LAN8651_WriteReg(dev, LAN8651_MMS_PHY_VENDOR, PLCA_TOTMR_REG, PLCA_TOTMR_DEFAULT);
    if (st != HAL_OK) return st;

    st = LAN8651_WriteReg(dev, LAN8651_MMS_PHY_VENDOR, PLCA_BURST_REG, PLCA_BURST_DEFAULT);
    if (st != HAL_OK) return st;

    st = LAN8651_WriteReg(dev, LAN8651_MMS_PHY_VENDOR, PLCA_CTRL1_REG, plca_ctrl1);
    if (st != HAL_OK) return st;

    return LAN8651_WriteReg(dev, LAN8651_MMS_PHY_VENDOR, PLCA_CTRL0_REG, PLCA_CTRL0_EN);
}

/* ================================================================== */
/*  AN1760 manufacturer default configuration                        */
/* ================================================================== */
static HAL_StatusTypeDef default_config(lan8651_t *dev)
{
    uint8_t value1, value2;
    int8_t offset1, offset2;
    uint16_t value3, value4, value5, value6, value7;
    uint16_t cfgparam1, cfgparam2, cfgparam3, cfgparam4, cfgparam5;

    if (indirect_read(dev, 0x04, 0x1F, &value1) != HAL_OK) return HAL_ERROR;
    offset1 = (value1 & 0x10) ? (int8_t)(value1 - 0x20) : (int8_t)value1;

    if (indirect_read(dev, 0x08, 0x1F, &value2) != HAL_OK) return HAL_ERROR;
    offset2 = (value2 & 0x10) ? (int8_t)(value2 - 0x20) : (int8_t)value2;

    if (read_reg16(dev, LAN8651_MMS_PHY_VENDOR, 0x0084, &value3) != HAL_OK) return HAL_ERROR;
    if (read_reg16(dev, LAN8651_MMS_PHY_VENDOR, 0x008A, &value4) != HAL_OK) return HAL_ERROR;
    if (read_reg16(dev, LAN8651_MMS_PHY_VENDOR, 0x00AD, &value5) != HAL_OK) return HAL_ERROR;
    if (read_reg16(dev, LAN8651_MMS_PHY_VENDOR, 0x00AE, &value6) != HAL_OK) return HAL_ERROR;
    if (read_reg16(dev, LAN8651_MMS_PHY_VENDOR, 0x00AF, &value7) != HAL_OK) return HAL_ERROR;

    cfgparam1 = (uint16_t)((value3 & 0x000FU) |
                ((((uint16_t)(9 + offset1)) & 0x003FU) << 10) |
                ((((uint16_t)(14 + offset1)) & 0x003FU) << 4));
    cfgparam2 = (uint16_t)((value4 & 0x03FFU) |
                ((((uint16_t)(40 + offset2)) & 0x003FU) << 10));
    cfgparam3 = (uint16_t)((value5 & 0xC0C0U) |
                ((((uint16_t)(5 + offset1)) & 0x003FU) << 8) |
                (((uint16_t)(9 + offset1)) & 0x003FU));
    cfgparam4 = (uint16_t)((value6 & 0xC0C0U) |
                ((((uint16_t)(9 + offset1)) & 0x003FU) << 8) |
                (((uint16_t)(14 + offset1)) & 0x003FU));
    cfgparam5 = (uint16_t)((value7 & 0xC0C0U) |
                ((((uint16_t)(17 + offset1)) & 0x003FU) << 8) |
                (((uint16_t)(22 + offset1)) & 0x003FU));

    /* Table aligned with the reference LAN8651 rev.B1 configuration flow */
    struct { uint8_t mms; uint16_t addr; uint32_t val; } cfg[] = {
        {0x4, 0x0091, 0x9660},
        {0x4, 0x0081, 0x00C0},
        {0x1, 0x0077, 0x0028},
        {0x4, 0x0043, 0x00FF},
        {0x4, 0x0044, 0xFFFF},
        {0x4, 0x0045, 0x0000},
        {0x4, 0x0053, 0x00FF},
        {0x4, 0x0054, 0xFFFF},
        {0x4, 0x0055, 0x0000},
        {0x4, 0x0040, 0x0002},
        {0x4, 0x0050, 0x0002},
        {0x4, 0x00D0, 0x5F21},
        {0x4, 0x0084, cfgparam1},
        {0x4, 0x008A, cfgparam2},
        {0x4, 0x00E9, 0x9E50},
        {0x4, 0x00F5, 0x1CF8},
        {0x4, 0x00F4, 0xC020},
        {0x4, 0x00F8, 0x9B00},
        {0x4, 0x00F9, 0x4E53},
        {0x4, 0x00AD, cfgparam3},
        {0x4, 0x00AE, cfgparam4},
        {0x4, 0x00AF, cfgparam5},
        {0x4, 0x00B0, 0x0103},
        {0x4, 0x00B1, 0x0910},
        {0x4, 0x00B2, 0x1D26},
        {0x4, 0x00B3, 0x002A},
        {0x4, 0x00B4, 0x0103},
        {0x4, 0x00B5, 0x070D},
        {0x4, 0x00B6, 0x1720},
        {0x4, 0x00B7, 0x0027},
        {0x4, 0x00B8, 0x0509},
        {0x4, 0x00B9, 0x0E13},
        {0x4, 0x00BA, 0x1C25},
        {0x4, 0x00BB, 0x002B},
    };

    for (unsigned i = 0; i < sizeof(cfg)/sizeof(cfg[0]); i++) {
        if (LAN8651_WriteReg(dev, cfg[i].mms, cfg[i].addr, cfg[i].val) != HAL_OK) {
            DBG("LAN8651: AN1760 cfg[%u] failed\r\n", i);
            return HAL_ERROR;
        }
    }
    return HAL_OK;
}

/* ================================================================== */
/*  Init                                                              */
/* ================================================================== */
HAL_StatusTypeDef LAN8651_Init(lan8651_t *dev)
{
    HAL_StatusTypeDef st;

    g_lan8651_stage = 0x1000U;
    g_lan8651_detail = 0U;
    dev->seq_num = 0;

    /* ----- Hardware reset ----- */
    g_lan8651_stage = 0x1100U;
    HAL_GPIO_WritePin(dev->rst_port, dev->rst_pin, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(dev->rst_port, dev->rst_pin, GPIO_PIN_SET);
    HAL_Delay(50);

    /* ----- Software reset ----- */
    g_lan8651_stage = 0x1200U;
    st = LAN8651_WriteReg(dev, LAN8651_MMS_OA, OA_RESET_REG, 0x01);
    if (st != HAL_OK) { g_lan8651_stage = 0x12EEU; DBG("LAN8651: SW reset write failed\r\n"); return st; }

    /* Wait for reset to clear */
    g_lan8651_stage = 0x1300U;
    for (int i = 0; i < 20; i++) {
        HAL_Delay(10);
        uint32_t v = 0;
        LAN8651_ReadReg(dev, LAN8651_MMS_OA, OA_RESET_REG, &v);
        if ((v & 0x01) == 0) break;
    }

    /* Wait for RESETC in STATUS0 */
    g_lan8651_stage = 0x1400U;
    for (int i = 0; i < 20; i++) {
        HAL_Delay(10);
        uint32_t v = 0;
        LAN8651_ReadReg(dev, LAN8651_MMS_OA, OA_STATUS0_REG, &v);
        if (v & OA_STATUS0_RESETC) {
            /* Clear RESETC */
            LAN8651_WriteReg(dev, LAN8651_MMS_OA, OA_STATUS0_REG, v);
            break;
        }
    }

    HAL_Delay(100);

    /* ----- Verify chip ID ----- */
    g_lan8651_stage = 0x1500U;
    uint32_t devid = 0;
    uint16_t model = 0;
    uint8_t rev = 0;

    for (int attempt = 0; attempt < 3; ++attempt) {
        st = LAN8651_ReadReg(dev, LAN8651_MMS_MISC, MISC_DEVID_REG, &devid);
        if (st != HAL_OK) {
            HAL_Delay(2);
            continue;
        }

        model = (devid >> 4) & 0xFFFF;
        rev = devid & 0x0F;
        if (devid == 0U || model == 0x8650U || model == 0x8651U) {
            break;
        }

        HAL_Delay(2);
    }

    if (st != HAL_OK) {
        g_lan8651_stage = 0x15E0U;
        DBG("LAN8651: DEVID read failed, continue with fallback path\r\n");
        devid = 0;
        model = 0;
        rev = 0;
    }

    DBG_IF(LAN8651_LOG_INFO,
           "LAN8651: DEVID=0x%08lX  model=0x%04X  rev=%u\r\n",
           (unsigned long)devid, model, rev);

    if (devid != 0U && model != 0x8650 && model != 0x8651) {
        g_lan8651_stage = 0x15EFU;
        g_lan8651_detail = devid;
        DBG("LAN8651: unexpected model, continue\r\n");
    }

    /* ----- AN1760 / reference default configuration ----- */
    g_lan8651_stage = 0x1600U;
    st = default_config(dev);
    if (st != HAL_OK) { g_lan8651_stage = 0x16EEU; DBG("LAN8651: AN1760 config failed\r\n"); return st; }

    g_lan8651_stage = 0x1700U;
    st = configure_plca(dev);
    if (st != HAL_OK) { g_lan8651_stage = 0x17EEU; DBG("LAN8651: PLCA config failed\r\n"); return st; }

    /* ----- MAC configuration ----- */
    g_lan8651_stage = 0x1800U;
    st = set_reg_bits(dev, LAN8651_MMS_MAC, MAC_NCFGR_REG,
                      MAC_NCFGR_MTIHEN | MAC_NCFGR_RFCS | MAC_NCFGR_EFRHD);
    if (st != HAL_OK) { g_lan8651_stage = 0x18EEU; return st; }

    /* ----- OA_CONFIG0 ----- */
    g_lan8651_stage = 0x1900U;
    uint32_t config0 = OA_CONFIG0_SYNC |
                       OA_CONFIG0_RFA_ZARFE |
                       OA_CONFIG0_BPS_64;
    st = LAN8651_WriteReg(dev, LAN8651_MMS_OA, OA_CONFIG0_REG, config0);
    if (st != HAL_OK) { g_lan8651_stage = 0x19EEU; return st; }

    /* ----- Set MAC address ----- */
    g_lan8651_stage = 0x1A00U;
    st = LAN8651_SetMACAddress(dev, dev->mac_addr);
    if (st != HAL_OK) { g_lan8651_stage = 0x1AEEU; return st; }

    uint32_t sab1 = 0;
    uint32_t sat1 = 0;
    uint32_t sab2 = 0;
    uint32_t sat2 = 0;
    if (LAN8651_ReadReg(dev, LAN8651_MMS_MAC, MAC_SAB1_REG, &sab1) == HAL_OK &&
        LAN8651_ReadReg(dev, LAN8651_MMS_MAC, MAC_SAT1_REG, &sat1) == HAL_OK &&
        LAN8651_ReadReg(dev, LAN8651_MMS_MAC, MAC_SAB2_REG, &sab2) == HAL_OK &&
        LAN8651_ReadReg(dev, LAN8651_MMS_MAC, MAC_SAT2_REG, &sat2) == HAL_OK) {
        DBG_IF(LAN8651_LOG_INFO,
               "LAN8651: MAC_SA1=%08lX:%04lX MAC_SA2=%08lX:%04lX\r\n",
               (unsigned long)sab1,
               (unsigned long)(sat1 & 0xFFFFU),
               (unsigned long)sab2,
               (unsigned long)(sat2 & 0xFFFFU));
    }

    uint32_t plca_ctrl0 = 0;
    uint32_t plca_ctrl1 = 0;
    uint32_t plca_totmr = 0;
    uint32_t plca_burst = 0;
    uint32_t plca_status = 0;
    if (LAN8651_ReadReg(dev, LAN8651_MMS_PHY_VENDOR, PLCA_CTRL0_REG, &plca_ctrl0) == HAL_OK &&
        LAN8651_ReadReg(dev, LAN8651_MMS_PHY_VENDOR, PLCA_CTRL1_REG, &plca_ctrl1) == HAL_OK &&
        LAN8651_ReadReg(dev, LAN8651_MMS_PHY_VENDOR, PLCA_TOTMR_REG, &plca_totmr) == HAL_OK &&
        LAN8651_ReadReg(dev, LAN8651_MMS_PHY_VENDOR, PLCA_BURST_REG, &plca_burst) == HAL_OK &&
        LAN8651_ReadReg(dev, LAN8651_MMS_PHY_VENDOR, PLCA_STS_REG, &plca_status) == HAL_OK) {
        DBG_IF(LAN8651_LOG_INFO,
               "LAN8651: PLCA_CTRL0=0x%04lX CTRL1=0x%04lX TOTMR=0x%04lX BURST=0x%04lX STS=0x%04lX pst=%u\r\n",
               (unsigned long)(plca_ctrl0 & 0xFFFFU),
               (unsigned long)(plca_ctrl1 & 0xFFFFU),
               (unsigned long)(plca_totmr & 0xFFFFU),
               (unsigned long)(plca_burst & 0xFFFFU),
               (unsigned long)(plca_status & 0xFFFFU),
               (plca_status & PLCA_STS_PST) ? 1U : 0U);
    }

    uint32_t mac_ncfgr = 0;
    if (LAN8651_ReadReg(dev, LAN8651_MMS_MAC, MAC_NCFGR_REG, &mac_ncfgr) == HAL_OK) {
        DBG_IF(LAN8651_LOG_INFO, "LAN8651: MAC_NCFGR=0x%08lX\r\n", (unsigned long)mac_ncfgr);
    }

    g_lan8651_stage = 0x1FFFU;
    DBG_IF(LAN8651_LOG_INFO, "LAN8651: init OK\r\n");
    return HAL_OK;
}

HAL_StatusTypeDef LAN8651_SetMACAddress(lan8651_t *dev, const uint8_t mac[6])
{
    HAL_StatusTypeDef st;
    memcpy(dev->mac_addr, mac, 6);

    /* SAB1 / SAT1 use a rotated address pattern for back-off timing. */
    uint32_t sab1 = mac[2] | (mac[3] << 8) | (mac[4] << 16) | (mac[5] << 24);
    uint32_t sat1 = mac[0] | (mac[1] << 8);
    st = LAN8651_WriteReg(dev, LAN8651_MMS_MAC, MAC_SAB1_REG, sab1);
    if (st != HAL_OK) return st;
    st = LAN8651_WriteReg(dev, LAN8651_MMS_MAC, MAC_SAT1_REG, sat1);
    if (st != HAL_OK) return st;

    /* SAB2 / SAT2 — used for RX filtering */
    uint32_t sab2 = mac[0] | (mac[1] << 8) | (mac[2] << 16) | (mac[3] << 24);
    uint32_t sat2 = mac[4] | (mac[5] << 8);
    st = LAN8651_WriteReg(dev, LAN8651_MMS_MAC, MAC_SAB2_REG, sab2);
    if (st != HAL_OK) return st;
    st = LAN8651_WriteReg(dev, LAN8651_MMS_MAC, MAC_SAT2_REG, sat2);
    return st;
}

HAL_StatusTypeDef LAN8651_Start(lan8651_t *dev)
{
    HAL_StatusTypeDef st = set_reg_bits(dev, LAN8651_MMS_MAC, MAC_NCR_REG,
                                        MAC_NCR_TXEN | MAC_NCR_RXEN);
    if (st != HAL_OK) return st;

    uint32_t mac_ncr = 0;
    st = LAN8651_ReadReg(dev, LAN8651_MMS_MAC, MAC_NCR_REG, &mac_ncr);
    if (st != HAL_OK) return st;

    if ((mac_ncr & (MAC_NCR_TXEN | MAC_NCR_RXEN)) != (MAC_NCR_TXEN | MAC_NCR_RXEN)) {
        st = LAN8651_WriteReg(dev, LAN8651_MMS_MAC, MAC_NCR_REG, MAC_NCR_TXEN | MAC_NCR_RXEN);
        if (st != HAL_OK) return st;

        st = LAN8651_ReadReg(dev, LAN8651_MMS_MAC, MAC_NCR_REG, &mac_ncr);
        if (st != HAL_OK) return st;
    }

    DBG_IF(LAN8651_LOG_INFO, "LAN8651: MAC_NCR=0x%08lX after start\r\n", (unsigned long)mac_ncr);
    return HAL_OK;
}

HAL_StatusTypeDef LAN8651_Stop(lan8651_t *dev)
{
    uint32_t v;
    HAL_StatusTypeDef st = LAN8651_ReadReg(dev, LAN8651_MMS_MAC, MAC_NCR_REG, &v);
    if (st != HAL_OK) return st;
    v &= ~(MAC_NCR_TXEN | MAC_NCR_RXEN);
    return LAN8651_WriteReg(dev, LAN8651_MMS_MAC, MAC_NCR_REG, v);
}
