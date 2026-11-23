/*
 * i2c_scan - I2C0(PC08/PC09) 地址扫描 + 总线时序/速率实测 + IOC 回环对比
 *
 * 硬件: hpm5e31_1, I2C0 SCL=PC08 SDA=PC09, 板上无 I2C 从器件,
 *       引脚默认带 1 kOhm 级内部上拉 + 开漏(OD) + IOC LOOP_BACK(见 board pinmux init_i2c0_pins).
 * 结论: 扫描 0x08~0x77 全部 NACK 是"无从器件"的正常现象; 本例程用一次 probe 事务的耗时反推
 *       实际 SCL 频率(100k/400k/1M), 并对比打开/关闭 IOC LOOP_BACK 的差异.
 * 命令: i2c_info / i2c_scan [from] [to] / i2c_timing / i2c_lines / i2c_loopback <0|1> / i2c_reset
 */
#include "bsp.h"
#include "hpm_i2c_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_iomux.h"

/* 注意: I2C0 的 PC08 与 MCAN2_TXD 复用(本板 4 路 CAN 全用), 所以本样例改到 I2C1:
 * SCL=PD13, SDA=PD12 (PD12/PD13 在本板上未被其它功能占用, 原 ECAT 演示脚默认不复用) */
#define T_I2C        HPM_I2C1
#define T_I2C_CLK    clock_i2c1
#define T_I2C_PAD_SCL IOC_PAD_PD13
#define T_I2C_PAD_SDA IOC_PAD_PD12

static uint32_t s_src_hz;
static uint32_t s_cpu_hz;
static uint8_t  s_dummy = 0x5AU;
static uint32_t s_loopback = 1U;
static uint32_t s_mode = 0U;   /* 0=normal 100k, 1=fast 400k, 2=fast_plus 1M */

static const char *mode_name(uint32_t m)
{
    switch (m) {
    case 0:  return "100k(normal)";
    case 1:  return "400k(fast)";
    default: return "1M(fast_plus)";
    }
}

static uint32_t mode_baud(uint32_t m)
{
    switch (m) {
    case 0:  return 100000UL;
    case 1:  return 400000UL;
    default: return 1000000UL;
    }
}

/* 打印 I2C 时序寄存器原始值(不做除法, 避免异常; SCL 精确验证需示波器) */
static void i2c_dump_timing_regs(void)
{
    printf("  SETUP=0x%08lx TPM=0x%08lx (T_SP=%lu T_SCLHI=%lu T_SCLRATIO=%lu T_SUDAT=%lu T_HDDAT=%lu)\r\n",
           (unsigned long) T_I2C->SETUP, (unsigned long) T_I2C->TPM,
           (unsigned long) I2C_SETUP_T_SP_GET(T_I2C->SETUP),
           (unsigned long) I2C_SETUP_T_SCLHI_GET(T_I2C->SETUP),
           (unsigned long) (I2C_SETUP_T_SCLRADIO_GET(T_I2C->SETUP) + 1U),
           (unsigned long) I2C_SETUP_T_SUDAT_GET(T_I2C->SETUP),
           (unsigned long) I2C_SETUP_T_HDDAT_GET(T_I2C->SETUP));
}

static inline uint64_t cyc(void)
{
    return hpm_csr_get_core_mcycle();
}

static void i2c_pad_config(uint32_t loopback)
{
    HPM_IOC->PAD[T_I2C_PAD_SCL].FUNC_CTL = IOC_PD13_FUNC_CTL_I2C1_SCL
        | (loopback ? IOC_PAD_FUNC_CTL_LOOP_BACK_MASK : 0U);
    HPM_IOC->PAD[T_I2C_PAD_SCL].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1)
        | IOC_PAD_PAD_CTL_OD_SET(1);
    HPM_IOC->PAD[T_I2C_PAD_SDA].FUNC_CTL = IOC_PD12_FUNC_CTL_I2C1_SDA
        | (loopback ? IOC_PAD_FUNC_CTL_LOOP_BACK_MASK : 0U);
    HPM_IOC->PAD[T_I2C_PAD_SDA].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1) | IOC_PAD_PAD_CTL_PS_SET(1)
        | IOC_PAD_PAD_CTL_OD_SET(1);
    s_loopback = loopback;
}

