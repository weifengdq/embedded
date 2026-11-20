/**
 * @file  lpspi_diag.c
 * @brief LPSPI0 硬件 SPI 深度诊断 (不进默认数据通路, 仅 shell 调试)
 *
 * 提供三类证据, 全部基于芯片自身, 不需要逻辑分析仪:
 *  1) regs  : VERID/PARAM(FIFO 深度)/CR/SR/CFGR/FCR/FSR/TCR/RSR + 焊盘 MSCR/GPDI
 *  2) hw    : 逐帧 trace (锁步/突发两种节拍), 打印每一帧 TXCOUNT/RXCOUNT/SR/RDR
 *  3) wav   : 用 GPDI + DWT 周期计数器当"逻辑分析仪", 在一次真实硬件传输中
 *             逐 SCK 上升沿采样 MOSI/MISO 焊盘, 事后按 MSB-first 还原字节,
 *             直接回答"SCK 到底打了多少拍 / MOSI 到底发出去了什么 / MISO 收了什么"
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "S32K312_SIUL2.h"
#include "S32K312_LPSPI.h"
#include "systick.h"
#include "spi_soft.h"
#include "shell.h"

#define LPSPI0          IP_LPSPI_0

#define SIUL2_MSCR(n)   (*(volatile uint32_t *)((uintptr_t)IP_SIUL2 + 0x240u + 4u * (uint32_t)(n)))
#define SIUL2_IMCR(n)   (*(volatile uint32_t *)((uintptr_t)IP_SIUL2 + 0xA40u + 4u * (uint32_t)(n)))

#define MSCR_SSS(v)     ((uint32_t)((v) & 0x7u))
#define MSCR_SRC        (0x00004000u)
#define MSCR_IBE        (0x00080000u)
#define MSCR_OBE        (0x00200000u)

#define PIN_MOSI        36u
#define PIN_CS          37u
#define PIN_SCK         111u
#define PIN_MISO        112u
#define IMCR_MISO       230u

#define GPDO_OFF        0x1300u
#define GPDI_OFF        0x1500u

#define DWT_CTRL        (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT      (*(volatile uint32_t *)0xE0001004u)
#define DEMCR           (*(volatile uint32_t *)0xE000EDFCu)

#define WAV_MAX         400u

static volatile uint8_t *s_cs_o, *s_sck_o, *s_mosi_o;
static volatile uint8_t *s_miso_i, *s_mosi_i, *s_sck_i;
static uint32_t s_tx_depth = 4u;
static uint32_t s_rx_depth = 4u;

static uint8_t  s_wav_mosi[WAV_MAX];
static uint8_t  s_wav_miso[WAV_MAX];
static uint32_t s_wav_n;
static uint32_t s_wav_cycles;

static inline volatile uint8_t *pin_gpdo(uint32_t pin)
{
    uint32_t off = (pin & ~3u) | ((~pin) & 3u);
    return (volatile uint8_t *)((uintptr_t)IP_SIUL2 + GPDO_OFF + off);
}

static inline volatile uint8_t *pin_gpdi(uint32_t pin)
{
    uint32_t off = (pin & ~3u) | ((~pin) & 3u);
    return (volatile uint8_t *)((uintptr_t)IP_SIUL2 + GPDI_OFF + off);
}

static void dwt_start(void)
{
    DEMCR |= (1u << 24);
    DWT_CYCCNT = 0u;
    DWT_CTRL |= 1u;
}

static void pins_bind(void)
{
    s_cs_o   = pin_gpdo(PIN_CS);
    s_sck_o  = pin_gpdo(PIN_SCK);
    s_mosi_o = pin_gpdo(PIN_MOSI);
    s_miso_i = pin_gpdi(PIN_MISO);
    s_mosi_i = pin_gpdi(PIN_MOSI);
    s_sck_i  = pin_gpdi(PIN_SCK);
}

/* 硬件模式引脚: SOUT/SCK 由 LPSPI 驱动, 同时打开 IBE 以便用 GPDI 旁路采样 */
static void pins_hw(void)
{
    pins_bind();
    SIUL2_MSCR(PIN_MOSI)  = MSCR_SSS(3u) | MSCR_OBE | MSCR_SRC | MSCR_IBE;
    SIUL2_MSCR(PIN_CS)    = MSCR_OBE;
    SIUL2_MSCR(PIN_SCK)   = MSCR_SSS(4u) | MSCR_OBE | MSCR_SRC | MSCR_IBE;
    SIUL2_MSCR(PIN_MISO)  = MSCR_IBE;
    SIUL2_IMCR(IMCR_MISO) = 3u;
    *s_cs_o   = 1u;
    *s_sck_o  = 0u;
    *s_mosi_o = 0u;
}

