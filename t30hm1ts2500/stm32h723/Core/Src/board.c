/**
 * @file  board.c
 * @brief STM32H723ZGT6 板级初始化: 550MHz 时钟 / MPU + D-Cache / SPI1 DMA / USART1 / DWT
 *
 * H7 与 F4 的关键差异 (踩坑点, 全部在本文件处理):
 *   1. D-Cache: SPI DMA 缓冲放在 RAM_D2 (0x30000000) 并由 MPU 配成
 *      Normal non-cacheable, 所以 DMA 与 CPU 之间天然一致; 另外在
 *      传输前后仍做 clean/invalidate, 作为 MPU 配错时的兜底。
 *   2. DMA 请求号走 DMAMUX: SxCR.CHSEL 直接写 SPI1_RX=37 / SPI1_TX=38。
 *   3. 新 SPI IP: 波特率分频在 CFG1.MBR (不是 F4 的 CR1.BR),
 *      DMA 使能在 CFG1.RXDMAEN/TXDMAEN, 传输长度写 CR2.TSIZE,
 *      TX/RX 是分开的 TXDR/RXDR, 结束标志是 SR.TXC/EOT (不是 BSY)。
 *   4. DWT->CYCCNT 必须先置 DEMCR.TRCENA。
 */
#include "board.h"
#include <stdio.h>
#include <string.h>

SPI_HandleTypeDef  hspi1;
UART_HandleTypeDef huart1;
volatile uint32_t  board_irq_count;

uint32_t g_board_sysclk_hz;
uint32_t g_board_hclk_hz;
uint32_t g_board_pclk2_hz;
uint32_t g_board_spi_kernel_hz;
uint8_t  g_board_hse_ok;
uint8_t  g_board_dcache_on;
uint8_t  g_board_icache_on;

/* ------------------------------------------------------------------ */
/* 调试串口接收环形缓冲                                                */
/* ------------------------------------------------------------------ */
#define UART_RX_SIZE 512u
static volatile uint8_t  s_rx[UART_RX_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;

/* ------------------------------------------------------------------ */
/* DWT 周期计数器 (us 延时)                                            */
/* ------------------------------------------------------------------ */
static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   /* 不开这个 CYCCNT 不动 */
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void board_delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000u);
    if (ticks == 0u) {
        ticks = 1u;
    }
    while ((uint32_t)(DWT->CYCCNT - start) < ticks) {
        __NOP();
    }
}

uint32_t board_millis(void)
{
    return HAL_GetTick();
}

static void error_spin(void)
{
    __disable_irq();
    for (;;) { }
}

/* ------------------------------------------------------------------ */
/* MPU: 把 RAM_D2 (0x30000000, 32KB) 配成 Normal non-cacheable         */
/*      SPI DMA 缓冲 (.dma_buffer 段) 就落在这里                        */
/* ------------------------------------------------------------------ */
static void mpu_config(void)
{
    MPU_Region_InitTypeDef mpu;

    memset(&mpu, 0, sizeof(mpu));

    HAL_MPU_Disable();

    mpu.Enable            = MPU_REGION_ENABLE;
    mpu.Number            = MPU_REGION_NUMBER0;
    mpu.BaseAddress       = 0x30000000u;
    mpu.Size              = MPU_REGION_SIZE_32KB;
    mpu.SubRegionDisable  = 0x00;
    /* TEX=1, C=0, B=0 -> Normal memory, non-cacheable (inner/outer) */
    mpu.TypeExtField      = MPU_TEX_LEVEL1;
    mpu.AccessPermission  = MPU_REGION_FULL_ACCESS;
    mpu.DisableExec       = MPU_INSTRUCTION_ACCESS_DISABLE;
    mpu.IsShareable       = MPU_ACCESS_SHAREABLE;
    mpu.IsCacheable       = MPU_ACCESS_NOT_CACHEABLE;
    mpu.IsBufferable      = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&mpu);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static void cache_enable(void)
{
    SCB_EnableICache();
    g_board_icache_on = (SCB->CCR & SCB_CCR_IC_Msk) ? 1u : 0u;
    SCB_EnableDCache();
    g_board_dcache_on = (SCB->CCR & SCB_CCR_DC_Msk) ? 1u : 0u;
}

