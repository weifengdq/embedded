/*
 * EthTrcv.c —— AUTOSAR Ethernet Transceiver Driver（模块 ID 73）实现
 *
 * 依据：AUTOSAR_CP_SWS_EthernetTransceiverDriver（R25-11）
 *       autosar_txt/AUTOSAR_CP_SWS_EthernetTransceiverDriver.txt
 * 硬件：YT8011AN 千兆车载以太网 PHY（1000BASE-T1 / 100BASE-T1，RGMII）
 *       YT8011A_1.pdf（Issue V1.4）§4 寄存器总览；YT8011Ax_应用说明_V2.2
 *         §2.1.2 EXT 寄存器间接访问（0x1E/0x1F）
 *         §2.1.3 MMD 间接访问（0x0D/0x0E）
 *         §5.2 链路状态获取（0x11 PHYSR）、§5.3 主从（MMD1 0x834）、§5.4 速率（0x8140/0xA100）
 * 分层：本文件不直接访问 ENET 寄存器，全部经 Eth_ReadMii/WriteMii/ReadMmd/WriteMmd。
 * 文档：docs/chapters/50-mcal-eth.md；例程：samples/mcal/eth_trcv。
 */
#include "EthTrcv.h"
#include "EthTrcv_PBcfg.h"
#include "Det.h"
#include "Eth.h"
#include "Mcal_Version.h"
#include "board.h"
#include <string.h>

/* ========================================================================
 * YT8011AN 寄存器定义（只放本文件，避免引入 components 的 SDK 头）
 * ======================================================================== */
#define YT_BMCR              (0x00U)  /*!< Basic Control Register */
#define YT_BMSR              (0x01U)  /*!< Basic Status Register */
#define YT_PHYID1            (0x02U)  /*!< PHY Identifier 1 */
#define YT_PHYID2            (0x03U)  /*!< PHY Identifier 2 */
#define YT_GBCR              (0x09U)  /*!< 1000BASE-T Control */
#define YT_GBSR              (0x0AU)  /*!< 1000BASE-T Status */
#define YT_PHYSR             (0x11U)  /*!< PHY Specific Status */
#define YT_EXT_ADDR          (0x1EU)  /*!< EXT 寄存器地址（间接访问） */
#define YT_EXT_DATA          (0x1FU)  /*!< EXT 寄存器数据 */

#define YT_BMCR_RESET        (0x8000U)
#define YT_BMCR_LOOPBACK     (0x4000U)
#define YT_BMCR_AN_EN        (0x1000U)
#define YT_BMCR_PWR_DOWN     (0x0800U)
#define YT_BMCR_AN_RESTART   (0x0200U)
#define YT_BMCR_FORCE_1000M  (0x8140U) /*!< 软复位 + 1000BASE-T1（应用说明 §5.4） */
#define YT_BMCR_FORCE_100M   (0xA100U) /*!< 软复位 + 100BASE-T1（应用说明 §5.4） */
#define YT_BMCR_RESET_AN     (0x9140U) /*!< 软复位 + 使能自协商 */

#define YT_BMSR_LINK         (0x0004U)
#define YT_BMSR_ANEG_DONE    (0x0020U)

#define YT_PHYSR_LINK        (0x0400U) /*!< bit10 实时链路状态 */
#define YT_PHYSR_MASTER      (0x2000U) /*!< bit13 1=Master */
#define YT_PHYSR_SPEED_SHIFT (14U)     /*!< [15:14] 10=1000M 01=100M */

#define YT_EXT_RGMII_CFG1    (0x9001U)
#define YT_EXT_SQI_DEBUG     (0x3074U)
#define YT_RGMII_RX_DLY_EN   (0x0100U) /*!< [8] */
#define YT_RGMII_TX_CLK_SEL  (0x0200U) /*!< [9] */
#define YT_RGMII_TX_DLY_SHIFT (4U)     /*!< [7:4] */
#define YT_RGMII_TX_DLY_MASK (0x00F0U)

#define YT_MMD1_MODE_REG     (0x0834U) /*!< MMD1：force_config(bit14)/force_phy_type(bits[3:0]) */
#define YT_MMD7_AN_CTRL      (0x0200U) /*!< MMD7：自协商控制，bit12 = AN enable */

