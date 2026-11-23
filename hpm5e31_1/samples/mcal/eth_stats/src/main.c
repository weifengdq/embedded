/*
 * eth_stats —— AUTOSAR Eth 计数器口径与错误路径（Det）实测
 *
 * 目的：把 SWS 的四个统计 API（GetCounterValues / GetRxStats / GetTxStats /
 * GetTxErrorCounterValues）与 DET 错误路径都跑一遍，给出"计数在什么情况下涨"的证据。
 *
 * 命令：stat | stat snap | det | busy | types
 * 串口自证格式：
 *   STAT tx_frames=... rx_frames=... crc=... ovr=... drop=...
 *   DET n=... last=(mod,inst,api,err)
 */
#include "bsp.h"
#include "Eth.h"
#include "Eth_PBcfg.h"
#include "EthTrcv.h"
#include "EthTrcv_PBcfg.h"
#include "Det.h"
#include <stdio.h>
#include <string.h>

#define CTRL      0U
#define TRCV      0U
#define TEST_ETYPE 0x88B5U

static Eth_CounterType    s_c0;
static Eth_RxStatsType    s_rx0;
static Eth_TxStatsType    s_tx0;
static Eth_TxErrorCounterValuesType s_txe0;
static boolean s_have_snap;

static void snapshot(void)
{
    (void) Eth_GetCounterValues(CTRL, &s_c0);
    (void) Eth_GetRxStats(CTRL, &s_rx0);
    (void) Eth_GetTxStats(CTRL, &s_tx0);
    (void) Eth_GetTxErrorCounterValues(CTRL, &s_txe0);
    s_have_snap = TRUE;
}

static void print_stat(boolean delta)
{
    Eth_CounterType c;
    Eth_RxStatsType rx;
    Eth_TxStatsType tx;
    Eth_TxErrorCounterValuesType txe;

    (void) Eth_GetCounterValues(CTRL, &c);
    (void) Eth_GetRxStats(CTRL, &rx);
    (void) Eth_GetTxStats(CTRL, &tx);
    (void) Eth_GetTxErrorCounterValues(CTRL, &txe);

    if (delta && s_have_snap) {
        printf("STAT d_tx_frames=%u d_rx_frames=%u d_crc=%u d_ovr=%u d_drop_in=%u d_drop_busy=%u d_err_in=%u\r\n",
               (unsigned) (tx.TxUniCastPkts - s_tx0.TxUniCastPkts),
               (unsigned) (rx.RxStatsPkts - s_rx0.RxStatsPkts),
               (unsigned) (c.DropPktCrc - s_c0.DropPktCrc),
               (unsigned) (c.DropPktBufOverrun - s_c0.DropPktBufOverrun),
               (unsigned) (c.DiscInbdPkt - s_c0.DiscInbdPkt),
               (unsigned) (c.DiscOtbdPkt - s_c0.DiscOtbdPkt),
               (unsigned) (c.ErrInbdPkt - s_c0.ErrInbdPkt));
    } else {
        printf("STAT tx_frames=%u tx_bytes=%u rx_frames=%u rx_octets=%u crc=%u algn=%u ovr=%u err_in=%u\r\n",
               (unsigned) tx.TxUniCastPkts, (unsigned) tx.TxNumberOfOctets,
               (unsigned) rx.RxStatsPkts, (unsigned) rx.RxStatsOctets,
               (unsigned) c.DropPktCrc, (unsigned) c.AlgnmtErr,
               (unsigned) c.DropPktBufOverrun, (unsigned) c.ErrInbdPkt);
        printf("STAT rx buckets 64=%u 65-127=%u 128-255=%u 256-511=%u 512-1023=%u 1024-1518=%u bcast=%u ucast=%u\r\n",
               (unsigned) rx.RxStatsPkts64Octets, (unsigned) rx.RxStatsPkts65to127Octets,
               (unsigned) rx.RxStatsPkts128to255Octets, (unsigned) rx.RxStatsPkts256to511Octets,
               (unsigned) rx.RxStatsPkts512to1023Octets, (unsigned) rx.RxStatsPkts1024to1518Octets,
               (unsigned) rx.RxStatsBroadcastPkts, (unsigned) rx.RxUnicastFrames);
        printf("STAT txerr drop_noerr=%u drop_err=%u defer=%u late=%u underflow=%u\r\n",
               (unsigned) txe.TxDroppedNoErrorPkts, (unsigned) txe.TxDroppedErrorPkts,
               (unsigned) txe.TxDeferredTrans, (unsigned) txe.TxLateCollision,
               (unsigned) txe.TxUnderflowError);
    }
}

