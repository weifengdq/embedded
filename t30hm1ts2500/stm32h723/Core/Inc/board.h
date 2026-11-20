/**
 * @file  board.h
 * @brief STM32H723ZGT6 板级定义 (T30HM1TS2500 / S2500 10BASE-T1S 转接板)
 *
 * 接线 (STM32H723ZGT6 <-> T30HM1TS2500, 严格按新板接线表):
 *   PG10 -> CS_N   (GPIO 软件片选, 低有效, 覆盖整帧 SPI 事务)
 *   PG11 -> SCLK   (SPI1_SCK,  AF5)
 *   PG9  <- MISO   (SPI1_MISO, AF5)
 *   PD7  -> MOSI   (SPI1_MOSI, AF5)
 *   PD5  <- IRQ_N  (EXTI5, 下降沿, 模块侧上拉)
 *   PD6  -> RST_N  (GPIO 输出, 高=释放)
 *   PA9  -> USART1_TX (AF7, 115200 8N1 -> STLINK-V3 VCP)
 *   PA10 <- USART1_RX (AF7)
 *
 * 时钟: HSE 25MHz -> PLL1 (M=2,N=44,P=1) = SYSCLK 550MHz / HCLK 275MHz / APB 137.5MHz
 *       PLL2 (M=5,N=80,P=2) = 200MHz -> SPI1 内核时钟 (BR/16=12.5MHz, BR/8=25MHz)
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- S2500 控制引脚 --- */
#define S2500_CS_PORT      GPIOG
#define S2500_CS_PIN       GPIO_PIN_10   /* PG10 CS_N  */
#define S2500_IRQ_PORT     GPIOD
#define S2500_IRQ_PIN      GPIO_PIN_5    /* PD5  IRQ_N */
#define S2500_RST_PORT     GPIOD
#define S2500_RST_PIN      GPIO_PIN_6    /* PD6  RST_N */

/* SPI1 引脚 (AF5) */
#define S2500_SCK_PORT     GPIOG
#define S2500_SCK_PIN      GPIO_PIN_11
#define S2500_MISO_PORT    GPIOG
#define S2500_MISO_PIN     GPIO_PIN_9
#define S2500_MOSI_PORT    GPIOD
#define S2500_MOSI_PIN     GPIO_PIN_7
#define S2500_SPI_AF       GPIO_AF5_SPI1

/* SPI1 内核时钟 = PLL2P = 200MHz
 *   BR/16 -> 12.5MHz (上电默认, 先求稳)
 *   BR/8  -> 25MHz   (S2500 上限, 用 "spi speed 25" 切换)               */
#define S2500_SPI_KERNEL_HZ 200000000u
/* 上电默认 25MHz (实测稳定, 见 README 实测表); 现场怀疑信号完整性时用
 * "spi speed 12" 退到 12.5MHz 做 A/B。 */
#define S2500_SPI_BR_DEFAULT SPI_BAUDRATEPRESCALER_8    /* 25 MHz */
#define S2500_SPI_HZ_DEFAULT 25000000u

/* SPI1 DMA: 任意流都可 (DMAMUX), 这里 RX=DMA1_Stream0, TX=DMA1_Stream1 */
#define S2500_SPI_DMA       1

/* SPI 事务统计 (port.c 累加, shell "spi" 命令打印) */
typedef struct {
    uint32_t xfers;        /* 事务数 (12B 控制 / 68B 数据) */
    uint32_t bytes;        /* 累计字节数 */
    uint32_t dma_xfers;    /* 走 DMA 的事务数 */
    uint32_t dma_bytes;
    uint32_t dma_fallback; /* DMA 失败回退到轮询的次数 */
    uint32_t timeouts;     /* DMA 等 TC 超时次数 */
    uint64_t cycles_total; /* CS 低 -> CS 高累计周期数 (32 位会 25.6s 溢出) */
    uint32_t cycles_last;
    uint32_t cycles_max;
} board_spi_stats_t;

extern volatile board_spi_stats_t g_spi_stats;
extern volatile uint32_t g_spi_dma_dbg[10];

/* 一次 SPI1 全双工传输 (不含 CS_N), 返回 0 成功 */
int      board_spi_transfer(const uint8_t *tx, uint8_t *rx, uint16_t len);
void     board_spi_dma_enable(int on);
int      board_spi_dma_is_enabled(void);
uint32_t board_spi_clock_hz(void);
/* 运行期改 SCLK: hz 会被向下取到最接近的分频档, 返回实际 Hz */
uint32_t board_spi_set_hz(uint32_t hz);

#define BOARD_UART_BAUD     115200u

extern SPI_HandleTypeDef  hspi1;
extern UART_HandleTypeDef huart1;
extern volatile uint32_t  board_irq_count;

/* 运行时状态 (shell "spi" 打印, 排错用) */
extern uint32_t g_board_sysclk_hz;
extern uint32_t g_board_hclk_hz;
extern uint32_t g_board_pclk2_hz;
extern uint32_t g_board_spi_kernel_hz;
extern uint8_t  g_board_hse_ok;      /* 1 = HSE 25MHz 起振, 0 = 退回 HSI 64MHz */
extern uint8_t  g_board_dcache_on;
extern uint8_t  g_board_icache_on;

/* 板级初始化: 时钟/GPIO/SPI1/USART1/DMA/DWT, 不返回错误 (失败则停机) */
void     board_init(void);

/* 调试串口 (USART1, 中断接收 + 轮询发送) */
void     board_uart_write(const char *data, unsigned int len);
int      board_uart_getc(void);
void     board_uart_rx_isr(void);

uint32_t board_millis(void);
void     board_delay_us(uint32_t us);

/* S2500 平台 HAL */
struct s2500_hal_s;
const void *s2500_platform_hal(void);

/* 故障停机: 打印后死循环 (调试器可在此处暂停) */
void board_fatal(const char *msg);

#ifdef __cplusplus
}
#endif
#endif
/* end board.h */
