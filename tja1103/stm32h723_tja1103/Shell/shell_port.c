/**
  ******************************************************************************
  * @file    shell_port.c
  * @brief   Letter-shell port for STM32H723 + TJA1103 diagnostics
  ******************************************************************************
  */

#include "shell_port.h"

#include "ethernetif.h"
#include "shell.h"
#include "eth_custom_phy_interface.h"

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

static int shell_modify_c45(uint16_t devad, uint16_t reg, uint16_t clear_mask, uint16_t set_mask)
{
    user_phy_Object_t *phy = shell_get_phy();

    if (phy == NULL)
    {
        return USER_PHY_STATUS_ERROR;
    }

    return USER_PHY_ModifyC45(phy, devad, reg, clear_mask, set_mask);
}

static void shell_print_timer_value(const char *label, uint32_t raw)
{
    uint32_t time_ms = (raw & USER_PHY_VEND1_TIMING_TIME_MS_MASK) >> USER_PHY_VEND1_TIMING_TIME_MS_SHIFT;
    uint32_t time_sub = (raw & USER_PHY_VEND1_TIMING_TIME_SUBMS_MASK) >> USER_PHY_VEND1_TIMING_TIME_SUBMS_SHIFT;

    printf("%s: %lu.%03lu ms (raw=0x%04lX)\r\n",
           label,
           (unsigned long)time_ms,
           (unsigned long)(time_sub * 125UL),
           (unsigned long)raw);
}

static int shell_parse_on_off(const char *arg, uint16_t mask, uint16_t *set_mask)
{
    if ((arg == NULL) || (set_mask == NULL))
    {
        return -1;
    }

    if (strcmp(arg, "on") == 0)
    {
        *set_mask = mask;
        return 0;
    }

    if (strcmp(arg, "off") == 0)
    {
        *set_mask = 0U;
        return 0;
    }

    return -1;
}

static int shell_parse_mac48(const char *arg, uint8_t mac[6])
{
    char copy[24];
    char *cursor;
    unsigned int index = 0U;

    if ((arg == NULL) || (mac == NULL))
    {
        return -1;
    }

    if (strlen(arg) >= sizeof(copy))
    {
        return -1;
    }

    (void)strcpy(copy, arg);

    for (cursor = copy; *cursor != '\0'; ++cursor)
    {
        if (*cursor == '-')
        {
            *cursor = ':';
        }
    }

    cursor = copy;
    while ((cursor != NULL) && (index < 6U))
    {
        char *next = strchr(cursor, ':');
        char *end = NULL;
        unsigned long value;

        if (next != NULL)
        {
            *next = '\0';
        }

        if ((*cursor == '\0') || (strlen(cursor) > 2U))
        {
            return -1;
        }

        value = strtoul(cursor, &end, 16);
        if ((end == NULL) || (*end != '\0') || (value > 0xFFUL))
        {
            return -1;
        }

        mac[index++] = (uint8_t)value;
        cursor = (next != NULL) ? (next + 1) : NULL;
    }

    return (index == 6U) ? 0 : -1;
}

