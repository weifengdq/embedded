/**
  ******************************************************************************
  * @file    shell_port.c
  * @brief   Letter-shell port for STM32H723 + LAN8671 diagnostics
  ******************************************************************************
  */

#include "shell_port.h"

#include "ethernetif.h"
#include "eth_custom_phy_interface.h"
#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static UART_HandleTypeDef *gShellUart;
static Shell gShell;
static char gShellBuffer[512];

#define SHELL_RX_RING_SIZE 256U
static volatile uint16_t gShellRxHead;
static volatile uint16_t gShellRxTail;
static uint8_t gShellRxByte;
static uint8_t gShellRxRing[SHELL_RX_RING_SIZE] __attribute__((section(".lwip_sec")));

static void shell_rx_push(uint8_t ch)
{
    uint16_t next = (uint16_t)((gShellRxHead + 1U) % SHELL_RX_RING_SIZE);

    if (next != gShellRxTail)
    {
        gShellRxRing[gShellRxHead] = ch;
        gShellRxHead = next;
    }
}

static int shell_rx_pop(uint8_t *ch)
{
    if ((ch == NULL) || (gShellRxHead == gShellRxTail))
    {
        return 0;
    }

    *ch = gShellRxRing[gShellRxTail];
    gShellRxTail = (uint16_t)((gShellRxTail + 1U) % SHELL_RX_RING_SIZE);
    return 1;
}

static short userShellWrite(char *data, unsigned short len)
{
    if ((gShellUart == NULL) || (data == NULL) || (len == 0U))
    {
        return 0;
    }

    if (HAL_UART_Transmit(gShellUart, (uint8_t *)data, len, HAL_MAX_DELAY) != HAL_OK)
    {
        return 0;
    }

    return (short)len;
}

static short userShellRead(char *data, unsigned short len)
{
    unsigned short index;

    if ((data == NULL) || (len == 0U))
    {
        return 0;
    }

    for (index = 0; index < len; ++index)
    {
        uint8_t ch;

        if (!shell_rx_pop(&ch))
        {
            return (short)index;
        }

        data[index] = (char)ch;
    }

    return (short)len;
}

int __io_putchar(int ch)
{
    uint8_t byte = (uint8_t)ch;

    if (gShellUart != NULL)
    {
        (void)HAL_UART_Transmit(gShellUart, &byte, 1U, HAL_MAX_DELAY);
    }

    return ch;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == LPUART1))
    {
        shell_rx_push(gShellRxByte);
        (void)HAL_UART_Receive_IT(huart, &gShellRxByte, 1U);
    }
}

static user_phy_Object_t *shell_get_phy(void)
{
    user_phy_Object_t *phy = ethernetif_get_user_phy();

    if ((phy == NULL) || (phy->Is_Initialized == 0U))
    {
        printf("PHY not ready\r\n");
        return NULL;
    }

    return phy;
}

static int shell_parse_u32(const char *arg, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed;

    if ((arg == NULL) || (value == NULL) || (*arg == '\0'))
    {
        return -1;
    }

    parsed = strtoul(arg, &end, 0);
    if ((end == NULL) || (*end != '\0'))
    {
        return -1;
    }

    *value = (uint32_t)parsed;
    return 0;
}

static int shell_parse_on_off(const char *arg, uint8_t *enabled)
{
    if ((arg == NULL) || (enabled == NULL))
    {
        return -1;
    }

    if (strcmp(arg, "on") == 0)
    {
        *enabled = 1U;
        return 0;
    }

    if (strcmp(arg, "off") == 0)
    {
        *enabled = 0U;
        return 0;
    }

    return -1;
}

static int shell_read_c45(uint16_t devad, uint16_t reg, uint32_t *value)
{
    user_phy_Object_t *phy = shell_get_phy();

    if (phy == NULL)
    {
        return USER_PHY_STATUS_ERROR;
    }

    return USER_PHY_ReadC45(phy, devad, reg, value);
}

