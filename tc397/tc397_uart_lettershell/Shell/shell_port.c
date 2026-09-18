#include "shell_port.h"
#include "shell.h"
#include "IfxAsclin_Asc.h"
#include "IfxCpu_Irq.h"
#include "IfxStm.h"
#include "IfxPort.h"
#include "IfxScuRcu.h"
#include "IfxScuCcu.h"
#include "IfxScuWdt.h"
#include "Dts/Dts/IfxDts_Dts.h"
#include "Dts/Std/IfxDts.h"
#include "IfxScu_reg.h"
#include "IfxPms_reg.h"
#include "Configuration.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* UART handle is defined in UART_Logging.c as g_asc */
extern IfxAsclin_Asc g_asc;

/* letter-shell objects — enlarged for 921600 to avoid overrun on paste */
static Shell gShell;
static char gShellBuffer[1024];

#define SHELL_RX_RING_SIZE 1024
static volatile uint16_t gRxHead = 0;
static volatile uint16_t gRxTail = 0;
static volatile uint8_t gRxRing[SHELL_RX_RING_SIZE];
/* Overflow counter for diagnostics */
volatile uint32_t gShellRxOverflow = 0;

/* LED P13.0: low = on. Heartbeat enabled by default (1 Hz toggle). */
static volatile uint8_t gLedHeartbeat = 1;
static uint32_t gLedLastTick = 0;

static inline void Led_On(void)  { IfxPort_setPinLow(&MODULE_P13, 0); }
static inline void Led_Off(void) { IfxPort_setPinHigh(&MODULE_P13, 0); }
static inline void Led_Toggle(void) { IfxPort_togglePin(&MODULE_P13, 0); }

/* TASKING 不支持 GCC 扩展内联汇编 __asm__ volatile("dsync")；iLLD 的 __dsync()
 * 内联函数在双工具链下都可用（GCC 走 IntrinsicsGnuc，TASKING 走 IntrinsicsTasking）。
 * 此处仅做写屏障（volatile 环形队列索引），用 __dsync() 等价。 */
#if defined(__TASKING__)
#include "IfxCpu_Intrinsics.h"
#define SHELL_DSYNC() __dsync()
#else
#define SHELL_DSYNC() __asm__ volatile("dsync":::"memory")
#endif

void Shell_RxPush(uint8_t ch)
{
    uint16_t next = (uint16_t)((gRxHead + 1U) % SHELL_RX_RING_SIZE);
    if (next != gRxTail) {
        gRxRing[gRxHead] = ch;
        SHELL_DSYNC();
        gRxHead = next;
        SHELL_DSYNC();
    } else {
        gShellRxOverflow++;
    }
}

/* Bulk push — used by ISR to reduce per-byte overhead */
void Shell_RxPushBulk(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; ++i) Shell_RxPush(data[i]);
}

static short userShellWrite(char *data, unsigned short len)
{
    if (data == NULL || len == 0) {
        return 0;
    }
    Ifx_SizeT count = (Ifx_SizeT)len;
    /* Chunked write to keep TX ISR responsive at 921600 */
    Ifx_SizeT offset = 0;
    while (offset < count) {
        Ifx_SizeT chunk = (count - offset) > 128 ? 128 : (count - offset);
        Ifx_SizeT c = chunk;
        IfxAsclin_Asc_write(&g_asc, (uint8_t*)(data + offset), &c, TIME_INFINITE);
        if (c == 0) break;
        offset += c;
    }
    return (short)offset;
}

static short userShellRead(char *data, unsigned short len)
{
    if (data == NULL || len == 0) {
        return 0;
    }
    unsigned short i = 0;
    /* Snapshot head to avoid volatile re-read per iteration */
    uint16_t head = gRxHead;
    uint16_t tail = gRxTail;
    for (i = 0; i < len; i++) {
        if (head == tail) break;
        data[i] = (char)gRxRing[tail];
        tail = (uint16_t)((tail + 1U) % SHELL_RX_RING_SIZE);
    }
    gRxTail = tail;
    SHELL_DSYNC();
    return (short)i;
}

/* ---------- MCU / System commands ---------- */
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0

