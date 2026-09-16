/* TC397 P40 GPIO driver for 2x WINSEMI WSTD6020AN high-side switches.
 *
 * All 8 control pins are push-pull outputs, power-on safe state = all low
 * (OUT off, DEN off -> IS Hi-Z). HSS inputs are 3V/5V CMOS compatible with
 * hysteresis; P40 is a 5V-domain port, high ~= 5V is within the 6.0V max.
 */
#include "hss.h"
#include "IfxPort.h"

/* P40 pin indices for [dev][signal]. IN has 2 channels per dev. */
static const uint8 s_InPin[2][2]  = {{0, 3}, {4, 7}};
static const uint8 s_DenPin[2]    = {1, 5};
static const uint8 s_DselPin[2]   = {2, 6};

/* 1ms tick from Cpu0_Main.c */
extern volatile uint32 g_TickCount_1ms;

static int Hss_ValidDevCh(uint8 dev, uint8 ch)
{
    return (dev < HSS_DEV_COUNT) && (ch < HSS_CH_COUNT);
}

void Hss_Init(void)
{
    uint8 dev, ch;

    for (dev = 0; dev < HSS_DEV_COUNT; ++dev)
    {
        for (ch = 0; ch < HSS_CH_COUNT; ++ch)
        {
            IfxPort_setPinMode(&MODULE_P40, s_InPin[dev][ch],
                               IfxPort_Mode_outputPushPullGeneral);
            IfxPort_setPinLow(&MODULE_P40, s_InPin[dev][ch]);
        }
        IfxPort_setPinMode(&MODULE_P40, s_DenPin[dev],
                           IfxPort_Mode_outputPushPullGeneral);
        IfxPort_setPinLow(&MODULE_P40, s_DenPin[dev]);
        IfxPort_setPinMode(&MODULE_P40, s_DselPin[dev],
                           IfxPort_Mode_outputPushPullGeneral);
        IfxPort_setPinLow(&MODULE_P40, s_DselPin[dev]);
    }
}

int Hss_SetIn(uint8 dev, uint8 ch, uint8 on)
{
    if (!Hss_ValidDevCh(dev, ch))
    {
        return 0;
    }
    if (on)
    {
        IfxPort_setPinHigh(&MODULE_P40, s_InPin[dev][ch]);
    }
    else
    {
        IfxPort_setPinLow(&MODULE_P40, s_InPin[dev][ch]);
    }
    return 1;
}

int Hss_GetIn(uint8 dev, uint8 ch)
{
    if (!Hss_ValidDevCh(dev, ch))
    {
        return -1;
    }
    return IfxPort_getPinState(&MODULE_P40, s_InPin[dev][ch]) ? 1 : 0;
}

int Hss_SetDen(uint8 dev, uint8 on)
{
    if (dev >= HSS_DEV_COUNT)
    {
        return 0;
    }
    if (on)
    {
        IfxPort_setPinHigh(&MODULE_P40, s_DenPin[dev]);
    }
    else
    {
        IfxPort_setPinLow(&MODULE_P40, s_DenPin[dev]);
    }
    return 1;
}

int Hss_GetDen(uint8 dev)
{
    if (dev >= HSS_DEV_COUNT)
    {
        return -1;
    }
    return IfxPort_getPinState(&MODULE_P40, s_DenPin[dev]) ? 1 : 0;
}

int Hss_SetDsel(uint8 dev, uint8 sel)
{
    if (dev >= HSS_DEV_COUNT)
    {
        return 0;
    }
    if (sel)
    {
        IfxPort_setPinHigh(&MODULE_P40, s_DselPin[dev]);
    }
    else
    {
        IfxPort_setPinLow(&MODULE_P40, s_DselPin[dev]);
    }
    return 1;
}

int Hss_GetDsel(uint8 dev)
{
    if (dev >= HSS_DEV_COUNT)
    {
        return -1;
    }
    return IfxPort_getPinState(&MODULE_P40, s_DselPin[dev]) ? 1 : 0;
}

int Hss_SelectDiag(uint8 dev, uint8 ch)
{
    if (!Hss_ValidDevCh(dev, ch))
    {
        return 0;
    }
    Hss_SetDen(dev, 1);
    Hss_SetDsel(dev, ch);
    return 1;
}

void Hss_AllOff(void)
{
    uint8 dev, ch;

    for (dev = 0; dev < HSS_DEV_COUNT; ++dev)
    {
        for (ch = 0; ch < HSS_CH_COUNT; ++ch)
        {
            Hss_SetIn(dev, ch, 0);
        }
        Hss_SetDen(dev, 0);
        Hss_SetDsel(dev, 0);
    }
}

void Hss_DelayMs(uint32 ms)
{
    uint32 start = g_TickCount_1ms;

    while ((g_TickCount_1ms - start) < ms)
    {
    }
}
