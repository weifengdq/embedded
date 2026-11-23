/*
 * app_lwip_eth_hw_mcal.c - lwIP netif 的 MCAL Eth 硬件后端
 *
 * 只做"帧进出控制器"这件事，全部通过 mcal/Eth 的公共 API：
 *   Eth_ProvideTxBuffer / Eth_Transmit / Eth_GetRxBuffer / Eth_ReleaseRxBuffer /
 *   Eth_GetPhysAddr / Eth_GetControllerMode / Eth_ReadMii / Eth_MainFunction / Eth_GetCounterValues
 * 不 include 任何 SDK enet 驱动头，也不碰 Eth 的内部实现（第 50 章的"分层"卖点）。
 *
 * TX 走 Eth.h 契约里的扩展语义：PhysAddrPtr == NULL 时 BufPtr 指向整帧起始，
 *   LenByte = 整帧长度，驱动负责补 0 到 60 B / cache clean / 交 DMA。
 *   ProvideTxBuffer 的 in/out 参数 LenBytePtr 是"净荷长度"口径，所以申请时传
 *   frame_len - 14；返回的 BufPtr 仍是帧起始（见 Eth.h 头部说明）。
 *
 * RX 走 Eth_GetRxBuffer()：返回 DMA 环内真实缓冲（含 14 B 头，驱动已 invalidate），
 *   用 BufIdx 当 cookie，lwIP 释放 pbuf 时 Eth_ReleaseRxBuffer() 归还。
 *
 * 链路状态：EthTrcv 还没就绪前，直接读 PHY 标准寄存器（BMSR 0x01 连读两次取当前值、
 *   PHYSR 0x11 取速率/主从，YT8011AN 应用说明 §5.2）。EthTrcv 落地后这里换成
 *   EthTrcv_GetLinkState()/EthTrcv_GetBaudRate()，netif 层不用改。
 *
 * 作者：eth-stack（task-2）
 */
#include <string.h>
#include <stdio.h>

#include "lwip/opt.h"

#include "Eth.h"
#include "EthTrcv.h"
#include "Eth_GeneralTypes.h"

#include "app_lwip_eth_hw.h"

#if !NO_SYS
#include "FreeRTOS.h"
#include "semphr.h"
#endif

/* EthTrcv 的收发器索引（Eth_ReadMii 的 TrcvIdx / EthTrcv_GetLinkState 的 TrcvIdx，都是"索引"不是 PHY 地址） */
/* EthTrcv 的收发器索引（EthTrcv_GetLinkState 的 TrcvIdx，不是 MDIO 地址） */
#ifndef APP_LWIP_ETH_ETHTRCV_IDX
#define APP_LWIP_ETH_ETHTRCV_IDX     (0U)
#endif

/* YT8011AN 标准寄存器（应用说明 §5.2） */
#define YT8011_REG_BMSR         (0x01U)
#define YT8011_REG_PHYSR        (0x11U)
#define YT8011_BMSR_LINK        (0x0004U)
#define YT8011_PHYSR_LINK       (0x0400U)   /* 0x11[10] real-time link */
#define YT8011_PHYSR_MASTER     (0x2000U)   /* 0x11[13] master */
#define YT8011_PHYSR_SPEED_SHIFT (14U)      /* 0x11[15:14]: 2=1000M, 1=100M */

static app_lwip_eth_hw_stats_t s_hw;
static bool s_ready;

#if !NO_SYS
static SemaphoreHandle_t s_eth_lock;
#endif

/* ------------------------------------------------------------------ */
/* 串行化：RTOS 下 TX 在 tcpip 线程、RX 在收包任务，两个上下文都会进 Eth 驱动 */
/* ------------------------------------------------------------------ */
static void mcal_eth_lock(uint8_t ctrl_idx)
{
#if !NO_SYS
    (void) ctrl_idx;
    if (s_eth_lock != NULL) {
        (void) xSemaphoreTake(s_eth_lock, portMAX_DELAY);
    }
#else
    (void) ctrl_idx;
#endif
}

