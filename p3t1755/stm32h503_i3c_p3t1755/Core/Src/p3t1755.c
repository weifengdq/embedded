#include "p3t1755.h"

#include <string.h>

#define P3T1755_ENTDAA_PID_MASK      0xFFFFFFFFFFFFULL

static uint32_t P3T1755_BigToLittleEndian32(uint32_t value)
{
  return (((value & 0xFF000000UL) >> 24) |
          ((value & 0x00FF0000UL) >> 8)  |
          ((value & 0x0000FF00UL) << 8)  |
          ((value & 0x000000FFUL) << 24));
}

static HAL_StatusTypeDef P3T1755_SendDirectCccWrite(P3T1755_HandleTypeDef *handle,
                                                    uint8_t targetAddress,
                                                    uint8_t cccCode,
                                                    const uint8_t *buffer,
                                                    uint32_t size)
{
  I3C_CCCTypeDef ccc = {0};
  I3C_XferTypeDef xfer = {0};

  ccc.TargetAddr = targetAddress;
  ccc.CCC = cccCode;
  ccc.CCCBuf.pBuffer = (uint8_t *)buffer;
  ccc.CCCBuf.Size = size;
  ccc.Direction = LL_I3C_DIRECTION_WRITE;

  xfer.CtrlBuf.pBuffer = handle->controlBuffer;
  xfer.CtrlBuf.Size = 2U;
  xfer.TxBuf.pBuffer = (uint8_t *)buffer;
  xfer.TxBuf.Size = size;

  if (HAL_I3C_AddDescToFrame(handle->bus,
                             &ccc,
                             NULL,
                             &xfer,
                             1U,
                             I3C_DIRECT_WITHOUT_DEFBYTE_RESTART) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_I3C_Ctrl_TransmitCCC(handle->bus, &xfer, P3T1755_XFER_TIMEOUT_MS);
}

static HAL_StatusTypeDef P3T1755_SendDirectCccRead(P3T1755_HandleTypeDef *handle,
                                                   uint8_t targetAddress,
                                                   uint8_t cccCode,
                                                   uint8_t *buffer,
                                                   uint32_t size)
{
  I3C_CCCTypeDef ccc = {0};
  I3C_XferTypeDef xfer = {0};

  ccc.TargetAddr = targetAddress;
  ccc.CCC = cccCode;
  ccc.CCCBuf.pBuffer = NULL;
  ccc.CCCBuf.Size = 0U;
  ccc.Direction = LL_I3C_DIRECTION_READ;

  xfer.CtrlBuf.pBuffer = handle->controlBuffer;
  xfer.CtrlBuf.Size = 2U;
  xfer.RxBuf.pBuffer = buffer;
  xfer.RxBuf.Size = size;

  if (HAL_I3C_AddDescToFrame(handle->bus,
                             &ccc,
                             NULL,
                             &xfer,
                             1U,
                             I3C_DIRECT_WITHOUT_DEFBYTE_STOP) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_I3C_Ctrl_ReceiveCCC(handle->bus, &xfer, P3T1755_XFER_TIMEOUT_MS);
}

static void P3T1755_PopulateBcrInfo(P3T1755_HandleTypeDef *handle, uint8_t rawBcr)
{
  handle->rawBcr = rawBcr;
  handle->entdaaBcr = rawBcr;
  handle->entdaaInfo.BCR.MaxDataSpeedLimitation = ((rawBcr & 0x01U) != 0U) ? ENABLE : DISABLE;
  handle->entdaaInfo.BCR.IBIRequestCapable = ((rawBcr & 0x02U) != 0U) ? ENABLE : DISABLE;
  handle->entdaaInfo.BCR.IBIPayload = ((rawBcr & 0x04U) != 0U) ? ENABLE : DISABLE;
  handle->entdaaInfo.BCR.OfflineCapable = ((rawBcr & 0x08U) != 0U) ? ENABLE : DISABLE;
  handle->entdaaInfo.BCR.VirtualTargetSupport = ((rawBcr & 0x10U) != 0U) ? ENABLE : DISABLE;
  handle->entdaaInfo.BCR.AdvancedCapabilities = ((rawBcr & 0x20U) != 0U) ? ENABLE : DISABLE;
  handle->entdaaInfo.BCR.DeviceRole = ((rawBcr & 0x40U) != 0U) ? ENABLE : DISABLE;
}

static void P3T1755_PopulatePidInfo(P3T1755_HandleTypeDef *handle, const uint8_t pidBytes[6])
{
  uint64_t pid = 0U;
  uint32_t index;

  for (index = 0U; index < 6U; ++index)
  {
    pid = (pid << 8) | pidBytes[index];
  }

  handle->provisionedId = pid;
  handle->entdaaInfo.PID.MIPIMID = (uint16_t)((pid >> 33) & 0x7FFFU);
  handle->entdaaInfo.PID.IDTSEL = (uint8_t)((pid >> 32) & 0x01U);
  handle->entdaaInfo.PID.PartID = (uint16_t)((pid >> 16) & 0xFFFFU);
  handle->entdaaInfo.PID.MIPIID = (uint8_t)((pid >> 12) & 0x0FU);
}

static void P3T1755_ApplyEntdaaPayload(P3T1755_HandleTypeDef *handle, uint64_t payload)
{
  uint64_t pid = (payload & P3T1755_ENTDAA_PID_MASK);

  handle->rawBcr = (uint8_t)__HAL_I3C_GET_BCR(payload);
  handle->entdaaBcr = handle->rawBcr;
  handle->hasEntdaaInfo = (HAL_I3C_Get_ENTDAA_Payload_Info(handle->bus, payload, &handle->entdaaInfo) == HAL_OK) ? 1U : 0U;
  handle->rawDcr = (uint8_t)handle->entdaaInfo.DCR;

  pid = (uint64_t)((((uint64_t)P3T1755_BigToLittleEndian32((uint32_t)pid) << 32) |
                    (uint64_t)P3T1755_BigToLittleEndian32((uint32_t)(pid >> 32))) >> 16);

  handle->provisionedId = pid;
  handle->hasDeviceInfo = 1U;
}

static HAL_StatusTypeDef P3T1755_PrivateTransmit(P3T1755_HandleTypeDef *handle,
                                                 uint8_t targetAddress,
                                                 uint8_t *buffer,
                                                 uint32_t size)
{
  I3C_PrivateTypeDef descriptor = {0};
  I3C_XferTypeDef xfer = {0};

  descriptor.TargetAddr = targetAddress;
  descriptor.TxBuf.pBuffer = buffer;
  descriptor.TxBuf.Size = size;
  descriptor.Direction = HAL_I3C_DIRECTION_WRITE;

  xfer.CtrlBuf.pBuffer = handle->controlBuffer;
  xfer.CtrlBuf.Size = 1U;
  xfer.TxBuf.pBuffer = buffer;
  xfer.TxBuf.Size = size;

  if (HAL_I3C_AddDescToFrame(handle->bus,
                             NULL,
                             &descriptor,
                             &xfer,
                             1U,
                             I3C_PRIVATE_WITH_ARB_STOP) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_I3C_Ctrl_Transmit(handle->bus, &xfer, P3T1755_XFER_TIMEOUT_MS);
}

static HAL_StatusTypeDef P3T1755_SelectRegister(P3T1755_HandleTypeDef *handle, uint8_t reg)
{
  return P3T1755_PrivateTransmit(handle, handle->dynamicAddress, &reg, 1U);
}

static HAL_StatusTypeDef P3T1755_ReadRegister(P3T1755_HandleTypeDef *handle,
                                              uint8_t reg,
                                              uint8_t *buffer,
                                              uint32_t size)
{
  I3C_PrivateTypeDef descriptor = {0};
  I3C_XferTypeDef xfer = {0};
  HAL_StatusTypeDef status;

  status = P3T1755_SelectRegister(handle, reg);
  if (status != HAL_OK)
  {
    return status;
  }

  descriptor.TargetAddr = handle->dynamicAddress;
  descriptor.RxBuf.pBuffer = buffer;
  descriptor.RxBuf.Size = size;
  descriptor.Direction = HAL_I3C_DIRECTION_READ;

  xfer.CtrlBuf.pBuffer = handle->controlBuffer;
  xfer.CtrlBuf.Size = 1U;
  xfer.RxBuf.pBuffer = buffer;
  xfer.RxBuf.Size = size;

  if (HAL_I3C_AddDescToFrame(handle->bus,
                             NULL,
                             &descriptor,
                             &xfer,
                             1U,
                             I3C_PRIVATE_WITH_ARB_STOP) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_I3C_Ctrl_Receive(handle->bus, &xfer, P3T1755_XFER_TIMEOUT_MS);
}

static HAL_StatusTypeDef P3T1755_WriteRegister(P3T1755_HandleTypeDef *handle,
                                               uint8_t reg,
                                               const uint8_t *buffer,
                                               uint32_t size)
{
  uint8_t txBuffer[3] = {0};

  if (size > 2U)
  {
    return HAL_ERROR;
  }

  txBuffer[0] = reg;
  (void)memcpy(&txBuffer[1], buffer, size);

  return P3T1755_PrivateTransmit(handle, handle->dynamicAddress, txBuffer, size + 1U);
}

void P3T1755_Init(P3T1755_HandleTypeDef *handle,
                  I3C_HandleTypeDef *bus,
                  uint8_t staticAddress,
                  uint8_t dynamicAddress)
{
  (void)memset(handle, 0, sizeof(*handle));
  handle->bus = bus;
  handle->staticAddress = staticAddress;
  handle->dynamicAddress = dynamicAddress;
  handle->deviceIndex = 1U;
}

HAL_StatusTypeDef P3T1755_ProbeStaticAddress(I3C_HandleTypeDef *bus, uint8_t staticAddress)
{
  return HAL_I3C_Ctrl_IsDeviceI2C_Ready(bus,
                                        staticAddress,
                                        P3T1755_READY_TRIALS,
                                        P3T1755_XFER_TIMEOUT_MS);
}

HAL_StatusTypeDef P3T1755_AssignDynamicAddress(P3T1755_HandleTypeDef *handle)
{
  HAL_StatusTypeDef status;
  uint8_t setDasaData[1] = {(uint8_t)(handle->dynamicAddress << 1)};

  status = P3T1755_SendDirectCccWrite(handle,
                                      handle->staticAddress,
                                      P3T1755_CCC_SETDASA,
                                      setDasaData,
                                      sizeof(setDasaData));
  if (status != HAL_OK)
  {
    return status;
  }

  return HAL_I3C_Ctrl_IsDeviceI3C_Ready(handle->bus,
                                        handle->dynamicAddress,
                                        P3T1755_READY_TRIALS,
                                        P3T1755_XFER_TIMEOUT_MS);
}

HAL_StatusTypeDef P3T1755_ReadDeviceInfo(P3T1755_HandleTypeDef *handle)
{
  uint8_t pidBytes[6] = {0};
  uint8_t rawBcr = 0;
  uint8_t rawDcr = 0;
  HAL_StatusTypeDef status;

  status = P3T1755_SendDirectCccRead(handle,
                                     handle->dynamicAddress,
                                     P3T1755_CCC_GETPID,
                                     pidBytes,
                                     sizeof(pidBytes));
  if (status != HAL_OK)
  {
    return status;
  }

  status = P3T1755_SendDirectCccRead(handle,
                                     handle->dynamicAddress,
                                     P3T1755_CCC_GETBCR,
                                     &rawBcr,
                                     sizeof(rawBcr));
  if (status != HAL_OK)
  {
    return status;
  }

  status = P3T1755_SendDirectCccRead(handle,
                                     handle->dynamicAddress,
                                     P3T1755_CCC_GETDCR,
                                     &rawDcr,
                                     sizeof(rawDcr));
  if (status != HAL_OK)
  {
    return status;
  }

  P3T1755_PopulatePidInfo(handle, pidBytes);
  P3T1755_PopulateBcrInfo(handle, rawBcr);
  handle->rawDcr = rawDcr;
  handle->entdaaInfo.DCR = rawDcr;
  handle->hasEntdaaInfo = 1U;
  handle->hasDeviceInfo = 1U;

  return HAL_OK;
}

HAL_StatusTypeDef P3T1755_AssignDynamicAddressByEntdaa(P3T1755_HandleTypeDef *handle)
{
  HAL_StatusTypeDef status;
  uint64_t payload = 0U;
  uint32_t assigned = 0U;

  do
  {
    status = HAL_I3C_Ctrl_DynAddrAssign(handle->bus, &payload, I3C_ONLY_ENTDAA, P3T1755_XFER_TIMEOUT_MS);

    if (status == HAL_BUSY)
    {
      if (HAL_I3C_Ctrl_SetDynAddr(handle->bus, handle->dynamicAddress) != HAL_OK)
      {
        return HAL_ERROR;
      }

      P3T1755_ApplyEntdaaPayload(handle, payload);
      assigned++;
    }
  } while (status == HAL_BUSY);

  if ((status != HAL_OK) || (assigned != 1U))
  {
    return HAL_ERROR;
  }

  return HAL_I3C_Ctrl_IsDeviceI3C_Ready(handle->bus,
                                        handle->dynamicAddress,
                                        P3T1755_READY_TRIALS,
                                        P3T1755_XFER_TIMEOUT_MS);
}

HAL_StatusTypeDef P3T1755_EnsureReady(P3T1755_HandleTypeDef *handle)
{
  HAL_StatusTypeDef status;
  uint64_t payload = 0U;
  uint32_t assigned = 0U;

  status = HAL_I3C_Ctrl_IsDeviceI3C_Ready(handle->bus,
                                          handle->dynamicAddress,
                                          P3T1755_READY_TRIALS,
                                          P3T1755_XFER_TIMEOUT_MS);
  if (status == HAL_OK)
  {
    return HAL_OK;
  }

  do
  {
    status = HAL_I3C_Ctrl_DynAddrAssign(handle->bus, &payload, I3C_ONLY_ENTDAA, P3T1755_XFER_TIMEOUT_MS);

    if (status == HAL_BUSY)
    {
      if (HAL_I3C_Ctrl_SetDynAddr(handle->bus, handle->dynamicAddress) != HAL_OK)
      {
        return HAL_ERROR;
      }

      assigned++;
    }
  } while (status == HAL_BUSY);

  if ((status == HAL_OK) && (assigned > 0U))
  {
    P3T1755_ApplyEntdaaPayload(handle, payload);
  }
  else
  {
    status = P3T1755_AssignDynamicAddress(handle);
    if (status != HAL_OK)
    {
      return status;
    }

    return HAL_OK;
  }

  return HAL_I3C_Ctrl_IsDeviceI3C_Ready(handle->bus,
                                        handle->dynamicAddress,
                                        P3T1755_READY_TRIALS,
                                        P3T1755_XFER_TIMEOUT_MS);
}

HAL_StatusTypeDef P3T1755_ReadConfig(P3T1755_HandleTypeDef *handle, uint8_t *configValue)
{
  return P3T1755_ReadRegister(handle, P3T1755_REG_CONF, configValue, 1U);
}

HAL_StatusTypeDef P3T1755_WriteConfig(P3T1755_HandleTypeDef *handle, uint8_t configValue)
{
  return P3T1755_WriteRegister(handle, P3T1755_REG_CONF, &configValue, 1U);
}

HAL_StatusTypeDef P3T1755_SetInterruptMode(P3T1755_HandleTypeDef *handle)
{
  uint8_t configValue = 0;
  HAL_StatusTypeDef status;

  status = P3T1755_ReadConfig(handle, &configValue);
  if (status != HAL_OK)
  {
    return status;
  }

  configValue |= 0x02U;

  return P3T1755_WriteConfig(handle, configValue);
}

HAL_StatusTypeDef P3T1755_ReadTemperature(P3T1755_HandleTypeDef *handle,
                                          uint16_t *rawValue,
                                          int32_t *temperatureMilliC)
{
  uint8_t buffer[2] = {0};
  HAL_StatusTypeDef status;

  status = P3T1755_ReadRegister(handle, P3T1755_REG_TEMP, buffer, sizeof(buffer));
  if (status != HAL_OK)
  {
    return status;
  }

  if (rawValue != NULL)
  {
    *rawValue = (uint16_t)(((uint16_t)buffer[0] << 8) | buffer[1]);
  }

  if (temperatureMilliC != NULL)
  {
    *temperatureMilliC = P3T1755_RawToMilliC((uint16_t)(((uint16_t)buffer[0] << 8) | buffer[1]));
  }

  return HAL_OK;
}

HAL_StatusTypeDef P3T1755_ReadThresholds(P3T1755_HandleTypeDef *handle,
                                         int32_t *lowThresholdMilliC,
                                         int32_t *highThresholdMilliC)
{
  uint8_t buffer[2] = {0};
  HAL_StatusTypeDef status;

  status = P3T1755_ReadRegister(handle, P3T1755_REG_T_LOW, buffer, sizeof(buffer));
  if (status != HAL_OK)
  {
    return status;
  }

  if (lowThresholdMilliC != NULL)
  {
    *lowThresholdMilliC = P3T1755_RawToMilliC((uint16_t)(((uint16_t)buffer[0] << 8) | buffer[1]));
  }

  status = P3T1755_ReadRegister(handle, P3T1755_REG_T_HIGH, buffer, sizeof(buffer));
  if (status != HAL_OK)
  {
    return status;
  }

  if (highThresholdMilliC != NULL)
  {
    *highThresholdMilliC = P3T1755_RawToMilliC((uint16_t)(((uint16_t)buffer[0] << 8) | buffer[1]));
  }

  return HAL_OK;
}

HAL_StatusTypeDef P3T1755_WriteThresholds(P3T1755_HandleTypeDef *handle,
                                          int32_t lowThresholdMilliC,
                                          int32_t highThresholdMilliC)
{
  uint8_t buffer[2] = {0};
  uint16_t rawValue;
  HAL_StatusTypeDef status;

  rawValue = P3T1755_MilliCToRaw(lowThresholdMilliC);
  buffer[0] = (uint8_t)(rawValue >> 8);
  buffer[1] = (uint8_t)rawValue;
  status = P3T1755_WriteRegister(handle, P3T1755_REG_T_LOW, buffer, sizeof(buffer));
  if (status != HAL_OK)
  {
    return status;
  }

  rawValue = P3T1755_MilliCToRaw(highThresholdMilliC);
  buffer[0] = (uint8_t)(rawValue >> 8);
  buffer[1] = (uint8_t)rawValue;

  return P3T1755_WriteRegister(handle, P3T1755_REG_T_HIGH, buffer, sizeof(buffer));
}

HAL_StatusTypeDef P3T1755_EnableIbi(P3T1755_HandleTypeDef *handle)
{
  HAL_StatusTypeDef status;
  I3C_DeviceConfTypeDef deviceConf = {0};
  uint8_t eventMask = P3T1755_EVENT_SIR;

  deviceConf.DeviceIndex = handle->deviceIndex;
  deviceConf.TargetDynamicAddr = handle->dynamicAddress;
  deviceConf.IBIAck = handle->hasEntdaaInfo ? __HAL_I3C_GET_IBI_CAPABLE(handle->entdaaBcr) : ENABLE;
  deviceConf.IBIPayload = handle->hasEntdaaInfo ? __HAL_I3C_GET_IBI_PAYLOAD(handle->entdaaBcr) : DISABLE;
  deviceConf.CtrlRoleReqAck = handle->hasEntdaaInfo ? __HAL_I3C_GET_CR_CAPABLE(handle->entdaaBcr) : DISABLE;
  deviceConf.CtrlStopTransfer = DISABLE;

  status = HAL_I3C_Ctrl_ConfigBusDevices(handle->bus, &deviceConf, 1U);
  if (status != HAL_OK)
  {
    return status;
  }

  status = HAL_I3C_ActivateNotification(handle->bus, NULL, HAL_I3C_IT_IBIIE);
  if (status != HAL_OK)
  {
    return status;
  }

  return P3T1755_SendDirectCccWrite(handle,
                                    handle->dynamicAddress,
                                    P3T1755_CCC_ENEC,
                                    &eventMask,
                                    sizeof(eventMask));
}

int32_t P3T1755_RawToMilliC(uint16_t rawValue)
{
  int16_t temperatureCode = (int16_t)((rawValue >> 4) & 0x0FFFU);

  if ((temperatureCode & 0x0800) != 0)
  {
    temperatureCode = (int16_t)(temperatureCode | (int16_t)0xF000);
  }

  return ((int32_t)temperatureCode * 625) / 10;
}

uint16_t P3T1755_MilliCToRaw(int32_t temperatureMilliC)
{
  int32_t temperatureCode = temperatureMilliC * 16;

  if (temperatureCode >= 0)
  {
    temperatureCode = (temperatureCode + 500) / 1000;
  }
  else
  {
    temperatureCode = (temperatureCode - 500) / 1000;
  }

  if (temperatureCode > 2047)
  {
    temperatureCode = 2047;
  }
  else if (temperatureCode < -2048)
  {
    temperatureCode = -2048;
  }

  return (uint16_t)(((uint16_t)((int16_t)temperatureCode)) << 4);
}