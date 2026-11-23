/*
 * can_fd_echo —— AUTOSAR Can 驱动 CAN FD 长帧回环（CAN0 <-> CAN1）
 *
 * 硬件：hpm5e31_1，CAN0(PD00/PD01) <-> CAN1(PD05/PD04)，TCAN1044AV，两端 120 Ω。
 * 驱动：mcal/Can（AUTOSAR Can_Init/SetBaudrate/SetControllerMode/Write/MainFunction_*）
 * 配置：位时序来自 Can_PBcfg.c 的 baudrate 表
 *       ID2 = FD 仲裁 500 kbit/s / 数据 2 Mbit/s，BRS 开，TDC 15/15
 *       ID3 = FD 仲裁 500 kbit/s / 数据 5 Mbit/s，BRS 开，TDC 20/20
 * 用法：上电自动跑 64B ping-pong + 各 DLC 单帧延迟；命令可重跑。
 */
#include "bsp.h"
#include "Can.h"
#include "Can_PBcfg.h"
#include "Det.h"
#include "hpm_mcan_drv.h"
#include <stdio.h>
#include <string.h>

#define CTRL_A   0U
#define CTRL_B   1U
#define ID_FD    0x123U /* PBcfg 过滤器 #0：0x123 精确命中 RXFIFO0 */
#define HTH_OF(ctrl) ((Can_HwHandleType) ((ctrl) * 4U))

#define RXQ_LEN 64U
typedef struct {
    uint8     ctrl;
    Can_IdType id;
    uint8     len;
    uint8     data[64];
} rx_rec_t;

static rx_rec_t s_rxq[RXQ_LEN];
static volatile uint32 s_rx_head;
static volatile uint32 s_rx_tail;
static volatile uint32 s_rx_total;
static volatile uint32 s_rx_overflow;
static volatile uint32 s_tx_confirmed;
static volatile uint32 s_busoff_cnt;
static uint32 s_cpu_mhz;
static uint32 s_dbg_mismatch;

static uint32_t arg_u32(char *s, uint32_t def)
{
    uint32_t v = def;
    if (s != NULL) {
        (void) bsp_parse_u32(s, &v);
    }
    return v;
}

/* ===================== 驱动回调 ===================== */
void CanIf_RxIndication(const Can_HwType *Mailbox, const PduInfoType *PduInfoPtr)
{
    uint32 next = (s_rx_head + 1U) % RXQ_LEN;
    if ((Mailbox == NULL_PTR) || (PduInfoPtr == NULL_PTR)) {
        return;
    }
    s_rx_total++;
    if (next == s_rx_tail) {
        s_rx_overflow++;
        return;
    }
    s_rxq[s_rx_head].ctrl = Mailbox->ControllerId;
    s_rxq[s_rx_head].id = Mailbox->CanId;
    s_rxq[s_rx_head].len = (uint8) PduInfoPtr->SduLength;
    if (s_rxq[s_rx_head].len > 64U) {
        s_rxq[s_rx_head].len = 64U;
    }
    (void) memcpy(s_rxq[s_rx_head].data, PduInfoPtr->SduDataPtr, s_rxq[s_rx_head].len);
    s_rx_head = next;
}

void CanIf_TxConfirmation(PduIdType CanTxPduId)
{
    (void) CanTxPduId;
    s_tx_confirmed++;
}

void CanIf_ControllerBusOff(uint8 ControllerId)
{
    (void) ControllerId;
    s_busoff_cnt++;
}

void CanIf_ControllerModeIndication(uint8 ControllerId, Can_ControllerStateType ControllerMode)
{
    printf("  [mode] ctrl=%u -> %u\r\n", (unsigned) ControllerId, (unsigned) ControllerMode);
}

/* ===================== 工具 ===================== */
static uint64_t cyc_now(void)
{
    return (uint64_t) hpm_csr_get_core_mcycle();
}

