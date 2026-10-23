// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Microchip 10BASE-T1S PHYs
 *
 * Support: Microchip Phys:
 *  lan8670/1/2 Rev.B1/C1/C2/D0
 *  lan8650/1 Rev.B0/B1 Internal PHYs
 */

#include <linux/kernel.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/phy.h>

#define PHY_ID_LAN867X_REVB1 0x0007C162
#define PHY_ID_LAN867X_REVC1 0x0007C164
#define PHY_ID_LAN867X_REVC2 0x0007C165
#define PHY_ID_LAN867X_REVD0 0x0007C166
/* Both Rev.B0 and B1 clause 22 PHYID's are same due to B1 chip limitation */
#define PHY_ID_LAN865X_REVB 0x0007C1B3

#define LAN867X_REG_STS2 0x0019

#define LAN867x_RESET_COMPLETE_STS BIT(11)

#define LAN865X_REG_CFGPARAM_ADDR 0x00D8
#define LAN865X_REG_CFGPARAM_DATA 0x00D9
#define LAN865X_REG_CFGPARAM_CTRL 0x00DA
#define LAN865X_REG_STS2 0x0019

/* PHY interrupt status and mask registers (MDIO_MMD_VEND2). The status bits
 * are read-to-clear; a mask bit is enabled by writing 0.
 */
#define LAN86XX_REG_STS1		0x0018
#define LAN86XX_REG_IMSK1		0x001C

#define LAN86XX_STS1_LINK_STS_CHANGED	BIT(13)
#define LAN86XX_STS1_PLCA_STS_CHANGED	BIT(11)

/* Collision Detector Control 0 Register */
#define LAN86XX_REG_COL_DET_CTRL0	0x0087
#define COL_DET_CTRL0_ENABLE_BIT_MASK	BIT(15)
#define COL_DET_ENABLE			BIT(15)
#define COL_DET_DISABLE			0x0000
#define COL_DET_CTRL0_CCMFC_MASK	GENMASK(10, 9)
/* OA default: collisions gated by PLCA_Status in hardware */
#define COL_DET_CTRL0_CCMFC_OA_DEFAULT	BIT(9)

/* LAN8670/1/2 Rev.D0 Link Status Selection Register */
#define LAN867X_REG_LINK_STATUS		0x0012
#define LINK_ACTIVE_SEL			GENMASK(12, 11)
#define LINK_SEMAPHORE			BIT(0)

/* Vendor Specific Deep Sleep Ctrl 0 Register */
#define LAN86XX_DEEP_SLEEP_CTRL0	0x0080
#define DEEP_SLEEP_CTRL0_WUP		BIT(14)
#define DEEP_SLEEP_CTRL0_WAKEIN		BIT(15)

/* Vendor Specific Deep Sleep Ctrl 1 Register */
#define LAN86XX_DEEP_SLEEP_CTRL1	0x0081
#define DEEP_SLEEP_CTRL1_WAKE_FWD	BIT(0)

/* OA TC14 Wake Sleep Ctrl Register */
#define MDIO_OATC14_WS_CONTROL		0xD001
#define WS_CONTROL_LPREQ		BIT(15) /* Request low power */
#define WS_CONTROL_LPEXIT		BIT(14) /* Wakeout nodes in n/w */

#define LAN865X_CFGPARAM_READ_ENABLE BIT(1)

/* The arrays below are pulled from the following table from AN1699
 * Access MMD Address Value Mask
 * RMW 0x1F 0x00D0 0x0002 0x0E03
 * RMW 0x1F 0x00D1 0x0000 0x0300
 * RMW 0x1F 0x0084 0x3380 0xFFC0
 * RMW 0x1F 0x0085 0x0006 0x000F
 * RMW 0x1F 0x008A 0xC000 0xF800
 * RMW 0x1F 0x0087 0x801C 0x801C
 * RMW 0x1F 0x0088 0x033F 0x1FFF
 * W   0x1F 0x008B 0x0404 ------
 * RMW 0x1F 0x0080 0x0600 0x0600
 * RMW 0x1F 0x00F1 0x2400 0x7F00
 * RMW 0x1F 0x0096 0x2000 0x2000
 * W   0x1F 0x0099 0x7F80 ------
 */

static const u32 lan867x_revb1_fixup_registers[12] = {
	0x00D0, 0x00D1, 0x0084, 0x0085,
	0x008A, 0x0087, 0x0088, 0x008B,
	0x0080, 0x00F1, 0x0096, 0x0099,
};

