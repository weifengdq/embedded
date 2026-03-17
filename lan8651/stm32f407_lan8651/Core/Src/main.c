/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32F407 + LAN8651 10BASE-T1S + lwIP demo
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "ethernetif.h"
#include "lan8651.h"

#include "lwip/apps/lwiperf.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "netif/ethernet.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NET_IP          "192.168.1.108"
#define NET_MASK        "255.255.255.0"
#define NET_GW          "192.168.1.1"
#define UDP_ECHO_PORT   8
#define IPERF_PORT      5008

#define APP_MAC_ADDR0   0x02
#define APP_MAC_ADDR1   0x00
#define APP_MAC_ADDR2   0x00
#define APP_MAC_ADDR3   0x10
#define APP_MAC_ADDR4   0xBA
#define APP_MAC_ADDR5   0x6C
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#define APP_LOGF(enabled, ...) do { if ((enabled) != 0) debug_printf(__VA_ARGS__); } while (0)
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi3;
DMA_HandleTypeDef hdma_spi3_rx;
DMA_HandleTypeDef hdma_spi3_tx;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static lan8651_t lan_dev;
static struct netif netif_lan;
static struct udp_pcb *udp_echo_pcb;
static void *lwiperf_session;
static uint32_t last_err_snapshot[8];
typedef struct {
  uint32_t magic;
  uint32_t sequence;
  uint32_t report_type;
  uint32_t bytes;
  uint32_t duration_ms;
  uint32_t bandwidth_kbitpsec;
  uint32_t local_port;
  uint32_t remote_port;
  uint32_t lan_status_read_mask;
  uint32_t mac_nsr;
  uint32_t mac_tsr;
  uint32_t mac_rsr;
  uint32_t oa_bufsts;
  uint32_t plca_status;
  lan8651_rx_diag_t lan_rx_diag;
} app_iperf_stats_t;

volatile app_iperf_stats_t g_app_iperf_stats __attribute__((used));
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI3_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void SPI3_SetPrescaler(uint32_t prescaler);
static void Network_Init(void);
static void UDP_Echo_Init(void);
static void LWIPerf_Init(void);
static void udp_echo_recv(void *arg, struct udp_pcb *pcb,
                          struct pbuf *p, const ip_addr_t *addr, u16_t port);
static void lwiperf_report(void *arg, enum lwiperf_report_type report_type,
                           const ip_addr_t *local_addr, u16_t local_port,
                           const ip_addr_t *remote_addr, u16_t remote_port,
                           u32_t bytes_transferred, u32_t ms_duration,
                           u32_t bandwidth_kbitpsec);
static int uart_write_all(const char *data, int len);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static int uart_write_all(const char *data, int len)
{
  char tx_buf[192];
  int src_index = 0;
  char prev_ch = '\0';

  if (len <= 0) {
    return 0;
  }

  while (src_index < len) {
    int tx_len = 0;

    while (src_index < len && tx_len < (int)sizeof(tx_buf)) {
      char ch = data[src_index++];

      if (ch == '\n') {
        if (prev_ch != '\r') {
          if (tx_len >= (int)sizeof(tx_buf) - 1) {
            src_index--;
            break;
          }
          tx_buf[tx_len++] = '\r';
        }
        if (tx_len >= (int)sizeof(tx_buf)) {
          src_index--;
          break;
        }
        tx_buf[tx_len++] = '\n';
        prev_ch = ch;
        continue;
      }

      tx_buf[tx_len++] = ch;
      prev_ch = ch;
    }

    if (tx_len > 0) {
      uint32_t timeout_ms = (uint32_t)(tx_len + 8);
      if (HAL_UART_Transmit(&huart1, (uint8_t *)tx_buf, (uint16_t)tx_len, timeout_ms) != HAL_OK) {
        return -1;
      }
    }
  }

  return len;
}

int debug_printf(const char *fmt, ...)
{
  char buf[256];
  va_list args;
  int n;

  va_start(args, fmt);
  n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (n < 0) {
    return n;
  }

  if (n >= (int)sizeof(buf)) {
    n = (int)sizeof(buf) - 1;
  }

  if (uart_write_all(buf, n) < 0) {
    return -1;
  }

  return n;
}

