#include "eth_custom_phy_interface.h"

#include <stddef.h>

#define USER_PHY_SW_RESET_TIMEOUT_MS            ((uint32_t)500U)
#define USER_PHY_MAX_DEV_ADDR                   ((uint32_t)31U)

static user_phy_plca_config_t g_user_phy_plca_config = {
  USER_PHY_DEFAULT_PLCA_ENABLED,
  USER_PHY_DEFAULT_PLCA_NODE_ID,
  USER_PHY_DEFAULT_PLCA_NODE_COUNT,
  USER_PHY_DEFAULT_PLCA_TOTMR,
  USER_PHY_DEFAULT_PLCA_BURST_COUNT,
  USER_PHY_DEFAULT_PLCA_BURST_TIMER,
  USER_PHY_DEFAULT_PSTC_IRQ_ENABLED,
  USER_PHY_DEFAULT_COLLISION_AUTO
};

typedef struct
{
  uint16_t reg;
  uint16_t value;
} user_phy_reg_write_t;

static const user_phy_reg_write_t g_user_phy_rev_c2_writes[] = {
  {USER_PHY_CFG_D0, 0x3F31U},
  {USER_PHY_CFG_E0, 0xC000U},
  {USER_PHY_CFG_E9, 0x9E50U},
  {USER_PHY_CFG_F5, 0x1CF8U},
  {USER_PHY_CFG_F4, 0xC020U},
  {USER_PHY_CFG_F8, 0xB900U},
  {USER_PHY_CFG_F9, 0x4E53U},
  {USER_PHY_CFG_81, 0x0080U},
  {USER_PHY_CFG_91, 0x9660U}
};

static const user_phy_reg_write_t g_user_phy_sqi_writes[] = {
  {USER_PHY_SQI_B0, 0x0103U},
  {USER_PHY_SQI_B1, 0x0910U},
  {USER_PHY_SQI_B2, 0x1D26U},
  {USER_PHY_SQI_B3, 0x002AU},
  {USER_PHY_SQI_B4, 0x0103U},
  {USER_PHY_SQI_B5, 0x070DU},
  {USER_PHY_SQI_B6, 0x1720U},
  {USER_PHY_SQI_B7, 0x0027U},
  {USER_PHY_SQI_B8, 0x0509U},
  {USER_PHY_SQI_B9, 0x0E13U},
  {USER_PHY_SQI_BA, 0x1C25U},
  {USER_PHY_SQI_BB, 0x002BU}
};

static int32_t USER_PHY_ReadMmdReg(user_phy_Object_t *pObj, uint32_t devAddr,
    uint32_t regAddr, uint32_t *value);
static int32_t USER_PHY_WriteMmdReg(user_phy_Object_t *pObj, uint32_t devAddr,
    uint32_t regAddr, uint32_t value);
static int32_t USER_PHY_ModifyMmdReg(user_phy_Object_t *pObj, uint32_t devAddr,
    uint32_t regAddr, uint32_t mask, uint32_t value);
static int32_t USER_PHY_ProbeAddress(user_phy_Object_t *pObj);
static int32_t user_phy_apply_rev_c2_config(user_phy_Object_t *pObj);
static int32_t user_phy_apply_sqi_config(user_phy_Object_t *pObj, int8_t offset1);
static int32_t user_phy_apply_plca_config(user_phy_Object_t *pObj,
    const user_phy_plca_config_t *config);
static int32_t user_phy_update_pstc_irq(user_phy_Object_t *pObj, uint8_t enabled);
static int32_t user_phy_set_collision_detect(user_phy_Object_t *pObj, uint8_t enabled);
static int8_t user_phy_decode_signed5(uint32_t raw_value);

static int8_t user_phy_decode_signed5(uint32_t raw_value)
{
  uint8_t value = (uint8_t)(raw_value & 0x1FU);

  if ((value & 0x10U) != 0U)
  {
    return (int8_t)(value - 0x20U);
  }

  return (int8_t)value;
}

