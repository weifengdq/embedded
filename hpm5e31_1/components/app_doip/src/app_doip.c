/*
 * app_doip.c - DoIP(ISO 13400-2) 服务端核心：TCP 分帧、路由激活、诊断转发、alive check、
 *              UDP 车辆声明/发现、连接超时
 *
 * 数据流（裸机，全部在主循环上下文）：
 *   app_lwip_poll() -> enet_common_handler() -> lwIP tcp_input/udp_input
 *     -> 本文件的 on_tcp_recv / on_udp_recv 回调
 *     -> conn_feed()（8 B 头 + 载荷分帧）
 *     -> handle_xxx()（路由激活 / 诊断 / alive check / 车辆识别）
 *     -> cfg.diag_rx()（应用层调 app_uds_handle）
 *     -> app_doip_send_diag() 把 UDS 响应打成 0x8001 写回
 *
 * 作者：doip（task-3）
 */
#include <string.h>
#include <stdio.h>

#include "app_doip.h"

#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

#include "app_lwip.h"

/* 收发共用的拼包缓冲：tcp_write 用 TCP_WRITE_FLAG_COPY 拷贝后即可复用，
 * 所以单缓冲足够（单线程 + 无重入）。 */
static uint8_t s_txbuf[APP_DOIP_HDR_LEN + APP_DOIP_MAX_PAYLOAD];

/* ---------------- 小工具 ---------------- */

const char *app_doip_conn_state_name(app_doip_conn_state_t s)
{
    switch (s) {
    case APP_DOIP_CONN_CLOSED:     return "CLOSED";
    case APP_DOIP_CONN_CONNECTED:  return "CONNECTED";
    case APP_DOIP_CONN_ACTIVATED:  return "ACTIVATED";
    case APP_DOIP_CONN_ALIVE_CHECK: return "ALIVE_CHECK";
    default:                       return "?";
    }
}

static uint32_t now_ms(void)
{
    return app_lwip_now_ms();
}

static bool sa_is_valid(uint16_t sa)
{
    /* ISO 13400-2：0x0E00~0x0EFF 给外部测试设备，0x0001~0x0DFF 给车内其它诊断仪；
     * 0x0000、0xFFFF 与 0xE000 以上的功能/组地址不能出现在 0x8001 的源地址里。 */
    return ((sa >= 0x0001u) && (sa <= 0x0EFFu));
}

/* 单条报文为什么会被当成"大报文"：以太网 MTU 1500 - IP/TCP 头 40 = 1460 B，
 * 超过它必然跨多个 TCP 段，主机侧能看到 TCP 分段重组（第 32 章实测项）。 */
#define APP_DOIP_LAN_MSS  1460u

/* ---------------- 发送 ---------------- */

/* 把 [载荷] 打包成 通用头+载荷 写到 TCP 连接；成功返回 0 */
static int conn_send_frame(app_doip_conn_t *c, uint16_t pt, const uint8_t *payload, uint32_t len)
{
    app_doip_t *d = c->owner;
    uint8_t ver;
    uint32_t total;
    err_t err = ERR_OK;
    int retry;

    if ((c->pcb == NULL) || (len > APP_DOIP_MAX_PAYLOAD)) {
        return -1;
    }
    ver = (d->cfg.version == APP_DOIP_VER_2019) ? APP_DOIP_VER_2019 : APP_DOIP_VER_2012;
    if (app_doip_hdr_encode(s_txbuf, sizeof(s_txbuf), pt, len, ver) != APP_DOIP_HDR_LEN) {
        return -1;
    }
    if ((payload != NULL) && (len != 0u)) {
        memcpy(&s_txbuf[APP_DOIP_HDR_LEN], payload, len);
    }
    total = APP_DOIP_HDR_LEN + len;
    /* sndbuf 不够时先 tcp_output 把队列推出去再重试（4 KB 报文在 TCP_SND_BUF=8*MSS 下够用，
     * 这里只是防御：连续大报文 + 窗口被对端压到 0 时会出现 ERR_MEM）。 */
    for (retry = 0; retry < 8; retry++) {
        err = tcp_write(c->pcb, s_txbuf, (u16_t) total, TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK) {
            break;
        }
        (void) tcp_output(c->pcb);
    }
    if (err != ERR_OK) {
        return -1;
    }
    (void) tcp_output(c->pcb);
    c->st_tx_frames++;
    if (total > APP_DOIP_LAN_MSS) {
        d->st_big_msg++;
    }
    return 0;
}

/* UDP 发送（车辆声明/识别响应/实体状态），dst 为 NULL 时用受限广播 255.255.255.255 */
static int doip_udp_send(app_doip_t *d, const ip_addr_t *dst, uint16_t port,
                         uint16_t pt, const uint8_t *payload, uint32_t len)
{
    struct pbuf *p;
    ip_addr_t bcast;
    err_t err;

    if ((d == NULL) || (d->udp_pcb == NULL) || (len > APP_DOIP_MAX_PAYLOAD)) {
        return -1;
    }
    if (app_doip_hdr_encode(s_txbuf, sizeof(s_txbuf), pt, len,
                            (d->cfg.version == APP_DOIP_VER_2019) ? APP_DOIP_VER_2019
                                                                 : APP_DOIP_VER_2012) != APP_DOIP_HDR_LEN) {
        return -1;
    }
    if ((payload != NULL) && (len != 0u)) {
        memcpy(&s_txbuf[APP_DOIP_HDR_LEN], payload, len);
    }
    p = pbuf_alloc(PBUF_TRANSPORT, (u16_t) (APP_DOIP_HDR_LEN + len), PBUF_RAM);
    if (p == NULL) {
        return -1;
    }
    if (pbuf_take(p, s_txbuf, (u16_t) (APP_DOIP_HDR_LEN + len)) != ERR_OK) {
        pbuf_free(p);
        return -1;
    }
    if (dst == NULL) {
        IP_ADDR4(&bcast, 255, 255, 255, 255);
        dst = &bcast;
    }
    err = udp_sendto(d->udp_pcb, p, dst, port);
    pbuf_free(p);
    return (err == ERR_OK) ? 0 : -1;
}

/* ---------------- 连接管理 ---------------- */

