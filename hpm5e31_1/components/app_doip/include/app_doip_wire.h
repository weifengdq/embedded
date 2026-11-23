/*
 * app_doip_wire.h - DoIP(ISO 13400-2) 报文编解码：纯函数，不依赖 lwIP，可直接单元测试
 *
 * 所有字段大端（网络序）。通用头固定 8 字节：
 *   +-------+-------+---------+--------------------------------------+
 *   | 0     | 1     | 2..3    | 4..7                                 |
 *   | 版本  | 取反  | 载荷类型| 载荷长度（不含头，最大 2^32-1）      |
 *   +-------+-------+---------+--------------------------------------+
 * 版本 0x02 = ISO 13400-2:2012，取反字节必须 = 0xFD（0x02 ^ 0xFF）；
 * 版本 0x03 = ISO 13400-2:2019（多一个 VIN/GID 同步状态字节），取反 = 0xFC。
 *
 * 作者：doip（task-3）
 */
#ifndef APP_DOIP_WIRE_H
#define APP_DOIP_WIRE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_DOIP_HDR_LEN        8u
#define APP_DOIP_PORT           13400u

/* ---- 协议版本 / 取反 ---- */
#define APP_DOIP_VER_2012       0x02u   /* ISO 13400-2:2012 */
#define APP_DOIP_VER_2019       0x03u   /* ISO 13400-2:2019，载荷比 2012 多 1 B 同步状态 */
#define APP_DOIP_VER_ANY        0xFFu   /* 请求里表示“任何版本都行” */
#define APP_DOIP_VER_INV(v)     ((uint8_t) ((v) ^ 0xFFu))

/* ---- 载荷类型（ISO 13400-2 表 6/7/8；注意不是 0x0001 起连续编号） ---- */
#define APP_DOIP_PT_GENERIC_NACK        0x0000u  /* UDP/TCP 通用头 NACK */
#define APP_DOIP_PT_VEHICLE_ID_REQ      0x0001u  /* UDP 车辆识别请求（无参数） */
#define APP_DOIP_PT_VEHICLE_ID_REQ_EID  0x0002u  /* UDP 带 EID 的识别请求（6 B） */
#define APP_DOIP_PT_VEHICLE_ID_REQ_VIN  0x0003u  /* UDP 带 VIN 的识别请求（17 B） */
#define APP_DOIP_PT_VEHICLE_ANNOUNCE    0x0004u  /* UDP 车辆声明/识别响应 */
#define APP_DOIP_PT_ROUTING_ACT_REQ     0x0005u  /* TCP 路由激活请求 */
#define APP_DOIP_PT_ROUTING_ACT_RESP    0x0006u  /* TCP 路由激活响应 */
#define APP_DOIP_PT_ALIVE_CHECK_REQ     0x0007u  /* TCP alive check 请求（实体→tester） */
#define APP_DOIP_PT_ALIVE_CHECK_RESP    0x0008u  /* TCP alive check 响应（2 B 源地址） */
#define APP_DOIP_PT_ENTITY_STATUS_REQ   0x4001u  /* UDP DoIP 实体状态请求 */
#define APP_DOIP_PT_ENTITY_STATUS_RESP  0x4002u  /* UDP DoIP 实体状态响应 */
#define APP_DOIP_PT_POWER_MODE_REQ      0x4003u  /* UDP 诊断电源模式请求 */
#define APP_DOIP_PT_POWER_MODE_RESP     0x4004u  /* UDP 诊断电源模式响应 */
#define APP_DOIP_PT_DIAG_MESSAGE        0x8001u  /* TCP 诊断报文（SA+TA+UDS 数据） */
#define APP_DOIP_PT_DIAG_ACK            0x8002u  /* TCP 诊断报文肯定应答 */
#define APP_DOIP_PT_DIAG_NACK           0x8003u  /* TCP 诊断报文否定应答 */

/* ---- 通用头 NACK 码（0x0000 载荷 1 B） ---- */
#define APP_DOIP_NACK_INC_PATTERN       0x00u  /* 头格式错（版本/取反/长度不一致） */
#define APP_DOIP_NACK_UNKNOWN_PT        0x01u  /* 载荷类型未知 */
#define APP_DOIP_NACK_MSG_TOO_LARGE     0x02u  /* 报文大于实体能处理的上限 */
#define APP_DOIP_NACK_OUT_OF_MEM        0x03u  /* 实体内存不足 */
#define APP_DOIP_NACK_INV_PAYLOAD_LEN   0x04u  /* 载荷长度字段非法 */

/* ---- 路由激活响应码（0x0006 载荷第 5 字节） ---- */
#define APP_DOIP_RA_UNKNOWN_SA          0x00u  /* 拒绝：未知源地址 */
#define APP_DOIP_RA_ALL_SOCKETS         0x01u  /* 拒绝：所有 socket 都在用 */
#define APP_DOIP_RA_SA_DIFF             0x02u  /* 拒绝：与已激活的 SA 不同 */
#define APP_DOIP_RA_SA_USED             0x03u  /* 拒绝：该 SA 已在别的 socket 激活 */
#define APP_DOIP_RA_MISSING_AUTH        0x04u  /* 拒绝：缺少鉴权（OEM 特定字段） */
#define APP_DOIP_RA_REJECT_CONFIRM      0x05u  /* 拒绝：确认被拒 */
#define APP_DOIP_RA_UNSUPPORTED_AT      0x06u  /* 拒绝：不支持的路由激活类型 */
#define APP_DOIP_RA_SECURE_REQUIRED     0x07u  /* 拒绝：必须用 TLS socket（2019） */
#define APP_DOIP_RA_OK                  0x10u  /* 成功 */
#define APP_DOIP_RA_OK_CONFIRM          0x11u  /* 需确认后才激活 */

