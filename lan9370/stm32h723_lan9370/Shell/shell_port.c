/**
  ******************************************************************************
  * @file    shell_port.c
  * @brief   Letter-shell port for STM32H723 + LAN9370 diagnostics
  ******************************************************************************
  */

#include "shell_port.h"
#include "shell.h"
#include "lan9370_driver.h"
#include "lan9370_persist.h"
#include "lan9370_spi.h"
#include "lan9370_reg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static UART_HandleTypeDef *pHuart = NULL;
static Shell gShell;
static char gShellBuffer[512];

/* ---- UART RX ring buffer (MUST be in DMA-accessible SRAM, not DTCM) ---- */
#define SHELL_RX_RING_SIZE 512
static volatile uint16_t gRxHead = 0;
static volatile uint16_t gRxTail = 0;
/* Placed in D2 SRAM (.lwip_sec) — BDMA can access this */
static uint8_t gRxRing[SHELL_RX_RING_SIZE] __attribute__((section(".lwip_sec")));

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
    if (ch == NULL || gRxHead == gRxTail) return 0;
    *ch = gRxRing[gRxTail];
    gRxTail = (uint16_t)((gRxTail + 1U) % SHELL_RX_RING_SIZE);
    return 1;
}

/* ---- Letter-shell write/read callbacks ---- */
static short userShellWrite(char *data, unsigned short len)
{
    if (pHuart == NULL || data == NULL || len == 0) return 0;
    if (HAL_UART_Transmit(pHuart, (uint8_t *)data, len, HAL_MAX_DELAY) != HAL_OK) return 0;
    return len;
}

/* ---- Redirect printf to LPUART1 (overrides weak __io_putchar) ---- */
int __io_putchar(int ch)
{
    uint8_t byte = (uint8_t)ch;
    if (pHuart != NULL) {
        HAL_UART_Transmit(pHuart, &byte, 1, HAL_MAX_DELAY);
    }
    return ch;
}

static short userShellRead(char *data, unsigned short len)
{
    if (pHuart == NULL || data == NULL || len == 0) return 0;
    for (unsigned short i = 0; i < len; i++) {
        uint8_t ch;
        if (!Shell_RxPop(&ch)) return i;
        data[i] = (char)ch;
    }
    return len;
}

/* ---- UART Rx callback (called from interrupt) ---- */
static uint8_t gShellRxByte;  /* must be static - HAL stores pointer to it */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == LPUART1) {
        Shell_RxPush(gShellRxByte);
        /* Re-arm single-byte interrupt receive */
        HAL_UART_Receive_IT(huart, &gShellRxByte, 1);
    }
}

/* ---- Helper parsers ---- */
static int parse_port(const char *arg, int min, int max, int *port)
{
    char *end = NULL;
    long v = strtol(arg, &end, 0);
    if (arg == NULL || port == NULL || *arg == '\0' || (end && *end != '\0')) return -1;
    if (v < min || v > max) return -1;
    *port = (int)v;
    return 0;
}

static int parse_u32(const char *arg, uint32_t *value)
{
    char *end = NULL;
    unsigned long v = strtoul(arg, &end, 0);
    if (arg == NULL || value == NULL || *arg == '\0' || (end && *end != '\0')) return -1;
    *value = (uint32_t)v;
    return 0;
}

/* ---- Shell init ---- */
int Shell_Init(UART_HandleTypeDef *huart)
{
    pHuart = huart;
    gRxHead = gRxTail = 0;

    /* Unlink DMA — shell uses interrupt-based RX, not DMA */
    huart->hdmarx = NULL;
    huart->hdmatx = NULL;
    huart->RxState = HAL_UART_STATE_READY;  /* Required by HAL_UART_Receive_IT */

    gShell.write = userShellWrite;
    gShell.read  = userShellRead;
    shellInit(&gShell, gShellBuffer, sizeof(gShellBuffer));

    /* Start UART RX in interrupt mode (NVIC enabled in HAL_UART_MspInit).
     * Each byte triggers LPUART1_IRQHandler → HAL_UART_RxCpltCallback
     * → Shell_RxPush → re-arm for next byte. */
    HAL_UART_Receive_IT(huart, &gShellRxByte, 1);

    return 0;
}

void Shell_Process(void)
{
    /* Drain ring buffer filled by interrupt callback */
    uint8_t ch;
    while (Shell_RxPop(&ch)) {
        shellHandler(&gShell, ch);
    }
}

void Shell_PrintBanner(void)
{
    printf("\r\n=== STM32H723 + LAN9370 Shell ===\r\n");
    printf("Type 'help' for command list\r\n\r\n");
}

/* ================================================================
 *  Shell Commands
 * ================================================================ */

