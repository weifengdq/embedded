/*
 * app_can_gw.c - cannelloni v2 兼容的 CAN<->UDP 网关（裸机 lwIP）
 *
 * 参考实现：ref2/cannelloni/{cannelloni.h,parser.cpp,decoder.cpp,
 *           framebuffer.cpp,udpthread.cpp}（v2.1.2）
 * 关键点（已与主机 stock cannelloni 互通验证）：
 *   - 5 字节包头，count 用 htons（大端）；
 *   - can_id 用 htonl（大端），CAN_EFF_FLAG=0x80000000 / CAN_RTR_FLAG=0x40000000；
 *   - len 字节 bit7=0x80 表示 CAN FD，低 7 位是真实长度；FD 帧再跟 1 字节 flags；
 *   - RTR 帧不携带数据段；
 *   - UDP 路径只有 DATA，没有 ACK/NACK/重传。
 *
 * 作者：tp-infra
 */
#include <string.h>
#include <stdio.h>

#include "app_can_gw.h"
#include "app_lwip.h"

#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

typedef struct {
    bool inited;
    bool forwarding;
    app_can_gw_cfg_t cfg;
    app_can_t can[APP_CAN_GW_MAX_CH];
    struct udp_pcb *pcb[APP_CAN_GW_MAX_CH];
    ip_addr_t peer_addr;
    uint16_t peer_port;
    bool peer_known;
    uint8_t seq[APP_CAN_GW_MAX_CH];
    app_can_gw_stats_t st;
    uint8_t txbuf[APP_CAN_GW_PKT_MAX];
    uint8_t rxbuf[APP_CAN_GW_PKT_MAX];
} app_can_gw_t;

static app_can_gw_t s_gw;
/* udp_recv 回调的 arg：通道下标（静态地址，供 pcb->recv_arg 使用） */
static const int s_ch_arg[APP_CAN_GW_MAX_CH] = {0, 1, 2, 3};

/* ---------------- 字节序辅助（大端） ---------------- */

static void put_u16be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

static void put_u32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

static uint16_t get_u16be(const uint8_t *p)
{
    return (uint16_t) (((uint16_t) p[0] << 8) | p[1]);
}

static uint32_t get_u32be(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | (uint32_t) p[3];
}

/* ---------------- 帧编码/解码 ---------------- */

/* 一帧序列化，返回写入字节数；len 已保证 <= 64 */
static uint32_t gw_encode_frame(uint8_t *out, const app_can_frame_t *f)
{
    uint8_t *p = out;
    uint32_t can_id = f->id & 0x1FFFFFFFUL;

    if (f->ext) {
        can_id |= APP_CAN_GW_CAN_EFF_FLAG;
    }
    if (f->rtr) {
        can_id |= APP_CAN_GW_CAN_RTR_FLAG;
    }
    put_u32be(p, can_id);
    p += 4;
    if (f->fd) {
        *p++ = (uint8_t) (APP_CAN_GW_CANFD_FRAME | (f->len & 0x7FU));
        *p++ = f->brs ? APP_CAN_GW_CANFD_BRS : 0U;
    } else {
        *p++ = (uint8_t) (f->len & 0x7FU);
    }
    if (!f->rtr) {
        memcpy(p, f->data, f->len);
        p += f->len;
    }
    return (uint32_t) (p - out);
}

/* 从 UDP 载荷解一帧，返回消耗字节数，<0 表示非法 */
static int32_t gw_decode_frame(const uint8_t *in, const uint8_t *end, app_can_frame_t *f)
{
    const uint8_t *p = in;
    uint32_t can_id;
    uint8_t lenb;
    uint8_t len;

    if ((size_t) (end - in) < 5U) {
        return -1;
    }
    can_id = get_u32be(p);
    p += 4;
    lenb = *p++;
    len = (uint8_t) (lenb & 0x7FU);
    if (len > APP_CAN_MAX_DATA_LEN) {
        return -1;
    }
    memset(f, 0, sizeof(*f));
    f->id = can_id & 0x1FFFFFFFUL;
    f->ext = (can_id & APP_CAN_GW_CAN_EFF_FLAG) ? 1U : 0U;
    f->rtr = (can_id & APP_CAN_GW_CAN_RTR_FLAG) ? 1U : 0U;
    if (lenb & APP_CAN_GW_CANFD_FRAME) {
        uint8_t flags;
        if ((size_t) (end - p) < 1U) {
            return -1;
        }
        flags = *p++;
        f->fd = 1U;
        f->brs = (flags & APP_CAN_GW_CANFD_BRS) ? 1U : 0U;
    }
    if (!f->rtr) {
        if ((size_t) (end - p) < len) {
            return -1;
        }
        if (len > 0U) {
            memcpy(f->data, p, len);
        }
        p += len;
    } else {
        len = 0U;
    }
    f->len = len;
    return (int32_t) (p - in);
}

