/**
 * @file  lan8651.h
 * @brief LAN8651 10BASE-T1S MAC-PHY driver over OPEN Alliance TC6 SPI protocol
 *        Ported from Espressif ESP-IDF lan865x driver (reference)
 */
#ifndef LAN8651_H
#define LAN8651_H

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  Memory Map Selector (MMS) values                                  */
/* ------------------------------------------------------------------ */
#define LAN8651_MMS_OA          0x00   /* Open Alliance */
#define LAN8651_MMS_MAC         0x01   /* MAC registers */
#define LAN8651_MMS_PHY_PCS     0x02
#define LAN8651_MMS_PHY_PMA     0x03
#define LAN8651_MMS_PHY_VENDOR  0x04
#define LAN8651_MMS_MISC        0x0A

/* ------------------------------------------------------------------ */
/*  OA registers  (MMS=0)                                             */
/* ------------------------------------------------------------------ */
#define OA_ID_REG               0x0000
#define OA_PHYID_REG            0x0001
#define OA_RESET_REG            0x0003
#define OA_CONFIG0_REG          0x0004
#define OA_STATUS0_REG          0x0008
#define OA_STATUS1_REG          0x0009
#define OA_BUFSTS_REG           0x000B
#define OA_IMASK0_REG           0x000C
#define OA_IMASK1_REG           0x000D

/* OA_CONFIG0 bits */
#define OA_CONFIG0_SYNC         (1U << 15)
#define OA_CONFIG0_RFA_ZARFE    (1U << 12)   /* Zero-Align Receive Frame Enable */
#define OA_CONFIG0_RFA_CSARFE   (1U << 13)   /* Chunk-Spanning Align Receive Frame Enable */
#define OA_CONFIG0_TXCTHRESH_16_CREDITS  (3U << 10)
#define OA_CONFIG0_PROTE        (1U << 5)
#define OA_CONFIG0_BPS_64       (0x06U << 0) /* 64-byte Block Payload Size */

/* OA_STATUS0 bits */
#define OA_STATUS0_RESETC       (1U << 6)

/* OA_BUFSTS bits */
#define OA_BUFSTS_TXC_MASK      0x0000FF00U
#define OA_BUFSTS_TXC_SHIFT     8
#define OA_BUFSTS_RBA_MASK      0x000000FFU
#define OA_BUFSTS_RBA_SHIFT     0

/* PHY indirect access (MMS=0) */
#define OA_PHY_REG_OFFSET       0xFF00

/* Standard PHY registers via OA indirect access (MMS=0, addr=0xFF00|reg) */
#define PHY_BMCR_REG            (OA_PHY_REG_OFFSET | 0x0000U)
#define PHY_BMSR_REG            (OA_PHY_REG_OFFSET | 0x0001U)
#define PHY_ID1_REG             (OA_PHY_REG_OFFSET | 0x0002U)
#define PHY_ID2_REG             (OA_PHY_REG_OFFSET | 0x0003U)

/* PHY_BMCR bits */
#define PHY_BMCR_DUPLEX         0x0100U
#define PHY_BMCR_RESTART_AN     0x0200U
#define PHY_BMCR_POWER_DOWN     0x0800U
#define PHY_BMCR_AUTONEG_EN     0x1000U
#define PHY_BMCR_LOOPBACK       0x4000U
#define PHY_BMCR_RESET          0x8000U

/* PHY_BMSR bits */
#define PHY_BMSR_LINK_STATUS    0x0004U
#define PHY_BMSR_REMOTE_FAULT   0x0010U
#define PHY_BMSR_AN_COMPLETE    0x0020U

/* ------------------------------------------------------------------ */
/*  MAC registers  (MMS=1)                                            */
/* ------------------------------------------------------------------ */
#define MAC_NCR_REG             0x0000  /* Network Control */
#define MAC_NCFGR_REG           0x0001  /* Network Configuration */
#define MAC_NSR_REG             0x0002  /* Network Status */
#define MAC_UR_REG              0x0003  /* User Register */
#define MAC_DCFGR_REG           0x0004  /* DMA Configuration */
#define MAC_TSR_REG             0x0005  /* Transmit Status */
#define MAC_RSR_REG             0x0008  /* Receive Status */
#define MAC_HRB_REG             0x0020  /* Hash Register Bottom */
#define MAC_HRT_REG             0x0021  /* Hash Register Top */
#define MAC_SAB1_REG            0x0022  /* Specific Address 1 Bottom */
#define MAC_SAT1_REG            0x0023  /* Specific Address 1 Top */
#define MAC_SAB2_REG            0x0024  /* Specific Address 2 Bottom */
#define MAC_SAT2_REG            0x0025  /* Specific Address 2 Top */
#define MAC_TISUBN_REG          0x0077  /* TSU Timer Increment Sub-Nanoseconds */

/* MAC_NCR bits */
#define MAC_NCR_TXEN            (1U << 3)
#define MAC_NCR_RXEN            (1U << 2)

/* MAC_NSR bits */
#define MAC_NSR_MDIO            (1U << 1)
#define MAC_NSR_IDLE            (1U << 2)

