/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    eth_custom_phy_interface.h
  * @author  MCD Application Team
  * @brief   This file contains all the functions prototypes for the
  *          eth_custom_phy_interface.c PHY driver.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2014 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef ETH_CUSTOM_PHY_INTERFACE_H
#define ETH_CUSTOM_PHY_INTERFACE_H

#ifdef   __cplusplus
extern   "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/** @addtogroup BSP
  * @{
  */

/** @addtogroup Component
  * @{
  */

/** @defgroup USER_PHY
  * @{
  */
/* Exported constants --------------------------------------------------------*/
/** @defgroup USER_PHY_Exported_Constants USER_PHY Exported Constants
  * @{
  */

/** @defgroup USER_PHY_Registers_Mapping USER_PHY Registers Mapping
  * @{
  */
#define USER_PHY_BCR                              ((uint16_t)0x0000U)
#define USER_PHY_BSR                              ((uint16_t)0x0001U)
#define USER_PHY_PHYI1R                           ((uint16_t)0x0002U)
#define USER_PHY_PHYI2R                           ((uint16_t)0x0003U)
#define USER_PHY_MMD_CTRL                         ((uint16_t)0x000DU)
#define USER_PHY_MMD_DATA                         ((uint16_t)0x000EU)

#define USER_PHY_DEVAD_PMAPMD                     ((uint16_t)0x0001U)
#define USER_PHY_DEVAD_AN                         ((uint16_t)0x0007U)
#define USER_PHY_DEVAD_VEND1                      ((uint16_t)0x001EU)

#define USER_PHY_MDIO_CTRL1                       ((uint16_t)0x0000U)
#define USER_PHY_MDIO_CTRL2                       ((uint16_t)0x0007U)
#define USER_PHY_MDIO_STAT1                       ((uint16_t)0x0001U)
#define USER_PHY_MDIO_STAT2                       ((uint16_t)0x0008U)
#define USER_PHY_MDIO_PMA_RXDET                   ((uint16_t)0x000AU)
#define USER_PHY_MDIO_AN_CTRL1                    ((uint16_t)0x0200U)
#define USER_PHY_MDIO_AN_STAT1                    ((uint16_t)0x0201U)
#define USER_PHY_MDIO_AN_T1_ADV_L                 ((uint16_t)0x0202U)
#define USER_PHY_MDIO_AN_T1_ADV_M                 ((uint16_t)0x0203U)
#define USER_PHY_MDIO_PMA_PMD_BT1_CTRL            ((uint16_t)0x0834U)

#define USER_PHY_DEVAD_PCS                        ((uint16_t)0x0003U)

#define USER_PHY_VEND1_DEVICE_CONTROL             ((uint16_t)0x0040U)
#define USER_PHY_VEND1_ALWAYS_ACCESSIBLE          ((uint16_t)0x801FU)
#define USER_PHY_VEND1_PORT_CONTROL               ((uint16_t)0x8040U)
#define USER_PHY_VEND1_PORT_FUNC_ENABLES          ((uint16_t)0x8048U)
#define USER_PHY_VEND1_PHY_CONTROL                ((uint16_t)0x8100U)
#define USER_PHY_VEND1_PHY_STATUS                 ((uint16_t)0x8102U)
#define USER_PHY_VEND1_PHY_CONFIG                 ((uint16_t)0x8108U)
#define USER_PHY_VEND1_PHY_STATE                  ((uint16_t)0x810CU)
#define USER_PHY_VEND1_PHY_PARAMETERS             ((uint16_t)0x8110U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONTROL         ((uint16_t)0x8180U)
#define USER_PHY_VEND1_WAKE_SLEEP_STATUS          ((uint16_t)0x8181U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG          ((uint16_t)0x8182U)
#define USER_PHY_VEND1_WAKE_SLEEP_PARAMETERS      ((uint16_t)0x8184U)
#define USER_PHY_VEND1_SIGNAL_QUALITY             ((uint16_t)0x8320U)
#define USER_PHY_VEND1_MSE                        ((uint16_t)0x8324U)
#define USER_PHY_VEND1_MAX_MSE                    ((uint16_t)0x8325U)
#define USER_PHY_VEND1_CABLE_TEST                 ((uint16_t)0x8330U)
#define USER_PHY_VEND1_LINK_TRAINING_TIMER        ((uint16_t)0x8340U)
#define USER_PHY_VEND1_LOC_RCVR_STATUS_TIMER      ((uint16_t)0x8341U)
#define USER_PHY_VEND1_REM_RCVR_STATUS_TIMER      ((uint16_t)0x8342U)
#define USER_PHY_VEND1_FOLLOWER_SILENT_TIMER      ((uint16_t)0x8343U)
#define USER_PHY_VEND1_SYMBOL_ERROR_COUNTER       ((uint16_t)0x8350U)
#define USER_PHY_VEND1_ERROR_COUNTER_MISC         ((uint16_t)0x8352U)
#define USER_PHY_VEND1_LINK_DROP_COUNTER          ((uint16_t)0x8352U)
#define USER_PHY_VEND1_LINK_LOSSES_AND_FAILURES   ((uint16_t)0x8353U)
#define USER_PHY_VEND1_PORT_BIST_CONTROL          ((uint16_t)0xA800U)
#define USER_PHY_VEND1_BIST_INTERCEPT_CONFIG      ((uint16_t)0xA807U)
#define USER_PHY_VEND1_BIST_GEN_CTRL              ((uint16_t)0xA880U)
#define USER_PHY_VEND1_BIST_GEN_STATUS            ((uint16_t)0xA881U)
#define USER_PHY_VEND1_BIST_PREAMBLE_IPG_SIZE     ((uint16_t)0xA887U)
#define USER_PHY_VEND1_BIST_DA_0                  ((uint16_t)0xA888U)
#define USER_PHY_VEND1_BIST_DA_1                  ((uint16_t)0xA889U)
#define USER_PHY_VEND1_BIST_DA_2                  ((uint16_t)0xA88AU)
#define USER_PHY_VEND1_BIST_SA_0                  ((uint16_t)0xA88BU)
#define USER_PHY_VEND1_BIST_SA_1                  ((uint16_t)0xA88CU)
#define USER_PHY_VEND1_BIST_SA_2                  ((uint16_t)0xA88DU)
#define USER_PHY_VEND1_BIST_PTP_CONFIG            ((uint16_t)0xA88FU)
#define USER_PHY_VEND1_BIST_ETHER_TYPE            ((uint16_t)0xA890U)
#define USER_PHY_VEND1_BIST_PAYLOAD_CONFIG        ((uint16_t)0xA891U)
#define USER_PHY_VEND1_BIST_PAYLOAD_SIZE          ((uint16_t)0xA892U)
#define USER_PHY_VEND1_BIST_PRBS_DATA_CONFIG      ((uint16_t)0xA893U)
#define USER_PHY_VEND1_BIST_LFSR_SEED             ((uint16_t)0xA894U)
#define USER_PHY_VEND1_BIST_GOOD_FRAMES_PLAN      ((uint16_t)0xA8A2U)
#define USER_PHY_VEND1_BIST_G_GOOD_FRAME_CNT      ((uint16_t)0xA8A4U)
#define USER_PHY_VEND1_BIST_BAD_FRAMES_PLAN       ((uint16_t)0xA8A6U)
#define USER_PHY_VEND1_BIST_CHECK_CTRL            ((uint16_t)0xA8C0U)
#define USER_PHY_VEND1_BIST_PROD_STATUS           ((uint16_t)0xA8C1U)
#define USER_PHY_VEND1_BIST_WAIT_TIMER            ((uint16_t)0xA8C4U)
#define USER_PHY_VEND1_BIST_R_GOOD_FRAME_CNT      ((uint16_t)0xA8D0U)
#define USER_PHY_VEND1_BIST_R_BAD_FRAME_CNT       ((uint16_t)0xA8D2U)
#define USER_PHY_VEND1_BIST_R_RXER_FRAME_CNT      ((uint16_t)0xA8D4U)
#define USER_PHY_VEND1_PORT_INFRA_CONTROL         ((uint16_t)0xAC00U)
#define USER_PHY_VEND1_INFRA_CONFIG               ((uint16_t)0xAC06U)
#define USER_PHY_VEND1_XMII_CONTROL               ((uint16_t)0xAFC0U)
#define USER_PHY_VEND1_PORT_PTP_CONTROL           ((uint16_t)0x9000U)
#define USER_PHY_VEND1_PTP_CONFIG                 ((uint16_t)0x1102U)
#define USER_PHY_VEND1_PTP_CLK_PERIOD             ((uint16_t)0x1104U)
#define USER_PHY_VEND1_PTP_RX_TS_METHOD           ((uint16_t)0x114DU)
#define USER_PHY_VEND1_GPIO_FUNC_CONFIG_BASE      ((uint16_t)0x2C40U)
#define USER_PHY_VEND1_MII_BASIC_CONFIG           ((uint16_t)0xAFC6U)
#define USER_PHY_VEND1_RX_PREAMBLE_COUNT          ((uint16_t)0xAFCEU)
#define USER_PHY_VEND1_TX_PREAMBLE_COUNT          ((uint16_t)0xAFCFU)
#define USER_PHY_VEND1_RX_IPG_LENGTH              ((uint16_t)0xAFD0U)
#define USER_PHY_VEND1_TX_IPG_LENGTH              ((uint16_t)0xAFD1U)
/**
  * @}
  */

/** @defgroup USER_PHY_BCR_Bit_Definition USER_PHY BCR Bit Definition
  * @{
  */
#define USER_PHY_BCR_SOFT_RESET                ((uint16_t)0x8000U)
#define USER_PHY_BCR_LOOPBACK                  ((uint16_t)0x4000U)
#define USER_PHY_BCR_SPEED_SELECT              ((uint16_t)0x2000U)
#define USER_PHY_BCR_AUTONEGO_EN               ((uint16_t)0x1000U)
#define USER_PHY_BCR_POWER_DOWN                ((uint16_t)0x0800U)
#define USER_PHY_BCR_ISOLATE                   ((uint16_t)0x0400U)
#define USER_PHY_BCR_RESTART_AUTONEGO          ((uint16_t)0x0200U)
#define USER_PHY_BCR_DUPLEX_MODE               ((uint16_t)0x0100U)
/**
  * @}
  */

/** @defgroup USER_PHY_BSR_Bit_Definition USER_PHY BSR Bit Definition
  * @{
  */
#define USER_PHY_BSR_100BASE_T4                ((uint16_t)0x8000U)
#define USER_PHY_BSR_100BASE_TX_FD             ((uint16_t)0x4000U)
#define USER_PHY_BSR_100BASE_TX_HD             ((uint16_t)0x2000U)
#define USER_PHY_BSR_10BASE_T_FD               ((uint16_t)0x1000U)
#define USER_PHY_BSR_10BASE_T_HD               ((uint16_t)0x0800U)
#define USER_PHY_BSR_100BASE_T2_FD             ((uint16_t)0x0400U)
#define USER_PHY_BSR_100BASE_T2_HD             ((uint16_t)0x0200U)
#define USER_PHY_BSR_EXTENDED_STATUS           ((uint16_t)0x0100U)
#define USER_PHY_BSR_AUTONEGO_CPLT             ((uint16_t)0x0020U)
#define USER_PHY_BSR_REMOTE_FAULT              ((uint16_t)0x0010U)
#define USER_PHY_BSR_AUTONEGO_ABILITY          ((uint16_t)0x0008U)
#define USER_PHY_BSR_LINK_STATUS               ((uint16_t)0x0004U)
#define USER_PHY_BSR_JABBER_DETECT             ((uint16_t)0x0002U)
#define USER_PHY_BSR_EXTENDED_CAP              ((uint16_t)0x0001U)
/**
  * @}
  */

/** @defgroup USER_PHY_C45_Bit_Definition USER_PHY Clause45 Bit Definition
  * @{
  */
#define USER_PHY_MMD_CTRL_ADDR                  ((uint16_t)0x0000U)
#define USER_PHY_MMD_CTRL_DATA_NO_POST_INC      ((uint16_t)0x4000U)

#define USER_PHY_MDIO_STAT1_LINK_STATUS         ((uint16_t)0x0004U)

#define USER_PHY_MDIO_CTRL1_SPEEDSEL            ((uint16_t)0x2040U)
#define USER_PHY_MDIO_CTRL1_SPEED100            ((uint16_t)0x2000U)
#define USER_PHY_MDIO_CTRL1_FULLDUPLEX          ((uint16_t)0x0100U)

#define USER_PHY_MDIO_PMA_CTRL2_TYPE_MASK       ((uint16_t)0x003FU)
#define USER_PHY_MDIO_PMA_CTRL2_100BTX          ((uint16_t)0x000EU)

#define USER_PHY_MDIO_AN_CTRL1_ENABLE           ((uint16_t)0x1000U)
#define USER_PHY_MDIO_AN_CTRL1_RESTART          ((uint16_t)0x0200U)
#define USER_PHY_MDIO_AN_STAT1_COMPLETE         ((uint16_t)0x0020U)

#define USER_PHY_MDIO_PMA_PMD_BT1_CTRL_STRAP    ((uint16_t)0x000FU)
#define USER_PHY_MDIO_PMA_PMD_BT1_CTRL_CFG_MST  ((uint16_t)0x4000U)
#define USER_PHY_MDIO_PMA_PMD_BT1_CTRL_MANUAL_LF ((uint16_t)0x8000U)

#define USER_PHY_MDIO_AN_T1_ADV_L_FORCE_MS      ((uint16_t)0x1000U)
#define USER_PHY_MDIO_AN_T1_ADV_M_100BT1        ((uint16_t)0x0020U)
#define USER_PHY_MDIO_AN_T1_ADV_M_MST           ((uint16_t)0x0010U)

#define USER_PHY_PMA_CONTROL1_LOW_POWER         ((uint16_t)0x0800U)
#define USER_PHY_PMA_CONTROL1_REMOTE_LOOPBACK   ((uint16_t)0x0002U)
#define USER_PHY_PMA_CONTROL1_LOCAL_LOOPBACK    ((uint16_t)0x0001U)

#define USER_PHY_PCS_CONTROL1_LOW_POWER         ((uint16_t)0x0800U)

#define USER_PHY_VEND1_DEVICE_CONTROL_GLOBAL_EN ((uint16_t)0x4000U)
#define USER_PHY_VEND1_DEVICE_CONTROL_ALL_EN    ((uint16_t)0x2000U)
#define USER_PHY_VEND1_ALWAYS_ACCESSIBLE_FUSA_PASS ((uint16_t)0x0010U)
#define USER_PHY_VEND1_ALWAYS_ACCESSIBLE_WU_SMI ((uint16_t)0x8000U)
#define USER_PHY_VEND1_PORT_CONTROL_EN          ((uint16_t)0x4000U)
#define USER_PHY_VEND1_PORT_FUNC_PHY_TEST_ENABLE ((uint16_t)0x0001U)
#define USER_PHY_VEND1_PORT_FUNC_PTP_ENABLE     ((uint16_t)0x0008U)
#define USER_PHY_VEND1_PORT_FUNC_BIST_ENABLE    ((uint16_t)0x0800U)
#define USER_PHY_VEND1_PORT_FUNC_TEST_ENABLE    ((uint16_t)0x0001U)
#define USER_PHY_VEND1_PORT_FUNC_EPHY_ENABLE    ((uint16_t)0x0004U)
#define USER_PHY_VEND1_PHY_CONTROL_CONFIG_EN    ((uint16_t)0x4000U)
#define USER_PHY_VEND1_PHY_CONTROL_START_OP      ((uint16_t)0x0001U)
#define USER_PHY_VEND1_PHY_CONFIG_AUTO          ((uint16_t)0x0001U)
#define USER_PHY_VEND1_PHY_CONFIG_TEST_ENABLE   ((uint16_t)0x0002U)
#define USER_PHY_VEND1_PHY_CONFIG_POLARITY_CORRECT_DISABLE ((uint16_t)0x0004U)
#define USER_PHY_VEND1_PHY_CONFIG_POLARITY_SWAP ((uint16_t)0x0008U)
#define USER_PHY_VEND1_PHY_STATUS_DETECTED_POLARITY ((uint16_t)0x0080U)
#define USER_PHY_VEND1_PHY_STATUS_LINK_AVAILABLE ((uint16_t)0x0040U)
#define USER_PHY_VEND1_PHY_STATUS_SENDN_OR_DATA ((uint16_t)0x0020U)
#define USER_PHY_VEND1_PHY_STATUS_REM_RCVR_STATUS ((uint16_t)0x0010U)
#define USER_PHY_VEND1_PHY_STATUS_SCRAMBLER_STATUS ((uint16_t)0x0008U)
#define USER_PHY_VEND1_PHY_STATUS_LINK_STATUS    ((uint16_t)0x0004U)
#define USER_PHY_VEND1_PHY_STATUS_LOC_RCVR_STATUS ((uint16_t)0x0002U)
#define USER_PHY_VEND1_SIGNAL_QUALITY_VALID     ((uint16_t)0x4000U)
#define USER_PHY_VEND1_SIGNAL_QUALITY_MASK      ((uint16_t)0x0007U)
#define USER_PHY_VEND1_SIGNAL_QUALITY_WARN_LIMIT_MASK ((uint16_t)0x0700U)
#define USER_PHY_VEND1_SIGNAL_QUALITY_WORST_MASK ((uint16_t)0x0070U)
#define USER_PHY_VEND1_SIGNAL_QUALITY_WORST_SHIFT (4U)
#define USER_PHY_VEND1_MSE_ENABLE               ((uint16_t)0x8000U)
#define USER_PHY_VEND1_MSE_MASK                 ((uint16_t)0x0FF0U)
#define USER_PHY_VEND1_MSE_SHIFT                (4U)
#define USER_PHY_VEND1_COUNTER_EN               ((uint16_t)0x8000U)
#define USER_PHY_VEND1_CABLE_TEST_ENABLE        ((uint16_t)0x8000U)
#define USER_PHY_VEND1_CABLE_TEST_START         ((uint16_t)0x4000U)
#define USER_PHY_VEND1_CABLE_TEST_VALID         ((uint16_t)0x2000U)
#define USER_PHY_VEND1_CABLE_TEST_RESULT_MASK   ((uint16_t)0x0007U)
#define USER_PHY_VEND1_CABLE_TEST_RESULT_OK     ((uint16_t)0x0000U)
#define USER_PHY_VEND1_CABLE_TEST_RESULT_SHORT  ((uint16_t)0x0001U)
#define USER_PHY_VEND1_CABLE_TEST_RESULT_OPEN   ((uint16_t)0x0002U)
#define USER_PHY_VEND1_CABLE_TEST_RESULT_UNKNOWN ((uint16_t)0x0007U)
#define USER_PHY_VEND1_LINK_DROP_STATUS_SHIFT   (8U)
#define USER_PHY_VEND1_LINK_DROP_STATUS_MASK    ((uint16_t)0x3F00U)
#define USER_PHY_VEND1_LINK_DROP_AVAIL_MASK     ((uint16_t)0x003FU)
#define USER_PHY_VEND1_LINK_LOSSES_SHIFT        (10U)
#define USER_PHY_VEND1_LINK_LOSSES_MASK         ((uint16_t)0xFC00U)
#define USER_PHY_VEND1_LINK_FAILURES_MASK       ((uint16_t)0x03FFU)
#define USER_PHY_VEND1_TIMING_TIME_MS_MASK      ((uint16_t)0x0FF0U)
#define USER_PHY_VEND1_TIMING_TIME_MS_SHIFT     (4U)
#define USER_PHY_VEND1_TIMING_TIME_SUBMS_MASK   ((uint16_t)0x000EU)
#define USER_PHY_VEND1_TIMING_TIME_SUBMS_SHIFT  (1U)
#define USER_PHY_VEND1_PREAMBLE_COUNT_MASK      ((uint16_t)0x003FU)
#define USER_PHY_VEND1_IPG_LENGTH_MASK          ((uint16_t)0x01FFU)
#define USER_PHY_VEND1_PORT_INFRA_CONTROL_EN     ((uint16_t)0x4000U)
#define USER_PHY_VEND1_INFRA_CONFIG_TEST_ENABLE ((uint16_t)0x0001U)
#define USER_PHY_VEND1_PORT_PTP_BYPASS          ((uint16_t)0x0800U)
#define USER_PHY_VEND1_PTP_CONFIG_PPS_ENABLE    ((uint16_t)0x0008U)
#define USER_PHY_VEND1_PTP_CONFIG_PPS_POLARITY  ((uint16_t)0x0004U)
#define USER_PHY_VEND1_MII_BASIC_CONFIG_MODE    ((uint16_t)0x000FU)
#define USER_PHY_VEND1_MII_BASIC_CONFIG_REV     ((uint16_t)0x0010U)
#define USER_PHY_VEND1_MII_BASIC_CONFIG_RMII    ((uint16_t)0x0005U)
#define USER_PHY_VEND1_XMII_CONTROL_MAC_LOOPBACK ((uint16_t)0x0002U)
#define USER_PHY_VEND1_XMII_CONTROL_PHY_LOOPBACK ((uint16_t)0x0001U)
#define USER_PHY_VEND1_GPIO_FUNC_ENABLE         ((uint16_t)0x8000U)
#define USER_PHY_VEND1_GPIO_FUNC_PTP            ((uint16_t)0x0040U)
#define USER_PHY_VEND1_GPIO_FUNC_PPS_OUT        ((uint16_t)0x0012U)

#define USER_PHY_VEND1_WAKE_SLEEP_CTRL_WU_PHY_REQUEST ((uint16_t)0x8000U)
#define USER_PHY_VEND1_WAKE_SLEEP_CTRL_SLEEP_REJECT   ((uint16_t)0x0004U)
#define USER_PHY_VEND1_WAKE_SLEEP_CTRL_SLEEP_ACCEPT   ((uint16_t)0x0002U)
#define USER_PHY_VEND1_WAKE_SLEEP_CTRL_SLEEP_REQUEST  ((uint16_t)0x0001U)

#define USER_PHY_VEND1_WAKE_SLEEP_STATUS_WU_IO_RECEIVED ((uint16_t)0x4000U)
#define USER_PHY_VEND1_WAKE_SLEEP_STATUS_WU_PHY_RECEIVED ((uint16_t)0x1000U)
#define USER_PHY_VEND1_WAKE_SLEEP_STATUS_WUP_RECEIVED  ((uint16_t)0x0800U)
#define USER_PHY_VEND1_WAKE_SLEEP_STATUS_WUR_RECEIVED  ((uint16_t)0x0400U)
#define USER_PHY_VEND1_WAKE_SLEEP_STATUS_AUTO_SLEEP_EVENT ((uint16_t)0x0008U)
#define USER_PHY_VEND1_WAKE_SLEEP_STATUS_SLEEP_FAILED  ((uint16_t)0x0004U)
#define USER_PHY_VEND1_WAKE_SLEEP_STATUS_LPS_RECEIVED  ((uint16_t)0x0002U)
#define USER_PHY_VEND1_WAKE_SLEEP_STATUS_SLEEP_STATUS  ((uint16_t)0x0001U)

#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FUNCTION_SELECT_MASK ((uint16_t)0xC000U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FUNCTION_NONE      ((uint16_t)0x0000U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FUNCTION_BASIC     ((uint16_t)0x4000U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FUNCTION_TC10      ((uint16_t)0x8000U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG_WU_IO_ENABLE       ((uint16_t)0x0800U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG_WU_SMI_ENABLE      ((uint16_t)0x0400U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FWD_WU_PHY_TO_WU_IO ((uint16_t)0x0200U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG_FWD_WUPWUR_TO_WU_IO ((uint16_t)0x0100U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG_SLEEP_SILENT_CHECK ((uint16_t)0x0020U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG_WUR_IN_SEND_LPS    ((uint16_t)0x0010U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG_AUTO_REJECT        ((uint16_t)0x0008U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG_AUTO_SLEEP_ON_IDLE ((uint16_t)0x0004U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG_AUTO_SLEEP_ON_SILENCE ((uint16_t)0x0002U)
#define USER_PHY_VEND1_WAKE_SLEEP_CONFIG_NO_AUTO_ON_WAKE    ((uint16_t)0x0001U)

#define USER_PHY_VEND1_WAKE_SLEEP_PARAMS_SLEEP_ACK_MASK     ((uint16_t)0x7000U)
#define USER_PHY_VEND1_WAKE_SLEEP_PARAMS_SLEEP_ACK_SHIFT    (12U)
#define USER_PHY_VEND1_WAKE_SLEEP_PARAMS_AUTO_SLEEP_IDLE_MASK ((uint16_t)0x0700U)
#define USER_PHY_VEND1_WAKE_SLEEP_PARAMS_AUTO_SLEEP_IDLE_SHIFT (8U)
#define USER_PHY_VEND1_WAKE_SLEEP_PARAMS_AUTO_SLEEP_SILENCE_MASK ((uint16_t)0x0070U)
#define USER_PHY_VEND1_WAKE_SLEEP_PARAMS_AUTO_SLEEP_SILENCE_SHIFT (4U)
#define USER_PHY_VEND1_WAKE_SLEEP_PARAMS_MIN_SILENT_MASK    ((uint16_t)0x0007U)

#define USER_PHY_VEND1_BIST_PORT_CONFIG_ENABLE      ((uint16_t)0x4000U)
#define USER_PHY_VEND1_BIST_INTERCEPT_CHECK_EPHY    ((uint16_t)0x0800U)
#define USER_PHY_VEND1_BIST_INTERCEPT_CHECK_XMII    ((uint16_t)0x0000U)
#define USER_PHY_VEND1_BIST_INTERCEPT_TX_EPHY       ((uint16_t)0x0020U)
#define USER_PHY_VEND1_BIST_INTERCEPT_TX_NORMAL     ((uint16_t)0x0000U)
#define USER_PHY_VEND1_BIST_INTERCEPT_RX_XMII       ((uint16_t)0x0002U)
#define USER_PHY_VEND1_BIST_INTERCEPT_RX_NORMAL     ((uint16_t)0x0000U)
#define USER_PHY_VEND1_BIST_GEN_ENABLE              ((uint16_t)0x4000U)
#define USER_PHY_VEND1_BIST_GEN_CONTINUOUS          ((uint16_t)0x2000U)
#define USER_PHY_VEND1_BIST_GEN_STOP                ((uint16_t)0x1000U)
#define USER_PHY_VEND1_BIST_GEN_DONE                ((uint16_t)0x8000U)
#define USER_PHY_VEND1_BIST_CHECK_ENABLE            ((uint16_t)0x4000U)
#define USER_PHY_VEND1_BIST_CHECK_STAT_RESET        ((uint16_t)0x2000U)
#define USER_PHY_VEND1_BIST_CHECK_CONTINUOUS        ((uint16_t)0x0001U)
#define USER_PHY_VEND1_BIST_PROD_DONE               ((uint16_t)0x8000U)
#define USER_PHY_VEND1_BIST_PROD_FAIL               ((uint16_t)0x0010U)
#define USER_PHY_VEND1_BIST_PROD_RXER               ((uint16_t)0x0008U)
#define USER_PHY_VEND1_BIST_PROD_RXDV_ERR           ((uint16_t)0x0004U)
#define USER_PHY_VEND1_BIST_PROD_BAD_FRAME_ERR      ((uint16_t)0x0002U)
#define USER_PHY_VEND1_BIST_PROD_GOOD_FRAME_ERR     ((uint16_t)0x0001U)
#define USER_PHY_VEND1_BIST_PREAMBLE_LENGTH_MASK    ((uint16_t)0xF000U)
#define USER_PHY_VEND1_BIST_PREAMBLE_LENGTH_SHIFT   (12U)
#define USER_PHY_VEND1_BIST_IPG_TYPE_MASK           ((uint16_t)0x0300U)
#define USER_PHY_VEND1_BIST_IPG_TYPE_SHIFT          (8U)
#define USER_PHY_VEND1_BIST_IPG_LENGTH_MASK         ((uint16_t)0x00FFU)
#define USER_PHY_VEND1_BIST_PTP_INTERVAL_MASK       ((uint16_t)0xFF00U)
#define USER_PHY_VEND1_BIST_PTP_INTERVAL_SHIFT      (8U)
#define USER_PHY_VEND1_BIST_PTP_MSG_TYPE_MASK       ((uint16_t)0x000FU)
#define USER_PHY_VEND1_BIST_PAYLOAD_DATA_TYPE_MASK  ((uint16_t)0x3000U)
#define USER_PHY_VEND1_BIST_PAYLOAD_DATA_TYPE_SHIFT (12U)
#define USER_PHY_VEND1_BIST_PAYLOAD_DATA_FIXED      ((uint16_t)0x0000U)
#define USER_PHY_VEND1_BIST_PAYLOAD_DATA_RAMP       ((uint16_t)0x1000U)
#define USER_PHY_VEND1_BIST_PAYLOAD_DATA_PRBS       ((uint16_t)0x2000U)
#define USER_PHY_VEND1_BIST_PAYLOAD_SIZE_TYPE_MASK  ((uint16_t)0x0300U)
#define USER_PHY_VEND1_BIST_PAYLOAD_SIZE_TYPE_SHIFT (8U)
#define USER_PHY_VEND1_BIST_PAYLOAD_SIZE_FIXED      ((uint16_t)0x0000U)
#define USER_PHY_VEND1_BIST_PAYLOAD_SIZE_INCREMENT  ((uint16_t)0x0100U)
#define USER_PHY_VEND1_BIST_PAYLOAD_SIZE_RANDOM     ((uint16_t)0x0200U)
#define USER_PHY_VEND1_BIST_FIXED_PAYLOAD_DATA_MASK ((uint16_t)0x00FFU)
#define USER_PHY_VEND1_BIST_PAYLOAD_SIZE_MASK       ((uint16_t)0x3FFFU)
#define USER_PHY_VEND1_BIST_PRBS_SELECT_MASK        ((uint16_t)0xE000U)
#define USER_PHY_VEND1_BIST_PRBS_SELECT_SHIFT       (13U)
#define USER_PHY_VEND1_BIST_PRBS_SELECT_7           ((uint16_t)0x0000U)
#define USER_PHY_VEND1_BIST_PRBS_SELECT_9           ((uint16_t)0x2000U)
#define USER_PHY_VEND1_BIST_PRBS_SELECT_11          ((uint16_t)0x4000U)
#define USER_PHY_VEND1_BIST_PRBS_SELECT_15          ((uint16_t)0x6000U)
#define USER_PHY_VEND1_BIST_LFSR_SEED_USAGE         ((uint16_t)0x0800U)
#define USER_PHY_VEND1_BIST_LFSR_SEED_MASK          ((uint16_t)0x7FFFU)
/**
  * @}
  */

/** @defgroup USER_PHY_Project_Configuration USER_PHY Project Configuration
  * @{
  */
#define USER_PHY_AUTONOMOUS_MODE_DISABLED       0U
#define USER_PHY_AUTONOMOUS_MODE_ENABLED        1U

#define USER_PHY_MASTER_SLAVE_SLAVE_PREFERRED   0U
#define USER_PHY_MASTER_SLAVE_MASTER_PREFERRED  1U
#define USER_PHY_MASTER_SLAVE_SLAVE_FORCE       2U
#define USER_PHY_MASTER_SLAVE_MASTER_FORCE      3U

#define USER_PHY_PPS_GPIO_MIN_INDEX             0U
#define USER_PHY_PPS_GPIO_MAX_INDEX             11U
#define USER_PHY_PPS_PHASE_0_NS                 0U
#define USER_PHY_PPS_PHASE_500MS_NS             500000000U

#define USER_PHY_CFG_AUTONOMOUS_MODE            USER_PHY_AUTONOMOUS_MODE_ENABLED
#define USER_PHY_CFG_MASTER_SLAVE               USER_PHY_MASTER_SLAVE_SLAVE_PREFERRED
#define USER_PHY_CFG_PPS_ENABLE                 1U
#define USER_PHY_CFG_PPS_GPIO_INDEX             1U
#define USER_PHY_CFG_PPS_PHASE_NS               USER_PHY_PPS_PHASE_0_NS

#if (USER_PHY_CFG_PPS_GPIO_INDEX > USER_PHY_PPS_GPIO_MAX_INDEX)
#error "USER_PHY_CFG_PPS_GPIO_INDEX must be between 0 and 11"
#endif

#if ((USER_PHY_CFG_PPS_PHASE_NS != USER_PHY_PPS_PHASE_0_NS) && \
     (USER_PHY_CFG_PPS_PHASE_NS != USER_PHY_PPS_PHASE_500MS_NS))