#define YT_PHYID1_EXPECT     (0x4F51U) /*!< §4.1.3：ID1 = 0x4F51 */
#define YT_PHYID2_OUI        (0x3AU)   /*!< §4.1.4：ID2[15:10] = 0x3A */

/* ========================================================================
 * 运行状态
 * ======================================================================== */
typedef struct {
    boolean                          Init;
    const EthTrcv_TrcvConfigType    *Cfg;
    Eth_ModeType                     Mode;
    EthTrcv_LinkStateType            Link;      /*!< 已上报的链路状态 */
    EthTrcv_BaudRateType             BaudRate;  /*!< 已解析的速率 */
    boolean                          Master;    /*!< 已解析的主从 */
    uint32                           PhyId1;
    uint32                           PhyId2;
    uint32                           PollCnt;
    EthTrcv_LinkStateType            PendingLink;
    uint8                            DebounceCnt;
} EthTrcv_DriverStateType;

static EthTrcv_DriverStateType s_trcv[ETHTRCV_TRCV_COUNT];
static boolean s_initialized = FALSE;

#define TRCV_DET(idx, api, err)  MCAL_DET_REPORT(ETHTRCV_MODULE_ID, (uint8)(idx), (api), (err))

/* ========================================================================
 * 内部：取状态 + MDIO 基本读写（带重试）
 * ======================================================================== */
static EthTrcv_DriverStateType *trcv_state(uint8 TrcvIdx, uint8 ApiId)
{
    if (TrcvIdx >= ETHTRCV_TRCV_COUNT) {
        TRCV_DET(TrcvIdx, ApiId, ETHTRCV_E_INV_TRCV_IDX);
        return NULL;
    }
    if (!s_initialized) {
        TRCV_DET(TrcvIdx, ApiId, ETHTRCV_E_UNINIT);
        return NULL;
    }
    return &s_trcv[TrcvIdx];
}

static Std_ReturnType trcv_reg_read(uint8 TrcvIdx, uint8 Reg, uint16 *Val)
{
    const EthTrcv_TrcvConfigType *cfg = s_trcv[TrcvIdx].Cfg;
    uint8 retry;

    for (retry = 0U; retry < ETHTRCV_MDIO_RETRY; retry++) {
        if (Eth_ReadMii(cfg->CtrlIdx, 0U, Reg, Val) == E_OK) {
            return E_OK;
        }
    }
    return E_NOT_OK;
}

static Std_ReturnType trcv_reg_write(uint8 TrcvIdx, uint8 Reg, uint16 Val)
{
    const EthTrcv_TrcvConfigType *cfg = s_trcv[TrcvIdx].Cfg;
    uint8 retry;

    for (retry = 0U; retry < ETHTRCV_MDIO_RETRY; retry++) {
        if (Eth_WriteMii(cfg->CtrlIdx, 0U, Reg, Val) == E_OK) {
            return E_OK;
        }
    }
    return E_NOT_OK;
}

/*! EXT 寄存器读（应用说明 §2.1.2：写 0x1E=EXT 地址，读 0x1F） */
static Std_ReturnType trcv_ext_read(uint8 TrcvIdx, uint16 ExtAddr, uint16 *Val)
{
    if (trcv_reg_write(TrcvIdx, YT_EXT_ADDR, ExtAddr) != E_OK) {
        return E_NOT_OK;
    }
    return trcv_reg_read(TrcvIdx, YT_EXT_DATA, Val);
}

static Std_ReturnType trcv_ext_write(uint8 TrcvIdx, uint16 ExtAddr, uint16 Val)
{
    if (trcv_reg_write(TrcvIdx, YT_EXT_ADDR, ExtAddr) != E_OK) {
        return E_NOT_OK;
    }
    return trcv_reg_write(TrcvIdx, YT_EXT_DATA, Val);
}

