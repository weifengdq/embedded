/*
 * flash_rw_erase - 片内 1MB Flash(经 XPI0 映射到 0x80000000) 的读/写/擦与实测速度
 *
 * 硬件: hpm5e31_1 (HPM5E31IPB1), 片内 Flash 1MB, 无外接 NOR
 * 安全区: 0x800F0000 ~ 0x800FFFFF (最后 64KB), 程序镜像从 0x80000000 起, 约 80KB
 *         -> 本示例只允许操作 [0x800F0000, 0x80100000), 越界直接拒绝
 *
 * 现象: 见 README.md
 */
#include <string.h>
#include "bsp.h"
#include "hpm_romapi.h"
#include "hpm_mchtmr_drv.h"
#include "hpm_l1c_drv.h"
#include "hpm_bootheader.h"
#include "hpm_gptmr_drv.h"

#define APP_FLASH_BASE        (0x80000000UL)
#define APP_FLASH_SIZE        (1024UL * 1024UL)
#define APP_FLASH_SAFE_ADDR   (0x800F0000UL)
#define APP_FLASH_SAFE_END    (0x80100000UL)
#define APP_XPI_NOR_BASE      HPM_XPI0
#define APP_TEST_SIZE         (4096UL)

static xpi_nor_config_t s_nor_cfg;
static bool s_nor_ready;
static uint32_t s_tmr_hz;
static uint32_t s_flash_size, s_sector_size, s_page_size;
static uint8_t s_wbuf[APP_TEST_SIZE];
static uint8_t s_rbuf[APP_TEST_SIZE];

/* 程序镜像实际占用: etext 由链接脚本提供 */
extern char etext[];

static hpm_stat_t nor_setup(void)
{
    xpi_nor_config_option_t opt;
    opt.header.U  = BOARD_APP_XPI_NOR_CFG_OPT_HDR;
    opt.option0.U = BOARD_APP_XPI_NOR_CFG_OPT_OPT0;
    opt.option1.U = BOARD_APP_XPI_NOR_CFG_OPT_OPT1;
    hpm_stat_t st = rom_xpi_nor_auto_config(APP_XPI_NOR_BASE, &s_nor_cfg, &opt);
    if (st != status_success) {
        return st;
    }
    rom_xpi_nor_get_property(APP_XPI_NOR_BASE, &s_nor_cfg, xpi_nor_property_total_size, &s_flash_size);
    rom_xpi_nor_get_property(APP_XPI_NOR_BASE, &s_nor_cfg, xpi_nor_property_sector_size, &s_sector_size);
    rom_xpi_nor_get_property(APP_XPI_NOR_BASE, &s_nor_cfg, xpi_nor_property_page_size, &s_page_size);
    s_nor_ready = true;
    return status_success;
}

static uint32_t crc32_sw(const uint8_t *p, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t) (-(int32_t) (crc & 1U)));
        }
    }
    return ~crc;
}

static void print_flash_info(void)
{
    uint32_t img_end = (uint32_t) (uintptr_t) etext;
    const boot_header_t *bh = (const boot_header_t *) (uintptr_t) (APP_FLASH_BASE + 0x1000U);
    const fw_info_table_t *fw = (const fw_info_table_t *) (uintptr_t) (APP_FLASH_BASE + 0x1010U);

    printf("Flash @0x%08lx size=%lu KB (internal SIP NOR behind XPI0)\r\n",
           (unsigned long) APP_FLASH_BASE, (unsigned long) (s_flash_size / 1024U));
    printf("  sector=%lu B  page=%lu B\r\n",
           (unsigned long) s_sector_size, (unsigned long) s_page_size);
    printf("  app image: 0x%08lx .. 0x%08lx (etext), used %lu KB of 1024 KB\r\n",
           (unsigned long) APP_FLASH_BASE, (unsigned long) img_end,
           (unsigned long) ((img_end - APP_FLASH_BASE) / 1024U));
    printf("  boot_header: tag=0x%02x ver=0x%02x len=%u fw_count=%u\r\n",
           (unsigned) bh->tag, (unsigned) bh->version, (unsigned) bh->length,
           (unsigned) bh->fw_count);
    printf("  fw[0]: offset=0x%lx size=0x%lx(=%lu B) entry=0x%08lx\r\n",
           (unsigned long) fw->offset, (unsigned long) fw->size, (unsigned long) fw->size,
           (unsigned long) fw->entry_point);
    printf("  safe test window: 0x%08lx .. 0x%08lx (last 64KB)\r\n",
           (unsigned long) APP_FLASH_SAFE_ADDR, (unsigned long) APP_FLASH_SAFE_END);
    printf("  nor_cfg_option @0x%08lx: ", (unsigned long) (APP_FLASH_BASE + 0x400U));
    bsp_hexdump((const void *) (uintptr_t) (APP_FLASH_BASE + 0x400U), 16);
    printf("  boot header  @0x%08lx: ", (unsigned long) (APP_FLASH_BASE + 0x1000U));
    bsp_hexdump((const void *) (uintptr_t) (APP_FLASH_BASE + 0x1000U), 32);
}