#error "USER_PHY_CFG_PPS_PHASE_NS must be 0 or 500000000"
#endif
/**
  * @}
  */

/** @defgroup USER_PHY_PHYI1R_Bit_Definition USER_PHY PHYI1R Bit Definition
  * @{
  */
#define USER_PHY_PHYI1R_OUI_3_18               ((uint16_t)0x001BU)
/**
  * @}
  */

/** @defgroup USER_PHY_PHYI2R_Bit_Definition USER_PHY PHYI2R Bit Definition
  * @{
  */
#define USER_PHY_PHYI2R_OUI_19_24              ((uint16_t)0xFC00U)
#define USER_PHY_PHYI2R_MODEL_NBR              ((uint16_t)0x03F0U)
#define USER_PHY_PHYI2R_REVISION_NBR           ((uint16_t)0x000FU)
#define USER_PHY_PHYI2R_EXPECTED_MASK          ((uint16_t)0xFFF0U)
#define USER_PHY_PHYI2R_EXPECTED               ((uint16_t)0xB010U)
/**
  * @}
  */

/** @defgroup USER_PHY_Status USER_PHY Status
  * @{
  */
#define  USER_PHY_STATUS_TIMEOUT               ((int32_t)-6)
#define  USER_PHY_STATUS_READ_ERROR            ((int32_t)-5)
#define  USER_PHY_STATUS_WRITE_ERROR           ((int32_t)-4)
#define  USER_PHY_STATUS_ADDRESS_ERROR         ((int32_t)-3)
#define  USER_PHY_STATUS_RESET_TIMEOUT         ((int32_t)-2)
#define  USER_PHY_STATUS_ERROR                 ((int32_t)-1)
#define  USER_PHY_STATUS_OK                    ((int32_t) 0)
#define  USER_PHY_STATUS_LINK_DOWN             ((int32_t) 1)
#define  USER_PHY_STATUS_100MBITS_FULLDUPLEX   ((int32_t) 2)
#define  USER_PHY_STATUS_100MBITS_HALFDUPLEX   ((int32_t) 3)
#define  USER_PHY_STATUS_10MBITS_FULLDUPLEX    ((int32_t) 4)
#define  USER_PHY_STATUS_10MBITS_HALFDUPLEX    ((int32_t) 5)
#define  USER_PHY_STATUS_AUTONEGO_NOTDONE      ((int32_t) 6)
/**
  * @}
  */

