/* TC397 P40 GPIO driver for 2x WINSEMI WSTD6020AN high-side switches.
 *
 * SILICON LIMITATION (TC397 LFBGA292, confirmed 2026-09-16):
 * P40.0..P40.7 are INPUT-ONLY balls (datasheet pin type "I", buffer
 * "S/HighZ", no "O" row; user manual emergency-stop chapter: "P40.x ...
 * analog input ANx overlayed with GPI"). There is NO digital output driver.
 * IOCR accepts output mode and OMR/OUT latches set, but the ball never
 * moves (AN24=G3CH0 reads ~12mV with latch high or low; internal pull-up
 * works, push-pull high does not). Board pulldowns hold the nets at 0V.
 * => The HSS IN/DEN/DSEL nets CANNOT be driven from P40; board rework
 * (fly-wire from true GPO pins) is required to switch the HSS on.
 *
 * Until rework, this driver keeps P40.0..7 as INPUTS (board pulldown
 * defines the level) so `hss` status honestly reports net levels, and the
 * IS sense path (AN14/15, off-state ~0V) stays characterized. The set APIs
 * still poke the OMR latch (harmless, no ball effect); callers print the
 * input-only notice with the readback.
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

    /* Input-only balls: use no-pull inputs so the board pulldowns define
     * the level (measured ~0V). Do NOT use outputs (no driver exists). */
    for (dev = 0; dev < HSS_DEV_COUNT; ++dev)
    {
        for (ch = 0; ch < HSS_CH_COUNT; ++ch)
        {
            IfxPort_setPinMode(&MODULE_P40, s_InPin[dev][ch],
                               IfxPort_Mode_inputNoPullDevice);
        }
        IfxPort_setPinMode(&MODULE_P40, s_DenPin[dev],
                           IfxPort_Mode_inputNoPullDevice);
        IfxPort_setPinMode(&MODULE_P40, s_DselPin[dev],
                           IfxPort_Mode_inputNoPullDevice);
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
