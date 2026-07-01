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
#include "lan9370_driver.h"
#include "lan9370_persist.h"
#include "lan8720_driver.h"
#include "shell_port.h"
#include "app_extensions.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define STATUS_LED_PORT                    GPIOA
#define STATUS_LED_PIN                     GPIO_PIN_5

#define APP_MONITOR_POLL_MS                100U
#define APP_LED_SLOW_BLINK_MS              500U
#define APP_LED_FAST_BLINK_MS              100U
#define APP_LED_ACTIVITY_HOLD_MS           150U
#define APP_PORT2_RECOVERY_TRIGGER_MS      3000U
#define APP_PORT2_RECOVERY_INTERVAL_MS     5000U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

UART_HandleTypeDef hlpuart1;

SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */
typedef struct {
  uint32_t lastPollMs;
  uint32_t lastActivityMs;
  uint32_t lastLedToggleMs;
  uint32_t port2DownSinceMs;
  uint32_t lastRecoveryMs;
  uint32_t port2RxTotal;
  uint32_t port2TxTotal;
  uint32_t port5RxTotal;
  uint32_t port5TxTotal;
  uint8_t ledState;
  uint8_t countersValid;
} AppMonitorState_t;

static AppMonitorState_t gAppMonitor = {0};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_ICACHE_Init(void);
static void MX_LPUART1_UART_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */
static void App_StatusLed_Init(void);
static void App_StatusLed_Write(uint8_t on);
static void App_Port2Recover(void);
static void App_MonitorProcess(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void App_StatusLed_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitStruct.Pin = STATUS_LED_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(STATUS_LED_PORT, &GPIO_InitStruct);

  App_StatusLed_Write(0U);
}

static void App_StatusLed_Write(uint8_t on)
{
  HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
  gAppMonitor.ledState = on;
}

static void App_Port2Recover(void)
{
  printf("[RECOVER] Port2 link down, restarting T1 PHY...\r\n");
  (void)LAN9370_RecoverT1Port(LAN9370_PORT_2);
}

