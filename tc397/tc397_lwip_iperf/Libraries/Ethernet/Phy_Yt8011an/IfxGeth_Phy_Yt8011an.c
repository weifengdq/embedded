/**
 * \file IfxGeth_Phy_Yt8011an.c
 * \brief YT8011AN init + MDIO access via GETH MAC_MDIO_ADDRESS/DATA
 *
 * Init sequence: YT8011Ax 应用说明 V2.2 §3 基本配置 + RGMII 3.3V 配置 + 软复位.
 * Board strapping already selects Slave/RGMII/PHYAD=1/RXC-delay; SW does not
 * touch strapping, only registers. nRST (P20.1) is toggled by the caller
 * (netif low_level_init) before this init runs; nINT (P10.8) is left unused.
 */

/******************************************************************************/
/*----------------------------------Includes----------------------------------*/
/******************************************************************************/

#include "IfxGeth_Phy_Yt8011an.h"

/******************************************************************************/
/*----------------------------------Macros------------------------------------*/
/******************************************************************************/

#define IFXGETH_PHY_YT8011AN_WAIT_MDIO_READY() while (GETH_MAC_MDIO_ADDRESS.B.GB) {}

/******************************************************************************/
/*------------------------------Global variables------------------------------*/
/******************************************************************************/
#if CPU_WHICH_SERVICE_ETHERNET == 0
    #if defined(__GNUC__)
    #pragma section ".text_cpu0" ax
    #endif
    #if defined(__TASKING__)
    #pragma section code    "text_cpu0"
    #pragma section farbss  "bss_cpu0"
    #pragma section fardata "data_cpu0"
    #endif
    #if defined(__DCC__)
    #pragma section CODE ".text_cpu0"
    #pragma section DATA ".data_cpu0" ".bss_cpu0" far-absolute RW
    #endif
    #if defined(__ghs__)
    #pragma ghs section text=".text_cpu0"
    #pragma ghs section bss= ".bss_cpu0"
    #pragma ghs section data=".data_cpu0"
    #endif
#elif ((CPU_WHICH_SERVICE_ETHERNET == 1) && (CPU_WHICH_SERVICE_ETHERNET < IFXCPU_NUM_MODULES))
    #if defined(__GNUC__)
    #pragma section ".text_cpu1" ax
    #pragma section ".bss_cpu1" awc1
    #endif
    #if defined(__TASKING__)
    #pragma section code    "text_cpu1"
    #pragma section farbss  "bss_cpu1"
    #pragma section fardata "data_cpu1"
    #endif
    #if defined(__DCC__)
    #pragma section CODE ".text_cpu1"
    #pragma section DATA ".data_cpu1" ".bss_cpu1" far-absolute RW
    #endif
    #if defined(__ghs__)
    #pragma ghs section text=".text_cpu1"
    #pragma ghs section bss= ".bss_cpu1"
    #pragma ghs section data=".data_cpu1"
    #endif
#elif ((CPU_WHICH_SERVICE_ETHERNET == 2) && (CPU_WHICH_SERVICE_ETHERNET < IFXCPU_NUM_MODULES))
    #if defined(__GNUC__)
    #pragma section ".text_cpu2" ax
    #pragma section ".bss_cpu2" awc2
    #endif
    #if defined(__TASKING__)
    #pragma section code    "text_cpu2"
    #pragma section farbss  "bss_cpu2"
    #pragma section fardata "data_cpu2"
    #endif
    #if defined(__DCC__)
    #pragma section CODE ".text_cpu2"
    #pragma section DATA ".data_cpu2" ".bss_cpu2" far-absolute RW
    #endif
    #if defined(__ghs__)
    #pragma ghs section text=".text_cpu2"
    #pragma ghs section bss= ".bss_cpu2"
    #pragma ghs section data=".data_cpu2"
    #endif
#elif ((CPU_WHICH_SERVICE_ETHERNET == 3) && (CPU_WHICH_SERVICE_ETHERNET < IFXCPU_NUM_MODULES))
	#if defined(__GNUC__)
    #pragma section ".text_cpu3" ax
	#pragma section ".bss_cpu3" awc3
	#endif
	#if defined(__TASKING__)
    #pragma section code    "text_cpu3"
    #pragma section farbss  "bss_cpu3"
    #pragma section fardata "data_cpu3"
	#endif
	#if defined(__DCC__)
    #pragma section CODE ".text_cpu3"
    #pragma section DATA ".data_cpu3" ".bss_cpu3" far-absolute RW
	#endif
    #if defined(__ghs__)
    #pragma ghs section text=".text_cpu3"
    #pragma ghs section bss= ".bss_cpu3"
    #pragma ghs section data=".data_cpu3"
    #endif
