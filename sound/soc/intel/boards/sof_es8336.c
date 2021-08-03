// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2021 Intel Corporation.

/*
 * Intel SOF Machine Driver with es8336 Codec
 */

#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>

#define SOF_ES8336_SSP_CODEC(quirk)		((quirk) & GENMASK(3, 0))
#define SOF_ES8336_SSP_CODEC_MASK		(GENMASK(3, 0))

#define SOF_ES8336_PA_GPIO_QUIRK		BIT(4)

static unsigned long quirk;

static int quirk_override = -1;
module_param_named(quirk, quirk_override, int, 0444);
MODULE_PARM_DESC(quirk, "Board-specific quirk override");

struct sof_card_private {
	struct gpio_desc *gpio_pa;
	struct snd_soc_jack jack;
	bool speaker_en;
};

static void log_quirks(struct device *dev)
{
	dev_info(dev, "quirk SSP%ld",  SOF_ES8336_SSP_CODEC(quirk));
	if (quirk & SOF_ES8336_PA_GPIO_QUIRK)
		dev_info(dev, "quirk PA_GPIO enabled");
}

static int sof_es8316_speaker_power_event(struct snd_soc_dapm_widget *w,
					  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_card *card = w->dapm->card;
	struct sof_card_private *priv = snd_soc_card_get_drvdata(card);

	if (SND_SOC_DAPM_EVENT_ON(event))
		priv->speaker_en = false;
	else
		priv->speaker_en = true;

	gpiod_set_value_cansleep(priv->gpio_pa, priv->speaker_en);

	return 0;
}

static const struct snd_soc_dapm_widget sof_es8316_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),

	SND_SOC_DAPM_SUPPLY("Speaker Power", SND_SOC_NOPM, 0, 0,
			    sof_es8316_speaker_power_event,
			    SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
};

static const struct snd_soc_dapm_route sof_es8316_audio_map[] = {
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},

	/*
	 * There is no separate speaker output instead the speakers are muxed to
	 * the HP outputs. The mux is controlled by the "Speaker Power" supply.
	 */
	{"Speaker", NULL, "HPOL"},
	{"Speaker", NULL, "HPOR"},
	{"Speaker", NULL, "Speaker Power"},
};

static const struct snd_soc_dapm_route sof_es8316_intmic_in1_map[] = {
	{"MIC1", NULL, "Internal Mic"},
	{"MIC2", NULL, "Headset Mic"},
};

static const struct snd_kcontrol_new sof_es8316_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
};

static struct snd_soc_jack_pin sof_es8316_jack_pins[] = {
	{
		.pin	= "Headphone",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Headset Mic",
		.mask	= SND_JACK_MICROPHONE,
	},
};

static int sof_es8316_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_component *codec = asoc_rtd_to_codec(runtime, 0)->component;
	struct snd_soc_card *card = runtime->card;
	struct sof_card_private *priv = snd_soc_card_get_drvdata(card);
	const struct snd_soc_dapm_route *custom_map;
	int num_routes;
	int ret;

	card->dapm.idle_bias_off = true;

	custom_map = sof_es8316_intmic_in1_map;
	num_routes = ARRAY_SIZE(sof_es8316_intmic_in1_map);

	ret = snd_soc_dapm_add_routes(&card->dapm, custom_map, num_routes);
	if (ret)
		return ret;

	ret = snd_soc_card_jack_new(card, "Headset",
				    SND_JACK_HEADSET | SND_JACK_BTN_0,
				    &priv->jack, sof_es8316_jack_pins,
				    ARRAY_SIZE(sof_es8316_jack_pins));
	if (ret) {
		dev_err(card->dev, "jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(priv->jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);

	snd_soc_component_set_jack(codec, &priv->jack, NULL);

	return 0;
}

static void sof_es8316_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static const struct dmi_system_id sof_es8336_quirk_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "CHUWI Innovation And Technology"),
			DMI_MATCH(DMI_BOARD_NAME, "Hi10 X"),
		},
		.driver_data = (void *)(SOF_ES8336_SSP_CODEC(2) |
					SOF_ES8336_PA_GPIO_QUIRK)
	},
	{}
};

static int sof_es8336_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	const int sysclk = 19200000;
	int ret;

	if (!(IS_ERR_OR_NULL(ctx->gpio_pa))) {
		gpiod_set_value_cansleep(ctx->gpio_pa, false);
		gpiod_set_value_cansleep(ctx->gpio_pa, true);
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, 1, sysclk, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(rtd->dev, "%s, Failed to set ES8336 SYSCLK: %d\n",
			__func__, ret);
		return ret;
	}

	return 0;
}

/* machine stream operations */
static struct snd_soc_ops sof_es8336_ops = {
	.hw_params = sof_es8336_hw_params,
};

static struct snd_soc_dai_link_component platform_component[] = {
	{
		/* name might be overridden during probe */
		.name = "0000:00:1f.3"
	}
};

