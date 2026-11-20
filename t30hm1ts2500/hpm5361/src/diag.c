/**
 * @file  diag.c
 * @brief HPM5361 平台自带的 SPI1 诊断命令 (letter-shell)
 *
 *   spi                 打印 SPI1 状态 (SCLK / 时钟源 / DMA / 事务统计)
 *   spi speed <MHz>     运行时改 SCLK (0 = 回到默认 25MHz)
 *   spi dma <0|1>       开关数据事务 DMA
 *   spi bench <n>       纯 SPI 基准: n 次 68 字节数据块事务, 不依赖 PHY/总线
 *   spi reg <n>         连续读 IDVER n 次, 统计错误数与耗时
 *   spi loop <n>        PCS 环回收发 n 次 (不依赖外部总线)
 *   spi reset           清零 SPI 事务统计
 *
 * 这些命令只依赖 bsp 与 s2500 驱动, 是"SPI 层"问题的第一手定位手段:
 * 即使总线拔掉 / 链路 down, spi bench 与 spi reg 仍然必须全部成功。
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "bsp.h"
#include "s2500_app.h"
#include "s2500_shell.h"
#include "s2500_regs.h"
#include "shell.h"

#define P(...) s2500_shell_printf(__VA_ARGS__)

static int diag_parse_u32(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;

    if (!s || !out) {
        return -1;
    }
    v = strtoul(s, &end, 0);
    if (end == s) {
        return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

static void diag_show_status(void)
{
    bsp_spi_stats_t st;

    bsp_spi_get_stats(&st);
    P("CPU  : %lu MHz\r\n", (unsigned long)(bsp_cpu_hz() / 1000000UL));
    P("SPI1 : SCLK=%lu Hz  src=%lu Hz  DMA=%s (自检%s)\r\n",
      (unsigned long)st.sclk_hz, (unsigned long)st.src_hz,
      st.dma ? "on" : "off", st.dma_ok ? "通过" : "未通过");
    /* newlib-nano 没有 %llu: 周期数按"百万周期"和每事务周期打印 */
    P("     事务=%lu 字节=%lu  平均 %.1f 周期/事务  平均 %.1f 周期/字节\r\n",
      (unsigned long)st.xfers, (unsigned long)st.bytes,
      st.xfers ? (double)st.cyc / (double)st.xfers : 0.0,
      st.bytes ? (double)st.cyc / (double)st.bytes : 0.0);
    if (st.xfers) {
        double tot = (double)st.cyc;
        double wait = (double)st.wait_cyc;
        P("     CPU: 总周期=%.1f M  其中等待=%.1f M (%.0f%%)  有效=%.1f M (%.0f%%)\r\n",
          tot / 1e6, wait / 1e6,
          tot > 0.0 ? 100.0 * wait / tot : 0.0,
          (tot - wait) / 1e6,
          tot > 0.0 ? 100.0 * (tot - wait) / tot : 0.0);
    }
}

static void diag_bench(uint32_t n)
{
    s2500_dev_t *dev = s2500_app_dev();
    bsp_spi_stats_t st;
    uint32_t i, t0, dt, bitrate;

    if (!dev || n == 0) {
        P("usage: spi bench <n>\r\n");
        return;
    }
    bsp_spi_reset_stats();
    t0 = bsp_millis();
    for (i = 0; i < n; i++) {
        (void)s2500_raw_block_dump(dev, 0);
    }
    dt = bsp_millis() - t0;
    bsp_spi_get_stats(&st);

    P("spi bench: %lu x %u B  dt=%lu ms\r\n", (unsigned long)n,
      (unsigned)S2500_DATA_XFER_LEN, (unsigned long)dt);
    if (dt) {
        /* 每事务 68 字节 x 8 bit; 68B*8*n / ms = bit/ms = kbit/s */
        bitrate = (n * (uint32_t)S2500_DATA_XFER_LEN * 8u) / dt;
        P("  吞吐=%lu.%03lu Mbit/s (理论上限 = SCLK = %lu.%02lu Mbit/s)\r\n",
          (unsigned long)(bitrate / 1000u), (unsigned long)(bitrate % 1000u),
          (unsigned long)(st.sclk_hz / 1000000u),
          (unsigned long)((st.sclk_hz / 10000u) % 100u));
    }
    diag_show_status();
    P("  (CPU 周期由 bsp 在 SPI 事务内累计; 不含驱动的 memcpy/协议处理)\r\n");
}

