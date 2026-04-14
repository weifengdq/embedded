#include "eth_custom_phy_interface.h"

#include <stddef.h>

#define USER_PHY_SW_RESET_TIMEOUT_MS            ((uint32_t) 500U)
#define USER_PHY_MAX_DEV_ADDR                   ((uint32_t) 31U)

typedef struct
{
  uint16_t regAddr;
  uint16_t mask;
  uint16_t value;
} user_phy_patch_t;

volatile uint32_t g_lan8671_stage;
volatile uint32_t g_lan8671_detail;
volatile uint32_t g_lan8671_vendor_patch_state;
volatile uint32_t g_lan8671_vendor_patch_count;

#if USER_PHY_ENABLE_VENDOR_PATCHES
static const user_phy_patch_t user_phy_vendor_patches[] = {
  {0x00D0U, 0x0E03U, 0x0002U},
  {0x00D1U, 0x0300U, 0x0000U},
  {0x0084U, 0xFFC0U, 0x3380U},
  {0x0085U, 0x000FU, 0x0006U},
  {0x008AU, 0xF800U, 0xC000U},
  {0x0087U, 0x801CU, 0x801CU},
  {0x0088U, 0x1FFFU, 0x033FU},
  {0x008BU, 0xFFFFU, 0x0404U},
  {0x0080U, 0x0600U, 0x0600U},
  {0x00F1U, 0x7F00U, 0x2400U},
  {0x0096U, 0x2000U, 0x2000U},
  {0x0099U, 0xFFFFU, 0x7F80U},
};
#endif

static int32_t USER_PHY_ReadMmdReg(user_phy_Object_t *pObj, uint32_t devAddr,
    uint32_t regAddr, uint32_t *value);
static int32_t USER_PHY_WriteMmdReg(user_phy_Object_t *pObj, uint32_t devAddr,
    uint32_t regAddr, uint32_t value);
static int32_t USER_PHY_ModifyMmdReg(user_phy_Object_t *pObj, uint32_t devAddr,
    uint32_t regAddr, uint32_t mask, uint32_t value);
static int32_t USER_PHY_ProbeAddress(user_phy_Object_t *pObj);
#if USER_PHY_ENABLE_VENDOR_PATCHES
static int32_t USER_PHY_ApplyVendorConfig(user_phy_Object_t *pObj);
#endif
static int32_t USER_PHY_ConfigurePlca(user_phy_Object_t *pObj);

int32_t USER_PHY_RegisterBusIO(user_phy_Object_t *pObj, user_phy_IOCtx_t *ioctx)
{
  if ((pObj == NULL) || (ioctx == NULL) || (ioctx->ReadReg == NULL)
      || (ioctx->WriteReg == NULL) || (ioctx->GetTick == NULL)) {
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

  g_lan8671_stage = 0x1000U;
  g_lan8671_detail = 0U;
  g_lan8671_vendor_patch_state = 0U;
  g_lan8671_vendor_patch_count = 0U;

  if (pObj == NULL) {
    g_lan8671_stage = 0x10EEU;
    return USER_PHY_STATUS_ERROR;
  }

  if (pObj->Is_Initialized != 0U) {
    return USER_PHY_STATUS_OK;
  }

  if ((pObj->IO.Init != NULL) && (pObj->IO.Init() < 0)) {
    return USER_PHY_STATUS_ERROR;
  }

  status = USER_PHY_ProbeAddress(pObj);
  if (status != USER_PHY_STATUS_OK) {
    pObj->DevAddr = 0U;
    g_lan8671_stage = 0x11F0U;
    g_lan8671_detail = (uint32_t) status;
  }
  else {
    g_lan8671_stage = 0x1100U;
    g_lan8671_detail = pObj->DevAddr;
  }

  g_lan8671_stage = 0x1200U;

#if USER_PHY_ENABLE_VENDOR_PATCHES
  status = USER_PHY_ApplyVendorConfig(pObj);
  if (status != USER_PHY_STATUS_OK) {
    g_lan8671_stage = 0x12F0U;
    g_lan8671_detail = (uint32_t) status;
    g_lan8671_vendor_patch_state = 0xFFFFFFFFU;
  }
  else {
    g_lan8671_stage = 0x1201U;
    g_lan8671_detail = sizeof(user_phy_vendor_patches) / sizeof(user_phy_vendor_patches[0]);
    g_lan8671_vendor_patch_state = 1U;
    g_lan8671_vendor_patch_count = sizeof(user_phy_vendor_patches) / sizeof(user_phy_vendor_patches[0]);
  }
#else
  g_lan8671_stage = 0x1201U;
  g_lan8671_detail = 0U;
#endif

  g_lan8671_stage = 0x1300U;
  status = USER_PHY_ConfigurePlca(pObj);
  if (status != USER_PHY_STATUS_OK) {
    g_lan8671_stage = 0x13F1U;
    g_lan8671_detail = (uint32_t) status;
  }
  else {
    g_lan8671_stage = 0x1301U;
  }

  pObj->Is_Initialized = 1U;
  g_lan8671_stage = 0x1FFFU;
  return USER_PHY_STATUS_OK;
}

int32_t USER_PHY_DeInit(user_phy_Object_t *pObj)
{
  if (pObj == NULL) {
    return USER_PHY_STATUS_ERROR;
  }

  if ((pObj->Is_Initialized != 0U) && (pObj->IO.DeInit != NULL)
      && (pObj->IO.DeInit() < 0)) {
    return USER_PHY_STATUS_ERROR;
  }

  pObj->Is_Initialized = 0U;
  return USER_PHY_STATUS_OK;
}

int32_t USER_PHY_SoftwareReset(user_phy_Object_t *pObj)
{
  uint32_t tickstart;
  uint32_t regvalue;

  if ((pObj == NULL) || (pObj->IO.WriteReg == NULL) || (pObj->IO.ReadReg == NULL)) {
    return USER_PHY_STATUS_ERROR;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_BCR, USER_PHY_BCR_SOFT_RESET) < 0) {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  tickstart = (uint32_t) pObj->IO.GetTick();

  do {
    if (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BCR, &regvalue) < 0) {
      return USER_PHY_STATUS_READ_ERROR;
    }

    if ((regvalue & USER_PHY_BCR_SOFT_RESET) == 0U) {
      return USER_PHY_STATUS_OK;
    }
  } while (((uint32_t) pObj->IO.GetTick() - tickstart) <= USER_PHY_SW_RESET_TIMEOUT_MS);

  return USER_PHY_STATUS_RESET_TIMEOUT;
}

