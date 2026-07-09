/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    eth_custom_phy_interface.c
  * @author  MCD Application Team
  * @brief   This file provides a set of functions needed to manage
  *			 the ethernet phy.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* Private includes ----------------------------------------------------------*/
#include "eth_custom_phy_interface.h"
#include <stddef.h>

/* Private define ------------------------------------------------------------*/
#define USER_PHY_SW_RESET_TO                     ((uint32_t)500U)
#define USER_PHY_CABLE_TEST_TO                   ((uint32_t)250U)
#define USER_PHY_MAX_DEV_ADDR                    ((uint32_t)31U)

/* Private function prototypes -----------------------------------------------*/
static int32_t user_phy_c45_read(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint32_t *value);
static int32_t user_phy_c45_write(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint16_t value);
static int32_t user_phy_c45_modify(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint16_t clear_mask, uint16_t set_mask);
static uint16_t user_phy_get_phy_config_value(void);
static uint16_t user_phy_get_an_adv_l_value(void);
static uint16_t user_phy_get_an_adv_m_value(void);
static uint16_t user_phy_get_gpio_func_value(void);
static uint16_t user_phy_get_ptp_config_value(void);

static int32_t user_phy_c45_read(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint32_t *value)
{
  if ((pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMD_CTRL, devad | USER_PHY_MMD_CTRL_ADDR) < 0) ||
      (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMD_DATA, reg) < 0) ||
      (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMD_CTRL, devad | USER_PHY_MMD_CTRL_DATA_NO_POST_INC) < 0) ||
      (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_MMD_DATA, value) < 0))
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  return USER_PHY_STATUS_OK;
}

static int32_t user_phy_c45_write(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint16_t value)
{
  if ((pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMD_CTRL, devad | USER_PHY_MMD_CTRL_ADDR) < 0) ||
      (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMD_DATA, reg) < 0) ||
      (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMD_CTRL, devad | USER_PHY_MMD_CTRL_DATA_NO_POST_INC) < 0) ||
      (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMD_DATA, value) < 0))
  {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  return USER_PHY_STATUS_OK;
}

static int32_t user_phy_c45_modify(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint16_t clear_mask, uint16_t set_mask)
{
  uint32_t value = 0;

  if (user_phy_c45_read(pObj, devad, reg, &value) != USER_PHY_STATUS_OK)
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  value &= ~clear_mask;
  value |= set_mask;

  return user_phy_c45_write(pObj, devad, reg, (uint16_t)value);
}

static uint16_t user_phy_get_phy_config_value(void)
{
  return (USER_PHY_CFG_AUTONOMOUS_MODE == USER_PHY_AUTONOMOUS_MODE_ENABLED) ? USER_PHY_VEND1_PHY_CONFIG_AUTO : 0U;
}

static uint16_t user_phy_get_an_adv_l_value(void)
{
  switch (USER_PHY_CFG_MASTER_SLAVE)
  {
    case USER_PHY_MASTER_SLAVE_SLAVE_FORCE:
    case USER_PHY_MASTER_SLAVE_MASTER_FORCE:
      return USER_PHY_MDIO_AN_T1_ADV_L_FORCE_MS;

    case USER_PHY_MASTER_SLAVE_SLAVE_PREFERRED:
    case USER_PHY_MASTER_SLAVE_MASTER_PREFERRED:
    default:
      return 0U;
  }
}

static uint16_t user_phy_get_an_adv_m_value(void)
{
  uint16_t adv = USER_PHY_MDIO_AN_T1_ADV_M_100BT1;

  switch (USER_PHY_CFG_MASTER_SLAVE)
  {
    case USER_PHY_MASTER_SLAVE_MASTER_PREFERRED:
    case USER_PHY_MASTER_SLAVE_MASTER_FORCE:
      adv |= USER_PHY_MDIO_AN_T1_ADV_M_MST;
      break;

    case USER_PHY_MASTER_SLAVE_SLAVE_PREFERRED:
    case USER_PHY_MASTER_SLAVE_SLAVE_FORCE:
    default:
      break;
  }

  return adv;
}