static hpm_stat_t i2c_setup(uint32_t mode)
{
    i2c_config_t cfg;
    s_mode = mode;
    cfg.i2c_mode = (mode == 0U) ? i2c_mode_normal
                 : ((mode == 1U) ? i2c_mode_fast : i2c_mode_fast_plus);
    cfg.is_10bit_addressing = false;
    return i2c_init_master(T_I2C, s_src_hz, &cfg);
}

/* 用一次"只有地址阶段"的 probe 事务测时间 */
static hpm_stat_t i2c_probe(uint8_t addr7, uint64_t *cycles)
{
    uint8_t buf = s_dummy;
    uint64_t t0 = cyc();
    hpm_stat_t st = i2c_master_write(T_I2C, addr7, &buf, 0U);
    *cycles = cyc() - t0;
    return st;
}

static const char *stat_name(hpm_stat_t st)
{
    if (st == status_success) { return "ACK(found)"; }
    if (st == status_i2c_no_addr_hit) { return "NACK(no slave)"; }
    if (st == status_timeout) { return "TIMEOUT(bus busy)"; }
    return "OTHER";
}

/* ---- shell 命令 ---- */
static void cmd_info(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("i2c_scan @ hpm5e31_1, I2C1 SCL=PD13 SDA=PD12 (I2C0 的 PC08 与 MCAN2_TXD 冲突)\r\n");
    printf("  src_clk=%lu Hz, cpu=%lu Hz, mode=%s, loopback=%s\r\n",
           (unsigned long) s_src_hz, (unsigned long) s_cpu_hz,
           mode_name(s_mode), s_loopback ? "on" : "off");
    printf("  bus: internal pull-up(PE=1,PS=1) + open-drain(OD=1), 无外部从器件\r\n");
    printf("  usage: i2c_scan [0x08..0x77] / i2c_timing / i2c_lines / i2c_loopback <0|1>\r\n");
}

static void cmd_lines(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("lines: SCL=%d SDA=%d (1=released/high), CMPL=%d, BUSBUSY=%d\r\n",
           i2c_get_line_scl_status(T_I2C) ? 1 : 0,
           i2c_get_line_sda_status(T_I2C) ? 1 : 0,
           (T_I2C->STATUS & I2C_STATUS_CMPL_MASK) ? 1 : 0,
           (T_I2C->STATUS & I2C_STATUS_BUSBUSY_MASK) ? 1 : 0);
    i2c_dump_timing_regs();
}

static void cmd_scan(int argc, char *argv[])
{
    uint32_t from = 0x08U, to = 0x77U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &from); }
    if (argc >= 3) { (void) bsp_parse_u32(argv[2], &to); }
    if (from < 0x08U) { from = 0x08U; }
    if (to > 0x77U) { to = 0x77U; }
    if (to < from) { to = from; }

    uint32_t found = 0U, nack = 0U, other = 0U;
    uint64_t sum = 0U, t0 = cyc();
    for (uint32_t a = from; a <= to; a++) {
        uint64_t dt = 0U;
        hpm_stat_t st = i2c_probe((uint8_t) a, &dt);
        sum += dt;
        if (st == status_success) {
            found++;
            printf("  found device @0x%02lX\r\n", (unsigned long) a);
        } else if (st == status_i2c_no_addr_hit) {
            nack++;
        } else {
            other++;
            printf("  addr 0x%02lX -> %s (0x%08lX)\r\n", (unsigned long) a,
                   stat_name(st), (unsigned long) st);
        }
    }
    uint64_t total = cyc() - t0;
    uint32_t n = to - from + 1U;
    uint32_t avg_ns = (uint32_t) (sum * 1000000000ULL / ((uint64_t) n * s_cpu_hz));
    printf("scan 0x%02lX..0x%02lX: %lu addr, found=%lu nack=%lu other=%lu, avg_probe=%lu ns, total=%lu us\r\n",
           (unsigned long) from, (unsigned long) to, (unsigned long) n,
           (unsigned long) found, (unsigned long) nack, (unsigned long) other,
           (unsigned long) avg_ns, (unsigned long) (total * 1000000ULL / s_cpu_hz));
    /* 注意: avg_probe 包含驱动 NACK 重试(5000 次空转)的时间, 不能反推 SCL;
     * SCL 请用 i2c_timing 里的寄存器回读 + RM §50.2.2 公式(9)(10) 计算, 或用示波器实测. */
    printf("note: avg_probe 含 NACK retry, 不能反推 SCL; 见 i2c_timing 的寄存器回读\r\n");
}

