/**
 * @file  lan8651.h
 * @brief LAN8651 10BASE-T1S MAC-PHY driver for TC387 / TriCore
 *        Adapted from HPM5361 version; uses IfxQspi_SpiMaster for SPI.
 */
#ifndef LAN8651_H
#define LAN8651_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "Qspi/SpiMaster/IfxQspi_SpiMaster.h"
#include "Port/Std/IfxPort.h"

/* ------------------------------------------------------------------
 *  Size constants
 * ------------------------------------------------------------------ */
#define LAN8651_MAX_FRAME_SIZE          1536U
#define LAN8651_TC6_CHUNK_SIZE          64U
#define LAN8651_TC6_CTRL_BUF_SIZE       12U
#define LAN8651_TC6_DATA_BUF_SIZE       (4U + LAN8651_TC6_CHUNK_SIZE)   /* header + payload */

/* ------------------------------------------------------------------
 *  Register addresses  (MMS encoded as upper byte(s))
 * ------------------------------------------------------------------ */
#define LAN8651_OA_RESET                0x00000003U
#define LAN8651_OA_CONFIG0              0x00000004U
#define LAN8651_OA_STATUS0              0x00000008U
#define LAN8651_OA_BUFSTS               0x0000000BU
#define LAN8651_OA_PHY_REG_OFFSET       0x0000FF00U

#define LAN8651_PHY_BMCR                (LAN8651_OA_PHY_REG_OFFSET | 0x0000U)
#define LAN8651_PHY_BMSR                (LAN8651_OA_PHY_REG_OFFSET | 0x0001U)
#define LAN8651_PHY_ID1                 (LAN8651_OA_PHY_REG_OFFSET | 0x0002U)
#define LAN8651_PHY_ID2                 (LAN8651_OA_PHY_REG_OFFSET | 0x0003U)

#define LAN8651_MISC_DEVID              0x000A0094U

#define LAN8651_VENDOR_REG_D8           0x000400D8U
#define LAN8651_VENDOR_REG_D9           0x000400D9U
#define LAN8651_VENDOR_REG_DA           0x000400DAU

#define LAN8651_MAC_NCR                 0x00010000U
#define LAN8651_MAC_NCFGR               0x00010001U
#define LAN8651_MAC_NSR                 0x00010002U
#define LAN8651_MAC_TSR                 0x00010005U
#define LAN8651_MAC_RSR                 0x00010008U
#define LAN8651_MAC_TISUBN              0x00010077U
#define LAN8651_MAC_HASHL               0x00010020U
#define LAN8651_MAC_HASHH               0x00010021U
#define LAN8651_MAC_SAB1                0x00010022U
#define LAN8651_MAC_SAT1                0x00010023U
#define LAN8651_MAC_SAB2                0x00010024U
#define LAN8651_MAC_SAT2                0x00010025U

#define LAN8651_PLCA_CTRL0              0x0004CA01U
#define LAN8651_PLCA_CTRL1              0x0004CA02U
#define LAN8651_PLCA_STS                0x0004CA03U
#define LAN8651_PLCA_TOTMR              0x0004CA04U
#define LAN8651_PLCA_BURST              0x0004CA05U
#define LAN8651_COL_DET_CTRL0           0x00040087U

/* Diagnostics: status/event registers (MMS4). STS1/2/3 are read-clear. */
#define LAN8651_STS1                    0x0004CA18U
#define LAN8651_STS2                    0x0004CA19U
#define LAN8651_STS3                    0x0004CA1AU
#define LAN8651_CTRCTRL                 0x00040020U
#define LAN8651_TOCNTH                  0x00040024U
#define LAN8651_TOCNTL                  0x00040025U
#define LAN8651_BCNCNTH                 0x00040026U
#define LAN8651_BCNCNTL                 0x00040027U
#define LAN8651_PRSSTS                  0x00040036U
#define LAN8651_SQICTL                  0x000400A0U
#define LAN8651_SQISTS0                 0x000400A1U
#define LAN8651_SQICFG0                 0x000400AAU
#define LAN8651_SQICFG2                 0x000400ACU

