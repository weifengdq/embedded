/*
 * can_classic_echo —— AUTOSAR Can 驱动经典帧双节点回环（CAN0 <-> CAN1）
 *
 * 硬件：hpm5e31_1，CAN0(PD00/PD01) 与 CAN1(PD05/PD04) 经两颗 TCAN1044AV 接到同一段
 *       总线，两端各 120 Ω（板上已接），无需外部 CAN 分析仪。
 * 驱动：mcal/Can（AUTOSAR Can_Init/SetControllerMode/Write/MainFunction_*）
 * 用法：上电自动跑一遍 200 次 ping-pong 并打印表格；命令可重跑。
 *
 * 串口自证格式（便于 --expect / 文档引用）：
 *   ECHO classic 500k: n=200 ok=200 bad=0 total=....us avg=...us max=...us
 */
#include "bsp.h"
#include "Can.h"
#include "Can_PBcfg.h"
#include "Det.h"
#include "hpm_mcan_drv.h" /* 仅 diag 子命令用来 dump M_CAN 寄存器 */
#include <stdio.h>
#include <string.h>

#define CTRL_A   0U /* 发起端 CAN0 */
#define CTRL_B   1U /* 应答端 CAN1 */

#define ID_REQ   0x123U /* PBcfg 过滤器 #0（掩码 0x7FF） */
#define ID_ECHO  0x321U /* PBcfg 过滤器 #1（掩码 0x7FF） */

#define HTH_OF(ctrl)  ((Can_HwHandleType) ((ctrl) * 4U)) /* PBcfg：每控制器 4 个 HTH */

/* ===================== 接收队列（ISR 生产 / 主循环消费） ===================== */
#define RXQ_LEN 256U
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
static volatile uint32 s_err_notify_cnt;

static uint32 s_cpu_mhz;
static uint32 s_dbg_mismatch;

/*! 命令行参数解析（bsp_parse_u32 要 uint32_t*，AUTOSAR uint32 与之不同名同宽） */
static uint32_t arg_u32(char *s, uint32_t def)
{
    uint32_t v = def;
    if (s != NULL) {
        (void) bsp_parse_u32(s, &v);
    }
    return v;
}

/* ===================== 驱动回调（覆盖 Can.c 的 weak 桩） ===================== */
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

void CanIf_ErrorNotification(uint8 ControllerId, Can_ErrorType CanError)
{
    (void) ControllerId;
    (void) CanError;
    s_err_notify_cnt++;
}

void CanIf_ControllerModeIndication(uint8 ControllerId, Can_ControllerStateType ControllerMode)
{
    printf("  [mode] ctrl=%u -> %u\r\n", (unsigned) ControllerId, (unsigned) ControllerMode);
}

/* ===================== 小工具 ===================== */
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

static void rxq_flush(void)
{
    s_rx_tail = s_rx_head;
}

/*! 把硬件 RXFIFO 里残留的帧全部取走再丢空软件队列（跨测试用例必须做，
 *  否则上一轮的尾帧会串到下一轮的期望序列里） */
static void rxq_drain_all(void)
{
    uint32 i;
    for (i = 0U; i < 8U; i++) {
        Can_MainFunction_Read();
    }
    rxq_flush();
}

/* 组载荷：byte0..3 = 序号小端，byte4..7 = 0xA5+i */
static void fill_payload(uint8 *buf, uint32 seq)
{
    uint8 i;
    for (i = 0U; i < 4U; i++) {
        buf[i] = (uint8) (seq >> (8U * i));
    }
    for (i = 4U; i < 8U; i++) {
        buf[i] = (uint8) (0xA5U + i);
    }
}

static bool payload_match(const uint8 *buf, uint8 len, uint32 seq)
{
    uint32 v;
    if (len != 8U) {
        return false;
    }
    v = (uint32) buf[0] | ((uint32) buf[1] << 8) | ((uint32) buf[2] << 16) | ((uint32) buf[3] << 24);
    if (v != seq) {
        return false;
    }
    /* byte4..7 = 0xA5+i（i 从 4 起）→ 0xA9/0xAA/0xAB/0xAC */
    return (buf[4] == (uint8) (0xA5U + 4U)) && (buf[5] == (uint8) (0xA5U + 5U)) &&
           (buf[6] == (uint8) (0xA5U + 6U)) && (buf[7] == (uint8) (0xA5U + 7U));
}