static const u16 lan867x_revb1_fixup_values[12] = {
	0x0002, 0x0000, 0x3380, 0x0006,
	0xC000, 0x801C, 0x033F, 0x0404,
	0x0600, 0x2400, 0x2000, 0x7F80,
};

static const u16 lan867x_revb1_fixup_masks[12] = {
	0x0E03, 0x0300, 0xFFC0, 0x000F,
	0xF800, 0x801C, 0x1FFF, 0xFFFF,
	0x0600, 0x7F00, 0x2000, 0xFFFF,
};

/* LAN865x Rev.B0/B1 configuration parameters from AN1760
 * As per the Configuration Application Note AN1760 published in the below link,
 * https://www.microchip.com/en-us/application-notes/an1760
 * Revision F (DS60001760G - June 2024)
 */
static const u32 lan865x_revb_fixup_registers[17] = {
	0x00D0, 0x00E0, 0x00E9, 0x00F5,
	0x00F4, 0x00F8, 0x00F9, 0x0081,
	0x0091, 0x0043, 0x0044, 0x0045,
	0x0053, 0x0054, 0x0055, 0x0040,
	0x0050,
};

static const u16 lan865x_revb_fixup_values[17] = {
	0x3F31, 0xC000, 0x9E50, 0x1CF8,
	0xC020, 0xB900, 0x4E53, 0x0080,
	0x9660, 0x00FF, 0xFFFF, 0x0000,
	0x00FF, 0xFFFF, 0x0000, 0x0002,
	0x0002,
};

static const u16 lan865x_revb_fixup_cfg_regs[2] = {
	0x0084, 0x008A,
};

static const u32 lan865x_revb_sqi_fixup_regs[12] = {
	0x00B0, 0x00B1, 0x00B2, 0x00B3,
	0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x00B9, 0x00BA, 0x00BB,
};

static const u16 lan865x_revb_sqi_fixup_values[12] = {
	0x0103, 0x0910, 0x1D26, 0x002A,
	0x0103, 0x070D, 0x1720, 0x0027,
	0x0509, 0x0E13, 0x1C25, 0x002B,
};

static const u16 lan865x_revb_sqi_fixup_cfg_regs[3] = {
	0x00AD, 0x00AE, 0x00AF,
};

/* LAN867x Rev.D0 configuration parameters from AN1699
 * As per the Configuration Application Note AN1699 published in the below link,
 * https://www.microchip.com/en-us/application-notes/an1699
 * Revision G (DS60001699G - October 2025)
 */
static const u16 lan867x_revd0_fixup_regs[8] = {
	0x0037, 0x008A, 0x0118, 0x00D6,
	0x0082, 0x00FD, 0x00FD, 0x0091,
};

static const u16 lan867x_revd0_fixup_values[8] = {
	0x0800, 0xBFC0, 0x029C, 0x1001,
	0x001C, 0x0C0B, 0x8C07, 0x9660,
};

/* Pulled from AN1760 describing 'indirect read'
 *
 * write_register(0x4, 0x00D8, addr)
 * write_register(0x4, 0x00DA, 0x2)
 * return (int8)(read_register(0x4, 0x00D9))
 *
 * 0x4 refers to memory map selector 4, which maps to MDIO_MMD_VEND2
 */
static int lan865x_revb_indirect_read(struct phy_device *phydev, u16 addr)
{
	int ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, LAN865X_REG_CFGPARAM_ADDR,
			    addr);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, LAN865X_REG_CFGPARAM_CTRL,
			    LAN865X_CFGPARAM_READ_ENABLE);
	if (ret)
		return ret;

	return phy_read_mmd(phydev, MDIO_MMD_VEND2, LAN865X_REG_CFGPARAM_DATA);
}

/* This is pulled straight from AN1760 from 'calculation of offset 1' &
 * 'calculation of offset 2'
 */
static int lan865x_generate_cfg_offsets(struct phy_device *phydev, s8 offsets[])
{
	const u16 fixup_regs[2] = {0x0004, 0x0008};
	int ret;

	for (int i = 0; i < ARRAY_SIZE(fixup_regs); i++) {
		ret = lan865x_revb_indirect_read(phydev, fixup_regs[i]);
		if (ret < 0)
			return ret;

		/* 5-bit signed value, sign extend */
		ret &= GENMASK(4, 0);
		if (ret & BIT(4))
			offsets[i] = ret | 0xE0;
		else
			offsets[i] = ret;
	}

	return 0;
}