/* ---- 路由激活类型（0x0005 载荷第 3 字节） ---- */
#define APP_DOIP_AT_DEFAULT             0x00u  /* 默认 */
#define APP_DOIP_AT_WWH_OBD             0x01u  /* WWH-OBD */
#define APP_DOIP_AT_CENTRAL_SECURITY    0xE0u  /* 中央安全（2012 保留给 OEM 用） */

/* ---- 诊断报文应答码 ---- */
#define APP_DOIP_DIAG_ACK_OK            0x00u
#define APP_DOIP_DIAG_NACK_INV_SA       0x02u  /* 无效源地址 */
#define APP_DOIP_DIAG_NACK_UNKNOWN_TA   0x03u  /* 未知目标地址 */
#define APP_DOIP_DIAG_NACK_TOO_LARGE    0x04u  /* 诊断报文过大 */
#define APP_DOIP_DIAG_NACK_OUT_OF_MEM   0x05u  /* 内存不足 */
#define APP_DOIP_DIAG_NACK_TARGET_UNREACH 0x06u/* 目标不可达 */
#define APP_DOIP_DIAG_NACK_UNKNOWN_NET   0x07u /* 未知网络 */
#define APP_DOIP_DIAG_NACK_TP_ERROR     0x08u  /* 传输层错误 */

/* ---- 实体状态响应：节点类型 ---- */
#define APP_DOIP_NODE_DOIP_NODE         0x00u
#define APP_DOIP_NODE_DOIP_GATEWAY      0x01u

/* ---- 诊断电源模式 ---- */
#define APP_DOIP_PWR_NOT_READY          0x00u
#define APP_DOIP_PWR_READY              0x01u

/* ---- 通用头 ---- */
typedef struct {
    uint8_t  version;
    uint8_t  version_inv;
    uint16_t payload_type;
    uint32_t payload_len;
} app_doip_hdr_t;

/* ---- 路由激活请求（解析结果） ---- */
typedef struct {
    uint16_t sa;
    uint8_t  activation_type;
    uint8_t  oem[4];      /* 只有载荷 11 B 时才有效 */
    bool     has_oem;
} app_doip_ra_req_t;

/** 写通用头（不含载荷），返回 8；cap 不足返回 0 */
uint32_t app_doip_hdr_encode(uint8_t *buf, uint32_t cap, uint16_t pt, uint32_t payload_len,
                             uint8_t version);

/** 解析通用头并校验版本取反/长度是否可接受；返回 true = 头合法（NACK 码在 *nack 里） */
bool app_doip_hdr_decode(const uint8_t *buf, app_doip_hdr_t *h, uint8_t *nack);

/** 组 0x0004 车辆声明/识别响应；2019 版本多 1 B 同步状态。返回载荷长度（0=失败） */
uint32_t app_doip_build_vehicle_announce(uint8_t *buf, uint32_t cap,
                                         const char *vin, uint16_t entity_addr,
                                         const uint8_t eid[6], const uint8_t gid[6],
                                         uint8_t further_action, uint8_t version,
                                         uint8_t sync_status);

/** 组 0x0006 路由激活响应（13 B） */
uint32_t app_doip_build_ra_resp(uint8_t *buf, uint32_t cap, uint16_t tester_sa,
                                uint16_t entity_addr, uint8_t code);

/** 解析 0x0005 路由激活请求（7 B 无 OEM / 11 B 带 OEM） */
bool app_doip_parse_ra_req(const uint8_t *buf, uint32_t len, app_doip_ra_req_t *out);

/** 组 0x8001 诊断报文（SA+TA+UDS）到 buf，返回载荷长度 */
uint32_t app_doip_build_diag(uint8_t *buf, uint32_t cap, uint16_t sa, uint16_t ta,
                             const uint8_t *uds, uint32_t uds_len);

/** 组 0x8002 / 0x8003 诊断应答（5 B：SA+TA+code） */
uint32_t app_doip_build_diag_ack(uint8_t *buf, uint32_t cap, uint16_t sa, uint16_t ta,
                                 uint8_t code);

/** 组 0x4002 实体状态响应（7 B） */
uint32_t app_doip_build_entity_status(uint8_t *buf, uint32_t cap, uint8_t node_type,
                                      uint8_t max_sockets, uint8_t open_sockets,
                                      uint32_t max_data_size);

/** 组 0x4004 诊断电源模式响应（1 B） */
uint32_t app_doip_build_power_mode(uint8_t *buf, uint32_t cap, uint8_t mode);

/** 组 0x0000 通用头 NACK（1 B） */
uint32_t app_doip_build_generic_nack(uint8_t *buf, uint32_t cap, uint8_t code);

/* 小工具 */
uint16_t app_doip_get_u16(const uint8_t *p);
uint32_t app_doip_get_u32(const uint8_t *p);
void     app_doip_put_u16(uint8_t *p, uint16_t v);
void     app_doip_put_u32(uint8_t *p, uint32_t v);

/** 载荷类型名（打印/日志用） */
const char *app_doip_pt_name(uint16_t pt);

/** 协议自检（固定向量，不跑网络）；返回 0 = 全过，失败条数在 *fail 里 */
int app_doip_wire_selftest(uint32_t *checks, uint32_t *fail);

#ifdef __cplusplus
}
#endif

#endif /* APP_DOIP_WIRE_H */
