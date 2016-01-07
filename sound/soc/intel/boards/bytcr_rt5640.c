/*
 *  byt_cr_dpcm_rt5640.c - ASoc Machine driver for Intel Byt CR platform
 *
 *  Copyright (C) 2014 Intel Corp
 *  Author: Subhransu S. Prusty <subhransu.s.prusty@intel.com>
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
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/input.h> /* FIXME ? needed for KEY_MEDIA */
#include <linux/dmi.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/vlv2_plat_clock.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include "../../codecs/rt5640.h"
#include "../atom/sst-atom-controls.h"
#include "../common/sst-acpi.h"
#include "byt_cr_board_configs.h"

#define BYT_JD_INTR_DEBOUNCE		0

#define BYT_T_JACK_RECHECK		100 /* ms */
#define BYT_N_JACK_RECHECK		5
#define BYT_T_BUTTONS_RECHECK		100 /* ms */

#define RT5640_GPIO_NA			-1

enum jack_int_select {
	JACK_INT1, /* AUDIO_INT */
	JACK_INT2, /* JACK_DET */
};

enum jack_bp_select {
	JACK_BP_CODEC,
	JACK_BP_MICBIAS,
};

enum {
	RT5640_GPIO_JD_INT,
	RT5640_GPIO_JD_INT2,
	RT5640_GPIO_JD_JACK_SWITCH,
	RT5640_GPIO_JD_BUTTONS,
	RT5640_GPIO_I2S_TRISTATE,
};

struct rt5640_gpios {
	int jd_int_gpio;
	int jd_int2_gpio;
	int jd_buttons_gpio;
	int debug_mux_gpio;
	int i2s_tristate_en_gpio;
	int int_count;
};

struct byt_drvdata {
	struct snd_soc_codec *codec;

	int jack_active_low;
	enum jack_int_select jack_int_sel;
	enum jack_bp_select jack_bp_sel;

	struct snd_soc_jack jack;
	struct delayed_work jack_recheck;
	struct delayed_work bp_recheck;
	int t_jack_recheck;
	int t_buttons_recheck;
	int jack_hp_count;
	struct mutex jack_mlock;
	struct rt5640_gpios gpios;
};

#define BYT_CODEC_DAI	"rt5640-aif1"

static inline struct snd_soc_dai *byt_get_codec_dai(struct snd_soc_card *card)
{
	int i;

	for (i = 0; i < card->num_rtd; i++) {
		struct snd_soc_pcm_runtime *rtd;

		rtd = card->rtd + i;
		if (!strncmp(rtd->codec_dai->name, BYT_CODEC_DAI,
					strlen(BYT_CODEC_DAI)))
			return rtd->codec_dai;
	}
	return NULL;
}

static int platform_clock_control(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *codec_dai;
	int ret;

	codec_dai = byt_get_codec_dai(card);
	if (!codec_dai) {
		dev_err(card->dev,
				"Codec dai not found; Unable to set platform clock\n");
		return -EIO;
	}

	if (SND_SOC_DAPM_EVENT_ON(event)) {

		if (byt_rt5640_quirk & BYT_RT5640_MCLK_EN) {
			ret = vlv2_plat_configure_clock(VLV2_PLT_CLK_AUDIO,
					VLV2_PLT_CLK_CONFG_FORCE_ON);
			if (ret < 0) {
				dev_err(card->dev,
						"could not configure MCLK state");
				return ret;
			}
		}
		ret = snd_soc_dai_set_sysclk(codec_dai, RT5640_SCLK_S_PLL1,
				48000 * 512,
				SND_SOC_CLOCK_IN);
	} else {
		/* Set codec clock source to internal clock before
		   turning off the platform clock. Codec needs clock
		   for Jack detection and button press */

		ret = snd_soc_dai_set_sysclk(codec_dai, RT5640_SCLK_S_RCCLK,
				0,
				SND_SOC_CLOCK_IN);
		if (!ret) {
			if (byt_rt5640_quirk & BYT_RT5640_MCLK_EN) {
				ret = vlv2_plat_configure_clock(
						VLV2_PLT_CLK_AUDIO,
						VLV2_PLT_CLK_CONFG_FORCE_OFF);
				if (ret) {
					dev_err(card->dev,
							"could not configure MCLK state");
					return ret;
				}
			}
		}
	}

	if (ret < 0) {
		dev_err(card->dev, "can't set codec sysclk: %d\n", ret);
		return ret;
	}

	return 0;
}



static const struct snd_soc_dapm_widget byt_rt5640_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			platform_clock_control, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),

};

static const struct snd_soc_dapm_route byt_rt5640_audio_map[] = {
	{"AIF1 Playback", NULL, "ssp2 Tx"},
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx"},
	{"codec_in1", NULL, "ssp2 Rx"},
	{"ssp2 Rx", NULL, "AIF1 Capture"},

	{"Headphone", NULL, "Platform Clock"},
	{"Headset Mic", NULL, "Platform Clock"},
	{"Internal Mic", NULL, "Platform Clock"},
	{"Speaker", NULL, "Platform Clock"},

	{"Headset Mic", NULL, "MICBIAS1"},
	{"IN2P", NULL, "Headset Mic"},
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},
	{"Speaker", NULL, "SPOLP"},
	{"Speaker", NULL, "SPOLN"},
	{"Speaker", NULL, "SPORP"},
	{"Speaker", NULL, "SPORN"},
};

