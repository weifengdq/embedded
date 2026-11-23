/*
 * dio_led_button —— AUTOSAR Dio 读写 + 通道组 + 错误路径（HPM5E31）
 *
 * 硬件：USR LED PA02（高亮）、USR BTN PA03（按下为高）、8 颗 CAN 状态灯
 *       PF26/PF18、PF20/PF16、PF22/PF21、PF23/PF19（高亮）。
 *
 * 注意：AUTOSAR 的 Dio 没有 Dio_Init：先用 Port_Init 配置引脚方向/上下拉，
 *       再用 Dio_Read/Write/Flip/ReadPort/WritePort/ReadChannelGroup/WriteChannelGroup。
 *
 * 验证点：
 *   1) Dio_WriteChannel/FlipChannel/ReadChannel：USR LED；
 *   2) Dio_ReadChannel：USR BTN 轮询 + 软件消抖；
 *   3) Dio_WriteChannelGroup/ReadChannelGroup：CAN 灯的 GPIOF[23:16] 图案；
 *   4) Dio_ReadPort/WritePort/MaskedWritePort：整端口读写；
 *   5) 错误路径：非法 channel/port/group/空指针 → Det 计数增加。
 *
 * 例程：samples/mcal/dio_led_button；章节：docs/chapters/44-mcal-core.md
 */
#include "bsp.h"
#include "Port.h"
#include "Port_PBcfg.h"
#include "Dio.h"
#include "Dio_PBcfg.h"
#include "Det.h"
#include <stdlib.h>
#include <string.h>

static uint8_t g_led = 0u;
static uint32_t g_btn_events = 0u;

static void leds_write(uint32_t pattern8)
{
    /* 8 颗 CAN 灯分散在 GPIOF 的 8 个位上，逐个写；同时演示通道组写 */
    static const Dio_ChannelType ch[8] =
    {
        DIO_CHANNEL_CAN_LED0, DIO_CHANNEL_CAN_LED1, DIO_CHANNEL_CAN_LED2, DIO_CHANNEL_CAN_LED3,
        DIO_CHANNEL_CAN_LED4, DIO_CHANNEL_CAN_LED5, DIO_CHANNEL_CAN_LED6, DIO_CHANNEL_CAN_LED7
    };
    uint32_t i;

    for (i = 0u; i < 8u; i++)
    {
        Dio_WriteChannel(ch[i], ((pattern8 >> i) & 1u) ? STD_HIGH : STD_LOW);
    }
}

static void cmd_info(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Dio: moduleID=%u channels=%u groups=%u (Dio 无 Init，引脚由 Port_Init 配)\r\n",
           (unsigned)DIO_MODULE_ID, (unsigned)Dio_GetChannelCount(), (unsigned)Dio_GetGroupCount());
    printf("ch0=PA02 LED ch1=PA03 BTN ch2=PC11 ch3=PA27 ch4..11=CAN LEDs ch12=PC12 ch13=PA28 ch14=PC10 ch15=PA26\r\n");
    printf("commands: dio | dled <on|off|flip> | dbtn | dscan | dleds <hex> | dport <0..5> | dgroup | dmasked <lvl> <mask> | derr | dver\r\n");
}

static void cmd_led(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: dled <on|off|flip>\r\n");
        return;
    }
    if (strcmp(argv[1], "on") == 0)
    {
        Dio_WriteChannel(DIO_CHANNEL_USR_LED, STD_HIGH);
        g_led = 1u;
    }
    else if (strcmp(argv[1], "off") == 0)
    {
        Dio_WriteChannel(DIO_CHANNEL_USR_LED, STD_LOW);
        g_led = 0u;
    }
    else
    {
        g_led = (Dio_FlipChannel(DIO_CHANNEL_USR_LED) == STD_HIGH) ? 1u : 0u;
    }
    printf("LED: write=%u readback=%u\r\n", (unsigned)g_led,
           (unsigned)((Dio_ReadChannel(DIO_CHANNEL_USR_LED) == STD_HIGH) ? 1u : 0u));
}

static void cmd_btn(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("BTN: PA03=%u (pressed=high) events=%u\r\n",
           (unsigned)((Dio_ReadChannel(DIO_CHANNEL_USR_BTN) == STD_HIGH) ? 1u : 0u), (unsigned)g_btn_events);
}

static void cmd_scan(int argc, char **argv)
{
    uint32_t i;
    (void)argc; (void)argv;

    printf("SCAN: 8 颗 CAN 灯走 1\r\n");
    for (i = 0u; i < 8u; i++)
    {
        leds_write((uint32_t)1u << i);
        board_delay_ms(120u);
    }
    leds_write(0u);
    printf("SCAN: done\r\n");
}