static int shell_write_c45(uint16_t devad, uint16_t reg, uint16_t value)
{
    user_phy_Object_t *phy = shell_get_phy();

    if (phy == NULL)
    {
        return USER_PHY_STATUS_ERROR;
    }

    return USER_PHY_WriteC45(phy, devad, reg, value);
}

static void shell_print_sts1_flags(uint32_t value)
{
    printf("STS1 flags:");
    if ((value & USER_PHY_STS1_SQI) != 0U) printf(" SQI");
    if ((value & USER_PHY_STS1_PSTC) != 0U) printf(" PSTC");
    if ((value & USER_PHY_STS1_TXCOL) != 0U) printf(" TXCOL");
    if ((value & USER_PHY_STS1_TXJAB) != 0U) printf(" TXJAB");
    if ((value & USER_PHY_STS1_TSSI) != 0U) printf(" TSSI");
    if ((value & USER_PHY_STS1_EMPCYC) != 0U) printf(" EMPCYC");
    if ((value & USER_PHY_STS1_RXINTO) != 0U) printf(" RXINTO");
    if ((value & USER_PHY_STS1_UNEXPB) != 0U) printf(" UNEXPB");
    if ((value & USER_PHY_STS1_BCNBFTO) != 0U) printf(" BCNBFTO");
    if ((value & USER_PHY_STS1_UNCRS) != 0U) printf(" UNCRS");
    if ((value & USER_PHY_STS1_PLCASYM) != 0U) printf(" PLCASYM");
    if ((value & USER_PHY_STS1_ESDERR) != 0U) printf(" ESDERR");
    if ((value & USER_PHY_STS1_DEC5B) != 0U) printf(" DEC5B");
    if ((value & 0xFFFFU) == 0U) printf(" none");
    printf("\r\n");
}

static void shell_print_sts2_flags(uint32_t value)
{
    printf("STS2 flags:");
    if ((value & USER_PHY_STS2_RESETC) != 0U) printf(" RESETC");
    if ((value & USER_PHY_STS2_WKEMDI) != 0U) printf(" WKEMDI");
    if ((value & USER_PHY_STS2_WKEWI) != 0U) printf(" WKEWI");
    if ((value & USER_PHY_STS2_UV33) != 0U) printf(" UV33");
    if ((value & USER_PHY_STS2_OT) != 0U) printf(" OT");
    if ((value & USER_PHY_STS2_IWDTO) != 0U) printf(" IWDTO");
    if ((value & 0xFFFFU) == 0U) printf(" none");
    printf("\r\n");
}

static int cmd_uptime(int argc, char *argv[])
{
    uint32_t tick;
    uint32_t sec;
    uint32_t min;
    uint32_t hour;
    uint32_t day;

    (void)argc;
    (void)argv;

    tick = HAL_GetTick();
    sec = tick / 1000U;
    min = sec / 60U;
    hour = min / 60U;
    day = hour / 24U;

    printf("Uptime: %lu ms (%lu days %lu:%02lu:%02lu.%03lu)\r\n",
           (unsigned long)tick,
           (unsigned long)day,
           (unsigned long)(hour % 24U),
           (unsigned long)(min % 60U),
           (unsigned long)(sec % 60U),
           (unsigned long)(tick % 1000U));
    return 0;
}

static int cmd_reset(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("MCU software reset in 500 ms...\r\n");
    HAL_Delay(500U);
    NVIC_SystemReset();
    return 0;
}

