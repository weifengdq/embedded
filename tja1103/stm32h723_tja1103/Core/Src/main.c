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
#include "lwip/apps/lwiperf.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include "lwip/udp.h"
#include "shell_port.h"
#include <stdarg.h>
#include <stdio.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_UDP_ECHO_PORT                        7U
#define APP_IPERF_PORT                           LWIPERF_TCP_PORT_DEFAULT

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

UART_HandleTypeDef hlpuart1;

/* USER CODE BEGIN PV */
static struct udp_pcb *udp_echo_pcb;
static void *lwiperf_session;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_LPUART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void CPU_CACHE_Enable(void);
int debug_printf(const char *format, ...);
static void App_NetworkServicesInit(void);
static void App_PrintNetworkBanner(void);
static void App_UdpEchoInit(void);
static void App_UdpEchoRecv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);
static const char *App_LwiperfReportTypeString(enum lwiperf_report_type report_type);
static void App_LwiperfReport(void *arg, enum lwiperf_report_type report_type,
                              const ip_addr_t *local_addr, u16_t local_port,
                              const ip_addr_t *remote_addr, u16_t remote_port,
                              u32_t bytes_transferred, u32_t ms_duration,
                              u32_t bandwidth_kbitpsec);
static void App_LwiperfInit(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
extern struct netif gnetif;

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU caches before HAL and LwIP start touching AXI SRAM. */
  CPU_CACHE_Enable();

  /* MCU Configuration--------------------------------------------------------*/

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
  MX_LPUART1_UART_Init();
  MX_LWIP_Init();
  /* USER CODE BEGIN 2 */
  debug_printf("\r\nstm32h723_tja1103 boot\r\n");
  Shell_Init(&hlpuart1);
  Shell_PrintBanner();
  App_NetworkServicesInit();
  App_PrintNetworkBanner();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    MX_LWIP_Process();
    Shell_Process();
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

  /* ETH DMA descriptors and the zero-copy RX pool live in D2 SRAM1. */
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
  hlpuart1.Init.BaudRate = 921600;
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
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(PHY_NRST_GPIO_Port, PHY_NRST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : PHY_NRST_Pin */
  GPIO_InitStruct.Pin = PHY_NRST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PHY_NRST_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
int debug_printf(const char *format, ...)
{
  char buffer[256];
  int length;
  int transmit_length;
  va_list args;

  va_start(args, format);
  length = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  if (length <= 0)
  {
    return length;
  }

  transmit_length = (length >= (int)sizeof(buffer)) ? ((int)sizeof(buffer) - 1) : length;
  HAL_UART_Transmit(&hlpuart1, (uint8_t *)buffer, (uint16_t)transmit_length, HAL_MAX_DELAY);
  return transmit_length;
}

static void App_NetworkServicesInit(void)
{
  App_UdpEchoInit();
  App_LwiperfInit();
}

static void App_PrintNetworkBanner(void)
{
  const ip4_addr_t *ip;
  const ip4_addr_t *mask;
  const ip4_addr_t *gw;

  ip = netif_ip4_addr(&gnetif);
  mask = netif_ip4_netmask(&gnetif);
  gw = netif_ip4_gw(&gnetif);

  debug_printf("Network ready IP=%u.%u.%u.%u Mask=%u.%u.%u.%u GW=%u.%u.%u.%u\r\n",
               (unsigned int)ip4_addr1_16(ip),
               (unsigned int)ip4_addr2_16(ip),
               (unsigned int)ip4_addr3_16(ip),
               (unsigned int)ip4_addr4_16(ip),
               (unsigned int)ip4_addr1_16(mask),
               (unsigned int)ip4_addr2_16(mask),
               (unsigned int)ip4_addr3_16(mask),
               (unsigned int)ip4_addr4_16(mask),
               (unsigned int)ip4_addr1_16(gw),
               (unsigned int)ip4_addr2_16(gw),
               (unsigned int)ip4_addr3_16(gw),
               (unsigned int)ip4_addr4_16(gw));
  debug_printf("UDP echo server on port %u\r\n", (unsigned int)APP_UDP_ECHO_PORT);
  debug_printf("TCP iperf server on port %u\r\n", (unsigned int)APP_IPERF_PORT);
}

static void App_UdpEchoRecv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
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
  if (udp_echo_pcb == NULL)
  {
    debug_printf("udp_new failed\r\n");
    return;
  }

  if (udp_bind(udp_echo_pcb, IP_ADDR_ANY, APP_UDP_ECHO_PORT) != ERR_OK)
  {
    debug_printf("udp_bind failed\r\n");
    udp_remove(udp_echo_pcb);
    udp_echo_pcb = NULL;
    return;
  }

  udp_recv(udp_echo_pcb, App_UdpEchoRecv, NULL);
}

static const char *App_LwiperfReportTypeString(enum lwiperf_report_type report_type)
{
  switch (report_type)
  {
    case LWIPERF_TCP_DONE_SERVER:
      return "server-done";
    case LWIPERF_TCP_DONE_CLIENT:
      return "client-done";
    case LWIPERF_TCP_ABORTED_LOCAL:
      return "aborted-local";
    case LWIPERF_TCP_ABORTED_LOCAL_DATAERROR:
      return "aborted-data";
    case LWIPERF_TCP_ABORTED_LOCAL_TXERROR:
      return "aborted-tx";
    case LWIPERF_TCP_ABORTED_REMOTE:
      return "aborted-remote";
    default:
      return "unknown";
  }
}

static void App_LwiperfReport(void *arg, enum lwiperf_report_type report_type,
                              const ip_addr_t *local_addr, u16_t local_port,
                              const ip_addr_t *remote_addr, u16_t remote_port,
                              u32_t bytes_transferred, u32_t ms_duration,
                              u32_t bandwidth_kbitpsec)
{
  char local_addr_text[IPADDR_STRLEN_MAX];
  char remote_addr_text[IPADDR_STRLEN_MAX];

  (void)arg;

  ipaddr_ntoa_r(local_addr, local_addr_text, sizeof(local_addr_text));
  ipaddr_ntoa_r(remote_addr, remote_addr_text, sizeof(remote_addr_text));

  debug_printf("iperf %s local=%s:%u remote=%s:%u bytes=%lu time_ms=%lu avg_kbps=%lu\r\n",
               App_LwiperfReportTypeString(report_type),
               local_addr_text,
               (unsigned int)local_port,
               remote_addr_text,
               (unsigned int)remote_port,
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

  /** Initializes and configures the Region and the memory to be protected
  */
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

  /* Keep the ETH DMA descriptors and RX pool coherent when D-Cache is enabled. */
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

static void CPU_CACHE_Enable(void)
{
  SCB_EnableICache();
  SCB_EnableDCache();
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
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
