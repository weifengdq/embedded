/*
 * can_busoff —— AUTOSAR Can 驱动 bus-off / error passive 与恢复实测
 *
 * 方法：CAN0 与 CAN1 物理互连（120 Ω）。先正常 ping-pong 建立基线，然后把 CAN1
 *       STOPPED（它不再发 ACK），CAN0 继续发同一帧：
 *       M_CAN 自动重传失败 -> TEC 每次 +8 -> TEC>=128 error passive -> TEC>=256 bus-off。
 *       记录 TEC/REC、状态迁移时刻；然后恢复 CAN1，再 Can_SetControllerMode(CAN0, STARTED)
 *       触发恢复序列（驱动内部 mcan_recover_from_busoff + 重新初始化），测恢复时间与首帧成功时间。
 *
 * 运行：上电自动跑一遍；命令 can run [n] 可重跑。
 */
#include "bsp.h"
#include "Can.h"
#include "Can_PBcfg.h"
#include "Det.h"
#include <stdio.h>
#include <string.h>

#define CTRL_A 0U
#define CTRL_B 1U
#define ID_REQ 0x123U
#define ID_ECHO 0x321U
#define HTH_OF(ctrl) ((Can_HwHandleType) ((ctrl) * 4U))

#define RXQ_LEN 32U
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
static volatile uint32 s_tx_confirmed;
static volatile uint32 s_busoff_cb;
static uint32 s_cpu_mhz;

static uint32_t arg_u32(char *s, uint32_t def)
{
    uint32_t v = def;
    if (s != NULL) {
        (void) bsp_parse_u32(s, &v);
    }
    return v;
}

void CanIf_RxIndication(const Can_HwType *Mailbox, const PduInfoType *PduInfoPtr)
{
    uint32 next = (s_rx_head + 1U) % RXQ_LEN;
    if ((Mailbox == NULL_PTR) || (PduInfoPtr == NULL_PTR)) {
        return;
    }
    s_rx_total++;
    if (next == s_rx_tail) {
        return;
    }
    s_rxq[s_rx_head].ctrl = Mailbox->ControllerId;
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
    (void) ControllerId;
    s_busoff_cb++;
}