static int cmd_mcu(int argc, char *argv[])
{
    uint32_t rsr;
    uint32_t uid0;
    uint32_t uid1;
    uint32_t uid2;
    uint16_t flash_kb;

    (void)argc;
    (void)argv;

    rsr = RCC->RSR;
    uid0 = *(uint32_t *)0x1FF1E800U;
    uid1 = *(uint32_t *)0x1FF1E804U;
    uid2 = *(uint32_t *)0x1FF1E808U;
    flash_kb = *(uint16_t *)0x1FF1E880U;

    printf("\r\n=== MCU Info ===\r\n");
    printf("Build:  %s %s\r\n", __DATE__, __TIME__);
    printf("SYSCLK: %lu MHz\r\n", (unsigned long)(HAL_RCC_GetSysClockFreq() / 1000000U));
    printf("HCLK:   %lu MHz\r\n", (unsigned long)(HAL_RCC_GetHCLKFreq() / 1000000U));
    printf("RST:   ");
    if ((rsr & RCC_RSR_BORRSTF) != 0U) printf(" BOR");
    if ((rsr & RCC_RSR_PINRSTF) != 0U) printf(" PIN");
    if ((rsr & RCC_RSR_PORRSTF) != 0U) printf(" POR");
    if ((rsr & RCC_RSR_SFTRSTF) != 0U) printf(" SW");
    if ((rsr & RCC_RSR_IWDG1RSTF) != 0U) printf(" IWDG");
    if ((rsr & RCC_RSR_WWDG1RSTF) != 0U) printf(" WWDG");
    if ((rsr & RCC_RSR_LPWRRSTF) != 0U) printf(" LP");
    printf("\r\n");
    printf("UID:    %08lX-%08lX-%08lX\r\n",
           (unsigned long)uid0,
           (unsigned long)uid1,
           (unsigned long)uid2);
    printf("Flash:  %u KB\r\n", (unsigned int)flash_kb);
    printf("================\r\n\r\n");
    return 0;
}

static int cmd_phyinfo(int argc, char *argv[])
{
    user_phy_Object_t *phy;
    user_phy_status_snapshot_t snapshot;
    user_phy_plca_config_t config;
    uint32_t irq_pin;
    int32_t status;

    (void)argc;
    (void)argv;

    phy = shell_get_phy();
    if (phy == NULL)
    {
        return -1;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    status = USER_PHY_ReadStatusSnapshot(phy, &snapshot);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read status failed: %ld\r\n", (long)status);
        return -1;
    }

    if ((snapshot.status1 & USER_PHY_STS1_PSTC) != 0U)
    {
        (void)USER_PHY_SyncCollisionDetection(phy, 1U, NULL, NULL, NULL);
        (void)USER_PHY_ReadStatusSnapshot(phy, &snapshot);
    }

    status = USER_PHY_GetPlcaConfig(phy, &config);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read plca failed: %ld\r\n", (long)status);
        return -1;
    }

    irq_pin = (HAL_GPIO_ReadPin(LAN8671_IRQ_N_GPIO_Port, LAN8671_IRQ_N_Pin) == GPIO_PIN_SET) ? 1UL : 0UL;

    printf("PHYAD=%lu PHYID=0x%04lX/0x%04lX IRQ_N=%lu\r\n",
           (unsigned long)phy->DevAddr,
           (unsigned long)snapshot.phy_id1,
           (unsigned long)snapshot.phy_id2,
           (unsigned long)irq_pin);
    printf("Link=%s SQI=%lu valid=%lu err=%lu\r\n",
           ((snapshot.bsr & USER_PHY_BSR_LINK_STATUS) != 0U) ? "UP" : "DOWN",
           (unsigned long)((snapshot.sqi_status & USER_PHY_SQISTS0_SQIVAL_MASK) >> USER_PHY_SQISTS0_SQIVAL_SHIFT),
           (unsigned long)(((snapshot.sqi_status & USER_PHY_SQISTS0_SQIVLD) != 0U) ? 1UL : 0UL),
           (unsigned long)(snapshot.sqi_status & USER_PHY_SQISTS0_SQIERRC_MASK));
    printf("PLCA en=%lu pst=%lu node_id=%lu node_count=%lu totmr=0x%02lX burst=0x%04lX\r\n",
           (unsigned long)config.enabled,
           (unsigned long)(((snapshot.plca_status & USER_PHY_PLCA_STS_PST) != 0U) ? 1UL : 0UL),
           (unsigned long)config.node_id,
           (unsigned long)config.node_count,
           (unsigned long)config.totmr,
           (unsigned long)snapshot.plca_burst);
    printf("Collision auto=%s cden=%s pstc_irq=%s STS1=0x%04lX STS2=0x%04lX\r\n",
           (config.collision_auto != 0U) ? "on" : "off",
           ((snapshot.cdctl0 & USER_PHY_CDCTL0_CDEN) != 0U) ? "on" : "off",
           (config.pstc_irq_enabled != 0U) ? "on" : "off",
           (unsigned long)snapshot.status1,
           (unsigned long)snapshot.status2);
    return 0;
}

