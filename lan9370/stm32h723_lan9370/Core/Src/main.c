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
#include "lwip.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lan9370_driver.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include "lwip/udp.h"
#include "lwip/etharp.h"
#include "shell_port.h"
#include <stdarg.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_UDP_ECHO_PORT   7U
#define APP_IPERF_PORT      5001U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

UART_HandleTypeDef hlpuart1;
DMA_HandleTypeDef hdma_lpuart1_rx;
DMA_HandleTypeDef hdma_lpuart1_tx;

SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_rx;
DMA_HandleTypeDef hdma_spi1_tx;

/* USER CODE BEGIN PV */
static struct udp_pcb *udp_echo_pcb;
static void *lwiperf_session;
extern struct netif gnetif;
extern ETH_DMADescTypeDef DMATxDscrTab[];
extern ETH_DMADescTypeDef DMARxDscrTab[];
extern ETH_HandleTypeDef heth;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_BDMA_Init(void);
static void MX_DMA_Init(void);
static void MX_LPUART1_UART_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */
// static void CPU_CACHE_Enable(void);
int debug_printf(const char *format, ...);
static void App_NetworkServicesInit(void);
static void App_PrintNetworkBanner(void);
static void App_UdpEchoInit(void);
static void App_UdpEchoRecv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                            const ip_addr_t *addr, u16_t port);
static void App_LwiperfReport(void *arg, enum lwiperf_report_type report_type,
                              const ip_addr_t *local_addr, u16_t local_port,
                              const ip_addr_t *remote_addr, u16_t remote_port,
                              u32_t bytes_transferred, u32_t ms_duration,
                              u32_t bandwidth_kbitpsec);