static void conn_reset(app_doip_conn_t *c)
{
    app_doip_t *d = c->owner;

    memset(c, 0, sizeof(*c));
    c->owner = d;
    c->state = APP_DOIP_CONN_CLOSED;
}

static void conn_close(app_doip_conn_t *c, const char *why)
{
    app_doip_t *d = c->owner;

    if (c->pcb != NULL) {
        tcp_arg(c->pcb, NULL);
        tcp_recv(c->pcb, NULL);
        tcp_err(c->pcb, NULL);
        tcp_sent(c->pcb, NULL);
        (void) tcp_close(c->pcb);
        d->st_tcp_close++;
    }
    if (d->cfg.verbose && (why != NULL)) {
        printf("[doip] conn close: %s (peer=%s:%u state=%s)\r\n", why, c->peer,
               (unsigned) c->peer_port, app_doip_conn_state_name(c->state));
    }
    conn_reset(c);
}

int app_doip_find_conn(const app_doip_t *d, uint16_t tester_sa)
{
    uint32_t i;

    for (i = 0; i < APP_DOIP_MAX_CONN; i++) {
        const app_doip_conn_t *c = &d->conn[i];
        if ((c->state == APP_DOIP_CONN_ACTIVATED) && (c->tester_addr == tester_sa)) {
            return (int) i;
        }
    }
    /* 没匹配到就退回第一条已激活连接（单 tester 场景最常见） */
    for (i = 0; i < APP_DOIP_MAX_CONN; i++) {
        if (d->conn[i].state == APP_DOIP_CONN_ACTIVATED) {
            return (int) i;
        }
    }
    return -1;
}

static uint32_t conn_open_count(const app_doip_t *d)
{
    uint32_t i;
    uint32_t n = 0;

    for (i = 0; i < APP_DOIP_MAX_CONN; i++) {
        if (d->conn[i].state != APP_DOIP_CONN_CLOSED) {
            n++;
        }
    }
    return n;
}

/* ---------------- 各组报文处理 ---------------- */

static void conn_send_generic_nack(app_doip_conn_t *c, uint8_t code, bool close_after)
{
    uint8_t buf[1];
    uint32_t n;

    n = app_doip_build_generic_nack(buf, sizeof(buf), code);
    if (conn_send_frame(c, APP_DOIP_PT_GENERIC_NACK, buf, n) == 0) {
        c->owner->st_err_hdr++;
    }
    if (close_after) {
        c->close_pending = true;
    }
}

/* 诊断报文应答：0x8002 / 0x8003。SA 写实体地址、TA 写 tester 地址
 * （EcuBus-Pro 的解析按这个方向校验，见第 32 章“易错点”）。 */
static void conn_send_diag_ack(app_doip_conn_t *c, uint16_t tester_sa, uint16_t entity_sa,
                               bool positive, uint8_t code)
{
    uint8_t buf[5];
    uint32_t n;
    uint16_t pt = positive ? APP_DOIP_PT_DIAG_ACK : APP_DOIP_PT_DIAG_NACK;

    n = app_doip_build_diag_ack(buf, sizeof(buf), entity_sa, tester_sa, code);
    if (conn_send_frame(c, pt, buf, n) == 0) {
        if (positive) {
            c->st_tx_ack++;
        } else {
            c->st_tx_nack++;
        }
    }
}

static void handle_alive_check_req(app_doip_t *d, app_doip_conn_t *c, uint32_t len);

/* 0x0005 路由激活请求（7 B 无 OEM / 11 B 带 OEM） */
static void handle_route_activation(app_doip_t *d, app_doip_conn_t *c,
                                    const uint8_t *p, uint32_t len)
{
    app_doip_ra_req_t ra;
    uint8_t resp[13];
    uint32_t n;
    uint8_t code = APP_DOIP_RA_OK;
    uint32_t i;

    if (!app_doip_parse_ra_req(p, len, &ra)) {
        /* ISO 13400-2 只定义了 11 B；实测工具常发 7 B，所以两者都收，其它长度按长度错处理 */
        conn_send_generic_nack(c, APP_DOIP_NACK_INV_PAYLOAD_LEN, false);
        return;
    }
    c->last_rx_ms = now_ms();

    if (!sa_is_valid(ra.sa)) {
        code = APP_DOIP_RA_UNKNOWN_SA;
    } else if ((d->cfg.tester_addr != 0u) && (ra.sa != d->cfg.tester_addr)) {
        code = APP_DOIP_RA_UNKNOWN_SA;
    } else if (d->cfg.activation_mask != 0u) {
        /* 用位掩码表示允许的激活类型；0xE0 单独用 bit7 表示 */
        uint8_t bit = (ra.activation_type == APP_DOIP_AT_CENTRAL_SECURITY) ? 0x80u
                                                                          : (uint8_t) (1u << (ra.activation_type & 0x07u));
        if ((ra.activation_type > 0x01u) && (ra.activation_type != APP_DOIP_AT_CENTRAL_SECURITY)) {
            code = APP_DOIP_RA_UNSUPPORTED_AT;
        } else if ((d->cfg.activation_mask & bit) == 0u) {
            code = APP_DOIP_RA_UNSUPPORTED_AT;
        }
    }
    if ((code == APP_DOIP_RA_OK) && d->cfg.require_oem_auth) {
        if (!ra.has_oem || (memcmp(ra.oem, d->cfg.oem_key, 4u) != 0)) {
            code = APP_DOIP_RA_MISSING_AUTH;
        }
    }
    /* 同一个 tester 地址已在别的 socket 上激活：ISO 要求先对老连接做 alive check，
     * 老连接不应答（500 ms 超时）后才允许新连接接管 */
    if (code == APP_DOIP_RA_OK) {
        for (i = 0; i < APP_DOIP_MAX_CONN; i++) {
            app_doip_conn_t *o = &d->conn[i];
            if ((o == c) || (o->state != APP_DOIP_CONN_ACTIVATED) || (o->tester_addr != ra.sa)) {
                continue;
            }
            (void) app_doip_alive_check(d, (int) i);
            code = APP_DOIP_RA_SA_USED;
            break;
        }
    }
    /* 同一条连接已经激活过，又换了 SA：拒绝 */
    if ((code == APP_DOIP_RA_OK) && (c->state == APP_DOIP_CONN_ACTIVATED) &&
        (c->tester_addr != ra.sa)) {
        code = APP_DOIP_RA_SA_DIFF;
    }

    if (code == APP_DOIP_RA_OK) {
        c->state = APP_DOIP_CONN_ACTIVATED;
        c->tester_addr = ra.sa;
        c->activation_type = ra.activation_type;
        if (ra.has_oem) {
            memcpy(c->oem, ra.oem, 4u);
        }
        c->last_rx_ms = now_ms();
        c->state_since_ms = c->last_rx_ms;
        d->st_route_ok++;
    } else {
        d->st_route_deny++;
    }
    n = app_doip_build_ra_resp(resp, sizeof(resp), ra.sa, d->cfg.entity_addr, code);
    (void) conn_send_frame(c, APP_DOIP_PT_ROUTING_ACT_RESP, resp, n);
    if (d->cfg.verbose) {
        printf("[doip] routing activation sa=0x%04X at=0x%02X -> code=0x%02X (%s)\r\n",
               (unsigned) ra.sa, (unsigned) ra.activation_type, (unsigned) code,
               (code == APP_DOIP_RA_OK) ? "OK" : "DENIED");
    }
}