/*! 等待 PHY 软复位完成（BMCR[15] 自清） */
static Std_ReturnType trcv_wait_reset_done(uint8 TrcvIdx)
{
    uint32 waited = 0U;
    uint16 bmcr = 0U;

    while (waited < ETHTRCV_RESET_TIMEOUT_MS) {
        if (trcv_reg_read(TrcvIdx, YT_BMCR, &bmcr) == E_OK) {
            if ((bmcr & YT_BMCR_RESET) == 0U) {
                return E_OK;
            }
        }
        board_delay_ms(1U);
        waited++;
    }
    return E_NOT_OK;
}

/*! 读 PHY 链路/速率/主从原始状态（PHYSR 0x11，应用说明 §5.2） */
static Std_ReturnType trcv_read_physr(uint8 TrcvIdx, uint16 *Physr)
{
    return trcv_reg_read(TrcvIdx, YT_PHYSR, Physr);
}

static EthTrcv_BaudRateType trcv_decode_speed(uint16 physr)
{
    switch ((physr >> YT_PHYSR_SPEED_SHIFT) & 0x3U) {
    case 0x2U:
        return ETHTRCV_BAUD_RATE_1000MBIT;
    case 0x1U:
        return ETHTRCV_BAUD_RATE_100MBIT;
    default:
        return ETHTRCV_BAUD_RATE_1000MBIT;   /* 未解析时按配置默认 */
    }
}

/*! 把 PBcfg 的速率/主从/RGMII 延迟写进 PHY（应用说明 §5.3/§5.4） */
static Std_ReturnType trcv_apply_config(uint8 TrcvIdx)
{
    const EthTrcv_TrcvConfigType *cfg = s_trcv[TrcvIdx].Cfg;
    uint16 ext = 0U;
    uint16 mode = 0U;
    Std_ReturnType ret = E_NOT_OK;

    /* 1) RGMII 时钟延迟（EXT 0x9001） */
    if (trcv_ext_read(TrcvIdx, YT_EXT_RGMII_CFG1, &ext) != E_OK) {
        return E_NOT_OK;
    }
    ext &= (uint16) ~(YT_RGMII_RX_DLY_EN | YT_RGMII_TX_CLK_SEL | YT_RGMII_TX_DLY_MASK);
    if (cfg->RxDelayEn) {
        ext |= YT_RGMII_RX_DLY_EN;
    }
    if (cfg->TxClkSel) {
        ext |= YT_RGMII_TX_CLK_SEL;
    }
    ext |= (uint16) (((uint16) cfg->TxDelaySel << YT_RGMII_TX_DLY_SHIFT) & YT_RGMII_TX_DLY_MASK);
    if (trcv_ext_write(TrcvIdx, YT_EXT_RGMII_CFG1, ext) != E_OK) {
        return E_NOT_OK;
    }

    if (cfg->AutoNegotiation) {
        /* 2a) 自协商：MMD7 0x200 bit12=1，然后 BMCR=0x9140（软复位 + AN） */
        if (Eth_WriteMmd(cfg->CtrlIdx, 0U, 7U, YT_MMD7_AN_CTRL, 0x1000U) != E_OK) {
            return E_NOT_OK;
        }
        if (trcv_reg_write(TrcvIdx, YT_BMCR, YT_BMCR_RESET_AN) != E_OK) {
            return E_NOT_OK;
        }
    } else {
        /* 2b) 强制模式：MMD1 0x834 = force_config(Master) | force_phy_type(速率)，
         *     再写 0x8140/0xA100 做“带速率编码的软复位”（只写 0x8000 会退回上电默认） */
        mode = (uint16) ((cfg->Master ? 0x4000U : 0x0000U) |
                         ((cfg->DefaultBaudRate == ETHTRCV_BAUD_RATE_1000MBIT) ? 0x0001U : 0x0000U));
        if (Eth_WriteMmd(cfg->CtrlIdx, 0U, 1U, YT_MMD1_MODE_REG, mode) != E_OK) {
            return E_NOT_OK;
        }
        if (trcv_reg_write(TrcvIdx, YT_BMCR,
                           (cfg->DefaultBaudRate == ETHTRCV_BAUD_RATE_1000MBIT) ? YT_BMCR_FORCE_1000M
                                                                               : YT_BMCR_FORCE_100M) != E_OK) {
            return E_NOT_OK;
        }
    }

    if (trcv_wait_reset_done(TrcvIdx) == E_OK) {
        ret = E_OK;
    }
    return ret;
}