static void App_LwiperfInit(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* Save reset cause before HAL_Init potentially modifies RCC flags */
  volatile uint32_t g_rcc_rsr = RCC->RSR;
  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_BDMA_Init();
  MX_DMA_Init();
  MX_LPUART1_UART_Init();
  /* MX_LWIP_Init() is called after LAN9370 is configured in USER CODE 2 */
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */
  /* Print reset cause from saved RCC->RSR */
  debug_printf("\r\nstm32h723_lan9370 boot\r\n");
  {
    if (g_rcc_rsr & RCC_RSR_BORRSTF)   debug_printf("[RST] BOR\r\n");
    if (g_rcc_rsr & RCC_RSR_PINRSTF)   debug_printf("[RST] PIN\r\n");
    if (g_rcc_rsr & RCC_RSR_PORRSTF)   debug_printf("[RST] POR\r\n");
    if (g_rcc_rsr & RCC_RSR_SFTRSTF)   debug_printf("[RST] Software\r\n");
    if (g_rcc_rsr & RCC_RSR_IWDG1RSTF) debug_printf("[RST] IWDG\r\n");
    if (g_rcc_rsr & RCC_RSR_WWDG1RSTF) debug_printf("[RST] WWDG\r\n");
    if (g_rcc_rsr & RCC_RSR_LPWRRSTF)  debug_printf("[RST] LPower\r\n");
    __HAL_RCC_CLEAR_RESET_FLAGS();
  }

  /* Initialize LAN9370 via SPI before lwIP starts */
  debug_printf("[MAIN] LAN9370 init...\r\n");
  if (LAN9370_Init(&hspi1) != LAN9370_OK)
  {
    debug_printf("[MAIN] LAN9370 init FAILED\r\n");
    Error_Handler();
  }

  LAN9370_ChipInfo_t chipInfo;
  if (LAN9370_GetChipInfo(&chipInfo) == LAN9370_OK)
  {
    debug_printf("[MAIN] Chip: %s ID=0x%08lX Rev=%d\r\n",
                 chipInfo.chipName, (unsigned long)chipInfo.chipId, (int)chipInfo.revisionId);
  }
  /* Debug: read individual ID bytes */
  {
    uint8_t b0=0, b1=0, b2=0, b3=0;
    LAN9370_SPI_ReadReg8(0x0000, &b0);
    LAN9370_SPI_ReadReg8(0x0001, &b1);
    LAN9370_SPI_ReadReg8(0x0002, &b2);
    LAN9370_SPI_ReadReg8(0x0003, &b3);
    debug_printf("[SPI] ID bytes: %02X %02X %02X %02X (expect 00 93 70 0x)\r\n", b0, b1, b2, b3);
  }

  /* Port 2 = Master (connects to automotive slave device 192.168.0.68) */
  /* Port 4 = Slave  (connects to PC via automotive converter)          */
  LAN9370_SetT1MasterSlave(LAN9370_PORT_1, LAN9370_T1_SLAVE);
  if (LAN9370_SetT1MasterSlave(LAN9370_PORT_2, LAN9370_T1_MASTER) != LAN9370_OK) {
    debug_printf("[MAIN] Port2 set master FAILED\r\n");
  }
  LAN9370_SetT1MasterSlave(LAN9370_PORT_3, LAN9370_T1_SLAVE);
  LAN9370_SetT1MasterSlave(LAN9370_PORT_4, LAN9370_T1_SLAVE);

  /* Enable all ports */
  for (int p = 1; p <= LAN9370_PORT_COUNT; p++)
  {
    LAN9370_SetPortEnable((LAN9370_Port_t)p, true);
  }

  /* Configure Port 5 as RMII for STM32H723 ETH MAC */
  LAN9370_ConfigurePort5Rmii();
  /* Re-enable Port 5 after RMII config (register layout change may reset MSTP) */
  LAN9370_SetPortEnable(LAN9370_PORT_5, true);

  /* All ports forward to all (flat switching, no VLAN isolation) */
  for (int p = 1; p <= LAN9370_PORT_COUNT; p++)
  {
    LAN9370_SetPortMembership((LAN9370_Port_t)p, 0x1FU);
  }
  LAN9370_SetMACLearning(true);

  /* Allow PHY master/slave config to settle before reading status */
  HAL_Delay(50);

  /* Initialize lwIP / ETH MAC (LAN9370 must be configured before ETH starts) */
  /* Pre-select RMII mode and enable ETH clocks early, then wait for
   * RMII REF_CLK (PA1 from MCO2/PC9) to fully stabilize.
   * The H7 HAL's SYSCFG_ETHInterfaceSelect only sets EPIS_SEL but does NOT
   * close the PA1 analog switch. Without explicitly clearing PA1SO, the
   * internal REF_CLK path from PA1 pin to the ETH MAC is disconnected even
   * though the pin mux selects AF11_ETH. */
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  HAL_SYSCFG_ETHInterfaceSelect(SYSCFG_ETH_RMII);
  CLEAR_BIT(SYSCFG->PMCR, SYSCFG_PMCR_PA1SO);   /* close PA1 switch → REF_CLK reaches ETH */
  __HAL_RCC_ETH1MAC_CLK_ENABLE();
  __HAL_RCC_ETH1TX_CLK_ENABLE();
  __HAL_RCC_ETH1RX_CLK_ENABLE();
  HAL_Delay(200);

  /* LAN9370 Port5 RMII configured (verify in ConfigurePort5Rmii itself) */

  /* Initialize lwIP / ETH MAC (LAN9370 must be configured before ETH starts) */
  MX_LWIP_Init();
  debug_printf("[MAIN] lwIP init done\r\n");

  /* Re-verify Port2 master mode after lwIP/ETH init */
  {
    uint16_t ms_ctrl = 0;
    LAN9370_PHY_ReadReg(LAN9370_PORT_2, 0x09, &ms_ctrl);
    debug_printf("[MAIN] Port2 MS_CTRL(0x09)=0x%04X (expect 0x1800=master)\r\n", ms_ctrl);
    /* Re-apply if lost */
    if ((ms_ctrl & 0x1800U) != 0x1800U) {
      debug_printf("[MAIN] Port2 master config lost, re-applying...\r\n");
      LAN9370_SetT1MasterSlave(LAN9370_PORT_2, LAN9370_T1_MASTER);
    }
  }

  App_NetworkServicesInit();
  App_PrintNetworkBanner();

  /* Initialize shell */
  Shell_Init(&hlpuart1);
  Shell_PrintBanner();

  /* Send gratuitous ARP to announce our MAC/IP to the network */
  HAL_Delay(200);
  etharp_gratuitous(&gnetif);

  /* Configure ETH MAC filter to accept broadcast + unicast packets.
   * Default after HAL_ETH_Init is MACPFR=0 (all packets rejected). */
  {
    ETH_MACFilterConfigTypeDef filterConfig;
    HAL_ETH_GetMACFilterConfig(&heth, &filterConfig);
    filterConfig.PromiscuousMode = ENABLE;
    filterConfig.ReceiveAllMode = ENABLE;
    filterConfig.PassAllMulticast = ENABLE;
    filterConfig.BroadcastFilter = DISABLE;
    HAL_ETH_SetMACFilterConfig(&heth, &filterConfig);
  }

  /* Clear any sticky DMA error bits from init */
  ETH->DMACSR = ETH_DMACSR_FBE | ETH_DMACSR_TPS | ETH_DMACSR_RPS;

  /* --- Quick diag verification: read T1 PHY regs on Port2 --- */
  {
    uint16_t sqi=0, mse=0, ms_stat=0;
    LAN9370_PHY_ReadReg(LAN9370_PORT_2, T1_PHY_SQI, &sqi);
    LAN9370_PHY_ReadReg(LAN9370_PORT_2, T1_PHY_MSE, &mse);
    LAN9370_PHY_ReadReg(LAN9370_PORT_2, T1_PHY_MASTER_SLAVE_STATUS, &ms_stat);
    debug_printf("[DIAG] Port2 SQI=0x%04X MSE=0x%04X MS_STAT=0x%04X\r\n", sqi, mse, ms_stat);
    uint16_t temp_raw=0;
    LAN9370_SPI_ReadReg16(REG_TEMP_MON, &temp_raw);
    debug_printf("[DIAG] TEMP_MON(0x%04X)=0x%04X\r\n", REG_TEMP_MON, temp_raw);
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    Shell_Process();
    MX_LWIP_Process();
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

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /* Enable D2 SRAM1 clock — ETH DMA descriptors and RX pool at 0x30000000 */
  __HAL_RCC_D2SRAM1_CLK_ENABLE();

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 2;
  RCC_OscInitStruct.PLL.PLLN = 44;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
  __HAL_RCC_PLL2CLKOUT_ENABLE(RCC_PLL2_DIVP);
  HAL_RCC_MCOConfig(RCC_MCO2, RCC_MCO2SOURCE_PLL2PCLK, RCC_MCODIV_1);
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI1;
  PeriphClkInitStruct.PLL2.PLL2M = 5;
  PeriphClkInitStruct.PLL2.PLL2N = 40;
  PeriphClkInitStruct.PLL2.PLL2P = 4;
  PeriphClkInitStruct.PLL2.PLL2Q = 2;
  PeriphClkInitStruct.PLL2.PLL2R = 2;
  PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_3;
  PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
  PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
  PeriphClkInitStruct.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
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
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 0x0;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi1.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi1.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi1.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi1.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi1.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_BDMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_BDMA_CLK_ENABLE();

  /* DMA interrupt init */
  /* BDMA_Channel0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(BDMA_Channel0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(BDMA_Channel0_IRQn);
  /* BDMA_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(BDMA_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(BDMA_Channel1_IRQn);

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  /* DMA1_Stream1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LAN9370_NRST_GPIO_Port, LAN9370_NRST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : LAN9370_NRST_Pin */
  GPIO_InitStruct.Pin = LAN9370_NRST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LAN9370_NRST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PC9 */
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF0_MCO;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*AnalogSwitch Config - OPEN PC3 to avoid interference with LAN9370 nRST */
  HAL_SYSCFG_AnalogSwitchConfig(SYSCFG_SWITCH_PC3, SYSCFG_SWITCH_PC3_OPEN);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
int debug_printf(const char *format, ...)
{
  char buffer[256];
  int length;
  va_list args;

  va_start(args, format);
  length = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  if (length <= 0) return length;
  if (length >= (int)sizeof(buffer)) length = (int)sizeof(buffer) - 1;
  HAL_UART_Transmit(&hlpuart1, (uint8_t *)buffer, (uint16_t)length, HAL_MAX_DELAY);
  return length;
}

static void App_NetworkServicesInit(void)
{
  App_UdpEchoInit();
  App_LwiperfInit();
}

static void App_PrintNetworkBanner(void)
{
  const ip4_addr_t *ip   = netif_ip4_addr(&gnetif);
  const ip4_addr_t *mask = netif_ip4_netmask(&gnetif);
  const ip4_addr_t *gw   = netif_ip4_gw(&gnetif);

  debug_printf("Network ready IP=%u.%u.%u.%u Mask=%u.%u.%u.%u GW=%u.%u.%u.%u\r\n",
               ip4_addr1_16(ip), ip4_addr2_16(ip), ip4_addr3_16(ip), ip4_addr4_16(ip),
               ip4_addr1_16(mask), ip4_addr2_16(mask), ip4_addr3_16(mask), ip4_addr4_16(mask),
               ip4_addr1_16(gw), ip4_addr2_16(gw), ip4_addr3_16(gw), ip4_addr4_16(gw));
  debug_printf("UDP echo on port %u, TCP iperf on port %u\r\n",
               (unsigned)APP_UDP_ECHO_PORT, (unsigned)APP_IPERF_PORT);
}

static void App_UdpEchoRecv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                            const ip_addr_t *addr, u16_t port)
{
  (void)arg;
  if (p != NULL)
  {
    udp_sendto(pcb, p, addr, port);
    pbuf_free(p);
  }
}

static void App_UdpEchoInit(void)
{
  udp_echo_pcb = udp_new();
  if (udp_echo_pcb == NULL) { debug_printf("udp_new failed\r\n"); return; }
  if (udp_bind(udp_echo_pcb, IP_ADDR_ANY, APP_UDP_ECHO_PORT) != ERR_OK)
  {
    debug_printf("udp_bind failed\r\n");
    udp_remove(udp_echo_pcb); udp_echo_pcb = NULL; return;
  }
  udp_recv(udp_echo_pcb, App_UdpEchoRecv, NULL);
}

static void App_LwiperfReport(void *arg, enum lwiperf_report_type report_type,
                              const ip_addr_t *local_addr, u16_t local_port,
                              const ip_addr_t *remote_addr, u16_t remote_port,
                              u32_t bytes_transferred, u32_t ms_duration,
                              u32_t bandwidth_kbitpsec)
{
  char local_s[IPADDR_STRLEN_MAX], remote_s[IPADDR_STRLEN_MAX];
  (void)arg; (void)report_type;
  ipaddr_ntoa_r(local_addr,  local_s,  sizeof(local_s));
  ipaddr_ntoa_r(remote_addr, remote_s, sizeof(remote_s));
  debug_printf("iperf %s:%u <- %s:%u  %lu bytes  %lu ms  %lu kbps\r\n",
               local_s, (unsigned)local_port,
               remote_s, (unsigned)remote_port,
               (unsigned long)bytes_transferred,
               (unsigned long)ms_duration,
               (unsigned long)bandwidth_kbitpsec);
}

static void App_LwiperfInit(void)
{
  lwiperf_session = lwiperf_start_tcp_server_default(App_LwiperfReport, NULL);
  if (lwiperf_session == NULL)
  {
    debug_printf("lwiperf_start_tcp_server_default failed\r\n");
  }
}
/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /* Region 0: entire address space, no access (background protection) */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Region 1: D2 SRAM1 (0x30000000, 32KB) – non-cacheable for ETH DMA */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x30000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32KB;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
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
  debug_printf("Error_Handler\r\n");
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