static int cmd_c45read(int argc, char *argv[])
{
    uint32_t devad;
    uint32_t reg;
    uint32_t value;
    int32_t status;

    if ((argc < 3) || (shell_parse_u32(argv[1], &devad) != 0) || (shell_parse_u32(argv[2], &reg) != 0))
    {
        printf("usage: c45read <devad> <reg>\r\n");
        return -1;
    }

    status = shell_read_c45((uint16_t)devad, (uint16_t)reg, &value);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("c45read failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("MMD%lu[0x%04lX] = 0x%04lX\r\n",
           (unsigned long)devad,
           (unsigned long)reg,
           (unsigned long)value);
    return 0;
}

static int cmd_c45write(int argc, char *argv[])
{
    uint32_t devad;
    uint32_t reg;
    uint32_t value;
    int32_t status;

    if ((argc < 4) ||
        (shell_parse_u32(argv[1], &devad) != 0) ||
        (shell_parse_u32(argv[2], &reg) != 0) ||
        (shell_parse_u32(argv[3], &value) != 0))
    {
        printf("usage: c45write <devad> <reg> <val>\r\n");
        return -1;
    }

    status = shell_write_c45((uint16_t)devad, (uint16_t)reg, (uint16_t)value);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("c45write failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("OK\r\n");
    return 0;
}

static int shell_show_plca(user_phy_Object_t *phy)
{
    user_phy_plca_config_t config;
    user_phy_status_snapshot_t snapshot;
    int32_t status;

    status = USER_PHY_GetPlcaConfig(phy, &config);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read plca config failed: %ld\r\n", (long)status);
        return -1;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    status = USER_PHY_ReadStatusSnapshot(phy, &snapshot);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read plca status failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("plca enabled=%lu active=%lu node_id=%lu node_count=%lu\r\n",
           (unsigned long)config.enabled,
           (unsigned long)(((snapshot.plca_status & USER_PHY_PLCA_STS_PST) != 0U) ? 1UL : 0UL),
           (unsigned long)config.node_id,
           (unsigned long)config.node_count);
    printf("plca totmr=0x%02lX burst_count=%lu burst_timer=0x%02lX\r\n",
           (unsigned long)config.totmr,
           (unsigned long)config.burst_count,
           (unsigned long)config.burst_timer);
    printf("plca pstc_irq=%s collision_auto=%s cden=%s ctrl0=0x%04lX ctrl1=0x%04lX\r\n",
           (config.pstc_irq_enabled != 0U) ? "on" : "off",
           (config.collision_auto != 0U) ? "on" : "off",
           ((snapshot.cdctl0 & USER_PHY_CDCTL0_CDEN) != 0U) ? "on" : "off",
           (unsigned long)snapshot.plca_ctrl0,
           (unsigned long)snapshot.plca_ctrl1);
    return 0;
}

static int cmd_plca(int argc, char *argv[])
{
    user_phy_Object_t *phy;
    user_phy_plca_config_t config;
    uint32_t value;
    uint8_t enabled;
    int32_t status;

    phy = shell_get_phy();
    if (phy == NULL)
    {
        return -1;
    }

    if ((argc == 1) || (strcmp(argv[1], "show") == 0))
    {
        return shell_show_plca(phy);
    }

    status = USER_PHY_GetPlcaConfig(phy, &config);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read plca config failed: %ld\r\n", (long)status);
        return -1;
    }

    if ((argc == 3) && (strcmp(argv[1], "enable") == 0))
    {
        if (shell_parse_on_off(argv[2], &enabled) != 0)
        {
            printf("usage: plca enable on|off\r\n");
            return -1;
        }
        config.enabled = enabled;
    }
    else if ((argc == 3) && (strcmp(argv[1], "nodeid") == 0))
    {
        if ((shell_parse_u32(argv[2], &value) != 0) || (value > 0xFFU))
        {
            printf("usage: plca nodeid <0-255>\r\n");
            return -1;
        }
        config.node_id = (uint8_t)value;
    }
    else if ((argc == 3) && (strcmp(argv[1], "nodecount") == 0))
    {
        if ((shell_parse_u32(argv[2], &value) != 0) || (value == 0U) || (value > 0xFFU))
        {
            printf("usage: plca nodecount <1-255>\r\n");
            return -1;
        }
        config.node_count = (uint8_t)value;
    }
    else if ((argc == 3) && (strcmp(argv[1], "totmr") == 0))
    {
        if ((shell_parse_u32(argv[2], &value) != 0) || (value > 0xFFU))
        {
            printf("usage: plca totmr <0-255>\r\n");
            return -1;
        }
        config.totmr = (uint8_t)value;
    }
    else if ((argc == 3) && (strcmp(argv[1], "burstcnt") == 0))
    {
        if ((shell_parse_u32(argv[2], &value) != 0) || (value > 0xFFU))
        {
            printf("usage: plca burstcnt <0-255>\r\n");
            return -1;
        }
        config.burst_count = (uint8_t)value;
    }
    else if ((argc == 3) && (strcmp(argv[1], "bursttmr") == 0))
    {
        if ((shell_parse_u32(argv[2], &value) != 0) || (value > 0xFFU))
        {
            printf("usage: plca bursttmr <0-255>\r\n");
            return -1;
        }
        config.burst_timer = (uint8_t)value;
    }
    else if ((argc == 3) && (strcmp(argv[1], "irq") == 0))
    {
        if (shell_parse_on_off(argv[2], &enabled) != 0)
        {
            printf("usage: plca irq on|off\r\n");
            return -1;
        }
        config.pstc_irq_enabled = enabled;
    }
    else if ((argc == 3) && (strcmp(argv[1], "collauto") == 0))
    {
        if (shell_parse_on_off(argv[2], &enabled) != 0)
        {
            printf("usage: plca collauto on|off\r\n");
            return -1;
        }
        config.collision_auto = enabled;
    }
    else if ((argc == 2) && (strcmp(argv[1], "apply") == 0))
    {
    }
    else
    {
        printf("usage: plca [show|apply|enable on|off|nodeid <n>|nodecount <n>|totmr <n>|burstcnt <n>|bursttmr <n>|irq on|off|collauto on|off]\r\n");
        return -1;
    }

    status = USER_PHY_SetPlcaConfig(phy, &config, 1U);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("plca set failed: %ld\r\n", (long)status);
        return -1;
    }

    return shell_show_plca(phy);
}

