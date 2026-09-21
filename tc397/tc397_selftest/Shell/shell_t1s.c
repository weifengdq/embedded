/* shell_t1s.c - letter-shell front-end for the LAN8651 10BASE-T1S transceiver.
 *
 * Split out of the standalone tc397_lan8651_t1s project so that the combined
 * tc397_selftest firmware can expose the same diagnostics next to the GETH
 * (1000BASE-T1) command set. Only the LAN8651-specific commands live here;
 * the shared letter-shell plumbing is in shell_port.c.
 *
 * Two commands were renamed/dropped relative to the standalone project:
 *   - `link`  -> `t1link`  (the GETH YT8011 PHY already owns `link`)
 *   - `ifconfig` / `ping` come from shell_port.c and operate on both netifs.
 */
#include "shell_port.h"
#include "shell.h"
#include "IfxAsclin_Asc.h"
#include "IfxCpu_Irq.h"
#include "IfxStm.h"
#include "IfxPort.h"
#include "IfxScuRcu.h"
#include "IfxScuCcu.h"
#include "IfxScuWdt.h"
#include "IfxScu_reg.h"
#include "IfxPms_reg.h"
#include "Configuration.h"
#include "lan8651.h"
#define _SSIZE_T_DECLARED
#include "Ifx_Lwip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip.h"
#include "lwip/udp.h"
#include "UART_Logging.h"

extern volatile uint32 g_TickCount_1ms;

/* Same helper as shell_port.c (kept local/static to avoid a shared header). */
static int parse_u32(const char *arg, uint32_t *value)
{
    char *end = NULL;
    unsigned long v;
    if ((arg == NULL) || (value == NULL) || (*arg == '\0')) {
        return -1;
    }
    v = strtoul(arg, &end, 0);
    if ((end == NULL) || (*end != '\0')) {
        return -1;
    }
    *value = (uint32_t)v;
    return 0;
}
/* ---------- LAN8651 diagnostic commands (cf. lan8671 shell) ---------- */
static int cmd_t1stat(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32_t v = 0;
    uint32_t devid = 0, oas = 0, oac = 0, buf = 0;
    uint32_t c0 = 0, c1 = 0, sts = 0, tot = 0, bst = 0;
    uint32_t bmcr = 0, bmsr = 0, id1 = 0, id2 = 0;
    uint32_t ncr = 0, ncfgr = 0, nsr = 0;
    lan8651_read_reg(&g_lan8651, LAN8651_MISC_DEVID, &devid);
    lan8651_read_reg(&g_lan8651, LAN8651_OA_STATUS0, &oas);
    lan8651_read_reg(&g_lan8651, LAN8651_OA_CONFIG0, &oac);
    lan8651_read_reg(&g_lan8651, LAN8651_OA_BUFSTS, &buf);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL0, &c0);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL1, &c1);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_STS, &sts);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_TOTMR, &tot);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_BURST, &bst);
    lan8651_read_reg(&g_lan8651, LAN8651_PHY_BMCR, &bmcr);
    lan8651_read_reg(&g_lan8651, LAN8651_PHY_BMSR, &bmsr);
    lan8651_read_reg(&g_lan8651, LAN8651_PHY_ID1, &id1);
    lan8651_read_reg(&g_lan8651, LAN8651_PHY_ID2, &id2);
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_NCR, &ncr);
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_NCFGR, &ncfgr);
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_NSR, &nsr);
    (void)v;
    shellPrint(shell, "DEVID=0x%08lX SYNC=%u RESETC=%u oa_cfg=0x%04lX\r\n",
        (unsigned long)devid,
        (unsigned)((oas & LAN8651_OA_STATUS0_SYNC) != 0U),
        (unsigned)((oas & LAN8651_OA_STATUS0_RESETC) != 0U),
        (unsigned long)(oac & 0xFFFFU));
    shellPrint(shell, "PLCA en=%u id=%lu ncnt=%lu pst=%u tot=0x%04lX burst=0x%04lX\r\n",
        (unsigned)((c0 & LAN8651_PLCA_CTRL0_EN) != 0U),
        (unsigned long)(c1 & 0xFFU), (unsigned long)((c1 >> 8) & 0xFFU),
        (unsigned)((sts & LAN8651_PLCA_STS_PST) != 0U),
        (unsigned long)(tot & 0xFFFFU), (unsigned long)(bst & 0xFFFFU));
    shellPrint(shell, "PHY bmcr=0x%04lX bmsr=0x%04lX(link=%u) id=0x%04lX/0x%04lX\r\n",
        (unsigned long)(bmcr & 0xFFFFU), (unsigned long)(bmsr & 0xFFFFU),
        (unsigned)((bmsr & LAN8651_PHY_BMSR_LINK_STATUS) != 0U),
        (unsigned long)(id1 & 0xFFFFU), (unsigned long)(id2 & 0xFFFFU));
    shellPrint(shell, "MAC ncr=0x%02lX(TXEN=%u RXEN=%u) ncfgr=0x%08lX nsr=0x%02lX\r\n",
        (unsigned long)(ncr & 0xFFU),
        (unsigned)((ncr & LAN8651_MAC_NCR_TXEN) != 0U),
        (unsigned)((ncr & LAN8651_MAC_NCR_RXEN) != 0U),
        (unsigned long)ncfgr, (unsigned long)(nsr & 0xFFU));
    shellPrint(shell, "BUF rba=%lu txc=%lu irq=%s(%u)\r\n",
        (unsigned long)(buf & LAN8651_OA_BUFSTS_RBA_MASK),
        (unsigned long)((buf & LAN8651_OA_BUFSTS_TXC_MASK) >> LAN8651_OA_BUFSTS_TXC_SHIFT),
        lan8651_irq_asserted(&g_lan8651) ? "ASSERTED" : "idle",
        (unsigned)IfxPort_getPinState(LAN8651_INT_PORT, LAN8651_INT_PIN));
    {
        uint32_t to = 0, busy = 0, rec = 0, hdrb = 0, fail = 0;
        lan8651_spi_stats(&to, &busy, &rec);
        lan8651_tx_stats(&hdrb, &fail);
        shellPrint(shell, "SPI timeouts=%lu busy=%lu recovered=%lu | TX hdrb=%lu fail=%lu\r\n",
            (unsigned long)to, (unsigned long)busy, (unsigned long)rec,
            (unsigned long)hdrb, (unsigned long)fail);
    }
    return 0;
}

