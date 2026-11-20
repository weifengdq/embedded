/**
 * @file  board.c
 * @brief TC387 TriBoard 板级支持: ASCLIN0 调试串口 / QSPI2 / GPIO / STM
 *
 * 接线:
 *   P15.2 CS_N   P15.4 RST_N  P15.5 IRQ_N
 *   P15.6 MOSI   P15.7 MISO   P15.8 SCLK
 *   P14.0 ASCLIN0_TX  P14.1 ASCLIN0_RX  (DAP MiniWiggler 虚拟串口)
 */
#include "board.h"
#include "s2500_port.h"

#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxCpu_Irq.h"
#include "IfxScuWdt.h"
#include "IfxScuRcu.h"
#include "IfxPort.h"
#include "IfxAsclin_Asc.h"
#include "IfxQspi_SpiMaster.h"
#include "IfxStm.h"
#include "IfxDma.h"
#include "IfxScuCcu.h"
#include <string.h>
#include "shell.h"
#include "s2500_shell.h"

/* ------------------------------------------------------------------ */
/* 调试串口 ASCLIN0                                                    */
/* ------------------------------------------------------------------ */
#define UART_TX_PRIO    29
#define UART_RX_PRIO    28
#define UART_BUF_SIZE   256

static IfxAsclin_Asc g_asc;
static uint8 g_uart_tx[UART_BUF_SIZE + sizeof(Ifx_Fifo) + 8];
static uint8 g_uart_rx[UART_BUF_SIZE + sizeof(Ifx_Fifo) + 8];

IFX_INTERRUPT(uart_tx_isr, 0, UART_TX_PRIO);
void uart_tx_isr(void)
{
    IfxAsclin_Asc_isrTransmit(&g_asc);
}

IFX_INTERRUPT(uart_rx_isr, 0, UART_RX_PRIO);
void uart_rx_isr(void)
{
    IfxAsclin_Asc_isrReceive(&g_asc);
}

static void uart_init(void)
{
    IfxAsclin_Asc_Config cfg;
    memset(&g_asc, 0, sizeof(g_asc));
    memset(g_uart_tx, 0, sizeof(g_uart_tx));
    memset(g_uart_rx, 0, sizeof(g_uart_rx));
    IfxAsclin_Asc_initModuleConfig(&cfg, &MODULE_ASCLIN0);

    cfg.baudrate.baudrate = 115200.0f;
    cfg.baudrate.oversampling = IfxAsclin_OversamplingFactor_16;
    cfg.bitTiming.medianFilter = IfxAsclin_SamplesPerBit_three;
    cfg.bitTiming.samplePointPosition = IfxAsclin_SamplePointPosition_12;

    cfg.interrupt.txPriority = UART_TX_PRIO;
    cfg.interrupt.rxPriority = UART_RX_PRIO;
    cfg.interrupt.typeOfService = IfxCpu_Irq_getTos(0);

    cfg.txBuffer = g_uart_tx;
    cfg.txBufferSize = UART_BUF_SIZE;
    cfg.rxBuffer = g_uart_rx;
    cfg.rxBufferSize = UART_BUF_SIZE;

    static const IfxAsclin_Asc_Pins pins = {
        NULL_PTR,                     IfxPort_InputMode_pullUp,   /* CTS 不用 */
        &IfxAsclin0_RXA_P14_1_IN,     IfxPort_InputMode_pullUp,   /* RX  */
        NULL_PTR,                     IfxPort_OutputMode_pushPull,/* RTS 不用 */
        &IfxAsclin0_TX_P14_0_OUT,     IfxPort_OutputMode_pushPull,/* TX  */
        IfxPort_PadDriver_cmosAutomotiveSpeed1
    };
    cfg.pins = &pins;

    IfxAsclin_Asc_initModule(&g_asc, &cfg);
}

void board_uart_write(const char *data, unsigned int len)
{
    /* 轮询写 TX FIFO: 不依赖 TX 中断, 115200 下逐字节约 87us */
    unsigned int i;
    for (i = 0; i < len; i++) {
        while (IfxAsclin_getTxFifoFillLevel(&MODULE_ASCLIN0) >= 8) {
        }
        IfxAsclin_writeTxData(&MODULE_ASCLIN0, (uint8)data[i]);
    }
}

