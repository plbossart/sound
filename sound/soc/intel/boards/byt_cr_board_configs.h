/*
 *  byt_cr_board_configs.h - Intel SST Driver for audio engine
 *
 *  Copyright (C) 2014 Intel Corporation
 *  Authors:	Ola Lilja <ola.lilja@intel.com>
 * 
 *  Modified for upstream to use masks for board configurations
 * 
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  Board-specific hardware-configurations
 */

#ifndef __BYT_CR_BOARD_CONFIGS_H__
#define __BYT_CR_BOARD_CONFIGS_H__

enum {
	BYT_RT5640_DMIC1_MAP,
	BYT_RT5640_DMIC2_MAP,
	BYT_RT5640_IN1_MAP,
	BYT_RT5640_IN3_MAP,
};

#define BYT_RT5640_MAP(quirk)	((quirk) & 0xff)
#define BYT_RT5640_DMIC_EN	BIT(16)
#define BYT_RT5640_MCLK_EN	BIT(17)
#define BYT_RT5640_MCLK_25MHZ	BIT(18)
#define BYT_RT5640_JACK_DET_EN	        BIT(19)
#define BYT_RT5640_JACK_ACTIVE_LOW	BIT(20)  /* default 0 */
#define BYT_RT5640_JACK_INT1	        BIT(21)
#define BYT_RT5640_JACK_INT2	        BIT(22)
#define BYT_RT5640_JACK_BP_CODEC	BIT(23)
#define BYT_RT5640_JACK_BP_MICBIAS	BIT(24)
#define BYT_RT5640_JACK_DET_PIN2	BIT(25)  /* default 0 */
#define BYT_RT5640_JACK_BP_EN		BIT(26)


static unsigned long byt_rt5640_quirk = BYT_RT5640_DMIC1_MAP |
					BYT_RT5640_DMIC_EN;