static const struct snd_soc_dapm_route byt_rt5640_intmic_dmic1_map[] = {
	{"DMIC1", NULL, "Internal Mic"},
};

static const struct snd_soc_dapm_route byt_rt5640_intmic_dmic2_map[] = {
	{"DMIC2", NULL, "Internal Mic"},
};

static const struct snd_soc_dapm_route byt_rt5640_intmic_in1_map[] = {
	{"Internal Mic", NULL, "MICBIAS1"},
	{"IN1P", NULL, "Internal Mic"},
};

static const struct snd_soc_dapm_route byt_rt5640_intmic_in3_map[] = {
	{"Internal Mic", NULL, "MICBIAS1"},
	{"IN3P", NULL, "Internal Mic"},
};

static const struct snd_kcontrol_new byt_rt5640_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

static inline void byt_force_enable_pin(struct snd_soc_codec *codec,
		const char *bias_widget, bool enable)
{
	struct snd_soc_dapm_context *dapm;
	dapm = snd_soc_codec_get_dapm(codec);
	if (enable)
		snd_soc_dapm_force_enable_pin(dapm, bias_widget);
	else
		snd_soc_dapm_disable_pin(dapm, bias_widget);

	pr_debug("%s: %s widget %s.\n", __func__,
			enable ? "Enabled" : "Disabled", bias_widget);
}

static inline void byt_set_mic_bias_ldo(struct snd_soc_codec *codec,
		bool enable)
{
	struct snd_soc_dapm_context *dapm;
	dapm = snd_soc_codec_get_dapm(codec);
	if (enable) {
		byt_force_enable_pin(codec, "MICBIAS1", true);
		byt_force_enable_pin(codec, "LDO2", true);
	} else {
		byt_force_enable_pin(codec, "MICBIAS1", false);
		byt_force_enable_pin(codec, "LDO2", false);
	}

	snd_soc_dapm_sync(dapm);
}

static inline bool byt_hs_inserted(struct byt_drvdata *drvdata)
{
	bool val;
	const struct gpio_desc *desc;
	int gpio;

	if (drvdata->jack_int_sel == JACK_INT1)
		gpio = drvdata->gpios.jd_int_gpio;
	else /* JACK_INT2 */
		gpio = drvdata->gpios.jd_int2_gpio;

	desc = gpio_to_desc(gpio);
	val = (bool)gpiod_get_value(desc);

	if (drvdata->jack_active_low)
		val = !val;

	pr_info("%s: val = %d (pin = %d, active_low = %d, jack_int_sel = %d)\n",
			__func__, val, gpio, drvdata->jack_active_low,
			drvdata->jack_int_sel);

	return val;
}

static int byt_bp_check(struct byt_drvdata *drvdata, bool is_recheck)
{
	struct snd_soc_jack *jack = &drvdata->jack;
	struct snd_soc_codec *codec = drvdata->codec;
	int val;
	struct gpio_desc *desc;

	pr_debug("%s: Enter.\n", __func__);

	if (drvdata->jack_bp_sel == JACK_BP_CODEC)
		val = rt5640_check_bp_status(codec);
	else { /* JACK_BP_MICBIAS */
		desc = gpio_to_desc(drvdata->gpios.jd_buttons_gpio);
		val = gpiod_get_value(desc);
	}

	if (((val == 0) && !(jack->status & SND_JACK_BTN_0)) ||
			((val > 0) && (jack->status & SND_JACK_BTN_0))) {
		pr_debug("%s: No change (%d).\n", __func__, val);
		return false;
	}

	if (val == 0) {
		if (!is_recheck) {
			pr_info("%s: Button release.\n", __func__);
			jack->status &= ~SND_JACK_BTN_0;
			return true;
		} else {
			pr_warn("%s: Fishy interrupt detected.\n", __func__);
			return false;
		}
	} else {
		if (!is_recheck) {
			pr_debug("%s: Button press (preliminary).\n", __func__);
			schedule_delayed_work(&drvdata->bp_recheck,
					drvdata->t_buttons_recheck);
			return false;
		} else {
			jack->status |= SND_JACK_BTN_0;
			pr_info("%s: Button press.\n", __func__);
			return true;
		}
	}

	return true;
}

static void byt_bp_recheck(struct work_struct *work)
{
	struct byt_drvdata *drvdata =
		container_of(work, struct byt_drvdata, bp_recheck.work);
	struct snd_soc_jack *jack = &drvdata->jack;

	mutex_lock(&drvdata->jack_mlock);
	pr_debug("%s: Enter.\n", __func__);

	if (byt_bp_check(drvdata, true))
		snd_soc_jack_report(jack, jack->status, SND_JACK_BTN_0);

	mutex_unlock(&drvdata->jack_mlock);
}

