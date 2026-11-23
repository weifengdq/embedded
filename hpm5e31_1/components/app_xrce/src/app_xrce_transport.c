/*
 * app_xrce_transport.c —— Micro-XRCE-DDS Client 的 custom transport：lwIP 2.1.2 UDP 实现
 *
 * 为什么用 custom transport 而不是 uxrUDPTransport：
 *   上游 uxrUDPTransport 的 Posix 实现依赖 socket/poll/fcntl，裸机没有；custom transport
 *   只要求 4 个回调（open/close/write/read），把 lwIP 的 udp_recv 回调搬进环形缓冲即可。
 *
 * 线程模型（裸机单线程 + lwIP NO_SYS=1）：
 *   - 收：udp_recv 回调在主循环里被 ethernetif_input() 调用（本工程关闭 ENET 接收中断），
 *         回调只做 pbuf→槽位 memcpy 与指针前移，不做任何 printf/解析。
 *   - 发：udp_send(pbuf) 后立即 pbuf_free，ARP 未解析时由 lwIP 排队。
 *   - read() 是"阻塞直到超时"的语义，但阻塞期间必须继续驱动 lwIP，否则永远收不到包；
 *     因此通过 app_xrce_set_net_poll() 注入的钩子（= enet_common_handler）在循环里反复调用。
 */
#include "app_xrce.h"
#include <string.h>

#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "lwip/err.h"

#include <uxr/client/profile/transport/custom/custom_transport.h>
#include <uxr/client/util/time.h>

#ifndef APP_XRCE_DEBUG_TRANSPORT
#define APP_XRCE_DEBUG_TRANSPORT 0
#endif

static struct udp_pcb *s_pcb;
static ip_addr_t  s_agent_addr;
static uint16_t   s_agent_port;
static uint16_t   s_local_port;
static void     (*s_net_poll)(void);

/* 接收环形缓冲：槽数固定，满则丢最旧（并计数）。 */
static uint8_t  s_rx_buf[APP_XRCE_RX_SLOTS][APP_XRCE_MTU];
static uint16_t s_rx_len[APP_XRCE_RX_SLOTS];
static volatile uint16_t s_rx_head;   /* 写者（udp_recv 回调）递增 */
static volatile uint16_t s_rx_tail;   /* 读者（read 回调）递增 */

static uint32_t s_rx_datagrams;
static uint32_t s_rx_bytes;
static uint32_t s_tx_datagrams;
static uint32_t s_tx_bytes;
static uint32_t s_tx_errors;
static uint32_t s_rx_overrun;

/* ------------------------------------------------------------------ */
/* lwIP 接收回调                                                       */
/* ------------------------------------------------------------------ */
static void xrce_udp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                             const ip_addr_t *addr, u16_t port)
{
    (void)arg; (void)pcb; (void)addr; (void)port;

    if (p == NULL) {
        return;
    }
    uint16_t next = (uint16_t)((s_rx_head + 1U) % APP_XRCE_RX_SLOTS);
    if (next == s_rx_tail) {
        /* 环满：丢最旧一帧，保持最新数据 */
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % APP_XRCE_RX_SLOTS);
        s_rx_overrun++;
    }
    uint16_t len = p->tot_len;
    if (len > APP_XRCE_MTU) {
        len = APP_XRCE_MTU;   /* 超长帧按 MTU 截断（XRCE 不会发，防御性处理） */
    }
    pbuf_copy_partial(p, s_rx_buf[s_rx_head], len, 0);
    s_rx_len[s_rx_head] = len;
    s_rx_head = next;
    s_rx_datagrams++;
    s_rx_bytes += len;

    pbuf_free(p);
}