/* ========================================================================
 * API 实现
 * ======================================================================== */

/*! EthTrcv_Init（0x01） */
void EthTrcv_Init(const EthTrcv_ConfigType *CfgPtr)
{
    uint8 t;

    if ((CfgPtr == NULL) || (CfgPtr->TrcvConfigPtr == NULL) || (CfgPtr->TrcvCount == 0U) ||
        (CfgPtr->TrcvCount > ETHTRCV_TRCV_COUNT)) {
        TRCV_DET(0U, ETHTRCV_API_ID_INIT, ETHTRCV_E_PARAM_POINTER);
        return;
    }

    for (t = 0U; t < CfgPtr->TrcvCount; t++) {
        EthTrcv_DriverStateType *st = &s_trcv[t];
        const EthTrcv_TrcvConfigType *cfg = &CfgPtr->TrcvConfigPtr[t];
        uint16 id1 = 0U, id2 = 0U, physr = 0U;

        (void) memset(st, 0, sizeof(*st));
        st->Cfg = cfg;
        st->Mode = ETH_MODE_DOWN;
        st->Link = ETHTRCV_LINK_STATE_DOWN;
        st->PendingLink = ETHTRCV_LINK_STATE_DOWN;
        st->BaudRate = cfg->DefaultBaudRate;
        st->Master = cfg->Master;

        /* 1) 读 PHY ID 自检（ID1=0x4F51，ID2[15:10]=0x3A） */
        if ((trcv_reg_read(t, YT_PHYID1, &id1) != E_OK) || (trcv_reg_read(t, YT_PHYID2, &id2) != E_OK)) {
            TRCV_DET(t, ETHTRCV_API_ID_INIT, ETHTRCV_E_MDI_COMMUNICATION);
            continue;
        }
        st->PhyId1 = id1;
        st->PhyId2 = id2;
        if ((id1 != YT_PHYID1_EXPECT) || (((id2 >> 10) & 0x3FU) != YT_PHYID2_OUI)) {
            TRCV_DET(t, ETHTRCV_API_ID_INIT, ETHTRCV_E_MDI_COMMUNICATION);
            continue;
        }

        /* 2) 应用配置（速率/主从/RGMII 延迟） */
        if (trcv_apply_config(t) != E_OK) {
            TRCV_DET(t, ETHTRCV_API_ID_INIT, ETHTRCV_E_MDI_COMMUNICATION);
            continue;
        }

        /* 3) 读一次状态，缓存速率/主从 */
        if (trcv_read_physr(t, &physr) == E_OK) {
            st->BaudRate = trcv_decode_speed(physr);
            st->Master = ((physr & YT_PHYSR_MASTER) != 0U);
            st->Link = ((physr & YT_PHYSR_LINK) != 0U) ? ETHTRCV_LINK_STATE_ACTIVE
                                                       : ETHTRCV_LINK_STATE_DOWN;
            st->PendingLink = st->Link;
        }
        st->Init = TRUE;
        st->Mode = ETH_MODE_ACTIVE;
    }
    s_initialized = TRUE;
}

/*! EthTrcv_SetTransceiverMode（0xA8） */
Std_ReturnType EthTrcv_SetTransceiverMode(uint8 TrcvIdx, Eth_ModeType TrcvMode)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_SET_TRANSCEIVER_MODE);
    uint16 bmcr = 0U;

    if (st == NULL) {
        return E_NOT_OK;
    }
    if ((TrcvMode != ETH_MODE_ACTIVE) && (TrcvMode != ETH_MODE_DOWN)) {
        TRCV_DET(TrcvIdx, ETHTRCV_API_ID_SET_TRANSCEIVER_MODE, ETHTRCV_E_NOT_SUPPORTED);
        return E_NOT_OK;
    }
    if (trcv_reg_read(TrcvIdx, YT_BMCR, &bmcr) != E_OK) {
        return E_NOT_OK;
    }
    if (TrcvMode == ETH_MODE_DOWN) {
        bmcr |= YT_BMCR_PWR_DOWN;
        st->Mode = ETH_MODE_DOWN;
    } else {
        bmcr &= (uint16) ~YT_BMCR_PWR_DOWN;
        st->Mode = ETH_MODE_ACTIVE;
    }
    return trcv_reg_write(TrcvIdx, YT_BMCR, bmcr);
}