/** @defgroup USER_PHY_LINK_MODE_Definition USER_PHY Link Mode Definition
  * @{
  */
#define USER_PHY_FORCE_LINK   ((uint16_t)0x0001U) /**< Force link mode (manual configuration) */
#define USER_PHY_AUTO_NEG     ((uint16_t)0x0000U) /**< Auto-negotiation mode enabled */
/**
  * @}
  */

/**
  * @}
  */

/* Exported types ------------------------------------------------------------*/
/** @defgroup USER_PHY_Exported_Types USER_PHY Exported Types
  * @{
  */
typedef int32_t  (*USER_PHY_Init_Func)      (void);
typedef int32_t  (*USER_PHY_DeInit_Func)    (void);
typedef int32_t  (*USER_PHY_ReadReg_Func)   (uint32_t, uint32_t, uint32_t *);
typedef int32_t  (*USER_PHY_WriteReg_Func)  (uint32_t, uint32_t, uint32_t);
typedef int32_t  (*USER_PHY_GetTick_Func)   (void);

typedef struct
{
  USER_PHY_Init_Func      Init;
  USER_PHY_DeInit_Func    DeInit;
  USER_PHY_WriteReg_Func  WriteReg;
  USER_PHY_ReadReg_Func   ReadReg;
  USER_PHY_GetTick_Func   GetTick;
} user_phy_IOCtx_t;