static void cmd_leds(int argc, char **argv)
{
    uint32_t v;
    if (argc < 2)
    {
        printf("usage: dleds <hex 0..FF>\r\n");
        return;
    }
    v = (uint32_t)strtoul(argv[1], NULL, 16);
    leds_write(v & 0xFFu);
    printf("LEDS: pattern=0x%02X DO(GPIOF)=0x%08X\r\n", (unsigned)(v & 0xFFu),
           (unsigned)HPM_GPIO0->DO[DIO_PORT_GPIOF].VALUE);
    printf("LEDS: DI readback: ch4(PF26)=%u ch5(PF18)=%u ch6(PF20)=%u ch7(PF16)=%u ch8(PF22)=%u ch9(PF21)=%u ch10(PF23)=%u ch11(PF19)=%u\r\n",
           (unsigned)((Dio_ReadChannel(DIO_CHANNEL_CAN_LED0) == STD_HIGH) ? 1u : 0u),
           (unsigned)((Dio_ReadChannel(DIO_CHANNEL_CAN_LED1) == STD_HIGH) ? 1u : 0u),
           (unsigned)((Dio_ReadChannel(DIO_CHANNEL_CAN_LED2) == STD_HIGH) ? 1u : 0u),
           (unsigned)((Dio_ReadChannel(DIO_CHANNEL_CAN_LED3) == STD_HIGH) ? 1u : 0u),
           (unsigned)((Dio_ReadChannel(DIO_CHANNEL_CAN_LED4) == STD_HIGH) ? 1u : 0u),
           (unsigned)((Dio_ReadChannel(DIO_CHANNEL_CAN_LED5) == STD_HIGH) ? 1u : 0u),
           (unsigned)((Dio_ReadChannel(DIO_CHANNEL_CAN_LED6) == STD_HIGH) ? 1u : 0u),
           (unsigned)((Dio_ReadChannel(DIO_CHANNEL_CAN_LED7) == STD_HIGH) ? 1u : 0u));
}

static void cmd_port(int argc, char **argv)
{
    uint32_t port = 5u;
    if (argc >= 2)
    {
        port = (uint32_t)atoi(argv[1]);
        if (port > 5u) { port = 5u; }
    }
    printf("PORT%u: DI=0x%08X DO=0x%08X\r\n", (unsigned)port,
           (unsigned)Dio_ReadPort((Dio_PortType)port),
           (unsigned)((HPM_GPIO0->DO[port].VALUE)));
}

static void cmd_group(int argc, char **argv)
{
    uint32_t v = Dio_ReadChannelGroup(&Dio_ChannelGroup_Usr);
    uint32_t w = Dio_ReadChannelGroup(&Dio_ChannelGroup_CanLeds);
    (void)argc; (void)argv;
    printf("GROUP Usr  (GPIOA[3:2]) = 0x%X (bit0=PA02 LED bit1=PA03 BTN)\r\n", (unsigned)v);
    printf("GROUP CanLeds(GPIOF[23:16]) = 0x%02X (位号=引脚号-16，不是 LED 序号：PF26/PF19 不在组内)\r\n",
           (unsigned)w);
}

static void cmd_masked(int argc, char **argv)
{
    uint32_t level, mask;
    if (argc < 3)
    {
        printf("usage: dmasked <level> <mask>  目标端口 GPIOF\r\n");
        return;
    }
    level = (uint32_t)strtoul(argv[1], NULL, 0);
    mask = (uint32_t)strtoul(argv[2], NULL, 0);
    Dio_MaskedWritePort(DIO_PORT_GPIOF, level, mask);
    printf("MASKED: GPIOF DO=0x%08X (level=0x%X mask=0x%X)\r\n",
           (unsigned)HPM_GPIO0->DO[DIO_PORT_GPIOF].VALUE, (unsigned)level, (unsigned)mask);
}

