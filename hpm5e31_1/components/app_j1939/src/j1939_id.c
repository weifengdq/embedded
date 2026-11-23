/*
 * j1939_id.c - 29 位 ID / PGN / NAME 编解码（SAE J1939-21 §4、J1939-81 §4）
 *
 * 29 位 ID 布局（高位 -> 低位）：
 *   [28:26] Priority(3)  [25] EDP  [24] DP  [23:16] PF  [15:8] PS  [7:0] SA
 *
 * PGN = EDP:DP:PF:PS，但 PS 的含义取决于 PF：
 *   PF < 240  (PDU1)：PS 是目标地址 DA，PGN 的 PS 段补 0；
 *   PF >= 240 (PDU2)：PS 是组扩展 GE，参与 PGN。
 * 所以 PGN 0x00EE00（地址声明）是 PDU1，而 PGN 0x00FECA（DM1）是 PDU2。
 */
#include "app_j1939.h"

uint32_t j1939_pgn_from_id(uint32_t id)
{
    uint32_t pf = J1939_ID_PF(id);
    uint32_t ps = J1939_ID_PS(id);
    uint32_t pgn;

    if (pf < 240U) {
        /* PDU1：目的地址不参与 PGN */
        pgn = (J1939_ID_EDP(id) << 17) | (J1939_ID_DP(id) << 16) | (pf << 8);
    } else {
        /* PDU2：组扩展参与 PGN */
        pgn = (J1939_ID_EDP(id) << 17) | (J1939_ID_DP(id) << 16) | (pf << 8) | ps;
    }
    return pgn & 0x3FFFFUL;
}

uint32_t j1939_id_from_pgn(uint32_t pgn, uint8_t priority, uint8_t da, uint8_t sa)
{
    uint32_t pf = (pgn >> 8) & 0xFFU;
    uint32_t ps;

    if (pf < 240U) {
        ps = da & 0xFFU; /* PDU1：PS = DA */
    } else {
        ps = pgn & 0xFFU; /* PDU2：PS = GE */
    }
    return ((uint32_t) (priority & 0x07U) << 26) |
           ((uint32_t) ((pgn >> 17) & 0x01U) << 25) | /* EDP */
           ((uint32_t) ((pgn >> 16) & 0x01U) << 24) | /* DP  */
           (pf << 16) | (ps << 8) | (sa & 0xFFU);
}

bool j1939_is_pdu1(uint32_t pgn)
{
    return (((pgn >> 8) & 0xFFU) < 240U);
}

/*
 * 默认优先级（J1939-71 附录 A / J1939-21 表 3）。本书只覆盖用到的 PGN，
 * 其余按 6（最低）发送。应用可显式覆盖。
 */
uint8_t j1939_default_priority(uint32_t pgn)
{
    switch (pgn) {
    case J1939_PGN_EEC1: /* 发动机转速，10 ms，最高优先级之一 */
        return 3U;
    case 0xF003U: /* ETC1 */
    case 0xFEF2U: /* CCVS2 */
        return 3U;
    case J1939_PGN_ET1:
    case J1939_PGN_CCVS1:
    case J1939_PGN_LFE:
    case J1939_PGN_DM1:
    case J1939_PGN_DM2:
        return 6U;
    case J1939_PGN_ADDRESS_CLAIMED:
        return 6U;
    case J1939_PGN_REQUEST:
    case J1939_PGN_ACK:
        return 6U;
    case J1939_PGN_TP_CM:
    case J1939_PGN_TP_DT:
        return 7U;
    default:
        return 6U;
    }
}

/* ---------------- NAME：64 位，8 字节小端（J1939-81 §4.1） ---------------- */
/*
 * 位域（bit 0 = LSB，即报文第 1 字节最低位）：
 *   [0]     Arbitrary Address Capable
 *   [3:1]   Industry Group
 *   [7:4]   Vehicle System Instance
 *   [14:8]  Vehicle System
 *   [15]    Reserved
 *   [23:16] Function
 *   [28:24] Function Instance
 *   [31:29] ECU Instance
 *   [42:32] Manufacturer Code（11 bit）
 *   [63:43] Identity Number（21 bit）
 */
uint64_t j1939_name_encode(const j1939_name_t *n)
{
    uint64_t v = 0ULL;

    v |= ((uint64_t) (n->identity & 0x1FFFFFUL)) << 43;
    v |= ((uint64_t) (n->manufacturer & 0x7FFU)) << 32;
    v |= ((uint64_t) (n->ecu_instance & 0x07U)) << 29;
    v |= ((uint64_t) (n->function_instance & 0x1FU)) << 24;
    v |= ((uint64_t) n->function) << 16;
    v |= (n->reserved ? 1ULL : 0ULL) << 15;
    v |= ((uint64_t) (n->vehicle_system & 0x7FU)) << 8;
    v |= ((uint64_t) (n->vehicle_system_instance & 0x0FU)) << 4;
    v |= ((uint64_t) (n->industry_group & 0x07U)) << 1;
    v |= (n->arbitrary_address_capable ? 1ULL : 0ULL);
    return v;
}

void j1939_name_decode(uint64_t v, j1939_name_t *n)
{
    n->identity = (uint32_t) ((v >> 43) & 0x1FFFFFULL);
    n->manufacturer = (uint16_t) ((v >> 32) & 0x7FFULL);
    n->ecu_instance = (uint8_t) ((v >> 29) & 0x07U);
    n->function_instance = (uint8_t) ((v >> 24) & 0x1FU);
    n->function = (uint8_t) ((v >> 16) & 0xFFU);
    n->reserved = (((v >> 15) & 0x01ULL) != 0ULL);
    n->vehicle_system = (uint8_t) ((v >> 8) & 0x7FU);
    n->vehicle_system_instance = (uint8_t) ((v >> 4) & 0x0FU);
    n->industry_group = (uint8_t) ((v >> 1) & 0x07U);
    n->arbitrary_address_capable = ((v & 0x01ULL) != 0ULL);
}

void j1939_name_to_bytes(uint64_t v, uint8_t out[8])
{
    for (uint8_t i = 0U; i < 8U; i++) {
        out[i] = (uint8_t) ((v >> (8U * i)) & 0xFFULL);
    }
}