typedef struct
{
  uint32_t            DevAddr;
  uint32_t            Is_Initialized;
  user_phy_IOCtx_t    IO;
  void               *pData;
}user_phy_Object_t;

typedef struct
{
  uint32_t phy_id1;
  uint32_t phy_id2;
  uint32_t pma_ctrl1;
  uint32_t pma_ctrl2;
  uint32_t pma_stat1;
  uint32_t pma_stat2;
  uint32_t pma_rxdet;
  uint32_t always_accessible;
  uint32_t signal_quality;
  uint32_t cable_test;
  uint32_t symbol_error_counter;
  uint32_t link_drop_counter;
  uint32_t link_losses_and_failures;
  uint32_t mii_basic_config;
  uint32_t port_control;
  uint32_t phy_control;
  uint32_t phy_config;
  uint32_t port_infra_control;
  uint32_t port_func_enables;
  uint32_t rx_preamble_count;
  uint32_t tx_preamble_count;
  uint32_t rx_ipg_length;
  uint32_t tx_ipg_length;
  uint32_t bt1_ctrl;
  uint32_t an_ctrl1;
  uint32_t an_stat1;
  uint32_t an_adv_l;
  uint32_t an_adv_m;
} user_phy_Report_t;
/**
  * @}
  */

/* Exported macro ------------------------------------------------------------*/
/* Exported functions --------------------------------------------------------*/
/** @defgroup USER_PHY_Exported_Functions USER_PHY Exported Functions
  * @{
  */