int board_uart_getc(void)
{
    uint8 c;
    Ifx_SizeT n = 1;

    if (IfxAsclin_Asc_getReadCount(&g_asc) <= 0) {
        return -1;
    }
    if (IfxAsclin_Asc_read(&g_asc, &c, &n, TIME_NULL)) {
        return (int)c;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* QSPI2 -> S2500                                                      */
/* ------------------------------------------------------------------ */
/* QSPI2 25MHz + DMA (DAVE 风格: DMA 通道中断代替逐字节 FIFO 中断)
 *  - 25MHz 是 S2500 允许的上限 (VIO=3.3V, tclkp>=40ns)
 *  - TX/RX 各一条 DMA 通道, CPU 不再逐字节搬 FIFO
 *  - 每 68B 数据块只有 1 次 TX 完成 + 1 次 RX 完成中断 */
#define QSPI_TX_PRIO    110
#define QSPI_RX_PRIO    111
#define QSPI_ER_PRIO    112
#define QSPI_BAUDRATE   25000000u
#define QSPI_TX_DMA_CH  IfxDma_ChannelId_12
#define QSPI_RX_DMA_CH  IfxDma_ChannelId_13

volatile uint32 g_qspi_tx_irq, g_qspi_rx_irq, g_qspi_er_irq;
static IfxQspi_SpiMaster g_qspi;
static IfxQspi_SpiMaster_Channel g_qspi_ch;

/* DMA 模式下这两个中断来自 DMA 通道 (SRPN=通道号), 不是 QSPI 的 FIFO 中断 */
IFX_INTERRUPT(qspi2_tx_isr, 0, QSPI_TX_PRIO);
void qspi2_tx_isr(void)
{
    g_qspi_tx_irq++;
    IfxQspi_SpiMaster_isrDmaTransmit(&g_qspi);
}

IFX_INTERRUPT(qspi2_rx_isr, 0, QSPI_RX_PRIO);
void qspi2_rx_isr(void)
{
    g_qspi_rx_irq++;
    IfxQspi_SpiMaster_isrDmaReceive(&g_qspi);
}

IFX_INTERRUPT(qspi2_er_isr, 0, QSPI_ER_PRIO);
void qspi2_er_isr(void)
{
    g_qspi_er_irq++;
    IfxQspi_SpiMaster_isrError(&g_qspi);
}

static void qspi_init(void)
{
    memset(&g_qspi, 0, sizeof(g_qspi));
    memset(&g_qspi_ch, 0, sizeof(g_qspi_ch));
    g_qspi_tx_irq = g_qspi_rx_irq = g_qspi_er_irq = 0;
    static const IfxQspi_SpiMaster_Pins qspi_pins = {
        &IfxQspi2_SCLK_P15_8_OUT,  IfxPort_OutputMode_pushPull,
        &IfxQspi2_MTSR_P15_6_OUT,  IfxPort_OutputMode_pushPull,
        &IfxQspi2_MRSTB_P15_7_IN,  IfxPort_InputMode_pullDown,
        IfxPort_PadDriver_cmosAutomotiveSpeed1
    };
    static const IfxQspi_SpiMaster_Output slso = {
        &IfxQspi2_SLSO0_P15_2_OUT, IfxPort_OutputMode_pushPull,
        IfxPort_PadDriver_cmosAutomotiveSpeed1
    };

    IfxQspi_SpiMaster_Config cfg;
    IfxQspi_SpiMaster_initModuleConfig(&cfg, &MODULE_QSPI2);
    cfg.maximumBaudrate = QSPI_BAUDRATE;
    cfg.pins = &qspi_pins;
    cfg.isrProvider = IfxSrc_Tos_cpu0;
    cfg.txPriority = QSPI_TX_PRIO;      /* = DMA TX 通道中断优先级 */
    cfg.rxPriority = QSPI_RX_PRIO;      /* = DMA RX 通道中断优先级 */
    cfg.erPriority = QSPI_ER_PRIO;
    cfg.dma.useDma = TRUE;
    cfg.dma.txDmaChannelId = QSPI_TX_DMA_CH;
    cfg.dma.rxDmaChannelId = QSPI_RX_DMA_CH;
    IfxQspi_SpiMaster_initModule(&g_qspi, &cfg);

    IfxQspi_SpiMaster_ChannelConfig ch;
    IfxQspi_SpiMaster_initChannelConfig(&ch, &g_qspi);
    ch.ch.baudrate = QSPI_BAUDRATE;
    ch.ch.mode.autoCS = 0;   /* CS 由软件在整帧内保持低 */
    ch.ch.mode.shiftClock = IfxQspi_ShiftClock_shiftTransmitDataOnTrailingEdge; /* 模式 0 */
    ch.sls.output = slso;
    IfxQspi_SpiMaster_initChannel(&g_qspi_ch, &ch);
}

/* ------------------------------------------------------------------ */
/* GPIO: RST_N / IRQ_N                                                 */
/* ------------------------------------------------------------------ */
static void gpio_init(void)
{
    /* P15.4 RST_N, 默认高 (不复位) */
    IfxPort_setPinModeOutput(&MODULE_P15, 4, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinHigh(&MODULE_P15, 4);

    /* P15.5 IRQ_N, 上拉输入 */
    IfxPort_setPinModeInput(&MODULE_P15, 5, IfxPort_InputMode_pullUp);
}

static void s2500_hw_reset(void)
{
    IfxPort_setPinLow(&MODULE_P15, 4);
    board_delay_us(10000);
    IfxPort_setPinHigh(&MODULE_P15, 4);
    board_delay_us(50000);
}

/* ------------------------------------------------------------------ */
/* 时基 STM0                                                           */
/* ------------------------------------------------------------------ */
uint32_t board_millis(void)
{
    uint32 freq = (uint32)IfxStm_getFrequency(&MODULE_STM0);
    uint64 ticks = IfxStm_get(&MODULE_STM0);
    if (freq == 0u) {
        return 0u;
    }
    return (uint32_t)((ticks * 1000ull) / (uint64)freq);
}

void board_delay_us(uint32_t us)
{
    uint32 freq = (uint32)IfxStm_getFrequency(&MODULE_STM0);
    uint64 ticks;
    uint32 t0;

    if (freq == 0u || us == 0u) {
        return;
    }
    ticks = ((uint64)freq * (uint64)us) / 1000000ull;
    t0 = (uint32)IfxStm_get(&MODULE_STM0);
    while ((uint32)((uint32)IfxStm_get(&MODULE_STM0) - t0) < (uint32)ticks) {
    }
}

void board_reboot(void)
{
    IfxScuRcu_performReset(IfxScuRcu_ResetType_system, 0);
}

/* ------------------------------------------------------------------ */
/* S2500 HAL                                                            */
/* ------------------------------------------------------------------ */
static int hal_spi_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    uint32 guard = 2000000u;

    (void)ctx;
    if (IfxQspi_SpiMaster_exchange(&g_qspi_ch, tx, rx, (Ifx_SizeT)len) != IfxQspi_Status_ok) {
        return -1;
    }
    while (IfxQspi_SpiMaster_getStatus(&g_qspi_ch) == IfxQspi_Status_busy) {
        if (--guard == 0u) {
            return -1;
        }
    }
    return 0;
}

static void hal_delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    board_delay_us(us);
}

static uint32_t hal_millis(void *ctx)
{
    (void)ctx;
    return board_millis();
}

static int hal_irq_level(void *ctx)
{
    (void)ctx;
    /* IRQ_N 低有效 -> 返回 1 */
    return (IfxPort_getPinState(&MODULE_P15, 5) == 0u) ? 1 : 0;
}

const s2500_hal_t g_s2500_hal = {
    .ctx = NULL_PTR,
    .spi_xfer = hal_spi_xfer,
    .delay_us = hal_delay_us,
    .millis = hal_millis,
    .irq_level = hal_irq_level,
    .log = NULL_PTR,
};

/* ------------------------------------------------------------------ */
/* 板级初始化                                                          */
/* ------------------------------------------------------------------ */
void board_init(void)
{
    uart_init();
    board_uart_write("\r\n[TC387] board init: ASCLIN0 115200, QSPI2 25MHz+DMA\r\n", 55);

    gpio_init();
    s2500_hw_reset();

    qspi_init();

    board_uart_write("[TC387] QSPI2 + RST_N/IRQ_N ready\r\n", 35);
}

/* ------------------------------------------------------------------ */
/* common/s2500 要求的移植层                                            */
/* ------------------------------------------------------------------ */
void s2500_port_uart_write(const char *data, unsigned int len)
{
    board_uart_write(data, len);
}

int s2500_port_uart_getc(void)
{
    return board_uart_getc();
}

void s2500_port_reboot(void)
{
    board_reboot();
}

void s2500_board_init(void)
{
    board_init();
}

/* ------------------------------------------------------------------ */

/* 板级状态: P15 引脚与 QSPI 寄存器 */
static void cmd_tcstat(int argc, char **argv)
{
    (void)argc; (void)argv;
    s2500_shell_printf("P15_OUT=0x%08X (RST=%u CS=%u) IOCR0=0x%08X IOCR4=0x%08X IOCR8=0x%08X IOCR12=0x%08X\r\n",
        MODULE_P15.OUT.U, (unsigned)((MODULE_P15.OUT.U >> 4) & 1u), (unsigned)((MODULE_P15.OUT.U >> 2) & 1u),
        MODULE_P15.IOCR0.U, MODULE_P15.IOCR4.U, MODULE_P15.IOCR8.U, MODULE_P15.IOCR12.U);
    s2500_shell_printf("QSPI2 GLOBAL=0x%08X STATUS=0x%08X BACON=0x%08X\r\n",
        MODULE_QSPI2.GLOBALCON.U, MODULE_QSPI2.STATUS.U, MODULE_QSPI2.BACON.U);
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 tcstat, cmd_tcstat, TC387 P15/QSPI 状态);

/* QSPI2 时钟/DMA 诊断 */
static void cmd_tcqspi(int argc, char **argv)
{
    uint32 spb = (uint32)IfxScuCcu_getSpbFrequency();
    uint32 real = (uint32)(IfxQspi_calcRealBaudrate(&MODULE_QSPI2, IfxQspi_ChannelId_0) + 0.5f);
    (void)argc; (void)argv;
    s2500_shell_printf("f_SPB=%lu Hz  req=%lu Hz  real=%lu Hz  ch.baudrate=%lu\r\n",
        (unsigned long)spb, (unsigned long)QSPI_BAUDRATE,
        (unsigned long)real, (unsigned long)g_qspi_ch.baudrate);
    s2500_shell_printf("QSPI2 ECON0=0x%08X BACON=0x%08X GLOBALCON=0x%08X\r\n",
        MODULE_QSPI2.ECON[0].U, MODULE_QSPI2.BACON.U, MODULE_QSPI2.GLOBALCON.U);
    s2500_shell_printf("DMA txCH=%d rxCH=%d  irq tx=%lu rx=%lu er=%lu\r\n",
        (int)QSPI_TX_DMA_CH, (int)QSPI_RX_DMA_CH,
        (unsigned long)g_qspi_tx_irq, (unsigned long)g_qspi_rx_irq,
        (unsigned long)g_qspi_er_irq);
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 tcqspi, cmd_tcqspi, TC387 QSPI2 时钟/DMA 诊断);

/* QSPI2 原始吞吐: N 次 68B 交换 (含软件 CS), 用于定位 SPI/HAL 瓶颈 */
static void cmd_tcspeed(int argc, char **argv)
{
    static uint8 tx[68], rx[68];
    uint32 n = 2000, i;
    uint32 t0, t1, dt_us, per_chunk_x10;

    if (argc >= 3) {
        uint32 v = 0; const char *p = argv[2];
        while (*p >= '0' && *p <= '9') { v = v * 10u + (uint32)(*p - '0'); p++; }
        if (v) n = v;
    }
    memset(tx, 0, sizeof(tx));
    tx[0] = 0x10;                     /* 任意头, 只测总线时间 */
    /* 预热 */
    if (g_s2500_hal.spi_xfer(NULL, tx, rx, 68) != 0) {
        s2500_shell_printf("warmup failed\r\n");
        return;
    }
    t0 = board_millis();
    for (i = 0; i < n; i++) {
        if (g_s2500_hal.spi_xfer(NULL, tx, rx, 68) != 0) {
            s2500_shell_printf("xfer fail at %lu\r\n", (unsigned long)i);
            return;
        }
    }
    t1 = board_millis();
    dt_us = (t1 - t0) * 1000u;
    per_chunk_x10 = dt_us ? (dt_us * 10u) / n : 0;
    s2500_shell_printf("tcspeed: %lu x 68B  dt=%lu us  %lu.%lu us/chunk  %lu kbit/s\r\n",
        (unsigned long)n, (unsigned long)dt_us,
        (unsigned long)(per_chunk_x10 / 10u), (unsigned long)(per_chunk_x10 % 10u),
        (unsigned long)(dt_us ? (n * 68u * 8u) / dt_us : 0u));
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 tcspeed, cmd_tcspeed, TC387 QSPI2 68B 原始吞吐);

/* 原始控制事务测试: 读 IDVER (MMS0 addr0) */
static void cmd_tcspi(int argc, char **argv)
{
    uint8_t tx[12] = {0x10, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t rx[12] = {0};
    int i;
    (void)argc; (void)argv;

    IfxPort_setPinLow(&MODULE_P15, 2);
    int rc = g_s2500_hal.spi_xfer(NULL, tx, rx, 12);
    IfxPort_setPinHigh(&MODULE_P15, 2);
    s2500_shell_printf("tcspi rc=%d rx:", rc);
    for (i = 0; i < 12; i++) s2500_shell_printf(" %02X", rx[i]);
    s2500_shell_printf("\r\nQSPI2 STATUS=0x%08X irq tx=%lu rx=%lu er=%lu\r\n",
        MODULE_QSPI2.STATUS.U, (unsigned long)g_qspi_tx_irq, (unsigned long)g_qspi_rx_irq, (unsigned long)g_qspi_er_irq);
    s2500_shell_printf("SRC110=0x%08X SRC111=0x%08X SRC112=0x%08X\r\n",
        *(volatile uint32*)0xF00381B8u, *(volatile uint32*)0xF00381BCu, *(volatile uint32*)0xF00381C0u);
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 tcspi, cmd_tcspi, TC387 raw SPI test);
