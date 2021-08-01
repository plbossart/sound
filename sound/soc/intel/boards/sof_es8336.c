// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018-2020, Intel Corporation
//
// sof-es8336.c - ASoC machine driver for Up and Up2 board
// based on WM8804/Hifiberry Digi+


#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <linux/input.h>
#include <sound/soc-acpi.h>

#define SOF_PCM512X_SSP_CODEC(quirk)		((quirk) & GENMASK(3, 0))
#define SOF_PCM512X_SSP_CODEC_MASK			(GENMASK(3, 0))


#define SOF_ES8336_UP2_QUIRK			BIT(0)
#define SSP 2
struct sof_card_private {
	struct gpio_desc *gpio_pa;
	struct snd_soc_jack jack;
	bool speaker_en;
	//struct gpio_desc *gpio_48;
	int sample_rate;
};
///////////////////////////////////////////////////////////
enum {
	BYT_CHT_ES8316_INTMIC_IN1_MAP,
	BYT_CHT_ES8316_INTMIC_IN2_MAP,
};
#define BYT_CHT_ES8316_MAP(quirk)		((quirk) & GENMASK(3, 0))

static unsigned long quirk = 0;

static int quirk_override = -1;
module_param_named(quirk, quirk_override, int, 0444);
MODULE_PARM_DESC(quirk, "Board-specific quirk override");
////////////////////////////////////////////////////////////

static unsigned long sof_es8336_quirk=SOF_PCM512X_SSP_CODEC(SSP);
/////////////////////////////////////////////////////////////////////
static int byt_cht_es8316_speaker_power_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_card *card = w->dapm->card;
	struct sof_card_private *priv = snd_soc_card_get_drvdata(card);
	printk("Enter into %s %d \n", __func__,SND_SOC_DAPM_EVENT_ON(event));
	if (SND_SOC_DAPM_EVENT_ON(event))
		priv->speaker_en = false;
	else
		priv->speaker_en = true;

	gpiod_set_value_cansleep(priv->gpio_pa, priv->speaker_en);

	printk("Exit %s\n", __func__);
	return 0;
}

static const struct snd_soc_dapm_widget byt_cht_es8316_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),

	SND_SOC_DAPM_SUPPLY("Speaker Power", SND_SOC_NOPM, 0, 0,
			    byt_cht_es8316_speaker_power_event,
			    SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
};

static const struct snd_soc_dapm_route byt_cht_es8316_audio_map[] = {
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

static const struct snd_soc_dapm_route byt_cht_es8316_intmic_in1_map[] = {
	{"MIC1", NULL, "Internal Mic"},
	{"MIC2", NULL, "Headset Mic"},
};

static const struct snd_soc_dapm_route byt_cht_es8316_intmic_in2_map[] = {
	{"MIC2", NULL, "Internal Mic"},
	{"MIC1", NULL, "Headset Mic"},
};

static const struct snd_kcontrol_new byt_cht_es8316_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
};

static struct snd_soc_jack_pin byt_cht_es8316_jack_pins[] = {
	{
		.pin	= "Headphone",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Headset Mic",
		.mask	= SND_JACK_MICROPHONE,
	},
};		
static int byt_cht_es8316_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_component *codec = asoc_rtd_to_codec(runtime, 0)->component;
	struct snd_soc_card *card = runtime->card;
	struct sof_card_private *priv = snd_soc_card_get_drvdata(card);
	const struct snd_soc_dapm_route *custom_map;
	int num_routes;
	struct snd_soc_jack *jack;
	int ret;
	printk("Enter into %s\n", __func__);
#if 1
	card->dapm.idle_bias_off = true;
/*
	switch (BYT_CHT_ES8316_MAP(quirk)) {
	case BYT_CHT_ES8316_INTMIC_IN1_MAP:
	default:
		custom_map = byt_cht_es8316_intmic_in1_map;
		num_routes = ARRAY_SIZE(byt_cht_es8316_intmic_in1_map);
		break;
	case BYT_CHT_ES8316_INTMIC_IN2_MAP:
		custom_map = byt_cht_es8316_intmic_in2_map;
		num_routes = ARRAY_SIZE(byt_cht_es8316_intmic_in2_map);
		break;
	}
*/

	custom_map = byt_cht_es8316_intmic_in1_map;
	num_routes = ARRAY_SIZE(byt_cht_es8316_intmic_in1_map);
	
#endif
	ret = snd_soc_dapm_add_routes(&card->dapm, custom_map, num_routes);
	if (ret)
		return ret;

	ret = snd_soc_card_jack_new(card, "Headset",
				    SND_JACK_HEADSET | SND_JACK_BTN_0,
				    &priv->jack, byt_cht_es8316_jack_pins,
				    ARRAY_SIZE(byt_cht_es8316_jack_pins));
	if (ret) {
		dev_err(card->dev, "jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(priv->jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_soc_component_set_jack(codec, &priv->jack, NULL);
	//gpiod_set_value_cansleep(priv->gpio_pa, false);
	printk("Exit %s\n", __func__);
	return 0;
}
static void byt_cht_es8316_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;

	snd_soc_component_set_jack(component, NULL, NULL);
}
///////////////////////////////////////////////////////////////////
static int sof_es8336_quirk_cb(const struct dmi_system_id *id)
{
	sof_es8336_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id sof_es8336_quirk_table[] = {
	{
		.callback = sof_es8336_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "AAEON"),
			DMI_MATCH(DMI_PRODUCT_NAME, "UP-APL01"),
		},
		.driver_data =(void *)(SOF_PCM512X_SSP_CODEC(2)),
	},
	{}
};

