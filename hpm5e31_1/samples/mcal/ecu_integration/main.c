/*
 * ecu_integration —— 13 个 MCAL 模块的最小 ECU 集成演示
 *
 * 目的：验证"按 AUTOSAR 顺序初始化 + 1 ms 任务调度 + 模块协同"能否在 hpm5e31_1 上稳定跑；
 *      同时作为第 42/43 章"集成"一节的实物证据（日志里有各模块状态与 Det 计数）。
 *
 * 当前集成模块：Mcu / Port / Dio / Gpt / Spi / Can / Mem / Crc
 *               （Pwm / Icu / Adc / I2c / Wdg 落地后追加，见 README "模块矩阵"）
 *
 * 初始化顺序（对应 docs/chapters/43-mcal-infra.md 图 43-3）：
 *   Det -> Mcu(时钟) -> Port(引脚) -> Gpt -> Spi -> Can -> Mem -> Crc -> 主循环/Wdg
 *
 * 串口 921600 8N1，letter-shell 命令：
 *   ecu          状态总览（时钟/复位源/Det/各模块模式/tick）
 *   ecu led on|off|toggle       USR LED（PA02）
 *   ecu btn                     USR 按键（PA03）
 *   ecu tick <ms>               统计 N ms 内 1 ms tick 数（默认 10000）
 *   ecu can send <n>            CAN0 发 n 帧（ID 0x123，8 字节），CAN1 物理回环接收
 *   ecu can stat                CAN 控制器/RX/TX 计数
 *   ecu mem                     Mem 几何信息（保留扇区，只读）
 *   ecu crc                     CRC32 标准向量自检
 *   ecu reset                   软件复位（Mcu_PerformReset）
 */
#include "bsp.h"
#include "Mcal.h"
#include "Mcu.h"
#include "Port.h"
#include "Dio.h"
#include "Gpt.h"
#include "Spi.h"
#include "Can.h"
#include "Mem.h"
#include "Crc.h"
#include "ext/board_can.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- 板级逻辑通道（见各模块 PBcfg；换板只改 PBcfg 与这里的逻辑号） ---- */
#define ECU_LED_CH        (0u)   /* Dio 通道 0 = PA02 USR LED */
#define ECU_BTN_CH        (1u)   /* Dio 通道 1 = PA03 USR BTN */
#define ECU_CAN_HTH       (0u)   /* CAN0 的 HTH0 */
#define ECU_CAN_TX_ID     (0x123u)
#define ECU_TICK_TICKS    (100000u)  /* 1 ms @ GPTMR 100 MHz（第 45 章实测 100.0004 MHz） */

static volatile uint32 s_tick_1ms;
static volatile uint32 s_gpt_notifications;
static volatile uint32 s_can_tx_req;
static volatile uint32 s_can_tx_cfm;
static volatile uint32 s_can_rx;
static volatile uint32 s_can_busoff;
static volatile uint32 s_can_err;
static uint8 s_can_payload[8] = {0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u};

/* ---- Gpt 通知回调（PBcfg 里的弱符号被这里覆盖） ---- */
void Gpt_Notification_0(void)
{
    s_tick_1ms++;
    s_gpt_notifications++;
}

/* ---- CanIf 回调（Can 驱动里的弱符号被这里覆盖） ---- */
void CanIf_TxConfirmation(PduIdType CanTxPduId)
{
    (void)CanTxPduId;
    s_can_tx_cfm++;
}

void CanIf_RxIndication(const Can_HwType *Mailbox, const PduInfoType *PduInfoPtr)
{
    (void)Mailbox;
    (void)PduInfoPtr;
    s_can_rx++;
}

void CanIf_ControllerBusOff(uint8 ControllerId)
{
    (void)ControllerId;
    s_can_busoff++;
}

void CanIf_ErrorNotification(uint8 ControllerId, Can_ErrorType CanError)
{
    (void)ControllerId;
    (void)CanError;
    s_can_err++;
}

void CanIf_ControllerModeIndication(uint8 ControllerId, Can_ControllerStateType ControllerMode)
{
    (void)ControllerId;
    (void)ControllerMode;
}

void CanIf_ControllerWakeup(uint8 ControllerId)
{
    (void)ControllerId;
}

void CanIf_ControllerErrorStatePassive(uint8 ControllerId, uint16 RxErrorCounter, uint16 TxErrorCounter)
{
    (void)ControllerId;
    (void)RxErrorCounter;
    (void)TxErrorCounter;
}

