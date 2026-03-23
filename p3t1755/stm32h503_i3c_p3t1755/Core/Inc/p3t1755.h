#ifndef __P3T1755_H
#define __P3T1755_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define P3T1755_STATIC_ADDR_DEFAULT   0x4CU
#define P3T1755_STATIC_ADDR_MIN       0x48U
#define P3T1755_STATIC_ADDR_MAX       0x4FU
#define P3T1755_DYNAMIC_ADDR_DEFAULT  0x30U
#define P3T1755_MAX_TARGETS           8U

#define P3T1755_REG_TEMP              0x00U
#define P3T1755_REG_CONF              0x01U
#define P3T1755_REG_T_LOW             0x02U
#define P3T1755_REG_T_HIGH            0x03U

#define P3T1755_CCC_ENEC              0x80U
#define P3T1755_CCC_SETDASA           0x87U
#define P3T1755_CCC_GETPID            0x8DU
#define P3T1755_CCC_GETBCR            0x8EU
#define P3T1755_CCC_GETDCR            0x8FU

#define P3T1755_EVENT_SIR             0x01U

#define P3T1755_XFER_TIMEOUT_MS       1000U
#define P3T1755_READY_TRIALS          5U

typedef struct
{
  I3C_HandleTypeDef *bus;
  uint8_t staticAddress;
  uint8_t dynamicAddress;
  uint8_t deviceIndex;
  uint8_t rawBcr;
  uint8_t rawDcr;
  uint8_t entdaaBcr;
  uint8_t hasEntdaaInfo;
  uint8_t hasDeviceInfo;
  uint64_t provisionedId;
  uint32_t controlBuffer[4];
  I3C_ENTDAAPayloadTypeDef entdaaInfo;
} P3T1755_HandleTypeDef;

void P3T1755_Init(P3T1755_HandleTypeDef *handle,
                  I3C_HandleTypeDef *bus,
                  uint8_t staticAddress,
                  uint8_t dynamicAddress);
HAL_StatusTypeDef P3T1755_ProbeStaticAddress(I3C_HandleTypeDef *bus, uint8_t staticAddress);
HAL_StatusTypeDef P3T1755_AssignDynamicAddress(P3T1755_HandleTypeDef *handle);
HAL_StatusTypeDef P3T1755_AssignDynamicAddressByEntdaa(P3T1755_HandleTypeDef *handle);
HAL_StatusTypeDef P3T1755_ReadDeviceInfo(P3T1755_HandleTypeDef *handle);
HAL_StatusTypeDef P3T1755_EnsureReady(P3T1755_HandleTypeDef *handle);
HAL_StatusTypeDef P3T1755_ReadConfig(P3T1755_HandleTypeDef *handle, uint8_t *configValue);
HAL_StatusTypeDef P3T1755_WriteConfig(P3T1755_HandleTypeDef *handle, uint8_t configValue);
HAL_StatusTypeDef P3T1755_SetInterruptMode(P3T1755_HandleTypeDef *handle);
HAL_StatusTypeDef P3T1755_ReadTemperature(P3T1755_HandleTypeDef *handle,
                                          uint16_t *rawValue,
                                          int32_t *temperatureMilliC);
HAL_StatusTypeDef P3T1755_ReadThresholds(P3T1755_HandleTypeDef *handle,
                                         int32_t *lowThresholdMilliC,
                                         int32_t *highThresholdMilliC);
HAL_StatusTypeDef P3T1755_WriteThresholds(P3T1755_HandleTypeDef *handle,
                                          int32_t lowThresholdMilliC,
                                          int32_t highThresholdMilliC);
HAL_StatusTypeDef P3T1755_EnableIbi(P3T1755_HandleTypeDef *handle);
int32_t P3T1755_RawToMilliC(uint16_t rawValue);
uint16_t P3T1755_MilliCToRaw(int32_t temperatureMilliC);

#ifdef __cplusplus
}
#endif

#endif