/* 发送一帧（HTH 忙则重试到超时），返回 0=成功 */
static uint8 can_send(uint8 ctrl, Can_IdType id, const uint8 *data, uint8 len, uint32 timeout_us)
{
    Can_PduType pdu;
    uint64_t t0 = cyc_now();

    pdu.swPduHandle = (PduIdType) (id & CAN_ID_VALUE_MASK);
    pdu.length = len;
    pdu.id = id;
    pdu.sdu = (uint8 *) data;
    for (;;) {
        Can_ReturnType r = Can_Write(HTH_OF(ctrl), &pdu);
        if (r == CAN_OK) {
            return 0U;
        }
        /* 轮询模型：驱动的主函数由应用周期调用（相当于 CanIf 的 1 ms 任务） */
        Can_MainFunction_Write();
        Can_MainFunction_Read();
        if (cyc_to_us(cyc_now() - t0) > timeout_us) {
            return 1U;
        }
    }
}

/* 等一帧：返回 0=收到且匹配，1=超时，2=收到但不匹配 */
static uint8 can_recv(uint8 ctrl, Can_IdType id, uint32 seq, uint32 timeout_us)
{
    uint64_t t0 = cyc_now();
    rx_rec_t r;

    for (;;) {
        Can_MainFunction_Read();
        Can_MainFunction_Write();
        if (rxq_pop(&r)) {
            if ((r.ctrl == ctrl) && (r.id == id) && payload_match(r.data, r.len, seq)) {
                return 0U;
            }
            if (s_dbg_mismatch < 3U) {
                s_dbg_mismatch++;
                printf("[mismatch] want ctrl=%u id=0x%03X seq=%u | got ctrl=%u id=0x%08X len=%u data=%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                       (unsigned) ctrl, (unsigned) id, (unsigned) seq, (unsigned) r.ctrl,
                       (unsigned) r.id, (unsigned) r.len, r.data[0], r.data[1], r.data[2], r.data[3],
                       r.data[4], r.data[5], r.data[6], r.data[7]);
            }
            return 2U;
        }
        if (cyc_to_us(cyc_now() - t0) > timeout_us) {
            return 1U;
        }
    }
}

/* ===================== 测试 1：经典帧 ping-pong ===================== */
static void bench_pingpong(uint32 n)
{
    uint8 buf[8];
    uint32 ok = 0U, lost_req = 0U, lost_echo = 0U, bad = 0U;
    uint64_t t0, sum_us = 0U;
    uint32 max_us = 0U;
    uint32 seq;

    rxq_drain_all();
    s_rx_total = 0U;
    s_rx_overflow = 0U;
    s_tx_confirmed = 0U;
    s_dbg_mismatch = 0U;
    t0 = cyc_now();
    for (seq = 0U; seq < n; seq++) {
        uint64_t ts;
        uint32 dt;
        uint8 rc;

        fill_payload(buf, seq);
        if (can_send(CTRL_A, ID_REQ, buf, 8U, 5000U) != 0U) {
            lost_req++;
            continue;
        }
        rc = can_recv(CTRL_B, ID_REQ, seq, 5000U);
        if (rc == 1U) {
            lost_req++;
            continue;
        }
        if (rc == 2U) {
            bad++;
            continue;
        }
        /* 应答端把同一序号以 0x321 回显 */
        ts = cyc_now();
        if (can_send(CTRL_B, ID_ECHO, buf, 8U, 5000U) != 0U) {
            lost_echo++;
            continue;
        }
        rc = can_recv(CTRL_A, ID_ECHO, seq, 5000U);
        dt = cyc_to_us(cyc_now() - ts);
        if (rc != 0U) {
            lost_echo++;
            continue;
        }
        sum_us += dt;
        if (dt > max_us) {
            max_us = dt;
        }
        ok++;
    }
    {
        uint32 total_us = cyc_to_us(cyc_now() - t0);
        printf("ECHO classic 500k: n=%u ok=%u lost_req=%u lost_echo=%u bad=%u total=%uus\r\n",
               (unsigned) n, (unsigned) ok, (unsigned) lost_req, (unsigned) lost_echo,
               (unsigned) bad, (unsigned) total_us);
        if (ok != 0U) {
            printf("  echo_latency: avg=%uus max=%uus  roundtrip=%.0f/s\r\n",
                   (unsigned) (sum_us / ok), (unsigned) max_us,
                   (double) ok * 1000000.0 / (double) (total_us ? total_us : 1U));
        }
    }
    printf("  rx_total=%u rx_overflow=%u tx_confirmed=%u busoff=%u\r\n",
           (unsigned) s_rx_total, (unsigned) s_rx_overflow,
           (unsigned) s_tx_confirmed, (unsigned) s_busoff_cnt);
}

