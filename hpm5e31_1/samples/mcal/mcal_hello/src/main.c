/*
 * mcal_hello —— MCAL 公共基础设施自检（不依赖任何 MCAL 模块）
 *
 * 验证：Std_Types/Platform_Types/Compiler 能编译、Det 计数与查询可用、
 *       Mcal_ModuleId.h 的模块 ID 与 AUTOSAR SWS_BSWGeneral §11.2 一致、
 *       并用 mcycle 实测 Det_ReportError / Det_GetLastError 的 CPU 周期开销。
 */
#include "bsp.h"
#include "Mcal.h"
#include "hpm_csr_drv.h"
#include <stdio.h>

#define BENCH_ITER (1000u)

static void cmd_mcal(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("MCAL: %s\r\n", MCAL_VERSION_STRING);
    printf("AR release: %u.%u.%u, SW: %u.%u.%u\r\n",
           (unsigned)MCAL_AR_RELEASE_MAJOR_VERSION, (unsigned)MCAL_AR_RELEASE_MINOR_VERSION,
           (unsigned)MCAL_AR_RELEASE_REVISION_VERSION, (unsigned)MCAL_SW_MAJOR_VERSION,
           (unsigned)MCAL_SW_MINOR_VERSION, (unsigned)MCAL_SW_PATCH_VERSION);
    printf("module ids: Mcu=%u Port=%u Dio=%u Gpt=%u Pwm=%u Icu=%u\r\n",
           (unsigned)MCU_MODULE_ID, (unsigned)PORT_MODULE_ID, (unsigned)DIO_MODULE_ID,
           (unsigned)GPT_MODULE_ID, (unsigned)PWM_MODULE_ID, (unsigned)ICU_MODULE_ID);
    printf("            Adc=%u Spi=%u I2c=%u Can=%u Wdg=%u Mem=%u Crc=%u Det=%u\r\n",
           (unsigned)ADC_MODULE_ID, (unsigned)SPI_MODULE_ID, (unsigned)I2C_MODULE_ID,
           (unsigned)CAN_MODULE_ID, (unsigned)WDG_MODULE_ID, (unsigned)MEM_MODULE_ID,
           (unsigned)CRC_MODULE_ID, (unsigned)DET_MODULE_ID);
    printf("dev error detect: %s\r\n", (MCAL_DEV_ERROR_DETECT == STD_ON) ? "ON" : "OFF");
    printf("sizeof: uint8=%u uint16=%u uint32=%u uint64=%u float32=%u Std_ReturnType=%u\r\n",
           (unsigned)sizeof(uint8), (unsigned)sizeof(uint16), (unsigned)sizeof(uint32),
           (unsigned)sizeof(uint64), (unsigned)sizeof(float32), (unsigned)sizeof(Std_ReturnType));
}

static void cmd_det(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint16 mod = 0; uint8 inst = 0; uint8 api = 0; uint8 err = 0;
    printf("DET: count=%u\r\n", (unsigned)Det_GetErrorCount());
    if (Det_GetLastError(&mod, &inst, &api, &err) == E_OK) {
        printf("DET: last mod=%u inst=%u api=%u err=%u\r\n",
               (unsigned)mod, (unsigned)inst, (unsigned)api, (unsigned)err);
    } else {
        printf("DET: no error recorded\r\n");
    }
    /* 故意报一条，验证 Det 通路 */
    (void)Det_ReportError((uint16)MCU_MODULE_ID, 0u, 0x5Au, (uint8)DET_E_PARAM_VALUE);
    printf("DET: after inject count=%u\r\n", (unsigned)Det_GetErrorCount());
}

/* 用 mcycle 实测：空函数调用基线 / Det_ReportError / Det_GetLastError 的周期数 */
static void cmd_detbench(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint32 c0, c1, i;
    uint32 base_cyc, report_cyc, last_cyc;
    uint16 mod; uint8 inst, api, err;
    uint32 e = 0;

    /* 基线：同样的循环 + 空函数调用 */
    c0 = hpm_csr_get_core_mcycle();
    for (i = 0; i < BENCH_ITER; i++) {
        e += i;
    }
    c1 = hpm_csr_get_core_mcycle();
    base_cyc = (c1 - c0);

    c0 = hpm_csr_get_core_mcycle();
    for (i = 0; i < BENCH_ITER; i++) {
        (void)Det_ReportError((uint16)MCU_MODULE_ID, 0u, 0x5Au, (uint8)DET_E_PARAM_VALUE);
    }
    c1 = hpm_csr_get_core_mcycle();
    report_cyc = (c1 - c0);

    (void)Det_ReportError((uint16)MCU_MODULE_ID, 0u, 0x5Au, (uint8)DET_E_PARAM_VALUE);
    c0 = hpm_csr_get_core_mcycle();
    for (i = 0; i < BENCH_ITER; i++) {
        (void)Det_GetLastError(&mod, &inst, &api, &err);
    }
    c1 = hpm_csr_get_core_mcycle();
    last_cyc = (c1 - c0);

    printf("DETBENCH: %u iters, core clock=%u Hz\r\n", (unsigned)BENCH_ITER,
           (unsigned)clock_get_frequency(clock_cpu0));
    printf("DETBENCH: loop baseline      = %u cyc total (%u.%02u cyc/iter)\r\n",
           (unsigned)base_cyc, (unsigned)((base_cyc * 100u) / BENCH_ITER / 100u),
           (unsigned)((base_cyc * 100u) / BENCH_ITER % 100u));
    printf("DETBENCH: Det_ReportError    = %u cyc total (%u.%02u cyc/iter)\r\n",
           (unsigned)report_cyc, (unsigned)((report_cyc * 100u) / BENCH_ITER / 100u),
           (unsigned)((report_cyc * 100u) / BENCH_ITER % 100u));
    printf("DETBENCH: Det_GetLastError   = %u cyc total (%u.%02u cyc/iter)\r\n",
           (unsigned)last_cyc, (unsigned)((last_cyc * 100u) / BENCH_ITER / 100u),
           (unsigned)((last_cyc * 100u) / BENCH_ITER % 100u));
    printf("DETBENCH: at 480 MHz -> ReportError %.3f us/call, GetLastError %.3f us/call\r\n",
           (double)report_cyc / (double)BENCH_ITER / 480.0,
           (double)last_cyc / (double)BENCH_ITER / 480.0);
    (void)e;
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC) | SHELL_CMD_DISABLE_RETURN,
                 mcal, cmd_mcal, print MCAL version/module ids);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC) | SHELL_CMD_DISABLE_RETURN,
                 det, cmd_det, Det error tracer test);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC) | SHELL_CMD_DISABLE_RETURN,
                 detbench, cmd_detbench, measure Det API cycle cost);

int main(void)
{
    bsp_init();
    Det_Init();
    printf("\r\n=== mcal_hello: AUTOSAR MCAL common infra self-test ===\r\n");
    printf("MCAL %s\r\n", MCAL_VERSION_STRING);
    cmd_mcal(0, NULL);
    printf("type 'mcal' / 'det' / 'detbench' / 'help'\r\n");
    while (1) {
        bsp_shell_poll();
    }
    return 0;
}