static int32_t USER_PHY_ReadMmdReg(user_phy_Object_t *pObj, uint32_t devAddr,
    uint32_t regAddr, uint32_t *value)
{
  if ((pObj == NULL) || (value == NULL))
  {
    return USER_PHY_STATUS_ERROR;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMDCTRL,
      USER_PHY_MMDCTRL_FUNC_ADDR | (devAddr & USER_PHY_MMDCTRL_DEVAD_MASK)) < 0)
  {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMDAD, regAddr) < 0)
  {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMDCTRL,
      USER_PHY_MMDCTRL_FUNC_DATA_NO_POST_INC | (devAddr & USER_PHY_MMDCTRL_DEVAD_MASK)) < 0)
  {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_MMDAD, value) < 0)
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  *value &= 0xFFFFU;
  return USER_PHY_STATUS_OK;
}

static int32_t USER_PHY_WriteMmdReg(user_phy_Object_t *pObj, uint32_t devAddr,
    uint32_t regAddr, uint32_t value)
{
  if (pObj == NULL)
  {
    return USER_PHY_STATUS_ERROR;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMDCTRL,
      USER_PHY_MMDCTRL_FUNC_ADDR | (devAddr & USER_PHY_MMDCTRL_DEVAD_MASK)) < 0)
  {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMDAD, regAddr) < 0)
  {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMDCTRL,
      USER_PHY_MMDCTRL_FUNC_DATA_NO_POST_INC | (devAddr & USER_PHY_MMDCTRL_DEVAD_MASK)) < 0)
  {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMDAD, value & 0xFFFFU) < 0)
  {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  return USER_PHY_STATUS_OK;
}

static int32_t USER_PHY_ModifyMmdReg(user_phy_Object_t *pObj, uint32_t devAddr,
    uint32_t regAddr, uint32_t mask, uint32_t value)
{
  uint32_t reg_value;
  int32_t status;

  status = USER_PHY_ReadMmdReg(pObj, devAddr, regAddr, &reg_value);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  reg_value = (reg_value & ~mask) | value;
  return USER_PHY_WriteMmdReg(pObj, devAddr, regAddr, reg_value);
}

static int32_t USER_PHY_ProbeAddress(user_phy_Object_t *pObj)
{
  uint32_t addr;

  for (addr = 0U; addr <= USER_PHY_MAX_DEV_ADDR; ++addr)
  {
    uint32_t id1;
    uint32_t id2;

    if (pObj->IO.ReadReg(addr, USER_PHY_PHYID1, &id1) < 0)
    {
      continue;
    }

    if (pObj->IO.ReadReg(addr, USER_PHY_PHYID2, &id2) < 0)
    {
      continue;
    }

    id1 &= 0xFFFFU;
    id2 &= 0xFFFFU;

    if ((id1 == USER_PHY_LAN8671_ID1) &&
        ((id2 & USER_PHY_LAN8671_ID2_MASK) == USER_PHY_LAN8671_ID2_VALUE))
    {
      pObj->DevAddr = addr;
      return USER_PHY_STATUS_OK;
    }
  }

  return USER_PHY_STATUS_ADDRESS_ERROR;
}

static int32_t user_phy_update_pstc_irq(user_phy_Object_t *pObj, uint8_t enabled)
{
  return USER_PHY_ModifyMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_IMSK1,
      USER_PHY_IMSK1_PSTCM,
      (enabled != 0U) ? 0U : USER_PHY_IMSK1_PSTCM);
}

static int32_t user_phy_set_collision_detect(user_phy_Object_t *pObj, uint8_t enabled)
{
  return USER_PHY_ModifyMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_CDCTL0,
      USER_PHY_CDCTL0_CDEN,
      (enabled != 0U) ? USER_PHY_CDCTL0_CDEN : 0U);
}

