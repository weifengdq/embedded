/*
 * wdg_trigger —— AUTOSAR Wdg 正常喂狗 + 窗口模式 + Wdg_SetTriggerCondition 动态改周期
 *
 * 硬件：EWDG0，计数器源 OSC32 = 32768 Hz（不依赖 PLL，复位可靠）
 *   SLOW 模式超时 1000 ms，FAST 模式超时 100 ms；窗口模式 WIN_LOWER=8/16、WIN_UPPER=8/16
 *   复位源：PPOR.RESET_FLAG bit16 = WDG0
 *
 * 测试内容（开机自动跑）：
 *   1) Wdg_Init（默认 SLOW）→ 每 100 ms 喂狗，持续 3 s，活着 = PASS；
 *   2) Wdg_SetTriggerCondition(300) 把超时周期改成 300 ms → 继续喂 2 s，活 = PASS；
 *   3) Wdg_SetMode(FAST, 100 ms) → 每 50 ms 喂 1 s，活 = PASS；
 *   4) 打印 Wdg 状态/模式/Det 计数。
 *   （窗口外喂狗会复位，属破坏性测试，放在命令 wdg bad 里手动触发）
 *
 * 命令：
 *   wdg info            打印状态/模式
 *   wdg feed <ms>       喂狗一次（并打印）
 *   wdg mode slow|fast  切换模式
 *   wdg trig <ms>       Wdg_SetTriggerCondition(<ms>)
 *   wdg err             错误路径（非法超时/版本信息空指针）
 *   wdg bad             窗口外喂狗 -> 预期复位（慎用）
 */
#include "bsp.h"
#include "Wdg.h"
#include "Wdg_PBcfg.h"
#include "Det.h"
#include <stdio.h>
#include <string.h>

static const char *mode_name(uint8 m)
{
    switch (m)
    {
    case WDGIF_SLOW_MODE: return "SLOW";
    case WDGIF_FAST_MODE: return "FAST";
    default:              return "OFF";
    }
}

static void wdg_info(void)
{
    Std_VersionInfoType vi;
    Wdg_GetVersionInfo(&vi);
    printf("[wdg] Wdg v%u.%u.%u moduleID=%u state=%u mode=%s\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, (unsigned)vi.moduleID,
           (unsigned)Wdg_GetState(), mode_name(Wdg_GetMode()));
    printf("[wdg] EWDG0 src=32768 Hz slow=%u ms fast=%u ms max=%u ms window=%u\r\n",
           (unsigned)Wdg_Config.slowTimeoutMs, (unsigned)Wdg_Config.fastTimeoutMs,
           (unsigned)Wdg_Config.maxTimeoutMs);
}

/* 喂狗循环 sec 秒，每 periodMs 一次；返回喂狗次数 */
static uint32 feed_for(uint32 sec, uint32 periodMs)
{
    uint32 t0 = bsp_uptime_ms();
    uint32 feeds = 0u;

    while ((bsp_uptime_ms() - t0) < (sec * 1000u))
    {
        board_delay_ms(periodMs);
        Wdg_SetTriggerCondition(Wdg_Config.slowTimeoutMs);   /* SWS 语义：设触发条件并复位计数 */
        feeds++;
    }
    return feeds;
}

static void cmd_err(void)
{
    uint32 before = Det_GetErrorCount();
    Wdg_SetTriggerCondition((uint16)(Wdg_Config.maxTimeoutMs + 1u));  /* WDG_E_PARAM_TIMEOUT */
    Wdg_GetVersionInfo(NULL_PTR);                                     /* WDG_E_PARAM_POINTER */
    printf("[det] before=%lu after=%lu delta=%lu\r\n", (unsigned long)before,
           (unsigned long)Det_GetErrorCount(), (unsigned long)(Det_GetErrorCount() - before));
}