static uint32_t cyc_to_us(uint64_t c)
{
    return (uint32_t) (c / (uint64_t) s_cpu_mhz);
}

static bool rxq_pop(rx_rec_t *out)
{
    if (s_rx_tail == s_rx_head) {
        return false;
    }
    *out = s_rxq[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1U) % RXQ_LEN;
    return true;
}

static void rxq_drain_all(void)
{
    uint32 i;
    for (i = 0U; i < 8U; i++) {
        Can_MainFunction_Read();
    }
    s_rx_tail = s_rx_head;
}

/* FD 载荷：byte0..3 序号小端，其余 = (seq + i) 低 8 位 */
static void fill_fd(uint8 *buf, uint8 len, uint32 seq)
{
    uint8 i;
    for (i = 0U; i < 4U; i++) {
        buf[i] = (uint8) (seq >> (8U * i));
    }
    for (i = 4U; i < len; i++) {
        buf[i] = (uint8) (seq + i);
    }
}

static bool fd_match(const uint8 *buf, uint8 len, uint8 want_len, uint32 seq)
{
    uint32 v;
    uint8 i;
    if (len != want_len || len < 4U) {
        return false;
    }
    v = (uint32) buf[0] | ((uint32) buf[1] << 8) | ((uint32) buf[2] << 16) | ((uint32) buf[3] << 24);
    if (v != seq) {
        return false;
    }
    for (i = 4U; i < len; i++) {
        if (buf[i] != (uint8) (seq + i)) {
            return false;
        }
    }
    return true;
}

static uint8 can_send(uint8 ctrl, Can_IdType id, const uint8 *data, uint8 len, uint32 timeout_us)
{
    Can_PduType pdu;
    uint64_t t0 = cyc_now();

    pdu.swPduHandle = (PduIdType) (id & CAN_ID_VALUE_MASK);
    pdu.length = len;
    pdu.id = id;
    pdu.sdu = (uint8 *) data;
    for (;;) {
        if (Can_Write(HTH_OF(ctrl), &pdu) == CAN_OK) {
            return 0U;
        }
        Can_MainFunction_Write();
        Can_MainFunction_Read();
        if (cyc_to_us(cyc_now() - t0) > timeout_us) {
            return 1U;
        }
    }
}

static uint8 can_recv(uint8 ctrl, Can_IdType id, uint8 want_len, uint32 seq, uint32 timeout_us)
{
    uint64_t t0 = cyc_now();
    rx_rec_t r;

    for (;;) {
        Can_MainFunction_Read();
        Can_MainFunction_Write();
        if (rxq_pop(&r)) {
            if ((r.ctrl == ctrl) && (r.id == id) && fd_match(r.data, r.len, want_len, seq)) {
                return 0U;
            }
            if (s_dbg_mismatch < 3U) {
                s_dbg_mismatch++;
                printf("[mismatch] want ctrl=%u id=0x%08X len=%u seq=%u | got ctrl=%u id=0x%08X len=%u d0..3=%02X %02X %02X %02X\r\n",
                       (unsigned) ctrl, (unsigned) id, (unsigned) want_len, (unsigned) seq,
                       (unsigned) r.ctrl, (unsigned) r.id, (unsigned) r.len,
                       r.data[0], r.data[1], r.data[2], r.data[3]);
            }
            return 2U;
        }
        if (cyc_to_us(cyc_now() - t0) > timeout_us) {
            return 1U;
        }
    }
}

