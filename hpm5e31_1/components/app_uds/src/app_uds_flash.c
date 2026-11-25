/*
 * app_uds_flash.c - UDS 0x34/0x36/0x37 的片内 XPI NOR 后端（hpm5e31_1）
 *
 * 安全窗口：1 MB 片内 Flash 的最后 64 KB（0x800F0000~0x80100000）。
 * 应用镜像从 0x80000000 起（约 80~100 KB），boot header 在 0x80001000，
 * 所以把下载区放最后 64 KB **绝不会擦到正在 XIP 执行的代码**（第 31 章 31-7 节）。
 *
 * 关键点：
 *   1) 擦除/写入走 ROM API（rom_xpi_nor_*），执行体在 BootROM 而非 XIP Flash，
 *      因此"边跑边刷自己所在的 Flash"是安全的。
 *   2) 但 XPI 忙时任何从 Flash 取指/取常量都会读到无效数据，所以擦写期间关全局中断。
 *   3) XIP 直读可能命中 L1 D-Cache 里的旧数据，写完必须 l1c_dc_invalidate。
 */
#include "app_uds_flash.h"

#include <string.h>

#include "board.h"
#include "hpm_csr_drv.h"
#include "hpm_csr_regs.h"
#include "hpm_l1c_drv.h"
#include "hpm_mchtmr_drv.h"
#include "hpm_romapi.h"

#define UDS_FLASH_BASE       (0x80000000UL)
#define UDS_FLASH_SIZE       (1024UL * 1024UL)
#define UDS_FLASH_WIN_START  (0x800F0000UL)
#define UDS_FLASH_WIN_END    (0x80100000UL)

#define UDS_FLASH_BLOCK_MAX  258u          /* 0x36 请求 PDU: SID + seq + 256 B 数据 */

static struct {
    xpi_nor_config_t nor_cfg;
    bool     ready;
    uint32_t sector_size;
    uint32_t page_size;
    uint32_t programmed;     /* 本次下载写入字节数 */
    uint32_t erase_us;
    uint32_t write_us;
} s_fl = { .ready = false };

typedef struct {
    uint32_t base;
    bool     mie;
} uds_flash_irq_state_t;

static uds_flash_irq_state_t irq_save(void)
{
    uds_flash_irq_state_t st;
    st.mie = (read_csr(CSR_MSTATUS) & CSR_MSTATUS_MIE_MASK) ? true : false;
    clear_csr(CSR_MSTATUS, CSR_MSTATUS_MIE_MASK);
    return st;
}

static void irq_restore(uds_flash_irq_state_t st)
{
    if (st.mie) {
        set_csr(CSR_MSTATUS, CSR_MSTATUS_MIE_MASK);
    }
}

static int nor_setup(void)
{
    xpi_nor_config_option_t opt;
    hpm_stat_t st;

    if (s_fl.ready) {
        return 0;
    }
    opt.header.U = BOARD_APP_XPI_NOR_CFG_OPT_HDR;
    opt.option0.U = BOARD_APP_XPI_NOR_CFG_OPT_OPT0;
    opt.option1.U = BOARD_APP_XPI_NOR_CFG_OPT_OPT1;
    st = rom_xpi_nor_auto_config(HPM_XPI0, &s_fl.nor_cfg, &opt);
    if (st != status_success) {
        return -1;
    }
    (void) rom_xpi_nor_get_property(HPM_XPI0, &s_fl.nor_cfg, xpi_nor_property_sector_size, &s_fl.sector_size);
    (void) rom_xpi_nor_get_property(HPM_XPI0, &s_fl.nor_cfg, xpi_nor_property_page_size, &s_fl.page_size);
    if (s_fl.sector_size == 0u) {
        s_fl.sector_size = 4096u;
    }
    s_fl.ready = true;
    return 0;
}

static int fl_begin(void *ctx, uint32_t addr, uint32_t size)
{
    hpm_stat_t st;
    uds_flash_irq_state_t irq;
    uint32_t t0;

    (void) ctx;
    if (nor_setup() != 0) {
        return -1;
    }
    if ((addr < UDS_FLASH_WIN_START) || (size == 0u) || ((addr + size) > UDS_FLASH_WIN_END) ||
        ((addr & (s_fl.sector_size - 1u)) != 0u) || ((size & (s_fl.sector_size - 1u)) != 0u)) {
        return -1;
    }
    t0 = mchtmr_get_count(HPM_MCHTMR);
    irq = irq_save();
    st = rom_xpi_nor_erase(HPM_XPI0, xpi_xfer_channel_auto, &s_fl.nor_cfg, addr, size);
    irq_restore(irq);
    if (st != status_success) {
        return -2;
    }
    s_fl.erase_us = (mchtmr_get_count(HPM_MCHTMR) - t0) / 24u;
    s_fl.programmed = 0u;
    return 0;
}