int cmd_info(int argc, char *argv[])
{
    (void)argc; (void)argv;
    LAN9370_ChipInfo_t info;
    if (LAN9370_GetChipInfo(&info) != LAN9370_OK) {
        printf("read chip info failed\r\n"); return -1;
    }
    printf("Chip: %s  ID: 0x%08lX  Rev: %d\r\n", info.chipName, (unsigned long)info.chipId, info.revisionId);
    LAN9370_PrintAllPortsStatus();
    LAN9370_PrintVlanConfig();
    return 0;
}

int cmd_dump(int argc, char *argv[])
{
    (void)argc; (void)argv;
    return (LAN9370_DumpRegisters() == LAN9370_OK) ? 0 : -1;
}

int cmd_reset(int argc, char *argv[])
{
    (void)argc; (void)argv;
    printf("Resetting LAN9370 chip (hardware nRST)...\r\n");
    return (LAN9370_HardwareReset() == LAN9370_OK) ? 0 : -1;
}

int cmd_chipreset(int argc, char *argv[])
{
    (void)argc; (void)argv;
    printf("Resetting LAN9370 chip (hardware nRST)...\r\n");
    return (LAN9370_HardwareReset() == LAN9370_OK) ? 0 : -1;
}

int cmd_mcu_reset(int argc, char *argv[])
{
    (void)argc; (void)argv;
    printf("MCU software reset in 500ms...\r\n");
    HAL_Delay(500);
    NVIC_SystemReset();
    return 0;
}

int cmd_uptime(int argc, char *argv[])
{
    (void)argc; (void)argv;
    uint32_t tick = HAL_GetTick();
    uint32_t sec  = tick / 1000U;
    uint32_t min  = sec / 60U;
    uint32_t hour = min / 60U;
    uint32_t day  = hour / 24U;

    printf("Uptime: %lu ms  (%lu days %lu:%02lu:%02lu.%03lu)\r\n",
           (unsigned long)tick,
           (unsigned long)day,
           (unsigned long)(hour % 24U),
           (unsigned long)(min % 60U),
           (unsigned long)(sec % 60U),
           (unsigned long)(tick % 1000U));
    return 0;
}

int cmd_port(int argc, char *argv[])
{
    int p;
    if (argc < 2 || parse_port(argv[1], 1, 5, &p)) { printf("usage: port <1-5>\r\n"); return -1; }
    return (LAN9370_PrintPortStatus((LAN9370_Port_t)p) == LAN9370_OK) ? 0 : -1;
}

int cmd_master(int argc, char *argv[])
{
    int p;
    if (argc < 2 || parse_port(argv[1], 1, 4, &p)) { printf("usage: master <1-4>\r\n"); return -1; }
    return (LAN9370_SetT1MasterSlave((LAN9370_Port_t)p, LAN9370_T1_MASTER) == LAN9370_OK) ? 0 : -1;
}

int cmd_slave(int argc, char *argv[])
{
    int p;
    if (argc < 2 || parse_port(argv[1], 1, 4, &p)) { printf("usage: slave <1-4>\r\n"); return -1; }
    return (LAN9370_SetT1MasterSlave((LAN9370_Port_t)p, LAN9370_T1_SLAVE) == LAN9370_OK) ? 0 : -1;
}

int cmd_enable(int argc, char *argv[])
{
    int p;
    if (argc < 2 || parse_port(argv[1], 1, 5, &p)) { printf("usage: enable <1-5>\r\n"); return -1; }
    return (LAN9370_SetPortEnable((LAN9370_Port_t)p, true) == LAN9370_OK) ? 0 : -1;
}

int cmd_disable(int argc, char *argv[])
{
    int p;
    if (argc < 2 || parse_port(argv[1], 1, 5, &p)) { printf("usage: disable <1-5>\r\n"); return -1; }
    return (LAN9370_SetPortEnable((LAN9370_Port_t)p, false) == LAN9370_OK) ? 0 : -1;
}

int cmd_phyread(int argc, char *argv[])
{
    int p; uint16_t v;
    if (argc < 3 || parse_port(argv[1], 1, 4, &p)) { printf("usage: phyread <port> <reg>\r\n"); return -1; }
    uint32_t reg;
    if (parse_u32(argv[2], &reg) || reg > 31) { printf("reg 0-31\r\n"); return -1; }
    if (LAN9370_PHY_ReadReg((LAN9370_Port_t)p, (uint8_t)reg, &v) != LAN9370_OK) {
        printf("phyread failed\r\n"); return -1;
    }
    printf("PHY Port%d Reg%u = 0x%04X\r\n", p, (unsigned)reg, v);
    return 0;
}

