/* Letter-shell `adc` command: print AN0..AN47 in order.
 *
 *   adc        print all AN0..AN47 (skipped P40-GPIO rows show Reserved)
 *   adc <n>    print single AN channel 0..47
 *
 * Pin voltage: raw * 5.0 / 4096 (VREF = VDDM = 5V from TLF35584 VREF).
 * 47K+3K divider channels also show external voltage = pin * 50/3.
 * HW_VERSION (VUC 10K+1K) also shows VUC equivalent = pin * 11.
 */
#include "shell.h"
#include "adc.h"
#include <stdio.h>
#include <stdlib.h>

static void Adc_PrintOne(Shell *shell, uint8 an)
{
    const AdcAnInfo *info = &g_AdcAnTable[an];
    uint16 raw = 0;
    float vPin = 0.0f;

    if (info->kind == AdcKind_Skip)
    {
        shellPrint(shell, "AN%02u %-9s Reserved (P40.x GPIO, not sampled)\r\n",
                   (unsigned)an, info->name);
        return;
    }
    if (!Adc_ReadAn(an, &raw, &vPin))
    {
        shellPrint(shell, "AN%02u %-9s NO-DATA\r\n", (unsigned)an, info->name);
        return;
    }
    switch (info->kind)
    {
        case AdcKind_Div50_3:
            shellPrint(shell, "AN%02u %-9s pin=%5.3fV ext=%6.3fV raw=%4u (G%uCH%u)\r\n",
                       (unsigned)an, info->name, (double)vPin,
                       (double)Adc_Div50_3_ToExt(vPin), (unsigned)raw,
                       (unsigned)info->group, (unsigned)info->channel);
            break;
        case AdcKind_HwVer:
            shellPrint(shell, "AN%02u %-9s pin=%5.3fV vuc~%5.3fV raw=%4u (G%uCH%u)\r\n",
                       (unsigned)an, info->name, (double)vPin,
                       (double)Adc_HwVer_ToVuc(vPin), (unsigned)raw,
                       (unsigned)info->group, (unsigned)info->channel);
            break;
        default:
            shellPrint(shell, "AN%02u %-9s pin=%5.3fV raw=%4u (G%uCH%u)\r\n",
                       (unsigned)an, info->name, (double)vPin, (unsigned)raw,
                       (unsigned)info->group, (unsigned)info->channel);
            break;
    }
}

static int cmd_adc(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint8 an;

    if (argc >= 2)
    {
        long n = strtol(argv[1], 0, 0);
        if (n < 0 || n > 47)
        {
            shellPrint(shell, "Usage: adc [0..47]\r\n");
            return -1;
        }
        Adc_PrintOne(shell, (uint8)n);
        return 0;
    }
    shellPrint(shell, "AN   Signal    Pin      Ext      Raw   Src\r\n");
    for (an = 0; an < 48; ++an)
    {
        Adc_PrintOne(shell, an);
    }
    shellPrint(shell, "VREF=5.0V(12bit); ext=pin*50/3 (47K+3K); HW_VERSION vuc~=pin*11\r\n");
    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 adc, cmd_adc, AN0..AN47 voltages);