int32_t USER_PHY_GetLinkState(user_phy_Object_t *pObj)
{
  uint32_t bsr;
  uint32_t plcaStatus;

  g_lan8671_stage = 0x2000U;

  if ((pObj == NULL) || (pObj->Is_Initialized == 0U)) {
    g_lan8671_stage = 0x20EEU;
    return USER_PHY_STATUS_ADDRESS_ERROR;
  }

  if (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BSR, &bsr) < 0) {
    g_lan8671_stage = 0x21EEU;
    return USER_PHY_STATUS_READ_ERROR;
  }

  if (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_BSR, &bsr) < 0) {
    g_lan8671_stage = 0x22EEU;
    return USER_PHY_STATUS_READ_ERROR;
  }

  g_lan8671_stage = 0x2100U;
  g_lan8671_detail = bsr & 0xFFFFU;

  if ((bsr & USER_PHY_BSR_LINK_STATUS) == 0U) {
    g_lan8671_stage = 0x2101U;
    return USER_PHY_STATUS_LINK_DOWN;
  }

  if (USER_PHY_ReadMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE, USER_PHY_PLCA_STS,
      &plcaStatus) != USER_PHY_STATUS_OK) {
    g_lan8671_stage = 0x2201U;
    return USER_PHY_STATUS_10MBITS_HALFDUPLEX;
  }

  g_lan8671_stage = 0x2200U;
  g_lan8671_detail = plcaStatus & 0xFFFFU;

  return USER_PHY_STATUS_10MBITS_HALFDUPLEX;
}

int32_t USER_PHY_SetLinkState(user_phy_Object_t *pObj, uint32_t LinkState)
{
  if (pObj == NULL) {
    return USER_PHY_STATUS_ERROR;
  }

  if (LinkState != USER_PHY_STATUS_10MBITS_HALFDUPLEX) {
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

static int32_t USER_PHY_ReadMmdReg(user_phy_Object_t *pObj, uint32_t devAddr,
    uint32_t regAddr, uint32_t *value)
{
  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMDCTRL,
      USER_PHY_MMDCTRL_FUNC_ADDR | (devAddr & USER_PHY_MMDCTRL_DEVAD_MASK)) < 0) {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMDAD, regAddr) < 0) {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMDCTRL,
      USER_PHY_MMDCTRL_FUNC_DATA_NO_POST_INC | (devAddr & USER_PHY_MMDCTRL_DEVAD_MASK)) < 0) {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (pObj->IO.ReadReg(pObj->DevAddr, USER_PHY_MMDAD, value) < 0) {
    return USER_PHY_STATUS_READ_ERROR;
  }

  *value &= 0xFFFFU;
  return USER_PHY_STATUS_OK;
}

