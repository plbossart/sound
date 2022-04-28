// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.

/*
 * sof_dmic_generic_card.c - ASoc Machine driver for Intel platforms
 * with a DMIC interface
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/sof.h>

#define NAME_SIZE 32

static const struct snd_soc_dapm_widget dapm_widgets[] = {
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
};

static const struct snd_soc_dapm_route dapm_routes[] = {
	/* digital mics */
	{"DMic", NULL, "SoC DMIC"},
};

static struct snd_soc_card dmic_card = {
	.name         = "sof_dmic_generic_card",
	.owner        = THIS_MODULE,
	.dapm_widgets = dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(dapm_widgets),
	.dapm_routes = dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(dapm_routes),
	.fully_routed = true,
};

static struct snd_soc_dai_link_component platform_component[] = {
	{
		/* name might be overridden during probe */
		.name = "0000:00:1f.3"
	}
};

static struct snd_soc_dai_link_component dmic_component[] = {
	{
		.name = "dmic-codec",
		.dai_name = "dmic-hifi",
	}
};

static struct snd_soc_dai_link *sof_card_dai_links_create(struct device *dev,
							  int dmic_be_num)
{
	struct snd_soc_dai_link_component *cpus;
	struct snd_soc_dai_link *links;
	int i, id = 0;

	links = devm_kzalloc(dev, sizeof(struct snd_soc_dai_link) *
					dmic_card.num_links, GFP_KERNEL);
	cpus = devm_kzalloc(dev, sizeof(struct snd_soc_dai_link_component) *
					dmic_card.num_links, GFP_KERNEL);
	if (!links || !cpus)
		return NULL;

	/* dmic */
	if (dmic_be_num > 0) {
		/* at least we have dmic01 */
		links[id].name = "dmic01";
		links[id].cpus = &cpus[id];
		links[id].cpus->dai_name = "DMIC01 Pin";
		if (dmic_be_num > 1) {
			/* set up 2 BE links at most */
			links[id + 1].name = "dmic16k";
			links[id + 1].cpus = &cpus[id + 1];
			links[id + 1].cpus->dai_name = "DMIC16k Pin";
			dmic_be_num = 2;
		}
	}

	for (i = 0; i < dmic_be_num; i++) {
		links[id].id = id;
		links[id].num_cpus = 1;
		links[id].codecs = dmic_component;
		links[id].num_codecs = ARRAY_SIZE(dmic_component);
		links[id].platforms = platform_component;
		links[id].num_platforms = ARRAY_SIZE(platform_component);
		links[id].ignore_suspend = 1;
		links[id].dpcm_capture = 1;
		links[id].no_pcm = 1;
		id++;
	}

	return links;
}

static int dmic_generic_card_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_soc_dai_link *dai_links;
	struct snd_soc_acpi_mach *mach;
	int dmic_be_num = 2;
	int ret;

	mach = pdev->dev.platform_data;

	//if (!mach->mach_params.dmic_num) {
	//	dev_info(dev, "%s: no DMIC present\n", __func__);
	//	return 0;
	//}

	/* set number of dai links */
	dmic_card.num_links = dmic_be_num;

	dai_links = sof_card_dai_links_create(dev, dmic_be_num);
	if (!dai_links)
		return -ENOMEM;

	dmic_card.dai_link = dai_links;
	dmic_card.dev = dev;

	/* set platform name for each dailink */
	ret = snd_soc_fixup_dai_links_platform_name(&dmic_card,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	ret = devm_snd_soc_register_card(dev, &dmic_card);

	return ret;
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "dmic_generic_card",
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, board_ids);

static struct platform_driver generic_dmic_driver = {
	.probe          = dmic_generic_card_probe,
	.driver = {
		.name   = "dmic_generic_card",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = board_ids,
};
module_platform_driver(generic_dmic_driver);

MODULE_DESCRIPTION("ASoC Intel(R) SOF Generic DMIC card");
MODULE_LICENSE("GPL");
