/**
  ******************************************************************************
  * @file    lan9370_reg.h
  * @brief   LAN9370 Register Definitions
  * @details Based on Microchip LAN937x specifications and Linux kernel drivers
  ******************************************************************************
  */

#ifndef __LAN9370_REG_H
#define __LAN9370_REG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* =============================================================================
 * SPI Access Commands
 * ===========================================================================*/
#define LAN9370_SPI_CMD_READ        0x03
#define LAN9370_SPI_CMD_WRITE       0x02

/* =============================================================================
 * Chip ID and Global Registers
 * ===========================================================================*/
#define REG_CHIP_ID0                0x0000
#define REG_CHIP_ID1                0x0001
#define REG_CHIP_ID2                0x0002
#define REG_CHIP_ID3                0x0003

#define CHIP_ID_9370                0x00937000
#define CHIP_ID_9371                0x00937100
#define CHIP_ID_9372                0x00937200
#define CHIP_ID_9373                0x00937300
#define CHIP_ID_9374                0x00937400

/* Global Control Registers */
#define REG_GLOBAL_CTRL_0           0x0007
#define SW_PHY_REG_BLOCK            (1 << 7)
#define SW_FAST_MODE                (1 << 3)
#define SW_FLUSH_DYN_MAC_TABLE      (1 << 2)
#define SW_FLUSH_STA_MAC_TABLE      (1 << 1)

#define REG_SW_RESET                0x0043
#define SW_SOFT_RESET               (1 << 1)
#define SW_GLOBAL_RESET             (1 << 0)

#define REG_SW_PORT_INT_STATUS      0x0018
#define REG_SW_PORT_INT_MASK        0x001C

/* LUE (Learning and Unicast Engine) Control */
#define REG_LUE_CTRL_0             0x0310
#define SW_VLAN_ENABLE              (1 << 7)
#define SW_DROP_INVALID_VID         (1 << 6)
#define SW_AGE_CNT_M                0x7
#define SW_AGE_CNT_S                3
#define SW_RESV_MCAST_ENABLE        (1 << 2)
#define LUE_UNICAST_LEARNING        (1 << 0)

/* VPHY Indirect Access Registers */
#define REG_VPHY_IND_ADDR__2        0x075C
#define REG_VPHY_IND_DATA__2        0x0760
#define REG_VPHY_IND_CTRL__2        0x0768
#define VPHY_IND_WRITE              (1 << 1)
#define VPHY_IND_BUSY               (1 << 0)

#define REG_VPHY_SPECIAL_CTRL__2    0x077C
#define VPHY_SPI_INDIRECT_ENABLE    (1 << 12)

/* =============================================================================
 * Port Registers (Per-port, Port 1-5)
 * Port 1-4: 100BASE-T1 PHY ports
 * Port 5: RGMII/RMII Host port
 * ===========================================================================*/
#define PORT_CTRL_ADDR(port, addr)  ((addr) | (((port) + 1) << 12))

/* Port Default Registers */
#define REG_PORTn_DEFAULT_TAG0      0x0000
#define REG_PORTn_DEFAULT_TAG1      0x0001
#define REG_PORTn_DEFAULT_VLAN      0x0004

/* Port Operation Control */
#define REG_PORTn_OP_CTRL0          0x0020
#define PORT_MAC_LOOPBACK           (1 << 7)
#define PORT_FORCE_TX_FLOW_CTRL     (1 << 4)
#define PORT_FORCE_RX_FLOW_CTRL     (1 << 3)
#define PORT_TAIL_TAG_ENABLE        (1 << 2)
#define PORT_QUEUE_SPLIT_MASK       0x03

/* Port Status */
#define REG_PORTn_STATUS            0x0030
#define PORT_INTF_SPEED_MASK        (3 << 3)
#define PORT_INTF_SPEED_10          (0 << 3)
#define PORT_INTF_SPEED_100         (1 << 3)
#define PORT_INTF_SPEED_1000        (2 << 3)
#define PORT_INTF_FULL_DUPLEX       (1 << 2)
#define PORT_TX_FLOW_CTRL_EN        (1 << 1)
#define PORT_RX_FLOW_CTRL_EN        (1 << 0)

