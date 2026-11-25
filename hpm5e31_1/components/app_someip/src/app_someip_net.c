/*
 * app_someip_net.c —— SOME/IP 的 lwIP UDP 传输（基于 components/app_lwip 共享底座）
 *
 * 本文件只做三件事：
 *   1) 调 app_lwip_init()/app_lwip_poll()（ENET RGMII + YT8011AN + 静态 IP 192.168.2.100 + 1 ms 时基）
 *   2) 用 app_udp_socket() 建两个 UDP socket：SD 30490、服务实例 30501，并各自注册 udp_recv 回调
 *   3) 收包喂给 app_someip_on_rx()，发包用 udp_sendto()
 *
 * 单帧限制：ENET MTU 1500（SDK lwIP 的 IP_REASSEMBLY=0），>1500 B 的 UDP 会被丢——
 * 这正是 SOME/IP 需要 TP 分段的原因（每段 <=1392 B 载荷）。
 *
 * 历史：本文件最初自带一套 enet_init/lwip_init/netif_config/enet_services + YT8011 板级钩子
 *       （与 samples/enet/enet_lwip_iperf 相同），components/app_lwip 合入后已切换到底座，
 *       代码从 ~260 行降到 ~130 行，行为不变（第 35 章 §5 有说明）。
 */
#include <string.h>
#include <stdio.h>
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "app_lwip.h"
#include "app_someip.h"
#include "app_someip_net.h"
#include "common.h"        /* SDK lwIP 移植层：desc[] / enet_lwip_tx_release()（大帧 TX 排空用） */
#include "hpm_csr_drv.h"   /* mcycle：大帧之间的短忙等 */

#define NET_RX_MAX 1600U
#define ENET_TX_CRC_REPLACE_PAD  4U     /* ENET_TX_HW_CRC_MODE=replace 时 MAC 要写回 4 B FCS */
#define APP_SOMEIP_TX_DRAIN_LEN  2048U  /* 再大的帧（>2 KB，基本不会出现）才需要发后等排空 */

static struct udp_pcb *s_sd_pcb;
static struct udp_pcb *s_svc_pcb;
static struct udp_pcb *s_in_rx_pcb;    /* 回调期间记录"包是从哪个 socket 进来的" */
static app_someip_t  *s_srv;
static uint32_t       s_rx_drop;
static uint8_t        s_rx_buf[NET_RX_MAX];

static uint8_t        s_rx_buf[NET_RX_MAX];

/* TX pbuf 延迟释放环：
 * 现象（第 35 章 U-35-10）：SOME/IP-TP 连续发 3 段 4 KB 响应，线上只有**最后一段**到达主机，
 * 抓包确认板端只发出 1 个 UDP 报文。根因是 ENET 零拷贝 TX 期间 pbuf 被"立即 free + 下一次 pbuf_alloc
 * 复用同一块内存"，DMA 还在读的那块缓冲被下一段的数据覆盖 → 前两帧 FCS 错被 MAC 丢弃，最后一段没人覆盖所以活下来。
 * 解决：发送成功后不立即 free，而是压进 4 深度的环，等第 5 帧发送时再释放（此时对应帧早已发完：
 * 1 Gbit/s 下 1440 B 只要 ~11.5 µs，4 帧 ≈ 46 µs），既保住内存又不占太多（4 x ~1.5 KB）。 */
#define NET_TX_PENDING 4
static struct pbuf *s_tx_pending[NET_TX_PENDING];
static uint8_t      s_tx_pending_idx;

/* ---------------- UDP 接收 ---------------- */
static void someip_udp_rx(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                          const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    if (p == NULL) {
        return;
    }
    if ((s_srv != NULL) && (addr != NULL) && (p->tot_len <= NET_RX_MAX)) {
        const uint8_t *data;
        uint16_t len = (uint16_t)p->tot_len;
        uint8_t src[4];
        const ip4_addr_t *a4 = ip_2_ip4(addr);

        memcpy(src, &a4->addr, 4);           /* ip4_addr 内部就是网络序，memcpy 即点分顺序 */
        if (p->len == p->tot_len) {
            data = (const uint8_t *)p->payload;
        } else {
            (void)pbuf_copy_partial(p, s_rx_buf, len, 0);
            data = s_rx_buf;
        }
        struct udp_pcb *saved = s_in_rx_pcb;
        s_in_rx_pcb = pcb;
        app_someip_on_rx(s_srv, src, port, data, len);
        s_in_rx_pcb = saved;
    } else {
        s_rx_drop++;
    }
    pbuf_free(p);
}