static uint32_t fsr_txcnt(void) { return (uint32_t)(LPSPI0->FSR & 0x7u); }
static uint32_t fsr_rxcnt(void) { return (uint32_t)((LPSPI0->FSR >> 16) & 0x7u); }

static void lpspi_setup(uint32_t half)
{
    uint32_t par;

    LPSPI0->CR = LPSPI_CR_RST_MASK;
    LPSPI0->CR = 0u;
    LPSPI0->CR = LPSPI_CR_MEN_MASK;

    LPSPI0->CFGR0 = 0u;
    LPSPI0->CFGR1 = 0x1u;                              /* MASTER, PINCFG=00, SAMPLE=0 */
    LPSPI0->FCR   = 0u;                                /* TXWATER=0 / RXWATER=0 */
    LPSPI0->CCR   = (4u << 24) | (2u << 16) | (2u << 8);   /* SCKPCS/PCSSCK/DBT */
    LPSPI0->CCR1  = (2u << 24) | (2u << 16) |
                    ((half & 0xFFu) << 8) | (half & 0xFFu); /* SCKSCK/PCSPCS/SCKHLD/SCKSET */

    par = LPSPI0->PARAM;
    s_tx_depth = (par >> 0) & 0xFFu;
    s_rx_depth = (par >> 8) & 0xFFu;
    if (s_tx_depth == 0u) { s_tx_depth = 1u; }
    if (s_rx_depth == 0u) { s_rx_depth = 1u; }
}

static uint32_t tcr_build(uint32_t framesz_bits, int cont)
{
    uint32_t v = (framesz_bits - 1u) & 0xFFFu;         /* PRESCALE=0, PCS=0 */
    if (cont) { v |= LPSPI_TCR_CONT_MASK; }
    return v;
}

static void lpspi_flush(void)
{
    uint32_t guard = 100000u;

    LPSPI0->CR |= LPSPI_CR_RTF_MASK | LPSPI_CR_RRF_MASK;
    while ((LPSPI0->CR & (LPSPI_CR_RTF_MASK | LPSPI_CR_RRF_MASK)) != 0u) {
        if (--guard == 0u) { break; }
    }
    LPSPI0->SR = LPSPI_SR_WCF_MASK | LPSPI_SR_FCF_MASK | LPSPI_SR_TCF_MASK |
                 LPSPI_SR_TEF_MASK | LPSPI_SR_REF_MASK | LPSPI_SR_DMF_MASK;
}

/* 锁步: 每帧都要等 TX 完全排空 + RX 有数, 只依赖 FSR 计数, 不依赖水印 */
static int hw_lockstep(const uint8_t *tx, uint8_t *rx, uint32_t n, int trace)
{
    uint32_t i, guard;
    int rc = 0;

    lpspi_setup(1u);
    lpspi_flush();
    *s_cs_o = 0u;
    LPSPI0->TCR = tcr_build(8u, 1);
    guard = 200000u;
    while (fsr_txcnt() != 0u) { if (--guard == 0u) { break; } }
    if (trace) {
        printf("  after TCR: tcr=%08lx fsr=%08lx sr=%08lx\r\n",
               (unsigned long)LPSPI0->TCR, (unsigned long)LPSPI0->FSR,
               (unsigned long)LPSPI0->SR);
    }

    for (i = 0u; i < n; i++) {
        uint32_t v;
        guard = 200000u;
        while (fsr_txcnt() != 0u) { if (--guard == 0u) { rc = -1; break; } }
        if (rc != 0) { break; }
        LPSPI0->TDR = (tx != NULL) ? (uint32_t)tx[i] : 0xFFu;
        guard = 200000u;
        while (fsr_rxcnt() == 0u) { if (--guard == 0u) { rc = -2; break; } }
        if (rc != 0) { break; }
        v = LPSPI0->RDR;
        if (rx != NULL) { rx[i] = (uint8_t)(v & 0xFFu); }
        if (trace) {
            printf("  [%02lu] tx=%02x rdr=%08lx fsr=%08lx sr=%08lx\r\n",
                   (unsigned long)i, (unsigned long)((tx != NULL) ? tx[i] : 0xFFu),
                   (unsigned long)v, (unsigned long)LPSPI0->FSR,
                   (unsigned long)LPSPI0->SR);
        }
    }

    guard = 200000u;
    while (fsr_txcnt() != 0u) { if (--guard == 0u) { break; } }
    *s_cs_o = 1u;
    if (trace) {
        printf("  end: fsr=%08lx sr=%08lx rsr=%08lx\r\n",
               (unsigned long)LPSPI0->FSR, (unsigned long)LPSPI0->SR,
               (unsigned long)LPSPI0->RSR);
    }
    return rc;
}

