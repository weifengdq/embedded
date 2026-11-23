/*
 * plb_trgm_diag —— PLB / TRGM / GPTMR 捕获通路分层诊断 (第 15、17 章)
 *
 * 目的: 把 "PLB 输出经 TRGM 到 GPTMR 捕获计数恒为 0" 这件事拆成 5 段可观测的链路,
 *       逐段用寄存器回读判断信号死在哪一段, 而不是靠猜。
 *
 *   段 1  GPTMR0 CH2/CH3 比较输出真的翻转了吗   -> TRGM_IN[0x10/0x11] 电平翻转计数
 *   段 2  PLB TYPE A LUT 输出真的翻转了吗       -> TRGM_IN[0x44](PLB_OUT00) 翻转计数
 *   段 3  TRGM 输出真的翻转了吗                  -> TRGM_OUT[0x38/0x39] 翻转计数
 *   段 4  GPTMR 捕获通道收到边沿了吗             -> CH.CAPPOS / SR.CHxCAPF
 *   段 5  引脚输出 + IOC LOOP_BACK 回读通不通    -> TRGM_IN[0x37](TRGM0_P15) 翻转计数
 *
 * TRGM 的 TRGM_IN[5]/TRGM_OUT[5] 是只读回读寄存器(TRGM 基址 +0x600 / +0x680),
 * 每位对应一个输入/输出信号的电平, 这是本诊断的硬件依据。
 *
 * 串口命令:
 *   dgen                 GPTMR0 CH2(OUT2) + CH3(OUT3) 各输出 1 kHz 方波
 *   dstat                打印寄存器与各观测点翻转计数
 *   ddirect <2|3>        OUT(n) --TRGM--> CAPT_n 直连捕获
 *   dplb <2|3>           OUT(n) --TRGM--> PLB_IN_00 -> LUT0(0xAAAA) -> PLB_OUT00 --TRGM--> CAPT_n
 *   dpin <2|3>           PLB_OUT00 -> TRGM_P15(PF15, LOOP_BACK) -> TRGM_P15 -> CAPT_n
 *   dsw <n> <us>         PLB 软件注入 n 个 us 宽脉冲, 数捕获次数
 *   dquad <ms>           用 OUT2/OUT3 产生 90 度相差 A/B, PLB TYPE B 4 倍频计数
 *   dfilt <len> <mode> [hz]  引脚回环方波过 TRGM 数字滤波器, 数捕获并测周期
 *   dmon <min> <max>     GPTMR 输入监测(measure_width + MONITOR) 阈值试验
 */
#include "bsp.h"
#include "board_timer.h"
#include "hpm_gptmr_drv.h"
#include "hpm_plb_drv.h"
#include "hpm_trgm_drv.h"
#include "hpm_iomux.h"
#include "hpm_interrupt.h"
#include "clock.h"

#define GPTMR        BOARD_TIMER_GPTMR0
#define TRGM         BOARD_TIMER_TRGM
#define PLB          BOARD_TIMER_PLB
#define PLB_CH       plb_chn0

#define SRC_OUT2     HPM_TRGM0_INPUT_SRC_GPTMR0_OUT2
#define SRC_OUT3     HPM_TRGM0_INPUT_SRC_GPTMR0_OUT3
#define SRC_PLB00    HPM_TRGM0_INPUT_SRC_PLB_OUT00
#define SRC_P15      HPM_TRGM0_INPUT_SRC_TRGM0_P15
#define OUT_CAP2     HPM_TRGM0_OUTPUT_SRC_GPTMR0_CAPT_2
#define OUT_CAP3     HPM_TRGM0_OUTPUT_SRC_GPTMR0_CAPT_3
#define OUT_P14      HPM_TRGM0_OUTPUT_SRC_TRGM0_P14
#define OUT_P15      HPM_TRGM0_OUTPUT_SRC_TRGM0_P15
#define TRG_PLB_IN0  TRGM_TRGOCFG_PLB_IN_00
#define TRG_PLB_IN1  TRGM_TRGOCFG_PLB_IN_01
#define TRG_PLB_IN2  TRGM_TRGOCFG_PLB_IN_02
#define TRG_PLB_IN3  TRGM_TRGOCFG_PLB_IN_03

