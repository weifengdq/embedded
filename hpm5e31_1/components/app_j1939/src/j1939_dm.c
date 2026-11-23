/*
 * j1939_dm.c - J1939-73 诊断报文：DM1（激活 DTC，1 s 周期）、DM2（历史 DTC，按请求）、
 *              DM3（清除历史）、DM11（清除激活）
 *
 * 报文格式（DM1/DM2，单帧 8 字节）：
 *   byte1 : 灯状态（每灯 2 bit：MIL[1:0] RSL[3:2] AWL[5:4] PL[7:6]；0=off 1=on 2=error 3=N/A）
 *   byte2 : 闪灯状态（本实现恒 0，即常亮）
 *   byte3-6 : DTC1  SPN[2:0]+FMI[4:0] | OC[6:0]+SPN[19] | SPN[10:3] | SPN[18:11]
 *
 * SPN 是 19 位（0..524287），FMI 5 位，OC 7 位（0..126）。DM1 每帧只装 1 个 DTC
 * （标准允许一个 DM1 只带一个；多故障时按严重度逐个/或改用 DM2 多包）。
 * DM2 用 TP 传输全部历史 DTC（2 + 4×N 字节），便于验证 RTS/CTS。
 */
#include "app_j1939.h"
#include "j1939_internal.h"

#define J1939_DM_LAMP2(mil, rsl, awl, pl)     ((uint8_t) (((mil) & 0x03U) | (((rsl) & 0x03U) << 2) | (((awl) & 0x03U) << 4) |                 (((pl) & 0x03U) << 6)))

uint8_t j1939_dm_lamp_byte(uint8_t mil, uint8_t rsl, uint8_t awl, uint8_t pl)
{
    return J1939_DM_LAMP2(mil, rsl, awl, pl);
}

static void dm1_fill(j1939_stack_t *s, uint8_t *buf, uint16_t *len, void *user)
{
    (void) s;
    (void) user;
    *len = j1939_dm1_build(s, buf);
}

void j1939_dm_init(j1939_stack_t *s)
{
    (void) j1939_sched_add(s, J1939_PGN_DM1, 1000U, 6U, dm1_fill, s);
}

/* 演示用灯映射（不是任何 OEM 的诊断策略，仅用于例程可复现）：
 *   FMI 0..3  -> MIL（0/1 同时点亮 RSL 红停）
 *   FMI 4..11 -> AWL（黄警告）
 *   FMI 12..15-> PL（保护灯）
 *   其它      -> AWL
 */
static uint8_t fmi_to_lamp(uint8_t fmi)
{
    if (fmi <= 3U) {
        return 1U; /* MIL */
    }
    if (fmi <= 11U) {
        return 3U; /* AWL */
    }
    if (fmi <= 15U) {
        return 4U; /* PL */
    }
    return 3U;
}

int j1939_dm_add_fault(j1939_stack_t *s, uint32_t spn, uint8_t fmi, uint8_t oc)
{
    j1939_dtc_t *d;
    for (uint8_t i = 0U; i < s->dtc_count; i++) {
        if ((s->dtc[i].spn == (spn & 0x7FFFFUL)) && (s->dtc[i].fmi == (fmi & 0x1FU))) {
            if (s->dtc[i].oc < 126U) {
                s->dtc[i].oc++;
            }
            return 0;
        }
    }
    if (s->dtc_count >= J1939_DTC_MAX) {
        return -1;
    }
    d = &s->dtc[s->dtc_count++];
    d->spn = spn & 0x7FFFFUL;
    d->fmi = fmi & 0x1FU;
    d->oc = (oc == 0U) ? 1U : ((oc > 126U) ? 126U : oc);
    d->lamp = fmi_to_lamp(d->fmi);

    /* 历史表：同一 DTC 只保留一份（OC 叠加） */
    for (uint8_t i = 0U; i < s->dtc_hist_count; i++) {
        if ((s->dtc_hist[i].spn == d->spn) && (s->dtc_hist[i].fmi == d->fmi)) {
            s->dtc_hist[i].oc = d->oc;
            s->dtc_hist[i].lamp = d->lamp;
            return 0;
        }
    }
    if (s->dtc_hist_count < J1939_DTC_MAX) {
        s->dtc_hist[s->dtc_hist_count++] = *d;
    }
    return 0;
}

