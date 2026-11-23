/*
 * OPEN Alliance TC6 (10BASE-T1x MAC-PHY Serial Interface) - HPM MCU port
 *
 * Ported from the Linux driver drivers/net/ethernet/oa_tc6.c (Microchip
 * v1.1 / mainline) to a bare-metal, polling friendly implementation for
 * HPM5E31 + HPM SDK v1.13.0.
 *
 * Reference: OPEN Alliance 10BASE-T1x MAC-PHY Serial Interface V1.1
 *            https://opensig.org/download/document/OPEN_Alliance_10BASET1x_MAC-PHY_Serial_Interface_V1.1.pdf
 *
 * SPI framing (all multi-byte values are big endian on the wire):
 *   control transfer : [4B header][4B x N register values][4B ignored]
 *                      rx: [4B ignored][4B echoed header][4B x N values]
 *   data transfer    : N x 68B chunks
 *                      tx chunk: [4B data header][64B payload]
 *                      rx chunk: [64B payload][4B footer]
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef COMPONENTS_OA_TC6_OA_TC6_H
#define COMPONENTS_OA_TC6_OA_TC6_H

#include <stdint.h>
#include <stdbool.h>
#include "hpm_common.h"
#include "hpm_spi_drv.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- TC6 standard registers (memory map selector 0) ---------- */
#define OA_TC6_REG_STDCAP             0x0002U
#define OA_TC6_REG_RESET              0x0003U
#define OA_TC6_REG_CONFIG0            0x0004U
#define OA_TC6_REG_STATUS0            0x0008U
#define OA_TC6_REG_BUFFER_STATUS      0x000BU
#define OA_TC6_REG_INT_MASK0          0x000CU

#define STDCAP_DIRECT_PHY_REG_ACCESS  (1U << 8)
#define RESET_SWRESET                 (1U << 0)

#define CONFIG0_SYNC                  (1U << 15)
#define CONFIG0_ZARFE_ENABLE          (1U << 12)

#define STATUS0_CTRL_DATA_PROTECTION_ERROR (1U << 12)
#define STATUS0_TX_FRAME_CHECK_SEQ_ERROR   (1U << 11)
#define STATUS0_PHY_INT                    (1U << 7)
#define STATUS0_RESETC                     (1U << 6)  /* reset complete */
#define STATUS0_HEADER_ERROR               (1U << 5)
#define STATUS0_LOSS_OF_FRAME_ERROR        (1U << 4)
#define STATUS0_RX_BUFFER_OVERFLOW_ERROR   (1U << 3)
#define STATUS0_TX_BUFFER_UNDERFLOW_ERROR  (1U << 2)
#define STATUS0_TX_BUFFER_OVERFLOW_ERROR   (1U << 1)
#define STATUS0_TX_PROTOCOL_ERROR          (1U << 0)

#define BUFFER_STATUS_TX_CREDITS_SHIFT    8
#define BUFFER_STATUS_RX_CHUNKS_SHIFT     0

/* ---------------- memory map selectors (MMS) / MMD mapping ---------------- */
#define OA_TC6_MMS_PCS                 2U   /* MDIO MMD 3  */
#define OA_TC6_MMS_PMAPMD              3U   /* MDIO MMD 1  */
#define OA_TC6_MMS_VEND2               4U   /* MDIO MMD 31 (PLCA + vendor regs) */
#define OA_TC6_MMS_AUTONEG             5U   /* MDIO MMD 7  */
#define OA_TC6_MMS_POWER_UNIT          6U   /* MDIO MMD 13 */
/* Clause 22 registers are in MMS0 at 0xFF00..0xFF1F (DS60001734E 11.1) */
#define OA_TC6_C22_REG_BASE            0xFF00U

/* ---------------- control header / data header bit fields ---------------- */
#define OA_TC6_CTRL_HDR_DATA_NOT_CTRL  (1U << 31)
#define OA_TC6_CTRL_HDR_WRITE_NOT_READ (1U << 29)
#define OA_TC6_CTRL_HDR_PARITY         (1U << 0)

#define OA_TC6_DATA_HDR_DATA_NOT_CTRL  (1U << 31)
#define OA_TC6_DATA_HDR_DATA_VALID     (1U << 21)
#define OA_TC6_DATA_HDR_START_VALID    (1U << 20)
#define OA_TC6_DATA_HDR_START_WORD_OFFSET(s) (((uint32_t)(s) & 0xFU) << 16)
#define OA_TC6_DATA_HDR_END_VALID      (1U << 14)
#define OA_TC6_DATA_HDR_END_BYTE_OFFSET(e) (((uint32_t)(e) & 0x3FU) << 8)

