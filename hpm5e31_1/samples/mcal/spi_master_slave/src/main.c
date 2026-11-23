/*
 * spi_master_slave —— AUTOSAR MCAL Spi 例程：SPI1(主) <-> SPI3(从) 双向 1 MB 传输
 *
 * 接线（板级杜邦线，见 docs/MCAL开发约定.md §2）：
 *   SPI1_nCS PC11 <-> SPI3_nCS PA27
 *   SPI1_SCLK PC10 <-> SPI3_SCLK PA26
 *   SPI1_MISO PC12 <-> SPI3_MISO PA28
 *   SPI1_MOSI PC13 <-> SPI3_MOSI PA29
 *
 * 只用 MCAL API（Spi_Init/Spi_SetupEB/Spi_SyncTransmit/...），不直接碰 SPI 寄存器。
 * 命令：spi info | spi init 1m|10m | spi xfer [bytes] | spi width 8|16|32 [count]
 *       spi bench | spi ber [MB] [chunk] | spi ib | spi det
 */
#include "bsp.h"
#include "Spi.h"
#include "Spi_PBcfg.h"
#include "Det.h"
#include "hpm_csr_drv.h"
#include "hpm_soc.h"
#include "hpm_spi_drv.h"
#include "hpm_dmav2_drv.h"
#include "hpm_dmamux_drv.h"

#include <stdio.h>
#include <string.h>

#define BUF_SIZE (4096u)

ATTR_PLACE_AT_NONCACHEABLE static uint8 s_m_tx[BUF_SIZE + 16u];
ATTR_PLACE_AT_NONCACHEABLE static uint8 s_m_rx[BUF_SIZE + 16u];
ATTR_PLACE_AT_NONCACHEABLE static uint8 s_s_tx[BUF_SIZE + 16u];
ATTR_PLACE_AT_NONCACHEABLE static uint8 s_s_rx[BUF_SIZE + 16u];

static volatile uint32 s_job_end_cnt;
static volatile uint32 s_seq_end_cnt;
static uint32 s_cpu_hz;
static uint32 s_sclk_hz = 1000000u;
static const char *s_cfg_name = "1MHz";

/* 覆盖 Spi_PBcfg.c 的弱符号：验证 Job/Seq 结束通知确实被驱动回调 */
void Spi_JobEndNotification_HW(void) { s_job_end_cnt++; }
void Spi_SeqEndNotification_Seq(void) { s_seq_end_cnt++; }

static inline uint64_t cyc(void) { return hpm_csr_get_core_mcycle(); }

static void spi_channels(uint8 widthB, Spi_ChannelType *chm, Spi_ChannelType *chs, Spi_SequenceType *seq)
{
    if (widthB == 1u) {
        *chm = SPI_CH_MASTER_8BIT; *chs = SPI_CH_SLAVE_8BIT;  *seq = SPI_SEQ_MB8;
    } else if (widthB == 2u) {
        *chm = SPI_CH_MASTER_16BIT; *chs = SPI_CH_SLAVE_16BIT; *seq = SPI_SEQ_MB16;
    } else {
        *chm = SPI_CH_MASTER_32BIT; *chs = SPI_CH_SLAVE_32BIT; *seq = SPI_SEQ_MB32;
    }
}

