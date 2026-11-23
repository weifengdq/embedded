/*
 * i2c_error —— AUTOSAR MCAL I2C 错误路径例程：NACK / 总线被占(仲裁失败等) / DevError
 *
 * 接线同 i2c_master_slave：I2C2(PD02/PD03) <-> I2C3(PD07/PD06，目标 0x50)
 * 验证点（SWS I2CDriver §7.3）：
 *   1) 访问不存在的地址 0x33 -> I2C_E_NACK_RECEIVED 运行时错误，Det 计数 +1，序列结果 I2C_SEQ_NACK；
 *   2) 人为把 SDA 拉低模拟总线被占 -> 传输失败，上报 I2C_E_ARBITRATION_FAILURE；
 *   3) 非法参数 -> I2C_E_PARAM_JOB/SEQUENCE/POINTER/UNIT DevError，Det 计数增加。
 * 命令：i2c_err nack | i2c_err busy | i2c_err det | i2c_err all | i2c_err info
 */
#include "bsp.h"
#include "I2c.h"
#include "I2c_PBcfg.h"
#include "Det.h"
#include "hpm_soc.h"
#include "hpm_gpio_drv.h"
#include "hpm_iomux.h"

#include <stdio.h>
#include <string.h>

#define BUF_SIZE (64u)

ATTR_PLACE_AT_NONCACHEABLE static uint8 s_m_tx[BUF_SIZE];
ATTR_PLACE_AT_NONCACHEABLE static uint8 s_t_rx[BUF_SIZE];
ATTR_PLACE_AT_NONCACHEABLE static uint8 s_t_tx[BUF_SIZE];

static uint32 s_nack_cnt;
static uint32 s_busy_cnt;

void I2C_SeqEndNotification_Cb(void) { }

static void fill(uint32 n)
{
    uint32 i;
    for (i = 0u; i < n; i++) {
        s_m_tx[i] = (uint8)(0xA0u + i);
        s_t_rx[i] = 0u;
        s_t_tx[i] = (uint8)(0x50u + i);
    }
}

/* 1) NACK：目标地址无人响应 */
static void test_nack(void)
{
    Std_ReturnType rc;
    uint32 det0 = Det_GetErrorCount();
    uint32 rt0 = I2C_GetRuntimeErrorCount();

    fill(4u);
    (void)I2C_SetupEB(I2C_JOB_TARGET, 0u, NULL, s_t_rx, 4u);
    if (I2C_SetupEB(I2C_JOB_MASTER_TX, (I2C_AddressType)0x33u, s_m_tx, NULL, 4u) != E_OK) {
        printf("nack: SetupEB failed\r\n");
        return;
    }
    rc = I2C_SyncTransmit(I2C_SEQ_WRITE);
    if (rc != E_OK) { s_nack_cnt++; }
    printf("nack: addr=0x33 SyncTransmit=%s seqResult=%d (NACK=3) runtimeErr=%lu(+%lu) det=%lu(+%lu) => %s\r\n",
           (rc == E_OK) ? "E_OK" : "E_NOT_OK",
           (int)I2C_GetSequenceResult(I2C_SEQ_WRITE),
           (unsigned long)I2C_GetRuntimeErrorCount(),
           (unsigned long)(I2C_GetRuntimeErrorCount() - rt0),
           (unsigned long)Det_GetErrorCount(), (unsigned long)(Det_GetErrorCount() - det0),
           ((rc == E_NOT_OK) && (I2C_GetSequenceResult(I2C_SEQ_WRITE) == I2C_SEQ_NACK)) ? "PASS (NACK 被识别)" : "FAIL");
}