static void mcal_eth_unlock(uint8_t ctrl_idx)
{
#if !NO_SYS
    (void) ctrl_idx;
    if (s_eth_lock != NULL) {
        (void) xSemaphoreGive(s_eth_lock);
    }
#else
    (void) ctrl_idx;
#endif
}

/* ------------------------------------------------------------------ */
/* 初始化 / 轮询                                                       */
/* ------------------------------------------------------------------ */
static int mcal_eth_init(uint8_t ctrl_idx)
{
    Eth_ModeType mode = ETH_MODE_DOWN;

    if (Eth_GetControllerMode(ctrl_idx, &mode) != E_OK) {
        return -1;
    }
    if (mode != ETH_MODE_ACTIVE) {
        /* 例程应先 Eth_Init(&Eth_Config) 再 Eth_SetControllerMode(ctrl, ETH_MODE_ACTIVE) */
        return -1;
    }
#if !NO_SYS
    if (s_eth_lock == NULL) {
        s_eth_lock = xSemaphoreCreateMutex();
        if (s_eth_lock == NULL) {
            return -1;
        }
    }
#endif
    memset(&s_hw, 0, sizeof(s_hw));
    s_ready = true;
    return 0;
}

static void mcal_eth_poll(uint8_t ctrl_idx)
{
    (void) ctrl_idx;
    Eth_MainFunction();
}

static void mcal_eth_get_mac(uint8_t ctrl_idx, uint8_t mac[6])
{
    Eth_GetPhysAddr(ctrl_idx, mac);
}

/* ------------------------------------------------------------------ */
/* TX                                                                  */
/* ------------------------------------------------------------------ */
static uint8_t *mcal_eth_tx_buffer_get(uint8_t ctrl_idx, uint16_t frame_len, void **cookie_out)
{
    Eth_BufIdxType buf_idx = ETH_BUFIDX_INVALID;
    uint8_t *buf = NULL;
    uint16_t grant;
    BufReq_ReturnType ret;

    if ((frame_len <= ETH_HEADER_LEN) || (frame_len > ETH_MAX_FRAME_LEN)) {
        s_hw.tx_err++;
        return NULL;
    }
    /* in: 期望净荷长度（Eth.h 契约：LenBytePtr 是净荷口径） */
    grant = (uint16_t) (frame_len - ETH_HEADER_LEN);
    ret = Eth_ProvideTxBuffer(ctrl_idx, 0U, &buf_idx, &buf, &grant);
    if (ret != BUFREQ_OK) {
        if (ret == BUFREQ_E_BUSY) {
            s_hw.tx_busy++;
        } else {
            s_hw.tx_err++;
        }
        return NULL;
    }
    *cookie_out = (void *) (uintptr_t) buf_idx;
    return buf;
}

static int mcal_eth_tx_commit(uint8_t ctrl_idx, void *cookie, uint16_t frame_len)
{
    Std_ReturnType ret;

    /* PhysAddrPtr = NULL：整帧已在缓冲里，驱动只补 0/clean/交 DMA（Eth.h 扩展语义） */
    ret = Eth_Transmit(ctrl_idx, (Eth_BufIdxType) (uintptr_t) cookie, 0U, FALSE, frame_len, NULL);
    if (ret != E_OK) {
        s_hw.tx_err++;
        return -1;
    }
    s_hw.tx_frames++;
    s_hw.tx_bytes += frame_len;
    return 0;
}