/* 0x0007 alive check 请求（正常是实体发给 tester，测试仪主动发也回一条 0x0008） */
static void handle_alive_check_req(app_doip_t *d, app_doip_conn_t *c, uint32_t len)
{
    uint8_t buf[2];

    (void) len;
    app_doip_put_u16(buf, d->cfg.entity_addr);
    (void) conn_send_frame(c, APP_DOIP_PT_ALIVE_CHECK_RESP, buf, 2u);
}

/* 0x0008 alive check 响应：老连接还活着，退回 ACTIVATED */
static void handle_alive_check_resp(app_doip_t *d, app_doip_conn_t *c,
                                    const uint8_t *p, uint32_t len)
{
    if (len < 2u) {
        conn_send_generic_nack(c, APP_DOIP_NACK_INV_PAYLOAD_LEN, false);
        return;
    }
    (void) p;
    if (c->state == APP_DOIP_CONN_ALIVE_CHECK) {
        c->state = APP_DOIP_CONN_ACTIVATED;
        c->last_rx_ms = now_ms();
        d->st_alive_ok++;
        if (d->cfg.verbose) {
            printf("[doip] alive check resp from 0x%04X: socket 仍有效\r\n",
                   (unsigned) app_doip_get_u16(p));
        }
    }
}

/* 0x8001 诊断报文：SA(2) + TA(2) + UDS(n) */
static void handle_diag_message(app_doip_t *d, app_doip_conn_t *c,
                                const uint8_t *p, uint32_t len)
{
    uint16_t sa;
    uint16_t ta;

    if (len < 4u) {
        conn_send_generic_nack(c, APP_DOIP_NACK_INV_PAYLOAD_LEN, false);
        return;
    }
    sa = app_doip_get_u16(&p[0]);
    ta = app_doip_get_u16(&p[2]);
    if (!sa_is_valid(sa)) {
        conn_send_diag_ack(c, sa, d->cfg.entity_addr, false, APP_DOIP_DIAG_NACK_INV_SA);
        return;
    }
    if (ta != d->cfg.entity_addr) {
        conn_send_diag_ack(c, sa, d->cfg.entity_addr, false, APP_DOIP_DIAG_NACK_UNKNOWN_TA);
        return;
    }
    if (c->state != APP_DOIP_CONN_ACTIVATED) {
        /* 未路由激活就发诊断：ISO 语义是"源地址未注册"，这里回 0x02 但不断链，
         * 方便主机侧测异常用例（第 32 章异常用例 4） */
        conn_send_diag_ack(c, sa, d->cfg.entity_addr, false, APP_DOIP_DIAG_NACK_INV_SA);
        return;
    }
    if ((len - 4u) > d->cfg.max_diag_uds) {
        /* ISO 13400-2：超过实体处理能力回 0x8003 + 0x04（diagnostic message too large）。
         * 不能把超长 UDS 直接交给上层：app_uds_handle() 对 > APP_UDS_MAX_REQ 的请求直接
         * 返回 -1，主机侧表现为"收到 ACK 但永远没有响应"（第 32 章易错点 10）。 */
        conn_send_diag_ack(c, sa, d->cfg.entity_addr, false, APP_DOIP_DIAG_NACK_TOO_LARGE);
        return;
    }
    c->last_rx_ms = now_ms();
    /* 先回肯定应答（表示"收到了"），再交给 UDS 处理；UDS 慢服务会先出 0x78 */
    conn_send_diag_ack(c, sa, d->cfg.entity_addr, true, APP_DOIP_DIAG_ACK_OK);
    c->st_rx_diag++;
    d->st_diag_rx++;
    if (len > APP_DOIP_LAN_MSS) {
        d->st_big_msg++;
    }
    if (d->cfg.diag_rx != NULL) {
        (void) d->cfg.diag_rx(d->cfg.diag_ctx, sa, ta, &p[4], len - 4u);
    }
}

/* ---------------- TCP 分帧 ---------------- */

static void conn_dispatch(app_doip_t *d, app_doip_conn_t *c)
{
    uint16_t pt = c->rx_type;
    const uint8_t *p = c->rx_buf;
    uint32_t len = c->rx_got;

    c->st_rx_frames++;
    /* 重置信令，处理函数里可能马上又收到新帧（回调嵌套不会发生，但保持状态干净） */
    c->rx_hdr_got = 0u;
    c->rx_type = 0u;
    c->rx_want = 0u;
    c->rx_got = 0u;

    switch (pt) {
    case APP_DOIP_PT_ROUTING_ACT_REQ:
        handle_route_activation(d, c, p, len);
        break;
    case APP_DOIP_PT_DIAG_MESSAGE:
        handle_diag_message(d, c, p, len);
        break;
    case APP_DOIP_PT_ALIVE_CHECK_REQ:
        handle_alive_check_req(d, c, len);
        break;
    case APP_DOIP_PT_ALIVE_CHECK_RESP:
        handle_alive_check_resp(d, c, p, len);
        break;
    default:
        /* 车辆识别/实体状态/电源模式走 UDP；TCP 上收到按"未知载荷类型"回通用 NACK */
        conn_send_generic_nack(c, APP_DOIP_NACK_UNKNOWN_PT, false);
        break;
    }
}

