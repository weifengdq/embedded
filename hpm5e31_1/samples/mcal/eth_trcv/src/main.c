/*
 * eth_trcv —— AUTOSAR EthTrcv（YT8011AN）链路/速率/主从切换与建链耗时实测
 *
 * 硬件：hpm5e31_1，YT8011AN 1000BASE-T1 <-> 千兆车载以太网转换盒 <-> 主机 enp6s0
 * 驱动：mcal/EthTrcv（EthTrcv_Init/GetLinkState/GetBaudRate/GetDuplexMode/
 *       GetPhyIdentifier/MainFunction/SetTransceiverMode）+
 *       mcal/Eth 的 MII/MMD 服务（Eth_ReadMii/WriteMii/ReadMmd/WriteMmd）
 * 说明：SWS 没有 EthTrcv_SetBaudRate，速率/主从切换属于 PHY 的"配置类"操作；
 *       本样例用 Eth 提供的 MII/MMD 服务直接写 YT8011AN 的 MMD1 0x834 + BMCR，
 *       再读回 EthTrcv_GetBaudRate 验证 —— 正好演示 Eth→EthTrcv 的服务边界。
 *
 * 串口自证格式：
 *   TRCV link=UP baud=1000M duplex=FULL master=SLAVE oui=0x13D47A model=48 rev=1 sqi=0x0007
 *   SWITCH speed=100M link=UP t=...ms
 *   SWITCH master=MASTER link=UP t=...ms
 */
#include "bsp.h"
#include "Eth.h"
#include "Eth_PBcfg.h"
#include "EthTrcv.h"
#include "EthTrcv_PBcfg.h"
#include "Det.h"
#include "hpm_csr_drv.h"
#include "hpm_clock_drv.h"
#include <stdio.h>
#include <string.h>

#define CTRL   0U
#define TRCV   0U

/* YT8011AN 寄存器（与 mcal/EthTrcv/src/EthTrcv.c 的内部定义一致） */
#define YT_BMCR          (0x00U)
#define YT_BMSR          (0x01U)
#define YT_PHYID1        (0x02U)
#define YT_PHYID2        (0x03U)
#define YT_GBCR          (0x09U)
#define YT_GBSR          (0x0AU)
#define YT_PHYSR         (0x11U)
#define YT_EXT_ADDR      (0x1EU)
#define YT_EXT_DATA      (0x1FU)
#define YT_BMCR_FORCE_1000M (0x8140U)
#define YT_BMCR_FORCE_100M  (0xA100U)
#define YT_MMD1_MODE_REG (0x0834U)

static uint32 s_cycles_per_us = 480U;

static uint32 now_us(void)
{
    return (uint32) (hpm_csr_get_core_mcycle() / (uint64_t) s_cycles_per_us);
}

static const char *baud_str(EthTrcv_BaudRateType b)
{
    switch (b) {
    case ETHTRCV_BAUD_RATE_1000MBIT: return "1000M";
    case ETHTRCV_BAUD_RATE_100MBIT:  return "100M";
    case ETHTRCV_BAUD_RATE_10MBIT:   return "10M";
    default: return "?";
    }
}

/*! EXT 寄存器读（0x1E 地址 / 0x1F 数据） */
static Std_ReturnType ext_read(uint16 addr, uint16 *val)
{
    if (Eth_WriteMii(CTRL, 0U, YT_EXT_ADDR, addr) != E_OK) {
        return E_NOT_OK;
    }
    return Eth_ReadMii(CTRL, 0U, YT_EXT_DATA, val);
}

static void cmd_info(void)
{
    EthTrcv_LinkStateType link = ETHTRCV_LINK_STATE_DOWN;
    EthTrcv_BaudRateType baud = ETHTRCV_BAUD_RATE_1000MBIT;
    EthTrcv_DuplexModeType dup = ETHTRCV_DUPLEX_MODE_FULL;
    EthTrcv_MacMethodType mac = ETHTRCV_MAC_TYPE_UNKNOWN;
    Eth_ModeType tmode = ETH_MODE_DOWN;
    uint32 oui = 0U, sqi = 0U;
    uint8 model = 0U, rev = 0U, idx = TRCV;
    uint16 physr = 0U;

    (void) EthTrcv_GetLinkState(TRCV, &link);
    (void) EthTrcv_GetBaudRate(TRCV, &baud);
    (void) EthTrcv_GetDuplexMode(TRCV, &dup);
    (void) EthTrcv_GetTransceiverMode(TRCV, &tmode);
    (void) EthTrcv_GetPhyIdentifier(TRCV, &oui, &model, &rev);
    (void) EthTrcv_GetMacMethod(&idx, &mac);
    (void) EthTrcv_GetPhySignalQuality(TRCV, &sqi);
    (void) Eth_ReadMii(CTRL, 0U, YT_PHYSR, &physr);

    printf("TRCV link=%s baud=%s duplex=%s master=%s oui=0x%06X model=%u rev=%u sqi=0x%04X tmode=%s PHYSR=0x%04X\r\n",
           (link == ETHTRCV_LINK_STATE_ACTIVE) ? "UP" : "DOWN", baud_str(baud),
           (dup == ETHTRCV_DUPLEX_MODE_FULL) ? "FULL" : "HALF",
           ((physr & 0x2000U) != 0U) ? "MASTER" : "SLAVE",
           (unsigned) oui, (unsigned) model, (unsigned) rev, (unsigned) sqi,
           (tmode == ETH_MODE_ACTIVE) ? "ACTIVE" : "DOWN", (unsigned) physr);
}

