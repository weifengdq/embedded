/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : SJA1124 Quad LIN Commander Demo on STM32H503
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
#include "sja1124_drv.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SJA1124_DEVICE_INDEX    0
#define SJA1124_CLK_FREQ_KHZ   10000   /* 10 MHz external oscillator */
#define LIN_DEFAULT_BAUDRATE    19200

#define UART_RX_BUF_SIZE        64
#define UART_RX_TIMEOUT_MS      20000U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
SPI_HandleTypeDef hspi3;
UART_HandleTypeDef huart1;

static SJA1124_SpiHandle_t sja1124_spi;
static SJA1124_DeviceConfigType sja1124_config;

static volatile uint8_t g_intFlag = 0;
static volatile uint8_t uartRxByte = 0;
static volatile uint8_t uartRxBuf[UART_RX_BUF_SIZE];
static volatile uint16_t uartRxHead = 0;
static volatile uint16_t uartRxTail = 0;
static volatile uint8_t uartRxOverflow = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_ICACHE_Init(void);
/* USER CODE BEGIN PFP */
static void MX_SPI3_Init(void);
static void MX_USART1_UART_Init(void);

static int  UART_GetChar(void);
static void UART_PutChar(uint8_t ch);
static void UART_PutString(const char *str);
static void UART_GetLine(char *buf, int maxLen);
static uint32_t UART_GetUint(void);
static void UART_StartReceiveIT(void);

static void SJA1124_Demo_Init(void);
static void SJA1124_Demo_Menu(void);
static void SJA1124_Demo_SendFrame(SJA1124_LinChannelType channel);
static void SJA1124_Demo_ReceiveFrame(SJA1124_LinChannelType channel);
static void SJA1124_Demo_ReadRegisters(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ========================= UART helpers ========================= */

/* Redirect printf to UART1 */
int _write(int file, char *ptr, int len)
{
    (void)file;
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

static void UART_PutChar(uint8_t ch)
{
    HAL_UART_Transmit(&huart1, &ch, 1, HAL_MAX_DELAY);
}

static void UART_PutString(const char *str)
{
    HAL_UART_Transmit(&huart1, (const uint8_t *)str, strlen(str), HAL_MAX_DELAY);
}

static void UART_StartReceiveIT(void)
{
    (void)HAL_UART_Receive_IT(&huart1, (uint8_t *)&uartRxByte, 1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        uint16_t next = (uint16_t)((uartRxHead + 1U) % UART_RX_BUF_SIZE);
        if (next == uartRxTail) {
            uartRxOverflow = 1U;
        } else {
            uartRxBuf[uartRxHead] = uartRxByte;
            uartRxHead = next;
        }
        UART_StartReceiveIT();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF | UART_CLEAR_FEF |
                                     UART_CLEAR_NEF  | UART_CLEAR_PEF);
        UART_StartReceiveIT();
    }
}

static int UART_GetChar(void)
{
    uint32_t startTick = HAL_GetTick();

    while (1) {
        if (uartRxHead != uartRxTail) {
            uint8_t ch = uartRxBuf[uartRxTail];
            uartRxTail = (uint16_t)((uartRxTail + 1U) % UART_RX_BUF_SIZE);
            UART_PutChar(ch); /* echo */
            return (int)ch;
        }

        if (uartRxOverflow) {
            uartRxOverflow = 0U;
            UART_PutString("\r\n[WARN] UART RX overflow\r\n");
        }

        if ((HAL_GetTick() - startTick) > UART_RX_TIMEOUT_MS) {
            return -1;
        }

        if (huart1.RxState == HAL_UART_STATE_READY) {
            UART_StartReceiveIT();
        }
    }
}

static void UART_GetLine(char *buf, int maxLen)
{
    int idx = 0;
    while (idx < maxLen - 1) {
        int ch = UART_GetChar();
        if (ch < 0) continue;
        if (ch == '\r' || ch == '\n') {
            UART_PutString("\r\n");
            break;
        }
        if (ch == '\b' || ch == 127) {
            if (idx > 0) {
                idx--;
                UART_PutString("\b \b");
            }
            continue;
        }
        buf[idx++] = (char)ch;
    }
    buf[idx] = '\0';
}

static uint32_t UART_GetUint(void)
{
    char buf[16];
    UART_GetLine(buf, sizeof(buf));
    uint32_t val = 0;
    for (int i = 0; buf[i] != '\0'; i++) {
        if (buf[i] >= '0' && buf[i] <= '9')
            val = val * 10 + (buf[i] - '0');
    }
    return val;
}

static void UART_FlushStdout(void)
{
    fflush(stdout);
}

static uint8_t UART_GetHexByte(void)
{
    char buf[8];
    UART_GetLine(buf, sizeof(buf));
    uint8_t val = 0;
    for (int i = 0; buf[i] != '\0'; i++) {
        char c = buf[i];
        if (c >= '0' && c <= '9')      val = val * 16 + (c - '0');
        else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
    }
    return val;
}

/* ========================= SJA1124 Demo ========================= */

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == SJA1124_INT_Pin) {
        /* INT_N is active low, so rising edge = interrupt deasserted.
           Use falling edge instead. */
    }
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == SJA1124_INT_Pin) {
        g_intFlag = 1;
    }
}