/* ================== JEDEC ID / 自定义 XPI 指令 ==================
 * ROM API 只提供 auto_config/read/program/erase, 没有读 ID 的封装,
 * 但 ROM 的 xpi_driver_if 提供了 update_instr_table + transfer_blocking,
 * 可以自己拼指令序列发任意 (cmd / cmd+addr / cmd+dummy+read) 命令。
 * 这里用 0x9F 读 JEDEC ID(3B), 0x05/0x35 读状态寄存器, 0x4B 读 64bit Unique ID。
 */
typedef struct {
    uint32_t phase_idx;
    xpi_instr_seq_t seq;
} app_instr_ctx_t;

static void app_seq_init(app_instr_ctx_t *c)
{
    memset(c, 0, sizeof(*c));
}

static void app_seq_cmd(app_instr_ctx_t *c, uint8_t cmd, uint8_t pads)
{
    c->seq.entry[c->phase_idx++] = SUB_INSTR(XPI_PHASE_CMD_SDR, pads, cmd);
}

static void app_seq_dummy(app_instr_ctx_t *c, uint8_t bits, uint8_t pads)
{
    c->seq.entry[c->phase_idx++] = SUB_INSTR(XPI_PHASE_DUMMY_SDR, pads, bits);
}

static void app_seq_read(app_instr_ctx_t *c, uint8_t pads)
{
    c->seq.entry[c->phase_idx++] = SUB_INSTR(XPI_PHASE_READ_SDR, pads, 4);
}

static hpm_stat_t app_xpi_xfer(XPI_Type *base, const app_instr_ctx_t *ctx, uint8_t cmd_type,
                               uint32_t addr, void *buf, uint16_t len)
{
    xpi_xfer_ctx_t x = {0};
    x.cmd_type = cmd_type;
    x.addr = addr;
    x.channel = xpi_xfer_channel_auto;
    x.seq_idx = 1;
    x.seq_num = 1;
    x.buf = (uint32_t *) buf;
    x.xfer_size = len;
    (void) ROM_API_TABLE_ROOT->xpi_driver_if->update_instr_table(base, ctx->seq.entry, 1, 1);
    return ROM_API_TABLE_ROOT->xpi_driver_if->transfer_blocking(base, &x);
}

static const char *jedec_vendor(uint8_t mf)
{
    switch (mf) {
    case 0xEF: return "Winbond";
    case 0xC8: return "GigaDevice";
    case 0xC2: return "Macronix";
    case 0x20: return "Micron/ST";
    case 0x01: return "Spansion/Cypress";
    case 0x0B: return "XTX";
    case 0x5E: return "Zbit";
    case 0x85: return "Puya";
    case 0x1C: return "EON";
    case 0x9D: return "ISSI";
    case 0xBF: return "SST/Microchip";
    case 0x68: return "Boya";
    case 0xA1: return "Fudan Micro";
    case 0x62: return "SANYO?";
    default:   return "unknown/nerp";
    }
}