static bool byt_jack_check(struct byt_drvdata *drvdata, bool is_recheck,
		bool is_resume_check)
{
	struct snd_soc_jack *jack = &drvdata->jack;
	struct snd_soc_codec *codec = drvdata->codec;
	int inserted, status;
	bool changed = false;

	pr_debug("%s: Enter (jack->status = %d).\n", __func__, jack->status);

	if (codec == NULL) {
		pr_debug("%s: codec is not initialized, something went wrong\n", __func__);
		return false;
	}

	inserted = byt_hs_inserted(drvdata);
	if (inserted) {
		if (!(jack->status & SND_JACK_HEADPHONE)) {
			status = rt5640_detect_hs_type(codec, true);
			if (status == RT5640_HEADPHO_DET) {
				if (drvdata->jack_hp_count > 0) {
					pr_debug("%s: Headphones detected (preliminary, %d).\n",
							__func__,
							drvdata->jack_hp_count);
					drvdata->jack_hp_count--;
					schedule_delayed_work(
							&drvdata->jack_recheck,
							drvdata->t_jack_recheck);
				} else {
					pr_info("%s: Headphones present.\n",
							__func__);
					jack->status |= SND_JACK_HEADPHONE;
					changed = true;
					drvdata->jack_hp_count =
						BYT_N_JACK_RECHECK;
				}
			} else if (status == RT5640_HEADSET_DET) {
				pr_info("%s: Headset present.\n", __func__);
				byt_set_mic_bias_ldo(codec, true);
				if (drvdata->jack_bp_sel ==
						JACK_BP_CODEC)
					rt5640_enable_ovcd_interrupt(codec,
							true);
				jack->status |= SND_JACK_HEADSET;
				changed = true;
				drvdata->jack_hp_count = BYT_N_JACK_RECHECK;
			} else
				pr_warn("%s: No valid accessory present!\n",
						__func__);
		}
	} else {
		if (jack->status & SND_JACK_HEADPHONE) {
			if (jack->status & SND_JACK_MICROPHONE) {
				jack->status &= ~SND_JACK_HEADSET;
				changed = true;
				byt_set_mic_bias_ldo(codec, false);
				if (drvdata->jack_bp_sel ==
						JACK_BP_CODEC)
					rt5640_enable_ovcd_interrupt(codec,
							false);
				pr_info("%s: Headset removed.\n", __func__);
			} else {
				jack->status &= ~SND_JACK_HEADPHONE;
				pr_info("%s: Headphone removed.\n", __func__);
			}
		} else if (!is_resume_check)
			pr_warn("%s: Remove-interrupt while no accessory present!\n",
					__func__);
	}

	return changed;
}

static void byt_jack_recheck(struct work_struct *work)
{
	struct byt_drvdata *drvdata =
		container_of(work, struct byt_drvdata, jack_recheck.work);
	struct snd_soc_jack *jack = &drvdata->jack;

	mutex_lock(&drvdata->jack_mlock);
	pr_debug("%s: Enter.\n", __func__);

	if (byt_jack_check(drvdata, true, false))
		snd_soc_jack_report(jack, jack->status, SND_JACK_HEADSET);

	mutex_unlock(&drvdata->jack_mlock);
}

/* Interrupt on micbias (JACK_DET_BAK/HOOK_INT) */
static int byt_micbias_interrupt(void *data)
{
	struct byt_drvdata *drvdata = (struct byt_drvdata *)data;
	struct snd_soc_jack *jack = &drvdata->jack;

	mutex_lock(&drvdata->jack_mlock);
	pr_debug("%s: Enter.\n", __func__);

	if (cancel_delayed_work_sync(&drvdata->bp_recheck))
		pr_debug("%s: bp-recheck interrupted!\n", __func__);

	if (jack->status & SND_JACK_MICROPHONE)
		if (byt_bp_check(drvdata, false))
			snd_soc_jack_report(jack, jack->status,
					SND_JACK_BTN_0);

	mutex_unlock(&drvdata->jack_mlock);
	return jack->status;
}

/* Interrupt on int2 (JACK_DET) */
static int byt_jack_int2_interrupt(void *data)
{
	struct byt_drvdata *drvdata = (struct byt_drvdata *)data;
	struct snd_soc_jack *jack = &drvdata->jack;

	if ((drvdata->jack_bp_sel == JACK_BP_MICBIAS) &&
			(drvdata->jack_int_sel == JACK_INT1)) {
		pr_debug("%s: INT2 not used. Returning...\n", __func__);
		return jack->status;
	}

	mutex_lock(&drvdata->jack_mlock);
	pr_debug("%s: Enter (jack->status = %d).\n", __func__, jack->status);

	if (cancel_delayed_work_sync(&drvdata->bp_recheck))
		pr_debug("%s: bp-recheck interrupted!\n", __func__);
	if (cancel_delayed_work_sync(&drvdata->jack_recheck))
		pr_debug("%s: jack-recheck interrupted!\n", __func__);

	/* Check for jack-events */
	byt_jack_check(drvdata, false, false);

	mutex_unlock(&drvdata->jack_mlock);
	return jack->status;
}