/* ------------------------------------------------------------------ */
/* 时钟                                                                */
/*   HSE 25MHz -> PLL1 M=2 (12.5MHz) N=44 (550MHz VCO) P=1 -> 550MHz    */
/*   HCLK = 550/2 = 275MHz, APB1/2/3/4 = 137.5MHz, VOS0, FLASH 3WS     */
/*   PLL2  M=5 (5MHz) N=80 (400MHz VCO) P=2 -> 200MHz = SPI1 内核时钟  */
/* ------------------------------------------------------------------ */
static int clock_pll_on(uint32_t hse_hz, int use_hse)
{
    RCC_OscInitTypeDef osc;
    RCC_ClkInitTypeDef clk;
    RCC_PeriphCLKInitTypeDef pclk;

    memset(&osc, 0, sizeof(osc));
    memset(&clk, 0, sizeof(clk));
    memset(&pclk, 0, sizeof(pclk));

    if (use_hse) {
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
        osc.HSEState       = RCC_HSE_ON;
        osc.PLL.PLLM       = 2;      /* 25MHz / 2 = 12.5MHz */
        osc.PLL.PLLRGE     = RCC_PLL1VCIRANGE_3;   /* 8..16MHz */
        pclk.PLL2.PLL2M    = 5;      /* 25MHz / 5 = 5MHz    */
        pclk.PLL2.PLL2RGE  = RCC_PLL2VCIRANGE_2;   /* 4..8MHz     */
    } else {
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        osc.HSIState       = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        osc.PLL.PLLM       = 8;      /* 64MHz / 8 = 8MHz */
        osc.PLL.PLLRGE     = RCC_PLL1VCIRANGE_3;
        pclk.PLL2.PLL2M    = 8;      /* 64MHz / 8 = 8MHz */
        pclk.PLL2.PLL2RGE  = RCC_PLL2VCIRANGE_3;
    }
    (void)hse_hz;

    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = use_hse ? RCC_PLLSOURCE_HSE : RCC_PLLSOURCE_HSI;
    osc.PLL.PLLN       = 44;
    osc.PLL.PLLP       = 1;          /* 550MHz / 544MHz */
    osc.PLL.PLLQ       = 4;
    osc.PLL.PLLR       = 2;
    osc.PLL.PLLVCOSEL  = RCC_PLL1VCOWIDE;
    osc.PLL.PLLFRACN   = 0;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return -1;
    }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                         RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_D3PCLK1;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    clk.AHBCLKDivider  = RCC_HCLK_DIV2;      /* 275MHz */
    clk.APB1CLKDivider = RCC_APB1_DIV2;      /* 137.5MHz */
    clk.APB2CLKDivider = RCC_APB2_DIV2;
    clk.APB3CLKDivider = RCC_APB3_DIV2;
    clk.APB4CLKDivider = RCC_APB4_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_3) != HAL_OK) {
        return -1;
    }

    /* PLL2 -> SPI1 内核时钟 */
    pclk.PeriphClockSelection   = RCC_PERIPHCLK_SPI1;
    pclk.PLL2.PLL2N             = 80;    /* 400MHz VCO */
    pclk.PLL2.PLL2P             = 2;     /* 200MHz */
    pclk.PLL2.PLL2Q             = 4;
    pclk.PLL2.PLL2R             = 2;
    pclk.PLL2.PLL2VCOSEL        = RCC_PLL2VCOWIDE;
    pclk.PLL2.PLL2FRACN         = 0;
    pclk.Spi123ClockSelection   = RCC_SPI123CLKSOURCE_PLL2;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) {
        return -1;
    }
    return 0;
}

