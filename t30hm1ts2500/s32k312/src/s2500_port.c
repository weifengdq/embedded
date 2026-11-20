/**
 * @file  s2500_port.c
 * @brief S32K312 平台移植层: UART0 / SPI(硬件 LPSPI0) / 延时 / 复位 / 诊断
 *
 * R13 修复 (长时间压测后 UART shell 无响应、网络正常):
 *   旧实现依赖 RTD 的 Lpuart_Uart_Ip_AsyncReceive() + 回调逐字节重挂。
 *   LPUART 一旦出现 OR(溢出)/FE/NF/PF, RTD 的状态机会停在上一次请求,
 *   回调不再被调用, 于是 shell 收不到任何字符 (但 lwIP 主循环仍在跑)。
 *   现改为: 关掉 LPUART 全部中断, 主循环轮询 STAT/DATA,
 *   每轮清 w1c 错误标志并抽干 RX FIFO —— 不存在"卡死"的状态机,
 *   溢出只丢字节不会停止接收。计数值可用 s32k 命令查看。
 */
#include "s2500_port.h"
#include "s2500_port_board.h"
#include "s2500.h"
#include "s2500_spi.h"
#include "s2500_app.h"
#include "lpspi_hw.h"
#include "systick.h"

#include "Lpuart_Uart_Ip.h"
#include "IntCtrl_Ip.h"
#include "S32K312_SIUL2.h"
#include "S32K312_SCB.h"
#include "S32K312_SYSTICK.h"
#include "S32K312_LPUART.h"

#include "shell.h"

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#define UART_INSTANCE        0u
#define UART_RX_RING_SIZE    1024u

#define PIN_PTD14_RST_N      110u
#define PIN_PTE9_IRQ_N       137u
#define PIN_PTB10_LED        42u

/* ------------------------------------------------------------------ */
/* 调试串口 (直接操作寄存器, 不依赖 RTD 状态机)                        */
/* ------------------------------------------------------------------ */
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static uint8_t  s_rx_ring[UART_RX_RING_SIZE];
static s2500_uart_diag_t s_udiag;

/* RTD 仍会引用该回调符号, 保留空实现 (接收改轮询) */
void UART0_CALLBACK(const uint8 hwInstance,
                    const Lpuart_Uart_Ip_EventType event,
                    const void *userData)
{
    (void)hwInstance;
    (void)event;
    (void)userData;
}

void s2500_port_uart_init(void)
{
    LPUART_Type *u = IP_LPUART_0;

    /* RTD 的 Lpuart_Uart_Ip_Hw_Init() 把 CTRL 清 0 (TE/RE 都没开),
       它只在 SyncSend/AsyncReceive 里才按需打开收发器。本工程改成
       寄存器轮询后必须自己开 TE/RE, 否则 TDRE 永远不置位。 */
    u->CTRL = (u->CTRL & ~(LPUART_CTRL_RIE_MASK | LPUART_CTRL_ILIE_MASK | LPUART_CTRL_TIE_MASK |
                           LPUART_CTRL_TCIE_MASK | LPUART_CTRL_PEIE_MASK | LPUART_CTRL_FEIE_MASK |
                           LPUART_CTRL_NEIE_MASK | LPUART_CTRL_ORIE_MASK))
              | LPUART_CTRL_TE_MASK | LPUART_CTRL_RE_MASK;

    /* 打开 TX/RX FIFO: 不打开时 RX 只有 1 字节缓冲, 只要主循环有 >87us
     * 的空档 (一笔 68 字节 SPI 事务就要 36us, 一次 printf 更长) 就 OR 溢出丢字符,
     * 实测表现为 shell 命令被打断 ("s2500 counters" -> "s2500 couters")。 */
    u->FIFO = LPUART_FIFO_RXFE_MASK | LPUART_FIFO_TXFE_MASK |
              LPUART_FIFO_RXFLUSH_MASK | LPUART_FIFO_TXFLUSH_MASK;
    u->WATER = 0u;                             /* RX 水位 1 字节 / TX 水位 1 字节 */

    u->STAT = LPUART_STAT_OR_MASK | LPUART_STAT_NF_MASK | LPUART_STAT_FE_MASK |
              LPUART_STAT_PF_MASK | LPUART_STAT_IDLE_MASK;

    IntCtrl_Ip_DisableIrq(LPUART0_IRQn);

    while ((u->STAT & LPUART_STAT_RDRF_MASK) != 0u) {
        (void)u->DATA;
    }
    s_rx_head = 0u;
    s_rx_tail = 0u;
    (void)memset(&s_udiag, 0, sizeof(s_udiag));
}

