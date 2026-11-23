/*
 * mcu_clock_init —— AUTOSAR Mcu 时钟/PLL 初始化 + 主频实测（HPM5E31）
 *
 * 验证点：
 *   1) Mcu_Init / Mcu_InitClock / Mcu_GetPllStatus / Mcu_DistributePllClock 序列（SWS 图 9.1）；
 *   2) PLL0 = 24 MHz x 40 = 960 MHz，POSTDIV0=/2 -> 480 MHz，CPU0 节点 /1 -> 480 MHz；
 *   3) 用 mchtmr(24 MHz 固定) 做时基、rdcycle 数 CPU 周期，实测主频并与配置值比较误差；
 *   4) Mcu_InitClock 换到设置 1（CPU /2 = 240 MHz）后再实测，验证配置生效；
 *   5) 开发错误路径（非法 clock setting / mode / 空指针）→ Det 计数增加。
 *
 * 例程：samples/mcal/mcu_clock_init；章节：docs/chapters/44-mcal-core.md
 */
#include "bsp.h"
#include "Mcu.h"
#include "Det.h"
#include <stdlib.h>

/* RISC-V 周期计数器低 32 位（RV32 上 rdcycle 即 mcycle） */
static uint32_t rdcycle_low(void)
{
    uint32_t cycles;
    __asm__ volatile("rdcycle %0" : "=r"(cycles));
    return cycles;
}

/* 以 24 MHz mchtmr 为时基，实测 CPU 主频：core = cycles / (mchtmr ticks / 24 MHz) */
static uint32_t measure_core_hz(uint32_t window_us)
{
    MCHTMR_Type *tmr = HPM_MCHTMR;
    uint64_t ticks = (uint64_t)window_us * 24ull;   /* 24 ticks/us */
    uint64_t t0 = tmr->MTIME;
    uint32_t c0 = rdcycle_low();
    uint32_t c1;
    uint64_t dt;

    while ((tmr->MTIME - t0) < ticks)
    {
        /* 忙等，测量窗口内 CPU 全速跑 */
    }
    c1 = rdcycle_low();
    dt = tmr->MTIME - t0;
    if (dt == 0ull)
    {
        return 0u;
    }
    return (uint32_t)(((uint64_t)(uint32_t)(c1 - c0) * 24000000ull) / dt);
}

static void print_pll(void)
{
    PLLCTLV2_Type *pll = HPM_PLLCTLV2;
    uint32_t mfi = PLLCTLV2_PLL_MFI_MFI_GET(pll->PLL[PLLCTLV2_PLL_PLL0].MFI);
    uint32_t mfn = PLLCTLV2_PLL_MFN_MFN_GET(pll->PLL[PLLCTLV2_PLL_PLL0].MFN);
    uint32_t mfd = PLLCTLV2_PLL_MFD_MFD_GET(pll->PLL[PLLCTLV2_PLL_PLL0].MFD);
    uint32_t div0 = PLLCTLV2_PLL_DIV_DIV_GET(pll->PLL[PLLCTLV2_PLL_PLL0].DIV[0]);
    uint32_t div1 = PLLCTLV2_PLL_DIV_DIV_GET(pll->PLL[PLLCTLV2_PLL_PLL0].DIV[1]);
    Mcu_PllStatusType st = Mcu_GetPllStatus();

    /* 分频比 = DIV/5 + 1，打印成 x.y（DIV0=5 -> 2.0） */
    printf("PLL0: status=%s MFI=%u MFN=%u MFD=%u DIV0=%u(/%u.%u) DIV1=%u(/%u.%u)\r\n",
           (st == MCU_PLL_LOCKED) ? "LOCKED" : ((st == MCU_PLL_UNLOCKED) ? "UNLOCKED" : "UNDEFINED"),
           (unsigned)mfi, (unsigned)mfn, (unsigned)mfd,
           (unsigned)div0, (unsigned)(div0 / 5u + 1u), (unsigned)((div0 % 5u) * 2u),
           (unsigned)div1, (unsigned)(div1 / 5u + 1u), (unsigned)((div1 % 5u) * 2u));
}