static void SJA1124_Demo_Init(void)
{
    SJA1124_StatusType st;
    uint8_t deviceId = 0;

    printf("\r\n==================================\r\n");
    printf("  SJA1124 Quad LIN Commander Demo\r\n");
    printf("  STM32H503 + 10MHz Ext. Osc.\r\n");
    printf("==================================\r\n\r\n");

    /* Initialize SPI handle */
    st = SJA1124_SPI_Init(&sja1124_spi, &hspi3, SJA1124_CS_GPIO_Port, SJA1124_CS_Pin);
    if (st != SJA1124_SUCCESS) {
        printf("[ERROR] SPI handle init failed: %d\r\n", st);
        Error_Handler();
    }

    /* Get default config */
    SJA1124_GetDefaultConfig(&sja1124_config, &sja1124_spi);

    /* Enable all second-level interrupts for all channels */
    for (int ch = 0; ch < MAX_LIN_CHANNELS; ch++) {
        SJA1124_SecondLevelIntConfigType *sli =
            &sja1124_config.LinChannelList.LinChannels[ch].LinChannelConfig.SecondLevelIntConfig;
        sli->DataRecepCompleteIntEn = 1;
        sli->DataTransCompleteIntEn = 1;
        sli->BitErrIntEn = 1;
        sli->ChecksumErrIntEn = 1;
        sli->TimeoutIntEn = 1;
        sli->FrameErrIntEn = 1;
        sli->StuckAtZeroIntEn = 1;
    }

    /* Enable top-level controller status/error interrupts */
    for (int ch = 0; ch < MAX_LIN_CHANNELS; ch++) {
        sja1124_config.TopLevelIntConfig.LinGlobalIntConfig[ch].ControllerStatusIntEn = 1;
        sja1124_config.TopLevelIntConfig.LinGlobalIntConfig[ch].ControllerErrIntEn = 1;
    }
    sja1124_config.TopLevelIntConfig.PllInLockIntEn = 1;
    sja1124_config.TopLevelIntConfig.SpiErrIntEn = 1;

    /* Initialize device (power cycle = 1) */
    st = SJA1124_DeviceInit(SJA1124_DEVICE_INDEX, SJA1124_CLK_FREQ_KHZ,
                            &sja1124_config, 1);
    if (st != SJA1124_SUCCESS) {
        printf("[ERROR] SJA1124 DeviceInit failed: %d\r\n", st);
        printf("  Check: SPI wiring, CS pin, 10MHz oscillator\r\n");
        Error_Handler();
    }

    /* Read ID */
    SJA1124_GetDeviceId(SJA1124_DEVICE_INDEX, &deviceId);
    printf("[OK] SJA1124 Device ID: 0x%02X\r\n", deviceId);

    /* Wait for PLL lock */
    HAL_Delay(50);
    uint8_t statusReg = 0;
    SJA1124_DRV_ReadRegister(SJA1124_DEVICE_INDEX, SJA1124_STATUS, &statusReg);
    if (statusReg & SJA1124_STATUS_PLLINLOCK_MASK) {
        printf("[OK] PLL locked (STATUS=0x%02X)\r\n", statusReg);
    } else {
        printf("[WARN] PLL not locked (STATUS=0x%02X), check 10MHz osc.\r\n", statusReg);
    }

    /* Initialize all 4 LIN channels at default baudrate */
    for (int ch = 0; ch < MAX_LIN_CHANNELS; ch++) {
        uint8_t retries = 3;
        do {
            st = SJA1124_ChannelInit(SJA1124_DEVICE_INDEX, LIN_DEFAULT_BAUDRATE, (SJA1124_LinChannelType)ch);
            if (st == SJA1124_SUCCESS) {
                break;
            }
            HAL_Delay(2);
        } while (retries-- > 0);

        if (st != SJA1124_SUCCESS) {
            uint8_t lstate = 0, lcfg1 = 0;
            SJA1124_DRV_ReadRegister(SJA1124_DEVICE_INDEX,
                SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_GS_LSTATE, ch), &lstate);
            SJA1124_DRV_ReadRegister(SJA1124_DEVICE_INDEX,
                SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG1, ch), &lcfg1);
            printf("[ERROR] Channel %d init failed: %d (LSTATE=0x%02X, LCFG1=0x%02X)\r\n",
                   ch + 1, st, lstate, lcfg1);
        } else {
            printf("[OK] LIN Channel %d initialized @ %lu baud\r\n", ch + 1, (unsigned long)LIN_DEFAULT_BAUDRATE);
        }
    }

    printf("\r\n[OK] SJA1124 demo ready.\r\n\r\n");
}