static uint16_t user_phy_get_gpio_func_value(void)
{
  return (uint16_t)(USER_PHY_VEND1_GPIO_FUNC_ENABLE | USER_PHY_VEND1_GPIO_FUNC_PTP | USER_PHY_VEND1_GPIO_FUNC_PPS_OUT);
}

static uint16_t user_phy_get_ptp_config_value(void)
{
  if (USER_PHY_CFG_PPS_ENABLE == 0U)
  {
    return 0U;
  }

  return (USER_PHY_CFG_PPS_PHASE_NS == USER_PHY_PPS_PHASE_500MS_NS) ?
         (uint16_t)(USER_PHY_VEND1_PTP_CONFIG_PPS_ENABLE | USER_PHY_VEND1_PTP_CONFIG_PPS_POLARITY) :
         USER_PHY_VEND1_PTP_CONFIG_PPS_ENABLE;
}

/**
  * @brief  Register IO functions to component object
  * @param  pObj: device object of user_phy_Object_t.
  * @param  ioctx: holds device IO functions.
  * @retval USER_PHY_STATUS_OK  if OK
  *         USER_PHY_STATUS_ERROR if missing mandatory function
  */
int32_t  USER_PHY_RegisterBusIO(user_phy_Object_t *pObj, user_phy_IOCtx_t *ioctx)
{
  if(!pObj || !ioctx->ReadReg || !ioctx->WriteReg || !ioctx->GetTick)
  {
    return USER_PHY_STATUS_ERROR;
  }

  pObj->IO.Init = ioctx->Init;
  pObj->IO.DeInit = ioctx->DeInit;
  pObj->IO.ReadReg = ioctx->ReadReg;
  pObj->IO.WriteReg = ioctx->WriteReg;
  pObj->IO.GetTick = ioctx->GetTick;

  return USER_PHY_STATUS_OK;
}

int32_t USER_PHY_Init(user_phy_Object_t *pObj)
{
  uint32_t regvalue = 0;
  uint32_t regvalue2 = 0;
  uint32_t addr = 0;
  int32_t status = USER_PHY_STATUS_OK;

  if(pObj->Is_Initialized == 0)
  {
    if(pObj->IO.Init != 0)
    {
      /* GPIO and Clocks initialization */
      if (pObj->IO.Init() < 0)
      {
        return USER_PHY_STATUS_ERROR;
      }
    }

    /* for later check */
    pObj->DevAddr = USER_PHY_MAX_DEV_ADDR + 1;

    /* Search the PHY address by standard Clause 22 identifier registers. */
    for(addr = 0; addr <= USER_PHY_MAX_DEV_ADDR; addr ++)
    {
      if(pObj->IO.ReadReg(addr, USER_PHY_PHYI1R, &regvalue) < 0)
      {
        continue;
      }

      if(pObj->IO.ReadReg(addr, USER_PHY_PHYI2R, &regvalue2) < 0)
      {
        continue;
      }

      if((regvalue == USER_PHY_PHYI1R_OUI_3_18) &&
         ((regvalue2 & USER_PHY_PHYI2R_EXPECTED_MASK) == USER_PHY_PHYI2R_EXPECTED))
      {
        pObj->DevAddr = addr;
        status = USER_PHY_STATUS_OK;
        break;
      }
    }

    if(pObj->DevAddr > USER_PHY_MAX_DEV_ADDR)
    {
      status = USER_PHY_STATUS_ADDRESS_ERROR;
    }

    /* if device address is matched */
    if(status == USER_PHY_STATUS_OK)
    {
      pObj->Is_Initialized = 1;
    }
  }

  return status;
}

/**
  * @brief  De-Initialize the USER_PHY and it's hardware resources
  * @param  pObj: device object user_phy_Object_t.
  * @retval None
  */