static int byt_rt5640_quirk_cb(const struct dmi_system_id *id)
{
	byt_rt5640_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id byt_rt5640_quirk_table[] = {
	{
		.callback = byt_rt5640_quirk_cb,
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "T100TA"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN1_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_INT1 |
						 BYT_RT5640_JACK_BP_MICBIAS |
						 BYT_RT5640_MCLK_EN |
						 BYT_RT5640_JACK_DET_PIN2),
	},
	{
		.callback = byt_rt5640_quirk_cb,
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "T100TAF"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN1_MAP |
						 BYT_RT5640_MCLK_EN),
	},
	{
		.callback = byt_rt5640_quirk_cb,
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "DellInc."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Venue 8 Pro 5830"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_DMIC2_MAP |
						 BYT_RT5640_DMIC_EN   |
						 BYT_RT5640_MCLK_EN),
	},
	{
		.callback = byt_rt5640_quirk_cb,
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "HP ElitePad 1000 G2"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN1_MAP |
						 BYT_RT5640_MCLK_EN),
	},
	{
		.ident = "MRD7",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "TABLET"),
			DMI_MATCH(DMI_BOARD_VERSION, "MRD 7"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN3_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_INT1 |
						 BYT_RT5640_JACK_BP_MICBIAS |
						 BYT_RT5640_MCLK_EN),
	},
	/* Malata variants */
	{
		.ident = "MALATA",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "MALATA8"),
			DMI_MATCH(DMI_BOARD_VERSION, "0"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN3_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_ACTIVE_LOW |
						 BYT_RT5640_JACK_INT2 |
						 BYT_RT5640_JACK_BP_CODEC |
						 BYT_RT5640_MCLK_EN),
	},
	{
		.ident = "A82i",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "A82i"),
			DMI_MATCH(DMI_BOARD_VERSION, "2"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN3_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_ACTIVE_LOW |
						 BYT_RT5640_JACK_INT2 |
						 BYT_RT5640_JACK_BP_CODEC |
						 BYT_RT5640_MCLK_EN),
	},
	{
		.ident = "A8Low",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "MALATA8Low"),
			DMI_MATCH(DMI_BOARD_VERSION, "2"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN3_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_ACTIVE_LOW |
						 BYT_RT5640_JACK_INT2 |
						 BYT_RT5640_JACK_BP_CODEC |
						 BYT_RT5640_MCLK_EN),
	},
	{
		.ident = "A10",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "MALATA10"),
			DMI_MATCH(DMI_BOARD_VERSION, "0"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN3_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_ACTIVE_LOW |
						 BYT_RT5640_JACK_INT2 |
						 BYT_RT5640_JACK_BP_CODEC |
						 BYT_RT5640_MCLK_EN),
	},
	{
		.ident = "A105i",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "A105i"),
			DMI_MATCH(DMI_BOARD_VERSION, "1"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN3_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_ACTIVE_LOW |
						 BYT_RT5640_JACK_INT2 |
						 BYT_RT5640_JACK_BP_CODEC |
						 BYT_RT5640_MCLK_EN),
	},
	{
		.ident = "CHIPHD",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "CHIPHD8"),
			DMI_MATCH(DMI_BOARD_VERSION, "0"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN3_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_ACTIVE_LOW |
						 BYT_RT5640_JACK_INT2 |
						 BYT_RT5640_JACK_BP_CODEC |
						 BYT_RT5640_MCLK_EN),
	},
	{
		.ident = "IRA101",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "IRA101"),
			DMI_MATCH(DMI_BOARD_VERSION, "0"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN3_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_ACTIVE_LOW |
						 BYT_RT5640_JACK_INT1 |
						 BYT_RT5640_JACK_BP_MICBIAS |
						 BYT_RT5640_MCLK_EN),
	},
	{
		.ident = "IP3-T85",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "T85"),
			DMI_MATCH(DMI_BOARD_VERSION, "0"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN3_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_ACTIVE_LOW |
						 BYT_RT5640_JACK_INT2 |
						 BYT_RT5640_JACK_BP_CODEC |
						 BYT_RT5640_MCLK_EN),
	},
	/* Emdoor variants */
	{
		.ident = "VTA0705",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "VTA0705"),
			DMI_MATCH(DMI_BOARD_VERSION, "0"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN3_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_ACTIVE_LOW |
						 BYT_RT5640_JACK_INT1 |
						 BYT_RT5640_JACK_BP_MICBIAS |
						 BYT_RT5640_MCLK_EN),
	},
	{
		.ident = "VTA0803",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "VTA0803"),
			DMI_MATCH(DMI_BOARD_VERSION, "0"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN3_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_ACTIVE_LOW |
						 BYT_RT5640_JACK_INT1 |
						 BYT_RT5640_JACK_BP_MICBIAS |
						 BYT_RT5640_MCLK_EN),
	},
	{
		.ident = "Emdoor-i8811",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "I8811"),
			DMI_MATCH(DMI_BOARD_VERSION, "0"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN3_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_ACTIVE_LOW |
						 BYT_RT5640_JACK_INT1 |
						 BYT_RT5640_JACK_BP_MICBIAS |
						 BYT_RT5640_MCLK_EN),
	},
	{
		.ident = "Emdoor-i8889",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "I8889"),
			DMI_MATCH(DMI_BOARD_VERSION, "0"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN3_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_ACTIVE_LOW |
						 BYT_RT5640_JACK_INT1 |
						 BYT_RT5640_JACK_BP_MICBIAS |
						 BYT_RT5640_MCLK_EN),
	},
	{
		.ident = "TongFang-TF16",
		.matches = {
			DMI_MATCH(DMI_BOARD_NAME, "TF16"),
			DMI_MATCH(DMI_BOARD_VERSION, "0"),
		},
		.driver_data = (unsigned long *)(BYT_RT5640_IN3_MAP |
						 BYT_RT5640_JACK_DET_EN |
						 BYT_RT5640_JACK_ACTIVE_LOW |
						 BYT_RT5640_JACK_INT1 |
						 BYT_RT5640_JACK_BP_MICBIAS |
						 BYT_RT5640_MCLK_EN),
	},
	{}
};

#endif