/* 通用头收齐后：校验并决定要不要收载荷 */
static bool conn_begin_payload(app_doip_t *d, app_doip_conn_t *c)
{
    app_doip_hdr_t h;
    uint8_t nack = 0u;

    if (!app_doip_hdr_decode(c->rx_hdr, &h, &nack)) {
        c->st_err_hdr++;
        d->st_err_hdr++;
        conn_send_generic_nack(c, (nack != 0u) ? nack : APP_DOIP_NACK_INC_PATTERN, true);
        return false;
    }
    /* TCP 上只应出现 0x0005/0x0007/0x0008/0x8001~0x8003；其余按未知类型处理 */
    c->rx_type = h.payload_type;
    c->rx_want = h.payload_len;
    c->rx_got = 0u;
    if (c->rx_want > d->cfg.max_payload) {
        if (h.payload_type == APP_DOIP_PT_DIAG_MESSAGE) {
            /* 诊断报文超过实体声明的 max data size：按 ISO 回 0x8003 code 0x04 再断链 */
            conn_send_diag_ack(c, 0u, d->cfg.entity_addr, false, APP_DOIP_DIAG_NACK_TOO_LARGE);
        } else {
            conn_send_generic_nack(c, APP_DOIP_NACK_MSG_TOO_LARGE, true);
        }
        c->close_pending = true;
        return false;
    }
    if (c->rx_want == 0u) {
        conn_dispatch(d, c);
    }
    return (c->state != APP_DOIP_CONN_CLOSED);
}

/* 一帧收完 */
static void conn_frame_done(app_doip_t *d, app_doip_conn_t *c)
{
    conn_dispatch(d, c);
    if (c->state == APP_DOIP_CONN_CLOSED) {
        return;
    }
    if (c->close_pending) {
        conn_close(c, "frame error");
    }
}

/* 喂字节流（lwIP 是流式 TCP，一帧可能被拆到多个 pbuf / 多次 recv） */
static void conn_feed(app_doip_t *d, app_doip_conn_t *c, const uint8_t *data, uint32_t len)
{
    uint32_t i = 0u;

    while (i < len) {
        if (c->state == APP_DOIP_CONN_CLOSED) {
            return;
        }
        if (c->rx_hdr_got < APP_DOIP_HDR_LEN) {
            uint32_t n = APP_DOIP_HDR_LEN - c->rx_hdr_got;
            if (n > (len - i)) {
                n = len - i;
            }
            memcpy(&c->rx_hdr[c->rx_hdr_got], &data[i], n);
            c->rx_hdr_got += n;
            i += n;
            if (c->rx_hdr_got == APP_DOIP_HDR_LEN) {
                if (!conn_begin_payload(d, c)) {
                    return;
                }
            }
            continue;
        }
        /* 正在收载荷 */
        {
            uint32_t n = c->rx_want - c->rx_got;
            if (n > (len - i)) {
                n = len - i;
            }
            memcpy(&c->rx_buf[c->rx_got], &data[i], n);
            c->rx_got += n;
            i += n;
            if (c->rx_got == c->rx_want) {
                conn_frame_done(d, c);
                if ((c->state == APP_DOIP_CONN_CLOSED) || c->close_pending) {
                    return;
                }
            }
        }
    }
}

/* ---------------- lwIP TCP 回调 ---------------- */

static err_t on_tcp_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    app_doip_conn_t *c = (app_doip_conn_t *) arg;
    struct pbuf *q;

    if (c == NULL) {
        if (p != NULL) {
            pbuf_free(p);
        }
        return ERR_OK;
    }
    if (p == NULL) {
        /* 对端正常关闭 */
        conn_close(c, "peer closed");
        return ERR_OK;
    }
    if (err != ERR_OK) {
        pbuf_free(p);
        conn_close(c, "recv err");
        return ERR_OK;
    }
    /* 先开窗口再解析：解析里可能直接 tcp_close()，之后再 tcp_recved 就是野指针 */
    tcp_recved(pcb, p->tot_len);
    c->last_rx_ms = now_ms();
    for (q = p; q != NULL; q = q->next) {
        conn_feed(c->owner, c, (const uint8_t *) q->payload, q->len);
        if (c->state == APP_DOIP_CONN_CLOSED) {
            break;
        }
    }
    pbuf_free(p);
    if ((c->state != APP_DOIP_CONN_CLOSED) && c->close_pending) {
        conn_close(c, "close pending");
    }
    return ERR_OK;
}

static err_t on_tcp_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void) arg;
    (void) pcb;
    (void) len;
    return ERR_OK;
}

static void on_tcp_err(void *arg, err_t err)
{
    app_doip_conn_t *c = (app_doip_conn_t *) arg;

    if ((c == NULL) || (c->owner == NULL)) {
        return;
    }
    /* tcp_err 回调里 pcb 已被 lwIP 释放，**不能**再 tcp_close() */
    if (c->owner->cfg.verbose) {
        printf("[doip] conn error: err=%d peer=%s:%u\r\n", (int) err, c->peer,
               (unsigned) c->peer_port);
    }
    c->pcb = NULL;
    c->owner->st_tcp_close++;
    conn_reset(c);
}

static err_t on_tcp_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    app_doip_t *d = (app_doip_t *) arg;
    app_doip_conn_t *c = NULL;
    uint32_t i;

    if ((d == NULL) || (newpcb == NULL) || (err != ERR_OK)) {
        return ERR_VAL;
    }
    d->st_tcp_accept++;
    for (i = 0; i < d->cfg.max_conn; i++) {
        if (d->conn[i].state == APP_DOIP_CONN_CLOSED) {
            c = &d->conn[i];
            break;
        }
    }
    if (c == NULL) {
        /* 连接数到顶：直接 abort（ISO 允许实体在 socket 用尽时拒绝） */
        d->st_route_deny++;
        tcp_abort(newpcb);
        return ERR_ABRT;
    }
    conn_reset(c);
    c->owner = d;
    c->pcb = newpcb;
    c->state = APP_DOIP_CONN_CONNECTED;
    c->state_since_ms = now_ms();
    c->last_rx_ms = c->state_since_ms;
    c->peer_port = newpcb->remote_port;
    (void) ipaddr_ntoa_r(&newpcb->remote_ip, c->peer, sizeof(c->peer));
    tcp_arg(newpcb, c);
    tcp_recv(newpcb, on_tcp_recv);
    tcp_sent(newpcb, on_tcp_sent);
    tcp_err(newpcb, on_tcp_err);
    tcp_nagle_disable(newpcb);          /* 诊断是小报文，Nagle 会把响应攒 40 ms */
    return ERR_OK;
}
/* ---------------- 车辆声明 / UDP 发现 ---------------- */