#define OA_TC6_DATA_FTR_EXTENDED_STS   (1U << 31)
#define OA_TC6_DATA_FTR_RXD_HEADER_BAD (1U << 30)
#define OA_TC6_DATA_FTR_CONFIG_SYNC    (1U << 29)
#define OA_TC6_DATA_FTR_RX_CHUNKS(f)   (((f) >> 24) & 0x1FU)
#define OA_TC6_DATA_FTR_DATA_VALID     (1U << 21)
#define OA_TC6_DATA_FTR_START_VALID    (1U << 20)
#define OA_TC6_DATA_FTR_START_WORD_OFFSET(f) (((f) >> 16) & 0xFU)
#define OA_TC6_DATA_FTR_END_VALID      (1U << 14)
#define OA_TC6_DATA_FTR_END_BYTE_OFFSET(f)   (((f) >> 8) & 0x3FU)
#define OA_TC6_DATA_FTR_TX_CREDITS(f)  (((f) >> 1) & 0x1FU)

/* ---------------- sizes ---------------------------------------------------- */
#define OA_TC6_CTRL_HEADER_SIZE       4U
#define OA_TC6_CTRL_REG_VALUE_SIZE    4U
#define OA_TC6_CTRL_IGNORED_SIZE      4U
#define OA_TC6_CTRL_MAX_REGISTERS     32U
#define OA_TC6_CTRL_SPI_BUF_SIZE      (OA_TC6_CTRL_HEADER_SIZE + \
                                       (OA_TC6_CTRL_MAX_REGISTERS * OA_TC6_CTRL_REG_VALUE_SIZE) + \
                                       OA_TC6_CTRL_IGNORED_SIZE)

#define OA_TC6_CHUNK_PAYLOAD_SIZE     64U
#define OA_TC6_DATA_HEADER_SIZE       4U
#define OA_TC6_CHUNK_SIZE             (OA_TC6_DATA_HEADER_SIZE + OA_TC6_CHUNK_PAYLOAD_SIZE)
#define OA_TC6_MAX_CHUNKS             24U   /* one SPI data transfer */
#define OA_TC6_SPI_DATA_BUF_SIZE      (OA_TC6_MAX_CHUNKS * OA_TC6_CHUNK_SIZE)

#define OA_TC6_FRAME_MAX              1524U /* 1518 + 6 (rx padding tolerance) */
#define OA_TC6_RX_QUEUE_DEPTH         8U   /* ACK 突发时 2 个槽位会丢帧，见第 11 章 */

/** TC6 driver instance */
typedef struct {
    SPI_Type *spi;
    spi_control_config_t ctrl_cfg;   /* write_read_together, no cmd/addr phase */
    uint32_t sclk_hz;

    uint8_t ctrl_tx[OA_TC6_CTRL_SPI_BUF_SIZE] __attribute__((aligned(4)));
    uint8_t ctrl_rx[OA_TC6_CTRL_SPI_BUF_SIZE] __attribute__((aligned(4)));
    uint8_t data_tx[OA_TC6_SPI_DATA_BUF_SIZE] __attribute__((aligned(4)));
    uint8_t data_rx[OA_TC6_SPI_DATA_BUF_SIZE] __attribute__((aligned(4)));

    bool     gpio_cs;               /* true: CS driven by the caller as GPIO */
    bool     use_bb;                /* true: bit-banged SPI transport (fallback) */
    bool     use_ll_xfer;           /* true: low level FIFO aware transfer path */
    uint8_t  bb_half_delay;         /* bit-bang half period in NOP loops */
    uint8_t  tx_credits;            /* from buffer status register / footer */
    uint8_t  rx_chunks_available;

    /* receive frame assembly */
    uint8_t  rx_frame[OA_TC6_FRAME_MAX];
    uint16_t rx_frame_len;
    bool     rx_in_progress;

    /* completed receive frames */
    uint8_t  rx_q[OA_TC6_RX_QUEUE_DEPTH][OA_TC6_FRAME_MAX];
    uint16_t rx_q_len[OA_TC6_RX_QUEUE_DEPTH];
    uint8_t  rx_q_head;
    uint8_t  rx_q_tail;
    uint8_t  rx_q_cnt;

    /* statistics (all 32 bit saturating counters) */
    uint32_t stat_ctrl_xfer;
    uint32_t stat_ctrl_err;
    uint32_t stat_spi_err;
    uint32_t stat_data_xfer;
    uint32_t stat_tx_frames;
    uint32_t stat_tx_bytes;
    uint32_t stat_rx_frames;
    uint32_t stat_rx_bytes;
    uint32_t stat_rx_dropped;
    uint32_t stat_ext_status;
    uint32_t stat_status0;          /* last STATUS0 value seen via footer */
    uint32_t stat_hdr_bad;
    uint32_t stat_cfg_sync_lost;
    uint32_t stat_empty_xfer;       /* transfers with no data chunk (credit wait) */
} oa_tc6_t;

/* ---------------- lifecycle ----------------------------------------------- */
/**
 * @brief  initialise SPI2 (timing/format) and zero the driver state.
 * @param  clk_src_hz  frequency of the SPI peripheral source clock (Hz)
 * @param  sclk_hz     requested SCLK frequency (Hz)
 */