/*! EthTrcv_GetTransceiverMode（0x04） */
Std_ReturnType EthTrcv_GetTransceiverMode(uint8 TrcvIdx, Eth_ModeType *TrcvModePtr)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_GET_TRANSCEIVER_MODE);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (TrcvModePtr == NULL) {
        TRCV_DET(TrcvIdx, ETHTRCV_API_ID_GET_TRANSCEIVER_MODE, ETHTRCV_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    *TrcvModePtr = st->Mode;
    return E_OK;
}

/*! EthTrcv_TransceiverLinkStateRequest（0x02）：YT8011AN 无强制建链能力 */
Std_ReturnType EthTrcv_TransceiverLinkStateRequest(uint8 TrcvIdx, EthTrcv_LinkStateType LinkState)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_TRANSCEIVER_LINK_STATE_REQUEST);

    (void) LinkState;
    if (st == NULL) {
        return E_NOT_OK;
    }
    TRCV_DET(TrcvIdx, ETHTRCV_API_ID_TRANSCEIVER_LINK_STATE_REQUEST, ETHTRCV_E_NOT_SUPPORTED);
    return E_NOT_OK;
}

/*! EthTrcv_CheckWakeup（0x0F）：本板 PHY 无 WAKE 引脚，仅支持寄存器查询 */
Std_ReturnType EthTrcv_CheckWakeup(uint8 TrcvIdx)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_CHECK_WAKEUP);

    if (st == NULL) {
        return E_NOT_OK;
    }
    return E_NOT_OK;   /* 未实现：见文档“待确认项” */
}

/*! EthTrcv_GetBusWuReason（0x17） */
Std_ReturnType EthTrcv_GetBusWuReason(uint8 TrcvIdx, EthTrcv_WakeupReasonType *WakeupReasonPtr)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_GET_BUS_WU_REASON);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (WakeupReasonPtr == NULL) {
        TRCV_DET(TrcvIdx, ETHTRCV_API_ID_GET_BUS_WU_REASON, ETHTRCV_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    *WakeupReasonPtr = ETHTRCV_WUR_NONE;
    return E_NOT_OK;
}

/*! EthTrcv_StartAutoNegotiation（0xA9） */
Std_ReturnType EthTrcv_StartAutoNegotiation(uint8 TrcvIdx)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_START_AUTO_NEGOTIATION);
    uint16 bmsr = 0U;
    uint32 waited = 0U;

    if (st == NULL) {
        return E_NOT_OK;
    }
    /* MMD7 0x200 bit12 = AN enable，然后 BMCR 置 AN 使能 + 重启 */
    if (Eth_WriteMmd(st->Cfg->CtrlIdx, 0U, 7U, YT_MMD7_AN_CTRL, 0x1000U) != E_OK) {
        return E_NOT_OK;
    }
    if (trcv_reg_write(TrcvIdx, YT_BMCR, (uint16) (YT_BMCR_AN_EN | YT_BMCR_AN_RESTART)) != E_OK) {
        return E_NOT_OK;
    }
    /* 等 AN 完成（最多 3 s，1000BASE-T1 建链通常 < 1 s） */
    while (waited < 3000U) {
        if (trcv_reg_read(TrcvIdx, YT_BMSR, &bmsr) == E_OK) {
            if ((bmsr & YT_BMSR_ANEG_DONE) != 0U) {
                return E_OK;
            }
        }
        board_delay_ms(10U);
        waited += 10U;
    }
    return E_NOT_OK;
}