/* ---- 初始化 ---- */
static uint32 ecu_init(void)
{
    uint32 errors = 0u;
    uint32 i;
    Std_ReturnType st;

    Det_Init();

    /* 1. Mcu：时钟必须先于所有外设 */
    Mcu_Init(&Mcu_Config);
    st = Mcu_InitClock(MCU_CLOCK_SETTING_0);
    if (st != E_OK) { errors++; }
    for (i = 0u; i < 1000000u; i++) {
        if (Mcu_GetPllStatus() == MCU_PLL_LOCKED) { break; }
    }
    if (Mcu_GetPllStatus() != MCU_PLL_LOCKED) { errors++; }
    st = Mcu_DistributePllClock();
    if (st != E_OK) { errors++; }

    /* 2. Port：引脚复用/方向，必须在 Gpt/Spi/Can 之前 */
    Port_Init(&Port_Config);

    /* 3. 各外设模块 */
    board_can_init_pins();
    board_can_transceivers_enable();     /* TCAN1044 STB=0，板级动作，不属于 MCAL API */

    Gpt_Init(&Gpt_Config);
    st = Gpt_StartTimer((Gpt_ChannelType)0u, (Gpt_ValueType)ECU_TICK_TICKS);
    if (st != E_OK) { errors++; }
    Gpt_EnableNotification((Gpt_ChannelType)0u);

    Spi_Init(&Spi_Config);
    Can_Init(&Can_Config);
    if (Can_SetControllerMode(0u, CAN_CS_STARTED) != E_OK) { errors++; }
    if (Can_SetControllerMode(1u, CAN_CS_STARTED) != E_OK) { errors++; }
    Mem_Init(&Mem_Config);

    return errors;
}

/* ---- 状态命令 ---- */
static void ecu_print_status(void)
{
    Can_ControllerStateType mode0 = CAN_CS_UNINIT;
    Can_ControllerStateType mode1 = CAN_CS_UNINIT;
    uint8 rec = 0u, tec = 0u;
    (void)Can_GetControllerMode(0u, &mode0);
    (void)Can_GetControllerMode(1u, &mode1);
    (void)Can_GetControllerRxErrorCounter(0u, &rec);
    (void)Can_GetControllerTxErrorCounter(0u, &tec);

    printf("[ecu] core=%u Hz (cfg 480000000) reset=%u raw=0x%08x\r\n",
           (unsigned)Mcu_GetCoreClockHz(), (unsigned)Mcu_GetResetReason(), (unsigned)Mcu_GetResetRawValue());
    printf("[ecu] tick=%u gpt_notify=%u det=%u\r\n",
           (unsigned)s_tick_1ms, (unsigned)s_gpt_notifications, (unsigned)Det_GetErrorCount());
    printf("[ecu] dio: led=%u btn=%u port_pins=%u\r\n",
           (unsigned)Dio_ReadChannel((Dio_ChannelType)ECU_LED_CH),
           (unsigned)Dio_ReadChannel((Dio_ChannelType)ECU_BTN_CH),
           (unsigned)Port_GetPinCount());
    printf("[ecu] spi: status=%u hw0=%u hw1=%u\r\n",
           (unsigned)Spi_GetStatus(), (unsigned)Spi_GetHWUnitStatus(0u), (unsigned)Spi_GetHWUnitStatus(1u));
    printf("[ecu] can: mode0=%u mode1=%u rec=%u tec=%u rx=%u tx_req=%u tx_cfm=%u busoff=%u err=%u\r\n",
           (unsigned)mode0, (unsigned)mode1, (unsigned)rec, (unsigned)tec,
           (unsigned)s_can_rx, (unsigned)s_can_tx_req, (unsigned)s_can_tx_cfm,
           (unsigned)s_can_busoff, (unsigned)s_can_err);
    printf("[ecu] mem: instances=%u pending=%u\r\n",
           (unsigned)Mem_GetInstanceCount(), (unsigned)Mem_IsJobPending());
}

