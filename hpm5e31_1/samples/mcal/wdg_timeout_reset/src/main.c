/*
 * wdg_timeout_reset —— AUTOSAR Wdg 超时复位实测：复位源识别 + 跨复位启动计数
 *
 * 硬件：EWDG0，计数器源 OSC32 32768 Hz；复位源 = EWDG0 timeout -> PPOR.RESET_FLAG bit16
 *
 * 演示流程（两次 hwrun 配合，因为 SoC 复位会让 USB 串口重新枚举，单次抓包跨不过复位）：
 *   第 1 次（烧写）：BOOT#n 冷启动，打印复位标志=0 -> Wdg_Init + SetTriggerCondition(500)
 *                    后**不喂狗** -> 约 500 ms 后 EWDG0 复位；日志停在 "expect reset"。
 *   第 2 次（--no-flash，不重烧）：板子已经在看门狗复位后重新启动，固件看到
 *                    PPOR.RESET_FLAG 的 WDG0 位=1 -> 打印 BOOT#(n+1)、cause=1，
 *                    然后进入 shell 模式（主循环持续喂狗，不再复位）。
 *   启动计数放在 .noncacheable.non_init 段（NOLOAD，复位不清零）+ magic 判冷启动。
 *
 * 命令（从看门狗复位回来后可用）：
 *   wdg cause          打印本次启动读到的 PPOR 复位标志
 *   wdg info           打印 BOOT#/状态/模式
 *   wdg reset [ms]     装载超时后不喂狗 -> 再次触发复位（默认 200 ms）
 *   wdg feed <sec>     喂狗 sec 秒（验证不会复位）
 */
#include "bsp.h"
#include "Wdg.h"
#include "Wdg_PBcfg.h"
#include "Det.h"
#include "hpm_common.h"
#include "hpm_ppor_drv.h"
#include <stdio.h>
#include <string.h>

#define RESET_SOURCE_WDG0   (1UL << 16)
#define BOOT_MAGIC          (0x5A5A0000uL)

typedef struct
{
    uint32 magic;
    uint32 count;
} boot_cnt_t;

ATTR_PLACE_AT_NONCACHEABLE_NON_INIT static boot_cnt_t s_boot;

static uint32 s_resetFlag;

static void print_cause(void)
{
    printf("[wdg] PPOR.RESET_FLAG at this boot = 0x%08lx (WDG0 bit16 = %lu)\r\n",
           (unsigned long)s_resetFlag,
           (unsigned long)((s_resetFlag & RESET_SOURCE_WDG0) ? 1u : 0u));
    if ((s_resetFlag & RESET_SOURCE_WDG0) != 0u)
    {
        printf("[wdg] => 本次启动由 EWDG0 超时复位引起（复位源已定位）\r\n");
    }
    else if (s_resetFlag == 0u)
    {
        printf("[wdg] => 无复位标志（首次上电 / J-Link 烧写复位）\r\n");
    }
    else
    {
        printf("[wdg] => 其它复位源（非 EWDG0）\r\n");
    }
}

static void cmd_wdg(int argc, char **argv)
{
    const char *sub = (argc >= 2) ? argv[1] : "info";

    if (strcmp(sub, "cause") == 0)
    {
        print_cause();
    }
    else if (strcmp(sub, "reset") == 0)
    {
        uint32_t ms = 200u;
        if (argc >= 3)
        {
            (void)bsp_parse_u32(argv[2], &ms);
        }
        printf("[wdg] arm %lu ms and stop feeding -> expect reset ...\r\n", (unsigned long)ms);
        Wdg_SetTriggerCondition((uint16)ms);
        while (1) { bsp_shell_poll(); }   /* 不喂狗，等复位 */
    }
    else if ((strcmp(sub, "feed") == 0) && (argc >= 3))
    {
        uint32_t sec = 0u;
        uint32 t0, feeds = 0u;
        if ((bsp_parse_u32(argv[2], &sec) != 0) || (sec == 0u) || (sec > 60u))
        {
            printf("usage: wdg feed <sec 1..60>\r\n");
            return;
        }
        t0 = bsp_uptime_ms();
        while ((bsp_uptime_ms() - t0) < (sec * 1000u))
        {
            board_delay_ms(100u);
            Wdg_SetTriggerCondition(Wdg_Config.slowTimeoutMs);
            feeds++;
        }
        printf("[wdg] fed %lu times over %lu s -> SURVIVED\r\n", (unsigned long)feeds, (unsigned long)sec);
    }
    else
    {
        printf("[wdg] BOOT#%lu, RESET_FLAG=0x%08lx, state=%u, mode=%u, slow=%u ms\r\n",
               (unsigned long)s_boot.count, (unsigned long)s_resetFlag,
               (unsigned)Wdg_GetState(), (unsigned)Wdg_GetMode(), (unsigned)Wdg_Config.slowTimeoutMs);
        printf("usage: wdg cause|info|reset [ms]|feed <sec>\r\n");
    }
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 wdg, cmd_wdg, wdg reset demo: wdg <cause|info|reset [ms]|feed <sec>>);