static void SJA1124_Demo_Menu(void)
{
    printf("========== SJA1124 LIN Commander Menu ==========\r\n");
    printf("  1. Send LIN frame (Commander TX)\r\n");
    printf("  2. Request LIN frame (Commander RX)\r\n");
    printf("  3. Send wakeup request\r\n");
    printf("  4. Read device registers\r\n");
    printf("  5. Re-init channel with new baudrate\r\n");
    printf("  6. Check interrupt status\r\n");
    printf("  7. Enter low power mode\r\n");
    printf("  8. Exit low power mode\r\n");
    printf("  0. Show this menu\r\n");
    printf("================================================\r\n");
}

static SJA1124_LinChannelType SelectChannel(void)
{
    printf("Select LIN channel (1-4): ");
    UART_FlushStdout();
    uint32_t ch = UART_GetUint();
    if (ch < 1) ch = 1;
    if (ch > 4) ch = 4;
    return (SJA1124_LinChannelType)(ch - 1);
}

static void SJA1124_Demo_SendFrame(SJA1124_LinChannelType channel)
{
    SJA1124_LinFrameType frame;
    uint8_t txData[8] = {0};
    uint8_t dataLen;
    uint8_t pid;
    uint8_t csType;

    printf("\r\n--- Send LIN Frame (Channel %d) ---\r\n", channel + 1);

    printf("PID (hex, 0x00-0x3F): 0x");
    UART_FlushStdout();
    pid = UART_GetHexByte();
    printf("Data length (1-8): ");
    UART_FlushStdout();
    dataLen = (uint8_t)UART_GetUint();
    if (dataLen < 1) dataLen = 1;
    if (dataLen > 8) dataLen = 8;

    for (int i = 0; i < dataLen; i++) {
        printf("Data[%d] (hex): 0x", i);
        UART_FlushStdout();
        txData[i] = UART_GetHexByte();
    }

    printf("Checksum type (0=Enhanced, 1=Classic): ");
    UART_FlushStdout();
    csType = (uint8_t)UART_GetUint();

    frame.Pid             = pid;
    frame.DataFieldLength = dataLen;
    frame.ResponseDir     = RESPONSE_SEND;
    frame.ChecksumType    = (csType > 0) ? CHECKSUM_CLASSIC : CHECKSUM_ENHANCED;
    frame.Data            = txData;
    frame.Checksum        = SJA1124_CalculateChecksum(frame.ChecksumType, frame.Pid, txData, dataLen);

    printf("Sending frame: PID=0x%02X, Len=%d, CS=0x%02X...\r\n",
           frame.Pid, frame.DataFieldLength, frame.Checksum);

    SJA1124_StatusType st = SJA1124_SendFrame(SJA1124_DEVICE_INDEX, channel, &frame);
    if (st == SJA1124_SUCCESS) {
        HAL_Delay(50);
        SJA1124_LinStatusType ls = SJA1124_GetStatus(SJA1124_DEVICE_INDEX, channel);
        printf("Status: %d ", ls);
        switch (ls) {
            case LIN_TX_OK:           printf("(TX OK)\r\n"); break;
            case LIN_TX_BUSY:         printf("(TX BUSY)\r\n"); break;
            case LIN_TX_ERROR:        printf("(TX ERROR)\r\n"); break;
            case LIN_TX_HEADER_ERROR: printf("(TX HEADER ERROR)\r\n"); break;
            default:                  printf("(OTHER: %d)\r\n", ls); break;
        }
    } else {
        printf("[ERROR] SendFrame failed: %d\r\n", st);
    }
}