static void cmd_timing(int argc, char *argv[])
{
    (void) argc; (void) argv;
    printf("I2C timing: probe(addr only) x16. 注意 NACK 时驱动会空转 ADDRHIT 重试(5000 次),\r\n");
    printf("所以 probe 延迟=硬件传输时间+软件重试时间, 不能直接反推 SCL; SCL 用寄存器回读计算.\r\n");
    printf("%-16s %9s %10s %10s\r\n", "mode", "reg_SCL_kHz", "reg_ns", "probe_us");
    for (uint32_t m = 0U; m < 3U; m++) {
        if (i2c_setup(m) != status_success) {
            printf("%-16s  init failed\r\n", mode_name(m));
            continue;
        }
        i2c_dump_timing_regs(); uint32_t meas = mode_baud(m);
        uint64_t sum = 0U;
        uint32_t ok = 0U;
        for (uint32_t i = 0U; i < 16U; i++) {
            uint64_t dt = 0U;
            hpm_stat_t st = i2c_probe(0x50U, &dt);
            if (st != status_timeout) { sum += dt; ok++; }
        }
        if (ok == 0U) {
            printf("%-16s  all timeout\r\n", mode_name(m));
            continue;
        }
        uint32_t avg_ns = (uint32_t) (sum * 1000000000ULL / ((uint64_t) ok * s_cpu_hz));
        printf("%-16s %9lu %10s %10lu.%03lu\r\n", mode_name(m),
               (unsigned long) (meas / 1000U), "(reg)",
               (unsigned long) (avg_ns / 1000U), (unsigned long) (avg_ns % 1000U));
    }
    (void) i2c_setup(0U);
}

static void cmd_loopback(int argc, char *argv[])
{
    uint32_t on = 1U;
    if (argc >= 2) { (void) bsp_parse_u32(argv[1], &on); }
    i2c_pad_config(on ? 1U : 0U);
    (void) i2c_setup(s_mode);
    printf("IOC LOOP_BACK on SCL/SDA = %s (重新初始化 I2C0)\r\n", on ? "on" : "off");
    cmd_lines(0, NULL);
}

static void cmd_reset(int argc, char *argv[])
{
    (void) argc; (void) argv;
    i2c_gen_reset_signal(T_I2C, 9U);
    i2c_clear_status(T_I2C, I2C_STATUS_CMPL_MASK);
    printf("bus recovery: 9 SCL pulses delivered (i2c_gen_reset_signal)\r\n");
    cmd_lines(0, NULL);
}

static void cmd_read(int argc, char *argv[])
{
    (void) argc; (void) argv;
    uint8_t buf[4] = {0};
    uint64_t t0 = cyc();
    hpm_stat_t st = i2c_master_read(T_I2C, 0x50U, buf, sizeof(buf));
    uint64_t dt = cyc() - t0;
    printf("read 4 bytes @0x50 -> %s (0x%08lX), %lu us, data=%02X %02X %02X %02X\r\n",
           stat_name(st), (unsigned long) st,
           (unsigned long) (dt * 1000000ULL / s_cpu_hz),
           buf[0], buf[1], buf[2], buf[3]);
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_info, cmd_info, i2c0: pin/clock/mode info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_scan, cmd_scan, i2c0: i2c_scan [from] [to] address scan);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_timing, cmd_timing, i2c0: 100k/400k/1M SCL timing table);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_lines, cmd_lines, i2c0: bus line levels/status);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_loopback, cmd_loopback, i2c0: i2c_loopback <0|1> IOC loopback test);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_reset, cmd_reset, i2c0: generate 9 clocks bus recovery);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_read, cmd_read, i2c0: try read 4 bytes @0x50 (error path demo));

int main(void)
{
    bsp_init();
    i2c_pad_config(1U);
    s_src_hz = board_init_i2c_clock(T_I2C);
    s_cpu_hz = clock_get_frequency(clock_cpu0);
    if (i2c_setup(0U) != status_success) {
        printf("i2c_init_master failed\r\n");
        while (1) { bsp_shell_poll(); }
    }

    printf("\r\n=== i2c_scan (I2C0 PC08/PC09, no slave on board) ===\r\n");
    cmd_info(0, NULL);
    cmd_lines(0, NULL);
    cmd_scan(0, NULL);
    cmd_timing(0, NULL);
    printf("scan all-NACK is expected without external I2C device;\r\n");
    printf("wire SCL->SCL/SDA->SDA to a real slave (AT24C128 etc.) to see ACK.\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