int main(void)
{
    uint32_t last;

    bsp_init();
    Det_Init();
    board_delay_ms(1200u);   /* 等串口抓包挂上（hwrun 在烧写后 0.3 s 开始抓） */

    if (s_boot.magic != BOOT_MAGIC)
    {
        s_boot.magic = BOOT_MAGIC;
        s_boot.count = 0u;
    }
    s_boot.count++;
    s_resetFlag = ppor_reset_get_flags(HPM_PPOR);
    ppor_reset_clear_flags(HPM_PPOR, 0xFFFFFFFFuL);

    printf("\r\n=== mcal wdg_timeout_reset (board %s) ===\r\n", BOARD_NAME);
    printf("[boot] BOOT#%lu (non-init RAM counter)  PPOR.RESET_FLAG=0x%08lx  WDG0_reset=%lu\r\n",
           (unsigned long)s_boot.count, (unsigned long)s_resetFlag,
           (unsigned long)((s_resetFlag & RESET_SOURCE_WDG0) ? 1u : 0u));
    print_cause();

    if ((s_resetFlag & RESET_SOURCE_WDG0) == 0u)
    {
        /* 冷启动：证明"不喂狗 -> 复位" */
        Wdg_Init(&Wdg_Config);
        Wdg_SetTriggerCondition(500u);
        printf("[auto] BOOT#%lu: EWDG0 armed 500 ms, NOT feeding -> EWDG0 timeout reset expected\r\n",
               (unsigned long)s_boot.count);
        while (1)
        {
        }
    }

    /* 从看门狗复位回来：复位源已确认。
     * 因为 SoC 复位会让 /dev/ttyACM0 重新枚举，同一次 hwrun 的抓包会断掉，
     * 所以这里把"复位源 + 启动计数"连续打印 20 s，让紧随其后的第二次 hwrun 一定能抓到。 */
    Wdg_Init(&Wdg_Config);
    {
        uint32 t0 = bsp_uptime_ms();
        while ((bsp_uptime_ms() - t0) < 20000u)
        {
            board_delay_ms(500u);
            Wdg_SetTriggerCondition(Wdg_Config.slowTimeoutMs);
            printf("[post-reset] BOOT#%lu PPOR.RESET_FLAG=0x%08lx WDG0_reset=%lu (%lu s)\r\n",
                   (unsigned long)s_boot.count, (unsigned long)s_resetFlag,
                   (unsigned long)((s_resetFlag & RESET_SOURCE_WDG0) ? 1u : 0u),
                   (unsigned long)((bsp_uptime_ms() - t0) / 1000u));
        }
    }
    printf("[auto] 已从 EWDG0 复位恢复：BOOT#%lu, cause=1 -> 进入 shell 模式（主循环喂狗）\r\n",
           (unsigned long)s_boot.count);
    printf("[auto] cmds: wdg cause | wdg info | wdg reset [ms] | wdg feed <sec>\r\n");

    last = bsp_uptime_ms();
    while (1)
    {
        bsp_shell_poll();
        if ((bsp_uptime_ms() - last) >= 100u)
        {
            last = bsp_uptime_ms();
            Wdg_SetTriggerCondition(Wdg_Config.slowTimeoutMs);
        }
    }
    return 0;
}