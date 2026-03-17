#include "lan8651.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"

#define LAN8651_LOG_RAW_SPI APP_LAN8651_RAW_SPI_LOG_ENABLE

enum {
    kLan8651CtrlTransactionTimeoutUs = 5000,
    kLan8651DataTransactionTimeoutUs = 10000,
    kLan8651ResetDelayMs = 10,
    kLan8651PostResetDelayMs = 50,
    kLan8651CsGuardDelayUs = 1,
};

static volatile bool s_spi_tx_dma_done;
static volatile bool s_spi_rx_dma_done;
static uint32_t s_tx_footer_log_count;
static uint32_t s_tx_raw_log_count;
static uint32_t s_rx_raw_log_count;

static inline uint32_t lan8651_bswap32(uint32_t value)
{
    return ((value & 0x000000FFUL) << 24) |
           ((value & 0x0000FF00UL) << 8) |
           ((value & 0x00FF0000UL) >> 8) |
           ((value & 0xFF000000UL) >> 24);
}

static bool lan8651_tc6_parity(uint32_t value)
{
    value >>= 1;
    value ^= value >> 16;
    value ^= value >> 8;
    value ^= value >> 4;
    value ^= value >> 2;
    value ^= value >> 1;
    return (value & 1U) == 0U;
}

static void lan8651_log_raw_block(const char *tag, uint32_t index, const uint8_t *data, uint16_t len)
{
    printf("%s[%lu]: len=%u\n", tag, (unsigned long)index, len);
    for (uint16_t offset = 0U; offset < len; offset += 16U) {
        uint16_t chunk = (uint16_t)((len - offset) > 16U ? 16U : (len - offset));

        printf("%s[%lu] @%02u:", tag, (unsigned long)index, offset);
        for (uint16_t i = 0U; i < chunk; i++) {
            printf(" %02X", data[offset + i]);
        }
        printf("\n");
    }
}

static uint32_t lan8651_make_ctrl_header(bool write, uint32_t reg)
{
    uint8_t mms = (uint8_t)((reg >> 16) & 0x0FU);
    uint16_t addr = (uint16_t)(reg & 0xFFFFU);
    uint32_t header = 0;

    header |= LAN8651_TC6_CTRL_HDR_AID;
    if (write) {
        header |= LAN8651_TC6_CTRL_HDR_WNR;
    }
    header |= ((uint32_t)mms) << LAN8651_TC6_CTRL_HDR_MMS_SHIFT;
    header |= ((uint32_t)addr) << LAN8651_TC6_CTRL_HDR_ADDR_SHIFT;
    header |= (0U << LAN8651_TC6_CTRL_HDR_LEN_SHIFT);
    header |= lan8651_tc6_parity(header) ? 1U : 0U;

    return header;
}

static uint32_t lan8651_make_data_header(bool start_valid, uint16_t start_offset, bool end_valid,
    uint16_t end_offset, uint8_t tx_chunks)
{
    uint32_t header = 0;

    header |= LAN8651_TC6_DATA_HDR_DNC;
    if (tx_chunks != 0U) {
        header |= LAN8651_TC6_DATA_HDR_DV;
        header |= LAN8651_TC6_DATA_HDR_NORX;
    }
    if (start_valid) {
        header |= LAN8651_TC6_DATA_HDR_SV;
        header |= ((uint32_t)(start_offset / 4U) & 0x0FU) << LAN8651_TC6_DATA_HDR_SWO_SHIFT;
    }
    if (end_valid) {
        header |= LAN8651_TC6_DATA_HDR_EV;
        header |= ((uint32_t)end_offset & 0x3FU) << LAN8651_TC6_DATA_HDR_EBO_SHIFT;
    }
    header |= lan8651_tc6_parity(header) ? 1U : 0U;

    return header;
}

static lan8651_status_t lan8651_indirect_read(lan8651_t *dev, uint8_t addr, uint8_t mask, uint8_t *value)
{
    uint32_t reg_value;

    if (lan8651_write_reg(dev, LAN8651_VENDOR_REG_D8, addr) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }
    if (lan8651_write_reg(dev, LAN8651_VENDOR_REG_DA, 0x02U) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }
    if (lan8651_read_reg(dev, LAN8651_VENDOR_REG_D9, &reg_value) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }

    *value = (uint8_t)(reg_value & mask);
    return kLan8651Status_Ok;
}