/*! EthTrcv_GetLinkState（0x06） */
Std_ReturnType EthTrcv_GetLinkState(uint8 TrcvIdx, EthTrcv_LinkStateType *LinkStatePtr)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_GET_LINK_STATE);
    uint16 physr = 0U;

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (LinkStatePtr == NULL) {
        TRCV_DET(TrcvIdx, ETHTRCV_API_ID_GET_LINK_STATE, ETHTRCV_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    if (trcv_read_physr(TrcvIdx, &physr) != E_OK) {
        *LinkStatePtr = ETHTRCV_LINK_STATE_DOWN;
        return E_NOT_OK;
    }
    *LinkStatePtr = ((physr & YT_PHYSR_LINK) != 0U) ? ETHTRCV_LINK_STATE_ACTIVE
                                                    : ETHTRCV_LINK_STATE_DOWN;
    return E_OK;
}

/*! EthTrcv_GetBaudRate（0x07） */
Std_ReturnType EthTrcv_GetBaudRate(uint8 TrcvIdx, EthTrcv_BaudRateType *BaudRatePtr)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_GET_BAUD_RATE);
    uint16 physr = 0U;

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (BaudRatePtr == NULL) {
        TRCV_DET(TrcvIdx, ETHTRCV_API_ID_GET_BAUD_RATE, ETHTRCV_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    if (trcv_read_physr(TrcvIdx, &physr) == E_OK) {
        st->BaudRate = trcv_decode_speed(physr);
    }
    *BaudRatePtr = st->BaudRate;
    return E_OK;
}

/*! EthTrcv_GetDuplexMode（0x08）：1000/100BASE-T1 恒为全双工 */
Std_ReturnType EthTrcv_GetDuplexMode(uint8 TrcvIdx, EthTrcv_DuplexModeType *DuplexModePtr)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_GET_DUPLEX_MODE);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (DuplexModePtr == NULL) {
        TRCV_DET(TrcvIdx, ETHTRCV_API_ID_GET_DUPLEX_MODE, ETHTRCV_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    *DuplexModePtr = ETHTRCV_DUPLEX_MODE_FULL;
    return E_OK;
}

/*! EthTrcv_GetPhyIdentifier（0x15） */
Std_ReturnType EthTrcv_GetPhyIdentifier(uint8 TrcvIdx, uint32 *OrgUniqueIdPtr,
                                        uint8 *ModelNrPtr, uint8 *RevisionNrPtr)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_GET_PHY_IDENTIFIER);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if ((OrgUniqueIdPtr == NULL) || (ModelNrPtr == NULL) || (RevisionNrPtr == NULL)) {
        TRCV_DET(TrcvIdx, ETHTRCV_API_ID_GET_PHY_IDENTIFIER, ETHTRCV_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    /* IEEE 802.3 §22.2.4.3：OUI = ID1[15:0] << 6 | ID2[15:10] */
    *OrgUniqueIdPtr = (st->PhyId1 << 6) | ((st->PhyId2 >> 10) & 0x3FU);
    *ModelNrPtr = (uint8) ((st->PhyId2 >> 4) & 0x3FU);
    *RevisionNrPtr = (uint8) (st->PhyId2 & 0x0FU);
    return E_OK;
}

/*! EthTrcv_SetPhyTestMode（0x11）：YT8011AN 未公开 T1 测试模式寄存器 */
Std_ReturnType EthTrcv_SetPhyTestMode(uint8 TrcvIdx, EthTrcv_PhyTestModeType Mode)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_SET_PHY_TEST_MODE);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (Mode == ETHTRCV_PHYTESTMODE_NONE) {
        return E_OK;
    }
    TRCV_DET(TrcvIdx, ETHTRCV_API_ID_SET_PHY_TEST_MODE, ETHTRCV_E_NOT_SUPPORTED);
    return E_NOT_OK;
}

