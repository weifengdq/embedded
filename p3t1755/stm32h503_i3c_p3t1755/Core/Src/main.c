/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "p3t1755.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define P3T1755_POWER_UP_DELAY_MS     100U
#define P3T1755_POLL_PERIOD_MS        1000U
#define P3T1755_IBI_LOW_OFFSET_MILLIC 1000
#define P3T1755_IBI_HIGH_OFFSET_MILLIC 500
#define P3T1755_DYNAMIC_ADDR_BASE     0x30U
#define P3T1755_CCC_DISEC             0x01U
#define P3T1755_CCC_ENTDAA            0x07U
#define P3T1755_DISEC_INT_EVENT_MASK  0x08U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

I3C_HandleTypeDef hi3c1;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static P3T1755_HandleTypeDef g_p3t1755Targets[P3T1755_MAX_TARGETS];
static uint8_t g_p3t1755TargetCount;
static volatile uint32_t g_i3cEvents;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_I3C1_Init(void);
static void MX_ICACHE_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void APP_PrintBanner(void);
static void APP_LogI3CFailure(const char *operation, HAL_StatusTypeDef status);
static HAL_StatusTypeDef APP_I3C_ApplyBusTiming(uint8_t sclPpLow,
                                                uint8_t sclI3cHigh,
                                                uint8_t sclOdLow,
                                                uint8_t sclI2cHigh,
                                                uint8_t busFree,
                                                uint8_t busIdle);
static HAL_StatusTypeDef APP_I3C_ResetDynamicAddresses(void);
static void APP_PrintMilliC(const char *label, int32_t temperatureMilliC);
static const char *APP_StatusToText(HAL_StatusTypeDef status);
static const char *APP_CccToText(uint8_t cccCode);
static void APP_LogCccCommand(const char *phase,
                              const char *direction,
                              uint8_t targetAddress,
                              uint8_t cccCode,
                              const uint8_t *payload,
                              uint32_t size);