static int cmd_t1r(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32_t addr, val = 0;
    if (argc < 2 || parse_u32(argv[1], &addr)) {
        shellPrint(shell, "usage: t1r <reg_hex>  (ex: t1r 0x0004CA01)\r\n");
        return -1;
    }
    if (lan8651_read_reg(&g_lan8651, addr, &val) != kLan8651Status_Ok) {
        shellPrint(shell, "t1r 0x%08lX failed\r\n", (unsigned long)addr);
        return -1;
    }
    shellPrint(shell, "0x%08lX = 0x%08lX\r\n", (unsigned long)addr, (unsigned long)val);
    return 0;
}

static int cmd_t1w(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32_t addr, val, back = 0;
    if (argc < 3 || parse_u32(argv[1], &addr) || parse_u32(argv[2], &val)) {
        shellPrint(shell, "usage: t1w <reg_hex> <val_hex>\r\n");
        return -1;
    }
    if (lan8651_write_reg(&g_lan8651, addr, val) != kLan8651Status_Ok) {
        shellPrint(shell, "t1w write failed\r\n");
        return -1;
    }
    lan8651_read_reg(&g_lan8651, addr, &back);
    shellPrint(shell, "0x%08lX <= 0x%08lX (readback 0x%08lX)\r\n",
        (unsigned long)addr, (unsigned long)val, (unsigned long)back);
    return 0;
}