#elif ((CPU_WHICH_SERVICE_ETHERNET == 4) && (CPU_WHICH_SERVICE_ETHERNET < IFXCPU_NUM_MODULES))
	#if defined(__GNUC__)
    #pragma section ".text_cpu4" ax
	#pragma section ".bss_cpu4" awc4
	#endif
	#if defined(__TASKING__)
    #pragma section code    "text_cpu4"
    #pragma section farbss  "bss_cpu4"
    #pragma section fardata "data_cpu4"
	#endif
	#if defined(__DCC__)
    #pragma section CODE ".text_cpu4"
    #pragma section DATA ".data_cpu4" ".bss_cpu4" far-absolute RW
	#endif
    #if defined(__ghs__)
    #pragma ghs section text=".text_cpu4"
    #pragma ghs section bss= ".bss_cpu4"
    #pragma ghs section data=".data_cpu4"
    #endif
#elif ((CPU_WHICH_SERVICE_ETHERNET == 5) && (CPU_WHICH_SERVICE_ETHERNET < IFXCPU_NUM_MODULES))
	#if defined(__GNUC__)
    #pragma section ".text_cpu5" ax
	#pragma section ".bss_cpu5" awc5
	#endif
	#if defined(__TASKING__)
    #pragma section code    "text_cpu5"
    #pragma section farbss  "bss_cpu5"
    #pragma section fardata "data_cpu5"
	#endif
	#if defined(__DCC__)
    #pragma section CODE ".text_cpu5"
    #pragma section DATA ".data_cpu5" ".bss_cpu5" far-absolute RW
	#endif
    #if defined(__ghs__)
    #pragma ghs section text=".text_cpu5"
    #pragma ghs section bss= ".bss_cpu5"
    #pragma ghs section data=".data_cpu5"
    #endif
#else
#error "Set CPU_WHICH_SERVICE_ETHERNET to a valid value!"
#endif

/******************************************************************************/
/*-----------------------Exported Variables/Constants-------------------------*/
/******************************************************************************/

uint32 IfxGeth_Eth_Phy_Yt8011an_iPhyInitDone = 0;

/******************************************************************************/
/*------------------------Private Variables/Constants-------------------------*/
/******************************************************************************/
#if defined(__GNUC__)
    #pragma section // end bss section
#endif

/******************************************************************************/
/*-------------------------Function Implementations---------------------------*/
/******************************************************************************/

void IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(uint32 layeraddr, uint32 regaddr, uint32 *pdata)
{
    /* 5bit Physical Layer Address, 5bit GMII Regnr, 4bit csrclock divider, Read, Busy */
	GETH_MAC_MDIO_ADDRESS.U = (layeraddr << 21) | (regaddr << 16) | (0 << 8) | (3 << 2) | (1 << 0);

	IFXGETH_PHY_YT8011AN_WAIT_MDIO_READY();

    /* get data */
    *pdata = GETH_MAC_MDIO_DATA.U;
}


void IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(uint32 layeraddr, uint32 regaddr, uint32 data)
{
    /* put data */
	GETH_MAC_MDIO_DATA.U = data;

    /* 5bit Physical Layer Address, 5bit GMII Regnr, 4bit csrclock divider, Write, Busy */
    GETH_MAC_MDIO_ADDRESS.U = (layeraddr << 21) | (regaddr << 16) | (0 << 8) |  (1 << 2) | (1 << 0);

    IFXGETH_PHY_YT8011AN_WAIT_MDIO_READY();
}


uint32 IfxGeth_Eth_Phy_Yt8011an_read_ext(uint32 regaddr)
{
    uint32 d;
    IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR, 0x1e, regaddr);
    IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR, 0x1f, &d);
    return d & 0xFFFFu;
}


void IfxGeth_Eth_Phy_Yt8011an_write_ext(uint32 regaddr, uint32 data)
{
    IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR, 0x1e, regaddr);
    IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR, 0x1f, data);
}


uint32 IfxGeth_Eth_Phy_Yt8011an_read_mmd(uint32 device, uint32 regaddr)
{
    uint32 d;
    IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR, 0x0d, device);
    IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR, 0x0e, regaddr);
    IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR, 0x0d, 0x4000u + device);
    IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR, 0x0e, &d);
    return d & 0xFFFFu;
}


void IfxGeth_Eth_Phy_Yt8011an_write_mmd(uint32 device, uint32 regaddr, uint32 data)
{
    IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR, 0x0d, device);
    IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR, 0x0e, regaddr);
    IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR, 0x0d, 0x4000u + device);
    IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR, 0x0e, data);
}