static void cmd_jedec(void)
{
    if (!s_nor_ready) {
        printf("nor not ready\r\n");
        return;
    }
    uint8_t id[8] = {0};
    uint8_t st1 = 0, st2 = 0;
    app_instr_ctx_t c;

    app_seq_init(&c);
    app_seq_cmd(&c, 0x9F, XPI_1PAD);
    app_seq_read(&c, XPI_1PAD);
    hpm_stat_t st = app_xpi_xfer(APP_XPI_NOR_BASE, &c, xpi_apb_xfer_type_read, 0, id, 3);
    printf("JEDEC_ID cmd=0x9F status=%d bytes=%02x %02x %02x\r\n", (int) st, id[0], id[1], id[2]);
    if (st == status_success) {
        printf("  vendor=%02x (%s) type=%02x capacity=%02x -> %lu KB (2^%u bytes)\r\n",
               id[0], jedec_vendor(id[0]), id[1], id[2],
               (unsigned long) ((1UL << (id[2] & 0x1F)) / 1024UL), (unsigned) (id[2] & 0x1F));
    }

    app_seq_init(&c);
    app_seq_cmd(&c, 0x05, XPI_1PAD);
    app_seq_read(&c, XPI_1PAD);
    st = app_xpi_xfer(APP_XPI_NOR_BASE, &c, xpi_apb_xfer_type_read, 0, &st1, 1);
    printf("STATUS1 cmd=0x05 status=%d val=0x%02x\r\n", (int) st, st1);

    app_seq_init(&c);
    app_seq_cmd(&c, 0x35, XPI_1PAD);
    app_seq_read(&c, XPI_1PAD);
    st = app_xpi_xfer(APP_XPI_NOR_BASE, &c, xpi_apb_xfer_type_read, 0, &st2, 1);
    printf("STATUS2 cmd=0x35 status=%d val=0x%02x\r\n", (int) st, st2);

    app_seq_init(&c);
    app_seq_cmd(&c, 0x4B, XPI_1PAD);
    app_seq_dummy(&c, 32, XPI_1PAD);
    app_seq_read(&c, XPI_1PAD);
    st = app_xpi_xfer(APP_XPI_NOR_BASE, &c, xpi_apb_xfer_type_read, 0, id, 8);
    if (st == status_success) {
        printf("UNIQUE_ID cmd=0x4B status=0 %02x%02x%02x%02x%02x%02x%02x%02x\r\n",
               id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7]);
    } else {
        printf("UNIQUE_ID cmd=0x4B status=%d (器件可能不支持)\r\n", (int) st);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 jedec, cmd_jedec, read JEDEC ID/status/unique ID through XPI custom commands);

static void cmd_flashinfo(void)
{
    if (!s_nor_ready) {
        printf("nor not ready\r\n");
        return;
    }
    print_flash_info();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 flashinfo, cmd_flashinfo, flash size/properties/layout info);