static int cmd_plca(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32_t c0 = 0, c1 = 0, sts = 0, tot = 0, bst = 0;
    if (argc == 1) {
        lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL0, &c0);
        lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL1, &c1);
        lan8651_read_reg(&g_lan8651, LAN8651_PLCA_STS, &sts);
        lan8651_read_reg(&g_lan8651, LAN8651_PLCA_TOTMR, &tot);
        lan8651_read_reg(&g_lan8651, LAN8651_PLCA_BURST, &bst);
        shellPrint(shell, "PLCA en=%u id=%lu ncnt=%lu pst=%u tot=0x%04lX burst=0x%04lX\r\n",
            (unsigned)((c0 & LAN8651_PLCA_CTRL0_EN) != 0U),
            (unsigned long)(c1 & 0xFFU), (unsigned long)((c1 >> 8) & 0xFFU),
            (unsigned)((sts & LAN8651_PLCA_STS_PST) != 0U),
            (unsigned long)(tot & 0xFFFFU), (unsigned long)(bst & 0xFFFFU));
        shellPrint(shell, "usage: plca <node_id 0..255> [node_count]  (0=coordinator/master)\r\n");
        return 0;
    }
    {
        uint32_t id, ncnt = 8;
        uint32_t cur = 0;
        lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL1, &cur);
        if (parse_u32(argv[1], &id) || id > 255) {
            shellPrint(shell, "bad node_id (0..255)\r\n");
            return -1;
        }
        if (argc >= 3) {
            if (parse_u32(argv[2], &ncnt) || ncnt == 0 || ncnt > 255) {
                shellPrint(shell, "bad node_count (1..255)\r\n");
                return -1;
            }
        } else {
            ncnt = (cur >> 8) & 0xFFU;
            if (ncnt == 0) ncnt = 8;
        }
        {
            uint32_t v = ((ncnt & 0xFFU) << 8) | (id & 0xFFU);
            if (lan8651_write_reg(&g_lan8651, LAN8651_PLCA_CTRL1, v) != kLan8651Status_Ok) {
                shellPrint(shell, "plca write failed\r\n");
                return -1;
            }
            /* ensure PLCA enabled */
            lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL0, &c0);
            if ((c0 & LAN8651_PLCA_CTRL0_EN) == 0U) {
                lan8651_write_reg(&g_lan8651, LAN8651_PLCA_CTRL0, LAN8651_PLCA_CTRL0_EN);
            }
            lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL1, &c1);
            shellPrint(shell, "PLCA set id=%lu ncnt=%lu (readback id=%lu ncnt=%lu)%s\r\n",
                (unsigned long)id, (unsigned long)ncnt,
                (unsigned long)(c1 & 0xFFU), (unsigned long)((c1 >> 8) & 0xFFU),
                (id == 0) ? " [coordinator/master]" : " [follower/slave]");
        }
    }
    return 0;
}

static int cmd_t1link(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32_t oas = 0, sts = 0, bmsr = 0;
    lan8651_read_reg(&g_lan8651, LAN8651_OA_STATUS0, &oas);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_STS, &sts);
    lan8651_read_reg(&g_lan8651, LAN8651_PHY_BMSR, &bmsr);
    shellPrint(shell, "link: %s (sync=%u pst=%u phy_link=%u irq=%s)\r\n",
        lan8651_link_up(&g_lan8651) ? "UP" : "DOWN",
        (unsigned)((oas & LAN8651_OA_STATUS0_SYNC) != 0U),
        (unsigned)((sts & LAN8651_PLCA_STS_PST) != 0U),
        (unsigned)((bmsr & LAN8651_PHY_BMSR_LINK_STATUS) != 0U),
        lan8651_irq_asserted(&g_lan8651) ? "ASSERTED" : "idle");
    return 0;
}

/* ---------- LAN8651 diagnostics: SQI / PLCA / PCS / cable ---------- */

static const char *sqi_text(uint32_t v)
{
    switch (v & 0x7U) {
        case 0: return "SNR<=~5dB BER>=~3.8E-02 (worst)";
        case 1: return "~5-10dB BER~3.8E-02~7.8E-4";
        case 2: return "~10-12dB BER~7.8E-4~3.4E-5";
        case 3: return "~12-14dB BER~3.4E-5~2.7E-7";
        case 4: return "~14-16dB BER~2.7E-7~1.4E-10";
        case 5: return "~16-17dB BER~1.4E-10~7.2E-13";
        case 6: return "~17-18dB BER~7.2E-13~9.9E-16";
        default: return "SNR>=~18dB BER<=~9.9E-16 (best)";
    }
}

/* Run one SQI polling-mode measurement. Returns 0 + *sqival on success,
 * -1 on timeout, -2 on SQI error. Keeps lwIP polling so links survive. */