static void shell_print_mac48(const char *label, const uint8_t mac[6])
{
    printf("%s=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
           label,
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int shell_read_bist_mac(uint16_t reg0, uint8_t mac[6])
{
    uint32_t value0;
    uint32_t value1;
    uint32_t value2;

    if ((mac == NULL) ||
        (shell_read_c45(USER_PHY_DEVAD_VEND1, reg0, &value0) != USER_PHY_STATUS_OK) ||
        (shell_read_c45(USER_PHY_DEVAD_VEND1, (uint16_t)(reg0 + 1U), &value1) != USER_PHY_STATUS_OK) ||
        (shell_read_c45(USER_PHY_DEVAD_VEND1, (uint16_t)(reg0 + 2U), &value2) != USER_PHY_STATUS_OK))
    {
        return USER_PHY_STATUS_READ_ERROR;
    }

    mac[0] = (uint8_t)((value2 >> 8) & 0xFFU);
    mac[1] = (uint8_t)(value2 & 0xFFU);
    mac[2] = (uint8_t)((value1 >> 8) & 0xFFU);
    mac[3] = (uint8_t)(value1 & 0xFFU);
    mac[4] = (uint8_t)((value0 >> 8) & 0xFFU);
    mac[5] = (uint8_t)(value0 & 0xFFU);
    return USER_PHY_STATUS_OK;
}

static int shell_write_bist_mac(uint16_t reg0, const uint8_t mac[6])
{
    if (mac == NULL)
    {
        return USER_PHY_STATUS_ERROR;
    }

    if ((shell_write_c45(USER_PHY_DEVAD_VEND1, reg0,
                         (uint16_t)(((uint16_t)mac[4] << 8) | mac[5])) != USER_PHY_STATUS_OK) ||
        (shell_write_c45(USER_PHY_DEVAD_VEND1, (uint16_t)(reg0 + 1U),
                         (uint16_t)(((uint16_t)mac[2] << 8) | mac[3])) != USER_PHY_STATUS_OK) ||
        (shell_write_c45(USER_PHY_DEVAD_VEND1, (uint16_t)(reg0 + 2U),
                         (uint16_t)(((uint16_t)mac[0] << 8) | mac[1])) != USER_PHY_STATUS_OK))
    {
        return USER_PHY_STATUS_WRITE_ERROR;
    }

    return USER_PHY_STATUS_OK;
}

static int shell_bist_set_intercept(uint16_t clear_mask, uint16_t set_mask)
{
    return shell_modify_c45(USER_PHY_DEVAD_VEND1,
                            USER_PHY_VEND1_BIST_INTERCEPT_CONFIG,
                            clear_mask,
                            set_mask);
}

static int shell_enable_phy_cfg_access(void)
{
    int32_t status;

    status = shell_modify_c45(USER_PHY_DEVAD_VEND1,
                              USER_PHY_VEND1_PORT_CONTROL,
                              0U,
                              USER_PHY_VEND1_PORT_CONTROL_EN);
    if (status != USER_PHY_STATUS_OK)
    {
        return status;
    }

    return shell_modify_c45(USER_PHY_DEVAD_VEND1,
                            USER_PHY_VEND1_PHY_CONTROL,
                            0U,
                            USER_PHY_VEND1_PHY_CONTROL_CONFIG_EN);
}

static int shell_restart_phy_operation(void)
{
    int32_t status;

    status = shell_modify_c45(USER_PHY_DEVAD_AN,
                              USER_PHY_MDIO_AN_CTRL1,
                              0U,
                              (uint16_t)(USER_PHY_MDIO_AN_CTRL1_ENABLE | USER_PHY_MDIO_AN_CTRL1_RESTART));
    if (status != USER_PHY_STATUS_OK)
    {
        return status;
    }

    return shell_modify_c45(USER_PHY_DEVAD_VEND1,
                            USER_PHY_VEND1_PHY_CONTROL,
                            0U,
                            USER_PHY_VEND1_PHY_CONTROL_START_OP);
}

static const char *shell_role_mode_name(uint32_t adv_l, uint32_t bt1_ctrl)
{
    uint32_t force_ms = adv_l & USER_PHY_MDIO_AN_T1_ADV_L_FORCE_MS;
    uint32_t leader = bt1_ctrl & USER_PHY_MDIO_PMA_PMD_BT1_CTRL_CFG_MST;

    if (leader != 0U)
    {
        return (force_ms != 0U) ? "master-force" : "master-pref";
    }

    return (force_ms != 0U) ? "slave-force" : "slave-pref";
}

static const char *shell_polarity_mode_name(uint32_t phy_cfg)
{
    uint32_t swap = phy_cfg & USER_PHY_VEND1_PHY_CONFIG_POLARITY_SWAP;
    uint32_t no_correct = phy_cfg & USER_PHY_VEND1_PHY_CONFIG_POLARITY_CORRECT_DISABLE;

    if ((swap == 0U) && (no_correct == 0U))
    {
        return "auto";
    }
    if ((swap == 0U) && (no_correct != 0U))
    {
        return "nocorrect";
    }
    if ((swap != 0U) && (no_correct == 0U))
    {
        return "swap";
    }
    return "swap-nocorrect";
}

static int shell_set_role_mode(uint16_t adv_l_set, uint16_t adv_m_set, uint16_t leader_bit)
{
    int32_t status;

    status = shell_enable_phy_cfg_access();
    if (status != USER_PHY_STATUS_OK)
    {
        return status;
    }

    status = shell_modify_c45(USER_PHY_DEVAD_AN,
                              USER_PHY_MDIO_AN_T1_ADV_L,
                              USER_PHY_MDIO_AN_T1_ADV_L_FORCE_MS,
                              adv_l_set);
    if (status != USER_PHY_STATUS_OK)
    {
        return status;
    }

    status = shell_modify_c45(USER_PHY_DEVAD_AN,
                              USER_PHY_MDIO_AN_T1_ADV_M,
                              (uint16_t)(USER_PHY_MDIO_AN_T1_ADV_M_100BT1 | USER_PHY_MDIO_AN_T1_ADV_M_MST),
                              (uint16_t)(USER_PHY_MDIO_AN_T1_ADV_M_100BT1 | adv_m_set));
    if (status != USER_PHY_STATUS_OK)
    {
        return status;
    }

    status = shell_modify_c45(USER_PHY_DEVAD_PMAPMD,
                              USER_PHY_MDIO_PMA_PMD_BT1_CTRL,
                              USER_PHY_MDIO_PMA_PMD_BT1_CTRL_CFG_MST,
                              leader_bit);
    if (status != USER_PHY_STATUS_OK)
    {
        return status;
    }

    return shell_restart_phy_operation();
}

static int shell_show_role_status(void)
{
    uint32_t adv_l;
    uint32_t adv_m;
    uint32_t bt1_ctrl;
    uint32_t phy_status;

    if ((shell_read_c45(USER_PHY_DEVAD_AN, USER_PHY_MDIO_AN_T1_ADV_L, &adv_l) != USER_PHY_STATUS_OK) ||
        (shell_read_c45(USER_PHY_DEVAD_AN, USER_PHY_MDIO_AN_T1_ADV_M, &adv_m) != USER_PHY_STATUS_OK) ||
        (shell_read_c45(USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_PMA_PMD_BT1_CTRL, &bt1_ctrl) != USER_PHY_STATUS_OK) ||
        (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PHY_STATUS, &phy_status) != USER_PHY_STATUS_OK))
    {
        printf("role read failed\r\n");
        return -1;
    }

    printf("role=%s adv_l=0x%04lX adv_m=0x%04lX base_t1=0x%04lX\r\n",
           shell_role_mode_name(adv_l, bt1_ctrl),
           (unsigned long)adv_l,
           (unsigned long)adv_m,
           (unsigned long)bt1_ctrl);
    printf("leader_select=%s link_available=%lu link_status=%lu loc_rx=%lu rem_rx=%lu\r\n",
           ((bt1_ctrl & USER_PHY_MDIO_PMA_PMD_BT1_CTRL_CFG_MST) != 0U) ? "leader" : "follower",
           ((phy_status & USER_PHY_VEND1_PHY_STATUS_LINK_AVAILABLE) != 0U) ? 1UL : 0UL,
           ((phy_status & USER_PHY_VEND1_PHY_STATUS_LINK_STATUS) != 0U) ? 1UL : 0UL,
           ((phy_status & USER_PHY_VEND1_PHY_STATUS_LOC_RCVR_STATUS) != 0U) ? 1UL : 0UL,
           ((phy_status & USER_PHY_VEND1_PHY_STATUS_REM_RCVR_STATUS) != 0U) ? 1UL : 0UL);
    return 0;
}

static int shell_set_role_from_words(const char *w1, const char *w2)
{
    if ((w1 == NULL) || (w2 == NULL))
    {
        return -1;
    }

    if ((strcmp(w1, "master") == 0) && (strcmp(w2, "pref") == 0))
    {
        return shell_set_role_mode(0U, USER_PHY_MDIO_AN_T1_ADV_M_MST, USER_PHY_MDIO_PMA_PMD_BT1_CTRL_CFG_MST);
    }
    if ((strcmp(w1, "master") == 0) && (strcmp(w2, "force") == 0))
    {
        return shell_set_role_mode(USER_PHY_MDIO_AN_T1_ADV_L_FORCE_MS,
                                   USER_PHY_MDIO_AN_T1_ADV_M_MST,
                                   USER_PHY_MDIO_PMA_PMD_BT1_CTRL_CFG_MST);
    }
    if ((strcmp(w1, "slave") == 0) && (strcmp(w2, "pref") == 0))
    {
        return shell_set_role_mode(0U, 0U, 0U);
    }
    if ((strcmp(w1, "slave") == 0) && (strcmp(w2, "force") == 0))
    {
        return shell_set_role_mode(USER_PHY_MDIO_AN_T1_ADV_L_FORCE_MS, 0U, 0U);
    }

    return -1;
}

static int shell_enable_infra_test_access(uint16_t port_bits)
{
    int32_t status;

    status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PORT_INFRA_CONTROL, 0U, USER_PHY_VEND1_PORT_INFRA_CONTROL_EN);
    if (status != USER_PHY_STATUS_OK)
    {
        return status;
    }

    status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_INFRA_CONFIG, 0U, USER_PHY_VEND1_INFRA_CONFIG_TEST_ENABLE);
    if (status != USER_PHY_STATUS_OK)
    {
        return status;
    }

    status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PORT_FUNC_ENABLES, 0U,
                              (uint16_t)(USER_PHY_VEND1_PORT_FUNC_TEST_ENABLE | port_bits));
    if (status != USER_PHY_STATUS_OK)
    {
        return status;
    }

    return shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PHY_CONFIG, 0U, USER_PHY_VEND1_PHY_CONFIG_TEST_ENABLE);
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
    user_phy_Report_t report;
    uint32_t mse = 0U;
    uint32_t wake_status = 0U;
    uint32_t phy_state = 0U;
    int32_t status;

    (void)argc;
    (void)argv;

    phy = shell_get_phy();
    if (phy == NULL)
    {
        return -1;
    }

    status = USER_PHY_ReadReport(phy, &report);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("read report failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("PHYAD=%lu PHYID=0x%04lX/0x%04lX\r\n",
           (unsigned long)phy->DevAddr,
           (unsigned long)report.phy_id1,
           (unsigned long)report.phy_id2);
        (void)shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_MSE, &mse);
        (void)shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_WAKE_SLEEP_STATUS, &wake_status);
        (void)shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PHY_STATE, &phy_state);
        printf("Link=%s AN=%s SQI=%lu valid=%lu MSE=%lu\r\n",
           ((report.pma_stat1 & USER_PHY_MDIO_STAT1_LINK_STATUS) != 0U) ? "UP" : "DOWN",
           ((report.an_stat1 & USER_PHY_MDIO_AN_STAT1_COMPLETE) != 0U) ? "DONE" : "BUSY",
           (unsigned long)(report.signal_quality & USER_PHY_VEND1_SIGNAL_QUALITY_MASK),
            ((report.signal_quality & USER_PHY_VEND1_SIGNAL_QUALITY_VALID) != 0U) ? 1UL : 0UL,
            (unsigned long)((mse & USER_PHY_VEND1_MSE_MASK) >> USER_PHY_VEND1_MSE_SHIFT));
        printf("WakeStatus=0x%04lX PHY_STATE=0x%04lX cable=0x%04lX losses=0x%04lX\r\n",
            (unsigned long)wake_status,
            (unsigned long)phy_state,
            (unsigned long)report.cable_test,
            (unsigned long)report.link_losses_and_failures);
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

static int cmd_role(int argc, char *argv[])
{
    int32_t status;

    if ((argc == 1) || ((argc >= 2) && (strcmp(argv[1], "show") == 0)))
    {
        return shell_show_role_status();
    }

    if (argc < 2)
    {
        printf("usage: role [show|master-pref|master-force|slave-pref|slave-force]\r\n");
        return -1;
    }

    if (strcmp(argv[1], "master-pref") == 0)
    {
        status = shell_set_role_from_words("master", "pref");
    }
    else if (strcmp(argv[1], "master-force") == 0)
    {
        status = shell_set_role_from_words("master", "force");
    }
    else if (strcmp(argv[1], "slave-pref") == 0)
    {
        status = shell_set_role_from_words("slave", "pref");
    }
    else if (strcmp(argv[1], "slave-force") == 0)
    {
        status = shell_set_role_from_words("slave", "force");
    }
    else
    {
        printf("usage: role [show|master-pref|master-force|slave-pref|slave-force]\r\n");
        return -1;
    }

    if (status != USER_PHY_STATUS_OK)
    {
        printf("role set failed: %ld\r\n", (long)status);
        return -1;
    }

    return shell_show_role_status();
}

static int cmd_master(int argc, char *argv[])
{
    int32_t status;

    if (argc == 1)
    {
        return shell_show_role_status();
    }

    if (strcmp(argv[1], "pref") == 0)
    {
        status = shell_set_role_from_words("master", "pref");
    }
    else if (strcmp(argv[1], "force") == 0)
    {
        status = shell_set_role_from_words("master", "force");
    }
    else
    {
        printf("usage: master [pref|force]\r\n");
        return -1;
    }

    if (status != USER_PHY_STATUS_OK)
    {
        printf("master set failed: %ld\r\n", (long)status);
        return -1;
    }

    return shell_show_role_status();
}

static int cmd_slave(int argc, char *argv[])
{
    int32_t status;

    if (argc == 1)
    {
        return shell_show_role_status();
    }

    if (strcmp(argv[1], "pref") == 0)
    {
        status = shell_set_role_from_words("slave", "pref");
    }
    else if (strcmp(argv[1], "force") == 0)
    {
        status = shell_set_role_from_words("slave", "force");
    }
    else
    {
        printf("usage: slave [pref|force]\r\n");
        return -1;
    }

    if (status != USER_PHY_STATUS_OK)
    {
        printf("slave set failed: %ld\r\n", (long)status);
        return -1;
    }

    return shell_show_role_status();
}