/* Interrupt on int1 (AUDIO_INT) */
static int byt_jack_int1_interrupt(void *data)
{
	struct byt_drvdata *drvdata = (struct byt_drvdata *)data;
	struct snd_soc_jack *jack = &drvdata->jack;

	if ((drvdata->jack_bp_sel == JACK_BP_MICBIAS) &&
			(drvdata->jack_int_sel == JACK_INT2)) {
		pr_debug("%s: INT1 not used. Returning...\n", __func__);
		return jack->status;
	}

	mutex_lock(&drvdata->jack_mlock);
	pr_debug("%s: Enter (jack->status = %d).\n", __func__, jack->status);

	if (cancel_delayed_work_sync(&drvdata->bp_recheck))
		pr_debug("%s: bp-recheck interrupted!\n", __func__);
	if (drvdata->jack_bp_sel == JACK_BP_MICBIAS)
		if (cancel_delayed_work_sync(&drvdata->jack_recheck))
			pr_debug("%s: jack-recheck interrupted!\n", __func__);

	/* Check for button-events if a headset is present (codec-mode only) */
	if (drvdata->jack_bp_sel == JACK_BP_CODEC) {
		if (jack->status & SND_JACK_MICROPHONE)
			if (byt_bp_check(drvdata, false))
				snd_soc_jack_report(jack, jack->status,
						SND_JACK_BTN_0);
	} else /* In micbias-mode we use int1 for jack-check */
		byt_jack_check(drvdata, false, false);

	mutex_unlock(&drvdata->jack_mlock);
	return jack->status;
}

static void byt_export_gpio(struct gpio_desc *desc, char *name)
{
	int ret = gpiod_export(desc, true);
	if (ret)
		pr_debug("%s: Unable to export GPIO%d (%s)! Returned %d.\n",
				__func__, desc_to_gpio(desc), name, ret);
}

static void byt_init_gpios(struct snd_soc_codec *codec,
		struct byt_drvdata *drvdata)
{
	int ret;
	struct gpio_desc *desc;
	desc = devm_gpiod_get_index(codec->dev, NULL, RT5640_GPIO_JD_INT, GPIOD_IN);
	if (!IS_ERR(desc)) {
		drvdata->gpios.jd_int_gpio = desc_to_gpio(desc);
		byt_export_gpio(desc, "JD-int");

		pr_info("%s: GPIOs - JD-int: %d (pol = %d, val = %d)\n",
				__func__, drvdata->gpios.jd_int_gpio,
				gpiod_is_active_low(desc), gpiod_get_value(desc));

		devm_gpiod_put(codec->dev, desc);
	} else {
		drvdata->gpios.jd_int_gpio = RT5640_GPIO_NA;
		pr_err("%s: GPIOs - JD-int: Not present!\n", __func__);
	}

	/* FIXME: Define which GPIOS should be enabled */
	if (byt_rt5640_quirk & BYT_RT5640_JACK_BP_EN) {
		desc = devm_gpiod_get_index(codec->dev, NULL, RT5640_GPIO_JD_INT2, GPIOD_ASIS);
		if (!IS_ERR(desc)) {
			drvdata->gpios.jd_int2_gpio = desc_to_gpio(desc);
			byt_export_gpio(desc, "JD-int2");

			pr_info("%s: GPIOs - JD-int2: %d (pol = %d, val = %d)\n",
					__func__, drvdata->gpios.jd_int2_gpio,
					gpiod_is_active_low(desc), gpiod_get_value(desc));

			devm_gpiod_put(codec->dev, desc);
		} else {
			drvdata->gpios.jd_int2_gpio = RT5640_GPIO_NA;
			pr_warn("%s: GPIOs - JD-int2: Not present!\n", __func__);
		}

		desc = devm_gpiod_get_index(codec->dev, NULL, RT5640_GPIO_JD_BUTTONS, GPIOD_ASIS);
		if (!IS_ERR(desc)) {
			drvdata->gpios.jd_buttons_gpio = desc_to_gpio(desc);
			byt_export_gpio(desc, "JD-buttons");

			pr_info("%s: GPIOs - JD-buttons: %d (pol = %d, val = %d)\n",
					__func__, drvdata->gpios.jd_buttons_gpio,
					gpiod_is_active_low(desc), gpiod_get_value(desc));

			devm_gpiod_put(codec->dev, desc);
		} else {
			drvdata->gpios.jd_buttons_gpio = RT5640_GPIO_NA;
			pr_warn("%s: GPIOs - JD-buttons: Not present!\n", __func__);
		}

		desc = devm_gpiod_get_index(codec->dev, NULL, RT5640_GPIO_I2S_TRISTATE, GPIOD_ASIS);
		if (!IS_ERR(desc)) {
			drvdata->gpios.i2s_tristate_en_gpio = desc_to_gpio(desc);

			ret = gpiod_direction_output(desc, 0);
			if (ret)
				pr_warn("%s: Failed to set direction for GPIO%d (err = %d)!\n",
						__func__, drvdata->gpios.i2s_tristate_en_gpio,
						ret);

			byt_export_gpio(desc, "I2S-Tristate-En");

			pr_info("%s: GPIOs - I2S-Tristate-En: %d (pol = %d, val = %d)\n",
					__func__, drvdata->gpios.i2s_tristate_en_gpio,
					gpiod_is_active_low(desc), gpiod_get_value(desc));

			devm_gpiod_put(codec->dev, desc);
		} else {
			drvdata->gpios.i2s_tristate_en_gpio = RT5640_GPIO_NA;
			pr_warn("%s: GPIOs - i2s_tristate_en-mux: Not present!\n",
					__func__);
		}
	}
}