static uint32_t s_clk_hz;
static uint32_t s_tmg_hz;
static volatile uint32_t s_cap_n, s_cap_pos;

static inline uint32_t in_bit(uint8_t idx)
{
    return (TRGM->TRGM_IN[idx >> 5] >> (idx & 31U)) & 1U;
}

static inline uint32_t out_bit(uint8_t idx)
{
    return (TRGM->TRGM_OUT[idx >> 5] >> (idx & 31U)) & 1U;
}

SDK_DECLARE_EXT_ISR_M(BOARD_TIMER_GPTMR0_IRQ, diag_isr)
void diag_isr(void)
{
    uint32_t st = gptmr_get_status(GPTMR);
    for (uint32_t ch = 0U; ch < 4U; ch++) {
        if ((st & GPTMR_CH_CAP_STAT_MASK(ch)) != 0U) {
            gptmr_clear_status(GPTMR, GPTMR_CH_CAP_STAT_MASK(ch));
            s_cap_pos = gptmr_channel_get_counter(GPTMR, ch, gptmr_counter_type_rising_edge);
            s_cap_n++;
        }
    }
}

/* ---------------- 通用配置 ---------------- */

static void route_set(uint8_t out, uint8_t src, trgm_output_type_t type, bool inv)
{
    trgm_output_t c;
    c.invert = inv;
    c.type = type;
    c.input = src;
    trgm_output_config(TRGM, out, &c);
}

static void out_square(uint8_t ch, uint32_t reload, uint32_t cmp, bool init_high, uint32_t start)
{
    gptmr_channel_config_t cfg;
    gptmr_channel_get_default_config(GPTMR, &cfg);
    cfg.mode = gptmr_work_mode_no_capture;
    cfg.reload = reload;
    cfg.cmp[0] = cmp;
    cfg.cmp[1] = 0xFFFFFFFFUL;
    cfg.enable_cmp_output = true;      /* CMPEN: 比较输出才送给 TRGM/引脚 */
    cfg.cmp_initial_polarity_high = init_high;
    (void) gptmr_channel_config(GPTMR, ch, &cfg, false);
    gptmr_channel_enable(GPTMR, ch, true);
    gptmr_channel_reset_count(GPTMR, ch);
    if (start != 0U) {
        gptmr_channel_update_count(GPTMR, ch, start);
    }
    gptmr_start_counter(GPTMR, ch);
}

static void cap_init(uint8_t ch)
{
    gptmr_channel_config_t cfg;
    gptmr_disable_irq(GPTMR, 0xFFFFU);
    gptmr_channel_get_default_config(GPTMR, &cfg);
    cfg.mode = gptmr_work_mode_capture_at_rising_edge;
    cfg.reload = 0xFFFFFFFFUL;
    cfg.cmp[0] = 0xFFFFFFFFUL;
    cfg.cmp[1] = 0xFFFFFFFFUL;
    cfg.enable_cmp_output = false;
    (void) gptmr_channel_config(GPTMR, ch, &cfg, false);
    gptmr_channel_reset_count(GPTMR, ch);
    gptmr_start_counter(GPTMR, ch);
    s_cap_n = 0U;
    s_cap_pos = 0U;
    gptmr_enable_irq(GPTMR, GPTMR_CH_CAP_IRQ_MASK(ch));
    intc_m_enable_irq_with_priority(BOARD_TIMER_GPTMR0_IRQ, 1);
}

static void plb_passthrough(void)
{
    plb_type_a_truth_t lut;
    lut.val = 0xAAAA;                  /* out = in0 */
    plb_type_a_set_lut(PLB, PLB_CH, plb_type_a_table0, &lut);
    plb_type_a_inject_by_sw(PLB, PLB_CH, 0);
}