int cmd_phywrite(int argc, char *argv[])
{
    int p; uint32_t reg, val;
    if (argc < 4 || parse_port(argv[1], 1, 4, &p)) { printf("usage: phywrite <port> <reg> <val>\r\n"); return -1; }
    if (parse_u32(argv[2], &reg) || reg > 31) { printf("reg 0-31\r\n"); return -1; }
    if (parse_u32(argv[3], &val) || val > 0xFFFF) { printf("val 0-0xFFFF\r\n"); return -1; }
    return (LAN9370_PHY_WriteReg((LAN9370_Port_t)p, (uint8_t)reg, (uint16_t)val) == LAN9370_OK) ? 0 : -1;
}

int cmd_spiread(int argc, char *argv[])
{
    if (argc < 2) { printf("usage: spiread <addr>\r\n"); return -1; }
    uint32_t addr; uint8_t v;
    if (parse_u32(argv[1], &addr) || addr > 0xFFFF) { printf("addr 0-0xFFFF\r\n"); return -1; }
    if (LAN9370_SPI_ReadReg8((uint16_t)addr, &v) != LAN9370_SPI_OK) { printf("read fail\r\n"); return -1; }
    printf("[0x%04lX] = 0x%02X\r\n", (unsigned long)addr, v);
    return 0;
}

int cmd_spiwrite(int argc, char *argv[])
{
    if (argc < 3) { printf("usage: spiwrite <addr> <val>\r\n"); return -1; }
    uint32_t addr, val;
    if (parse_u32(argv[1], &addr) || addr > 0xFFFF) { printf("addr 0-0xFFFF\r\n"); return -1; }
    if (parse_u32(argv[2], &val) || val > 0xFF) { printf("val 0-0xFF\r\n"); return -1; }
    return (LAN9370_SPI_WriteReg8((uint16_t)addr, (uint8_t)val) == LAN9370_SPI_OK) ? 0 : -1;
}

int cmd_mib(int argc, char *argv[])
{
    int p = 5;
    if (argc >= 2) { if (parse_port(argv[1], 1, 5, &p)) { printf("usage: mib [port 1-5]\r\n"); return -1; } }
    uint32_t rx=0, tx=0, rxd=0, txd=0;
    LAN9370_ReadPortMibCounter((LAN9370_Port_t)p, LAN9370_MIB_RX_TOTAL_IDX, &rx);
    LAN9370_ReadPortMibCounter((LAN9370_Port_t)p, LAN9370_MIB_TX_TOTAL_IDX, &tx);
    LAN9370_ReadPortMibCounter((LAN9370_Port_t)p, LAN9370_MIB_RX_DROP_IDX, &rxd);
    LAN9370_ReadPortMibCounter((LAN9370_Port_t)p, LAN9370_MIB_TX_DROP_IDX, &txd);
    printf("Port%d MIB: rx=%lu tx=%lu rx_drop=%lu tx_drop=%lu\r\n", p,
           (unsigned long)rx, (unsigned long)tx, (unsigned long)rxd, (unsigned long)txd);
    return 0;
}

/* ---------- LED control ---------- */
int cmd_led(int argc, char *argv[])
{
    uint8_t ctrl;
    if (LAN9370_SPI_ReadReg8(REG_LED_CTRL0, &ctrl) != LAN9370_SPI_OK) {
        printf("read LED reg failed\r\n"); return -1;
    }
    printf("LED_CTRL0 = 0x%02X\r\n", ctrl);

    if (argc >= 3) {
        uint32_t mask, val;
        if (parse_u32(argv[1], &mask) || mask > 0xFF) { printf("mask 0-0xFF\r\n"); return -1; }
        if (parse_u32(argv[2], &val)  || val  > 0xFF) { printf("val 0-0xFF\r\n"); return -1; }
        ctrl = (uint8_t)((ctrl & (uint8_t)(~mask)) | ((uint8_t)val & (uint8_t)mask));
        LAN9370_SPI_WriteReg8(REG_LED_CTRL0, ctrl);
        printf("LED_CTRL0 = 0x%02X\r\n", ctrl);
    } else {
        printf("LEDs: default=activity blink. usage: led [mask val]\r\n");
        printf("  REG_LED_BLINK_CTRL (0x%04X) controls blink rate\r\n", REG_LED_BLINK_CTRL);
    }
    return 0;
}

/* ---------- Temperature ---------- */
int cmd_temp(int argc, char *argv[])
{
    (void)argc; (void)argv;
    uint16_t raw;
    if (LAN9370_SPI_ReadReg16(REG_TEMP_MON, &raw) != LAN9370_SPI_OK) {
        printf("temp read failed (try different address)\r\n");
        /* Try alternate addresses */
        for (uint16_t a = 0x0E10; a <= 0x0E20; a += 2) {
            uint16_t v;
            if (LAN9370_SPI_ReadReg16(a, &v) == LAN9370_SPI_OK && v != 0 && v != 0xFFFF) {
                printf("  0x%04X = 0x%04X\r\n", a, v);
            }
        }
        return -1;
    }
    /* Typical formula: raw is ADC count, convert to temperature */
    int temp = (int)raw;
    printf("TEMP_MON raw=0x%04X (%d)\r\n", raw, temp);
    return 0;
}

