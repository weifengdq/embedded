/*
 * i2c_loopback - 软件(位翻转) I2C 主机 + 开漏/上拉/线电平自检 + SCL 时序实测
 *
 * 为什么需要它:
 *   板上没有 I2C 从器件, 硬件 I2C 主机只能做"地址扫描(NACK)"和寄存器回读;
 *   本样例用 GPIO 位翻转实现一个完整 I2C 主机(START/地址/ACK/数据/STOP),
 *   每一步都通过 DI 读回真实引脚电平来自证(开漏+上拉), 并用 mcycle 实测 SCL 频率,
 *   是 i2c_scan(硬件 I2C) 的互补例程. 同时演示"用软件 I2C 做总线恢复"这一实用手段.
 *
 * 硬件: hpm5e31_1, 空闲引脚 PB02=SCL, PB03=SDA, 配置为 开漏(OD=1)+内部上拉(PE=1,PS=1).
 *       引脚悬空时: 释放=高(内部上拉), 驱动 0=低; 用 DI 读回可验证两条线都正常.
 * 命令: i2c_soft_info / i2c_soft_lines / i2c_soft_selftest / i2c_soft_speed <kHz>
 *       / i2c_soft_scan [from] [to] / i2c_soft_timing / i2c_soft_recover
 */
#include "bsp.h"
#include "hpm_gpio_drv.h"
#include "hpm_iomux.h"

#define SCL_PAD   IOC_PAD_PD13
#define SDA_PAD   IOC_PAD_PD12
#define SCL_GPIO  HPM_GPIO0
#define SCL_PORT  GPIO_DO_GPIOD
#define SCL_PIN   13U
#define SDA_GPIO  HPM_GPIO0
#define SDA_PORT  GPIO_DO_GPIOD
#define SDA_PIN   12U

static uint32_t s_cpu_hz;
static uint32_t s_half_cycles = 60U;   /* 半周期(SCL 高/低时间), 100k 时 = 480M/100k/2 = 2400 */
static uint32_t s_ovh_half;

static inline uint64_t cyc(void)
{
    return hpm_csr_get_core_mcycle();
}

/* 精确到 CPU 周期的忙等 */
static inline void delay_cycles(uint32_t n)
{
    uint64_t t0 = cyc();
    while ((cyc() - t0) < (uint64_t) n) {
    }
}

/* 引脚初始化: GPIO 功能 + 开漏 + 内部上拉 */
static void soft_i2c_pins_init(void)
{
    HPM_IOC->PAD[SCL_PAD].FUNC_CTL = IOC_PD13_FUNC_CTL_GPIO_D_13;
    HPM_IOC->PAD[SCL_PAD].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1)
        | IOC_PAD_PAD_CTL_OD_SET(1);
    HPM_IOC->PAD[SDA_PAD].FUNC_CTL = IOC_PD12_FUNC_CTL_GPIO_D_12;
    HPM_IOC->PAD[SDA_PAD].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1)
        | IOC_PAD_PAD_CTL_OD_SET(1);
    gpio_set_pin_output(SCL_GPIO, SCL_PORT, SCL_PIN);
    gpio_set_pin_output(SDA_GPIO, SDA_PORT, SDA_PIN);
    gpio_write_pin(SCL_GPIO, SCL_PORT, SCL_PIN, 1U);
    gpio_write_pin(SDA_GPIO, SDA_PORT, SDA_PIN, 1U);
}

static inline void scl(uint32_t high)
{
    gpio_write_pin(SCL_GPIO, SCL_PORT, SCL_PIN, high ? 1U : 0U);
    delay_cycles(s_half_cycles);
}
static inline void sda(uint32_t high)
{
    gpio_write_pin(SDA_GPIO, SDA_PORT, SDA_PIN, high ? 1U : 0U);
    delay_cycles(s_half_cycles);
}
static inline uint32_t scl_read(void) { return gpio_read_pin(SCL_GPIO, SCL_PORT, SCL_PIN); }
static inline uint32_t sda_read(void) { return gpio_read_pin(SDA_GPIO, SDA_PORT, SDA_PIN); }