/* 切到某个波特率配置：必须 STOPPED（SWS_Can_00256） */
static Std_ReturnType cfg_switch(uint16 baud_id)
{
    Std_ReturnType st = E_OK;
    Can_ControllerStateType mode = CAN_CS_UNINIT;
    /* 已经是 STOPPED 就不要再发一次：SWS_Can_00409 把重复 STOPPED 当非法迁移（DET） */
    (void) Can_GetControllerMode(CTRL_A, &mode);
    if (mode != CAN_CS_STOPPED) {
        if (Can_SetControllerMode(CTRL_A, CAN_CS_STOPPED) != E_OK) {
            st = E_NOT_OK;
        }
    }
    (void) Can_GetControllerMode(CTRL_B, &mode);
    if (mode != CAN_CS_STOPPED) {
        if (Can_SetControllerMode(CTRL_B, CAN_CS_STOPPED) != E_OK) {
            st = E_NOT_OK;
        }
    }
    if (Can_SetBaudrate(CTRL_A, baud_id) != E_OK) {
        st = E_NOT_OK;
    }
    if (Can_SetBaudrate(CTRL_B, baud_id) != E_OK) {
        st = E_NOT_OK;
    }
    if (Can_SetControllerMode(CTRL_A, CAN_CS_STARTED) != E_OK) {
        st = E_NOT_OK;
    }
    if (Can_SetControllerMode(CTRL_B, CAN_CS_STARTED) != E_OK) {
        st = E_NOT_OK;
    }
    return st;
}

/* ===================== 测试 1：各 DLC 单帧 FD ping-pong ===================== */
static void bench_sizes(uint32 n, uint32 nom_kbps, uint32 data_kbps)
{
    static const uint8 sizes[] = { 8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U };
    uint8 k;

    printf("FD sizes (arb %u kbit/s / data %u kbit/s, BRS on):\r\n",
           (unsigned) nom_kbps, (unsigned) data_kbps);
    printf("  len  dlc   ok/total   avg_us   max_us\r\n");
    for (k = 0U; k < (uint8) (sizeof(sizes) / sizeof(sizes[0])); k++) {
        uint8 len = sizes[k];
        uint8 buf[64];
        uint32 ok = 0U, i;
        uint64_t sum = 0U;
        uint32 mx = 0U;
        uint64_t t0 = cyc_now();

        rxq_drain_all();
        for (i = 0U; i < n; i++) {
            uint64_t ts;
            uint32 dt;
            fill_fd(buf, len, i);
            if (can_send(CTRL_A, CAN_ID_FD_STD_FRAME | ID_FD, buf, len, 5000U) != 0U) {
                break;
            }
            ts = cyc_now();
            if (can_recv(CTRL_B, CAN_ID_FD_STD_FRAME | ID_FD, len, i, 5000U) != 0U) {
                break;
            }
            if (can_send(CTRL_B, CAN_ID_FD_STD_FRAME | ID_FD, buf, len, 5000U) != 0U) {
                break;
            }
            if (can_recv(CTRL_A, CAN_ID_FD_STD_FRAME | ID_FD, len, i, 5000U) != 0U) {
                break;
            }
            dt = cyc_to_us(cyc_now() - ts);
            sum += dt;
            if (dt > mx) {
                mx = dt;
            }
            ok++;
        }
        (void) t0;
        printf("  %3u  %3u   %u/%u      %u       %u\r\n", (unsigned) len,
               (unsigned) (len <= 8U ? len : (len == 12U ? 9U : (len == 16U ? 10U : (len == 20U ? 11U :
                   (len == 24U ? 12U : (len == 32U ? 13U : (len == 48U ? 14U : 15U))))))),
               (unsigned) ok, (unsigned) n,
               (unsigned) (ok ? (uint32) (sum / ok) : 0U), (unsigned) mx);
    }
    printf("rx_total=%u rx_overflow=%u tx_confirmed=%u\r\n", (unsigned) s_rx_total,
           (unsigned) s_rx_overflow, (unsigned) s_tx_confirmed);
}