static int sqi_measure(uint32_t toid, uint32_t timeout_ms, uint32_t *sqival, uint32_t *errc)
{
    uint32_t v = 0;
    uint32_t start;
    int restarts = 0;

    /* TOID lives in SQICFG0 bits 11:4 */
    if (lan8651_read_reg(&g_lan8651, LAN8651_SQICFG0, &v) != kLan8651Status_Ok) return -3;
    v = (v & ~(uint32_t)LAN8651_SQICFG0_TOID_MASK) |
        (((toid << LAN8651_SQICFG0_TOID_SHIFT) & LAN8651_SQICFG0_TOID_MASK));
    if (lan8651_write_reg(&g_lan8651, LAN8651_SQICFG0, v) != kLan8651Status_Ok) return -3;

    /* polling mode: interrupt threshold disabled (0x1F) */
    if (lan8651_read_reg(&g_lan8651, LAN8651_SQICFG2, &v) != kLan8651Status_Ok) return -3;
    v = (v & ~(uint32_t)LAN8651_SQICFG2_INTTHR_MASK) | LAN8651_SQICFG2_INTTHR_OFF;
    if (lan8651_write_reg(&g_lan8651, LAN8651_SQICFG2, v) != kLan8651Status_Ok) return -3;

    /* reset SQI block, wait self-clear */
    if (lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v) != kLan8651Status_Ok) return -3;
    if (lan8651_write_reg(&g_lan8651, LAN8651_SQICTL, v | LAN8651_SQICTL_SQIRST) != kLan8651Status_Ok) return -3;
    start = g_TickCount_1ms;
    do {
        if (lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v) != kLan8651Status_Ok) return -3;
        if ((v & LAN8651_SQICTL_SQIRST) == 0U) break;
        Ifx_Lwip_pollTimerFlags();
        Ifx_Lwip_pollReceiveFlags();
    } while ((g_TickCount_1ms - start) < 500U);

    /* enable measurement */
    if (lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v) != kLan8651Status_Ok) return -3;
    if (lan8651_write_reg(&g_lan8651, LAN8651_SQICTL, v | LAN8651_SQICTL_SQIEN) != kLan8651Status_Ok) return -3;

    start = g_TickCount_1ms;
    while ((g_TickCount_1ms - start) < timeout_ms) {
        uint32_t s0;
        Ifx_Lwip_pollTimerFlags();
        Ifx_Lwip_pollReceiveFlags();
        if (lan8651_read_reg(&g_lan8651, LAN8651_SQISTS0, &s0) != kLan8651Status_Ok) return -3;
        if ((s0 & LAN8651_SQISTS0_SQIVLD) != 0U) {
            *sqival = (s0 & LAN8651_SQISTS0_SQIVAL_MASK) >> LAN8651_SQISTS0_SQIVAL_SHIFT;
            *errc = 0U;
            lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v);
            lan8651_write_reg(&g_lan8651, LAN8651_SQICTL, v & ~(uint32_t)LAN8651_SQICTL_SQIEN);
            return 0;
        }
        if ((s0 & LAN8651_SQISTS0_SQIERR) != 0U) {
            *errc = s0 & LAN8651_SQISTS0_SQIERRC_MASK;
            if (restarts == 0) {
                /* datasheet: clear SQIEN then re-enable and retry once */
                restarts++;
                lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v);
                lan8651_write_reg(&g_lan8651, LAN8651_SQICTL, v & ~(uint32_t)LAN8651_SQICTL_SQIEN);
                lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v);
                lan8651_write_reg(&g_lan8651, LAN8651_SQICTL, v | LAN8651_SQICTL_SQIEN);
                start = g_TickCount_1ms;
                continue;
            }
            lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v);
            lan8651_write_reg(&g_lan8651, LAN8651_SQICTL, v & ~(uint32_t)LAN8651_SQICTL_SQIEN);
            return -2;
        }
    }
    lan8651_read_reg(&g_lan8651, LAN8651_SQICTL, &v);
    lan8651_write_reg(&g_lan8651, LAN8651_SQICTL, v & ~(uint32_t)LAN8651_SQICTL_SQIEN);
    return -1;
}