static void clock_config(void)
{
    /* H72x: PWR 没有独立时钟门控, 不需要 __HAL_RCC_PWR_CLK_ENABLE() */
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

    /* 550MHz 必须 VOS0 (VOS1 最高 400MHz) */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while (__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY) == 0u) { }

    if (clock_pll_on(25000000u, 1) == 0) {
        g_board_hse_ok = 1;
    } else {
        /* 板上 25MHz 晶振不起振: 退回 HSI 64MHz, 频率略低但能起来 */
        g_board_hse_ok = 0;
        if (clock_pll_on(0u, 0) != 0) {
            error_spin();
        }
    }

    g_board_sysclk_hz     = HAL_RCC_GetSysClockFreq();
    g_board_hclk_hz       = HAL_RCC_GetHCLKFreq();
    g_board_pclk2_hz      = HAL_RCC_GetPCLK2Freq();
    g_board_spi_kernel_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI1);
}

/* ------------------------------------------------------------------ */
/* GPIO                                                                */
/* ------------------------------------------------------------------ */
static void gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    memset(&gpio, 0, sizeof(gpio));

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* PG10 = CS_N, 默认拉高 (软件片选) */
    HAL_GPIO_WritePin(S2500_CS_PORT, S2500_CS_PIN, GPIO_PIN_SET);
    gpio.Pin   = S2500_CS_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(S2500_CS_PORT, &gpio);

    /* PD6 = RST_N, 默认拉高 (释放复位) */
    HAL_GPIO_WritePin(S2500_RST_PORT, S2500_RST_PIN, GPIO_PIN_SET);
    gpio.Pin   = S2500_RST_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(S2500_RST_PORT, &gpio);

    /* PD5 = IRQ_N, 上拉输入 + 下降沿中断 */
    gpio.Pin   = S2500_IRQ_PIN;
    gpio.Mode  = GPIO_MODE_IT_FALLING;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(S2500_IRQ_PORT, &gpio);
    /* H7 的 EXTI5..9 共用一个向量 */
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

    /* SPI1: PG11 SCK / PG9 MISO / PD7 MOSI, AF5, 高速 */
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = S2500_SPI_AF;

    gpio.Pin = S2500_SCK_PIN;
    HAL_GPIO_Init(S2500_SCK_PORT, &gpio);
    gpio.Pin = S2500_MISO_PIN;
    HAL_GPIO_Init(S2500_MISO_PORT, &gpio);
    gpio.Pin = S2500_MOSI_PIN;
    HAL_GPIO_Init(S2500_MOSI_PORT, &gpio);
}

/* ------------------------------------------------------------------ */
/* USART1: 115200 8N1, 中断接收 (FIFO 模式)                            */
/* ------------------------------------------------------------------ */
static void uart_init(void)
{
    GPIO_InitTypeDef gpio;

    memset(&gpio, 0, sizeof(gpio));

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin       = GPIO_PIN_9 | GPIO_PIN_10;   /* PA9 TX, PA10 RX */
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart1.Instance                    = USART1;
    huart1.Init.BaudRate               = BOARD_UART_BAUD;
    huart1.Init.WordLength             = UART_WORDLENGTH_8B;
    huart1.Init.StopBits               = UART_STOPBITS_1;
    huart1.Init.Parity                 = UART_PARITY_NONE;
    huart1.Init.Mode                   = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart1.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    huart1.FifoMode                    = UART_FIFOMODE_ENABLE;
    huart1.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        error_spin();
    }

    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

