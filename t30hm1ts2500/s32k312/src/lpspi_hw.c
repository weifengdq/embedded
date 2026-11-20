/**
 * @file  lpspi_hw.c
 * @brief S32K312 LPSPI0 硬件 SPI 驱动 —— 驱动 T30HM1TS2500
 *
 * ================== 2026-11-20 根因与修复 ==================
 * 上一轮"每 4 字节重复首字 (40 00 00 00 x3)"的真正原因不是信号完整性, 也不是
 * 帧长/RDF 判据/上下拉/CONT, 而是 **FIFO 节拍**:
 *
 *   S32K312 LPSPI0 的 FIFO 只有 2 个字深 (PARAM=0x00080202 -> TXFIFO=2 RXFIFO=2),
 *   旧代码却把 FCR.TXWATER 设成 3。RM 70.6.1.x 定义 TDF = (TXCOUNT <= TXWATER),
 *   所以 TXWATER=3 时 TDF 恒为 1, 写循环从不停下来等空位, 第 3 个字以后的所有
 *   TDR 写被硬件静默丢弃 => S2500 只收到半个包头, 回显字里 HDRB=1 变成
 *   0x40000000, 于是每个字都读回 0x40000000 (即"重复首字")。
 *
 * 修复:
 *   - FIFO 深度从 PARAM 现读, FCR 水印设 depth-1;
 *   - TX 一律用 FSR.TXCOUNT < depth 判空位 (不靠水印);
 *   - 写 TCR 后必须等 TXCOUNT==0 (TCR 与数据共用同一条 FIFO);
 *   - 8 位帧改 32 位帧 (S2500 本来就是 32 位字协议), FIFO 操作少 4 倍;
 *   - CONT=1 让整笔事务连续, 结尾用 CONT=0 的命令字终止连续传输再抬 CS。
 *
 * 实测 (2026-11-20): 同一笔 IDVER 读, 硬件路径与软件 SPI 逐字节一致:
 *   rx = 00 00 3c 00 | 10 00 00 00 | 00 00 00 11
 * ===========================================================
 */
#include "lpspi_hw.h"
#include "S32K312_SIUL2.h"
#include "S32K312_LPSPI.h"
#include "S32K312_DMAMUX.h"
#include "systick.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "s2500_app.h"
#include "s2500_port_board.h"
#include "spi_soft.h"

#define LPSPI0          IP_LPSPI_0

#define SIUL2_MSCR(n)   (*(volatile uint32_t *)((uintptr_t)IP_SIUL2 + 0x240u + 4u * (uint32_t)(n)))
#define SIUL2_IMCR(n)   (*(volatile uint32_t *)((uintptr_t)IP_SIUL2 + 0xA40u + 4u * (uint32_t)(n)))

#define PIN_MOSI        36u
#define PIN_CS          37u
#define PIN_SCK         111u
#define PIN_MISO        112u
#define IMCR_MISO       230u

#define MSCR_SSS(v)     ((uint32_t)((v) & 0x7u))
#define MSCR_SRC        (0x00004000u)
#define MSCR_IBE        (0x00080000u)
#define MSCR_OBE        (0x00200000u)

#define SR_TDF          LPSPI_SR_TDF_MASK
#define SR_TCF          LPSPI_SR_TCF_MASK
#define SR_W1C          (LPSPI_SR_WCF_MASK | LPSPI_SR_FCF_MASK | LPSPI_SR_TCF_MASK | \
                         LPSPI_SR_TEF_MASK | LPSPI_SR_REF_MASK | LPSPI_SR_DMF_MASK)

#define FSR_TXCNT()     ((uint32_t)(LPSPI0->FSR & 0x7u))
#define FSR_RXCNT()     ((uint32_t)((LPSPI0->FSR >> 16) & 0x7u))

#define GUARD_SHORT     20000u
#define GUARD_LONG      400000u

static uint32_t s_func_hz;
static uint32_t s_sck_hz;
static uint32_t s_half = 1u;
static uint32_t s_tx_depth = 2u;
static uint32_t s_rx_depth = 2u;
static int      s_inited;
static lpspi_hw_info_t s_info;

/* ---------------- 引脚 ---------------- */
static void pins_hw(void)
{
    SIUL2_MSCR(PIN_MOSI)  = MSCR_SSS(3u) | MSCR_OBE | MSCR_SRC;   /* LPSPI0_SOUT */
    SIUL2_MSCR(PIN_CS)    = MSCR_OBE;                             /* GPIO 输出 */
    SIUL2_MSCR(PIN_SCK)   = MSCR_SSS(4u) | MSCR_OBE | MSCR_SRC;   /* LPSPI0_SCK */
    SIUL2_MSCR(PIN_MISO)  = MSCR_IBE;                             /* SSS=0 + IBE */
    SIUL2_IMCR(IMCR_MISO) = 3u;                                   /* SIN <- PTD16 */
    IP_SIUL2->GPDO37 = 1u;                                        /* CS_N 释放 */
}

static inline void cs_low(void)  { IP_SIUL2->GPDO37 = 0u; }
static inline void cs_high(void) { IP_SIUL2->GPDO37 = 1u; }

/* ---------------- 时钟/寄存器 ---------------- */
static void apply_half(uint32_t half)
{
    LPSPI0->CR &= ~LPSPI_CR_MEN_MASK;
    /* 注意: 写 CCR 会覆盖 CCR1 的 SCKSET/SCKHLD, 必须先 CCR 后 CCR1 */
    LPSPI0->CCR  = (4u << 24) | (2u << 16) | (2u << 8);
    LPSPI0->CCR1 = (2u << 24) | (2u << 16) |
                   (((half & 0xFFu) << 8) | (half & 0xFFu));
    s_half = half & 0xFFu;
    LPSPI0->CR |= LPSPI_CR_MEN_MASK;
}

static void module_setup(void)
{
    uint32_t par;

    LPSPI0->CR = LPSPI_CR_RST_MASK;
    LPSPI0->CR = 0u;
    LPSPI0->CR = LPSPI_CR_MEN_MASK;

    LPSPI0->CFGR0 = 0u;
    LPSPI0->CFGR1 = LPSPI_CFGR1_MASTER_MASK;      /* PINCFG=00, SAMPLE=0 */
    LPSPI0->IER   = 0u;
    LPSPI0->DER   = 0u;

    par = LPSPI0->PARAM;
    s_tx_depth = par & 0xFFu;
    s_rx_depth = (par >> 8) & 0xFFu;
    if (s_tx_depth == 0u) { s_tx_depth = 1u; }
    if (s_rx_depth == 0u) { s_rx_depth = 1u; }

    /* 水印: TDF = (TXCOUNT <= TXWATER) -> 有空位才算请求; RDF = (RXCOUNT > RXWATER) */
    LPSPI0->FCR = ((s_tx_depth - 1u) & 0x3u) | (((0u) & 0x3u) << 16);
}

static void fifo_flush(void)
{
    uint32_t guard = GUARD_SHORT;

    LPSPI0->CR |= LPSPI_CR_RTF_MASK | LPSPI_CR_RRF_MASK;
    while ((LPSPI0->CR & (LPSPI_CR_RTF_MASK | LPSPI_CR_RRF_MASK)) != 0u) {
        if (--guard == 0u) { break; }
    }
    LPSPI0->SR = SR_W1C;
}

static int wait_tx_empty(void)
{
    uint32_t guard = GUARD_LONG;
    while (FSR_TXCNT() != 0u) {
        if (--guard == 0u) { s_info.timeout++; return -1; }
    }
    return 0;
}