static int lan865x_read_cfg_params(struct phy_device *phydev,
				   const u16 cfg_regs[], u16 cfg_params[],
				   u8 count)
{
	int ret;

	for (int i = 0; i < count; i++) {
		ret = phy_read_mmd(phydev, MDIO_MMD_VEND2,
				   cfg_regs[i]);
		if (ret < 0)
			return ret;
		cfg_params[i] = (u16)ret;
	}

	return 0;
}

static int lan865x_write_cfg_params(struct phy_device *phydev,
				    const u16 cfg_regs[], u16 cfg_params[],
				    u8 count)
{
	int ret;

	for (int i = 0; i < count; i++) {
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, cfg_regs[i],
				    cfg_params[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int lan865x_setup_cfgparam(struct phy_device *phydev, s8 offsets[])
{
	u16 cfg_results[ARRAY_SIZE(lan865x_revb_fixup_cfg_regs)];
	u16 cfg_params[ARRAY_SIZE(lan865x_revb_fixup_cfg_regs)];
	int ret;

	ret = lan865x_read_cfg_params(phydev, lan865x_revb_fixup_cfg_regs,
				      cfg_params, ARRAY_SIZE(cfg_params));
	if (ret)
		return ret;

	cfg_results[0] = FIELD_PREP(GENMASK(15, 10), 9 + offsets[0]) |
			 FIELD_PREP(GENMASK(9, 4), 14 + offsets[0]) |
			 0x03;
	cfg_results[1] = FIELD_PREP(GENMASK(15, 10), 40 + offsets[1]);

	return lan865x_write_cfg_params(phydev, lan865x_revb_fixup_cfg_regs,
					cfg_results, ARRAY_SIZE(cfg_results));
}

static int lan865x_setup_sqi_cfgparam(struct phy_device *phydev, s8 offsets[])
{
	u16 cfg_results[ARRAY_SIZE(lan865x_revb_sqi_fixup_cfg_regs)];
	u16 cfg_params[ARRAY_SIZE(lan865x_revb_sqi_fixup_cfg_regs)];
	int ret;

	ret = lan865x_read_cfg_params(phydev, lan865x_revb_sqi_fixup_cfg_regs,
				      cfg_params, ARRAY_SIZE(cfg_params));
	if (ret)
		return ret;

	cfg_results[0] = FIELD_PREP(GENMASK(13, 8), 5 + offsets[0]) |
			 (9 + offsets[0]);
	cfg_results[1] = FIELD_PREP(GENMASK(13, 8), 9 + offsets[0]) |
			 (14 + offsets[0]);
	cfg_results[2] = FIELD_PREP(GENMASK(13, 8), 17 + offsets[0]) |
			 (22 + offsets[0]);

	return lan865x_write_cfg_params(phydev, lan865x_revb_sqi_fixup_cfg_regs,
					cfg_results, ARRAY_SIZE(cfg_results));
}

/* lan865x_phy_priv - per-PHY private data for LAN865x internal PHY.
 *
 * Stores the last PLCA configuration written by userspace so that
 * config_init can re-apply it after a MAC-PHY software reset wipes
 * the PHY register state.
 */
struct lan865x_phy_priv {
	struct phy_plca_cfg plca_cfg;	/* cached PLCA config */
	bool plca_cached;		/* true once set by userspace */
};

struct lan867x_revd0_priv {
	struct gpio_desc *wakein_gpio;
	bool phy_suspended;
};

static int lan86xx_plca_set_cfg(struct phy_device *phydev,
				const struct phy_plca_cfg *plca_cfg);

static int lan865x_revb_config_init(struct phy_device *phydev)
{
	struct lan865x_phy_priv *priv = phydev->priv;
	s8 offsets[2];
	int ret;

	/* Reference to AN1760
	 * https://ww1.microchip.com/downloads/aemDocuments/documents/AIS/ProductDocuments/SupportingCollateral/AN-LAN8650-1-Configuration-60001760.pdf
	 */
	ret = lan865x_generate_cfg_offsets(phydev, offsets);
	if (ret)
		return ret;

	for (int i = 0; i < ARRAY_SIZE(lan865x_revb_fixup_registers); i++) {
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				    lan865x_revb_fixup_registers[i],
				    lan865x_revb_fixup_values[i]);
		if (ret)
			return ret;

		if (i == 1) {
			ret = lan865x_setup_cfgparam(phydev, offsets);
			if (ret)
				return ret;
		}
	}

	ret = lan865x_setup_sqi_cfgparam(phydev, offsets);
	if (ret)
		return ret;

	for (int i = 0; i < ARRAY_SIZE(lan865x_revb_sqi_fixup_regs); i++) {
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				    lan865x_revb_sqi_fixup_regs[i],
				    lan865x_revb_sqi_fixup_values[i]);
		if (ret)
			return ret;
	}

	/* Re-apply PLCA config after reset if userspace has configured it */
	if (priv && priv->plca_cached)
		return lan86xx_plca_set_cfg(phydev, &priv->plca_cfg);

	return 0;
}

static int lan867x_check_reset_complete(struct phy_device *phydev)
{
	int err;

	/* The chip completes a reset in 3us, we might get here earlier than
	 * that, as an added margin we'll conditionally sleep 5us.
	 */
	err = phy_read_mmd(phydev, MDIO_MMD_VEND2, LAN867X_REG_STS2);
	if (err < 0)
		return err;

	if (!(err & LAN867x_RESET_COMPLETE_STS)) {
		udelay(5);
		err = phy_read_mmd(phydev, MDIO_MMD_VEND2, LAN867X_REG_STS2);
		if (err < 0)
			return err;
		if (!(err & LAN867x_RESET_COMPLETE_STS)) {
			phydev_err(phydev, "PHY reset failed\n");
			return -ENODEV;
		}
	}

	return 0;
}

static int lan867x_revc_config_init(struct phy_device *phydev)
{
	s8 offsets[2];
	int ret;

	ret = lan867x_check_reset_complete(phydev);
	if (ret)
		return ret;

	ret = lan865x_generate_cfg_offsets(phydev, offsets);
	if (ret)
		return ret;

	/* LAN867x Rev.C1/C2 configuration settings are equal to the first 9
	 * configuration settings and all the sqi fixup settings from LAN865x
	 * Rev.B0/B1. So the same fixup registers and values from LAN865x
	 * Rev.B0/B1 are used for LAN867x Rev.C1/C2 to avoid duplication.
	 * Refer the below links for the comparison.
	 * https://www.microchip.com/en-us/application-notes/an1760
	 * Revision F (DS60001760G - June 2024)
	 * https://www.microchip.com/en-us/application-notes/an1699
	 * Revision E (DS60001699F - June 2024)
	 */
	for (int i = 0; i < 9; i++) {
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				    lan865x_revb_fixup_registers[i],
				    lan865x_revb_fixup_values[i]);
		if (ret)
			return ret;

		if (i == 1) {
			ret = lan865x_setup_cfgparam(phydev, offsets);
			if (ret)
				return ret;
		}
	}

	ret = lan865x_setup_sqi_cfgparam(phydev, offsets);
	if (ret)
		return ret;

	for (int i = 0; i < ARRAY_SIZE(lan865x_revb_sqi_fixup_regs); i++) {
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				    lan865x_revb_sqi_fixup_regs[i],
				    lan865x_revb_sqi_fixup_values[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int lan867x_revb1_config_init(struct phy_device *phydev)
{
	int err;

	err = lan867x_check_reset_complete(phydev);
	if (err)
		return err;

	/* Reference to AN1699
	 * https://ww1.microchip.com/downloads/aemDocuments/documents/AIS/ProductDocuments/SupportingCollateral/AN-LAN8670-1-2-config-60001699.pdf
	 * AN1699 says Read, Modify, Write, but the Write is not required if the
	 * register already has the required value. So it is safe to use
	 * phy_modify_mmd here.
	 */
	for (int i = 0; i < ARRAY_SIZE(lan867x_revb1_fixup_registers); i++) {
		err = phy_modify_mmd(phydev, MDIO_MMD_VEND2,
				     lan867x_revb1_fixup_registers[i],
				     lan867x_revb1_fixup_masks[i],
				     lan867x_revb1_fixup_values[i]);
		if (err)
			return err;
	}

	return 0;
}

static int lan867x_revd0_link_active_selection(struct phy_device *phydev,
					       bool plca_enabled)
{
	u16 value;

	if (plca_enabled) {
		/* 0x1 - When PLCA is enabled: link active reflects plca_status */
		value = FIELD_PREP(LINK_ACTIVE_SEL, 0x1);
	} else {
		/* 0x2 - Link semaphore bit written 0x1 to force link always
		 * active in CSMA/CD mode as it doesn't support autoneg.
		 */
		value = FIELD_PREP(LINK_ACTIVE_SEL, 0x2) |
			FIELD_PREP(LINK_SEMAPHORE, 0x1);
	}

	return phy_write_mmd(phydev, MDIO_MMD_VEND2,
			     LAN867X_REG_LINK_STATUS, value);
}

static int lan867x_revd0_probe(struct phy_device *phydev)
{
	struct lan867x_revd0_priv *priv;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	priv->wakein_gpio = devm_gpiod_get_optional(&phydev->mdio.dev, "wakein",
						    GPIOD_OUT_LOW);
	if (IS_ERR(priv->wakein_gpio))
		return dev_err_probe(&phydev->mdio.dev,
				     PTR_ERR(priv->wakein_gpio),
				     "Failed to get wake-in GPIO");

	return 0;
}

static void lan867x_revd0_remove(struct phy_device *phydev)
{
	struct lan867x_revd0_priv *priv = phydev->priv;

	if (priv)
		devm_kfree(&phydev->mdio.dev, priv);
}

static int lan867x_revd0_config_init(struct phy_device *phydev)
{
	int ret;

	ret = lan867x_check_reset_complete(phydev);
	if (ret)
		return ret;

	for (int i = 0; i < ARRAY_SIZE(lan867x_revd0_fixup_regs); i++) {
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				     lan867x_revd0_fixup_regs[i],
				     lan867x_revd0_fixup_values[i]);
		if (ret)
			return ret;
	}

	/* AN1760: configure CCMFC to OA default so that the hardware
	 * automatically gates collision forwarding to the MAC based on
	 * PLCA_Status. Collisions are neither counted nor forwarded when
	 * PLCA_Status = OK, eliminating the need for software-driven CDEN
	 * toggling in the interrupt handler. CDEN remains enabled.
	 */
	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2, LAN86XX_REG_COL_DET_CTRL0,
			     COL_DET_CTRL0_CCMFC_MASK,
			     COL_DET_CTRL0_CCMFC_OA_DEFAULT);
	if (ret)
		return ret;

	/* Initially the PHY will be in CSMA/CD mode by default. So it is
	 * required to set the link always active as it doesn't support
	 * autoneg.
	 */
	return lan867x_revd0_link_active_selection(phydev, false);
}

static int lan86xx_suspend(struct phy_device *phydev)
{
	struct lan867x_revd0_priv *priv = phydev->priv;
	int ret;

	/* Local PHY suspend is only possible if a Wake-In pin is supported.
	 * Devices like the EVB-LAN8670-USB do not support a Wake-In pin and
	 * therefore only support remote wake-up using the WoL (Wake-on-LAN)
	 * WAKE_PHY feature.
	 */
	if (!priv->wakein_gpio)
		return -ENOTSUPP;

	/* Configure the PHY wake-up source from low power sleep state.
	 * There can be two wake-up sources, namely:
	 *  - WUP (Wakeup Pulse)
	 *  - Wake by Pin
	 */
	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2, LAN86XX_DEEP_SLEEP_CTRL0,
			       DEEP_SLEEP_CTRL0_WUP | DEEP_SLEEP_CTRL0_WAKEIN);
	if (ret)
		return ret;

	/* Send out a WUP (Wakeup Pulse) to the network while waking up this
	 * device, to wake other devices in the network.
	 */
	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2, LAN86XX_DEEP_SLEEP_CTRL1,
			       DEEP_SLEEP_CTRL1_WAKE_FWD);
	if (ret)
		return ret;

	/* Put the PHY into low power sleep mode */
	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2, MDIO_OATC14_WS_CONTROL,
			       WS_CONTROL_LPREQ);
	if (ret)
		return ret;

	priv->phy_suspended = true;

	return 0;
}