static int fl_write(void *ctx, uint32_t addr, const uint8_t *data, uint32_t len)
{
    uds_flash_irq_state_t irq;
    uint32_t t0;
    uint32_t off = 0;
    /* ROM API 要求源指针 4 字节对齐：先拷进对齐缓冲再编程 */
    static uint32_t __attribute__((aligned(4))) buf[64];   /* 256 B */

    (void) ctx;
    if (!s_fl.ready) {
        return -1;
    }
    if ((len == 0u) || (len > sizeof(buf)) || ((addr & 3u) != 0u) || ((len & 3u) != 0u) ||
        ((addr + len) > UDS_FLASH_WIN_END)) {
        return -1;
    }
    t0 = mchtmr_get_count(HPM_MCHTMR);
    irq = irq_save();
    /*
     * 关键易错点：NOR Flash 的 Page Program 命令在页内回绕 —— 一次编程跨过 256 B
     * 页边界时，超出部分会写回本页开头，数据整体错位。本示例的下载起点是
     * 0x800F0010（16 字节镜像头），第一个 256 B 块就跨页：不切分时
     * 0x800F0100 之后的 240 B 会回绕到 0x800F0000，把魔数写坏。
     * 因为 addr/len/page 都是 4 的倍数，切出来的每一段也都 4 字节对齐。
     */
    while (off < len) {
        uint32_t a = addr + off;
        uint32_t page_left = s_fl.page_size - (a % s_fl.page_size);
        uint32_t chunk = len - off;
        hpm_stat_t st;

        if (chunk > page_left) {
            chunk = page_left;
        }
        memcpy(buf, &data[off], chunk);
        st = rom_xpi_nor_program(HPM_XPI0, xpi_xfer_channel_auto, &s_fl.nor_cfg, buf, a, chunk);
        if (st != status_success) {
            irq_restore(irq);
            return -2;
        }
        off += chunk;
    }
    irq_restore(irq);
    s_fl.write_us += (mchtmr_get_count(HPM_MCHTMR) - t0) / 24u;
    s_fl.programmed += len;
    /* XIP 直读前必须无效化 D-Cache，否则读到旧数据 */
    if (l1c_dc_is_enabled()) {
        uint32_t a0 = HPM_L1C_CACHELINE_ALIGN_DOWN(addr);
        uint32_t a1 = HPM_L1C_CACHELINE_ALIGN_UP(addr + len);
        l1c_dc_invalidate(a0, a1 - a0);
    }
    return 0;
}

/* 读回 CRC32：64 KB 的位运算约 1~2 ms（480 MHz，XIP 直读） */
static int fl_crc32(void *ctx, uint32_t addr, uint32_t size, uint32_t *out)
{
    uint32_t c = 0xFFFFFFFFUL;

    (void) ctx;
    if ((out == NULL) || (size == 0u) || ((addr + size) > UDS_FLASH_WIN_END)) {
        return -1;
    }
    {
        /* 先无效化 D-Cache，保证读的是 Flash 里的新数据 */
        if (l1c_dc_is_enabled()) {
            uint32_t a0 = HPM_L1C_CACHELINE_ALIGN_DOWN(addr);
            uint32_t a1 = HPM_L1C_CACHELINE_ALIGN_UP(addr + size);
            l1c_dc_invalidate(a0, a1 - a0);
        }
    }
    for (uint32_t i = 0; i < size; i++) {
        c ^= *(const uint8_t *) (uintptr_t) (addr + i);
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320UL & (uint32_t) (-(int32_t) (c & 1u)));
        }
    }
    *out = ~c;
    return 0;
}

static int fl_finish(void *ctx)
{
    (void) ctx;
    return 0;
}

static uint32_t fl_max_block(void *ctx)
{
    (void) ctx;
    return UDS_FLASH_BLOCK_MAX;
}

static const app_uds_flash_driver_t s_drv = {
    .ctx = NULL,
    .begin = fl_begin,
    .write = fl_write,
    .finish = fl_finish,
    .crc32 = fl_crc32,
    .max_block = fl_max_block,
};

const app_uds_flash_driver_t *app_uds_flash_default(void)
{
    return &s_drv;
}

void app_uds_flash_stats(uint32_t *erase_us, uint32_t *write_us, uint32_t *programmed)
{
    if (erase_us != NULL) {
        *erase_us = s_fl.erase_us;
    }
    if (write_us != NULL) {
        *write_us = s_fl.write_us;
    }
    if (programmed != NULL) {
        *programmed = s_fl.programmed;
    }
}

void app_uds_flash_window(uint32_t *start, uint32_t *end)
{
    if (start != NULL) {
        *start = UDS_FLASH_WIN_START;
    }
    if (end != NULL) {
        *end = UDS_FLASH_WIN_END;
    }
}