/* ---------------- UDP 接收（UDP -> CAN） ---------------- */

static void gw_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                        const ip_addr_t *addr, u16_t port)
{
    int ch = (int) (intptr_t) arg;
    uint8_t hdr[APP_CAN_GW_HDR_LEN];
    const uint8_t *buf;
    const uint8_t *end;
    uint16_t count;
    uint16_t i;
    uint32_t off = APP_CAN_GW_HDR_LEN;
    uint16_t total;

    (void) pcb;
    if (p == NULL) {
        return;
    }
    if ((ch < 0) || (ch >= APP_CAN_GW_MAX_CH)) {
        pbuf_free(p);
        return;
    }
    (void) addr;
    (void) port;

    /* 学习对端（主机 -l 4000 的源端口），回包按源地址发，不依赖固定配置 */
    if ((addr != NULL) && !s_gw.peer_known) {
        ip_addr_copy(s_gw.peer_addr, *addr);
        s_gw.peer_port = port;
        s_gw.peer_known = true;
    } else if ((addr != NULL) && s_gw.peer_known &&
               (!ip_addr_cmp(&s_gw.peer_addr, addr) || (s_gw.peer_port != port))) {
        /* 主机端口变了（重启 cannelloni）也跟着走 */
        ip_addr_copy(s_gw.peer_addr, *addr);
        s_gw.peer_port = port;
    }

    s_gw.st.rx_udp_pkts++;

    total = p->tot_len;
    if (total < APP_CAN_GW_HDR_LEN || total > (u16_t) sizeof(s_gw.rxbuf)) {
        s_gw.st.bad_pkt++;
        pbuf_free(p);
        return;
    }
    if (p->len == total) {
        buf = (const uint8_t *) p->payload;
    } else {
        /* UDP 载荷跨 pbuf 链时先拷贝到线性缓冲 */
        (void) pbuf_copy_partial(p, s_gw.rxbuf, total, 0);
        buf = s_gw.rxbuf;
    }
    end = buf + total;
    memcpy(hdr, buf, APP_CAN_GW_HDR_LEN);
    if ((hdr[0] != APP_CAN_GW_VERSION) || (hdr[1] != APP_CAN_GW_OP_DATA)) {
        s_gw.st.bad_pkt++;
        pbuf_free(p);
        return;
    }
    count = get_u16be(&hdr[3]);
    if (count == 0U) {
        pbuf_free(p);
        return;
    }

    if (!s_gw.forwarding) {
        pbuf_free(p);
        return;
    }

    for (i = 0U; i < count; i++) {
        app_can_frame_t f;
        int32_t used;
        if ((uint32_t) (end - buf) < off) {
            s_gw.st.bad_pkt++;
            break;
        }
        used = gw_decode_frame(buf + off, end, &f);
        if (used < 0) {
            s_gw.st.bad_pkt++;
            break;
        }
        off += (uint32_t) used;
        s_gw.st.rx_udp_frames++;
        if (app_can_send(&s_gw.can[ch], &f, 0U) == 0) {
            s_gw.st.tx_can_frames++;
        } else {
            s_gw.st.drop_can++;
        }
    }
    pbuf_free(p);
}

/* ---------------- CAN -> UDP ---------------- */