static int lan86xx_resume(struct phy_device *phydev)
{
	struct lan867x_revd0_priv *priv = phydev->priv;

	if (!priv->phy_suspended)
		return 0;

	priv->phy_suspended = false;

	/* To wake up the PHY from low power sleep state, the wake-in GPIO
	 * needs to be activated for 40 microseconds. As an added margin,
	 * the GPIO is activated for 1 millisecond to ensure reliable wakeup.
	 */
	gpiod_set_value_cansleep(priv->wakein_gpio, 1);
	mdelay(1);
	gpiod_set_value_cansleep(priv->wakein_gpio, 0);

	/* When the PHY wakes up from the low-power sleep state,
	 * the entire PHY is reset and all configured settings are lost.
	 * Therefore, it is necessary to reconfigure all initial settings.
	 */
	return phydev->drv->config_init(phydev);
}

/* As per LAN8650/1 Rev.B0/B1 AN1760 (Revision F (DS60001760G - June 2024)) and
 * LAN8670/1/2 Rev.C1/C2 AN1699 (Revision E (DS60001699F - June 2024)), under
 * normal operation, the device should be operated in PLCA mode. Disabling
 * collision detection is recommended to allow the device to operate in noisy
 * environments or when reflections and other inherent transmission line
 * distortion cause poor signal quality. Collision detection must be re-enabled
 * if the device is configured to operate in CSMA/CD mode.
 *
 * AN1760: https://www.microchip.com/en-us/application-notes/an1760
 * AN1699: https://www.microchip.com/en-us/application-notes/an1699
 */
