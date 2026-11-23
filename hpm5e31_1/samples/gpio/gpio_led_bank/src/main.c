/*
 * gpio_led_bank - 8 颗 CAN 状态灯: 跑马灯 / 任意图案 / 软件 PWM 呼吸 / 通道活动模拟
 *
 * 硬件: hpm5e31_1, 8 颗高亮 LED 挂在 GPIOF:
 *   idx 0..7 = CAN0.RED(PF26) CAN0.WHITE(PF18) CAN1.RED(PF20) CAN1.WHITE(PF16)
 *              CAN2.RED(PF22) CAN2.WHITE(PF21) CAN3.RED(PF23) CAN3.WHITE(PF19)
 * 目的: 演示"端口整字写"外设位图(GPIOF 的 DO 一次写 8 位), 以及不用 PWM 外设、
 *       纯软件定时做出的 8 路呼吸灯(并说明它的 CPU 代价).
 * 命令: ledbank_info / ledbank_mask <hex> / ledbank_marquee [ms] [steps] /
 *       ledbank_breath [cycles] / ledbank_seq [ms] / ledbank_off
 */
#include "bsp.h"
#include "hpm_gpio_drv.h"
#include "hpm_mchtmr_drv.h"

#define BANK_CTRL HPM_GPIO0
#define BANK_PORT GPIO_DO_GPIOF
#define BANK_ALL  0xFFU
#define PWM_STEPS 50U      /* 一个 PWM 周期的软件划分数 */
#define PWM_PERIOD_US 2000U

/* PIN 顺序: CAN0.RED, CAN0.WHITE, CAN1.RED, CAN1.WHITE, CAN2.RED, CAN2.WHITE, CAN3.RED, CAN3.WHITE */
static const uint8_t s_pin[8] = { 26U, 18U, 20U, 16U, 22U, 21U, 23U, 19U };
static const char *s_name[8] = { "CAN0.RED  ", "CAN0.WHITE", "CAN1.RED  ", "CAN1.WHITE",
                                 "CAN2.RED  ", "CAN2.WHITE", "CAN3.RED  ", "CAN3.WHITE" };
static uint32_t s_mask;

static uint32_t s_ms_div = 24000U;   /* mchtmr 24 MHz -> counts per ms */

static inline uint32_t now_ms(void)
{
    return (uint32_t) (mchtmr_get_count(HPM_MCHTMR) / s_ms_div);
}

/* 一次性写整个端口(8 位掩码 -> 对应 PF 引脚), 演示端口级位图写 */
static void bank_write_mask(uint32_t mask)
{
    s_mask = mask & BANK_ALL;
    BANK_CTRL->DO[BANK_PORT].VALUE = 0U;
    for (uint32_t i = 0; i < 8U; i++) {
        if (s_mask & (1UL << i)) {
            BANK_CTRL->DO[BANK_PORT].SET = 1UL << s_pin[i];
        }
    }
}

static void bank_off(void)
{
    for (uint32_t i = 0; i < 8U; i++) {
        gpio_write_pin(BANK_CTRL, BANK_PORT, s_pin[i], 0U);
    }
    s_mask = 0U;
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("gpio_led_bank @ hpm5e31_1: 8x CAN status LED on GPIOF\r\n");
    for (uint32_t i = 0; i < 8U; i++) {
        printf("  [%lu] %s PF%02u\r\n", (unsigned long) i, s_name[i], (unsigned) s_pin[i]);
    }
    printf("  current mask=0x%02lX, DO[F]=0x%08lX, DI[F]=0x%08lX\r\n",
           (unsigned long) s_mask,
           (unsigned long) BANK_CTRL->DO[BANK_PORT].VALUE,
           (unsigned long) BANK_CTRL->DI[BANK_PORT].VALUE);
    printf("  usage: ledbank_mask <hex> | ledbank_marquee [ms] [steps] | ledbank_breath [cycles] | ledbank_seq [ms]\r\n");
}

static void cmd_mask(int argc, char *argv[])
{
    uint32_t m = 0x00U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &m); }
    bank_write_mask(m);
    printf("mask=0x%02lX, DO[F]=0x%08lX\r\n", (unsigned long) s_mask,
           (unsigned long) BANK_CTRL->DO[BANK_PORT].VALUE);
}