/* ------------------------------------------------------------------
 *  Register bit fields
 * ------------------------------------------------------------------ */
#define LAN8651_MAC_NCR_TXEN            (1UL << 3)
#define LAN8651_MAC_NCR_RXEN            (1UL << 2)
#define LAN8651_MAC_NSR_IDLE            (1UL << 2)
#define LAN8651_MAC_TSR_COL             (1UL << 1)
#define LAN8651_MAC_TSR_RLE             (1UL << 2)
#define LAN8651_MAC_TSR_TXGO            (1UL << 3)
#define LAN8651_MAC_TSR_TFC             (1UL << 4)
#define LAN8651_MAC_TSR_TXCOMP          (1UL << 5)
#define LAN8651_MAC_TSR_UND             (1UL << 6)
#define LAN8651_MAC_TSR_HRESP           (1UL << 8)
#define LAN8651_MAC_RSR_REC             (1UL << 1)
#define LAN8651_MAC_NCFGR_MTIHEN        (1UL << 6)
#define LAN8651_MAC_NCFGR_RFCS          (1UL << 17)
#define LAN8651_MAC_NCFGR_EFRHD         (1UL << 25)

#define LAN8651_OA_CONFIG0_SYNC         (1UL << 15)
#define LAN8651_OA_CONFIG0_RFA_ZARFE    (1UL << 12)
#define LAN8651_OA_CONFIG0_BPS_64       (0x06UL << 0)
#define LAN8651_OA_BUFSTS_TXC_SHIFT     8U
#define LAN8651_OA_BUFSTS_TXC_MASK      0x0000FF00U
#define LAN8651_OA_BUFSTS_RBA_MASK      0x000000FFU
#define LAN8651_OA_STATUS0_RESETC       (1UL << 6)
#define LAN8651_OA_STATUS0_SYNC         (1UL << 5)

#define LAN8651_PHY_BMCR_DUPLEX         0x0100U
#define LAN8651_PHY_BMCR_POWER_DOWN     0x0800U
#define LAN8651_PHY_BMCR_AUTONEG_EN     0x1000U
#define LAN8651_PHY_BMSR_LINK_STATUS    0x0004U

#define LAN8651_PLCA_CTRL0_EN           0x8000U
#define LAN8651_PLCA_CTRL1_NCNT_SHIFT   8U
#define LAN8651_PLCA_STS_PST            0x8000U
#define LAN8651_COL_DET_CTRL0_PLCA      0x0083U
#define LAN8651_PLCA_TOTMR_DEFAULT      0x0020U
#define LAN8651_PLCA_BURST_DEFAULT      0x0080U

/* STS1 (RC): bit12 SQI, bit11 PSTC, bit10 TXCOL, bit9 TXJAB, bit8 TSSI,
 * bit7 EMPCYC, bit6 RXINTO, bit5 UNEXPB, bit4 BCNBFTO,
 * bit3 UNCRS, bit2 PLCASYM, bit1 ESDERR, bit0 DEC5B */
#define LAN8651_STS1_SQI                0x1000U
#define LAN8651_STS1_PSTC               0x0800U
#define LAN8651_STS1_TXCOL              0x0400U
#define LAN8651_STS1_TXJAB              0x0200U
#define LAN8651_STS1_TSSI               0x0100U
#define LAN8651_STS1_EMPCYC             0x0080U
#define LAN8651_STS1_RXINTO             0x0040U
#define LAN8651_STS1_UNEXPB             0x0020U
#define LAN8651_STS1_BCNBFTO            0x0010U
#define LAN8651_STS1_UNCRS              0x0008U
#define LAN8651_STS1_PLCASYM            0x0004U
#define LAN8651_STS1_ESDERR             0x0002U
#define LAN8651_STS1_DEC5B              0x0001U

/* STS2: bit10 WKEMDI, bit9 WKEWI, bit8 UV33, bit6 OT, bit5 IWDTO */
#define LAN8651_STS2_WKEMDI             0x0400U
#define LAN8651_STS2_WKEWI              0x0200U
#define LAN8651_STS2_UV33               0x0100U
#define LAN8651_STS2_OT                 0x0040U
#define LAN8651_STS2_IWDTO              0x0020U