static lan8651_status_t lan8651_read_reg16(lan8651_t *dev, uint32_t reg, uint16_t *value)
{
    uint32_t reg_value;
    lan8651_status_t status = lan8651_read_reg(dev, reg, &reg_value);

    if (status != kLan8651Status_Ok) {
        return status;
    }

    *value = (uint16_t)(reg_value & 0xFFFFU);
    return kLan8651Status_Ok;
}

static void lan8651_gpio_write(GPIO_Type *gpio, uint32_t pin, bool level)
{
    gpio_write_pin(gpio, GPIO_GET_PORT_INDEX(pin), GPIO_GET_PIN_INDEX(pin), level ? 1U : 0U);
}

static bool lan8651_wait_for_dma(uint32_t timeout_us)
{
    while (timeout_us-- > 0U) {
        if (s_spi_tx_dma_done && s_spi_rx_dma_done) {
            return true;
        }
        board_delay_us(1U);
    }
    return false;
}

static lan8651_status_t lan8651_spi_transfer(lan8651_t *dev, size_t len, uint32_t timeout_us)
{
    hpm_stat_t stat;

    board_write_spi_cs(dev->cs_pin, BOARD_SPI_CS_ACTIVE_LEVEL);
    board_delay_us(kLan8651CsGuardDelayUs);

    if (len <= LAN8651_TC6_CTRL_BUF_SIZE) {
        stat = hpm_spi_transmit_receive_blocking(dev->spi, dev->tx_buf, dev->rx_buf, len, 100U);
        if (stat != status_success) {
            board_delay_us(kLan8651CsGuardDelayUs);
            board_write_spi_cs(dev->cs_pin, !BOARD_SPI_CS_ACTIVE_LEVEL);
            board_delay_us(kLan8651CsGuardDelayUs);
            return kLan8651Status_NotReady;
        }

        if (spi_wait_for_idle_status(dev->spi) != status_success) {
            board_delay_us(kLan8651CsGuardDelayUs);
            board_write_spi_cs(dev->cs_pin, !BOARD_SPI_CS_ACTIVE_LEVEL);
            board_delay_us(kLan8651CsGuardDelayUs);
            return kLan8651Status_Timeout;
        }
    } else {
        s_spi_tx_dma_done = false;
        s_spi_rx_dma_done = false;

        stat = hpm_spi_transmit_receive_nonblocking(dev->spi, dev->tx_buf, dev->rx_buf, len);
        if (stat != status_success) {
            board_delay_us(kLan8651CsGuardDelayUs);
            board_write_spi_cs(dev->cs_pin, !BOARD_SPI_CS_ACTIVE_LEVEL);
            board_delay_us(kLan8651CsGuardDelayUs);
            return kLan8651Status_NotReady;
        }

        if (!lan8651_wait_for_dma(timeout_us)) {
            board_delay_us(kLan8651CsGuardDelayUs);
            board_write_spi_cs(dev->cs_pin, !BOARD_SPI_CS_ACTIVE_LEVEL);
            board_delay_us(kLan8651CsGuardDelayUs);
            return kLan8651Status_Timeout;
        }

        if (spi_wait_for_idle_status(dev->spi) != status_success) {
            board_delay_us(kLan8651CsGuardDelayUs);
            board_write_spi_cs(dev->cs_pin, !BOARD_SPI_CS_ACTIVE_LEVEL);
            board_delay_us(kLan8651CsGuardDelayUs);
            return kLan8651Status_Timeout;
        }
    }

    board_delay_us(kLan8651CsGuardDelayUs);
    board_write_spi_cs(dev->cs_pin, !BOARD_SPI_CS_ACTIVE_LEVEL);
    board_delay_us(kLan8651CsGuardDelayUs);
    return kLan8651Status_Ok;
}

