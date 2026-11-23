/*
 * dlt_log - COVESA DLT 日志报文发送端（UDP 3490 -> 主机）
 *
 * 报文格式（与 dlt_protocol.h 对齐，简化版 verbose）：
 *   Standard Header 4 B : HTYP(1) MCNT(1) LEN(2, 大端, 含头总长)
 *     HTYP = 0x11 = UEH(0x01) | WTMS(0x10)
 *   Standard Header extra 4 B : TMSP（WTMS=1 时，0.1 ms，小端；位于扩展头之前）
 *   Extended Header 10 B : MSIN(1)=0x01(verbose) NOAR(1) APID(4) CTID(4)
 *   Payload            : NOAR 个参数，每个：
 *       TypeInfo(4 B, LE): bit0-3 TYLE(1=8bit,2=16,3=32,4=64)
 *                          bit6 UINT(0x40) / bit5 SINT(0x20) / bit9 STRG(0x200) / bit10 RAWD(0x400)
 *       UINT/SINT: 紧跟 TYLE 字节数据
 *       STRG     : u16 长度(LE) + 数据（ASCII）
 *
 * 4 个 context：MAIN / CAN / ADC / NET；5 个级别（本 profile 用第一个 uint32 参数表示）：
 *   0=fatal 1=error 2=warn 3=info 4=debug 5=verbose
 * 默认 100 条/s（10 ms），命令 dlt on|off、dlt rate <ms>、dlt stat。
 */
#include <stdio.h>
#include <string.h>

#include "bsp.h"
#include "app_lwip.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"

#define DLT_PEER_IP   "192.168.2.1"
#define DLT_PEER_PORT 3490U
#define DLT_LOCAL_PORT 3491U

#define DLT_HTYP_UEH  0x01U
#define DLT_HTYP_WTMS 0x10U
#define DLT_MSIN_VERB 0x01U

#define DLT_TYLE_8BIT  0x01U
#define DLT_TYLE_32BIT 0x03U
#define DLT_TI_UINT    0x40U
#define DLT_TI_STRG    0x200U

static struct udp_pcb *s_pcb;
static ip_addr_t s_peer;
static uint8_t s_mcnt;
static uint32_t s_sent;
static uint32_t s_rate_ms = 10U;
static uint32_t s_on = 1U;
static uint32_t s_seq;

static const char *const s_ctx[4] = {"MAIN", "CAN ", "ADC ", "NET "};

static uint16_t put16_be(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; return 2U; }
static void put32_le(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* 组一包并发送；返回 0 成功 */
static int dlt_send(uint8_t ctx_idx, uint8_t level, const char *msg)
{
    uint8_t buf[128];
    uint16_t n = 0U;
    uint16_t msg_len = (uint16_t) strlen(msg);
    uint32_t tmsp = (bsp_uptime_ms() * 10U); /* 0.1 ms */
    uint16_t total;

    if (msg_len > 40U) {
        msg_len = 40U;
    }
    /* Standard Header（4 B）：HTYP | MCNT | LEN(大端) */
    buf[n++] = (uint8_t) (DLT_HTYP_UEH | DLT_HTYP_WTMS);
    buf[n++] = s_mcnt++;
    n = (uint16_t) (n + put16_be(&buf[n], 0U)); /* LEN 先占位 */
    /* Standard Header extra：WTMS -> TMSP(4 B, 0.1 ms, LE)。注意位于扩展头之前！ */
    put32_le(&buf[n], tmsp);
    n = (uint16_t) (n + 4U);
    /* Extended Header（10 B）：MSIN | NOAR | APID | CTID */
    buf[n++] = DLT_MSIN_VERB;
    buf[n++] = 2U; /* NOAR: level(u32) + msg(str) */
    memcpy(&buf[n], s_ctx[ctx_idx & 3U], 4U);
    n = (uint16_t) (n + 4U);
    memcpy(&buf[n], s_ctx[ctx_idx & 3U], 4U); /* CTID 用同一 4 字符，简化 */
    n = (uint16_t) (n + 4U);
    /* 参数 1: uint32 level */
    put32_le(&buf[n], (uint32_t) (DLT_TI_UINT | DLT_TYLE_32BIT));
    n = (uint16_t) (n + 4U);
    put32_le(&buf[n], (uint32_t) level);
    n = (uint16_t) (n + 4U);
    /* 参数 2: string */
    put32_le(&buf[n], (uint32_t) (DLT_TI_STRG));
    n = (uint16_t) (n + 4U);
    buf[n++] = (uint8_t) (msg_len & 0xFFU);
    buf[n++] = (uint8_t) (msg_len >> 8);
    memcpy(&buf[n], msg, msg_len);
    n = (uint16_t) (n + msg_len);

    total = n;
    (void) put16_be(&buf[2], total);

    {
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, total, PBUF_RAM);
        if (p == NULL) {
            return -1;
        }
        memcpy(p->payload, buf, total);
        if (udp_sendto(s_pcb, p, &s_peer, DLT_PEER_PORT) != ERR_OK) {
            pbuf_free(p);
            return -2;
        }
        pbuf_free(p);
    }
    s_sent++;
    s_seq++;
    return 0;
}