void s2500_port_uart_poll(void)
{
    LPUART_Type *u = IP_LPUART_0;
    uint32_t stat = u->STAT;
    uint32_t errs = stat & (LPUART_STAT_OR_MASK | LPUART_STAT_NF_MASK |
                            LPUART_STAT_FE_MASK | LPUART_STAT_PF_MASK);

    if (errs != 0u) {
        if ((errs & LPUART_STAT_OR_MASK) != 0u) { s_udiag.or_cnt++; }
        if ((errs & LPUART_STAT_FE_MASK) != 0u) { s_udiag.fe_cnt++; }
        if ((errs & LPUART_STAT_NF_MASK) != 0u) { s_udiag.nf_cnt++; }
        if ((errs & LPUART_STAT_PF_MASK) != 0u) { s_udiag.pf_cnt++; }
        u->STAT = errs;                       /* w1c: 清掉即恢复接收 */
    }
    if ((stat & LPUART_STAT_IDLE_MASK) != 0u) {
        s_udiag.idle_irq++;
        u->STAT = LPUART_STAT_IDLE_MASK;
    }

    while ((u->STAT & LPUART_STAT_RDRF_MASK) != 0u) {
        uint8_t  ch   = (uint8_t)(u->DATA & 0xFFu);
        uint16_t next = (uint16_t)((s_rx_head + 1u) % UART_RX_RING_SIZE);

        s_udiag.rx_bytes++;
        if (next != s_rx_tail) {
            s_rx_ring[s_rx_head] = ch;
            s_rx_head = next;
        } else {
            s_udiag.rx_dropped++;
        }
    }
}

void s2500_port_uart_diag(s2500_uart_diag_t *out)
{
    if (out != NULL) {
        *out = s_udiag;
    }
}

int s2500_port_uart_getc(void)
{
    uint8_t ch;

    if (s_rx_head == s_rx_tail) {
        return -1;
    }
    ch = s_rx_ring[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1u) % UART_RX_RING_SIZE);
    return (int)ch;
}

void s2500_port_uart_write(const char *data, unsigned int len)
{
    LPUART_Type *u = IP_LPUART_0;
    unsigned int i;

    if (data == NULL) {
        return;
    }
    for (i = 0u; i < len; i++) {
        uint32_t guard = 4000000u;

        while ((u->STAT & LPUART_STAT_TDRE_MASK) == 0u) {
            if (--guard == 0u) {
                s_udiag.tx_timeout++;
                return;                        /* 绝不无限等待 (R13) */
            }
            /* 打印长输出时把串口接收也带上 (长输出期间主循环进不来) */
            if ((u->STAT & LPUART_STAT_RDRF_MASK) != 0u) {
                s2500_port_uart_poll();
            }
        }
        u->DATA = (uint32_t)(uint8_t)data[i];
    }
}

/* printf / puts 重定向 */
int _write(int file, char *ptr, int len)
{
    (void)file;
    if (ptr != NULL && len > 0) {
        s2500_port_uart_write(ptr, (unsigned int)len);
    }
    return len;
}

/* ------------------------------------------------------------------ */
/* 延时 (SysTick 24bit 递减计数器, 120MHz)                             */
/* ------------------------------------------------------------------ */
#define SYSTICK_LOAD_VAL     120000u

static void delay_us(uint32_t us)
{
    uint32_t need = us * 120u;
    uint32_t last = (uint32_t)S32_SysTick->CVR & 0xFFFFFFu;
    uint32_t acc = 0u;

    while (acc < need) {
        uint32_t now = (uint32_t)S32_SysTick->CVR & 0xFFFFFFu;
        if (last >= now) {
            acc += (last - now);
        } else {
            acc += (last + (SYSTICK_LOAD_VAL - now));
        }
        last = now;
    }
}

/* ------------------------------------------------------------------ */
/* 复位 / 板级                                                        */
/* ------------------------------------------------------------------ */
static void s2500_reset_pulse(void)
{
    IP_SIUL2->GPDO110 = 0u;
    delay_us(20000u);
    IP_SIUL2->GPDO110 = 1u;
    delay_us(30000u);
}

/* ------------------------------------------------------------------ */
/* S2500 HAL                                                          */
/* ------------------------------------------------------------------ */
static int hal_spi_xfer(void *ctx, const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    (void)ctx;
    /* 硬件 SPI 下一笔 68 字节事务 = 36us (15MHz), 一整帧要连做 24 笔 ≈ 1ms,
     * 这期间主循环进不来, LPUART 只有 4 字节 RX FIFO (115200 = 87us/字节),
     * 会在 350us 后溢出丢字符 (表现为 shell 命令被打断: "s2500 plca" -> "s2500 plc")。
     * 所以在每笔 SPI 事务前先收一次串口。 */
    s2500_port_uart_poll();
    return s2500_spi_xfer(tx, rx, len);
}