static int32_t user_phy_apply_rev_c2_config(user_phy_Object_t *pObj)
{
  uint32_t index;
  uint32_t raw_offset1;
  uint32_t raw_offset2;
  int8_t offset1;
  int8_t offset2;
  uint16_t cfgparam1;
  uint16_t cfgparam2;
  int32_t status;

  status = USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, 0x0004U, &raw_offset1);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, 0x0008U, &raw_offset2);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  offset1 = user_phy_decode_signed5(raw_offset1);
  offset2 = user_phy_decode_signed5(raw_offset2);

  cfgparam1 = (uint16_t)((((uint16_t)((9 + offset1) & 0x3F)) << 10) |
                         (((uint16_t)((14 + offset1) & 0x3F)) << 4) |
                         0x0003U);
  cfgparam2 = (uint16_t)(((uint16_t)((40 + offset2) & 0x3F)) << 10);

  status = USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_CFG_84, cfgparam1);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_CFG_8A, cfgparam2);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  for (index = 0U; index < (sizeof(g_user_phy_rev_c2_writes) / sizeof(g_user_phy_rev_c2_writes[0])); ++index)
  {
    status = USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE,
        g_user_phy_rev_c2_writes[index].reg,
        g_user_phy_rev_c2_writes[index].value);
    if (status != USER_PHY_STATUS_OK)
    {
      return status;
    }
  }

  if (USER_PHY_ENABLE_SQI_CONFIG != 0U)
  {
    status = user_phy_apply_sqi_config(pObj, offset1);
    if (status != USER_PHY_STATUS_OK)
    {
      return status;
    }
  }

  return USER_PHY_STATUS_OK;
}

static int32_t user_phy_apply_sqi_config(user_phy_Object_t *pObj, int8_t offset1)
{
  uint16_t cfgparam3;
  uint16_t cfgparam4;
  uint16_t cfgparam5;
  uint32_t index;
  int32_t status;

  cfgparam3 = (uint16_t)((((uint16_t)((5 + offset1) & 0x3F)) << 8) |
                         ((uint16_t)((9 + offset1) & 0x3F)));
  cfgparam4 = (uint16_t)((((uint16_t)((9 + offset1) & 0x3F)) << 8) |
                         ((uint16_t)((14 + offset1) & 0x3F)));
  cfgparam5 = (uint16_t)((((uint16_t)((17 + offset1) & 0x3F)) << 8) |
                         ((uint16_t)((22 + offset1) & 0x3F)));

  status = USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_SQI_AD, cfgparam3);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_SQI_AE, cfgparam4);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_SQI_AF, cfgparam5);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  for (index = 0U; index < (sizeof(g_user_phy_sqi_writes) / sizeof(g_user_phy_sqi_writes[0])); ++index)
  {
    status = USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE,
        g_user_phy_sqi_writes[index].reg,
        g_user_phy_sqi_writes[index].value);
    if (status != USER_PHY_STATUS_OK)
    {
      return status;
    }
  }

  return USER_PHY_STATUS_OK;
}

static int32_t user_phy_apply_plca_config(user_phy_Object_t *pObj,
    const user_phy_plca_config_t *config)
{
  uint16_t ctrl0;
  uint16_t ctrl1;
  uint16_t burst;
  int32_t status;

  if (config == NULL)
  {
    return USER_PHY_STATUS_ERROR;
  }

  ctrl0 = (config->enabled != 0U) ? USER_PHY_PLCA_CTRL0_EN : 0U;
  if (config->node_id == 0U)
  {
    ctrl1 = (uint16_t)((uint16_t)config->node_count << 8);
  }
  else
  {
    ctrl1 = (uint16_t)config->node_id;
  }
  burst = (uint16_t)(((uint16_t)config->burst_count << 8) | config->burst_timer);

  status = USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE,
      USER_PHY_PLCA_TOTMR, config->totmr);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE,
      USER_PHY_PLCA_BURST, burst);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE,
      USER_PHY_PLCA_CTRL1, ctrl1);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE,
      USER_PHY_PLCA_CTRL0, ctrl0);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_update_pstc_irq(pObj, config->pstc_irq_enabled);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  if (config->collision_auto == 0U)
  {
    return user_phy_set_collision_detect(pObj, (config->enabled != 0U) ? 0U : 1U);
  }

  return USER_PHY_STATUS_OK;
}