static int sof_es8336_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *codec = codec_dai->component;

	printk("%s",__func__);
	const int sysclk = 19200000; /* This is fixed on this board */
	int samplerate;
	long mclk_freq=19200000;
	int mclk_div;
	int sampling_freq;
	//bool clk_44;
	int ret;
	
	samplerate = params_rate(params);
	//printk("samplerate %d\n",samplerate);
	samplerate=48000;
	if (samplerate == ctx->sample_rate)
		return 0;

	ctx->sample_rate = 0;
	

 	if (!(IS_ERR_OR_NULL(ctx->gpio_pa))) {
	   printk("%s, set 'gpio-pa' pin to false\n", __func__);
           gpiod_set_value_cansleep(ctx->gpio_pa, false);
	gpiod_set_value_cansleep(ctx->gpio_pa, true);
        }

	//snd_soc_dai_set_clkdiv(codec_dai, WM8804_MCLK_DIV, mclk_div);
	//ret = snd_soc_dai_set_pll(codec_dai, 0, 0, sysclk, mclk_freq);
	//if (ret < 0) {
	//	dev_err(rtd->card->dev, "Failed to set WM8804 PLL\n");
		//return ret;
	//}

	ret = snd_soc_dai_set_sysclk(codec_dai, 1,
				     sysclk, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		printk("%s, Failed to set WM8804 SYSCLK: %d\n",
			 __func__, ret);
		return ret;
	}
	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x3, 0x3, 2, 24);
	if (ret < 0) {
		printk("%d\n",params_width(params));
		printk("%s, set TDM slot err:%d\n", __func__, ret);
		//return ret;
	}
	/* set sampling frequency status bits */

	ctx->sample_rate = samplerate;

	printk("Exit %s\n", __func__);
	return 0;
}