int _write(int fd, char *ptr, int len)
{
  (void)fd;
  if (uart_write_all(ptr, len) < 0) {
    return -1;
  }

  return len;
}

void LAN8651_EXTI_Callback(void)
{
  lan_dev.irq_pending = 1;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == LAN_INT_Pin) {
    LAN8651_EXTI_Callback();
  }
}

static void SPI3_SetPrescaler(uint32_t prescaler)
{
  hspi3.Init.BaudRatePrescaler = prescaler;
  if (HAL_SPI_Init(&hspi3) != HAL_OK) {
    Error_Handler();
  }
}

static const char *lwiperf_report_type_str(enum lwiperf_report_type report_type)
{
  switch (report_type) {
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
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  uint32_t diag_tick = 0;

  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_SPI3_Init();

  HAL_Delay(10);
  SPI3_SetPrescaler(SPI_BAUDRATEPRESCALER_256);

  debug_printf("\r\n\r\n========================================\r\n");
  debug_printf("  STM32F407 + LAN8651 10BASE-T1S Demo\r\n");
  debug_printf("========================================\r\n");

  lan_dev.hspi = &hspi3;
  lan_dev.cs_port = LAN_CS_GPIO_Port;
  lan_dev.cs_pin = LAN_CS_Pin;
  lan_dev.rst_port = LAN_RST_GPIO_Port;
  lan_dev.rst_pin = LAN_RST_Pin;
  lan_dev.int_port = LAN_INT_GPIO_Port;
  lan_dev.int_pin = LAN_INT_Pin;

  lan_dev.mac_addr[0] = APP_MAC_ADDR0;
  lan_dev.mac_addr[1] = APP_MAC_ADDR1;
  lan_dev.mac_addr[2] = APP_MAC_ADDR2;
  lan_dev.mac_addr[3] = APP_MAC_ADDR3;
  lan_dev.mac_addr[4] = APP_MAC_ADDR4;
  lan_dev.mac_addr[5] = APP_MAC_ADDR5;

  if (LAN8651_Init(&lan_dev) != HAL_OK) {
    debug_printf("ERROR: LAN8651 init failed!\r\n");
    Error_Handler();
  }

  if (LAN8651_Start(&lan_dev) != HAL_OK) {
    debug_printf("ERROR: LAN8651 start failed!\r\n");
    Error_Handler();
  }

  SPI3_SetPrescaler(SPI_BAUDRATEPRESCALER_2);

  debug_printf("LAN8651: start OK\r\n");
  Network_Init();
  UDP_Echo_Init();
  LWIPerf_Init();

  debug_printf("Network ready  IP=%s  Mask=%s  GW=%s\r\n", NET_IP, NET_MASK, NET_GW);
  debug_printf("UDP echo server on port %d\r\n", UDP_ECHO_PORT);
  debug_printf("TCP iperf server on port %d (iperf2 compatible)\r\n", IPERF_PORT);
  debug_printf("Try: ping %s\r\n", NET_IP);
  debug_printf("Try: echo \"hello\" | ncat -u %s %d\r\n", NET_IP, UDP_ECHO_PORT);
  debug_printf("Try: iperf -c %s -p %d -t 10\r\n", NET_IP, IPERF_PORT);

  while (1)
  {
    ethernetif_input(&netif_lan);
    sys_check_timeouts();

    if (APP_LOG_PERIODIC_DIAG && (HAL_GetTick() - diag_tick >= 1000U)) {
      uint32_t err_snapshot[8];

      diag_tick = HAL_GetTick();

      err_snapshot[0] = g_lan8651_ctrl_echo_errors;
      err_snapshot[1] = g_lan8651_ctrl_header_bad_errors;
      err_snapshot[2] = g_lan8651_tx_header_bad_errors;
      err_snapshot[3] = g_lan8651_tx_frame_drop_errors;
      err_snapshot[4] = g_lan8651_rx_footer_parity_errors;
      err_snapshot[5] = g_lan8651_rx_header_bad_errors;
      err_snapshot[6] = g_lan8651_rx_frame_drop_errors;
      err_snapshot[7] = g_lan8651_rx_unexpected_swo_errors;

      if (memcmp(last_err_snapshot, err_snapshot, sizeof(err_snapshot)) != 0) {
        memcpy(last_err_snapshot, err_snapshot, sizeof(err_snapshot));
        debug_printf("TC6 ce=%lu ch=%lu tx=%lu/%lu rx=%lu/%lu/%lu/%lu %08lX/%08lX/%08lX\r\n",
                     (unsigned long)g_lan8651_ctrl_echo_errors,
                     (unsigned long)g_lan8651_ctrl_header_bad_errors,
                     (unsigned long)g_lan8651_tx_header_bad_errors,
                     (unsigned long)g_lan8651_tx_frame_drop_errors,
                     (unsigned long)g_lan8651_rx_footer_parity_errors,
                     (unsigned long)g_lan8651_rx_header_bad_errors,
                     (unsigned long)g_lan8651_rx_frame_drop_errors,
                     (unsigned long)g_lan8651_rx_unexpected_swo_errors,
                     (unsigned long)g_lan8651_last_ctrl_echo,
                     (unsigned long)g_lan8651_last_tx_ftr,
                     (unsigned long)g_lan8651_last_rx_ftr);
      }
    }
  }
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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI3_Init(void)
{
  hspi3.Instance = SPI3;
  hspi3.Init.Mode = SPI_MODE_MASTER;
  hspi3.Init.Direction = SPI_DIRECTION_2LINES;
  hspi3.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi3.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi3.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi3.Init.NSS = SPI_NSS_SOFT;
  hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi3.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi3.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  HAL_GPIO_WritePin(LAN_CS_GPIO_Port, LAN_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LAN_RST_GPIO_Port, LAN_RST_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = LAN_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(LAN_CS_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LAN_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LAN_RST_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LAN_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(LAN_INT_GPIO_Port, &GPIO_InitStruct);

  HAL_NVIC_SetPriority(EXTI0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);
}

/* USER CODE BEGIN 4 */

static void Network_Init(void)
{
  ip4_addr_t ipaddr;
  ip4_addr_t netmask;
  ip4_addr_t gw;

  lwip_init();

  ip4addr_aton(NET_IP, &ipaddr);
  ip4addr_aton(NET_MASK, &netmask);
  ip4addr_aton(NET_GW, &gw);

  netif_add(&netif_lan, &ipaddr, &netmask, &gw,
            &lan_dev,
            ethernetif_init,
            ethernet_input);
  netif_set_default(&netif_lan);
  netif_set_link_up(&netif_lan);
  netif_set_up(&netif_lan);

  if (etharp_gratuitous(&netif_lan) != ERR_OK) {
    debug_printf("WARN: gratuitous ARP send failed\r\n");
  }

  debug_printf("lwIP %s initialised\r\n", LWIP_VERSION_STRING);
}

static void udp_echo_recv(void *arg, struct udp_pcb *pcb,
                          struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
  LWIP_UNUSED_ARG(arg);

  if (p != NULL) {
    APP_LOGF(APP_LOG_UDP_ECHO, "UDP echo: %u bytes from %s:%u\r\n",
             p->tot_len, ipaddr_ntoa(addr), port);
    udp_sendto(pcb, p, addr, port);
    pbuf_free(p);
  }

  LWIP_UNUSED_ARG(pcb);
}

static void UDP_Echo_Init(void)
{
  udp_echo_pcb = udp_new();
  if (udp_echo_pcb == NULL) {
    debug_printf("ERROR: udp_new() failed\r\n");
    return;
  }

  if (udp_bind(udp_echo_pcb, IP_ADDR_ANY, UDP_ECHO_PORT) != ERR_OK) {
    debug_printf("ERROR: udp_bind() failed\r\n");
    return;
  }

  udp_recv(udp_echo_pcb, udp_echo_recv, NULL);
}

static void lwiperf_report(void *arg, enum lwiperf_report_type report_type,
                           const ip_addr_t *local_addr, u16_t local_port,
                           const ip_addr_t *remote_addr, u16_t remote_port,
                           u32_t bytes_transferred, u32_t ms_duration,
                           u32_t bandwidth_kbitpsec)
{
  LWIP_UNUSED_ARG(arg);
  uint32_t lan_status_read_mask = 0U;
  uint32_t reg_value = 0U;

  g_app_iperf_stats.magic = 0x49504652u;
  g_app_iperf_stats.sequence++;
  g_app_iperf_stats.report_type = (uint32_t)report_type;
  g_app_iperf_stats.bytes = bytes_transferred;
  g_app_iperf_stats.duration_ms = ms_duration;
  g_app_iperf_stats.bandwidth_kbitpsec = bandwidth_kbitpsec;
  g_app_iperf_stats.local_port = local_port;
  g_app_iperf_stats.remote_port = remote_port;
  g_app_iperf_stats.mac_nsr = 0U;
  g_app_iperf_stats.mac_tsr = 0U;
  g_app_iperf_stats.mac_rsr = 0U;
  g_app_iperf_stats.oa_bufsts = 0U;
  g_app_iperf_stats.plca_status = 0U;
  if (LAN8651_ReadReg(&lan_dev, LAN8651_MMS_MAC, MAC_NSR_REG, &reg_value) == HAL_OK) {
    g_app_iperf_stats.mac_nsr = reg_value;
    lan_status_read_mask |= (1U << 0);
  }
  if (LAN8651_ReadReg(&lan_dev, LAN8651_MMS_MAC, MAC_TSR_REG, &reg_value) == HAL_OK) {
    g_app_iperf_stats.mac_tsr = reg_value;
    lan_status_read_mask |= (1U << 1);
  }
  if (LAN8651_ReadReg(&lan_dev, LAN8651_MMS_MAC, MAC_RSR_REG, &reg_value) == HAL_OK) {
    g_app_iperf_stats.mac_rsr = reg_value;
    lan_status_read_mask |= (1U << 2);
  }
  if (LAN8651_ReadReg(&lan_dev, LAN8651_MMS_OA, OA_BUFSTS_REG, &reg_value) == HAL_OK) {
    g_app_iperf_stats.oa_bufsts = reg_value;
    lan_status_read_mask |= (1U << 3);
  }
  if (LAN8651_ReadReg(&lan_dev, LAN8651_MMS_PHY_VENDOR, PLCA_STS_REG, &reg_value) == HAL_OK) {
    g_app_iperf_stats.plca_status = reg_value;
    lan_status_read_mask |= (1U << 4);
  }
  g_app_iperf_stats.lan_status_read_mask = lan_status_read_mask;
  g_app_iperf_stats.lan_rx_diag = g_lan8651_rx_diag;
  APP_LOGF(APP_LOG_IPERF,
           "iperf report: type=%s local=%s:%u remote=%s:%u bytes=%lu duration_ms=%lu avg_kbps=%lu nsr=0x%08lX tsr=0x%08lX rsr=0x%08lX bufsts=0x%08lX plca=0x%08lX\r\n",
           lwiperf_report_type_str(report_type),
           ipaddr_ntoa(local_addr),
           local_port,
           ipaddr_ntoa(remote_addr),
           remote_port,
           (unsigned long)bytes_transferred,
           (unsigned long)ms_duration,
           (unsigned long)bandwidth_kbitpsec,
           (unsigned long)g_app_iperf_stats.mac_nsr,
           (unsigned long)g_app_iperf_stats.mac_tsr,
           (unsigned long)g_app_iperf_stats.mac_rsr,
           (unsigned long)g_app_iperf_stats.oa_bufsts,
           (unsigned long)g_app_iperf_stats.plca_status);
}

static void LWIPerf_Init(void)
{
  lwiperf_session = lwiperf_start_tcp_server(IP_ADDR_ANY, IPERF_PORT, lwiperf_report, NULL);
  if (lwiperf_session == NULL) {
    debug_printf("ERROR: lwiperf_start_tcp_server() failed\r\n");
  } else {
    APP_LOGF(APP_LOG_IPERF, "lwiperf: TCP server started on port %d\r\n", IPERF_PORT);
  }
}

/* USER CODE END 4 */

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
