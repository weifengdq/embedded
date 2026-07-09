#ifndef ETH_CUSTOM_PHY_INTERFACE_H
#define ETH_CUSTOM_PHY_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define USER_PHY_BCR                               ((uint16_t)0x0000U)
#define USER_PHY_BSR                               ((uint16_t)0x0001U)
#define USER_PHY_PHYID1                            ((uint16_t)0x0002U)
#define USER_PHY_PHYID2                            ((uint16_t)0x0003U)
#define USER_PHY_MMDCTRL                           ((uint16_t)0x000DU)
#define USER_PHY_MMDAD                             ((uint16_t)0x000EU)

#define USER_PHY_MMDCTRL_FUNC_ADDR                 ((uint16_t)0x0000U)
#define USER_PHY_MMDCTRL_FUNC_DATA_NO_POST_INC     ((uint16_t)0x4000U)
#define USER_PHY_MMDCTRL_DEVAD_MASK                ((uint16_t)0x001FU)

#define USER_PHY_PMA_MMD_DEVICE                    ((uint16_t)0x0001U)
#define USER_PHY_PCS_MMD_DEVICE                    ((uint16_t)0x0003U)
#define USER_PHY_VENDOR_MMD_DEVICE                 ((uint16_t)0x001FU)

#define USER_PHY_STS1                              ((uint16_t)0x0018U)
#define USER_PHY_STS2                              ((uint16_t)0x0019U)
#define USER_PHY_STS3                              ((uint16_t)0x001AU)
#define USER_PHY_IMSK1                             ((uint16_t)0x001CU)

#define USER_PHY_CDCTL0                            ((uint16_t)0x0087U)
#define USER_PHY_SQICTL                            ((uint16_t)0x00A0U)
#define USER_PHY_SQISTS0                           ((uint16_t)0x00A1U)

#define USER_PHY_PLCA_CTRL0                        ((uint16_t)0xCA01U)
#define USER_PHY_PLCA_CTRL1                        ((uint16_t)0xCA02U)
#define USER_PHY_PLCA_STS                          ((uint16_t)0xCA03U)
#define USER_PHY_PLCA_TOTMR                        ((uint16_t)0xCA04U)
#define USER_PHY_PLCA_BURST                        ((uint16_t)0xCA05U)

#define USER_PHY_T1SPCSCTL                         ((uint16_t)0x08F3U)

#define USER_PHY_CFG_D0                            ((uint16_t)0x00D0U)
#define USER_PHY_CFG_84                            ((uint16_t)0x0084U)
#define USER_PHY_CFG_8A                            ((uint16_t)0x008AU)
#define USER_PHY_CFG_81                            ((uint16_t)0x0081U)
#define USER_PHY_CFG_91                            ((uint16_t)0x0091U)
#define USER_PHY_CFG_E0                            ((uint16_t)0x00E0U)
#define USER_PHY_CFG_E9                            ((uint16_t)0x00E9U)
#define USER_PHY_CFG_F4                            ((uint16_t)0x00F4U)
#define USER_PHY_CFG_F5                            ((uint16_t)0x00F5U)
#define USER_PHY_CFG_F8                            ((uint16_t)0x00F8U)
#define USER_PHY_CFG_F9                            ((uint16_t)0x00F9U)

#define USER_PHY_SQI_AD                            ((uint16_t)0x00ADU)
#define USER_PHY_SQI_AE                            ((uint16_t)0x00AEU)
#define USER_PHY_SQI_AF                            ((uint16_t)0x00AFU)
#define USER_PHY_SQI_B0                            ((uint16_t)0x00B0U)
#define USER_PHY_SQI_B1                            ((uint16_t)0x00B1U)
#define USER_PHY_SQI_B2                            ((uint16_t)0x00B2U)
#define USER_PHY_SQI_B3                            ((uint16_t)0x00B3U)
#define USER_PHY_SQI_B4                            ((uint16_t)0x00B4U)
#define USER_PHY_SQI_B5                            ((uint16_t)0x00B5U)
#define USER_PHY_SQI_B6                            ((uint16_t)0x00B6U)
#define USER_PHY_SQI_B7                            ((uint16_t)0x00B7U)
#define USER_PHY_SQI_B8                            ((uint16_t)0x00B8U)
#define USER_PHY_SQI_B9                            ((uint16_t)0x00B9U)
#define USER_PHY_SQI_BA                            ((uint16_t)0x00BAU)
#define USER_PHY_SQI_BB                            ((uint16_t)0x00BBU)