/* 跑马灯: 每次只亮一颗, 按索引移位 */
static void cmd_marquee(int argc, char *argv[])
{
    uint32_t ms = 120U, steps = 16U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &ms); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &steps); }
    if (ms < 10U) { ms = 10U; }
    if (steps > 64U) { steps = 64U; }
    printf("marquee: period=%lu ms, steps=%lu\r\n", (unsigned long) ms, (unsigned long) steps);
    for (uint32_t k = 0; k < steps; k++) {
        uint32_t idx = k % 8U;
        bank_write_mask(1UL << idx);
        printf("  t=%lu ms step=%lu -> %s (PF%02u) mask=0x%02lX\r\n",
               (unsigned long) now_ms(), (unsigned long) steps,
               s_name[idx], (unsigned) s_pin[idx], (unsigned long) s_mask);
        board_delay_ms(ms);
    }
    bank_off();
}

/* 软件 PWM 呼吸: 一个 2 ms 周期切成 50 份(每份 40 us), 按占空比决定亮/灭 */
static void cmd_breath(int argc, char *argv[])
{
    uint32_t cycles = 1U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &cycles); }
    if (cycles > 10U) { cycles = 10U; }
    printf("breath: software PWM, %lu steps / %lu us period, %lu cycles\r\n",
           (unsigned long) PWM_STEPS, (unsigned long) PWM_PERIOD_US, (unsigned long) cycles);
    for (uint32_t c = 0; c < cycles; c++) {
        for (uint32_t ph = 0; ph < (2U * PWM_STEPS); ph++) {
            uint32_t duty_step = (ph < PWM_STEPS) ? ph : (2U * PWM_STEPS - ph);
            for (uint32_t step = 0; step < PWM_STEPS; step++) {
                bank_write_mask((step < duty_step) ? BANK_ALL : 0U);
                board_delay_us(PWM_PERIOD_US / PWM_STEPS);
            }
            if ((ph % 10U) == 0U) {
                printf("  duty=%lu%% (32 位端口写)\r\n",
                       (unsigned long) (duty_step * 100U / PWM_STEPS));
            }
        }
    }
    bank_off();
}

/* 模拟 4 路 CAN 流量指示: 每路 RED/WHITE 交替闪烁 */
static void cmd_seq(int argc, char *argv[])
{
    uint32_t ms = 150U, rounds = 4U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &ms); }
    if (rounds > 16U) { rounds = 16U; }
    printf("seq: simulate 4 CAN channel activity, %lu rounds x %lu ms\r\n",
           (unsigned long) rounds, (unsigned long) ms);
    for (uint32_t r = 0; r < rounds; r++) {
        /* 每路: 先 RED 再 WHITE, 依次点亮 */
        for (uint32_t ch = 0; ch < 4U; ch++) {
            uint32_t red = ch * 2U;
            uint32_t white = red + 1U;
            bank_write_mask((1UL << red) | (1UL << white));
            printf("  round=%lu CAN%lu TX/RX (mask=0x%02lX)\r\n",
                   (unsigned long) r, (unsigned long) ch, (unsigned long) s_mask);
            board_delay_ms(ms);
            bank_write_mask(1UL << red);
            board_delay_ms(ms);
            bank_write_mask(0U);
            board_delay_ms(ms);
        }
    }
    bank_off();
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ledbank_info, cmd_info, led bank: pin map / current mask);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ledbank_mask, cmd_mask, led bank: ledbank_mask <hex> set 8-bit pattern);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ledbank_marquee, cmd_marquee, led bank: ledbank_marquee [ms] [steps]);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ledbank_breath, cmd_breath, led bank: ledbank_breath [cycles] software PWM);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ledbank_seq, cmd_seq, led bank: ledbank_seq [ms] simulate CAN activity);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ledbank_off, cmd_mask, led bank: ledbank_off (same as ledbank_mask 0));

int main(void)
{
    bsp_init();
    s_ms_div = clock_get_frequency(clock_mchtmr0) / 1000U;
    if (s_ms_div == 0U) { s_ms_div = 24000U; }
    init_canled_pins();
    for (uint32_t i = 0; i < 8U; i++) {
        gpio_set_pin_output(BANK_CTRL, BANK_PORT, s_pin[i]);
    }
    bank_off();

    printf("\r\n=== gpio_led_bank (8x CAN status LED on GPIOF) ===\r\n");
    cmd_info(0, NULL);
    /* 上电自证: 依次点亮 8 颗灯并同步打印, 证明位图写正确 */
    printf("self-test: walking ones 0x01..0x80\r\n");
    for (uint32_t i = 0; i < 8U; i++) {
        bank_write_mask(1UL << i);
        printf("  bit%lu -> %s DO[F]=0x%08lX\r\n", (unsigned long) i, s_name[i],
               (unsigned long) BANK_CTRL->DO[BANK_PORT].VALUE);
        board_delay_ms(120);
    }
    bank_off();
    printf("type 'ledbank_marquee 120 16' or 'ledbank_breath 1'\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