/*! EthTrcv_SetPhyLoopbackMode（0x12）：NONE / INTERNAL（BMCR[14]） */
Std_ReturnType EthTrcv_SetPhyLoopbackMode(uint8 TrcvIdx, EthTrcv_PhyLoopbackModeType Mode)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_SET_PHY_LOOPBACK_MODE);
    uint16 bmcr = 0U;

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (trcv_reg_read(TrcvIdx, YT_BMCR, &bmcr) != E_OK) {
        return E_NOT_OK;
    }
    switch (Mode) {
    case ETHTRCV_PHYLOOPBACK_NONE:
        bmcr &= (uint16) ~YT_BMCR_LOOPBACK;
        break;
    case ETHTRCV_PHYLOOPBACK_INTERNAL:
        bmcr |= YT_BMCR_LOOPBACK;
        break;
    default:
        TRCV_DET(TrcvIdx, ETHTRCV_API_ID_SET_PHY_LOOPBACK_MODE, ETHTRCV_E_NOT_SUPPORTED);
        return E_NOT_OK;
    }
    return trcv_reg_write(TrcvIdx, YT_BMCR, bmcr);
}

/*! EthTrcv_GetPhySignalQuality（0x10）：读 EXT 0x3074 SQI 调试寄存器 */
Std_ReturnType EthTrcv_GetPhySignalQuality(uint8 TrcvIdx, uint32 *SignalQualityPtr)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_GET_PHY_SIGNAL_QUALITY);
    uint16 val = 0U;

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (SignalQualityPtr == NULL) {
        TRCV_DET(TrcvIdx, ETHTRCV_API_ID_GET_PHY_SIGNAL_QUALITY, ETHTRCV_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    if (trcv_ext_read(TrcvIdx, YT_EXT_SQI_DEBUG, &val) != E_OK) {
        return E_NOT_OK;
    }
    *SignalQualityPtr = val;
    return E_OK;
}

/*! EthTrcv_SetPhyTxMode（0x13）：仅 NORMAL 支持 */
Std_ReturnType EthTrcv_SetPhyTxMode(uint8 TrcvIdx, EthTrcv_PhyTxModeType Mode)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_SET_PHY_TX_MODE);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (Mode == ETHTRCV_PHYTXMODE_NORMAL) {
        return E_OK;
    }
    TRCV_DET(TrcvIdx, ETHTRCV_API_ID_SET_PHY_TX_MODE, ETHTRCV_E_NOT_SUPPORTED);
    return E_NOT_OK;
}

/*! EthTrcv_RunCableDiagnostic（0x16）：单对线 T1 PHY 无标准线缆诊断 */
Std_ReturnType EthTrcv_RunCableDiagnostic(uint8 TrcvIdx)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_RUN_CABLE_DIAGNOSTIC);

    if (st == NULL) {
        return E_NOT_OK;
    }
    TRCV_DET(TrcvIdx, ETHTRCV_API_ID_RUN_CABLE_DIAGNOSTIC, ETHTRCV_E_NOT_SUPPORTED);
    return E_NOT_OK;
}