int32_t USER_PHY_DeInit(user_phy_Object_t *pObj)
{
  if(pObj->Is_Initialized)
  {
    if(pObj->IO.DeInit != 0)
    {
      if(pObj->IO.DeInit() < 0)
      {
        return USER_PHY_STATUS_ERROR;
      }
    }

    pObj->Is_Initialized = 0;
  }

  return USER_PHY_STATUS_OK;
}

/**
  * @brief  Perform a software reset of the USER_PHY and wait for its completion
  * @param  pObj: device object user_phy_Object_t.
  * @retval USER_PHY_STATUS_OK           if the reset completed successfully
  *         USER_PHY_STATUS_WRITE_ERROR  if writing the reset command failed
  *         USER_PHY_STATUS_READ_ERROR   if reading the register failed during reset
  *         USER_PHY_STATUS_RESET_TIMEOUT if the reset did not complete within the timeout period
  */
int32_t USER_PHY_SoftwareReset(user_phy_Object_t *pObj)
{
  int32_t status = USER_PHY_STATUS_OK;
  uint32_t tickstart = 0;
  uint32_t regvalue = 0;

  /* Set software reset bit */
  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_BCR, USER_PHY_BCR_SOFT_RESET) < 0)
  {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  /* Read register to check reset status */
  if (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BCR, &regvalue) < 0)
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  tickstart = pObj->IO.GetTick();

  /* Wait until reset bit is cleared or timeout */
  while (regvalue & USER_PHY_BCR_SOFT_RESET)
  {
    if ((pObj->IO.GetTick() - tickstart) > USER_PHY_SW_RESET_TO)
    {
      status = USER_PHY_STATUS_RESET_TIMEOUT;
      break;
    }

    if (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BCR, &regvalue) < 0)
    {
      status = USER_PHY_STATUS_READ_ERROR;
      break;
    }
  }

  return status;
}