/* ASoC Jack-definitions */
static struct snd_soc_jack_gpio jack_gpio_int1[] = {
	{
		.name                   = "byt-int1-int",
		.report                 = 0, /* Set dynamically */
		.debounce_time          = BYT_JD_INTR_DEBOUNCE,
		.jack_status_check      = byt_jack_int1_interrupt,
	},
};

static struct snd_soc_jack_gpio jack_gpio_int2[] = {
	{/*jack detection*/
		.name                   = "byt-int2-int",
		.report                 = SND_JACK_HEADSET,
		.debounce_time          = BYT_JD_INTR_DEBOUNCE,
		.jack_status_check      = byt_jack_int2_interrupt,
	},
};

static struct snd_soc_jack_gpio jack_gpio_micbias[] = {
	{
		.name                   = "byt-micbias-int",
		.report                 = SND_JACK_BTN_0,
		.debounce_time          = BYT_JD_INTR_DEBOUNCE,
		.jack_status_check      = byt_micbias_interrupt,
	},
};

static int byt_config_jack_gpios(struct byt_drvdata *drvdata)
{
	int int_sel = drvdata->jack_int_sel;
	int bp_sel = drvdata->jack_bp_sel;
	int int1_gpio = drvdata->gpios.jd_int_gpio;
	int int2_gpio = drvdata->gpios.jd_int2_gpio;
	int bp_gpio = drvdata->gpios.jd_buttons_gpio;

	pr_info("%s: Jack-GPIO config: jack_bp_sel = %d, jack_int_sel = %d\n",
		__func__, bp_sel, int_sel);

	if ((bp_sel == JACK_BP_CODEC) && (int_sel == JACK_INT1)) {
		pr_err("%s: Unsupported jack-config (jack_bp_sel = %d, jack_int_sel = %d)!\n",
			__func__, JACK_BP_CODEC, JACK_INT1);
		return -EINVAL;
	}

	if ((bp_sel == JACK_BP_MICBIAS) && (bp_gpio == RT5640_GPIO_NA)) {
		pr_err("%s: Micbias-GPIO missing (jack_bp_sel = %d, jack_int_sel = %d)!\n",
			__func__, JACK_BP_MICBIAS, int_sel);
		return -EINVAL;
	}

	if (((int_sel == JACK_INT1) && (int1_gpio == RT5640_GPIO_NA)) ||
		((int_sel == JACK_INT2) && (int2_gpio == RT5640_GPIO_NA))) {
		pr_err("%s: Missing jack-GPIO (jack_bp_sel = %d, jack_int_sel = %d, )\n",
			__func__, bp_sel, int_sel);
		return -EINVAL;
	}


	if ((int_sel == JACK_INT1) ||
		(bp_sel == JACK_BP_CODEC && (byt_rt5640_quirk & BYT_RT5640_JACK_BP_EN))) {

		jack_gpio_int1[0].gpio = int1_gpio;
		jack_gpio_int1[0].data = drvdata;
		jack_gpio_int1[0].report = (int_sel == JACK_INT1) ?
				SND_JACK_HEADSET : SND_JACK_BTN_0;
		pr_err("%s: Add jack-GPIO 1 (%d).\n", __func__, int1_gpio);
		if (snd_soc_jack_add_gpios(&drvdata->jack, 1,
					&jack_gpio_int1[0])) {
			pr_err("%s: Failed to add jack-GPIO 1!\n", __func__);
			return -EIO;
		}
	}
	if (int_sel == JACK_INT2) {
		jack_gpio_int2[0].gpio = int2_gpio;
		jack_gpio_int2[0].data = drvdata;
		pr_err("%s: Add jack-GPIO 2 (%d).\n", __func__, int2_gpio);
		if (snd_soc_jack_add_gpios(&drvdata->jack, 1,
					&jack_gpio_int2[0])) {
			pr_err("%s: Failed to add jack-GPIO 2!\n", __func__);
			return -EIO;
		}
	}

	if ((byt_rt5640_quirk & BYT_RT5640_JACK_BP_EN) && bp_sel == JACK_BP_MICBIAS) {
		jack_gpio_micbias[0].gpio = bp_gpio;
		jack_gpio_micbias[0].data = drvdata;
		pr_debug("%s: Add micbias-GPIO (%d).\n", __func__, bp_gpio);
		if (snd_soc_jack_add_gpios(&drvdata->jack, 1,
					&jack_gpio_micbias[0])) {
			pr_err("%s: Failed to add micbias-GPIO!\n", __func__);
			return -EIO;
		}
	}

	return 0;
}

static int byt_rt5640_aif1_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5640_SCLK_S_PLL1,
				     params_rate(params) * 512,
				     SND_SOC_CLOCK_IN);

	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec clock %d\n", ret);
		return ret;
	}

	if (!(byt_rt5640_quirk & BYT_RT5640_MCLK_EN)) {
		/* use bitclock as PLL input */
		ret = snd_soc_dai_set_pll(codec_dai, 0, RT5640_PLL1_S_BCLK1,
					params_rate(params) * 50,
					params_rate(params) * 512);
	} else {
		if (byt_rt5640_quirk & BYT_RT5640_MCLK_25MHZ) {
			ret = snd_soc_dai_set_pll(codec_dai, 0,
						RT5640_PLL1_S_MCLK,
						25000000,
						params_rate(params) * 512);
		} else {
			ret = snd_soc_dai_set_pll(codec_dai, 0,
				RT5640_PLL1_S_MCLK,
				19200000,
				params_rate(params) * 512);
		}
	}

	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec pll: %d\n", ret);
		return ret;
	}

	return 0;
}

