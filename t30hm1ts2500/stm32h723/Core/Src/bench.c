/**
 * @file  bench.c
 * @brief 板级性能/诊断命令: bench (UDP 连续发送) 与 spi (SPI1 配置+统计)
 *
 * 只属于 STM32H723 这块板, 不放进 common/, 避免影响其它平台。
 */
#include "s2500_app.h"
#include "s2500_shell.h"
#include "board.h"
#include "shell.h"
#include "lwip/ip4_addr.h"
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* IPv4 校验和                                                        */
/* ------------------------------------------------------------------ */
static uint16_t inet_csum(const uint8_t *p, int len)
{
    uint32_t sum = 0;

    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len) {
        sum += (uint32_t)p[0] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

/* ------------------------------------------------------------------ */
/* 组一个最小 IPv4/UDP 帧 (目的 MAC 广播, UDP 校验和 0 = 不校验)       */
/* 返回 L2 帧长 (不含 FCS)。                                          */
/* ------------------------------------------------------------------ */
static uint16_t build_udp_frame(uint8_t *f, const uint8_t *mac,
                                const uint8_t *src_ip, const uint8_t *dst_ip,
                                uint16_t payload, uint16_t sport, uint16_t dport)
{
    uint16_t l3    = (uint16_t)(20u + 8u + payload);
    uint16_t total = (uint16_t)(14u + l3);
    uint16_t i;

    memset(f, 0, (size_t)total);
    memset(f, 0xFF, 6);                       /* 目的: 广播, 不需要 ARP */
    memcpy(f + 6, mac, 6);                    /* 源 MAC */
    f[12] = 0x08; f[13] = 0x00;               /* IPv4 */

    f[14] = 0x45;                             /* v4, IHL=5 */
    f[16] = (uint8_t)(l3 >> 8); f[17] = (uint8_t)l3;
    f[18] = 0x00; f[19] = 0x01;               /* id */
    f[20] = 0x40; f[21] = 0x00;               /* DF */
    f[22] = 64;   f[23] = 17;                 /* TTL=64, UDP */
    memcpy(f + 26, src_ip, 4);
    memcpy(f + 30, dst_ip, 4);
    {
        uint16_t c = inet_csum(f + 14, 20);
        f[24] = (uint8_t)(c >> 8); f[25] = (uint8_t)c;
    }

    f[34] = (uint8_t)(sport >> 8); f[35] = (uint8_t)sport;
    f[36] = (uint8_t)(dport >> 8); f[37] = (uint8_t)dport;
    {
        uint16_t ul = (uint16_t)(8u + payload);
        f[38] = (uint8_t)(ul >> 8); f[39] = (uint8_t)ul;
    }

    for (i = 0; i < payload; i++) {
        f[42 + i] = (uint8_t)(0xA0u + (i & 0x0Fu));
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* bench [count] [payload]                                            */
/* ------------------------------------------------------------------ */
static int cmd_bench(int argc, char **argv)
{
    static uint8_t frame[S2500_MAX_FRAME];
    s2500_dev_t *dev = s2500_app_dev();
    struct netif *n  = s2500_app_netif();
    uint32_t count = 1000;
    uint32_t payload = 1000;
    uint32_t ok = 0, fail = 0, i;
    uint32_t t0, dt, us;
    uint32_t drop0, frame0;
    uint16_t len;
    uint8_t src_ip[4] = { 192, 168, 10, 11 };
    uint8_t dst_ip[4] = { 192, 168, 10, 10 };

    if (!dev) {
        s2500_shell_printf("S2500 未初始化\r\n");
        return -1;
    }
    if (argc > 1) count = (uint32_t)strtoul(argv[1], NULL, 0);
    if (argc > 2) payload = (uint32_t)strtoul(argv[2], NULL, 0);
    if (count == 0) count = 1;
    if (payload < 18) payload = 18;
    if (payload > 1472u) payload = 1472u;          /* 标准以太网 MTU: 1500-20-8 */

    if (n) {
        memcpy(src_ip, netif_ip4_addr(n), 4);
        memcpy(dst_ip, netif_ip4_gw(n), 4);
    }
    len = build_udp_frame(frame, dev->mac, src_ip, dst_ip,
                          (uint16_t)payload, 5001, 5001);

    drop0  = dev->stats.tx_dropped;
    frame0 = dev->stats.tx_frames;

    /* 预热, 让 TX 缓冲进入稳态 */
    for (i = 0; i < 10u; i++) {
        (void)s2500_send(dev, frame, len, 0);
    }

    s2500_shell_printf("\r\n开始发送 %lu 帧 x %u B ...\r\n",
                       (unsigned long)count, (unsigned)len);

    t0 = DWT->CYCCNT;
    for (i = 0; i < count; i++) {
        if (s2500_send(dev, frame, len, 0) == S2500_OK) {
            ok++;
        } else {
            fail++;
        }
    }
    dt = DWT->CYCCNT - t0;
    if (dt == 0) dt = 1;
    us = dt / (SystemCoreClock / 1000000u);
    if (us == 0) us = 1;

    {
        /* fps / Mbit/s 都用整数运算, 避免 newlib 浮点 printf */
        uint32_t fps_x10   = (uint32_t)((uint64_t)ok * 10000000u / us);
        uint32_t mbps_x100 = (uint32_t)((uint64_t)ok * len * 8u * 100u / us);
        uint32_t pmbps_x100 = (uint32_t)((uint64_t)ok * payload * 8u * 100u / us);
        uint32_t avg_us = ok ? (uint32_t)((uint64_t)us / ok) : 0;

        s2500_shell_printf("===== UDP 连续发送 bench =====\r\n");
        s2500_shell_printf("帧长     : %u B (L2, 不含 FCS)  payload %u B\r\n",
                           (unsigned)len, (unsigned)payload);
        s2500_shell_printf("成功/失败: %lu / %lu  (驱动 tx_dropped +%lu)\r\n",
                           (unsigned long)ok, (unsigned long)fail,
                           (unsigned long)(dev->stats.tx_dropped - drop0));
        s2500_shell_printf("耗时     : %lu ms, 平均 %lu us/帧\r\n",
                           (unsigned long)(us / 1000u), (unsigned long)avg_us);
        s2500_shell_printf("帧率     : %lu.%lu fps\r\n",
                           (unsigned long)(fps_x10 / 10u), (unsigned long)(fps_x10 % 10u));
        s2500_shell_printf("L2 速率  : %lu.%02lu Mbit/s (10BASE-T1S 线速上限 10)\r\n",
                           (unsigned long)(mbps_x100 / 100u),
                           (unsigned long)(mbps_x100 % 100u));
        s2500_shell_printf("UDP 速率 : %lu.%02lu Mbit/s (payload)\r\n",
                           (unsigned long)(pmbps_x100 / 100u),
                           (unsigned long)(pmbps_x100 % 100u));
        s2500_shell_printf("链路     : %s   tx_frames +%lu\r\n",
                           dev->link_up ? "UP" : "DOWN",
                           (unsigned long)(dev->stats.tx_frames - frame0));
        if (!dev->link_up) {
            s2500_shell_printf("提示     : link=DOWN 时帧可能被 MAC 丢弃, 只能验证 SPI/驱动路径\r\n");
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* spi bench [n]: 连续 n 次 12B 控制事务读 IDVER (只读, 安全)          */
/* ------------------------------------------------------------------ */
static void spi_bench(uint32_t n)
{
    s2500_dev_t *dev = s2500_app_dev();
    uint32_t i, t0, dt, v = 0;
    uint32_t us_per = SystemCoreClock / 1000000u;

    if (!dev || n == 0) return;

    t0 = DWT->CYCCNT;
    for (i = 0; i < n; i++) {
        (void)s2500_read_reg(dev, 0, 0, &v);
    }
    dt = DWT->CYCCNT - t0;

    s2500_shell_printf("\r\n===== SPI1 控制事务 microbench =====\r\n");
    s2500_shell_printf("事务     : %lu x 12 B (读 IDVER), 结果校验 %s\r\n",
                       (unsigned long)n, (v == 0x11u) ? "IDVER=0x11 OK" : "不一致");
    s2500_shell_printf("总周期   : %lu  (%lu us)\r\n",
                       (unsigned long)dt, (unsigned long)(dt / us_per));
    s2500_shell_printf("单次     : %lu cycles = %lu.%02lu us\r\n",
                       (unsigned long)(dt / n),
                       (unsigned long)(dt / n / us_per),
                       (unsigned long)(((dt / n) % us_per) * 100u / us_per));
    s2500_shell_printf("理论上限 : 12 B @ %lu kHz = %lu ns\r\n",
                       (unsigned long)(board_spi_clock_hz() / 1000u),
                       (unsigned long)((uint64_t)12u * 8u * 1000000000ull / board_spi_clock_hz()));
}

/* ------------------------------------------------------------------ */
/* spi | spi dma on|off | spi reset | spi bench [n] | spi speed <MHz>  */
/* ------------------------------------------------------------------ */
static int cmd_spi(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "dma")) {
        if (argc >= 3) {
            board_spi_dma_enable(strcmp(argv[2], "off") != 0);
        }
        s2500_shell_printf("SPI1 DMA: %s\r\n", board_spi_dma_is_enabled() ? "ON" : "OFF");
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "speed")) {
        if (argc >= 3) {
            uint32_t mhz = (uint32_t)strtoul(argv[2], NULL, 0);
            uint32_t hz = board_spi_set_hz(mhz * 1000000u);
            s2500_shell_printf("SPI1 SCLK -> %lu.%03lu MHz (请求 %lu MHz)\r\n",
                               (unsigned long)(hz / 1000000u),
                               (unsigned long)((hz / 1000u) % 1000u),
                               (unsigned long)mhz);
        } else {
            s2500_shell_printf("SPI1 SCLK = %lu.%03lu MHz\r\n",
                               (unsigned long)(board_spi_clock_hz() / 1000000u),
                               (unsigned long)((board_spi_clock_hz() / 1000u) % 1000u));
        }
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "reset")) {
        memset((void *)&g_spi_stats, 0, sizeof(g_spi_stats));
        s2500_shell_printf("SPI 统计已清零\r\n");
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "bench")) {
        uint32_t n = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 0) : 200u;
        spi_bench(n);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "stat") != 0) {
        s2500_shell_printf("用法: spi [stat] | spi dma on|off | spi reset | spi bench [n] | spi speed <MHz>\r\n");
        return -1;
    }

    {
        uint32_t ms  = HAL_GetTick();
        uint64_t avg64 = g_spi_stats.xfers ? (g_spi_stats.cycles_total / g_spi_stats.xfers) : 0u;
        uint32_t avg = (uint32_t)avg64;
        uint32_t dma_pm = g_spi_stats.xfers
                        ? (uint32_t)((uint64_t)g_spi_stats.dma_xfers * 1000u / g_spi_stats.xfers)
                        : 0u;
        uint32_t busy_pm = ms
                        ? (uint32_t)(g_spi_stats.cycles_total /
                                     ((uint64_t)ms * (SystemCoreClock / 1000u) / 1000u))
                        : 0u;
        uint32_t us_per = SystemCoreClock / 1000000u;
        uint32_t br  = (SPI1->CFG1 & SPI_CFG1_MBR) >> SPI_CFG1_MBR_Pos;
        uint32_t div = 1u << (br + 1u);

        s2500_shell_printf("\r\n===== STM32H723 SPI1 配置 / 统计 =====\r\n");
        s2500_shell_printf("SYSCLK   : %lu MHz (HSE %s)  HCLK %lu MHz  APB2 %lu MHz\r\n",
                           (unsigned long)(g_board_sysclk_hz / 1000000u),
                           g_board_hse_ok ? "25MHz" : "falls back to HSI",
                           (unsigned long)(g_board_hclk_hz / 1000000u),
                           (unsigned long)(g_board_pclk2_hz / 1000000u));
        s2500_shell_printf("Cache    : I-Cache %s, D-Cache %s, DMA 缓冲在 RAM_D2 MPU 非缓存区\r\n",
                           g_board_icache_on ? "ON" : "OFF",
                           g_board_dcache_on ? "ON" : "OFF");
        s2500_shell_printf("时钟     : PLL2P %lu MHz / %lu = %lu.%03lu MHz (模式 0, MSB first)\r\n",
                           (unsigned long)(g_board_spi_kernel_hz / 1000000u),
                           (unsigned long)div,
                           (unsigned long)(board_spi_clock_hz() / 1000000u),
                           (unsigned long)((board_spi_clock_hz() / 1000u) % 1000u));
        s2500_shell_printf("DMA      : %s  (TX DMA1_Stream1/req38, RX DMA1_Stream0/req37)\r\n",
                           board_spi_dma_is_enabled() ? "ON" : "OFF");
        s2500_shell_printf("事务     : %lu 次 / %lu B\r\n",
                           (unsigned long)g_spi_stats.xfers,
                           (unsigned long)g_spi_stats.bytes);
        s2500_shell_printf("耗时     : 平均 %lu.%02lu us, 最大 %lu.%02lu us\r\n",
                           (unsigned long)(avg / us_per),
                           (unsigned long)((avg % us_per) * 100u / us_per),
                           (unsigned long)(g_spi_stats.cycles_max / us_per),
                           (unsigned long)((g_spi_stats.cycles_max % us_per) * 100u / us_per));
        s2500_shell_printf("DMA 占比 : %lu.%lu%%   回退 %lu 次   超时 %lu 次\r\n",
                           (unsigned long)(dma_pm / 10u), (unsigned long)(dma_pm % 10u),
                           (unsigned long)g_spi_stats.dma_fallback,
                           (unsigned long)g_spi_stats.timeouts);
        s2500_shell_printf("SPI 占用 : %lu.%lu%% (累计 CS 低电平时间 / 上电时间)\r\n",
                           (unsigned long)(busy_pm / 10u), (unsigned long)(busy_pm % 10u));
        s2500_shell_printf("理论耗时 : 12B %lu ns, 68B %lu ns\r\n",
                           (unsigned long)((uint64_t)12u * 8u * 1000000000ull / board_spi_clock_hz()),
                           (unsigned long)((uint64_t)68u * 8u * 1000000000ull / board_spi_clock_hz()));
        s2500_shell_printf("IRQ      : IRQ_N 下降沿 %lu 次\r\n",
                           (unsigned long)board_irq_count);
        if (g_spi_stats.timeouts) {
            s2500_shell_printf("DMA DBG  : LISR=%08lX HISR=%08lX SR=%04lX CFG1=%08lX NDTR0=%lu NDTR1=%lu CR0=%08lX CR1=%08lX CR2=%08lX CLK=%lu\r\n",
                               (unsigned long)g_spi_dma_dbg[0], (unsigned long)g_spi_dma_dbg[1],
                               (unsigned long)g_spi_dma_dbg[2], (unsigned long)g_spi_dma_dbg[3],
                               (unsigned long)g_spi_dma_dbg[4], (unsigned long)g_spi_dma_dbg[5],
                               (unsigned long)g_spi_dma_dbg[6], (unsigned long)g_spi_dma_dbg[7],
                               (unsigned long)g_spi_dma_dbg[8], (unsigned long)g_spi_dma_dbg[9]);
        }
    }
    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 bench, cmd_bench, UDP 连续发送: bench [帧数] [payload字节]);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 spi, cmd_spi, SPI1 配置/统计/DMA/速率: spi | spi dma on|off | spi speed 25 | spi bench [n]);