/* 采样 ms 毫秒, 统计某输入位的翻转次数 */
static uint32_t toggle_in(uint8_t idx, uint32_t ms)
{
    uint32_t last = in_bit(idx), n = 0U;
    uint32_t t0 = bsp_uptime_ms();
    while ((bsp_uptime_ms() - t0) < ms) {
        uint32_t v = in_bit(idx);
        if (v != last) { n++; last = v; }
        for (volatile uint32_t i = 0U; i < 20U; i++) { }
    }
    return n;
}

static uint32_t toggle_out(uint8_t idx, uint32_t ms)
{
    uint32_t last = out_bit(idx), n = 0U;
    uint32_t t0 = bsp_uptime_ms();
    while ((bsp_uptime_ms() - t0) < ms) {
        uint32_t v = out_bit(idx);
        if (v != last) { n++; last = v; }
        for (volatile uint32_t i = 0U; i < 20U; i++) { }
    }
    return n;
}

/* ---------------- 命令 ---------------- */

static void cmd_dgen(int argc, char *argv[])
{
    (void) argc; (void) argv;
    out_square(2U, s_clk_hz / 1000U, s_clk_hz / 2000U, false, 0U);
    out_square(3U, s_clk_hz / 1000U, s_clk_hz / 2000U, false, s_clk_hz / 4000U);
    printf("OUT2 = 1 kHz 50%%; OUT3 = 1 kHz 50%% 相移 90 度\r\n");
}