void j1939_dm_clear_active(j1939_stack_t *s)
{
    s->dtc_count = 0U;
}

void j1939_dm_clear_history(j1939_stack_t *s)
{
    s->dtc_hist_count = 0U;
}

uint8_t j1939_dm_lamp(const j1939_stack_t *s)
{
    uint8_t worst = 0U;
    for (uint8_t i = 0U; i < s->dtc_count; i++) {
        uint8_t l = s->dtc[i].lamp;
        /* RSL(2) 最严重 > MIL(1) > AWL(3) > PL(4) */
        uint8_t rank = (l == 2U) ? 4U : (l == 1U) ? 3U : (l == 3U) ? 2U : (l == 4U) ? 1U : 0U;
        uint8_t wrank = (worst == 2U) ? 4U : (worst == 1U) ? 3U : (worst == 3U) ? 2U : (worst == 4U) ? 1U : 0U;
        if (rank > wrank) {
            worst = l;
        }
    }
    return worst;
}

static void dtc_encode(const j1939_dtc_t *d, uint8_t *b)
{
    b[0] = (uint8_t) (((d->fmi & 0x1FU) << 3) | (d->spn & 0x07U));
    b[1] = (uint8_t) ((d->oc & 0x7FU) | (((d->spn >> 19) & 0x01U) << 7));
    b[2] = (uint8_t) ((d->spn >> 3) & 0xFFU);
    b[3] = (uint8_t) ((d->spn >> 11) & 0xFFU);
}

static void lamp_bytes_calc(const j1939_dtc_t *dtc, uint8_t count, uint8_t *mil, uint8_t *rsl,
                            uint8_t *awl, uint8_t *pl)
{
    *mil = 0U;
    *rsl = 0U;
    *awl = 0U;
    *pl = 0U;
    for (uint8_t i = 0U; i < count; i++) {
        switch (dtc[i].lamp) {
        case 1U:
            *mil = 1U;
            break;
        case 2U:
            *rsl = 1U;
            break;
        case 3U:
            *awl = 1U;
            break;
        case 4U:
            *pl = 1U;
            break;
        default:
            break;
        }
    }
}

uint16_t j1939_dm1_build(j1939_stack_t *s, uint8_t *buf)
{
    uint8_t mil, rsl, awl, pl;
    lamp_bytes_calc(s->dtc, s->dtc_count, &mil, &rsl, &awl, &pl);
    buf[0] = j1939_dm_lamp_byte(mil, rsl, awl, pl);
    buf[1] = 0x00U; /* 无闪码 */
    if (s->dtc_count == 0U) {
        return 2U; /* 无故障：只发灯状态 + 闪灯状态 */
    }
    dtc_encode(&s->dtc[0], &buf[2]); /* 单帧只装 1 个 DTC */
    return 6U;
}

uint16_t j1939_dm2_build(j1939_stack_t *s, uint8_t *buf)
{
    uint8_t mil, rsl, awl, pl;
    uint16_t n = 2U;
    /* DM2 的灯状态反映"曾经激活"的灯，用历史表推导 */
    lamp_bytes_calc(s->dtc_hist, s->dtc_hist_count, &mil, &rsl, &awl, &pl);
    buf[0] = j1939_dm_lamp_byte(mil, rsl, awl, pl);
    buf[1] = 0x00U;
    for (uint8_t i = 0U; i < s->dtc_hist_count; i++) {
        if ((uint16_t) (n + 4U) > J1939_TP_MAX_LEN) {
            break;
        }
        dtc_encode(&s->dtc_hist[i], &buf[n]);
        n = (uint16_t) (n + 4U);
    }
    return n;
}
uint16_t j1939_dm_build(j1939_stack_t *s, uint32_t pgn, uint8_t *buf)
{
    if (pgn == J1939_PGN_DM1) {
        return j1939_dm1_build(s, buf);
    }
    if (pgn == J1939_PGN_DM2) {
        return j1939_dm2_build(s, buf);
    }
    return 0U;
}