int32_t USER_PHY_Configure(user_phy_Object_t *pObj)
{
  int32_t status = USER_PHY_STATUS_OK;
  uint32_t delay_start = 0U;
  uint16_t phy_config_value = user_phy_get_phy_config_value();
  uint16_t an_adv_l_value = user_phy_get_an_adv_l_value();
  uint16_t an_adv_m_value = user_phy_get_an_adv_m_value();
  uint16_t ptp_config_value = user_phy_get_ptp_config_value();

  status = user_phy_c45_write(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_DEVICE_CONTROL,
                              (uint16_t)(USER_PHY_VEND1_DEVICE_CONTROL_GLOBAL_EN | USER_PHY_VEND1_DEVICE_CONTROL_ALL_EN));
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  delay_start = pObj->IO.GetTick();
  while ((pObj->IO.GetTick() - delay_start) < 1U)
  {
  }

  status = user_phy_c45_write(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PORT_CONTROL,
                              USER_PHY_VEND1_PORT_CONTROL_EN);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_write(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PHY_CONTROL,
                              USER_PHY_VEND1_PHY_CONTROL_CONFIG_EN);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_write(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PORT_INFRA_CONTROL,
                              USER_PHY_VEND1_PORT_INFRA_CONTROL_EN);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PHY_CONFIG,
                               USER_PHY_VEND1_PHY_CONFIG_AUTO,
                               phy_config_value);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_MII_BASIC_CONFIG,
                               (uint16_t)(USER_PHY_VEND1_MII_BASIC_CONFIG_MODE | USER_PHY_VEND1_MII_BASIC_CONFIG_REV),
                               (uint16_t)(USER_PHY_VEND1_MII_BASIC_CONFIG_RMII | USER_PHY_VEND1_MII_BASIC_CONFIG_REV));
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_CTRL1,
                               USER_PHY_MDIO_CTRL1_SPEEDSEL,
                               (uint16_t)(USER_PHY_MDIO_CTRL1_SPEED100 | USER_PHY_MDIO_CTRL1_FULLDUPLEX));
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_CTRL2,
                               USER_PHY_MDIO_PMA_CTRL2_TYPE_MASK,
                               USER_PHY_MDIO_PMA_CTRL2_100BTX);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_write(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PTP_RX_TS_METHOD, 0x0002U);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_LINK_DROP_COUNTER,
                               USER_PHY_VEND1_COUNTER_EN,
                               USER_PHY_VEND1_COUNTER_EN);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_RX_PREAMBLE_COUNT,
                               USER_PHY_VEND1_COUNTER_EN,
                               USER_PHY_VEND1_COUNTER_EN);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_TX_PREAMBLE_COUNT,
                               USER_PHY_VEND1_COUNTER_EN,
                               USER_PHY_VEND1_COUNTER_EN);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_RX_IPG_LENGTH,
                               USER_PHY_VEND1_COUNTER_EN,
                               USER_PHY_VEND1_COUNTER_EN);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_TX_IPG_LENGTH,
                               USER_PHY_VEND1_COUNTER_EN,
                               USER_PHY_VEND1_COUNTER_EN);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_write(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PTP_CLK_PERIOD, 15U);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PORT_FUNC_ENABLES,
                               USER_PHY_VEND1_PORT_FUNC_PTP_ENABLE,
                               (USER_PHY_CFG_PPS_ENABLE != 0U) ? USER_PHY_VEND1_PORT_FUNC_PTP_ENABLE : 0U);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PORT_PTP_CONTROL,
                               USER_PHY_VEND1_PORT_PTP_BYPASS,
                               (USER_PHY_CFG_PPS_ENABLE != 0U) ? 0U : USER_PHY_VEND1_PORT_PTP_BYPASS);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_write(pObj, USER_PHY_DEVAD_VEND1,
                              (uint16_t)(USER_PHY_VEND1_GPIO_FUNC_CONFIG_BASE + USER_PHY_CFG_PPS_GPIO_INDEX),
                              (USER_PHY_CFG_PPS_ENABLE != 0U) ? user_phy_get_gpio_func_value() : 0U);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PTP_CONFIG,
                               (uint16_t)(USER_PHY_VEND1_PTP_CONFIG_PPS_ENABLE | USER_PHY_VEND1_PTP_CONFIG_PPS_POLARITY),
                               ptp_config_value);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PHY_CONTROL,
                               0U,
                               USER_PHY_VEND1_PHY_CONTROL_START_OP);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_AN, USER_PHY_MDIO_AN_T1_ADV_L,
                               USER_PHY_MDIO_AN_T1_ADV_L_FORCE_MS,
                               an_adv_l_value);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_AN, USER_PHY_MDIO_AN_T1_ADV_M,
                               (uint16_t)(USER_PHY_MDIO_AN_T1_ADV_M_100BT1 | USER_PHY_MDIO_AN_T1_ADV_M_MST),
                               an_adv_m_value);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_AN, USER_PHY_MDIO_AN_CTRL1,
                               (uint16_t)(USER_PHY_MDIO_AN_CTRL1_ENABLE | USER_PHY_MDIO_AN_CTRL1_RESTART),
                               (uint16_t)(USER_PHY_MDIO_AN_CTRL1_ENABLE | USER_PHY_MDIO_AN_CTRL1_RESTART));

  return status;
}