/* ---------- T1 PHY Diagnostics (per-port) ---------- */
int cmd_sqi(int argc, char *argv[])
{
    int p = 2;
    if (argc >= 2) { if (parse_port(argv[1], 1, 4, &p)) { printf("usage: sqi [port 1-4]\r\n"); return -1; } }
    uint16_t sqi;
    if (LAN9370_PHY_ReadReg((LAN9370_Port_t)p, T1_PHY_SQI, &sqi) != LAN9370_OK) {
        printf("SQI read failed for port %d\r\n", p); return -1;
    }
    printf("Port%d SQI = 0x%04X (higher=better, 0x07=max)\r\n", p, sqi);
    return 0;
}

int cmd_mse(int argc, char *argv[])
{
    int p = 2;
    if (argc >= 2) { if (parse_port(argv[1], 1, 4, &p)) { printf("usage: mse [port 1-4]\r\n"); return -1; } }
    uint16_t mse;
    if (LAN9370_PHY_ReadReg((LAN9370_Port_t)p, T1_PHY_MSE, &mse) != LAN9370_OK) {
        printf("MSE read failed for port %d\r\n", p); return -1;
    }
    printf("Port%d MSE = 0x%04X (lower=better)\r\n", p, mse);
    return 0;
}

int cmd_linkinfo(int argc, char *argv[])
{
    int p = 2;
    if (argc >= 2) { if (parse_port(argv[1], 1, 4, &p)) { printf("usage: linkinfo [port 1-4]\r\n"); return -1; } }

    uint16_t status, ms_stat, sqi, mse;
    LAN9370_PHY_ReadReg((LAN9370_Port_t)p, T1_PHY_BASIC_STATUS, &status);
    LAN9370_PHY_ReadReg((LAN9370_Port_t)p, T1_PHY_MASTER_SLAVE_STATUS, &ms_stat);
    LAN9370_PHY_ReadReg((LAN9370_Port_t)p, T1_PHY_SQI, &sqi);
    LAN9370_PHY_ReadReg((LAN9370_Port_t)p, T1_PHY_MSE, &mse);

    printf("Port%d Link Info:\r\n", p);
    printf("  Link:  %s\r\n", (status & T1_PHY_LINK_STATUS) ? "UP" : "DOWN");
    printf("  MS:    %s cfg_fault=%d resolved=%d\r\n",
           (ms_stat & T1_PHY_MS_CFG_RESOLUTION) ? "Resolved" : "Unresolved",
           (ms_stat & T1_PHY_MS_CFG_FAULT) ? 1 : 0,
           (ms_stat & T1_PHY_MS_CFG_RESOLUTION) ? 1 : 0);
    printf("  Local Rx:  %s\r\n", (ms_stat & T1_PHY_LOCAL_RCVR_STATUS) ? "OK" : "FAIL");
    printf("  Remote Rx: %s\r\n", (ms_stat & T1_PHY_REMOTE_RCVR_STATUS) ? "OK" : "FAIL");
    printf("  SQI:   0x%04X\r\n", sqi);
    printf("  MSE:   0x%04X\r\n", mse);
    return 0;
}

int cmd_cable(int argc, char *argv[])
{
    int p = 2;
    if (argc >= 2) { if (parse_port(argv[1], 1, 4, &p)) { printf("usage: cable [port 1-4]\r\n"); return -1; } }
    uint16_t diag;
    if (LAN9370_PHY_ReadReg((LAN9370_Port_t)p, T1_PHY_CABLE_DIAG, &diag) != LAN9370_OK) {
        printf("Cable diag read failed (reg 0x%02X not implemented)\r\n", T1_PHY_CABLE_DIAG);
        return -1;
    }
    printf("Port%d Cable Diag = 0x%04X\r\n", p, diag);
    if (diag & (1 << 0)) printf("  Short detected\r\n");
    if (diag & (1 << 1)) printf("  Open detected\r\n");
    if (diag == 0) printf("  No fault detected\r\n");
    return 0;
}

/* ---------- UART passthrough for SMI ---------- */
int cmd_smiread(int argc, char *argv[])
{
    if (argc < 3) { printf("usage: smiread <phy_addr> <reg>\r\n"); return -1; }
    uint32_t pa, ra; uint16_t v;
    if (parse_u32(argv[1], &pa) || pa > 31) { printf("phy_addr 0-31\r\n"); return -1; }
    if (parse_u32(argv[2], &ra) || ra > 31) { printf("reg 0-31\r\n"); return -1; }
    if (LAN9370_SMI_Read((uint8_t)pa, (uint8_t)ra, &v) != LAN9370_SMI_OK) {
        printf("SMI read fail\r\n"); return -1;
    }
    printf("SMI PHY%u Reg%u = 0x%04X\r\n", (unsigned)pa, (unsigned)ra, v);
    return 0;
}