static int cmd_sqi(int argc, char *argv[])
{
    user_phy_Object_t *phy;
    uint32_t ctrl;
    uint32_t value;
    int32_t status;

    phy = shell_get_phy();
    if (phy == NULL)
    {
        return -1;
    }

    if (argc >= 2)
    {
        if (strcmp(argv[1], "on") == 0)
        {
            status = shell_write_c45(USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_SQICTL,
                                     (uint16_t)(USER_PHY_SQICTL_SQIEN | 0x1400U));
            if (status != USER_PHY_STATUS_OK)
            {
                printf("enable sqi failed: %ld\r\n", (long)status);
                return -1;
            }
        }
        else if (strcmp(argv[1], "off") == 0)
        {
            status = shell_write_c45(USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_SQICTL, 0x1400U);
            if (status != USER_PHY_STATUS_OK)
            {
                printf("disable sqi failed: %ld\r\n", (long)status);
                return -1;
            }
        }
        else if (strcmp(argv[1], "reset") == 0)
        {
            status = shell_write_c45(USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_SQICTL,
                                     (uint16_t)(USER_PHY_SQICTL_SQIRST | USER_PHY_SQICTL_SQIEN | 0x1400U));
            if (status != USER_PHY_STATUS_OK)
            {
                printf("reset sqi failed: %ld\r\n", (long)status);
                return -1;
            }
            status = shell_write_c45(USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_SQICTL,
                                     (uint16_t)(USER_PHY_SQICTL_SQIEN | 0x1400U));
            if (status != USER_PHY_STATUS_OK)
            {
                printf("restart sqi failed: %ld\r\n", (long)status);
                return -1;
            }
        }
        else
        {
            printf("usage: sqi [on|off|reset]\r\n");
            return -1;
        }
    }

    status = shell_read_c45(USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_SQICTL, &ctrl);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read sqi control failed: %ld\r\n", (long)status);
        return -1;
    }

    status = shell_read_c45(USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_SQISTS0, &value);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read sqi failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("SQI ctl=0x%04lX raw=0x%04lX valid=%lu value=%lu err=%lu errcode=%lu\r\n",
           (unsigned long)ctrl,
           (unsigned long)value,
           (unsigned long)(((value & USER_PHY_SQISTS0_SQIVLD) != 0U) ? 1UL : 0UL),
           (unsigned long)((value & USER_PHY_SQISTS0_SQIVAL_MASK) >> USER_PHY_SQISTS0_SQIVAL_SHIFT),
           (unsigned long)(((value & USER_PHY_SQISTS0_SQIERR) != 0U) ? 1UL : 0UL),
           (unsigned long)(value & USER_PHY_SQISTS0_SQIERRC_MASK));
    return 0;
}