void IfxGeth_Eth_Phy_Yt8011an_soft_reset(void)
{
    uint32 v;
    IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR,
        IFXGETH_PHY_YT8011AN_MII_BMCR, &v);
    IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR,
        IFXGETH_PHY_YT8011AN_MII_BMCR, (v | 0x8000u));
    do
    {
        IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR,
            IFXGETH_PHY_YT8011AN_MII_BMCR, &v);
    } while (v & 0x8000u);
}


uint32 IfxGeth_Eth_Phy_Yt8011an_init(void)
{
    IFXGETH_PHY_YT8011AN_WAIT_MDIO_READY();

    /* Wait until clause-22 is responsive (PHY out of hardware reset).
     * BMCR reads 0xFFFF while MDIO has no responder; spin briefly. */
    {
        uint32 v = 0, tries = 0;
        do
        {
            IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR,
                IFXGETH_PHY_YT8011AN_MII_BMCR, &v);
            v &= 0xFFFFu;
            if ((v != 0xFFFFu) && (v != 0x0000u))
            {
                break;
            }
        } while (++tries < 100000u);
    }

    /* ---- §3 基本配置 ---- */
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x1008, 0x2119); /* 降功耗 */
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x1092, 0x0712);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x90bc, 0x7676); /* Under Voltage */
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x90b9, 0x620b);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x2001, 0x6418); /* 调小 phyc send_s */
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x1019, 0x3712); /* 100M template psd */
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x101a, 0x3713);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x2005, 0x0810); /* IOP */
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x2015, 0x1012);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x2013, 0xff06); /* Link Up */
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x3017, 0x0004); /* 100M training 优化 */
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x3027, 0xffe8);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x3026, 0x1b58);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x301e, 0x0b40);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x3019, 0xffd4);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x3014, 0x1115);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x301a, 0x7800);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x1000, 0x0028); /* ADC Manual */
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x1053, 0x000f); /* PLL */
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x105e, 0xa46c);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x1088, 0x002b); /* Sleep 相关 */
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x1088, 0x002b);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x1088, 0x000b);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x3008, 0x0141);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x3009, 0x1918);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x9095, 0x1a1a); /* CSD 阈值 */
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x9096, 0x1a10);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x9097, 0x101a);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x9098, 0x01ff);

    /* ---- §3 RGMII 配置 (VDDIO=3.3V) ---- */
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x9000, 0x8000);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x0062, 0x0000);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x9000, 0x0000);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x9031, 0xb200);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x903b, 0x0040);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x903e, 0x3b3b);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x903c, 0x000f);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x903d, 0x1000);
    IfxGeth_Eth_Phy_Yt8011an_write_ext(0x9038, 0x0000);

    /* ---- RGMII TX delay: 0x9001[7:4], 125ps/step, max ~2ns.
     * RGMII requires ~2ns TXC skew; the MAC (SKEWCTL=0, same as tc387_2)
     * adds none, so the PHY must. Keep bit[8] (RX delay from strapping). */
    {
        uint32 v9001 = IfxGeth_Eth_Phy_Yt8011an_read_ext(0x9001);
        IfxGeth_Eth_Phy_Yt8011an_write_ext(0x9001, (v9001 & ~0xF0u) | 0xF0u);
    }

    /* ---- 软复位 (mii 0x0 = 0x8140: reset + autoneg enable) ---- */
    IfxGeth_Eth_Phy_Yt8011an_write_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR,
        IFXGETH_PHY_YT8011AN_MII_BMCR, 0x8140u);
    {
        uint32 v;
        uint32 tries = 0;
        do
        {
            IfxGeth_Eth_Phy_Yt8011an_read_mdio_reg(IFXGETH_PHY_YT8011AN_ADDR,
                IFXGETH_PHY_YT8011AN_MII_BMCR, &v);
            if (++tries > 1000000u)
            {
                break;
            }
        } while (v & 0x8000u);
    }

    /* done */
    IfxGeth_Eth_Phy_Yt8011an_iPhyInitDone = 1;

    return 1;
}


#if defined(__GNUC__)
#pragma section // end text section
#endif
#if defined(__TASKING__)
#pragma section code restore
#pragma section fardata restore
#pragma section farbss restore
#endif
#if defined(__DCC__)
#pragma section CODE
#pragma section DATA RW
#endif
#if defined(__ghs__)
#pragma ghs section text=default
#pragma ghs section data=default
#pragma ghs section bss=default
#endif