static int lan86xx_plca_set_cfg(struct phy_device *phydev,
				const struct phy_plca_cfg *plca_cfg)
{
	int ret;

	ret = genphy_c45_plca_set_cfg(phydev, plca_cfg);
	if (ret)
		return ret;

	/* PHYs with routed interrupts handle CDEN dynamically via the interrupt
	 * handler, so skip the static write. PHYs running with PHY_POLL have no
	 * interrupt handler, so apply the static CDEN write as a baseline on
	 * every ethtool PLCA reconfiguration. The limitation is that autonomous
	 * PLCA mode transitions between ethtool reconfigurations are not
	 * tracked on such boards.
	 */
	if (phydev->irq != PHY_POLL)
		return 0;

	if (plca_cfg->enabled)
		return phy_modify_mmd(phydev, MDIO_MMD_VEND2,
				      LAN86XX_REG_COL_DET_CTRL0,
				      COL_DET_CTRL0_ENABLE_BIT_MASK,
				      COL_DET_DISABLE);

	return phy_modify_mmd(phydev, MDIO_MMD_VEND2, LAN86XX_REG_COL_DET_CTRL0,
			      COL_DET_CTRL0_ENABLE_BIT_MASK, COL_DET_ENABLE);
}

static int lan86xx_read_status(struct phy_device *phydev)
{
	/* The phy has some limitations, namely:
	 *  - always reports link up
	 *  - only supports 10MBit half duplex
	 *  - does not support auto negotiate
	 */
	phydev->link = 1;
	phydev->duplex = DUPLEX_HALF;
	phydev->speed = SPEED_10;
	phydev->autoneg = AUTONEG_DISABLE;

	return 0;
}