/* ---------------- 传输: 32 位帧 ---------------- */
static int xfer32(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    uint32_t nw = len / 4u;
    uint32_t sent = 0u, got = 0u, guard = 0u;
    int rc = 0;

    if (nw == 0u) { return -1; }

    fifo_flush();
    cs_low();

    /* 命令字: FRAMESZ=31 (32bit), CONT=1; 写 TCR 后要等它被 FIFO 吃掉 */
    if (wait_tx_empty() != 0) { cs_high(); return -1; }
    LPSPI0->TCR = LPSPI_TCR_FRAMESZ(31u) | LPSPI_TCR_CONT_MASK;
    if (wait_tx_empty() != 0) { cs_high(); return -1; }

    while ((sent < nw) || (got < nw)) {
        if ((sent < nw) && (FSR_TXCNT() < s_tx_depth)) {
            uint32_t w;
            if (tx != NULL) {
                const uint8_t *p = &tx[sent * 4u];
                w = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                    ((uint32_t)p[2] << 8) | (uint32_t)p[3];
            } else {
                w = 0xFFFFFFFFu;
            }
            LPSPI0->TDR = w;
            sent++;
            guard = 0u;
        }
        if ((got < nw) && (FSR_RXCNT() != 0u)) {
            uint32_t v = LPSPI0->RDR;
            if (rx != NULL) {
                uint8_t *p = &rx[got * 4u];
                p[0] = (uint8_t)(v >> 24);
                p[1] = (uint8_t)(v >> 16);
                p[2] = (uint8_t)(v >> 8);
                p[3] = (uint8_t)v;
            }
            got++;
            guard = 0u;
        }
        if (++guard > GUARD_LONG) { s_info.timeout++; rc = -2; break; }
    }

    /* 结束连续传输: 命令字 CONT=0 -> 当前字结束后撤回内部 PCS */
    if (wait_tx_empty() == 0) {
        LPSPI0->TCR = LPSPI_TCR_FRAMESZ(31u);
        guard = GUARD_LONG;
        while ((LPSPI0->SR & LPSPI_SR_MBF_MASK) != 0u) {
            if (--guard == 0u) { break; }
        }
    }
    LPSPI0->SR = SR_W1C;
    cs_high();
    return rc;
}

/* ---------------- 传输: 8 位帧兜底 (len 非 4 对齐时) ---------------- */
static int xfer8(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    uint32_t i, guard = 0u;
    int rc = 0;

    fifo_flush();
    cs_low();
    if (wait_tx_empty() != 0) { cs_high(); return -1; }
    LPSPI0->TCR = LPSPI_TCR_FRAMESZ(7u) | LPSPI_TCR_CONT_MASK;
    if (wait_tx_empty() != 0) { cs_high(); return -1; }

    for (i = 0u; i < len; i++) {
        while (FSR_TXCNT() >= s_tx_depth) {
            if (FSR_RXCNT() != 0u) { break; }
            if (--guard == 0u) { s_info.timeout++; rc = -2; cs_high(); return rc; }
        }
        LPSPI0->TDR = (tx != NULL) ? (uint32_t)tx[i] : 0xFFu;
        guard = GUARD_LONG;
        while (FSR_RXCNT() == 0u) {
            if (--guard == 0u) { s_info.timeout++; rc = -2; cs_high(); return rc; }
        }
        if (rx != NULL) { rx[i] = (uint8_t)(LPSPI0->RDR & 0xFFu); }
        else { (void)LPSPI0->RDR; }
    }

    if (wait_tx_empty() == 0) {
        LPSPI0->TCR = LPSPI_TCR_FRAMESZ(7u);
        guard = GUARD_LONG;
        while ((LPSPI0->SR & LPSPI_SR_MBF_MASK) != 0u) {
            if (--guard == 0u) { break; }
        }
    }
    LPSPI0->SR = SR_W1C;
    cs_high();
    return rc;
}

/* ============================ eDMA ============================
 * 2026-11-20: 旧版 DMA 从来没能工作的原因是 eDMA 基地址写错 (0x40210000,
 * 实际 IP_EDMA_BASE=0x4020C000), 而且 DMAMUX 通道映射也理解错了:
 * S32K312 只有 DMAMUX_0 的 channel 0..5 可用, 分别映射 TCD 0..5
 * (DMAMUX_1 ch0..5 -> TCD 6..11)。这里 TX 用 channel 0, RX 用 channel 1。
 *
 * TCD 元素宽度 32 位 (ATTR: DSIZE=2 | SSIZE=2<<8), minor loop = 1 个字 (NBYTES=4),
 * major loop = 字数; 一次 DMA 请求搬一个字。
 *
 * D-Cache 是打开的 (system.c 里 SCB->CCR |= DC), 所以用一个 32 字节对齐的
 * staging buffer + dc cvac / dc ivac 做一致性维护, 不动调用者的缓冲。
 * ============================================================== */
#define EDMA_MGMT_BASE   0x4020C000u   /* eDMA 管理页 (CSR/ES/INT/HRS/GRPRI) */
#define EDMA_BASE        0x40210000u   /* RM 36692: TCD base address = 4021_0000h
                                       
                                       * 通道寄存器 CHn_CSR/ES 和 TCDn 都在这个页里,
                                       * 偏移 0x20 起是 TCD, 通道之间 0x4000。
                                       * 注意: 读管理页的 0x20 是保留地址 -> 总线错误。 */
#define EDMA_CH_OFF(n)   (EDMA_BASE + (uint32_t)(n) * 0x4000u)
#define EDMA_CH_CSR(n)   (*(volatile uint32_t *)(EDMA_CH_OFF(n) + 0x00u))
#define EDMA_CH_INT(n)   (*(volatile uint32_t *)(EDMA_CH_OFF(n) + 0x08u))
#define EDMA_CH_SBR(n)   (*(volatile uint32_t *)(EDMA_CH_OFF(n) + 0x0Cu))
#define EDMA_CH_PRI(n)   (*(volatile uint32_t *)(EDMA_CH_OFF(n) + 0x10u))
#define EDMA_CH_ES(n)    (*(volatile uint32_t *)(EDMA_CH_OFF(n) + 0x04u))
#define EDMA_TCD_SADDR(n)(*(volatile uint32_t *)(EDMA_CH_OFF(n) + 0x20u))
#define EDMA_TCD_SOFF(n) (*(volatile uint16_t *)(EDMA_CH_OFF(n) + 0x24u))
#define EDMA_TCD_ATTR(n) (*(volatile uint16_t *)(EDMA_CH_OFF(n) + 0x26u))
#define EDMA_TCD_NBYTES(n)(*(volatile uint32_t *)(EDMA_CH_OFF(n) + 0x28u))
#define EDMA_TCD_SLAST(n)(*(volatile uint32_t *)(EDMA_CH_OFF(n) + 0x2Cu))
#define EDMA_TCD_DADDR(n)(*(volatile uint32_t *)(EDMA_CH_OFF(n) + 0x30u))
#define EDMA_TCD_DOFF(n) (*(volatile uint16_t *)(EDMA_CH_OFF(n) + 0x34u))
#define EDMA_TCD_CITER(n)(*(volatile uint16_t *)(EDMA_CH_OFF(n) + 0x36u))
#define EDMA_TCD_DLAST(n)(*(volatile uint32_t *)(EDMA_CH_OFF(n) + 0x38u))
#define EDMA_TCD_CSR(n)  (*(volatile uint16_t *)(EDMA_CH_OFF(n) + 0x3Cu))
#define EDMA_TCD_BITER(n)(*(volatile uint16_t *)(EDMA_CH_OFF(n) + 0x3Eu))