static void byt_rt5640_configure_jd2(struct snd_soc_codec *codec)
{ /* only for devices using JD2 pin like T100 */
	/* FIXME it's ugly, is there a better way? */
	snd_soc_write(codec, RT5640_JD_CTRL, 0x6003);
	snd_soc_write(codec, RT5640_IN3_IN4, 0x0000);
	snd_soc_write(codec, RT5640_IN1_IN2, 0x51c0);

	/* selecting pin as an interrupt */
	snd_soc_write(codec, RT5640_GPIO_CTRL1, 0x8400);
	/* set GPIO1 output */
	snd_soc_write(codec, RT5640_GPIO_CTRL3, 0x0004);

	/* enabling jd2 in general control 1 */
	snd_soc_write(codec, RT5640_DUMMY1, 0x3f41);

	/* enabling jd2 in general control 2 */
	snd_soc_write(codec, RT5640_DUMMY2, 0x4001);

	snd_soc_write(codec, RT5640_REC_L2_MIXER, 0x007f);

	/* FIXME end */
}

static int byt_rt5640_init(struct snd_soc_pcm_runtime *runtime)
{
	int ret;
	struct snd_soc_codec *codec = runtime->codec;
	struct snd_soc_card *card = runtime->card;
	struct byt_drvdata *drvdata;
	const struct snd_soc_dapm_route *custom_map;
	int num_routes;

	drvdata = snd_soc_card_get_drvdata(card);
	drvdata->codec = codec; /* needed for jack detection */

	card->dapm.idle_bias_off = true;

	rt5640_sel_asrc_clk_src(codec,
				RT5640_DA_STEREO_FILTER |
				RT5640_DA_MONO_L_FILTER	|
				RT5640_DA_MONO_R_FILTER	|
				RT5640_AD_STEREO_FILTER	|
				RT5640_AD_MONO_L_FILTER	|
				RT5640_AD_MONO_R_FILTER,
				RT5640_CLK_SEL_ASRC);

	if (byt_rt5640_quirk & BYT_RT5640_JACK_DET_EN) {
		/* Init GPIOs */
		byt_init_gpios(codec, drvdata);

		/* Add ASoC-jack */
		ret = snd_soc_card_jack_new(card, "BYT-CR Audio Jack",
					SND_JACK_HEADSET | SND_JACK_BTN_0,
					&drvdata->jack, NULL, 0);
		if (ret) {
			pr_err("%s: snd_soc_card_jack_new failed (ret = %d)!\n",
				__func__, ret);
			return ret;
		}

		mutex_init(&drvdata->jack_mlock);

		/* Handle jack/bp GPIOs */
		ret = byt_config_jack_gpios(drvdata);
		if (ret) {
			pr_err("%s: byt_config_jack_gpios failed (ret = %d)!\n",
				__func__, ret);
			return ret;
		}
		/* Other jack/bp stuff */

		/* for devices using JD2 pin should be configured some registers */
		if (byt_rt5640_quirk & BYT_RT5640_JACK_DET_PIN2) {
			byt_rt5640_configure_jd2(codec);
		} else {
			/* JACK_DET_N signal as JD-source */
			snd_soc_update_bits(codec, RT5640_JD_CTRL,
					RT5640_JD_MASK, RT5640_JD_JD2_IN4N);
		}

		/* Prevent sta_jd_internal to trigger IRQ in CODEC-mode */
		if (drvdata->jack_bp_sel == JACK_BP_CODEC)
			snd_soc_write(codec, RT5640_IRQ_CTRL1, 0x0000);
		else
			/*TODO: 0x8800 is jack active but polarity inverted, why do I need to invert it?*/
			snd_soc_write(codec, RT5640_IRQ_CTRL1, 0x8800);

		/* Set OVCD-threshold */
		rt5640_config_ovcd_thld(codec, RT5640_MIC1_OVTH_1500UA,
					RT5640_MIC_OVCD_SF_1P0);

		drvdata->t_jack_recheck = msecs_to_jiffies(BYT_T_JACK_RECHECK);
		INIT_DELAYED_WORK(&drvdata->jack_recheck, byt_jack_recheck);
		drvdata->t_buttons_recheck = msecs_to_jiffies(BYT_T_BUTTONS_RECHECK);
		INIT_DELAYED_WORK(&drvdata->bp_recheck, byt_bp_recheck);
		drvdata->jack_hp_count = 5;

		/* FIXME? */
		snd_jack_set_key(drvdata->jack.jack, SND_JACK_BTN_0, KEY_MEDIA);
	}

	ret = snd_soc_add_card_controls(card, byt_rt5640_controls,
					ARRAY_SIZE(byt_rt5640_controls));
	if (ret) {
		dev_err(card->dev, "unable to add card controls\n");
		return ret;
	}

	switch (BYT_RT5640_MAP(byt_rt5640_quirk)) {
	case BYT_RT5640_IN1_MAP:
		custom_map = byt_rt5640_intmic_in1_map;
		num_routes = ARRAY_SIZE(byt_rt5640_intmic_in1_map);
		break;
	case BYT_RT5640_IN3_MAP:
		custom_map = byt_rt5640_intmic_in3_map;
		num_routes = ARRAY_SIZE(byt_rt5640_intmic_in3_map);
		break;
	case BYT_RT5640_DMIC2_MAP:
		custom_map = byt_rt5640_intmic_dmic2_map;
		num_routes = ARRAY_SIZE(byt_rt5640_intmic_dmic2_map);
		break;
	default:
		custom_map = byt_rt5640_intmic_dmic1_map;
		num_routes = ARRAY_SIZE(byt_rt5640_intmic_dmic1_map);
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, custom_map, num_routes);
	if (ret)
		return ret;

	if (byt_rt5640_quirk & BYT_RT5640_DMIC_EN) {
		ret = rt5640_dmic_enable(codec, 0, 0);
		if (ret)
			return ret;
	}

	ret = snd_soc_dapm_sync(&card->dapm);
	if (ret) {
		pr_err("%s: snd_soc_dapm_sync failed!\n", __func__);
		return ret;
	}


	snd_soc_dapm_ignore_suspend(&card->dapm, "Headphone");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Speaker");

	if (byt_rt5640_quirk & BYT_RT5640_MCLK_EN) {
		ret = vlv2_plat_configure_clock(VLV2_PLT_CLK_AUDIO,
						VLV2_PLT_CLK_CONFG_FORCE_OFF);
		if (ret) {
			dev_err(card->dev, "could not configure MCLK state");
			return ret;
		}

		if (byt_rt5640_quirk & BYT_RT5640_MCLK_25MHZ) {
			ret = vlv2_plat_set_clock_freq(VLV2_PLT_CLK_AUDIO,
						VLV2_PLT_CLK_FREQ_TYPE_XTAL);
		} else {
			ret = vlv2_plat_set_clock_freq(VLV2_PLT_CLK_AUDIO,
						VLV2_PLT_CLK_FREQ_TYPE_PLL);
		}

		if (ret)
			dev_err(card->dev, "unable to set MCLK rate \n");
	}

	return ret;
}