/* ---------- MCU info ---------- */
int cmd_mcu(int argc, char *argv[])
{
    (void)argc; (void)argv;

    printf("\r\n=== MCU Info ===\r\n");
    printf("Chip:    STM32H723ZG (Cortex-M7)\r\n");
    printf("Build:   " __DATE__ " " __TIME__ "\r\n");

    /* System clock */
    uint32_t hclk = HAL_RCC_GetHCLKFreq();
    uint32_t sysclk = HAL_RCC_GetSysClockFreq();
    printf("SYSCLK:  %lu MHz\r\n", (unsigned long)(sysclk / 1000000U));
    printf("HCLK:    %lu MHz\r\n", (unsigned long)(hclk / 1000000U));

    /* Reset flags */
    uint32_t rsr = RCC->RSR;
    printf("RST:    ");
    if (rsr & RCC_RSR_BORRSTF)   printf(" BOR");
    if (rsr & RCC_RSR_PINRSTF)   printf(" PIN");
    if (rsr & RCC_RSR_PORRSTF)   printf(" POR");
    if (rsr & RCC_RSR_SFTRSTF)   printf(" SW");
    if (rsr & RCC_RSR_IWDG1RSTF) printf(" IWDG");
    if (rsr & RCC_RSR_WWDG1RSTF) printf(" WWDG");
    if (rsr & RCC_RSR_LPWRRSTF)  printf(" LP");
    printf("\r\n");

    /* Unique ID (96-bit) */
    uint32_t uid0 = *(uint32_t *)0x1FF1E800U;
    uint32_t uid1 = *(uint32_t *)0x1FF1E804U;
    uint32_t uid2 = *(uint32_t *)0x1FF1E808U;
    printf("UID:     %08lX-%08lX-%08lX\r\n",
           (unsigned long)uid0, (unsigned long)uid1, (unsigned long)uid2);

    /* Flash size */
    uint16_t flashSize = *(uint16_t *)0x1FF1E880U;
    printf("Flash:   %u KB\r\n", (unsigned)flashSize);

    printf("=============================\r\n\r\n");
    return 0;
}

/* ---- Config persist helpers ---- */
static int capture_current_settings(LAN9370_PersistSettings_t *settings)
{
    if (settings == NULL) return -1;
    memset(settings, 0, sizeof(*settings));

    for (int port = 1; port <= 4; port++) {
        LAN9370_T1_Mode_t mode;
        if (LAN9370_GetT1MasterSlave((LAN9370_Port_t)port, &mode) != LAN9370_OK) return -1;
        settings->t1Mode[port - 1] = (uint8_t)((mode == LAN9370_T1_MASTER) ? 1U : 0U);
    }

    for (int port = 1; port <= 5; port++) {
        LAN9370_Port_t dst = 0;
        uint16_t vid = 1;
        if (LAN9370_GetPortMirror((LAN9370_Port_t)port, &dst, NULL) != LAN9370_OK) return -1;
        if (LAN9370_GetPortDefaultVlan((LAN9370_Port_t)port, &vid) != LAN9370_OK) return -1;
        settings->mirrorDst[port - 1] = (uint8_t)dst;
        settings->vlanVid[port - 1] = vid;
    }

    bool vlanEnabled, ptpEnabled, gptpEnabled;
    if (LAN9370_GetVlanEnable(&vlanEnabled) != LAN9370_OK) return -1;
    if (LAN9370_GetPtpEnable(&ptpEnabled) != LAN9370_OK) return -1;
    if (LAN9370_GetGptpEnable(&gptpEnabled) != LAN9370_OK) return -1;
    settings->vlanEnable  = (uint8_t)(vlanEnabled ? 1U : 0U);
    settings->ptpEnable   = (uint8_t)(ptpEnabled ? 1U : 0U);
    settings->gptpEnable  = (uint8_t)(gptpEnabled ? 1U : 0U);
    return 0;
}