#define DMAMUX_CFG(n)    (IP_DMAMUX_0->CHCFG[n])

#define DMA_CH_TX        0u
#define DMA_CH_RX        1u
#define DMA_ATTR_32BIT   0x0202u      /* DSIZE=32bit | SSIZE=32bit<<8 */
#define DMA_CSR_ERQ      0x00000001u
#define DMA_CSR_DONE     0x40000000u
#define DMA_TCDCSR_DREQ  0x0008u
#define DMA_TCDCSR_START 0x0001u
#define DMA_ES_VLD       0x80000000u
#define DMA_STAGE_LEN    80u

#ifndef LPSPI_DMA_TX_SRC
#define LPSPI_DMA_TX_SRC 43u
#endif
#ifndef LPSPI_DMA_RX_SRC
#define LPSPI_DMA_RX_SRC 44u
#endif

static uint32_t s_dma_tx_src = LPSPI_DMA_TX_SRC;
static uint32_t s_dma_rx_src = LPSPI_DMA_RX_SRC;
static int      s_dma_on;
static uint32_t s_dma_err;
static uint8_t  s_tx_stage[DMA_STAGE_LEN] __attribute__((aligned(32)));
static uint8_t  s_rx_stage[DMA_STAGE_LEN] __attribute__((aligned(32)));

/* Cortex-M7 的 cache 维护走 SCB 里的内存映射寄存器 (MVA 方式):
 * 本工具链的汇编器不认 dc cvac/dccmvac 助记符, 用 SCB->DCCMVAC/DCIMVAC 等价。
 * 地址: SCB(0xE000ED00) + 0x268 / 0x25C */
#define SCB_DCCMVAC (*(volatile uint32_t *)0xE000EF68u)
#define SCB_DCIMVAC (*(volatile uint32_t *)0xE000EF5Cu)

static void dcache_clean(void *addr, uint32_t len)
{
    uint32_t a = ((uint32_t)(uintptr_t)addr) & ~31u;
    uint32_t e = (uint32_t)(uintptr_t)addr + len;
    __asm volatile ("dsb" ::: "memory");
    for (; a < e; a += 32u) {
        SCB_DCCMVAC = a;
    }
    __asm volatile ("dsb" ::: "memory");
}

static void dcache_invalidate(void *addr, uint32_t len)
{
    uint32_t a = ((uint32_t)(uintptr_t)addr) & ~31u;
    uint32_t e = (uint32_t)(uintptr_t)addr + len;
    for (; a < e; a += 32u) {
        SCB_DCIMVAC = a;
    }
    __asm volatile ("dsb" ::: "memory");
}

static void dma_ch_setup(uint32_t ch, uint32_t saddr, uint16_t soff,
                         uint32_t daddr, uint16_t doff, uint32_t nbytes,
                         uint32_t n, int32_t slast, int32_t dlast)
{
    EDMA_CH_CSR(ch) = 0u;
    EDMA_CH_CSR(ch) = DMA_CSR_DONE;         /* W1C */
    EDMA_CH_ES(ch)  = 0xFFFFFFFFu;          /* W1C */
    EDMA_TCD_SADDR(ch) = saddr;
    EDMA_TCD_SOFF(ch)  = soff;
    EDMA_TCD_ATTR(ch)  = DMA_ATTR_32BIT;
    EDMA_TCD_NBYTES(ch)= nbytes;
    EDMA_TCD_SLAST(ch) = (uint32_t)slast;
    EDMA_TCD_DADDR(ch) = daddr;
    EDMA_TCD_DOFF(ch)  = doff;
    EDMA_TCD_CITER(ch) = (uint16_t)(n & 0x7FFFu);
    EDMA_TCD_DLAST(ch) = (uint32_t)dlast;
    EDMA_TCD_CSR(ch)   = DMA_TCDCSR_DREQ;
    EDMA_TCD_BITER(ch) = (uint16_t)(n & 0x7FFFu);
}

static void dma_ch_start(uint32_t ch)
{
    EDMA_CH_CSR(ch) |= DMA_CSR_ERQ;
    EDMA_TCD_CSR(ch) = (uint16_t)(DMA_TCDCSR_DREQ | DMA_TCDCSR_START);
}

/* 一次 32 位帧全双工事务, TX/RX 都由 eDMA 搬 */
/* ===== 生产路径: TX 用 eDMA 突发写 TDBR, RX 仍由 CPU 从 FIFO 读 =====
 * 依据 (全部实测):
 *  - 每个 DMAMUX 请求脉冲只驱动一个 eDMA minor loop (用 "Always On" 源验证过);
 *  - LPSPI 的 TDBR 是 128 字突发别名区, 一个 minor loop 就能把整块推进去, FIFO 不会溢出;
 *  - TDBR/RDBR 与 TDR/RDR 的字节序相反 (实测 S2500 回显字是反的), 所以写入前
 *    把每个字按小端重排 (等价于硬件 BYSW, 但只影响 DMA 路径);
 *  - RX 侧 RDF 是电平 (RXCOUNT>RXWATER), 一个脉冲只能驱动一个 minor loop,
 *    而 RDBR 读空 FIFO 不会等待, 所以 RX 突发读拿不到尚未到达的字 —— RX 保持 CPU 读。
 */
static int xfer32_dma(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    uint32_t nw = len >> 2;
    uint32_t i, guard;
    int rc = 0;

    if ((nw == 0u) || (nw > 32u)) {
        return xfer32(tx, rx, len);
    }

    /* TDBR 与 TDR 的字节序一致 (实测: 发 0x12345678, S2500 回显 0x12345678),
     * 所以按 MSB-first 组字即可, 不需要交换。 */
    for (i = 0u; i < nw; i++) {
        const uint8_t *p = &tx[i * 4u];
        ((uint32_t *)(void *)s_tx_stage)[i] =
            ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
            ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    }
    dcache_clean(s_tx_stage, len);
    dcache_invalidate(s_rx_stage, len);

    fifo_flush();
    cs_low();
    if (wait_tx_empty() != 0) { cs_high(); return -1; }
    LPSPI0->TCR = LPSPI_TCR_FRAMESZ(31u) | LPSPI_TCR_CONT_MASK;
    if (wait_tx_empty() != 0) { cs_high(); return -1; }

    DMAMUX_CFG(DMA_CH_TX) = (uint8_t)(43u | 0x80u);
    dma_ch_setup(DMA_CH_TX, (uint32_t)(uintptr_t)s_tx_stage, 4,
                 (uint32_t)(uintptr_t)&LPSPI0->TDBR[0], 4,
                 len, 1u, 0, -(int32_t)len);
    LPSPI0->DER = LPSPI_DER_TDDE_MASK;
    EDMA_TCD_CSR(DMA_CH_TX) = (uint16_t)(DMA_TCDCSR_DREQ | DMA_TCDCSR_START);

    /* RX: CPU 逐字从 FIFO 取, 既保证字节序, 也不会因为请求脉冲不够而卡住 */
    for (i = 0u; i < nw; i++) {
        uint32_t v;
        guard = GUARD_LONG;
        while (FSR_RXCNT() == 0u) {
            if (--guard == 0u) { s_info.timeout++; rc = -6; break; }
        }
        if (rc != 0) { break; }
        v = LPSPI0->RDR;
        (void)v;                        /* 先出队, 下面按 s_rx_stage 处理已无必要 */
        if (rx != NULL) {
            uint8_t *p = &rx[i * 4u];
            p[0] = (uint8_t)(v >> 24);
            p[1] = (uint8_t)(v >> 16);
            p[2] = (uint8_t)(v >> 8);
            p[3] = (uint8_t)v;
        }
    }

    guard = GUARD_LONG;
    while ((EDMA_CH_CSR(DMA_CH_TX) & DMA_CSR_DONE) == 0u) {
        if ((guard & 0xFFu) == 0u) { s2500_port_uart_poll(); }
        if (--guard == 0u) { s_info.timeout++; rc = -3; break; }
    }
    LPSPI0->DER = 0u;
    EDMA_CH_CSR(DMA_CH_TX) = DMA_CSR_DONE;
    if ((EDMA_CH_ES(DMA_CH_TX) & DMA_ES_VLD) != 0u) { s_info.dma_tx_err++; rc = -4; }
    EDMA_CH_ES(DMA_CH_TX)  = 0xFFFFFFFFu;
    DMAMUX_CFG(DMA_CH_TX) = 0u;

    if (wait_tx_empty() == 0) {
        LPSPI0->TCR = LPSPI_TCR_FRAMESZ(31u);
        guard = GUARD_LONG;
        while ((LPSPI0->SR & LPSPI_SR_MBF_MASK) != 0u) {
            if (--guard == 0u) { break; }
        }
    }
    LPSPI0->SR = SR_W1C;
    cs_high();
    return rc;
}