static int cmd_t1cfg(int argc, char *argv[])
{
    uint32_t phy_cfg;
    uint32_t phy_status;
    uint32_t pma_ctrl1;
    uint32_t bt1_ctrl;
    uint32_t adv_l;
    uint32_t adv_m;
    int32_t status;

    if ((argc == 1) || ((argc >= 2) && (strcmp(argv[1], "show") == 0)))
    {
        if ((shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PHY_CONFIG, &phy_cfg) != USER_PHY_STATUS_OK) ||
            (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PHY_STATUS, &phy_status) != USER_PHY_STATUS_OK) ||
            (shell_read_c45(USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_CTRL1, &pma_ctrl1) != USER_PHY_STATUS_OK) ||
            (shell_read_c45(USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_PMA_PMD_BT1_CTRL, &bt1_ctrl) != USER_PHY_STATUS_OK) ||
            (shell_read_c45(USER_PHY_DEVAD_AN, USER_PHY_MDIO_AN_T1_ADV_L, &adv_l) != USER_PHY_STATUS_OK) ||
            (shell_read_c45(USER_PHY_DEVAD_AN, USER_PHY_MDIO_AN_T1_ADV_M, &adv_m) != USER_PHY_STATUS_OK))
        {
            printf("t1cfg read failed\r\n");
            return -1;
        }

        printf("role=%s auto=%s lowpower=%s polarity=%s\r\n",
               shell_role_mode_name(adv_l, bt1_ctrl),
               ((phy_cfg & USER_PHY_VEND1_PHY_CONFIG_AUTO) != 0U) ? "on" : "off",
               ((pma_ctrl1 & USER_PHY_PMA_CONTROL1_LOW_POWER) != 0U) ? "on" : "off",
               shell_polarity_mode_name(phy_cfg));
        printf("status: detected_polarity=%s link_available=%lu link_status=%lu loc_rx=%lu rem_rx=%lu send_data=%lu scrambler=%lu\r\n",
               ((phy_status & USER_PHY_VEND1_PHY_STATUS_DETECTED_POLARITY) != 0U) ? "inverted" : "normal",
               ((phy_status & USER_PHY_VEND1_PHY_STATUS_LINK_AVAILABLE) != 0U) ? 1UL : 0UL,
               ((phy_status & USER_PHY_VEND1_PHY_STATUS_LINK_STATUS) != 0U) ? 1UL : 0UL,
               ((phy_status & USER_PHY_VEND1_PHY_STATUS_LOC_RCVR_STATUS) != 0U) ? 1UL : 0UL,
               ((phy_status & USER_PHY_VEND1_PHY_STATUS_REM_RCVR_STATUS) != 0U) ? 1UL : 0UL,
               ((phy_status & USER_PHY_VEND1_PHY_STATUS_SENDN_OR_DATA) != 0U) ? 1UL : 0UL,
               ((phy_status & USER_PHY_VEND1_PHY_STATUS_SCRAMBLER_STATUS) != 0U) ? 1UL : 0UL);
        return 0;
    }

    if (strcmp(argv[1], "restart") == 0)
    {
        status = shell_restart_phy_operation();
        if (status != USER_PHY_STATUS_OK)
        {
            printf("t1cfg restart failed: %ld\r\n", (long)status);
            return -1;
        }
        return cmd_t1cfg(1, argv);
    }

    status = shell_enable_phy_cfg_access();
    if (status != USER_PHY_STATUS_OK)
    {
        printf("t1cfg access failed: %ld\r\n", (long)status);
        return -1;
    }

    if ((strcmp(argv[1], "auto") == 0) && (argc >= 3))
    {
        uint16_t set_mask;
        if (shell_parse_on_off(argv[2], USER_PHY_VEND1_PHY_CONFIG_AUTO, &set_mask) != 0)
        {
            printf("usage: t1cfg auto on|off\r\n");
            return -1;
        }
        status = shell_modify_c45(USER_PHY_DEVAD_VEND1,
                                  USER_PHY_VEND1_PHY_CONFIG,
                                  USER_PHY_VEND1_PHY_CONFIG_AUTO,
                                  set_mask);
    }
    else if ((strcmp(argv[1], "lowpower") == 0) && (argc >= 3))
    {
        uint16_t set_mask;
        if (shell_parse_on_off(argv[2], USER_PHY_PMA_CONTROL1_LOW_POWER, &set_mask) != 0)
        {
            printf("usage: t1cfg lowpower on|off\r\n");
            return -1;
        }
        status = shell_modify_c45(USER_PHY_DEVAD_PMAPMD,
                                  USER_PHY_MDIO_CTRL1,
                                  USER_PHY_PMA_CONTROL1_LOW_POWER,
                                  set_mask);
    }
    else if ((strcmp(argv[1], "polarity") == 0) && (argc >= 3))
    {
        uint16_t set_mask;
        if (strcmp(argv[2], "auto") == 0)
        {
            set_mask = 0U;
        }
        else if (strcmp(argv[2], "nocorrect") == 0)
        {
            set_mask = USER_PHY_VEND1_PHY_CONFIG_POLARITY_CORRECT_DISABLE;
        }
        else if (strcmp(argv[2], "swap") == 0)
        {
            set_mask = USER_PHY_VEND1_PHY_CONFIG_POLARITY_SWAP;
        }
        else if (strcmp(argv[2], "swap-nocorrect") == 0)
        {
            set_mask = (uint16_t)(USER_PHY_VEND1_PHY_CONFIG_POLARITY_SWAP |
                                  USER_PHY_VEND1_PHY_CONFIG_POLARITY_CORRECT_DISABLE);
        }
        else
        {
            printf("usage: t1cfg polarity auto|nocorrect|swap|swap-nocorrect\r\n");
            return -1;
        }
        status = shell_modify_c45(USER_PHY_DEVAD_VEND1,
                                  USER_PHY_VEND1_PHY_CONFIG,
                                  (uint16_t)(USER_PHY_VEND1_PHY_CONFIG_POLARITY_SWAP |
                                             USER_PHY_VEND1_PHY_CONFIG_POLARITY_CORRECT_DISABLE),
                                  set_mask);
    }
    else
    {
        printf("usage: t1cfg [show|restart|auto on|off|lowpower on|off|polarity auto|nocorrect|swap|swap-nocorrect]\r\n");
        return -1;
    }

    if (status != USER_PHY_STATUS_OK)
    {
        printf("t1cfg set failed: %ld\r\n", (long)status);
        return -1;
    }

    if ((strcmp(argv[1], "auto") == 0) || (strcmp(argv[1], "polarity") == 0))
    {
        status = shell_restart_phy_operation();
        if (status != USER_PHY_STATUS_OK)
        {
            printf("t1cfg restart failed: %ld\r\n", (long)status);
            return -1;
        }
    }

    return cmd_t1cfg(1, argv);
}

static int cmd_sqi(int argc, char *argv[])
{
    uint32_t value;
    int32_t status;

    (void)argc;
    (void)argv;

    status = shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_SIGNAL_QUALITY, &value);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("SQI read failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("SQI raw=0x%04lX valid=%lu current=%lu worst=%lu warn_limit=%lu\r\n",
           (unsigned long)value,
           ((value & USER_PHY_VEND1_SIGNAL_QUALITY_VALID) != 0U) ? 1UL : 0UL,
           (unsigned long)(value & USER_PHY_VEND1_SIGNAL_QUALITY_MASK),
           (unsigned long)((value & USER_PHY_VEND1_SIGNAL_QUALITY_WORST_MASK) >> USER_PHY_VEND1_SIGNAL_QUALITY_WORST_SHIFT),
           (unsigned long)((value & USER_PHY_VEND1_SIGNAL_QUALITY_WARN_LIMIT_MASK) >> 8U));
    return 0;
}

static int cmd_mse(int argc, char *argv[])
{
    uint32_t value;
    int32_t status;

    if (argc >= 2)
    {
        if (strcmp(argv[1], "on") == 0)
        {
            status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_MSE, 0U, USER_PHY_VEND1_MSE_ENABLE);
        }
        else if (strcmp(argv[1], "off") == 0)
        {
            status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_MSE, USER_PHY_VEND1_MSE_ENABLE, 0U);
        }
        else
        {
            printf("usage: mse [on|off]\r\n");
            return -1;
        }

        if (status != USER_PHY_STATUS_OK)
        {
            printf("MSE configure failed: %ld\r\n", (long)status);
            return -1;
        }
    }

    status = shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_MSE, &value);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("MSE read failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("MSE raw=0x%04lX enabled=%lu value=%lu\r\n",
           (unsigned long)value,
           ((value & USER_PHY_VEND1_MSE_ENABLE) != 0U) ? 1UL : 0UL,
           (unsigned long)((value & USER_PHY_VEND1_MSE_MASK) >> USER_PHY_VEND1_MSE_SHIFT));
    return 0;
}

static int cmd_cable(int argc, char *argv[])
{
    user_phy_Object_t *phy;
    uint32_t result = 0U;
    int32_t status;

    (void)argc;
    (void)argv;

    phy = shell_get_phy();
    if (phy == NULL)
    {
        return -1;
    }

    status = USER_PHY_RunCableTest(phy, &result);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("Cable test failed: %ld raw=0x%04lX\r\n", (long)status, (unsigned long)result);
        return -1;
    }

    printf("Cable test raw=0x%04lX fault=%lu\r\n",
           (unsigned long)result,
           (unsigned long)(result & USER_PHY_VEND1_CABLE_TEST_RESULT_MASK));
    return 0;
}