int32_t USER_PHY_RegisterBusIO(user_phy_Object_t *pObj, user_phy_IOCtx_t *ioctx)
{
  if ((pObj == NULL) || (ioctx == NULL) || (ioctx->ReadReg == NULL) ||
      (ioctx->WriteReg == NULL) || (ioctx->GetTick == NULL))
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
  int32_t status;

  if (pObj == NULL)
  {
    return USER_PHY_STATUS_ERROR;
  }

  if (pObj->Is_Initialized != 0U)
  {
    return USER_PHY_STATUS_OK;
  }

  if ((pObj->IO.Init != NULL) && (pObj->IO.Init() < 0))
  {
    return USER_PHY_STATUS_ERROR;
  }

  status = USER_PHY_ProbeAddress(pObj);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  pObj->Is_Initialized = 1U;
  return USER_PHY_STATUS_OK;
}

int32_t USER_PHY_DeInit(user_phy_Object_t *pObj)
{
  if (pObj == NULL)
  {
    return USER_PHY_STATUS_ERROR;
  }

  if ((pObj->Is_Initialized != 0U) && (pObj->IO.DeInit != NULL) &&
      (pObj->IO.DeInit() < 0))
  {
    return USER_PHY_STATUS_ERROR;
  }

  pObj->Is_Initialized = 0U;
  return USER_PHY_STATUS_OK;
}

int32_t USER_PHY_SoftwareReset(user_phy_Object_t *pObj)
{
  uint32_t reg_value;
  uint32_t tick_start;

  if ((pObj == NULL) || (pObj->IO.ReadReg == NULL) || (pObj->IO.WriteReg == NULL))
  {
    return USER_PHY_STATUS_ERROR;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_BCR, USER_PHY_BCR_SOFT_RESET) < 0)
  {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  tick_start = (uint32_t)pObj->IO.GetTick();

  do
  {
    if (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BCR, &reg_value) < 0)
    {
      return USER_PHY_STATUS_READ_ERROR;
    }

    if ((reg_value & USER_PHY_BCR_SOFT_RESET) == 0U)
    {
      return USER_PHY_STATUS_OK;
    }
  } while (((uint32_t)pObj->IO.GetTick() - tick_start) <= USER_PHY_SW_RESET_TIMEOUT_MS);

  return USER_PHY_STATUS_RESET_TIMEOUT;
}

int32_t USER_PHY_Configure(user_phy_Object_t *pObj)
{
  int32_t status;

  if ((pObj == NULL) || (pObj->Is_Initialized == 0U))
  {
    return USER_PHY_STATUS_ERROR;
  }

  status = user_phy_apply_rev_c2_config(pObj);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = user_phy_apply_plca_config(pObj, &g_user_phy_plca_config);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  if (g_user_phy_plca_config.collision_auto != 0U)
  {
    return USER_PHY_SyncCollisionDetection(pObj, 1U, NULL, NULL, NULL);
  }

  return USER_PHY_STATUS_OK;
}

int32_t USER_PHY_GetLinkState(user_phy_Object_t *pObj)
{
  uint32_t bsr;

  if ((pObj == NULL) || (pObj->Is_Initialized == 0U))
  {
    return USER_PHY_STATUS_ADDRESS_ERROR;
  }

  if (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BSR, &bsr) < 0)
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  if (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BSR, &bsr) < 0)
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  if ((bsr & USER_PHY_BSR_LINK_STATUS) == 0U)
  {
    return USER_PHY_STATUS_LINK_DOWN;
  }

  if (g_user_phy_plca_config.collision_auto != 0U)
  {
    (void)USER_PHY_SyncCollisionDetection(pObj, 0U, NULL, NULL, NULL);
  }

  return USER_PHY_STATUS_10MBITS_HALFDUPLEX;
}

int32_t USER_PHY_SetLinkState(user_phy_Object_t *pObj, uint32_t LinkState)
{
  (void)pObj;

  if ((LinkState != USER_PHY_STATUS_10MBITS_HALFDUPLEX) &&
      (LinkState != USER_PHY_STATUS_10MBITS_FULLDUPLEX))
  {
    return USER_PHY_STATUS_ERROR;
  }

  return USER_PHY_STATUS_OK;
}