/* 把通道 ch 当前积压的 CAN 帧打包成一个 UDP 包发出去（最多 max_frames_per_pkt 帧） */
static void gw_flush_channel(int ch)
{
    uint8_t *pkt = s_gw.txbuf;
    uint32_t off = APP_CAN_GW_HDR_LEN;
    uint8_t n = 0U;
    uint8_t max = s_gw.cfg.max_frames_per_pkt;
    app_can_frame_t f;

    if ((ch < 0) || (ch >= APP_CAN_GW_MAX_CH)) {
        return;
    }
    if (max > APP_CAN_GW_MAX_FRAMES_PER_PKT) {
        max = APP_CAN_GW_MAX_FRAMES_PER_PKT;
    }
    while (n < max) {
        if (app_can_recv(&s_gw.can[ch], &f, 0U) != 0) {
            break;
        }
        off += gw_encode_frame(pkt + off, &f);
        n++;
        s_gw.st.rx_can_frames++;
    }
    if (n == 0U) {
        return;
    }
    pkt[0] = APP_CAN_GW_VERSION;
    pkt[1] = APP_CAN_GW_OP_DATA;
    pkt[2] = s_gw.seq[ch]++;
    put_u16be(&pkt[3], n);

    if (s_gw.peer_known && (s_gw.pcb[ch] != NULL)) {
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t) off, PBUF_RAM);
        if (p == NULL) {
            s_gw.st.drop_udp += n;
            return;
        }
        memcpy(p->payload, pkt, off);
        if (udp_sendto(s_gw.pcb[ch], p, &s_gw.peer_addr, s_gw.peer_port) == ERR_OK) {
            s_gw.st.tx_udp_pkts++;
            s_gw.st.tx_udp_frames += n;
        } else {
            s_gw.st.drop_udp += n;
        }
        pbuf_free(p);
    } else {
        /* 还没有对端（主机 cannelloni 未发包），帧直接丢，避免无限缓存 */
        s_gw.st.drop_udp += n;
    }
}

/* ---------------- 初始化 ---------------- */

static int gw_setup_channel(const app_can_gw_chan_cfg_t *cc)
{
    app_can_cfg_t ccfg = APP_CAN_CFG_DEFAULT;
    struct udp_pcb *pcb;
    ip_addr_t local;

    ccfg.speed = s_gw.cfg.bus_speed;
    if (app_can_init(&s_gw.can[cc->can_ch], (cc->can_ch == 0U) ? HPM_MCAN0 :
                     (cc->can_ch == 1U) ? HPM_MCAN1 :
                     (cc->can_ch == 2U) ? HPM_MCAN2 : HPM_MCAN3, &ccfg) != 0) {
        printf("[gw] CAN%u init FAILED\r\n", (unsigned) cc->can_ch);
        return -2;
    }

    pcb = udp_new();
    if (pcb == NULL) {
        return -3;
    }
    if (s_gw.cfg.local_ip != NULL) {
        if (!app_lwip_parse_ip(s_gw.cfg.local_ip, &local)) {
            udp_remove(pcb);
            return -1;
        }
    } else {
        ip_addr_set_any(false, &local);
    }
    if (udp_bind(pcb, &local, cc->udp_port) != ERR_OK) {
        udp_remove(pcb);
        printf("[gw] bind UDP %u FAILED\r\n", (unsigned) cc->udp_port);
        return -3;
    }
    udp_recv(pcb, gw_udp_recv, (void *) (intptr_t) s_ch_arg[cc->can_ch]);
    s_gw.pcb[cc->can_ch] = pcb;
    printf("[gw] ch%u: CAN%u <-> UDP %u (local %s)\r\n",
           (unsigned) cc->can_ch, (unsigned) cc->can_ch, (unsigned) cc->udp_port,
           (s_gw.cfg.local_ip != NULL) ? s_gw.cfg.local_ip : "0.0.0.0");
    return 0;
}