static int cmd_train(int argc, char *argv[])
{
    uint32_t value;

    (void)argc;
    (void)argv;

    if (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_LINK_TRAINING_TIMER, &value) == USER_PHY_STATUS_OK)
    {
        shell_print_timer_value("Link training", value);
    }
    if (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_LOC_RCVR_STATUS_TIMER, &value) == USER_PHY_STATUS_OK)
    {
        shell_print_timer_value("Local receiver", value);
    }
    if (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_REM_RCVR_STATUS_TIMER, &value) == USER_PHY_STATUS_OK)
    {
        shell_print_timer_value("Remote receiver", value);
    }
    if (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_FOLLOWER_SILENT_TIMER, &value) == USER_PHY_STATUS_OK)
    {
        shell_print_timer_value("Follower silent", value);
    }
    return 0;
}

static int cmd_symerr(int argc, char *argv[])
{
    uint32_t sym;
    uint32_t misc;
    uint32_t lf;

    (void)argc;
    (void)argv;

    if ((shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_SYMBOL_ERROR_COUNTER, &sym) != USER_PHY_STATUS_OK) ||
        (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_ERROR_COUNTER_MISC, &misc) != USER_PHY_STATUS_OK) ||
        (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_LINK_LOSSES_AND_FAILURES, &lf) != USER_PHY_STATUS_OK))
    {
        printf("Counter read failed\r\n");
        return -1;
    }

    printf("SYM=%lu link_status_drops=%lu link_available_drops=%lu link_losses=%lu link_failures=%lu\r\n",
           (unsigned long)sym,
           (unsigned long)((misc & USER_PHY_VEND1_LINK_DROP_STATUS_MASK) >> USER_PHY_VEND1_LINK_DROP_STATUS_SHIFT),
           (unsigned long)(misc & USER_PHY_VEND1_LINK_DROP_AVAIL_MASK),
           (unsigned long)((lf & USER_PHY_VEND1_LINK_LOSSES_MASK) >> USER_PHY_VEND1_LINK_LOSSES_SHIFT),
           (unsigned long)(lf & USER_PHY_VEND1_LINK_FAILURES_MASK));
    return 0;
}

static int cmd_temp(int argc, char *argv[])
{
    uint32_t value;
    int32_t status;

    (void)argc;
    (void)argv;

    status = shell_read_c45(USER_PHY_DEVAD_VEND1, 0x031FU, &value);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("TEMP_STATUS read failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("TEMP_STATUS=0x%04lX event=%lu now=%lu latent=%lu limit=0x%02lX\r\n",
           (unsigned long)value,
           ((value & 0x8000U) != 0U) ? 1UL : 0UL,
           ((value & 0x2000U) != 0U) ? 1UL : 0UL,
           ((value & 0x0800U) != 0U) ? 1UL : 0UL,
           (unsigned long)(value & 0x00FFU));
    return 0;
}

static int cmd_supply(int argc, char *argv[])
{
    uint32_t ao;
    uint32_t core;
    uint32_t vddio;
    uint32_t vregd;
    uint32_t vrega;

    (void)argc;
    (void)argv;

    if (shell_read_c45(USER_PHY_DEVAD_VEND1, 0x0311U, &ao) == USER_PHY_STATUS_OK)
    {
        printf("AO_SYSTEM_SUPPLY_STATUS=0x%04lX\r\n", (unsigned long)ao);
    }
    if (shell_read_c45(USER_PHY_DEVAD_VEND1, 0x0313U, &core) == USER_PHY_STATUS_OK)
    {
        printf("CORE_SUPPLY_STATUS=0x%04lX uv_now=%lu ov_now=%lu\r\n",
               (unsigned long)core,
               ((core & 0x1000U) != 0U) ? 1UL : 0UL,
               ((core & 0x2000U) != 0U) ? 1UL : 0UL);
    }
    if (shell_read_c45(USER_PHY_DEVAD_VEND1, 0x0315U, &vddio) == USER_PHY_STATUS_OK)
    {
        printf("VDDIO_SUPPLY_STATUS=0x%04lX level=%lu\r\n",
               (unsigned long)vddio,
               (unsigned long)(vddio & 0x0003U));
    }
    if (shell_read_c45(USER_PHY_DEVAD_VEND1, 0x0318U, &vregd) == USER_PHY_STATUS_OK)
    {
        printf("VREGD_SUPPLY_STATUS=0x%04lX\r\n", (unsigned long)vregd);
    }
    if (shell_read_c45(USER_PHY_DEVAD_VEND1, 0x0319U, &vrega) == USER_PHY_STATUS_OK)
    {
        printf("VREGA_SUPPLY_STATUS=0x%04lX\r\n", (unsigned long)vrega);
    }
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

static int cmd_wake(int argc, char *argv[])
{
    uint32_t status;
    uint32_t config;
    uint32_t params;
    uint32_t always;

    if ((argc >= 2) && (strcmp(argv[1], "clear") == 0))
    {
        if (shell_write_c45(USER_PHY_DEVAD_VEND1,
                            USER_PHY_VEND1_WAKE_SLEEP_STATUS,
                            (uint16_t)(USER_PHY_VEND1_WAKE_SLEEP_STATUS_WU_IO_RECEIVED |
                                       USER_PHY_VEND1_WAKE_SLEEP_STATUS_WU_PHY_RECEIVED |
                                       USER_PHY_VEND1_WAKE_SLEEP_STATUS_WUP_RECEIVED |
                                       USER_PHY_VEND1_WAKE_SLEEP_STATUS_WUR_RECEIVED |
                                       USER_PHY_VEND1_WAKE_SLEEP_STATUS_AUTO_SLEEP_EVENT |
                                       USER_PHY_VEND1_WAKE_SLEEP_STATUS_SLEEP_FAILED |
                                       USER_PHY_VEND1_WAKE_SLEEP_STATUS_LPS_RECEIVED)) != USER_PHY_STATUS_OK)
        {
            printf("wake clear failed\r\n");
            return -1;
        }
    }

    if ((shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_WAKE_SLEEP_STATUS, &status) != USER_PHY_STATUS_OK) ||
        (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_WAKE_SLEEP_CONFIG, &config) != USER_PHY_STATUS_OK) ||
        (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_WAKE_SLEEP_PARAMETERS, &params) != USER_PHY_STATUS_OK) ||
        (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_ALWAYS_ACCESSIBLE, &always) != USER_PHY_STATUS_OK))
    {
        printf("wake/sleep read failed\r\n");
        return -1;
    }

        printf("WAKE_SLEEP_STATUS=0x%04lX sleep=%lu lps=%lu failed=%lu auto=%lu wur=%lu wup=%lu wuphy=%lu wuio=%lu\r\n",
           (unsigned long)status,
           ((status & USER_PHY_VEND1_WAKE_SLEEP_STATUS_SLEEP_STATUS) != 0U) ? 1UL : 0UL,
           ((status & USER_PHY_VEND1_WAKE_SLEEP_STATUS_LPS_RECEIVED) != 0U) ? 1UL : 0UL,
           ((status & USER_PHY_VEND1_WAKE_SLEEP_STATUS_SLEEP_FAILED) != 0U) ? 1UL : 0UL,
           ((status & USER_PHY_VEND1_WAKE_SLEEP_STATUS_AUTO_SLEEP_EVENT) != 0U) ? 1UL : 0UL,
           ((status & USER_PHY_VEND1_WAKE_SLEEP_STATUS_WUR_RECEIVED) != 0U) ? 1UL : 0UL,
           ((status & USER_PHY_VEND1_WAKE_SLEEP_STATUS_WUP_RECEIVED) != 0U) ? 1UL : 0UL,
           ((status & USER_PHY_VEND1_WAKE_SLEEP_STATUS_WU_PHY_RECEIVED) != 0U) ? 1UL : 0UL,
           ((status & USER_PHY_VEND1_WAKE_SLEEP_STATUS_WU_IO_RECEIVED) != 0U) ? 1UL : 0UL);
        printf("WAKE_SLEEP_CONFIG=0x%04lX function=%lu io=%lu smi=%lu fwd_phy=%lu fwd_mdi=%lu no_auto=%lu\r\n",
           (unsigned long)config,
           (unsigned long)((config & USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FUNCTION_SELECT_MASK) >> 14U),
           ((config & USER_PHY_VEND1_WAKE_SLEEP_CONFIG_WU_IO_ENABLE) != 0U) ? 1UL : 0UL,
           ((config & USER_PHY_VEND1_WAKE_SLEEP_CONFIG_WU_SMI_ENABLE) != 0U) ? 1UL : 0UL,
           ((config & USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FWD_WU_PHY_TO_WU_IO) != 0U) ? 1UL : 0UL,
           ((config & USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FWD_WUPWUR_TO_WU_IO) != 0U) ? 1UL : 0UL,
           ((config & USER_PHY_VEND1_WAKE_SLEEP_CONFIG_NO_AUTO_ON_WAKE) != 0U) ? 1UL : 0UL);
        printf("WAKE_SLEEP_PARAMETERS=0x%04lX ack=%lu idle=%lu silence=%lu min_silent=%lu ALWAYS_ACCESSIBLE=0x%04lX inhibit=%lu limited=%lu\r\n",
           (unsigned long)params,
           (unsigned long)((params & USER_PHY_VEND1_WAKE_SLEEP_PARAMS_SLEEP_ACK_MASK) >> USER_PHY_VEND1_WAKE_SLEEP_PARAMS_SLEEP_ACK_SHIFT),
           (unsigned long)((params & USER_PHY_VEND1_WAKE_SLEEP_PARAMS_AUTO_SLEEP_IDLE_MASK) >> USER_PHY_VEND1_WAKE_SLEEP_PARAMS_AUTO_SLEEP_IDLE_SHIFT),
           (unsigned long)((params & USER_PHY_VEND1_WAKE_SLEEP_PARAMS_AUTO_SLEEP_SILENCE_MASK) >> USER_PHY_VEND1_WAKE_SLEEP_PARAMS_AUTO_SLEEP_SILENCE_SHIFT),
           (unsigned long)(params & USER_PHY_VEND1_WAKE_SLEEP_PARAMS_MIN_SILENT_MASK),
           (unsigned long)always,
           ((always & 0x0001U) != 0U) ? 1UL : 0UL,
           ((always & 0x0002U) != 0U) ? 1UL : 0UL);
    return 0;
}

