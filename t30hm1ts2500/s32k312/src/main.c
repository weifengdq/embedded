/**
 * @file  main.c
 * @brief S32K312 + T30HM1TS2500 10BASE-T1S 主程序
 *
 * 流程: 时钟 -> 引脚 -> 中断 -> SysTick -> UART0 -> board_init -> s2500_app_init
 *       -> while(1) { 串口轮询; s2500_app_poll(); 心跳灯 }
 */
#include "Clock_Ip.h"
#include "IntCtrl_Ip.h"
#include "Lpuart_Uart_Ip.h"
#include "Siul2_Port_Ip.h"
#include "OsIf.h"
#include "S32K312_SIUL2.h"

#include "systick.h"
#include "s2500_port.h"
#include "s2500_port_board.h"
#include "s2500_app.h"
#include "s2500_spi.h"

#include <stdint.h>
#include <stdio.h>

#define PIN_PTB10_LED   42u

/* DWT 周期计数器 (120MHz) —— 给主循环提供微秒级节拍 */
#define DWT_CTRL        (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT      (*(volatile uint32_t *)0xE0001004u)
#define DEMCR           (*(volatile uint32_t *)0xE000EDFCu)

/* 主循环轮询周期 (us): 一次 s2500_app_poll 会批量取最多 16 帧,
 * 周期只要远小于一帧的空中时间 (1500B @10Mbit/s = 1.2ms) 就不影响吞吐。 */
#define POLL_PERIOD_US  150u
#define POLL_PERIOD_CYC ((uint32_t)POLL_PERIOD_US * 120u)

/* 运行时可在 shell 里用 "s32k poll <us>" 改 (0 = 每轮都轮询) */
volatile uint32_t g_poll_period_cyc = POLL_PERIOD_CYC;

extern const Siul2_Port_Ip_PinSettingsConfig
    g_pin_mux_InitConfigArr_PortContainer_0_BOARD_InitPeripherals[];
extern const IntCtrl_Ip_CtrlConfigType IntCtrlConfig_0;
extern const Clock_Ip_ClockConfigType Clock_Ip_aClockConfig[];
extern const Lpuart_Uart_Ip_UserConfigType Lpuart_Uart_Ip_xHwConfigPB_0;

int main(void)
{
    uint32_t now;
    uint32_t led_tick;
    uint32_t next_poll;
    s2500_dev_t *dev;
    uint8_t  led = 0u;
    char     spibuf[96];

    setvbuf(stdout, NULL, _IONBF, 0);

    /* 1. 时钟 120MHz */
    Clock_Ip_Init(&Clock_Ip_aClockConfig[0]);

    /* 2. 引脚复用 (LED / UART0 / S2500 RST / IRQ; SPI 引脚由 lpspi_hw 覆盖) */
    Siul2_Port_Ip_Init(NUM_OF_CONFIGURED_PINS_PortContainer_0_BOARD_InitPeripherals,
                       g_pin_mux_InitConfigArr_PortContainer_0_BOARD_InitPeripherals);

    /* 3. 中断 + SysTick 1ms */
    IntCtrl_Ip_Init(&IntCtrlConfig_0);
    SysTick_Init(120000000u);

    /* 4. UART0 115200: RTD 只做波特率配置, 收发器/接收由本工程接管 */
    Lpuart_Uart_Ip_Init(0u, &Lpuart_Uart_Ip_xHwConfigPB_0);
    s2500_port_uart_init();

    /* 5. 板级: SPI + S2500 复位 */
    s2500_board_init();

    s2500_spi_info_str(spibuf, sizeof(spibuf));
    printf("\r\n\r\n==================================================\r\n");
    printf(" S32K312 + T30HM1TS2500 10BASE-T1S\r\n");
    printf(" CORE 120MHz  UART0 115200\r\n");
    printf(" SPI : %s\r\n", spibuf);
    printf("==================================================\r\n");

    /* 6. S2500 + lwIP + shell */
    {
        /* mac = NULL: 读取芯片出厂 MAC */
        s2500_app_cfg_t cfg = {
            .hal         = s2500_port_hal(),
            .mac         = NULL,
            .plca_id     = 3,
            .plca_ncnt   = 8,
            .plca_totmr  = 32,
            .plca_en     = 0,
            .promiscuous = 0,
            .ip          = 0xC0A80A0Du,   /* 192.168.10.13 */
            .mask        = 0xFFFFFF00u,
            .gw          = 0xC0A80A0Au,   /* 192.168.10.10 */
            .board_name  = "S32K312 + T30HM1TS2500",
        };
        (void)s2500_app_init(&cfg);
    }

    dev = s2500_app_dev();

    /* DWT 周期计数器使能 */
    DEMCR |= (1u << 24);
    DWT_CYCCNT = 0u;
    DWT_CTRL |= 1u;

    led_tick = SysTick_GetMs();
    next_poll = DWT_CYCCNT;

    while (1) {
        /* R13: 串口接收轮询 (清 OR/NF/FE/PF, 抽干 FIFO) */
        s2500_port_uart_poll();

        /* 2026-11-20: 空闲时不要每轮都发 12 字节 BUFSTS 控制事务
         * (10MHz SCK 下一次 12B 读 = 9.6us, 每轮都发会吃掉一半 SPI 带宽;
         *  实测 iperf 下行 3.15Mbit/s 时 SPI 原始换算速率已占满 ~80%,
         *  其中大半是这个空转轮询)。 */
        if ((int32_t)(DWT_CYCCNT - next_poll) >= 0) {
            /* 自适应轮询: 上一步真收到包 (rx_polls 没涨) 就立刻再轮询,
             * 保证一边收一边抽干 chunk 不落后于空中速率; 空轮询才退避
             * g_poll_period_cyc, 省下 12 字节 BUFSTS 控制事务的 SPI 带宽。 */
            uint32_t p0 = (dev != NULL) ? dev->stats.rx_polls : 0u;
            s2500_app_poll();
            if ((dev != NULL) && (dev->stats.rx_polls == p0)) {
                next_poll = DWT_CYCCNT;
            } else {
                next_poll = DWT_CYCCNT + g_poll_period_cyc;
            }
        }

        now = SysTick_GetMs();
        if ((now - led_tick) >= 500u) {
            led_tick = now;
            led ^= 1u;
            IP_SIUL2->GPDO42 = led;
        }
    }
}