/*! 等链路到目标状态，返回耗时 ms（超时打印 timeout） */
static uint32 wait_link(EthTrcv_LinkStateType want, uint32 timeout_ms)
{
    uint32 t0 = now_us();
    EthTrcv_LinkStateType ls = ETHTRCV_LINK_STATE_DOWN;

    while ((now_us() - t0) < (timeout_ms * 1000U)) {
        EthTrcv_MainFunction();
        (void) EthTrcv_GetLinkState(TRCV, &ls);
        if (ls == want) {
            return (now_us() - t0) / 1000U;
        }
        board_delay_ms(1U);
    }
    printf("TRCV wait_link timeout (%s)\r\n", (want == ETHTRCV_LINK_STATE_ACTIVE) ? "UP" : "DOWN");
    return 0xFFFFFFFFU;
}

/*! 速率/主从切换：写 MMD1 0x834 + 带速率编码的软复位，然后测建链耗时 */
static void cmd_switch(boolean to_1000m, boolean master)
{
    uint16 mode = (uint16) ((master ? 0x4000U : 0x0000U) | (to_1000m ? 0x0001U : 0x0000U));
    uint16 bmcr = to_1000m ? YT_BMCR_FORCE_1000M : YT_BMCR_FORCE_100M;
    uint32 t_switch, t_up;
    EthTrcv_BaudRateType baud = ETHTRCV_BAUD_RATE_1000MBIT;
    uint16 physr = 0U;

    t_switch = now_us();
    (void) Eth_WriteMmd(CTRL, 0U, 1U, YT_MMD1_MODE_REG, mode);
    (void) Eth_WriteMii(CTRL, 0U, YT_BMCR, bmcr);
    /* 等软复位自清 */
    {
        uint16 b = 0U;
        uint32 t = 0U;
        do {
            (void) Eth_ReadMii(CTRL, 0U, YT_BMCR, &b);
            board_delay_ms(1U);
            t++;
        } while (((b & 0x8000U) != 0U) && (t < 100U));
    }
    t_up = wait_link(ETHTRCV_LINK_STATE_ACTIVE, 5000U);
    (void) EthTrcv_GetBaudRate(TRCV, &baud);
    (void) Eth_ReadMii(CTRL, 0U, YT_PHYSR, &physr);
    printf("SWITCH speed=%s master=%s link=%s t_reset=%ums t_link=%ums baud=%s PHYSR=0x%04X det=%u\r\n",
           to_1000m ? "1000M" : "100M", master ? "MASTER" : "SLAVE",
           (t_up == 0xFFFFFFFFU) ? "DOWN" : "UP",
           (unsigned) ((now_us() - t_switch) / 1000U), (unsigned) t_up,
           baud_str(baud), (unsigned) physr, (unsigned) Det_GetErrorCount());
}

static void cmd_regs(void)
{
    const uint8 names[][12] = { "BMCR", "BMSR", "PHYID1", "PHYID2", "ANAR", "ANLPAR",
                                "ANER", "ANNPTR", "ANNPRR", "GBCR", "GBSR" };
    uint16 v = 0U;
    uint16 ext = 0U, mmd = 0U;
    uint8 i;

    for (i = 0U; i < 11U; i++) {
        if (Eth_ReadMii(CTRL, 0U, i, &v) == E_OK) {
            printf("REG %-8s(0x%02X) = 0x%04X\r\n", (const char *) &names[i][0], (unsigned) i, (unsigned) v);
        }
    }
    if (Eth_ReadMii(CTRL, 0U, YT_PHYSR, &v) == E_OK) {
        printf("REG %-8s(0x%02X) = 0x%04X\r\n", "PHYSR", (unsigned) YT_PHYSR, (unsigned) v);
    }
    if (ext_read(0x9001U, &ext) == E_OK) {
        printf("EXT 0x9001 (RGMII cfg1) = 0x%04X (RXC_DLY_EN=%u TX_DLY=%u TX_CLK_SEL=%u)\r\n",
               (unsigned) ext, (unsigned) ((ext >> 8) & 1U), (unsigned) ((ext >> 4) & 0xFU),
               (unsigned) ((ext >> 9) & 1U));
    }
    if (Eth_ReadMmd(CTRL, 0U, 1U, YT_MMD1_MODE_REG, &mmd) == E_OK) {
        printf("MMD1 0x0834 (force mode) = 0x%04X (master=%u phy_type=%u)\r\n",
               (unsigned) mmd, (unsigned) ((mmd >> 14) & 1U), (unsigned) (mmd & 0xFU));
    }
}

