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
#define USER_PHY_BCR      ((uint16_t)0x0000U) /* Basic Control Register */
#define USER_PHY_BSR      ((uint16_t)0x0001U) /* Basic Status Register */
#define USER_PHY_PHYI1R   ((uint16_t)0x0002U) /* PHY Identifier Register 1 */
#define USER_PHY_PHYI2R   ((uint16_t)0x0003U) /* PHY Identifier Register 2 */
#define USER_PHY_ANAR     ((uint16_t)0x0004U) /* Auto-Negociation Advertisment Register */
#define USER_PHY_ANLPAR   ((uint16_t)0x0005U) /* Auto-Negociation Link-Partner Advertisment Register */
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

/** @defgroup USER_PHY_PHYI1R_Bit_Definition USER_PHY PHYI1R Bit Definition
  * @{
  */
#define USER_PHY_PHYI1R_OUI_3_18               ((uint16_t)0xFFFFU)
/**
  * @}
  */

/** @defgroup USER_PHY_PHYI2R_Bit_Definition USER_PHY PHYI2R Bit Definition
  * @{
  */
#define USER_PHY_PHYI2R_OUI_19_24              ((uint16_t)0xFC00U)
#define USER_PHY_PHYI2R_MODEL_NBR              ((uint16_t)0x03F0U)
#define USER_PHY_PHYI2R_REVISION_NBR           ((uint16_t)0x000FU)
/**
  * @}
  */

/** @defgroup USER_PHY_ANAR_Bit_Definition USER_PHY ANAR Bit Definition
  * @{
  */
#define USER_PHY_ANAR_NEXT_PAGE                ((uint16_t)0x8000U)
#define USER_PHY_ANAR_REMOTE_FAULT             ((uint16_t)0x2000U)
#define USER_PHY_ANAR_PAUSE_OPERATION          ((uint16_t)0x0C00U)
#define USER_PHY_ANAR_PO_NOPAUSE               ((uint16_t)0x0000U)
#define USER_PHY_ANAR_PO_SYMMETRIC_PAUSE       ((uint16_t)0x0400U)
#define USER_PHY_ANAR_PO_ASYMMETRIC_PAUSE      ((uint16_t)0x0800U)
#define USER_PHY_ANAR_PO_ADVERTISE_SUPPORT     ((uint16_t)0x0C00U)
#define USER_PHY_ANAR_100BASE_TX_FD            ((uint16_t)0x0100U)
#define USER_PHY_ANAR_100BASE_TX               ((uint16_t)0x0080U)
#define USER_PHY_ANAR_10BASE_T_FD              ((uint16_t)0x0040U)
#define USER_PHY_ANAR_10BASE_T                 ((uint16_t)0x0020U)
#define USER_PHY_ANAR_SELECTOR_FIELD           ((uint16_t)0x000FU)
/**
  * @}
  */

/** @defgroup USER_PHY_ANLPAR_Bit_Definition USER_PHY ANLPAR Bit Definition
  * @{
  */
#define USER_PHY_ANLPAR_NEXT_PAGE              ((uint16_t)0x8000U)
#define USER_PHY_ANLPAR_REMOTE_FAULT           ((uint16_t)0x2000U)
#define USER_PHY_ANLPAR_PAUSE_OPERATION        ((uint16_t)0x0C00U)
#define USER_PHY_ANLPAR_PO_NOPAUSE             ((uint16_t)0x0000U)
#define USER_PHY_ANLPAR_PO_SYMMETRIC_PAUSE     ((uint16_t)0x0400U)
#define USER_PHY_ANLPAR_PO_ASYMMETRIC_PAUSE    ((uint16_t)0x0800U)
#define USER_PHY_ANLPAR_PO_ADVERTISE_SUPPORT   ((uint16_t)0x0C00U)
#define USER_PHY_ANLPAR_100BASE_TX_FD          ((uint16_t)0x0100U)
#define USER_PHY_ANLPAR_100BASE_TX             ((uint16_t)0x0080U)
#define USER_PHY_ANLPAR_10BASE_T_FD            ((uint16_t)0x0040U)
#define USER_PHY_ANLPAR_10BASE_T               ((uint16_t)0x0020U)
#define USER_PHY_ANLPAR_SELECTOR_FIELD         ((uint16_t)0x000FU)
/**
  * @}
  */

/** @defgroup USER_PHY_Status USER_PHY Status
  * @{
  */
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
int32_t USER_PHY_GetLinkState(user_phy_Object_t *pObj);
int32_t USER_PHY_SetLinkState(user_phy_Object_t *pObj, uint32_t LinkState);
int32_t USER_PHY_EnableLoopbackMode(user_phy_Object_t *pObj);
int32_t USER_PHY_DisableLoopbackMode(user_phy_Object_t *pObj);
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