int32_t USER_PHY_ReadReport(user_phy_Object_t *pObj, user_phy_Report_t *pReport)
{
  if ((pObj == NULL) || (pReport == NULL))
  {
    return USER_PHY_STATUS_ERROR;
  }

  if ((pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_PHYI1R, &pReport->phy_id1) < 0) ||
      (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_PHYI2R, &pReport->phy_id2) < 0) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_CTRL1, &pReport->pma_ctrl1) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_CTRL2, &pReport->pma_ctrl2) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_STAT1, &pReport->pma_stat1) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_STAT2, &pReport->pma_stat2) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_PMA_RXDET, &pReport->pma_rxdet) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_ALWAYS_ACCESSIBLE, &pReport->always_accessible) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_SIGNAL_QUALITY, &pReport->signal_quality) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_CABLE_TEST, &pReport->cable_test) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_SYMBOL_ERROR_COUNTER, &pReport->symbol_error_counter) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_LINK_DROP_COUNTER, &pReport->link_drop_counter) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_LINK_LOSSES_AND_FAILURES, &pReport->link_losses_and_failures) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_MII_BASIC_CONFIG, &pReport->mii_basic_config) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PORT_CONTROL, &pReport->port_control) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PHY_CONTROL, &pReport->phy_control) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PHY_CONFIG, &pReport->phy_config) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PORT_INFRA_CONTROL, &pReport->port_infra_control) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PORT_FUNC_ENABLES, &pReport->port_func_enables) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_RX_PREAMBLE_COUNT, &pReport->rx_preamble_count) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_TX_PREAMBLE_COUNT, &pReport->tx_preamble_count) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_RX_IPG_LENGTH, &pReport->rx_ipg_length) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_TX_IPG_LENGTH, &pReport->tx_ipg_length) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_PMA_PMD_BT1_CTRL, &pReport->bt1_ctrl) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_AN, USER_PHY_MDIO_AN_CTRL1, &pReport->an_ctrl1) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_AN, USER_PHY_MDIO_AN_STAT1, &pReport->an_stat1) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_AN, USER_PHY_MDIO_AN_T1_ADV_L, &pReport->an_adv_l) != USER_PHY_STATUS_OK) ||
      (user_phy_c45_read(pObj, USER_PHY_DEVAD_AN, USER_PHY_MDIO_AN_T1_ADV_M, &pReport->an_adv_m) != USER_PHY_STATUS_OK))
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  return USER_PHY_STATUS_OK;
}

int32_t USER_PHY_RunCableTest(user_phy_Object_t *pObj, uint32_t *pCableTestResult)
{
  uint32_t regvalue = 0U;
  uint32_t tickstart = 0U;
  int32_t status;

  if (pObj == NULL)
  {
    return USER_PHY_STATUS_ERROR;
  }

  if (pCableTestResult != NULL)
  {
    *pCableTestResult = 0U;
  }

  status = user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PORT_FUNC_ENABLES,
                               USER_PHY_VEND1_PORT_FUNC_PHY_TEST_ENABLE,
                               USER_PHY_VEND1_PORT_FUNC_PHY_TEST_ENABLE);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_c45_write(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_CABLE_TEST,
                              (uint16_t)(USER_PHY_VEND1_CABLE_TEST_ENABLE | USER_PHY_VEND1_CABLE_TEST_START));
  if (status != USER_PHY_STATUS_OK)
  {
    goto cleanup;
  }

  tickstart = pObj->IO.GetTick();

  do
  {
    status = user_phy_c45_read(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_CABLE_TEST, &regvalue);
    if (status != USER_PHY_STATUS_OK)
    {
      goto cleanup;
    }

    if ((regvalue & USER_PHY_VEND1_CABLE_TEST_VALID) != 0U)
    {
      if (pCableTestResult != NULL)
      {
        *pCableTestResult = regvalue;
      }

      status = USER_PHY_STATUS_OK;
      goto cleanup;
    }
  } while ((pObj->IO.GetTick() - tickstart) < USER_PHY_CABLE_TEST_TO);

  if (pCableTestResult != NULL)
  {
    *pCableTestResult = regvalue;
  }

  status = USER_PHY_STATUS_TIMEOUT;

cleanup:
  (void)user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_CABLE_TEST,
                            USER_PHY_VEND1_CABLE_TEST_ENABLE,
                            0U);
  (void)user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PORT_FUNC_ENABLES,
                            USER_PHY_VEND1_PORT_FUNC_PHY_TEST_ENABLE,
                            0U);
  (void)user_phy_c45_modify(pObj, USER_PHY_DEVAD_VEND1, USER_PHY_VEND1_PHY_CONTROL,
                            0U,
                            USER_PHY_VEND1_PHY_CONTROL_START_OP);

  return status;
}