static void cmd_flashread(int argc, char *argv[])
{
    uint32_t addr = APP_FLASH_BASE, len = 64;
    if (argc >= 2 && bsp_parse_u32(argv[1], &addr)) {
        printf("usage: flashread [addr] [len]\r\n");
        return;
    }
    if (argc >= 3) {
        bsp_parse_u32(argv[2], &len);
    }
    if (len > 4096U) {
        len = 4096U;
    }
    if (addr >= APP_FLASH_BASE && addr + len <= APP_FLASH_BASE + APP_FLASH_SIZE) {
        /* XIP 直读: 0x80000000 起可直接按地址访问 */
        bsp_hexdump((const void *) (uintptr_t) addr, len);
    } else {
        printf("addr out of flash\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 flashread, cmd_flashread, XIP hexdump: flashread [addr] [len]);

static void cmd_flasherase(int argc, char *argv[])
{
    uint32_t addr = APP_FLASH_SAFE_ADDR;
    if (argc >= 2 && bsp_parse_u32(argv[1], &addr)) {
        printf("usage: flasherase [addr] (sector aligned)\r\n");
        return;
    }
    if ((addr < APP_FLASH_SAFE_ADDR) || (addr + s_sector_size > APP_FLASH_SAFE_END) ||
        (addr & (s_sector_size - 1U))) {
        printf("REFUSED: only sector-aligned erase inside 0x%08lx..0x%08lx allowed\r\n",
               (unsigned long) APP_FLASH_SAFE_ADDR, (unsigned long) APP_FLASH_SAFE_END);
        return;
    }
    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    hpm_stat_t st = rom_xpi_nor_erase(APP_XPI_NOR_BASE, xpi_xfer_channel_auto, &s_nor_cfg,
                                      addr, s_sector_size);
    uint32_t ticks = mchtmr_get_count(HPM_MCHTMR) - t0;
    if (st != status_success) {
        printf("erase failed: %d\r\n", (int) st);
        return;
    }
    printf("erase 0x%08lx (%lu B) OK in %.3f ms\r\n", (unsigned long) addr,
           (unsigned long) s_sector_size, (double) ticks * 1000.0 / (double) s_tmr_hz);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 flasherase, cmd_flasherase, erase one sector in safe window: flasherase [addr]);

static void cmd_flashrw(int argc, char *argv[])
{
    uint32_t addr = APP_FLASH_SAFE_ADDR;
    uint32_t size = s_sector_size ? s_sector_size : APP_TEST_SIZE;
    if (!s_nor_ready) {
        printf("nor not ready\r\n");
        return;
    }
    if (argc >= 2 && bsp_parse_u32(argv[1], &addr)) {
        printf("usage: flashrw [addr] [size]\r\n");
        return;
    }
    if (argc >= 3) {
        bsp_parse_u32(argv[2], &size);
    }
    if ((addr < APP_FLASH_SAFE_ADDR) || (size == 0) || (size > 65536U) ||
        (addr + size > APP_FLASH_SAFE_END) || (addr & 3U)) {
        printf("REFUSED: need 4-byte aligned addr in 0x%08lx..0x%08lx, size<=64KB\r\n",
               (unsigned long) APP_FLASH_SAFE_ADDR, (unsigned long) APP_FLASH_SAFE_END);
        return;
    }
    printf("rw test @0x%08lx size=%lu B (safe window, app image untouched)\r\n",
           (unsigned long) addr, (unsigned long) size);

    /* 擦除 */
    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    hpm_stat_t st = rom_xpi_nor_erase(APP_XPI_NOR_BASE, xpi_xfer_channel_auto, &s_nor_cfg,
                                      addr, size);
    double erase_ms = (double) (mchtmr_get_count(HPM_MCHTMR) - t0) * 1000.0 / (double) s_tmr_hz;
    if (st != status_success) {
        printf("erase failed: %d\r\n", (int) st);
        return;
    }
    bool blank = true;
    for (uint32_t i = 0; i < size; i++) {
        if (*(volatile uint8_t *) (uintptr_t) (addr + i) != 0xFFU) {
            blank = false;
            break;
        }
    }
    printf("  erase  %.3f ms -> %.1f KB/s, blank check: %s\r\n",
           erase_ms, (double) size / 1024.0 / (erase_ms / 1000.0), blank ? "0xFF ok" : "FAIL");

    /* 写入: 每页 256B 递增, 与地址异或, 便于定位错位 */
    for (uint32_t i = 0; i < size; i++) {
        s_wbuf[i] = (uint8_t) ((i * 31U + (addr >> 4)) & 0xFFU);
    }
    t0 = mchtmr_get_count(HPM_MCHTMR);
    st = rom_xpi_nor_program(APP_XPI_NOR_BASE, xpi_xfer_channel_auto, &s_nor_cfg,
                             (uint32_t *) s_wbuf, addr, size);
    double prog_ms = (double) (mchtmr_get_count(HPM_MCHTMR) - t0) * 1000.0 / (double) s_tmr_hz;
    if (st != status_success) {
        printf("program failed: %d\r\n", (int) st);
        return;
    }
    printf("  write  %.3f ms -> %.1f KB/s\r\n", prog_ms,
           (double) size / 1024.0 / (prog_ms / 1000.0));

    /* 读回 + CRC 校验 (XIP 直读与 ROM API 读各一次) */
    memset(s_rbuf, 0, size);
    t0 = mchtmr_get_count(HPM_MCHTMR);
    st = rom_xpi_nor_read(APP_XPI_NOR_BASE, xpi_xfer_channel_auto, &s_nor_cfg,
                          (uint32_t *) s_rbuf, addr, size);
    double read_ms = (double) (mchtmr_get_count(HPM_MCHTMR) - t0) * 1000.0 / (double) s_tmr_hz;
    if (st != status_success) {
        printf("read failed: %d\r\n", (int) st);
        return;
    }
    uint32_t crc_w = crc32_sw(s_wbuf, size);
    uint32_t crc_r = crc32_sw(s_rbuf, size);
    printf("  read   %.3f ms -> %.1f KB/s\r\n", read_ms,
           (double) size / 1024.0 / (read_ms / 1000.0));
    printf("  verify crc32 write=0x%08lx read=0x%08lx -> %s\r\n",
           (unsigned long) crc_w, (unsigned long) crc_r,
           (crc_w == crc_r) ? "MATCH" : "MISMATCH");
    /* 易错点: ROM API 写完 Flash 后, XIP 直读可能命中 L1 D-Cache 里的旧数据, 必须先 invalidate */
    if (l1c_dc_is_enabled()) {
        uint32_t a0 = HPM_L1C_CACHELINE_ALIGN_DOWN(addr);
        uint32_t a1 = HPM_L1C_CACHELINE_ALIGN_UP(addr + size);
        l1c_dc_invalidate(a0, a1 - a0);
    }
    printf("  XIP direct read 0x%08lx (after dcache invalidate): ", (unsigned long) addr);
    bsp_hexdump((const void *) (uintptr_t) addr, 16);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 flashrw, cmd_flashrw, erase+program+verify in safe window: flashrw [addr] [size]);


/* XIP 直读带宽: memcpy(flash -> AXI SRAM), cold(dcache invalidate) 与 warm(命中 cache) 对比 */
static uint8_t s_xip_dst[32768];

static uint32_t xip_copy_once(uint32_t src, uint32_t size, bool cold)
{
    if (cold && l1c_dc_is_enabled()) {
        l1c_dc_invalidate(src, size);
    }
    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    memcpy(s_xip_dst, (const void *) (uintptr_t) src, size);
    return mchtmr_get_count(HPM_MCHTMR) - t0;
}

static void cmd_xipbw(int argc, char *argv[])
{
    uint32_t size = 16384;
    if (argc >= 2 && (bsp_parse_u32(argv[1], &size) || size < 1024U || size > sizeof(s_xip_dst))) {
        printf("usage: xipbw [1024..%lu]\r\n", (unsigned long) sizeof(s_xip_dst));
        return;
    }
    uint32_t src = APP_FLASH_BASE + 0x20000U;
    printf("XIP read: memcpy(0x%08lx -> RAM 0x%08lx), %lu B, dcache %s\r\n",
           (unsigned long) src, (unsigned long) (uintptr_t) s_xip_dst, (unsigned long) size,
           l1c_dc_is_enabled() ? "on" : "off");
    for (int pass = 0; pass < 2; pass++) {
        bool cold = (pass == 0);
        uint32_t best = 0xFFFFFFFFUL;
        for (int i = 0; i < 3; i++) {
            uint32_t t = xip_copy_once(src, size, cold);
            if (t < best) {
                best = t;
            }
        }
        double us = (double) best * 1e6 / (double) s_tmr_hz;
        printf("  %s: %6.2f us -> %7.1f MB/s\r\n", cold ? "cold" : "warm", us,
               ((double) size / (1024.0 * 1024.0)) / (us / 1e6));
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 xipbw, cmd_xipbw, XIP direct-read bandwidth (cold/warm dcache): xipbw [bytes]);


/* ================== 擦写期间高频中断 ==================
 * 目的: Flash 擦/写时 XPI 控制器会阻塞取指, 若 ISR 代码也在 flash 里,
 *       中断是否丢失/迟到? 用一个 10 kHz 的 GPTMR 比较中断做探针:
 *       1) 空闲 200ms 数中断次数(基准频率)
 *       2) 擦+写 4KB 期间再数一次, 与"实际耗时 x 基准频率"对比
 */
#define APP_INT_TIMEOUT_MS  (200U)
static volatile uint32_t s_flash_isr_cnt;

SDK_DECLARE_EXT_ISR_M(IRQn_GPTMR0, isr_flash_probe)
void isr_flash_probe(void)
{
    s_flash_isr_cnt++;
    gptmr_clear_status(HPM_GPTMR0, GPTMR_CH_CMP_STAT_MASK(0, 0));
}

static void flash_int_probe_setup(void)
{
    gptmr_channel_config_t cfg;
    board_init_gptmr_clock(HPM_GPTMR0);
    clock_set_source_divider(clock_gptmr0, clk_src_pll0_clk1, 4);  /* 400MHz/4000 = 100kHz */
    gptmr_channel_get_default_config(HPM_GPTMR0, &cfg);
    cfg.reload = 0xFFFFFFFFUL;
    cfg.cmp[0] = 2000;      /* 100kHz / 10 = 10kHz 中断 */
    (void) gptmr_channel_config(HPM_GPTMR0, 0, &cfg, false);
    gptmr_channel_reset_count(HPM_GPTMR0, 0);
    gptmr_enable_irq(HPM_GPTMR0, GPTMR_CH_CMP_IRQ_MASK(0, 0));
    gptmr_start_counter(HPM_GPTMR0, 0);
    intc_m_enable_irq_with_priority(IRQn_GPTMR0, 1);
}

static void cmd_flashint(int argc, char *argv[])
{
    uint32_t addr = APP_FLASH_SAFE_ADDR;
    if (argc >= 2 && (bsp_parse_u32(argv[1], &addr) || addr < APP_FLASH_SAFE_ADDR ||
                      addr + APP_TEST_SIZE > APP_FLASH_SAFE_END)) {
        printf("usage: flashint [addr in 0x%08lx..0x%08lx]\r\n",
               (unsigned long) APP_FLASH_SAFE_ADDR, (unsigned long) (APP_FLASH_SAFE_END - APP_TEST_SIZE));
        return;
    }
    addr &= ~(s_sector_size - 1U);
    if (!s_nor_ready) {
        printf("nor not ready\r\n");
        return;
    }
    for (uint32_t i = 0; i < APP_TEST_SIZE; i++) {
        s_wbuf[i] = (uint8_t) (i * 31U + 7U);
    }
    flash_int_probe_setup();

    /* 1) 空闲基准 */
    uint32_t t0 = mchtmr_get_count(HPM_MCHTMR);
    uint32_t c0 = s_flash_isr_cnt;
    board_delay_ms(APP_INT_TIMEOUT_MS);
    uint32_t t1 = mchtmr_get_count(HPM_MCHTMR);
    uint32_t c1 = s_flash_isr_cnt;
    double idle_ms = (double) (t1 - t0) * 1e3 / (double) s_tmr_hz;
    double idle_khz = (double) (c1 - c0) / idle_ms;

    /* 2) 擦+写期间 */
    t0 = mchtmr_get_count(HPM_MCHTMR);
    c0 = s_flash_isr_cnt;
    hpm_stat_t st = rom_xpi_nor_erase(APP_XPI_NOR_BASE, xpi_xfer_channel_auto, &s_nor_cfg, addr, s_sector_size);
    if (st == status_success) {
        st = rom_xpi_nor_program(APP_XPI_NOR_BASE, xpi_xfer_channel_auto, &s_nor_cfg, s_wbuf, addr, APP_TEST_SIZE);
    }
    t1 = mchtmr_get_count(HPM_MCHTMR);
    c1 = s_flash_isr_cnt;
    double busy_ms = (double) (t1 - t0) * 1e3 / (double) s_tmr_hz;

    if (st != status_success) {
        printf("flashint: erase/program failed st=%d\r\n", (int) st);
        return;
    }
    double expect = idle_khz * busy_ms;
    double got = (double) (c1 - c0);
    printf("FLASH_INT idle_khz=%.2f busy_ms=%.2f isr_got=%.0f isr_expect=%.0f loss=%.1f%% result=%s\r\n",
           idle_khz, busy_ms, got, expect,
           (expect > 0.0) ? (100.0 * (expect - got) / expect) : 0.0,
           (expect > 0.0 && ((expect - got) / expect) < 0.02) ? "PASS" : "CHECK");

    /* 擦写后数据校验 */
    st = rom_xpi_nor_read(APP_XPI_NOR_BASE, xpi_xfer_channel_auto, &s_nor_cfg, s_rbuf, addr, APP_TEST_SIZE);
    printf("FLASH_INT verify=%s\r\n",
           (st == status_success && memcmp(s_wbuf, s_rbuf, APP_TEST_SIZE) == 0) ? "match" : "MISMATCH");
    gptmr_disable_irq(HPM_GPTMR0, GPTMR_CH_CMP_IRQ_MASK(0, 0));
    gptmr_stop_counter(HPM_GPTMR0, 0);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 flashint, cmd_flashint, interrupt latency/count during flash erase+program);

int main(void)
{
    bsp_init();
    s_tmr_hz = clock_get_frequency(clock_mchtmr0);

    printf("\r\n=== flash_rw_erase: internal 1MB flash via XPI0 ===\r\n");

    hpm_stat_t st = nor_setup();
    if (st != status_success) {
        printf("rom_xpi_nor_auto_config failed: %d\r\n", (int) st);
    } else {
        print_flash_info();
    }
    printf("shell: flashinfo | jedec | flashread | flasherase | flashrw | xipbw | flashint\r\n");

    /* R5 自动化: 单次上板采集 JEDEC ID 与擦写期间中断计数 */
    board_delay_ms(3000);
    cmd_jedec();
    cmd_flashint(0, NULL);


    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