static void cmd_mode(const char *sub)
{
    Eth_ModeType m = ETH_MODE_DOWN;

    if (strcmp(sub, "down") == 0) {
        (void) EthTrcv_SetTransceiverMode(TRCV, ETH_MODE_DOWN);
    } else if (strcmp(sub, "active") == 0) {
        (void) EthTrcv_SetTransceiverMode(TRCV, ETH_MODE_ACTIVE);
    } else {
        printf("usage: trcv mode down|active\r\n");
        return;
    }
    (void) EthTrcv_GetTransceiverMode(TRCV, &m);
    printf("TRCV tmode=%s\r\n", (m == ETH_MODE_ACTIVE) ? "ACTIVE" : "DOWN");
}

/*! 链路轮询：跑 secs 秒 EthTrcv_MainFunction，统计状态翻转次数 */
static void cmd_poll(uint32 secs)
{
    uint32 t0 = now_us();
    uint32 flips = 0U;
    EthTrcv_LinkStateType last = ETHTRCV_LINK_STATE_DOWN;
    EthTrcv_LinkStateType now = ETHTRCV_LINK_STATE_DOWN;

    (void) EthTrcv_GetLinkState(TRCV, &last);
    while ((now_us() - t0) < (secs * 1000000U)) {
        EthTrcv_MainFunction();
        (void) EthTrcv_GetLinkState(TRCV, &now);
        if (now != last) {
            flips++;
            last = now;
            printf("TRCV link_change -> %s at %ums\r\n",
                   (now == ETHTRCV_LINK_STATE_ACTIVE) ? "UP" : "DOWN",
                   (unsigned) ((now_us() - t0) / 1000U));
        }
        board_delay_ms(1U);
    }
    printf("TRCV poll secs=%u flips=%u\r\n", (unsigned) secs, (unsigned) flips);
}

static void cmd_trcv(int argc, char *argv[])
{
    const char *sub = (argc >= 2) ? argv[1] : "info";

    if (strcmp(sub, "info") == 0) {
        cmd_info();
    } else if (strcmp(sub, "regs") == 0) {
        cmd_regs();
    } else if (strcmp(sub, "link") == 0) {
        uint32 ms = (argc >= 3) ? (uint32) strtoul(argv[2], NULL, 0) : 3000U;
        uint32 t = wait_link(ETHTRCV_LINK_STATE_ACTIVE, ms);
        printf("TRCV link=%s t=%ums\r\n", (t == 0xFFFFFFFFU) ? "DOWN" : "UP", (unsigned) t);
    } else if (strcmp(sub, "s1000") == 0) {
        cmd_switch(TRUE, FALSE);
    } else if (strcmp(sub, "s100") == 0) {
        cmd_switch(FALSE, FALSE);
    } else if (strcmp(sub, "master") == 0) {
        cmd_switch(TRUE, TRUE);
    } else if (strcmp(sub, "slave") == 0) {
        cmd_switch(TRUE, FALSE);
    } else if (strcmp(sub, "mode") == 0) {
        cmd_mode((argc >= 3) ? argv[2] : "");
    } else if (strcmp(sub, "poll") == 0) {
        cmd_poll((argc >= 3) ? (uint32) strtoul(argv[2], NULL, 0) : 5U);
    } else {
        printf("unknown sub: %s (info|regs|link [ms]|s1000|s100|master|slave|mode|poll [s])\r\n", sub);
    }
}
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN) | SHELL_CMD_DISABLE_RETURN,
                 trcv, cmd_trcv, ETH transceiver test: trcv info|regs|link [ms]|s1000|s100|master|slave|mode|poll [s]);

int main(void)
{
    uint32 cpu_mhz;

    bsp_init();
    cpu_mhz = clock_get_frequency(clock_cpu0) / 1000000U;
    s_cycles_per_us = (cpu_mhz != 0U) ? cpu_mhz : 480U;

    printf("\r\n=== eth_trcv: AUTOSAR EthTrcv + YT8011AN 1000BASE-T1 link/speed/master test ===\r\n");
    Det_Init();
    Det_ClearErrorCount();

    Eth_Init(&Eth_Config);
    EthTrcv_Init(&EthTrcv_Config);
    (void) Eth_SetControllerMode(CTRL, ETH_MODE_ACTIVE);
    printf("[boot] cpu=%u MHz, det=%u\r\n", (unsigned) cpu_mhz, (unsigned) Det_GetErrorCount());

    cmd_info();

    while (1) {
        Eth_MainFunction();
        EthTrcv_MainFunction();
        bsp_shell_poll();
    }
}