/* ===================== 测试 2：单向流水（经典帧吞吐） ===================== */
static void bench_stream(uint32 n)
{
    uint8 buf[8];
    uint32 sent = 0U, got = 0U, bad = 0U;
    uint64_t t0;
    uint32 i;

    rxq_drain_all();
    s_rx_total = 0U;
    s_rx_overflow = 0U;
    t0 = cyc_now();
    for (i = 0U; i < n; i++) {
        fill_payload(buf, i);
        if (can_send(CTRL_A, ID_REQ, buf, 8U, 5000U) != 0U) {
            break;
        }
        sent++;
        Can_MainFunction_Read(); /* 保持 RX 被及时抽干 */
        Can_MainFunction_Write();
        {
            rx_rec_t rr;
            while (rxq_pop(&rr)) {
                if ((rr.ctrl == CTRL_B) && (rr.id == ID_REQ) && (rr.len == 8U)) {
                    got++;
                } else {
                    bad++;
                }
            }
        }
    }
    /* 等接收端把剩余帧收完；received 用驱动回调计数（不受应用队列长度影响） */
    {
        uint64_t tw = cyc_now();
        while (cyc_to_us(cyc_now() - tw) < 20000U) {
            rx_rec_t r;
            Can_MainFunction_Read();
            Can_MainFunction_Write();
            while (rxq_pop(&r)) {
                if ((r.ctrl == CTRL_B) && (r.id == ID_REQ) && (r.len == 8U)) {
                    got++;
                } else {
                    bad++;
                }
            }
            if (s_rx_total >= sent) {
                break;
            }
        }
    }
    {
        uint32 total_us = cyc_to_us(cyc_now() - t0);
        double fps = (double) sent * 1000000.0 / (double) (total_us ? total_us : 1U);
        printf("STREAM classic 500k: sent=%u rx=%u bad=%u total=%uus => %.0f frame/s\r\n",
               (unsigned) sent, (unsigned) got, (unsigned) bad, (unsigned) total_us, fps);
        {
            double payload_mbps = fps * 8.0 * 8.0 / 1000000.0;
            printf("  payload=%.2f Mbit/s (8B/frame, 108 bit on wire)  bus=0.5 Mbit/s  wire_eff=%.1f%%\r\n",
                   payload_mbps, payload_mbps * 100.0 / 0.5);
        }
    }
    printf("  rx_overflow=%u tx_confirmed=%u\r\n", (unsigned) s_rx_overflow, (unsigned) s_tx_confirmed);
}

/* ===================== 测试 3：错误路径（Det） ===================== */
static void test_error_paths(void)
{
    uint32 before = Det_GetErrorCount();
    Can_ControllerStateType mode;
    Can_ErrorStateType estate;
    uint8 cnt = 0U;
    Can_PduType pdu;
    uint8 dummy[8] = {0};
    uint16 mod = 0U;
    uint8 inst = 0U, api = 0U, err = 0U;

    pdu.swPduHandle = 0U;
    pdu.length = 8U;
    pdu.id = ID_REQ;
    pdu.sdu = dummy;

    /* 1) 句柄越界 -> CAN_E_PARAM_HANDLE */
    (void) Can_Write((Can_HwHandleType) 999U, &pdu);
    /* 2) 空指针 -> CAN_E_PARAM_POINTER */
    (void) Can_Write(HTH_OF(CTRL_A), NULL_PTR);
    /* 3) 控制器越界 -> CAN_E_PARAM_CONTROLLER */
    (void) Can_GetControllerMode(9U, &mode);
    (void) Can_GetControllerErrorState(9U, &estate);
    (void) Can_GetControllerRxErrorCounter(9U, &cnt);
    /* 4) 空指针 -> CAN_E_PARAM_POINTER */
    (void) Can_GetControllerMode(0U, NULL_PTR);
    /* 5) 非法波特率配置 ID -> CAN_E_PARAM_BAUDRATE */
    (void) Can_SetBaudrate(0U, 0x55U);
    /* 6) 经典帧长度 > 8 -> CAN_E_PARAM_DATA_LENGTH */
    pdu.length = 12U;
    (void) Can_Write(HTH_OF(CTRL_A), &pdu);

    printf("DET: errors before=%u after=%u (delta=%u)\r\n", (unsigned) before,
           (unsigned) Det_GetErrorCount(), (unsigned) (Det_GetErrorCount() - before));
    if (Det_GetLastError(&mod, &inst, &api, &err) == E_OK) {
        printf("DET: last mod=%u inst=%u api=0x%02X err=0x%02X\r\n",
               (unsigned) mod, (unsigned) inst, (unsigned) api, (unsigned) err);
    }
    /* 正常路径必须仍然是 E_OK */
    printf("DET: Can_GetVersionInfo / GetControllerMode ok=%u\r\n",
           (unsigned) (Can_GetControllerMode(0U, &mode) == E_OK));
}