static int cmd_sqi(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32_t toid = 0xFFU;
    uint32_t sqival = 0U, errc = 0U;
    int rc;
    if (argc >= 2 && parse_u32(argv[1], &toid)) {
        shellPrint(shell, "usage: sqi [toid 0..255, default 0xFF=all]\r\n");
        return -1;
    }
    shellPrint(shell, "SQI measuring (toid=0x%02lX, up to 6s, needs RX traffic)...\r\n", (unsigned long)toid);
    rc = sqi_measure(toid, 6000U, &sqival, &errc);
    if (rc == 0) {
        shellPrint(shell, "SQI=%lu (%s)\r\n", (unsigned long)sqival, sqi_text(sqival));
    } else if (rc == -2) {
        shellPrint(shell, "SQI error, code=0x%lX (see datasheet SQIERRC)\r\n", (unsigned long)errc);
    } else if (rc == -1) {
        shellPrint(shell, "SQI timeout (no valid estimation in 6s; generate RX traffic e.g. ping/iperf and retry)\r\n");
    } else {
        shellPrint(shell, "SQI register access failed\r\n");
    }
    return rc == 0 ? 0 : -1;
}

static uint32_t ctr32(uint32_t hi_reg, uint32_t lo_reg)
{
    uint32_t hi = 0U, lo = 0U;
    lan8651_read_reg(&g_lan8651, hi_reg, &hi);
    lan8651_read_reg(&g_lan8651, lo_reg, &lo);
    return ((hi & 0xFFFFU) << 16) | (lo & 0xFFFFU);
}

static int cmd_plcadiag(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32_t c0 = 0, c1 = 0, sts = 0, tot = 0, bst = 0, s1 = 0, prs = 0, ctr = 0;
    uint32_t bcn0, to0, bcn1, to1;
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL0, &c0);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_CTRL1, &c1);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_STS, &sts);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_TOTMR, &tot);
    lan8651_read_reg(&g_lan8651, LAN8651_PLCA_BURST, &bst);
    lan8651_read_reg(&g_lan8651, LAN8651_PRSSTS, &prs);
    /* enable TO/BCN counters if needed (sticky once enabled) */
    lan8651_read_reg(&g_lan8651, LAN8651_CTRCTRL, &ctr);
    if ((ctr & (LAN8651_CTRCTRL_TOCTRE | LAN8651_CTRCTRL_BCNCTRE)) !=
        (LAN8651_CTRCTRL_TOCTRE | LAN8651_CTRCTRL_BCNCTRE)) {
        lan8651_write_reg(&g_lan8651, LAN8651_CTRCTRL,
            ctr | LAN8651_CTRCTRL_TOCTRE | LAN8651_CTRCTRL_BCNCTRE);
        shellPrint(shell, "(CTRCTRL counters enabled)\r\n");
    }
    bcn0 = ctr32(LAN8651_BCNCNTH, LAN8651_BCNCNTL);
    to0 = ctr32(LAN8651_TOCNTH, LAN8651_TOCNTL);
    { uint32_t s = g_TickCount_1ms; while ((g_TickCount_1ms - s) < 1000U) {
        Ifx_Lwip_pollTimerFlags(); Ifx_Lwip_pollReceiveFlags(); } }
    bcn1 = ctr32(LAN8651_BCNCNTH, LAN8651_BCNCNTL);
    to1 = ctr32(LAN8651_TOCNTH, LAN8651_TOCNTL);
    s1 = 0U;
    lan8651_read_reg(&g_lan8651, LAN8651_STS1, &s1); /* RC: read-clear */
    shellPrint(shell, "PLCA en=%u id=%lu ncnt=%lu pst=%u tot=0x%04lX burst=0x%04lX maxid=%lu\r\n",
        (unsigned)((c0 & LAN8651_PLCA_CTRL0_EN) != 0U),
        (unsigned long)(c1 & 0xFFU), (unsigned long)((c1 >> 8) & 0xFFU),
        (unsigned)((sts & LAN8651_PLCA_STS_PST) != 0U),
        (unsigned long)(tot & 0xFFFFU), (unsigned long)(bst & 0xFFFFU),
        (unsigned long)((prs >> LAN8651_PRSSTS_MAXID_SHIFT) & 0xFFU));
    shellPrint(shell, "BEACON %lu/s, TO %lu/s (1s window, 32-bit wrap-aware diff)\r\n",
        (unsigned long)(bcn1 - bcn0), (unsigned long)(to1 - to0));
    shellPrint(shell, "STS1(RC,read-clear)=0x%04lX: EMPCYC=%u RXINTO=%u UNEXPB=%u BCNBFTO=%u PSTC=%u\r\n",
        (unsigned long)(s1 & 0xFFFFU),
        (unsigned)((s1 & LAN8651_STS1_EMPCYC) != 0U),
        (unsigned)((s1 & LAN8651_STS1_RXINTO) != 0U),
        (unsigned)((s1 & LAN8651_STS1_UNEXPB) != 0U),
        (unsigned)((s1 & LAN8651_STS1_BCNBFTO) != 0U),
        (unsigned)((s1 & LAN8651_STS1_PSTC) != 0U));
    return 0;
}