static int apply_settings(const LAN9370_PersistSettings_t *settings)
{
    if (settings == NULL) return -1;

    for (int port = 1; port <= 4; port++) {
        LAN9370_T1_Mode_t mode = (settings->t1Mode[port - 1] != 0U) ? LAN9370_T1_MASTER : LAN9370_T1_SLAVE;
        if (LAN9370_SetT1MasterSlave((LAN9370_Port_t)port, mode) != LAN9370_OK) return -1;
    }
    if (LAN9370_SetVlanEnable(settings->vlanEnable != 0U) != LAN9370_OK) return -1;

    for (int port = 1; port <= 5; port++) {
        uint16_t vid = settings->vlanVid[port - 1];
        uint8_t dst = settings->mirrorDst[port - 1];
        if (vid >= 1U && vid <= 4094U)
            if (LAN9370_SetPortDefaultVlan((LAN9370_Port_t)port, vid) != LAN9370_OK) return -1;
        if (dst <= 5U)
            if (LAN9370_SetPortMirror((LAN9370_Port_t)port, (LAN9370_Port_t)dst, false) != LAN9370_OK) return -1;
    }
    if (LAN9370_SetPtpEnable(settings->ptpEnable != 0U) != LAN9370_OK) return -1;
    if (LAN9370_SetGptpEnable(settings->gptpEnable != 0U) != LAN9370_OK) return -1;
    return 0;
}

/* ---- VLAN ---- */
int cmd_vlan(int argc, char *argv[])
{
    int port;
    long vid;

    if (argc < 2) { printf("usage: vlan on|off|set <port> <vid>|show|add <vid> <mask>\r\n"); return -1; }
    if (strcmp(argv[1], "on") == 0)
        return (LAN9370_SetVlanEnable(true) == LAN9370_OK) ? 0 : -1;
    if (strcmp(argv[1], "off") == 0)
        return (LAN9370_SetVlanEnable(false) == LAN9370_OK) ? 0 : -1;
    if (strcmp(argv[1], "show") == 0)
        return (LAN9370_PrintVlanConfig() == LAN9370_OK) ? 0 : -1;
    if (strcmp(argv[1], "set") != 0 || argc < 4 || parse_port(argv[2], 1, 5, &port)) {
        printf("usage: vlan set <1-5> <1-4094>\r\n"); return -1;
    }
    vid = strtol(argv[3], NULL, 0);
    if (vid < 1 || vid > 4094) { printf("invalid vid\r\n"); return -1; }
    return (LAN9370_SetPortDefaultVlan((LAN9370_Port_t)port, (uint16_t)vid) == LAN9370_OK) ? 0 : -1;
}

/* ---- VLAN add (program VLAN table entry) ---- */
int cmd_vlan_add(int argc, char *argv[])
{
    uint32_t vid, mask;
    if (argc < 3 || parse_u32(argv[1], &vid) || vid < 1 || vid > 4094) {
        printf("usage: vlanadd <vid> <memberMask>\r\n");
        printf("  memberMask: bit0=Port1 bit1=Port2 bit2=Port3 bit3=Port4 bit4=Port5\r\n");
        printf("  Example: vlanadd 100 0x1A  → Port2+4+5 in VLAN 100 (0x1A=26)\r\n");
        printf("  NOTE: LAN9370 VLAN table may differ from KSZ9477; use 'portgroup' for reliable isolation\r\n");
        return -1;
    }
    if (parse_u32(argv[2], &mask) || mask > 0x1F) { printf("mask 0-0x1F\r\n"); return -1; }
    return (LAN9370_WriteVlanEntry((uint16_t)vid, (uint8_t)mask) == LAN9370_OK) ? 0 : -1;
}

/* ---- Port group isolation (membership-based, works reliably on LAN9370) ---- */
int cmd_portgroup(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: portgroup <port> <memberMask>\r\n");
        printf("  Sets which ports a given port can forward TO\r\n");
        printf("  memberMask: bit0=Port1 bit1=Port2 bit2=Port3 bit3=Port4 bit4=Port5\r\n");
        printf("  Examples:\r\n");
        printf("    portgroup 2 0x18  -> Port2 can only forward to Port4+5 (0x18=24)\r\n");
        printf("    portgroup 4 0x12  -> Port4 can only forward to Port2+5 (0x12=18)\r\n");
        printf("    portgroup 5 0x1F  -> Port5 can forward to all ports (host)\r\n");
        printf("    portgroup 2 0x1F  -> restore Port2 to full forwarding\r\n");
        return -1;
    }

    int port;
    uint32_t mask;
    if (parse_port(argv[1], 1, 5, &port)) { printf("port 1-5\r\n"); return -1; }
    if (parse_u32(argv[2], &mask) || mask > 0x1F) { printf("mask 0-0x1F\r\n"); return -1; }

    if (LAN9370_SetPortMembership((LAN9370_Port_t)port, (uint8_t)mask) != LAN9370_OK) {
        printf("portgroup failed\r\n"); return -1;
    }
    printf("Port%d egress members set to 0x%02lX\r\n", port, (unsigned long)mask);
    return 0;
}

