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
#include "Gpt_PBcfg.h"
#include "Spi.h"
#include "Can.h"
#include "Can_PBcfg.h"
#include "Mem.h"
#include "Mem_PBcfg.h"
#include "Crc.h"
#include "Pwm.h"
#include "Pwm_PBcfg.h"
#include "Icu.h"
#include "Icu_PBcfg.h"
#include "Wdg.h"
#include "Wdg_PBcfg.h"
#include "I2c.h"
#include "I2c_PBcfg.h"
#include "Adc.h"
#include "Adc_PBcfg.h"
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
static volatile uint16 s_pwm_duty0 = 0x4000u;   /* Pwm ch0 当前占空比（0..0x8000） */
/* PD02/PD03 与 PD07/PD06 的归属：0 = Pwm/Icu，1 = I2c（板载杜邦线把两组引脚连在一起，只能分时复用） */
static volatile uint32 s_pin_owner;
static uint8 s_i2c_txbuf[4] = {0xA5u, 0x5Au, 0x12u, 0x34u};
static Adc_ValueGroupType s_adc_buf[2] = {0u, 0u};

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

/* 长阻塞命令里也要喂狗：shell 命令执行期间主循环不跑，1 s 的 busy-wait 会触发 1000 ms 看门狗 */
static void ecu_feed_wdg(void)
{
    Wdg_SetTriggerCondition((uint16)Wdg_Config.slowTimeoutMs);
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
    Gpt_EnableNotification((Gpt_ChannelType)0u);
    Gpt_StartTimer((Gpt_ChannelType)0u, (Gpt_ValueType)ECU_TICK_TICKS);

    Spi_Init(&Spi_Config);
    Can_Init(&Can_Config);
    if (Can_SetControllerMode(0u, CAN_CS_STARTED) != E_OK) { errors++; }
    if (Can_SetControllerMode(1u, CAN_CS_STARTED) != E_OK) { errors++; }
    Mem_Init(&Mem_Config);

    /* 4. 输出/捕获/看门狗（Pwm 与 Icu 的引脚通过板载杜邦线互连；Wdg 最后使能） */
    Pwm_Init(&Pwm_Config);
    s_pwm_duty0 = 0x4000u;                                     /* 50% */
    Pwm_SetDutyCycle((Pwm_ChannelType)0u, s_pwm_duty0);
    Pwm_SetDutyCycle((Pwm_ChannelType)1u, (uint16)0x2000u);    /* 25% */
    Icu_Init(&Icu_Config);
    Icu_EnableEdgeDetection((Icu_ChannelType)0u);
    Icu_EnableEdgeDetection((Icu_ChannelType)1u);

    Adc_Init(&Adc_Config);
    (void)Adc_SetupResultBuffer(ADC_GROUP_SINGLE, s_adc_buf);
    Adc_StartGroupConversion(ADC_GROUP_SINGLE);

    Wdg_Init(&Wdg_Config);
    Wdg_SetTriggerCondition((uint16)Wdg_Config.slowTimeoutMs);

    return errors;
}

/* ---- PD02/PD03/PD06/PD07 引脚归属切换 ---- */
static void pins_to_pwm_icu(void)
{
    if (s_pin_owner == 0u) {
        return;
    }
    I2C_DeInit();
    Pwm_Init(&Pwm_Config);
    Pwm_SetDutyCycle((Pwm_ChannelType)0u, s_pwm_duty0);
    Pwm_SetDutyCycle((Pwm_ChannelType)1u, (uint16)0x2000u);
    Icu_Init(&Icu_Config);
    Icu_EnableEdgeDetection((Icu_ChannelType)0u);
    Icu_EnableEdgeDetection((Icu_ChannelType)1u);
    s_pin_owner = 0u;
    printf("[ecu] pins -> Pwm/Icu (I2c deinit)\r\n");
}

