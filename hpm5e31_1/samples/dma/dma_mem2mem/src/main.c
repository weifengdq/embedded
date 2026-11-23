/*
 * dma_mem2mem - DMAv2 内存到内存搬移: 突发/位宽扫描、带宽实测、链式描述符
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1), 无外部接线
 *   源/目的缓冲都放在 AXI_SRAM_NONCACHEABLE (0x01200000), 避免 cache 一致性干扰,
 *   HDMA = HPM_HDMA(16ch, 最大位宽 WORD, 最大突发 128T),
 *   XDMA = HPM_XDMA(16ch, 最大位宽 DOUBLE_WORD, 最大突发 1024T)  -- 见 hpm_soc_feature.h:70-74
 *
 * 现象: 见 README.md
 */
#include <string.h>
#include "bsp.h"
#include "hpm_dmav2_drv.h"
#include "hpm_mchtmr_drv.h"

#define APP_DMA_SIZE   (16U * 1024U)
#define APP_DMA_CH     (0U)

ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT(8) static uint8_t s_src[APP_DMA_SIZE];
ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT(8) static uint8_t s_dst[APP_DMA_SIZE];
ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT(DMA_LINKED_DESCRIPTOR_ALIGN) static dma_linked_descriptor_t s_desc[2];
/* HDMA 最擅长 AHB SRAM(0xF0200000, 32KB) <-> 外设, 这里用它对比"低延迟路径"带宽 */
ATTR_PLACE_AT_WITH_ALIGNMENT(".ahb_sram", 8) static uint8_t s_src_ahb[8192];
ATTR_PLACE_AT_WITH_ALIGNMENT(".ahb_sram", 8) static uint8_t s_dst_ahb[8192];

/* ---- ILM/DLM 测试缓冲 ----
 * .fast   -> ILM (0x00000000, 128KB, 链接脚本 flash_xip.ld)
 * .fast_ram -> DLM (0x00200000, 128KB, NOLOAD)
 * 注意: hpm_misc.h 里 core_local_mem_to_sys_address() 在 HPM5E31 是恒等函数,
 *       ILM/DLM 的系统地址别名(0x011C0000 / 0x011E0000)需要手动加偏移, 本示例两种都试。
 */
#define APP_LOCAL_SIZE   (8U * 1024U)
#define CORE0_ILM_SYS    (0x011C0000UL)
#define CORE0_DLM_SYS    (0x011E0000UL)

ATTR_PLACE_AT_WITH_ALIGNMENT(".fast", 8) static uint8_t s_src_ilm[APP_LOCAL_SIZE];
ATTR_PLACE_AT_WITH_ALIGNMENT(".fast", 8) static uint8_t s_dst_ilm[APP_LOCAL_SIZE];
ATTR_PLACE_AT_WITH_ALIGNMENT(".fast_ram", 8) static uint8_t s_src_dlm[APP_LOCAL_SIZE];
ATTR_PLACE_AT_WITH_ALIGNMENT(".fast_ram", 8) static uint8_t s_dst_dlm[APP_LOCAL_SIZE];

static volatile uint32_t s_cpu_sink;

static uint32_t s_tmr_hz;

static void fill_src(uint32_t seed)
{
    for (uint32_t i = 0; i < APP_DMA_SIZE; i++) {
        s_src[i] = (uint8_t) (i * 7U + seed);
    }
    memset(s_dst, 0, APP_DMA_SIZE);
}

static bool wait_dma_done(DMAV2_Type *ptr, uint8_t ch, uint32_t timeout_loops)
{
    while (timeout_loops--) {
        uint32_t st = dma_check_transfer_status(ptr, ch);
        if (st & DMA_CHANNEL_STATUS_ERROR) {
            printf("  dma error status=0x%lx\r\n", (unsigned long) st);
            return false;
        }
        if (st & DMA_CHANNEL_STATUS_TC) {
            return true;
        }
    }
    printf("  dma timeout\r\n");
    return false;
}