static int cmd_version(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    shellPrint(shell, "\r\nTC397 Letter-Shell Firmware\r\n");
    shellPrint(shell, "Version : %d.%d.%d\r\n", FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    shellPrint(shell, "Build   : %s %s\r\n", __DATE__, __TIME__);
    shellPrint(shell, "Board   : TC397XX 292pin (ASCLIN0 P14.0 TX / P14.1 RX, 921600)\r\n");
    return 0;
}

static int cmd_mcu(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32 chipId = SCU_CHIPID.U;
    shellPrint(shell, "\r\n=== MCU Info ===\r\n");
    shellPrint(shell, "ChipID  : 0x%08lX (CHREV=0x%02lX)\r\n", (unsigned long)chipId, (unsigned long)SCU_CHIPID.B.CHREV);
    shellPrint(shell, "SCU_ID  : 0x%08lX\r\n", (unsigned long)SCU_ID.U);
    shellPrint(shell, "RSTSTAT : 0x%08lX RSTCON: 0x%08lX\r\n", (unsigned long)SCU_RSTSTAT.U, (unsigned long)SCU_RSTCON.U);
    shellPrint(shell, "STM Freq: %lu Hz\r\n", (unsigned long)IfxStm_getFrequency(&MODULE_STM0));
    shellPrint(shell, "CCUCON0 : 0x%08lX CCUCON1: 0x%08lX\r\n", (unsigned long)SCU_CCUCON0.U, (unsigned long)SCU_CCUCON1.U);
    shellPrint(shell, "Build   : %s %s\r\n", __DATE__, __TIME__);
    shellPrint(shell, "================\r\n");
    return 0;
}

static int cmd_uid(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    shellPrint(shell, "UID/ChipID: 0x%08lX\r\n", (unsigned long)SCU_CHIPID.U);
    shellPrint(shell, "SCU_ID  : 0x%08lX\r\n", (unsigned long)SCU_ID.U);
    shellPrint(shell, "PMS DTSSTAT: 0x%04X\r\n", (unsigned)IfxDts_getTemperatureValue());
    return 0;
}

static int cmd_uptime(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32_t tick = g_TickCount_1ms;
    uint32_t sec = tick / 1000U;
    uint32_t ms = tick % 1000U;
    uint32_t min = sec / 60U;
    uint32_t hour = min / 60U;
    uint32_t day = hour / 24U;
    shellPrint(shell, "Uptime: %lu ms (%lu days %02lu:%02lu:%02lu.%03lu)\r\n",
           (unsigned long)tick,
           (unsigned long)day,
           (unsigned long)(hour % 24U),
           (unsigned long)(min % 60U),
           (unsigned long)(sec % 60U),
           (unsigned long)ms);
    return 0;
}

static int cmd_reset(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    shellPrint(shell, "MCU software reset in 500 ms...\r\n");
    uint32_t start = g_TickCount_1ms;
    while ((g_TickCount_1ms - start) < 500U) { }
    IfxScuRcu_performReset(IfxScuRcu_ResetType_system, 0);
    return 0;
}

static int cmd_temp(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint16 raw = IfxDts_getTemperatureValue();
    float32 c = IfxDts_Dts_convertToCelsius(raw);
    shellPrint(shell, "DTS raw=0x%04X (%u) -> %.2f C\r\n", (unsigned)raw, (unsigned)raw, (double)c);
    shellPrint(shell, "Limits: LOW=%d C HIGH=%d C\r\n", (int)IFXDTS_DEFAULT_TEMPERATURELIMIT_LOW, (int)IFXDTS_DEFAULT_TEMPERATURELIMIT_UPPER);
    shellPrint(shell, "PMS DTSSTAT RESULT field = %u\r\n", (unsigned)raw);
    return 0;
}

static int cmd_sysinfo(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    cmd_mcu(argc, argv);
    cmd_temp(argc, argv);
    cmd_uptime(argc, argv);
    shellPrint(shell, "sysinfo done\r\n");
    return 0;
}

/* ---------- LED P13.0 (low = on) ---------- */
static int cmd_led(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    if (argc < 2) {
        shellPrint(shell, "Usage: led <on|off|toggle|blink|hb> [args]\r\n");
        shellPrint(shell, "  led on            - LED on (P13.0 low), heartbeat off\r\n");
        shellPrint(shell, "  led off           - LED off (P13.0 high), heartbeat off\r\n");
        shellPrint(shell, "  led toggle        - toggle once, heartbeat off\r\n");
        shellPrint(shell, "  led blink <n> [ms]- blink n times (default 5 x 200ms), heartbeat off\r\n");
        shellPrint(shell, "  led hb <on|off>   - heartbeat 1Hz toggle on/off (default on)\r\n");
        shellPrint(shell, "LED P13.0 low=on. heartbeat=%s\r\n", gLedHeartbeat ? "on" : "off");
        return 0;
    }
    if (strcmp(argv[1], "on") == 0) {
        gLedHeartbeat = 0;
        Led_On();
        shellPrint(shell, "LED on (P13.0 low)\r\n");
    } else if (strcmp(argv[1], "off") == 0) {
        gLedHeartbeat = 0;
        Led_Off();
        shellPrint(shell, "LED off (P13.0 high)\r\n");
    } else if (strcmp(argv[1], "toggle") == 0) {
        gLedHeartbeat = 0;
        Led_Toggle();
        shellPrint(shell, "LED toggled\r\n");
    } else if (strcmp(argv[1], "blink") == 0) {
        int n = 5, ms = 200;
        if (argc >= 3) n = atoi(argv[2]);
        if (argc >= 4) ms = atoi(argv[3]);
        if (n <= 0) n = 1;
        if (n > 50) n = 50;
        if (ms < 20) ms = 20;
        if (ms > 2000) ms = 2000;
        gLedHeartbeat = 0;
        shellPrint(shell, "LED blink %d x %dms\r\n", n, ms);
        for (int i = 0; i < n; i++) {
            Led_Toggle();
            uint32_t start = g_TickCount_1ms;
            while ((g_TickCount_1ms - start) < (uint32_t)ms) { }
        }
        Led_Off();
        shellPrint(shell, "LED blink done (now off)\r\n");
    } else if (strcmp(argv[1], "hb") == 0) {
        if (argc >= 3 && strcmp(argv[2], "off") == 0) {
            gLedHeartbeat = 0;
            shellPrint(shell, "LED heartbeat off\r\n");
        } else {
            gLedHeartbeat = 1;
            gLedLastTick = g_TickCount_1ms;
            shellPrint(shell, "LED heartbeat on (1Hz)\r\n");
        }
    } else {
        shellPrint(shell, "Unknown led arg '%s'. Try 'led' for usage.\r\n", argv[1]);
        return -1;
    }
    return 0;
}

/* ---------- System commands (no LAN) ---------- */

static int cmd_mem(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    shellPrint(shell, "mem stats: bare-metal NO_SYS, no heap tracker\r\n");
    shellPrint(shell, "Use 'mcu' for chip info, 'help' for commands\r\n");
    return 0;
}

static int cmd_reboot(int argc, char *argv[]) { return cmd_reset(argc, argv); }
static int cmd_ver(int argc, char *argv[]) { return cmd_version(argc, argv); }

/* ---------- Shell export ---------- */
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), version, cmd_version, version info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ver, cmd_ver, version alias);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), mcu, cmd_mcu, MCU info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), uid, cmd_uid, chip UID);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), uptime, cmd_uptime, uptime);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), reset, cmd_reset, software reset);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), reboot, cmd_reboot, reboot alias);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), temp, cmd_temp, die temperature);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), sysinfo, cmd_sysinfo, system info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), mem, cmd_mem, memory info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), led, cmd_led, P13.0 LED control);