/* ===================== 命令 ===================== */
/* 统一前缀 can + 子命令（契约 §6.2）：can info / echo / stream / stat / err / mode / ver */
static void cmd_can(int argc, char *argv[])
{
    const char *sub = (argc >= 2) ? argv[1] : "info";

    if (strcmp(sub, "info") == 0) {
        printf("sample: can_classic_echo   CAN0(PD00/PD01) <-> CAN1(PD05/PD04), TCAN1044AV, 120R x2\r\n");
        printf("driver: mcal/Can  AUTOSAR Can_Init/SetControllerMode/Write/MainFunction_*\r\n");
        printf("baud  : ID0 classic 500 kbit/s, presc=1 seg1=139 seg2=20 sjw=4, SP=87.5%%\r\n");
        printf("hth   : ctrl0 -> HTH0..3, ctrl1 -> HTH4..7 (每控制器 4 个专用 TX buffer)\r\n");
        printf("filt  : 0x123/0x321 精确命中 RXFIFO0；未命中帧按 GFC 丢弃\r\n");
        printf("cpu   : %u MHz, 1us = %u cycles\r\n", (unsigned) s_cpu_mhz, (unsigned) s_cpu_mhz);
    } else if (strcmp(sub, "echo") == 0) {
        uint32 n = (uint32) arg_u32((argc >= 3) ? argv[2] : NULL, 200U);
        bench_pingpong(n);
    } else if (strcmp(sub, "stream") == 0) {
        uint32 n = (uint32) arg_u32((argc >= 3) ? argv[2] : NULL, 500U);
        bench_stream(n);
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
            printf("CAN%u: mode=%u errstate=%u REC=%u TEC=%u\r\n",
                   (unsigned) c, (unsigned) mode, (unsigned) es, (unsigned) rec, (unsigned) tec);
        }
        printf("rx_total=%u rx_overflow=%u tx_confirmed=%u busoff=%u err_notify=%u det=%u\r\n",
               (unsigned) s_rx_total, (unsigned) s_rx_overflow, (unsigned) s_tx_confirmed,
               (unsigned) s_busoff_cnt, (unsigned) s_err_notify_cnt, (unsigned) Det_GetErrorCount());
    } else if (strcmp(sub, "err") == 0) {
        test_error_paths();
    } else if (strcmp(sub, "mode") == 0) {
        uint8 c = (uint8) arg_u32((argc >= 3) ? argv[2] : NULL, 0U);
        if (c >= CAN_CONTROLLER_COUNT) {
            printf("ctrl range 0..3\r\n");
            return;
        }
        if ((argc >= 4) && (strcmp(argv[3], "start") == 0)) {
            printf("Can_SetControllerMode(%u, STARTED) = %u\r\n", (unsigned) c,
                   (unsigned) Can_SetControllerMode(c, CAN_CS_STARTED));
        } else if ((argc >= 4) && (strcmp(argv[3], "stop") == 0)) {
            printf("Can_SetControllerMode(%u, STOPPED) = %u\r\n", (unsigned) c,
                   (unsigned) Can_SetControllerMode(c, CAN_CS_STOPPED));
        } else {
            Can_ControllerStateType m = CAN_CS_UNINIT;
            (void) Can_GetControllerMode(c, &m);
            printf("CAN%u mode=%u\r\n", (unsigned) c, (unsigned) m);
        }
    } else if (strcmp(sub, "diag") == 0) {
        Can_PduType pdu;
        uint8 d[8] = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U };
        Can_ReturnType wr;

        pdu.swPduHandle = 0U;
        pdu.length = 8U;
        pdu.id = ID_REQ;
        pdu.sdu = d;
        printf("MCAN0: CCCR=0x%08X PSR=0x%08X IE=0x%08X IR=0x%08X GLB=0x%08X NBTP=0x%08X\r\n",
               (unsigned) HPM_MCAN0->CCCR, (unsigned) HPM_MCAN0->PSR, (unsigned) HPM_MCAN0->IE,
               (unsigned) HPM_MCAN0->IR, (unsigned) HPM_MCAN0->GLB_CTL, (unsigned) HPM_MCAN0->NBTP);
        printf("MCAN0: TXBC=0x%08X TXBRP=0x%08X TXBTO=0x%08X SIDFC=0x%08X RXF0C=0x%08X\r\n",
               (unsigned) HPM_MCAN0->TXBC, (unsigned) HPM_MCAN0->TXBRP, (unsigned) HPM_MCAN0->TXBTO,
               (unsigned) HPM_MCAN0->SIDFC, (unsigned) HPM_MCAN0->RXF0C);
        printf("MCAN1: CCCR=0x%08X PSR=0x%08X IR=0x%08X RXF0C=0x%08X\r\n",
               (unsigned) HPM_MCAN1->CCCR, (unsigned) HPM_MCAN1->PSR, (unsigned) HPM_MCAN1->IR,
               (unsigned) HPM_MCAN1->RXF0C);
        wr = Can_Write(0U, &pdu);
        printf("Can_Write(0)=%u TXBRP=0x%08X TXBTO=0x%08X PSR=0x%08X ECR=0x%08X DBTP=0x%08X\r\n",
               (unsigned) wr, (unsigned) HPM_MCAN0->TXBRP, (unsigned) HPM_MCAN0->TXBTO,
               (unsigned) HPM_MCAN0->PSR, (unsigned) HPM_MCAN0->ECR, (unsigned) HPM_MCAN0->DBTP);
        board_delay_us(2000U);
        printf("after 2ms: TXBRP=0x%08X TXBTO=0x%08X IR=0x%08X PSR=0x%08X ECR=0x%08X RXF0S(fill)=%u\r\n",
               (unsigned) HPM_MCAN0->TXBRP, (unsigned) HPM_MCAN0->TXBTO, (unsigned) HPM_MCAN0->IR,
               (unsigned) HPM_MCAN0->PSR, (unsigned) HPM_MCAN0->ECR,
               (unsigned) mcan_get_rxfifo_fill_level(HPM_MCAN0, 0U));
    } else if (strcmp(sub, "ver") == 0) {
        Std_VersionInfoType v;
        Can_GetVersionInfo(&v);
        printf("Can: vendor=%u module=%u sw=%u.%u.%u\r\n", (unsigned) v.vendorID,
               (unsigned) v.moduleID, (unsigned) v.sw_major_version,
               (unsigned) v.sw_minor_version, (unsigned) v.sw_patch_version);
        printf("AR release %u.%u.%u, module id %u, DET=%s\r\n",
               (unsigned) CAN_AR_RELEASE_MAJOR_VERSION, (unsigned) CAN_AR_RELEASE_MINOR_VERSION,
               (unsigned) CAN_AR_RELEASE_REVISION_VERSION, (unsigned) CAN_MODULE_ID,
               (CAN_DEV_ERROR_DETECT == STD_ON) ? "ON" : "OFF");
    } else {
        printf("unknown sub: %s (info|echo|stream|stat|err|mode|ver)\r\n", sub);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 can, cmd_can, CAN echo test: can info|echo [n]|stream [n]|stat|err|mode|ver);