static void cmd_ecu(int argc, char **argv)
{
    if ((argc >= 2) && (strcmp(argv[1], "led") == 0)) {
        if ((argc >= 3) && (strcmp(argv[2], "on") == 0)) {
            (void)Dio_WriteChannel((Dio_ChannelType)ECU_LED_CH, STD_HIGH);
        } else if ((argc >= 3) && (strcmp(argv[2], "off") == 0)) {
            (void)Dio_WriteChannel((Dio_ChannelType)ECU_LED_CH, STD_LOW);
        } else {
            (void)Dio_FlipChannel((Dio_ChannelType)ECU_LED_CH);
        }
        printf("[ecu] led=%u\r\n", (unsigned)Dio_ReadChannel((Dio_ChannelType)ECU_LED_CH));
        return;
    }
    if ((argc >= 2) && (strcmp(argv[1], "btn") == 0)) {
        printf("[ecu] btn=%u\r\n", (unsigned)Dio_ReadChannel((Dio_ChannelType)ECU_BTN_CH));
        return;
    }
    if ((argc >= 2) && (strcmp(argv[1], "tick") == 0)) {
        uint32 ms = (argc >= 3) ? (uint32)atoi(argv[2]) : 10000u;
        uint32 t0 = s_tick_1ms;
        uint32 start = s_tick_1ms;
        uint32 spins = 0u;
        while (((s_tick_1ms - start) < ms) && (spins < 500000000u)) { spins++; }
        printf("[ecu] tick: %u ms -> %u ticks (delta=%u), spins=%u\r\n",
               (unsigned)ms, (unsigned)s_tick_1ms, (unsigned)(s_tick_1ms - t0), (unsigned)spins);
        return;
    }
    if ((argc >= 2) && (strcmp(argv[1], "can") == 0)) {
        if ((argc >= 3) && (strcmp(argv[2], "send") == 0)) {
            uint32 n = (argc >= 4) ? (uint32)atoi(argv[3]) : 100u;
            uint32 ok = 0u, i;
            uint32 tick0 = s_tick_1ms;
            Can_PduType pdu;
            pdu.swPduHandle = 0x10u;
            pdu.length = (uint8)sizeof(s_can_payload);
            pdu.id = (Can_IdType)ECU_CAN_TX_ID;
            pdu.sdu = s_can_payload;
            for (i = 0u; i < n; i++) {
                if (Can_Write((Can_HwHandleType)ECU_CAN_HTH, &pdu) == E_OK) {
                    ok++;
                    s_can_tx_req++;
                }
                /* 每帧等 1 ms，避免把 HTH 队列灌满（demo 用节流，不是性能测试） */
                while ((s_tick_1ms - tick0) <= i) { }
            }
            printf("[ecu] can send: n=%u accepted=%u\r\n", (unsigned)n, (unsigned)ok);
            return;
        }
        if ((argc >= 3) && (strcmp(argv[2], "stat") == 0)) {
            ecu_print_status();
            return;
        }
    }
    if ((argc >= 2) && (strcmp(argv[1], "mem") == 0)) {
        uint32 geom[4] = {0u, 0u, 0u, 0u};
        Mem_LengthType len = (Mem_LengthType)sizeof(geom);
        if (Mem_HwSpecificService(0u, MEM_HW_SERVICE_ID_GET_GEOMETRY, (Mem_DataType *)geom, &len) == E_OK) {
            printf("[ecu] mem geometry: base=0x%08x size=%u sector=%u page=%u\r\n",
                   (unsigned)geom[0], (unsigned)geom[1], (unsigned)geom[2], (unsigned)geom[3]);
        } else {
            printf("[ecu] mem geometry: request failed\r\n");
        }
        printf("[ecu] mem instances=%u pending=%u\r\n",
               (unsigned)Mem_GetInstanceCount(), (unsigned)Mem_IsJobPending());
        return;
    }
    if ((argc >= 2) && (strcmp(argv[1], "crc") == 0)) {
        static const uint8 vec[] = "123456789";
        uint32 crc = Crc_CalculateCRC32(vec, 9u, 0xFFFFFFFFu, TRUE);
        printf("[ecu] crc32(\"123456789\")=0x%08x %s\r\n", (unsigned)crc,
               (crc == 0xCBF43926u) ? "PASS" : "FAIL(expect 0xCBF43926)");
        return;
    }
    if ((argc >= 2) && (strcmp(argv[1], "reset") == 0)) {
        printf("[ecu] software reset now...\r\n");
        Mcu_PerformReset();
        return;
    }
    ecu_print_status();
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC) | SHELL_CMD_DISABLE_RETURN,
                 ecu, cmd_ecu, minimal ECU integration demo);

/* ---- 1 ms 任务骨架：每 500 tick 翻转 LED，每 5000 tick 打印一次心跳 ---- */
static void ecu_task_1ms(void)
{
    if ((s_tick_1ms % 500u) == 0u) {
        (void)Dio_FlipChannel((Dio_ChannelType)ECU_LED_CH);
    }
    if ((s_tick_1ms % 5000u) == 0u) {
        printf("[hb] tick=%u det=%u can_rx=%u can_tx=%u\r\n",
               (unsigned)s_tick_1ms, (unsigned)Det_GetErrorCount(),
               (unsigned)s_can_rx, (unsigned)s_can_tx_cfm);
    }
}

int main(void)
{
    static uint32 last_tick;
    uint32 errs;

    bsp_init();
    printf("\r\n=== mcal ecu_integration: 13-module minimal ECU ===\r\n");
    errs = ecu_init();
    printf("[ecu] init done, errors=%u det=%u core=%u Hz\r\n",
           (unsigned)errs, (unsigned)Det_GetErrorCount(), (unsigned)Mcu_GetCoreClockHz());
    ecu_print_status();
    printf("[ecu] type 'ecu' / 'ecu led on' / 'ecu can send 100' / 'ecu crc' / 'ecu mem' / 'help'\r\n");

    while (1) {
        bsp_shell_poll();
        if (s_tick_1ms != last_tick) {
            last_tick = s_tick_1ms;
            ecu_task_1ms();
        }
    }
    return 0;
}