/* ===================== 测试 2：64B 单向流水（FD 吞吐） ===================== */
static void bench_stream(uint32 n, uint8 len, uint32 nom_kbps, uint32 data_kbps)
{
    uint8 buf[64];
    uint32 sent = 0U, got = 0U, bad = 0U, i;
    uint64_t t0;

    rxq_drain_all();
    s_rx_total = 0U;
    s_rx_overflow = 0U;
    t0 = cyc_now();
    for (i = 0U; i < n; i++) {
        rx_rec_t r;
        fill_fd(buf, len, i);
        if (can_send(CTRL_A, CAN_ID_FD_STD_FRAME | ID_FD, buf, len, 5000U) != 0U) {
            break;
        }
        sent++;
        Can_MainFunction_Read();
        while (rxq_pop(&r)) {
            if ((r.ctrl == CTRL_B) && (r.id == (CAN_ID_FD_STD_FRAME | ID_FD))) {
                got++;
            } else {
                bad++;
            }
        }
    }
    {
        uint64_t tw = cyc_now();
        while ((cyc_to_us(cyc_now() - tw) < 20000U) && (s_rx_total < sent)) {
            rx_rec_t r;
            Can_MainFunction_Read();
            while (rxq_pop(&r)) {
                if ((r.ctrl == CTRL_B) && (r.id == (CAN_ID_FD_STD_FRAME | ID_FD))) {
                    got++;
                } else {
                    bad++;
                }
            }
        }
    }
    {
        uint32 total_us = cyc_to_us(cyc_now() - t0);
        double fps = (double) sent * 1000000.0 / (double) (total_us ? total_us : 1U);
        printf("FD stream %uB: sent=%u rx_driver=%u rx_app=%u bad=%u total=%uus => %.0f frame/s\r\n",
               (unsigned) len, (unsigned) sent, (unsigned) s_rx_total, (unsigned) got,
               (unsigned) bad, (unsigned) total_us, fps);
        printf("  payload=%.2f Mbit/s (uid=%u B), 仲裁 %u kbit/s / 数据 %u kbit/s\r\n",
               fps * (double) len * 8.0 / 1000000.0, (unsigned) len,
               (unsigned) nom_kbps, (unsigned) data_kbps);
    }
    printf("  rx_overflow=%u tx_confirmed=%u\r\n", (unsigned) s_rx_overflow, (unsigned) s_tx_confirmed);
}

/* 诊断: 打印 MCAN 关键寄存器 (CCCR/NBTP/DBTP/TDCR/PSR/ECR) 与 TDCV */
static void dump_ctrl(uint8 ctrl)
{
    MCAN_Type *base = (MCAN_Type *) ((ctrl == 0U) ? BOARD_CAN0_BASE : BOARD_CAN1_BASE);
    uint32 psr = base->PSR;
    printf("  [regs] CAN%u CCCR=0x%08lX NBTP=0x%08lX DBTP=0x%08lX TDCR=0x%08lX PSR=0x%08lX ECR=0x%08lX\r\n",
           (unsigned) ctrl, (unsigned long) base->CCCR, (unsigned long) base->NBTP,
           (unsigned long) base->DBTP, (unsigned long) base->TDCR,
           (unsigned long) psr, (unsigned long) base->ECR);
    printf("         LEC=%lu TDCV=%lu mtq (PSR[22:16]), DLEC=%lu\r\n",
           (unsigned long) (psr & 0x7U), (unsigned long) ((psr >> 16) & 0x7FU),
           (unsigned long) ((psr >> 24) & 0x7U));
}

