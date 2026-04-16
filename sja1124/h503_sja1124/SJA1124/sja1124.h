/*
 * Copyright 2025 NXP
 * Adapted for STM32 HAL
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SJA1124_H_
#define SJA1124_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief The SJA1124 SPI command definitions
 */
#define SJA1124_SPI_READ_CMD         (0x80)
#define SJA1124_SPI_WRITE_CMD        (0x00)
#define SJA1124_REG_SIZE_BYTE        (1)
#define MAX_RETRIES                  100

/**
 * @brief The SJA1124 Internal Register Map.
 */
typedef enum
{
    /* System control registers */
    SJA1124_MODE      = 0x00,
    SJA1124_PLLCFG    = 0x01,
    SJA1124_INT1EN    = 0x02,
    SJA1124_INT2EN    = 0x03,
    SJA1124_INT3EN    = 0x04,

    /* System status registers */
    SJA1124_INT1      = 0x10,
    SJA1124_INT2      = 0x11,
    SJA1124_INT3      = 0x12,
    SJA1124_STATUS    = 0x13,

    /* LIN commander controller global registers */
    SJA1124_LCOM1     = 0x20,
    SJA1124_LCOM2     = 0x21,

    /* LIN1 channel initialization registers */
    SJA1124_LIN1_CI_LCFG1 = 0x30,
    SJA1124_LIN1_CI_LCFG2 = 0x31,
    SJA1124_LIN1_CI_LITC  = 0x32,
    SJA1124_LIN1_CI_LGC   = 0x33,
    SJA1124_LIN1_CI_LRTC  = 0x34,
    SJA1124_LIN1_CI_LFR   = 0x35,
    SJA1124_LIN1_CI_LBRM  = 0x36,
    SJA1124_LIN1_CI_LBRL  = 0x37,
    SJA1124_LIN1_CI_LIE   = 0x38,

    /* LIN1 channel send frame registers */
    SJA1124_LIN1_SF_LC    = 0x39,
    SJA1124_LIN1_SF_LBI   = 0x3A,
    SJA1124_LIN1_SF_LBC   = 0x3B,
    SJA1124_LIN1_SF_LCF   = 0x3C,
    SJA1124_LIN1_SF_LBD1  = 0x3D,

    /* LIN1 channel get status registers */
    SJA1124_LIN1_GS_LSTATE = 0x4F,
    SJA1124_LIN1_GS_LES    = 0x50,
    SJA1124_LIN1_GS_LS     = 0x51,
    SJA1124_LIN1_GS_LCF    = 0x52,
    SJA1124_LIN1_GS_LBD1   = 0x53,

    /* LIN commander termination configuration registers */
    SJA1124_LIN_CT_CFG_MCFG   = 0xF0,
    SJA1124_LIN_CT_CFG_MMTPS  = 0xF1,
    SJA1124_LIN_CT_CFG_MMTPSS = 0xF2,

    /* Other registers */
    SJA1124_OR_MTPCS = 0xFE,
    SJA1124_OR_ID    = 0xFF,
} SJA1124_RegisterSet_t;

/* ============ Register bit masks and shifts ============ */

/* register MODE */
#define SJA1124_MODE_RESET_SHIFT            (7)
#define SJA1124_MODE_RESET_MASK             (0x80u)
#define SJA1124_MODE_LOWPWRMODE_SHIFT       (0)
#define SJA1124_MODE_LOWPWRMODE_MASK        (0x01u)

/* register PLLCFG */
#define SJA1124_PLLCFG_PLLMULT_SHIFT        (0)
#define SJA1124_PLLCFG_PLLMULT_MASK         (0x0Fu)

/* register INT1EN */
#define SJA1124_INT1EN_L4WKUPIEN_MASK       (0x08u)
#define SJA1124_INT1EN_L3WKUPIEN_MASK       (0x04u)
#define SJA1124_INT1EN_L2WKUPIEN_MASK       (0x02u)
#define SJA1124_INT1EN_L1WKUPIEN_MASK       (0x01u)

