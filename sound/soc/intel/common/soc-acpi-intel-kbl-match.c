// SPDX-License-Identifier: GPL-2.0
/*
 * soc-apci-intel-kbl-match.c - tables and support for KBL ACPI enumeration.
 *
 * Copyright (c) 2018, Intel Corporation.
 *
 */

#include <sound/soc-acpi.h>
#include <sound/soc-acpi-intel-match.h>
#include "../skylake/skl.h"

static struct skl_machine_pdata skl_dmic_data;

static struct snd_soc_acpi_codecs kbl_codecs = {
	.num_codecs = 1,
	.codecs = {"10508825"}
};

static struct snd_soc_acpi_codecs kbl_poppy_codecs = {
	.num_codecs = 1,
	.codecs = {"10EC5663"}
};

static struct snd_soc_acpi_codecs kbl_5663_5514_codecs = {
	.num_codecs = 2,
	.codecs = {"10EC5663", "10EC5514"}
};

static struct snd_soc_acpi_codecs kbl_7219_98357_codecs = {
	.num_codecs = 1,
	.codecs = {"MX98357A"}
};

struct snd_soc_acpi_mach snd_soc_acpi_intel_kbl_machines[] = {
	{
		.id = "INT343A",
		.drv_name = "kbl_alc286s_i2s",
		.fw_filename = "intel/dsp_fw_kbl.bin",
		.sof_fw_filename = "intel/sof-kbl.ri",
		.sof_tplg_filename = "intel/sof-kbl.tplg",
		.asoc_plat_name = "0000:00:1f.03",
	},
	{
		.id = "INT343B",
		.drv_name = "kbl_n88l25_s4567",
		.fw_filename = "intel/dsp_fw_kbl.bin",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &kbl_codecs,
		.pdata = &skl_dmic_data,
		.sof_fw_filename = "intel/sof-kbl.ri",
		.sof_tplg_filename = "intel/sof-kbl.tplg",
		.asoc_plat_name = "0000:00:1f.03",
	},
	{
		.id = "MX98357A",
		.drv_name = "kbl_n88l25_m98357a",
		.fw_filename = "intel/dsp_fw_kbl.bin",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &kbl_codecs,
		.pdata = &skl_dmic_data,
		.sof_fw_filename = "intel/sof-kbl.ri",
		.sof_tplg_filename = "intel/sof-kbl.tplg",
		.asoc_plat_name = "0000:00:1f.03",
	},
	{
		.id = "MX98927",
		.drv_name = "kbl_r5514_5663_max",
		.fw_filename = "intel/dsp_fw_kbl.bin",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &kbl_5663_5514_codecs,
		.pdata = &skl_dmic_data,
		.sof_fw_filename = "intel/sof-kbl.ri",
		.sof_tplg_filename = "intel/sof-kbl.tplg",
		.asoc_plat_name = "0000:00:1f.03",
	},
	{
		.id = "MX98927",
		.drv_name = "kbl_rt5663_m98927",
		.fw_filename = "intel/dsp_fw_kbl.bin",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &kbl_poppy_codecs,
		.pdata = &skl_dmic_data,
		.sof_fw_filename = "intel/sof-kbl.ri",
		.sof_tplg_filename = "intel/sof-kbl.tplg",
		.asoc_plat_name = "0000:00:1f.03",
	},
	{
		.id = "10EC5663",
		.drv_name = "kbl_rt5663",
		.fw_filename = "intel/dsp_fw_kbl.bin",
		.sof_fw_filename = "intel/sof-kbl.ri",
		.sof_tplg_filename = "intel/sof-kbl.tplg",
		.asoc_plat_name = "0000:00:1f.03",
	},
	{
		.id = "DLGS7219",
		.drv_name = "kbl_da7219_max98357a",
		.fw_filename = "intel/dsp_fw_kbl.bin",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &kbl_7219_98357_codecs,
		.pdata = &skl_dmic_data,
		.sof_fw_filename = "intel/sof-kbl.ri",
		.sof_tplg_filename = "intel/sof-kbl.tplg",
		.asoc_plat_name = "0000:00:1f.03",
	},
	{},
};
EXPORT_SYMBOL_GPL(snd_soc_acpi_intel_kbl_machines);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Common ACPI Match module");