static void print_clock_tree(void)
{
    printf("node      Mcu_GetClockNodeHz   SDK clock_get_frequency\r\n");
    printf("cpu0      %10u Hz        %10u Hz\r\n", (unsigned)Mcu_GetClockNodeHz(0u),
           (unsigned)clock_get_frequency(clock_cpu0));
    printf("ahb0      %10u Hz        %10u Hz\r\n", (unsigned)Mcu_GetClockNodeHz(2u),
           (unsigned)clock_get_frequency(clock_ahb0));
    printf("xpi0      %10u Hz        %10u Hz\r\n", (unsigned)Mcu_GetClockNodeHz(32u),
           (unsigned)clock_get_frequency(clock_xpi0));
    printf("mchtmr0   %10u Hz        %10u Hz   (固定 24 MHz)\r\n", (unsigned)Mcu_GetClockNodeHz(1u),
           (unsigned)clock_get_frequency(clock_mchtmr0));
    printf("core(ext) %10u Hz\r\n", (unsigned)Mcu_GetCoreClockHz());
}

static void cmd_mcu(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Mcu: moduleID=%u SW=%u.%u.%u initialized=%s\r\n", (unsigned)MCU_MODULE_ID,
           (unsigned)MCU_SW_MAJOR_VERSION, (unsigned)MCU_SW_MINOR_VERSION, (unsigned)MCU_SW_PATCH_VERSION,
           Mcu_IsInitialized() ? "yes" : "no");
    printf("config: PLL0 = 24MHz * MFI, cpu0 node=%u\r\n", 0u);
    printf("commands: mcu clock | mcu pll | mcu measure [us] | mcu set 0|1 | mcu ver | mcu err\r\n");
    printf("clock settings: 0 = 480 MHz (PLL0 960/2), 1 = 240 MHz (cpu node /2)\r\n");
}

static void cmd_clock(int argc, char **argv)
{
    (void)argc; (void)argv;
    print_pll();
    print_clock_tree();
}

static void cmd_measure(int argc, char **argv)
{
    uint32_t window = 10000u;
    uint32_t v[5];
    uint32_t cfgHz = Mcu_GetCoreClockHz();
    uint32_t i;
    uint32_t minv, maxv, sum;
    int32_t errPpm;

    if (argc >= 2)
    {
        window = (uint32_t)atoi(argv[1]);
        if ((window < 100u) || (window > 200000u))
        {
            window = 10000u;
        }
    }
    for (i = 0u; i < 5u; i++)
    {
        v[i] = measure_core_hz(window);
    }
    minv = v[0]; maxv = v[0]; sum = 0u;
    for (i = 0u; i < 5u; i++)
    {
        if (v[i] < minv) { minv = v[i]; }
        if (v[i] > maxv) { maxv = v[i]; }
        sum += v[i];
    }
    /* 误差按配置值算，单位 ppm */
    if (cfgHz != 0u)
    {
        errPpm = (int32_t)(((int64_t)(int32_t)((sum / 5u) - cfgHz) * 1000000) / (int64_t)cfgHz);
    }
    else
    {
        errPpm = 0;
    }
    printf("MEAS: window=%u us samples=%u %u %u %u %u\r\n", (unsigned)window,
           (unsigned)v[0], (unsigned)v[1], (unsigned)v[2], (unsigned)v[3], (unsigned)v[4]);
    printf("MEAS: core avg=%u Hz min=%u max=%u cfg=%u Hz err=%d ppm\r\n",
           (unsigned)(sum / 5u), (unsigned)minv, (unsigned)maxv, (unsigned)cfgHz, (int)errPpm);
    printf("MEAS: spread=%u Hz\r\n", (unsigned)(maxv - minv));
}

