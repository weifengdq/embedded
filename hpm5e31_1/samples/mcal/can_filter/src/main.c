/*
 * can_filter —— AUTOSAR Can 驱动硬件过滤器验证（CAN0 发 / CAN1 收）
 *
 * 过滤器表在 Can_PBcfg.c（post-build）：
 *   标准 ID 列表 #0 掩码 0x123/0x7FF -> RXFIFO0
 *                #1 掩码 0x321/0x7FF -> RXFIFO0
 *                #2 范围 0x400..0x40F -> RXFIFO0
 *                #3 双 ID {0x500,0x501} -> RXFIFO1
 *                #4 掩码 0x600/0x7F0   -> RXFIFO0
 *                #5 范围 0x700..0x7FF  -> 丢弃（SFEC=reject）
 *   扩展 ID 列表 #0 范围 0x18000000..0x18FFFFFF -> RXFIFO0
 *                #1 掩码 0x1ABC0000/0x1FFF0000   -> RXFIFO0
 *                #2 双 ID {0x1FFFFFF0,0x1FFFFFF1}-> RXFIFO1
 *   全局过滤：未命中帧 -> 丢弃（GFC ANFS/ANFE = reject）
 *
 * 判定依据：CanIf_RxIndication 的 Mailbox->Hoh 是 HRH 句柄，PBcfg 里
 *   HRH[2] = {ctrl=1, FIFO0}，HRH[3] = {ctrl=1, FIFO1}，因此能从 Hoh 反推目标 FIFO。
 */
#include "bsp.h"
#include "Can.h"
#include "Can_PBcfg.h"
#include "Det.h"
#include <stdio.h>
#include <string.h>

#define CTRL_TX  0U
#define CTRL_RX  1U
#define HTH_OF(ctrl) ((Can_HwHandleType) ((ctrl) * 4U))
#define HRH_FIFO0_RX 2U /* PBcfg: HRH[2] = ctrl1 + RXFIFO0 */
#define HRH_FIFO1_RX 3U /* PBcfg: HRH[3] = ctrl1 + RXFIFO1 */

#define RXQ_LEN 64U
typedef struct {
    uint8     ctrl;
    Can_IdType id;
    uint8     len;
    uint8     hoh;
    uint8     data[8];
} rx_rec_t;

static rx_rec_t s_rxq[RXQ_LEN];
static volatile uint32 s_rx_head;
static volatile uint32 s_rx_tail;
static volatile uint32 s_rx_total;
static volatile uint32 s_rx_overflow;
static volatile uint32 s_tx_confirmed;
static uint32 s_cpu_mhz;
static uint32 s_case_hits[16];
static uint32 s_case_drops[16];

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
    s_rxq[s_rx_head].hoh = (uint8) Mailbox->Hoh;
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

/* 发一帧（8 字节），等 2 ms 收；返回 1=命中（Hoh 写入），0=丢弃太（未收到） */
static uint8 try_frame(Can_IdType id, uint8 *hoh_out)
{
    Can_PduType pdu;
    uint8 d[8] = { 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U };
    uint64_t t0 = cyc_now();
    rx_rec_t r;

    pdu.swPduHandle = 0U;
    pdu.length = 8U;
    pdu.id = id;
    pdu.sdu = d;
    for (;;) {
        if (Can_Write(HTH_OF(CTRL_TX), &pdu) == CAN_OK) {
            break;
        }
        Can_MainFunction_Write();
        if (cyc_to_us(cyc_now() - t0) > 5000U) {
            return 0U;
        }
    }
    rxq_drain_all(); /* 清掉上一轮的残留 */
    t0 = cyc_now();
    while (cyc_to_us(cyc_now() - t0) < 2000U) {
        Can_MainFunction_Read();
        Can_MainFunction_Write();
        if (rxq_pop(&r)) {
            if ((r.ctrl == CTRL_RX) && (r.id == id)) {
                *hoh_out = r.hoh;
                return 1U;
            }
        }
    }
    return 0U;
}

typedef struct {
    Can_IdType id;
    const char *name;
    uint8 expect_hit;   /* 1 = 应命中，0 = 应被丢弃 */
    uint8 expect_fifo;  /* 0 = RXFIFO0, 1 = RXFIFO1 */
} filter_case_t;

static const filter_case_t s_cases[] = {
    { 0x123U,                    "mask 0x123/0x7FF",     1U, 0U },
    { 0x321U,                    "mask 0x321/0x7FF",     1U, 0U },
    { 0x405U,                    "range 0x400..0x40F",   1U, 0U },
    { 0x60AU,                    "mask 0x600/0x7F0",     1U, 0U },
    { 0x500U,                    "dual {0x500,0x501}",   1U, 1U },
    { 0x501U,                    "dual {0x500,0x501}",   1U, 1U },
    { 0x502U,                    "no match -> drop",     0U, 0U },
    { 0x705U,                    "reject 0x700..0x7FF",  0U, 0U },
    { 0x7FFU,                    "reject 0x700..0x7FF",  0U, 0U },
    { CAN_ID_EXT_FRAME | 0x18FF0001UL, "ext range 0x18FFFFFF", 1U, 0U },
    { CAN_ID_EXT_FRAME | 0x1ABC1234UL, "ext mask 0x1FFF0000",   1U, 0U },
    { CAN_ID_EXT_FRAME | 0x1FFFFFF0UL, "ext dual {..F0,..F1}",  1U, 1U },
    { CAN_ID_EXT_FRAME | 0x12345678UL, "ext no match -> drop",  0U, 0U }
};

