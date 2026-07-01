/**
  ******************************************************************************
  * @file    shell_port.c
  * @brief   Letter-shell port for LAN9370 diagnostics
  ******************************************************************************
  */

#include "shell_port.h"
#include "lan9370_driver.h"
#include "lan9370_persist.h"
#include "lan9370_spi.h"
#include "mdio_bitbang.h"
#include "lan8720_driver.h"
#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LAN8720_ACTIVE_PHY_ADDR    LAN8720_PHY_ADDR_DEFAULT

static UART_HandleTypeDef *pHuart = NULL;
extern SPI_HandleTypeDef hspi1;
static Shell gShell;
static char gShellBuffer[512];
static uint8_t gRxByte;

#define SHELL_RX_RING_SIZE 512
static volatile uint16_t gRxHead = 0;
static volatile uint16_t gRxTail = 0;
static uint8_t gRxRing[SHELL_RX_RING_SIZE];

static void Shell_RxPush(uint8_t ch)
{
    uint16_t next = (uint16_t)((gRxHead + 1U) % SHELL_RX_RING_SIZE);
    if (next != gRxTail) {
        gRxRing[gRxHead] = ch;
        gRxHead = next;
    }
}

static int Shell_RxPop(uint8_t *ch)
{
    if (ch == NULL || gRxHead == gRxTail) {
        return 0;
    }
    *ch = gRxRing[gRxTail];
    gRxTail = (uint16_t)((gRxTail + 1U) % SHELL_RX_RING_SIZE);
    return 1;
}

static short userShellWrite(char *data, unsigned short len)
{
    if (pHuart == NULL || data == NULL || len == 0) {
        return 0;
    }
    if (HAL_UART_Transmit(pHuart, (uint8_t *)data, len, HAL_MAX_DELAY) != HAL_OK) {
        return 0;
    }
    return len;
}

static short userShellRead(char *data, unsigned short len)
{
    if (pHuart == NULL || data == NULL || len == 0) {
        return 0;
    }

    if (HAL_UART_Receive(pHuart, (uint8_t *)data, len, 1) == HAL_OK) {
        return len;
    }
    return 0;
}

static int parse_port(const char *arg, int min_port, int max_port, int *port)
{
    long value;
    char *end_ptr = NULL;

    if (arg == NULL || port == NULL) {
        return -1;
    }

    value = strtol(arg, &end_ptr, 0);
    if (*arg == '\0' || (end_ptr != NULL && *end_ptr != '\0')) {
        return -1;
    }
    if (value < min_port || value > max_port) {
        return -1;
    }

    *port = (int)value;
    return 0;
}

static int parse_u32(const char *arg, uint32_t *value)
{
    char *end_ptr = NULL;
    unsigned long parsed;

    if (arg == NULL || value == NULL) {
        return -1;
    }

    parsed = strtoul(arg, &end_ptr, 0);
    if (*arg == '\0' || (end_ptr != NULL && *end_ptr != '\0')) {
        return -1;
    }

    *value = (uint32_t)parsed;
    return 0;
}

static int capture_current_settings(LAN9370_PersistSettings_t *settings)
{
    if (settings == NULL) {
        return -1;
    }

    memset(settings, 0, sizeof(*settings));

    for (int port = 1; port <= 4; port++) {
        LAN9370_T1_Mode_t mode;
        if (LAN9370_GetT1MasterSlave((LAN9370_Port_t)port, &mode) != LAN9370_OK) {
            return -1;
        }
        settings->t1Mode[port - 1] = (uint8_t)((mode == LAN9370_T1_MASTER) ? 1U : 0U);
    }

    for (int port = 1; port <= 5; port++) {
        LAN9370_Port_t dst = 0;
        uint16_t vid = 1;
        if (LAN9370_GetPortMirror((LAN9370_Port_t)port, &dst, NULL) != LAN9370_OK) {
            return -1;
        }
        if (LAN9370_GetPortDefaultVlan((LAN9370_Port_t)port, &vid) != LAN9370_OK) {
            return -1;
        }
        settings->mirrorDst[port - 1] = (uint8_t)dst;
        settings->vlanVid[port - 1] = vid;
    }

    bool vlanEnabled;
    bool ptpEnabled;
    bool gptpEnabled;

    if (LAN9370_GetVlanEnable(&vlanEnabled) != LAN9370_OK) {
        return -1;
    }
    if (LAN9370_GetPtpEnable(&ptpEnabled) != LAN9370_OK) {
        return -1;
    }
    if (LAN9370_GetGptpEnable(&gptpEnabled) != LAN9370_OK) {
        return -1;
    }

    settings->vlanEnable = (uint8_t)(vlanEnabled ? 1U : 0U);
    settings->ptpEnable = (uint8_t)(ptpEnabled ? 1U : 0U);
    settings->gptpEnable = (uint8_t)(gptpEnabled ? 1U : 0U);

    return 0;
}