void board_uart_rx_isr(void)
{
    uint32_t isr = USART1->ISR;

    if ((isr & (USART_ISR_RXNE_RXFNE | USART_ISR_ORE | USART_ISR_FE |
                USART_ISR_NE | USART_ISR_PE)) == 0u) {
        return;
    }
    {
        uint8_t c = (uint8_t)(USART1->RDR & 0xFFu);   /* 读 RDR 清 RXNE/FE/NE/PE */
        if ((isr & USART_ISR_RXNE_RXFNE) != 0u) {
            uint16_t next = (uint16_t)((s_rx_head + 1u) % UART_RX_SIZE);
            if (next != s_rx_tail) {
                s_rx[s_rx_head] = c;
                s_rx_head = next;
            }
        }
    }
    if ((isr & USART_ISR_ORE) != 0u) {
        USART1->ICR = USART_ICR_ORECF;
    }
}

void board_uart_write(const char *data, unsigned int len)
{
    unsigned int i;
    for (i = 0; i < len; i++) {
        while ((USART1->ISR & USART_ISR_TXE_TXFNF) == 0u) { }
        USART1->TDR = (uint8_t)data[i];
    }
    while ((USART1->ISR & USART_ISR_TC) == 0u) { }
}

int board_uart_getc(void)
{
    if (s_rx_head == s_rx_tail) {
        return -1;
    }
    {
        uint8_t c = s_rx[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1u) % UART_RX_SIZE);
        return (int)c;
    }
}

/* newlib printf 重定向 */
int __io_putchar(int ch)
{
    char c = (char)ch;
    board_uart_write(&c, 1);
    return ch;
}

/* ------------------------------------------------------------------ */
/* SPI1: 模式 0, MSB first, 内核时钟 PLL2P=200MHz                      */
/* ------------------------------------------------------------------ */
static void spi_init(void)
{
    __HAL_RCC_SPI1_CLK_ENABLE();

    hspi1.Instance                        = SPI1;
    hspi1.Init.Mode                       = SPI_MODE_MASTER;
    hspi1.Init.Direction                  = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize                   = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity                = SPI_POLARITY_LOW;    /* CPOL = 0 */
    hspi1.Init.CLKPhase                   = SPI_PHASE_1EDGE;     /* CPHA = 0 -> 模式 0 */
    hspi1.Init.NSS                        = SPI_NSS_SOFT;        /* CS_N 由 PG10 控制 */
    hspi1.Init.BaudRatePrescaler          = S2500_SPI_BR_DEFAULT;
    hspi1.Init.FirstBit                   = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode                     = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation             = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial              = 0;
    hspi1.Init.CRCLength                  = SPI_CRC_LENGTH_4BIT;
    hspi1.Init.NSSPMode                   = SPI_NSS_PULSE_DISABLE;
    hspi1.Init.NSSPolarity                = SPI_NSS_POLARITY_LOW;
    hspi1.Init.FifoThreshold              = SPI_FIFO_THRESHOLD_01DATA;
    hspi1.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    hspi1.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    hspi1.Init.MasterSSIdleness           = SPI_MASTER_SS_IDLENESS_00CYCLE;
    hspi1.Init.MasterInterDataIdleness    = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    hspi1.Init.MasterReceiverAutoSusp     = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    hspi1.Init.MasterKeepIOState          = SPI_MASTER_KEEP_IO_STATE_DISABLE;
    hspi1.Init.IOSwap                     = SPI_IO_SWAP_DISABLE;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        error_spin();
    }
    /* HAL_SPI_Init 结束时 SPE 状态随版本不同, 这里明确关掉, 由每次事务自己打开 */
    __HAL_SPI_DISABLE(&hspi1);
}