/* MAC_NCFGR bits */
#define MAC_NCFGR_MTIHEN        (1U << 6)   /* Multicast Hash Enable */
#define MAC_NCFGR_MAXFS         (1U << 8)
#define MAC_NCFGR_RFCS          (1U << 17)  /* Remove FCS */
#define MAC_NCFGR_CAF           (1U << 4)   /* Copy All Frames (promiscuous) */
#define MAC_NCFGR_EFRHD         (1U << 25)  /* Enable Frames Received in half-duplex */

/* MAC_TSR bits */
#define MAC_TSR_COL             (1U << 1)
#define MAC_TSR_RLE             (1U << 2)
#define MAC_TSR_TXGO            (1U << 3)
#define MAC_TSR_TFC             (1U << 4)
#define MAC_TSR_TXCOMP          (1U << 5)
#define MAC_TSR_UND             (1U << 6)
#define MAC_TSR_HRESP           (1U << 8)

/* MAC_RSR bits */
#define MAC_RSR_BNA             (1U << 0)
#define MAC_RSR_REC             (1U << 1)

/* ------------------------------------------------------------------ */
/*  PLCA registers  (MMS=4)                                            */
/* ------------------------------------------------------------------ */
#define PLCA_CTRL0_REG          0xCA01
#define PLCA_CTRL1_REG          0xCA02
#define PLCA_STS_REG            0xCA03
#define PLCA_TOTMR_REG          0xCA04
#define PLCA_BURST_REG          0xCA05
#define COL_DET_CTRL0_REG       0x0087

#define PLCA_CTRL0_EN           0x8000U
#define PLCA_CTRL0_RST          0x4000U
#define PLCA_CTRL1_NCNT_SHIFT   8U
#define PLCA_CTRL1_ID_MASK      0x00FFU
#define PLCA_STS_PST            0x8000U
#define COL_DET_CTRL0_PLCA      0x0083U

#define PLCA_TOTMR_DEFAULT      0x0020U
#define PLCA_BURST_DEFAULT      0x0080U
#define PLCA_NODE_COUNT_DEFAULT 8U
#define PLCA_LOCAL_ID_DEFAULT   1U

/* ------------------------------------------------------------------ */
/*  MISC registers  (MMS=0xA)                                        */
/* ------------------------------------------------------------------ */
#define MISC_DEVID_REG          0x0094

/* ------------------------------------------------------------------ */
/*  TC6 data block constants                                          */
/* ------------------------------------------------------------------ */
#define LAN8651_DATA_BLOCK_SIZE     64
#define LAN8651_HEADER_SIZE         4
#define LAN8651_FOOTER_SIZE         4
#define LAN8651_TX_BLOCK_SIZE       (LAN8651_HEADER_SIZE + LAN8651_DATA_BLOCK_SIZE)
#define LAN8651_CTRL_DUMMY_OFFSET   4

#define LAN8651_MAX_FRAME_SIZE      1536

/* ------------------------------------------------------------------ */
/*  TX Header bit fields                                              */
/* ------------------------------------------------------------------ */
#define TX_HDR_DNC              (1U << 31)   /* Data-Not-Control = 1 */
#define TX_HDR_SEQ              (1U << 30)
#define TX_HDR_NORX             (1U << 29)
#define TX_HDR_DV               (1U << 21)   /* Data Valid */
#define TX_HDR_SV               (1U << 20)   /* Start Valid */
#define TX_HDR_SWO_SHIFT        16
#define TX_HDR_EV               (1U << 14)   /* End Valid */
#define TX_HDR_EBO_SHIFT        8
#define TX_HDR_EBO_MASK         (0x3FU << TX_HDR_EBO_SHIFT)

/* ------------------------------------------------------------------ */
/*  RX Footer bit fields                                              */
/* ------------------------------------------------------------------ */
#define RX_FTR_EXST             (1U << 31)
#define RX_FTR_HDRB             (1U << 30)
#define RX_FTR_SYNC             (1U << 29)
#define RX_FTR_RBA_SHIFT        24
#define RX_FTR_RBA_MASK         (0x1FU << RX_FTR_RBA_SHIFT)
#define RX_FTR_DV               (1U << 21)
#define RX_FTR_SV               (1U << 20)
#define RX_FTR_SWO_SHIFT        16
#define RX_FTR_SWO_MASK         (0x0FU << RX_FTR_SWO_SHIFT)
#define RX_FTR_FD               (1U << 15)
#define RX_FTR_EV               (1U << 14)
#define RX_FTR_EBO_SHIFT        8
#define RX_FTR_EBO_MASK         (0x3FU << RX_FTR_EBO_SHIFT)
#define RX_FTR_TXC_SHIFT        1
#define RX_FTR_TXC_MASK         (0x1FU << RX_FTR_TXC_SHIFT)

/* ------------------------------------------------------------------ */
/*  Control Header bit fields                                         */
/* ------------------------------------------------------------------ */
#define CTRL_HDR_DNC            0           /* DNC = 0 for control */
#define CTRL_HDR_WR             (1U << 29)
#define CTRL_HDR_RD             0
#define CTRL_HDR_AID            (1U << 28)  /* Address Increment Disable */
#define CTRL_HDR_MMS_SHIFT      24
#define CTRL_HDR_ADDR_SHIFT     8
#define CTRL_HDR_LEN_SHIFT      1

