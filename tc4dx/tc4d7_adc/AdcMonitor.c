#include "AdcMonitor.h"

#include "Adc/Tmadc/IfxAdc_Tmadc.h"
#include "IfxAdc_PinMap.h"
#include "Pms/Std/IfxPmsPm.h"

#define ADCMONITOR_POTI_REFERENCE_VOLTAGE  (3.3f)
#define ADCMONITOR_ADC_FULL_SCALE          (4095.0f)
#define ADCMONITOR_TEMP_WARMUP_SAMPLES     (2U)

static IfxAdc_Tmadc g_tmadc;
static IfxAdc_Tmadc_Ch g_potiChannel;
static IfxAdc_Tmadc_Config g_tmadcConfig;
static IfxAdc_Tmadc_ChConfig g_potiChannelConfig;
static IfxAdc_Tmadc_ChannelPinConfig g_potiPinConfig;
static uint32 g_tempWarmupCount;

static float32 AdcMonitor_kelvinToCelsius(float32 kelvin)
{
    return kelvin - 273.15f;
}

void AdcMonitor_init(void)
{
    IfxAdc_enableModule(&MODULE_ADC);

    IfxAdc_Tmadc_initModuleConfig(&g_tmadcConfig, &MODULE_ADC);
    g_tmadcConfig.id = IfxAdc_TmadcModule_0;
    g_tmadcConfig.calEnable = TRUE;
    IfxAdc_Tmadc_initModule(&g_tmadc, &g_tmadcConfig);

    IfxAdc_Tmadc_initChannelConfig(&g_potiChannelConfig, &MODULE_ADC);
    g_potiChannelConfig.id = IfxAdc_TmadcChannel_0;
    g_potiChannelConfig.moduleId = IfxAdc_TmadcModule_0;
    g_potiChannelConfig.samplingTimeNS = 100.0f;
    g_potiChannelConfig.mode = IfxAdc_TmadcOpMode_continuous;
    g_potiChannelConfig.core = IfxAdc_TmadcSarCore_0;
    g_potiChannelConfig.resultCfg.resultReg = IfxAdc_TmadcResultReg_0;
    g_potiChannelConfig.resultCfg.waitForRead = TRUE;

    g_potiPinConfig.tmadcInPin = &IfxAdc_Tmadc_TMADC0CH0_AN0_IN;
    g_potiPinConfig.tmadcPinMode = IfxPort_InputMode_noPullDevice;
    g_potiPinConfig.pinDriver = IfxPort_PadDriver_cmosAutomotiveSpeed1;
    g_potiChannelConfig.channelPin = &g_potiPinConfig;

    IfxAdc_Tmadc_initChannel(&g_potiChannel, &g_potiChannelConfig);
    IfxAdc_Tmadc_runModule(&g_tmadc);

    IfxPmsPm_dtsEnable(&MODULE_PMS, IfxPmsPm_Dts_enable);
    IfxPmsPm_dtsWarningEnable(&MODULE_PMS, IfxPmsPm_DtsWarning_disable);
    IfxPmsPm_dtsStartAdcConversion(&MODULE_PMS, IfxPmsPm_DtsAdcConversion_start);

    g_tempWarmupCount = 0U;
}

boolean AdcMonitor_sample(AdcMonitor_Reading *reading)
{
    float32 kelvin;

    if (reading == NULL_PTR)
    {
        return FALSE;
    }

    reading->potiRaw = IfxAdc_Tmadc_readChannelResult(&g_potiChannel);
    reading->potiRatio = ((float32)reading->potiRaw) / ADCMONITOR_ADC_FULL_SCALE;
    reading->potiVoltage = reading->potiRatio * ADCMONITOR_POTI_REFERENCE_VOLTAGE;

    reading->temperatureRaw = IfxPmsPm_getDtsTemperatureResult(&MODULE_PMS);
    reading->temperatureValid = FALSE;
    reading->temperatureCelsius = 0.0f;

    if (IfxPmsPm_getDtsStatus(&MODULE_PMS) != FALSE)
    {
        if (g_tempWarmupCount < ADCMONITOR_TEMP_WARMUP_SAMPLES)
        {
            g_tempWarmupCount++;
        }
        else
        {
            kelvin = IfxPmsPm_getDtsTemperatureResultKv(&MODULE_PMS);
            reading->temperatureCelsius = AdcMonitor_kelvinToCelsius(kelvin);
            reading->temperatureValid = TRUE;
        }
    }

    return TRUE;
}