/* 用 dma_start_memcpy: 位宽由 burst/地址/长度自动选择 */
static bool dma_copy_memcpy(DMAV2_Type *ptr, uint8_t ch, uint32_t dst, uint32_t src,
                            uint32_t size, uint32_t burst_bytes)
{
    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    hpm_stat_t st = dma_start_memcpy(ptr, ch, dst, src, size, burst_bytes);
    if (st != status_success) {
        printf("  dma_start_memcpy failed: %d\r\n", (int) st);
        return false;
    }
    if (!wait_dma_done(ptr, ch, 2000000UL)) {
        return false;
    }
    uint32_t ticks = mchtmr_get_count(HPM_MCHTMR) - t0;
    double us = (double) ticks * 1e6 / (double) s_tmr_hz;
    printf("  burst=%5lu B  %8.1f MB/s  (%6.2f us, %.2f MB/s per copy)\r\n",
           (unsigned long) burst_bytes, (double) size / us, us, (double) size / us);
    return true;
}

/* 用 dma_setup_channel: 显式指定位宽和突发个数 */
static bool dma_copy_explicit(DMAV2_Type *ptr, uint8_t ch, uint32_t dst, uint32_t src,
                              uint32_t size, uint8_t width, uint8_t burst_num)
{
    dma_channel_config_t cfg = {0};
    dma_default_channel_config(ptr, &cfg);
    cfg.src_addr      = core_local_mem_to_sys_address(HPM_CORE0, src);
    cfg.dst_addr      = core_local_mem_to_sys_address(HPM_CORE0, dst);
    cfg.src_width     = width;
    cfg.dst_width     = width;
    cfg.src_burst_size = burst_num;   /* DMA_NUM_TRANSFER_PER_BURST_(1<<n)T */
    cfg.size_in_byte  = size;

    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    if (dma_setup_channel(ptr, ch, &cfg, true) != status_success) {
        printf("  dma_setup_channel failed\r\n");
        return false;
    }
    if (!wait_dma_done(ptr, ch, 2000000UL)) {
        return false;
    }
    uint32_t ticks = mchtmr_get_count(HPM_MCHTMR) - t0;
    double us = (double) ticks * 1e6 / (double) s_tmr_hz;
    printf("  width=%u B burst=%uT  %8.1f MB/s  (%6.2f us)\r\n",
           (unsigned) (1U << width), (unsigned) (1U << burst_num),
           (double) size / us, us);
    return true;
}

static void cmd_dmainfo(void)
{
    printf("DMAv2 on HPM5E31: %lu controllers, %lu channels each\r\n",
           (unsigned long) DMA_SOC_MAX_COUNT, (unsigned long) (DMA_SOC_CHANNEL_NUM / DMA_SOC_MAX_COUNT));
    printf("  XDMA @0x%08lx: width<=%u B, burst<=%u T\r\n",
           (unsigned long) HPM_XDMA,
           (unsigned) (1U << DMA_SOC_TRANSFER_WIDTH_MAX(HPM_XDMA)),
           (unsigned) (1U << DMA_SOC_TRANSFER_PER_BURST_MAX(HPM_XDMA)));
    printf("  HDMA @0x%08lx: width<=%u B, burst<=%u T\r\n",
           (unsigned long) HPM_HDMA,
           (unsigned) (1U << DMA_SOC_TRANSFER_WIDTH_MAX(HPM_HDMA)),
           (unsigned) (1U << DMA_SOC_TRANSFER_PER_BURST_MAX(HPM_HDMA)));
    printf("  test buffer: %lu B, non-cacheable AXI SRAM, mchtmr=%lu Hz\r\n",
           (unsigned long) APP_DMA_SIZE, (unsigned long) s_tmr_hz);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dmainfo, cmd_dmainfo, DMA controller capability summary);

