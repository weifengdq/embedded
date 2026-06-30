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

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
#define USER_PHY_SW_RESET_TO    ((uint32_t)500U)
#define USER_PHY_MAX_DEV_ADDR   ((uint32_t)31U)
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

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
  uint32_t regvalue = 0, addr = 0;
  int32_t status = USER_PHY_STATUS_OK;

  /* USER CODE BEGIN PHY_INIT_0 */

  /* USER CODE END PHY_INIT_0 */

  if(pObj->Is_Initialized == 0)
  {
    if(pObj->IO.Init != 0)
    {
      /* GPIO and Clocks initialization */
      pObj->IO.Init();
    }

    /* for later check */
    pObj->DevAddr = USER_PHY_MAX_DEV_ADDR + 1;

    /* Get the device address from special mode register */
    for(addr = 0; addr <= USER_PHY_MAX_DEV_ADDR; addr ++)
    {
      if(pObj->IO.ReadReg(addr, USER_PHY_PHYI1R, &regvalue) < 0)
      {
        status = USER_PHY_STATUS_READ_ERROR;
        /* Can't read from this device address
           continue with next address */
        continue;
      }

      if(regvalue != USER_PHY_PHYI1R_OUI_3_18)
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

  /* USER CODE BEGIN PHY_INIT_1 */

  /* USER CODE END PHY_INIT_1 */

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

  /* Read Status register  */
  if(pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BSR, &readval) < 0)
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  /* Read Status register again */
  if(pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BSR, &readval) < 0)
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  if((readval & USER_PHY_BSR_LINK_STATUS) == 0)
  {
    /* Return Link Down status */
    return USER_PHY_STATUS_LINK_DOWN;
  }

  /* Check Auto negotiation */
  if(pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BCR, &readval) < 0)
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  if((readval & USER_PHY_BCR_AUTONEGO_EN) != USER_PHY_BCR_AUTONEGO_EN)
  {
    if(((readval & USER_PHY_BCR_SPEED_SELECT) == USER_PHY_BCR_SPEED_SELECT) && ((readval & USER_PHY_BCR_DUPLEX_MODE) == USER_PHY_BCR_DUPLEX_MODE))
    {
      return USER_PHY_STATUS_100MBITS_FULLDUPLEX;
    }
    else if ((readval & USER_PHY_BCR_SPEED_SELECT) == USER_PHY_BCR_SPEED_SELECT)
    {
      return USER_PHY_STATUS_100MBITS_HALFDUPLEX;
    }
    else if ((readval & USER_PHY_BCR_DUPLEX_MODE) == USER_PHY_BCR_DUPLEX_MODE)
    {
      return USER_PHY_STATUS_10MBITS_FULLDUPLEX;
    }
    else
    {
      return USER_PHY_STATUS_10MBITS_HALFDUPLEX;
    }
  }
  else /* Auto Nego enabled */
  {
    /* Read Basic Status Register (BSR) to check if auto-negotiation is done */
    if(pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BSR, &readval) < 0)
    {
      return USER_PHY_STATUS_READ_ERROR;
    }

    if((readval & USER_PHY_BSR_AUTONEGO_CPLT) == 0)
    {
      return USER_PHY_STATUS_AUTONEGO_NOTDONE;
    }

    /* Read Auto-Negotiation Advertisement Register (ANAR) */
    if(pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_ANAR, &readval) < 0)
    {
      return USER_PHY_STATUS_READ_ERROR;
    }
    uint16_t anar = (uint16_t)readval;

    /* Read Auto-Negotiation Link Partner Advertisement Register (ANLPAR) */
    if(pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_ANLPAR, &readval) < 0)
    {
      return USER_PHY_STATUS_READ_ERROR;
    }
    uint16_t anlpar = (uint16_t)readval;

    /* Calculate common advertised capabilities */
    uint16_t common_adv = anar & anlpar;

    /* Priority: 100BASE-TX Full Duplex */
    if((common_adv & USER_PHY_ANLPAR_100BASE_TX_FD) == USER_PHY_ANLPAR_100BASE_TX_FD)
    {
      return USER_PHY_STATUS_100MBITS_FULLDUPLEX;
    }
    /* Then 100BASE-TX Half Duplex */
    else if((common_adv & USER_PHY_ANLPAR_100BASE_TX) == USER_PHY_ANLPAR_100BASE_TX)
    {
      return USER_PHY_STATUS_100MBITS_HALFDUPLEX;
    }
    /* Then 10BASE-T Full Duplex */
    else if((common_adv & USER_PHY_ANLPAR_10BASE_T_FD) == USER_PHY_ANLPAR_10BASE_T_FD)
    {
      return USER_PHY_STATUS_10MBITS_FULLDUPLEX;
    }
    /* Otherwise 10BASE-T Half Duplex */
    else if((common_adv & USER_PHY_ANLPAR_10BASE_T) == USER_PHY_ANLPAR_10BASE_T)
    {
      return USER_PHY_STATUS_10MBITS_HALFDUPLEX;
    }
    else
    {
      /* No common mode detected, link down or error */
      return USER_PHY_STATUS_LINK_DOWN;
    }
  }
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
  uint32_t bcrvalue = 0;
  int32_t status = USER_PHY_STATUS_OK;

  if(pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BCR, &bcrvalue) < 0)
  {
    status = USER_PHY_STATUS_READ_ERROR;
  }

  /* Disable auto-negotiation, speed and duplex bits cleared */
  bcrvalue &= ~(USER_PHY_BCR_AUTONEGO_EN | USER_PHY_BCR_SPEED_SELECT | USER_PHY_BCR_DUPLEX_MODE);

  if (LinkState == USER_PHY_STATUS_100MBITS_FULLDUPLEX)
  {
    bcrvalue |= (USER_PHY_BCR_SPEED_SELECT | USER_PHY_BCR_DUPLEX_MODE);
  }
  else if (LinkState == USER_PHY_STATUS_100MBITS_HALFDUPLEX)
  {
    bcrvalue |= USER_PHY_BCR_SPEED_SELECT;
  }
  else if (LinkState == USER_PHY_STATUS_10MBITS_FULLDUPLEX)
  {
    bcrvalue |= USER_PHY_BCR_DUPLEX_MODE;
  }
  else if (LinkState == USER_PHY_STATUS_10MBITS_HALFDUPLEX)
  {
    /* No bits to set */
  }
  else
  {
    status = USER_PHY_STATUS_ERROR;
  }

  if(pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_BCR, bcrvalue) < 0)
  {
    status = USER_PHY_STATUS_WRITE_ERROR;
  }

  return status;
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

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