static void SJA1124_Demo_ReceiveFrame(SJA1124_LinChannelType channel)
{
    SJA1124_LinFrameType frame;
    uint8_t rxData[8] = {0};
    uint8_t dataLen;
    uint8_t pid;
    uint8_t csType;
    SJA1124_SecondLevelIntType intType;

    printf("\r\n--- Receive LIN Frame (Channel %d) ---\r\n", channel + 1);

    printf("Responder PID (hex, 0x00-0x3F): 0x");
    UART_FlushStdout();
    pid = UART_GetHexByte();
    printf("Expected data length (1-8): ");
    UART_FlushStdout();
    dataLen = (uint8_t)UART_GetUint();
    if (dataLen < 1) dataLen = 1;
    if (dataLen > 8) dataLen = 8;

    printf("Checksum type (0=Enhanced, 1=Classic): ");
    UART_FlushStdout();
    csType = (uint8_t)UART_GetUint();

    frame.Pid             = pid;
    frame.DataFieldLength = dataLen;
    frame.ResponseDir     = RESPONSE_RECEIVE;
    frame.ChecksumType    = (csType > 0) ? CHECKSUM_CLASSIC : CHECKSUM_ENHANCED;
    frame.Data            = rxData;
    frame.Checksum        = 0;

    printf("Sending header, waiting for responder...\r\n");

    SJA1124_StatusType st = SJA1124_SendHeader(SJA1124_DEVICE_INDEX, channel, &frame);
    if (st != SJA1124_SUCCESS) {
        printf("[ERROR] SendHeader failed: %d\r\n", st);
        return;
    }

    /* Wait for response (with timeout) */
    HAL_Delay(100);

    st = SJA1124_GetData(SJA1124_DEVICE_INDEX, channel, &frame, &intType);
    if (st == SJA1124_SUCCESS) {
        printf("[OK] Received data: ");
        for (int i = 0; i < dataLen; i++) {
            printf("0x%02X ", rxData[i]);
        }
        printf("\r\nChecksum: 0x%02X\r\n", frame.Checksum);
    } else {
        printf("[ERROR] GetData failed: %d, intType=%d\r\n", st, intType);
        switch (intType) {
            case INT_TIMEOUT_ERROR:   printf("  -> Timeout (no responder?)\r\n"); break;
            case INT_CHECKSUM_ERROR:  printf("  -> Checksum error\r\n"); break;
            case INT_BIT_ERROR:       printf("  -> Bit error\r\n"); break;
            case INT_FRAME_ERROR:     printf("  -> Frame error\r\n"); break;
            case INT_STUCK_AT_ZERO:   printf("  -> Stuck at zero\r\n"); break;
            default: break;
        }
    }
}