static int cmd_irq(int argc, char *argv[])
{
    user_phy_Object_t *phy;
    user_phy_status_snapshot_t snapshot;
    uint32_t irq_pin;
    int32_t status;

    (void)argc;
    (void)argv;

    phy = shell_get_phy();
    if (phy == NULL)
    {
        return -1;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    status = USER_PHY_ReadStatusSnapshot(phy, &snapshot);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read irq status failed: %ld\r\n", (long)status);
        return -1;
    }

    irq_pin = (HAL_GPIO_ReadPin(LAN8671_IRQ_N_GPIO_Port, LAN8671_IRQ_N_Pin) == GPIO_PIN_SET) ? 1UL : 0UL;
    printf("IRQ_N=%lu STS1=0x%04lX STS2=0x%04lX STS3=0x%04lX IMSK1=0x%04lX\r\n",
           (unsigned long)irq_pin,
           (unsigned long)snapshot.status1,
           (unsigned long)snapshot.status2,
           (unsigned long)snapshot.status3,
           (unsigned long)snapshot.imsk1);
    shell_print_sts1_flags(snapshot.status1);
    shell_print_sts2_flags(snapshot.status2);
    return 0;
}

static int cmd_plcadiag(int argc, char *argv[])
{
    user_phy_Object_t *phy;
    user_phy_status_snapshot_t snapshot;
    int32_t status;

    (void)argc;
    (void)argv;

    phy = shell_get_phy();
    if (phy == NULL)
    {
        return -1;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    status = USER_PHY_ReadStatusSnapshot(phy, &snapshot);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read plca diag failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("PLCA diag STS1=0x%04lX STS3(ERRTOID)=0x%02lX active=%lu\r\n",
           (unsigned long)snapshot.status1,
           (unsigned long)(snapshot.status3 & 0xFFU),
           (unsigned long)(((snapshot.plca_status & USER_PHY_PLCA_STS_PST) != 0U) ? 1UL : 0UL));
    printf("EMPCYC=%lu RXINTO=%lu UNEXPB=%lu BCNBFTO=%lu UNCRS=%lu PLCASYM=%lu\r\n",
           (unsigned long)(((snapshot.status1 & USER_PHY_STS1_EMPCYC) != 0U) ? 1UL : 0UL),
           (unsigned long)(((snapshot.status1 & USER_PHY_STS1_RXINTO) != 0U) ? 1UL : 0UL),
           (unsigned long)(((snapshot.status1 & USER_PHY_STS1_UNEXPB) != 0U) ? 1UL : 0UL),
           (unsigned long)(((snapshot.status1 & USER_PHY_STS1_BCNBFTO) != 0U) ? 1UL : 0UL),
           (unsigned long)(((snapshot.status1 & USER_PHY_STS1_UNCRS) != 0U) ? 1UL : 0UL),
           (unsigned long)(((snapshot.status1 & USER_PHY_STS1_PLCASYM) != 0U) ? 1UL : 0UL));
    return 0;
}

static int cmd_pcsdiag(int argc, char *argv[])
{
    uint32_t t1spcssts;
    uint32_t rmtjab;
    uint32_t cortx;
    int32_t status;

    (void)argc;
    (void)argv;

    status = shell_read_c45(USER_PHY_PCS_MMD_DEVICE, 0x08F4U, &t1spcssts);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read pcs status failed: %ld\r\n", (long)status);
        return -1;
    }

    status = shell_read_c45(USER_PHY_PCS_MMD_DEVICE, 0x08F5U, &rmtjab);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read pcs diag1 failed: %ld\r\n", (long)status);
        return -1;
    }

    status = shell_read_c45(USER_PHY_PCS_MMD_DEVICE, 0x08F6U, &cortx);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read pcs diag2 failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("PCS status=0x%04lX fault=%lu\r\n",
           (unsigned long)t1spcssts,
           (unsigned long)(((t1spcssts & 0x0080U) != 0U) ? 1UL : 0UL));
    printf("PCS remote_jabber_count=%lu corrupted_tx_count=%lu\r\n",
           (unsigned long)rmtjab,
           (unsigned long)cortx);
    return 0;
}

