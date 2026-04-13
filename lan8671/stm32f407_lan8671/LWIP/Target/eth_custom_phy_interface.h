#ifndef ETH_CUSTOM_PHY_INTERFACE_H
#define ETH_CUSTOM_PHY_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define USER_PHY_BCR                               ((uint16_t) 0x0000U)
#define USER_PHY_BSR                               ((uint16_t) 0x0001U)
#define USER_PHY_PHYID1                            ((uint16_t) 0x0002U)
#define USER_PHY_PHYID2                            ((uint16_t) 0x0003U)
#define USER_PHY_MMDCTRL                           ((uint16_t) 0x000DU)
#define USER_PHY_MMDAD                             ((uint16_t) 0x000EU)

#define USER_PHY_BCR_SOFT_RESET                    ((uint16_t) 0x8000U)
#define USER_PHY_BSR_LINK_STATUS                   ((uint16_t) 0x0004U)

#define USER_PHY_MMDCTRL_FUNC_ADDR                 ((uint16_t) 0x0000U)
#define USER_PHY_MMDCTRL_FUNC_DATA_NO_POST_INC     ((uint16_t) 0x4000U)
#define USER_PHY_MMDCTRL_DEVAD_MASK                ((uint16_t) 0x001FU)

#define USER_PHY_VENDOR_MMD_DEVICE                 ((uint16_t) 0x001FU)
#define USER_PHY_PCS_MMD_DEVICE                    ((uint16_t) 0x0002U)

#define USER_PHY_T1SPCSCTL                         ((uint16_t) 0x08F3U)
#define USER_PHY_T1SPCSCTL_LBE                     ((uint16_t) 0x4000U)

#define USER_PHY_PLCA_CTRL0                        ((uint16_t) 0xCA01U)
#define USER_PHY_PLCA_CTRL1                        ((uint16_t) 0xCA02U)
#define USER_PHY_PLCA_STS                          ((uint16_t) 0xCA03U)
#define USER_PHY_PLCA_TOTMR                        ((uint16_t) 0xCA04U)
#define USER_PHY_PLCA_BURST                        ((uint16_t) 0xCA05U)

#define USER_PHY_PLCA_CTRL0_EN                     ((uint16_t) 0x8000U)
#define USER_PHY_PLCA_STS_PST                      ((uint16_t) 0x8000U)

#define USER_PHY_LAN8671_ID1                       ((uint16_t) 0x0007U)
#define USER_PHY_LAN8671_ID2_MASK                  ((uint16_t) 0xFFF0U)
#define USER_PHY_LAN8671_ID2_VALUE                 ((uint16_t) 0xC160U)

#define USER_PHY_PLCA_NODE_ID                      ((uint8_t) 3U)
#define USER_PHY_PLCA_NODE_COUNT                   ((uint8_t) 8U)
#define USER_PHY_PLCA_TOTMR_DEFAULT                ((uint8_t) 0x20U)
#define USER_PHY_PLCA_BURST_MAX_COUNT              ((uint8_t) 0x00U)
#define USER_PHY_PLCA_BURST_TIMER_DEFAULT          ((uint8_t) 0x80U)

#define USER_PHY_ENABLE_VENDOR_PATCHES             0U

#define USER_PHY_STATUS_READ_ERROR                 ((int32_t) -5)
#define USER_PHY_STATUS_WRITE_ERROR                ((int32_t) -4)
#define USER_PHY_STATUS_ADDRESS_ERROR              ((int32_t) -3)
#define USER_PHY_STATUS_RESET_TIMEOUT              ((int32_t) -2)
#define USER_PHY_STATUS_ERROR                      ((int32_t) -1)
#define USER_PHY_STATUS_OK                         ((int32_t)  0)
#define USER_PHY_STATUS_LINK_DOWN                  ((int32_t)  1)
#define USER_PHY_STATUS_100MBITS_FULLDUPLEX        ((int32_t)  2)
#define USER_PHY_STATUS_100MBITS_HALFDUPLEX        ((int32_t)  3)
#define USER_PHY_STATUS_10MBITS_FULLDUPLEX         ((int32_t)  4)
#define USER_PHY_STATUS_10MBITS_HALFDUPLEX         ((int32_t)  5)
#define USER_PHY_STATUS_AUTONEGO_NOTDONE           ((int32_t)  6)

typedef int32_t (*USER_PHY_Init_Func)(void);
typedef int32_t (*USER_PHY_DeInit_Func)(void);
typedef int32_t (*USER_PHY_ReadReg_Func)(uint32_t, uint32_t, uint32_t *);
typedef int32_t (*USER_PHY_WriteReg_Func)(uint32_t, uint32_t, uint32_t);
typedef int32_t (*USER_PHY_GetTick_Func)(void);

typedef struct
{
  USER_PHY_Init_Func Init;
  USER_PHY_DeInit_Func DeInit;
  USER_PHY_WriteReg_Func WriteReg;
  USER_PHY_ReadReg_Func ReadReg;
  USER_PHY_GetTick_Func GetTick;
} user_phy_IOCtx_t;

typedef struct
{
  uint32_t DevAddr;
  uint32_t Is_Initialized;
  user_phy_IOCtx_t IO;
  void *pData;
} user_phy_Object_t;

int32_t USER_PHY_RegisterBusIO(user_phy_Object_t *pObj, user_phy_IOCtx_t *ioctx);
int32_t USER_PHY_Init(user_phy_Object_t *pObj);
int32_t USER_PHY_DeInit(user_phy_Object_t *pObj);
int32_t USER_PHY_SoftwareReset(user_phy_Object_t *pObj);
int32_t USER_PHY_GetLinkState(user_phy_Object_t *pObj);
int32_t USER_PHY_SetLinkState(user_phy_Object_t *pObj, uint32_t LinkState);
int32_t USER_PHY_EnableLoopbackMode(user_phy_Object_t *pObj);
int32_t USER_PHY_DisableLoopbackMode(user_phy_Object_t *pObj);

#ifdef __cplusplus
}
#endif

#endif