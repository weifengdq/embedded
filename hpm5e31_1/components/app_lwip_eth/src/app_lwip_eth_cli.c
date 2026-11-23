/*
 * app_lwip_eth_cli.c - app_lwip_eth 的 letter-shell 命令
 *
 * 命令集（与第 50 章"例程"节的串口自证格式一一对应）：
 *   eth                     打印 netif/PHY/统计
 *   eth stat                只打印统计
 *   eth ping <ip> [n] [ms]  ICMP echo，输出 RTT 与抖动
 *   eth iserver [tcp|udp]   启动 iperf2 服务端（端口 5001）
 *   eth iclient <ip> [tcp|udp]  启动 iperf2 客户端（-1000 = 10 s）
 *   eth istop               中止 iperf
 *
 * 注意：letter-shell 的 SHELL_TYPE_CMD_MAIN 才会把 (argc, argv) 原样传进来，
 * 用 SHELL_TYPE_CMD_FUNC 会把参数当整数解析（docs/HANDOVER §3.3 坑 4）。
 *
 * 作者：eth-stack（task-2）
 */
#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include "app_lwip_eth.h"

static void cmd_eth(int argc, char *argv[])
{
    if (argc < 2) {
        app_lwip_eth_print();
        app_lwip_eth_print_stats();
        return;
    }
    if (strcmp(argv[1], "stat") == 0) {
        app_lwip_eth_print_stats();
    } else if (strcmp(argv[1], "ping") == 0) {
        uint32_t n = 10U;
        uint32_t interval = 100U;
        app_lwip_eth_ping_result_t r;
        if (argc < 3) {
            printf("usage: eth ping <ip> [count] [interval_ms]\r\n");
            return;
        }
        if (argc >= 4) {
            (void) bsp_parse_u32(argv[3], &n);
        }
        if (argc >= 5) {
            (void) bsp_parse_u32(argv[4], &interval);
        }
        (void) app_lwip_eth_ping(argv[2], n, interval, &r);
    } else if (strcmp(argv[1], "iserver") == 0) {
        bool udp = (argc >= 3) && (strcmp(argv[2], "udp") == 0);
        if (app_lwip_eth_iperf_server(udp) == 0) {
            printf("[iperf] %s server started on port %u\r\n", udp ? "UDP" : "TCP",
                   (unsigned) (udp ? 5001 : 5001));
        } else {
            printf("[iperf] server start FAILED (已有会话在跑?)\r\n");
        }
    } else if (strcmp(argv[1], "iclient") == 0) {
        bool udp;
        if (argc < 3) {
            printf("usage: eth iclient <ip> [tcp|udp]\r\n");
            return;
        }
        udp = (argc >= 4) && (strcmp(argv[3], "udp") == 0);
        if (app_lwip_eth_iperf_client(argv[2], udp) == 0) {
            printf("[iperf] %s client -> %s started\r\n", udp ? "UDP" : "TCP", argv[2]);
        } else {
            printf("[iperf] client start FAILED\r\n");
        }
    } else if (strcmp(argv[1], "istop") == 0) {
        app_lwip_eth_iperf_stop();
        printf("[iperf] aborted\r\n");
    } else {
        printf("usage: eth [stat|ping <ip> [n] [ms]|iserver [tcp|udp]|iclient <ip> [tcp|udp]|istop]\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 eth, cmd_eth, MCAL Eth + lwIP: eth [stat|ping|iserver|iclient|istop]);
