#ifndef ADCMONITOR_H
#define ADCMONITOR_H

#include "Ifx_Types.h"

typedef struct
{
    uint16  potiRaw;
    float32 potiRatio;
    float32 potiVoltage;
    uint16  temperatureRaw;
    float32 temperatureCelsius;
    boolean temperatureValid;
} AdcMonitor_Reading;

void AdcMonitor_init(void);
boolean AdcMonitor_sample(AdcMonitor_Reading *reading);

#endif