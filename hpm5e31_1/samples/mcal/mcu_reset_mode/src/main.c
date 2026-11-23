/*
 * mcu_reset_mode —— AUTOSAR Mcu 复位源读取 + 软件复位 + 模式切换（HPM5E31）
 *
 * 验证点：
 *   1) Mcu_GetResetReason / Mcu_GetResetRawValue 读 PPOR RESET_FLAG；
 *   2) Mcu_PerformReset 触发软件复位（PPOR SOFTWARE_RESET），重启后复位源应变为 MCU_SW_RESET，
 *      串口前后两次启动自证（同一个串口日志里出现 SW_RESET 即证明复位链路通）；
 *   3) Mcu_SetMode(MCU_MODE_RESET) 与 Mcu_PerformReset 等效；MCU_MODE_RUN 可切回运行态；
 *   4) 开发错误路径 → Det 计数增加。
 *
 * 例程：samples/mcal/mcu_reset_mode；章节：docs/chapters/44-mcal-core.md
 */
#include "bsp.h"
#include "Mcu.h"
#include "Det.h"
#include <stdlib.h>

static const char *reset_name(Mcu_ResetType r)
{
    switch (r)
    {
    case MCU_POWER_ON_RESET: return "MCU_POWER_ON_RESET";
    case MCU_WATCHDOG_RESET: return "MCU_WATCHDOG_RESET";
    case MCU_SW_RESET:       return "MCU_SW_RESET";
    default:                 return "MCU_RESET_UNDEFINED";
    }
}

static void print_reset(void)
{
    Mcu_ResetType reason = Mcu_GetResetReason();
    Mcu_RawResetType raw = Mcu_GetResetRawValue();

    printf("RESET: reason=%s(%u) raw=0x%08X\r\n", reset_name(reason), (unsigned)reason, (unsigned)raw);
    if ((raw & 0x80000000u) != 0u) { printf("RESET: bit31 software reset <- Mcu_PerformReset worked\r\n"); }
    if ((raw & 0x00010000u) != 0u) { printf("RESET: bit16 wdog0\r\n"); }
    if ((raw & 0x00000010u) != 0u) { printf("RESET: bit4 debug reset (J-Link)\r\n"); }
    if ((raw & 0x00000001u) != 0u) { printf("RESET: bit0 brownout / power-on\r\n"); }
}

static void cmd_info(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Mcu reset/mode demo: moduleID=%u\r\n", (unsigned)MCU_MODULE_ID);
    print_reset();
    printf("commands: minfo | mclear | mreset | mmode <run|reset> | mver | merr\r\n");
}

static void cmd_clear(int argc, char **argv)
{
    (void)argc; (void)argv;
    Mcu_ClearResetFlags();
    printf("PPOR RESET_FLAG cleared (W1C)\r\n");
    print_reset();
}

static void cmd_reset(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Mcu_PerformReset(): software reset in ~4 us ...\r\n");
    Mcu_PerformReset();
    printf("ERROR: Mcu_PerformReset() returned (should not happen)\r\n");
}

static void cmd_mode(int argc, char **argv)
{
    uint32_t mode;

    if (argc < 2)
    {
        printf("usage: mmode <0=run|2=reset>\r\n");
        return;
    }
    mode = (uint32_t)atoi(argv[1]);
    if (mode == 0u)
    {
        Mcu_SetMode(MCU_MODE_RUN);
        printf("Mcu_SetMode(MCU_MODE_RUN) done\r\n");
    }
    else if (mode == 2u)
    {
        printf("Mcu_SetMode(MCU_MODE_RESET): software reset ...\r\n");
        Mcu_SetMode(MCU_MODE_RESET);
        printf("ERROR: Mcu_SetMode(MCU_MODE_RESET) returned (should not happen)\r\n");
    }
    else
    {
        printf("mmode: unsupported %u (only 0=run, 2=reset; 1=sleep 需先配唤醒源)\r\n", (unsigned)mode);
    }
}

static void cmd_ver(int argc, char **argv)
{
    Std_VersionInfoType vi;
    (void)argc; (void)argv;
    Mcu_GetVersionInfo(&vi);
    printf("Mcu_GetVersionInfo: vendor=0x%04X module=%u sw=%u.%u.%u\r\n", (unsigned)vi.vendorID,
           (unsigned)vi.moduleID, (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version);
}

static void cmd_err(int argc, char **argv)
{
    uint32_t before = Det_GetErrorCount();
    (void)argc; (void)argv;
    Mcu_SetMode((Mcu_ModeType)7u);        /* MCU_E_PARAM_MODE */
    Mcu_GetVersionInfo(NULL_PTR);         /* MCU_E_PARAM_POINTER */
    printf("DET: before=%u after=%u delta=%u\r\n", (unsigned)before, (unsigned)Det_GetErrorCount(),
           (unsigned)(Det_GetErrorCount() - before));
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 minfo, cmd_info, Mcu reset info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mclear, cmd_clear, clear PPOR reset flags);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mreset, cmd_reset, Mcu_PerformReset software reset);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mmode, cmd_mode, mmode <0=run|2=reset>: Mcu_SetMode);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mver, cmd_ver, Mcu_GetVersionInfo);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 merr, cmd_err, inject Dev errors);

int main(void)
{
    bsp_init();
    Det_Init();

    printf("\r\n=== mcu_reset_mode: AUTOSAR Mcu reset source / software reset ===\r\n");
    Mcu_Init(&Mcu_Config);
    print_reset();
    printf("BOOT: reason=%s\r\n", reset_name(Mcu_GetResetReason()));
    printf("type: minfo / mclear / mreset / mmode 0|2 / mver / merr\r\n");
    printf("try: mclear;mreset  -> board reboots and reports MCU_SW_RESET\r\n");

    while (1)
    {
        bsp_shell_poll();
    }
    return 0;
}