/* XMII Control */
#define REG_PORTn_XMII_CTRL0        0x0300
#define PORT_SGMII_EN               (1 << 7)
#define PORT_MII_SEL_EDGE           (1 << 5)
#define PORT_RGMII_ID_IG_EN         (1 << 4)
#define PORT_RGMII_ID_EG_EN         (1 << 3)
#define PORT_MII_MODE_MASK          (3 << 0)
#define PORT_MII_MODE_RGMII         (0 << 0)
#define PORT_RMII_MODE_RMII         (1 << 0)
#define PORT_MII_MODE_MII           (2 << 0)

#define REG_PORTn_XMII_CTRL1        0x0301
#define PORT_RMII_CLK_SEL           (1 << 7)
#define PORT_MII_NOT_1GBIT          (1 << 6)
#define PORT_MII_SEL_M              (3 << 4)
#define PORT_RGMII_SEL_M            (3 << 2)
#define PORT_RMII_SEL_M             (3 << 0)

/* Port MAC Control */
#define REG_PORTn_MAC_CTRL0         0x0400
#define PORT_BROADCAST_STORM        (1 << 1)
#define PORT_DIFFSERV_PRIO_EN       (1 << 0)

/* Port VLAN membership */
#define REG_PORT_VLAN_MEMBERSHIP__4 0x0A04

/* Port MIB counters */
#define REG_PORT_MIB_CTRL_STAT__4   0x0500
#define REG_PORT_MIB_DATA           0x0504
#define MIB_COUNTER_READ            (1UL << 25)
#define MIB_COUNTER_FLUSH_FREEZE    (1UL << 24)
#define MIB_COUNTER_INDEX_S         16

#define LAN9370_MIB_RX_TOTAL_IDX    0x80
#define LAN9370_MIB_TX_TOTAL_IDX    0x81
#define LAN9370_MIB_RX_DROP_IDX     0x82
#define LAN9370_MIB_TX_DROP_IDX     0x83

/* Port MSTP State */
#define REG_PORTn_MSTP_STATE        0x0B04
#define PORT_TX_ENABLE              (1 << 2)
#define PORT_RX_ENABLE              (1 << 1)
#define PORT_LEARN_DISABLE          (1 << 0)

/* Per-port T1 PHY register window base for indirect VPHY access */
#define REG_PORT_T1_PHY_CTRL_BASE   0x0100

/* =============================================================================
 * T1 PHY Registers (Per-port, for 100BASE-T1 ports)
 * ===========================================================================*/
#define LAN9370_PORTn_T1_PHY_REG(port, addr)  (0x0100 + ((port) * 0x1000) + ((addr) * 4))

/* Basic Control (Address 0x00) */
#define T1_PHY_BASIC_CTRL           0x00
#define T1_PHY_RESET                (1 << 15)
#define T1_PHY_LOOPBACK             (1 << 14)
#define T1_PHY_SPEED_SEL_LSB        (1 << 13)
#define T1_PHY_AN_ENABLE            (1 << 12)
#define T1_PHY_POWER_DOWN           (1 << 11)
#define T1_PHY_ISOLATE              (1 << 10)
#define T1_PHY_RESTART_AN           (1 << 9)
#define T1_PHY_DUPLEX_MODE          (1 << 8)

/* Basic Status (Address 0x01) */
#define T1_PHY_BASIC_STATUS         0x01
#define T1_PHY_LINK_STATUS          (1 << 2)

/* PHY ID */
#define T1_PHY_ID1                  0x02
#define T1_PHY_ID2                  0x03

/* Auto-Negotiation Advertisement */
#define T1_PHY_AN_ADV               0x04
#define T1_PHY_AN_LP_ABILITY        0x05