static int apply_settings(const LAN9370_PersistSettings_t *settings)
{
    if (settings == NULL) {
        return -1;
    }

    for (int port = 1; port <= 4; port++) {
        LAN9370_T1_Mode_t mode = (settings->t1Mode[port - 1] != 0U) ? LAN9370_T1_MASTER : LAN9370_T1_SLAVE;
        if (LAN9370_SetT1MasterSlave((LAN9370_Port_t)port, mode) != LAN9370_OK) {
            return -1;
        }
    }

    if (LAN9370_SetVlanEnable(settings->vlanEnable != 0U) != LAN9370_OK) {
        return -1;
    }

    for (int port = 1; port <= 5; port++) {
        uint16_t vid = settings->vlanVid[port - 1];
        uint8_t dst = settings->mirrorDst[port - 1];
        if (vid >= 1U && vid <= 4094U) {
            if (LAN9370_SetPortDefaultVlan((LAN9370_Port_t)port, vid) != LAN9370_OK) {
                return -1;
            }
        }
        if (dst <= 5U) {
            if (LAN9370_SetPortMirror((LAN9370_Port_t)port, (LAN9370_Port_t)dst, false) != LAN9370_OK) {
                return -1;
            }
        }
    }

    if (LAN9370_SetPtpEnable(settings->ptpEnable != 0U) != LAN9370_OK) {
        return -1;
    }
    if (LAN9370_SetGptpEnable(settings->gptpEnable != 0U) != LAN9370_OK) {
        return -1;
    }

    return 0;
}

int cmd_info(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    LAN9370_ChipInfo_t info;
    if (LAN9370_GetChipInfo(&info) != LAN9370_OK) {
        printf("read chip info failed\r\n");
        return -1;
    }
    printf("Chip: %s, ID: 0x%08lX, Rev: %d\r\n", info.chipName, info.chipId, info.revisionId);
    
    /* Print all port status */
    if (LAN9370_PrintAllPortsStatus() != LAN9370_OK) {
        printf("read port status failed\r\n");
        return -1;
    }
    
    /* Print VLAN configuration */
    if (LAN9370_PrintVlanConfig() != LAN9370_OK) {
        printf("read vlan config failed\r\n");
        return -1;
    }
    
    return 0;
}

int cmd_dump(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return (LAN9370_DumpRegisters() == LAN9370_OK) ? 0 : -1;
}

int cmd_reset(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    return (LAN9370_HardwareReset() == LAN9370_OK) ? 0 : -1;
}

int cmd_port(int argc, char *argv[])
{
    int port;
    if (argc < 2 || parse_port(argv[1], 1, 5, &port) != 0) {
        printf("usage: port <1-5>\r\n");
        return -1;
    }
    return (LAN9370_PrintPortStatus((LAN9370_Port_t)port) == LAN9370_OK) ? 0 : -1;
}

int cmd_master(int argc, char *argv[])
{
    int port;
    if (argc < 2 || parse_port(argv[1], 1, 4, &port) != 0) {
        printf("usage: master <1-4>\r\n");
        return -1;
    }
    return (LAN9370_SetT1MasterSlave((LAN9370_Port_t)port, LAN9370_T1_MASTER) == LAN9370_OK) ? 0 : -1;
}

int cmd_slave(int argc, char *argv[])
{
    int port;
    if (argc < 2 || parse_port(argv[1], 1, 4, &port) != 0) {
        printf("usage: slave <1-4>\r\n");
        return -1;
    }
    return (LAN9370_SetT1MasterSlave((LAN9370_Port_t)port, LAN9370_T1_SLAVE) == LAN9370_OK) ? 0 : -1;
}

