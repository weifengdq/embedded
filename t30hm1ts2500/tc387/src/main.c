/**
 * @file  main.c
 * @brief TC387 + T30HM1TS2500 主程序 (仅 CPU0 运行)
 *
 * PLCA 默认作跟随者 (en=1, id=4, ncnt=8, totmr=32): 总线由主机 USB-10T1S
 * 当协调者 (ID0)。开 PLCA 后不再 CSMA 碰撞, TCP 7.95 -> 8.80 Mbit/s。
 * 需要退回 CSMA 时: shell 里 `s2500 plca en 0`。
 */
#include "board.h"
#include "s2500_app.h"

#include "IfxCpu.h"
#include "IfxScuWdt.h"

IfxCpu_syncEvent cpuSyncEvent = 0;

int core0_main(void)
{
    /* 打开全局中断 (QSPI SpiMaster 使用中断) */
    IfxCpu_enableInterrupts();

    /* iLLD 启动代码最后会重新打开看门狗, 这里再次关闭 */
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    static const s2500_app_cfg_t cfg = {
        .hal        = &g_s2500_hal,
        .mac        = NULL_PTR,
        .plca_id    = 4,
        .plca_ncnt  = 8,
        .plca_totmr = 32,
        .plca_en    = 1,   /* follower: 主机是协调者 (ID0), 板子 ID4; 实测 TCP 7.95 -> 8.80 */
        .promiscuous = 0,
        .ip         = 0xC0A80A0Eu,   /* 192.168.10.14 */
        .mask       = 0xFFFFFF00u,
        .gw         = 0xC0A80A0Au,   /* 192.168.10.10 */
        .board_name = "TC387 TriBoard (DAP UART)",
    };

    board_init();
    s2500_app_init(&cfg);

    for (;;) {
        s2500_app_poll();
    }
}