static uint32_t udp_reply_announce(app_doip_t *d, const ip_addr_t *addr, uint16_t port)
{
    uint8_t buf[40];
    uint32_t n;

    n = app_doip_build_vehicle_announce(buf, sizeof(buf), d->cfg.vin, d->cfg.entity_addr,
                                        d->cfg.eid, d->cfg.gid, d->cfg.further_action,
                                        d->cfg.version, d->cfg.sync_status);
    if (n == 0u) {
        return 0u;
    }
    if (doip_udp_send(d, addr, port, APP_DOIP_PT_VEHICLE_ANNOUNCE, buf, n) == 0) {
        d->st_announce_tx++;
    }
    return n;
}

int app_doip_send_announce(app_doip_t *d, uint32_t num)
{
    uint8_t buf[40];
    uint32_t n;

    if ((d == NULL) || (!d->running)) {
        return -1;
    }
    n = app_doip_build_vehicle_announce(buf, sizeof(buf), d->cfg.vin, d->cfg.entity_addr,
                                        d->cfg.eid, d->cfg.gid, d->cfg.further_action,
                                        d->cfg.version, d->cfg.sync_status);
    if (n == 0u) {
        return -1;
    }
    if (num == 0u) {
        num = (d->cfg.announce_num != 0u) ? d->cfg.announce_num : 1u;
    }
    /* 第一条立刻广播，其余按 announce_interval_ms 交给 poll() 节拍 */
    if (doip_udp_send(d, NULL, APP_DOIP_PORT, APP_DOIP_PT_VEHICLE_ANNOUNCE, buf, n) == 0) {
        d->st_announce_tx++;
    }
    if (num > 1u) {
        d->ann_active = true;
        d->ann_left = num - 1u;
        d->ann_next_ms = now_ms() + d->cfg.announce_interval_ms;
    }
    return 0;
}

/* 0x4001 实体状态 / 0x4003 电源模式 / 车辆识别请求都在 UDP 13400 上 */
static void on_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                        const ip_addr_t *addr, u16_t port)
{
    app_doip_t *d = (app_doip_t *) arg;
    uint8_t hdr[APP_DOIP_HDR_LEN];
    uint8_t payload[64];
    uint8_t resp[40];
    app_doip_hdr_t h;
    uint8_t nack = 0u;
    uint32_t n;

    (void) pcb;
    if ((d == NULL) || (p == NULL)) {
        if (p != NULL) {
            pbuf_free(p);
        }
        return;
    }
    d->st_udp_rx++;
    if (p->tot_len < APP_DOIP_HDR_LEN) {
        pbuf_free(p);
        return;
    }
    (void) pbuf_copy_partial(p, hdr, APP_DOIP_HDR_LEN, 0);
    if (!app_doip_hdr_decode(hdr, &h, &nack)) {
        n = app_doip_build_generic_nack(resp, sizeof(resp), nack);
        (void) doip_udp_send(d, addr, port, APP_DOIP_PT_GENERIC_NACK, resp, n);
        pbuf_free(p);
        return;
    }
    if ((h.payload_len + APP_DOIP_HDR_LEN) != p->tot_len) {
        n = app_doip_build_generic_nack(resp, sizeof(resp), APP_DOIP_NACK_INV_PAYLOAD_LEN);
        (void) doip_udp_send(d, addr, port, APP_DOIP_PT_GENERIC_NACK, resp, n);
        pbuf_free(p);
        return;
    }
    if (h.payload_len > sizeof(payload)) {
        n = app_doip_build_generic_nack(resp, sizeof(resp), APP_DOIP_NACK_MSG_TOO_LARGE);
        (void) doip_udp_send(d, addr, port, APP_DOIP_PT_GENERIC_NACK, resp, n);
        pbuf_free(p);
        return;
    }
    if (h.payload_len != 0u) {
        (void) pbuf_copy_partial(p, payload, (u16_t) h.payload_len, APP_DOIP_HDR_LEN);
    }
    pbuf_free(p);

    switch (h.payload_type) {
    case APP_DOIP_PT_VEHICLE_ID_REQ:            /* 0x0001：无参数，谁都问都答 */
        d->st_udp_ident_req++;
        (void) udp_reply_announce(d, addr, port);
        break;
    case APP_DOIP_PT_VEHICLE_ID_REQ_EID:        /* 0x0002：带 EID，全 FF = 广播 */
    {
        bool any = true;
        uint32_t i;
        if (h.payload_len != 6u) {
            n = app_doip_build_generic_nack(resp, sizeof(resp), APP_DOIP_NACK_INV_PAYLOAD_LEN);
            (void) doip_udp_send(d, addr, port, APP_DOIP_PT_GENERIC_NACK, resp, n);
            break;
        }
        for (i = 0; i < 6u; i++) {
            if (payload[i] != 0xFFu) {
                any = false;
            }
        }
        if (any || (memcmp(payload, d->cfg.eid, 6u) == 0)) {
            d->st_udp_ident_req++;
            (void) udp_reply_announce(d, addr, port);
        }
        break;
    }
    case APP_DOIP_PT_VEHICLE_ID_REQ_VIN:        /* 0x0003：带 VIN，全 0 = 广播 */
    {
        bool any = true;
        uint32_t i;
        if (h.payload_len != 17u) {
            n = app_doip_build_generic_nack(resp, sizeof(resp), APP_DOIP_NACK_INV_PAYLOAD_LEN);
            (void) doip_udp_send(d, addr, port, APP_DOIP_PT_GENERIC_NACK, resp, n);
            break;
        }
        for (i = 0; i < 17u; i++) {
            if (payload[i] != 0x00u) {
                any = false;
            }
        }
        if (any || (memcmp(payload, d->cfg.vin, 17u) == 0)) {
            d->st_udp_ident_req++;
            (void) udp_reply_announce(d, addr, port);
        }
        break;
    }
    case APP_DOIP_PT_ENTITY_STATUS_REQ:         /* 0x4001 */
        if (h.payload_len != 0u) {
            n = app_doip_build_generic_nack(resp, sizeof(resp), APP_DOIP_NACK_INV_PAYLOAD_LEN);
            (void) doip_udp_send(d, addr, port, APP_DOIP_PT_GENERIC_NACK, resp, n);
            break;
        }
        n = app_doip_build_entity_status(resp, sizeof(resp), APP_DOIP_NODE_DOIP_NODE,
                                         (uint8_t) d->cfg.max_conn,
                                         (uint8_t) conn_open_count(d),
                                         4u + d->cfg.max_diag_uds);
        (void) doip_udp_send(d, addr, port, APP_DOIP_PT_ENTITY_STATUS_RESP, resp, n);
        break;
    case APP_DOIP_PT_POWER_MODE_REQ:            /* 0x4003 */
        if (h.payload_len != 0u) {
            n = app_doip_build_generic_nack(resp, sizeof(resp), APP_DOIP_NACK_INV_PAYLOAD_LEN);
            (void) doip_udp_send(d, addr, port, APP_DOIP_PT_GENERIC_NACK, resp, n);
            break;
        }
        n = app_doip_build_power_mode(resp, sizeof(resp), d->cfg.power_mode);
        (void) doip_udp_send(d, addr, port, APP_DOIP_PT_POWER_MODE_RESP, resp, n);
        break;
    case APP_DOIP_PT_VEHICLE_ANNOUNCE:          /* 别的实体在宣告，忽略 */
        break;
    default:
        n = app_doip_build_generic_nack(resp, sizeof(resp), APP_DOIP_NACK_UNKNOWN_PT);
        (void) doip_udp_send(d, addr, port, APP_DOIP_PT_GENERIC_NACK, resp, n);
        break;
    }
}