/* ---------------- 初始化 / 收发 ---------------- */
int app_someip_net_init(void)
{
    if (app_lwip_init(NULL) != 0) {
        return -1;
    }
    return 0;
}

void app_someip_net_poll(void)
{
    app_lwip_poll();     /* 收包 + sys_check_timeouts + 链路自适应 */
}

static int open_udp(struct udp_pcb **pcb_out, uint16_t port)
{
    struct udp_pcb *pcb = app_udp_socket(NULL, port);
    if (pcb == NULL) {
        return -1;
    }
    udp_recv(pcb, someip_udp_rx, NULL);
    *pcb_out = pcb;
    return 0;
}

int app_someip_net_open(app_someip_t *srv, uint16_t sd_port, uint16_t svc_port)
{
    int rc;
    s_srv = srv;
    rc = open_udp(&s_sd_pcb, sd_port);
    if (rc != 0) {
        return rc;
    }
    rc = open_udp(&s_svc_pcb, svc_port);
    if (rc != 0) {
        return rc;
    }
    return 0;
}

int app_someip_net_send(void *ctx, const uint8_t dst_ip[4], uint16_t dst_port,
                        const uint8_t *buf, uint16_t len)
{
    struct udp_pcb *pcb = s_in_rx_pcb;     /* 优先用"收包的那个 socket"回，端口语义最准 */
    ip_addr_t dst;
    struct pbuf *p;
    err_t e;

    (void)ctx;
    if (pcb == NULL) {
        pcb = (dst_port == APP_SOMEIP_SD_PORT) ? s_sd_pcb : s_svc_pcb;
    }
    if (pcb == NULL) {
        return -1;
    }
    /* 关键：多申请 4 字节。
     * SDK 的 enet_lwip_output() 在 ENET_TX_HW_CRC_MODE=replace 时，最后一段描述符的
     * tbs1 长度是 chunk + 4（MAC 要把 4 B FCS 写回缓冲）。如果 pbuf 只申请 len 字节，
     * 这 4 B 会写到相邻堆内存里——单个小帧通常遇到填充区看不出问题，但连续发多个大帧时
     * 会把下一帧的 pbuf 头/数据踩坏 -> 该帧 FCS 错、被对端 MAC 丢弃（抓包也看不到）。
     * 这正是"连续 3 个 1412 B 的 SOME/IP-TP 段只上线最后 1 个"的根因（U-35-10）。 */
    p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)(len + ENET_TX_CRC_REPLACE_PAD), PBUF_RAM);
    if (p == NULL) {
        return -2;
    }
    p->len = len;          /* 实际数据长度仍是 len，多出来的 4 B 留给 MAC 写 FCS */
    p->tot_len = len;
    (void)memcpy(p->payload, buf, len);
    IP_ADDR4(&dst, dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
    e = udp_sendto(pcb, p, &dst, dst_port);
    pbuf_free(p);

    if ((e == ERR_OK) && (len >= APP_SOMEIP_TX_DRAIN_LEN)) {
        /* 大帧（SOME/IP-TP 段，>1400 B）连发时的排空：
         * 实测（第 35 章 U-35-10）连续 udp_sendto 3 个 1412 B 帧，只有最后 1 个能上线；
         * 在每帧之后等上一帧真正发完（1 Gbit/s 下 1448 B ≈ 11.6 us）就 3 个都上线。
         * 这里用"驱动释放已完成的 TX hold + 短忙等"实现，避免依赖 lwIP 的下一次调用时机。 */
        for (int i = 0; i < 3; i++) {
            enet_lwip_tx_release(&desc[0]);
            uint32_t t0 = (uint32_t)hpm_csr_get_core_mcycle();
            while (((uint32_t)hpm_csr_get_core_mcycle() - t0) < 7200U) {   /* 480 MHz: 15 us */
            }
        }
    }
    return (e == ERR_OK) ? 0 : -3;
}

uint32_t app_someip_net_now_ms(void *ctx)
{
    (void)ctx;
    return app_lwip_now_ms();
}

/* ---------------- 诊断 ---------------- */
bool app_someip_net_link_up(void)
{
    return app_lwip_link_up();
}

uint32_t app_someip_net_rx_drop(void)
{
    return s_rx_drop;
}

void app_someip_net_info(void)
{
    app_lwip_print();          /* netif / MAC / IP / PHY 状态（底座提供） */
    printf("[net] rx_drop=%lu\r\n", (unsigned long)s_rx_drop);
}