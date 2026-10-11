// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek MT8127 ALSA SoC machine driver for boards using the MT6323 codec
 *
 * The headphone outputs of the codec also feed an optional external speaker
 * amplifier, which is switched by a GPIO. A GPIO that follows the jack switch
 * mutes the speaker while headphones are plugged in.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <sound/jack.h>
#include <sound/soc.h>

/* The amplifier needs this long after enable before it passes audio. */
#define MT8127_MT6323_SPK_WARMUP_MS	10

struct mt8127_mt6323_priv {
	struct gpio_desc *spk_en;
	struct snd_soc_jack jack;
};

static struct snd_soc_jack_pin mt8127_mt6323_jack_pins[] = {
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE,
	},
	{
		.pin = "Speaker",
		.mask = SND_JACK_HEADPHONE,
		.invert = 1,
	},
};

static struct snd_soc_jack_gpio mt8127_mt6323_jack_gpio = {
	.name = "hp-det",
	.report = SND_JACK_HEADPHONE,
	.debounce_time = 150,
};

static int mt8127_mt6323_spk_event(struct snd_soc_dapm_widget *w,
				   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_card *card = snd_soc_dapm_to_card(w->dapm);
	struct mt8127_mt6323_priv *priv = snd_soc_card_get_drvdata(card);

	if (!priv->spk_en)
		return 0;

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		gpiod_set_value_cansleep(priv->spk_en, 1);
		msleep(MT8127_MT6323_SPK_WARMUP_MS);
	} else {
		gpiod_set_value_cansleep(priv->spk_en, 0);
	}

	return 0;
}

static const struct snd_soc_dapm_widget mt8127_mt6323_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", mt8127_mt6323_spk_event),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route mt8127_mt6323_routes[] = {
	{ "Speaker", NULL, "HPL" },
	{ "Speaker", NULL, "HPR" },
	{ "Headphone", NULL, "HPL" },
	{ "Headphone", NULL, "HPR" },
	{ "Int Mic", NULL, "MICBIAS" },
	{ "MIC1", NULL, "Int Mic" },
	{ "Headset Mic", NULL, "MICBIAS" },
	{ "MIC2", NULL, "Headset Mic" },
};

static const struct snd_kcontrol_new mt8127_mt6323_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static int mt8127_mt6323_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct mt8127_mt6323_priv *priv = snd_soc_card_get_drvdata(card);
	int ret;

	if (!device_property_present(card->dev, "hp-det-gpios"))
		return 0;

	ret = snd_soc_card_jack_new_pins(card, "Headphone Jack",
					 SND_JACK_HEADPHONE, &priv->jack,
					 mt8127_mt6323_jack_pins,
					 ARRAY_SIZE(mt8127_mt6323_jack_pins));
	if (ret)
		return ret;

	mt8127_mt6323_jack_gpio.gpiod_dev = card->dev;

	return snd_soc_jack_add_gpios(&priv->jack, 1,
				      &mt8127_mt6323_jack_gpio);
}

SND_SOC_DAILINK_DEFS(playback,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture,
		     DAILINK_COMP_ARRAY(COMP_CPU("VUL")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(fm_capture,
		     DAILINK_COMP_ARRAY(COMP_CPU("AWB")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(codec,
		     DAILINK_COMP_ARRAY(COMP_CPU("ADDA")),
		     DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "mt6323-aif1")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(fm_i2s,
		     DAILINK_COMP_ARRAY(COMP_CPU("I2S")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link mt8127_mt6323_dais[] = {
	/* Front-end DAI links */
	{
		.name = "MT6323 Playback",
		.stream_name = "MT6323 Playback",
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
			     SND_SOC_DPCM_TRIGGER_POST },
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(playback),
	},
	{
		.name = "MT6323 Capture",
		.stream_name = "MT6323 Capture",
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
			     SND_SOC_DPCM_TRIGGER_POST },
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(capture),
	},
	{
		.name = "FM Capture",
		.stream_name = "FM Capture",
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
			     SND_SOC_DPCM_TRIGGER_POST },
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(fm_capture),
	},
	/* Back-end DAI links */
	{
		.name = "Codec",
		.no_pcm = 1,
		.init = mt8127_mt6323_codec_init,
		SND_SOC_DAILINK_REG(codec),
	},
	{
		/* I2S input from the FM receiver of the CONSYS */
		.name = "FM I2S",
		.no_pcm = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(fm_i2s),
	},
};

static struct snd_soc_card mt8127_mt6323_card = {
	.name = "mt8127-mt6323",
	.owner = THIS_MODULE,
	.dai_link = mt8127_mt6323_dais,
	.num_links = ARRAY_SIZE(mt8127_mt6323_dais),
	.controls = mt8127_mt6323_controls,
	.num_controls = ARRAY_SIZE(mt8127_mt6323_controls),
	.dapm_widgets = mt8127_mt6323_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8127_mt6323_widgets),
	.dapm_routes = mt8127_mt6323_routes,
	.num_dapm_routes = ARRAY_SIZE(mt8127_mt6323_routes),
};

static int mt8127_mt6323_dev_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &mt8127_mt6323_card;
	struct device *dev = &pdev->dev;
	struct device_node *platform_node, *codec_node;
	struct mt8127_mt6323_priv *priv;
	struct snd_soc_dai_link *dai_link;
	int ret, i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->spk_en = devm_gpiod_get_optional(dev, "spk-en", GPIOD_OUT_LOW);
	if (IS_ERR(priv->spk_en))
		return dev_err_probe(dev, PTR_ERR(priv->spk_en),
				     "failed to get the speaker enable GPIO\n");

	platform_node = of_parse_phandle(dev->of_node, "mediatek,platform", 0);
	if (!platform_node)
		return dev_err_probe(dev, -EINVAL,
				     "property 'mediatek,platform' missing\n");

	codec_node = of_parse_phandle(dev->of_node, "mediatek,audio-codec", 0);
	if (!codec_node) {
		ret = dev_err_probe(dev, -EINVAL,
				    "property 'mediatek,audio-codec' missing\n");
		goto put_platform_node;
	}

	for_each_card_prelinks(card, i, dai_link) {
		if (!dai_link->platforms->name)
			dai_link->platforms->of_node = platform_node;
		if (dai_link->no_pcm && dai_link->num_codecs)
			dai_link->codecs->of_node = codec_node;
	}

	card->dev = dev;
	snd_soc_card_set_drvdata(card, priv);

	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret)
		goto put_codec_node;

	ret = devm_snd_soc_register_card(dev, card);
	if (ret)
		dev_err_probe(dev, ret, "failed to register the card\n");

put_codec_node:
	of_node_put(codec_node);
put_platform_node:
	of_node_put(platform_node);
	return ret;
}

static const struct of_device_id mt8127_mt6323_dt_match[] = {
	{ .compatible = "mediatek,mt8127-mt6323" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt8127_mt6323_dt_match);

static struct platform_driver mt8127_mt6323_driver = {
	.driver = {
		.name = "mt8127-mt6323",
		.of_match_table = mt8127_mt6323_dt_match,
		.pm = &snd_soc_pm_ops,
	},
	.probe = mt8127_mt6323_dev_probe,
};
module_platform_driver(mt8127_mt6323_driver);

MODULE_DESCRIPTION("MediaTek MT8127 MT6323 ALSA SoC machine driver");
MODULE_AUTHOR("Marc Lainez <marc.lainez@gmail.com>");
MODULE_LICENSE("GPL");