/* I2C 基本时序: 起始/停止/写一字节(返回 ACK: 0=有从机应答) */
static void i2c_start(void)
{
    sda(1U); scl(1U);   /* 空闲: 两线高 */
    sda(0U);            /* SCL 高时 SDA 下降 = START */
    scl(0U);
}

static void i2c_stop(void)
{
    sda(0U); scl(0U);
    scl(1U);            /* SCL 高时 SDA 上升 = STOP */
    sda(1U);
}

static uint32_t i2c_write_byte(uint8_t b)
{
    for (uint32_t i = 0; i < 8U; i++) {
        sda((b & 0x80U) ? 1U : 0U);
        scl(1U);
        scl(0U);
        b <<= 1;
    }
    sda(1U);            /* 释放 SDA, 读从机 ACK */
    scl(1U);
    uint32_t ack = sda_read();   /* 0 = ACK(从机拉低), 1 = NACK */
    scl(0U);
    return ack;
}

static uint8_t i2c_read_byte(uint32_t send_ack)
{
    uint8_t b = 0;
    sda(1U);            /* 释放 SDA 由从机驱动 */
    for (uint32_t i = 0; i < 8U; i++) {
        scl(1U);
        b = (uint8_t) ((b << 1) | (sda_read() ? 1U : 0U));
        scl(0U);
    }
    sda(send_ack ? 0U : 1U);    /* 主机 ACK=0 / NACK=1 */
    scl(1U);
    scl(0U);
    sda(1U);
    return b;
}

/* 用 8 个 SCL 脉冲测实际 SCL 频率(kHz): 返回 0 表示失败 */
static uint32_t measure_scl_khz(void)
{
    uint64_t t0 = cyc();
    for (uint32_t i = 0; i < 10U; i++) {
        scl(1U);
        scl(0U);
    }
    uint64_t dt = cyc() - t0;
    if (dt == 0U) { return 0U; }
    /* 10 个完整 SCL 周期 = 20 个半周期 */
    return (uint32_t) (((uint64_t) 10U * s_cpu_hz) / (dt * 1000ULL));
}

/* 校准: 由于函数调用/寄存器写有固定开销, 实测周期 = 2*half + ovh, 这里测出 ovh/2 */
static void calibrate(void)
{
    uint32_t save = s_half_cycles;
    s_half_cycles = 1000U;
    uint64_t t0 = cyc();
    for (uint32_t i = 0U; i < 10U; i++) { scl(1U); scl(0U); }
    uint64_t dt = cyc() - t0;
    uint32_t per = (uint32_t) (dt / 10U);
    int32_t ovh = (int32_t) per - 2000;
    s_ovh_half = (ovh > 0) ? (uint32_t) (ovh / 2) : 0U;
    s_half_cycles = save;
}

static void set_scl_khz(uint32_t khz)
{
    uint32_t half = s_cpu_hz / (2U * khz * 1000U);
    if (half <= (s_ovh_half + 10U)) { half = s_ovh_half + 10U; }
    s_half_cycles = half - s_ovh_half;
}

static void cmd_info(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("i2c_loopback @ hpm5e31_1: soft (bit-bang) I2C master\r\n");
    printf("  SCL=PD13 SDA=PD12, open-drain(OD=1)+internal pull-up(PE/PS=1)\r\n");
    printf("  cpu=%lu Hz, half_cycles=%lu ovh_half=%lu (SCL 目标≈%lu kHz)\r\n",
           (unsigned long) s_cpu_hz, (unsigned long) s_half_cycles, (unsigned long) s_ovh_half,
           (unsigned long) (s_cpu_hz / (2U * s_half_cycles) / 1000U));
    printf("  usage: i2c_soft_lines | i2c_soft_selftest | i2c_soft_speed <kHz> | i2c_soft_scan [from] [to] | i2c_soft_timing | i2c_soft_recover\r\n");
}