static int cmd_pcsdiag(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32_t s1 = 0, s2 = 0, s3 = 0, ncr = 0, ncfgr = 0, nsr = 0, tsr = 0, rsr = 0;
    lan8651_read_reg(&g_lan8651, LAN8651_STS1, &s1); /* RC */
    lan8651_read_reg(&g_lan8651, LAN8651_STS2, &s2); /* RC */
    lan8651_read_reg(&g_lan8651, LAN8651_STS3, &s3); /* RC */
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_NCR, &ncr);
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_NCFGR, &ncfgr);
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_NSR, &nsr);
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_TSR, &tsr);
    lan8651_read_reg(&g_lan8651, LAN8651_MAC_RSR, &rsr);
    shellPrint(shell, "STS1(RC)=0x%04lX: DEC5B=%u ESDERR=%u PLCASYM=%u UNCRS=%u SQI=%u TXCOL=%u TXJAB=%u TSSI=%u\r\n",
        (unsigned long)(s1 & 0xFFFFU),
        (unsigned)((s1 & LAN8651_STS1_DEC5B) != 0U),
        (unsigned)((s1 & LAN8651_STS1_ESDERR) != 0U),
        (unsigned)((s1 & LAN8651_STS1_PLCASYM) != 0U),
        (unsigned)((s1 & LAN8651_STS1_UNCRS) != 0U),
        (unsigned)((s1 & LAN8651_STS1_SQI) != 0U),
        (unsigned)((s1 & LAN8651_STS1_TXCOL) != 0U),
        (unsigned)((s1 & LAN8651_STS1_TXJAB) != 0U),
        (unsigned)((s1 & LAN8651_STS1_TSSI) != 0U));
    shellPrint(shell, "STS2(RC)=0x%04lX: UV33=%u OT=%u IWDTO=%u WKEMDI=%u WKEWI=%u; STS3(RC)=0x%04lX ERRTOID=%lu\r\n",
        (unsigned long)(s2 & 0xFFFFU),
        (unsigned)((s2 & LAN8651_STS2_UV33) != 0U),
        (unsigned)((s2 & LAN8651_STS2_OT) != 0U),
        (unsigned)((s2 & LAN8651_STS2_IWDTO) != 0U),
        (unsigned)((s2 & LAN8651_STS2_WKEMDI) != 0U),
        (unsigned)((s2 & LAN8651_STS2_WKEWI) != 0U),
        (unsigned long)(s3 & 0xFFFFU), (unsigned long)(s3 & 0xFFU));
    shellPrint(shell, "MAC ncr=0x%02lX ncfgr=0x%08lX nsr=0x%02lX(IDLE=%u) tsr=0x%02lX(COL=%u TXCOMP=%u) rsr=0x%02lX(REC=%u)\r\n",
        (unsigned long)(ncr & 0xFFU), (unsigned long)ncfgr, (unsigned long)(nsr & 0xFFU),
        (unsigned)((nsr & LAN8651_MAC_NSR_IDLE) != 0U),
        (unsigned long)(tsr & 0xFFU),
        (unsigned)((tsr & LAN8651_MAC_TSR_COL) != 0U),
        (unsigned)((tsr & LAN8651_MAC_TSR_TXCOMP) != 0U),
        (unsigned long)(rsr & 0xFFU),
        (unsigned)((rsr & LAN8651_MAC_RSR_REC) != 0U));
    shellPrint(shell, "(STS1/2/3 are read-clear: bits show events since last pcsdiag/plcadiag)\r\n");
    return 0;
}

