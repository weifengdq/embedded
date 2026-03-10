#include "ClockReport.h"

#include <stdio.h>

#include "IfxClock.h"

#define CLOCKREPORT_MHZ_SCALE 1000000.0f

static float32 ClockReport_toMHz(uint32 frequencyHz)
{
    return ((float32)frequencyHz) / CLOCKREPORT_MHZ_SCALE;
}


static float32 ClockReport_getSysPllPreDivider(uint32 selector)
{
    static const float32 preDividers[4] = {1.0f, 2.0f, 1.2f, 1.6f};

    return preDividers[selector & 0x3U];
}


static const char *ClockReport_getPllInputName(void)
{
    switch (CLOCK_OSCCON.B.INSEL)
    {
    case IfxClock_PllInputClockSelection_back:
        return "EVR backup oscillator";
    case IfxClock_PllInputClockSelection_fOsc0:
        return "XTAL / fOSC0";
    case IfxClock_PllInputClockSelection_fSysclk:
        return "SYSCLK pin";
    default:
        return "unknown";
    }
}


static const char *ClockReport_getSystemSourceName(void)
{
    switch (CLOCK_CCUSTAT.B.CLKSELS)
    {
    case IfxClock_SysClockSourceSelect_back:
        return "EVR backup oscillator";
    case IfxClock_SysClockSourceSelect_pll:
        return "SYSPLL";
    case IfxClock_SysClockSourceSelect_ramp:
        return "RAMP oscillator";
    default:
        return "unknown";
    }
}


static const char *ClockReport_getPeripheralSourceName(void)
{
    if (CLOCK_CCUSTAT.B.CLKSELP == IfxClock_PerClockSourceSelect_pll)
    {
        return "Peripheral PLLs";
    }

    return "EVR backup oscillator";
}


static uint32 ClockReport_getQspiSourceFrequency(void)
{
    switch (CLOCK_PERCCUCON0.B.CLKSELQSPI)
    {
    case 1:
        return IfxClock_getPerSourceFrequency(IfxClock_Fsource_1);
    case 2:
        return IfxClock_getPerSourceFrequency(IfxClock_Fsource_2);
    default:
        return 0U;
    }
}


static const char *ClockReport_getQspiSourceName(void)
{
    switch (CLOCK_PERCCUCON0.B.CLKSELQSPI)
    {
    case 1:
        return "PERPLL1";
    case 2:
        return "PERPLL2";
    default:
        return "disabled";
    }
}


static uint32 ClockReport_getMcanSourceFrequency(void)
{
    switch (CLOCK_PERCCUCON0.B.CLKSELMCAN)
    {
    case 1:
        return IfxClock_getPerSourceFrequency(IfxClock_Fsource_1);
    case 2:
        return IfxClock_getOsc0Frequency();
    default:
        return 0U;
    }
}


static const char *ClockReport_getMcanSourceName(void)
{
    switch (CLOCK_PERCCUCON0.B.CLKSELMCAN)
    {
    case 1:
        return "PERPLL1";
    case 2:
        return "XTAL / fOSC0";
    default:
        return "disabled";
    }
}


static uint32 ClockReport_getAsclinSlowSourceFrequency(void)
{
    switch (CLOCK_PERCCUCON1.B.CLKSELASCLINS)
    {
    case 1:
        return IfxClock_getPerSourceFrequency(IfxClock_Fsource_1);
    case 2:
        return IfxClock_getOsc0Frequency();
    default:
        return 0U;
    }
}


static const char *ClockReport_getAsclinSlowSourceName(void)
{
    switch (CLOCK_PERCCUCON1.B.CLKSELASCLINS)
    {
    case 1:
        return "PERPLL1";
    case 2:
        return "XTAL / fOSC0";
    default:
        return "disabled";
    }
}


static uint32 ClockReport_getAsclinSlowFrequency(void)
{
    uint32 source = ClockReport_getAsclinSlowSourceFrequency();
    uint8  divider[16] = {1, 1, 2, 3, 4, 5, 6, 6, 8, 8, 10, 10, 12, 12, 12, 15};
    uint8  actualDiv = CLOCK_PERCCUCON1.B.ASCLINSDIV;

    if ((source == 0U) || (actualDiv == 0U))
    {
        return 0U;
    }

    return source / divider[actualDiv];
}