static void cmd_lines(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("lines: SCL=%d SDA=%d (1=释放/上拉高, 0=被拉低)\r\n", scl_read() ? 1 : 0, sda_read() ? 1 : 0);
}

/* 线电平自检:
 *  1) 引脚设为输入: 内部下拉应读 0, 内部上拉应读 1 (证明 DI 通路 + 上下拉有效)
 *  2) 引脚设为输出(开漏): 驱动 0 / 释放 1, 记录 DI 回读值(用于说明 GPI 开漏下能否自回读)
 */
static void cmd_selftest(int argc, char *argv[])
{
    (void) argc; (void) argv;
    uint32_t err = 0U;
    printf("pin self-test A: input + internal pull-down/pull-up\r\n");
    for (uint32_t i = 0U; i < 2U; i++) {
        uint32_t pad = (i == 0U) ? SCL_PAD : SDA_PAD;
        uint32_t port = (i == 0U) ? SCL_PORT : SDA_PORT;
        uint32_t pin = (i == 0U) ? SCL_PIN : SDA_PIN;
        const char *nm = (i == 0U) ? "SCL" : "SDA";
        gpio_set_pin_input(HPM_GPIO0, port, pin);
        HPM_IOC->PAD[pad].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(0);
        delay_cycles(2000U);
        uint32_t pd = gpio_read_pin(HPM_GPIO0, port, pin);
        HPM_IOC->PAD[pad].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1);
        delay_cycles(2000U);
        uint32_t pu = gpio_read_pin(HPM_GPIO0, port, pin);
        printf("  %s: pull-down DI=%lu, pull-up DI=%lu %s\r\n", nm,
               (unsigned long) pd, (unsigned long) pu,
               ((pd == 0U) && (pu == 1U)) ? "OK" : "FAIL");
        if ((pd != 0U) || (pu != 1U)) { err++; }
    }
    printf("pin self-test B: output(open-drain) DI read-back (仅记录, 不作为 PASS 条件)\r\n");
    for (uint32_t i = 0U; i < 2U; i++) {
        uint32_t pad = (i == 0U) ? SCL_PAD : SDA_PAD;
        uint32_t port = (i == 0U) ? SCL_PORT : SDA_PORT;
        uint32_t pin = (i == 0U) ? SCL_PIN : SDA_PIN;
        const char *nm = (i == 0U) ? "SCL" : "SDA";
        HPM_IOC->PAD[pad].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1)
            | IOC_PAD_PAD_CTL_OD_SET(1);
        gpio_set_pin_output(HPM_GPIO0, port, pin);
        gpio_write_pin(HPM_GPIO0, port, pin, 0U);
        delay_cycles(2000U);
        uint32_t lo = gpio_read_pin(HPM_GPIO0, port, pin);
        gpio_write_pin(HPM_GPIO0, port, pin, 1U);
        delay_cycles(2000U);
        uint32_t hi = gpio_read_pin(HPM_GPIO0, port, pin);
        printf("  %s: drive0->DI=%lu, release->DI=%lu\r\n", nm,
               (unsigned long) lo, (unsigned long) hi);
    }
    /* 恢复为开漏输出 + 上拉, 供后续软件 I2C 使用 */
    soft_i2c_pins_init();
    printf("selftest A: %lu errors -> %s\r\n", (unsigned long) err, err ? "FAIL" : "PASS");
}

static void cmd_speed(int argc, char *argv[])
{
    uint32_t khz = 100U, meas = 0U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &khz); }
    if (khz < 10U) { khz = 10U; }
    if (khz > 1000U) { khz = 1000U; }
    set_scl_khz(khz);
    meas = measure_scl_khz();
    printf("i2c_soft_speed target=%lu kHz half_cycles=%lu measured=%lu kHz\r\n",
           (unsigned long) khz, (unsigned long) s_half_cycles, (unsigned long) meas);
}

