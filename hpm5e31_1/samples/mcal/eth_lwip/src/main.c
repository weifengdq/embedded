/*
 * eth_lwip —— AUTOSAR MCAL Eth 驱动 + lwIP 2.1.2（裸机 NO_SYS=1）
 *
 * 硬件：hpm5e31_1，ENET0(RGMII) + YT8011AN 1000BASE-T1 <-> 转换盒 <-> 主机 enp6s0
 * 网络：板 192.168.2.100/24，主机 192.168.2.1/24
 * 协议：lwIP 自带 lwiperf 实现的是 **iperf2**（端口 5001），主机请用 iperf v2 而不是 iperf3
 *
 * 分层（第 50 章"lwIP 移植"节的核心）：
 *   lwIP 协议栈  <->  components/app_lwip_eth（netif 适配层，零拷贝 RX）
 *                <->  mcal/Eth（Eth_ProvideTxBuffer / Eth_Transmit / Eth_GetRxBuffer / Eth_ReleaseRxBuffer）
 *                <->  ENET0 DMA + YT8011AN
 *
 * 串口命令（letter-shell）：
 *   eth                     打印 netif/PHY/统计
 *   eth ping <ip> [n] [ms]   ICMP echo（RTT/抖动）
 *   eth iserver [tcp|udp]    iperf2 服务端
 *   eth iclient <ip> [tcp|udp]  iperf2 客户端
 *   eth istop                中止
 *
 * 上电自证：等链路起来后自动 ping 网关 5 次（不给命令也能看出链路通不通）。
 */
#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include "board.h"
#include "Eth.h"
#include "Eth_PBcfg.h"
#include "EthTrcv.h"
#include "EthTrcv_PBcfg.h"
#include "lwip/init.h"        /* LWIP_VERSION_STRING */
#include "lwip/sys.h"        /* sys_now */
#include "app_lwip_eth.h"

#define ETH_CTRL_IDX     (0U)

/* 延时命令：脚本化测试用（--send "eth iserver;sleep 12000;eth" 可以在打流中间看统计） */
static void cmd_sleep(int argc, char *argv[])
{
    uint32_t ms = 1000;
    if (argc >= 2) {
        (void) bsp_parse_u32(argv[1], &ms);
    }
    printf("[sys] sleep %lu ms\r\n", (unsigned long) ms);
    {
        uint32_t t0 = sys_now();
        while ((sys_now() - t0) < ms) {
            app_lwip_eth_poll();
        }
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 sleep, cmd_sleep, delay N ms (keeps polling lwIP): sleep <ms>);

int main(void)
{
    Std_VersionInfoType ver;
    uint32_t t_last_stat;
    int rc;

    bsp_init();
    printf("\r\n=== mcal_eth_lwip: AUTOSAR MCAL Eth + lwIP %s (baremetal NO_SYS=1) ===\r\n",
           LWIP_VERSION_STRING);
    Eth_GetVersionInfo(&ver);
    printf("[init] Eth MCAL v%u.%u.%u (module %u)\r\n",
           (unsigned) ver.sw_major_version, (unsigned) ver.sw_minor_version,
           (unsigned) ver.sw_patch_version, (unsigned) ver.moduleID);

    /* 1) MCAL Eth 初始化：R25-11 把 ControllerInit 合并进 Eth_Init。
     *    顺序必须是 Eth_Init → EthTrcv_Init → SetControllerMode(ACTIVE)：
     *    Eth_Init 做板级（RGMII 引脚/时钟/PHY 硬复位/DMA/MAC），EthTrcv_Init 做 PHY
     *    自检 + RGMII 延迟 + 速率/主从（YT8011AN MMD1 0x834），最后才打开 MAC 收发。 */
    Eth_Init(&Eth_Config);
    EthTrcv_Init(&EthTrcv_Config);
    if (Eth_SetControllerMode(ETH_CTRL_IDX, ETH_MODE_ACTIVE) != E_OK) {
        printf("[init] Eth_SetControllerMode(ACTIVE) FAILED\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }
    printf("[init] Eth controller %u ACTIVE\r\n", (unsigned) ETH_CTRL_IDX);

    /* 2) lwIP 1 ms 时基（sys_now/sys_check_timeouts 全靠它） */
    app_lwip_eth_timebase_start();

    /* 3) netif 适配层：静态 IP 192.168.2.100/24
     *    assume_link_up 保持 false：链路状态由 EthTrcv 给出。早期为了在 EthTrcv 就绪前
     *    先打通 lwIP，曾把它置 true（第 50 章"易错点"里记了那次的现象与代价）。 */
    {
        app_lwip_eth_cfg_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.ctrl_idx = ETH_CTRL_IDX;
        cfg.link_timeout_ms = APP_LWIP_ETH_DEFAULT_LINK_TIMEOUT_MS;
        cfg.verbose = true;
        cfg.assume_link_up = false;   /* 链路状态由 EthTrcv 提供（上层不做 PHY 配置） */
        rc = app_lwip_eth_init(&cfg);
    }
    printf("[init] app_lwip_eth_init rc=%d (0=link up, 1=link down)\r\n", rc);

    /* 4) 上电自证：ping 网关（主机 enp6s0）5 次 */
    if (rc == 0) {
        app_lwip_eth_ping_result_t pr;
        (void) app_lwip_eth_ping(APP_LWIP_ETH_DEFAULT_GW, 5U, 200U, &pr);
        printf("[selftest] ping gw: %lu/%lu rtt min/avg/max=%lu/%lu/%lu us jitter=%lu us\r\n",
               (unsigned long) pr.recv, (unsigned long) pr.sent,
               (unsigned long) pr.rtt_min_us, (unsigned long) pr.rtt_avg_us,
               (unsigned long) pr.rtt_max_us, (unsigned long) pr.rtt_jitter_us);
    }
    printf("[run] commands: eth | eth ping <ip> [n] [ms] | eth iserver [tcp|udp] | eth iclient <ip>\r\n");

    t_last_stat = sys_now();
    while (1) {
        app_lwip_eth_poll();
        bsp_shell_poll();
        /* 每 10 s 打一次统计，方便长时间打流时从串口日志取样 */
        if ((sys_now() - t_last_stat) >= 10000U) {
            t_last_stat = sys_now();
            app_lwip_eth_print_stats();
        }
    }
    return 0;
}