static int cmd_wakecfg(int argc, char *argv[])
{
    uint16_t set_mask;
    int32_t status;

    if (argc < 2)
    {
        printf("usage: wakecfg off|basic|tc10|io on|off|smi on|off|fwdphy on|off|fwdmdi on|off|noauto on|off|autoidle on|off|autosilence on|off|autoreject on|off\r\n");
        return -1;
    }

    if (strcmp(argv[1], "off") == 0)
    {
        status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_WAKE_SLEEP_CONFIG,
                                  USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FUNCTION_SELECT_MASK,
                                  USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FUNCTION_NONE);
    }
    else if (strcmp(argv[1], "basic") == 0)
    {
        status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_WAKE_SLEEP_CONFIG,
                                  USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FUNCTION_SELECT_MASK,
                                  USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FUNCTION_BASIC);
    }
    else if (strcmp(argv[1], "tc10") == 0)
    {
        status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_WAKE_SLEEP_CONFIG,
                                  USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FUNCTION_SELECT_MASK,
                                  USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FUNCTION_TC10);
    }
    else
    {
        uint16_t clear_mask;

        if (argc < 3)
        {
            printf("usage: wakecfg io|smi|fwdphy|fwdmdi|noauto|autoidle|autosilence|autoreject on|off\r\n");
            return -1;
        }

        if (strcmp(argv[1], "io") == 0)
        {
            clear_mask = USER_PHY_VEND1_WAKE_SLEEP_CONFIG_WU_IO_ENABLE;
        }
        else if (strcmp(argv[1], "smi") == 0)
        {
            clear_mask = USER_PHY_VEND1_WAKE_SLEEP_CONFIG_WU_SMI_ENABLE;
        }
        else if (strcmp(argv[1], "fwdphy") == 0)
        {
            clear_mask = USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FWD_WU_PHY_TO_WU_IO;
        }
        else if (strcmp(argv[1], "fwdmdi") == 0)
        {
            clear_mask = USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FWD_WUPWUR_TO_WU_IO;
        }
        else if (strcmp(argv[1], "noauto") == 0)
        {
            clear_mask = USER_PHY_VEND1_WAKE_SLEEP_CONFIG_NO_AUTO_ON_WAKE;
        }
        else if (strcmp(argv[1], "autoidle") == 0)
        {
            clear_mask = USER_PHY_VEND1_WAKE_SLEEP_CONFIG_AUTO_SLEEP_ON_IDLE;
        }
        else if (strcmp(argv[1], "autosilence") == 0)
        {
            clear_mask = USER_PHY_VEND1_WAKE_SLEEP_CONFIG_AUTO_SLEEP_ON_SILENCE;
        }
        else if (strcmp(argv[1], "autoreject") == 0)
        {
            clear_mask = USER_PHY_VEND1_WAKE_SLEEP_CONFIG_AUTO_REJECT;
        }
        else
        {
            printf("unknown wakecfg field: %s\r\n", argv[1]);
            return -1;
        }

        if (shell_parse_on_off(argv[2], clear_mask, &set_mask) != 0)
        {
            printf("value must be on|off\r\n");
            return -1;
        }

        status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_WAKE_SLEEP_CONFIG, clear_mask, set_mask);
    }

    if (status != USER_PHY_STATUS_OK)
    {
        printf("wakecfg failed: %ld\r\n", (long)status);
        return -1;
    }

    return cmd_wake(0, NULL);
}

static int cmd_wakeparam(int argc, char *argv[])
{
    uint32_t value;
    uint16_t mask;
    uint16_t shift;
    uint16_t set_mask;
    int32_t status;

    if ((argc < 3) || (shell_parse_u32(argv[2], &value) != 0) || (value > 7U))
    {
        printf("usage: wakeparam ack|idle|silence|min <0-7>\r\n");
        return -1;
    }

    if (strcmp(argv[1], "ack") == 0)
    {
        mask = USER_PHY_VEND1_WAKE_SLEEP_PARAMS_SLEEP_ACK_MASK;
        shift = USER_PHY_VEND1_WAKE_SLEEP_PARAMS_SLEEP_ACK_SHIFT;
    }
    else if (strcmp(argv[1], "idle") == 0)
    {
        mask = USER_PHY_VEND1_WAKE_SLEEP_PARAMS_AUTO_SLEEP_IDLE_MASK;
        shift = USER_PHY_VEND1_WAKE_SLEEP_PARAMS_AUTO_SLEEP_IDLE_SHIFT;
    }
    else if (strcmp(argv[1], "silence") == 0)
    {
        mask = USER_PHY_VEND1_WAKE_SLEEP_PARAMS_AUTO_SLEEP_SILENCE_MASK;
        shift = USER_PHY_VEND1_WAKE_SLEEP_PARAMS_AUTO_SLEEP_SILENCE_SHIFT;
    }
    else if (strcmp(argv[1], "min") == 0)
    {
        mask = USER_PHY_VEND1_WAKE_SLEEP_PARAMS_MIN_SILENT_MASK;
        shift = 0U;
    }
    else
    {
        printf("usage: wakeparam ack|idle|silence|min <0-7>\r\n");
        return -1;
    }

    set_mask = (uint16_t)((value << shift) & mask);
    status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_WAKE_SLEEP_PARAMETERS, mask, set_mask);
    if (status != USER_PHY_STATUS_OK)
    {
        printf("wakeparam failed: %ld\r\n", (long)status);
        return -1;
    }

    return cmd_wake(0, NULL);
}