/* 突发: 按 FIFO 深度喂 TX, 收 RX; 打印每帧 RDR (32bit 原值) */
static int hw_burst(const uint8_t *tx, uint8_t *rx, uint32_t n, uint32_t half, int trace)
{
    uint32_t fed = 0u, got = 0u, guard;
    uint32_t rdr[68];
    int rc = 0;

    if (n > 68u) { return -9; }
    lpspi_setup(half);
    lpspi_flush();
    *s_cs_o = 0u;
    LPSPI0->TCR = tcr_build(8u, 1);
    guard = 200000u;
    while (fsr_txcnt() != 0u) { if (--guard == 0u) { break; } }

    for (;;) {
        while ((fed < n) && (fsr_txcnt() < s_tx_depth)) {
            LPSPI0->TDR = (tx != NULL) ? (uint32_t)tx[fed] : 0xFFu;
            fed++;
        }
        if (fsr_rxcnt() != 0u) {
            rdr[got] = LPSPI0->RDR;
            if (rx != NULL && got < n) { rx[got] = (uint8_t)(rdr[got] & 0xFFu); }
            got++;
            continue;
        }
        if ((fed >= n) && (fsr_txcnt() == 0u)) {
            if (got >= n) { break; }
            guard = 200000u;
            while (fsr_rxcnt() == 0u) { if (--guard == 0u) { rc = -2; break; } }
            if (rc != 0) { break; }
            continue;
        }
        if (++guard > 0x7FFFFFFFu) { rc = -3; break; }
    }
    *s_cs_o = 1u;

    if (trace) {
        uint32_t k;
        printf("  burst fed=%lu got=%lu fsr=%08lx sr=%08lx\r\n",
               (unsigned long)fed, (unsigned long)got,
               (unsigned long)LPSPI0->FSR, (unsigned long)LPSPI0->SR);
        for (k = 0u; k < got; k++) {
            printf("    rdr[%02lu]=%08lx\r\n", (unsigned long)k, (unsigned long)rdr[k]);
        }
    }
    return rc;
}

/* 波形捕获: 逐 SCK 上升沿采 MOSI/MISO, 返回采到的边沿数 */
static uint32_t hw_wave(const uint8_t *tx, uint32_t n, uint32_t half)
{
    uint32_t fed = 0u, last = 1u;

    lpspi_setup(half);
    lpspi_flush();
    *s_cs_o = 0u;
    LPSPI0->TCR = tcr_build(8u, 1);
    while (fsr_txcnt() != 0u) { }

    while ((fed < n) && (fsr_txcnt() < s_tx_depth)) {
        LPSPI0->TDR = (tx != NULL) ? (uint32_t)tx[fed] : 0xFFu;
        fed++;
    }

    dwt_start();
    s_wav_n = 0u;
    for (;;) {
        uint32_t sck = (uint32_t)(*s_sck_i & 1u);
        if (sck != last) {
            if (sck != 0u) {                     /* 上升沿 = 采样沿 */
                if (s_wav_n < WAV_MAX) {
                    s_wav_mosi[s_wav_n] = (uint8_t)(*s_mosi_i & 1u);
                    s_wav_miso[s_wav_n] = (uint8_t)(*s_miso_i & 1u);
                    s_wav_n++;
                }
                if ((fed < n) && (fsr_txcnt() < s_tx_depth)) {
                    LPSPI0->TDR = (tx != NULL) ? (uint32_t)tx[fed] : 0xFFu;
                    fed++;
                }
            }
            last = sck;
        }
        if (DWT_CYCCNT > 120000000u) { break; }  /* 1s 超时 */
        if ((fed >= n) && (fsr_txcnt() == 0u) && (s_wav_n >= (n * 8u))) { break; }
        if (s_wav_n >= WAV_MAX) { break; }
    }
    s_wav_cycles = DWT_CYCCNT;
    *s_cs_o = 1u;
    return s_wav_n;
}

