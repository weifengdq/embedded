/**
 * ptp_master - 板作 PTP 主钟（E2E，two-step）（PTP master (E2E two-step, hw timestamp)）
 *
 * 硬件：hpm5e31_1 (HPM5E31) ENET0 RGMII + YT8011AN 1000BASE-T1
 *       板 192.168.2.100/24  <->  千兆车载以太网转换盒  <->  主机 enp6s0 192.168.2.1/24
 * 软件：lwIP 2.1.2 + HPM SDK ptpd v2（IEEE 1588-2008 Ordinary Clock）+ ENET MAC 硬件时间戳
 *
 * shell 命令：ptp（状态/offset/路径延迟） | ptpstats [reset]（板上 offset 统计） |
 *            ptpreset <sec>（直接写硬件 PTP 时钟）| ip / link（网络）| sleep <ms>
 *
 * 现象与实测数据见 README.md 与 logs/。
 */
#include <stdio.h>
#include "bsp.h"
#include "common.h"
#include "netconf.h"
#include "lwip/init.h"
#include "app_ptp.h"

int main(void)
{
    bsp_init();
    printf("\r\n=== ptp_master: 板作 PTP 主钟（E2E，two-step）（PTP master (E2E two-step, hw timestamp)） ===\r\n");
    printf("lwIP %s\r\n", LWIP_VERSION_STRING);

    if (enet_init(ENET) != status_success) {
        printf("[net] ENET init FAILED\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }
    printf("[net] ENET + YT8011AN init done\r\n");

    if (app_ptp_hw_init(NULL) != 0) {
        printf("[ptp] hardware clock init FAILED\r\n");
        while (1) {
            bsp_shell_poll();
        }
    }

    lwip_init();
    netif_config(&gnetif);
    enet_services(&gnetif);

    if (app_ptp_start() != 0) {
        printf("[ptp] protocol stack start FAILED\r\n");
    }

    printf("[ptp] ready: ptp | ptpstats [reset] | ptpreset <sec> | ip | link\r\n");

    while (1) {
        enet_common_handler(&gnetif);
        app_ptp_poll();
        bsp_shell_poll();
    }
    return 0;
}