static void run_filter_test(void)
{
    uint32 pass = 0U, fail = 0U, i;

    printf("filter test: %u cases (expect hit/drop vs actual)\r\n",
           (unsigned) (sizeof(s_cases) / sizeof(s_cases[0])));
    printf("  #  can_id      case                      expect    got       verdict\r\n");
    for (i = 0U; i < (uint32) (sizeof(s_cases) / sizeof(s_cases[0])); i++) {
        const filter_case_t *c = &s_cases[i];
        uint8 hoh = 0xFFU;
        uint8 hit = try_frame(c->id, &hoh);
        uint8 ok;
        const char *fifo_name = "-";

        if (c->expect_hit != 0U) {
            ok = (hit != 0U) ? 1U : 0U;
            if (ok != 0U) {
                if (hoh == HRH_FIFO0_RX) {
                    fifo_name = "FIFO0";
                    if (c->expect_fifo != 0U) {
                        ok = 0U;
                    }
                } else if (hoh == HRH_FIFO1_RX) {
                    fifo_name = "FIFO1";
                    if (c->expect_fifo == 0U) {
                        ok = 0U;
                    }
                } else {
                    fifo_name = "?";
                    ok = 0U;
                }
            }
            s_case_hits[i] = ok;
        } else {
            ok = (hit == 0U) ? 1U : 0U;
            fifo_name = (hit != 0U) ? "LEAK" : "drop";
            s_case_drops[i] = ok;
        }
        if (ok != 0U) {
            pass++;
        } else {
            fail++;
        }
        printf("  %2u 0x%08X  %-24s  %-8s  %-8s  %s\r\n", (unsigned) i, (unsigned) c->id, c->name,
               (c->expect_hit != 0U) ? (c->expect_fifo == 0U ? "FIFO0" : "FIFO1") : "drop",
               fifo_name, (ok != 0U) ? "PASS" : "FAIL");
    }
    printf("filter: pass=%u fail=%u  (rx_total=%u rx_overflow=%u tx_confirmed=%u)\r\n",
           (unsigned) pass, (unsigned) fail, (unsigned) s_rx_total, (unsigned) s_rx_overflow,
           (unsigned) s_tx_confirmed);
}

static void cmd_can(int argc, char *argv[])
{
    const char *sub = (argc >= 2) ? argv[1] : "run";

    if (strcmp(sub, "run") == 0) {
        run_filter_test();
    } else if (strcmp(sub, "tour") == 0) {
        for (;;) {
            run_filter_test();
            break;
        }
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
        printf("rx_total=%u rx_overflow=%u tx_confirmed=%u det=%u\r\n", (unsigned) s_rx_total,
               (unsigned) s_rx_overflow, (unsigned) s_tx_confirmed, (unsigned) Det_GetErrorCount());
    } else if (strcmp(sub, "info") == 0) {
        printf("sample: can_filter  CAN0 -> CAN1，13 个过滤器用例（掩码/范围/双 ID/拒绝/未命中）\r\n");
        printf("rx hoh: FIFO0->HRH%u, FIFO1->HRH%u\r\n", (unsigned) HRH_FIFO0_RX, (unsigned) HRH_FIFO1_RX);
    } else {
        printf("unknown sub: %s (run|stat|info)\r\n", sub);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 can, cmd_can, CAN filter test: can run|stat|info);

int main(void)
{
    Std_ReturnType st;

    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    Det_Init();

    printf("\r\n=== can_filter: AUTOSAR Can driver HW filter (mask/range/dual/reject) ===\r\n");
    Can_Init(&Can_Config);
    st = E_OK;
    if (Can_SetControllerMode(CTRL_TX, CAN_CS_STARTED) != E_OK) {
        st = E_NOT_OK;
    }
    if (Can_SetControllerMode(CTRL_RX, CAN_CS_STARTED) != E_OK) {
        st = E_NOT_OK;
    }
    printf("[start] CAN0/CAN1 STARTED st=%u, filters=%u\r\n", (unsigned) st, (unsigned) Can_Config.FilterCount);
    if (st == E_OK) {
        run_filter_test();
    }
    printf("type 'can run' to rerun\r\n");
    while (1) {
        bsp_shell_poll();
        Can_MainFunction_Write();
        Can_MainFunction_Read();
        Can_MainFunction_BusOff();
        Can_MainFunction_Mode();
    }
    return 0;
}