void CanIf_ControllerModeIndication(uint8 ControllerId, Can_ControllerStateType ControllerMode)
{
    printf("  [mode] ctrl=%u -> %u\r\n", (unsigned) ControllerId, (unsigned) ControllerMode);
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

static uint8 can_send(uint8 ctrl, Can_HwHandleType hth, Can_IdType id, const uint8 *data, uint8 len,
                      uint32 timeout_us)
{
    Can_PduType pdu;
    uint64_t t0 = cyc_now();

    pdu.swPduHandle = (PduIdType) (id & CAN_ID_VALUE_MASK);
    pdu.length = len;
    pdu.id = id;
    pdu.sdu = (uint8 *) data;
    for (;;) {
        if (Can_Write(hth, &pdu) == CAN_OK) {
            return 0U;
        }
        Can_MainFunction_Write();
        Can_MainFunction_Read();
        Can_MainFunction_BusOff();
        if (cyc_to_us(cyc_now() - t0) > timeout_us) {
            return 1U;
        }
    }
}

static uint8 can_recv(uint8 ctrl, Can_IdType id, uint32 timeout_us)
{
    uint64_t t0 = cyc_now();
    rx_rec_t r;

    for (;;) {
        Can_MainFunction_Read();
        Can_MainFunction_Write();
        Can_MainFunction_BusOff();
        if (rxq_pop(&r)) {
            if ((r.ctrl == ctrl) && (r.id == id)) {
                return 0U;
            }
            return 2U;
        }
        if (cyc_to_us(cyc_now() - t0) > timeout_us) {
            return 1U;
        }
    }
}

/* 基线 ping-pong：成功返回 0 */
static uint8 baseline_pingpong(uint32 n)
{
    uint8 d[8] = { 0 };
    uint32 i;

    rxq_drain_all();
    for (i = 0U; i < n; i++) {
        d[0] = (uint8) i;
        if (can_send(CTRL_A, HTH_OF(CTRL_A), ID_REQ, d, 8U, 5000U) != 0U) {
            return 1U;
        }
        if (can_recv(CTRL_B, ID_REQ, 5000U) != 0U) {
            return 1U;
        }
        if (can_send(CTRL_B, HTH_OF(CTRL_B), ID_ECHO, d, 8U, 5000U) != 0U) {
            return 1U;
        }
        if (can_recv(CTRL_A, ID_ECHO, 5000U) != 0U) {
            return 1U;
        }
    }
    return 0U;
}

static void dump_state(const char *tag)
{
    Can_ControllerStateType mode = CAN_CS_UNINIT;
    Can_ErrorStateType es = CAN_ERRORSTATE_ACTIVE;
    uint8 rec = 0U, tec = 0U;
    (void) Can_GetControllerMode(CTRL_A, &mode);
    (void) Can_GetControllerErrorState(CTRL_A, &es);
    (void) Can_GetControllerRxErrorCounter(CTRL_A, &rec);
    (void) Can_GetControllerTxErrorCounter(CTRL_A, &tec);
    printf("  %-18s mode=%u errstate=%u TEC=%u REC=%u busoff_cb=%u\r\n", tag, (unsigned) mode,
           (unsigned) es, (unsigned) tec, (unsigned) rec, (unsigned) s_busoff_cb);
}

static void run_busoff_test(void)
{
    uint8 d[8] = { 0xA5U, 0x5AU, 1U, 2U, 3U, 4U, 5U, 6U };
    uint64_t t_start;
    uint32 t_passive = 0U, tec_passive = 0U;
    uint32 t_plateau = 0U, tec_plateau = 0U;
    uint32 tmo = 0U;

    printf("--- error-passive / bus-off / recovery test ---\r\n");
    if (baseline_pingpong(20U) != 0U) {
        printf("baseline ping-pong FAILED, abort\r\n");
        return;
    }
    dump_state("baseline ok");

    /* 1) 对端停止 ACK */
    (void) Can_SetControllerMode(CTRL_B, CAN_CS_STOPPED);
    printf("  CAN1 -> STOPPED (no ACK on bus)\r\n");

    /* 2) CAN0 发一帧；自动重传持续失败，观察 TEC 与状态 */
    (void) can_send(CTRL_A, HTH_OF(CTRL_A), ID_REQ, d, 8U, 2000U);
    t_start = cyc_now();
    for (;;) {
        Can_ErrorStateType es = CAN_ERRORSTATE_ACTIVE;
        uint8 rec = 0U, tec = 0U;
        uint32 t_us;

        Can_MainFunction_BusOff();
        (void) Can_GetControllerErrorState(CTRL_A, &es);
        (void) Can_GetControllerRxErrorCounter(CTRL_A, &rec);
        (void) Can_GetControllerTxErrorCounter(CTRL_A, &tec);
        t_us = cyc_to_us(cyc_now() - t_start);

        if ((t_passive == 0U) && (es == CAN_ERRORSTATE_PASSIVE)) {
            t_passive = t_us;
            tec_passive = tec;
            printf("  t=%6uus  -> ERROR_PASSIVE  TEC=%u REC=%u\r\n", (unsigned) t_us,
                   (unsigned) tec, (unsigned) rec);
        }
        if ((t_passive != 0U) && (t_plateau == 0U) && (t_us > (t_passive + 20000U))) {
            t_plateau = t_us;
            tec_plateau = tec;
            printf("  t=%6uus  TEC 平台 %u（20 ms 内不再增长）\r\n", (unsigned) t_us, (unsigned) tec);
            break;
        }
        if ((++tmo > 4000000U) || (t_us > 100000U)) {
            printf("  timeout: es=%u TEC=%u\r\n", (unsigned) es, (unsigned) tec);
            break;
        }
    }
    dump_state("after acks lost");
    printf("  summary: error-passive @ %uus, TEC=%u；平台 TEC=%u\r\n",
           (unsigned) t_passive, (unsigned) tec_passive, (unsigned) tec_plateau);
    printf("  note: ISO 11898-1 规定 error-passive 节点遇 ACK 错误不再增加 TEC；\r\n");
    printf("        所以对端不 ACK 只能到 TEC=128（error passive），到不了 bus-off(TEC>255)。\r\n");

    /* 3) 对端恢复 ACK，观察 TEC 回落 + 业务恢复时间 */
    {
        uint64_t tr;
        uint8 rc;
        (void) Can_SetControllerMode(CTRL_B, CAN_CS_STARTED);
        tr = cyc_now();
        /* bus-off 恢复序列 API：硬件不在 bus-off 时按非法迁移返回 E_NOT_OK 并报 DET */
        printf("  Can_SetControllerMode(CAN0, STARTED) while error-passive = %u (expect 1/DET)\r\n",
               (unsigned) Can_SetControllerMode(CTRL_A, CAN_CS_STARTED));
        rc = baseline_pingpong(10U);
        printf("  peer back: ping-pong(10) rc=%u in %uus\r\n", (unsigned) rc,
               (unsigned) cyc_to_us(cyc_now() - tr));
    }
    dump_state("after peer back");
    printf("rx_total=%u tx_confirmed=%u busoff_cb=%u det=%u\r\n", (unsigned) s_rx_total,
           (unsigned) s_tx_confirmed, (unsigned) s_busoff_cb, (unsigned) Det_GetErrorCount());
}

static void cmd_can(int argc, char *argv[])
{
    const char *sub = (argc >= 2) ? argv[1] : "run";

    if (strcmp(sub, "run") == 0) {
        run_busoff_test();
    } else if (strcmp(sub, "stat") == 0) {
        dump_state("CAN0 now");
        printf("rx_total=%u tx_confirmed=%u busoff_cb=%u det=%u\r\n", (unsigned) s_rx_total,
               (unsigned) s_tx_confirmed, (unsigned) s_busoff_cb, (unsigned) Det_GetErrorCount());
    } else if (strcmp(sub, "info") == 0) {
        printf("sample: can_busoff  CAN0 <-> CAN1，停对端 ACK 制造 error passive/bus-off\r\n");
        printf("expect: TEC +8/次失败重传，TEC>=128 passive，TEC>=256 bus-off\r\n");
    } else {
        (void) arg_u32(NULL, 0U);
        printf("unknown sub: %s (run|stat|info)\r\n", sub);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 can, cmd_can, CAN bus-off/recovery test: can run|stat|info);

int main(void)
{
    Std_ReturnType st;

    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    Det_Init();

    printf("\r\n=== can_busoff: AUTOSAR Can driver error-passive/bus-off/recovery ===\r\n");
    Can_Init(&Can_Config);
    st = E_OK;
    if (Can_SetControllerMode(CTRL_A, CAN_CS_STARTED) != E_OK) {
        st = E_NOT_OK;
    }
    if (Can_SetControllerMode(CTRL_B, CAN_CS_STARTED) != E_OK) {
        st = E_NOT_OK;
    }
    printf("[start] CAN0/CAN1 STARTED st=%u\r\n", (unsigned) st);
    if (st == E_OK) {
        run_busoff_test();
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