/* ===================== 测试 3：不同 FD 位时序下 64B ping-pong（能力边界） ===================== */
static void bench_at(uint16 baud_id, uint32 n, const char *tag)
{
    Std_ReturnType st = cfg_switch(baud_id);
    uint8 buf[64];
    uint32 ok = 0U, i;
    uint64_t sum = 0U;
    uint32 mx = 0U;

    printf("--- %s (baud cfg ID%u) ---\r\n", tag, (unsigned) baud_id);
    if (st != E_OK) {
        printf("  cfg_switch failed st=%u\r\n", (unsigned) st);
        return;
    }
    rxq_drain_all();
    for (i = 0U; i < n; i++) {
        uint64_t ts;
        uint32 dt;
        fill_fd(buf, 64U, i);
        if (can_send(CTRL_A, CAN_ID_FD_STD_FRAME | ID_FD, buf, 64U, 5000U) != 0U) {
            break;
        }
        ts = cyc_now();
        if (can_recv(CTRL_B, CAN_ID_FD_STD_FRAME | ID_FD, 64U, i, 5000U) != 0U) {
            break;
        }
        if (can_send(CTRL_B, CAN_ID_FD_STD_FRAME | ID_FD, buf, 64U, 5000U) != 0U) {
            break;
        }
        if (can_recv(CTRL_A, CAN_ID_FD_STD_FRAME | ID_FD, 64U, i, 5000U) != 0U) {
            break;
        }
        dt = cyc_to_us(cyc_now() - ts);
        sum += dt;
        if (dt > mx) {
            mx = dt;
        }
        ok++;
    }
    {
        Can_ErrorStateType es = CAN_ERRORSTATE_ACTIVE;
        uint8 rec = 0U, tec = 0U;
        (void) Can_GetControllerErrorState(CTRL_A, &es);
        (void) Can_GetControllerRxErrorCounter(CTRL_A, &rec);
        (void) Can_GetControllerTxErrorCounter(CTRL_A, &tec);
        dump_ctrl(0U);
        printf("  64B pingpong: ok=%u/%u avg=%uus max=%uus | CAN0 errstate=%u REC=%u TEC=%u\r\n",
               (unsigned) ok, (unsigned) n, (unsigned) (ok ? (uint32) (sum / ok) : 0U), (unsigned) mx,
               (unsigned) es, (unsigned) rec, (unsigned) tec);
    }
    if (ok == 0U) {
        /* 出问题就停掉再重配，避免 bus-off 卡住后续用例 */
        (void) Can_SetControllerMode(CTRL_A, CAN_CS_STOPPED);
        (void) Can_SetControllerMode(CTRL_B, CAN_CS_STOPPED);
    }
}

