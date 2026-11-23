/*
 * port_pin_config —— AUTOSAR Port 引脚配置 + SPI1<->SPI3 杜邦线 GPIO 回环（HPM5E31）
 *
 * 接线（板载杜邦线，见 docs/MCAL开发约定.md §2）：
 *   PC11(SPI1 nCS, 输出) <-> PA27(SPI3 nCS, 输入)
 *   PC12(SPI1 MISO, 开漏输出) <-> PA28(SPI3 MISO, 输入，内部上拉)
 *   PC10(SPI1 SCLK, 输出) <-> PA26(SPI3 SCLK, 输入)
 *
 * 验证点：
 *   1) Port_Init 一次性配置 16 个引脚（方向/上下拉/开漏/驱动强度）；
 *   2) 输出->输入回环：LFSR 随机图案 + 走 1 + 全 0/全 1，统计误码；
 *   3) 上拉/下拉：对端设为高阻后，内部上拉读 1、下拉读 0；
 *   4) 开漏：OD 输出 1（释放）时靠对端上拉读 1，输出 0 时被拉低；
 *   5) 运行时改方向 / 刷新方向（Port_SetPinDirection / Port_RefreshPortDirection）；
 *   6) 开发错误路径 → Det 计数增加。
 *
 * 例程：samples/mcal/port_pin_config；章节：docs/chapters/44-mcal-core.md
 */
#include "bsp.h"
#include "Port.h"
#include "Dio.h"
#include "Dio_PBcfg.h"
#include "Port_PBcfg.h"
#include "Det.h"
#include <stdlib.h>

static uint32_t g_lfsr = 0xACE1u;

static uint8_t next_bit(void)
{
    /* 16 位 LFSR，x^16+x^14+x^13+x^11+1 */
    uint32_t bit = ((g_lfsr >> 0) ^ (g_lfsr >> 2) ^ (g_lfsr >> 3) ^ (g_lfsr >> 5)) & 1u;
    g_lfsr = (g_lfsr >> 1) | (bit << 15);
    return (uint8_t)(g_lfsr & 1u);
}

static void drive(uint8_t level)
{
    Dio_WriteChannel(DIO_CHANNEL_LOOP_OUT, (level != 0u) ? STD_HIGH : STD_LOW);
    Dio_WriteChannel(DIO_CHANNEL_LOOP_SCLK_OUT, (level != 0u) ? STD_HIGH : STD_LOW);
}

static uint32_t loop_once(uint8_t level)
{
    uint32_t errors = 0u;
    drive(level);
    if (Dio_ReadChannel(DIO_CHANNEL_LOOP_IN) != (level != 0u ? STD_HIGH : STD_LOW)) { errors++; }
    if (Dio_ReadChannel(DIO_CHANNEL_LOOP_SCLK_IN) != (level != 0u ? STD_HIGH : STD_LOW)) { errors++; }
    return errors;
}

static void cmd_info(int argc, char **argv)
{
    uint16_t i;
    (void)argc; (void)argv;

    printf("Port: moduleID=%u pins=%u\r\n", (unsigned)PORT_MODULE_ID, (unsigned)Port_GetPinCount());
    printf("idx name(pad)      dir mode pull od init\r\n");
    for (i = 0u; i < Port_GetPinCount(); i++)
    {
        const Port_PinConfigType *p = Port_GetPinConfig((Port_PinType)i);
        printf("%2u  pad=%-3u gpio%c%02u  %s  %u   %u    %u   %u\r\n", (unsigned)i, (unsigned)p->padIndex,
               (char)('A' + p->gpioPort), (unsigned)p->gpioPin,
               (p->direction == PORT_PIN_OUT) ? "OUT" : "IN ", (unsigned)p->mode,
               (unsigned)p->pull, (unsigned)p->openDrain, (unsigned)p->initialLevel);
    }
    printf("commands: pinfo | ploop [n] | ppull | pod | pdir | prefresh | perr | pver\r\n");
}

static void cmd_loop(int argc, char **argv)
{
    uint32_t n = 1000u;
    uint32_t i;
    uint32_t err = 0u;

    if (argc >= 2)
    {
        n = (uint32_t)atoi(argv[1]);
        if ((n == 0u) || (n > 1000000u)) { n = 1000u; }
    }
    /* 全 0 / 全 1 定值，再随机图案 */
    err += loop_once(0u);
    err += loop_once(1u);
    for (i = 0u; i < n; i++)
    {
        err += loop_once(next_bit());
    }
    printf("LOOP: n=%u patterns=2+%u errors=%u -> %s\r\n", (unsigned)n, (unsigned)n, (unsigned)err,
           (err == 0u) ? "PASS" : "FAIL");
}

/* 对一组"输出->输入"回环线做上拉/下拉测试：输出端设输入(高阻)，输入端换内部上下拉 */
static uint8_t pull_pair(Port_PinType outPin, Dio_ChannelType inCh, Port_PinType inPin, const char *name)
{
    uint8_t up, down, ok;
    uint8_t padIndex = (uint8_t)Port_GetPinConfig(inPin)->padIndex;

    Port_SetPinDirection(outPin, PORT_PIN_IN);
    Port_SetPinPull(inPin, PORT_PIN_PULL_UP);
    board_delay_us(200u);        /* 等 pad 电容按 47k/100k 充放电稳定 */
    up = (Dio_ReadChannel(inCh) == STD_HIGH) ? 1u : 0u;
    Port_SetPinPull(inPin, PORT_PIN_PULL_DOWN);
    board_delay_us(200u);
    down = (Dio_ReadChannel(inCh) == STD_HIGH) ? 1u : 0u;
    ok = ((up == 1u) && (down == 0u)) ? 1u : 0u;
    printf("PULL: %s pull-up read=%u pull-down read=%u PAD_CTL=0x%08X -> %s\r\n", name, (unsigned)up,
           (unsigned)down, (unsigned)HPM_IOC->PAD[padIndex].PAD_CTL, ok ? "PASS" : "FAIL");
    Port_SetPinPull(inPin, PORT_PIN_PULL_NONE);
    Port_SetPinDirection(outPin, PORT_PIN_OUT);
    return ok;
}