void lpspi_hw_dma(int on)
{
    s_dma_on = (on != 0) ? 1 : 0;
    s_info.dma_enabled = (uint32_t)s_dma_on;
}

int lpspi_hw_dma_enabled(void) { return s_dma_on; }

void lpspi_hw_dma_set_src(uint32_t tx, uint32_t rx)
{
    s_dma_tx_src = tx & 0x3Fu;
    s_dma_rx_src = rx & 0x3Fu;
}

/* eDMA 引擎自检: RAM -> RAM, 软件触发, 不涉及 DMAMUX/LPSPI。
 * 用来把"TCD 编程对不对"和"源号对不对"两件事分开。 */
int lpspi_hw_dma_ram_test(void)
{
    static uint32_t src[8] __attribute__((aligned(32))) = {
        0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u, 0, 0, 0, 0 };
    static uint32_t dst[8] __attribute__((aligned(32)));
    uint32_t guard, i, ok = 1;

    memset(dst, 0, sizeof(dst));
    dcache_clean(src, 16u);
    dcache_invalidate(dst, 16u);

    /* 软件触发一次性传输: NBYTES 覆盖整块, CITER=1 (每次请求只搬一个 minor loop)
     * 若用 NBYTES=4/CITER=4 而不重新置 ERQ, 只会搬走第一个字 (实测就是这样)。 */
    dma_ch_setup(2u, (uint32_t)(uintptr_t)src, 4, (uint32_t)(uintptr_t)dst, 4,
                 16u, 1u, -16, -16);
    EDMA_TCD_CSR(2u) = DMA_TCDCSR_START;      /* DREQ=0 -> 软件触发 */
    EDMA_CH_CSR(2u) = DMA_CSR_ERQ;

    guard = 100000u;
    while ((EDMA_CH_CSR(2u) & DMA_CSR_DONE) == 0u) {
        if ((guard & 0xFFu) == 0u) { s2500_port_uart_poll(); }
        if (--guard == 0u) { break; }
    }
    printf("RAM->RAM: ch2 CSR=%08lx ES=%08lx CITER=%u\r\n",
           (unsigned long)EDMA_CH_CSR(2u), (unsigned long)EDMA_CH_ES(2u),
           (unsigned)EDMA_TCD_CITER(2u));
    printf("  src:");
    for (i = 0u; i < 4u; i++) { printf(" %08lx", (unsigned long)src[i]); }
    printf("\r\n  dst:");
    for (i = 0u; i < 4u; i++) {
        printf(" %08lx", (unsigned long)dst[i]);
        if (dst[i] != src[i]) { ok = 0u; }
    }
    printf("\r\n  => %s\r\n", (ok != 0u) ? "PASS (TCD 编程正确)" : "FAIL");
    EDMA_CH_CSR(2u) = DMA_CSR_DONE;
    EDMA_CH_ES(2u) = 0xFFFFFFFFu;
    return (int)ok;
}






/* MC_ME 分区时钟门 (eDMA/DMAMUX 的 CLKEN 位) */
#define MC_ME_BASE      0x402DC000u
#define ME_PRTN1_PCONF  (*(volatile uint32_t *)(MC_ME_BASE + 0x300u))
#define ME_PRTN1_PUPD   (*(volatile uint32_t *)(MC_ME_BASE + 0x304u))
#define ME_COFB0_STAT   (*(volatile uint32_t *)(MC_ME_BASE + 0x310u))
#define ME_COFB1_STAT   (*(volatile uint32_t *)(MC_ME_BASE + 0x314u))
#define ME_COFB0_CLKEN  (*(volatile uint32_t *)(MC_ME_BASE + 0x330u))
#define ME_COFB1_CLKEN  (*(volatile uint32_t *)(MC_ME_BASE + 0x334u))
#define ME_REQ3         (1u << 3)    /* EDMA0_CLK  */
#define ME_REQ4         (1u << 4)    /* EDMA0_TCD0 */
#define ME_REQ5         (1u << 5)    /* EDMA0_TCD1 */
#define ME_REQ0_COL1    (1u << 0)    /* DMAMUX0_CLK = REQ32 */

int lpspi_hw_dma_clock(int fix)
{
    uint32_t need0 = ME_REQ3 | ME_REQ4 | ME_REQ5;
    uint32_t need1 = ME_REQ0_COL1;

    printf("MC_ME PRTN1: PCONF=%08lx PUPD=%08lx\r\n",
           (unsigned long)ME_PRTN1_PCONF, (unsigned long)ME_PRTN1_PUPD);
    printf("  COFB0 STAT=%08lx CLKEN=%08lx (REQ3=EDMA0,REQ4/5=TCD0/1)\r\n",
           (unsigned long)ME_COFB0_STAT, (unsigned long)ME_COFB0_CLKEN);
    printf("  COFB1 STAT=%08lx CLKEN=%08lx (REQ32=DMAMUX0)\r\n",
           (unsigned long)ME_COFB1_STAT, (unsigned long)ME_COFB1_CLKEN);

    if (fix == 0) {
        return 0;
    }
    if (((ME_COFB0_CLKEN & need0) == need0) && ((ME_COFB1_CLKEN & need1) == need1)) {
        printf("  门控已开, 无需修改\r\n");
        return 1;
    }
    ME_COFB0_CLKEN |= need0;
    ME_COFB1_CLKEN |= need1;
    ME_PRTN1_PCONF |= 0x1u;          /* PCE */
    ME_PRTN1_PUPD  |= 0x1u;          /* PCUD */
    {
        uint32_t guard = 1000000u;
        while ((ME_PRTN1_PUPD & 0x1u) != 0u) { if (--guard == 0u) { break; } }
    }
    printf("  已补开门控: COFB0=%08lx COFB1=%08lx\r\n",
           (unsigned long)ME_COFB0_CLKEN, (unsigned long)ME_COFB1_CLKEN);
    return 1;
}