static lan8651_status_t lan8651_ctrl_transaction(lan8651_t *dev, bool write, uint32_t reg, uint32_t *value)
{
    uint32_t header = lan8651_make_ctrl_header(write, reg);
    uint32_t response;
    lan8651_status_t status;

    memset(dev->tx_buf, 0, LAN8651_TC6_CTRL_BUF_SIZE);
    memset(dev->rx_buf, 0, LAN8651_TC6_CTRL_BUF_SIZE);

    *(uint32_t *)&dev->tx_buf[0] = lan8651_bswap32(header);
    *(uint32_t *)&dev->tx_buf[4] = write ? lan8651_bswap32(*value) : 0U;
    *(uint32_t *)&dev->tx_buf[8] = 0U;

    status = lan8651_spi_transfer(dev, LAN8651_TC6_CTRL_BUF_SIZE, kLan8651CtrlTransactionTimeoutUs);
    if (status != kLan8651Status_Ok) {
        return status;
    }

    response = lan8651_bswap32(*(uint32_t *)&dev->rx_buf[4]);
    if (lan8651_tc6_parity(response) != ((response & 1U) != 0U)) {
        return kLan8651Status_BadResponse;
    }

    if ((response & LAN8651_TC6_CTRL_RESP_HDRB) != 0U) {
        return kLan8651Status_BadResponse;
    }

    if (!write && value != NULL) {
        *value = lan8651_bswap32(*(uint32_t *)&dev->rx_buf[8]);
    }

    return kLan8651Status_Ok;
}

static lan8651_status_t lan8651_set_reg_bits(lan8651_t *dev, uint32_t reg, uint32_t bits)
{
    uint32_t value;
    lan8651_status_t status = lan8651_read_reg(dev, reg, &value);

    if (status != kLan8651Status_Ok) {
        return status;
    }

    value |= bits;
    return lan8651_write_reg(dev, reg, value);
}

static lan8651_status_t lan8651_default_config(lan8651_t *dev)
{
    uint8_t value1;
    uint8_t value2;
    int8_t offset1;
    int8_t offset2;
    uint16_t value3;
    uint16_t value4;
    uint16_t value5;
    uint16_t value6;
    uint16_t value7;
    uint16_t cfgparam1;
    uint16_t cfgparam2;
    uint16_t cfgparam3;
    uint16_t cfgparam4;
    uint16_t cfgparam5;
    static const struct {
        uint32_t reg;
        uint32_t value;
    } cfg[] = {
        { 0x00040091U, 0x9660U },
        { 0x00040081U, 0x00C0U },
        { LAN8651_MAC_TISUBN, 0x0028U },
        { 0x00040043U, 0x00FFU },
        { 0x00040044U, 0xFFFFU },
        { 0x00040045U, 0x0000U },
        { 0x00040053U, 0x00FFU },
        { 0x00040054U, 0xFFFFU },
        { 0x00040055U, 0x0000U },
        { 0x00040040U, 0x0002U },
        { 0x00040050U, 0x0002U },
        { 0x000400D0U, 0x5F21U },
        { 0x000400E9U, 0x9E50U },
        { 0x000400F5U, 0x1CF8U },
        { 0x000400F4U, 0xC020U },
        { 0x000400F8U, 0x9B00U },
        { 0x000400F9U, 0x4E53U },
        { 0x000400B0U, 0x0103U },
        { 0x000400B1U, 0x0910U },
        { 0x000400B2U, 0x1D26U },
        { 0x000400B3U, 0x002AU },
        { 0x000400B4U, 0x0103U },
        { 0x000400B5U, 0x070DU },
        { 0x000400B6U, 0x1720U },
        { 0x000400B7U, 0x0027U },
        { 0x000400B8U, 0x0509U },
        { 0x000400B9U, 0x0E13U },
        { 0x000400BAU, 0x1C25U },
        { 0x000400BBU, 0x002BU },
    };
    size_t i;

    if (lan8651_indirect_read(dev, 0x04U, 0x1FU, &value1) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }
    offset1 = (value1 & 0x10U) != 0U ? (int8_t)(value1 - 0x20) : (int8_t)value1;

    if (lan8651_indirect_read(dev, 0x08U, 0x1FU, &value2) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }
    offset2 = (value2 & 0x10U) != 0U ? (int8_t)(value2 - 0x20) : (int8_t)value2;

    if (lan8651_read_reg16(dev, 0x00040084U, &value3) != kLan8651Status_Ok ||
        lan8651_read_reg16(dev, 0x0004008AU, &value4) != kLan8651Status_Ok ||
        lan8651_read_reg16(dev, 0x000400ADU, &value5) != kLan8651Status_Ok ||
        lan8651_read_reg16(dev, 0x000400AEU, &value6) != kLan8651Status_Ok ||
        lan8651_read_reg16(dev, 0x000400AFU, &value7) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }

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

    if (lan8651_write_reg(dev, 0x00040084U, cfgparam1) != kLan8651Status_Ok ||
        lan8651_write_reg(dev, 0x0004008AU, cfgparam2) != kLan8651Status_Ok ||
        lan8651_write_reg(dev, 0x000400ADU, cfgparam3) != kLan8651Status_Ok ||
        lan8651_write_reg(dev, 0x000400AEU, cfgparam4) != kLan8651Status_Ok ||
        lan8651_write_reg(dev, 0x000400AFU, cfgparam5) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }

    for (i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
        lan8651_status_t status = lan8651_write_reg(dev, cfg[i].reg, cfg[i].value);
        if (status != kLan8651Status_Ok) {
            return status;
        }
    }

    return kLan8651Status_Ok;
}