/* 一次同步双工传输：master 发 A 收 B，slave 发 B 收 A，双向逐字节比对 */
static int spi_bidir(uint32 count, uint8 widthB, uint32 *errs, uint64_t *cycles)
{
    Spi_ChannelType chm;
    Spi_ChannelType chs;
    Spi_SequenceType seq;
    uint32 bytes = count * (uint32)widthB;
    uint32 i;
    Std_ReturnType rc;
    uint64_t t0;
    uint32 e = 0u;

    if ((count == 0u) || (bytes > BUF_SIZE)) { return -1; }
    spi_channels(widthB, &chm, &chs, &seq);
    for (i = 0u; i < bytes; i++) {
        s_m_tx[i] = (uint8)((i * 7u) + 0x31u);
        s_s_tx[i] = (uint8)((i * 11u) + 0xA5u);
        s_m_rx[i] = 0u;
        s_s_rx[i] = 0u;
    }
    /* HPM 从机 TX FIFO 在首个 SCLK 前会欠载，多发 1 个元素补偿（见 Spi_PBcfg.c ExtraTxElements） */
    s_s_tx[bytes] = 0x5Au;

    if (Spi_SetupEB(chm, s_m_tx, s_m_rx, (Spi_NumberOfDataType)count) != E_OK) { return -2; }
    if (Spi_SetupEB(chs, s_s_tx, s_s_rx, (Spi_NumberOfDataType)count) != E_OK) { return -3; }
    t0 = cyc();
    rc = Spi_SyncTransmit(seq);
    *cycles = cyc() - t0;

    for (i = 0u; i < bytes; i++) {
        if (s_m_rx[i] != s_s_tx[i]) { e++; }
        if (s_s_rx[i] != s_m_tx[i]) { e++; }
    }
    *errs = e;
    if (e != 0u) {
        printf("[x] n=%lu w=%u bw=%ld\r\n", (unsigned long)count, (unsigned)widthB, (long)rc);
        printf("[x] m_tx:"); for (i = 0u; i < 6u; i++) { printf(" %02X", s_m_tx[i]); }
        printf(" | m_rx:"); for (i = 0u; i < 6u; i++) { printf(" %02X", s_m_rx[i]); }
        printf(" | s_tx:"); for (i = 0u; i < 6u; i++) { printf(" %02X", s_s_tx[i]); }
        printf(" | s_rx:"); for (i = 0u; i < 6u; i++) { printf(" %02X", s_s_rx[i]); }
        printf("\r\n");
    }
    if (rc != E_OK) { return -4; }
    return (e == 0u) ? 0 : -5;
}

static void report(const char *tag, uint32 count, uint8 widthB, int rc, uint32 errs, uint64_t cycles)
{
    uint32 bytes = count * (uint32)widthB;
    uint32 ns = (uint32)((cycles * 1000000000ull) / (uint64_t)s_cpu_hz);
    uint32 kbs;
    if (cycles == 0u) { cycles = 1u; }
    kbs = (uint32)(((uint64_t)bytes * 2ull * (uint64_t)s_cpu_hz) / cycles / 1000ull);
    printf("  %-6s %-5s n=%-5lu w=%-2u %s err=%-4lu t=%lu.%03lu us 双向=%lu KB/s\r\n",
           tag, s_cfg_name, (unsigned long)count, (unsigned)widthB,
           ((rc == 0) && (errs == 0u)) ? "PASS" : "FAIL",
           (unsigned long)errs, (unsigned long)(ns / 1000u), (unsigned long)(ns % 1000u),
           (unsigned long)kbs);
}

