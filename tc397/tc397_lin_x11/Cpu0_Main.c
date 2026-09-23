/**********************************************************************************************************************
 * \file Cpu0_Main.c
 * \brief TC397 LIN x11 (ASCLIN LIN master/slave + TLIN1024) + UART0 Letter-Shell + P13.0 LED.
 * \copyright Copyright (C) Infineon Technologies AG 2019
 *********************************************************************************************************************/
#include "Ifx_Types.h"
#include "IfxStm.h"
#include "IfxScuWdt.h"
#include "IfxScuRcu.h"
#include "IfxScuCcu.h"
#include "IfxPort.h"
#include "IfxCpu.h"
#include "IfxCpu_Irq.h"
#include "Bsp.h"
#include "Configuration.h"
#include "ConfigurationIsr.h"
#include "UART_Logging.h"
#include "shell_port.h"
#include "lin12.h"
#include "Dts/Dts/IfxDts_Dts.h"
#include "Dts/Std/IfxDts.h"
#include "IfxScu_reg.h"
#include "IfxPms_reg.h"
#include <stdio.h>
#include <string.h>

IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0;

/* STM tick for shell and uptime */
volatile uint32 g_TickCount_1ms = 0;

/* STM ISR: 1ms tick */
IFX_INTERRUPT(updateTickISR, 0, ISR_PRIORITY_OS_TICK);
void updateTickISR(void)
{
    IfxStm_increaseCompare(&MODULE_STM0, IfxStm_Comparator_0, IFX_CFG_STM_TICKS_PER_MS);
    g_TickCount_1ms++;
}

void core0_main(void)
{
    IfxCpu_enableInterrupts();
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());
    IfxCpu_emitEvent(&cpuSyncEvent);
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    /* STM 1ms tick */
    IfxStm_CompareConfig stmCompareConfig;
    IfxStm_initCompareConfig(&stmCompareConfig);
    stmCompareConfig.triggerPriority = ISR_PRIORITY_OS_TICK;
    stmCompareConfig.comparatorInterrupt = IfxStm_ComparatorInterrupt_ir0;
    stmCompareConfig.ticks = IFX_CFG_STM_TICKS_PER_MS * 10;
    stmCompareConfig.typeOfService = IfxSrc_Tos_cpu0;
    IfxStm_initCompare(&MODULE_STM0, &stmCompareConfig);

    /* P13.0 LED: output, high = off (low = on) */
    IfxPort_setPinMode(&MODULE_P13, 0, IfxPort_Mode_outputPushPullGeneral);
    IfxPort_setPinState(&MODULE_P13, 0, IfxPort_State_high);

    initUART();
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    /* DTS init */
    {
        IfxDts_Dts_Config dtsConf;
        IfxDts_Dts_initModuleConfig(&dtsConf);
        dtsConf.lowerTemperatureLimit = -40;
        dtsConf.upperTemperatureLimit = 170;
        dtsConf.isrPriority = 0;
        IfxDts_Dts_initModule(&dtsConf);
    }

    Shell_Init();
    {
        const char *msg = "After Shell_Init direct\r\n";
        Ifx_SizeT cnt = strlen(msg);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg, &cnt, TIME_INFINITE);
    }
    Shell_PrintBanner();
    {
        const char *msg2 = "After Banner direct\r\n";
        Ifx_SizeT cnt2 = strlen(msg2);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg2, &cnt2, TIME_INFINITE);
    }
    {
        const char *msg3 = "\r\nTC397 LIN x11 + Letter-Shell\r\n";
        Ifx_SizeT cnt3 = strlen(msg3);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg3, &cnt3, TIME_INFINITE);
        const char *msg4 = "Board: TC397XX 292pin (ASCLIN0 P14.0 TX / P14.1 RX, 921600)\r\n";
        Ifx_SizeT cnt4 = strlen(msg4);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg4, &cnt4, TIME_INFINITE);
        const char *msg5 = "Type 'help' for commands, 'linstat' for LIN, 'linpair' for master/slave test\r\n";
        Ifx_SizeT cnt5 = strlen(msg5);
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)msg5, &cnt5, TIME_INFINITE);
    }
    /* Also print early system info via direct */
    {
        uint32_t chipId = SCU_CHIPID.U;
        char buf[128];
        int len = snprintf(buf, sizeof(buf), "ChipID: 0x%08lX CHREV=0x%02lX\r\n", (unsigned long)chipId, (unsigned long)SCU_CHIPID.B.CHREV);
        Ifx_SizeT cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
        len = snprintf(buf, sizeof(buf), "SCU_ID: 0x%08lX RSTSTAT: 0x%08lX\r\n", (unsigned long)SCU_ID.U, (unsigned long)SCU_RSTSTAT.U);
        cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
        len = snprintf(buf, sizeof(buf), "STM Freq: %lu Hz Tick: %lu\r\n", (unsigned long)IfxStm_getFrequency(&MODULE_STM0), (unsigned long)g_TickCount_1ms);
        cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
        uint16_t raw = IfxDts_getTemperatureValue();
        float c = IfxDts_Dts_convertToCelsius(raw);
        len = snprintf(buf, sizeof(buf), "DTS raw=0x%04X -> %.2f C\r\n", (unsigned)raw, (double)c);
        cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
    }

    /* TLIN1024 EN (board SLP_N) to Normal, then LIN1..11: 19200.
     * Masters: LIN3 (bus A: 1-2-3), LIN7 (bus B: 4-5-6-7),
     * LIN11 (bus C: 8-9-10-11); rest slave. */
    lin_xcvr_init();
    lin12_init_all_19200();
    {
        char buf[128];
        int len = snprintf(buf, sizeof(buf),
            "LIN 11ch init done: 19200 masters=3(A:1-3) 7(B:4-7) 11(C:8-11) (LIN0 placeholder)\r\n");
        Ifx_SizeT cnt = len;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)buf, &cnt, TIME_INFINITE);
    }

    /* LED on briefly to show boot, then heartbeat takes over (1 Hz in Shell_Process) */
    IfxPort_setPinLow(&MODULE_P13, 0);

    while(1)
    {
        UART_Poll();
        Shell_Process();
    }
}