int32_t USER_PHY_EnableLoopbackMode(user_phy_Object_t *pObj)
{
  return USER_PHY_ModifyMmdReg(pObj, USER_PHY_PCS_MMD_DEVICE, USER_PHY_T1SPCSCTL,
      USER_PHY_T1SPCSCTL_LBE, USER_PHY_T1SPCSCTL_LBE);
}

int32_t USER_PHY_DisableLoopbackMode(user_phy_Object_t *pObj)
{
  return USER_PHY_ModifyMmdReg(pObj, USER_PHY_PCS_MMD_DEVICE, USER_PHY_T1SPCSCTL,
      USER_PHY_T1SPCSCTL_LBE, 0U);
}

int32_t USER_PHY_ReadC45(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint32_t *value)
{
  return USER_PHY_ReadMmdReg(pObj, devad, reg, value);
}

int32_t USER_PHY_WriteC45(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint16_t value)
{
  return USER_PHY_WriteMmdReg(pObj, devad, reg, value);
}

int32_t USER_PHY_ModifyC45(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg,
    uint16_t clear_mask, uint16_t set_mask)
{
  return USER_PHY_ModifyMmdReg(pObj, devad, reg, clear_mask, set_mask);
}

int32_t USER_PHY_ReadStatusSnapshot(user_phy_Object_t *pObj, user_phy_status_snapshot_t *snapshot)
{
  if ((pObj == NULL) || (snapshot == NULL))
  {
    return USER_PHY_STATUS_ERROR;
  }

  if ((pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_PHYID1, &snapshot->phy_id1) < 0) ||
      (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_PHYID2, &snapshot->phy_id2) < 0) ||
      (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BCR, &snapshot->bcr) < 0) ||
      (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BSR, &snapshot->bsr) < 0) ||
      (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_STS1, &snapshot->status1) != USER_PHY_STATUS_OK) ||
      (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_STS2, &snapshot->status2) != USER_PHY_STATUS_OK) ||
      (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_STS3, &snapshot->status3) != USER_PHY_STATUS_OK) ||
      (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_IMSK1, &snapshot->imsk1) != USER_PHY_STATUS_OK) ||
      (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_SQISTS0, &snapshot->sqi_status) != USER_PHY_STATUS_OK) ||
      (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_PLCA_CTRL0, &snapshot->plca_ctrl0) != USER_PHY_STATUS_OK) ||
      (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_PLCA_CTRL1, &snapshot->plca_ctrl1) != USER_PHY_STATUS_OK) ||
      (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_PLCA_STS, &snapshot->plca_status) != USER_PHY_STATUS_OK) ||
      (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_PLCA_TOTMR, &snapshot->plca_totmr) != USER_PHY_STATUS_OK) ||
      (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_PLCA_BURST, &snapshot->plca_burst) != USER_PHY_STATUS_OK) ||
      (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_CDCTL0, &snapshot->cdctl0) != USER_PHY_STATUS_OK))
  {
    return USER_PHY_STATUS_READ_ERROR;
  }

  snapshot->phy_id1 &= 0xFFFFU;
  snapshot->phy_id2 &= 0xFFFFU;
  snapshot->bcr &= 0xFFFFU;
  snapshot->bsr &= 0xFFFFU;
  return USER_PHY_STATUS_OK;
}

