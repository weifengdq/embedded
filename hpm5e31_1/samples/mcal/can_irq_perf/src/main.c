/*
 * can_irq_perf —— AUTOSAR Can 驱动 4 路并发吞吐 / CPU 占用 / 延迟分布
 *
 * 拓扑（hpm5e31_1，两对 120 Ω 互连总线）：
 *   总线 A：CAN0(PD00/PD01) -> CAN1(PD05/PD04)
 *   总线 B：CAN2(PC08/PA09) -> CAN3(PC15/PC14)
 * 4 路 MCAN 同时工作：A、B 两组各一个发送端 + 一个接收端。
 *
 * 处理模型说明（重要）：本构建 Can_Cfg.h 里
 *   CAN_RX_PROCESSING_INTERRUPT=0、CAN_TX_CONFIRM_IN_INTERRUPT=0，
 * 即由应用周期调用 Can_MainFunction_Read/Write 完成收发推进（SWS 允许的两种模型之一）。
 * 因此下面测到的延迟 = 总线传输 + 主函数轮询间隔；CPU 占用 = 主函数里的 cycle 数占比。
 * 中断模型的实现见驱动（同一套状态机，仅把推进点搬到 ISR），第 47 章有对比与限制说明。
 *
 * 命令：can run [n]（单向流水，默认 1000）、can pp [n]（双向 ping-pong）、can stat
 */
#include "bsp.h"
#include "Can.h"
#include "Can_PBcfg.h"
#include "Det.h"
#include <stdio.h>
#include <string.h>

#define CTRL_A_TX 0U
#define CTRL_A_RX 1U
#define CTRL_B_TX 2U
#define CTRL_B_RX 3U
#define ID_REQ    0x123U
#define HTH_OF(ctrl) ((Can_HwHandleType) ((ctrl) * 4U))

#define N_MAX     1024U
#define RXQ_LEN   256U

typedef struct {
    uint8 ctrl;
    Can_IdType id;
    uint8 len;
    uint8 data[8];
} rx_rec_t;

static rx_rec_t s_rxq[RXQ_LEN];
static volatile uint32 s_rx_head;
static volatile uint32 s_rx_tail;
static volatile uint32 s_rx_total;
static volatile uint32 s_rx_ctrl[4];
static volatile uint32 s_tx_confirmed;
static uint32 s_cpu_mhz;

/* 延迟统计：按 (总线, 序号) 记录发送时刻的 cycle，接收回调里按序号回填 */
static uint32 s_tx_cyc[2][N_MAX];
static uint32 s_rx_cyc[2][N_MAX];
static volatile uint8 s_rx_seen[2][N_MAX];
static uint64 s_main_cyc; /* 主函数里花掉的 cycle */
static uint32 s_busy_retry;

static uint32_t arg_u32(char *s, uint32_t def)
{
    uint32_t v = def;
    if (s != NULL) {
        (void) bsp_parse_u32(s, &v);
    }
    return v;
}

static uint32_t cyc32(void)
{
    return (uint32_t) hpm_csr_get_core_mcycle();
}

static uint32_t cyc_to_us(uint64_t c)
{
    return (uint32_t) (c / (uint64_t) s_cpu_mhz);
}