/**
  * @brief  Get the link state of USER_PHY device.
  * @param  pObj: Pointer to device object.
  * @param  pLinkState: Pointer to link state
  * @retval USER_PHY_STATUS_LINK_DOWN  if link is down
  *         USER_PHY_STATUS_AUTONEGO_NOTDONE if Auto nego not completed
  *         USER_PHY_STATUS_100MBITS_FULLDUPLEX if 100Mb/s FD
  *         USER_PHY_STATUS_100MBITS_HALFDUPLEX if 100Mb/s HD
  *         USER_PHY_STATUS_10MBITS_FULLDUPLEX  if 10Mb/s FD
  *         USER_PHY_STATUS_10MBITS_HALFDUPLEX  if 10Mb/s HD
  *         USER_PHY_STATUS_READ_ERROR if cannot read register
  *         USER_PHY_STATUS_WRITE_ERROR if cannot write to register
  */
int32_t USER_PHY_GetLinkState(user_phy_Object_t *pObj)
{
  uint32_t readval = 0;

  if(user_phy_c45_read(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_STAT1, &readval) != USER_PHY_STATUS_OK)
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  if(user_phy_c45_read(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_STAT1, &readval) != USER_PHY_STATUS_OK)
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  if((readval & USER_PHY_MDIO_STAT1_LINK_STATUS) == 0U)
  {
    return USER_PHY_STATUS_LINK_DOWN;
  }

  if(user_phy_c45_read(pObj, USER_PHY_DEVAD_AN, USER_PHY_MDIO_AN_CTRL1, &readval) != USER_PHY_STATUS_OK)
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  if((readval & USER_PHY_MDIO_AN_CTRL1_ENABLE) != 0U)
  {
    if(user_phy_c45_read(pObj, USER_PHY_DEVAD_AN, USER_PHY_MDIO_AN_STAT1, &readval) != USER_PHY_STATUS_OK)
    {
      return USER_PHY_STATUS_READ_ERROR;
    }

    if((readval & USER_PHY_MDIO_AN_STAT1_COMPLETE) == 0U)
    {
      return USER_PHY_STATUS_AUTONEGO_NOTDONE;
    }
  }

  return USER_PHY_STATUS_100MBITS_FULLDUPLEX;
}

/**
  * @brief  Set the link state of USER_PHY device.
  * @param  pObj: Pointer to device object.
  * @param  LinkState: link state can be one of the following
  *         USER_PHY_STATUS_100MBITS_FULLDUPLEX if 100Mb/s FD
  *         USER_PHY_STATUS_100MBITS_HALFDUPLEX if 100Mb/s HD
  *         USER_PHY_STATUS_10MBITS_FULLDUPLEX  if 10Mb/s FD
  *         USER_PHY_STATUS_10MBITS_HALFDUPLEX  if 10Mb/s HD
  * @retval USER_PHY_STATUS_OK  if OK
  *         USER_PHY_STATUS_ERROR  if parameter error
  *         USER_PHY_STATUS_READ_ERROR if cannot read register
  *         USER_PHY_STATUS_WRITE_ERROR if cannot write to register
  */