static void pins_to_i2c(void)
{
    if (s_pin_owner == 1u) {
        return;
    }
    Pwm_DeInit();
    Icu_DeInit();
    I2C_Init(&I2C_Config);
    s_pin_owner = 1u;
    printf("[ecu] pins -> I2c (Pwm/Icu deinit)\r\n");
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
    {
        Icu_DutyCycleType duty = {0u, 0u};
        Icu_GetDutyCycleValues((Icu_ChannelType)0u, &duty);
        printf("[ecu] pwm: ch0=0x%04x ch1=0x2000 state=%u\r\n",
               (unsigned)s_pwm_duty0,
               (unsigned)Pwm_GetOutputState((Pwm_ChannelType)0u));
        printf("[ecu] icu: ch0 in=%u active=0x%08x period=0x%08x\r\n",
               (unsigned)Icu_GetInputState((Icu_ChannelType)0u),
               (unsigned)duty.ActiveTime, (unsigned)duty.PeriodTime);
        if (s_pin_owner == 1u) {
            printf("[ecu] i2c: ch0=%u ch1=%u\r\n",
                   (unsigned)I2C_GetChannelStatus(I2C_CH_I2C2_MASTER),
                   (unsigned)I2C_GetChannelStatus(I2C_CH_I2C3_TARGET));
        } else {
            printf("[ecu] i2c: not initialized (pins owned by Pwm/Icu)\r\n");
        }
    }
    {
        uint32 raw = 0u;
        if (Adc_ReadGroup(ADC_GROUP_SINGLE, s_adc_buf) == E_OK) {
            raw = (uint32)s_adc_buf[0];
        }
        printf("[ecu] adc: group0 status=%u raw=%u (%u mV, VREFH=3300)\r\n",
               (unsigned)Adc_GetGroupStatus(ADC_GROUP_SINGLE), (unsigned)raw,
               (unsigned)((raw * 3300u) / 65535u));
    }
    printf("[ecu] wdg: state=%u mode=%u timeout=%u ms\r\n",
           (unsigned)Wdg_GetState(), (unsigned)Wdg_GetMode(),
           (unsigned)Wdg_Config.slowTimeoutMs);
    printf("[ecu] mem: instances=%u pending=%u\r\n",
           (unsigned)Mem_GetInstanceCount(), (unsigned)Mem_IsJobPending());
}