static void cmd_dstat(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("--- TRGM 配置回读 ---\r\n");
    printf("TRGOCFG: PLB_IN_00=0x%08lx(源0x%02lx) PLB_IN_01=0x%08lx PLB_IN_02=0x%08lx PLB_IN_03=0x%08lx\r\n",
           (unsigned long) TRGM->TRGOCFG[TRG_PLB_IN0],
           (unsigned long) (TRGM->TRGOCFG[TRG_PLB_IN0] & 0xFFU),
           (unsigned long) TRGM->TRGOCFG[TRG_PLB_IN1],
           (unsigned long) TRGM->TRGOCFG[TRG_PLB_IN2],
           (unsigned long) TRGM->TRGOCFG[TRG_PLB_IN3]);
    printf("TRGOCFG: CAPT_2=0x%08lx(源0x%02lx) CAPT_3=0x%08lx(源0x%02lx) P15=0x%08lx GCR=0x%08lx\r\n",
           (unsigned long) TRGM->TRGOCFG[OUT_CAP2],
           (unsigned long) (TRGM->TRGOCFG[OUT_CAP2] & 0xFFU),
           (unsigned long) TRGM->TRGOCFG[OUT_CAP3],
           (unsigned long) (TRGM->TRGOCFG[OUT_CAP3] & 0xFFU),
           (unsigned long) TRGM->TRGOCFG[OUT_P15],
           (unsigned long) TRGM->GCR);
    printf("--- GPTMR0 通道 ---\r\n");
    for (uint32_t ch = 0U; ch < 4U; ch++) {
        printf("CH%lu CR=0x%08lx CNT=%lu RLD=%lu CMP0=%lu CMP1=%lu\r\n",
               (unsigned long) ch,
               (unsigned long) GPTMR->CHANNEL[ch].CR,
               (unsigned long) GPTMR->CHANNEL[ch].CNT,
               (unsigned long) GPTMR->CHANNEL[ch].RLD,
               (unsigned long) GPTMR->CHANNEL[ch].CMP[0],
               (unsigned long) GPTMR->CHANNEL[ch].CMP[1]);
    }
    printf("CH3 CAP: CAPPOS=%lu CAPNEG=%lu CAPPRD=%lu CAPDTY=%lu\r\n",
           (unsigned long) GPTMR->CHANNEL[3].CAPPOS,
           (unsigned long) GPTMR->CHANNEL[3].CAPNEG,
           (unsigned long) GPTMR->CHANNEL[3].CAPPRD,
           (unsigned long) GPTMR->CHANNEL[3].CAPDTY);
    printf("CH2 CAP: CAPPOS=%lu CAPNEG=%lu CAPPRD=%lu CAPDTY=%lu\r\n",
           (unsigned long) GPTMR->CHANNEL[2].CAPPOS,
           (unsigned long) GPTMR->CHANNEL[2].CAPNEG,
           (unsigned long) GPTMR->CHANNEL[2].CAPPRD,
           (unsigned long) GPTMR->CHANNEL[2].CAPDTY);
    printf("SR=0x%08lx IRQEN=0x%08lx\r\n",
           (unsigned long) GPTMR->SR, (unsigned long) GPTMR->IRQEN);
    printf("--- PLB ---\r\n");
    printf("TYPE_A0: LUT0=0x%04lx LUT1=0x%04lx SW_INJECT=0x%08lx\r\n",
           (unsigned long) (PLB->TYPE_A[0].LOOKUP_TABLE[0] & 0xFFFFU),
           (unsigned long) (PLB->TYPE_A[0].LOOKUP_TABLE[1] & 0xFFFFU),
           (unsigned long) PLB->TYPE_A[0].SW_INJECT);
    printf("TYPE_B0: MODE=0x%08lx LUT0=0x%08lx LUT1=0x%08lx CMP0=%lu CMP1=%lu CNT=%lu\r\n",
           (unsigned long) PLB->TYPE_B[0].MODE,
           (unsigned long) PLB->TYPE_B[0].LUT[0],
           (unsigned long) PLB->TYPE_B[0].LUT[1],
           (unsigned long) PLB->TYPE_B[0].CMP[0],
           (unsigned long) PLB->TYPE_B[0].CMP[1],
           (unsigned long) plb_type_b_get_counter(PLB, PLB_CH));
    printf("--- 20 ms 内电平翻转次数(期望 1 kHz 方波 -> 约 40) ---\r\n");
    printf("IN[0x10]GPTMR0_OUT2=%lu IN[0x11]GPTMR0_OUT3=%lu IN[0x44]PLB_OUT00=%lu IN[0x37]TRGM0_P15=%lu\r\n",
           (unsigned long) toggle_in(HPM_TRGM0_INPUT_SRC_GPTMR0_OUT2, 20U),
           (unsigned long) toggle_in(HPM_TRGM0_INPUT_SRC_GPTMR0_OUT3, 20U),
           (unsigned long) toggle_in(SRC_PLB00, 20U),
           (unsigned long) toggle_in(SRC_P15, 20U));
    printf("OUT[0x38]CAPT_2=%lu OUT[0x39]CAPT_3=%lu OUT[0x0F]P15=%lu OUT[0x0E]P14=%lu\r\n",
           (unsigned long) toggle_out(OUT_CAP2, 20U),
           (unsigned long) toggle_out(OUT_CAP3, 20U),
           (unsigned long) toggle_out(OUT_P15, 20U),
           (unsigned long) toggle_out(OUT_P14, 20U));
    printf("cap_n=%lu cap_pos=%lu  PF15.FUNC_CTL=0x%08lx\r\n",
           (unsigned long) s_cap_n, (unsigned long) s_cap_pos,
           (unsigned long) HPM_IOC->PAD[IOC_PAD_PF15].FUNC_CTL);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dstat, cmd_dstat, dump trgm/plb/gptmr registers and toggle counts);

static void report_caps(const char *tag, uint32_t wait_ms)
{
    uint32_t n0 = s_cap_n;
    (void) n0;
    s_cap_n = 0U;
    board_delay_ms(wait_ms);
    printf("[%s] %lu ms 内捕获 %lu 次, 最后一次 CAPPOS=%lu\r\n",
           tag, (unsigned long) wait_ms,
           (unsigned long) s_cap_n, (unsigned long) s_cap_pos);
}