/* ---- Mirror ---- */
int cmd_mirror(int argc, char *argv[])
{
    int srcPort, dstPort;
    if (argc < 3) {
        printf("usage: mirror <src_port> <dst_port|off>\r\n");
        return -1;
    }
    if (parse_port(argv[1], 1, 5, &srcPort)) { printf("invalid src port\r\n"); return -1; }
    if (strcmp(argv[2], "off") == 0) dstPort = 0;
    else if (parse_port(argv[2], 1, 5, &dstPort)) { printf("invalid dst port\r\n"); return -1; }

    if (LAN9370_SetPortMirror((LAN9370_Port_t)srcPort, (LAN9370_Port_t)dstPort, false) != LAN9370_OK) {
        printf("mirror failed\r\n"); return -1;
    }
    printf(dstPort == 0 ? "mirror disabled on port %d\r\n" : "port %d traffic mirrored to port %d\r\n", srcPort, dstPort);
    return 0;
}

/* ---- PTP / gPTP ---- */
int cmd_ptp(int argc, char *argv[])
{
    if (argc < 2) { printf("usage: ptp on|off|status|gptp on|off|status\r\n"); return -1; }
    if (strcmp(argv[1], "on") == 0)
        return (LAN9370_SetPtpEnable(true) == LAN9370_OK) ? 0 : -1;
    if (strcmp(argv[1], "off") == 0)
        return (LAN9370_SetPtpEnable(false) == LAN9370_OK) ? 0 : -1;
    if (strcmp(argv[1], "status") == 0) {
        bool pe=false, ge=false;
        LAN9370_GetPtpEnable(&pe); LAN9370_GetGptpEnable(&ge);
        printf("PTP: %s, gPTP: %s\r\n", pe ? "ENABLED" : "DISABLED", ge ? "ENABLED" : "DISABLED");
        return 0;
    }
    if (strcmp(argv[1], "gptp") == 0) {
        if (argc < 3) { printf("usage: ptp gptp on|off|status\r\n"); return -1; }
        if (strcmp(argv[2], "on") == 0)
            return (LAN9370_SetGptpEnable(true) == LAN9370_OK) ? 0 : -1;
        if (strcmp(argv[2], "off") == 0)
            return (LAN9370_SetGptpEnable(false) == LAN9370_OK) ? 0 : -1;
        if (strcmp(argv[2], "status") == 0) {
            bool ge=false;
            LAN9370_GetGptpEnable(&ge);
            printf("gPTP: %s\r\n", ge ? "ENABLED" : "DISABLED");
            return 0;
        }
    }
    printf("invalid ptp command\r\n"); return -1;
}

/* ---- Config persist ---- */
int cmd_config(int argc, char *argv[])
{
    LAN9370_PersistSettings_t settings;

    if (argc < 2) { printf("usage: config save|load|show|erase\r\n"); return -1; }
    if (strcmp(argv[1], "save") == 0) {
        if (capture_current_settings(&settings) != 0) { printf("capture failed\r\n"); return -1; }
        if (LAN9370_PersistSave(&settings) != 0) { printf("save failed\r\n"); return -1; }
        printf("settings saved to MCU flash\r\n"); return 0;
    }
    if (strcmp(argv[1], "load") == 0) {
        if (LAN9370_PersistLoad(&settings) != 0) { printf("no valid saved settings\r\n"); return -1; }
        if (apply_settings(&settings) != 0) { printf("apply failed\r\n"); return -1; }
        printf("saved settings loaded and applied\r\n"); return 0;
    }
    if (strcmp(argv[1], "show") == 0) {
        if (LAN9370_PersistLoad(&settings) != 0) { printf("no valid saved settings\r\n"); return -1; }
        printf("Saved settings:\r\n");
        printf("  VLAN: %s\r\n", settings.vlanEnable ? "ON" : "OFF");
        printf("  PTP: %s\r\n", settings.ptpEnable ? "ON" : "OFF");
        printf("  gPTP: %s\r\n", settings.gptpEnable ? "ON" : "OFF");
        for (int p = 1; p <= 4; p++)
            printf("  Port%d mode: %s\r\n", p, settings.t1Mode[p-1] ? "Master" : "Slave");
        for (int p = 1; p <= 5; p++)
            printf("  Port%d VLAN VID: %u, mirror dst: %u\r\n", p,
                   (unsigned)settings.vlanVid[p-1], (unsigned)settings.mirrorDst[p-1]);
        return 0;
    }
    if (strcmp(argv[1], "erase") == 0) {
        if (LAN9370_PersistErase() != 0) { printf("erase failed\r\n"); return -1; }
        printf("saved settings erased\r\n"); return 0;
    }
    printf("usage: config save|load|show|erase\r\n"); return -1;
}