/* 正确用法 (对照 RM 70.3.6.1 "DMA support registers"):
 * TDBR0..127 (0x400..0x5FC) / RDBR0..127 (0x600..0x7FC) 是 128 字的突发别名区,
 * DMA 用"地址递增"的方式一次 minor loop 把整块写进去/读出来;
 * 每次 DMAMUX 请求只驱动一个 minor loop, 所以必须 NBYTES=整块, CITER=1。 */
int lpspi_hw_dma_burst_test(void)
{
    static const uint8_t expect[12] = {
        0x00, 0x00, 0x3c, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11
    };
    uint8_t tx[12] = { 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t rx[12];
    uint32_t guard, k, ok = 1u;

    memset(rx, 0, sizeof(rx));
    memcpy(s_tx_stage, tx, 12u);
    dcache_clean(s_tx_stage, 12u);
    dcache_invalidate(s_rx_stage, 12u);

    module_setup();
    apply_half(s_half);
    fifo_flush();
    LPSPI0->FCR = 0u;
    cs_low();
    (void)wait_tx_empty();
    LPSPI0->TCR = LPSPI_TCR_FRAMESZ(31u) | LPSPI_TCR_CONT_MASK;
    (void)wait_tx_empty();

    DMAMUX_CFG(DMA_CH_TX) = (uint8_t)(43u | 0x80u);
    DMAMUX_CFG(DMA_CH_RX) = (uint8_t)(44u | 0x80u);

    /* TX: 一个 minor loop 写满 12 字节到 TDBR0..2 */
    dma_ch_setup(DMA_CH_TX, (uint32_t)(uintptr_t)s_tx_stage, 4,
                 (uint32_t)(uintptr_t)&LPSPI0->TDBR[0], 4, 12u, 1u, 0, -12);
    /* RX: 一个 minor loop 从 RDBR0..2 读 12 字节 */
    dma_ch_setup(DMA_CH_RX, (uint32_t)(uintptr_t)&LPSPI0->RDBR[0], 4,
                 (uint32_t)(uintptr_t)s_rx_stage, 4, 12u, 1u, -12, 0);

    /* 关键: 不要自己置 ERQ —— 置 ERQ 等于立刻做一次软件请求,
     * 会让 RX 在数据到来之前就把 RDBR 读走 (读回全 0)。
     * 只 START + DREQ, 让 TDF/RDF 的硬件请求脉冲各驱动一个 minor loop。 */
    LPSPI0->DER = LPSPI_DER_TDDE_MASK | LPSPI_DER_RDDE_MASK;
    EDMA_TCD_CSR(DMA_CH_RX) = (uint16_t)(DMA_TCDCSR_DREQ | DMA_TCDCSR_START);
    EDMA_TCD_CSR(DMA_CH_TX) = (uint16_t)(DMA_TCDCSR_DREQ | DMA_TCDCSR_START);

    guard = 60000u;
    while (((EDMA_CH_CSR(DMA_CH_TX) & DMA_CSR_DONE) == 0u) ||
           ((EDMA_CH_CSR(DMA_CH_RX) & DMA_CSR_DONE) == 0u)) {
        if ((guard & 0xFFu) == 0u) { s2500_port_uart_poll(); }
        if (--guard == 0u) { break; }
    }
    LPSPI0->DER = 0u;
    (void)wait_tx_empty();
    LPSPI0->TCR = LPSPI_TCR_FRAMESZ(31u);
    guard = 20000u;
    while ((LPSPI0->SR & LPSPI_SR_MBF_MASK) != 0u) { if (--guard == 0u) { break; } }
    cs_high();

    printf("  FSR=%08lx SR=%08lx LCDR(before clear)\r\n",
           (unsigned long)LPSPI0->FSR, (unsigned long)LPSPI0->SR);
    printf("burst-DMA: TX DONE=%lu CITER=%u ES=%08lx | RX DONE=%lu CITER=%u ES=%08lx\r\n",
           (unsigned long)((EDMA_CH_CSR(DMA_CH_TX) & DMA_CSR_DONE) ? 1u : 0u),
           (unsigned)EDMA_TCD_CITER(DMA_CH_TX), (unsigned long)EDMA_CH_ES(DMA_CH_TX),
           (unsigned long)((EDMA_CH_CSR(DMA_CH_RX) & DMA_CSR_DONE) ? 1u : 0u),
           (unsigned)EDMA_TCD_CITER(DMA_CH_RX), (unsigned long)EDMA_CH_ES(DMA_CH_RX));
    printf("  rx(stage):");
    for (k = 0u; k < 12u; k++) {
        printf(" %02x", (unsigned)s_rx_stage[k]);
        if (s_rx_stage[k] != expect[k]) { ok = 0u; }
    }
    printf("\r\n  => %s\r\n", (ok != 0u) ? "PASS (IDVER 读回正确)" : "FAIL");

    EDMA_CH_CSR(DMA_CH_TX) = DMA_CSR_DONE;
    EDMA_CH_CSR(DMA_CH_RX) = DMA_CSR_DONE;
    EDMA_CH_ES(DMA_CH_TX) = 0xFFFFFFFFu;
    EDMA_CH_ES(DMA_CH_RX) = 0xFFFFFFFFu;
    DMAMUX_CFG(DMA_CH_TX) = 0u;
    DMAMUX_CFG(DMA_CH_RX) = 0u;
    module_setup();
    apply_half(s_half);
    return (int)ok;
}

/* 变体: TX 先用 burst 发出去, 等 RXCOUNT>0 之后再用一个 minor loop 从 RDBR 读整块,
 * 验证 RDBR 的突发读是否会"等数据"(若不等于就会读到 0)。 */


/* 组合方案: TX 一个 minor loop 突发写 TDBR (128 字缓冲, 不会溢出),
 * RX 每字一个 minor loop (NBYTES=4/CITER=nw), 让 FIFO 在字间被抽干,
 * 这样每个字都能产生一次 RDF 上升沿 -> 每次一个请求 -> 一个 minor loop。
 * 字节序: TDBR/RDBR 与 TDR/RDR 相反, 用 TCR.BYSW=1 让硬件交换。 */
static uint32_t s_best_rx[4];

int lpspi_hw_dma_best_test(void)
{
    static const uint32_t expect[3] = { 0x00003C00u, 0x10000000u, 0x00000011u };
    uint8_t tx[12] = { 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint32_t guard, k, ok = 1u;

    memset(s_rx_stage, 0, 16u);
    memcpy(s_tx_stage, tx, 12u);
    dcache_clean(s_tx_stage, 12u);
    dcache_invalidate(s_rx_stage, 16u);

    module_setup();
    apply_half(s_half);
    fifo_flush();
    LPSPI0->FCR = 0u;
    cs_low();
    (void)wait_tx_empty();
    LPSPI0->TCR = LPSPI_TCR_FRAMESZ(31u) | LPSPI_TCR_CONT_MASK | LPSPI_TCR_BYSW_MASK;
    (void)wait_tx_empty();

    DMAMUX_CFG(DMA_CH_TX) = (uint8_t)(43u | 0x80u);
    DMAMUX_CFG(DMA_CH_RX) = (uint8_t)(44u | 0x80u);
    /* TX: 整块突发 */
    dma_ch_setup(DMA_CH_TX, (uint32_t)(uintptr_t)s_tx_stage, 4,
                 (uint32_t)(uintptr_t)&LPSPI0->TDBR[0], 4, 12u, 1u, 0, -12);
    /* RX: 每次请求搬 2 个字 (RXWATER=1 -> RXCOUNT>1 才请求), 搬完正好清空 FIFO,
     * 下一个字到来时再产生一次请求脉冲 (每次请求只能驱动一个 minor loop)。 */
    LPSPI0->FCR = (1u << 16);        /* RXWATER=1 */
    dma_ch_setup(DMA_CH_RX, (uint32_t)(uintptr_t)&LPSPI0->RDBR[0], 4,
                 (uint32_t)(uintptr_t)s_rx_stage, 4, 8u, 2u, -16, 0);

    LPSPI0->DER = LPSPI_DER_TDDE_MASK | LPSPI_DER_RDDE_MASK;
    EDMA_TCD_CSR(DMA_CH_RX) = (uint16_t)(DMA_TCDCSR_DREQ | DMA_TCDCSR_START);
    EDMA_TCD_CSR(DMA_CH_TX) = (uint16_t)(DMA_TCDCSR_DREQ | DMA_TCDCSR_START);

    guard = 60000u;
    while ((EDMA_CH_CSR(DMA_CH_RX) & DMA_CSR_DONE) == 0u) {
        if ((guard & 0xFFu) == 0u) { s2500_port_uart_poll(); }
        if (--guard == 0u) { break; }
    }
    LPSPI0->DER = 0u;
    (void)wait_tx_empty();
    LPSPI0->TCR = LPSPI_TCR_FRAMESZ(31u);
    guard = 20000u;
    while ((LPSPI0->SR & LPSPI_SR_MBF_MASK) != 0u) { if (--guard == 0u) { break; } }
    cs_high();

    printf("best: TX DONE=%lu CITER=%u | RX DONE=%lu CITER=%u FSR=%08lx\r\n",
           (unsigned long)((EDMA_CH_CSR(DMA_CH_TX) & DMA_CSR_DONE) ? 1u : 0u),
           (unsigned)EDMA_TCD_CITER(DMA_CH_TX),
           (unsigned long)((EDMA_CH_CSR(DMA_CH_RX) & DMA_CSR_DONE) ? 1u : 0u),
           (unsigned)EDMA_TCD_CITER(DMA_CH_RX), (unsigned long)LPSPI0->FSR);

    {
        const uint32_t *w = (const uint32_t *)(const void *)s_rx_stage;
        printf("  rx:");
        for (k = 0u; k < 4u; k++) {
            printf(" %08lx", (unsigned long)w[k]);
            if (w[k] != expect[k]) { ok = 0u; }
        }
        printf("\r\n  => %s\r\n", (ok != 0u) ? "PASS (DMA 全链路正确)" : "FAIL");
        s_best_rx[0] = w[0]; s_best_rx[1] = w[1]; s_best_rx[2] = w[2];
    }

    EDMA_CH_CSR(DMA_CH_TX) = DMA_CSR_DONE;
    EDMA_CH_CSR(DMA_CH_RX) = DMA_CSR_DONE;
    EDMA_CH_ES(DMA_CH_TX) = 0xFFFFFFFFu;
    EDMA_CH_ES(DMA_CH_RX) = 0xFFFFFFFFu;
    DMAMUX_CFG(DMA_CH_TX) = 0u;
    DMAMUX_CFG(DMA_CH_RX) = 0u;
    module_setup();
    apply_half(s_half);
    return (int)ok;
}

/* 用不可解析但可回显的包头 0x12345678 探明 TDBR 的字节序:
 * S2500 的 rx 第 2 个字就是它实际收到的包头, 一比即知 */
int lpspi_hw_dma_byte_test(void)
{
    uint8_t tx[12];
    uint32_t pass, guard, k;

    memset(tx, 0, sizeof(tx));
    tx[0] = 0x12; tx[1] = 0x34; tx[2] = 0x56; tx[3] = 0x78;

    for (pass = 0u; pass < 2u; pass++) {
        memset(s_rx_stage, 0, 16u);
        /* pass0: 预交换; pass1: 不预交换 */
        for (k = 0u; k < 3u; k++) {
            const uint8_t *p = &tx[k * 4u];
            if (pass == 0u) {
                ((uint32_t *)(void *)s_tx_stage)[k] =
                    (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                    ((uint32_t)p[3] << 24);
            } else {
                ((uint32_t *)(void *)s_tx_stage)[k] =
                    ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                    ((uint32_t)p[2] << 8) | (uint32_t)p[3];
            }
        }
        dcache_clean(s_tx_stage, 12u);
        fifo_flush();
        cs_low();
        (void)wait_tx_empty();
        LPSPI0->TCR = LPSPI_TCR_FRAMESZ(31u) | LPSPI_TCR_CONT_MASK;
        (void)wait_tx_empty();
        DMAMUX_CFG(DMA_CH_TX) = (uint8_t)(43u | 0x80u);
        dma_ch_setup(DMA_CH_TX, (uint32_t)(uintptr_t)s_tx_stage, 4,
                     (uint32_t)(uintptr_t)&LPSPI0->TDBR[0], 4, 12u, 1u, 0, -12);
        LPSPI0->DER = LPSPI_DER_TDDE_MASK;
        EDMA_TCD_CSR(DMA_CH_TX) = (uint16_t)(DMA_TCDCSR_DREQ | DMA_TCDCSR_START);
        printf("pass%lu (%s): rx =", (unsigned long)pass,
               (pass == 0u) ? "预交换" : "不预交换");
        for (k = 0u; k < 3u; k++) {
            guard = 40000u;
            while (((LPSPI0->FSR >> 16) & 0x7u) == 0u) { if (--guard == 0u) { break; } }
            printf(" %08lx", (unsigned long)LPSPI0->RDR);
        }
        printf("\r\n");
        LPSPI0->DER = 0u;
        DMAMUX_CFG(DMA_CH_TX) = 0u;
        EDMA_CH_CSR(DMA_CH_TX) = DMA_CSR_DONE;
        EDMA_CH_ES(DMA_CH_TX) = 0xFFFFFFFFu;
        (void)wait_tx_empty();
        LPSPI0->TCR = LPSPI_TCR_FRAMESZ(31u);
        guard = 20000u;
        while ((LPSPI0->SR & LPSPI_SR_MBF_MASK) != 0u) { if (--guard == 0u) { break; } }
        cs_high();
        fifo_flush();
    }
    module_setup();
    apply_half(s_half);
    return 0;
}




/* 只读寄存器转储 (先确认 eDMA 寄存器本身可访问) */
void lpspi_hw_dma_regs(void)
{
    uint32_t i;

    (void)lpspi_hw_dma_clock(1);
    printf("eDMA mgmt=%08lx CSR=%08lx ES=%08lx HRS=%08lx\r\n",
           (unsigned long)EDMA_MGMT_BASE,
           (unsigned long)(*(volatile uint32_t *)(EDMA_MGMT_BASE + 0x00u)),
           (unsigned long)(*(volatile uint32_t *)(EDMA_MGMT_BASE + 0x04u)),
           (unsigned long)(*(volatile uint32_t *)(EDMA_MGMT_BASE + 0x0Cu)));
    printf("eDMA base=%08lx (RM 36157: eDMA base address 4020_C000h)\r\n",
           (unsigned long)EDMA_BASE);
    for (i = 0u; i < 2u; i++) {
        printf(" ch%lu CSR=%08lx ES=%08lx INT=%08lx SBR=%08lx PRI=%08lx\r\n",
               (unsigned long)i, (unsigned long)EDMA_CH_CSR(i), (unsigned long)EDMA_CH_ES(i),
               (unsigned long)EDMA_CH_INT(i), (unsigned long)EDMA_CH_SBR(i),
               (unsigned long)EDMA_CH_PRI(i));
        printf("    TCD SADDR=%08lx SOFF=%u ATTR=%04x NBYTES=%08lx SLAST=%08lx\r\n",
               (unsigned long)EDMA_TCD_SADDR(i), (unsigned)EDMA_TCD_SOFF(i),
               (unsigned)EDMA_TCD_ATTR(i), (unsigned long)EDMA_TCD_NBYTES(i),
               (unsigned long)EDMA_TCD_SLAST(i));
        printf("        DADDR=%08lx DOFF=%u CITER=%u DLAST=%08lx CSR=%04x BITER=%u\r\n",
               (unsigned long)EDMA_TCD_DADDR(i), (unsigned)EDMA_TCD_DOFF(i),
               (unsigned)EDMA_TCD_CITER(i), (unsigned long)EDMA_TCD_DLAST(i),
               (unsigned)EDMA_TCD_CSR(i), (unsigned)EDMA_TCD_BITER(i));
    }
    for (i = 0u; i < 6u; i++) {
        printf(" DMAMUX ch%lu = %02x\r\n", (unsigned long)i, (unsigned)DMAMUX_CFG(i));
    }
}

/* 单次 DMA 事务, 分阶段打印, 便于定位卡在哪一步 */
int lpspi_hw_dma_test(uint32_t txs, uint32_t rxs)
{
    uint8_t tx[12] = { 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t rx[12];
    uint32_t guard;
    int rc = 0;

    s_dma_tx_src = txs & 0x3Fu;
    s_dma_rx_src = rxs & 0x3Fu;
    memset(rx, 0, sizeof(rx));
    memcpy(s_tx_stage, tx, 12u);
    dcache_clean(s_tx_stage, 12u);
    dcache_invalidate(s_rx_stage, 12u);
    printf("dmatest TX=%lu RX=%lu\r\n", (unsigned long)s_dma_tx_src, (unsigned long)s_dma_rx_src);

    fifo_flush();
    printf("  fifo flushed, FSR=%08lx\r\n", (unsigned long)LPSPI0->FSR);
    cs_low();
    (void)wait_tx_empty();
    LPSPI0->TCR = LPSPI_TCR_FRAMESZ(31u) | LPSPI_TCR_CONT_MASK;
    (void)wait_tx_empty();
    printf("  tcr set, FSR=%08lx SR=%08lx\r\n",
           (unsigned long)LPSPI0->FSR, (unsigned long)LPSPI0->SR);

    DMAMUX_CFG(DMA_CH_TX) = (uint8_t)((s_dma_tx_src & 0x3Fu) | 0x80u);
    DMAMUX_CFG(DMA_CH_RX) = (uint8_t)((s_dma_rx_src & 0x3Fu) | 0x80u);
    dma_ch_setup(DMA_CH_TX, (uint32_t)(uintptr_t)s_tx_stage, 4,
                 (uint32_t)(uintptr_t)&LPSPI0->TDR, 0, 4u, 3u,
                 -12, 0);
    dma_ch_setup(DMA_CH_RX, (uint32_t)(uintptr_t)&LPSPI0->RDR, 0,
                 (uint32_t)(uintptr_t)s_rx_stage, 4, 4u, 3u,
                 0, -12);
    printf("  tcd armed: ch0 CSR=%08lx ES=%08lx\r\n",
           (unsigned long)EDMA_CH_CSR(0), (unsigned long)EDMA_CH_ES(0));

    LPSPI0->DER = LPSPI_DER_TDDE_MASK | LPSPI_DER_RDDE_MASK;
    dma_ch_start(DMA_CH_RX);
    dma_ch_start(DMA_CH_TX);
    printf("  started: ch0 CSR=%08lx ch1 CSR=%08lx\r\n",
           (unsigned long)EDMA_CH_CSR(0), (unsigned long)EDMA_CH_CSR(1));

    guard = 50000u;
    while ((((EDMA_CH_CSR(DMA_CH_TX)) & DMA_CSR_DONE) == 0u) ||
           (((EDMA_CH_CSR(DMA_CH_RX)) & DMA_CSR_DONE) == 0u)) {
        if (--guard == 0u) { rc = -3; break; }
    }
    printf("  done rc=%d ch0 CSR=%08lx ES=%08lx ch1 CSR=%08lx ES=%08lx\r\n", rc,
           (unsigned long)EDMA_CH_CSR(0), (unsigned long)EDMA_CH_ES(0),
           (unsigned long)EDMA_CH_CSR(1), (unsigned long)EDMA_CH_ES(1));
    LPSPI0->DER = 0u;
    EDMA_CH_CSR(DMA_CH_TX) = DMA_CSR_DONE;
    EDMA_CH_CSR(DMA_CH_RX) = DMA_CSR_DONE;
    EDMA_CH_ES(DMA_CH_TX) = 0xFFFFFFFFu;
    EDMA_CH_ES(DMA_CH_RX) = 0xFFFFFFFFu;
    (void)wait_tx_empty();
    LPSPI0->TCR = LPSPI_TCR_FRAMESZ(31u);
    guard = GUARD_LONG;
    while ((LPSPI0->SR & LPSPI_SR_MBF_MASK) != 0u) { if (--guard == 0u) { break; } }
    cs_high();

    printf("  rx:");
    {
        uint32_t k;
        for (k = 0u; k < 12u; k++) { printf(" %02x", (unsigned)rx[k]); }
        printf("\r\n  (全局 dma_on 未改动 = %d)\r\n", s_dma_on);
    }
    return rc;
}

/* 用一笔真实的 IDVER 读回值反查 DMAMUX 源号 (RM 的源号表在单独的 xlsx 附件里) */
int lpspi_hw_dma_probe(void)
{
    static const uint8_t expect[12] = {
        0x00, 0x00, 0x3c, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11
    };
    uint8_t tx[12] = { 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t rx[12];
    uint32_t txs, rxs;
    int found = 0;

    printf("DMA 源扫描 (40..50 x 40..50), 判据 = IDVER 读回 00 00 3c 00 10.. 11\r\n");
    for (txs = 0u; (txs <= 63u) && (found == 0); txs++) {
        for (rxs = 0u; rxs <= 63u; rxs++) {
            uint32_t k;
            int ok = 1;
            s_dma_tx_src = txs;
            s_dma_rx_src = rxs;
            memset(rx, 0, sizeof(rx));
            if (xfer32_dma(tx, rx, 12u) != 0) { continue; }
            for (k = 0u; k < 12u; k++) {
                if (rx[k] != expect[k]) { ok = 0; break; }
            }
            if (ok != 0) {
                printf("  ==> TX 源=%lu RX 源=%lu 命中 (rx:", (unsigned long)txs,
                       (unsigned long)rxs);
                for (k = 0u; k < 12u; k++) { printf(" %02x", (unsigned)rx[k]); }
                printf(")\r\n");
                found = 1;
                break;
            }
        }
    }
    if (found == 0) {
        s_dma_tx_src = LPSPI_DMA_TX_SRC;
        s_dma_rx_src = LPSPI_DMA_RX_SRC;
        printf("  未命中, 回落 TX=%lu RX=%lu\r\n",
               (unsigned long)s_dma_tx_src, (unsigned long)s_dma_rx_src);
    }
    return found;
}

/* ---------------- 标定 ---------------- */
static uint32_t calibrate_func_hz(void)
{
    static uint8_t a[68] __attribute__((aligned(4)));
    static uint8_t b[68] __attribute__((aligned(4)));
    const uint32_t iters = 20u;
    uint32_t i, t0, t1, ms, sck;

    memset(a, 0xA5, sizeof(a));
    apply_half(63u);
    (void)xfer32(a, b, 68u);            /* 预热 */
    t0 = SysTick_GetMs();
    for (i = 0u; i < iters; i++) {
        (void)xfer32(a, b, 68u);
    }
    t1 = SysTick_GetMs();
    ms = t1 - t0;
    if (ms == 0u) { return 0u; }
    /* 27 个 32 位帧 = 864 bit, 每笔 */
    sck = (uint32_t)(((uint64_t)iters * 68u * 8u * 1000u) / (uint64_t)ms);
    /* SCK = func / (2*(half+1)) */
    return sck * 2u * 64u;
}

/* ---------------- 对外 API ---------------- */
void lpspi_hw_init(uint32_t target_hz)
{
    s_info.xfers = 0u;
    s_info.bytes = 0u;
    s_info.timeout = 0u;
    s_info.dma_tx_err = 0u;
    s_info.dma_rx_err = 0u;
    s_info.dma_enabled = 0u;

    pins_hw();
    module_setup();

    s_info.dma_enabled = (uint32_t)s_dma_on;

    s_func_hz = calibrate_func_hz();
    if (s_func_hz == 0u) { s_func_hz = 60000000u; }
    s_inited = 1;
    (void)lpspi_hw_set_speed(target_hz != 0u ? target_hz : LPSPI_HW_TARGET_HZ);
}

void lpspi_hw_stop(void)
{
    LPSPI0->CR = 0u;
    s_inited = 0;
}

uint32_t lpspi_hw_set_speed(uint32_t target_hz)
{
    uint32_t div2, half, limit;

    if (target_hz == 0u) { target_hz = LPSPI_HW_TARGET_HZ; }
    if (s_func_hz == 0u) { s_func_hz = 60000000u; }

    /* SCK = func / (2*(half+1)); 允许 5% 超调取整 (功能时钟标定有 ±1% 误差),
     * 再用 S2500 的 25MHz 上限硬卡一道, 避免取到 30MHz 这种超规格档。 */
    limit = target_hz + (target_hz / 20u);
    if (limit > LPSPI_HW_MAX_HZ) { limit = LPSPI_HW_MAX_HZ; }
    div2 = (s_func_hz + (2u * limit) - 1u) / (2u * limit);
    if (div2 == 0u) { div2 = 1u; }
    if (div2 > 256u) { div2 = 256u; }
    half = div2 - 1u;

    apply_half(half);
    s_sck_hz = s_func_hz / (2u * (half + 1u));

    s_info.sck_hz = s_sck_hz;
    s_info.func_hz = s_func_hz;
    s_info.half = half;
    s_info.tx_depth = s_tx_depth;
    s_info.rx_depth = s_rx_depth;
    return s_sck_hz;
}

uint32_t lpspi_hw_sck_hz(void) { return s_sck_hz; }

void lpspi_hw_info(lpspi_hw_info_t *out)
{
    if (out != NULL) {
        *out = s_info;
        out->sck_hz = s_sck_hz;
        out->func_hz = s_func_hz;
        out->half = s_half;
        out->tx_depth = s_tx_depth;
        out->rx_depth = s_rx_depth;
    }
}

int lpspi_hw_xfer(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    int rc;

    if (len == 0u) { return 0; }
    if (s_inited == 0) { return -1; }

    if ((len & 3u) == 0u) {
        rc = ((s_dma_on != 0) && (len <= DMA_STAGE_LEN)) ? xfer32_dma(tx, rx, len)
                                                        : xfer32(tx, rx, len);
    } else {
        rc = xfer8(tx, rx, len);
    }

    s_info.xfers++;
    s_info.bytes += len;
    return rc;
}

/* ---------------- 诊断 ---------------- */
void lpspi_hw_dump(void)
{
    uint32_t par = LPSPI0->PARAM;

    printf("LPSPI0 VERID=%08lx PARAM=%08lx TXFIFO=%lu RXFIFO=%lu\r\n",
           (unsigned long)LPSPI0->VERID, (unsigned long)par,
           (unsigned long)(par & 0xFFu), (unsigned long)((par >> 8) & 0xFFu));
    printf("  CR=%08lx SR=%08lx CFGR0=%08lx CFGR1=%08lx\r\n",
           (unsigned long)LPSPI0->CR, (unsigned long)LPSPI0->SR,
           (unsigned long)LPSPI0->CFGR0, (unsigned long)LPSPI0->CFGR1);
    printf("  CCR=%08lx CCR1=%08lx FCR=%08lx FSR=%08lx TCR=%08lx\r\n",
           (unsigned long)LPSPI0->CCR, (unsigned long)LPSPI0->CCR1,
           (unsigned long)LPSPI0->FCR, (unsigned long)LPSPI0->FSR,
           (unsigned long)LPSPI0->TCR);
    printf("  MSCR36=%08lx MSCR37=%08lx MSCR111=%08lx MSCR112=%08lx IMCR230=%08lx\r\n",
           (unsigned long)SIUL2_MSCR(PIN_MOSI), (unsigned long)SIUL2_MSCR(PIN_CS),
           (unsigned long)SIUL2_MSCR(PIN_SCK), (unsigned long)SIUL2_MSCR(PIN_MISO),
           (unsigned long)SIUL2_IMCR(IMCR_MISO));
    printf("  DMA=%s TXsrc=%lu RXsrc=%lu err=%lu txerr=%lu rxerr=%lu\r\n",
           (s_dma_on != 0) ? "ON" : "off", (unsigned long)s_dma_tx_src,
           (unsigned long)s_dma_rx_src, (unsigned long)s_dma_err,
           (unsigned long)s_info.dma_tx_err, (unsigned long)s_info.dma_rx_err);
    printf("  SCK=%lukHz func=%luMHz half=%lu xfer=%lu bytes=%lu timeout=%lu\r\n",
           (unsigned long)(s_sck_hz / 1000u), (unsigned long)(s_func_hz / 1000000u),
           (unsigned long)s_half, (unsigned long)s_info.xfers,
           (unsigned long)s_info.bytes, (unsigned long)s_info.timeout);
}

/* 调试: 同一笔控制事务先用软件 SPI 再用硬件 SPI 收, 对比两边读回值 */
void lpspi_hw_cmp2(void)
{
    static uint8_t t[12], rs[12], rh[12];
    s2500_dev_t *dev = s2500_app_dev();
    uint32_t v = 0u;
    int k;

    if (dev == NULL) { printf("dev NULL\r\n"); return; }

    (void)s2500_read_reg(dev, S2500_MMS_STD, S2500_REG_IDVER, &v);
    for (k = 0; k < 12; k++) { t[k] = dev->txb[k]; rs[k] = 0u; rh[k] = 0u; }

    /* 软件 SPI */
    lpspi_hw_stop();
    spi_soft_init();
    (void)spi_soft_xfer(t, rs, 12u);

    /* 硬件 SPI */
    pins_hw();
    module_setup();
    apply_half(s_half);
    s_inited = 1;
    (void)lpspi_hw_xfer(t, rh, 12u);

    printf("tx :");
    for (k = 0; k < 12; k++) { printf(" %02x", (unsigned)t[k]); }
    printf("\r\nsw :");
    for (k = 0; k < 12; k++) { printf(" %02x", (unsigned)rs[k]); }
    printf("\r\nhw :");
    for (k = 0; k < 12; k++) { printf(" %02x", (unsigned)rh[k]); }
    printf("\r\nidver=%08lx\r\n", (unsigned long)v);
}