int32_t USER_PHY_SetLinkState(user_phy_Object_t *pObj, uint32_t LinkState)
{
  uint32_t ctrl1 = 0U;
  uint32_t ctrl2 = 0U;

  if (LinkState != USER_PHY_STATUS_100MBITS_FULLDUPLEX)
  {
    return USER_PHY_STATUS_ERROR;
  }

  if (user_phy_c45_read(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_CTRL1, &ctrl1) != USER_PHY_STATUS_OK)
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  ctrl1 &= (uint32_t)(~USER_PHY_MDIO_CTRL1_SPEEDSEL);
  ctrl1 |= (uint32_t)(USER_PHY_MDIO_CTRL1_SPEED100 | USER_PHY_MDIO_CTRL1_FULLDUPLEX);

  if (user_phy_c45_write(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_CTRL1, (uint16_t)ctrl1) != USER_PHY_STATUS_OK)
  {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (user_phy_c45_read(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_CTRL2, &ctrl2) != USER_PHY_STATUS_OK)
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  ctrl2 &= (uint32_t)(~USER_PHY_MDIO_PMA_CTRL2_TYPE_MASK);
  ctrl2 |= USER_PHY_MDIO_PMA_CTRL2_100BTX;

  if (user_phy_c45_write(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_CTRL2, (uint16_t)ctrl2) != USER_PHY_STATUS_OK)
  {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (user_phy_c45_modify(pObj, USER_PHY_DEVAD_AN, USER_PHY_MDIO_AN_CTRL1,
                          (uint16_t)(USER_PHY_MDIO_AN_CTRL1_ENABLE | USER_PHY_MDIO_AN_CTRL1_RESTART),
                          0U) != USER_PHY_STATUS_OK)
  {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (user_phy_c45_modify(pObj, USER_PHY_DEVAD_PMAPMD, USER_PHY_MDIO_PMA_PMD_BT1_CTRL,
                          (uint16_t)(USER_PHY_MDIO_PMA_PMD_BT1_CTRL_CFG_MST | USER_PHY_MDIO_PMA_PMD_BT1_CTRL_STRAP),
                          0U) != USER_PHY_STATUS_OK)
  {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  return USER_PHY_STATUS_OK;
}

/**
  * @brief  Enable loopback mode.
  * @param  pObj: Pointer to device object.
  * @retval USER_PHY_STATUS_OK  if OK
  *         USER_PHY_STATUS_READ_ERROR if cannot read register
  *         USER_PHY_STATUS_WRITE_ERROR if cannot write to register
  */
int32_t USER_PHY_EnableLoopbackMode(user_phy_Object_t *pObj)
{
  uint32_t readval = 0;
  int32_t status = USER_PHY_STATUS_OK;

  if(pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BCR, &readval) >= 0)
  {
    readval |= USER_PHY_BCR_LOOPBACK;

    /* Apply configuration */
    if(pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_BCR, readval) < 0)
    {
      status = USER_PHY_STATUS_WRITE_ERROR;
    }
  }
  else
  {
    status = USER_PHY_STATUS_READ_ERROR;
  }

  return status;
}

/**
  * @brief  Disable loopback mode.
  * @param  pObj: Pointer to device object.
  * @retval USER_PHY_STATUS_OK  if OK
  *         USER_PHY_STATUS_READ_ERROR if cannot read register
  *         USER_PHY_STATUS_WRITE_ERROR if cannot write to register
  */
int32_t USER_PHY_DisableLoopbackMode(user_phy_Object_t *pObj)
{
  uint32_t readval = 0;
  int32_t status = USER_PHY_STATUS_OK;

  if(pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BCR, &readval) >= 0)
  {
    readval &= ~USER_PHY_BCR_LOOPBACK;

    /* Apply configuration */
    if(pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_BCR, readval) < 0)
    {
      status =  USER_PHY_STATUS_WRITE_ERROR;
    }
  }
  else
  {
    status = USER_PHY_STATUS_READ_ERROR;
  }

  return status;
}

int32_t USER_PHY_ReadC45(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint32_t *value)
{
  return user_phy_c45_read(pObj, devad, reg, value);
}

int32_t USER_PHY_WriteC45(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint16_t value)
{
  return user_phy_c45_write(pObj, devad, reg, value);
}

int32_t USER_PHY_ModifyC45(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint16_t clear_mask, uint16_t set_mask)
{
  return user_phy_c45_modify(pObj, devad, reg, clear_mask, set_mask);
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
