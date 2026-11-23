/*
 * app_doip_wire.c - DoIP 报文编解码（纯函数实现 + 固定向量自检）
 *
 * 这里只做"字节 <-> 结构体"的机械活，不碰 socket、不碰时间。
 * 好处：上板前的逻辑错误全部能在自检里暴露（samples/app/doip_server 的 selftest 命令），
 * 主机侧 host/doip/doip_client.py 用同一套字节序实现，两边可以互相印证。
 *
 * 作者：doip（task-3）
 */
#include <string.h>

#include "app_doip_wire.h"

uint16_t app_doip_get_u16(const uint8_t *p)
{
    return (uint16_t) (((uint16_t) p[0] << 8) | (uint16_t) p[1]);
}

uint32_t app_doip_get_u32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | (uint32_t) p[3];
}

void app_doip_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

void app_doip_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

uint32_t app_doip_hdr_encode(uint8_t *buf, uint32_t cap, uint16_t pt, uint32_t payload_len,
                             uint8_t version)
{
    if ((buf == NULL) || (cap < APP_DOIP_HDR_LEN)) {
        return 0u;
    }
    buf[0] = version;
    buf[1] = APP_DOIP_VER_INV(version);
    app_doip_put_u16(&buf[2], pt);
    app_doip_put_u32(&buf[4], payload_len);
    return APP_DOIP_HDR_LEN;
}

bool app_doip_hdr_decode(const uint8_t *buf, app_doip_hdr_t *h, uint8_t *nack)
{
    uint8_t code = APP_DOIP_NACK_INC_PATTERN;

    if ((buf == NULL) || (h == NULL)) {
        return false;
    }
    h->version = buf[0];
    h->version_inv = buf[1];
    h->payload_type = app_doip_get_u16(&buf[2]);
    h->payload_len = app_doip_get_u32(&buf[4]);
    /* 版本取反必须严格是 ~版本；版本只接受 0x02/0x03/0xFF（0xFF 只出现在请求里） */
    if ((uint8_t) (h->version ^ h->version_inv) != 0xFFu) {
        code = APP_DOIP_NACK_INC_PATTERN;
        goto fail;
    }
    if ((h->version != APP_DOIP_VER_2012) && (h->version != APP_DOIP_VER_2019) &&
        (h->version != APP_DOIP_VER_ANY)) {
        code = APP_DOIP_NACK_INC_PATTERN;
        goto fail;
    }
    if (nack != NULL) {
        *nack = 0u;
    }
    return true;

fail:
    if (nack != NULL) {
        *nack = code;
    }
    return false;
}

uint32_t app_doip_build_vehicle_announce(uint8_t *buf, uint32_t cap,
                                         const char *vin, uint16_t entity_addr,
                                         const uint8_t eid[6], const uint8_t gid[6],
                                         uint8_t further_action, uint8_t version,
                                         uint8_t sync_status)
{
    uint32_t len = (version == APP_DOIP_VER_2019) ? 33u : 32u;

    if ((buf == NULL) || (vin == NULL) || (eid == NULL) || (gid == NULL) || (cap < len)) {
        return 0u;
    }
    memset(buf, 0, len);
    memcpy(buf, vin, 17u);                       /* 0..16  VIN（ASCII，不满 17 位补 0） */
    app_doip_put_u16(&buf[17], entity_addr);     /* 17..18 实体逻辑地址 */
    memcpy(&buf[19], eid, 6u);                   /* 19..24 EID（一般用 MAC） */
    memcpy(&buf[25], gid, 6u);                   /* 25..30 GID（同组实体共享） */
    buf[31] = further_action;                    /* 31     进一步动作 0x00/0x10 */
    if (len > 32u) {
        buf[32] = sync_status;                   /* 32     2019：VIN/GID 同步状态 */
    }
    return len;
}

