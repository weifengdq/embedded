#ifndef __HSS_H
#define __HSS_H

#include "Ifx_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Board mapping: two WINSEMI WSTD6020AN (HSS0/HSS1), controlled via P40.x.
 * All 8 pins are 5V-domain GPIO outputs from TC397 (3V/5V CMOS compatible
 * inputs on the HSS side).
 *
 *   HSS0_IN0  P40.0 (shares pin with AN24, 5V domain)
 *   HSS0_DEN  P40.1 (shares pin with AN25)
 *   HSS0_DSEL P40.2 (shares pin with AN26)
 *   HSS0_IN1  P40.3 (shares pin with AN27)
 *   HSS1_IN0  P40.4 (shares pin with AN32)
 *   HSS1_DEN  P40.5 (shares pin with AN33)
 *   HSS1_DSEL P40.6 (shares pin with AN36)
 *   HSS1_IN1  P40.7 (shares pin with AN37)
 *
 * INx: output switch control (high = OUT on).
 * DEN: diagnostic enable, active high (high = IS pin active, low = IS Hi-Z).
 * DSEL: IS multiplexer address (low = ch0, high = ch1).
 * IS: sense current output, 1K to GND on board -> Vis = Iis * 1K.
 *     AN14 = HSS0 IS, AN15 = HSS1 IS (see adc table).
 */
#define HSS_DEV_COUNT   (2)
#define HSS_CH_COUNT    (2)

/* Sense resistor on board (IS -> 1K -> GND) */
#define HSS_RIS_OHM     (1000.0f)

/* Typical current-sense ratios Iout/Iis (Vden=5V), see datasheet */
#define HSS_K0          (1560.0f)   /* Iout = 50mA, +/-15% */
#define HSS_K1          (2500.0f)   /* Iout = 0.5A, +/-5% */
#define HSS_K2          (2630.0f)   /* Iout = 2A/4A/7A, +/-3% */

/* Safe power-on state: all outputs OFF, diagnostics OFF */
void Hss_Init(void);

/* IN control: dev 0..1, ch 0..1, on 0/1. Returns 0 on bad args, 1 on OK. */
int Hss_SetIn(uint8 dev, uint8 ch, uint8 on);
int Hss_GetIn(uint8 dev, uint8 ch);

/* DEN control: dev 0..1. Returns 0 on bad args, 1 on OK. */
int Hss_SetDen(uint8 dev, uint8 on);
int Hss_GetDen(uint8 dev);

/* DSEL control: dev 0..1, sel 0 = ch0, 1 = ch1. Returns 0/1 like above. */
int Hss_SetDsel(uint8 dev, uint8 sel);
int Hss_GetDsel(uint8 dev);

/* Convenience: enable IS mux for one channel (DEN=1, DSEL=ch). */
int Hss_SelectDiag(uint8 dev, uint8 ch);

/* All outputs off + diagnostics off (safe state). */
void Hss_AllOff(void);

/* Iout estimate from sense-node voltage: Iout = K * (Vis / Ris). */
static inline float Hss_IoutFromVis(float visVolt, float k)
{
    return k * (visVolt / HSS_RIS_OHM);
}

/* Pick a nominal K from a rough Iout estimate (nonlinear K, see table).
 * <0.1A -> K0, <1A -> K1, else K2. */
static inline float Hss_PickK(float ioutEst)
{
    if (ioutEst < 0.1f)
    {
        return HSS_K0;
    }
    if (ioutEst < 1.0f)
    {
        return HSS_K1;
    }
    return HSS_K2;
}

/* Millisecond busy-wait on the 1ms STM tick (defined in Cpu0_Main.c). */
void Hss_DelayMs(uint32 ms);

#ifdef __cplusplus
}
#endif

#endif /* __HSS_H */