/* ---- Static MAC ---- */
int cmd_staticmac(int argc, char *argv[])
{
    if (argc < 2) { printf("usage: staticmac list|flush\r\n"); return -1; }
    if (strcmp(argv[1], "list") == 0) return LAN9370_PrintStaticMacTable();
    if (strcmp(argv[1], "flush") == 0) return (LAN9370_FlushStaticMAC() == LAN9370_OK) ? 0 : -1;
    printf("usage: staticmac list|flush\r\n"); return -1;
}

/* ---- help ---- */
int cmd_help(int argc, char *argv[])
{
    (void)argc; (void)argv;
    printf("\r\n=== LAN9370 Shell Commands ===\r\n");
    printf("  info                  - chip info + all port status + VLAN\r\n");
    printf("  dump                  - register dump\r\n");
    printf("  reset                 - MCU software reset (NVIC)\r\n");
    printf("  chipreset             - hardware reset LAN9370 (nRST)\r\n");
    printf("  uptime                - MCU uptime since boot\r\n");
    printf("  port    <1-5>          - port status detail\r\n");
    printf("  master  <1-4>          - set T1 master\r\n");
    printf("  slave   <1-4>          - set T1 slave\r\n");
    printf("  enable  <1-5>          - enable port\r\n");
    printf("  disable <1-5>          - disable port\r\n");
    printf("  vlan    on|off|set <p> <vid>|show - VLAN control\r\n");
    printf("  vlanadd <vid> <mask>   - write VLAN table entry (experimental)\r\n");
    printf("  portgroup <p> <mask>   - port egress isolation (reliable)\r\n");
    printf("  mirror  <src> <dst|off> - port mirroring\r\n");
    printf("  ptp     on|off|status|gptp on|off|status - PTP/gPTP\r\n");
    printf("  config  save|load|show|erase - settings persistence\r\n");
    printf("  staticmac list|flush   - static MAC table\r\n");
    printf("  phyread <port> <reg>   - read T1 PHY register\r\n");
    printf("  phywrite<port> <reg> <val> - write T1 PHY register\r\n");
    printf("  spiread <addr>         - raw SPI read byte\r\n");
    printf("  spiwrite<addr> <val>   - raw SPI write byte\r\n");
    printf("  smiread <phy> <reg>    - SMI/MDIO read\r\n");
    printf("  mib     [port]         - port MIB counters\r\n");
    printf("  led     [mask val]     - LED control register\r\n");
    printf("  temp                   - chip temperature\r\n");
    printf("  sqi     [port]         - Signal Quality Indicator (T1)\r\n");
    printf("  mse     [port]         - Mean Square Error (T1)\r\n");
    printf("  linkinfo[port]         - detailed link diagnostics\r\n");
    printf("  cable   [port]         - cable fault diagnosis\r\n");
    printf("  mcu                    - MCU info (clk, UID, flash)\r\n");
    printf("  help                   - this help\r\n");
    printf("=============================\r\n\r\n");
    return 0;
}

/* ---- Built-in 'help' is provided by letter-shell; we only export custom commands ---- */
/* SHELL_EXPORT_CMD(help...) removed to avoid conflict with letter-shell built-in */

/* Commands registered via SHELL_EXPORT_CMD below */


 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), info, cmd_info, LAN9370 chip info);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), dump, cmd_dump, Register dump);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), reset, cmd_mcu_reset, MCU software reset);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), chipreset, cmd_chipreset, LAN9370 hardware reset);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), uptime, cmd_uptime, MCU uptime);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), port, cmd_port, Port status);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), master, cmd_master, Set T1 master);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), slave, cmd_slave, Set T1 slave);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), enable, cmd_enable, Enable port);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), disable, cmd_disable, Disable port);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), vlan, cmd_vlan, VLAN control);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), vlanadd, cmd_vlan_add, Add VLAN table entry);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), portgroup, cmd_portgroup, Port egress isolation);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), mirror, cmd_mirror, Port mirroring);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ptp, cmd_ptp, PTP/gPTP control);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), config, cmd_config, Settings persistence);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), staticmac, cmd_staticmac, Static MAC table);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), phyread, cmd_phyread, PHY register read);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), spiread, cmd_spiread, Raw SPI read);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), mib, cmd_mib, Port MIB counters);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), led, cmd_led, LED control);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), temp, cmd_temp, Chip temperature);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), sqi, cmd_sqi, Signal Quality Indicator);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), linkinfo, cmd_linkinfo, Link diagnostics);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), mse, cmd_mse, Mean Square Error T1);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), cable, cmd_cable, Cable fault diagnosis);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), spiwrite, cmd_spiwrite, Raw SPI write);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), phywrite, cmd_phywrite, PHY register write);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), smiread, cmd_smiread, SMI/MDIO read);
 SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), mcu, cmd_mcu, MCU info/diag);