static int lan86xx_config_intr(struct phy_device *phydev)
{
	u16 mask = LAN86XX_STS1_PLCA_STS_CHANGED;
	int ret;

	if (phydev->phy_id == PHY_ID_LAN867X_REVD0)
		mask |= LAN86XX_STS1_LINK_STS_CHANGED;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		/* Read to clear any pending status before enabling. */
		ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, LAN86XX_REG_STS1);
		if (ret < 0)
			return ret;

		/* A mask bit of 0 enables the corresponding interrupt. */
		return phy_clear_bits_mmd(phydev, MDIO_MMD_VEND2,
					  LAN86XX_REG_IMSK1, mask);
	}

	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2, LAN86XX_REG_IMSK1,
			       mask);
	if (ret)
		return ret;

	/* Read to clear any pending status after disabling. */
	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, LAN86XX_REG_STS1);
	if (ret < 0)
		return ret;

	return 0;
}

static irqreturn_t lan86xx_handle_interrupt(struct phy_device *phydev)
{
	struct phy_plca_status plca_st;
	irqreturn_t ret_irq = IRQ_NONE;
	int sts1, ret;

	/* Reading the status register clears the latched event bits. */
	sts1 = phy_read_mmd(phydev, MDIO_MMD_VEND2, LAN86XX_REG_STS1);
	if (sts1 < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (sts1 & LAN86XX_STS1_PLCA_STS_CHANGED) {
		ret = genphy_c45_plca_get_status(phydev, &plca_st);
		if (ret < 0) {
			phy_error(phydev);
			return IRQ_NONE;
		}

		/* AN1760/AN1699: disable collision detection in PLCA mode to
		 * improve signal quality; re-enable it in CSMA/CD mode.
		 *
		 * https://www.microchip.com/en-us/application-notes/an1760
		 * https://www.microchip.com/en-us/application-notes/an1699
		 */
		ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2,
				     LAN86XX_REG_COL_DET_CTRL0,
				     COL_DET_CTRL0_ENABLE_BIT_MASK,
				     plca_st.pst ? COL_DET_DISABLE :
				     COL_DET_ENABLE);
		if (ret < 0) {
			phy_error(phydev);
			return IRQ_NONE;
		}

		ret_irq = IRQ_HANDLED;
	}

	return ret_irq;
}