/* ------------------------------------------------------------------ */
/* SPI1 DMA                                                            */
/*                                                                     */
/* 裸机 DMA (不经 HAL 的 DMA 状态机/中断): 一次事务 = 拉低 CS -> 配 TSIZE */
/* -> 开 RX/TX 两条流 -> 开 SPE -> 轮询 DMA TC -> 等 SPI TXC -> 拉高 CS。*/
/* common/s2500 的同步 spi_xfer 语义完全不变, 但去掉 HAL 逐字节等 TXE/  */
/* RXNE 的开销, 68B 事务接近 SPI 线速。                                 */
/*                                                                     */
/* 缓冲区刻意放在 .dma_buffer 段 (RAM_D2, MPU 非缓存), 不需要 clean/    */
/* invalidate; 仍然各做一次, 作为 MPU 配错时的兜底。                     */
/* ------------------------------------------------------------------ */
#define SPI_DMA_RX_STREAM   DMA1_Stream0
#define SPI_DMA_TX_STREAM   DMA1_Stream1
/* H7: LISR 管 stream0..3, HISR 管 stream4..7; 两条流都在 LISR 里 */
#define SPI_DMA_RX_TC       DMA_LISR_TCIF0
#define SPI_DMA_TX_TC       DMA_LISR_TCIF1
#define SPI_DMA_LIFCR_ALL   (0x3Du | (0x3Du << 6))   /* stream0 + stream1 */
/* H7 的 DMA 请求号不在 SxCR.CHSEL 里, 而是写 DMAMUX 通道的 CCR */
#define SPI_DMA_RX_MUX      DMAMUX1_Channel0
#define SPI_DMA_TX_MUX      DMAMUX1_Channel1

volatile board_spi_stats_t g_spi_stats;
volatile uint32_t g_spi_dma_dbg[10];

#define S2500_XFER_MAX 96u
static uint8_t s_tx_stage[S2500_XFER_MAX] __attribute__((aligned(32), section(".dma_buffer")));
static uint8_t s_rx_stage[S2500_XFER_MAX] __attribute__((aligned(32), section(".dma_buffer")));

static uint32_t s_rx_cr;
static uint32_t s_tx_cr;
static volatile int s_spi_dma_on = S2500_SPI_DMA;

static void spi_dma_init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    SPI_DMA_RX_STREAM->CR = 0u;
    SPI_DMA_TX_STREAM->CR = 0u;
    while ((SPI_DMA_RX_STREAM->CR & DMA_SxCR_EN) || (SPI_DMA_TX_STREAM->CR & DMA_SxCR_EN)) { }
    DMA1->LIFCR = SPI_DMA_LIFCR_ALL;

    /* H7: DMAMUX 通道 CCR 选请求号 (SPI1_RX=37, SPI1_TX=38);
     * SxCR 里只配 8bit/8bit + 内存自增 + 高优先级 */
    SPI_DMA_RX_MUX->CCR = (uint32_t)DMA_REQUEST_SPI1_RX;
    SPI_DMA_TX_MUX->CCR = (uint32_t)DMA_REQUEST_SPI1_TX;

    s_rx_cr = DMA_SxCR_MINC | DMA_SxCR_PL_1;
    s_tx_cr = DMA_SxCR_MINC | DMA_SxCR_PL_1 | DMA_SxCR_DIR_0;   /* 内存 -> 外设 */

    SPI_DMA_RX_STREAM->CR = s_rx_cr;
    SPI_DMA_TX_STREAM->CR = s_tx_cr;
    /* 新 SPI IP: TX/RX 是分开的寄存器 */
    SPI_DMA_RX_STREAM->PAR = (uint32_t)&SPI1->RXDR;
    SPI_DMA_TX_STREAM->PAR = (uint32_t)&SPI1->TXDR;
}