int cmd_enable(int argc, char *argv[])
{
    int port;
    if (argc < 2 || parse_port(argv[1], 1, 5, &port) != 0) {
        printf("usage: enable <1-5>\r\n");
        return -1;
    }
    return (LAN9370_SetPortEnable((LAN9370_Port_t)port, true) == LAN9370_OK) ? 0 : -1;
}

int cmd_disable(int argc, char *argv[])
{
    int port;
    if (argc < 2 || parse_port(argv[1], 1, 5, &port) != 0) {
        printf("usage: disable <1-5>\r\n");
        return -1;
    }
    return (LAN9370_SetPortEnable((LAN9370_Port_t)port, false) == LAN9370_OK) ? 0 : -1;
}

int cmd_phyread(int argc, char *argv[])
{
    int port;
    uint16_t value;
    long reg;

    if (argc < 3 || parse_port(argv[1], 1, 4, &port) != 0) {
        printf("usage: phyread <port> <reg>\r\n");
        return -1;
    }

    reg = strtol(argv[2], NULL, 0);
    if (reg < 0 || reg > 31) {
        printf("invalid reg\r\n");
        return -1;
    }

    if (LAN9370_PHY_ReadReg((LAN9370_Port_t)port, (uint8_t)reg, &value) != LAN9370_OK) {
        return -1;
    }

    printf("PHY[%d][0x%02lX]=0x%04X\r\n", port, reg, value);
    return 0;
}

int cmd_phywrite(int argc, char *argv[])
{
    int port;
    long reg;
    long value;

    if (argc < 4 || parse_port(argv[1], 1, 4, &port) != 0) {
        printf("usage: phywrite <port> <reg> <value>\r\n");
        return -1;
    }

    reg = strtol(argv[2], NULL, 0);
    value = strtol(argv[3], NULL, 0);
    if (reg < 0 || reg > 31 || value < 0 || value > 0xFFFF) {
        printf("invalid reg/value\r\n");
        return -1;
    }

    return (LAN9370_PHY_WriteReg((LAN9370_Port_t)port, (uint8_t)reg, (uint16_t)value) == LAN9370_OK) ? 0 : -1;
}

int cmd_vlan(int argc, char *argv[])
{
    int port;
    long vid;

    if (argc < 2) {
        printf("usage: vlan on|off|set <port> <vid>|show\r\n");
        return -1;
    }

    if (strcmp(argv[1], "on") == 0) {
        return (LAN9370_SetVlanEnable(true) == LAN9370_OK) ? 0 : -1;
    }
    if (strcmp(argv[1], "off") == 0) {
        return (LAN9370_SetVlanEnable(false) == LAN9370_OK) ? 0 : -1;
    }
    if (strcmp(argv[1], "show") == 0) {
        return (LAN9370_PrintVlanConfig() == LAN9370_OK) ? 0 : -1;
    }
    if (strcmp(argv[1], "set") != 0 || argc < 4 || parse_port(argv[2], 1, 5, &port) != 0) {
        printf("usage: vlan set <1-5> <1-4094>\r\n");
        return -1;
    }

    vid = strtol(argv[3], NULL, 0);
    if (vid < 1 || vid > 4094) {
        printf("invalid vid\r\n");
        return -1;
    }

    return (LAN9370_SetPortDefaultVlan((LAN9370_Port_t)port, (uint16_t)vid) == LAN9370_OK) ? 0 : -1;
}

int cmd_portgroup(int argc, char *argv[])
{
    int port;
    uint32_t mask;

    if (argc < 3) {
        printf("usage: portgroup <port> <memberMask>\r\n");
        printf("  Sets which ports a given port can forward TO\r\n");
        printf("  memberMask: bit0=Port1 bit1=Port2 bit2=Port3 bit3=Port4 bit4=Port5\r\n");
        printf("  Examples:\r\n");
        printf("    portgroup 2 0x10  -> Port2 can only forward to Port5\r\n");
        printf("    portgroup 5 0x02  -> Port5 can only forward to Port2\r\n");
        printf("    portgroup 2 0x1F  -> restore Port2 to full forwarding\r\n");
        return -1;
    }

    if (parse_port(argv[1], 1, 5, &port) != 0) {
        printf("port 1-5\r\n");
        return -1;
    }
    if (parse_u32(argv[2], &mask) != 0 || mask > 0x1F) {
        printf("mask 0-0x1F\r\n");
        return -1;
    }

    if (LAN9370_SetPortMembership((LAN9370_Port_t)port, (uint8_t)mask) != LAN9370_OK) {
        printf("portgroup failed\r\n");
        return -1;
    }

    printf("Port%d egress members set to 0x%02lX\r\n", port, (unsigned long)mask);
    return 0;
}