static int cmd_collision(int argc, char *argv[])
{
    user_phy_Object_t *phy;
    user_phy_plca_config_t config;
    user_phy_status_snapshot_t snapshot;
    int32_t status;

    phy = shell_get_phy();
    if (phy == NULL)
    {
        return -1;
    }

    status = USER_PHY_GetPlcaConfig(phy, &config);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read config failed: %ld\r\n", (long)status);
        return -1;
    }

    if (argc >= 2)
    {
        if (strcmp(argv[1], "auto") == 0)
        {
            config.collision_auto = 1U;
            status = USER_PHY_SetPlcaConfig(phy, &config, 0U);
            if (status == USER_PHY_STATUS_OK)
            {
                status = USER_PHY_SyncCollisionDetection(phy, 1U, NULL, NULL, NULL);
            }
        }
        else if (strcmp(argv[1], "on") == 0)
        {
            config.collision_auto = 0U;
            status = USER_PHY_SetPlcaConfig(phy, &config, 0U);
            if (status == USER_PHY_STATUS_OK)
            {
                status = USER_PHY_ModifyC45(phy, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_CDCTL0,
                                            USER_PHY_CDCTL0_CDEN, USER_PHY_CDCTL0_CDEN);
            }
        }
        else if (strcmp(argv[1], "off") == 0)
        {
            config.collision_auto = 0U;
            status = USER_PHY_SetPlcaConfig(phy, &config, 0U);
            if (status == USER_PHY_STATUS_OK)
            {
                status = USER_PHY_ModifyC45(phy, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_CDCTL0,
                                            USER_PHY_CDCTL0_CDEN, 0U);
            }
        }
        else
        {
            printf("usage: collision [show|auto|on|off]\r\n");
            return -1;
        }

        if (status != USER_PHY_STATUS_OK)
        {
            printf("collision set failed: %ld\r\n", (long)status);
            return -1;
        }
    }

    memset(&snapshot, 0, sizeof(snapshot));
    status = USER_PHY_ReadStatusSnapshot(phy, &snapshot);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read collision status failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("collision auto=%s cden=%s plca_active=%s cdctl0=0x%04lX\r\n",
           (config.collision_auto != 0U) ? "on" : "off",
           ((snapshot.cdctl0 & USER_PHY_CDCTL0_CDEN) != 0U) ? "on" : "off",
           ((snapshot.plca_status & USER_PHY_PLCA_STS_PST) != 0U) ? "yes" : "no",
           (unsigned long)snapshot.cdctl0);
    return 0;
}