/* ---------- Shell init ---------- */
int Shell_Init(void)
{
    gShell.write = userShellWrite;
    gShell.read = userShellRead;
    shellInit(&gShell, gShellBuffer, sizeof(gShellBuffer));
    return 0;
}

void Shell_Process(void)
{
    /* 1 Hz heartbeat on P13.0 when enabled */
    if (gLedHeartbeat) {
        uint32_t now = g_TickCount_1ms;
        if ((now - gLedLastTick) >= 500U) {
            gLedLastTick = now;
            Led_Toggle();
        }
    }
    shellTask(&gShell);
}

void Shell_PrintBanner(void)
{
    Shell *shell = shellGetCurrent();
    if (shell) {
        shellPrint(shell, "\r\n");
        shellPrint(shell, "  _____  _____ _____  ___ ______\r\n");
        shellPrint(shell, " |_   _|/ ____|  __ \\|__  /__  /\r\n");
        shellPrint(shell, "   | | | |    | |__) |  / /  / / \r\n");
        shellPrint(shell, "   | | | |    |  _  /  / /  / /  \r\n");
        shellPrint(shell, "   | | | |____| | \\ \\ / /_ / /_ \r\n");
        shellPrint(shell, "   |_|  \\_____|_|  \\_\\____/____|\r\n");
        shellPrint(shell, "\r\nTC397 Letter-Shell v%d.%d.%d\r\n", FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
        shellPrint(shell, "Type 'help' for commands, 'mcu' for chip info\r\n");
        shellPrint(shell, "UART0: P14.0(TX) P14.1(RX) 921600bps (oversampling 16)\r\n");
        shellPrint(shell, "LED: P13.0 (low=on), try 'led blink 5'\r\n");
        shellPrint(shell, "\r\n");
    } else {
        printf("\r\nTC397 Letter-Shell v%d.%d.%d (no shell)\r\n", FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    }
}

int Shell_Exec(const char *cmd)
{
    if (cmd == NULL) return -1;
    return shellRun(&gShell, cmd);
}