/* ---------------- 诊断发送 / alive check ---------------- */

/* 载荷已经在 s_txbuf+APP_DOIP_HDR_LEN，只补通用头（避免 4 KB 再拷一遍） */
static int conn_send_frame_inplace(app_doip_conn_t *c, uint16_t pt, uint32_t len)
{
    app_doip_t *d = c->owner;
    uint32_t total;
    err_t err = ERR_OK;
    int retry;

    if ((c->pcb == NULL) || (len > APP_DOIP_MAX_PAYLOAD)) {
        return -1;
    }
    (void) app_doip_hdr_encode(s_txbuf, sizeof(s_txbuf), pt, len,
                               (d->cfg.version == APP_DOIP_VER_2019) ? APP_DOIP_VER_2019
                                                                    : APP_DOIP_VER_2012);
    total = APP_DOIP_HDR_LEN + len;
    for (retry = 0; retry < 8; retry++) {
        err = tcp_write(c->pcb, s_txbuf, (u16_t) total, TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK) {
            break;
        }
        (void) tcp_output(c->pcb);
    }
    if (err != ERR_OK) {
        return -1;
    }
    (void) tcp_output(c->pcb);
    c->st_tx_frames++;
    if (total > APP_DOIP_LAN_MSS) {
        d->st_big_msg++;
    }
    return 0;
}

int app_doip_send_diag(app_doip_t *d, uint16_t ta, const uint8_t *uds, uint32_t len)
{
    int idx;
    app_doip_conn_t *c;
    uint32_t n;

    if ((d == NULL) || ((uds == NULL) && (len != 0u))) {
        return -1;
    }
    idx = app_doip_find_conn(d, ta);
    if (idx < 0) {
        return -1;                     /* 没有已激活连接：UDS 响应无处可发 */
    }
    c = &d->conn[idx];
    n = app_doip_build_diag(&s_txbuf[APP_DOIP_HDR_LEN], APP_DOIP_MAX_PAYLOAD,
                            d->cfg.entity_addr, ta, uds, len);
    if (n == 0u) {
        return -1;
    }
    if (conn_send_frame_inplace(c, APP_DOIP_PT_DIAG_MESSAGE, n) != 0) {
        return -1;
    }
    c->st_tx_diag++;
    d->st_diag_tx++;
    return 0;
}

int app_doip_alive_check(app_doip_t *d, int conn_idx)
{
    app_doip_conn_t *c;
    int idx = conn_idx;

    if (d == NULL) {
        return -1;
    }
    if (idx < 0) {
        idx = app_doip_find_conn(d, 0u);
    }
    if ((idx < 0) || ((uint32_t) idx >= APP_DOIP_MAX_CONN)) {
        return -1;
    }
    c = &d->conn[idx];
    if (c->state == APP_DOIP_CONN_ACTIVATED) {
        if (conn_send_frame(c, APP_DOIP_PT_ALIVE_CHECK_REQ, NULL, 0u) != 0) {
            return -1;
        }
        d->st_alive_tx++;
    } else if (c->state != APP_DOIP_CONN_ALIVE_CHECK) {
        return -1;
    }
    c->state = APP_DOIP_CONN_ALIVE_CHECK;
    c->alive_deadline_ms = now_ms() + d->cfg.alive_check_ms;
    return idx;
}

/* ---------------- 周期任务 ---------------- */