static lan8651_status_t lan8651_configure_plca(lan8651_t *dev)
{
    uint32_t ctrl1 = 0;

    ctrl1 |= ((uint32_t)APP_LAN8651_PLCA_NODE_COUNT & 0xFFU) << LAN8651_PLCA_CTRL1_NCNT_SHIFT;
    ctrl1 |= ((uint32_t)APP_LAN8651_PLCA_NODE_ID & 0xFFU);

    if (lan8651_write_reg(dev, LAN8651_COL_DET_CTRL0, LAN8651_COL_DET_CTRL0_PLCA) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }
    if (lan8651_write_reg(dev, LAN8651_PLCA_TOTMR, LAN8651_PLCA_TOTMR_DEFAULT) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }
    if (lan8651_write_reg(dev, LAN8651_PLCA_BURST, LAN8651_PLCA_BURST_DEFAULT) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }
    if (lan8651_write_reg(dev, LAN8651_PLCA_CTRL1, ctrl1) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }
    if (APP_LAN8651_PLCA_ENABLE != 0U) {
        uint32_t ctrl0 = LAN8651_PLCA_CTRL0_EN;
        if (lan8651_write_reg(dev, LAN8651_PLCA_CTRL0, ctrl0) != kLan8651Status_Ok) {
            return kLan8651Status_NotReady;
        }
    }

    return kLan8651Status_Ok;
}

static lan8651_status_t lan8651_set_mac_address(lan8651_t *dev)
{
    uint32_t sab1 = ((uint32_t)dev->mac_addr[5] << 24) |
                    ((uint32_t)dev->mac_addr[4] << 16) |
                    ((uint32_t)dev->mac_addr[3] << 8) |
                    ((uint32_t)dev->mac_addr[2]);
    uint32_t sat1 = ((uint32_t)dev->mac_addr[1] << 8) |
                    ((uint32_t)dev->mac_addr[0]);
    uint32_t sab2 = ((uint32_t)dev->mac_addr[3] << 24) |
                    ((uint32_t)dev->mac_addr[2] << 16) |
                    ((uint32_t)dev->mac_addr[1] << 8) |
                    ((uint32_t)dev->mac_addr[0]);
    uint32_t sat2 = ((uint32_t)dev->mac_addr[5] << 8) |
                    ((uint32_t)dev->mac_addr[4]);

    if (lan8651_write_reg(dev, LAN8651_MAC_HASHL, 0U) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }
    if (lan8651_write_reg(dev, LAN8651_MAC_HASHH, 0U) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }
    if (lan8651_write_reg(dev, LAN8651_MAC_SAB1, sab1) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }
    if (lan8651_write_reg(dev, LAN8651_MAC_SAT1, sat1) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }
    if (lan8651_write_reg(dev, LAN8651_MAC_SAB2, sab2) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }
    if (lan8651_write_reg(dev, LAN8651_MAC_SAT2, sat2) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }

    return kLan8651Status_Ok;
}