static void App_MonitorProcess(void)
{
  LAN9370_PortStatus_t port2Status;
  LAN8720_Status_t lan8720Status;
  uint32_t nowMs;
  uint32_t port2Rx = 0;
  uint32_t port2Tx = 0;
  uint32_t port5Rx = 0;
  uint32_t port5Tx = 0;
  uint8_t port2Mstp = 0U;
  uint8_t port2LinkUp;
  uint8_t port5LinkUp;
  uint8_t activityDetected = 0U;

  nowMs = HAL_GetTick();
  if ((nowMs - gAppMonitor.lastPollMs) < APP_MONITOR_POLL_MS) {
    return;
  }
  gAppMonitor.lastPollMs = nowMs;

  port2LinkUp = (LAN9370_GetPortStatus(LAN9370_PORT_2, &port2Status) == LAN9370_OK && port2Status.linkUp) ? 1U : 0U;
  port5LinkUp = (LAN8720_GetStatus(&lan8720Status) == LAN8720_OK && lan8720Status.linkUp) ? 1U : 0U;
  (void)LAN9370_SPI_ReadReg8(PORT_CTRL_ADDR(1U, REG_PORTn_MSTP_STATE), &port2Mstp);

  if (LAN9370_ReadPortMibCounter(LAN9370_PORT_2, LAN9370_MIB_RX_TOTAL_IDX, &port2Rx) == LAN9370_OK &&
      LAN9370_ReadPortMibCounter(LAN9370_PORT_2, LAN9370_MIB_TX_TOTAL_IDX, &port2Tx) == LAN9370_OK &&
      LAN9370_ReadPortMibCounter(LAN9370_PORT_5, LAN9370_MIB_RX_TOTAL_IDX, &port5Rx) == LAN9370_OK &&
      LAN9370_ReadPortMibCounter(LAN9370_PORT_5, LAN9370_MIB_TX_TOTAL_IDX, &port5Tx) == LAN9370_OK) {

    if (gAppMonitor.countersValid != 0U) {
      if (port2Rx != gAppMonitor.port2RxTotal ||
          port2Tx != gAppMonitor.port2TxTotal ||
          port5Rx != gAppMonitor.port5RxTotal ||
          port5Tx != gAppMonitor.port5TxTotal) {
        activityDetected = 1U;
        gAppMonitor.lastActivityMs = nowMs;
      }
    } else {
      gAppMonitor.countersValid = 1U;
      gAppMonitor.lastActivityMs = nowMs;
    }

    gAppMonitor.port2RxTotal = port2Rx;
    gAppMonitor.port2TxTotal = port2Tx;
    gAppMonitor.port5RxTotal = port5Rx;
    gAppMonitor.port5TxTotal = port5Tx;
  }

  if ((port2Mstp & (PORT_TX_ENABLE | PORT_RX_ENABLE)) == 0U) {
    gAppMonitor.port2DownSinceMs = 0U;
  } else if (port2LinkUp == 0U) {
    if (gAppMonitor.port2DownSinceMs == 0U) {
      gAppMonitor.port2DownSinceMs = nowMs;
    } else if ((nowMs - gAppMonitor.port2DownSinceMs) >= APP_PORT2_RECOVERY_TRIGGER_MS &&
               (nowMs - gAppMonitor.lastRecoveryMs) >= APP_PORT2_RECOVERY_INTERVAL_MS) {
      App_Port2Recover();
      gAppMonitor.lastRecoveryMs = HAL_GetTick();
      gAppMonitor.port2DownSinceMs = gAppMonitor.lastRecoveryMs;
    }
  } else {
    gAppMonitor.port2DownSinceMs = 0U;
  }

  if (port2LinkUp == 0U && port5LinkUp == 0U) {
    App_StatusLed_Write(0U);
  } else if (port2LinkUp == 0U || port5LinkUp == 0U) {
    if ((nowMs - gAppMonitor.lastLedToggleMs) >= APP_LED_SLOW_BLINK_MS) {
      gAppMonitor.lastLedToggleMs = nowMs;
      App_StatusLed_Write((uint8_t)!gAppMonitor.ledState);
    }
  } else if (activityDetected != 0U || (nowMs - gAppMonitor.lastActivityMs) <= APP_LED_ACTIVITY_HOLD_MS) {
    if ((nowMs - gAppMonitor.lastLedToggleMs) >= APP_LED_FAST_BLINK_MS) {
      gAppMonitor.lastLedToggleMs = nowMs;
      App_StatusLed_Write((uint8_t)!gAppMonitor.ledState);
    }
  } else {
    App_StatusLed_Write(1U);
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
  App_StatusLed_Init();
  MX_ICACHE_Init();
  MX_LPUART1_UART_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */

  /* Initialize shell UART output first so printf works during init */
  Shell_Init(&hlpuart1);

  /* ======================================================================= */
  /* Initialize LAN9370 Switch                                              */
  /* ======================================================================= */
  if (LAN9370_Init(&hspi1) != LAN9370_OK) {
    printf("[ERR] LAN9370 init failed\r\n");
    Error_Handler();
  }

  /* Chip info */
  {
    LAN9370_ChipInfo_t chipInfo;
    if (LAN9370_GetChipInfo(&chipInfo) == LAN9370_OK) {
      printf("[LAN9370] %s, ID=0x%08lX, Rev=%d\r\n",
             chipInfo.chipName, chipInfo.chipId, chipInfo.revisionId);
    }
  }

  /* Post-reset VPHY indirect access enable (critical for T1 PHY access) */
  {
    uint8_t vphyVal = 0;
    for (int attempt = 0; attempt < 5; attempt++) {
      HAL_Delay(10);
      LAN9370_SPI_ReadReg8(0x077C, &vphyVal);
      LAN9370_SPI_WriteReg8(0x077C, vphyVal | 0x10);
      HAL_Delay(5);
      LAN9370_SPI_ReadReg8(0x077C, &vphyVal);
      if (vphyVal & 0x10) break;
    }
    if (!(vphyVal & 0x10)) {
      printf("[WARN] VPHY enable failed, T1 ports may not work\r\n");
    }
  }

  /* ---- 100BASE-T1 Port Configuration ----
   * Current bench wiring only uses Port2 <-> external 100BASE-T1 device.
   * Port1/3/4 are left floating and should stay disabled in release builds. */
  LAN9370_SetPortEnable(LAN9370_PORT_1, false);
  LAN9370_SetT1MasterSlave(LAN9370_PORT_2, LAN9370_T1_MASTER);
  LAN9370_SetPortEnable(LAN9370_PORT_2, true);
  LAN9370_SetPortEnable(LAN9370_PORT_3, false);
  LAN9370_SetPortEnable(LAN9370_PORT_4, false);

  /* ---- Port 5: RMII to LAN8720 ---- */
  {
    uint8_t xmii0, xmii1;
    LAN9370_SPI_ReadReg8(0x5300, &xmii0);
    xmii0 = (xmii0 & 0xE3) | 0x01;  /* RMII mode, TX/RX clocks output */
    LAN9370_SPI_WriteReg8(0x5300, xmii0);

    LAN9370_SPI_ReadReg8(0x5301, &xmii1);
    xmii1 |= 0xC0;  /* REFCLK input, 100M only */
    LAN9370_SPI_WriteReg8(0x5301, xmii1);
  }
  LAN9370_SetPortEnable(LAN9370_PORT_5, true);

  /* ---- LAN8720 External PHY Init (MCU direct MDIO on PB6/PB5) ---- */
  {
    LAN8720_Ret_t lan8720Ret = LAN8720_Init();
    if (lan8720Ret == LAN8720_OK) {
      printf("[LAN8720] Init OK\r\n");
    } else if (lan8720Ret == LAN8720_NO_PHY) {
      printf("[LAN8720] No PHY found (check MDIO wiring)\r\n");
    } else {
      printf("[LAN8720] Init failed (%d)\r\n", (int)lan8720Ret);
    }
  }

  /* ---- L2 Forwarding ----
   * Active datapath is Port2 <-> Port5 only.
   * Isolate floating ports to reduce noise during release testing. */
  LAN9370_SetPortMembership(LAN9370_PORT_1, 0x00);
  LAN9370_SetPortMembership(LAN9370_PORT_2, 0x12);
  LAN9370_SetPortMembership(LAN9370_PORT_3, 0x00);
  LAN9370_SetPortMembership(LAN9370_PORT_4, 0x00);
  LAN9370_SetPortMembership(LAN9370_PORT_5, 0x12);

  /* Enable MAC learning */
  LAN9370_SetMACLearning(true);

  printf("[LAN9370] Configuration complete\r\n");

  /* ======================================================================= */
  /* Extensions                                                            */
  /* ======================================================================= */
  App_Extensions_Init();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* Process shell commands */
    Shell_Process();
    App_MonitorProcess();
    
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 62;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 4096;
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
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPUART1_UART_Init(void)
{

  /* USER CODE BEGIN LPUART1_Init 0 */

  /* USER CODE END LPUART1_Init 0 */

  /* USER CODE BEGIN LPUART1_Init 1 */

  /* USER CODE END LPUART1_Init 1 */
  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 2000000;
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart1.Init.StopBits = UART_STOPBITS_1;
  hlpuart1.Init.Parity = UART_PARITY_NONE;
  hlpuart1.Init.Mode = UART_MODE_TX_RX;
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_RXOVERRUNDISABLE_INIT|UART_ADVFEATURE_DMADISABLEONERROR_INIT;
  hlpuart1.AdvancedInit.OverrunDisable = UART_ADVFEATURE_OVERRUN_DISABLE;
  hlpuart1.AdvancedInit.DMADisableonRxError = UART_ADVFEATURE_DMA_DISABLEONRXERROR;
  hlpuart1.FifoMode = UART_FIFOMODE_DISABLE;
  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&hlpuart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&hlpuart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPUART1_Init 2 */

  /* USER CODE END LPUART1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 0x7;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi1.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi1.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi1.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  hspi1.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
  hspi1.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

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