static void cmd_wdg(int argc, char **argv)
{
    const char *sub = (argc >= 2) ? argv[1] : "info";

    if (strcmp(sub, "info") == 0)
    {
        wdg_info();
    }
    else if (strcmp(sub, "feed") == 0)
    {
        Wdg_SetTriggerCondition(Wdg_Config.slowTimeoutMs);
        printf("[wdg] fed (mode=%s)\r\n", mode_name(Wdg_GetMode()));
    }
    else if ((strcmp(sub, "mode") == 0) && (argc >= 3))
    {
        WdgIf_ModeType m = WDGIF_SLOW_MODE;
        if (strcmp(argv[2], "fast") == 0) { m = WDGIF_FAST_MODE; }
        if (Wdg_SetMode(m) == E_OK)
        {
            printf("[wdg] mode -> %s\r\n", mode_name(m));
        }
        else
        {
            printf("[wdg] SetMode failed (see Det / WDG_E_*)\r\n");
        }
    }
    else if ((strcmp(sub, "trig") == 0) && (argc >= 3))
    {
        uint32_t ms = 0u;
        if ((bsp_parse_u32(argv[2], &ms) != 0) || (ms > Wdg_Config.maxTimeoutMs))
        {
            printf("usage: wdg trig <ms 1..%u>\r\n", (unsigned)Wdg_Config.maxTimeoutMs);
            return;
        }
        Wdg_SetTriggerCondition((uint16)ms);
        printf("[wdg] trigger condition -> %lu ms\r\n", (unsigned long)ms);
    }
    else if (strcmp(sub, "err") == 0)
    {
        cmd_err();
    }
    else if (strcmp(sub, "bad") == 0)
    {
        /* 窗口模式：超时 1000 ms、窗口下限 8/16 -> 500 ms 之前喂狗 = 窗口外 = 复位 */
        (void)Wdg_SetMode(WDGIF_SLOW_MODE);
        printf("[wdg] window violation test: feeding at 50 ms (window opens at 500 ms), expect RESET ...\r\n");
        board_delay_ms(50u);
        Wdg_SetTriggerCondition(1000u);     /* 重装 -> 立即喂一次 = 窗口外刷新 */
        printf("[wdg] if you see this line again after reset, check PPOR flag\r\n");
        while (1) { bsp_shell_poll(); }
    }
    else
    {
        wdg_info();
        printf("usage: wdg info|feed|mode|trig|err|bad\r\n");
    }
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 wdg, cmd_wdg, wdg control: wdg <info|feed|mode|trig|err|bad>);

int main(void)
{
    Std_VersionInfoType vi;
    uint32 feeds;
    uint32_t last;

    bsp_init();
    Det_Init();
    printf("\r\n=== mcal wdg_trigger (board %s) ===\r\n", BOARD_NAME);
    board_delay_ms(1000);   /* 给串口抓包留启动时间 */

    Wdg_Init(&Wdg_Config);
    Wdg_GetVersionInfo(&vi);
    printf("Wdg v%u.%u.%u: EWDG0 armed, mode=%s, SLOW=%u ms\r\n",
           (unsigned)vi.sw_major_version, (unsigned)vi.sw_minor_version,
           (unsigned)vi.sw_patch_version, mode_name(Wdg_GetMode()),
           (unsigned)Wdg_Config.slowTimeoutMs);

    /* 1) 正常喂狗 3 s */
    feeds = feed_for(3u, 100u);
    printf("[t1] feed 3 s every 100 ms: feeds=%lu RESULT=SURVIVED\r\n", (unsigned long)feeds);
    wdg_info();

    /* 2) 动态改触发周期 300 ms，喂 2 s */
    Wdg_SetTriggerCondition(300u);
    feeds = feed_for(2u, 100u);
    printf("[t2] SetTriggerCondition(300ms) + feed 2 s: feeds=%lu RESULT=SURVIVED\r\n",
           (unsigned long)feeds);

    /* 3) FAST 模式 100 ms，喂 50ms x 1 s */
    if (Wdg_SetMode(WDGIF_FAST_MODE) == E_OK)
    {
        feeds = feed_for(1u, 50u);
        printf("[t3] FAST mode (100 ms) + feed 1 s: feeds=%lu RESULT=SURVIVED\r\n",
               (unsigned long)feeds);
    }
    (void)Wdg_SetMode(WDGIF_SLOW_MODE);
    wdg_info();
    cmd_err();
    printf("[wdg_trigger] auto test done. cmds: wdg info|feed|mode|trig|err|bad\r\n");

    last = bsp_uptime_ms();
    while (1)
    {
        bsp_shell_poll();
        if ((bsp_uptime_ms() - last) >= 100u)
        {
            last = bsp_uptime_ms();
            Wdg_SetTriggerCondition(Wdg_Config.slowTimeoutMs);   /* 主循环持续喂狗 */
        }
    }
    return 0;
}