static int cmd_ecu(int argc, char **argv)
{
    /* letter-shell 的 CMD_MAIN 约定：argc 含命令名，argv[0]="ecu"，argv[1] 起才是参数 */
    if ((argc >= 2) && (strcmp(argv[1], "led") == 0)) {
        if ((argc >= 3) && (strcmp(argv[2], "on") == 0)) {
            (void)Dio_WriteChannel((Dio_ChannelType)ECU_LED_CH, STD_HIGH);
        } else if ((argc >= 3) && (strcmp(argv[2], "off") == 0)) {
            (void)Dio_WriteChannel((Dio_ChannelType)ECU_LED_CH, STD_LOW);
        } else {
            (void)Dio_FlipChannel((Dio_ChannelType)ECU_LED_CH);
        }
        printf("[ecu] led=%u\r\n", (unsigned)Dio_ReadChannel((Dio_ChannelType)ECU_LED_CH));
        return 0;
    }
    if ((argc >= 2) && (strcmp(argv[1], "btn") == 0)) {
        printf("[ecu] btn=%u\r\n", (unsigned)Dio_ReadChannel((Dio_ChannelType)ECU_BTN_CH));
        return 0;
    }
    if ((argc >= 2) && (strcmp(argv[1], "tick") == 0)) {
        uint32 ms = (argc >= 3) ? (uint32)atoi(argv[2]) : 10000u;
        uint32 t0 = s_tick_1ms;
        uint32 spins = 0u;
        while (((s_tick_1ms - t0) < ms) && (spins < 200000000u)) {
            spins++;
            if ((spins & 0x3FFFu) == 0u) {
                ecu_feed_wdg();
            }
        }
        printf("[ecu] tick: %u ms -> %u ticks (delta=%u), spins=%u\r\n",
               (unsigned)ms, (unsigned)s_tick_1ms, (unsigned)(s_tick_1ms - t0), (unsigned)spins);
        return 0;
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
                ecu_feed_wdg();
                /* 每帧等 1 ms，并跑 CanIf 周期函数（确认/释放 HTH），避免把队列灌满 */
                while ((s_tick_1ms - tick0) <= i) {
                    Can_MainFunction_Write();
                    Can_MainFunction_Read();
                }
            }
            printf("[ecu] can send: n=%u accepted=%u\r\n", (unsigned)n, (unsigned)ok);
            return 0;
        }
        if ((argc >= 3) && (strcmp(argv[2], "stat") == 0)) {
            ecu_print_status();
            return 0;
        }
    }
    if ((argc >= 2) && (strcmp(argv[1], "pwm") == 0)) {
        uint32 pct = (argc >= 3) ? (uint32)atoi(argv[2]) : 50u;
        pins_to_pwm_icu();
        uint16 duty = (uint16)((pct > 100u) ? 0x8000u : (pct * 0x8000u / 100u));
        Pwm_SetDutyCycle((Pwm_ChannelType)0u, duty);
        s_pwm_duty0 = duty;
        printf("[ecu] pwm ch0 duty=%u%% (0x%04x) state=%u\r\n",
               (unsigned)pct, (unsigned)duty, (unsigned)Pwm_GetOutputState((Pwm_ChannelType)0u));
        return 0;
    }
    if ((argc >= 2) && (strcmp(argv[1], "icu") == 0)) {
        Icu_DutyCycleType duty = {0u, 0u};
        pins_to_pwm_icu();
        Icu_GetDutyCycleValues((Icu_ChannelType)0u, &duty);
        printf("[ecu] icu ch0: in=%u active=0x%08x period=0x%08x elapsed=%u\r\n",
               (unsigned)Icu_GetInputState((Icu_ChannelType)0u),
               (unsigned)duty.ActiveTime, (unsigned)duty.PeriodTime,
               (unsigned)Icu_GetTimeElapsed((Icu_ChannelType)0u));
        return 0;
    }
    if ((argc >= 2) && (strcmp(argv[1], "i2c") == 0)) {
        Std_ReturnType st;
        pins_to_i2c();
        st = I2C_SetupEB(I2C_JOB_MASTER_TX, I2C_TARGET_ADDR, s_i2c_txbuf, NULL_PTR,
                         (I2C_NumberOfDataType)sizeof(s_i2c_txbuf));
        printf("[ecu] i2c setupEB=%u\r\n", (unsigned)st);
        st = I2C_SyncTransmit(I2C_SEQ_WRITE);
        printf("[ecu] i2c sync seq0 rc=%u result=%u ch0=%u ch1=%u\r\n",
               (unsigned)st, (unsigned)I2C_GetSequenceResult(I2C_SEQ_WRITE),
               (unsigned)I2C_GetChannelStatus(I2C_CH_I2C2_MASTER),
               (unsigned)I2C_GetChannelStatus(I2C_CH_I2C3_TARGET));
        return 0;
    }
    if ((argc >= 2) && (strcmp(argv[1], "adc") == 0)) {
        uint32 n = (argc >= 3) ? (uint32)atoi(argv[2]) : 100u;
        uint32 i, raw, min = 0xFFFFu, max = 0u;
        uint32 sum = 0u;
        Adc_ValueGroupType v[2];
        if (n > 10000u) { n = 10000u; }
        for (i = 0u; i < n; i++) {
            ecu_feed_wdg();
            Adc_StartGroupConversion(ADC_GROUP_SINGLE);
            if (Adc_ReadGroup(ADC_GROUP_SINGLE, v) == E_OK) {
                raw = (uint32)v[0];
                sum += raw;
                if (raw < min) { min = raw; }
                if (raw > max) { max = raw; }
            }
        }
        printf("[ecu] adc: n=%u mean=%u min=%u max=%u status=%u\r\n",
               (unsigned)n, (unsigned)((n != 0u) ? (sum / n) : 0u),
               (unsigned)min, (unsigned)max,
               (unsigned)Adc_GetGroupStatus(ADC_GROUP_SINGLE));
        return 0;
    }
    if ((argc >= 2) && (strcmp(argv[1], "wdg") == 0)) {
        printf("[ecu] wdg: state=%u mode=%u slow=%u fast=%u max=%u\r\n",
               (unsigned)Wdg_GetState(), (unsigned)Wdg_GetMode(),
               (unsigned)Wdg_Config.slowTimeoutMs, (unsigned)Wdg_Config.fastTimeoutMs,
               (unsigned)Wdg_Config.maxTimeoutMs);
        Wdg_SetTriggerCondition((uint16)Wdg_Config.slowTimeoutMs);
        return 0;
    }
    if ((argc >= 2) && (strcmp(argv[1], "mem") == 0)) {
        uint32 geom[4] = {0u, 0u, 0u, 0u};
        Mem_LengthType len = (Mem_LengthType)sizeof(geom);
        if (Mem_HwSpecificService(0u, MEM_HW_SERVICE_ID_GET_GEOMETRY, (Mem_DataType *)geom, &len) == E_OK) {
            uint32 guard = 0u;
            while (Mem_IsJobPending() && (guard < 100000u)) {
                Mem_MainFunction();
                guard++;
            }
            if ((Mem_GetJobResult(0u) == MEM_JOB_OK) && (len >= 16u)) {
                printf("[ecu] mem geometry: base=0x%08x size=%u sector=%u page=%u (guard=%u)\r\n",
                       (unsigned)geom[0], (unsigned)geom[1], (unsigned)geom[2], (unsigned)geom[3],
                       (unsigned)guard);
            } else {
                printf("[ecu] mem geometry: job result=%u len=%u\r\n",
                       (unsigned)Mem_GetJobResult(0u), (unsigned)len);
            }
        } else {
            printf("[ecu] mem geometry: request failed\r\n");
        }
        printf("[ecu] mem instances=%u pending=%u\r\n",
               (unsigned)Mem_GetInstanceCount(), (unsigned)Mem_IsJobPending());
        return 0;
    }
    if ((argc >= 2) && (strcmp(argv[1], "crc") == 0)) {
        static const uint8 vec[] = "123456789";
        uint32 crc;
        printf("[ecu] crc: sw path start\r\n");
        Crc_SetHwEnabled(FALSE);
        crc = Crc_CalculateCRC32(vec, 9u, 0xFFFFFFFFu, TRUE);
        printf("[ecu] crc32 sw =0x%08x %s\r\n", (unsigned)crc,
               (crc == 0xCBF43926u) ? "PASS" : "FAIL(expect 0xCBF43926)");
        Crc_SetHwEnabled(TRUE);
        printf("[ecu] crc: hw path start\r\n");
        crc = Crc_CalculateCRC32(vec, 9u, 0xFFFFFFFFu, TRUE);
        printf("[ecu] crc32 hw =0x%08x %s\r\n", (unsigned)crc,
               (crc == 0xCBF43926u) ? "PASS" : "FAIL(expect 0xCBF43926)");
        return 0;
    }
    if ((argc >= 2) && (strcmp(argv[1], "reset") == 0)) {
        printf("[ecu] software reset now...\r\n");
        Mcu_PerformReset();
        return 0;
    }
    ecu_print_status();
    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ecu, cmd_ecu, minimal ECU integration demo);

/* ---- 1 ms 任务骨架：每 500 tick 翻转 LED，每 5000 tick 打印一次心跳 ---- */
static void ecu_task_1ms(void)
{
    if ((s_tick_1ms % 500u) == 0u) {
        (void)Dio_FlipChannel((Dio_ChannelType)ECU_LED_CH);
    }
    if ((s_tick_1ms % 100u) == 0u) {
        Wdg_SetTriggerCondition((uint16)Wdg_Config.slowTimeoutMs);   /* 100 ms 喂狗，超时 500 ms */
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
        Mem_MainFunction();
        Can_MainFunction_Write();
        Can_MainFunction_Read();
        Can_MainFunction_BusOff();
        Can_MainFunction_Mode();
        if (s_pin_owner == 1u) {
            I2C_MainFunction();
        }
        if (s_tick_1ms != last_tick) {
            last_tick = s_tick_1ms;
            ecu_task_1ms();
        }
    }
    return 0;
}