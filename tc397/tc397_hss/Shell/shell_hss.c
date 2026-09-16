/* Letter-shell `hss` command: 2x WSTD6020AN high-side switch control + sense.
 *
 *   hss                       status of both devices (GPIO + AN14/AN15 + Iout est)
 *   hss on <dev> <ch>         IN high -> OUT on (dev 0..1, ch 0..1)
 *   hss off <dev> <ch>        IN low  -> OUT off
 *   hss den <dev> <0|1>       diagnostic enable (1 = IS active, 0 = IS Hi-Z)
 *   hss dsel <dev> <0|1>      IS mux address (0 = ch0, 1 = ch1)
 *   hss diag [dev [ch]]       turn IN on + DEN=1/DSEL=ch, wait 3ms, read IS once
 *   hss offall                all IN low + DEN low (safe state)
 *
 * Sense path: IS -> 1K to GND -> Vis = Iis * 1K -> AN14 (HSS0) / AN15 (HSS1).
 * adc convention prints pin (ADC pin) and ext (pin*50/3, 47K+3K divider).
 * Which node `ext` corresponds to depends on board wiring; both hypotheses
 * are printed so the 1K-load (~12mA @ 12V) measurement disambiguates:
 *   A) ext = Vis (divider between IS node and ADC): Iis = ext/1K
 *   B) pin = Vis (direct to ADC):                   Iis = pin/1K
 * Iout = K * Iis with K0=1560 (50mA) / K1=2500 (0.5A) / K2=2630 (>=2A).
 */
#include "shell.h"
#include "adc.h"
#include "hss.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* HSS dev -> sense AN */
static uint8 Hss_SenseAn(uint8 dev)
{
    return (dev == 0) ? 14u : 15u;
}

static void Hss_PrintSense(Shell *shell, uint8 dev)
{
    uint8 an = Hss_SenseAn(dev);
    uint16 raw = 0;
    float vPin = 0.0f;

    if (!Adc_ReadAn(an, &raw, &vPin))
    {
        shellPrint(shell, "  IS: AN%02u NO-DATA (DEN=%d?)\r\n",
                   (unsigned)an, Hss_GetDen(dev));
        return;
    }
    {
        float vExt  = Adc_Div50_3_ToExt(vPin);
        float iisA  = vExt / HSS_RIS_OHM;   /* hypothesis A: ext = Vis */
        float iisB  = vPin / HSS_RIS_OHM;   /* hypothesis B: pin = Vis */
        float iA0 = Hss_IoutFromVis(vExt, HSS_K0);
        float iA1 = Hss_IoutFromVis(vExt, HSS_K1);
        float iA2 = Hss_IoutFromVis(vExt, HSS_K2);
        float iB0 = Hss_IoutFromVis(vPin, HSS_K0);
        float iB1 = Hss_IoutFromVis(vPin, HSS_K1);
        float iB2 = Hss_IoutFromVis(vPin, HSS_K2);

        shellPrint(shell, "  IS: AN%02u pin=%5.3fV ext=%6.3fV raw=%4u\r\n",
                   (unsigned)an, (double)vPin, (double)vExt, (unsigned)raw);
        shellPrint(shell, "      A(ext=Vis): Iis=%6.3fmA Iout(K0/K1/K2)=%7.2f/%7.2f/%7.2fmA\r\n",
                   (double)(iisA * 1000.0f),
                   (double)(iA0 * 1000.0f),
                   (double)(iA1 * 1000.0f),
                   (double)(iA2 * 1000.0f));
        shellPrint(shell, "      B(pin=Vis): Iis=%6.3fmA Iout(K0/K1/K2)=%7.2f/%7.2f/%7.2fmA\r\n",
                   (double)(iisB * 1000.0f),
                   (double)(iB0 * 1000.0f),
                   (double)(iB1 * 1000.0f),
                   (double)(iB2 * 1000.0f));
    }
}

static void Hss_PrintDev(Shell *shell, uint8 dev)
{
    int in0  = Hss_GetIn(dev, 0);
    int in1  = Hss_GetIn(dev, 1);
    int den  = Hss_GetDen(dev);
    int dsel = Hss_GetDsel(dev);

    shellPrint(shell, "HSS%u: IN0=%d IN1=%d DEN=%d DSEL=%d (mux->ch%d)\r\n",
               (unsigned)dev, in0, in1, den, dsel, (dsel > 0) ? 1 : 0);
    Hss_PrintSense(shell, dev);
}

static void Hss_PrintStatus(Shell *shell)
{
    shellPrint(shell, "WSTD6020AN x2 (P40.0-7), IS 1K->GND, AN14=HSS0 AN15=HSS1\r\n");
    Hss_PrintDev(shell, 0);
    Hss_PrintDev(shell, 1);
    shellPrint(shell, "Expect 1K load @12V: Iout~12mA -> Vis~7.7mV(K0); DEN=0 => IS Hi-Z ~0V\r\n");
}