int cmd_portrecover(int argc, char *argv[])
{
    int port;

    if (argc < 2 || parse_port(argv[1], 1, 4, &port) != 0) {
        printf("usage: portrecover <1-4>\r\n");
        return -1;
    }

    if (LAN9370_RecoverT1Port((LAN9370_Port_t)port) != LAN9370_OK) {
        printf("portrecover failed\r\n");
        return -1;
    }

    printf("Port%d recovery requested\r\n", port);
    return 0;
}

int cmd_mirror(int argc, char *argv[])
{
    int srcPort, dstPort;

    if (argc < 3) {
        printf("usage: mirror <src_port> <dst_port|off>\r\n");
        printf("  Mirrors traffic from src_port to dst_port (1-5)\r\n");
        printf("  Use 'off' to disable mirror\r\n");
        return -1;
    }

    if (parse_port(argv[1], 1, 5, &srcPort) != 0) {
        printf("invalid source port\r\n");
        return -1;
    }

    if (strcmp(argv[2], "off") == 0) {
        dstPort = 0; /* Disable mirror */
    } else {
        if (parse_port(argv[2], 1, 5, &dstPort) != 0) {
            printf("invalid destination port\r\n");
            return -1;
        }
    }

    if (LAN9370_SetPortMirror((LAN9370_Port_t)srcPort, (LAN9370_Port_t)dstPort, false) != LAN9370_OK) {
        printf("set mirror failed\r\n");
        return -1;
    }

    if (dstPort == 0) {
        printf("mirror disabled on port %d\r\n", srcPort);
    } else {
        printf("port %d traffic mirrored to port %d\r\n", srcPort, dstPort);
    }

    return 0;
}

int cmd_ptp(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: ptp on|off|status|gptp on|off|status\r\n");
        return -1;
    }

    if (strcmp(argv[1], "on") == 0) {
        return (LAN9370_SetPtpEnable(true) == LAN9370_OK) ? 0 : -1;
    }
    if (strcmp(argv[1], "off") == 0) {
        return (LAN9370_SetPtpEnable(false) == LAN9370_OK) ? 0 : -1;
    }
    if (strcmp(argv[1], "status") == 0) {
        bool ptpEnabled;
        bool gptpEnabled;
        if (LAN9370_GetPtpEnable(&ptpEnabled) != LAN9370_OK ||
            LAN9370_GetGptpEnable(&gptpEnabled) != LAN9370_OK) {
            printf("ptp status read failed\r\n");
            return -1;
        }
        printf("PTP: %s, gPTP: %s\r\n",
               ptpEnabled ? "ENABLED" : "DISABLED",
               gptpEnabled ? "ENABLED" : "DISABLED");
        return 0;
    }

    if (strcmp(argv[1], "gptp") == 0) {
        if (argc < 3) {
            printf("usage: ptp gptp on|off|status\r\n");
            return -1;
        }
        if (strcmp(argv[2], "on") == 0) {
            return (LAN9370_SetGptpEnable(true) == LAN9370_OK) ? 0 : -1;
        }
        if (strcmp(argv[2], "off") == 0) {
            return (LAN9370_SetGptpEnable(false) == LAN9370_OK) ? 0 : -1;
        }
        if (strcmp(argv[2], "status") == 0) {
            bool gptpEnabled;
            if (LAN9370_GetGptpEnable(&gptpEnabled) != LAN9370_OK) {
                printf("gptp status read failed\r\n");
                return -1;
            }
            printf("gPTP: %s\r\n", gptpEnabled ? "ENABLED" : "DISABLED");
            return 0;
        }
        printf("usage: ptp gptp on|off|status\r\n");
        return -1;
    }

    printf("invalid ptp command\r\n");
    return -1;
}