static void diag_reg(uint32_t n)
{
    s2500_dev_t *dev = s2500_app_dev();
    uint32_t i, err = 0, t0, dt, v = 0;

    if (!dev || n == 0) {
        P("usage: spi reg <n>\r\n");
        return;
    }
    t0 = bsp_millis();
    for (i = 0; i < n; i++) {
        v = 0;
        if (s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_IDVER, &v) != S2500_OK || v != 0x11u) {
            err++;
        }
    }
    dt = bsp_millis() - t0;
    P("spi reg: %lu 次读 IDVER, 错误=%lu, dt=%lu ms (%.1f 次/ms)\r\n",
      (unsigned long)n, (unsigned long)err, (unsigned long)dt,
      dt ? (double)n / (double)dt : 0.0);
}

static void diag_loop(uint32_t n)
{
    s2500_dev_t *dev = s2500_app_dev();
    uint32_t i, ok = 0, fail = 0;

    if (!dev || n == 0) {
        P("usage: spi loop <n>\r\n");
        return;
    }
    for (i = 0; i < n; i++) {
        s2500_lb_result_t res;
        if (s2500_loopback_test_ex(dev, 1, 100, &res) == S2500_OK) {
            ok++;
        } else {
            fail++;
        }
    }
    P("spi loop(PCS 环回): %lu 次 成功=%lu 失败=%lu\r\n",
      (unsigned long)n, (unsigned long)ok, (unsigned long)fail);
}

static int diag_spi_cmd(int argc, char **argv)
{
    uint32_t v = 0;

    if (argc < 2) {
        diag_show_status();
        return 0;
    }
    if (!strcmp(argv[1], "speed")) {
        if (argc >= 3 && diag_parse_u32(argv[2], &v) == 0) {
            if (bsp_spi_set_hz(v * 1000000u) != 0) {
                P("设置失败\r\n");
                return 0;
            }
        }
        diag_show_status();
        return 0;
    }
    if (!strcmp(argv[1], "dma")) {
        if (argc >= 3 && diag_parse_u32(argv[2], &v) == 0) {
            bsp_spi_set_dma(v ? 1 : 0);
        }
        P("DMA=%s\r\n", bsp_spi_dma() ? "on" : "off");
        return 0;
    }
    if (!strcmp(argv[1], "bench")) {
        if (argc >= 3 && diag_parse_u32(argv[2], &v) == 0) {
            diag_bench(v);
        } else {
            P("usage: spi bench <n>\r\n");
        }
        return 0;
    }
    if (!strcmp(argv[1], "reg")) {
        if (argc >= 3 && diag_parse_u32(argv[2], &v) == 0) {
            diag_reg(v);
        } else {
            P("usage: spi reg <n>\r\n");
        }
        return 0;
    }
    if (!strcmp(argv[1], "loop")) {
        if (argc >= 3 && diag_parse_u32(argv[2], &v) == 0) {
            diag_loop(v);
        } else {
            P("usage: spi loop <n>\r\n");
        }
        return 0;
    }
    if (!strcmp(argv[1], "reset")) {
        bsp_spi_reset_stats();
        P("SPI 统计已清零\r\n");
        return 0;
    }
    if (!strcmp(argv[1], "selftest")) {
        P("DMA 自检: %s\r\n", bsp_spi_dma_selftest() ? "通过" : "失败 (已回退阻塞)");
        return 0;
    }
    P("usage: spi [speed <MHz> | dma <0|1> | bench <n> | reg <n> | loop <n> | reset | selftest]\r\n");
    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 spi, diag_spi_cmd, SPI1 诊断: spi [speed|dma|bench|reg|loop|selftest]);