static int32_t USER_PHY_WriteMmdReg(user_phy_Object_t *pObj, uint32_t devAddr,
    uint32_t regAddr, uint32_t value)
{
  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMDCTRL,
      USER_PHY_MMDCTRL_FUNC_ADDR | (devAddr & USER_PHY_MMDCTRL_DEVAD_MASK)) < 0) {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMDAD, regAddr) < 0) {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMDCTRL,
      USER_PHY_MMDCTRL_FUNC_DATA_NO_POST_INC | (devAddr & USER_PHY_MMDCTRL_DEVAD_MASK)) < 0) {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  if (pObj->IO.WriteReg(pObj->DevAddr, USER_PHY_MMDAD, value & 0xFFFFU) < 0) {
    return USER_PHY_STATUS_WRITE_ERROR;
  }

  return USER_PHY_STATUS_OK;
}

static int32_t USER_PHY_ModifyMmdReg(user_phy_Object_t *pObj, uint32_t devAddr,
    uint32_t regAddr, uint32_t mask, uint32_t value)
{
  uint32_t regValue;
  int32_t status;

  status = USER_PHY_ReadMmdReg(pObj, devAddr, regAddr, &regValue);
  if (status != USER_PHY_STATUS_OK) {
    return status;
  }

  regValue = (regValue & ~mask) | value;
  return USER_PHY_WriteMmdReg(pObj, devAddr, regAddr, regValue);
}

static int32_t USER_PHY_ProbeAddress(user_phy_Object_t *pObj)
{
  uint32_t addr;

  for (addr = 0U; addr <= USER_PHY_MAX_DEV_ADDR; addr++) {
    uint32_t id1;
    uint32_t id2;

    if (pObj->IO.ReadReg(addr, USER_PHY_PHYID1, &id1) < 0) {
      continue;
    }

    if (pObj->IO.ReadReg(addr, USER_PHY_PHYID2, &id2) < 0) {
      continue;
    }

    id1 &= 0xFFFFU;
    id2 &= 0xFFFFU;

    if ((id1 == USER_PHY_LAN8671_ID1)
        && ((id2 & USER_PHY_LAN8671_ID2_MASK) == USER_PHY_LAN8671_ID2_VALUE)) {
      pObj->DevAddr = addr;
      return USER_PHY_STATUS_OK;
    }
  }

  return USER_PHY_STATUS_ADDRESS_ERROR;
}

#if USER_PHY_ENABLE_VENDOR_PATCHES
static int32_t USER_PHY_ApplyVendorConfig(user_phy_Object_t *pObj)
{
  uint32_t index;

  for (index = 0U; index < (sizeof(user_phy_vendor_patches) / sizeof(user_phy_vendor_patches[0])); index++) {
    int32_t status = USER_PHY_ModifyMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE,
        user_phy_vendor_patches[index].regAddr,
        user_phy_vendor_patches[index].mask,
        user_phy_vendor_patches[index].value);
    if (status != USER_PHY_STATUS_OK) {
      return status;
    }
  }

  return USER_PHY_STATUS_OK;
}
#endif

static int32_t USER_PHY_ConfigurePlca(user_phy_Object_t *pObj)
{
  int32_t status;
  uint32_t plcaCtrl1 = (((uint32_t) USER_PHY_PLCA_NODE_COUNT) << 8)
      | USER_PHY_PLCA_NODE_ID;
  uint32_t plcaBurst = (((uint32_t) USER_PHY_PLCA_BURST_MAX_COUNT) << 8)
      | USER_PHY_PLCA_BURST_TIMER_DEFAULT;

  status = USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE,
      USER_PHY_PLCA_TOTMR, USER_PHY_PLCA_TOTMR_DEFAULT);
  if (status != USER_PHY_STATUS_OK) {
    return status;
  }

  status = USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE,
      USER_PHY_PLCA_BURST, plcaBurst);
  if (status != USER_PHY_STATUS_OK) {
    return status;
  }

  status = USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE,
      USER_PHY_PLCA_CTRL1, plcaCtrl1);
  if (status != USER_PHY_STATUS_OK) {
    return status;
  }

  return USER_PHY_WriteMmdReg(pObj, USER_PHY_VENDOR_MMD_DEVICE,
      USER_PHY_PLCA_CTRL0, USER_PHY_PLCA_CTRL0_EN);
}