uint32_t app_doip_build_ra_resp(uint8_t *buf, uint32_t cap, uint16_t tester_sa,
                                uint16_t entity_addr, uint8_t code)
{
    if ((buf == NULL) || (cap < 13u)) {
        return 0u;
    }
    app_doip_put_u16(&buf[0], tester_sa);        /* 0..1  tester 逻辑地址 */
    app_doip_put_u16(&buf[2], entity_addr);      /* 2..3  实体逻辑地址 */
    buf[4] = code;                               /* 4     响应码 */
    memset(&buf[5], 0, 4u);                      /* 5..8  ISO 保留 */
    memset(&buf[9], 0, 4u);                      /* 9..12 OEM 特定 */
    return 13u;
}

bool app_doip_parse_ra_req(const uint8_t *buf, uint32_t len, app_doip_ra_req_t *out)
{
    if ((buf == NULL) || (out == NULL)) {
        return false;
    }
    /* ISO 13400-2 规定 11 B（含 4 B OEM）；实测 EcuBus-Pro 等工具默认发 7 B，
     * 所以两种都收，>=11 才算带 OEM（第 32 章“坑”里记录了这一点）。 */
    if ((len != 7u) && (len != 11u)) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->sa = app_doip_get_u16(&buf[0]);
    out->activation_type = buf[2];
    if (len == 11u) {
        out->has_oem = true;
        memcpy(out->oem, &buf[7], 4u);
    }
    return true;
}

uint32_t app_doip_build_diag(uint8_t *buf, uint32_t cap, uint16_t sa, uint16_t ta,
                             const uint8_t *uds, uint32_t uds_len)
{
    if ((buf == NULL) || (cap < (4u + uds_len)) || ((uds == NULL) && (uds_len != 0u))) {
        return 0u;
    }
    app_doip_put_u16(&buf[0], sa);
    app_doip_put_u16(&buf[2], ta);
    if (uds_len != 0u) {
        memcpy(&buf[4], uds, uds_len);
    }
    return 4u + uds_len;
}

uint32_t app_doip_build_diag_ack(uint8_t *buf, uint32_t cap, uint16_t sa, uint16_t ta,
                                 uint8_t code)
{
    if ((buf == NULL) || (cap < 5u)) {
        return 0u;
    }
    app_doip_put_u16(&buf[0], sa);
    app_doip_put_u16(&buf[2], ta);
    buf[4] = code;
    return 5u;
}

uint32_t app_doip_build_entity_status(uint8_t *buf, uint32_t cap, uint8_t node_type,
                                      uint8_t max_sockets, uint8_t open_sockets,
                                      uint32_t max_data_size)
{
    if ((buf == NULL) || (cap < 7u)) {
        return 0u;
    }
    buf[0] = node_type;
    buf[1] = max_sockets;
    buf[2] = open_sockets;
    app_doip_put_u32(&buf[3], max_data_size);    /* 单条诊断报文最大字节数（ISO 叫 max data size） */
    return 7u;
}

uint32_t app_doip_build_power_mode(uint8_t *buf, uint32_t cap, uint8_t mode)
{
    if ((buf == NULL) || (cap < 1u)) {
        return 0u;
    }
    buf[0] = mode;
    return 1u;
}

uint32_t app_doip_build_generic_nack(uint8_t *buf, uint32_t cap, uint8_t code)
{
    if ((buf == NULL) || (cap < 1u)) {
        return 0u;
    }
    buf[0] = code;
    return 1u;
}

const char *app_doip_pt_name(uint16_t pt)
{
    switch (pt) {
    case APP_DOIP_PT_GENERIC_NACK:       return "generic-nack(0000)";
    case APP_DOIP_PT_VEHICLE_ID_REQ:     return "vehicle-id-req(0001)";
    case APP_DOIP_PT_VEHICLE_ID_REQ_EID: return "vehicle-id-req-eid(0002)";
    case APP_DOIP_PT_VEHICLE_ID_REQ_VIN: return "vehicle-id-req-vin(0003)";
    case APP_DOIP_PT_VEHICLE_ANNOUNCE:   return "vehicle-announce(0004)";
    case APP_DOIP_PT_ROUTING_ACT_REQ:    return "routing-act-req(0005)";
    case APP_DOIP_PT_ROUTING_ACT_RESP:   return "routing-act-resp(0006)";
    case APP_DOIP_PT_ALIVE_CHECK_REQ:    return "alive-check-req(0007)";
    case APP_DOIP_PT_ALIVE_CHECK_RESP:   return "alive-check-resp(0008)";
    case APP_DOIP_PT_ENTITY_STATUS_REQ:  return "entity-status-req(4001)";
    case APP_DOIP_PT_ENTITY_STATUS_RESP: return "entity-status-resp(4002)";
    case APP_DOIP_PT_POWER_MODE_REQ:     return "power-mode-req(4003)";
    case APP_DOIP_PT_POWER_MODE_RESP:    return "power-mode-resp(4004)";
    case APP_DOIP_PT_DIAG_MESSAGE:       return "diag-message(8001)";
    case APP_DOIP_PT_DIAG_ACK:           return "diag-ack(8002)";
    case APP_DOIP_PT_DIAG_NACK:          return "diag-nack(8003)";
    default:                             return "unknown";
    }
}