static void cmd_ver(int argc, char **argv)
{
    Std_VersionInfoType vi;
    (void)argc; (void)argv;
    Dio_GetVersionInfo(&vi);
    printf("Dio_GetVersionInfo: vendor=0x%04X module=%u sw=%u.%u.%u\r\n", (unsigned)vi.vendorID,
           (unsigned)vi.moduleID, (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version);
}

static void cmd_err(int argc, char **argv)
{
    uint32_t before = Det_GetErrorCount();
    Dio_ChannelGroupType badGroup = { .port = DIO_PORT_GPIOF, .offset = 0u, .mask = 0x7u };
    (void)argc; (void)argv;

    (void)Dio_ReadChannel((Dio_ChannelType)0xFFFFu);              /* DIO_E_PARAM_INVALID_CHANNEL_ID */
    Dio_WriteChannel((Dio_ChannelType)0xFFFFu, STD_HIGH);         /* 同上 */
    (void)Dio_ReadPort((Dio_PortType)99u);                        /* DIO_E_PARAM_INVALID_PORT_ID */
    (void)Dio_ReadChannelGroup(NULL_PTR);                         /* DIO_E_PARAM_POINTER */
    (void)Dio_ReadChannelGroup(&badGroup);                        /* DIO_E_PARAM_INVALID_GROUP（未在配置里声明） */
    Dio_GetVersionInfo(NULL_PTR);                                 /* DIO_E_PARAM_POINTER */
    printf("DET: before=%u after=%u delta=%u\r\n", (unsigned)before, (unsigned)Det_GetErrorCount(),
           (unsigned)(Det_GetErrorCount() - before));
}

/* 性能：mchtmr(24 MHz) 计时，测 Dio_WriteChannel / Dio_ReadChannel 的单次开销 */
static void cmd_perf(int argc, char **argv)
{
    MCHTMR_Type *tmr = HPM_MCHTMR;
    uint32_t n = 1000000u;
    uint32_t i;
    uint64_t t0, t1, t2, t3;
    uint64_t nsW, nsR;
    volatile uint32_t acc = 0u;

    if (argc >= 2)
    {
        n = (uint32_t)atoi(argv[1]);
        if ((n < 1000u) || (n > 50000000u)) { n = 1000000u; }
    }
    t0 = tmr->MTIME;
    for (i = 0u; i < n; i++)
    {
        Dio_WriteChannel(DIO_CHANNEL_USR_LED, ((i & 1u) != 0u) ? STD_HIGH : STD_LOW);
    }
    t1 = tmr->MTIME;
    t2 = tmr->MTIME;
    for (i = 0u; i < n; i++)
    {
        acc += Dio_ReadChannel(DIO_CHANNEL_USR_BTN);
    }
    t3 = tmr->MTIME;
    /* 1 mchtmr tick = 1/24 MHz = 41.666 ns = 125/3 ns */
    nsW = (((t1 - t0) * 125u) / 3u) / n;
    nsR = (((t3 - t2) * 125u) / 3u) / n;
    printf("PERF: n=%u write: ticks=%u total=%uns ns/op=%u ; read: ticks=%u total=%uns ns/op=%u (acc=%u)\r\n",
           (unsigned)n, (unsigned)(t1 - t0), (unsigned)(((t1 - t0) * 125u) / 3u), (unsigned)nsW,
           (unsigned)(t3 - t2), (unsigned)(((t3 - t2) * 125u) / 3u), (unsigned)nsR, (unsigned)acc);
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dio, cmd_info, Dio module info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dled, cmd_led, dled <on|off|flip>: USR LED PA02);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dbtn, cmd_btn, read USR BTN PA03);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dscan, cmd_scan, walking-one on 8 CAN LEDs);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dleds, cmd_leds, dleds <hex>: 8 CAN LEDs pattern);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dport, cmd_port, dport <0..5>: Dio_ReadPort);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dgroup, cmd_group, Dio_ReadChannelGroup);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dmasked, cmd_masked, dmasked <level> <mask>: Dio_MaskedWritePort on GPIOF);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dperf, cmd_perf, dperf [n]: per-call cost of Dio Write/Read);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 derr, cmd_err, inject Dev errors and print Det delta);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dver, cmd_ver, Dio_GetVersionInfo);

int main(void)
{
    uint8_t btn;
    uint8_t btn_stable = 0u;
    uint32_t stable_cnt = 0u;

    bsp_init();
    Det_Init();

    printf("\r\n=== dio_led_button: AUTOSAR Dio on HPM5E31 ===\r\n");
    Port_Init(&Port_Config);       /* Dio 之前必须先 Port_Init（AUTOSAR 分工） */
    printf("Port_Init done; Dio channels=%u groups=%u\r\n", (unsigned)Dio_GetChannelCount(),
           (unsigned)Dio_GetGroupCount());

    /* 上电自检：LED 亮 -> 读回 -> 灭 */
    Dio_WriteChannel(DIO_CHANNEL_USR_LED, STD_HIGH);
    printf("SELFTEST: LED write=1 readback=%u\r\n",
           (unsigned)((Dio_ReadChannel(DIO_CHANNEL_USR_LED) == STD_HIGH) ? 1u : 0u));
    board_delay_ms(200u);
    Dio_WriteChannel(DIO_CHANNEL_USR_LED, STD_LOW);
    leds_write(0u);
    printf("type: dio / dled on|off|flip / dbtn / dscan / dleds FF / dport 5 / dgroup / dmasked 0 0xFF / derr / dver\r\n");

    while (1)
    {
        bsp_shell_poll();

        /* 按键消抖：连续 5 次采样一致才认 */
        btn = (Dio_ReadChannel(DIO_CHANNEL_USR_BTN) == STD_HIGH) ? 1u : 0u;
        if (btn == btn_stable)
        {
            stable_cnt++;
        }
        else
        {
            stable_cnt = 0u;
            btn_stable = btn;
        }
        if ((stable_cnt == 5u) && (btn_stable == 1u))
        {
            stable_cnt = 0u;
            g_btn_events++;
            g_led ^= 1u;
            Dio_WriteChannel(DIO_CHANNEL_USR_LED, (g_led != 0u) ? STD_HIGH : STD_LOW);
            printf("BTN: pressed, events=%u, LED=%u\r\n", (unsigned)g_btn_events, (unsigned)g_led);
        }
    }
    return 0;
}