static void cmd_ddirect(int argc, char *argv[])
{
    uint32_t cap = (argc > 1) ? (uint32_t) strtoul(argv[1], NULL, 0) : 3U;
    uint8_t out_ch = (cap == 2U) ? 3U : 2U;
    uint8_t cap_out = (cap == 2U) ? OUT_CAP2 : OUT_CAP3;
    uint8_t src = (out_ch == 2U) ? SRC_OUT2 : SRC_OUT3;

    out_square(out_ch, s_clk_hz / 1000U, s_clk_hz / 2000U, false, 0U);
    route_set(cap_out, src, trgm_output_same_as_input, false);
    cap_init((uint8_t) cap);
    report_caps("ddirect 直连", 300U);
    printf("       IN[src]=%lu OUT[cap]=%lu\r\n",
           (unsigned long) toggle_in(src, 10U), (unsigned long) toggle_out(cap_out, 10U));
}

static void cmd_dplb(int argc, char *argv[])
{
    uint32_t cap = (argc > 1) ? (uint32_t) strtoul(argv[1], NULL, 0) : 3U;
    uint8_t out_ch = (cap == 2U) ? 3U : 2U;
    uint8_t cap_out = (cap == 2U) ? OUT_CAP2 : OUT_CAP3;
    uint8_t src = (out_ch == 2U) ? SRC_OUT2 : SRC_OUT3;

    out_square(out_ch, s_clk_hz / 1000U, s_clk_hz / 2000U, false, 0U);
    plb_passthrough();
    route_set(TRG_PLB_IN0, src, trgm_output_same_as_input, false);
    route_set(cap_out, SRC_PLB00, trgm_output_same_as_input, false);
    cap_init((uint8_t) cap);
    report_caps("dplb 经 PLB", 300U);
    printf("       IN[src]=%lu IN[PLB_OUT00]=%lu OUT[cap]=%lu\r\n",
           (unsigned long) toggle_in(src, 10U),
           (unsigned long) toggle_in(SRC_PLB00, 10U),
           (unsigned long) toggle_out(cap_out, 10U));
}

static void dpin_one(uint32_t cap, bool use_pd15)
{
    uint8_t cap_out = (cap == 2U) ? OUT_CAP2 : OUT_CAP3;

    out_square(3U, s_clk_hz / 1000U, s_clk_hz / 2000U, false, 0U);
    plb_passthrough();
    route_set(TRG_PLB_IN0, SRC_OUT3, trgm_output_same_as_input, false);
    /* PLB_OUT00 -> TRGM_P15 引脚输出 */
    route_set(OUT_P15, SRC_PLB00, trgm_output_same_as_input, false);
    trgm_enable_io_output(TRGM, 1UL << 15);
    if (use_pd15) {
        HPM_IOC->PAD[IOC_PAD_PF15].FUNC_CTL = 0UL;   /* 先松掉 PF15, 避免两个 pad 抢同一 TRGM 信号 */
        HPM_IOC->PAD[IOC_PAD_PD15].FUNC_CTL =
            IOC_PD15_FUNC_CTL_TRGM_P_15 | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
    } else {
        HPM_IOC->PAD[IOC_PAD_PD15].FUNC_CTL = 0UL;
        HPM_IOC->PAD[IOC_PAD_PF15].FUNC_CTL =
            IOC_PF15_FUNC_CTL_TRGM_P_15 | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
    }
    route_set(cap_out, SRC_P15, trgm_output_same_as_input, false);
    cap_init((uint8_t) cap);
    report_caps(use_pd15 ? "dpin PD15 回环" : "dpin PF15 回环", 300U);
    printf("       pad=%s: IN[PLB_OUT00]=%lu OUT[P15]=%lu IN[TRGM_P15]=%lu\r\n",
           use_pd15 ? "PD15" : "PF15",
           (unsigned long) toggle_in(SRC_PLB00, 10U),
           (unsigned long) toggle_out(OUT_P15, 10U),
           (unsigned long) toggle_in(SRC_P15, 10U));
}