/* ---------------- 固定向量自检 ---------------- */

#define CHECK(cond) do {                                   \
        (*checks)++;                                       \
        if (!(cond)) {                                     \
            (*fail)++;                                     \
        }                                                  \
    } while (0)

int app_doip_wire_selftest(uint32_t *checks, uint32_t *fail)
{
    static const uint8_t eid[6] = {0x02u, 0x00u, 0x5Eu, 0x10u, 0x20u, 0x30u};
    static const uint8_t gid[6] = {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u};
    static const char vin[17] = "HPM5E31DOIP00001";
    uint8_t buf[64];
    uint8_t extra[4];
    app_doip_hdr_t h;
    app_doip_ra_req_t ra;
    uint8_t nack = 0xEEu;
    uint32_t n;

    if (checks == NULL) {
        return -1;
    }
    *checks = 0u;
    *fail = 0u;

    /* 1) 通用头 8 B / 大端 / 版本取反 */
    memset(buf, 0, sizeof(buf));
    n = app_doip_hdr_encode(buf, sizeof(buf), APP_DOIP_PT_DIAG_MESSAGE, 0x00000404u, APP_DOIP_VER_2012);
    CHECK(n == 8u);
    CHECK(buf[0] == 0x02u);
    CHECK(buf[1] == 0xFDu);
    CHECK(buf[2] == 0x80u && buf[3] == 0x01u);
    CHECK(buf[4] == 0x00u && buf[5] == 0x00u && buf[6] == 0x04u && buf[7] == 0x04u);

    /* 2) 头解码回读 + 取反错误必须被拒 */
    CHECK(app_doip_hdr_decode(buf, &h, &nack) == true);
    CHECK(h.payload_type == APP_DOIP_PT_DIAG_MESSAGE);
    CHECK(h.payload_len == 0x00000404u);
    CHECK(h.version == APP_DOIP_VER_2012);
    buf[1] = 0x00u;                        /* 故意写错取反字节 */
    CHECK(app_doip_hdr_decode(buf, &h, &nack) == false);
    CHECK(nack == APP_DOIP_NACK_INC_PATTERN);
    buf[1] = 0xFDu;
    buf[0] = 0x04u;                        /* 未知版本 */
    buf[1] = APP_DOIP_VER_INV(0x04u);
    CHECK(app_doip_hdr_decode(buf, &h, &nack) == false);

    /* 3) 车辆声明：2012 = 32 B，2019 = 33 B，VIN 在最前 */
    memset(buf, 0, sizeof(buf));
    n = app_doip_build_vehicle_announce(buf, sizeof(buf), vin, 0x1000u, eid, gid, 0x00u,
                                        APP_DOIP_VER_2012, 0u);
    CHECK(n == 32u);
    CHECK(memcmp(buf, vin, 17u) == 0);
    CHECK(app_doip_get_u16(&buf[17]) == 0x1000u);
    CHECK(memcmp(&buf[19], eid, 6u) == 0);
    CHECK(memcmp(&buf[25], gid, 6u) == 0);
    CHECK(buf[31] == 0x00u);
    n = app_doip_build_vehicle_announce(buf, sizeof(buf), vin, 0x1000u, eid, gid, 0x10u,
                                        APP_DOIP_VER_2019, 0x01u);
    CHECK(n == 33u);
    CHECK(buf[31] == 0x10u && buf[32] == 0x01u);

    /* 4) 路由激活请求：7 B（无 OEM）与 11 B（带 OEM） */
    app_doip_put_u16(&buf[0], 0x0E00u);
    buf[2] = APP_DOIP_AT_DEFAULT;
    memset(&buf[3], 0, 4u);
    buf[7] = 0xAAu; buf[8] = 0xBBu; buf[9] = 0xCCu; buf[10] = 0xDDu;
    CHECK(app_doip_parse_ra_req(buf, 7u, &ra) == true);
    CHECK(ra.sa == 0x0E00u && ra.activation_type == 0x00u && ra.has_oem == false);
    CHECK(app_doip_parse_ra_req(buf, 11u, &ra) == true);
    CHECK(ra.has_oem == true);
    memcpy(extra, ra.oem, 4u);
    CHECK(extra[0] == 0xAAu && extra[3] == 0xDDu);
    CHECK(app_doip_parse_ra_req(buf, 9u, &ra) == false);   /* 长度非法 */

    /* 5) 路由激活响应 13 B */
    memset(buf, 0, sizeof(buf));
    n = app_doip_build_ra_resp(buf, sizeof(buf), 0x0E00u, 0x1000u, APP_DOIP_RA_OK);
    CHECK(n == 13u);
    CHECK(app_doip_get_u16(&buf[0]) == 0x0E00u);
    CHECK(app_doip_get_u16(&buf[2]) == 0x1000u);
    CHECK(buf[4] == APP_DOIP_RA_OK);
    CHECK(buf[5] == 0u && buf[12] == 0u);

    /* 6) 诊断报文：SA/TA/数据 */
    memset(buf, 0, sizeof(buf));
    n = app_doip_build_diag(buf, sizeof(buf), 0x1000u, 0x0E00u,
                            (const uint8_t *) "\x62\xF1\x90", 3u);
    CHECK(n == 7u);
    CHECK(app_doip_get_u16(&buf[0]) == 0x1000u);
    CHECK(app_doip_get_u16(&buf[2]) == 0x0E00u);
    CHECK(buf[4] == 0x62u && buf[6] == 0x90u);
    n = app_doip_build_diag(buf, sizeof(buf), 0x1000u, 0x0E00u, NULL, 0u);
    CHECK(n == 4u);                        /* 只有 SA/TA 也合法：UDS 空报文 */

    /* 7) 诊断应答 5 B */
    n = app_doip_build_diag_ack(buf, sizeof(buf), 0x1000u, 0x0E00u, APP_DOIP_DIAG_ACK_OK);
    CHECK(n == 5u);
    CHECK(app_doip_get_u16(&buf[0]) == 0x1000u);
    CHECK(app_doip_get_u16(&buf[2]) == 0x0E00u);
    CHECK(buf[4] == APP_DOIP_DIAG_ACK_OK);

    /* 8) 实体状态 7 B / 电源模式 1 B / 通用 NACK 1 B */
    n = app_doip_build_entity_status(buf, sizeof(buf), APP_DOIP_NODE_DOIP_NODE, 3u, 1u, 4096u);
    CHECK(n == 7u);
    CHECK(buf[0] == 0x00u && buf[1] == 0x03u && buf[2] == 0x01u);
    CHECK(app_doip_get_u32(&buf[3]) == 4096u);
    CHECK(app_doip_build_power_mode(buf, sizeof(buf), APP_DOIP_PWR_READY) == 1u);
    CHECK(buf[0] == 0x01u);
    CHECK(app_doip_build_generic_nack(buf, sizeof(buf), APP_DOIP_NACK_UNKNOWN_PT) == 1u);
    CHECK(buf[0] == 0x01u);

    /* 9) 容量不足按 0 返回（不做越界写） */
    CHECK(app_doip_build_diag(buf, 3u, 0x1000u, 0x0E00u, NULL, 0u) == 0u);
    CHECK(app_doip_build_ra_resp(buf, 12u, 0, 0, 0) == 0u);

    return 0;
}