static void wav_print(const uint8_t *tx, uint32_t n, uint32_t edges)
{
    uint32_t i, k, nb = n;

    printf("  SCK 上升沿 %lu 个 (整包应 %lu 个), 耗时 %lu cyc\r\n",
           (unsigned long)edges, (unsigned long)(n * 8u), (unsigned long)s_wav_cycles);
    if (edges == 0u) {
        printf("  => SCK 完全没有翻转! (Tx 未被启动 / SCK 焊盘未接到 LPSPI0_SCK)\r\n");
        return;
    }

    printf("  MOSI(焊盘实际): ");
    for (k = 0u; k < nb && (k * 8u + 8u) <= edges; k++) {
        uint32_t b, v = 0u;
        for (b = 0u; b < 8u; b++) { v = (v << 1) | s_wav_mosi[k * 8u + b]; }
        printf("%02lx ", (unsigned long)v);
    }
    printf("\r\n  MISO(焊盘实际): ");
    for (k = 0u; k < nb && (k * 8u + 8u) <= edges; k++) {
        uint32_t b, v = 0u;
        for (b = 0u; b < 8u; b++) { v = (v << 1) | s_wav_miso[k * 8u + b]; }
        printf("%02lx ", (unsigned long)v);
    }
    printf("\r\n  TX  (期望发出): ");
    for (i = 0u; i < nb; i++) { printf("%02lx ", (unsigned long)((tx != NULL) ? tx[i] : 0xFFu)); }
    printf("\r\n");
}

void lpspi_diag_regs(void)
{
    uint32_t par = LPSPI0->PARAM;

    printf("VERID=%08lx PARAM=%08lx (TXFIFO=%lu RXFIFO=%lu)\r\n",
           (unsigned long)LPSPI0->VERID, (unsigned long)par,
           (unsigned long)(par & 0xFFu), (unsigned long)((par >> 8) & 0xFFu));
    printf("CR=%08lx SR=%08lx CFGR0=%08lx CFGR1=%08lx\r\n",
           (unsigned long)LPSPI0->CR, (unsigned long)LPSPI0->SR,
           (unsigned long)LPSPI0->CFGR0, (unsigned long)LPSPI0->CFGR1);
    printf("CCR=%08lx CCR1=%08lx FCR=%08lx FSR=%08lx\r\n",
           (unsigned long)LPSPI0->CCR, (unsigned long)LPSPI0->CCR1,
           (unsigned long)LPSPI0->FCR, (unsigned long)LPSPI0->FSR);
    printf("TCR=%08lx RSR=%08lx DER=%08lx\r\n",
           (unsigned long)LPSPI0->TCR, (unsigned long)LPSPI0->RSR,
           (unsigned long)LPSPI0->DER);
    printf("MSCR36(mosi)=%08lx MSCR37(cs)=%08lx MSCR111(sck)=%08lx MSCR112(miso)=%08lx IMCR230=%08lx\r\n",
           (unsigned long)SIUL2_MSCR(PIN_MOSI), (unsigned long)SIUL2_MSCR(PIN_CS),
           (unsigned long)SIUL2_MSCR(PIN_SCK), (unsigned long)SIUL2_MSCR(PIN_MISO),
           (unsigned long)SIUL2_IMCR(IMCR_MISO));
    pins_bind();
    printf("GPDI: cs=%u sck=%u mosi=%u miso=%u\r\n",
           (unsigned)(*s_cs_o & 1u), (unsigned)(*pin_gpdi(PIN_SCK) & 1u),
           (unsigned)(*s_mosi_i & 1u), (unsigned)(*s_miso_i & 1u));
}