/* ------------------------------------------------------------------ */
/*  Driver state                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    SPI_HandleTypeDef  *hspi;
    GPIO_TypeDef       *cs_port;
    uint16_t            cs_pin;
    GPIO_TypeDef       *rst_port;
    uint16_t            rst_pin;
    GPIO_TypeDef       *int_port;
    uint16_t            int_pin;
    uint8_t             mac_addr[6];
    volatile uint8_t    irq_pending;     /* set by EXTI ISR */
    /* SPI buffers (must be dword-aligned) */
    uint8_t  tx_buf[LAN8651_TX_BLOCK_SIZE + LAN8651_CTRL_DUMMY_OFFSET];
    uint8_t  rx_buf[LAN8651_TX_BLOCK_SIZE + LAN8651_CTRL_DUMMY_OFFSET];
    /* Frame reassembly */
    uint8_t  rx_frame[LAN8651_MAX_FRAME_SIZE];
    uint8_t  rx_pending_frame[LAN8651_MAX_FRAME_SIZE];
    uint16_t rx_frame_len;
    uint16_t rx_pending_frame_len;
    uint8_t  seq_num;
} lan8651_t;

typedef struct {
    uint32_t magic;
    uint32_t session_seq;
    uint32_t receive_calls;
    uint32_t frames_returned;
    uint32_t frame_bytes_total;
    uint32_t blocks_total;
    uint32_t dv_blocks;
    uint32_t empty_polls;
    uint32_t no_dv_with_rca;
    uint32_t last_frame_len;
    uint32_t max_frame_len;
    uint32_t last_blocks_per_frame;
    uint32_t max_blocks_per_frame;
    uint32_t last_rba;
} lan8651_rx_diag_t;

/* ------------------------------------------------------------------ */
/*  API                                                               */
/* ------------------------------------------------------------------ */
/**
 * @brief  Initialise the LAN8651 (reset, ID check, AN1760 config, MAC/OA setup)
 * @param  dev  Pointer to a pre-filled lan8651_t (hspi, GPIOs must be ready)
 * @return HAL_OK on success
 */
HAL_StatusTypeDef LAN8651_Init(lan8651_t *dev);

/**
 * @brief  Set the station MAC address
 */
HAL_StatusTypeDef LAN8651_SetMACAddress(lan8651_t *dev, const uint8_t mac[6]);

/**
 * @brief  Enable TX and RX in the MAC
 */
HAL_StatusTypeDef LAN8651_Start(lan8651_t *dev);

/**
 * @brief  Disable TX and RX in the MAC
 */
HAL_StatusTypeDef LAN8651_Stop(lan8651_t *dev);

/**
 * @brief  Transmit one Ethernet frame over TC6
 * @param  frame  Pointer to raw Ethernet frame (including MAC header, no FCS)
 * @param  len    Frame length in bytes
 */
HAL_StatusTypeDef LAN8651_Transmit(lan8651_t *dev, const uint8_t *frame, uint16_t len);

/**
 * @brief  Receive one Ethernet frame from TC6
 * @param  frame  Output buffer (at least LAN8651_MAX_FRAME_SIZE bytes)
 * @param  len    Output: received frame length
 * @return HAL_OK if a frame was received, HAL_ERROR if nothing available
 */
HAL_StatusTypeDef LAN8651_Receive(lan8651_t *dev, uint8_t *frame, uint16_t *len);

/**
 * @brief  Read a single 32-bit register
 */
HAL_StatusTypeDef LAN8651_ReadReg(lan8651_t *dev, uint8_t mms, uint16_t addr, uint32_t *value);

/**
 * @brief  Write a single 32-bit register
 */
HAL_StatusTypeDef LAN8651_WriteReg(lan8651_t *dev, uint8_t mms, uint16_t addr, uint32_t value);

/**
 * @brief  Check how many RX blocks are available (OA_BUFSTS.RBA)
 */
uint8_t LAN8651_GetRxBlocksAvailable(lan8651_t *dev);

void LAN8651_ResetRxDiag(void);

extern volatile uint32_t g_lan8651_ctrl_echo_errors;
extern volatile uint32_t g_lan8651_ctrl_header_bad_errors;
extern volatile uint32_t g_lan8651_tx_header_bad_errors;
extern volatile uint32_t g_lan8651_tx_frame_drop_errors;
extern volatile uint32_t g_lan8651_rx_footer_parity_errors;
extern volatile uint32_t g_lan8651_rx_header_bad_errors;
extern volatile uint32_t g_lan8651_rx_frame_drop_errors;
extern volatile uint32_t g_lan8651_rx_unexpected_swo_errors;
extern volatile uint32_t g_lan8651_last_ctrl_echo;
extern volatile uint32_t g_lan8651_last_tx_ftr;
extern volatile uint32_t g_lan8651_last_rx_ftr;
extern volatile lan8651_rx_diag_t g_lan8651_rx_diag;

#endif /* LAN8651_H */