static void ClockReport_printSystemClockChain(void)
{
    uint32 xtalFrequency = IFX_CFG_CLOCK_XTAL_FREQUENCY;
    uint32 oscFrequency = IfxClock_getOscFrequency();
    uint32 pllFrequency = IfxClock_getPllFrequency();
    uint32 sysSourceFrequency = IfxClock_getSysSourceFrequency();
    uint32 cpuFrequency = IfxClock_getCpuFrequency();
    uint32 sriFrequency = IfxClock_getSriFrequency();
    uint32 spbFrequency = IfxClock_getSpbFrequency();
    uint32 fsiFrequency = IfxClock_getFsiFrequency();
    uint32 stmFrequency = IfxClock_getStmFrequency();
    uint32 sysPllVcoFrequency = (uint32)((((float32)oscFrequency) * (float32)(CLOCK_SYSPLLCON0.B.NDIV + 1U)) / (float32)(CLOCK_SYSPLLCON0.B.PDIV + 1U));
    float32 sysPllPreDivider = ClockReport_getSysPllPreDivider(CLOCK_SYSPLLCON1.B.K2PREDIV);

    printf("\r\n[System clock chain]\r\n");
    printf("XTAL (fOSC0)          : %.3f MHz\r\n", ClockReport_toMHz(xtalFrequency));
    printf("PLL input             : %s -> %.3f MHz\r\n", ClockReport_getPllInputName(), ClockReport_toMHz(oscFrequency));
    printf("SYSPLL VCO            : %.3f MHz (PDIV=%u, NDIV=%u)\r\n",
        ClockReport_toMHz(sysPllVcoFrequency),
        CLOCK_SYSPLLCON0.B.PDIV,
        CLOCK_SYSPLLCON0.B.NDIV);
    printf("SYSPLL output         : %.3f MHz (K2 prediv=%.1f, K2DIV=%u)\r\n",
        ClockReport_toMHz(pllFrequency),
        sysPllPreDivider,
        CLOCK_SYSPLLCON1.B.K2DIV);
    printf("System source         : %s -> %.3f MHz\r\n", ClockReport_getSystemSourceName(), ClockReport_toMHz(sysSourceFrequency));
    printf("Derived system clocks : CPU=%.3f MHz, SRI=%.3f MHz, SPB=%.3f MHz, FSI=%.3f MHz, STM=%.3f MHz\r\n",
        ClockReport_toMHz(cpuFrequency),
        ClockReport_toMHz(sriFrequency),
        ClockReport_toMHz(spbFrequency),
        ClockReport_toMHz(fsiFrequency),
        ClockReport_toMHz(stmFrequency));
}


static void ClockReport_printPeripheralClocks(void)
{
    uint32 perPll1Frequency = IfxClock_getPerPllFrequency1();
    uint32 perPll2Frequency = IfxClock_getPerPllFrequency2();
    uint32 perPll3Frequency = IfxClock_getPerPllFrequency3();
    uint32 qspiSourceFrequency = ClockReport_getQspiSourceFrequency();
    uint32 qspiFrequency = IfxClock_getQspiFrequency();
    uint32 i2cFrequency = IfxClock_getI2cFrequency();
    uint32 asclinFastFrequency = IfxClock_getAsclinFFrequency();
    uint32 asclinSlowSourceFrequency = ClockReport_getAsclinSlowSourceFrequency();
    uint32 asclinSlowFrequency = ClockReport_getAsclinSlowFrequency();
    uint32 mcanSourceFrequency = ClockReport_getMcanSourceFrequency();
    uint32 mcanFrequency = IfxClock_getMcanFrequency();
    uint32 mcanhFrequency = IfxClock_getMcanhFrequency();
    uint32 gethFrequency = IfxClock_getXGeth0Frequency();

    printf("[Peripheral root and kernel clocks]\r\n");
    printf("Peripheral source sel : %s\r\n", ClockReport_getPeripheralSourceName());
    printf("PERPLL1 / PERPLL2 / PERPLL3 : %.3f / %.3f / %.3f MHz\r\n",
        ClockReport_toMHz(perPll1Frequency),
        ClockReport_toMHz(perPll2Frequency),
        ClockReport_toMHz(perPll3Frequency));
    printf("SPI  (QSPI)           : %s %.3f MHz -> %.3f MHz\r\n",
        ClockReport_getQspiSourceName(),
        ClockReport_toMHz(qspiSourceFrequency),
        ClockReport_toMHz(qspiFrequency));
    printf("I2C                   : PERPLL2 %.3f MHz -> %.3f MHz\r\n",
        ClockReport_toMHz(perPll2Frequency),
        ClockReport_toMHz(i2cFrequency));
    printf("UART (ASCLINF/ASCLINS): PERPLL2 %.3f MHz -> %.3f MHz; %s %.3f MHz -> %.3f MHz\r\n",
        ClockReport_toMHz(perPll2Frequency),
        ClockReport_toMHz(asclinFastFrequency),
        ClockReport_getAsclinSlowSourceName(),
        ClockReport_toMHz(asclinSlowSourceFrequency),
        ClockReport_toMHz(asclinSlowFrequency));
    printf("CAN  (MCAN/MCANH)     : %s %.3f MHz -> %.3f MHz; SYS %.3f MHz -> %.3f MHz\r\n",
        ClockReport_getMcanSourceName(),
        ClockReport_toMHz(mcanSourceFrequency),
        ClockReport_toMHz(mcanFrequency),
        ClockReport_toMHz(IfxClock_getSysSourceFrequency()),
        ClockReport_toMHz(mcanhFrequency));
    printf("GETH                  : SYS %.3f MHz -> %.3f MHz\r\n",
        ClockReport_toMHz(IfxClock_getSysSourceFrequency()),
        ClockReport_toMHz(gethFrequency));
}


void ClockReport_printSnapshot(void)
{
    ClockReport_printSystemClockChain();
    ClockReport_printPeripheralClocks();
}