static void SJA1124_Demo_ReadRegisters(void)
{
    uint8_t val;

    printf("\r\n--- SJA1124 Registers ---\r\n");

    SJA1124_DRV_ReadRegister(SJA1124_DEVICE_INDEX, SJA1124_MODE, &val);
    printf("MODE     = 0x%02X\r\n", val);

    SJA1124_DRV_ReadRegister(SJA1124_DEVICE_INDEX, SJA1124_PLLCFG, &val);
    printf("PLLCFG   = 0x%02X\r\n", val);

    SJA1124_DRV_ReadRegister(SJA1124_DEVICE_INDEX, SJA1124_STATUS, &val);
    printf("STATUS   = 0x%02X (PLL_LOCK=%d)\r\n", val,
           (val & SJA1124_STATUS_PLLINLOCK_MASK) ? 1 : 0);

    SJA1124_DRV_ReadRegister(SJA1124_DEVICE_INDEX, SJA1124_INT1, &val);
    printf("INT1     = 0x%02X\r\n", val);

    SJA1124_DRV_ReadRegister(SJA1124_DEVICE_INDEX, SJA1124_INT2, &val);
    printf("INT2     = 0x%02X\r\n", val);

    SJA1124_DRV_ReadRegister(SJA1124_DEVICE_INDEX, SJA1124_INT3, &val);
    printf("INT3     = 0x%02X\r\n", val);

    SJA1124_DRV_ReadRegister(SJA1124_DEVICE_INDEX, SJA1124_LCOM1, &val);
    printf("LCOM1    = 0x%02X\r\n", val);

    SJA1124_GetDeviceId(SJA1124_DEVICE_INDEX, &val);
    printf("ID       = 0x%02X\r\n", val);

    /* Per-channel LCFG and state */
    for (int ch = 0; ch < MAX_LIN_CHANNELS; ch++) {
        SJA1124_DRV_ReadRegister(SJA1124_DEVICE_INDEX,
            SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_CI_LCFG1, ch), &val);
        printf("LIN%d LCFG1 = 0x%02X", ch + 1, val);

        SJA1124_DRV_ReadRegister(SJA1124_DEVICE_INDEX,
            SJA1124_CHAN_ADDR_OFFSET(SJA1124_LIN1_GS_LSTATE, ch), &val);
        printf("  STATE = 0x%02X\r\n", val);
    }
    printf("-------------------------\r\n");
}