int32_t USER_PHY_RegisterBusIO(user_phy_Object_t *pObj, user_phy_IOCtx_t *ioctx);
int32_t USER_PHY_Init(user_phy_Object_t *pObj);
int32_t USER_PHY_DeInit(user_phy_Object_t *pObj);
int32_t USER_PHY_SoftwareReset(user_phy_Object_t *pObj);
int32_t USER_PHY_Configure(user_phy_Object_t *pObj);
int32_t USER_PHY_ReadReport(user_phy_Object_t *pObj, user_phy_Report_t *pReport);
int32_t USER_PHY_RunCableTest(user_phy_Object_t *pObj, uint32_t *pCableTestResult);
int32_t USER_PHY_GetLinkState(user_phy_Object_t *pObj);
int32_t USER_PHY_SetLinkState(user_phy_Object_t *pObj, uint32_t LinkState);
int32_t USER_PHY_EnableLoopbackMode(user_phy_Object_t *pObj);
int32_t USER_PHY_DisableLoopbackMode(user_phy_Object_t *pObj);
int32_t USER_PHY_ReadC45(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint32_t *value);
int32_t USER_PHY_WriteC45(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint16_t value);
int32_t USER_PHY_ModifyC45(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint16_t clear_mask, uint16_t set_mask);
/**
  * @}
  */

#ifdef   __cplusplus
}
#endif
#endif /* ETH_CUSTOM_PHY_INTERFACE_H */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