void app_doip_poll(app_doip_t *d)
{
    uint32_t t;
    uint32_t i;

    if ((d == NULL) || (!d->running)) {
        return;
    }
    t = now_ms();
    for (i = 0; i < d->cfg.max_conn; i++) {
        app_doip_conn_t *c = &d->conn[i];
        switch (c->state) {
        case APP_DOIP_CONN_CONNECTED:
            if ((uint32_t) (t - c->state_since_ms) >= d->cfg.initial_inactivity_ms) {
                d->st_initial_timeout++;
                conn_close(c, "initial inactivity (A_DoIP_Initial_Inactivity)");
            }
            break;
        case APP_DOIP_CONN_ACTIVATED:
            if ((uint32_t) (t - c->last_rx_ms) >= d->cfg.general_inactivity_ms) {
                d->st_general_timeout++;
                conn_close(c, "general inactivity (A_DoIP_General_Inactivity)");
            }
            break;
        case APP_DOIP_CONN_ALIVE_CHECK:
            if ((int32_t) (t - c->alive_deadline_ms) >= 0) {
                d->st_alive_timeout++;
                conn_close(c, "alive check timeout");
            }
            break;
        default:
            break;
        }
    }
    if (d->ann_active) {
        if ((int32_t) (t - d->ann_next_ms) >= 0) {
            (void) app_doip_send_announce(d, 1u);
            if (d->ann_left > 0u) {
                d->ann_left--;
            }
            d->ann_next_ms = t + d->cfg.announce_interval_ms;
            if (d->ann_left == 0u) {
                d->ann_active = false;
            }
        }
    }
    /* 踢一次 TCP 输出：ENET 发送环满时 low_level_output 会返回 ERR_MEM，lwIP 把数据留在
     * unsent 队列里，只等 RTO（默认 200 ms）才重发——实测会让诊断往返从 0.3 ms 涨到 279 ms。
     * 主循环每轮主动 tcp_output()，有数据就发、没数据是空操作（第 32 章易错点 3）。 */
    for (i = 0; i < d->cfg.max_conn; i++) {
        if (d->conn[i].pcb != NULL) {
            (void) tcp_output(d->conn[i].pcb);
        }
    }
    if (d->cfg.announce_period_ms != 0u) {
        if ((int32_t) (t - d->ann_period_next_ms) >= 0) {
            (void) app_doip_send_announce(d, 1u);
            d->ann_period_next_ms = t + d->cfg.announce_period_ms;
        }
    }
}

/* ---------------- 初始化 / 启停 ---------------- */

static uint32_t s_rand_state = 0x12345678u;

static uint32_t doip_rand(void)
{
    /* xorshift32：只给上电公告加 0~500 ms 随机抖动，不用于安全用途 */
    s_rand_state ^= s_rand_state << 13;
    s_rand_state ^= s_rand_state >> 17;
    s_rand_state ^= s_rand_state << 5;
    return s_rand_state;
}

int app_doip_init(app_doip_t *d, const app_doip_cfg_t *cfg)
{
    uint32_t i;

    if ((d == NULL) || (cfg == NULL)) {
        return -1;
    }
    memset(d, 0, sizeof(*d));
    d->cfg = *cfg;
    /* 0 表示"用默认值" */
    if (d->cfg.initial_inactivity_ms == 0u) {
        d->cfg.initial_inactivity_ms = APP_DOIP_INITIAL_INACTIVITY_MS;
    }
    if (d->cfg.general_inactivity_ms == 0u) {
        d->cfg.general_inactivity_ms = APP_DOIP_GENERAL_INACTIVITY_MS;
    }
    if (d->cfg.alive_check_ms == 0u) {
        d->cfg.alive_check_ms = APP_DOIP_ALIVE_CHECK_MS;
    }
    if (d->cfg.announce_num == 0u) {
        d->cfg.announce_num = APP_DOIP_ANNOUNCE_NUM;
    }
    if (d->cfg.announce_interval_ms == 0u) {
        d->cfg.announce_interval_ms = APP_DOIP_ANNOUNCE_INTERVAL_MS;
    }
    if (d->cfg.announce_wait_ms == 0u) {
        d->cfg.announce_wait_ms = APP_DOIP_ANNOUNCE_WAIT_MS;
    }
    if (d->cfg.max_conn == 0u) {
        d->cfg.max_conn = APP_DOIP_MAX_CONN;
    }
    if (d->cfg.max_conn > APP_DOIP_MAX_CONN) {
        d->cfg.max_conn = APP_DOIP_MAX_CONN;
    }
    if (d->cfg.max_payload == 0u) {
        d->cfg.max_payload = APP_DOIP_MAX_PAYLOAD;
    }
    if (d->cfg.max_payload > APP_DOIP_MAX_PAYLOAD) {
        d->cfg.max_payload = APP_DOIP_MAX_PAYLOAD;
    }
    if (d->cfg.max_diag_uds == 0u) {
        d->cfg.max_diag_uds = APP_DOIP_MAX_DIAG_UDS;
    }
    if (d->cfg.max_diag_uds > d->cfg.max_payload - 4u) {
        d->cfg.max_diag_uds = d->cfg.max_payload - 4u;
    }
    if (d->cfg.version == 0u) {
        d->cfg.version = APP_DOIP_VER_2012;
    }
    if (d->cfg.power_mode == 0u) {
        d->cfg.power_mode = APP_DOIP_PWR_READY;
    }
    for (i = 0; i < APP_DOIP_MAX_CONN; i++) {
        conn_reset(&d->conn[i]);
        d->conn[i].owner = d;
    }
    s_rand_state = 0x12345678u ^ (uint32_t) app_lwip_now_ms();
    d->inited = true;
    return 0;
}

int app_doip_start(app_doip_t *d)
{
    struct tcp_pcb *pcb;
    struct tcp_pcb *lpcb;
    err_t err;

    if ((d == NULL) || (!d->inited)) {
        return -1;
    }
    if (d->running) {
        return 0;
    }
    /* UDP：车辆发现/声明/实体状态/电源模式 */
    d->udp_pcb = app_udp_socket(NULL, APP_DOIP_PORT);
    if (d->udp_pcb == NULL) {
        return -2;
    }
    udp_recv(d->udp_pcb, on_udp_recv, d);

    /* TCP：路由激活 + 诊断 */
    pcb = tcp_new();
    if (pcb == NULL) {
        udp_remove(d->udp_pcb);
        d->udp_pcb = NULL;
        return -3;
    }
    err = tcp_bind(pcb, IP_ADDR_ANY, APP_DOIP_PORT);
    if (err != ERR_OK) {
        tcp_close(pcb);
        udp_remove(d->udp_pcb);
        d->udp_pcb = NULL;
        return -4;
    }
    lpcb = tcp_listen_with_backlog(pcb, 4);
    if (lpcb == NULL) {
        tcp_close(pcb);
        udp_remove(d->udp_pcb);
        d->udp_pcb = NULL;
        return -5;
    }
    d->listen_pcb = lpcb;
    tcp_arg(lpcb, d);
    tcp_accept(lpcb, on_tcp_accept);

    d->running = true;
    /* 上电公告：A_DoIP_Announce_Wait 随机 0~wait，然后按 interval 连发 num 次 */
    d->ann_active = true;
    d->ann_left = d->cfg.announce_num;
    d->ann_next_ms = now_ms() + (doip_rand() % (d->cfg.announce_wait_ms + 1u));
    d->ann_period_next_ms = now_ms() + d->cfg.announce_period_ms;
    return 0;
}