hpm_stat_t oa_tc6_spi_init(oa_tc6_t *tc6, SPI_Type *spi, uint32_t clk_src_hz, uint32_t sclk_hz);

/** software reset through TC6 RESET register, polls STATUS0.RESETC */
int oa_tc6_soft_reset(oa_tc6_t *tc6);
/** poll STATUS0 until RESETC is set (after an external hardware reset) */
int oa_tc6_wait_reset_complete(oa_tc6_t *tc6, uint32_t timeout_ms);
/** clear STATUS0 (write-1-to-clear) */
int oa_tc6_clear_status0(oa_tc6_t *tc6);
/** unmask the MAC-PHY error interrupts used by this port */
int oa_tc6_unmask_error_interrupts(oa_tc6_t *tc6);
/** enable configuration synchronization (needed before any data transfer) */
int oa_tc6_enable_data_transfer(oa_tc6_t *tc6);
/** enable zero-align receive frame (LAN865x errata workaround) */
int oa_tc6_zarfe_enable(oa_tc6_t *tc6);
/**
 * @brief switch the TC6 transport between the SPI2 controller and a
 *        software bit-banged SPI on the same four pads.
 *
 * The bit-banged path was needed on hpm5e31_1 rev.1 because the SPI2
 * controller produced no echo on the LAN8651 control channel; it is kept as a
 * documented fallback and as a way to prove which side is at fault.
 */
void oa_tc6_use_bitbang(oa_tc6_t *tc6, bool enable);
void oa_tc6_set_bb_delay(oa_tc6_t *tc6, uint8_t half_delay_nops);
/** measure the achieved bit-bang SCLK frequency (Hz), 0 if not in bit-bang mode */
uint32_t oa_tc6_bb_measure_sclk(oa_tc6_t *tc6);
/** set the PAD_CTL of PA20..PA23 (SPI2 pads), default SR=0/SPD=0 (slow, reference design) */
void oa_tc6_set_pad_ctl(oa_tc6_t *tc6, uint32_t pad_ctl);
uint32_t oa_tc6_get_pad_ctl(void);
/** raw transfer helper (diagnostics): one TC6 SPI frame on the current transport */
int oa_tc6_spi_transfer_raw(oa_tc6_t *tc6, uint8_t *tx, uint8_t *rx, uint32_t len);
/** measured/derived SCLK frequency (Hz); uses mcycle in bit-bang mode */
uint32_t oa_tc6_spi_get_sclk(oa_tc6_t *tc6, uint32_t clk_src_hz);
/** use a GPIO driven chip select instead of the SPI2 hardware CS (debug) */
void oa_tc6_use_gpio_cs(oa_tc6_t *tc6, bool enable);
/** change SPI polarity/phase at runtime: cpol 0/1, cpha 0 = first edge, 1 = second */
hpm_stat_t oa_tc6_set_spi_mode(oa_tc6_t *tc6, uint8_t cpol, uint8_t cpha);
/** refresh tx_credits / rx_chunks_available from BUFFER_STATUS */
int oa_tc6_update_buffer_status(oa_tc6_t *tc6);

/* ---------------- register access ---------------------------------------- */
int oa_tc6_read_reg(oa_tc6_t *tc6, uint32_t addr, uint32_t *value);
int oa_tc6_write_reg(oa_tc6_t *tc6, uint32_t addr, uint32_t value);
int oa_tc6_read_regs(oa_tc6_t *tc6, uint32_t addr, uint32_t *value, uint8_t length);
int oa_tc6_write_regs(oa_tc6_t *tc6, uint32_t addr, const uint32_t *value, uint8_t length);
/** clause 45 access through a memory map selector (mms) */
int oa_tc6_mmd_read(oa_tc6_t *tc6, uint8_t mms, uint16_t reg, uint16_t *value);
int oa_tc6_mmd_write(oa_tc6_t *tc6, uint8_t mms, uint16_t reg, uint16_t value);
/** clause 22 access (indirect window 0xFF00 | reg) */
int oa_tc6_c22_read(oa_tc6_t *tc6, uint8_t reg, uint16_t *value);
int oa_tc6_c22_write(oa_tc6_t *tc6, uint8_t reg, uint16_t value);

/* ---------------- ethernet frame transfer -------------------------------- */
/** queue one ethernet frame (without FCS) and push it out over the 10BASE-T1S bus */
int oa_tc6_send_frame(oa_tc6_t *tc6, const uint8_t *frame, uint16_t len);
/** one data transfer: send an empty chunk only to fetch buffer status/rx chunks */
int oa_tc6_poll_rx(oa_tc6_t *tc6);
/** pop one received frame, returns frame length or 0 when the queue is empty */
uint16_t oa_tc6_rx_pop(oa_tc6_t *tc6, uint8_t *buf, uint16_t buf_size);

#ifdef __cplusplus
}
#endif
#endif /* COMPONENTS_OA_TC6_OA_TC6_H */