static void dlt_tick(void)
{
    static uint32_t last_ms;
    uint32_t now = bsp_uptime_ms();
    uint8_t ctx;
    uint8_t level;
    char msg[48];

    if ((s_on == 0U) || ((now - last_ms) < s_rate_ms)) {
        return;
    }
    last_ms = now;
    ctx = (uint8_t) (s_seq % 4U);
    level = (uint8_t) (s_seq % 6U); /* 0..5 => fatal..verbose */
    snprintf(msg, sizeof(msg), "seq=%u ctx=%s level=%u", (unsigned) s_seq, s_ctx[ctx], (unsigned) level);
    (void) dlt_send(ctx, level, msg);
}

/* ---------------- shell ---------------- */
static void cmd_dlt(int argc, char *argv[])
{
    const char *a = (argc >= 2) ? argv[1] : NULL;
    if ((a != NULL) && (strcmp(a, "on") == 0)) {
        s_on = 1U;
        printf("dlt: ON rate=%u ms -> %s:%u\r\n", (unsigned) s_rate_ms, DLT_PEER_IP, (unsigned) DLT_PEER_PORT);
    } else if ((a != NULL) && (strcmp(a, "off") == 0)) {
        s_on = 0U;
        printf("dlt: OFF sent=%u\r\n", (unsigned) s_sent);
    } else if ((a != NULL) && (strcmp(a, "rate") == 0)) {
        uint32_t ms = 10U;
        if (argc >= 3) {
            (void) bsp_parse_u32(argv[2], &ms);
        }
        if (ms >= 1U && ms <= 1000U) {
            s_rate_ms = ms;
        }
        printf("dlt: rate=%u ms\r\n", (unsigned) s_rate_ms);
    } else if ((a != NULL) && (strcmp(a, "stat") == 0)) {
        printf("dlt: %s sent=%u rate=%u ms mcnt=%u uptime=%u ms\r\n", s_on ? "ON" : "OFF", (unsigned) s_sent,
               (unsigned) s_rate_ms, (unsigned) s_mcnt, (unsigned) bsp_uptime_ms());
    } else {
        printf("usage: dlt on|off|rate <ms>|stat\r\n");
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 dlt, cmd_dlt, DLT sender: dlt on|off|rate <ms>|stat);

int main(void)
{
    int rc_ip;
    uint32_t t_end;

    board_init();
    bsp_init();
    rc_ip = app_lwip_init(NULL);
    printf("[app] dlt_log ready: lwip rc=%d link=%s\r\n", rc_ip, app_lwip_link_up() ? "UP" : "DOWN");
    s_pcb = app_udp_socket(NULL, DLT_LOCAL_PORT);
    if ((s_pcb == NULL) || !app_lwip_parse_ip(DLT_PEER_IP, &s_peer)) {
        printf("[app] dlt: UDP socket / peer parse FAILED\r\n");
    }
    printf("[app] dlt -> %s:%u, 4 ctx x 5 level, %u ms\r\n", DLT_PEER_IP, (unsigned) DLT_PEER_PORT,
           (unsigned) s_rate_ms);

    t_end = bsp_uptime_ms() + 1200U;
    while ((int32_t) (bsp_uptime_ms() - t_end) < 0) {
        bsp_shell_poll();
        app_lwip_poll();
        dlt_tick();
    }
    printf("[auto] 1.2 s 发送完成: ");
    cmd_dlt(2, (char *[]){"dlt", "stat"});

    while (1) {
        bsp_shell_poll();
        app_lwip_poll();
        dlt_tick();
    }
}