static void SJA1124_Demo_CheckInterrupts(void)
{
    SJA1124_TopLevelInterruptsType tli = 0;
    SJA1124_GetTopLevelInterrupts(SJA1124_DEVICE_INDEX, &tli);
    printf("\r\nTop-level interrupts: 0x%06lX\r\n", (unsigned long)tli);

    if (tli) {
        SJA1124_ClearTopLevelInterrupts(SJA1124_DEVICE_INDEX, tli);
        printf("  (cleared)\r\n");
    }

    for (int ch = 0; ch < MAX_LIN_CHANNELS; ch++) {
        SJA1124_SecondLevelIntType intType;
        SJA1124_ProcessSecondLevelInterrupt(SJA1124_DEVICE_INDEX,
                                             (SJA1124_LinChannelType)ch, &intType);
        if (intType != NO_SECOND_LEVEL_INT) {
            printf("  LIN%d: SecondLevel IntType = %d\r\n", ch + 1, intType);
        }
    }

    g_intFlag = 0;
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
  MX_ICACHE_Init();
  /* USER CODE BEGIN 2 */
  MX_USART1_UART_Init();
  MX_SPI3_Init();
    setvbuf(stdout, NULL, _IONBF, 0);
    UART_StartReceiveIT();

  /* Init SJA1124 demo */
  SJA1124_Demo_Init();
  SJA1124_Demo_Menu();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    printf("\r\n> ");
    UART_FlushStdout();
    uint32_t choice = UART_GetUint();

    switch (choice) {
        case 0:
            SJA1124_Demo_Menu();
            break;
        case 1: {
            SJA1124_LinChannelType ch = SelectChannel();
            SJA1124_Demo_SendFrame(ch);
            break;
        }
        case 2: {
            SJA1124_LinChannelType ch = SelectChannel();
            SJA1124_Demo_ReceiveFrame(ch);
            break;
        }
        case 3: {
            SJA1124_LinChannelType ch = SelectChannel();
            SJA1124_SendWakeupRequest(SJA1124_DEVICE_INDEX, ch);
            printf("[OK] Wakeup request sent on channel %d\r\n", ch + 1);
            break;
        }
        case 4:
            SJA1124_Demo_ReadRegisters();
            break;
        case 5: {
            SJA1124_LinChannelType ch = SelectChannel();
            printf("New baudrate (e.g. 9600, 19200): ");
            UART_FlushStdout();
            uint32_t br = UART_GetUint();
            if (br < 1000) br = 19200;
            SJA1124_StatusType st = SJA1124_ChannelInit(SJA1124_DEVICE_INDEX, br, ch);
            if (st == SJA1124_SUCCESS)
                printf("[OK] Channel %d re-initialized @ %lu baud\r\n", ch + 1, (unsigned long)br);
            else
                printf("[ERROR] Channel re-init failed: %d\r\n", st);
            break;
        }
        case 6:
            SJA1124_Demo_CheckInterrupts();
            break;
        case 7:
            SJA1124_EnterLowPowerMode(SJA1124_DEVICE_INDEX);
            printf("[OK] Entered low power mode\r\n");
            break;
        case 8:
            SJA1124_ExitLowPowerMode(SJA1124_DEVICE_INDEX);
            printf("[OK] Exited low power mode, re-init channels...\r\n");
            for (int i = 0; i < MAX_LIN_CHANNELS; i++)
                SJA1124_ChannelInit(SJA1124_DEVICE_INDEX, LIN_DEFAULT_BAUDRATE, (SJA1124_LinChannelType)i);
            break;
        default:
            printf("Unknown option. Press 0 for menu.\r\n");
            break;
    }

    /* Check interrupt flag from EXTI */
    if (g_intFlag) {
        printf("\r\n[INT] SJA1124 interrupt detected!\r\n");
        SJA1124_Demo_CheckInterrupts();
    }
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
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* SJA1124 CS: PA15 - output push-pull, start HIGH (deselected) */
  HAL_GPIO_WritePin(SJA1124_CS_GPIO_Port, SJA1124_CS_Pin, GPIO_PIN_SET);
  GPIO_InitStruct.Pin   = SJA1124_CS_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(SJA1124_CS_GPIO_Port, &GPIO_InitStruct);

  /* SJA1124 INT_N: PD2 - input, falling edge interrupt */
  GPIO_InitStruct.Pin   = SJA1124_INT_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull  = GPIO_PULLUP;
  HAL_GPIO_Init(SJA1124_INT_GPIO_Port, &GPIO_InitStruct);

  /* Enable EXTI2 interrupt for INT_N */
  HAL_NVIC_SetPriority(SJA1124_INT_EXTI_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(SJA1124_INT_EXTI_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
  * @brief SPI3 Initialization Function
  *        SCK=PC10, MISO=PC11, MOSI=PC12, CS=PA15 (software)
  *        SJA1124: CPOL=0, CPHA=1 (SPI Mode 1), 3.90625 MHz
  */
static void MX_SPI3_Init(void)
{
  hspi3.Instance               = SPI3;
  hspi3.Init.Mode              = SPI_MODE_MASTER;
  hspi3.Init.Direction         = SPI_DIRECTION_2LINES;
  hspi3.Init.DataSize          = SPI_DATASIZE_8BIT;
  hspi3.Init.CLKPolarity       = SPI_POLARITY_LOW;
  hspi3.Init.CLKPhase          = SPI_PHASE_2EDGE;
  hspi3.Init.NSS               = SPI_NSS_SOFT;
  hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64; /* ~3.9 MHz @ 250 MHz */
  hspi3.Init.FirstBit          = SPI_FIRSTBIT_MSB;
  hspi3.Init.TIMode            = SPI_TIMODE_DISABLE;
  hspi3.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
  hspi3.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;
  hspi3.Init.NSSPolarity       = SPI_NSS_POLARITY_LOW;
  hspi3.Init.FifoThreshold     = SPI_FIFO_THRESHOLD_01DATA;
  hspi3.Init.MasterSSIdleness  = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi3.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi3.Init.MasterReceiverAutoSusp  = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi3.Init.IOSwap            = SPI_IO_SWAP_DISABLE;
  hspi3.Init.ReadyMasterManagement   = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
  hspi3.Init.ReadyPolarity     = SPI_RDY_POLARITY_HIGH;
  if (HAL_SPI_Init(&hspi3) != HAL_OK) {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
    *        TX=PA2, RX=PA1, 115200 8N1
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance          = USART1;
  huart1.Init.BaudRate     = 115200;
  huart1.Init.WordLength   = UART_WORDLENGTH_8B;
  huart1.Init.StopBits     = UART_STOPBITS_1;
  huart1.Init.Parity       = UART_PARITY_NONE;
  huart1.Init.Mode         = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_RXOVERRUNDISABLE_INIT;
  huart1.AdvancedInit.OverrunDisable = UART_ADVFEATURE_OVERRUN_DISABLE;
  if (HAL_UART_Init(&huart1) != HAL_OK) {
    Error_Handler();
  }
}

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
