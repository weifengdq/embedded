#ifndef __ADC_H
#define __ADC_H

#include "Ifx_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* VREF (VDDM/VREF from TLF35584 VREF) = 5.0V, 12-bit EVADC */
#define ADC_VREF_VOLT   (5.0f)
#define ADC_MAX_CODE    (4096.0f)

/* AN signal kinds */
typedef enum
{
    AdcKind_Skip = 0,   /* P40.x GPIO reuse, not sampled -> print Reserved */
    AdcKind_Pin,        /* spare: print pin voltage only */
    AdcKind_Direct,     /* named direct-connect signal: print pin voltage */
    AdcKind_Div50_3,    /* 47K+3K divider: ext = pin * 50/3 */
    AdcKind_HwVer       /* HW_VERSION: VUC 10K+1K divider: vuc = pin * 11 */
} AdcKind;

/* One row of the AN0..AN47 table */
typedef struct
{
    uint8   an;         /* 0..47 */
    uint8   group;      /* EVADC group id (0,1,2,3,8), 0xFF if not sampled */
    uint8   channel;    /* channel id within group */
    AdcKind kind;
    const char *name;
} AdcAnInfo;

/* AN0..AN47 table (index = AN number) */
extern const AdcAnInfo g_AdcAnTable[48];

/* Init EVADC groups + queue scan. Called once from core0_main. */
void Adc_Init(void);

/* Sample one AN. Returns 1 on success (raw+volt valid), 0 if skipped/failed. */
int Adc_ReadAn(uint8 an, uint16 *raw, float *voltPin);

/* Convert raw code to pin voltage */
static inline float Adc_RawToVolt(uint16 raw)
{
    return ((float)raw * ADC_VREF_VOLT) / ADC_MAX_CODE;
}

/* External (pre-divider) voltage for 47K+3K channels */
static inline float Adc_Div50_3_ToExt(float vPin)
{
    return vPin * (50.0f / 3.0f);
}

/* VUC equivalent for HW_VERSION (10K+1K divider) */
static inline float Adc_HwVer_ToVuc(float vPin)
{
    return vPin * 11.0f;
}

#ifdef __cplusplus
}
#endif

#endif /* __ADC_H */
