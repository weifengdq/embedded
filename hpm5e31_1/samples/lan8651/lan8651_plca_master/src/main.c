/*
 * lan8651_plca_master - LAN8651 10BASE-T1S PLCA 协调者（Master，node 0）
 *
 * 本例程把 PLCA 的**协调者**角色做成独立示例：上电后自动完成 LAN8651
 * bring-up（含传输层自动降级到位翻转 SPI），按 PLCA 协调者角色配置
 * 寄存器并打印回读（期望 status=0x8000，即 PST=1），主循环每 1 ms 收包、
 * 收到自定义 EtherType 帧即回帧，每 2 s 打印一次 PLCA 状态心跳。
 *
 * 主机侧配套命令（USB-10BASE-T1S 适配器 enx001ec0d1c337）见 README.md。
 *
 * 硬件: hpm5e31_1, SPI2 PA20/21/22/23, nRST PA19, nINT PA24。
 */
#include <string.h>
#include "bsp.h"
#include "plca_demo.h"

int main(void)
{
    plca_demo_role_t role = PLCA_ROLE_COORDINATOR;
    int ret;

    bsp_init();
    printf("\r\n=== hpm5e31_1 LAN8651 PLCA 协调者（Master，node 0） ===\r\n");
    ret = plca_demo_init(role, BOARD_PLCA_NODE_CNT_DEFAULT, BOARD_PLCA_TO_TMR_DEFAULT,
                         BOARD_PLCA_BURST_CNT_DEFAULT, BOARD_PLCA_BURST_TMR_DEFAULT);
    printf("[init] result %d\r\n", ret);
    plca_demo_status();
    printf("commands: status | send [n] | plca <on|off> [id] [cnt] [to] [bc] [bt] | trans <bb|hw> | levels\r\n");

    plca_demo_run();
    return 0;
}