/* 2) 总线被占：把控制器 SDA(PD03) 拉低，模拟总线被别的设备拽住 */
static void test_busy(void)
{
    Std_ReturnType rc;
    uint32 det0 = Det_GetErrorCount();
    uint32 rt0 = I2C_GetRuntimeErrorCount();

    fill(4u);
    (void)I2C_SetupEB(I2C_JOB_TARGET, 0u, NULL, s_t_rx, 4u);
    (void)I2C_SetupEB(I2C_JOB_MASTER_TX, I2C_TARGET_ADDR, s_m_tx, NULL, 4u);

    /* 抢占 PD03 为 GPIO 输出低（I2C SDA 低 = 总线忙） */
    HPM_IOC->PAD[IOC_PAD_PD03].FUNC_CTL = IOC_PD03_FUNC_CTL_GPIO_D_03;
    HPM_IOC->PAD[IOC_PAD_PD03].PAD_CTL = IOC_PAD_PAD_CTL_PE_SET(1);
    gpio_set_pin_output(HPM_GPIO0, GPIO_DO_GPIOD, 3u);
    gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOD, 3u, 0u);
    for (volatile uint32 d = 0u; d < 50000u; d++) { }

    rc = I2C_SyncTransmit(I2C_SEQ_WRITE);
    if (rc != E_OK) { s_busy_cnt++; }
    printf("busy: SDA 被拉低后 SyncTransmit=%s seqResult=%d runtimeErr=%lu(+%lu) det=%lu(+%lu) => %s\r\n",
           (rc == E_OK) ? "E_OK" : "E_NOT_OK",
           (int)I2C_GetSequenceResult(I2C_SEQ_WRITE),
           (unsigned long)I2C_GetRuntimeErrorCount(),
           (unsigned long)(I2C_GetRuntimeErrorCount() - rt0),
           (unsigned long)Det_GetErrorCount(), (unsigned long)(Det_GetErrorCount() - det0),
           (rc == E_NOT_OK) ? "PASS (总线故障被识别)" : "FAIL");

    /* 恢复引脚：重新初始化 I2C 通道 */
    I2C_DeInit();
    I2C_Init(&I2C_Config);
    printf("busy: 引脚与驱动已恢复 (I2C_Init)\r\n");
}

/* 3) DevError：非法 job/sequence/指针 */
static void test_det(void)
{
    uint32 c0 = Det_GetErrorCount();

    (void)I2C_SetupEB(0x7Fu, I2C_TARGET_ADDR, s_m_tx, s_t_rx, 4u);   /* I2C_E_PARAM_JOB */
    (void)I2C_SetupEB(I2C_JOB_MASTER_TX, I2C_TARGET_ADDR, NULL, NULL, 4u); /* I2C_E_PARAM_POINTER */
    (void)I2C_SetupEB(I2C_JOB_MASTER_TX, I2C_TARGET_ADDR, s_m_tx, NULL, 0u); /* I2C_E_PARAM_JOB(长度) */
    (void)I2C_SyncTransmit(0x7Fu);                                  /* I2C_E_PARAM_SEQUENCE */
    (void)I2C_AsyncTransmit(0x7Fu);                                 /* I2C_E_PARAM_SEQUENCE */
    (void)I2C_GetSequenceResult(0x7Fu);                             /* I2C_E_PARAM_SEQUENCE */
    (void)I2C_GetChannelStatus(0x7Fu);                              /* I2C_E_PARAM_UNIT */
    I2C_GetVersionInfo(NULL);                                       /* I2C_E_PARAM_POINTER */
    printf("det: DevError +%lu (期望 8) 总计数=%lu => %s\r\n",
           (unsigned long)(Det_GetErrorCount() - c0), (unsigned long)Det_GetErrorCount(),
           ((Det_GetErrorCount() - c0) == 8u) ? "PASS" : "FAIL");
}

static void cmd_info(void)
{
    Std_VersionInfoType vi;
    I2C_GetVersionInfo(&vi);
    printf("i2c_error @ hpm5e31_1 | MCAL I2c %u.%u.%u moduleID=%u\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, (unsigned)vi.moduleID);
    printf("  runtimeErr=%lu det=%lu nackPass=%lu busyPass=%lu\r\n",
           (unsigned long)I2C_GetRuntimeErrorCount(), (unsigned long)Det_GetErrorCount(),
           (unsigned long)s_nack_cnt, (unsigned long)s_busy_cnt);
}

static void cmd_err(int argc, char **argv)
{
    if (argc < 1) { cmd_info(); return; }
    if (strcmp(argv[0], "nack") == 0) { test_nack(); }
    else if (strcmp(argv[0], "busy") == 0) { test_busy(); }
    else if (strcmp(argv[0], "det") == 0) { test_det(); }
    else if (strcmp(argv[0], "all") == 0) { test_nack(); test_busy(); test_det(); }
    else { cmd_info(); }
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 i2c_err, cmd_err, i2c error paths: nack|busy|det|all|info);

int main(void)
{
    bsp_init();
    printf("\r\n=== mcal i2c_error: NACK / bus-busy / DevError paths ===\r\n");
    I2C_Init(&I2C_Config);
    cmd_info();
    test_nack();
    test_busy();
    test_det();
    printf("i2c_error: done, runtimeErr=%lu det=%lu\r\n",
           (unsigned long)I2C_GetRuntimeErrorCount(), (unsigned long)Det_GetErrorCount());
    printf("type 'i2c_err all' / 'i2c_err info'\r\n");

    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