/* ------------------------------------------------------------------ */
/* RX                                                                  */
/* ------------------------------------------------------------------ */
static int mcal_eth_rx_get(uint8_t ctrl_idx, const uint8_t **frame_out, uint16_t *len_out,
                           void **cookie_out)
{
    Eth_BufIdxType buf_idx = ETH_BUFIDX_INVALID;
    const uint8_t *data = NULL;
    uint16_t len = 0U;
    Eth_FrameType frame_type = 0U;
    Eth_RxStatusType st;

    st = Eth_GetRxBuffer(ctrl_idx, 0U, &buf_idx, &data, &len, &frame_type);
    if (st == ETH_NOT_RECEIVED) {
        s_hw.rx_empty++;
        return 1;
    }
    if ((data == NULL) || (len == 0U)) {
        s_hw.rx_err++;
        return -1;
    }
    *frame_out = data;
    *len_out = len;
    *cookie_out = (void *) (uintptr_t) buf_idx;
    s_hw.rx_frames++;
    s_hw.rx_bytes += len;
    return 0;
}

static void mcal_eth_rx_release(uint8_t ctrl_idx, void *cookie)
{
    Eth_ReleaseRxBuffer(ctrl_idx, (Eth_BufIdxType) (uintptr_t) cookie);
}

/* ------------------------------------------------------------------ */
/* 链路状态（PHY 标准寄存器，EthTrcv 就绪前的过渡实现）                  */
/* ------------------------------------------------------------------ */
static void mcal_eth_link_get(uint8_t ctrl_idx, app_lwip_eth_hw_link_t *out)
{
    EthTrcv_LinkStateType link_state = ETHTRCV_LINK_STATE_DOWN;
    EthTrcv_BaudRateType baud = ETHTRCV_BAUD_RATE_1000MBIT;
    EthTrcv_DuplexModeType duplex = ETHTRCV_DUPLEX_MODE_FULL;
    Eth_ModeType mode = ETH_MODE_DOWN;
    uint16_t physr = 0U;

    out->up = false;
    out->known = false;
    out->speed_mbps = 0U;
    out->full_duplex = true;
    out->master = false;

    if (!s_ready) {
        return;
    }
    if (Eth_GetControllerMode(ctrl_idx, &mode) != E_OK) {
        return;
    }
    if (mode != ETH_MODE_ACTIVE) {
        out->known = true;
        return;
    }
    /* 链路状态只问 EthTrcv（PHY 的 RGMII 延迟/主从/速率都归它管，本层不碰 PHY 配置） */
    if (EthTrcv_GetLinkState(APP_LWIP_ETH_ETHTRCV_IDX, &link_state) != E_OK) {
        return;
    }
    out->known = true;
    out->up = (link_state == ETHTRCV_LINK_STATE_ACTIVE);
    if (!out->up) {
        return;
    }
    if (EthTrcv_GetBaudRate(APP_LWIP_ETH_ETHTRCV_IDX, &baud) == E_OK) {
        out->speed_mbps = (baud == ETHTRCV_BAUD_RATE_100MBIT) ? 100U : 1000U;
    }
    if (EthTrcv_GetDuplexMode(APP_LWIP_ETH_ETHTRCV_IDX, &duplex) == E_OK) {
        out->full_duplex = (duplex == ETHTRCV_DUPLEX_MODE_FULL);
    }
    /* 主从：EthTrcv 没有对应 API，读 PHYSR 0x11[13]（只读，不配置） */
    if (Eth_ReadMii(ctrl_idx, APP_LWIP_ETH_ETHTRCV_IDX, YT8011_REG_PHYSR, &physr) == E_OK) {
        out->master = ((physr & YT8011_PHYSR_MASTER) != 0U);
    }
}