static void cmd_set(int argc, char **argv)
{
    uint32_t setting;
    Std_ReturnType ret;

    if (argc < 2)
    {
        printf("usage: mset <0|1>\r\n");
        return;
    }
    setting = (uint32_t)atoi(argv[1]);
    if (setting >= MCU_CLOCK_SETTING_COUNT)
    {
        printf("mcu set: invalid setting %u\r\n", (unsigned)setting);
        return;
    }
    ret = Mcu_InitClock((Mcu_ClockType)setting);
    printf("Mcu_InitClock(%u) -> %s, pll=%s\r\n", (unsigned)setting,
           (ret == E_OK) ? "E_OK" : "E_NOT_OK",
           (Mcu_GetPllStatus() == MCU_PLL_LOCKED) ? "LOCKED" : "NOT-LOCKED");
    ret = Mcu_DistributePllClock();
    printf("Mcu_DistributePllClock() -> %s\r\n", (ret == E_OK) ? "E_OK" : "E_NOT_OK");
    printf("after switch: cfg=%u Hz, sdk=%u Hz\r\n", (unsigned)Mcu_GetCoreClockHz(),
           (unsigned)clock_get_frequency(clock_cpu0));
}

static void cmd_ver(int argc, char **argv)
{
    Std_VersionInfoType vi;
    (void)argc; (void)argv;

    Mcu_GetVersionInfo(&vi);
    printf("Mcu_GetVersionInfo: vendor=0x%04X module=%u sw=%u.%u.%u\r\n", (unsigned)vi.vendorID,
           (unsigned)vi.moduleID, (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version);
}

static void cmd_err(int argc, char **argv)
{
    uint32_t before = Det_GetErrorCount();
    (void)argc; (void)argv;

    (void)Mcu_InitClock((Mcu_ClockType)9u);      /* MCU_E_PARAM_CLOCK 0x0B */
    Mcu_SetMode((Mcu_ModeType)9u);               /* MCU_E_PARAM_MODE  0x0C */
    Mcu_GetVersionInfo(NULL_PTR);                /* MCU_E_PARAM_POINTER 0x10 */
    printf("DET: before=%u after=%u delta=%u\r\n", (unsigned)before, (unsigned)Det_GetErrorCount(),
           (unsigned)(Det_GetErrorCount() - before));
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mcu, cmd_mcu, print Mcu module info / clock settings);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mclock, cmd_clock, print PLL + clock tree);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mmeasure, cmd_measure, measure core clock by mchtmr+rdcycle);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mset, cmd_set, mset <0|1>: Mcu_InitClock setting);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 mver, cmd_ver, Mcu_GetVersionInfo);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 merr, cmd_err, inject Dev errors and print Det delta);

int main(void)
{
    Std_ReturnType ret;
    uint32_t cfgHz;
    uint32_t measHz;

    bsp_init();
    Det_Init();

    printf("\r\n=== mcu_clock_init: AUTOSAR Mcu clock/PLL on HPM5E31 ===\r\n");
    Mcu_Init(&Mcu_Config);
    ret = Mcu_InitClock(MCU_CLOCK_SETTING_0);
    printf("Mcu_InitClock(0) -> %s\r\n", (ret == E_OK) ? "E_OK" : "E_NOT_OK");
    printf("Mcu_GetPllStatus() -> %s\r\n",
           (Mcu_GetPllStatus() == MCU_PLL_LOCKED) ? "MCU_PLL_LOCKED" :
           ((Mcu_GetPllStatus() == MCU_PLL_UNLOCKED) ? "MCU_PLL_UNLOCKED" : "MCU_PLL_STATUS_UNDEFINED"));
    ret = Mcu_DistributePllClock();
    printf("Mcu_DistributePllClock() -> %s\r\n", (ret == E_OK) ? "E_OK" : "E_NOT_OK");

    print_pll();
    print_clock_tree();

    cfgHz = Mcu_GetCoreClockHz();
    measHz = measure_core_hz(10000u);
    printf("BOOT MEAS: cfg=%u Hz meas=%u Hz err=%d ppm\r\n", (unsigned)cfgHz, (unsigned)measHz,
           (cfgHz != 0u) ? (int)(((int64_t)((int32_t)(measHz - cfgHz)) * 1000000) / (int64_t)cfgHz) : 0);
    printf("type: mcu / mclock / mmeasure / mset 0|1 / mver / merr / help\r\n");

    while (1)
    {
        bsp_shell_poll();
    }
    return 0;
}