static int hex2bin(const char *s, uint8_t *out, int max)
{
    int len, i;
    if (s == NULL) { return -1; }
    len = (int)strlen(s);
    if (((len & 1) != 0) || (len == 0) || ((len / 2) > max)) { return -1; }
    for (i = 0; i < len / 2; i++) {
        unsigned hi = (unsigned)s[2 * i];
        unsigned lo = (unsigned)s[2 * i + 1];
        unsigned a, b;
        a = (hi >= '0' && hi <= '9') ? (hi - '0') : ((hi >= 'a' && hi <= 'f') ? (hi - 'a' + 10u) :
            ((hi >= 'A' && hi <= 'F') ? (hi - 'A' + 10u) : 0xFFu));
        b = (lo >= '0' && lo <= '9') ? (lo - '0') : ((lo >= 'a' && lo <= 'f') ? (lo - 'a' + 10u) :
            ((lo >= 'A' && lo <= 'F') ? (lo - 'A' + 10u) : 0xFFu));
        if (a > 15u || b > 15u) { return -1; }
        out[i] = (uint8_t)((a << 4) | b);
    }
    return len / 2;
}

static void hexdump(const char *tag, const uint8_t *b, int n)
{
    int i;
    printf("%s", tag);
    for (i = 0; i < n; i++) { printf(" %02x", (unsigned)b[i]); }
    printf("\r\n");
}

static const uint8_t k_idver_cmd[12] = {
    0x11, 0x00, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void restore_soft(void)
{
    LPSPI0->CR = 0u;
    spi_soft_init();
}

static int sdiag_cmd(int argc, char *argv[])
{
    static uint8_t tx[72], rx[72];
    int n = 0;

    if (argc < 2) {
        printf("sdiag regs | hw lock|burst|wave <hex> | soft <hex> | idver <n>\r\n");
        return 0;
    }

    if (strcmp(argv[1], "regs") == 0) {
        pins_hw();
        lpspi_diag_regs();
        restore_soft();
        return 0;
    }

    if (strcmp(argv[1], "soft") == 0) {
        memset(rx, 0, sizeof(rx));
        if (argc >= 3) {
            n = hex2bin(argv[2], tx, 68);
            if (n < 0) { printf("bad hex\r\n"); return 0; }
        } else {
            memcpy(tx, k_idver_cmd, 12); n = 12;
        }
        spi_soft_init();
        (void)spi_soft_xfer(tx, rx, (uint32_t)n);
        hexdump("soft tx:", tx, n);
        hexdump("soft rx:", rx, n);
        return 0;
    }

    if (strcmp(argv[1], "hw") == 0) {
        const char *mode = (argc >= 3) ? argv[2] : "lock";
        int rc;

        if (argc >= 4) {
            n = hex2bin(argv[3], tx, 68);
            if (n < 0) { printf("bad hex\r\n"); return 0; }
        } else {
            memcpy(tx, k_idver_cmd, 12); n = 12;
        }
        memset(rx, 0, sizeof(rx));
        pins_hw();
        if (strcmp(mode, "burst") == 0) {
            rc = hw_burst(tx, rx, (uint32_t)n, 1u, 1);
        } else if (strcmp(mode, "wave") == 0) {
            uint32_t edges = hw_wave(tx, (uint32_t)n, 40u);
            wav_print(tx, (uint32_t)n, edges);
            rc = 0;
        } else {
            rc = hw_lockstep(tx, rx, (uint32_t)n, 1);
        }
        printf("  rc=%d\r\n", rc);
        hexdump("  hw  tx:", tx, n);
        hexdump("  hw  rx:", rx, n);
        restore_soft();
        return 0;
    }

    if (strcmp(argv[1], "idver") == 0) {
        uint32_t iters = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 0) : 100u;
        uint32_t i, bad = 0u, to = 0u, t0, t1;
        memcpy(tx, k_idver_cmd, 12);
        pins_hw();
        t0 = SysTick_GetMs();
        for (i = 0u; i < iters; i++) {
            int rc;
            memset(rx, 0, sizeof(rx));
            rc = hw_burst(tx, rx, 12u, 1u, 0);
            if (rc != 0) { to++; }
            if ((rx[2] != 0x11u) || (rx[4] != 0x10u) || (rx[10] != 0x00u) || (rx[11] != 0x11u)) { bad++; }
        }
        t1 = SysTick_GetMs();
        printf("idver: %lu 次, 坏 %lu, 超时 %lu, 耗时 %lums\r\n",
               (unsigned long)iters, (unsigned long)bad, (unsigned long)to,
               (unsigned long)(t1 - t0));
        hexdump("  last rx:", rx, 12);
        restore_soft();
        return 0;
    }

    printf("unknown sub\r\n");
    return 0;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_DISABLE_RETURN | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
                 sdiag, sdiag_cmd, LPSPI 深度诊断: sdiag regs|hw lock|burst|wave <hex>|soft <hex>|idver <n>);