static void cmd_pull(int argc, char **argv)
{
    uint8_t a, b;
    (void)argc; (void)argv;

    a = pull_pair(PORT_PIN_LOOP_CS_OUT, DIO_CHANNEL_LOOP_IN, PORT_PIN_LOOP_CS_IN, "PA27");
    b = pull_pair(PORT_PIN_LOOP_SCLK_OUT, DIO_CHANNEL_LOOP_SCLK_IN, PORT_PIN_LOOP_SCLK_IN, "PA26");
    printf("PULL: summary %s\r\n", ((a == 1u) && (b == 1u)) ? "PASS" : "FAIL");
}

static void cmd_od(int argc, char **argv)
{
    uint8_t released, low;
    (void)argc; (void)argv;

    /* PC12 开漏：写 1 = 释放（对端 PA28 内部上拉读 1），写 0 = 拉低 */
    Dio_WriteChannel(DIO_CHANNEL_LOOP_OD_OUT, STD_HIGH);
    released = (Dio_ReadChannel(DIO_CHANNEL_LOOP_OD_IN) == STD_HIGH) ? 1u : 0u;
    Dio_WriteChannel(DIO_CHANNEL_LOOP_OD_OUT, STD_LOW);
    low = (Dio_ReadChannel(DIO_CHANNEL_LOOP_OD_IN) == STD_LOW) ? 1u : 0u;
    Dio_WriteChannel(DIO_CHANNEL_LOOP_OD_OUT, STD_HIGH);
    printf("OD: PC12 released -> PA28=%u, PC12 low -> PA28=%u -> %s\r\n", (unsigned)released,
           (unsigned)low, ((released == 1u) && (low == 1u)) ? "PASS" : "FAIL");
}

static void cmd_dir(int argc, char **argv)
{
    (void)argc; (void)argv;
    Port_SetPinDirection(PORT_PIN_LOOP_CS_OUT, PORT_PIN_IN);
    Port_SetPinDirection(PORT_PIN_LOOP_CS_OUT, PORT_PIN_OUT);
    Port_RefreshPortDirection();
    printf("DIR: Port_SetPinDirection IN->OUT + Port_RefreshPortDirection done\r\n");
}

static void cmd_refresh(int argc, char **argv)
{
    (void)argc; (void)argv;
    Port_RefreshPortDirection();
    printf("REFRESH: 只刷新 directionChangeable=0 的引脚（CAN 灯/按键）\r\n");
}

static void cmd_ver(int argc, char **argv)
{
    Std_VersionInfoType vi;
    (void)argc; (void)argv;
    Port_GetVersionInfo(&vi);
    printf("Port_GetVersionInfo: vendor=0x%04X module=%u sw=%u.%u.%u\r\n", (unsigned)vi.vendorID,
           (unsigned)vi.moduleID, (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version);
}

static void cmd_err(int argc, char **argv)
{
    uint32_t before = Det_GetErrorCount();
    (void)argc; (void)argv;

    Port_SetPinDirection((Port_PinType)0xFFFFu, PORT_PIN_OUT);      /* PORT_E_PARAM_PIN */
    Port_SetPinDirection(PORT_PIN_LOOP_CS_OUT, (Port_PinDirectionType)7); /* PORT_E_PARAM_INVALID_DIRECTION */
    Port_SetPinMode(PORT_PIN_LOOP_CS_OUT, (Port_PinModeType)99u);   /* PORT_E_PARAM_INVALID_MODE */
    Port_SetPinDirection(PORT_PIN_CAN_LED0, PORT_PIN_IN);           /* PORT_E_DIRECTION_UNCHANGEABLE */
    Port_GetVersionInfo(NULL_PTR);                                  /* PORT_E_PARAM_POINTER */
    printf("DET: before=%u after=%u delta=%u\r\n", (unsigned)before, (unsigned)Det_GetErrorCount(),
           (unsigned)(Det_GetErrorCount() - before));
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 pinfo, cmd_info, Port pin table);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ploop, cmd_loop, ploop [n]: GPIO loopback PC11->PA27 / PC10->PA26);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 ppull, cmd_pull, internal pull-up/pull-down test);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 pod, cmd_od, open-drain test PC12->PA28);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 pdir, cmd_dir, runtime direction change);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 prefresh, cmd_refresh, Port_RefreshPortDirection);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 perr, cmd_err, inject Dev errors);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 pver, cmd_ver, Port_GetVersionInfo);

int main(void)
{
    uint32_t err;

    bsp_init();
    Det_Init();

    printf("\r\n=== port_pin_config: AUTOSAR Port on HPM5E31 ===\r\n");
    Port_Init(&Port_Config);
    printf("Port_Init done, pins=%u initialized=%s\r\n", (unsigned)Port_GetPinCount(),
           Port_IsInitialized() ? "yes" : "no");

    /* 上电自检：先把回环线跑一遍，结果直接打在串口 */
    err = loop_once(0u) + loop_once(1u);
    printf("SELFTEST: loopback errors=%u -> %s\r\n", (unsigned)err, (err == 0u) ? "PASS" : "FAIL");
    printf("type: pinfo / ploop 1000 / ppull / pod / pdir / prefresh / perr / pver / help\r\n");

    while (1)
    {
        bsp_shell_poll();
    }
    return 0;
}
