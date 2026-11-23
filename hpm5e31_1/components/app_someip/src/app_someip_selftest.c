/*
 * app_someip_selftest.c —— 协议层自证测试（不需要上位机，串口直接出 PASS/FAIL）
 *
 * 为什么需要它：SOME/IP 的坑大多集中在"字节序 / Length 语义 / SD option 长度 / TP offset"，
 * 这些都能在板端用"编码 -> 捕获发送 -> 回灌解析"的闭环验证，不必每次都上电连主机。
 * 上板后的第一条命令就是 selftest，10 ms 内出结论。
 *
 * 覆盖：
 *   T1 报文头 16 B 编解码往返
 *   T2 非法报文（太短 / Length 与实际不符 / 协议版本错）必须被拒绝
 *   T3 OfferService 报文：SD 解析回读条目与 IPv4 endpoint option
 *   T4 SOME/IP-TP：4 KB 载荷分段 -> 捕获 -> 重组 -> CRC 校验
 *   T5 未知方法 -> ERROR(0x03)
 *   T6 未知服务 -> ERROR(0x02)
 *   T7 收到 FindService -> 回 OfferService
 */
#include <string.h>
#include <stdio.h>
#include "app_someip.h"
#include "app_someip_wire.h"
#include "app_someip_sd.h"
#include "app_someip_tp.h"

#define ST_SLOTS      8
#define ST_SLOT_LEN   1500

typedef struct {
    uint8_t  buf[ST_SLOTS][ST_SLOT_LEN];
    uint16_t len[ST_SLOTS];
    uint32_t count;
} st_capture_t;

static st_capture_t st_cap;

static int st_cap_send(void *ctx, const uint8_t dst_ip[4], uint16_t dst_port,
                       const uint8_t *buf, uint16_t len)
{
    st_capture_t *c = (st_capture_t *)ctx;
    (void)dst_ip;
    (void)dst_port;
    if ((c->count < ST_SLOTS) && (len <= ST_SLOT_LEN)) {
        memcpy(c->buf[c->count], buf, len);
        c->len[c->count] = len;
    }
    c->count++;
    return 0;
}