static int geminilake_ssp_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *chan = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* The ADSP will convert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
	chan->min = chan->max = 2;

	/* set SSP to 24 bit */
	snd_mask_none(fmt);
	snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_LE);

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
#if SSP==0
SND_SOC_DAILINK_DEF(ssp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP0 Pin")));//ssp
#elif SSP==1
SND_SOC_DAILINK_DEF(ssp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP1 Pin")));//ssp
#elif SSP==2
SND_SOC_DAILINK_DEF(ssp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP2 Pin")));//ssp
#endif
SND_SOC_DAILINK_DEF(ssp1_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-ESSX8336:00", "ES8316 HiFi")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("0000:00:1f.3")));
/* SoC card */
static struct snd_soc_card sof_es8336_card = {
	.name = "essx8336", /* sof- prefix added automatically */
	.owner = THIS_MODULE,
	.dapm_widgets = byt_cht_es8316_widgets,
	.num_dapm_widgets = ARRAY_SIZE(byt_cht_es8316_widgets),
	.dapm_routes = byt_cht_es8316_audio_map,
	.num_dapm_routes = ARRAY_SIZE(byt_cht_es8316_audio_map),
	.controls = byt_cht_es8316_controls,
	.num_controls = ARRAY_SIZE(byt_cht_es8316_controls),
	.fully_routed = true,
	//.dai_link = dailink,
	.num_links = 1,
};
static struct snd_soc_dai_link *sof_card_dai_links_create(struct device *dev,
							  int ssp_codec
							 )
{
	struct snd_soc_dai_link_component *idisp_components;
	struct snd_soc_dai_link_component *cpus;
	struct snd_soc_dai_link *links;
	int i, id = 0;
	
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
	links[id].init = byt_cht_es8316_init;
	links[id].exit = byt_cht_es8316_exit;
	links[id].ops = &sof_es8336_ops;
	links[id].nonatomic = true;
	links[id].dpcm_playback = 1;
	links[id].dpcm_capture = 1;
	/*
	 * capture only supported with specific versions of the Hifiberry DAC+
	 * links[id].dpcm_capture = 1;
	 */
	links[id].no_pcm = 1;
	links[id].cpus = &cpus[id];
	links[id].num_cpus = 1;
	links[id].be_hw_params_fixup = geminilake_ssp_fixup,//531

	links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
							  "SSP%d Pin",
							  ssp_codec);
		if (!links[id].cpus->dai_name)
			goto devm_err;

	id++;
	
	printk("%s %d",__func__,__LINE__);
	return links;
devm_err:
	return NULL;
}



static struct snd_soc_dai_link dailink[] = {
	/* back ends */
	{
#if SSP==0	
		.name = "SSP0-Codec",
#elif SSP==1
		.name = "SSP1-Codec",
#elif SSP==2
		.name = "SSP2-Codec",
#endif		
		.id = 0,
		.no_pcm = 1,
		.nonatomic = true,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &sof_es8336_ops,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS,
		.be_hw_params_fixup = geminilake_ssp_fixup,
		SND_SOC_DAILINK_REG(ssp1_pin, ssp1_codec, platform),
	},
};



 /* i2c-<HID>:00 with HID being 8 chars */
static char codec_name[SND_ACPI_I2C_ID_LEN];

/*
 * to control the HifiBerry Digi+ PRO, it's required to toggle GPIO to
 * select the clock source. On the Up2 board, this means
 * Pin29/BCM5/Linux GPIO 430 and Pin 31/BCM6/ Linux GPIO 404.
 *
 * Using the ACPI device name is not very nice, but since we only use
 * the value for the Up2 board there is no risk of conflict with other
 * platforms.
 */

static struct gpiod_lookup_table up2_gpios_table = {
	/* .dev_id is set during probe */
	.table = {
		//GPIO_LOOKUP("INT3453:02", 9, "PA_ENABLE", GPIO_ACTIVE_LOW),
		//GPIO_LOOKUP("INT34BB:00", 264, "PA_ENABLE", GPIO_ACTIVE_LOW),//cnl kb
		GPIO_LOOKUP("INT3453:00", 29, "PA_ENABLE", GPIO_ACTIVE_LOW), //glk kb
		{ },
	},
};

static int sof_es8336_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct snd_soc_acpi_mach *mach= pdev->dev.platform_data;
	struct sof_card_private *ctx;
	struct acpi_device *adev;
	int dai_index = 0;
	int ret;
	int i;
	struct snd_soc_dai_link *dai_links;
	printk("enter into %s %d",__func__,__LINE__);

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mach = pdev->dev.platform_data;

	card = &sof_es8336_card;
	card->dev = &pdev->dev;

	dmi_check_system(sof_es8336_quirk_table);

       //if (sof_es8336_quirk & SOF_ES8336_UP2_QUIRK) {
        printk("%s", dev_name(&pdev->dev));
	up2_gpios_table.dev_id = dev_name(&pdev->dev);
        gpiod_add_lookup_table(&up2_gpios_table);

                /*
                 * The gpios are required for specific boards with
                 * local oscillators, and optional in other cases.
                 * Since we can't identify when they are needed, use
                 * the GPIO as non-optional
                 */

        ctx->gpio_pa = devm_gpiod_get(&pdev->dev, "PA_ENABLE",
                                      GPIOD_OUT_LOW);
        if (IS_ERR(ctx->gpio_pa)) {
                ret = PTR_ERR(ctx->gpio_pa);
                printk("%s, could not get PA_ENABLE: %d\n",
                        ret);
                return ret;
        }
	//}
     
	
	printk("%s %d-------1",__func__,__LINE__);
	dai_links = sof_card_dai_links_create(&pdev->dev, SSP);
	if(!dai_links)
		return -ENOMEM;
	printk("%s %d",__func__,__LINE__);
	sof_es8336_card.dai_link = dai_links;
	// fix index of codec dai 
	printk("ARRAY_SIZE(dailink) %ld",ARRAY_SIZE(dailink));
	for (i = 0; i < ARRAY_SIZE(dailink); i++) {
		
		if (!strcmp(dailink[i].codecs->name, "i2c-ESSX8336:00")) {
			dai_index = i;
			
			printk("dailink[%d].codecs->name %s",dai_index,dailink[i].codecs->name);
			break;
		}
	}
	ret = snd_soc_fixup_dai_links_platform_name(&sof_es8336_card,
						    mach->mach_params.platform);
	if (ret)
		return ret;
	// fixup codec name based on HID 
	adev = acpi_dev_get_first_match_dev(mach->id, NULL, -1);
	if (adev) {
		snprintf(codec_name, sizeof(codec_name),
			 "%s%s", "i2c-", acpi_dev_name(adev));
		put_device(&adev->dev);
		dailink[dai_index].codecs->name = codec_name;
	}
	printk("cpus %s",dailink[0].cpus->dai_name);	
	printk("dailink[%d].codecs->name %s dailink[0].codecs->dai_name %s\n",
		dai_index,dailink[0].codecs->name, 
		dailink[0].codecs->dai_name );
	printk("name %s stream_name %s",dailink[0].name, dailink[0].stream_name);
	printk("%d platform %s dainame %s",dailink[0].num_platforms, 
		dailink[0].platforms->name, dailink[0].platforms->dai_name);
	
	
	snd_soc_card_set_drvdata(card, ctx);
	
	return devm_snd_soc_register_card(&pdev->dev, card);
}

static int sof_es8336_remove(struct platform_device *pdev)
{
	if (sof_es8336_quirk & SOF_ES8336_UP2_QUIRK)
		gpiod_remove_lookup_table(&up2_gpios_table);
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
MODULE_AUTHOR("zhuning@everest-semi.com");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sof-essx8336");