static HAL_StatusTypeDef APP_DiscoverTargets(void);
static HAL_StatusTypeDef APP_AssociateIdentityViaEntdaa(void);
static void APP_PrintTargetTable(void);
static void APP_PrintTargetIdentity(const P3T1755_HandleTypeDef *target);
static HAL_StatusTypeDef APP_VerifyTargetsAtCurrentTiming(void);
static HAL_StatusTypeDef APP_ReadAndPrintTemperatures(void);
static void APP_LogThresholds(const char *label, int32_t lowThresholdMilliC, int32_t highThresholdMilliC);
static HAL_StatusTypeDef APP_SetupThresholdsAndIbi(P3T1755_HandleTypeDef *target, int32_t currentTempMilliC);
static HAL_StatusTypeDef APP_SetupMultiTargetIbi(void);
static P3T1755_HandleTypeDef *APP_FindTargetByDynamicAddress(uint8_t dynamicAddress);
static void APP_HandleIbiEvent(void);
int __io_putchar(int ch);
int __io_getchar(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int __io_putchar(int ch)
{
  uint8_t data = (uint8_t)ch;

  (void)HAL_UART_Transmit(&huart1, &data, 1U, HAL_MAX_DELAY);

  return ch;
}

int __io_getchar(void)
{
  uint8_t data = 0;

  (void)HAL_UART_Receive(&huart1, &data, 1U, HAL_MAX_DELAY);

  return (int)data;
}

static void APP_PrintBanner(void)
{
  printf("\r\n=== stm32h503_i3c_p3t1755 ===\r\n");
  printf("  board        : STM32H503 / I3C1 / USART1\r\n");
  printf("  i3c pins     : SDA=PC9 SCL=PC8\r\n");
  printf("  uart pins    : TX=PA2 RX=PA1\r\n");
  printf("  static scan  : 0x%02X..0x%02X\r\n",
         P3T1755_STATIC_ADDR_MIN,
    P3T1755_STATIC_ADDR_MAX);
  printf("  dynamic base : 0x%02X\r\n",
    P3T1755_DYNAMIC_ADDR_BASE);
}

static void APP_LogI3CFailure(const char *operation, HAL_StatusTypeDef status)
{
  printf("%s failed: status=%d state=0x%02lX error=0x%08lX\r\n",
         operation,
         (int)status,
         (unsigned long)HAL_I3C_GetState(&hi3c1),
         (unsigned long)hi3c1.ErrorCode);
}

static HAL_StatusTypeDef APP_I3C_ApplyBusTiming(uint8_t sclPpLow,
                                                uint8_t sclI3cHigh,
                                                uint8_t sclOdLow,
                                                uint8_t sclI2cHigh,
                                                uint8_t busFree,
                                                uint8_t busIdle)
{
  LL_I3C_CtrlBusConfTypeDef ctrlBusConf = {0};

  ctrlBusConf.SDAHoldTime = HAL_I3C_SDA_HOLD_TIME_1_5;
  ctrlBusConf.WaitTime = HAL_I3C_OWN_ACTIVITY_STATE_0;
  ctrlBusConf.SCLPPLowDuration = sclPpLow;
  ctrlBusConf.SCLI3CHighDuration = sclI3cHigh;
  ctrlBusConf.SCLODLowDuration = sclOdLow;
  ctrlBusConf.SCLI2CHighDuration = sclI2cHigh;
  ctrlBusConf.BusFreeDuration = busFree;
  ctrlBusConf.BusIdleDuration = busIdle;

  return HAL_I3C_Ctrl_BusCharacteristicConfig(&hi3c1, &ctrlBusConf);
}

static HAL_StatusTypeDef APP_I3C_ResetDynamicAddresses(void)
{
  static const uint8_t disableEventsMask[1] = {P3T1755_DISEC_INT_EVENT_MASK};
  I3C_CCCTypeDef ccc[2] = {0};
  I3C_XferTypeDef xfer = {0};
  uint32_t controlBuffer[4] = {0};

  APP_LogCccCommand("init", "TX", 0x7EU, P3T1755_CCC_DISEC, disableEventsMask, sizeof(disableEventsMask));
  APP_LogCccCommand("init", "TX", 0x7EU, Broadcast_RSTDAA, NULL, 0U);

  ccc[0].TargetAddr = 0U;
  ccc[0].CCC = P3T1755_CCC_DISEC;
  ccc[0].Direction = LL_I3C_DIRECTION_WRITE;
  ccc[0].CCCBuf.pBuffer = (uint8_t *)disableEventsMask;
  ccc[0].CCCBuf.Size = sizeof(disableEventsMask);

  ccc[1].TargetAddr = 0U;
  ccc[1].CCC = Broadcast_RSTDAA;
  ccc[1].Direction = LL_I3C_DIRECTION_WRITE;
  ccc[1].CCCBuf.pBuffer = NULL;
  ccc[1].CCCBuf.Size = 0U;

  xfer.CtrlBuf.pBuffer = controlBuffer;
  xfer.CtrlBuf.Size = 4U;
  xfer.TxBuf.pBuffer = (uint8_t *)disableEventsMask;
  xfer.TxBuf.Size = sizeof(disableEventsMask);

  if (HAL_I3C_AddDescToFrame(&hi3c1,
                             ccc,
                             NULL,
                             &xfer,
                             2U,
                             I3C_BROADCAST_WITHOUT_DEFBYTE_RESTART) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_I3C_Ctrl_TransmitCCC(&hi3c1, &xfer, P3T1755_XFER_TIMEOUT_MS);
}

static void APP_PrintMilliC(const char *label, int32_t temperatureMilliC)
{
  int32_t absMilliC = (temperatureMilliC < 0) ? -temperatureMilliC : temperatureMilliC;

  printf("%s%s%ld.%03ld C",
         label,
         (temperatureMilliC < 0) ? "-" : "",
         (long)(absMilliC / 1000),
         (long)(absMilliC % 1000));
}

static void APP_PrintProvisionedId(uint64_t provisionedId)
{
  unsigned long pidHigh = (unsigned long)((provisionedId >> 32) & 0xFFFFUL);
  unsigned long pidLow = (unsigned long)(provisionedId & 0xFFFFFFFFUL);

  printf("0x%04lX%08lX", pidHigh, pidLow);
}

static const char *APP_StatusToText(HAL_StatusTypeDef status)
{
  switch (status)
  {
    case HAL_OK:
      return "OK";
    case HAL_ERROR:
      return "ERR";
    case HAL_BUSY:
      return "BUSY";
    case HAL_TIMEOUT:
      return "TIMEOUT";
    default:
      return "UNKNOWN";
  }
}

static const char *APP_CccToText(uint8_t cccCode)
{
  switch (cccCode)
  {
    case P3T1755_CCC_DISEC:
      return "DISEC";
    case Broadcast_RSTDAA:
      return "RSTDAA";
    case P3T1755_CCC_SETDASA:
      return "SETDASA";
    case P3T1755_CCC_ENEC:
      return "ENEC";
    case P3T1755_CCC_ENTDAA:
      return "ENTDAA";
    case P3T1755_CCC_GETPID:
      return "GETPID";
    case P3T1755_CCC_GETBCR:
      return "GETBCR";
    case P3T1755_CCC_GETDCR:
      return "GETDCR";
    default:
      return "CCC";
  }
}

static void APP_LogCccCommand(const char *phase,
                              const char *direction,
                              uint8_t targetAddress,
                              uint8_t cccCode,
                              const uint8_t *payload,
                              uint32_t size)
{
  uint32_t index;

    printf("  [%s] CCC %-2s %-6s target=0x%02X",
      phase,
         direction,
         APP_CccToText(cccCode),
         targetAddress);

  if ((payload != NULL) && (size > 0U))
  {
    printf(" data=");
    for (index = 0U; index < size; ++index)
    {
      printf("%02X", payload[index]);
      if ((index + 1U) < size)
      {
        printf(" ");
      }
    }
  }

  printf("\r\n");
}

static HAL_StatusTypeDef APP_DiscoverTargets(void)
{
  uint8_t staticAddress;
  uint8_t dynamicAddress = P3T1755_DYNAMIC_ADDR_BASE;

  g_p3t1755TargetCount = 0U;

  printf("\r\n[init] Static Address Discovery\r\n");

  for (staticAddress = P3T1755_STATIC_ADDR_MIN;
       staticAddress <= P3T1755_STATIC_ADDR_MAX;
       ++staticAddress)
  {
    HAL_StatusTypeDef status;
    P3T1755_HandleTypeDef *target = &g_p3t1755Targets[g_p3t1755TargetCount];
    uint8_t setDasaData[1] = {(uint8_t)(dynamicAddress << 1)};

    P3T1755_Init(target, &hi3c1, staticAddress, dynamicAddress);
    target->deviceIndex = (uint8_t)(g_p3t1755TargetCount + 1U);

    APP_LogCccCommand("init", "TX", staticAddress, P3T1755_CCC_SETDASA, setDasaData, sizeof(setDasaData));

    status = P3T1755_AssignDynamicAddress(target);
    if (status == HAL_OK)
    {
          printf("  target[%u]\r\n",
            (unsigned int)(g_p3t1755TargetCount + 1U));
          printf("    static      : 0x%02X\r\n", staticAddress);
          printf("    dynamic     : 0x%02X\r\n", dynamicAddress);
          printf("    method      : SETDASA\r\n");

      ++g_p3t1755TargetCount;
      ++dynamicAddress;

      if (g_p3t1755TargetCount >= P3T1755_MAX_TARGETS)
      {
        break;
      }
    }
    else if (hi3c1.ErrorCode == HAL_I3C_ERROR_ADDRESS_NACK)
    {
      hi3c1.ErrorCode = HAL_I3C_ERROR_NONE;
    }
    else
    {
      printf("Assign dyn addr failed for static=0x%02X dyn=0x%02X error=0x%08lX\r\n",
             staticAddress,
             dynamicAddress,
             (unsigned long)hi3c1.ErrorCode);
      return status;
    }
  }

  return (g_p3t1755TargetCount > 0U) ? HAL_OK : HAL_ERROR;
}

static HAL_StatusTypeDef APP_AssociateIdentityViaEntdaa(void)
{
  uint8_t identityIndex;

  printf("\r\n[init] ENTDAA Identity Correlation\r\n");

  for (identityIndex = 0U; identityIndex < g_p3t1755TargetCount; ++identityIndex)
  {
    HAL_StatusTypeDef status;
    uint8_t parkedIndex;
    P3T1755_HandleTypeDef *target = &g_p3t1755Targets[identityIndex];

    status = APP_I3C_ResetDynamicAddresses();
    if (status != HAL_OK)
    {
      return status;
    }

        printf("  target[%u]\r\n",
          (unsigned int)target->deviceIndex);
        printf("    static      : 0x%02X\r\n",
          target->staticAddress);
        printf("    strategy    : isolate peers then ENTDAA\r\n");

    for (parkedIndex = 0U; parkedIndex < g_p3t1755TargetCount; ++parkedIndex)
    {
      P3T1755_HandleTypeDef *parkedTarget = &g_p3t1755Targets[parkedIndex];
      uint8_t setDasaData[1];

      if (parkedIndex == identityIndex)
      {
        continue;
      }

      setDasaData[0] = (uint8_t)(parkedTarget->dynamicAddress << 1);
      APP_LogCccCommand("init", "TX", parkedTarget->staticAddress, P3T1755_CCC_SETDASA, setDasaData, sizeof(setDasaData));

      status = P3T1755_AssignDynamicAddress(parkedTarget);
      if (status != HAL_OK)
      {
        printf("[init] park target failed static=0x%02X dyn=0x%02X error=0x%08lX\r\n",
               parkedTarget->staticAddress,
               parkedTarget->dynamicAddress,
               (unsigned long)hi3c1.ErrorCode);
        return status;
      }

      printf("    parked peer : static=0x%02X dynamic=0x%02X\r\n",
             parkedTarget->staticAddress,
             parkedTarget->dynamicAddress);
    }

    APP_LogCccCommand("init", "TX", 0x7EU, P3T1755_CCC_ENTDAA, NULL, 0U);

    status = P3T1755_AssignDynamicAddressByEntdaa(target);
    if (status != HAL_OK)
    {
      printf("[init] ENTDAA identity capture failed static=0x%02X dyn=0x%02X error=0x%08lX\r\n",
             target->staticAddress,
             target->dynamicAddress,
             (unsigned long)hi3c1.ErrorCode);
      return status;
    }

                printf("    dynamic     : 0x%02X\r\n",
                  target->dynamicAddress);
                printf("    pid         : ");
                APP_PrintProvisionedId(target->provisionedId);
                printf("\r\n");
                printf("    bcr         : 0x%02X\r\n",
                  target->rawBcr);
                printf("    dcr         : 0x%02X\r\n",
                  target->rawDcr);
  }

  return HAL_OK;
}

static void APP_PrintTargetTable(void)
{
  uint8_t index;

          printf("\r\n[init] Target Summary\r\n");
          printf("  count        : %u\r\n",
            (unsigned int)g_p3t1755TargetCount);

  for (index = 0U; index < g_p3t1755TargetCount; ++index)
  {
            printf("  target[%u]\r\n",
              (unsigned int)(index + 1U));
    APP_PrintTargetIdentity(&g_p3t1755Targets[index]);
  }
}

static void APP_PrintTargetIdentity(const P3T1755_HandleTypeDef *target)
{
  if (target->hasDeviceInfo == 0U)
  {
    printf("    static      : 0x%02X\r\n",
      target->staticAddress);
    printf("    dynamic     : 0x%02X\r\n",
      target->dynamicAddress);
    printf("    identity    : unavailable via ENTDAA correlation\r\n");
    return;
  }

  printf("    static      : 0x%02X\r\n",
    target->staticAddress);
  printf("    dynamic     : 0x%02X\r\n",
    target->dynamicAddress);
  printf("    pid         : ");
  APP_PrintProvisionedId(target->provisionedId);
  printf("\r\n");
  printf("    pid.mfg     : 0x%04lX\r\n",
    (unsigned long)target->entdaaInfo.PID.MIPIMID);
  printf("    pid.idsel   : %lu\r\n",
    (unsigned long)target->entdaaInfo.PID.IDTSEL);
  printf("    pid.part    : 0x%04lX\r\n",
    (unsigned long)target->entdaaInfo.PID.PartID);
  printf("    pid.inst    : 0x%01lX\r\n",
    (unsigned long)target->entdaaInfo.PID.MIPIID);
  printf("    bcr         : 0x%02X\r\n",
    target->rawBcr);
  printf("    bcr.ibi     : %u\r\n",
    (unsigned int)target->entdaaInfo.BCR.IBIRequestCapable);
  printf("    bcr.payload : %u\r\n",
    (unsigned int)target->entdaaInfo.BCR.IBIPayload);
  printf("    bcr.roleReq : %u\r\n",
    (unsigned int)target->entdaaInfo.BCR.DeviceRole);
  printf("    bcr.offline : %u\r\n",
    (unsigned int)target->entdaaInfo.BCR.OfflineCapable);
  printf("    bcr.virtual : %u\r\n",
    (unsigned int)target->entdaaInfo.BCR.VirtualTargetSupport);
  printf("    bcr.adv     : %u\r\n",
    (unsigned int)target->entdaaInfo.BCR.AdvancedCapabilities);
  printf("    bcr.speed   : %u\r\n",
    (unsigned int)target->entdaaInfo.BCR.MaxDataSpeedLimitation);
  printf("    dcr         : 0x%02X\r\n",
    target->rawDcr);
}

static HAL_StatusTypeDef APP_VerifyTargetsAtCurrentTiming(void)
{
  uint8_t index;

  for (index = 0U; index < g_p3t1755TargetCount; ++index)
  {
    HAL_StatusTypeDef status = HAL_I3C_Ctrl_IsDeviceI3C_Ready(&hi3c1,
                                                              g_p3t1755Targets[index].dynamicAddress,
                                                              P3T1755_READY_TRIALS,
                                                              P3T1755_XFER_TIMEOUT_MS);
    if (status != HAL_OK)
    {
      printf("Target ready check failed: static=0x%02X dyn=0x%02X\r\n",
             g_p3t1755Targets[index].staticAddress,
             g_p3t1755Targets[index].dynamicAddress);
      return status;
    }
  }

  return HAL_OK;
}

static HAL_StatusTypeDef APP_ReadAndPrintTemperatures(void)
{
  HAL_StatusTypeDef overallStatus = HAL_OK;
  uint8_t index;

  for (index = 0U; index < g_p3t1755TargetCount; ++index)
  {
    HAL_StatusTypeDef status;
    uint16_t tempRaw = 0;
    uint8_t confRaw = 0;
    int32_t tempMilliC = 0;

    status = P3T1755_ReadTemperature(&g_p3t1755Targets[index], &tempRaw, &tempMilliC);
    if (status != HAL_OK)
    {
      overallStatus = status;
      printf("T%u[0x%02X->0x%02X]=%s(error=0x%08lX)",
             (unsigned int)(index + 1U),
             g_p3t1755Targets[index].staticAddress,
             g_p3t1755Targets[index].dynamicAddress,
             APP_StatusToText(status),
             (unsigned long)hi3c1.ErrorCode);
      if ((index + 1U) < g_p3t1755TargetCount)
      {
        printf(" ");
      }
      continue;
    }

    status = P3T1755_ReadConfig(&g_p3t1755Targets[index], &confRaw);
    if (status != HAL_OK)
    {
      overallStatus = status;
      printf("T%u[0x%02X->0x%02X]=CONF_%s(error=0x%08lX)",
             (unsigned int)(index + 1U),
             g_p3t1755Targets[index].staticAddress,
             g_p3t1755Targets[index].dynamicAddress,
             APP_StatusToText(status),
             (unsigned long)hi3c1.ErrorCode);
      if ((index + 1U) < g_p3t1755TargetCount)
      {
        printf(" ");
      }
      continue;
    }

    printf("T%u[0x%02X->0x%02X]=%02X%02X/conf=0x%02X/",
           (unsigned int)(index + 1U),
           g_p3t1755Targets[index].staticAddress,
           g_p3t1755Targets[index].dynamicAddress,
           (unsigned int)(tempRaw >> 8),
           (unsigned int)(tempRaw & 0xFFU),
           confRaw);
    APP_PrintMilliC("", tempMilliC);
    if ((index + 1U) < g_p3t1755TargetCount)
    {
      printf(" ");
    }
  }

  printf("\r\n");
  return overallStatus;
}

static void APP_LogThresholds(const char *label, int32_t lowThresholdMilliC, int32_t highThresholdMilliC)
{
  printf("%s", label);
  APP_PrintMilliC("T_LOW=", lowThresholdMilliC);
  printf(" ");
  APP_PrintMilliC("T_HIGH=", highThresholdMilliC);
  printf("\r\n");
}

static HAL_StatusTypeDef APP_SetupThresholdsAndIbi(P3T1755_HandleTypeDef *target, int32_t currentTempMilliC)
{
  int32_t lowThresholdMilliC = currentTempMilliC + P3T1755_IBI_LOW_OFFSET_MILLIC;
  int32_t highThresholdMilliC = currentTempMilliC + P3T1755_IBI_HIGH_OFFSET_MILLIC + P3T1755_IBI_LOW_OFFSET_MILLIC;
  int32_t readbackLowMilliC = 0;
  int32_t readbackHighMilliC = 0;
  uint8_t configValue = 0;
  HAL_StatusTypeDef status;

  status = P3T1755_SetInterruptMode(target);
  if (status != HAL_OK)
  {
    return status;
  }

  status = P3T1755_ReadConfig(target, &configValue);
  if (status != HAL_OK)
  {
    return status;
  }

  printf("Config updated for interrupt mode: 0x%02X\r\n", configValue);

  status = P3T1755_WriteThresholds(target, lowThresholdMilliC, highThresholdMilliC);
  if (status != HAL_OK)
  {
    return status;
  }

  status = P3T1755_ReadThresholds(target, &readbackLowMilliC, &readbackHighMilliC);
  if (status != HAL_OK)
  {
    return status;
  }

  APP_LogThresholds("IBI thresholds programmed: ", readbackLowMilliC, readbackHighMilliC);

  APP_LogCccCommand("init", "TX", target->dynamicAddress, P3T1755_CCC_ENEC, (const uint8_t[]){P3T1755_EVENT_SIR}, 1U);

  return P3T1755_EnableIbi(target);
}

static HAL_StatusTypeDef APP_SetupMultiTargetIbi(void)
{
  uint8_t index;

  for (index = 0U; index < g_p3t1755TargetCount; ++index)
  {
    HAL_StatusTypeDef status;
    uint16_t tempRaw = 0;
    int32_t tempMilliC = 0;
    P3T1755_HandleTypeDef *target = &g_p3t1755Targets[index];

    if (target->hasEntdaaInfo && (target->entdaaInfo.BCR.IBIRequestCapable == DISABLE))
    {
      printf("IBI skipped for T%u[0x%02X->0x%02X]: BCR reports no IBI support\r\n",
             (unsigned int)target->deviceIndex,
             target->staticAddress,
             target->dynamicAddress);
      continue;
    }

    status = P3T1755_ReadTemperature(target, &tempRaw, &tempMilliC);
    if (status != HAL_OK)
    {
      APP_LogI3CFailure("ReadTemperatureBeforeIbiSetup", status);
      return status;
    }

    printf("IBI setup for T%u[0x%02X->0x%02X] tempRaw=%02X%02X ",
           (unsigned int)target->deviceIndex,
           target->staticAddress,
           target->dynamicAddress,
           (unsigned int)(tempRaw >> 8),
           (unsigned int)(tempRaw & 0xFFU));
    APP_PrintMilliC("temp=", tempMilliC);
    printf("\r\n");

    status = APP_SetupThresholdsAndIbi(target, tempMilliC);
    if (status != HAL_OK)
    {
      printf("IBI enable failed for T%u[0x%02X->0x%02X]\r\n",
             (unsigned int)target->deviceIndex,
             target->staticAddress,
             target->dynamicAddress);
      return status;
    }

    printf("IBI enabled for T%u[0x%02X->0x%02X]\r\n",
           (unsigned int)target->deviceIndex,
           target->staticAddress,
           target->dynamicAddress);
  }

  return HAL_OK;
}

static P3T1755_HandleTypeDef *APP_FindTargetByDynamicAddress(uint8_t dynamicAddress)
{
  uint8_t index;

  for (index = 0U; index < g_p3t1755TargetCount; ++index)
  {
    if (g_p3t1755Targets[index].dynamicAddress == dynamicAddress)
    {
      return &g_p3t1755Targets[index];
    }
  }

  return NULL;
}

static void APP_HandleIbiEvent(void)
{
  HAL_StatusTypeDef status;
  I3C_CCCInfoTypeDef cccInfo = {0};
  P3T1755_HandleTypeDef *target;

  status = HAL_I3C_GetCCCInfo(&hi3c1, EVENT_ID_IBI, &cccInfo);
  if (status != HAL_OK)
  {
    APP_LogI3CFailure("GetCCCInfo(IBI)", status);
    return;
  }

  target = APP_FindTargetByDynamicAddress((uint8_t)cccInfo.IBICRTgtAddr);
  if (target == NULL)
  {
    printf("IBI event: unknown target=0x%02X payload=0x%08lX\r\n",
           (unsigned int)cccInfo.IBICRTgtAddr,
           (unsigned long)cccInfo.IBITgtPayload);
    return;
  }

  printf("IBI event: T%u[0x%02X->0x%02X] payload=0x%08lX ",
         (unsigned int)target->deviceIndex,
         target->staticAddress,
         target->dynamicAddress,
         (unsigned long)cccInfo.IBITgtPayload);

  {
    uint16_t tempRaw = 0;
    int32_t tempMilliC = 0;
    uint8_t confRaw = 0;

    status = P3T1755_ReadTemperature(target, &tempRaw, &tempMilliC);
    if (status != HAL_OK)
    {
      printf("tempRead=%s(error=0x%08lX)\r\n",
             APP_StatusToText(status),
             (unsigned long)hi3c1.ErrorCode);
      return;
    }

    status = P3T1755_ReadConfig(target, &confRaw);
    if (status != HAL_OK)
    {
      printf("confRead=%s(error=0x%08lX)\r\n",
             APP_StatusToText(status),
             (unsigned long)hi3c1.ErrorCode);
      return;
    }

    printf("tempRaw=%02X%02X conf=0x%02X ",
           (unsigned int)(tempRaw >> 8),
           (unsigned int)(tempRaw & 0xFFU),
           confRaw);
    APP_PrintMilliC("temp=", tempMilliC);
    printf("\r\n");
  }
}

void HAL_I3C_NotifyCallback(I3C_HandleTypeDef *hi3c, uint32_t eventId)
{
  if (hi3c == &hi3c1)
  {
    g_i3cEvents |= eventId;
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I3C1_Init();
  MX_ICACHE_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_StatusTypeDef status;

  HAL_Delay(P3T1755_POWER_UP_DELAY_MS);
  APP_PrintBanner();

  status = APP_I3C_ApplyBusTiming(0x7cU, 0x7cU, 0x7cU, 0x00U, 0x32U, 0xf8U);
  if (status != HAL_OK)
  {
    APP_LogI3CFailure("ApplySafeBusTiming", status);
    Error_Handler();
  }

  status = APP_I3C_ResetDynamicAddresses();
  if (status != HAL_OK)
  {
    APP_LogI3CFailure("BroadcastRSTDAA", status);
    Error_Handler();
  }

  status = APP_DiscoverTargets();
  if (status != HAL_OK)
  {
    APP_LogI3CFailure("P3T1755_DiscoverTargets", status);
    Error_Handler();
  }

  status = APP_AssociateIdentityViaEntdaa();
  if (status != HAL_OK)
  {
    APP_LogI3CFailure("P3T1755_AssociateIdentityViaEntdaa", status);
    Error_Handler();
  }

  APP_PrintTargetTable();

  status = APP_I3C_ApplyBusTiming(0x0bU, 0x07U, 0x59U, 0x00U, 0x31U, 0xf8U);
  if (status != HAL_OK)
  {
    APP_LogI3CFailure("ApplyFastBusTiming", status);
    Error_Handler();
  }

  status = APP_VerifyTargetsAtCurrentTiming();
  if (status != HAL_OK)
  {
    APP_LogI3CFailure("VerifyTargetsAfterFastTiming", status);
    Error_Handler();
  }

  printf("I3C bus timing switched to 12.5MHz for private SDR transfers\r\n");

  status = APP_ReadAndPrintTemperatures();
  if (status != HAL_OK)
  {
    APP_LogI3CFailure("InitialTemperatureRead", status);
  }

  status = APP_SetupMultiTargetIbi();
  if (status != HAL_OK)
  {
    APP_LogI3CFailure("SetupMultiTargetIbi", status);
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    while ((g_i3cEvents & EVENT_ID_IBI) == EVENT_ID_IBI)
    {
      g_i3cEvents &= ~EVENT_ID_IBI;
      APP_HandleIbiEvent();
    }

    (void)APP_ReadAndPrintTemperatures();

    HAL_Delay(P3T1755_POLL_PERIOD_MS);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_CSI;
  RCC_OscInitStruct.CSIState = RCC_CSI_ON;
  RCC_OscInitStruct.CSICalibrationValue = RCC_CSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_CSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 125;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the programming delay
  */
  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_2);
}

/**
  * @brief I3C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I3C1_Init(void)
{

  /* USER CODE BEGIN I3C1_Init 0 */

  /* USER CODE END I3C1_Init 0 */

  I3C_FifoConfTypeDef sFifoConfig = {0};
  I3C_CtrlConfTypeDef sCtrlConfig = {0};

  /* USER CODE BEGIN I3C1_Init 1 */

  /* USER CODE END I3C1_Init 1 */
  hi3c1.Instance = I3C1;
  hi3c1.Mode = HAL_I3C_MODE_CONTROLLER;
  hi3c1.Init.CtrlBusCharacteristic.SDAHoldTime = HAL_I3C_SDA_HOLD_TIME_1_5;
  hi3c1.Init.CtrlBusCharacteristic.WaitTime = HAL_I3C_OWN_ACTIVITY_STATE_0;
  hi3c1.Init.CtrlBusCharacteristic.SCLPPLowDuration = 0x0b;
  hi3c1.Init.CtrlBusCharacteristic.SCLI3CHighDuration = 0x07;
  hi3c1.Init.CtrlBusCharacteristic.SCLODLowDuration = 0x59;
  hi3c1.Init.CtrlBusCharacteristic.SCLI2CHighDuration = 0x00;
  hi3c1.Init.CtrlBusCharacteristic.BusFreeDuration = 0x31;
  hi3c1.Init.CtrlBusCharacteristic.BusIdleDuration = 0xf8;
  if (HAL_I3C_Init(&hi3c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure FIFO
  */
  sFifoConfig.RxFifoThreshold = HAL_I3C_RXFIFO_THRESHOLD_1_4;
  sFifoConfig.TxFifoThreshold = HAL_I3C_TXFIFO_THRESHOLD_1_4;
  sFifoConfig.ControlFifo = HAL_I3C_CONTROLFIFO_DISABLE;
  sFifoConfig.StatusFifo = HAL_I3C_STATUSFIFO_DISABLE;
  if (HAL_I3C_SetConfigFifo(&hi3c1, &sFifoConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure controller
  */
  sCtrlConfig.DynamicAddr = 0;
  sCtrlConfig.StallTime = 0x00;
  sCtrlConfig.HotJoinAllowed = DISABLE;
  sCtrlConfig.ACKStallState = DISABLE;
  sCtrlConfig.CCCStallState = DISABLE;
  sCtrlConfig.TxStallState = DISABLE;
  sCtrlConfig.RxStallState = DISABLE;
  sCtrlConfig.HighKeeperSDA = DISABLE;
  if (HAL_I3C_Ctrl_Config(&hi3c1, &sCtrlConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I3C1_Init 2 */

  /* USER CODE END I3C1_Init 2 */

}

/**
  * @brief ICACHE Initialization Function
  * @param None
  * @retval None
  */
static void MX_ICACHE_Init(void)
{

  /* USER CODE BEGIN ICACHE_Init 0 */

  /* USER CODE END ICACHE_Init 0 */

  /* USER CODE BEGIN ICACHE_Init 1 */

  /* USER CODE END ICACHE_Init 1 */

  /** Enable instruction cache in 1-way (direct mapped cache)
  */
  if (HAL_ICACHE_ConfigAssociativityMode(ICACHE_1WAY) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_ICACHE_Enable() != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ICACHE_Init 2 */

  /* USER CODE END ICACHE_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_RXOVERRUNDISABLE_INIT|UART_ADVFEATURE_DMADISABLEONERROR_INIT;
  huart1.AdvancedInit.OverrunDisable = UART_ADVFEATURE_OVERRUN_DISABLE;
  huart1.AdvancedInit.DMADisableonRxError = UART_ADVFEATURE_DMA_DISABLEONRXERROR;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};
  MPU_Attributes_InitTypeDef MPU_AttributesInit = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region 0 and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x08FFF000;
  MPU_InitStruct.LimitAddress = 0x08FFFFFF;
  MPU_InitStruct.AttributesIndex = MPU_ATTRIBUTES_NUMBER0;
  MPU_InitStruct.AccessPermission = MPU_REGION_ALL_RO;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Attribute 0 and the memory to be protected
  */
  MPU_AttributesInit.Number = MPU_ATTRIBUTES_NUMBER0;
  MPU_AttributesInit.Attributes = INNER_OUTER(MPU_NOT_CACHEABLE);

  HAL_MPU_ConfigMemoryAttributes(&MPU_AttributesInit);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  printf("Error_Handler entered, hi3c1.State=0x%02lX hi3c1.ErrorCode=0x%08lX\r\n",
         (unsigned long)HAL_I3C_GetState(&hi3c1),
         (unsigned long)hi3c1.ErrorCode);
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