/* ===================== 命令 ===================== */
static void cmd_can(int argc, char *argv[])
{
    const char *sub = (argc >= 2) ? argv[1] : "info";

    if (strcmp(sub, "info") == 0) {
        printf("sample: can_fd_echo   CAN0 <-> CAN1, TCAN1044AV, FD arb 500k / data 2M&5M\r\n");
        printf("id2: BRS on, TDC ssp=15 filt=15, presc(data)=2 seg1=14 seg2=5 (75%%, 2 Mbit/s)\r\n");
        printf("id3: BRS on, TDC ssp=12 filt=12, presc(data)=1 seg1=11 seg2=4 (75%%, 5 Mbit/s)\r\n");
        printf("id4: BRS on, no TDC, data 1 Mbit/s (presc=2 seg1=19 seg2=5)\r\n");
        printf("id5: BRS on, auto TDC, arb 1M / data 2M\r\n");
        printf("DLC: 9..15 = 12/16/20/24/32/48/64 bytes\r\n");
        printf("cpu: %u MHz\r\n", (unsigned) s_cpu_mhz);
    } else if (strcmp(sub, "sizes") == 0) {
        static const uint32 nom_tab[19] = { 500U,500U,500U,500U,500U,1000U,500U,500U,500U,500U,
                                            500U,500U,500U,1000U,500U,500U,500U,500U,500U };
        static const uint32 dat_tab[19] = { 0U,0U,2000U,5000U,1000U,2000U,5000U,5000U,5000U,5000U,
                                            5000U,5000U,5000U,5000U,2000U,5000U,2000U,2000U,5000U };
        uint32 n = (uint32) arg_u32((argc >= 3) ? argv[2] : NULL, 50U);
        uint32 id = (uint32) arg_u32((argc >= 4) ? argv[3] : NULL, 2U);
        if (id > 18U) {
            printf("baud id range 0..18\r\n");
            return;
        }
        if (cfg_switch((uint16) id) != E_OK) {
            printf("sizes: cfg_switch(%u) failed\r\n", (unsigned) id);
            return;
        }
        bench_sizes(n, nom_tab[id], dat_tab[id]);
    } else if (strcmp(sub, "stream") == 0) {
        uint32 n = (uint32) arg_u32((argc >= 3) ? argv[2] : NULL, 500U);
        uint32 id = (uint32) arg_u32((argc >= 4) ? argv[3] : NULL, 2U);
        if (cfg_switch((uint16) id) != E_OK) {
            printf("stream: cfg_switch(%u) failed\r\n", (unsigned) id);
            return;
        }
        {
            static const uint32 nom_tab[19] = { 500U,500U,500U,500U,500U,1000U,500U,500U,500U,500U,
                                                500U,500U,500U,1000U,500U,500U,500U,500U,500U };
            static const uint32 dat_tab[19] = { 0U,0U,2000U,5000U,1000U,2000U,5000U,5000U,5000U,5000U,
                                                5000U,5000U,5000U,5000U,2000U,5000U,2000U,2000U,5000U };
            printf("stream @ baud cfg ID%u\r\n", (unsigned) id);
            bench_stream(n, 64U, nom_tab[id], dat_tab[id]);
        }
    } else if (strcmp(sub, "at") == 0) {
        uint32 n = (uint32) arg_u32((argc >= 3) ? argv[2] : NULL, 50U);
        uint32 id = (uint32) arg_u32((argc >= 4) ? argv[3] : NULL, 4U);
        if (id > 18U) {
            printf("baud id range 0..5\r\n");
            return;
        }
        bench_at((uint16) id, n, "FD probe");
    } else if (strcmp(sub, "matrix") == 0) {
        static const struct { uint16 id; const char *tag; } mx[] = {
            { 2U,  "FD 500k/2M  TDCO=15 (已知可用基线)" },
            { 16U, "FD 500k/2M  SP80 TDCO=16" },
            { 17U, "FD 500k/2M  SP85 TDCO=17" },
            { 4U,  "FD 500k/1M  no TDC" },
            { 6U,  "FD 500k/5M  SP75 TDCO=12" },
            { 7U,  "FD 500k/5M  SP75 TDCO=8" },
            { 8U,  "FD 500k/5M  SP75 TDCO=10" },
            { 9U,  "FD 500k/5M  SP75 TDCO=13" },
            { 10U, "FD 500k/5M  SP75 TDCO=14" },
            { 11U, "FD 500k/5M  SP80 TDCO=12" },
            { 12U, "FD 500k/5M  SP875 TDCO=13" },
            { 13U, "FD 1M/5M    SP75 TDCO=12" },
            { 3U,  "FD 500k/5M  SP75 TDCO=12 (默认配置,已修正)" },
            { 14U, "FD 500k/2M  TDCO=1 (测 TDCV)" },
            { 15U, "FD 500k/5M  TDCO=1 (测 TDCV)" },
            { 18U, "FD 500k/5M  TDCO=20 (旧故障配置,驱动拒绝)" },
        };
        uint32 n = (uint32) arg_u32((argc >= 3) ? argv[2] : NULL, 20U);
        uint32 k;
        printf("FD 矩阵：64B ping-pong，每项 %u 轮（A->B 与 B->A 各一帧为 1 轮）\r\n",
               (unsigned) n);
        for (k = 0U; k < (uint32) (sizeof(mx) / sizeof(mx[0])); k++) {
            bench_at(mx[k].id, n, mx[k].tag);
        }
        (void) cfg_switch(2U);
    } else if (strcmp(sub, "probe") == 0) {
        uint32 n = (uint32) arg_u32((argc >= 3) ? argv[2] : NULL, 50U);
        bench_at(4U, n, "FD 500k/1M (no TDC)");
        bench_at(2U, n, "FD 500k/2M (TDC 15/15)");
        bench_at(3U, n, "FD 500k/5M (TDC 20/20)");
        (void) cfg_switch(2U);
    } else if (strcmp(sub, "stat") == 0) {
        uint8 c;
        for (c = 0U; c < 2U; c++) {
            Can_ControllerStateType mode = CAN_CS_UNINIT;
            Can_ErrorStateType es = CAN_ERRORSTATE_ACTIVE;
            uint8 rec = 0U, tec = 0U;
            (void) Can_GetControllerMode(c, &mode);
            (void) Can_GetControllerErrorState(c, &es);
            (void) Can_GetControllerRxErrorCounter(c, &rec);
            (void) Can_GetControllerTxErrorCounter(c, &tec);
            printf("CAN%u: mode=%u errstate=%u REC=%u TEC=%u\r\n", (unsigned) c, (unsigned) mode,
                   (unsigned) es, (unsigned) rec, (unsigned) tec);
        }
        printf("rx_total=%u rx_overflow=%u tx_confirmed=%u busoff=%u det=%u\r\n",
               (unsigned) s_rx_total, (unsigned) s_rx_overflow, (unsigned) s_tx_confirmed,
               (unsigned) s_busoff_cnt, (unsigned) Det_GetErrorCount());
    } else if (strcmp(sub, "err") == 0) {
        uint32 before = Det_GetErrorCount();
        Can_PduType pdu;
        uint8 d[8] = { 0 };
        pdu.swPduHandle = 0U;
        pdu.length = 12U; /* 经典帧长度 > 8：DET CAN_E_PARAM_DATA_LENGTH */
        pdu.id = 0x123U;
        pdu.sdu = d;
        (void) Can_Write(HTH_OF(CTRL_A), &pdu);
        (void) Can_SetBaudrate(CTRL_A, 0x77U);           /* CAN_E_PARAM_BAUDRATE */
        (void) Can_GetControllerMode(0U, NULL_PTR);      /* CAN_E_PARAM_POINTER */
        (void) Can_GetControllerMode(0xFEU, NULL_PTR);   /* CAN_E_PARAM_CONTROLLER */
        printf("DET: before=%u after=%u delta=%u\r\n", (unsigned) before,
               (unsigned) Det_GetErrorCount(), (unsigned) (Det_GetErrorCount() - before));
    } else {
        printf("unknown sub: %s (info|probe|at n id|sizes|stream|stat|err)\r\n", sub);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 can, cmd_can, CAN FD echo: can info|sizes [n]|stream [n]|5m [n]|stat|err);

int main(void)
{
    Std_ReturnType st;

    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    Det_Init();

    printf("\r\n=== can_fd_echo: AUTOSAR Can driver, CAN FD, CAN0 <-> CAN1 ===\r\n");
    Can_Init(&Can_Config);
    st = cfg_switch(2U);
    printf("[start] FD 500k/2M STARTED st=%u, controllers=%u hth=%u filters=%u\r\n", (unsigned) st,
           (unsigned) Can_Config.ControllerCount, (unsigned) Can_Config.HthCount,
           (unsigned) Can_Config.FilterCount);
    if (st == E_OK) {
        bench_at(4U, 50U, "FD 500k/1M (no TDC)");
        bench_at(2U, 50U, "FD 500k/2M (TDC 15/15)");
        bench_at(3U, 20U, "FD 500k/5M (TDC 20/20)");
        (void) cfg_switch(2U);
        bench_sizes(50U, 500U, 2000U);
        bench_stream(500U, 64U, 500U, 2000U);
    } else {
        printf("start failed, check transceiver/wiring\r\n");
    }
    printf("type 'can probe' / 'can sizes' / 'can stream 500' / 'can stat' / 'can err'\r\n");
    while (1) {
        bsp_shell_poll();
        Can_MainFunction_Write();
        Can_MainFunction_Read();
        Can_MainFunction_BusOff();
        Can_MainFunction_Mode();
    }
    return 0;
}