static int cmd_loopback(int argc, char *argv[])
{
    user_phy_Object_t *phy;
    uint32_t value;
    int32_t status;

    phy = shell_get_phy();
    if (phy == NULL)
    {
        return -1;
    }

    if (argc >= 2)
    {
        if (strcmp(argv[1], "on") == 0)
        {
            status = USER_PHY_EnableLoopbackMode(phy);
        }
        else if (strcmp(argv[1], "off") == 0)
        {
            status = USER_PHY_DisableLoopbackMode(phy);
        }
        else
        {
            printf("usage: loopback [on|off]\r\n");
            return -1;
        }

        if (status != USER_PHY_STATUS_OK)
        {
            printf("loopback command failed: %ld\r\n", (long)status);
            return -1;
        }
    }

    status = shell_read_c45(USER_PHY_PCS_MMD_DEVICE, USER_PHY_T1SPCSCTL, &value);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read loopback state failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("loopback=%s raw=0x%04lX\r\n",
           ((value & USER_PHY_T1SPCSCTL_LBE) != 0U) ? "on" : "off",
           (unsigned long)value);
    return 0;
}

static int cmd_ethdiag(int argc, char *argv[])
{
    ethernetif_diag_t diag;

    (void)argc;
    (void)argv;

    memset(&diag, 0, sizeof(diag));
    ethernetif_get_diag(&diag);

    printf("ETHDIAG rx_alloc_status=%lu rx_alloc_error_count=%lu rx_alloc_recover_count=%lu\r\n",
           (unsigned long)diag.rx_alloc_status,
           (unsigned long)diag.rx_alloc_error_count,
           (unsigned long)diag.rx_alloc_recover_count);
    printf("ETHDIAG rx_desc_idx=%lu rx_build_desc_idx=%lu rx_build_desc_cnt=%lu dmacsr=0x%08lX\r\n",
           (unsigned long)diag.rx_desc_idx,
           (unsigned long)diag.rx_build_desc_idx,
           (unsigned long)diag.rx_build_desc_cnt,
           (unsigned long)diag.dma_csr);
    return 0;
}

int Shell_Init(UART_HandleTypeDef *huart)
{
    gShellUart = huart;
    gShellRxHead = 0U;
    gShellRxTail = 0U;

    gShell.write = userShellWrite;
    gShell.read = userShellRead;
    shellInit(&gShell, gShellBuffer, sizeof(gShellBuffer));

    (void)HAL_UART_Receive_IT(huart, &gShellRxByte, 1U);
    return 0;
}

void Shell_Process(void)
{
    uint8_t ch;

    while (shell_rx_pop(&ch) != 0)
    {
        shellHandler(&gShell, ch);
    }
}

void Shell_PrintBanner(void)
{
    printf("\r\n=== STM32H723 + LAN8671 Shell ===\r\n");
    printf("Type 'help' for command list\r\n\r\n");
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), mcu, cmd_mcu, show MCU info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), uptime, cmd_uptime, show MCU uptime);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), reset, cmd_reset, reset MCU);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), phyinfo, cmd_phyinfo, show LAN8671 summary);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), c45read, cmd_c45read, read Clause45 register);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), c45write, cmd_c45write, write Clause45 register);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), plca, cmd_plca, show or set PLCA parameters);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), sqi, cmd_sqi, show signal quality indicator);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), irq, cmd_irq, read IRQ and status registers);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), plcadiag, cmd_plcadiag, show PLCA diagnostic status);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), pcsdiag, cmd_pcsdiag, show PCS diagnostic counters);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), collision, cmd_collision, control collision detect policy);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), loopback, cmd_loopback, control PCS loopback);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ethdiag, cmd_ethdiag, show ETH DMA and RX pool state);