static void hal_delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    delay_us(us);
}

static uint32_t hal_millis(void *ctx)
{
    (void)ctx;
    return SysTick_GetMs();
}

static int hal_irq_level(void *ctx)
{
    (void)ctx;
    return (IP_SIUL2->GPDI137 & 0x1u) ? 0 : 1;
}

static void hal_log(void *ctx, const char *fmt, ...)
{
    va_list ap;
    (void)ctx;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static const s2500_hal_t s_hal = {
    NULL,
    hal_spi_xfer,
    hal_delay_us,
    hal_millis,
    hal_irq_level,
    hal_log,
};

const s2500_hal_t *s2500_port_hal(void)
{
    return &s_hal;
}

void s2500_board_init(void)
{
    s2500_spi_init();
    s2500_reset_pulse();
}

void s2500_port_reboot(void)
{
    printf("\r\n[reboot] ...\r\n");
    __asm volatile ("dsb 0xF" ::: "memory");
    S32_SCB->AIRCR = 0x05FA0004u;
    while (1) {
    }
}

/* ------------------------------------------------------------------ */
/* 调试辅助                                                           */
/* ------------------------------------------------------------------ */
static void probe_idver(void)
{
    s2500_dev_t *dev = s2500_app_dev();
    uint32_t v = 0u;
    int rc;

    if (dev == NULL) {
        printf("dev NULL\r\n");
        return;
    }
    rc = s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_IDVER, &v);
    {
        int k;
        printf("rx:");
        for (k = 0; k < 12; k++) { printf(" %02x", dev->rxb[k]); }
        printf("\r\n");
        printf("tx:");
        for (k = 0; k < 12; k++) { printf(" %02x", dev->txb[k]); }
        printf("\r\n");
    }
    printf("IDVER rc=%d val=%08lx %s\r\n", rc, (unsigned long)v,
           (v == 0x11u) ? "OK" : "BAD");
}

/* ------------------------------------------------------------------ */
/* 平台诊断命令: s32k [...]                                           */
/* ------------------------------------------------------------------ */
extern volatile uint32_t g_poll_period_cyc;

static void s32k_stats(void)
{
    s2500_dev_t *dev = s2500_app_dev();
    const s2500_stats_t *s;

    if (dev == NULL) { printf("dev NULL\r\n"); return; }
    s = &dev->stats;
    printf("TX frames=%lu chunks=%lu busy_wait=%lu drop=%lu prot=%lu\r\n",
           (unsigned long)s->tx_frames, (unsigned long)s->tx_chunks,
           (unsigned long)s->tx_busy_waits, (unsigned long)s->tx_dropped,
           (unsigned long)s->tx_protocol_err);
    printf("RX frames=%lu chunks=%lu polls=%lu drop=%lu ovf=%lu resync=%lu fcs=%lu\r\n",
           (unsigned long)s->rx_frames, (unsigned long)s->rx_chunks,
           (unsigned long)s->rx_polls, (unsigned long)s->rx_dropped,
           (unsigned long)s->rx_overflow, (unsigned long)s->rx_resync,
           (unsigned long)s->rx_fcs_err);
    printf("CTRL xfer=%lu parity=%lu hdr_bad=%lu ftr_parity=%lu\r\n",
           (unsigned long)s->ctrl_xfer, (unsigned long)s->ctrl_parity_err,
           (unsigned long)s->header_bad, (unsigned long)s->footer_parity_err);
}

static void s32k_selftest(uint32_t iters)
{
    s2500_dev_t *dev = s2500_app_dev();
    uint32_t i, bad = 0u, to = 0u, v, t0, t1;

    if (dev == NULL) {
        printf("dev NULL\r\n");
        return;
    }
    t0 = SysTick_GetMs();
    for (i = 0u; i < iters; i++) {
        v = 0u;
        if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_IDVER, &v) != S2500_OK) {
            to++;
        } else if (v != 0x11u) {
            bad++;
        }
    }
    t1 = SysTick_GetMs();
    printf("IDVER x%lu: bad=%lu timeout=%lu %lums %s\r\n",
           (unsigned long)iters, (unsigned long)bad, (unsigned long)to,
           (unsigned long)(t1 - t0),
           ((bad == 0u) && (to == 0u)) ? "PASS" : "FAIL");
}