static void cmd_dpin(int argc, char *argv[])
{
    uint32_t cap = (argc > 1) ? (uint32_t) strtoul(argv[1], NULL, 0) : 3U;

    dpin_one(cap, false);
    dpin_one(cap, true);
}
static void cmd_dsw(int argc, char *argv[])
{
    uint32_t n = (argc > 1) ? (uint32_t) strtoul(argv[1], NULL, 0) : 10U;
    uint32_t us = (argc > 2) ? (uint32_t) strtoul(argv[2], NULL, 0) : 100U;

    plb_passthrough();
    route_set(TRG_PLB_IN0, SRC_OUT2, trgm_output_same_as_input, false);
    route_set(OUT_CAP3, SRC_PLB00, trgm_output_same_as_input, false);
    cap_init(3U);
    board_delay_ms(2U);
    s_cap_n = 0U;
    for (uint32_t i = 0U; i < n; i++) {
        plb_type_a_inject_by_sw(PLB, PLB_CH, 1U);
        board_delay_us(us);
        plb_type_a_inject_by_sw(PLB, PLB_CH, 0U);
        board_delay_us(us);
    }
    board_delay_ms(2U);
    printf("[dsw] 软件注入 %lu 个脉冲(宽 %lu us): 捕获 %lu 次, 末次 CAPPOS=%lu\r\n",
           (unsigned long) n, (unsigned long) us,
           (unsigned long) s_cap_n, (unsigned long) s_cap_pos);
    printf("      IN[PLB_OUT00]=%lu\r\n", (unsigned long) toggle_in(SRC_PLB00, 10U));
}

static void quad_config(void)
{
    uint32_t t = s_clk_hz / 1000U;
    plb_type_a_truth_t lut;

    /* A = OUT2, B = OUT3(计数器预装 T/4, 即 90 度) */
    out_square(2U, t, t / 2U, false, 0U);
    out_square(3U, t, t / 2U, false, t / 4U);

    route_set(TRG_PLB_IN0, SRC_OUT2, trgm_output_pulse_at_input_both_edge, false);
    route_set(TRG_PLB_IN1, SRC_OUT3, trgm_output_pulse_at_input_both_edge, false);
    route_set(TRG_PLB_IN2, SRC_OUT2, trgm_output_same_as_input, false);
    route_set(TRG_PLB_IN3, SRC_OUT3, trgm_output_same_as_input, false);

    /* 官方 hpm_qei_encoder 的 AB 模式 LUT 与数据单元配置 */
    lut.val = 0x2442;
    plb_type_a_set_lut(PLB, PLB_CH, plb_type_a_table0, &lut);
    lut.val = 0x4224;
    plb_type_a_set_lut(PLB, PLB_CH, plb_type_a_table1, &lut);
    plb_type_b_set_all_slice(PLB, PLB_CH, plb_slice_opt_keep);
    plb_type_b_set_lut(PLB, PLB_CH, plb_type_b_slice_1, plb_slice_opt_add_one);
    plb_type_b_set_lut(PLB, PLB_CH, plb_type_b_slice_2, plb_slice_opt_sub_one);
    board_delay_ms(1U);
    plb_type_a_inject_by_sw(PLB, PLB_CH, 0U);
    plb_type_b_inject_by_sw(PLB, PLB_CH, 0U);
    plb_type_a_inject_by_sw(PLB, PLB_CH, 0U);
    plb_type_b_inject_by_sw(PLB, PLB_CH, 0U);
}

static void cmd_dquad(int argc, char *argv[])
{
    uint32_t ms = (argc > 1) ? (uint32_t) strtoul(argv[1], NULL, 0) : 200U;
    int32_t c0, c1;

    quad_config();
    board_delay_ms(2U);
    c0 = (int32_t) plb_type_b_get_counter(PLB, PLB_CH);
    board_delay_ms(ms);
    c1 = (int32_t) plb_type_b_get_counter(PLB, PLB_CH);
    printf("[dquad] %lu ms: TYPE_B 计数 %ld -> %ld, 差 %ld; 期望 4 x 1000 x %lu ms = %ld\r\n",
           (unsigned long) ms, (long) c0, (long) c1, (long) (c1 - c0),
           (unsigned long) ms, (long) (4 * 1000 * (int32_t) ms / 1000));
    printf("        IN[OUT2]=%lu IN[OUT3]=%lu IN[PLB_OUT00]=%lu IN[PLB_OUT01]=%lu\r\n",
           (unsigned long) toggle_in(SRC_OUT2, 10U),
           (unsigned long) toggle_in(SRC_OUT3, 10U),
           (unsigned long) toggle_in(SRC_PLB00, 10U),
           (unsigned long) toggle_in(HPM_TRGM0_INPUT_SRC_PLB_OUT01, 10U));
}