static irqreturn_t lan867x_revd0_handle_interrupt(struct phy_device *phydev)
{
	struct phy_plca_status plca_st;
	irqreturn_t ret_irq = IRQ_NONE;
	int sts1, ret;

	/* Reading the status register clears the latched event bits. */
	sts1 = phy_read_mmd(phydev, MDIO_MMD_VEND2, LAN86XX_REG_STS1);
	if (sts1 < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (sts1 & LAN86XX_STS1_LINK_STS_CHANGED) {
		phy_trigger_machine(phydev);
		ret_irq = IRQ_HANDLED;
	}

	if (sts1 & LAN86XX_STS1_PLCA_STS_CHANGED) {
		ret = genphy_c45_plca_get_status(phydev, &plca_st);
		if (ret < 0) {
			phy_error(phydev);
			return IRQ_NONE;
		}

		/* Collision detection is handled autonomously by the hardware
		 * via CCMFC. Only the link status selection needs to be updated
		 * on each PLCA status transition.
		 */
		ret = lan867x_revd0_link_active_selection(phydev, plca_st.pst);
		if (ret < 0) {
			phy_error(phydev);
			return IRQ_NONE;
		}

		ret_irq = IRQ_HANDLED;
	}

	return ret_irq;
}

static int lan865x_phy_probe(struct phy_device *phydev)
{
	struct lan865x_phy_priv *priv;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Mark invalid until userspace explicitly configures PLCA */
	priv->plca_cached = false;
	phydev->priv = priv;

	return 0;
}

/* lan865x_plca_set_cfg - set PLCA config and cache it for post-reset restore.
 *
 * Thin wrapper around lan86xx_plca_set_cfg().  On success the full effective
 * configuration is read back and saved in phydev->priv so that the MAC
 * driver's post-reset callback can re-apply it after a software reset clears
 * the PHY register state.
 */
static int lan865x_plca_set_cfg(struct phy_device *phydev,
				const struct phy_plca_cfg *plca_cfg)
{
	struct lan865x_phy_priv *priv = phydev->priv;
	int ret;

	ret = lan86xx_plca_set_cfg(phydev, plca_cfg);
	if (ret)
		return ret;

	/* The incoming plca_cfg is a sparse delta: fields the user did not
	 * specify are -1 and were left untouched in hardware.  Caching that
	 * delta is not enough to restore state after a reset, because a reset
	 * returns the unspecified fields to chip defaults (e.g. node_id 0, the
	 * PLCA coordinator) rather than the values the node was running with.
	 * Snapshot the full effective config instead, so the post-reset restore
	 * rewrites every field.
	 */
	if (priv) {
		ret = genphy_c45_plca_get_cfg(phydev, &priv->plca_cfg);
		if (ret)
			return ret;
		/* get_cfg populates 'version' from the read-only PLCA IDVER
		 * register, but genphy_c45_plca_set_cfg() rejects any config
		 * with version >= 0 (-EINVAL).  Force it to -1 so the cached
		 * config can be written back unchanged during recovery.
		 */
		priv->plca_cfg.version = -1;
		priv->plca_cached = true;
	}

	return 0;
}

/* OPEN Alliance 10BASE-T1x compliance MAC-PHYs will have both C22 and
 * C45 registers space. If the PHY is discovered via C22 bus protocol it assumes
 * it uses C22 protocol and always uses C22 registers indirect access to access
 * C45 registers. This is because, we don't have a clean separation between
 * C22/C45 register space and C22/C45 MDIO bus protocols. Resulting, PHY C45
 * registers direct access can't be used which can save multiple SPI bus access.
 * To support this feature, set .read_mmd/.write_mmd in the PHY driver to call
 * .read_c45/.write_c45 in the OPEN Alliance framework
 * drivers/net/ethernet/oa_tc6.c
 */
static int lan865x_phy_read_mmd(struct phy_device *phydev, int devnum,
				u16 regnum)
{
	struct mii_bus *bus = phydev->mdio.bus;
	int addr = phydev->mdio.addr;

	return __mdiobus_c45_read(bus, addr, devnum, regnum);
}

static int lan865x_phy_write_mmd(struct phy_device *phydev, int devnum,
				 u16 regnum, u16 val)
{
	struct mii_bus *bus = phydev->mdio.bus;
	int addr = phydev->mdio.addr;

	return __mdiobus_c45_write(bus, addr, devnum, regnum, val);
}

static int lan86xx_set_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	struct lan867x_revd0_priv *priv = phydev->priv;

	if (priv->wakein_gpio)
		return 0;

	/* Configure the PHY wake-up source from low power sleep state
	 * for remote wake-up functionality.
	 * The wake-up source used is WUP (Wakeup Pulse).
	 */
	return phy_set_bits_mmd(phydev, MDIO_MMD_VEND2,
				LAN86XX_DEEP_SLEEP_CTRL0, DEEP_SLEEP_CTRL0_WUP);
}