void lan8651_spi_tx_dma_complete(uint32_t channel)
{
    (void)channel;
    s_spi_tx_dma_done = true;
}

void lan8651_spi_rx_dma_complete(uint32_t channel)
{
    (void)channel;
    s_spi_rx_dma_done = true;
}

void lan8651_reset(lan8651_t *dev)
{
    lan8651_gpio_write(dev->gpio, dev->reset_pin, false);
    board_delay_ms(kLan8651ResetDelayMs);
    lan8651_gpio_write(dev->gpio, dev->reset_pin, true);
    board_delay_ms(kLan8651PostResetDelayMs);
}

lan8651_status_t lan8651_set_spi_clock(lan8651_t *dev, uint32_t freq)
{
    hpm_stat_t stat = hpm_spi_set_sclk_frequency(dev->spi, freq);
    return (stat == status_success) ? kLan8651Status_Ok : kLan8651Status_NotReady;
}

lan8651_status_t lan8651_init(lan8651_t *dev, SPI_Type *spi, GPIO_Type *gpio,
    uint32_t cs_pin, uint32_t reset_pin, uint32_t irq_pin, const uint8_t *mac_addr)
{
    uint32_t devid = 0;

    memset(dev, 0, sizeof(*dev));
    dev->spi = spi;
    dev->gpio = gpio;
    dev->cs_pin = cs_pin;
    dev->reset_pin = reset_pin;
    dev->irq_pin = irq_pin;
    memcpy(dev->mac_addr, mac_addr, sizeof(dev->mac_addr));

    lan8651_reset(dev);

    if (lan8651_write_reg(dev, LAN8651_OA_RESET, 0x00000001U) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }

    for (uint32_t i = 0; i < 20U; i++) {
        uint32_t value = 0;
        board_delay_ms(10U);
        if (lan8651_read_reg(dev, LAN8651_OA_RESET, &value) == kLan8651Status_Ok && (value & 0x1U) == 0U) {
            break;
        }
    }

    for (uint32_t i = 0; i < 20U; i++) {
        uint32_t value = 0;
        board_delay_ms(10U);
        if (lan8651_read_reg(dev, LAN8651_OA_STATUS0, &value) == kLan8651Status_Ok && (value & LAN8651_OA_STATUS0_RESETC) != 0U) {
            lan8651_write_reg(dev, LAN8651_OA_STATUS0, value);
            break;
        }
    }

    board_delay_ms(100U);

    if (lan8651_read_reg(dev, LAN8651_MISC_DEVID, &devid) == kLan8651Status_Ok) {
        uint16_t model = (uint16_t)((devid >> 4) & 0xFFFFU);
        uint8_t rev = (uint8_t)(devid & 0x0FU);
        printf("LAN8651: DEVID=0x%08lX model=0x%04X rev=%u\n",
            (unsigned long)devid,
            model,
            rev);
    } else {
        printf("LAN8651: DEVID read failed\n");
    }

    if (lan8651_default_config(dev) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }

    if (lan8651_configure_plca(dev) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }

    if (lan8651_set_reg_bits(dev, LAN8651_MAC_NCFGR,
            LAN8651_MAC_NCFGR_MTIHEN | LAN8651_MAC_NCFGR_RFCS | LAN8651_MAC_NCFGR_EFRHD) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }

    if (lan8651_write_reg(dev, LAN8651_OA_CONFIG0,
            LAN8651_OA_CONFIG0_SYNC | LAN8651_OA_CONFIG0_RFA_ZARFE | LAN8651_OA_CONFIG0_BPS_64) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }

    if (lan8651_set_mac_address(dev) != kLan8651Status_Ok) {
        return kLan8651Status_NotReady;
    }

    return kLan8651Status_Ok;
}