/* ------------------------------------------------------------------ */
/* 打印 / 统计                                                         */
/* ------------------------------------------------------------------ */
static void mcal_eth_print(uint8_t ctrl_idx)
{
    Eth_CounterType ctr;
    Eth_RxStatsType rx;
    Eth_TxStatsType tx;
    uint16_t bmsr = 0U;
    uint16_t physr = 0U;
    uint16_t gbsr = 0U;
    uint16_t bmcr = 0U;

    memset(&ctr, 0, sizeof(ctr));
    memset(&rx, 0, sizeof(rx));
    memset(&tx, 0, sizeof(tx));
    (void) Eth_GetCounterValues(ctrl_idx, &ctr);
    (void) Eth_GetRxStats(ctrl_idx, &rx);
    (void) Eth_GetTxStats(ctrl_idx, &tx);
    (void) Eth_ReadMii(ctrl_idx, APP_LWIP_ETH_ETHTRCV_IDX, 0x00U, &bmcr);
    (void) Eth_ReadMii(ctrl_idx, APP_LWIP_ETH_ETHTRCV_IDX, 0x01U, &bmsr);
    (void) Eth_ReadMii(ctrl_idx, APP_LWIP_ETH_ETHTRCV_IDX, 0x01U, &bmsr);
    (void) Eth_ReadMii(ctrl_idx, APP_LWIP_ETH_ETHTRCV_IDX, 0x0AU, &gbsr);
    (void) Eth_ReadMii(ctrl_idx, APP_LWIP_ETH_ETHTRCV_IDX, 0x11U, &physr);
    printf("[mcal-eth] PHY BMCR=%04X BMSR=%04X GBSR=%04X PHYSR=%04X\r\n",
           (unsigned) bmcr, (unsigned) bmsr, (unsigned) gbsr, (unsigned) physr);
    printf("[mcal-eth] ETH counter: rx_pkts=%lu tx_pkts=%lu drop_ovr=%lu crc=%lu undersize=%lu oversize=%lu\r\n",
           (unsigned long) rx.RxStatsPkts, (unsigned long) (tx.TxUniCastPkts + tx.TxNUcastPkts),
           (unsigned long) ctr.DropPktBufOverrun, (unsigned long) ctr.DropPktCrc,
           (unsigned long) ctr.UndersizePkt, (unsigned long) ctr.OversizePkt);
    /* 发送侧错误：ErrOtbdPkt=TDES0.ES 汇总，HwDepCtr3=TDES0.UF（发送下溢）。
     * 高帧率下如果 UF 增长，说明 MAC 的 TX FIFO 被抽空（DMA 供给不足），
     * 会在线上表现为对端 CRC 错 —— 和 §50.9.15 的 5% CRC 错排查对应。 */
    printf("[mcal-eth] ETH txerr: es=%lu underflow=%lu defer=%lu latecoll=%lu | disc_otbd=%lu\r\n",
           (unsigned long) ctr.ErrOtbdPkt, (unsigned long) ctr.HwDepCtr3,
           (unsigned long) ctr.DfrdPkt, (unsigned long) ctr.LatCollPkt,
           (unsigned long) ctr.DiscOtbdPkt);
}

static void mcal_eth_stats_get(uint8_t ctrl_idx, app_lwip_eth_hw_stats_t *out)
{
    (void) ctrl_idx;
    *out = s_hw;
}

/* ------------------------------------------------------------------ */
/* ops 表                                                              */
/* ------------------------------------------------------------------ */
static const app_lwip_eth_hw_ops_t s_mcal_ops = {
    .name = "mcal-eth",
    .max_frame_len = ETH_MAX_FRAME_LEN,      /* 1518 */
    .rx_buf_count = ETH_RX_BUF_COUNT,        /* 16 */
    .tx_buf_count = ETH_TX_BUF_COUNT,        /* 8 */
    .init = mcal_eth_init,
    .poll = mcal_eth_poll,
    .get_mac = mcal_eth_get_mac,
    .tx_buffer_get = mcal_eth_tx_buffer_get,
    .tx_commit = mcal_eth_tx_commit,
    .rx_get = mcal_eth_rx_get,
    .rx_release = mcal_eth_rx_release,
    .link_get = mcal_eth_link_get,
    .print = mcal_eth_print,
    .stats_get = mcal_eth_stats_get,
    .lock = mcal_eth_lock,
    .unlock = mcal_eth_unlock,
};

const app_lwip_eth_hw_ops_t *app_lwip_eth_hw_mcal(void)
{
    return &s_mcal_ops;
}