static void s32k_bench(uint32_t iters)
{
    static uint8_t tx[68] __attribute__((aligned(4)));
    static uint8_t rx[68] __attribute__((aligned(4)));
    uint32_t i, t0, t1, ms, kbps;
    uint64_t bits;

    memset(tx, 0, sizeof(tx));
    memset(rx, 0, sizeof(rx));
    t0 = SysTick_GetMs();
    for (i = 0u; i < iters; i++) {
        (void)s2500_spi_xfer(tx, rx, sizeof(tx));
    }
    t1 = SysTick_GetMs();
    ms = t1 - t0;
    if (ms == 0u) {
        ms = 1u;
    }
    bits = (uint64_t)iters * 68u * 8u;
    kbps = (uint32_t)(bits / (uint64_t)ms);          /* kbit/s */
    printf("SPI bench %lux68B: %lums %lu.%03lu Mbit/s\r\n",
           (unsigned long)iters, (unsigned long)ms,
           (unsigned long)(kbps / 1000u), (unsigned long)(kbps % 1000u));
}

static int s32k_cmd(int argc, char *argv[])
{
    char sbuf[96];

    if (argc >= 2 && strcmp(argv[1], "spi") == 0) {
        lpspi_hw_dump();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "xcmp") == 0) {
        lpspi_hw_cmp2();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "probe") == 0) {
        probe_idver();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "hw") == 0) {
        int on = ((argc >= 3) && (strcmp(argv[2], "on") == 0)) ? 1 : 0;
        s2500_spi_select(on);
        s2500_spi_info_str(sbuf, sizeof(sbuf));
        printf("backend -> %s\r\n", sbuf);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "speed") == 0) {
        uint32_t hz = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 0) : 0u;
        uint32_t got = s2500_spi_set_speed(hz);
        s2500_spi_info_str(sbuf, sizeof(sbuf));
        printf("SPI: %s (req %lu -> %lu Hz)\r\n",
               sbuf, (unsigned long)hz, (unsigned long)got);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "selftest") == 0) {
        uint32_t n = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 0) : 1000u;
        s32k_selftest(n);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "bench") == 0) {
        uint32_t n = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 0) : 2000u;
        s32k_bench(n);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "dma") == 0) {
        int on = ((argc >= 3) && (strcmp(argv[2], "on") == 0)) ? 1 : 0;
        lpspi_hw_dma(on);
        printf("LPSPI0 DMA: %s\r\n", (on != 0) ? "on" : "off");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "meclk") == 0) {
        int fix = ((argc >= 3) && (strcmp(argv[2], "fix") == 0)) ? 1 : 0;
        (void)lpspi_hw_dma_clock(fix);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "dmamem") == 0) {
        (void)lpspi_hw_dma_ram_test();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "dmaburst") == 0) {
        (void)lpspi_hw_dma_burst_test();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "dmabest") == 0) {
        (void)lpspi_hw_dma_best_test();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "dmabyte") == 0) {
        (void)lpspi_hw_dma_byte_test();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "dmareg") == 0) {
        lpspi_hw_dma_regs();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "dmatest") == 0) {
        uint32_t t = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 0) : 43u;
        uint32_t r = (argc >= 4) ? (uint32_t)strtoul(argv[3], NULL, 0) : 44u;
        (void)lpspi_hw_dma_test(t, r);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "dmaprobe") == 0) {
        (void)lpspi_hw_dma_probe();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "poll") == 0) {
        uint32_t us = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 0) : 150u;
        g_poll_period_cyc = us * 120u;
        printf("poll period = %lu us%s\r\n", (unsigned long)us,
               (us == 0u) ? " (every loop)" : "");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "stats") == 0) {
        s32k_stats();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        s_udiag.or_cnt = s_udiag.fe_cnt = s_udiag.nf_cnt = s_udiag.pf_cnt = 0u;
        s_udiag.rx_bytes = s_udiag.rx_dropped = s_udiag.tx_timeout = 0u;
        printf("uart counters cleared\r\n");
        return 0;
    }

    s2500_spi_info_str(sbuf, sizeof(sbuf));
    printf("SPI  : %s\r\n", sbuf);
    printf("UART : or=%lu fe=%lu nf=%lu pf=%lu idle=%lu\r\n",
           (unsigned long)s_udiag.or_cnt, (unsigned long)s_udiag.fe_cnt,
           (unsigned long)s_udiag.nf_cnt, (unsigned long)s_udiag.pf_cnt,
           (unsigned long)s_udiag.idle_irq);
    printf("       rx=%lu drop=%lu tx_to=%lu\r\n",
           (unsigned long)s_udiag.rx_bytes, (unsigned long)s_udiag.rx_dropped,
           (unsigned long)s_udiag.tx_timeout);
    printf("PIN  : IRQ_N=%d RST_N=%d\r\n",
           hal_irq_level(NULL), (int)(IP_SIUL2->GPDO110 & 0x1u));
    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 s32k, s32k_cmd, Lpsp S32K312 平台诊断: s32k [spi|probe|hw on|off|speed <hz>|selftest <n>|bench <n>|xcmp|clear]);