/* ------------------------------------------------------------------ */
/* uxrCustomTransport 回调                                             */
/* ------------------------------------------------------------------ */
static bool xrce_udp_open(uxrCustomTransport *transport)
{
    (void)transport;

    if (s_pcb != NULL) {
        return true;
    }
    s_rx_head = 0;
    s_rx_tail = 0;

    s_pcb = udp_new();
    if (s_pcb == NULL) {
        return false;
    }
    udp_recv(s_pcb, xrce_udp_recv_cb, NULL);

    if (udp_bind(s_pcb, IP_ADDR_ANY, s_local_port) != ERR_OK) {
        udp_remove(s_pcb);
        s_pcb = NULL;
        return false;
    }
    /* connect 之后只收该 Agent 的报文，且 udp_send 无需每次给地址 */
    if (udp_connect(s_pcb, &s_agent_addr, s_agent_port) != ERR_OK) {
        udp_remove(s_pcb);
        s_pcb = NULL;
        return false;
    }
    return true;
}

static bool xrce_udp_close(uxrCustomTransport *transport)
{
    (void)transport;
    if (s_pcb != NULL) {
        udp_disconnect(s_pcb);
        udp_remove(s_pcb);
        s_pcb = NULL;
    }
    return true;
}

static size_t xrce_udp_write(uxrCustomTransport *transport, const uint8_t *buf,
                            size_t len, uint8_t *errcode)
{
    (void)transport;

    if (s_pcb == NULL) {
        *errcode = 1;
        s_tx_errors++;
        return 0;
    }
    if (len > 0xFFFFU) {
        *errcode = 2;
        s_tx_errors++;
        return 0;
    }
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    if (p == NULL) {
        *errcode = 3;
        s_tx_errors++;
        return 0;
    }
    if (pbuf_take(p, buf, (u16_t)len) != ERR_OK) {
        pbuf_free(p);
        *errcode = 4;
        s_tx_errors++;
        return 0;
    }
    err_t e = udp_send(s_pcb, p);
    pbuf_free(p);
    if (e != ERR_OK) {
        *errcode = 5;
        s_tx_errors++;
        return 0;
    }
    s_tx_datagrams++;
    s_tx_bytes += (uint32_t)len;
    *errcode = 0;
    return len;
}

static size_t xrce_udp_read(uxrCustomTransport *transport, uint8_t *buf,
                           size_t len, int timeout, uint8_t *errcode)
{
    (void)transport;

    /* 超时语义：timeout<0 视为"最多 100 ms 一轮"，避免死等；XRCE 正常都传非负值。 */
    int budget = (timeout < 0) ? 100 : timeout;
    int64_t t0 = uxr_millis();

    for (;;) {
        /* 驱动 lwIP：收包、ARP、超时处理都在这里推进 */
        if (s_net_poll != NULL) {
            s_net_poll();
        }
        if (s_rx_tail != s_rx_head) {
            uint16_t i = s_rx_tail;
            uint16_t n = s_rx_len[i];
            if (n > len) {
                n = (uint16_t)len;
            }
            memcpy(buf, s_rx_buf[i], n);
            s_rx_tail = (uint16_t)((s_rx_tail + 1U) % APP_XRCE_RX_SLOTS);
            *errcode = 0;
            return n;
        }
        if ((int)(uxr_millis() - t0) >= budget) {
            *errcode = 0;
            return 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 组件内部接口（app_xrce.c 使用）                                      */
/* ------------------------------------------------------------------ */
void app_xrce_transport_configure(uint32_t agent_ip, uint16_t agent_port, uint16_t local_port)
{
    s_agent_port = agent_port;
    s_local_port = local_port;
    ip_addr_set_ip4_u32(&s_agent_addr, lwip_htonl(agent_ip));
}

void app_xrce_transport_set_poll(void (*fn)(void))
{
    s_net_poll = fn;
}

void app_xrce_transport_bind(uxrCustomTransport *transport)
{
    uxr_set_custom_transport_callbacks(transport, false,
                                       xrce_udp_open, xrce_udp_close,
                                       xrce_udp_write, xrce_udp_read);
}

void app_xrce_transport_get_counters(uint32_t *tx_dg, uint32_t *tx_bytes, uint32_t *rx_dg,
                                     uint32_t *rx_bytes, uint32_t *overrun, uint32_t *errors)
{
    *tx_dg = s_tx_datagrams;
    *tx_bytes = s_tx_bytes;
    *rx_dg = s_rx_datagrams;
    *rx_bytes = s_rx_bytes;
    *overrun = s_rx_overrun;
    *errors = s_tx_errors;
}