static int cmd_sleep(int argc, char *argv[])
{
    int32_t status;

    if (argc < 2)
    {
        printf("usage: sleep req|accept|reject|wakephy|wusmi|startop\r\n");
        return -1;
    }

    if (strcmp(argv[1], "req") == 0)
    {
        status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_WAKE_SLEEP_CONTROL, USER_PHY_VEND1_WAKE_SLEEP_CTRL_SLEEP_REQUEST);
    }
    else if (strcmp(argv[1], "accept") == 0)
    {
        status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_WAKE_SLEEP_CONTROL, USER_PHY_VEND1_WAKE_SLEEP_CTRL_SLEEP_ACCEPT);
    }
    else if (strcmp(argv[1], "reject") == 0)
    {
        status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_WAKE_SLEEP_CONTROL, USER_PHY_VEND1_WAKE_SLEEP_CTRL_SLEEP_REJECT);
    }
    else if (strcmp(argv[1], "wakephy") == 0)
    {
        status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_WAKE_SLEEP_CONTROL, USER_PHY_VEND1_WAKE_SLEEP_CTRL_WU_PHY_REQUEST);
    }
    else if (strcmp(argv[1], "wusmi") == 0)
    {
        status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_ALWAYS_ACCESSIBLE, 0U, USER_PHY_VEND1_ALWAYS_ACCESSIBLE_WU_SMI);
    }
    else if (strcmp(argv[1], "startop") == 0)
    {
        status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PHY_CONTROL, 0U, USER_PHY_VEND1_PHY_CONTROL_START_OP);
    }
    else
    {
        printf("usage: sleep req|accept|reject|wakephy|wusmi|startop\r\n");
        return -1;
    }

    if (status != USER_PHY_STATUS_OK)
    {
        printf("sleep command failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("OK\r\n");
    return 0;
}

static int cmd_loopback(int argc, char *argv[])
{
    uint16_t pma_clear = (uint16_t)(USER_PHY_PMA_CONTROL1_LOCAL_LOOPBACK | USER_PHY_PMA_CONTROL1_REMOTE_LOOPBACK);
    uint16_t xmii_clear = (uint16_t)(USER_PHY_VEND1_XMII_CONTROL_PHY_LOOPBACK | USER_PHY_VEND1_XMII_CONTROL_MAC_LOOPBACK);
    int32_t status;

    if (argc < 2)
    {
        printf("usage: loopback off|pma-local|pma-remote|phy|mac\r\n");
        return -1;
    }

    status = shell_enable_infra_test_access(0U);
    if (status == USER_PHY_STATUS_OK)
    {
        status = shell_modify_c45(USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_CTRL1, pma_clear, 0U);
    }
    if (status == USER_PHY_STATUS_OK)
    {
        status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_XMII_CONTROL, xmii_clear, 0U);
    }

    if (status != USER_PHY_STATUS_OK)
    {
        printf("loopback clear failed: %ld\r\n", (long)status);
        return -1;
    }

    if (strcmp(argv[1], "off") == 0)
    {
        printf("Loopback off\r\n");
        return 0;
    }
    if (strcmp(argv[1], "pma-local") == 0)
    {
        status = shell_modify_c45(USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_CTRL1, 0U, USER_PHY_PMA_CONTROL1_LOCAL_LOOPBACK);
    }
    else if (strcmp(argv[1], "pma-remote") == 0)
    {
        status = shell_modify_c45(USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_CTRL1, 0U, USER_PHY_PMA_CONTROL1_REMOTE_LOOPBACK);
    }
    else if (strcmp(argv[1], "phy") == 0)
    {
        status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_XMII_CONTROL, 0U, USER_PHY_VEND1_XMII_CONTROL_PHY_LOOPBACK);
    }
    else if (strcmp(argv[1], "mac") == 0)
    {
        status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_XMII_CONTROL, 0U, USER_PHY_VEND1_XMII_CONTROL_MAC_LOOPBACK);
    }
    else
    {
        printf("usage: loopback off|pma-local|pma-remote|phy|mac\r\n");
        return -1;
    }

    if (status != USER_PHY_STATUS_OK)
    {
        printf("loopback set failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("Loopback mode set: %s\r\n", argv[1]);
    return 0;
}

static int shell_bist_enable_path(void)
{
    int32_t status;

    status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PORT_CONTROL, 0U, USER_PHY_VEND1_PORT_CONTROL_EN);
    if (status != USER_PHY_STATUS_OK)
    {
        return status;
    }

    status = shell_modify_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PORT_BIST_CONTROL, 0U, USER_PHY_VEND1_BIST_PORT_CONFIG_ENABLE);
    if (status != USER_PHY_STATUS_OK)
    {
        return status;
    }

    return shell_enable_infra_test_access(USER_PHY_VEND1_PORT_FUNC_BIST_ENABLE);
}

static void shell_bist_print_route(uint32_t value)
{
    const char *check_path = ((value & USER_PHY_VEND1_BIST_INTERCEPT_CHECK_EPHY) != 0U) ? "ephy" : "xmii";
    const char *tx_path = ((value & USER_PHY_VEND1_BIST_INTERCEPT_TX_EPHY) != 0U) ? "ephy" : "normal";
    const char *rx_path = ((value & USER_PHY_VEND1_BIST_INTERCEPT_RX_XMII) != 0U) ? "xmii" : "normal";

    printf("route: check=%s tx=%s rx=%s raw=0x%04lX\r\n",
        check_path,
        tx_path,
        rx_path,
        (unsigned long)value);
}

static int shell_bist_show_config(void)
{
    uint32_t intercept;
    uint32_t preamble_ipg;
    uint32_t etype;
    uint32_t payload_cfg;
    uint32_t payload_size;
    uint32_t prbs_cfg;
    uint32_t seed;
    uint32_t good_plan;
    uint32_t gen_cnt;
    uint32_t bad_plan;
    uint32_t wait_timer;
    uint32_t gen_ctrl;
    uint32_t gen_status;
    uint32_t chk_ctrl;
    uint32_t prod_status;
    uint32_t good_rx;
    uint32_t bad_rx;
    uint32_t rxer;
    uint8_t da[6];
    uint8_t sa[6];

    if ((shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_INTERCEPT_CONFIG, &intercept) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_PREAMBLE_IPG_SIZE, &preamble_ipg) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_ETHER_TYPE, &etype) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_PAYLOAD_CONFIG, &payload_cfg) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_PAYLOAD_SIZE, &payload_size) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_PRBS_DATA_CONFIG, &prbs_cfg) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_LFSR_SEED, &seed) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_GOOD_FRAMES_PLAN, &good_plan) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_G_GOOD_FRAME_CNT, &gen_cnt) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_BAD_FRAMES_PLAN, &bad_plan) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_WAIT_TIMER, &wait_timer) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_GEN_CTRL, &gen_ctrl) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_GEN_STATUS, &gen_status) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_CHECK_CTRL, &chk_ctrl) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_PROD_STATUS, &prod_status) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_R_GOOD_FRAME_CNT, &good_rx) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_R_BAD_FRAME_CNT, &bad_rx) != USER_PHY_STATUS_OK) ||
     (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_R_RXER_FRAME_CNT, &rxer) != USER_PHY_STATUS_OK) ||
     (shell_read_bist_mac(USER_PHY_VEND1_BIST_DA_0, da) != USER_PHY_STATUS_OK) ||
     (shell_read_bist_mac(USER_PHY_VEND1_BIST_SA_0, sa) != USER_PHY_STATUS_OK))
    {
     printf("bist show failed\r\n");
     return -1;
    }

    shell_bist_print_route(intercept);
    printf("gen: ctrl=0x%04lX status=0x%04lX check=0x%04lX prod=0x%04lX\r\n",
        (unsigned long)gen_ctrl,
        (unsigned long)gen_status,
        (unsigned long)chk_ctrl,
        (unsigned long)prod_status);
    printf("preamble=%lu ipg_type=%lu ipg_len=%lu etype=0x%04lX\r\n",
        (unsigned long)((preamble_ipg & USER_PHY_VEND1_BIST_PREAMBLE_LENGTH_MASK) >> USER_PHY_VEND1_BIST_PREAMBLE_LENGTH_SHIFT),
        (unsigned long)((preamble_ipg & USER_PHY_VEND1_BIST_IPG_TYPE_MASK) >> USER_PHY_VEND1_BIST_IPG_TYPE_SHIFT),
        (unsigned long)(preamble_ipg & USER_PHY_VEND1_BIST_IPG_LENGTH_MASK),
        (unsigned long)etype);
    printf("payload_cfg=0x%04lX data_type=%lu size_type=%lu fixed_byte=0x%02lX payload_size=0x%04lX\r\n",
        (unsigned long)payload_cfg,
        (unsigned long)((payload_cfg & USER_PHY_VEND1_BIST_PAYLOAD_DATA_TYPE_MASK) >> USER_PHY_VEND1_BIST_PAYLOAD_DATA_TYPE_SHIFT),
        (unsigned long)((payload_cfg & USER_PHY_VEND1_BIST_PAYLOAD_SIZE_TYPE_MASK) >> USER_PHY_VEND1_BIST_PAYLOAD_SIZE_TYPE_SHIFT),
        (unsigned long)(payload_cfg & USER_PHY_VEND1_BIST_FIXED_PAYLOAD_DATA_MASK),
        (unsigned long)(payload_size & USER_PHY_VEND1_BIST_PAYLOAD_SIZE_MASK));
    printf("prbs_cfg=0x%04lX prbs=%lu seedmode=%s seed=0x%04lX good=%lu bad=%lu wait_us=%lu gen_count=%lu\r\n",
        (unsigned long)prbs_cfg,
        (unsigned long)((prbs_cfg & USER_PHY_VEND1_BIST_PRBS_SELECT_MASK) >> USER_PHY_VEND1_BIST_PRBS_SELECT_SHIFT),
        ((prbs_cfg & USER_PHY_VEND1_BIST_LFSR_SEED_USAGE) != 0U) ? "running" : "perframe",
        (unsigned long)(seed & USER_PHY_VEND1_BIST_LFSR_SEED_MASK),
        (unsigned long)good_plan,
        (unsigned long)bad_plan,
        (unsigned long)wait_timer,
        (unsigned long)gen_cnt);
    printf("rx_good=%lu rx_bad=%lu rxer=%lu\r\n",
        (unsigned long)good_rx,
        (unsigned long)bad_rx,
        (unsigned long)rxer);
    shell_print_mac48("DA", da);
    shell_print_mac48("SA", sa);
    return 0;
}