static void loopback_pin(uint32_t hz)
{
    uint32_t t = s_clk_hz / hz;
    /* 源用 CH3: 捕获用 CH2, 同一通道不能既做输出又做捕获 */
    out_square(3U, t, t / 2U, false, 0U);
    /* OUT3 -> TRGM_P15 引脚输出(同相方波), IOC 回环 -> TRGM_P15 输入 */
    route_set(OUT_P15, SRC_OUT3, trgm_output_same_as_input, false);
    trgm_enable_io_output(TRGM, 1UL << 15);
    HPM_IOC->PAD[IOC_PAD_PF15].FUNC_CTL =
        IOC_PF15_FUNC_CTL_TRGM_P_15 | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
    route_set(OUT_CAP2, SRC_P15, trgm_output_same_as_input, false);
}

static void cmd_dfilt(int argc, char *argv[])
{
    uint32_t len = (argc > 1) ? (uint32_t) strtoul(argv[1], NULL, 0) : 0U;
    uint32_t mode = (argc > 2) ? (uint32_t) strtoul(argv[2], NULL, 0) : 4U;
    uint32_t hz = (argc > 3) ? (uint32_t) strtoul(argv[3], NULL, 0) : 100000U;
    trgm_filter_t f;

    loopback_pin(hz);
    cap_init(2U);
    f.sync = true;
    f.invert = false;
    f.mode = (trgm_filter_mode_t) mode;
    f.filter_length = len;
    trgm_filter_config(TRGM, HPM_TRGM0_FILTER_SRC_TRGM0_P15, &f);
    board_delay_ms(3U);
    s_cap_n = 0U;
    board_delay_ms(200U);
    printf("[dfilt] len=%lu mode=%lu 源=%lu Hz: 捕获 %lu 次(期望约 %lu), CAPPOS=%lu\r\n",
           (unsigned long) len, (unsigned long) mode, (unsigned long) hz,
           (unsigned long) s_cap_n, (unsigned long) (hz * 200U / 1000U),
           (unsigned long) s_cap_pos);
    printf("        IN[TRGM_P15]=%lu OUT[P15]=%lu\r\n",
           (unsigned long) toggle_in(SRC_P15, 10U), (unsigned long) toggle_out(OUT_P15, 10U));
}