int app_doip_stop(app_doip_t *d)
{
    uint32_t i;

    if (d == NULL) {
        return -1;
    }
    for (i = 0; i < APP_DOIP_MAX_CONN; i++) {
        if (d->conn[i].state != APP_DOIP_CONN_CLOSED) {
            conn_close(&d->conn[i], "stop");
        }
    }
    if (d->listen_pcb != NULL) {
        tcp_arg(d->listen_pcb, NULL);
        tcp_accept(d->listen_pcb, NULL);
        (void) tcp_close(d->listen_pcb);
        d->listen_pcb = NULL;
    }
    if (d->udp_pcb != NULL) {
        udp_remove(d->udp_pcb);
        d->udp_pcb = NULL;
    }
    d->running = false;
    d->ann_active = false;
    return 0;
}

/* ---------------- 配置 / 打印 ---------------- */

int app_doip_set_vin(app_doip_t *d, const char *vin)
{
    if ((d == NULL) || (vin == NULL) || (strlen(vin) != 17u)) {
        return -1;
    }
    memset(d->cfg.vin, 0, sizeof(d->cfg.vin));
    memcpy(d->cfg.vin, vin, 17u);
    return 0;
}

int app_doip_set_eid(app_doip_t *d, const uint8_t eid[6])
{
    if ((d == NULL) || (eid == NULL)) {
        return -1;
    }
    memcpy(d->cfg.eid, eid, 6u);
    return 0;
}

void app_doip_print(const app_doip_t *d)
{
    if (d == NULL) {
        return;
    }
    printf("[doip] running=%u ver=0x%02X entity=0x%04X tester=0x%04X port=%u max_conn=%u max_payload=%lu\r\n",
           (unsigned) d->running, (unsigned) d->cfg.version, (unsigned) d->cfg.entity_addr,
           (unsigned) d->cfg.tester_addr, (unsigned) APP_DOIP_PORT,
           (unsigned) d->cfg.max_conn, (unsigned long) d->cfg.max_payload);
    printf("[doip] VIN=%.17s\r\n", d->cfg.vin);
    printf("[doip] EID=%02X:%02X:%02X:%02X:%02X:%02X GID=%02X:%02X:%02X:%02X:%02X:%02X further=0x%02X\r\n",
           d->cfg.eid[0], d->cfg.eid[1], d->cfg.eid[2], d->cfg.eid[3], d->cfg.eid[4], d->cfg.eid[5],
           d->cfg.gid[0], d->cfg.gid[1], d->cfg.gid[2], d->cfg.gid[3], d->cfg.gid[4], d->cfg.gid[5],
           (unsigned) d->cfg.further_action);
    printf("[doip] timer initial=%lu ms general=%lu ms alive=%lu ms announce=%lu x %lu ms period=%lu ms\r\n",
           (unsigned long) d->cfg.initial_inactivity_ms,
           (unsigned long) d->cfg.general_inactivity_ms,
           (unsigned long) d->cfg.alive_check_ms,
           (unsigned long) d->cfg.announce_num,
           (unsigned long) d->cfg.announce_interval_ms,
           (unsigned long) d->cfg.announce_period_ms);
    printf("[doip] udp_rx=%lu ident=%lu ann_tx=%lu tcp_accept=%lu tcp_close=%lu\r\n",
           (unsigned long) d->st_udp_rx, (unsigned long) d->st_udp_ident_req,
           (unsigned long) d->st_announce_tx, (unsigned long) d->st_tcp_accept,
           (unsigned long) d->st_tcp_close);
    printf("[doip] route_ok=%lu route_deny=%lu diag_rx=%lu diag_tx=%lu big_msg=%lu\r\n",
           (unsigned long) d->st_route_ok, (unsigned long) d->st_route_deny,
           (unsigned long) d->st_diag_rx, (unsigned long) d->st_diag_tx,
           (unsigned long) d->st_big_msg);
    printf("[doip] alive_tx=%lu alive_ok=%lu alive_timeout=%lu initial_timeout=%lu general_timeout=%lu err_hdr=%lu\r\n",
           (unsigned long) d->st_alive_tx, (unsigned long) d->st_alive_ok,
           (unsigned long) d->st_alive_timeout, (unsigned long) d->st_initial_timeout,
           (unsigned long) d->st_general_timeout, (unsigned long) d->st_err_hdr);
}

void app_doip_print_conns(const app_doip_t *d)
{
    uint32_t i;
    uint32_t n = 0;

    if (d == NULL) {
        return;
    }
    for (i = 0; i < APP_DOIP_MAX_CONN; i++) {
        const app_doip_conn_t *c = &d->conn[i];
        if (c->state == APP_DOIP_CONN_CLOSED) {
            continue;
        }
        printf("[doip] conn%lu %-11s peer=%s:%u sa=0x%04X at=0x%02X age=%lu ms rx=%lu tx=%lu diag=%lu/%lu ack=%lu nack=%lu\r\n",
               (unsigned long) i, app_doip_conn_state_name(c->state), c->peer,
               (unsigned) c->peer_port, (unsigned) c->tester_addr,
               (unsigned) c->activation_type,
               (unsigned long) (now_ms() - c->state_since_ms),
               (unsigned long) c->st_rx_frames, (unsigned long) c->st_tx_frames,
               (unsigned long) c->st_rx_diag, (unsigned long) c->st_tx_diag,
               (unsigned long) c->st_tx_ack, (unsigned long) c->st_tx_nack);
        n++;
    }
    if (n == 0u) {
        printf("[doip] 没有活动连接\r\n");
    }
}