int32_t USER_PHY_GetPlcaConfig(user_phy_Object_t *pObj, user_phy_plca_config_t *config)
{
  if (config == NULL)
  {
    return USER_PHY_STATUS_ERROR;
  }

  *config = g_user_phy_plca_config;

  if ((pObj == NULL) || (pObj->Is_Initialized == 0U))
  {
    return USER_PHY_STATUS_OK;
  }

  {
    uint32_t ctrl0;
    uint32_t ctrl1;
    uint32_t totmr;
    uint32_t burst;
    uint32_t imsk1;

    if ((USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_PLCA_CTRL0, &ctrl0) != USER_PHY_STATUS_OK) ||
        (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_PLCA_CTRL1, &ctrl1) != USER_PHY_STATUS_OK) ||
        (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_PLCA_TOTMR, &totmr) != USER_PHY_STATUS_OK) ||
        (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_PLCA_BURST, &burst) != USER_PHY_STATUS_OK) ||
        (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_IMSK1, &imsk1) != USER_PHY_STATUS_OK))
    {
      return USER_PHY_STATUS_READ_ERROR;
    }

    config->enabled = ((ctrl0 & USER_PHY_PLCA_CTRL0_EN) != 0U) ? 1U : 0U;
    config->node_id = (uint8_t)(ctrl1 & 0x00FFU);
    if (config->node_id == 0U)
    {
      config->node_count = (uint8_t)((ctrl1 >> 8) & 0x00FFU);
    }
    config->totmr = (uint8_t)(totmr & 0x00FFU);
    config->burst_count = (uint8_t)((burst >> 8) & 0x00FFU);
    config->burst_timer = (uint8_t)(burst & 0x00FFU);
    config->pstc_irq_enabled = ((imsk1 & USER_PHY_IMSK1_PSTCM) == 0U) ? 1U : 0U;
  }

  return USER_PHY_STATUS_OK;
}

int32_t USER_PHY_SetPlcaConfig(user_phy_Object_t *pObj, const user_phy_plca_config_t *config,
    uint8_t apply_now)
{
  user_phy_plca_config_t candidate;
  int32_t status;

  if (config == NULL)
  {
    return USER_PHY_STATUS_ERROR;
  }

  candidate = *config;

  if (candidate.node_count == 0U)
  {
    candidate.node_count = 1U;
  }

  if ((candidate.enabled != 0U) && (candidate.node_id == 0U) && (candidate.node_count == 0U))
  {
    return USER_PHY_STATUS_ERROR;
  }

  if (apply_now != 0U)
  {
    status = user_phy_apply_plca_config(pObj, &candidate);
    if (status != USER_PHY_STATUS_OK)
    {
      return status;
    }

    if (candidate.collision_auto != 0U)
    {
      status = USER_PHY_SyncCollisionDetection(pObj, 1U, NULL, NULL, NULL);
      if (status != USER_PHY_STATUS_OK)
      {
        return status;
      }
    }
  }

  g_user_phy_plca_config = candidate;
  return USER_PHY_STATUS_OK;
}

int32_t USER_PHY_SyncCollisionDetection(user_phy_Object_t *pObj, uint8_t force,
    uint32_t *status1, uint32_t *plca_status, uint32_t *cdctl0)
{
  uint32_t sts1_value;
  uint32_t plca_value;
  uint32_t cdctl0_value;
  uint8_t want_collision_detect;
  int32_t status;

  if ((pObj == NULL) || (pObj->Is_Initialized == 0U))
  {
    return USER_PHY_STATUS_ERROR;
  }

  status = USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_STS1, &sts1_value);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  if (status1 != NULL)
  {
    *status1 = sts1_value;
  }

  if ((force == 0U) && ((sts1_value & USER_PHY_STS1_PSTC) == 0U))
  {
    return USER_PHY_STATUS_OK;
  }

  status = USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_PLCA_STS, &plca_value);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  status = USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_CDCTL0, &cdctl0_value);
  if (status != USER_PHY_STATUS_OK)
  {
    return status;
  }

  if (plca_status != NULL)
  {
    *plca_status = plca_value;
  }

  if (cdctl0 != NULL)
  {
    *cdctl0 = cdctl0_value;
  }

  if (g_user_phy_plca_config.collision_auto == 0U)
  {
    return USER_PHY_STATUS_OK;
  }

  want_collision_detect = ((g_user_phy_plca_config.enabled == 0U) ||
                           ((plca_value & USER_PHY_PLCA_STS_PST) == 0U)) ? 1U : 0U;

  if (((cdctl0_value & USER_PHY_CDCTL0_CDEN) != 0U) == (want_collision_detect != 0U))
  {
    return USER_PHY_STATUS_OK;
  }

  return user_phy_set_collision_detect(pObj, want_collision_detect);
}