int app_can_gw_init(const app_can_gw_cfg_t *cfg)
{
    static const app_can_gw_cfg_t def = {
        APP_LWIP_DEFAULT_IP, NULL, 0, APP_CAN_SPEED_CLASSIC_500K,
        APP_CAN_GW_MAX_FRAMES_PER_PKT,
        { {1U, 4000U, true}, {0U, 0U, false}, {0U, 0U, false}, {0U, 0U, false} },
        1,
    };
    int i;

    if (cfg == NULL) {
        cfg = &def;
    }
    /* 允许运行期重配（gw full / gw lite）：先释放旧 socket，否则会泄漏 pcb */
    if (s_gw.inited) {
        for (i = 0; i < APP_CAN_GW_MAX_CH; i++) {
            if (s_gw.pcb[i] != NULL) {
                udp_remove(s_gw.pcb[i]);
                s_gw.pcb[i] = NULL;
            }
        }
    }
    memset(&s_gw, 0, sizeof(s_gw));
    s_gw.cfg = *cfg;
    s_gw.forwarding = true;
    if (s_gw.cfg.max_frames_per_pkt == 0U) {
        s_gw.cfg.max_frames_per_pkt = APP_CAN_GW_MAX_FRAMES_PER_PKT;
    }
    if (cfg->peer_ip != NULL) {
        if (!app_lwip_parse_ip(cfg->peer_ip, &s_gw.peer_addr)) {
            return -1;
        }
        s_gw.peer_port = cfg->peer_port;
        s_gw.peer_known = (cfg->peer_port != 0U);
    }
    for (i = 0; (i < cfg->ch_count) && (i < APP_CAN_GW_MAX_CH); i++) {
        if (cfg->ch[i].enabled) {
            int rc = gw_setup_channel(&cfg->ch[i]);
            if (rc != 0) {
                return rc;
            }
        }
    }
    s_gw.inited = true;
    printf("[gw] cannelloni v%u gateway ready, %d channel(s), peer=%s\r\n",
           (unsigned) APP_CAN_GW_VERSION, (int) cfg->ch_count,
           s_gw.peer_known ? "configured" : "auto-learn");
    return 0;
}

void app_can_gw_poll(void)
{
    int i;
    if (!s_gw.inited) {
        return;
    }
    /* pcb[] 按 CAN 通道号索引（cfg.ch 的位置不一定等于 can_ch） */
    for (i = 0; i < APP_CAN_GW_MAX_CH; i++) {
        if (s_gw.pcb[i] != NULL) {
            gw_flush_channel(i);
        }
    }
}

/* ---------------- 便利 API ---------------- */

void app_can_gw_stats(app_can_gw_stats_t *st)
{
    if (st != NULL) {
        *st = s_gw.st;
    }
}

void app_can_gw_print(void)
{
    printf("[gw] fwd=%u udp_rx=%lu pkt/%lu frame, udp_tx=%lu pkt/%lu frame, can_rx=%lu can_tx=%lu drop_can=%lu drop_udp=%lu bad=%lu\r\n",
           s_gw.forwarding ? 1U : 0U,
           (unsigned long) s_gw.st.rx_udp_pkts, (unsigned long) s_gw.st.rx_udp_frames,
           (unsigned long) s_gw.st.tx_udp_pkts, (unsigned long) s_gw.st.tx_udp_frames,
           (unsigned long) s_gw.st.rx_can_frames, (unsigned long) s_gw.st.tx_can_frames,
           (unsigned long) s_gw.st.drop_can, (unsigned long) s_gw.st.drop_udp,
           (unsigned long) s_gw.st.bad_pkt);
}

void app_can_gw_set_forward(bool on)
{
    s_gw.forwarding = on;
}

bool app_can_gw_forwarding(void)
{
    return s_gw.forwarding;
}

uint16_t app_can_gw_port(int ch)
{
    int i;
    if ((ch < 0) || (ch >= APP_CAN_GW_MAX_CH) || (s_gw.pcb[ch] == NULL)) {
        return 0U;
    }
    for (i = 0; (i < s_gw.cfg.ch_count) && (i < APP_CAN_GW_MAX_CH); i++) {
        if ((int) s_gw.cfg.ch[i].can_ch == ch) {
            return s_gw.cfg.ch[i].udp_port;
        }
    }
    return 0U;
}

int app_can_gw_set_peer(const char *ip_str, uint16_t port)
{
    if (ip_str == NULL) {
        return -1;
    }
    if (!app_lwip_parse_ip(ip_str, &s_gw.peer_addr)) {
        return -1;
    }
    s_gw.peer_port = port;
    s_gw.peer_known = true;
    return 0;
}

const char *app_can_gw_peer_str(void)
{
    static char buf[48];
    if (!s_gw.peer_known) {
        return "(none)";
    }
    snprintf(buf, sizeof(buf), "%s:%u", ipaddr_ntoa(&s_gw.peer_addr), (unsigned) s_gw.peer_port);
    return buf;
}