static int spi_dma_xfer(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    uint32_t t0;
    int ok = 0;

    /* CS 低到这里: SPI 处于关闭态, 可以先清干净上一事务的残留 */
    __HAL_SPI_DISABLE(&hspi1);
    {
        uint32_t guard = 16u;
        while (((SPI1->SR & SPI_SR_RXP) != 0u) && (guard-- > 0u)) {
            (void)SPI1->RXDR;              /* 丢掉残留数据, 否则 RX 会整体错位 */
        }
    }
    SPI1->IFCR = SPI_IFCR_EOTC  | SPI_IFCR_TXTFC | SPI_IFCR_UDRC |
                 SPI_IFCR_OVRC  | SPI_IFCR_MODFC | SPI_IFCR_CRCEC |
                 SPI_IFCR_TIFREC | SPI_IFCR_SUSPC | SPI_IFCR_TSERFC;

    /* 本事务长度: 新 SPI IP 必须写 TSIZE, 否则主机不产生时钟 */
    MODIFY_REG(SPI1->CR2, SPI_CR2_TSIZE, (uint32_t)len);
    SPI1->CFG1 |= (SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);   /* SPE=0 时改 CFG1 */

    /* 两条流先就位, 再开 SPE, 避免首字节溢出 */
    SPI_DMA_RX_STREAM->M0AR = (uint32_t)rx;
    SPI_DMA_RX_STREAM->NDTR = len;
    SPI_DMA_TX_STREAM->M0AR = (uint32_t)tx;
    SPI_DMA_TX_STREAM->NDTR = len;
    DMA1->LIFCR = SPI_DMA_LIFCR_ALL;

    SCB_CleanDCache_by_Addr((uint32_t *)tx, (int32_t)len);   /* 兜底: MPU 若非缓存则无操作 */

    SPI_DMA_RX_STREAM->CR |= DMA_SxCR_EN;
    SPI_DMA_TX_STREAM->CR |= DMA_SxCR_EN;
    __DSB();
    __HAL_SPI_ENABLE(&hspi1);
    /* 新 SPI IP (H7) 与 F4 最大的不同: 光 SPE=1 不会产生时钟,
     * 必须再置 CR1.CSTART, 否则 TX DMA 只把数据灌进 16 字节 FIFO 就"完成",
     * RX 收不到一个字节。这是 H7 移植最容易踩的坑。 */
    SPI1->CR1 |= SPI_CR1_CSTART;
    __DSB();

    t0 = DWT->CYCCNT;
    while (((DMA1->LISR & SPI_DMA_RX_TC) == 0u) ||
           ((DMA1->LISR & SPI_DMA_TX_TC) == 0u)) {
        if ((uint32_t)(DWT->CYCCNT - t0) > (SystemCoreClock / 1000u)) {   /* 1 ms */
            g_spi_stats.timeouts++;
            g_spi_dma_dbg[0] = DMA1->LISR;
            g_spi_dma_dbg[1] = DMA1->LISR;
            g_spi_dma_dbg[2] = SPI1->SR;
            g_spi_dma_dbg[3] = SPI1->CFG1;
            g_spi_dma_dbg[4] = SPI_DMA_RX_STREAM->NDTR;
            g_spi_dma_dbg[5] = SPI_DMA_TX_STREAM->NDTR;
            g_spi_dma_dbg[6] = SPI_DMA_RX_STREAM->CR;
            g_spi_dma_dbg[7] = SPI_DMA_TX_STREAM->CR;
            g_spi_dma_dbg[8] = SPI1->CR2;
            g_spi_dma_dbg[9] = SystemCoreClock;
            break;
        }
    }
    if ((DMA1->LISR & SPI_DMA_RX_TC) && (DMA1->LISR & SPI_DMA_TX_TC)) {
        ok = 1;
    }

    /* 等最后一帧真正移出 (新 SPI IP: EOT/TXC, 不是 F4 的 BSY) */
    {
        uint32_t t1 = DWT->CYCCNT;
        while ((SPI1->SR & (SPI_SR_EOT | SPI_SR_TXC)) != (SPI_SR_EOT | SPI_SR_TXC)) {
            if ((uint32_t)(DWT->CYCCNT - t1) > (SystemCoreClock / 1000u)) {
                if (ok) {
                    g_spi_stats.timeouts++;
                    g_spi_dma_dbg[2] = SPI1->SR;
                }
                ok = 0;
                break;
            }
        }
    }

    SPI1->CR1 &= ~SPI_CR1_CSTART;
    __HAL_SPI_DISABLE(&hspi1);
    SPI1->CFG1 &= ~(SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);
    SPI_DMA_TX_STREAM->CR &= ~DMA_SxCR_EN;
    SPI_DMA_RX_STREAM->CR &= ~DMA_SxCR_EN;
    while ((SPI_DMA_RX_STREAM->CR & DMA_SxCR_EN) || (SPI_DMA_TX_STREAM->CR & DMA_SxCR_EN)) { }
    DMA1->LIFCR = SPI_DMA_LIFCR_ALL;

    SCB_InvalidateDCache_by_Addr((uint32_t *)rx, (int32_t)len);

    return ok ? 0 : -1;
}