static int cmd_bist(int argc, char *argv[])
{
    uint32_t value;
    int32_t status;

    if (argc < 2)
    {
        printf("usage: bist status|cfg ...|genstart prod|cont|genstop|chkstart prod|cont|chkstop|rxstart|rxstop|clr\r\n");
        return -1;
    }

    if ((strcmp(argv[1], "status") == 0) || (strcmp(argv[1], "show") == 0))
    {
        return shell_bist_show_config();
    }

    if (strcmp(argv[1], "cfg") == 0)
    {
        if (argc < 3)
        {
            printf("usage: bist cfg show|check xmii|ephy|tx normal|ephy|rx normal|xmii|da <mac>|sa <mac>|etype <hex>|payload fixed <byte>|ramp <byte>|prbs|sizemode fixed|inc|random|psize <value>|prbs 7|9|11|15|seed <hex>|seedmode perframe|running|good <count>|bad <count>|wait <usec>|ipg fixed|inc|random <len>|preamble <len>\r\n");
            return -1;
        }

        status = shell_bist_enable_path();
        if (status != USER_PHY_STATUS_OK)
        {
            printf("BIST enable path failed: %ld\r\n", (long)status);
            return -1;
        }

        if (strcmp(argv[2], "show") == 0)
        {
            return shell_bist_show_config();
        }
        else if ((strcmp(argv[2], "check") == 0) && (argc >= 4))
        {
            uint16_t set_mask;
            if (strcmp(argv[3], "ephy") == 0)
            {
                set_mask = USER_PHY_VEND1_BIST_INTERCEPT_CHECK_EPHY;
            }
            else if (strcmp(argv[3], "xmii") == 0)
            {
                set_mask = USER_PHY_VEND1_BIST_INTERCEPT_CHECK_XMII;
            }
            else
            {
                printf("usage: bist cfg check xmii|ephy\r\n");
                return -1;
            }
            status = shell_bist_set_intercept(USER_PHY_VEND1_BIST_INTERCEPT_CHECK_EPHY, set_mask);
        }
        else if ((strcmp(argv[2], "tx") == 0) && (argc >= 4))
        {
            uint16_t set_mask;
            if (strcmp(argv[3], "ephy") == 0)
            {
                set_mask = USER_PHY_VEND1_BIST_INTERCEPT_TX_EPHY;
            }
            else if (strcmp(argv[3], "normal") == 0)
            {
                set_mask = USER_PHY_VEND1_BIST_INTERCEPT_TX_NORMAL;
            }
            else
            {
                printf("usage: bist cfg tx normal|ephy\r\n");
                return -1;
            }
            status = shell_bist_set_intercept(USER_PHY_VEND1_BIST_INTERCEPT_TX_EPHY, set_mask);
        }
        else if ((strcmp(argv[2], "rx") == 0) && (argc >= 4))
        {
            uint16_t set_mask;
            if (strcmp(argv[3], "xmii") == 0)
            {
                set_mask = USER_PHY_VEND1_BIST_INTERCEPT_RX_XMII;
            }
            else if (strcmp(argv[3], "normal") == 0)
            {
                set_mask = USER_PHY_VEND1_BIST_INTERCEPT_RX_NORMAL;
            }
            else
            {
                printf("usage: bist cfg rx normal|xmii\r\n");
                return -1;
            }
            status = shell_bist_set_intercept(USER_PHY_VEND1_BIST_INTERCEPT_RX_XMII, set_mask);
        }
        else if (((strcmp(argv[2], "da") == 0) || (strcmp(argv[2], "sa") == 0)) && (argc >= 4))
        {
            uint8_t mac[6];
            if (shell_parse_mac48(argv[3], mac) != 0)
            {
                printf("invalid mac, use 02:00:00:00:00:01\r\n");
                return -1;
            }
            status = shell_write_bist_mac((strcmp(argv[2], "da") == 0) ? USER_PHY_VEND1_BIST_DA_0 : USER_PHY_VEND1_BIST_SA_0, mac);
        }
        else if ((strcmp(argv[2], "etype") == 0) && (argc >= 4))
        {
            if (shell_parse_u32(argv[3], &value) != 0)
            {
                printf("usage: bist cfg etype <hex>\r\n");
                return -1;
            }
            status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_ETHER_TYPE, (uint16_t)value);
        }
        else if ((strcmp(argv[2], "payload") == 0) && (argc >= 4))
        {
            uint32_t payload_cfg;
            uint16_t data_type;
            if (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_PAYLOAD_CONFIG, &payload_cfg) != USER_PHY_STATUS_OK)
            {
                printf("payload cfg read failed\r\n");
                return -1;
            }
            if (strcmp(argv[3], "fixed") == 0)
            {
                data_type = USER_PHY_VEND1_BIST_PAYLOAD_DATA_FIXED;
            }
            else if (strcmp(argv[3], "ramp") == 0)
            {
                data_type = USER_PHY_VEND1_BIST_PAYLOAD_DATA_RAMP;
            }
            else if (strcmp(argv[3], "prbs") == 0)
            {
                data_type = USER_PHY_VEND1_BIST_PAYLOAD_DATA_PRBS;
            }
            else
            {
                printf("usage: bist cfg payload fixed <byte>|ramp <byte>|prbs\r\n");
                return -1;
            }

            payload_cfg &= (uint32_t)(~(USER_PHY_VEND1_BIST_PAYLOAD_DATA_TYPE_MASK | USER_PHY_VEND1_BIST_FIXED_PAYLOAD_DATA_MASK));
            payload_cfg |= data_type;
            if ((argc >= 5) && (strcmp(argv[3], "prbs") != 0))
            {
                if ((shell_parse_u32(argv[4], &value) != 0) || (value > 0xFFU))
                {
                    printf("payload byte 0-255\r\n");
                    return -1;
                }
                payload_cfg |= (value & USER_PHY_VEND1_BIST_FIXED_PAYLOAD_DATA_MASK);
            }
            status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_PAYLOAD_CONFIG, (uint16_t)payload_cfg);
        }
        else if ((strcmp(argv[2], "sizemode") == 0) && (argc >= 4))
        {
            uint16_t set_mask;
            if (strcmp(argv[3], "fixed") == 0)
            {
                set_mask = USER_PHY_VEND1_BIST_PAYLOAD_SIZE_FIXED;
            }
            else if (strcmp(argv[3], "inc") == 0)
            {
                set_mask = USER_PHY_VEND1_BIST_PAYLOAD_SIZE_INCREMENT;
            }
            else if (strcmp(argv[3], "random") == 0)
            {
                set_mask = USER_PHY_VEND1_BIST_PAYLOAD_SIZE_RANDOM;
            }
            else
            {
                printf("usage: bist cfg sizemode fixed|inc|random\r\n");
                return -1;
            }
            status = shell_modify_c45(USER_PHY_DEVAD_VEND1,
                                      USER_PHY_VEND1_BIST_PAYLOAD_CONFIG,
                                      USER_PHY_VEND1_BIST_PAYLOAD_SIZE_TYPE_MASK,
                                      set_mask);
        }
        else if ((strcmp(argv[2], "psize") == 0) && (argc >= 4))
        {
            if ((shell_parse_u32(argv[3], &value) != 0) || (value > USER_PHY_VEND1_BIST_PAYLOAD_SIZE_MASK))
            {
                printf("payload size 0-0x3FFF\r\n");
                return -1;
            }
            status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_PAYLOAD_SIZE, (uint16_t)value);
        }
        else if ((strcmp(argv[2], "prbs") == 0) && (argc >= 4))
        {
            uint16_t set_mask;
            if (strcmp(argv[3], "7") == 0)
            {
                set_mask = USER_PHY_VEND1_BIST_PRBS_SELECT_7;
            }
            else if (strcmp(argv[3], "9") == 0)
            {
                set_mask = USER_PHY_VEND1_BIST_PRBS_SELECT_9;
            }
            else if (strcmp(argv[3], "11") == 0)
            {
                set_mask = USER_PHY_VEND1_BIST_PRBS_SELECT_11;
            }
            else if (strcmp(argv[3], "15") == 0)
            {
                set_mask = USER_PHY_VEND1_BIST_PRBS_SELECT_15;
            }
            else
            {
                printf("usage: bist cfg prbs 7|9|11|15\r\n");
                return -1;
            }
            status = shell_modify_c45(USER_PHY_DEVAD_VEND1,
                                      USER_PHY_VEND1_BIST_PRBS_DATA_CONFIG,
                                      USER_PHY_VEND1_BIST_PRBS_SELECT_MASK,
                                      set_mask);
        }
        else if ((strcmp(argv[2], "seed") == 0) && (argc >= 4))
        {
            if ((shell_parse_u32(argv[3], &value) != 0) || (value == 0U) || (value > USER_PHY_VEND1_BIST_LFSR_SEED_MASK))
            {
                printf("seed 1-0x7FFF\r\n");
                return -1;
            }
            status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_LFSR_SEED, (uint16_t)value);
        }
        else if ((strcmp(argv[2], "seedmode") == 0) && (argc >= 4))
        {
            uint16_t set_mask = 0U;
            if (strcmp(argv[3], "running") == 0)
            {
                set_mask = USER_PHY_VEND1_BIST_LFSR_SEED_USAGE;
            }
            else if (strcmp(argv[3], "perframe") != 0)
            {
                printf("usage: bist cfg seedmode perframe|running\r\n");
                return -1;
            }
            status = shell_modify_c45(USER_PHY_DEVAD_VEND1,
                                      USER_PHY_VEND1_BIST_PRBS_DATA_CONFIG,
                                      USER_PHY_VEND1_BIST_LFSR_SEED_USAGE,
                                      set_mask);
        }
        else if ((strcmp(argv[2], "good") == 0) && (argc >= 4))
        {
            if ((shell_parse_u32(argv[3], &value) != 0) || (value > 0xFFFFU))
            {
                printf("good frames 0-65535\r\n");
                return -1;
            }
            status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_GOOD_FRAMES_PLAN, (uint16_t)value);
        }
        else if ((strcmp(argv[2], "bad") == 0) && (argc >= 4))
        {
            if ((shell_parse_u32(argv[3], &value) != 0) || (value > 0xFFFFU))
            {
                printf("bad frames 0-65535\r\n");
                return -1;
            }
            status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_BAD_FRAMES_PLAN, (uint16_t)value);
        }
        else if ((strcmp(argv[2], "wait") == 0) && (argc >= 4))
        {
            if ((shell_parse_u32(argv[3], &value) != 0) || (value > 0xFFFFU))
            {
                printf("wait 0-65535 us\r\n");
                return -1;
            }
            status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_WAIT_TIMER, (uint16_t)value);
        }
        else if ((strcmp(argv[2], "ipg") == 0) && (argc >= 5))
        {
            uint32_t reg;
            uint16_t set_mask;
            if ((shell_parse_u32(argv[4], &value) != 0) || (value > 0xFFU))
            {
                printf("ipg length 0-255\r\n");
                return -1;
            }
            if (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_PREAMBLE_IPG_SIZE, &reg) != USER_PHY_STATUS_OK)
            {
                printf("ipg cfg read failed\r\n");
                return -1;
            }
            if (strcmp(argv[3], "fixed") == 0)
            {
                set_mask = 0U;
            }
            else if (strcmp(argv[3], "inc") == 0)
            {
                set_mask = (uint16_t)(1U << USER_PHY_VEND1_BIST_IPG_TYPE_SHIFT);
            }
            else if (strcmp(argv[3], "random") == 0)
            {
                set_mask = (uint16_t)(2U << USER_PHY_VEND1_BIST_IPG_TYPE_SHIFT);
            }
            else
            {
                printf("usage: bist cfg ipg fixed|inc|random <len>\r\n");
                return -1;
            }
            reg &= (uint32_t)(~(USER_PHY_VEND1_BIST_IPG_TYPE_MASK | USER_PHY_VEND1_BIST_IPG_LENGTH_MASK));
            reg |= (uint32_t)(set_mask | (uint16_t)value);
            status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_PREAMBLE_IPG_SIZE, (uint16_t)reg);
        }
        else if ((strcmp(argv[2], "preamble") == 0) && (argc >= 4))
        {
            if ((shell_parse_u32(argv[3], &value) != 0) || (value > 0xFU))
            {
                printf("preamble 0-15\r\n");
                return -1;
            }
            status = shell_modify_c45(USER_PHY_DEVAD_VEND1,
                                      USER_PHY_VEND1_BIST_PREAMBLE_IPG_SIZE,
                                      USER_PHY_VEND1_BIST_PREAMBLE_LENGTH_MASK,
                                      (uint16_t)(value << USER_PHY_VEND1_BIST_PREAMBLE_LENGTH_SHIFT));
        }
        else
        {
            printf("unknown bist cfg field\r\n");
            return -1;
        }

        if (status != USER_PHY_STATUS_OK)
        {
            printf("bist cfg failed: %ld\r\n", (long)status);
            return -1;
        }

        return shell_bist_show_config();
    }

    status = shell_bist_enable_path();
    if (status != USER_PHY_STATUS_OK)
    {
        printf("BIST enable path failed: %ld\r\n", (long)status);
        return -1;
    }

    if (strcmp(argv[1], "genstart") == 0)
    {
        if (argc < 3)
        {
            printf("usage: bist genstart prod|cont\r\n");
            return -1;
        }

        if (strcmp(argv[2], "prod") == 0)
        {
            status = shell_write_c45(USER_PHY_DEVAD_VEND1,
                                     USER_PHY_VEND1_BIST_GEN_CTRL,
                                     USER_PHY_VEND1_BIST_GEN_ENABLE);
        }
        else if (strcmp(argv[2], "cont") == 0)
        {
            status = shell_write_c45(USER_PHY_DEVAD_VEND1,
                                     USER_PHY_VEND1_BIST_GEN_CTRL,
                                     (uint16_t)(USER_PHY_VEND1_BIST_GEN_ENABLE |
                                                USER_PHY_VEND1_BIST_GEN_CONTINUOUS));
        }
        else
        {
            printf("usage: bist genstart prod|cont\r\n");
            return -1;
        }
    }
    else if (strcmp(argv[1], "genstop") == 0)
    {
        status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_GEN_CTRL, USER_PHY_VEND1_BIST_GEN_STOP);
    }
    else if (strcmp(argv[1], "chkstart") == 0)
    {
        if (argc < 3)
        {
            printf("usage: bist chkstart prod|cont\r\n");
            return -1;
        }

        if (strcmp(argv[2], "prod") == 0)
        {
            status = shell_write_c45(USER_PHY_DEVAD_VEND1,
                                     USER_PHY_VEND1_BIST_CHECK_CTRL,
                                     USER_PHY_VEND1_BIST_CHECK_ENABLE);
        }
        else if (strcmp(argv[2], "cont") == 0)
        {
            status = shell_write_c45(USER_PHY_DEVAD_VEND1,
                                     USER_PHY_VEND1_BIST_CHECK_CTRL,
                                     (uint16_t)(USER_PHY_VEND1_BIST_CHECK_ENABLE |
                                                USER_PHY_VEND1_BIST_CHECK_CONTINUOUS));
        }
        else
        {
            printf("usage: bist chkstart prod|cont\r\n");
            return -1;
        }
    }
    else if (strcmp(argv[1], "chkstop") == 0)
    {
        status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_CHECK_CTRL, 0U);
    }
    else if (strcmp(argv[1], "rxstart") == 0)
    {
        status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_INTERCEPT_CONFIG,
                                 (uint16_t)(USER_PHY_VEND1_BIST_INTERCEPT_CHECK_EPHY |
                                            USER_PHY_VEND1_BIST_INTERCEPT_TX_EPHY |
                                            USER_PHY_VEND1_BIST_INTERCEPT_RX_XMII));
        if (status == USER_PHY_STATUS_OK)
        {
            status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_CHECK_CTRL,
                                     (uint16_t)(USER_PHY_VEND1_BIST_CHECK_ENABLE |
                                                USER_PHY_VEND1_BIST_CHECK_CONTINUOUS));
        }
    }
    else if (strcmp(argv[1], "rxstop") == 0)
    {
        status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_CHECK_CTRL, 0U);
        if (status == USER_PHY_STATUS_OK)
        {
            status = shell_write_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_GEN_CTRL, USER_PHY_VEND1_BIST_GEN_STOP);
        }
    }
    else if (strcmp(argv[1], "clr") == 0)
    {
        if (shell_read_c45(USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_BIST_CHECK_CTRL, &value) != USER_PHY_STATUS_OK)
        {
            printf("bist clr read failed\r\n");
            return -1;
        }
        status = shell_write_c45(USER_PHY_DEVAD_VEND1,
                                 USER_PHY_VEND1_BIST_CHECK_CTRL,
                                 (uint16_t)(value | USER_PHY_VEND1_BIST_CHECK_STAT_RESET));
    }
    else
    {
        printf("usage: bist status|cfg ...|genstart prod|cont|genstop|chkstart prod|cont|chkstop|rxstart|rxstop|clr\r\n");
        return -1;
    }

    if (status != USER_PHY_STATUS_OK)
    {
        printf("bist command failed: %ld\r\n", (long)status);
        return -1;
    }

    printf("OK\r\n");
    return shell_bist_show_config();
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
    printf("\r\n=== STM32H723 + TJA1103 Shell ===\r\n");
    printf("Type 'help' for command list\r\n\r\n");
}

SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), mcu, cmd_mcu, show MCU info);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), uptime, cmd_uptime, show MCU uptime);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), reset, cmd_reset, reset MCU);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), phyinfo, cmd_phyinfo, show TJA1103 summary);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), role, cmd_role, show or set master slave role);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), master, cmd_master, set master preference or force);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), slave, cmd_slave, set slave preference or force);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), t1cfg, cmd_t1cfg, show or set common 100BASE T1 options);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), c45read, cmd_c45read, read Clause45 register);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), c45write, cmd_c45write, write Clause45 register);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), sqi, cmd_sqi, show signal quality indicator);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), mse, cmd_mse, show mean square error);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), cable, cmd_cable, run cable diagnosis);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), train, cmd_train, show training timers);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), symerr, cmd_symerr, show error counters);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), temp, cmd_temp, show temperature status);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), supply, cmd_supply, show supply status);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), ethdiag, cmd_ethdiag, show ETH DMA and RX pool state);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), wake, cmd_wake, show or clear wake sleep status);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), wakecfg, cmd_wakecfg, configure wake sleep mode);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), wakeparam, cmd_wakeparam, configure wake sleep timers);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), sleep, cmd_sleep, low power control and start operation);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), loopback, cmd_loopback, control diagnostic loopback);
SHELL_EXPORT_CMD(SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN), bist, cmd_bist, BIST generator checker control);