static const struct snd_soc_pcm_stream byt_rt5640_dai_params = {
	.formats = SNDRV_PCM_FMTBIT_S24_LE,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
};

static int byt_rt5640_codec_fixup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	int ret;

	/* The DSP will covert the FE rate to 48k, stereo, 24bits */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSP2 to 24-bit */
	params_set_format(params, SNDRV_PCM_FORMAT_S24_LE);

	/*
	 * Default mode for SSP configuration is TDM 4 slot, override config
	 * with explicit setting to I2S 2ch 24-bit. The word length is set with
	 * dai_set_tdm_slot() since there is no other API exposed
	 */
	ret = snd_soc_dai_set_fmt(rtd->cpu_dai,
				  SND_SOC_DAIFMT_I2S     |
				  SND_SOC_DAIFMT_NB_IF   |
				  SND_SOC_DAIFMT_CBS_CFS
				  );
	if (ret < 0) {
		dev_err(rtd->dev, "can't set format to I2S, err %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(rtd->cpu_dai, 0x3, 0x3, 2, 24);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set I2S config, err %d\n", ret);
		return ret;
	}

	return 0;
}

static unsigned int rates_48000[] = {
	48000,
};

static struct snd_pcm_hw_constraint_list constraints_48000 = {
	.count = ARRAY_SIZE(rates_48000),
	.list  = rates_48000,
};

static int byt_rt5640_aif1_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE,
			&constraints_48000);
}

static struct snd_soc_ops byt_rt5640_aif1_ops = {
	.startup = byt_rt5640_aif1_startup,
};

static struct snd_soc_ops byt_rt5640_be_ssp2_ops = {
	.hw_params = byt_rt5640_aif1_hw_params,
};

static struct snd_soc_dai_link byt_rt5640_dais[] = {
	[MERR_DPCM_AUDIO] = {
		.name = "Baytrail Audio Port",
		.stream_name = "Baytrail Audio",
		.cpu_dai_name = "media-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.platform_name = "sst-mfld-platform",
		.ignore_suspend = 1,
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &byt_rt5640_aif1_ops,
	},
	[MERR_DPCM_DEEP_BUFFER] = {
		.name = "Deep-Buffer Audio Port",
		.stream_name = "Deep-Buffer Audio",
		.cpu_dai_name = "deepbuffer-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.platform_name = "sst-mfld-platform",
		.ignore_suspend = 1,
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.ops = &byt_rt5640_aif1_ops,
	},
	[MERR_DPCM_COMPR] = {
		.name = "Baytrail Compressed Port",
		.stream_name = "Baytrail Compress",
		.cpu_dai_name = "compress-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.platform_name = "sst-mfld-platform",
	},
		/* back ends */
	{
		.name = "SSP2-Codec",
		.be_id = 1,
		.cpu_dai_name = "ssp2-port",
		.platform_name = "sst-mfld-platform",
		.no_pcm = 1,
		.codec_dai_name = "rt5640-aif1",
		.codec_name = "i2c-10EC5640:00", /* overwritten with HID */
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS,
		.be_hw_params_fixup = byt_rt5640_codec_fixup,
		.ignore_suspend = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.init = byt_rt5640_init,
		.ops = &byt_rt5640_be_ssp2_ops,
	},
};