lan8651_status_t lan8651_start(lan8651_t *dev)
{
    return lan8651_write_reg(dev, LAN8651_MAC_NCR, LAN8651_MAC_NCR_TXEN | LAN8651_MAC_NCR_RXEN);
}

lan8651_status_t lan8651_read_reg(lan8651_t *dev, uint32_t reg, uint32_t *value)
{
    return lan8651_ctrl_transaction(dev, false, reg, value);
}

lan8651_status_t lan8651_write_reg(lan8651_t *dev, uint32_t reg, uint32_t value)
{
    return lan8651_ctrl_transaction(dev, true, reg, &value);
}

lan8651_status_t lan8651_transmit(lan8651_t *dev, const uint8_t *frame, uint16_t len)
{
    uint32_t chunks = (len + LAN8651_TC6_CHUNK_SIZE - 1U) / LAN8651_TC6_CHUNK_SIZE;
    uint32_t tx_status_clear_mask = LAN8651_MAC_TSR_COL |
                                    LAN8651_MAC_TSR_RLE |
                                    LAN8651_MAC_TSR_TXGO |
                                    LAN8651_MAC_TSR_TFC |
                                    LAN8651_MAC_TSR_TXCOMP |
                                    LAN8651_MAC_TSR_UND |
                                    LAN8651_MAC_TSR_HRESP;

    if ((frame == NULL) || (len == 0U) || (len > LAN8651_MAX_FRAME_SIZE)) {
        return kLan8651Status_TxError;
    }

    if (frame != dev->tx_frame) {
        memcpy(dev->tx_frame, frame, len);
    }

    (void)lan8651_write_reg(dev, LAN8651_MAC_TSR, tx_status_clear_mask);

    for (uint32_t i = 0; i < chunks; i++) {
        uint16_t offset = (uint16_t)(i * LAN8651_TC6_CHUNK_SIZE);
        uint16_t remaining = len - offset;
        uint16_t chunk_len = (remaining > LAN8651_TC6_CHUNK_SIZE) ? LAN8651_TC6_CHUNK_SIZE : remaining;
        bool is_first = (i == 0U);
        bool is_last = (i == (chunks - 1U));
        uint8_t ebo = is_last ? (uint8_t)((len - 1U) % LAN8651_TC6_CHUNK_SIZE) : 0U;
        lan8651_status_t status;

        *(uint32_t *)&dev->tx_buf[0] = lan8651_bswap32(lan8651_make_data_header(is_first, 0U, is_last, ebo, 1U));
        memset(&dev->tx_buf[4], 0, LAN8651_TC6_CHUNK_SIZE);
        memcpy(&dev->tx_buf[4], &dev->tx_frame[offset], chunk_len);

        if (LAN8651_LOG_RAW_SPI && s_tx_raw_log_count < 4U) {
            lan8651_log_raw_block("tx_tc6_pre", s_tx_raw_log_count, dev->tx_buf, LAN8651_TC6_DATA_BUF_SIZE);
        }

        status = lan8651_spi_transfer(dev, LAN8651_TC6_DATA_BUF_SIZE, kLan8651DataTransactionTimeoutUs);
        if (status != kLan8651Status_Ok) {
            return status;
        }

        if (LAN8651_LOG_RAW_SPI && s_tx_raw_log_count < 4U) {
            lan8651_log_raw_block("tx_tc6_post", s_tx_raw_log_count, dev->tx_buf, LAN8651_TC6_DATA_BUF_SIZE);
            lan8651_log_raw_block("rx_tc6_post", s_tx_raw_log_count, dev->rx_buf, LAN8651_TC6_DATA_BUF_SIZE);
            s_tx_raw_log_count++;
        }

        uint32_t footer = lan8651_bswap32(*(uint32_t *)&dev->rx_buf[LAN8651_TC6_CHUNK_SIZE]);
        if (s_tx_footer_log_count < APP_LAN8651_TX_CHUNK_LOG_LIMIT) {
            uint32_t mac_tsr = 0U;
            uint32_t mac_nsr = 0U;
            uint32_t oa_bufsts = 0U;
            (void)lan8651_read_reg(dev, LAN8651_MAC_TSR, &mac_tsr);
            (void)lan8651_read_reg(dev, LAN8651_MAC_NSR, &mac_nsr);
            (void)lan8651_read_reg(dev, LAN8651_OA_BUFSTS, &oa_bufsts);
            printf("tx_chunk[%lu/%lu]: len=%u ftr=0x%08lX txc=%lu rca=%lu tsr=0x%03lX nsr=0x%02lX bufsts=0x%04lX\n",
                (unsigned long)(i + 1U),
                (unsigned long)chunks,
                chunk_len,
                (unsigned long)footer,
                (unsigned long)((footer & LAN8651_TC6_DATA_FTR_TXC_MASK) >> LAN8651_TC6_DATA_FTR_TXC_SHIFT),
                (unsigned long)((footer & LAN8651_TC6_DATA_FTR_RCA_MASK) >> LAN8651_TC6_DATA_FTR_RCA_SHIFT),
                (unsigned long)(mac_tsr & 0x1FFU),
                (unsigned long)(mac_nsr & 0xFFU),
                (unsigned long)(oa_bufsts & 0xFFFFU));
            s_tx_footer_log_count++;
        }
        if ((footer & LAN8651_TC6_DATA_FTR_HDRB) != 0U || (footer & LAN8651_TC6_DATA_FTR_FD) != 0U) {
            return kLan8651Status_TxError;
        }
    }

    return kLan8651Status_Ok;
}