void CanIf_RxIndication(const Can_HwType *Mailbox, const PduInfoType *PduInfoPtr)
{
    uint32 next = (s_rx_head + 1U) % RXQ_LEN;
    uint32 seq;
    uint8 ctrl;

    if ((Mailbox == NULL_PTR) || (PduInfoPtr == NULL_PTR)) {
        return;
    }
    ctrl = Mailbox->ControllerId;
    s_rx_total++;
    if (ctrl < 4U) {
        s_rx_ctrl[ctrl]++;
    }
    if ((PduInfoPtr->SduLength >= 4U) && (ctrl == CTRL_A_RX || ctrl == CTRL_B_RX)) {
        seq = (uint32) PduInfoPtr->SduDataPtr[0] | ((uint32) PduInfoPtr->SduDataPtr[1] << 8) |
              ((uint32) PduInfoPtr->SduDataPtr[2] << 16) | ((uint32) PduInfoPtr->SduDataPtr[3] << 24);
        if (seq < N_MAX) {
            uint8 pair = (ctrl == CTRL_A_RX) ? 0U : 1U;
            s_rx_cyc[pair][seq] = cyc32();
            s_rx_seen[pair][seq] = 1U;
        }
    }
    if (next == s_rx_tail) {
        return;
    }
    s_rxq[s_rx_head].ctrl = ctrl;
    s_rxq[s_rx_head].id = Mailbox->CanId;
    s_rxq[s_rx_head].len = (uint8) PduInfoPtr->SduLength;
    if (s_rxq[s_rx_head].len > 8U) {
        s_rxq[s_rx_head].len = 8U;
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
    printf("  [busoff] ctrl=%u\r\n", (unsigned) ControllerId);
}

/* 周期"任务"：CanIf 的 1 ms 主函数等价物 */
static void pump(void)
{
    uint32 t0 = cyc32();
    Can_MainFunction_Read();
    Can_MainFunction_Write();
    s_main_cyc += (uint64) (cyc32() - t0);
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

static uint8 send_one(uint8 ctrl, uint32 seq, uint32 timeout_us)
{
    Can_PduType pdu;
    uint8 d[8];
    uint64 t0 = cyc32();

    d[0] = (uint8) (seq >> 0);
    d[1] = (uint8) (seq >> 8);
    d[2] = (uint8) (seq >> 16);
    d[3] = (uint8) (seq >> 24);
    d[4] = 0xA5U;
    d[5] = 0x5AU;
    d[6] = (uint8) (seq & 0xFFU);
    d[7] = (uint8) ((seq >> 8) & 0xFFU);

    pdu.swPduHandle = (PduIdType) seq;
    pdu.length = 8U;
    pdu.id = ID_REQ;
    pdu.sdu = d;
    for (;;) {
        if (Can_Write(HTH_OF(ctrl), &pdu) == CAN_OK) {
            return 0U;
        }
        s_busy_retry++;
        Can_MainFunction_Write();
        if ((uint32) cyc_to_us((uint64) (cyc32() - t0)) > timeout_us) {
            return 1U;
        }
    }
}

/* 延迟直方图（单位 us） */
static void print_latency(uint8 pair, uint32 n)
{
    static const uint32 bucket_us[] = { 300U, 400U, 500U, 600U, 800U, 1000U, 2000U, 0xFFFFFFFFU };
    static const char *bucket_name[] = { "<300", "300-400", "400-500", "500-600", "600-800",
                                         "800-1000", "1000-2000", ">=2000" };
    uint32 cnt[8] = { 0 };
    uint32 i, k, got = 0U;
    uint64 sum = 0U;
    uint32 mx = 0U;

    for (i = 0U; i < n; i++) {
        uint32 lat;
        if (s_rx_seen[pair][i] == 0U) {
            continue;
        }
        lat = (uint32) (((uint64) (s_rx_cyc[pair][i] - s_tx_cyc[pair][i])) / (uint64) s_cpu_mhz);
        sum += lat;
        if (lat > mx) {
            mx = lat;
        }
        got++;
        for (k = 0U; k < 8U; k++) {
            if (lat < bucket_us[k]) {
                cnt[k]++;
                break;
            }
        }
    }
    if (got == 0U) {
        printf("  pair %c: no samples\r\n", pair ? 'B' : 'A');
        return;
    }
    printf("  pair %c: n=%u avg=%uus max=%uus  histogram:\r\n", pair ? 'B' : 'A', (unsigned) got,
           (unsigned) (uint32) (sum / got), (unsigned) mx);
    for (k = 0U; k < 8U; k++) {
        if (cnt[k] != 0U) {
            printf("    %-10s %5u (%5.1f%%)\r\n", bucket_name[k], (unsigned) cnt[k],
                   (double) cnt[k] * 100.0 / (double) got);
        }
    }
}

/* ===================== 单向流水：两对总线同时打 ===================== */
static void run_stream(uint32 n)
{
    uint32 i, sent_a = 0U, sent_b = 0U, total_cycles;
    uint64 t0;

    if (n > N_MAX) {
        n = N_MAX;
    }
    {
        uint32 k;
        uint32 m;
        for (k = 0U; k < 2U; k++) {
            for (m = 0U; m < N_MAX; m++) {
                s_rx_seen[k][m] = 0U;
            }
        }
    }
    s_main_cyc = 0U;
    s_busy_retry = 0U;
    s_rx_total = 0U;
    rxq_drain_all();
    t0 = (uint64) cyc32();
    for (i = 0U; i < n; i++) {
        if (send_one(CTRL_A_TX, i, 5000U) == 0U) {
            s_tx_cyc[0][i] = cyc32();
            sent_a++;
        }
        if (send_one(CTRL_B_TX, i, 5000U) == 0U) {
            s_tx_cyc[1][i] = cyc32();
            sent_b++;
        }
        pump();
    }
    /* 等接收侧把剩余帧收完（最多 50 ms） */
    {
        uint64 tw = (uint64) cyc32();
        while (cyc_to_us((uint64) (uint32) ((uint32) cyc32() - (uint32) tw)) < 50000U) {
            pump();
            if (s_rx_total >= (sent_a + sent_b)) {
                break;
            }
        }
    }
    total_cycles = (uint32) ((uint32) cyc32() - (uint32) t0);
    {
        uint32 elapsed_us = (uint32) (((uint64) total_cycles) / (uint64) s_cpu_mhz);
        double fps = (double) (sent_a + sent_b) * 1000000.0 / (double) (elapsed_us ? elapsed_us : 1U);
        printf("STREAM 4ch (A:CAN0->CAN1, B:CAN2->CAN3) 8B classic 500k:\r\n");
        printf("  sent A=%u B=%u  rx_total=%u  elapsed=%uus => %.0f frame/s total\r\n",
               (unsigned) sent_a, (unsigned) sent_b, (unsigned) s_rx_total, (unsigned) elapsed_us, fps);
        printf("  per-channel: %.0f frame/s (500 kbit/s 8B 理论上限 ~4386 frame/s)\r\n", fps / 2.0);
        printf("  CPU in Can_MainFunction_*: %u cycles = %uus of %uus = %.2f%%\r\n",
               (unsigned) s_main_cyc, (unsigned) (uint32) (s_main_cyc / s_cpu_mhz),
               (unsigned) elapsed_us, (double) s_main_cyc * 100.0 / (double) total_cycles);
        printf("  Can_Write CAN_BUSY retries=%u  tx_confirmed=%u\r\n", (unsigned) s_busy_retry,
               (unsigned) s_tx_confirmed);
    }
    print_latency(0U, n);
    print_latency(1U, n);
}

/* ===================== 4 路双向 ping-pong ===================== */
static void run_pingpong4(uint32 n)
{
    uint32 i, ok_a = 0U, ok_b = 0U;
    uint64 sum_a = 0U, sum_b = 0U;
    uint32 mx_a = 0U, mx_b = 0U;
    uint64 t0 = (uint64) cyc32();

    rxq_drain_all();
    for (i = 0U; i < n; i++) {
        uint32 ts, dt;

        /* 总线 A */
        ts = cyc32();
        if (send_one(CTRL_A_TX, i, 5000U) == 0U) {
            rx_rec_t r;
            uint32 tmo = 0U;
            while (tmo++ < 200000U) {
                pump();
                if (rxq_pop(&r) && (r.ctrl == CTRL_A_RX) && (r.id == ID_REQ)) {
                    dt = (uint32) (((uint64) (cyc32() - ts)) / (uint64) s_cpu_mhz);
                    sum_a += dt;
                    if (dt > mx_a) {
                        mx_a = dt;
                    }
                    ok_a++;
                    break;
                }
            }
        }
        /* 总线 B */
        ts = cyc32();
        if (send_one(CTRL_B_TX, i, 5000U) == 0U) {
            rx_rec_t r;
            uint32 tmo = 0U;
            while (tmo++ < 200000U) {
                pump();
                if (rxq_pop(&r) && (r.ctrl == CTRL_B_RX) && (r.id == ID_REQ)) {
                    dt = (uint32) (((uint64) (cyc32() - ts)) / (uint64) s_cpu_mhz);
                    sum_b += dt;
                    if (dt > mx_b) {
                        mx_b = dt;
                    }
                    ok_b++;
                    break;
                }
            }
        }
    }
    {
        uint32 us = (uint32) (((uint64) (cyc32() - (uint32) t0)) / (uint64) s_cpu_mhz);
        printf("PINGPONG 4ch simultaneous: n=%u  A ok=%u avg=%uus max=%uus | B ok=%u avg=%uus max=%uus  total=%uus\r\n",
               (unsigned) n, (unsigned) ok_a, (unsigned) (ok_a ? (uint32) (sum_a / ok_a) : 0U),
               (unsigned) mx_a, (unsigned) ok_b, (unsigned) (ok_b ? (uint32) (sum_b / ok_b) : 0U),
               (unsigned) mx_b, (unsigned) us);
    }
}

static void cmd_can(int argc, char *argv[])
{
    const char *sub = (argc >= 2) ? argv[1] : "run";

    if (strcmp(sub, "run") == 0) {
        uint32 n = (uint32) arg_u32((argc >= 3) ? argv[2] : NULL, 1000U);
        run_stream(n);
    } else if (strcmp(sub, "pp") == 0) {
        uint32 n = (uint32) arg_u32((argc >= 3) ? argv[2] : NULL, 200U);
        run_pingpong4(n);
    } else if (strcmp(sub, "stat") == 0) {
        uint8 c;
        for (c = 0U; c < 4U; c++) {
            Can_ControllerStateType mode = CAN_CS_UNINIT;
            Can_ErrorStateType es = CAN_ERRORSTATE_ACTIVE;
            uint8 rec = 0U, tec = 0U;
            (void) Can_GetControllerMode(c, &mode);
            (void) Can_GetControllerErrorState(c, &es);
            (void) Can_GetControllerRxErrorCounter(c, &rec);
            (void) Can_GetControllerTxErrorCounter(c, &tec);
            printf("CAN%u: mode=%u errstate=%u REC=%u TEC=%u rx=%u\r\n", (unsigned) c, (unsigned) mode,
                   (unsigned) es, (unsigned) rec, (unsigned) tec, (unsigned) s_rx_ctrl[c]);
        }
        printf("rx_total=%u tx_confirmed=%u det=%u\r\n", (unsigned) s_rx_total,
               (unsigned) s_tx_confirmed, (unsigned) Det_GetErrorCount());
    } else if (strcmp(sub, "info") == 0) {
        printf("sample: can_irq_perf  4 路 MCAN 并发（CAN0->CAN1, CAN2->CAN3）\r\n");
        printf("model : MainFunction 轮询推进（CAN_RX_PROCESSING_INTERRUPT=0）\r\n");
        printf("cpu   : %u MHz\r\n", (unsigned) s_cpu_mhz);
    } else {
        printf("unknown sub: %s (run [n]|pp [n]|stat|info)\r\n", sub);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 can, cmd_can, CAN 4ch perf: can run [n]|pp [n]|stat|info);

int main(void)
{
    Std_ReturnType st = E_OK;
    uint8 c;

    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    Det_Init();

    printf("\r\n=== can_irq_perf: 4x MCAN concurrent throughput / CPU / latency ===\r\n");
    Can_Init(&Can_Config);
    for (c = 0U; c < 4U; c++) {
        if (Can_SetControllerMode(c, CAN_CS_STARTED) != E_OK) {
            st = E_NOT_OK;
        }
    }
    printf("[start] 4 controllers STARTED st=%u, hth=%u hrh=%u filters=%u\r\n", (unsigned) st,
           (unsigned) Can_Config.HthCount, (unsigned) Can_Config.HrhCount,
           (unsigned) Can_Config.FilterCount);
    if (st == E_OK) {
        run_stream(1000U);
        run_pingpong4(200U);
    }
    printf("type 'can run 1000' / 'can pp 200' / 'can stat'\r\n");
    while (1) {
        bsp_shell_poll();
        pump();
        Can_MainFunction_BusOff();
        Can_MainFunction_Mode();
    }
    return 0;
}