int cmd_config(int argc, char *argv[])
{
    LAN9370_PersistSettings_t settings;

    if (argc < 2) {
        printf("usage: config save|load|show|erase\r\n");
        return -1;
    }

    if (strcmp(argv[1], "save") == 0) {
        if (capture_current_settings(&settings) != 0) {
            printf("capture current settings failed\r\n");
            return -1;
        }
        if (LAN9370_PersistSave(&settings) != 0) {
            printf("save settings failed\r\n");
            return -1;
        }
        printf("settings saved to MCU flash\r\n");
        return 0;
    }

    if (strcmp(argv[1], "load") == 0) {
        if (LAN9370_PersistLoad(&settings) != 0) {
            printf("no valid saved settings\r\n");
            return -1;
        }
        if (apply_settings(&settings) != 0) {
            printf("apply saved settings failed\r\n");
            return -1;
        }
        printf("saved settings loaded and applied\r\n");
        return 0;
    }

    if (strcmp(argv[1], "show") == 0) {
        if (LAN9370_PersistLoad(&settings) != 0) {
            printf("no valid saved settings\r\n");
            return -1;
        }
        printf("Saved settings:\r\n");
        printf("  VLAN: %s\r\n", settings.vlanEnable ? "ON" : "OFF");
        printf("  PTP: %s\r\n", settings.ptpEnable ? "ON" : "OFF");
        printf("  gPTP: %s\r\n", settings.gptpEnable ? "ON" : "OFF");
        for (int port = 1; port <= 4; port++) {
            printf("  Port%d mode: %s\r\n", port, settings.t1Mode[port - 1] ? "Master" : "Slave");
        }
        for (int port = 1; port <= 5; port++) {
            printf("  Port%d VLAN VID: %u, mirror dst: %u\r\n",
                   port,
                   (unsigned int)settings.vlanVid[port - 1],
                   (unsigned int)settings.mirrorDst[port - 1]);
        }
        return 0;
    }

    if (strcmp(argv[1], "erase") == 0) {
        if (LAN9370_PersistErase() != 0) {
            printf("erase saved settings failed\r\n");
            return -1;
        }
        printf("saved settings erased\r\n");
        return 0;
    }

    printf("usage: config save|load|show|erase\r\n");
    return -1;
}

int cmd_staticmac(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: staticmac list|flush\r\n");
        return -1;
    }
    if (strcmp(argv[1], "list") == 0) {
        return LAN9370_PrintStaticMacTable();
    }
    if (strcmp(argv[1], "flush") == 0) {
        return (LAN9370_FlushStaticMAC() == LAN9370_OK) ? 0 : -1;
    }
    printf("usage: staticmac list|flush\r\n");
    return -1;
}

int cmd_mib(int argc, char *argv[])
{
    int port;

    if (argc < 2 || parse_port(argv[1], 1, 5, &port) != 0) {
        printf("usage: mib <1-5>\r\n");
        return -1;
    }

    return (LAN9370_PrintPortMib((LAN9370_Port_t)port) == LAN9370_OK) ? 0 : -1;
}