/* Counter control */
#define LAN8651_CTRCTRL_TOCTRE          0x0002U
#define LAN8651_CTRCTRL_BCNCTRE         0x0001U

/* PRSSTS: MAXID in bits 15:8 */
#define LAN8651_PRSSTS_MAXID_SHIFT      8U

/* SQI: SQICTL bit15 SQIRST(SC) / bit14 SQIEN; SQISTS0 bit7 SQIERR /
 * bit6 SQIVLD(RC) / bit5:3 SQIVAL / bit2:0 SQIERRC; SQICFG0 TOID in
 * bits 11:4 (0xFF = all nodes); SQICFG2 SQIINTTHR bits 12:8 (0x1F = off) */
#define LAN8651_SQICTL_SQIRST           0x8000U
#define LAN8651_SQICTL_SQIEN            0x4000U
#define LAN8651_SQISTS0_SQIERR          0x0080U
#define LAN8651_SQISTS0_SQIVLD          0x0040U
#define LAN8651_SQISTS0_SQIVAL_SHIFT    3U
#define LAN8651_SQISTS0_SQIVAL_MASK     0x0038U
#define LAN8651_SQISTS0_SQIERRC_MASK    0x0007U
#define LAN8651_SQICFG0_TOID_SHIFT      4U
#define LAN8651_SQICFG0_TOID_MASK       0x0FF0U
#define LAN8651_SQICFG2_INTTHR_MASK     0x1F00U
#define LAN8651_SQICFG2_INTTHR_OFF      0x1F00U

/* ------------------------------------------------------------------
 *  TC6 control header / response bits
 * ------------------------------------------------------------------ */
#define LAN8651_TC6_CTRL_HDR_WNR        (1UL << 29)
#define LAN8651_TC6_CTRL_HDR_AID        (1UL << 28)
#define LAN8651_TC6_CTRL_HDR_MMS_SHIFT  24U
#define LAN8651_TC6_CTRL_HDR_ADDR_SHIFT 8U
#define LAN8651_TC6_CTRL_HDR_LEN_SHIFT  1U
#define LAN8651_TC6_CTRL_RESP_HDRB      (1UL << 30)

/* ------------------------------------------------------------------
 *  TC6 data header / footer bits
 * ------------------------------------------------------------------ */
#define LAN8651_TC6_DATA_HDR_DNC        (1UL << 31)
#define LAN8651_TC6_DATA_HDR_SEQ        (1UL << 30)
#define LAN8651_TC6_DATA_HDR_NORX       (1UL << 29)
#define LAN8651_TC6_DATA_HDR_DV         (1UL << 21)
#define LAN8651_TC6_DATA_HDR_SV         (1UL << 20)
#define LAN8651_TC6_DATA_HDR_SWO_SHIFT  16U
#define LAN8651_TC6_DATA_HDR_EV         (1UL << 14)
#define LAN8651_TC6_DATA_HDR_EBO_SHIFT  8U

#define LAN8651_TC6_DATA_FTR_EXST       (1UL << 31)
#define LAN8651_TC6_DATA_FTR_HDRB       (1UL << 30)
#define LAN8651_TC6_DATA_FTR_SYNC       (1UL << 29)
#define LAN8651_TC6_DATA_FTR_RCA_SHIFT  24U
#define LAN8651_TC6_DATA_FTR_RCA_MASK   (0x1FUL << LAN8651_TC6_DATA_FTR_RCA_SHIFT)
#define LAN8651_TC6_DATA_FTR_DV         (1UL << 21)
#define LAN8651_TC6_DATA_FTR_SV         (1UL << 20)
#define LAN8651_TC6_DATA_FTR_SWO_SHIFT  16U
#define LAN8651_TC6_DATA_FTR_SWO_MASK   (0x0FUL << LAN8651_TC6_DATA_FTR_SWO_SHIFT)
#define LAN8651_TC6_DATA_FTR_FD         (1UL << 15)
#define LAN8651_TC6_DATA_FTR_EV         (1UL << 14)
#define LAN8651_TC6_DATA_FTR_EBO_MASK   (0x3FUL << LAN8651_TC6_DATA_HDR_EBO_SHIFT)
#define LAN8651_TC6_DATA_FTR_TXC_SHIFT  1U
#define LAN8651_TC6_DATA_FTR_TXC_MASK   (0x1FUL << LAN8651_TC6_DATA_FTR_TXC_SHIFT)