static void Hss_PrintUsage(Shell *shell)
{
    shellPrint(shell, "Usage:\r\n");
    shellPrint(shell, "  hss                       status (GPIO + AN14/15 + Iout est)\r\n");
    shellPrint(shell, "  hss on <dev> <ch>         IN high (OUT on), dev 0..1 ch 0..1\r\n");
    shellPrint(shell, "  hss off <dev> <ch>        IN low (OUT off)\r\n");
    shellPrint(shell, "  hss den <dev> <0|1>       diagnostic enable\r\n");
    shellPrint(shell, "  hss dsel <dev> <0|1>      IS mux (0=ch0 1=ch1)\r\n");
    shellPrint(shell, "  hss diag [dev [ch]]       IN on + DEN/DSEL select, 3ms, read IS\r\n");
    shellPrint(shell, "  hss offall                all IN/DEN low (safe)\r\n");
}

static int Hss_ParseDevCh(Shell *shell, const char *sDev, const char *sCh,
                          uint8 *dev, uint8 *ch)
{
    long d = strtol(sDev, 0, 0);
    long c = strtol(sCh, 0, 0);

    if (d < 0 || d > 1 || c < 0 || c > 1)
    {
        shellPrint(shell, "dev/ch out of range (dev 0..1, ch 0..1)\r\n");
        return 0;
    }
    *dev = (uint8)d;
    *ch  = (uint8)c;
    return 1;
}

static void Hss_DiagOne(Shell *shell, uint8 dev, uint8 ch)
{
    /* Normal-mode sense needs IN high + DEN high; wait out the
     * tDSENSE1H (DEN rise, <=100us) + tDSENSE2H (IN rise, <=250us). */
    Hss_SetIn(dev, ch, 1);
    Hss_SelectDiag(dev, ch);
    Hss_DelayMs(3);
    shellPrint(shell, "--- HSS%u CH%u (IN=1 DEN=1 DSEL=%u, +3ms) ---\r\n",
               (unsigned)dev, (unsigned)ch, (unsigned)ch);
    Hss_PrintSense(shell, dev);
}

static int cmd_hss(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();

    if (argc < 2)
    {
        Hss_PrintStatus(shell);
        return 0;
    }
    if (strcmp(argv[1], "on") == 0)
    {
        uint8 dev, ch;

        if (argc < 4 || !Hss_ParseDevCh(shell, argv[2], argv[3], &dev, &ch))
        {
            Hss_PrintUsage(shell);
            return -1;
        }
        Hss_SetIn(dev, ch, 1);
        shellPrint(shell, "HSS%u CH%u IN=1 (OUT on)\r\n", (unsigned)dev, (unsigned)ch);
        return 0;
    }
    if (strcmp(argv[1], "off") == 0)
    {
        uint8 dev, ch;

        if (argc < 4 || !Hss_ParseDevCh(shell, argv[2], argv[3], &dev, &ch))
        {
            Hss_PrintUsage(shell);
            return -1;
        }
        Hss_SetIn(dev, ch, 0);
        shellPrint(shell, "HSS%u CH%u IN=0 (OUT off)\r\n", (unsigned)dev, (unsigned)ch);
        return 0;
    }
    if (strcmp(argv[1], "den") == 0)
    {
        long d, v;

        if (argc < 4)
        {
            Hss_PrintUsage(shell);
            return -1;
        }
        d = strtol(argv[2], 0, 0);
        v = strtol(argv[3], 0, 0);
        if (d < 0 || d > 1 || (v != 0 && v != 1))
        {
            Hss_PrintUsage(shell);
            return -1;
        }
        Hss_SetDen((uint8)d, (uint8)v);
        shellPrint(shell, "HSS%ld DEN=%ld\r\n", d, v);
        return 0;
    }
    if (strcmp(argv[1], "dsel") == 0)
    {
        long d, v;

        if (argc < 4)
        {
            Hss_PrintUsage(shell);
            return -1;
        }
        d = strtol(argv[2], 0, 0);
        v = strtol(argv[3], 0, 0);
        if (d < 0 || d > 1 || (v != 0 && v != 1))
        {
            Hss_PrintUsage(shell);
            return -1;
        }
        Hss_SetDsel((uint8)d, (uint8)v);
        shellPrint(shell, "HSS%ld DSEL=%ld (mux->ch%ld)\r\n", d, v, v);
        return 0;
    }
    if (strcmp(argv[1], "diag") == 0)
    {
        if (argc >= 4)
        {
            uint8 dev, ch;

            if (!Hss_ParseDevCh(shell, argv[2], argv[3], &dev, &ch))
            {
                return -1;
            }
            Hss_DiagOne(shell, dev, ch);
            return 0;
        }
        if (argc >= 3)
        {
            long d = strtol(argv[2], 0, 0);

            if (d < 0 || d > 1)
            {
                Hss_PrintUsage(shell);
                return -1;
            }
            Hss_DiagOne(shell, (uint8)d, 0);
            Hss_DiagOne(shell, (uint8)d, 1);
            return 0;
        }
        {
            uint8 dev, ch;

            for (dev = 0; dev < 2; ++dev)
            {
                for (ch = 0; ch < 2; ++ch)
                {
                    Hss_DiagOne(shell, dev, ch);
                }
            }
            return 0;
        }
    }
    if (strcmp(argv[1], "offall") == 0)
    {
        Hss_AllOff();
        shellPrint(shell, "All HSS IN/DEN low (safe, IS Hi-Z)\r\n");
        return 0;
    }
    if (strcmp(argv[1], "status") == 0)
    {
        Hss_PrintStatus(shell);
        return 0;
    }
    Hss_PrintUsage(shell);
    return -1;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 hss, cmd_hss, HSS0/1 switch + sense);