static void cmd_dmon(int argc, char *argv[])
{
    uint32_t minv = (argc > 1) ? (uint32_t) strtoul(argv[1], NULL, 0) : 50000U;
    uint32_t maxv = (argc > 2) ? (uint32_t) strtoul(argv[2], NULL, 0) : 200000U;
    gptmr_channel_monitor_config_t mc;
    uint32_t n0;

    loopback_pin(1000U);
    /* measure_width + 监测: CMP0=min, CMP1=max, 单位按 gptmr 时钟周期 */
    gptmr_channel_monitor_config(GPTMR, 2U, &mc, false);
    mc.monitor_type = monitor_signal_period;
    mc.min_value = minv;
    mc.max_value = maxv;
    (void) gptmr_channel_monitor_config(GPTMR, 2U, &mc, true);
    gptmr_clear_status(GPTMR, 0xFFFFU);
    s_cap_n = 0U;
    board_delay_ms(200U);
    n0 = s_cap_n;
    printf("[dmon] min=%lu max=%lu (tick,1 tick=%.1f ns): 200 ms 捕获/监测中断 %lu 次, 期望 200(1 kHz)\r\n",
           (unsigned long) minv, (unsigned long) maxv,
           1e9 / (double) s_clk_hz, (unsigned long) n0);
    printf("       CAPPRD=%lu CAPDTY=%lu CR=0x%08lx SR=0x%08lx CMP0=%lu CMP1=%lu\r\n",
           (unsigned long) GPTMR->CHANNEL[2].CAPPRD,
           (unsigned long) GPTMR->CHANNEL[2].CAPDTY,
           (unsigned long) GPTMR->CHANNEL[2].CR,
           (unsigned long) GPTMR->SR,
           (unsigned long) GPTMR->CHANNEL[2].CMP[0],
           (unsigned long) GPTMR->CHANNEL[2].CMP[1]);
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dgen, cmd_dgen, gptmr0 ch2/ch3 1kHz square output);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ddirect, cmd_ddirect, direct gptmr out -> trgm -> capture <capch>);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dplb, cmd_dplb, gptmr out -> plb lut -> capture <capch>);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dpin, cmd_dpin, plb -> trgm pin -> loopback -> capture <capch>);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dsw, cmd_dsw, inject n pulses by plb sw and count captures);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dquad, cmd_dquad, plb type b quadrature decode test <ms>);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dfilt, cmd_dfilt, trgm filter sweep <len> <mode> [hz]);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dmon, cmd_dmon, gptmr monitor threshold test <min> <max>);

static void cmd_dall(int argc, char *argv[])
{
    static const uint32_t lens[] = {0U, 16U, 64U, 128U, 256U, 511U};
    (void) argc; (void) argv;
    printf("\r\n########## dall 开始 ##########\r\n");
    cmd_dgen(0, NULL);
    board_delay_ms(2U);
    cmd_dstat(0, NULL);
    cmd_ddirect(0, NULL);
    cmd_dplb(0, NULL);
    cmd_dpin(0, NULL);
    cmd_dsw(0, NULL);
    cmd_dquad(0, NULL);
    for (uint32_t i = 0U; i < sizeof(lens) / sizeof(lens[0]); i++) {
        char a1[8], a2[8];
        char *av[4];
        snprintf(a1, sizeof(a1), "%lu", (unsigned long) lens[i]);
        snprintf(a2, sizeof(a2), "%u", 4U);
        av[0] = (char *) "dfilt"; av[1] = a1; av[2] = a2; av[3] = (char *) "100000";
        cmd_dfilt(4, av);
    }
    {
        char *av[4] = {(char *) "dfilt", (char *) "64", (char *) "5", (char *) "100000"};
        cmd_dfilt(4, av);
    }
    cmd_dmon(0, NULL);
    {
        char *av[4] = {(char *) "dmon", (char *) "500000", (char *) "200000"};
        cmd_dmon(3, av);
    }
    cmd_dstat(0, NULL);
    printf("########## dall 结束 ##########\r\n");
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dall, cmd_dall, run the whole diagnostic battery);

int main(void)
{
    bsp_init();
    board_init_led_pins();
    init_gptmr0_clock();
    clock_add_to_group(BOARD_TIMER_PLB_CLK, 0);
    s_clk_hz = clock_get_frequency(BOARD_TIMER_GPTMR0_CLK);
    s_tmg_hz = s_clk_hz;
    printf("\r\n=== plb_trgm_diag (board %s) ===\r\n", BOARD_NAME);
    printf("GPTMR0 clk=%lu Hz (1 tick=%.1f ns), TRGM_IN/OUT 回读基址 0x%08lx/0x%08lx\r\n",
           (unsigned long) s_clk_hz, 1e9 / (double) s_clk_hz,
           (unsigned long) (uint32_t) (uintptr_t) &TRGM->TRGM_IN[0],
           (unsigned long) (uint32_t) (uintptr_t) &TRGM->TRGM_OUT[0]);
    plb_passthrough();
    cmd_dgen(0, NULL);
    cap_init(3U);
    printf("commands: dgen dstat ddirect dplb dpin dsw dquad dfilt dmon\r\n");
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