static void cmd_info(void)
{
    Std_VersionInfoType vi;
    Spi_GetVersionInfo(&vi);
    printf("spi_master_slave @ hpm5e31_1 | MCAL Spi %u.%u.%u moduleID=%u\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, (unsigned)vi.moduleID);
    printf("  cfg=%s SCLK=%lu Hz level=%u status=%s\r\n", s_cfg_name,
           (unsigned long)s_sclk_hz, (unsigned)SPI_LEVEL_DELIVERED,
           (Spi_GetStatus() == SPI_IDLE) ? "IDLE" : ((Spi_GetStatus() == SPI_BUSY) ? "BUSY" : "UNINIT"));
    printf("  SPI1 master: SCLK PC10 CS PC11 MISO PC12 MOSI PC13 (mode0, DMA ch2/3)\r\n");
    printf("  SPI3 slave : SCLK PA26 CS PA27 MISO PA28 MOSI PA29 (DMA ch4/5)\r\n");
    printf("  jobs=%u seqs=%u eb buf=%u B, jobEnd=%lu seqEnd=%lu\r\n",
           (unsigned)SPI_JOB_COUNT, (unsigned)SPI_SEQUENCE_COUNT, (unsigned)BUF_SIZE,
           (unsigned long)s_job_end_cnt, (unsigned long)s_seq_end_cnt);
    printf("  usage: spi info|init 1m|10m|xfer [n]|width 8|16|32 [n]|bench|ber [MB] [chunk]|ib|det\r\n");
}

static void cmd_init(int argc, char **argv)
{
    const Spi_ConfigType *cfg = &Spi_Config;
    uint32 hz = 1000000u;
    const char *name = "1MHz";
    if ((argc >= 1) && (strcmp(argv[0], "10m") == 0)) {
        cfg = &Spi_Config_10M; hz = 10000000u; name = "10MHz";
    }
    if (Spi_GetStatus() != SPI_UNINIT) {
        if (Spi_DeInit() != E_OK) { printf("Spi_DeInit failed\r\n"); return; }
    }
    Spi_Init(cfg);
    if (Spi_GetStatus() == SPI_UNINIT) { printf("Spi_Init failed\r\n"); return; }
    s_sclk_hz = hz;
    s_cfg_name = name;
    printf("spi: re-init -> cfg=%s SCLK=%lu Hz status=IDLE\r\n", name, (unsigned long)hz);
}

static void cmd_xfer(int argc, char **argv)
{
    uint32_t n = 64u;
    uint32 errs = 0u;
    uint64_t cy = 0u;
    int rc;
    if (argc >= 1) { (void)bsp_parse_u32(argv[0], &n); }
    if ((n == 0u) || (n > BUF_SIZE)) { printf("usage: spi xfer [1..%u]\r\n", (unsigned)BUF_SIZE); return; }
    rc = spi_bidir(n, 1u, &errs, &cy);
    report("bidir", n, 1u, rc, errs, cy);
}

static void cmd_width(int argc, char **argv)
{
    uint32_t w = 8u;
    uint32_t n = 64u;
    uint32 errs = 0u;
    uint64_t cy = 0u;
    int rc;
    if (argc >= 1) { (void)bsp_parse_u32(argv[0], &w); }
    if (argc >= 2) { (void)bsp_parse_u32(argv[1], &n); }
    if ((w != 8u) && (w != 16u) && (w != 32u)) { printf("usage: spi width 8|16|32 [count]\r\n"); return; }
    if (n > (BUF_SIZE / (w / 8u))) { n = BUF_SIZE / (w / 8u); }
    rc = spi_bidir(n, (uint8)(w / 8u), &errs, &cy);
    report("width", n, (uint8)(w / 8u), rc, errs, cy);
}

static void cmd_bench(void)
{
    static const uint32 sizes[3] = { 64u, 256u, 1024u };
    uint32 i;
    printf("bench: 双向传输 1 MHz / 10 MHz 对比\r\n");
    for (i = 0u; i < 3u; i++) {
        uint32 errs = 0u;
        uint64_t cy = 0u;
        int rc = spi_bidir(sizes[i], 1u, &errs, &cy);
        report("1MHz", sizes[i], 1u, rc, errs, cy);
    }
    {
        char *a[2];
        a[0] = (char *)"10m"; a[1] = NULL;
        cmd_init(1, a);
    }
    for (i = 0u; i < 3u; i++) {
        uint32 errs = 0u;
        uint64_t cy = 0u;
        int rc = spi_bidir(sizes[i], 1u, &errs, &cy);
        report("10MHz", sizes[i], 1u, rc, errs, cy);
    }
    {
        char *a[2];
        a[0] = (char *)"1m"; a[1] = NULL;
        cmd_init(1, a);
    }
}

/* 连续 N MB 传输：按 chunk 分块；统计误码与吞吐 */
static void cmd_ber(int argc, char **argv)
{
    uint32_t mb = 1u;
    uint32_t chunk = 1024u;
    uint64_t total;
    uint64_t done = 0u;
    uint64_t err_bytes = 0u;
    uint64_t fail = 0u;
    uint64_t xfers = 0u;
    uint64_t t0;
    uint64_t dt;
    if (argc >= 1) { (void)bsp_parse_u32(argv[0], &mb); }
    if (argc >= 2) { (void)bsp_parse_u32(argv[1], &chunk); }
    if (mb == 0u) { mb = 1u; }
    if (mb > 4u) { mb = 4u; }
    if ((chunk == 0u) || (chunk > BUF_SIZE)) { chunk = 1024u; }
    total = (uint64_t)mb * 1024ull * 1024ull;
    t0 = cyc();
    while (done < total) {
        uint32 n = chunk;
        uint32 errs = 0u;
        uint64_t cy = 0u;
        int rc;
        if ((total - done) < (uint64_t)n) { n = (uint32)(total - done); }
        rc = spi_bidir(n, 1u, &errs, &cy);
        if (rc != 0) { fail++; }
        err_bytes += errs;
        done += n;
        xfers++;
    }
    dt = cyc() - t0;
    {
        uint64_t ms100 = dt * 100000ull / (uint64_t)s_cpu_hz;
        uint64_t kbs = (dt != 0u) ? (done * (uint64_t)s_cpu_hz / dt / 1000ull) : 0ull;
        printf("ber: %lu MB @ %s chunk=%lu xfers=%lu fail=%lu err_bytes=%lu, %lu.%02lu ms\r\n",
               (unsigned long)mb, s_cfg_name, (unsigned long)chunk, (unsigned long)xfers,
               (unsigned long)fail, (unsigned long)err_bytes,
               (unsigned long)(ms100 / 100ull), (unsigned long)(ms100 % 100ull));
        printf("     单向吞吐=%lu KB/s (%lu.%02lu MB/s)，双向=%lu KB/s，误码=%lu/%lu bit => %s\r\n",
               (unsigned long)kbs, (unsigned long)(kbs / 1024ull),
               (unsigned long)((kbs % 1024ull) * 100ull / 1024ull),
               (unsigned long)(kbs * 2ull),
               (unsigned long)err_bytes, (unsigned long)(done * 16ull),
               ((fail == 0u) && (err_bytes == 0u)) ? "PASS (0 误码)" : "FAIL");
    }
}

static void cmd_ib(void)
{
    uint8 txm[64];
    uint8 txs[64];
    uint8 rxm[64];
    uint8 rxs[64];
    uint32 i;
    uint32 errs = 0u;
    uint64_t cy = 0u;
    uint64_t t0;
    Std_ReturnType rc;
    for (i = 0u; i < 64u; i++) {
        txm[i] = (uint8)(i + 0x10u);
        txs[i] = (uint8)(0xFFu - i);
        rxm[i] = 0u;
        rxs[i] = 0u;
    }
    if (Spi_WriteIB(SPI_CH_MASTER_IB, txm) != E_OK) { printf("spi ib: WriteIB master failed\r\n"); return; }
    if (Spi_WriteIB(SPI_CH_SLAVE_IB, txs) != E_OK) { printf("spi ib: WriteIB slave failed\r\n"); return; }
    t0 = cyc();
    rc = Spi_SyncTransmit(SPI_SEQ_IB8);
    cy = cyc() - t0;
    if (Spi_ReadIB(SPI_CH_MASTER_IB, rxm) != E_OK) { printf("spi ib: ReadIB master failed\r\n"); return; }
    if (Spi_ReadIB(SPI_CH_SLAVE_IB, rxs) != E_OK) { printf("spi ib: ReadIB slave failed\r\n"); return; }
    for (i = 0u; i < 64u; i++) {
        if (rxm[i] != txs[i]) { errs++; }
        if (rxs[i] != txm[i]) { errs++; }
    }
    report("ib", 64u, 1u, (rc == E_OK) ? 0 : -1, errs, cy);
    printf("  IB: rc=%d det=%lu masterTx[0..3]=%02X %02X %02X %02X masterRx[0..3]=%02X %02X %02X %02X\r\n",
           (int)rc, (unsigned long)Det_GetErrorCount(), txm[0], txm[1], txm[2], txm[3],
           rxm[0], rxm[1], rxm[2], rxm[3]);
    printf("  IB: slaveTx[0..3]=%02X %02X %02X %02X slaveRx[0..3]=%02X %02X %02X %02X jobR6=%d jobR7=%d seqR3=%d\r\n",
           txs[0], txs[1], txs[2], txs[3], rxs[0], rxs[1], rxs[2], rxs[3],
           (int)Spi_GetJobResult(SPI_JOB_MASTER_IB), (int)Spi_GetJobResult(SPI_JOB_SLAVE_IB),
           (int)Spi_GetSequenceResult(SPI_SEQ_IB8));
}

static void cmd_det(void)
{
    uint32 c0;
    Std_VersionInfoType vi;
    cmd_info();
    c0 = Det_GetErrorCount();
    printf("det: count0=%lu, inject 10 invalid calls...\r\n", (unsigned long)c0);
    (void)Spi_SetupEB(0x7Fu, s_m_tx, s_m_rx, 4u);
    (void)Spi_SyncTransmit(0x7Fu);
    (void)Spi_SetupEB(SPI_CH_MASTER_IB, s_m_tx, s_m_rx, 4u);
    (void)Spi_WriteIB(SPI_CH_MASTER_8BIT, s_m_tx);
    (void)Spi_SetupEB(SPI_CH_MASTER_8BIT, NULL, s_m_rx, 4u);
    (void)Spi_SetupEB(SPI_CH_MASTER_8BIT, s_m_tx, s_m_rx, 0u);
    (void)Spi_GetJobResult(0x7Fu);
    (void)Spi_GetSequenceResult(0x7Fu);
    (void)Spi_GetHWUnitStatus(0x7Fu);
    (void)Spi_Cancel(0x7Fu);
    printf("det: after inject count=%lu (期望 +10)\r\n", (unsigned long)Det_GetErrorCount());
    Spi_GetVersionInfo(&vi);
    printf("det: GetVersionInfo vendor=%04X module=%u sw=%u.%u.%u\r\n", (unsigned)vi.vendorID,
           (unsigned)vi.moduleID, (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version);
    printf("det: status=%s\r\n", (Spi_GetStatus() == SPI_IDLE) ? "IDLE" : "OTHER");
}

/* ---- 诊断用：完全照抄已验证的 SDK 路径（samples/spi/spi1_spi3_master_slave） ---- */
static hpm_stat_t raw_dma_setup(uint8_t ch, uint32_t src, uint32_t dst, uint32_t size, bool fixed_src, bool fixed_dst, uint8_t dma_src)
{
    dma_handshake_config_t cfg;
    dma_default_handshake_config(HPM_HDMA, &cfg);
    cfg.ch_index = ch;
    cfg.src = src;
    cfg.src_fixed = fixed_src;
    cfg.dst = dst;
    cfg.dst_fixed = fixed_dst;
    cfg.data_width = DMA_TRANSFER_WIDTH_BYTE;
    cfg.size_in_byte = size;
    dmamux_config(HPM_DMAMUX, (uint8_t)DMA_SOC_CHN_TO_DMAMUX_CHN(HPM_HDMA, ch), dma_src, true);
    return dma_setup_handshake(HPM_HDMA, &cfg, true);
}

static bool raw_dma_wait(uint8_t ch)
{
    uint64_t t0 = cyc();
    while ((cyc() - t0) < 200000000ull) {
        uint32_t st = dma_check_transfer_status(HPM_HDMA, ch);
        if (st & (DMA_CHANNEL_STATUS_ERROR | DMA_CHANNEL_STATUS_ABORT)) { return false; }
        if (st & DMA_CHANNEL_STATUS_TC) { return true; }
    }
    return false;
}

static void cmd_raw(void)
{
    spi_format_config_t fmt = {0};
    spi_control_config_t mctrl = {0};
    spi_control_config_t sctrl = {0};
    uint32_t n = 64u;
    uint32_t err = 0u;
    uint32_t i;
    static const uint32_t pin_pads[8] = { IOC_PAD_PC10, IOC_PAD_PC11, IOC_PAD_PC12, IOC_PAD_PC13,
                                          IOC_PAD_PA26, IOC_PAD_PA27, IOC_PAD_PA28, IOC_PAD_PA29 };
    (void)pin_pads;

    /* 引脚+时钟：照 old sample */
    init_spi1_master_pins();
    init_spi3_slave_pins();
    s_sclk_hz = board_init_spi_clock(HPM_SPI1);
    (void)board_init_spi_clock(HPM_SPI3);

    spi_master_get_default_timing_config(&(spi_timing_config_t){0});
    {
        spi_timing_config_t t;
        spi_master_get_default_timing_config(&t);
        t.master_config.clk_src_freq_in_hz = s_sclk_hz;
        t.master_config.sclk_freq_in_hz = 1000000u;
        (void)spi_master_timing_init(HPM_SPI1, &t);
    }
    spi_master_get_default_format_config(&fmt);
    fmt.common_config.data_len_in_bits = 8u;
    fmt.common_config.mode = spi_master_mode;
    fmt.common_config.cpol = spi_sclk_low_idle;
    fmt.common_config.cpha = spi_sclk_sampling_odd_clk_edges;
    spi_format_init(HPM_SPI1, &fmt);

    fmt.common_config.data_merge = false;
    fmt.common_config.mosi_bidir = false;
    fmt.common_config.lsb = false;
    fmt.common_config.mode = spi_slave_mode;
    spi_format_init(HPM_SPI3, &fmt);

    spi_master_get_default_control_config(&mctrl);
    mctrl.master_config.cmd_enable = false;
    mctrl.master_config.addr_enable = false;
    mctrl.common_config.tx_dma_enable = false;
    mctrl.common_config.rx_dma_enable = false;
    mctrl.common_config.trans_mode = spi_trans_write_read_together;
    mctrl.common_config.data_phase_fmt = spi_single_io_mode;

    spi_slave_get_default_control_config(&sctrl);
    sctrl.slave_config.slave_data_only = true;
    sctrl.common_config.tx_dma_enable = true;
    sctrl.common_config.rx_dma_enable = true;
    sctrl.common_config.trans_mode = spi_trans_write_read_together;
    sctrl.common_config.data_phase_fmt = spi_single_io_mode;

    for (i = 0; i < n; i++) {
        s_m_tx[i] = (uint8)(i * 7u + 0x31u);
        s_s_tx[i] = (uint8)(i * 11u + 0xA5u);
        s_m_rx[i] = 0; s_s_rx[i] = 0;
    }
    s_s_tx[n] = 0x5Au;
    (void)dma_check_transfer_status(HPM_HDMA, 4u);
    (void)dma_check_transfer_status(HPM_HDMA, 5u);
    (void)raw_dma_setup(5u, (uint32_t)&HPM_SPI3->DATA, (uint32_t)s_s_rx, n, true, false, (uint8_t)HPM_DMA_SRC_SPI3_RX);
    (void)raw_dma_setup(4u, (uint32_t)s_s_tx, (uint32_t)&HPM_SPI3->DATA, n + 1u, false, true, (uint8_t)HPM_DMA_SRC_SPI3_TX);
    if (spi_setup_dma_transfer(HPM_SPI3, &sctrl, NULL, NULL, n, n) != status_success) {
        printf("raw: slave dma setup failed\r\n");
        return;
    }
    {
        hpm_stat_t st = spi_transfer(HPM_SPI1, &mctrl, NULL, NULL, s_m_tx, n, s_m_rx, n);
        bool ok = raw_dma_wait(4u) && raw_dma_wait(5u);
        printf("raw: spi_transfer=%d slaveDma=%d\r\n", (int)st, ok ? 1 : 0);
    }
    (void)spi_wait_for_idle_status(HPM_SPI3);
    for (i = 0; i < n; i++) {
        if (s_m_rx[i] != s_s_tx[i]) { err++; }
        if (s_s_rx[i] != s_m_tx[i]) { err++; }
    }
    printf("raw: n=%lu err=%lu => %s\r\n", (unsigned long)n, (unsigned long)err,
           (err == 0u) ? "PASS" : "FAIL");
    printf("raw dump: SPI1 CTRL=0x%08lx TRANSFMT=0x%08lx TIMING=0x%08lx STATUS=0x%08lx / SPI3 CTRL=0x%08lx TRANSFMT=0x%08lx\r\n",
           (unsigned long)HPM_SPI1->CTRL, (unsigned long)HPM_SPI1->TRANSFMT, (unsigned long)HPM_SPI1->TIMING,
           (unsigned long)HPM_SPI1->STATUS, (unsigned long)HPM_SPI3->CTRL, (unsigned long)HPM_SPI3->TRANSFMT);
    /* 还原 MCAL 配置，避免后续命令状态错乱 */
    if (Spi_GetStatus() != SPI_UNINIT) { (void)Spi_DeInit(); }
    Spi_Init(&Spi_Config);
}

static void cmd_spi(int argc, char **argv)
{
    if (argc < 2) { cmd_info(); return; }
    if (strcmp(argv[1], "info") == 0) {
        cmd_info();
    } else if (strcmp(argv[1], "init") == 0) {
        cmd_init(argc - 2, &argv[2]);
    } else if (strcmp(argv[1], "xfer") == 0) {
        cmd_xfer(argc - 2, &argv[2]);
    } else if (strcmp(argv[1], "width") == 0) {
        cmd_width(argc - 2, &argv[2]);
    } else if (strcmp(argv[1], "bench") == 0) {
        cmd_bench();
    } else if (strcmp(argv[1], "ber") == 0) {
        cmd_ber(argc - 2, &argv[2]);
    } else if (strcmp(argv[1], "ib") == 0) {
        cmd_ib();
    } else if (strcmp(argv[1], "det") == 0) {
        cmd_det();
    } else if (strcmp(argv[1], "raw") == 0) {
        cmd_raw();
    } else if (strcmp(argv[1], "dbg") == 0) {
        printf("SPI1 CTRL=0x%08lx TRANSFMT=0x%08lx TIMING=0x%08lx TRANSCTRL=0x%08lx STATUS=0x%08lx INTREN=0x%08lx\r\n",
               (unsigned long)HPM_SPI1->CTRL, (unsigned long)HPM_SPI1->TRANSFMT,
               (unsigned long)HPM_SPI1->TIMING, (unsigned long)HPM_SPI1->TRANSCTRL,
               (unsigned long)HPM_SPI1->STATUS, (unsigned long)HPM_SPI1->INTREN);
        printf("SPI3 CTRL=0x%08lx TRANSFMT=0x%08lx TIMING=0x%08lx TRANSCTRL=0x%08lx STATUS=0x%08lx INTREN=0x%08lx\r\n",
               (unsigned long)HPM_SPI3->CTRL, (unsigned long)HPM_SPI3->TRANSFMT,
               (unsigned long)HPM_SPI3->TIMING, (unsigned long)HPM_SPI3->TRANSCTRL,
               (unsigned long)HPM_SPI3->STATUS, (unsigned long)HPM_SPI3->INTREN);
    } else {
        printf("unknown subcommand '%s'\r\n", argv[1]);
        cmd_info();
    }
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 spi, cmd_spi, spi: info|init|xfer|width|bench|ber|ib|det);

int main(void)
{
    uint32 errs = 0u;
    uint64_t cy = 0u;
    int rc;

    bsp_init();
    s_cpu_hz = clock_get_frequency(clock_cpu0);

    printf("\r\n=== mcal spi_master_slave: SPI1(master) <-> SPI3(slave), AUTOSAR Spi driver ===\r\n");
    Spi_Init(&Spi_Config);
    if (Spi_GetStatus() != SPI_IDLE) {
        printf("Spi_Init FAILED\r\n");
        while (1) { bsp_shell_poll(); }
    }
    cmd_info();
    rc = spi_bidir(64u, 1u, &errs, &cy);
    report("boot", 64u, 1u, rc, errs, cy);
    printf("spi: boot self-test %s, jobEnd=%lu seqEnd=%lu\r\n",
           ((rc == 0) && (errs == 0u)) ? "PASS" : "FAIL",
           (unsigned long)s_job_end_cnt, (unsigned long)s_seq_end_cnt);
    printf("type 'spi info' / 'spi xfer 256' / 'spi bench' / 'spi ber 1 1024' / 'spi det'\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