/*! DET 错误路径：逐个调非法 API，看 Det 计数与最后一条错误码 */
static void cmd_det(void)
{
    uint32 n0 = Det_GetErrorCount();
    uint8 buf[4] = { 0 };
    uint16 v = 0U;
    Eth_BufIdxType idx = 0U;
    uint8 *p = NULL;
    uint16 len = 64U;
    uint16 mi = 0U;
    uint16 m[6];

    /* 1) 空指针 */
    Eth_GetPhysAddr(CTRL, (uint8 *) 0);
    Eth_GetControllerMode(CTRL, NULL);
    (void) Eth_GetCounterValues(CTRL, NULL);
    (void) Eth_ReadMii(CTRL, 0U, 0x01U, NULL);
    /* 2) 非法控制器索引 */
    Eth_GetPhysAddr(9U, m);
    (void) Eth_SetControllerMode(9U, ETH_MODE_ACTIVE);
    /* 3) 非法模式 */
    (void) Eth_SetControllerMode(CTRL, (Eth_ModeType) 7U);
    /* 4) MII 非法 TrcvIdx（本板只有 0） */
    (void) Eth_ReadMii(CTRL, 1U, 0x01U, &v);
    (void) Eth_WriteMii(CTRL, 1U, 0x00U, 0U);
    /* 5) 非法 BufIdx 提交 */
    (void) Eth_Transmit(CTRL, 0xDEADBEEFU, (Eth_FrameType) TEST_ETYPE, FALSE, 64U, buf);
    /* 6) 非法缓冲句柄归还 */
    Eth_ReleaseRxBuffer(CTRL, 0x12345678U);
    /* 7) 非法 ProvideTxBuffer 指针 */
    (void) Eth_ProvideTxBuffer(CTRL, 0U, NULL, &p, &len);
    (void) Eth_ProvideTxBuffer(CTRL, 0U, &idx, &p, NULL);
    /* 8) 非法 MMD */
    (void) Eth_ReadMmd(CTRL, 1U, 1U, 0x0834U, &mi);

    {
        uint16 mod = 0U;
        uint8 inst = 0U, api = 0U, err = 0U;
        (void) Det_GetLastError(&mod, &inst, &api, &err);
        printf("DET n=%u last=(mod=%u,inst=%u,api=0x%02X,err=0x%02X)\r\n",
               (unsigned) (Det_GetErrorCount() - n0), (unsigned) mod, (unsigned) inst,
               (unsigned) api, (unsigned) err);
    }
}

/*! 把 8 个 TX 缓冲打满，制造 BUFREQ_E_BUSY（计入 DiscOtbdPkt） */
static void cmd_busy(void)
{
    Eth_BufIdxType idx;
    uint8 *p = NULL;
    uint16 len;
    uint8 i;
    uint32 busy = 0U;

    for (i = 0U; i < 20U; i++) {
        len = 64U;
        if (Eth_ProvideTxBuffer(CTRL, 0U, &idx, &p, &len) == BUFREQ_OK) {
            /* 只锁定不提交：故意不调 Eth_Transmit，占住缓冲 */
        } else {
            busy++;
        }
    }
    printf("BUSY locked_then_busy=%u (未提交的缓冲在 mode down/active 时才会释放)\r\n", (unsigned) busy);
    /* 用一次模式切换把锁定的缓冲全部释放（SWS_Eth_00280/00281） */
    (void) Eth_SetControllerMode(CTRL, ETH_MODE_DOWN);
    (void) Eth_SetControllerMode(CTRL, ETH_MODE_ACTIVE);
}

/*! 不同帧长的发送：验证长度分桶与最小帧补齐 */
static void cmd_types(void)
{
    static const uint16 lens[] = { 1U, 18U, 46U, 100U, 500U, 1000U, 1500U };
    uint8 k;
    uint8 dst[6] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU };

    for (k = 0U; k < (uint8) (sizeof(lens) / sizeof(lens[0])); k++) {
        Eth_BufIdxType idx;
        uint8 *p = NULL;
        uint16 grant = lens[k];

        if (Eth_ProvideTxBuffer(CTRL, 0U, &idx, &p, &grant) == BUFREQ_OK) {
            (void) memset(&p[ETH_HEADER_LEN], (int) (0x40U + k), lens[k]);
            if (Eth_Transmit(CTRL, idx, (Eth_FrameType) TEST_ETYPE, TRUE, lens[k], dst) != E_OK) {
                printf("TYPES len=%u transmit FAILED\r\n", (unsigned) lens[k]);
            }
        }
        Eth_TxConfirmation(CTRL);
    }
    printf("TYPES sent=%u (1/18/46/100/500/1000/1500 B payload; <46 应被补 0 到 60 B 帧)\r\n",
           (unsigned) (sizeof(lens) / sizeof(lens[0])));
}

static void cmd_stats(int argc, char *argv[])
{
    const char *sub = (argc >= 2) ? argv[1] : "print";

    if (strcmp(sub, "snap") == 0) {
        snapshot();
        printf("STAT snapshot taken\r\n");
    } else if (strcmp(sub, "delta") == 0) {
        print_stat(TRUE);
    } else if (strcmp(sub, "det") == 0) {
        cmd_det();
    } else if (strcmp(sub, "busy") == 0) {
        cmd_busy();
    } else if (strcmp(sub, "types") == 0) {
        cmd_types();
    } else {
        print_stat(FALSE);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 stat, cmd_stats, ETH counters/errors: stat|snap|delta|det|busy|types);

int main(void)
{
    bsp_init();
    printf("\r\n=== eth_stats: AUTOSAR Eth counters + DET error paths ===\r\n");
    Det_Init();
    Det_ClearErrorCount();
    Eth_Init(&Eth_Config);
    EthTrcv_Init(&EthTrcv_Config);
    (void) Eth_SetControllerMode(CTRL, ETH_MODE_ACTIVE);
    printf("[boot] det=%u\r\n", (unsigned) Det_GetErrorCount());
    print_stat(FALSE);
    while (1) {
        Eth_MainFunction();
        EthTrcv_MainFunction();
        bsp_shell_poll();
    }
}