/* ------------------------------------------------------------------
 *  Status enum
 * ------------------------------------------------------------------ */
typedef enum {
    kLan8651Status_Ok          =  0,
    kLan8651Status_Timeout     = -1,
    kLan8651Status_BadResponse = -2,
    kLan8651Status_NotReady    = -3,
    kLan8651Status_TxError     = -4,
    kLan8651Status_RxError     = -5,
} lan8651_status_t;

/* ------------------------------------------------------------------
 *  Device handle
 * ------------------------------------------------------------------ */
typedef struct {
    IfxQspi_SpiMaster         spi;
    IfxQspi_SpiMaster_Channel spiChannel;
    Ifx_P                    *rst_port;
    uint8_t                   rst_pin;
    Ifx_P                    *irq_port;
    uint8_t                   irq_pin;
    uint8_t                   mac_addr[6];
    uint16_t                  rx_frame_len;
    uint16_t                  rx_pending_frame_len;
    uint8_t                   tx_frame[LAN8651_MAX_FRAME_SIZE];
    uint8_t                   rx_frame[LAN8651_MAX_FRAME_SIZE];
    uint8_t                   rx_pending_frame[LAN8651_MAX_FRAME_SIZE];
    /* Aligned SPI buffers (data transfer = 68 bytes) */
    uint8_t                   tx_buf[LAN8651_TC6_DATA_BUF_SIZE];
    uint8_t                   rx_buf[LAN8651_TC6_DATA_BUF_SIZE];
} lan8651_t;

IFX_EXTERN lan8651_t g_lan8651;

/* ------------------------------------------------------------------
 *  QSPI2 ISR callbacks (call from ISR in Cpu0_Main.c)
 * ------------------------------------------------------------------ */
void lan8651_qspi_tx_isr(void);
void lan8651_qspi_rx_isr(void);
void lan8651_qspi_er_isr(void);

/* SPI transaction statistics (see lan8651.c).  All should stay 0 on a healthy
 * link; they are printed by the `t1stat` shell command.
 *   timeouts  : a transaction never completed within LAN8651_SPI_TIMEOUT_US
 *   busy      : the QSPI channel was still busy when a transfer was started
 *   recovered : the iLLD state was forced back to idle and the transfer retried
 *   hdrb      : the chip rejected our data header (footer HDRB)
 *   fail      : frames that low_level_output could not send at all */
void lan8651_spi_stats(uint32_t *timeouts, uint32_t *busy, uint32_t *recovered);
void lan8651_tx_stats(uint32_t *hdrb, uint32_t *fail);

/* ------------------------------------------------------------------
 *  Public API
 * ------------------------------------------------------------------ */
void             lan8651_reset(lan8651_t *dev);
lan8651_status_t lan8651_init(lan8651_t *dev,
                              Ifx_QSPI *qspi_module,
                              Ifx_P *rst_port, uint8_t rst_pin,
                              Ifx_P *irq_port, uint8_t irq_pin,
                              const uint8_t *mac_addr);
lan8651_status_t lan8651_start(lan8651_t *dev);
lan8651_status_t lan8651_read_reg(lan8651_t *dev, uint32_t reg, uint32_t *value);
lan8651_status_t lan8651_write_reg(lan8651_t *dev, uint32_t reg, uint32_t value);
lan8651_status_t lan8651_transmit(lan8651_t *dev, const uint8_t *frame, uint16_t len);
lan8651_status_t lan8651_receive(lan8651_t *dev, uint8_t **frame, uint16_t *len);
bool             lan8651_irq_asserted(const lan8651_t *dev);
bool             lan8651_link_up(lan8651_t *dev);

#endif /* LAN8651_H */