SND_SOC_DAILINK_DEF(ssp1_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-ESSX8336:00", "ES8316 HiFi")));

/* SoC card */
static struct snd_soc_card sof_es8336_card = {
	.name = "essx8336", /* sof- prefix added automatically */
	.owner = THIS_MODULE,
	.dapm_widgets = sof_es8316_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sof_es8316_widgets),
	.dapm_routes = sof_es8316_audio_map,
	.num_dapm_routes = ARRAY_SIZE(sof_es8316_audio_map),
	.controls = sof_es8316_controls,
	.num_controls = ARRAY_SIZE(sof_es8316_controls),
	.fully_routed = true,
	.num_links = 1,
};

static struct snd_soc_dai_link *sof_card_dai_links_create(struct device *dev, int ssp_codec)
{
	struct snd_soc_dai_link_component *cpus;
	struct snd_soc_dai_link *links;
	int id = 0;

	links = devm_kcalloc(dev, sof_es8336_card.num_links,
			     sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	cpus = devm_kcalloc(dev, sof_es8336_card.num_links,
			    sizeof(struct snd_soc_dai_link_component), GFP_KERNEL);
	if (!links || !cpus)
		goto devm_err;

	/* codec SSP */
	links[id].name = devm_kasprintf(dev, GFP_KERNEL,
					"SSP%d-Codec", ssp_codec);
	if (!links[id].name)
		goto devm_err;

	links[id].id = id;
	links[id].codecs = ssp1_codec;
	links[id].num_codecs = ARRAY_SIZE(ssp1_codec);
	links[id].platforms = platform_component;
	links[id].num_platforms = ARRAY_SIZE(platform_component);
	links[id].init = sof_es8316_init;
	links[id].exit = sof_es8316_exit;
	links[id].ops = &sof_es8336_ops;
	links[id].nonatomic = true;
	links[id].dpcm_playback = 1;
	links[id].dpcm_capture = 1;
	links[id].no_pcm = 1;
	links[id].cpus = &cpus[id];
	links[id].num_cpus = 1;

	links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
						  "SSP%d Pin",
						  ssp_codec);
	if (!links[id].cpus->dai_name)
		goto devm_err;

	id++;

	return links;
devm_err:
	return NULL;
}

 /* i2c-<HID>:00 with HID being 8 chars */
static char codec_name[SND_ACPI_I2C_ID_LEN];

// FIXME: add comment on INT3453
static struct gpiod_lookup_table gpios_table = {
	/* .dev_id is set during probe */
	.table = {
		GPIO_LOOKUP("INT3453:00", 29, "PA_ENABLE", GPIO_ACTIVE_LOW),
		{ },
	},
};

static int sof_es8336_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card;
	struct snd_soc_acpi_mach *mach = pdev->dev.platform_data;
	const struct dmi_system_id *dmi_id;
	struct sof_card_private *ctx;
	struct acpi_device *adev;
	struct snd_soc_dai_link *dai_links;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mach = pdev->dev.platform_data;

	card = &sof_es8336_card;
	card->dev = dev;

	dmi_id = dmi_first_match(sof_es8336_quirk_table);
	if (dmi_id) {
		quirk = (unsigned long)dmi_id->driver_data;
	} else {
		quirk = SOF_ES8336_SSP_CODEC(2) |
			SOF_ES8336_PA_GPIO_QUIRK;
	}
	if (quirk_override != -1) {
		dev_info(dev, "Overriding quirk 0x%lx => 0x%x\n",
			 quirk, quirk_override);
		quirk = quirk_override;
	}
	log_quirks(dev);

	if (quirk & SOF_ES8336_PA_GPIO_QUIRK) {
		gpios_table.dev_id = dev_name(dev);
		gpiod_add_lookup_table(&gpios_table);

		ctx->gpio_pa = devm_gpiod_get(dev, "PA_ENABLE",
					      GPIOD_OUT_LOW);
		if (IS_ERR(ctx->gpio_pa)) {
			ret = PTR_ERR(ctx->gpio_pa);
			dev_err(dev, "%s, could not get PA_ENABLE: %d\n",
				__func__, ret);
			return ret;
		}
	}

	dai_links = sof_card_dai_links_create(dev,
					      SOF_ES8336_SSP_CODEC(quirk));
	if (!dai_links)
		return -ENOMEM;

	sof_es8336_card.dai_link = dai_links;

	/* fixup codec name based on HID */
	adev = acpi_dev_get_first_match_dev(mach->id, NULL, -1);
	if (adev) {
		snprintf(codec_name, sizeof(codec_name),
			 "i2c-%s", acpi_dev_name(adev));
		put_device(&adev->dev);
		dai_links[0].codecs->name = codec_name;
	}

	ret = snd_soc_fixup_dai_links_platform_name(&sof_es8336_card,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(card, ctx);

	return devm_snd_soc_register_card(dev, card);
}

static int sof_es8336_remove(struct platform_device *pdev)
{
	if (quirk & SOF_ES8336_PA_GPIO_QUIRK)
		gpiod_remove_lookup_table(&gpios_table);
	return 0;
}

static struct platform_driver sof_es8336_driver = {
	.driver = {
		.name = "sof-essx8336",
		.pm = &snd_soc_pm_ops,
	},
	.probe = sof_es8336_probe,
	.remove = sof_es8336_remove,
};
module_platform_driver(sof_es8336_driver);

MODULE_DESCRIPTION("ASoC Intel(R) SOF + ES8336 Machine driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sof-essx8336");