int cmd_spiread(int argc, char *argv[])
{
    uint32_t addr;
    uint32_t count = 1;
    uint8_t val;

    if (argc < 2 || parse_u32(argv[1], &addr) != 0 || addr > 0xFFFF) {
        printf("usage: spiread <addr> [count]\r\n");
        return -1;
    }

    if (argc >= 3) {
        if (parse_u32(argv[2], &count) != 0 || count == 0 || count > 32) {
            printf("invalid count (1-32)\r\n");
            return -1;
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        if (LAN9370_SPI_ReadReg8((uint16_t)(addr + i), &val) != LAN9370_SPI_OK) {
            printf("SPI read failed at 0x%04lX\r\n", addr + i);
            return -1;
        }
        printf("SPI[0x%04lX]=0x%02X\r\n", addr + i, val);
    }

    return 0;
}

int cmd_spiwrite(int argc, char *argv[])
{
    uint32_t addr;
    uint32_t value;

    if (argc < 3 || parse_u32(argv[1], &addr) != 0 || parse_u32(argv[2], &value) != 0 || addr > 0xFFFF || value > 0xFF) {
        printf("usage: spiwrite <addr> <value>\r\n");
        return -1;
    }

    if (LAN9370_SPI_WriteReg8((uint16_t)addr, (uint8_t)value) != LAN9370_SPI_OK) {
        printf("SPI write failed\r\n");
        return -1;
    }

    printf("SPI[0x%04lX] <= 0x%02lX\r\n", addr, value);
    return 0;
}

int cmd_smiread(int argc, char *argv[])
{
    uint32_t phy;
    uint32_t reg;
    uint16_t value;

    if (argc < 3 || parse_u32(argv[1], &phy) != 0 || parse_u32(argv[2], &reg) != 0 || phy > 31 || reg > 31) {
        printf("usage: smiread <phy 0-31> <reg 0-31>\r\n");
        return -1;
    }

    if (MDIO_BitBang_Init() != MDIO_BITBANG_OK) {
        printf("SMI init failed\r\n");
        return -1;
    }

    if (MDIO_BitBang_Read((uint8_t)phy, (uint8_t)reg, &value) != MDIO_BITBANG_OK) {
        printf("SMI read failed\r\n");
        return -1;
    }

    printf("SMI[%lu][0x%02lX]=0x%04X\r\n", phy, reg, value);
    return 0;
}

int cmd_smiwrite(int argc, char *argv[])
{
    uint32_t phy;
    uint32_t reg;
    uint32_t value;

    if (argc < 4 || parse_u32(argv[1], &phy) != 0 || parse_u32(argv[2], &reg) != 0 || parse_u32(argv[3], &value) != 0 || phy > 31 || reg > 31 || value > 0xFFFF) {
        printf("usage: smiwrite <phy 0-31> <reg 0-31> <value>\r\n");
        return -1;
    }

    if (MDIO_BitBang_Init() != MDIO_BITBANG_OK) {
        printf("SMI init failed\r\n");
        return -1;
    }

    if (MDIO_BitBang_Write((uint8_t)phy, (uint8_t)reg, (uint16_t)value) != MDIO_BITBANG_OK) {
        printf("SMI write failed\r\n");
        return -1;
    }

    printf("SMI[%lu][0x%02lX] <= 0x%04lX\r\n", phy, reg, value);
    return 0;
}

int cmd_diagbus(int argc, char *argv[])
{
    uint8_t id[4] = {0};
    uint8_t gctrl = 0;
    uint8_t swrst = 0;
    uint16_t smi_id1 = 0;
    uint16_t smi_id2 = 0;
    int spi_ok = 0;
    int smi_ok = 0;

    (void)argc;
    (void)argv;

    printf("diagbus: reset -> probe SPI/SMI\r\n");
    LAN9370_HardwareReset();

    if (MDIO_BitBang_Init() != MDIO_BITBANG_OK) {
        printf("SMI init failed\r\n");
    }

    for (int i = 0; i < 5; i++) {
        HAL_Delay((uint32_t)(10 + (i * 40)));
        LAN9370_SPI_ReadReg8(REG_CHIP_ID0, &id[0]);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID1, &id[1]);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID2, &id[2]);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID3, &id[3]);
        printf("SPI ID sample[%d]: %02X %02X %02X %02X\r\n", i, id[0], id[1], id[2], id[3]);
    }

    if (LAN9370_SPI_ReadReg8(REG_GLOBAL_CTRL_0, &gctrl) == LAN9370_SPI_OK &&
        LAN9370_SPI_ReadReg8(REG_SW_RESET, &swrst) == LAN9370_SPI_OK) {
        printf("SPI REG global_ctrl0=0x%02X sw_reset=0x%02X\r\n", gctrl, swrst);
        if (!((id[0] == 0x00 && id[1] == 0x00 && id[2] == 0x00 && id[3] == 0x00) ||
              (id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF && id[3] == 0xFF))) {
            spi_ok = 1;
        }
    }

    if (MDIO_BitBang_Read(LAN8720_ACTIVE_PHY_ADDR, MII_PHYSID1, &smi_id1) == MDIO_BITBANG_OK &&
        MDIO_BitBang_Read(LAN8720_ACTIVE_PHY_ADDR, MII_PHYSID2, &smi_id2) == MDIO_BITBANG_OK) {
        printf("SMI PHY%u ID1=0x%04X ID2=0x%04X\r\n",
               (unsigned int)LAN8720_ACTIVE_PHY_ADDR, smi_id1, smi_id2);
        if (!((smi_id1 == 0x0000 && smi_id2 == 0x0000) ||
              (smi_id1 == 0xFFFF && smi_id2 == 0xFFFF))) {
            smi_ok = 1;
        }
    }

    printf("diagbus result: SPI_OK=%d SMI_OK=%d\r\n", spi_ok, smi_ok);
    return (spi_ok && smi_ok) ? 0 : -1;
}