static int cmd_evcnt(int argc, char *argv[])
{
    (void)argc; (void)argv;
    Shell *shell = shellGetCurrent();
    uint32_t ctr = 0;
    lan8651_read_reg(&g_lan8651, LAN8651_CTRCTRL, &ctr);
    if ((ctr & (LAN8651_CTRCTRL_TOCTRE | LAN8651_CTRCTRL_BCNCTRE)) !=
        (LAN8651_CTRCTRL_TOCTRE | LAN8651_CTRCTRL_BCNCTRE)) {
        lan8651_write_reg(&g_lan8651, LAN8651_CTRCTRL,
            ctr | LAN8651_CTRCTRL_TOCTRE | LAN8651_CTRCTRL_BCNCTRE);
    }
    shellPrint(shell, "TO_cnt=%lu BCN_cnt=%lu (cumulative since counter enable)\r\n",
        (unsigned long)ctr32(LAN8651_TOCNTH, LAN8651_TOCNTL),
        (unsigned long)ctr32(LAN8651_BCNCNTH, LAN8651_BCNCNTL));
    return 0;
}

static int cmd_cable(int argc, char *argv[])
{
    Shell *shell = shellGetCurrent();
    uint32_t toid = 0xFFU;
    uint32_t sqival = 0U, errc = 0U;
    uint32_t s1 = 0, s2 = 0, bmsr = 0;
    int rc;
    if (argc >= 2 && parse_u32(argv[1], &toid)) {
        shellPrint(shell, "usage: cable [toid, default 0xFF=all]\r\n");
        shellPrint(shell, "  cable health = SQI + error sticky bits (note: Microchip HDD algorithm is NDA-only)\r\n");
        return -1;
    }
    shellPrint(shell, "cable health check (SQI up to 6s, needs RX traffic)...\r\n");
    rc = sqi_measure(toid, 6000U, &sqival, &errc);
    lan8651_read_reg(&g_lan8651, LAN8651_STS1, &s1); /* RC */
    lan8651_read_reg(&g_lan8651, LAN8651_STS2, &s2); /* RC */
    lan8651_read_reg(&g_lan8651, LAN8651_PHY_BMSR, &bmsr);
    lan8651_read_reg(&g_lan8651, LAN8651_PHY_BMSR, &bmsr);
    if (rc == 0) {
        const char *verdict =
            (sqival >= 6) ? "GOOD" :
            (sqival >= 4) ? "MARGINAL" : "POOR (check wiring/termination/length)";
        shellPrint(shell, "SQI=%lu (%s) -> cable %s\r\n", (unsigned long)sqival, sqi_text(sqival), verdict);
    } else if (rc == -2) {
        shellPrint(shell, "SQI error code=0x%lX -> cable check inconclusive, retry with traffic\r\n", (unsigned long)errc);
    } else {
        shellPrint(shell, "SQI timeout -> cable check inconclusive (generate RX traffic and retry)\r\n");
    }
    shellPrint(shell, "phy_link=%u STS1err(DEC5B/ESDERR/PLCASYM/UNCRS)=%u%u%u%u UV33=%u OT=%u\r\n",
        (unsigned)((bmsr & LAN8651_PHY_BMSR_LINK_STATUS) != 0U),
        (unsigned)((s1 & LAN8651_STS1_DEC5B) != 0U),
        (unsigned)((s1 & LAN8651_STS1_ESDERR) != 0U),
        (unsigned)((s1 & LAN8651_STS1_PLCASYM) != 0U),
        (unsigned)((s1 & LAN8651_STS1_UNCRS) != 0U),
        (unsigned)((s2 & LAN8651_STS2_UV33) != 0U),
        (unsigned)((s2 & LAN8651_STS2_OT) != 0U));
    return 0;
}


/* ---------- Shell export (LAN8651 / 10BASE-T1S) ---------- */
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), t1stat, cmd_t1stat, LAN8651 status dump);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), t1r, cmd_t1r, LAN8651 reg read);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), t1w, cmd_t1w, LAN8651 reg write);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), plca, cmd_plca, show or set PLCA id/count);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), t1link, cmd_t1link, T1S link status);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), sqi, cmd_sqi, SQI signal quality 0-7);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), plcadiag, cmd_plcadiag, PLCA diag + beacon rate);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), pcsdiag, cmd_pcsdiag, PCS/MAC error sticky bits);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), evcnt, cmd_evcnt, TO/BEACON cumulative counters);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), cable, cmd_cable, cable health via SQI+errors);