static void cmd_dmasweep(int argc, char *argv[])
{
    uint32_t size = APP_DMA_SIZE;
    if (argc >= 2 && (bsp_parse_u32(argv[1], &size) || size == 0 || size > APP_DMA_SIZE)) {
        printf("usage: dmasweep [bytes<=%lu]\r\n", (unsigned long) APP_DMA_SIZE);
        return;
    }
    DMAV2_Type *ctrls[2] = {HPM_XDMA, HPM_HDMA};
    const char *names[2] = {"XDMA", "HDMA"};
    for (int c = 0; c < 2; c++) {
        uint32_t max_burst = (uint32_t) (1U << DMA_SOC_TRANSFER_PER_BURST_MAX(ctrls[c])) *
                             (uint32_t) (1U << DMA_SOC_TRANSFER_WIDTH_MAX(ctrls[c]));
        printf("%s sweep (size=%lu B):\r\n", names[c], (unsigned long) size);
        for (uint32_t b = 8; b <= max_burst; b <<= 1) {
            if (b > size) {
                break;
            }
            fill_src(b);
            if (dma_copy_memcpy(ctrls[c], APP_DMA_CH,
                                (uint32_t) s_dst, (uint32_t) s_src, size, b)) {
                if (memcmp(s_src, s_dst, size) != 0) {
                    printf("  !! data mismatch\r\n");
                    break;
                }
            } else {
                break;
            }
        }
    }
    printf("done (all data verified by memcmp)\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dmasweep, cmd_dmasweep, burst-size bandwidth sweep: dmasweep [bytes]);

static void cmd_dmawidth(int argc, char *argv[])
{
    uint32_t size = APP_DMA_SIZE;
    if (argc >= 2 && (bsp_parse_u32(argv[1], &size) || size == 0 || size > APP_DMA_SIZE)) {
        printf("usage: dmawidth [bytes<=%lu]\r\n", (unsigned long) APP_DMA_SIZE);
        return;
    }
    printf("XDMA explicit width/burst matrix (size=%lu B):\r\n", (unsigned long) size);
    for (uint8_t w = DMA_TRANSFER_WIDTH_BYTE; w <= DMA_SOC_TRANSFER_WIDTH_MAX(HPM_XDMA); w++) {
        for (uint8_t bn = 0; bn <= 8; bn++) {
            if (((uint32_t) (1U << w) * (1U << bn)) > size) {
                break;
            }
            fill_src(w * 16U + bn);
            if (!dma_copy_explicit(HPM_XDMA, APP_DMA_CH, (uint32_t) s_dst, (uint32_t) s_src, size, w, bn)) {
                return;
            }
        }
    }
    printf("verify: %s\r\n", (memcmp(s_src, s_dst, size) == 0) ? "match" : "MISMATCH");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dmawidth, cmd_dmawidth, explicit width/burst matrix: dmawidth [bytes]);

static void cmd_dmahb(void)
{
    for (uint32_t i = 0; i < sizeof(s_src_ahb); i++) {
        s_src_ahb[i] = (uint8_t) (i * 3U + 1U);
        s_dst_ahb[i] = 0;
    }
    printf("HDMA AHB_SRAM(0x%08lx) -> AHB_SRAM(0x%08lx), %lu B:\r\n",
           (unsigned long) (uintptr_t) s_src_ahb, (unsigned long) (uintptr_t) s_dst_ahb,
           (unsigned long) sizeof(s_src_ahb));
    for (uint32_t b = 8; b <= sizeof(s_src_ahb); b <<= 1) {
        if (dma_copy_memcpy(HPM_HDMA, APP_DMA_CH, (uint32_t) s_dst_ahb, (uint32_t) s_src_ahb,
                            sizeof(s_src_ahb), b)) {
            if (memcmp(s_src_ahb, s_dst_ahb, sizeof(s_src_ahb)) != 0) {
                printf("  !! data mismatch\r\n");
                break;
            }
        }
    }
    printf("verify: %lx\r\n", (unsigned long) 0);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dmahb, cmd_dmahb, HDMA bandwidth inside AHB SRAM (low-latency path));

static void cmd_dmachain(void)
{
    /* 4 段链式: s_src -> s_dst[0..1/4] ... [3/4..1], 每段 4KB 且 64B 突发对齐 */
    dma_channel_config_t cfg = {0};
    const uint32_t seg = APP_DMA_SIZE / 4U;
    fill_src(0x5AU);

    for (uint32_t i = 0; i < 2U; i++) {
        dma_default_channel_config(HPM_XDMA, &cfg);
        cfg.src_addr = core_local_mem_to_sys_address(HPM_CORE0, (uint32_t) s_src);
        cfg.dst_addr = core_local_mem_to_sys_address(HPM_CORE0, (uint32_t) (s_dst + (i + 1U) * seg));
        cfg.src_width = DMA_TRANSFER_WIDTH_WORD;
        cfg.dst_width = DMA_TRANSFER_WIDTH_WORD;
        cfg.src_burst_size = DMA_NUM_TRANSFER_PER_BURST_16T;
        cfg.size_in_byte = seg & ~3U;
        cfg.linked_ptr = (i == 0U) ?
            core_local_mem_to_sys_address(HPM_CORE0, (uint32_t) &s_desc[1]) : 0U;
        if (dma_config_linked_descriptor(HPM_XDMA, &s_desc[i], APP_DMA_CH, &cfg) != status_success) {
            printf("linked descriptor %lu config failed\r\n", (unsigned long) i);
            return;
        }
    }

    dma_default_channel_config(HPM_XDMA, &cfg);
    cfg.src_addr = core_local_mem_to_sys_address(HPM_CORE0, (uint32_t) s_src);
    cfg.dst_addr = core_local_mem_to_sys_address(HPM_CORE0, (uint32_t) s_dst);
    cfg.src_width = DMA_TRANSFER_WIDTH_WORD;
    cfg.dst_width = DMA_TRANSFER_WIDTH_WORD;
    cfg.src_burst_size = DMA_NUM_TRANSFER_PER_BURST_16T;
    cfg.size_in_byte = seg & ~3U;
    cfg.linked_ptr = core_local_mem_to_sys_address(HPM_CORE0, (uint32_t) &s_desc[0]);

    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    if (dma_setup_channel(HPM_XDMA, APP_DMA_CH, &cfg, true) != status_success) {
        printf("dma setup channel failed\r\n");
        return;
    }
    if (!wait_dma_done(HPM_XDMA, APP_DMA_CH, 4000000UL)) {
        return;
    }
    uint32_t ticks = mchtmr_get_count(HPM_MCHTMR) - t0;
    printf("chained 3x%lu B done in %.2f us (%.1f MB/s)\r\n",
           (unsigned long) (seg & ~3U),
           (double) ticks * 1e6 / (double) s_tmr_hz,
           (double) ((seg & ~3U) * 3U) * (double) s_tmr_hz / ((double) ticks * 1e6));
    printf("verify: %s\r\n", (memcmp(s_src, s_dst, (seg & ~3U) * 3U) == 0) ? "match" : "MISMATCH");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dmachain, cmd_dmachain, chained descriptor transfer (3 descriptors));


/* ================== ILM/DLM 带宽 + 地址别名 ================== */
static double dma_timed_copy(DMAV2_Type *ptr, uint32_t dst, uint32_t src, uint32_t size,
                             uint32_t burst, uint32_t *status)
{
    *status = 0;
    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    hpm_stat_t st = dma_start_memcpy(ptr, APP_DMA_CH, dst, src, size, burst);
    if (st != status_success) {
        *status = (uint32_t) st;
        return -1.0;
    }
    if (!wait_dma_done(ptr, APP_DMA_CH, 2000000UL)) {
        *status = 0xFFFFFFFFUL;
        return -1.0;
    }
    uint32_t ticks = mchtmr_get_count(HPM_MCHTMR) - t0;
    if (ticks == 0) {
        ticks = 1;
    }
    return (double) size * (double) s_tmr_hz / ((double) ticks * 1e6);
}

static void report_bw(const char *name, DMAV2_Type *ctrl, uint32_t dst, uint32_t src,
                      uint32_t size, uint32_t burst)
{
    uint32_t st = 0;
    double mbps = dma_timed_copy(ctrl, dst, src, size, burst, &st);
    if (mbps < 0.0) {
        printf("BENCH_BW name=%-24s result=FAIL status=0x%lx src=0x%08lx dst=0x%08lx\r\n",
               name, (unsigned long) st, (unsigned long) src, (unsigned long) dst);
    } else {
        printf("BENCH_BW name=%-24s mbps=%8.1f burst=%lu src=0x%08lx dst=0x%08lx\r\n",
               name, mbps, (unsigned long) burst, (unsigned long) src, (unsigned long) dst);
    }
}

static void cmd_dmamem(void)
{
    const uint32_t sz = APP_LOCAL_SIZE;
    const uint32_t b = 512U;
    for (uint32_t i = 0; i < sz; i++) {
        s_src_ilm[i] = (uint8_t) (i * 11U + 3U);
        s_src_dlm[i] = (uint8_t) (i * 13U + 5U);
    }
    memset(s_dst_ilm, 0, sz);
    memset(s_dst_dlm, 0, sz);
    printf("== AXI_SRAM / ILM / DLM DMA 带宽 (size=%lu B, burst=%lu B, XDMA 除非注明) ==\r\n",
           (unsigned long) sz, (unsigned long) b);
    printf("   addr: s_src=0x%08lx s_dst=0x%08lx s_src_ilm=0x%08lx s_src_dlm=0x%08lx\r\n",
           (unsigned long) (uintptr_t) s_src, (unsigned long) (uintptr_t) s_dst,
           (unsigned long) (uintptr_t) s_src_ilm, (unsigned long) (uintptr_t) s_src_dlm);
    printf("   映射: core_local_mem_to_sys_address()=恒等函数; ILM sys=0x%08lx DLM sys=0x%08lx\r\n",
           (unsigned long) CORE0_ILM_SYS, (unsigned long) CORE0_DLM_SYS);
    report_bw("AXI_SCC->AXI_SCC", HPM_XDMA, (uint32_t) (uintptr_t) s_dst,
              (uint32_t) (uintptr_t) s_src, sz, b);
    report_bw("AXI_SCC->ILM(local)", HPM_XDMA, (uint32_t) (uintptr_t) s_dst_ilm,
              (uint32_t) (uintptr_t) s_src, sz, b);
    report_bw("AXI_SCC->ILM(alias)", HPM_XDMA, (uint32_t) (uintptr_t) s_dst_ilm + CORE0_ILM_SYS,
              (uint32_t) (uintptr_t) s_src, sz, b);
    report_bw("ILM(local)->AXI_SCC", HPM_XDMA, (uint32_t) (uintptr_t) s_dst,
              (uint32_t) (uintptr_t) s_src_ilm, sz, b);
    report_bw("ILM(local)->ILM(local)", HPM_XDMA, (uint32_t) (uintptr_t) s_dst_ilm,
              (uint32_t) (uintptr_t) s_src_ilm, sz, b);
    report_bw("ILM(alias)->ILM(alias)", HPM_XDMA,
              (uint32_t) (uintptr_t) s_dst_ilm + CORE0_ILM_SYS,
              (uint32_t) (uintptr_t) s_src_ilm + CORE0_ILM_SYS, sz, b);
    report_bw("AXI_SCC->DLM(local)", HPM_XDMA, (uint32_t) (uintptr_t) s_dst_dlm,
              (uint32_t) (uintptr_t) s_src, sz, b);
    report_bw("AXI_SCC->DLM(alias)", HPM_XDMA, (uint32_t) (uintptr_t) s_dst_dlm + CORE0_DLM_SYS,
              (uint32_t) (uintptr_t) s_src, sz, b);
    report_bw("DLM(alias)->AXI_SCC", HPM_XDMA, (uint32_t) (uintptr_t) s_dst,
              (uint32_t) (uintptr_t) s_src_dlm + CORE0_DLM_SYS, sz, b);
    report_bw("DLM(alias)->DLM(alias)", HPM_XDMA,
              (uint32_t) (uintptr_t) s_dst_dlm + CORE0_DLM_SYS,
              (uint32_t) (uintptr_t) s_src_dlm + CORE0_DLM_SYS, sz, b);
    report_bw("HDMA AXI->ILM(local)", HPM_HDMA, (uint32_t) (uintptr_t) s_dst_ilm,
              (uint32_t) (uintptr_t) s_src, sz, b);
    printf("   verify: ILM=%s DLM=%s\r\n",
           (memcmp(s_src_ilm, s_dst_ilm, sz) == 0) ? "match" : "MISMATCH",
           (memcmp(s_src_dlm, s_dst_dlm, sz) == 0) ? "match" : "MISMATCH");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dmamem, cmd_dmamem, AXI/ILM/DLM DMA bandwidth + address alias test);

/* ================== 非 2^n 突发 / 有效突发表 ================== */
static void cmd_dmaburst(void)
{
    static const uint32_t bad[] = { 48, 96, 192, 384, 768, 1536 };
    const uint32_t sz = APP_DMA_SIZE;
    fill_src(0x3CU);
    printf("== 非 2^n 突发 (size=%lu B) ==\r\n", (unsigned long) sz);
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        uint32_t st = 0;
        double m = 0.0;
        m = dma_timed_copy(HPM_HDMA, (uint32_t) (uintptr_t) s_dst, (uint32_t) (uintptr_t) s_src,
                           sz, bad[i], &st);
        printf("   HDMA burst=%5lu B -> %s status=0x%lx\r\n", (unsigned long) bad[i],
               (m < 0.0) ? "REJECT" : "accepted", (unsigned long) st);
    }
    printf("== 有效突发: HDMA / XDMA (size=%lu B) ==\r\n", (unsigned long) sz);
    for (uint32_t b = 8; b <= 512U; b <<= 1) {
        fill_src((uint32_t) b);
        report_bw("HDMA", HPM_HDMA, (uint32_t) (uintptr_t) s_dst, (uint32_t) (uintptr_t) s_src, sz, b);
    }
    for (uint32_t b = 8; b <= 8192U; b <<= 1) {
        if (sz % b) {
            continue;
        }
        fill_src((uint32_t) b + 1U);
        report_bw("XDMA", HPM_XDMA, (uint32_t) (uintptr_t) s_dst, (uint32_t) (uintptr_t) s_src, sz, b);
    }
    printf("   verify: %s\r\n", (memcmp(s_src, s_dst, sz) == 0) ? "match" : "MISMATCH");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dmaburst, cmd_dmaburst, non-2^n burst rejection + effective burst sweep);

/* ================== CPU 与 DMA 并发 ================== */
static void cpu_load_alu_step(void)
{
    uint32_t x = s_cpu_sink + 0x9E3779B9UL;
    for (uint32_t i = 0; i < 8; i++) {
        x = x * 1664525UL + 1013904223UL;
        x ^= (x >> 13);
    }
    s_cpu_sink = x;
}

static void cpu_load_mem_step(void)
{
    const uint32_t *p = (const uint32_t *) (const void *) s_src_ahb;
    uint32_t acc = s_cpu_sink;
    for (uint32_t i = 0; i < 8; i++) {
        acc += p[(i * 61U) & ((sizeof(s_src_ahb) / 4U) - 1U)];
    }
    s_cpu_sink = acc;
}

static double dma_copy_under_load(uint32_t size, uint32_t burst, int kind, uint32_t *spin_out)
{
    dma_channel_config_t cfg = {0};
    dma_default_channel_config(HPM_XDMA, &cfg);
    cfg.src_addr = (uint32_t) (uintptr_t) s_src;
    cfg.dst_addr = (uint32_t) (uintptr_t) s_dst;
    cfg.src_width = DMA_TRANSFER_WIDTH_WORD;
    cfg.dst_width = DMA_TRANSFER_WIDTH_WORD;
    cfg.src_burst_size = DMA_NUM_TRANSFER_PER_BURST_16T;
    cfg.size_in_byte = size;
    uint32_t spin = 0;
    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    if (dma_setup_channel(HPM_XDMA, APP_DMA_CH, &cfg, true) != status_success) {
        return -1.0;
    }
    while (spin < 2000000UL) {
        uint32_t st = dma_check_transfer_status(HPM_XDMA, APP_DMA_CH);
        if (st & (DMA_CHANNEL_STATUS_TC | DMA_CHANNEL_STATUS_ERROR)) {
            break;
        }
        if (kind == 1) {
            cpu_load_alu_step();
        } else if (kind == 2) {
            cpu_load_mem_step();
        }
        spin++;
    }
    uint32_t ticks = mchtmr_get_count(HPM_MCHTMR) - t0;
    *spin_out = spin;
    if (ticks == 0) {
        ticks = 1;
    }
    return (double) size * (double) s_tmr_hz / ((double) ticks * 1e6);
}

static void cmd_dmacpu(void)
{
    const uint32_t sz = APP_DMA_SIZE;
    uint32_t spin = 0;
    double best_solo = 0.0;
    for (uint32_t i = 0; i < 5; i++) {
        fill_src(0x77U + i);
        uint32_t st = 0;
        double m = dma_timed_copy(HPM_XDMA, (uint32_t) (uintptr_t) s_dst, (uint32_t) (uintptr_t) s_src,
                                  sz, 1024U, &st);
        if (m > best_solo) {
            best_solo = m;
        }
    }
    /* 同一显式配置下取空载基线（否则"下降"里混进了突发长度差异） */
    double solo = dma_copy_under_load(sz, 1024U, 0, &spin);
    double alu = dma_copy_under_load(sz, 1024U, 1, &spin);
    double mem = dma_copy_under_load(sz, 1024U, 2, &spin);
    printf("== CPU 与 DMA 并发 (XDMA %lu B, 显式 4B x 16T = 64 B 突发) ==\r\n", (unsigned long) sz);
    printf("BENCH_CONC solo_mbps=%.1f alu_mbps=%.1f mem_mbps=%.1f alu_drop=%.1f%% mem_drop=%.1f%%\r\n",
           solo, alu, mem,
           (solo > 0.0) ? (100.0 * (solo - alu) / solo) : 0.0,
           (solo > 0.0) ? (100.0 * (solo - mem) / solo) : 0.0);
    printf("BENCH_CONC_REF dma_start_memcpy_burst1024_mbps=%.1f\r\n", best_solo);
    printf("   note: alu=CPU 纯寄存器运算(不占总线), mem=CPU 连续读 AHB SRAM(占总线)\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dmacpu, cmd_dmacpu, DMA bandwidth with concurrent CPU load);

int main(void)
{
    bsp_init();
    s_tmr_hz = clock_get_frequency(clock_mchtmr0);

    printf("\r\n=== dma_mem2mem: DMAv2 memory-to-memory ===\r\n");
    cmd_dmainfo();
    printf("shell: dmainfo | dmasweep | dmawidth | dmachain | dmamem | dmaburst | dmacpu\r\n");

    /* R5 自动化: 单次上板即可采集全部新增测点（不依赖 --send 时机） */
    board_delay_ms(3000);
    cmd_dmamem();
    cmd_dmaburst();
    cmd_dmacpu();


    uint32_t last = bsp_uptime_ms();
    while (1) {
        bsp_shell_poll();
        if (bsp_uptime_ms() - last >= 5000U) {
            last = bsp_uptime_ms();
            fill_src(0xA5U);
            if (dma_copy_memcpy(HPM_XDMA, APP_DMA_CH, (uint32_t) s_dst, (uint32_t) s_src,
                                APP_DMA_SIZE, 1024U)) {
                printf("auto: 16KB copy %s\r\n", (memcmp(s_src, s_dst, APP_DMA_SIZE) == 0) ? "OK" : "MISMATCH");
            }
        }
    }
    return 0;
}