/*! EthTrcv_GetCableDiagnosticsResult（0x14） */
Std_ReturnType EthTrcv_GetCableDiagnosticsResult(uint8 TrcvIdx,
                                                 EthTrcv_CableDiagResultType *ResultPtr)
{
    EthTrcv_DriverStateType *st = trcv_state(TrcvIdx, ETHTRCV_API_ID_GET_CABLE_DIAGNOSTICS_RESULT);

    if (st == NULL) {
        return E_NOT_OK;
    }
    if (ResultPtr == NULL) {
        TRCV_DET(TrcvIdx, ETHTRCV_API_ID_GET_CABLE_DIAGNOSTICS_RESULT, ETHTRCV_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    *ResultPtr = ETHTRCV_CABLEDIAG_ERROR;
    return E_NOT_OK;
}

/*! EthTrcv_GetMacMethod（0x18，DRAFT） */
Std_ReturnType EthTrcv_GetMacMethod(uint8 *TrcvIdx, EthTrcv_MacMethodType *MacModePtr)
{
    uint8 idx;

    if ((TrcvIdx == NULL) || (MacModePtr == NULL)) {
        TRCV_DET(0U, ETHTRCV_API_ID_GET_MAC_METHOD, ETHTRCV_E_PARAM_POINTER);
        return E_NOT_OK;
    }
    idx = *TrcvIdx;
    if (idx >= ETHTRCV_TRCV_COUNT) {
        TRCV_DET(idx, ETHTRCV_API_ID_GET_MAC_METHOD, ETHTRCV_E_INV_TRCV_IDX);
        return E_NOT_OK;
    }
    if (!s_initialized) {
        TRCV_DET(idx, ETHTRCV_API_ID_GET_MAC_METHOD, ETHTRCV_E_UNINIT);
        return E_NOT_OK;
    }
    *MacModePtr = s_trcv[idx].Cfg->MacMethod;
    return E_OK;
}

/*! EthTrcv_GetVersionInfo（0x0B） */
void EthTrcv_GetVersionInfo(Std_VersionInfoType *VersionInfoPtr)
{
    if (VersionInfoPtr == NULL) {
        TRCV_DET(0U, ETHTRCV_API_ID_GET_VERSION_INFO, ETHTRCV_E_PARAM_POINTER);
        return;
    }
    VersionInfoPtr->vendorID = (uint16) MCAL_VENDOR_ID;
    VersionInfoPtr->moduleID = (uint16) ETHTRCV_MODULE_ID;
    VersionInfoPtr->sw_major_version = (uint8) ETHTRCV_SW_MAJOR_VERSION;
    VersionInfoPtr->sw_minor_version = (uint8) ETHTRCV_SW_MINOR_VERSION;
    VersionInfoPtr->sw_patch_version = (uint8) ETHTRCV_SW_PATCH_VERSION;
}

/*! EthTrcv_MainFunction（0x0C）：轮询链路 → 通知 EthIf → 联动 Eth 速率 */
void EthTrcv_MainFunction(void)
{
    uint8 t;

    if (!s_initialized) {
        return;
    }
    for (t = 0U; t < ETHTRCV_TRCV_COUNT; t++) {
        EthTrcv_DriverStateType *st = &s_trcv[t];
        uint16 physr = 0U;
        EthTrcv_LinkStateType raw;
        EthTrcv_LinkStateType reported;

        if (!st->Init) {
            continue;
        }
        st->PollCnt++;
        if ((st->Cfg->LinkPollDivider != 0U) && ((st->PollCnt % st->Cfg->LinkPollDivider) != 0U)) {
            continue;
        }
        if (trcv_read_physr(t, &physr) != E_OK) {
            continue;
        }
        raw = ((physr & YT_PHYSR_LINK) != 0U) ? ETHTRCV_LINK_STATE_ACTIVE : ETHTRCV_LINK_STATE_DOWN;

        /* 去抖：连续 ETHTRCV_LINK_DEBOUNCE_COUNT 次同一状态才采纳 */
        if (raw == st->PendingLink) {
            if (st->DebounceCnt < 0xFFU) {
                st->DebounceCnt++;
            }
        } else {
            st->PendingLink = raw;
            st->DebounceCnt = 0U;
        }
        if (st->DebounceCnt < ETHTRCV_LINK_DEBOUNCE_COUNT) {
            continue;
        }

        reported = st->Link;
        if (raw != reported) {
            st->Link = raw;
            if (raw == ETHTRCV_LINK_STATE_ACTIVE) {
                st->BaudRate = trcv_decode_speed(physr);
                st->Master = ((physr & YT_PHYSR_MASTER) != 0U);
                /* 链路起来了：把 PHY 协商结果同步给 MAC（FES/DM 位） */
                (void) Eth_SetLineSpeed(st->Cfg->CtrlIdx,
                                        (st->BaudRate == ETHTRCV_BAUD_RATE_100MBIT) ? 100U : 1000U,
                                        TRUE);
            }
#if (ETHTRCV_USE_ETHIF_CALLBACK == STD_ON)
            EthIf_TrcvLinkStateChg(t, st->Link);
#endif
        }
    }
}

/*! EthTrcv_MainFunctionMacSec（0x25）：无 MACsec，空实现 */
void EthTrcv_MainFunctionMacSec(void)
{
}

#if (ETHTRCV_USE_ETHIF_CALLBACK == STD_ON)
__attribute__((weak)) void EthIf_TrcvLinkStateChg(uint8 TrcvIdx, EthTrcv_LinkStateType LinkState)
{
    (void) TrcvIdx;
    (void) LinkState;
}
#endif