/* register INT2EN */
#define SJA1124_INT2EN_OVERTMPWARNIEN_SHIFT (5)
#define SJA1124_INT2EN_OVERTMPWARNIEN_MASK  (0x20u)
#define SJA1124_INT2EN_PLLNOLOCKIEN_SHIFT   (4)
#define SJA1124_INT2EN_PLLNOLOCKIEN_MASK    (0x10u)
#define SJA1124_INT2EN_PLLINLOCKIEN_SHIFT   (3)
#define SJA1124_INT2EN_PLLINLOCKIEN_MASK    (0x08u)
#define SJA1124_INT2EN_PLLFRQFAILIEN_SHIFT  (2)
#define SJA1124_INT2EN_PLLFRQFAILIEN_MASK   (0x04u)
#define SJA1124_INT2EN_SPIERRIEN_SHIFT      (1)
#define SJA1124_INT2EN_SPIERRIEN_MASK       (0x02u)

/* register INT3EN */
#define SJA1124_INT3EN_L4CONTERRIEN_SHIFT   (7)
#define SJA1124_INT3EN_L3CONTERRIEN_SHIFT   (6)
#define SJA1124_INT3EN_L2CONTERRIEN_SHIFT   (5)
#define SJA1124_INT3EN_L1CONTERRIEN_SHIFT   (4)
#define SJA1124_INT3EN_L4CONTSTATIEN_SHIFT  (3)
#define SJA1124_INT3EN_L3CONTSTATIEN_SHIFT  (2)
#define SJA1124_INT3EN_L2CONTSTATIEN_SHIFT  (1)
#define SJA1124_INT3EN_L1CONTSTATIEN_SHIFT  (0)

/* register INT1 */
#define SJA1124_INT1_INITSTATINT_SHIFT      (7)
#define SJA1124_INT1_INITSTATINT_MASK       (0x80u)
#define SJA1124_INT1_L4WAKEUPINT_MASK       (0x08u)
#define SJA1124_INT1_L3WAKEUPINT_MASK       (0x04u)
#define SJA1124_INT1_L2WAKEUPINT_MASK       (0x02u)
#define SJA1124_INT1_L1WAKEUPINT_MASK       (0x01u)

/* register INT2 */
#define SJA1124_INT2_OVERTMPWARNINT_MASK    (0x20u)
#define SJA1124_INT2_PLLNOLOCKINT_MASK      (0x10u)
#define SJA1124_INT2_PLLINLOCKINT_MASK      (0x08u)
#define SJA1124_INT2_PLLFRQFAILINT_MASK     (0x04u)
#define SJA1124_INT2_SPIERRINT_MASK         (0x02u)

/* register STATUS */
#define SJA1124_STATUS_OVERTMPWARN_MASK     (0x20u)
#define SJA1124_STATUS_PLLINLOCK_MASK       (0x08u)
#define SJA1124_STATUS_PLLINFRQFAIL_MASK    (0x04u)

/* register LCOM1 */
#define SJA1124_LCOM1_L1HSMODE_SHIFT        (4)

/* LIN channel register bit definitions (channel 1 as base) */
#define SJA1124_LCFG1_CSCALCDIS_SHIFT       (7)
#define SJA1124_LCFG1_CSCALSW_MASK          (0x80u)
#define SJA1124_LCFG1_MASBREAKLEN_SHIFT     (3)
#define SJA1124_LCFG1_MASBREAKLEN_MASK      (0x78u)
#define SJA1124_LCFG1_SLEEP_SHIFT           (1)
#define SJA1124_LCFG1_SLEEP_MASK            (0x02u)
#define SJA1124_LCFG1_INIT_SHIFT            (0)
#define SJA1124_LCFG1_INIT_MASK             (0x01u)

#define SJA1124_LCFG2_TWOBITDELIM_SHIFT     (7)
#define SJA1124_LCFG2_TWOBITDELIM_MASK      (0x80u)
#define SJA1124_LCFG2_IDLEONBITERR_SHIFT    (6)
#define SJA1124_LCFG2_IDLEONBITERR_MASK     (0x40u)

