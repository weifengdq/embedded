/**
 * \file IfxGeth_Phy_Yt8011an.h
 * \brief YT8011AN 100/1000BASE-T1 automotive PHY driver over GETH MDIO (clause 22)
 *
 * Strapping on this board (informational, not touched by SW):
 *  RCTL PU 4.7K (AutoMode), RXD3 PD (Slave), RXD2 PD (RGMII),
 *  RXD1 PD + RXD0 PU (PHYAD=1), RXCLK PU (RXC delay enable).
 * Init sequence follows YT8011Ax 应用说明 V2.2 §3 (basic + RGMII 3.3V + soft reset).
 */

#ifndef IFXGETH_ETH_PHY_YT8011AN_H
#define IFXGETH_ETH_PHY_YT8011AN_H 1

/******************************************************************************/
/*----------------------------------Includes----------------------------------*/
/******************************************************************************/
#include "Configuration.h"
#include "IfxCpu_cfg.h"
#include "IfxGeth_Eth.h"

#if defined(__DCC__)
    #if CPU_WHICH_SERVICE_ETHERNET == 0
	#pragma section DATA ".data_cpu0" ".bss_cpu0" far-absolute RW
    #pragma section CODE ".text_cpu0"
    #elif ((CPU_WHICH_SERVICE_ETHERNET == 1) && (CPU_WHICH_SERVICE_ETHERNET < IFXCPU_NUM_MODULES))
	#pragma section DATA ".data_cpu1" ".bss_cpu1" far-absolute RW
    #pragma section CODE ".text_cpu1"
    #elif ((CPU_WHICH_SERVICE_ETHERNET == 2) && (CPU_WHICH_SERVICE_ETHERNET < IFXCPU_NUM_MODULES))
	#pragma section DATA ".data_cpu2" ".bss_cpu2" far-absolute RW
    #pragma section CODE ".text_cpu2"
    #elif ((CPU_WHICH_SERVICE_ETHERNET == 3) && (CPU_WHICH_SERVICE_ETHERNET < IFXCPU_NUM_MODULES))
	#pragma section DATA ".data_cpu3" ".bss_cpu3" far-absolute RW
    #pragma section CODE ".text_cpu3"
    #elif ((CPU_WHICH_SERVICE_ETHERNET == 4) && (CPU_WHICH_SERVICE_ETHERNET < IFXCPU_NUM_MODULES))
	#pragma section DATA ".data_cpu4" ".bss_cpu4" far-absolute RW
    #pragma section CODE ".text_cpu4"
    #elif ((CPU_WHICH_SERVICE_ETHERNET == 5) && (CPU_WHICH_SERVICE_ETHERNET < IFXCPU_NUM_MODULES))
	#pragma section DATA ".data_cpu5" ".bss_cpu5" far-absolute RW
    #pragma section CODE ".text_cpu5"
    #endif
#endif

/******************************************************************************/
/*-----------------------------------Macros-----------------------------------*/
/******************************************************************************/
#define IFXGETH_PHY_YT8011AN_ADDR       1u      /* PHYAD strapped to 1 */

/* MII (clause 22) registers */
#define IFXGETH_PHY_YT8011AN_MII_BMCR   0x00u
#define IFXGETH_PHY_YT8011AN_MII_BMSR   0x01u
#define IFXGETH_PHY_YT8011AN_MII_PHYID1 0x02u
#define IFXGETH_PHY_YT8011AN_MII_PHYID2 0x03u
#define IFXGETH_PHY_YT8011AN_MII_SPEC_SR 0x11u  /* speed/master/link status */

/* Expected PHY ID */
#define IFXGETH_PHY_YT8011AN_PHYID1     0x4F51u
#define IFXGETH_PHY_YT8011AN_PHYID2     0x3A30u /* approx: type 0x30, rev may vary; compare top 12 bits */

/******************************************************************************/
/*------------------------------Global variables------------------------------*/
/******************************************************************************/
IFX_EXTERN uint32 IfxGeth_Eth_Phy_Yt8011an_iPhyInitDone;

/******************************************************************************/
/*-------------------------Function Prototypes--------------------------------*/
/******************************************************************************/
IFX_EXTERN uint32 IfxGeth_Eth_Phy_Yt8011an_init(void);
IFX_EXTERN void IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(uint32 layeraddr, uint32 regaddr, uint32 *pdata);
IFX_EXTERN void IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(uint32 layeraddr, uint32 regaddr, uint32 data);
/* Extended register (via mii 0x1e/0x1f) and MMD (via mii 0xd/0xe) helpers */
IFX_EXTERN uint32 IfxGeth_Eth_Phy_Yt8011an_read_ext(uint32 regaddr);
IFX_EXTERN void IfxGeth_Eth_Phy_Yt8011an_write_ext(uint32 regaddr, uint32 data);
IFX_EXTERN uint32 IfxGeth_Eth_Phy_Yt8011an_read_mmd(uint32 device, uint32 regaddr);
IFX_EXTERN void IfxGeth_Eth_Phy_Yt8011an_write_mmd(uint32 device, uint32 regaddr, uint32 data);
IFX_EXTERN void IfxGeth_Eth_Phy_Yt8011an_soft_reset(void);


#if defined(__DCC__)
#pragma section CODE
#pragma section DATA RW
#endif

#endif /* IFXGETH_ETH_PHY_YT8011AN_H */