int cmd_rstprobe(int argc, char *argv[])
{
    long rounds = 6;
    uint8_t id0 = 0;
    uint8_t id1 = 0;
    uint8_t id2 = 0;
    uint8_t id3 = 0;

    if (argc >= 2) {
        rounds = strtol(argv[1], NULL, 0);
    }

    if (rounds < 1 || rounds > 20) {
        printf("usage: rstprobe [1-20]\r\n");
        return -1;
    }

    for (long i = 0; i < rounds; i++) {
        LAN9370_HardwareReset();
        HAL_Delay(5);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID0, &id0);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID1, &id1);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID2, &id2);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID3, &id3);
        printf("rstprobe[%ld]-5ms: %02X %02X %02X %02X\r\n", i, id0, id1, id2, id3);

        HAL_Delay(45);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID0, &id0);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID1, &id1);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID2, &id2);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID3, &id3);
        printf("rstprobe[%ld]-50ms: %02X %02X %02X %02X\r\n", i, id0, id1, id2, id3);

        HAL_Delay(50);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID0, &id0);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID1, &id1);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID2, &id2);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID3, &id3);
        printf("rstprobe[%ld]-100ms: %02X %02X %02X %02X\r\n", i, id0, id1, id2, id3);
    }

    return 0;
}

int cmd_spiprobe(int argc, char *argv[])
{
    uint8_t id0;
    uint8_t id1;
    uint8_t id2;
    uint8_t id3;
    uint32_t cpol;
    uint32_t cpha;

    (void)argc;
    (void)argv;

    for (int mode = 0; mode < 4; mode++) {
        cpol = (mode >= 2) ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW;
        cpha = ((mode == 1) || (mode == 3)) ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;

        hspi1.Init.CLKPolarity = cpol;
        hspi1.Init.CLKPhase = cpha;
        HAL_SPI_DeInit(&hspi1);
        if (HAL_SPI_Init(&hspi1) != HAL_OK) {
            printf("spiprobe mode%d: HAL_SPI_Init failed\r\n", mode);
            continue;
        }

        HAL_Delay(2);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID0, &id0);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID1, &id1);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID2, &id2);
        LAN9370_SPI_ReadReg8(REG_CHIP_ID3, &id3);

        printf("spiprobe mode%d: id=%02X %02X %02X %02X\r\n", mode, id0, id1, id2, id3);
    }

    hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
    hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
    HAL_SPI_DeInit(&hspi1);
    HAL_SPI_Init(&hspi1);

    return 0;
}

int cmd_spispeed(int argc, char *argv[])
{
    long div;
    uint32_t prescaler;

    if (argc < 2) {
        printf("usage: spispeed <2|4|8|16|32|64|128|256>\r\n");
        return -1;
    }

    div = strtol(argv[1], NULL, 0);
    switch (div) {
        case 2: prescaler = SPI_BAUDRATEPRESCALER_2; break;
        case 4: prescaler = SPI_BAUDRATEPRESCALER_4; break;
        case 8: prescaler = SPI_BAUDRATEPRESCALER_8; break;
        case 16: prescaler = SPI_BAUDRATEPRESCALER_16; break;
        case 32: prescaler = SPI_BAUDRATEPRESCALER_32; break;
        case 64: prescaler = SPI_BAUDRATEPRESCALER_64; break;
        case 128: prescaler = SPI_BAUDRATEPRESCALER_128; break;
        case 256: prescaler = SPI_BAUDRATEPRESCALER_256; break;
        default:
            printf("invalid divider\r\n");
            return -1;
    }

    hspi1.Init.BaudRatePrescaler = prescaler;
    HAL_SPI_DeInit(&hspi1);
    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        printf("spispeed apply failed\r\n");
        return -1;
    }

    printf("spispeed set to /%ld\r\n", div);
    return 0;
}