lan8651_status_t lan8651_receive(lan8651_t *dev, uint8_t **frame, uint16_t *len)
{
    uint32_t footer;
    bool start_found = false;

    if ((frame == NULL) || (len == NULL)) {
        return kLan8651Status_RxError;
    }

    *len = 0U;
    dev->rx_frame_len = 0U;

    if (dev->rx_pending_frame_len > 0U) {
        memcpy(dev->rx_frame, dev->rx_pending_frame, dev->rx_pending_frame_len);
        dev->rx_frame_len = dev->rx_pending_frame_len;
        dev->rx_pending_frame_len = 0U;
        start_found = true;
    }

    for (;;) {
        memset(dev->tx_buf, 0, LAN8651_TC6_DATA_BUF_SIZE);
        *(uint32_t *)&dev->tx_buf[0] = lan8651_bswap32(lan8651_make_data_header(false, 0U, false, 0U, 0U));

        if (lan8651_spi_transfer(dev, LAN8651_TC6_DATA_BUF_SIZE, kLan8651DataTransactionTimeoutUs) != kLan8651Status_Ok) {
            return kLan8651Status_RxError;
        }

        if (LAN8651_LOG_RAW_SPI && s_rx_raw_log_count < 6U) {
            lan8651_log_raw_block("rx_poll_tx", s_rx_raw_log_count, dev->tx_buf, LAN8651_TC6_DATA_BUF_SIZE);
            lan8651_log_raw_block("rx_poll_rx", s_rx_raw_log_count, dev->rx_buf, LAN8651_TC6_DATA_BUF_SIZE);
            s_rx_raw_log_count++;
        }

        footer = lan8651_bswap32(*(uint32_t *)&dev->rx_buf[LAN8651_TC6_CHUNK_SIZE]);
        if (lan8651_tc6_parity(footer) != ((footer & 1U) != 0U)) {
            if (LAN8651_LOG_RAW_SPI) {
                lan8651_log_raw_block("rx_footer_parity_tx", s_rx_raw_log_count, dev->tx_buf, LAN8651_TC6_DATA_BUF_SIZE);
                lan8651_log_raw_block("rx_footer_parity_rx", s_rx_raw_log_count, dev->rx_buf, LAN8651_TC6_DATA_BUF_SIZE);
            }
            return kLan8651Status_RxError;
        }
        if ((footer & LAN8651_TC6_DATA_FTR_HDRB) != 0U || (footer & LAN8651_TC6_DATA_FTR_FD) != 0U) {
            if (LAN8651_LOG_RAW_SPI) {
                lan8651_log_raw_block("rx_footer_bad_tx", s_rx_raw_log_count, dev->tx_buf, LAN8651_TC6_DATA_BUF_SIZE);
                lan8651_log_raw_block("rx_footer_bad_rx", s_rx_raw_log_count, dev->rx_buf, LAN8651_TC6_DATA_BUF_SIZE);
            }
            return kLan8651Status_RxError;
        }

        bool dv = (footer & LAN8651_TC6_DATA_FTR_DV) != 0U;
        bool sv = (footer & LAN8651_TC6_DATA_FTR_SV) != 0U;
        bool ev = (footer & LAN8651_TC6_DATA_FTR_EV) != 0U;
        uint8_t rca = (uint8_t)((footer & LAN8651_TC6_DATA_FTR_RCA_MASK) >> LAN8651_TC6_DATA_FTR_RCA_SHIFT);
        uint8_t swo = (uint8_t)((footer & LAN8651_TC6_DATA_FTR_SWO_MASK) >> LAN8651_TC6_DATA_FTR_SWO_SHIFT);
        uint8_t sbo = sv ? (uint8_t)(swo * 4U) : 0U;
        uint8_t ebo = ev ? (uint8_t)(((footer & LAN8651_TC6_DATA_FTR_EBO_MASK) >> LAN8651_TC6_DATA_HDR_EBO_SHIFT) + 1U)
                         : LAN8651_TC6_CHUNK_SIZE;
        bool two_frames = (ebo <= sbo);

        if (!dv) {
            if (rca != 0U) {
                continue;
            }
            break;
        }

        if (!start_found) {
            if (!sv) {
                continue;
            }

            if (two_frames) {
                uint16_t pending_len = (uint16_t)(LAN8651_TC6_CHUNK_SIZE - sbo);
                if (pending_len == 0U || pending_len > LAN8651_MAX_FRAME_SIZE) {
                    return kLan8651Status_RxError;
                }
                memcpy(dev->rx_frame, &dev->rx_buf[sbo], pending_len);
                dev->rx_frame_len = pending_len;
                start_found = true;
                continue;
            }

            if (sbo != 0U) {
                continue;
            }

            start_found = true;
        }

        uint16_t copy_offset = two_frames ? 0U : sbo;
        uint16_t copy_len = two_frames ? ebo : (uint16_t)(ebo - sbo);

        if ((dev->rx_frame_len + copy_len) > LAN8651_MAX_FRAME_SIZE) {
            return kLan8651Status_RxError;
        }

        memcpy(dev->rx_frame + dev->rx_frame_len, &dev->rx_buf[copy_offset], copy_len);
        dev->rx_frame_len += copy_len;

        if (two_frames && sv) {
            uint16_t pending_len = (uint16_t)(LAN8651_TC6_CHUNK_SIZE - sbo);
            if (pending_len == 0U || pending_len > LAN8651_MAX_FRAME_SIZE) {
                return kLan8651Status_RxError;
            }
            memcpy(dev->rx_pending_frame, &dev->rx_buf[sbo], pending_len);
            dev->rx_pending_frame_len = pending_len;
        }

        if (two_frames || ev) {
            break;
        }
    }

    if (dev->rx_frame_len == 0U) {
        return kLan8651Status_NotReady;
    }

    *frame = dev->rx_frame;
    *len = dev->rx_frame_len;
    return kLan8651Status_Ok;
}

bool lan8651_irq_asserted(const lan8651_t *dev)
{
    return gpio_read_pin(dev->gpio, GPIO_GET_PORT_INDEX(dev->irq_pin), GPIO_GET_PIN_INDEX(dev->irq_pin)) == 0U;
}

bool lan8651_link_up(lan8651_t *dev)
{
    uint32_t value = 0U;
    uint32_t plca_status = 0U;

    if (lan8651_read_reg(dev, LAN8651_OA_STATUS0, &value) != kLan8651Status_Ok) {
        return false;
    }

    if (lan8651_read_reg(dev, LAN8651_PLCA_STS, &plca_status) != kLan8651Status_Ok) {
        plca_status = 0U;
    }

    return ((value & LAN8651_OA_STATUS0_SYNC) != 0U) || ((plca_status & LAN8651_PLCA_STS_PST) != 0U);
}