/* T1 Specific Registers */
#define T1_PHY_MASTER_SLAVE_CTRL    0x09
#define T1_PHY_MS_CFG_ENABLE        (1 << 12)
#define T1_PHY_MS_CFG_VALUE         (1 << 11)  /* 1=Master, 0=Slave */
#define T1_PHY_PORT_TYPE            (1 << 10)

#define T1_PHY_MASTER_SLAVE_STATUS  0x0A
#define T1_PHY_MS_CFG_FAULT         (1 << 15)
#define T1_PHY_MS_CFG_RESOLUTION    (1 << 14)
#define T1_PHY_LOCAL_RCVR_STATUS    (1 << 13)
#define T1_PHY_REMOTE_RCVR_STATUS   (1 << 12)

/* T1 PHY Diagnostics */
#define T1_PHY_SQI                  0x1B  /* Signal Quality Indicator */
#define T1_PHY_MSE                  0x1C  /* Mean Square Error */
#define T1_PHY_CABLE_DIAG           0x1A  /* Cable diagnostic (short/open) */

/* =============================================================================
 * LED Control Registers (pins 47-50: LED_1..LED_4)
 * ===========================================================================*/
#define REG_LED_CTRL0               0x0E00
#define REG_LED_CTRL1               0x0E01
#define REG_LED_BLINK_CTRL          0x0E06  /* Blink rate control */

/* LED mode bits (in LED_CTRL0) */
#define LED_MODE_ACTIVITY           0x01  /* Blink on TX/RX activity */
#define LED_MODE_LINK100            0x02  /* On when 100M link */
#define LED_MODE_LINK_ACT           0x03  /* On=link, Blink=activity */
#define LED_MODE_FORCE_ON           0x07  /* Force LED on */
#define LED_MODE_FORCE_OFF          0x00  /* Force LED off */

/* =============================================================================
 * Temperature Monitor Registers
 * ===========================================================================*/
#define REG_TEMP_MON                0x0E10  /* Temperature monitor ADC value */
#define REG_TEMP_CFG                0x0E12  /* Temperature monitor config */

/* Port Mirroring (per KSZ9477/LAN937x register map) */
#define REG_PORT_MRI_MIRROR_CTRL    0x0800
#define PORT_MIRROR_RX              (1 << 6)
#define PORT_MIRROR_TX              (1 << 5)
#define PORT_MIRROR_SNIFFER         (1 << 1)

#define REG_SW_MRI_CTRL_0           0x0370
#define SW_MIRROR_RX_TX             (1 << 0)

/* PTP / gPTP (per KSZ9477/LAN937x PTP block at 0x0500 region) */
#define REG_PTP_MSG_CONF1           0x0514
#define PTP_ENABLE                  (1 << 6)
#define PTP_802_1AS                 (1 << 7)

/* VLAN per-port lookup control */
#define REG_PORT_LUE_CTRL           0x0B00
#define PORT_VLAN_LOOKUP_VID_0      (1 << 7)

/* Port Default VLAN ID (16-bit, per-port offset 0x0000) */
#define REG_PORT_DEFAULT_VID        0x0000

/* VLAN Table (per KSZ9477 / LAN937x) */
#define REG_SW_VLAN_ENTRY__4        0x0400
#define REG_SW_VLAN_ENTRY_UNTAG__4  0x0404
#define REG_SW_VLAN_ENTRY_PORTS__4  0x0408
#define REG_SW_VLAN_ENTRY_INDEX__2  0x040C
#define VLAN_INDEX_M                0x0FFF

#define REG_SW_VLAN_CTRL             0x040E
#define VLAN_START                  (1 << 7)
#define VLAN_ACTION                 0x3
#define VLAN_WRITE                  1
#define VLAN_READ                   2
#define VLAN_CLEAR                  3

#define VLAN_VALID                  (1UL << 31)

/* =============================================================================
 * Switch Monitoring / Statistics
 * ===========================================================================*/
#define REG_GLOBAL_MIB_CTRL         0x04F0

#define LAN9370_PORT_COUNT          5

#ifdef __cplusplus
}
#endif

#endif /* __LAN9370_REG_H */