int Shell_Init(UART_HandleTypeDef *huart)
{
    if (huart == NULL) {
        return -1;
    }

    pHuart = huart;
    gShell.write = userShellWrite;
    gShell.read = userShellRead;
    shellInit(&gShell, gShellBuffer, sizeof(gShellBuffer));

    gRxHead = 0;
    gRxTail = 0;
    HAL_UART_Receive_IT(pHuart, &gRxByte, 1);

    Shell_PrintBanner();

    return 0;
}

void Shell_Process(void)
{
    uint8_t ch;

    if (pHuart == NULL) {
        return;
    }

    while (Shell_RxPop(&ch)) {
        shellHandler(&gShell, (char)ch);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (pHuart != NULL && huart == pHuart) {
        Shell_RxPush(gRxByte);
        HAL_UART_Receive_IT(pHuart, &gRxByte, 1);
    }
}

void Shell_PrintBanner(void)
{
    printf("\r\nLAN9370 shell ready. type 'help'\r\n");
}

int cmd_sysreset(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    printf("MCU software reset...\r\n");
    HAL_Delay(50);
    NVIC_SystemReset();
    return 0;
}

/* =============================================================================
 * Direct MDIO Scan Command (MCU PB6/PB5)
 * ===========================================================================*/

int cmd_mdioscan(int argc, char *argv[])
{
    uint32_t from = 0;
    uint32_t to = 31;

    if (argc >= 2 && parse_u32(argv[1], &from) != 0) {
        printf("usage: mdioscan [from] [to]\r\n");
        return -1;
    }
    if (argc >= 3 && parse_u32(argv[2], &to) != 0) {
        printf("usage: mdioscan [from] [to]\r\n");
        return -1;
    }
    if (from > 31 || to > 31 || from > to) {
        printf("invalid range\r\n");
        return -1;
    }

    if (MDIO_BitBang_Init() != MDIO_BITBANG_OK) {
        printf("SMI init failed\r\n");
        return -1;
    }

    printf("MDIO scan addr %lu..%lu\r\n", from, to);
    for (uint32_t phy = from; phy <= to; phy++) {
        uint16_t id1 = 0;
        uint16_t id2 = 0;
        if (MDIO_BitBang_Read((uint8_t)phy, MII_PHYSID1, &id1) == MDIO_BITBANG_OK &&
            MDIO_BitBang_Read((uint8_t)phy, MII_PHYSID2, &id2) == MDIO_BITBANG_OK &&
            id1 != 0x0000 && id1 != 0xFFFF &&
            id2 != 0x0000 && id2 != 0xFFFF) {
            printf("  PHY[%2lu]: ID=0x%04X%04X\r\n", phy, id1, id2);
        }
    }
    return 0;
}

/* =============================================================================
 * LAN8720 Shell Command
 * ===========================================================================*/

int cmd_lan8720(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* Auto-init if not yet initialized */
    {
        LAN8720_Status_t st;
        if (LAN8720_GetStatus(&st) != LAN8720_OK) {
            printf("[LAN8720] Not initialized, trying LAN8720_Init()...\r\n");
            LAN8720_Init();
        }
    }

    printf("=== LAN8720 External PHY ===\r\n");
    LAN8720_PrintStatus();
    return 0;
}

#ifdef __GNUC__
int _write(int file, char *ptr, int len)
{
    (void)file;
    if (pHuart != NULL) {
        HAL_UART_Transmit(pHuart, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    }
    return len;
}
#endif

#ifdef __ICCARM__
#include <yfuns.h>
size_t __write(int handle, const unsigned char *buffer, size_t size)
{
    (void)handle;
    if (pHuart != NULL && buffer != NULL) {
        HAL_UART_Transmit(pHuart, (uint8_t *)buffer, size, HAL_MAX_DELAY);
    }
    return size;
}
#endif

#ifdef __CC_ARM
int fputc(int ch, FILE *f)
{
    (void)f;
    if (pHuart != NULL) {
        HAL_UART_Transmit(pHuart, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    }
    return ch;
}
#endif