#define SJA1124_LITC_IDLEONTIMEOUT_SHIFT    (1)
#define SJA1124_LITC_IDLEONTIMEOUT_MASK     (0x02u)

#define SJA1124_LGC_STOPBITCONF_SHIFT       (1)
#define SJA1124_LGC_STOPBITCONF_MASK        (0x02u)
#define SJA1124_LGC_SOFTRESET_MASK          (0x01u)

#define SJA1124_LRTC_RESPTIMEOUT_MASK       (0x0Fu)

#define SJA1124_LIE_STUCKZEROIEN_SHIFT      (7)
#define SJA1124_LIE_STUCKZEROIEN_MASK       (0x80u)
#define SJA1124_LIE_TIMEOUTIEN_SHIFT        (6)
#define SJA1124_LIE_TIMEOUTIEN_MASK         (0x40u)
#define SJA1124_LIE_BITERRIEN_SHIFT         (5)
#define SJA1124_LIE_BITERRIEN_MASK          (0x20u)
#define SJA1124_LIE_CSERRIEN_SHIFT          (4)
#define SJA1124_LIE_CSERRIEN_MASK           (0x10u)
#define SJA1124_LIE_DTRCVIEN_SHIFT          (2)
#define SJA1124_LIE_DTRCVIEN_MASK           (0x04u)
#define SJA1124_LIE_DTTRANSIEN_SHIFT        (1)
#define SJA1124_LIE_DTTRANSIEN_MASK         (0x02u)
#define SJA1124_LIE_FRAMEERRIEN_SHIFT       (0)
#define SJA1124_LIE_FRAMEERRIEN_MASK        (0x01u)

#define SJA1124_LC_WAKEUPREQ_MASK           (0x10u)
#define SJA1124_LC_DTDISCARDREQ_MASK        (0x08u)
#define SJA1124_LC_ABORTREQ_MASK            (0x02u)
#define SJA1124_LC_HEADERTRANSREQ_MASK      (0x01u)

#define SJA1124_LBI_IDENTIFIER_MASK         (0x3Fu)

#define SJA1124_LBC_DTFIELDLEN_SHIFT        (2)
#define SJA1124_LBC_DTFIELDLEN_MASK         (0x1Cu)
#define SJA1124_LBC_DIRECTION_SHIFT         (1)
#define SJA1124_LBC_DIRECTION_MASK          (0x02u)
#define SJA1124_LBC_CLASSICCS_SHIFT         (0)
#define SJA1124_LBC_CLASSICCS_MASK          (0x01u)

#define SJA1124_LSTATE_RCVBUSY_SHIFT        (7)
#define SJA1124_LSTATE_RCVBUSY_MASK         (0x80u)
#define SJA1124_LSTATE_LINSTATE_MASK        (0x0Fu)

#define SJA1124_LES_STUCKZEROFLG_MASK       (0x80u)
#define SJA1124_LES_TIMEOUTERRFLG_MASK      (0x40u)
#define SJA1124_LES_BITERRFLG_MASK          (0x20u)
#define SJA1124_LES_CSERRFLG_MASK           (0x10u)
#define SJA1124_LES_FRAMEERRFLG_MASK        (0x01u)

#define SJA1124_LS_RCVBUFFFULLFLG_MASK      (0x40u)
#define SJA1124_LS_RCVCOMPFLG_MASK          (0x04u)
#define SJA1124_LS_TRANSCOMPFLG_MASK        (0x02u)

/* register ID */
#define SJA1124_ID_IDENTIFICATION_MASK      (0x3Fu)

/**
 * @brief Calculates address offset from base register address of given LIN channel.
 *        Channels are spaced 0x30 apart.
 */
#define SJA1124_CHAN_ADDR_OFFSET(BaseAddress, Channel) \
    ((BaseAddress) + (0x30U * (uint8_t)(Channel)))

#endif /* SJA1124_H_ */