int board_spi_transfer(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    if (len == 0u || len > S2500_XFER_MAX) {
        return -1;
    }

    if (s_spi_dma_on) {
        if (tx != NULL) {
            memcpy(s_tx_stage, tx, len);
        } else {
            memset(s_tx_stage, 0, len);
        }
        if (spi_dma_xfer(s_tx_stage, s_rx_stage, len) == 0) {
            if (rx != NULL) {
                memcpy(rx, s_rx_stage, len);
            }
            g_spi_stats.dma_xfers++;
            g_spi_stats.dma_bytes += len;
            return 0;
        }
        /* DMA 出错: 关掉相关位, 本次直接用轮询, 并永久回退 (现场更稳) */
        g_spi_stats.dma_fallback++;
        s_spi_dma_on = 0;
        __HAL_SPI_DISABLE(&hspi1);
        SPI1->CFG1 &= ~(SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);
        (void)SPI1->RXDR;
    }
    return (HAL_SPI_TransmitReceive(&hspi1, (uint8_t *)tx, rx, len, 20u) == HAL_OK) ? 0 : -1;
}

void board_spi_dma_enable(int on)
{
    s_spi_dma_on = on ? 1 : 0;
}

int board_spi_dma_is_enabled(void)
{
    return s_spi_dma_on;
}

uint32_t board_spi_clock_hz(void)
{
    uint32_t br = (SPI1->CFG1 & SPI_CFG1_MBR) >> SPI_CFG1_MBR_Pos;
    uint32_t kclk = g_board_spi_kernel_hz ? g_board_spi_kernel_hz
                                          : HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI1);
    return kclk >> (br + 1u);
}

uint32_t board_spi_set_hz(uint32_t hz)
{
    uint32_t kclk = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI1);
    uint32_t br;

    for (br = 0; br <= 7u; br++) {
        if ((kclk >> (br + 1u)) <= hz) {
            break;
        }
    }
    if (br > 7u) {
        br = 7u;
    }
    __HAL_SPI_DISABLE(&hspi1);
    MODIFY_REG(SPI1->CFG1, SPI_CFG1_MBR, br << SPI_CFG1_MBR_Pos);
    hspi1.Init.BaudRatePrescaler = br << SPI_CFG1_MBR_Pos;
    return kclk >> (br + 1u);
}

/* ------------------------------------------------------------------ */
/* 板级入口                                                            */
/* ------------------------------------------------------------------ */
void board_init(void)
{
    mpu_config();          /* 先定非缓存区, 再开 D-Cache */
    cache_enable();

    HAL_Init();
    clock_config();
    gpio_init();
    uart_init();
    spi_init();
    spi_dma_init();
    dwt_init();

    /* 给 S2500 一个复位低脉冲: PD6 低 2ms -> 高, 再等 5ms 让芯片完成 boot */
    HAL_GPIO_WritePin(S2500_RST_PORT, S2500_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(2);
    HAL_GPIO_WritePin(S2500_RST_PORT, S2500_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(5);
}

void board_fatal(const char *msg)
{
    board_uart_write("\r\n[FATAL] ", 9);
    board_uart_write(msg, (unsigned int)strlen(msg));
    board_uart_write("\r\n", 2);
    __disable_irq();
    for (;;) { }
}