static void lan86xx_get_wol(struct phy_device *phydev,
			    struct ethtool_wolinfo *wol)
{
	struct lan867x_revd0_priv *priv = phydev->priv;

	if (priv->wakein_gpio)
		return;

	wol->supported = WAKE_PHY;
	wol->wolopts = WAKE_PHY;
}

static struct phy_driver microchip_t1s_driver[] = {
	{
		PHY_ID_MATCH_EXACT(PHY_ID_LAN867X_REVB1),
		.name               = "LAN867X Rev.B1",
		.features           = PHY_BASIC_T1S_P2MP_FEATURES,
		.config_init        = lan867x_revb1_config_init,
		.read_status        = lan86xx_read_status,
		.config_intr        = lan86xx_config_intr,
		.handle_interrupt   = lan86xx_handle_interrupt,
		.get_plca_cfg	    = genphy_c45_plca_get_cfg,
		.set_plca_cfg	    = genphy_c45_plca_set_cfg,
		.get_plca_status    = genphy_c45_plca_get_status,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_LAN867X_REVC1),
		.name               = "LAN867X Rev.C1",
		.features           = PHY_BASIC_T1S_P2MP_FEATURES,
		.config_init        = lan867x_revc_config_init,
		.read_status        = lan86xx_read_status,
		.config_intr        = lan86xx_config_intr,
		.handle_interrupt   = lan86xx_handle_interrupt,
		.get_plca_cfg	    = genphy_c45_plca_get_cfg,
		.set_plca_cfg	    = lan86xx_plca_set_cfg,
		.get_plca_status    = genphy_c45_plca_get_status,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_LAN867X_REVC2),
		.name               = "LAN867X Rev.C2",
		.features           = PHY_BASIC_T1S_P2MP_FEATURES,
		.config_init        = lan867x_revc_config_init,
		.read_status        = lan86xx_read_status,
		.config_intr        = lan86xx_config_intr,
		.handle_interrupt   = lan86xx_handle_interrupt,
		.get_plca_cfg	    = genphy_c45_plca_get_cfg,
		.set_plca_cfg	    = lan86xx_plca_set_cfg,
		.get_plca_status    = genphy_c45_plca_get_status,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_LAN865X_REVB),
		.name               = "LAN865X Rev.B0/B1 Internal Phy",
		.features           = PHY_BASIC_T1S_P2MP_FEATURES,
		.probe              = lan865x_phy_probe,
		.config_init        = lan865x_revb_config_init,
		.read_status        = lan86xx_read_status,
		.config_intr        = lan86xx_config_intr,
		.handle_interrupt   = lan86xx_handle_interrupt,
		.read_mmd           = lan865x_phy_read_mmd,
		.write_mmd          = lan865x_phy_write_mmd,
		.get_plca_cfg	    = genphy_c45_plca_get_cfg,
		.set_plca_cfg	    = lan865x_plca_set_cfg,
		.get_plca_status    = genphy_c45_plca_get_status,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_LAN867X_REVD0),
		.name               = "LAN867X Rev.D0",
		.features           = PHY_BASIC_T1S_P2MP_FEATURES,
		.probe              = lan867x_revd0_probe,
		.remove             = lan867x_revd0_remove,
		.config_init        = lan867x_revd0_config_init,
		.config_intr        = lan86xx_config_intr,
		.handle_interrupt   = lan867x_revd0_handle_interrupt,
		.get_plca_cfg	    = genphy_c45_plca_get_cfg,
		.set_plca_cfg	    = lan86xx_plca_set_cfg,
		.get_plca_status    = genphy_c45_plca_get_status,
		.suspend            = lan86xx_suspend,
		.resume             = lan86xx_resume,
		.get_wol            = lan86xx_get_wol,
		.set_wol            = lan86xx_set_wol,
	},
};

module_phy_driver(microchip_t1s_driver);

static struct mdio_device_id __maybe_unused tbl[] = {
	{ PHY_ID_MATCH_EXACT(PHY_ID_LAN867X_REVB1) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_LAN867X_REVC1) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_LAN867X_REVC2) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_LAN865X_REVB) },
	{ PHY_ID_MATCH_EXACT(PHY_ID_LAN867X_REVD0) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, tbl);

MODULE_DESCRIPTION("Microchip 10BASE-T1S PHYs driver");
MODULE_AUTHOR("Ramón Nordin Rodriguez");
MODULE_LICENSE("GPL");