#define USER_PHY_BCR_SOFT_RESET                    ((uint16_t)0x8000U)
#define USER_PHY_BSR_LINK_STATUS                   ((uint16_t)0x0004U)

#define USER_PHY_STS1_SQI                          ((uint16_t)0x1000U)
#define USER_PHY_STS1_PSTC                         ((uint16_t)0x0800U)
#define USER_PHY_STS1_TXCOL                        ((uint16_t)0x0400U)
#define USER_PHY_STS1_TXJAB                        ((uint16_t)0x0200U)
#define USER_PHY_STS1_TSSI                         ((uint16_t)0x0100U)
#define USER_PHY_STS1_EMPCYC                       ((uint16_t)0x0080U)
#define USER_PHY_STS1_RXINTO                       ((uint16_t)0x0040U)
#define USER_PHY_STS1_UNEXPB                       ((uint16_t)0x0020U)
#define USER_PHY_STS1_BCNBFTO                      ((uint16_t)0x0010U)
#define USER_PHY_STS1_UNCRS                        ((uint16_t)0x0008U)
#define USER_PHY_STS1_PLCASYM                      ((uint16_t)0x0004U)
#define USER_PHY_STS1_ESDERR                       ((uint16_t)0x0002U)
#define USER_PHY_STS1_DEC5B                        ((uint16_t)0x0001U)

#define USER_PHY_STS2_RESETC                       ((uint16_t)0x0800U)
#define USER_PHY_STS2_WKEMDI                       ((uint16_t)0x0400U)
#define USER_PHY_STS2_WKEWI                        ((uint16_t)0x0200U)
#define USER_PHY_STS2_UV33                         ((uint16_t)0x0100U)
#define USER_PHY_STS2_OT                           ((uint16_t)0x0040U)
#define USER_PHY_STS2_IWDTO                        ((uint16_t)0x0020U)

#define USER_PHY_SQICTL_SQIRST                     ((uint16_t)0x8000U)
#define USER_PHY_SQICTL_SQIEN                      ((uint16_t)0x4000U)

#define USER_PHY_IMSK1_SQIM                        ((uint16_t)0x1000U)
#define USER_PHY_IMSK1_PSTCM                       ((uint16_t)0x0800U)
#define USER_PHY_IMSK1_TXCOLM                      ((uint16_t)0x0400U)
#define USER_PHY_IMSK1_TXJABM                      ((uint16_t)0x0200U)
#define USER_PHY_IMSK1_TSSIM                       ((uint16_t)0x0100U)
#define USER_PHY_IMSK1_EMPCYCM                     ((uint16_t)0x0080U)
#define USER_PHY_IMSK1_RXINTOM                     ((uint16_t)0x0040U)
#define USER_PHY_IMSK1_UNEXPBM                     ((uint16_t)0x0020U)
#define USER_PHY_IMSK1_BCNBFTO                     ((uint16_t)0x0010U)
#define USER_PHY_IMSK1_UNCRSM                      ((uint16_t)0x0008U)
#define USER_PHY_IMSK1_PLCASYMM                    ((uint16_t)0x0004U)
#define USER_PHY_IMSK1_ESDERRM                     ((uint16_t)0x0002U)
#define USER_PHY_IMSK1_DEC5BM                      ((uint16_t)0x0001U)

#define USER_PHY_SQISTS0_SQIERR                    ((uint16_t)0x0080U)
#define USER_PHY_SQISTS0_SQIVLD                    ((uint16_t)0x0040U)
#define USER_PHY_SQISTS0_SQIVAL_MASK               ((uint16_t)0x0038U)
#define USER_PHY_SQISTS0_SQIVAL_SHIFT              (3U)
#define USER_PHY_SQISTS0_SQIERRC_MASK              ((uint16_t)0x0007U)

#define USER_PHY_CDCTL0_CDEN                       ((uint16_t)0x8000U)

#define USER_PHY_PLCA_CTRL0_EN                     ((uint16_t)0x8000U)
#define USER_PHY_PLCA_STS_PST                      ((uint16_t)0x8000U)

#define USER_PHY_T1SPCSCTL_LBE                     ((uint16_t)0x4000U)

#define USER_PHY_LAN8671_ID1                       ((uint16_t)0x0007U)
#define USER_PHY_LAN8671_ID2_MASK                  ((uint16_t)0xFFF0U)
#define USER_PHY_LAN8671_ID2_VALUE                 ((uint16_t)0xC160U)