/* 手工构造一个 FindService SD 报文（验证 SD 接收路径） */
static uint16_t st_build_find(uint8_t *buf, uint16_t cap, uint16_t service, uint16_t instance,
                              uint8_t major)
{
    uint8_t *p;
    uint16_t plen;
    if (cap < 64U) {
        return 0U;
    }
    p = &buf[SOMEIP_HEADER_LEN];
    p[0] = 0xC0U;
    p[1] = 0U;
    p[2] = 0U;
    p[3] = 0U;
    someip_put_u32(&p[4], 16U);
    memset(&p[8], 0, 16U);
    p[8] = SOMEIP_SD_ENTRY_FIND_SERVICE;
    someip_put_u16(&p[12], service);
    someip_put_u16(&p[14], instance);
    p[16] = major;
    someip_put_u24(&p[17], 3U);
    someip_put_u32(&p[20], 0U);
    someip_put_u32(&p[24], 0U);
    plen = 28U;
    (void)app_someip_encode_header(buf, cap, SOMEIP_SD_SERVICE_ID, SOMEIP_SD_METHOD_ID,
                                  0x0000U, 1U, SOMEIP_SD_INTERFACE_VERSION,
                                  SOMEIP_MT_NOTIFICATION, SOMEIP_RC_OK, plen);
    return (uint16_t)(SOMEIP_HEADER_LEN + plen);
}
int app_someip_selftest(app_someip_t *s, bool verbose)
{
    int fails = 0;
    someip_send_fn saved_send = s->cfg.send;
    void *saved_ctx = s->cfg.net_ctx;

    if (verbose) {
        printf("[selftest] someip %s, service 0x%04X instance 0x%04X\r\n",
               app_someip_ver_string(), (unsigned)s->cfg.service_id, (unsigned)s->cfg.instance_id);
    }
    s->cfg.send = st_cap_send;
    s->cfg.net_ctx = &st_cap;

    /* ---------- T1 报文头编解码往返 ---------- */
    {
        uint8_t buf[64];
        someip_msg_t m;
        uint16_t n = app_someip_encode_header(buf, sizeof(buf), 0x1234U, 0x0001U, 0x00A5U, 0x0102U,
                                             0x01U, SOMEIP_MT_RESPONSE, SOMEIP_RC_OK, 4U);
        memset(&buf[n], 0x5A, 4U);
        int ok = (n == SOMEIP_HEADER_LEN) && (app_someip_decode(buf, (uint16_t)(n + 4U), &m) == 0) &&
                 (m.service_id == 0x1234U) && (m.method_id == 0x0001U) && (m.client_id == 0x00A5U) &&
                 (m.session_id == 0x0102U) && (m.message_type == SOMEIP_MT_RESPONSE) &&
                 (m.length == 12U) && (m.payload_len == 4U) && (m.payload[0] == 0x5AU);
        printf("[selftest] T1 header codec      : %s\r\n", ok ? "PASS" : "FAIL");
        fails += ok ? 0 : 1;
    }

    /* ---------- T2 非法报文必须被拒 ---------- */
    {
        uint8_t buf[64];
        someip_msg_t m;
        (void)app_someip_encode_header(buf, sizeof(buf), 0x1234U, 0x0001U, 1U, 1U, 0x01U,
                                       SOMEIP_MT_REQUEST, SOMEIP_RC_OK, 8U);
        int ok = (app_someip_decode(buf, 10U, &m) != 0);
        ok = ok && (app_someip_decode(buf, 20U, &m) != 0);
        buf[12] = 0x02U;
        ok = ok && (app_someip_decode(buf, 24U, &m) != 0);
        printf("[selftest] T2 malformed reject  : %s\r\n", ok ? "PASS" : "FAIL");
        fails += ok ? 0 : 1;
    }

    /* ---------- T3 OfferService 编码/解析 ---------- */
    {
        st_cap.count = 0;
        int r = app_someip_offer(s);
        int ok = (r == 0) && (st_cap.count == 1U);
        if (ok) {
            someip_msg_t m;
            someip_sd_msg_t sd;
            ok = (app_someip_decode(st_cap.buf[0], st_cap.len[0], &m) == 0) &&
                 (m.service_id == SOMEIP_SD_SERVICE_ID) && (m.method_id == SOMEIP_SD_METHOD_ID) &&
                 (app_someip_sd_parse(m.payload, m.payload_len, &sd) == 0) &&
                 (sd.n_entries == 1) && (sd.entries[0].type == SOMEIP_SD_ENTRY_OFFER_SERVICE) &&
                 (sd.entries[0].service_id == s->cfg.service_id) &&
                 (sd.entries[0].instance_id == s->cfg.instance_id) &&
                 (sd.entries[0].ttl == s->cfg.offer_ttl_s) && (sd.n_options == 1) &&
                 (memcmp(sd.options[0].ip, s->cfg.local_ip, 4) == 0) &&
                 (sd.options[0].proto == SOMEIP_SD_PROTO_UDP) &&
                 (sd.options[0].port == s->cfg.service_port);
            if (ok && verbose) {
                printf("[selftest]   offer %u bytes, ttl %lus, endpoint %u.%u.%u.%u:%u/udp\r\n",
                       (unsigned)st_cap.len[0], (unsigned long)sd.entries[0].ttl,
                       (unsigned)sd.options[0].ip[0], (unsigned)sd.options[0].ip[1],
                       (unsigned)sd.options[0].ip[2], (unsigned)sd.options[0].ip[3],
                       (unsigned)sd.options[0].port);
            }
        }
        printf("[selftest] T3 SD offer encode  : %s\r\n", ok ? "PASS" : "FAIL");
        fails += ok ? 0 : 1;
    }
    /* ---------- T4 SOME/IP-TP 4 KB 分段/重组 ---------- */
    {
        static uint8_t payload[SOMEIP_MAX_PAYLOAD];
        int ok = (s->tp != NULL);
        if (ok) {
            for (uint32_t i = 0; i < sizeof(payload); i++) {
                payload[i] = (uint8_t)((i * 31U + 7U) & 0xFFU);
            }
            uint32_t crc = someip_crc32(payload, (uint16_t)sizeof(payload));
            st_cap.count = 0;
            int segs = app_someip_tp_send(s, s->cfg.local_ip, s->cfg.service_port, 0x0006U,
                                          0x00A5U, 0x0001U, SOMEIP_MT_RESPONSE, SOMEIP_RC_OK,
                                          payload, (uint16_t)sizeof(payload));
            ok = (segs == 3) && (st_cap.count == 3U);
            if (ok) {
                int total = 0;
                app_someip_tp_reasm_reset(s->tp);
                for (uint32_t i = 0; i < st_cap.count; i++) {
                    someip_msg_t m;
                    if (app_someip_decode(st_cap.buf[i], st_cap.len[i], &m) != 0) {
                        ok = false;
                        break;
                    }
                    int r2 = app_someip_tp_feed(s->tp, &m, 0U);
                    if (r2 < 0) {
                        ok = false;
                        break;
                    }
                    if (r2 > 0) {
                        total = r2;
                    }
                }
                ok = ok && (total == (int)sizeof(payload)) &&
                     (someip_crc32(s->tp->buf, (uint16_t)total) == crc) &&
                     (memcmp(s->tp->buf, payload, sizeof(payload)) == 0);
                if (ok && verbose) {
                    printf("[selftest]   tp %d segs -> %d bytes, crc 0x%08lX verified\r\n",
                           segs, total, (unsigned long)crc);
                }
            }
        }
        printf("[selftest] T4 SOMEIP TP 4KB    : %s\r\n", ok ? "PASS" : "FAIL");
        fails += ok ? 0 : 1;
    }

    /* ---------- T5 未知方法 -> ERROR(0x03) ---------- */
    {
        uint8_t buf[64];
        someip_msg_t m;
        uint16_t n = app_someip_encode_header(buf, sizeof(buf), s->cfg.service_id, 0x00FEU,
                                              0x00A5U, 0x0009U, s->cfg.interface_version,
                                              SOMEIP_MT_REQUEST, SOMEIP_RC_OK, 0U);
        st_cap.count = 0;
        app_someip_on_rx(s, s->cfg.local_ip, 30501U, buf, n);
        int ok = (st_cap.count == 1U) &&
                 (app_someip_decode(st_cap.buf[0], st_cap.len[0], &m) == 0) &&
                 (m.message_type == SOMEIP_MT_ERROR) &&
                 (m.return_code == SOMEIP_RC_UNKNOWN_METHOD) && (m.session_id == 0x0009U);
        printf("[selftest] T5 unknown method   : %s\r\n", ok ? "PASS" : "FAIL");
        fails += ok ? 0 : 1;
    }

    /* ---------- T6 未知服务 -> ERROR(0x02) ---------- */
    {
        uint8_t buf[64];
        someip_msg_t m;
        uint16_t n = app_someip_encode_header(buf, sizeof(buf), 0x9A9AU, 0x0001U,
                                              0x00A5U, 0x000AU, s->cfg.interface_version,
                                              SOMEIP_MT_REQUEST, SOMEIP_RC_OK, 0U);
        st_cap.count = 0;
        app_someip_on_rx(s, s->cfg.local_ip, 30501U, buf, n);
        int ok = (st_cap.count == 1U) &&
                 (app_someip_decode(st_cap.buf[0], st_cap.len[0], &m) == 0) &&
                 (m.message_type == SOMEIP_MT_ERROR) &&
                 (m.return_code == SOMEIP_RC_UNKNOWN_SERVICE);
        printf("[selftest] T6 unknown service  : %s\r\n", ok ? "PASS" : "FAIL");
        fails += ok ? 0 : 1;
    }

    /* ---------- T7 FindService -> OfferService ---------- */
    {
        uint8_t buf[64];
        someip_msg_t m;
        uint16_t n = st_build_find(buf, sizeof(buf), s->cfg.service_id, s->cfg.instance_id,
                                   s->cfg.major_version);
        st_cap.count = 0;
        app_someip_on_rx(s, s->cfg.local_ip, s->cfg.sd_port, buf, n);
        int ok = (n > 0U) && (st_cap.count == 1U) &&
                 (app_someip_decode(st_cap.buf[0], st_cap.len[0], &m) == 0) &&
                 (m.service_id == SOMEIP_SD_SERVICE_ID);
        if (ok) {
            someip_sd_msg_t sd;
            ok = (app_someip_sd_parse(m.payload, m.payload_len, &sd) == 0) &&
                 (sd.n_entries == 1) &&
                 (sd.entries[0].type == SOMEIP_SD_ENTRY_OFFER_SERVICE);
        }
        printf("[selftest] T7 find to offer    : %s\r\n", ok ? "PASS" : "FAIL");
        fails += ok ? 0 : 1;
    }

    s->cfg.send = saved_send;
    s->cfg.net_ctx = saved_ctx;
    printf("[selftest] %s (%d failed)\r\n", (fails == 0) ? "PASS" : "FAIL", fails);
    return fails;
}