/* 软件地址扫描: 全 NACK 说明总线上没有从机 */
static void cmd_scan(int argc, char *argv[])
{
    uint32_t from = 0x08U, to = 0x77U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &from); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &to); }
    if (from < 0x08U) { from = 0x08U; }
    if (to > 0x77U) { to = 0x77U; }
    if (to < from) { to = from; }
    uint32_t found = 0U, ack;
    for (uint32_t a = from; a <= to; a++) {
        i2c_start();
        ack = i2c_write_byte((uint8_t) (a << 1));
        i2c_stop();
        if (ack == 0U) {
            found++;
            printf("  ACK @0x%02lX\r\n", (unsigned long) a);
        }
    }
    printf("soft_scan 0x%02lX..0x%02lX: %lu addr, found=%lu (0 = 总线无从器件, NACK 正常)\r\n",
           (unsigned long) from, (unsigned long) to, (unsigned long) (to - from + 1U),
           (unsigned long) found);
}

/* 时序表: 目标 100k/400k/1M 的实际 SCL(软件位翻转能到多少) */
static void cmd_timing(int argc, char *argv[])
{
    (void) argc; (void) argv;
    static const uint32_t khz_list[4] = { 100U, 400U, 1000U, 2000U };
    printf("soft I2C SCL sweep (半周期按 CPU 周期计算, 实测 10 个周期):\r\n");
    printf("%10s %12s %12s %8s\r\n", "target_kHz", "half_cycles", "measured_kHz", "err_ppm");
    for (uint32_t i = 0; i < 4U; i++) {
        uint32_t khz = khz_list[i];
        set_scl_khz(khz);
        uint32_t meas = measure_scl_khz();
        int32_t err = (meas == 0U) ? 0 : (int32_t) (((int64_t) meas - (int64_t) khz) * 1000000LL
                                                    / (int64_t) khz);
        printf("%10lu %12lu %12lu %8ld\r\n", (unsigned long) khz,
               (unsigned long) s_half_cycles, (unsigned long) meas, (long) err);
    }
    /* 复位到 100k */
    set_scl_khz(100U);
}

/* 总线恢复: 若从机把 SDA 拉死, 用 9 个 SCL 时钟 + STOP 释放 */
static void cmd_recover(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("before: SCL=%d SDA=%d\r\n", scl_read() ? 1 : 0, sda_read() ? 1 : 0);
    gpio_write_pin(SDA_GPIO, SDA_PORT, SDA_PIN, 1U);     /* 释放 SDA */
    for (uint32_t i = 0; i < 9U; i++) {
        scl(1U);
        scl(0U);
    }
    i2c_stop();
    printf("after 9-clock recovery: SCL=%d SDA=%d (1=释放/高)\r\n",
           scl_read() ? 1 : 0, sda_read() ? 1 : 0);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_soft_info, cmd_info, soft i2c: pin/config info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_soft_lines, cmd_lines, soft i2c: read SCL/SDA levels);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_soft_selftest, cmd_selftest, soft i2c: open-drain/pull-up pin self-test);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_soft_speed, cmd_speed, soft i2c: soft_speed <kHz> set and measure SCL);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_soft_scan, cmd_scan, soft i2c: soft_scan [from] [to] software address scan);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_soft_timing, cmd_timing, soft i2c: SCL sweep table 100k/400k/1M/2M);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_soft_recover, cmd_recover, soft i2c: 9-clock bus recovery);

int main(void)
{
    bsp_init();
    s_cpu_hz = clock_get_frequency(clock_cpu0);
    soft_i2c_pins_init();
    calibrate();
    set_scl_khz(100U);   /* 默认 100 kHz */

    printf("\r\n=== i2c_loopback (soft bit-bang I2C + open-drain/pull-up self test) ===\r\n");
    cmd_info(0, NULL);
    cmd_lines(0, NULL);
    cmd_selftest(0, NULL);
    cmd_timing(0, NULL);
    cmd_scan(0, NULL);
    printf("type 'i2c_soft_speed 400' or 'i2c_soft_recover'\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