#define USER_PHY_DEFAULT_PLCA_ENABLED              1U
#define USER_PHY_DEFAULT_PLCA_NODE_ID              3U
#define USER_PHY_DEFAULT_PLCA_NODE_COUNT           8U
#define USER_PHY_DEFAULT_PLCA_TOTMR                0x20U
#define USER_PHY_DEFAULT_PLCA_BURST_COUNT          0x00U
#define USER_PHY_DEFAULT_PLCA_BURST_TIMER          0x80U
#define USER_PHY_DEFAULT_PSTC_IRQ_ENABLED          1U
#define USER_PHY_DEFAULT_COLLISION_AUTO            1U
#define USER_PHY_ENABLE_SQI_CONFIG                 1U

#define USER_PHY_STATUS_TIMEOUT                    ((int32_t)-6)
#define USER_PHY_STATUS_READ_ERROR                 ((int32_t)-5)
#define USER_PHY_STATUS_WRITE_ERROR                ((int32_t)-4)
#define USER_PHY_STATUS_ADDRESS_ERROR              ((int32_t)-3)
#define USER_PHY_STATUS_RESET_TIMEOUT              ((int32_t)-2)
#define USER_PHY_STATUS_ERROR                      ((int32_t)-1)
#define USER_PHY_STATUS_OK                         ((int32_t)0)
#define USER_PHY_STATUS_LINK_DOWN                  ((int32_t)1)
#define USER_PHY_STATUS_100MBITS_FULLDUPLEX        ((int32_t)2)
#define USER_PHY_STATUS_100MBITS_HALFDUPLEX        ((int32_t)3)
#define USER_PHY_STATUS_10MBITS_FULLDUPLEX         ((int32_t)4)
#define USER_PHY_STATUS_10MBITS_HALFDUPLEX         ((int32_t)5)
#define USER_PHY_STATUS_AUTONEGO_NOTDONE           ((int32_t)6)

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

typedef struct
{
  uint8_t enabled;
  uint8_t node_id;
  uint8_t node_count;
  uint8_t totmr;
  uint8_t burst_count;
  uint8_t burst_timer;
  uint8_t pstc_irq_enabled;
  uint8_t collision_auto;
} user_phy_plca_config_t;

typedef struct
{
  uint32_t phy_id1;
  uint32_t phy_id2;
  uint32_t bcr;
  uint32_t bsr;
  uint32_t status1;
  uint32_t status2;
  uint32_t status3;
  uint32_t imsk1;
  uint32_t sqi_status;
  uint32_t plca_ctrl0;
  uint32_t plca_ctrl1;
  uint32_t plca_status;
  uint32_t plca_totmr;
  uint32_t plca_burst;
  uint32_t cdctl0;
} user_phy_status_snapshot_t;

int32_t USER_PHY_RegisterBusIO(user_phy_Object_t *pObj, user_phy_IOCtx_t *ioctx);
int32_t USER_PHY_Init(user_phy_Object_t *pObj);
int32_t USER_PHY_DeInit(user_phy_Object_t *pObj);
int32_t USER_PHY_SoftwareReset(user_phy_Object_t *pObj);
int32_t USER_PHY_Configure(user_phy_Object_t *pObj);
int32_t USER_PHY_GetLinkState(user_phy_Object_t *pObj);
int32_t USER_PHY_SetLinkState(user_phy_Object_t *pObj, uint32_t LinkState);
int32_t USER_PHY_EnableLoopbackMode(user_phy_Object_t *pObj);
int32_t USER_PHY_DisableLoopbackMode(user_phy_Object_t *pObj);
int32_t USER_PHY_ReadC45(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint32_t *value);
int32_t USER_PHY_WriteC45(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint16_t value);
int32_t USER_PHY_ModifyC45(user_phy_Object_t *pObj, uint16_t devad, uint16_t reg, uint16_t clear_mask, uint16_t set_mask);
int32_t USER_PHY_ReadStatusSnapshot(user_phy_Object_t *pObj, user_phy_status_snapshot_t *snapshot);
int32_t USER_PHY_GetPlcaConfig(user_phy_Object_t *pObj, user_phy_plca_config_t *config);
int32_t USER_PHY_SetPlcaConfig(user_phy_Object_t *pObj, const user_phy_plca_config_t *config, uint8_t apply_now);
int32_t USER_PHY_SyncCollisionDetection(user_phy_Object_t *pObj, uint8_t force, uint32_t *status1, uint32_t *plca_status, uint32_t *cdctl0);

#ifdef __cplusplus
}
#endif

#endif