int main(void)
{
    Std_ReturnType st;

    bsp_init();
    s_cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    Det_Init();
    Det_ClearErrorCount();

    printf("\r\n=== can_classic_echo: AUTOSAR Can driver, classic 500k, CAN0 <-> CAN1 ===\r\n");
    st = E_OK;
    Can_Init(&Can_Config);
    printf("[init] Can_Init done, controllers=%u hth=%u hrh=%u filters=%u\r\n",
           (unsigned) Can_Config.ControllerCount, (unsigned) Can_Config.HthCount,
           (unsigned) Can_Config.HrhCount, (unsigned) Can_Config.FilterCount);
    if (Can_SetControllerMode(CTRL_A, CAN_CS_STARTED) != E_OK) {
        st = E_NOT_OK;
    }
    if (Can_SetControllerMode(CTRL_B, CAN_CS_STARTED) != E_OK) {
        st = E_NOT_OK;
    }
    printf("[start] CAN0/CAN1 STARTED, st=%u\r\n", (unsigned) st);
    if (st == E_OK) {
        bench_pingpong(200U);
        bench_stream(500U);
        test_error_paths();
    } else {
        printf("start failed, check transceiver/wiring\r\n");
    }
    printf("type 'echo [n]' / 'stream [n]' / 'stat' / 'err' / 'mode 0 start' / 'ver' / 'info'\r\n");
    while (1) {
        bsp_shell_poll();
        Can_MainFunction_Write();
        Can_MainFunction_Read();
        Can_MainFunction_BusOff();
        Can_MainFunction_Mode();
    }
    return 0;
}