/* SoC card */
static struct snd_soc_card byt_rt5640_card = {
	.name = "bytcr-rt5640",
	.owner = THIS_MODULE,
	.dai_link = byt_rt5640_dais,
	.num_links = ARRAY_SIZE(byt_rt5640_dais),
	.dapm_widgets = byt_rt5640_widgets,
	.num_dapm_widgets = ARRAY_SIZE(byt_rt5640_widgets),
	.dapm_routes = byt_rt5640_audio_map,
	.num_dapm_routes = ARRAY_SIZE(byt_rt5640_audio_map),
	.fully_routed = true,
};

static char byt_rt5640_codec_name[16]; /* i2c-<HID>:00 with HID being 8 chars */

static int snd_byt_rt5640_mc_probe(struct platform_device *pdev)
{
	int ret_val = 0;
	struct sst_acpi_mach *mach;
	struct platform_device *pdev_mclk;
	struct byt_drvdata *drvdata;
	struct snd_soc_card *card;

	card = &byt_rt5640_card;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_ATOMIC);
	if (!drvdata) {
		pr_err("Allocation failed!\n");
		return -ENOMEM;
	}

	pdev_mclk = platform_device_register_simple("vlv2_plat_clk", -1,
						NULL, 0);
	if (IS_ERR(pdev_mclk)) {
		dev_err(&pdev->dev,
			"platform_vlv2_plat_clk:register failed: %ld\n",
			PTR_ERR(pdev_mclk));
		return PTR_ERR(pdev_mclk);
	}

	/* register the soc card */
	card->dev = &pdev->dev;
	mach = byt_rt5640_card.dev->platform_data;

	/* fixup codec name based on HID */
	snprintf(byt_rt5640_codec_name, sizeof(byt_rt5640_codec_name),
		 "%s%s%s", "i2c-", mach->id, ":00");
	byt_rt5640_dais[MERR_DPCM_COMPR+1].codec_name = byt_rt5640_codec_name;

	/* Get board-specific HW-settings, fill up drvdata */
	dmi_check_system(byt_rt5640_quirk_table);

	if (byt_rt5640_quirk & BYT_RT5640_JACK_DET_EN) {
		if (byt_rt5640_quirk & BYT_RT5640_JACK_ACTIVE_LOW)
			drvdata->jack_active_low = 1;
		else
			drvdata->jack_active_low = 0;

		if (byt_rt5640_quirk & BYT_RT5640_JACK_INT1)
			drvdata->jack_int_sel = JACK_INT1;
		else
			drvdata->jack_int_sel = JACK_INT2;

		if (byt_rt5640_quirk & BYT_RT5640_JACK_BP_CODEC)
			drvdata->jack_bp_sel = JACK_BP_CODEC;
		else
			drvdata->jack_bp_sel = JACK_BP_MICBIAS;
	}


	snd_soc_card_set_drvdata(card, drvdata);
	ret_val = devm_snd_soc_register_card(card->dev, card);

	if (ret_val) {
		dev_err(card->dev, "devm_snd_soc_register_card failed %d\n",
			ret_val);
		return ret_val;
	}
	platform_set_drvdata(pdev, card);
	return ret_val;
}

static void snd_byt_unregister_jack(struct byt_drvdata *drvdata)
{
	cancel_delayed_work_sync(&drvdata->jack_recheck);
	snd_soc_jack_free_gpios(&drvdata->jack, drvdata->gpios.int_count,
				jack_gpio_int1);

}
static int snd_byt_rt5640_mc_remove(struct platform_device *pdev)
{
	struct snd_soc_card *soc_card = platform_get_drvdata(pdev);
	struct byt_drvdata *drv = snd_soc_card_get_drvdata(soc_card);

	pr_debug("%s: Enter.\n", __func__);

	if (byt_rt5640_quirk & BYT_RT5640_JACK_DET_EN) {
		snd_byt_unregister_jack(drv);
	}
	snd_soc_card_set_drvdata(soc_card, NULL);
	snd_soc_unregister_card(soc_card);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void snd_byt_rt5640_mc_shutdown(struct platform_device *pdev)
{
	struct snd_soc_card *soc_card = platform_get_drvdata(pdev);
	struct byt_drvdata *drv = snd_soc_card_get_drvdata(soc_card);

	pr_debug("In %s\n", __func__);

	if (byt_rt5640_quirk & BYT_RT5640_JACK_DET_EN) {
		snd_byt_unregister_jack(drv);
	}
}

static struct platform_driver snd_byt_rt5640_mc_driver = {
	.driver = {
		.name = "bytcr_rt5640",
		.pm = &snd_soc_pm_ops,
	},
	.probe = snd_byt_rt5640_mc_probe,
	.remove = snd_byt_rt5640_mc_remove,
	.shutdown = snd_byt_rt5640_mc_shutdown,
};

module_platform_driver(snd_byt_rt5640_mc_driver);

MODULE_DESCRIPTION("ASoC Intel(R) Baytrail CR Machine driver");
MODULE_AUTHOR("Subhransu S. Prusty <subhransu.s.prusty@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bytcr_rt5640");
