// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek MT8127 audio front-end (AFE) platform driver
 *
 * The AFE moves PCM data between memory and the internal ADDA interface,
 * which carries the samples to the codec in the MT6323 PMIC over the
 * AUD_CLK/AUD_DAT pins. Its registers belong to the audsys block, a syscon
 * that also provides the audio clock gates; the AFE is a child of it.
 *
 * Based on the MT8173 AFE driver, whose register layout the MT8127 shares.
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include "../common/mtk-afe-fe-dai.h"
#include "../common/mtk-afe-platform-driver.h"
#include "../common/mtk-base-afe.h"

/* Registers */
#define AUDIO_TOP_CON0		0x0000
#define AFE_DAC_CON0		0x0010
#define AFE_DAC_CON1		0x0014
#define AFE_CONN1		0x0024
#define AFE_CONN2		0x0028
#define AFE_CONN3		0x002c
#define AFE_I2S_CON1		0x0034
#define AFE_DL1_BASE		0x0040
#define AFE_DL1_CUR		0x0044
#define AFE_DL1_END		0x0048
#define AFE_CONN_24BIT		0x006c
#define AFE_VUL_BASE		0x0080
#define AFE_VUL_END		0x0088
#define AFE_VUL_CUR		0x008c
#define AFE_ADDA_DL_SRC2_CON0	0x0108
#define AFE_ADDA_DL_SRC2_CON1	0x010c
#define AFE_ADDA_UL_SRC_CON0	0x0114
#define AFE_ADDA_UL_SRC_CON1	0x0118
#define AFE_ADDA_TOP_CON0	0x0120
#define AFE_ADDA_UL_DL_CON0	0x0124
#define AFE_ADDA_NEWIF_CFG0	0x0138
#define AFE_ADDA_NEWIF_CFG1	0x013c
#define AFE_ADDA_PREDIS_CON0	0x0260
#define AFE_ADDA_PREDIS_CON1	0x0264
#define AFE_IRQ_MCU_CON		0x03a0
#define AFE_IRQ_STATUS		0x03a4
#define AFE_IRQ_CLR		0x03a8
#define AFE_IRQ_CNT1		0x03ac
#define AFE_IRQ_CNT2		0x03b0
#define AFE_IRQ_MCU_EN		0x03b4

/* AUDIO_TOP_CON0 */
#define AUD_TCON0_PDN_DAC_PREDIS	BIT(26)
#define AUD_TCON0_PDN_DAC		BIT(25)
#define AUD_TCON0_PDN_ADC		BIT(24)
#define AUD_TCON0_APB_R2T		BIT(30)
#define AUD_TCON0_APB_W2T		BIT(29)
#define AUD_TCON0_APB_BUS		BIT(14)

/* AFE_I2S_CON1: the I2S output that feeds the DL path of the ADDA */
#define AFE_I2S_CON1_RATE(x)		(((x) & 0xf) << 8)
#define AFE_I2S_CON1_FORMAT_I2S		BIT(3)
#define AFE_I2S_CON1_EN			BIT(0)

/* AFE_ADDA_DL_SRC2_CON0 */
#define DL_SRC2_CON0_RATE(x)		(((x) & 0xf) << 28)
#define DL_SRC2_CON0_OUT_MASK		(0x3 << 24)
#define DL_SRC2_CON0_MUTE_OFF		(0x3 << 11)
#define DL_SRC2_CON0_VOICE_MODE		BIT(5)
#define DL_SRC2_CON0_GAIN_ON		BIT(1)
#define DL_SRC2_CON0_EN			BIT(0)
/* -0.3 dB, the stock setting */
#define DL_SRC2_CON1_GAIN		0xf74f0000

/* AFE_ADDA_UL_SRC_CON0 */
#define UL_SRC_CON0_MODE_MASK		GENMASK(20, 17)
#define UL_SRC_CON0_MODE(x)		(((x) & 0x3) * (BIT(19) | BIT(17)))
#define UL_SRC_CON0_EN			BIT(0)

/* AFE_ADDA_NEWIF_CFG0: up8x txif, saturation on */
#define ADDA_NEWIF_CFG0_UL		0x03f87201
/* AFE_ADDA_NEWIF_CFG1 */
#define ADDA_NEWIF_CFG1_UL_MASK		GENMASK(11, 10)
#define ADDA_NEWIF_CFG1_UL(x)		(((x) & 0x3) << 10)

#define AFE_IRQ_STATUS_BITS		0xff

enum {
	MT8127_AFE_MEMIF_DL1,
	MT8127_AFE_MEMIF_VUL,
	MT8127_AFE_MEMIF_NUM,
	MT8127_AFE_IO_ADDA = MT8127_AFE_MEMIF_NUM,
};

/* The AFE has two MCU interrupt counters for its memory interfaces. */
enum {
	MT8127_AFE_IRQ1,
	MT8127_AFE_IRQ2,
	MT8127_AFE_IRQ_NUM,
};

enum {
	MT8127_CLK_INFRASYS_AUD,
	MT8127_CLK_TOP_PDN_AUD,
	MT8127_CLK_TOP_PDN_AUD_BUS,
	MT8127_CLK_AUD_AFE,
	MT8127_CLK_NUM,
};

static const char * const mt8127_afe_clk_names[MT8127_CLK_NUM] = {
	[MT8127_CLK_INFRASYS_AUD] = "infra_sys_audio_clk",
	[MT8127_CLK_TOP_PDN_AUD] = "top_pdn_audio",
	[MT8127_CLK_TOP_PDN_AUD_BUS] = "top_pdn_aud_intbus",
	[MT8127_CLK_AUD_AFE] = "aud_afe",
};

struct mt8127_afe_private {
	struct clk_bulk_data clks[MT8127_CLK_NUM];
	bool adda_dl_on;
	bool adda_ul_on;
};

static const unsigned int mt8127_afe_backup_list[] = {
	AUDIO_TOP_CON0,
	AFE_CONN1,
	AFE_CONN2,
	AFE_CONN3,
	AFE_DAC_CON1,
	AFE_DL1_BASE,
	AFE_DL1_END,
	AFE_VUL_BASE,
	AFE_VUL_END,
	AFE_DAC_CON0,
};

static const struct snd_pcm_hardware mt8127_afe_hardware = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.buffer_bytes_max = 64 * 1024,
	.period_bytes_min = 256,
	.period_bytes_max = 32 * 1024,
	.periods_min = 2,
	.periods_max = 256,
	.fifo_size = 0,
};

struct mt8127_afe_rate {
	unsigned int rate;
	unsigned int fs;	/* memif, IRQ and I2S rate index */
	unsigned int adda;	/* ADDA DL rate index */
};

static const struct mt8127_afe_rate mt8127_afe_rates[] = {
	{ .rate = 8000, .fs = 0, .adda = 0 },
	{ .rate = 11025, .fs = 1, .adda = 1 },
	{ .rate = 12000, .fs = 2, .adda = 2 },
	{ .rate = 16000, .fs = 4, .adda = 3 },
	{ .rate = 22050, .fs = 5, .adda = 4 },
	{ .rate = 24000, .fs = 6, .adda = 5 },
	{ .rate = 32000, .fs = 8, .adda = 6 },
	{ .rate = 44100, .fs = 9, .adda = 7 },
	{ .rate = 48000, .fs = 10, .adda = 8 },
};

static const struct mt8127_afe_rate *mt8127_afe_find_rate(unsigned int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt8127_afe_rates); i++)
		if (mt8127_afe_rates[i].rate == rate)
			return &mt8127_afe_rates[i];

	return NULL;
}

static int mt8127_afe_fs(unsigned int rate)
{
	const struct mt8127_afe_rate *r = mt8127_afe_find_rate(rate);

	return r ? r->fs : -EINVAL;
}

/* Voice mode index of the ADDA uplink: 8, 16, 32 and 48 kHz only */
static int mt8127_afe_ul_mode(unsigned int rate)
{
	switch (rate) {
	case 8000:
		return 0;
	case 16000:
		return 1;
	case 32000:
		return 2;
	case 48000:
		return 3;
	default:
		return -EINVAL;
	}
}

static int mt8127_memif_fs(struct snd_pcm_substream *substream,
			   unsigned int rate)
{
	return mt8127_afe_fs(rate);
}

static int mt8127_irq_fs(struct snd_pcm_substream *substream,
			 unsigned int rate)
{
	return mt8127_afe_fs(rate);
}

static int mt8127_afe_adda_dl_start(struct mtk_base_afe *afe,
				    unsigned int rate)
{
	const struct mt8127_afe_rate *r = mt8127_afe_find_rate(rate);
	unsigned int val;

	if (!r)
		return -EINVAL;

	regmap_write(afe->regmap, AFE_ADDA_PREDIS_CON0, 0);
	regmap_write(afe->regmap, AFE_ADDA_PREDIS_CON1, 0);

	val = DL_SRC2_CON0_RATE(r->adda) | DL_SRC2_CON0_OUT_MASK |
	      DL_SRC2_CON0_MUTE_OFF | DL_SRC2_CON0_GAIN_ON;
	if (rate == 8000 || rate == 16000)
		val |= DL_SRC2_CON0_VOICE_MODE;
	regmap_write(afe->regmap, AFE_ADDA_DL_SRC2_CON0, val);
	regmap_write(afe->regmap, AFE_ADDA_DL_SRC2_CON1, DL_SRC2_CON1_GAIN);

	/* 16-bit I2S, no LR swap, no LRCK inversion */
	regmap_write(afe->regmap, AFE_I2S_CON1,
		     AFE_I2S_CON1_RATE(r->fs) | AFE_I2S_CON1_FORMAT_I2S);

	regmap_update_bits(afe->regmap, AFE_ADDA_DL_SRC2_CON0,
			   DL_SRC2_CON0_EN, DL_SRC2_CON0_EN);
	regmap_update_bits(afe->regmap, AFE_I2S_CON1,
			   AFE_I2S_CON1_EN, AFE_I2S_CON1_EN);
	regmap_update_bits(afe->regmap, AFE_ADDA_UL_DL_CON0, 0x1, 0x1);

	return 0;
}

static void mt8127_afe_adda_dl_stop(struct mtk_base_afe *afe)
{
	struct mt8127_afe_private *afe_priv = afe->platform_priv;

	regmap_update_bits(afe->regmap, AFE_ADDA_DL_SRC2_CON0,
			   DL_SRC2_CON0_EN, 0);
	regmap_update_bits(afe->regmap, AFE_I2S_CON1, AFE_I2S_CON1_EN, 0);

	if (!afe_priv->adda_ul_on)
		regmap_update_bits(afe->regmap, AFE_ADDA_UL_DL_CON0, 0x1, 0);
}

static int mt8127_afe_adda_ul_start(struct mtk_base_afe *afe,
				    unsigned int rate)
{
	int mode = mt8127_afe_ul_mode(rate);

	if (mode < 0)
		return mode;

	/* use the internal ADC */
	regmap_update_bits(afe->regmap, AFE_ADDA_TOP_CON0, 0x1, 0);

	regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0,
			   UL_SRC_CON0_MODE_MASK, UL_SRC_CON0_MODE(mode));
	regmap_write(afe->regmap, AFE_ADDA_NEWIF_CFG0, ADDA_NEWIF_CFG0_UL);
	regmap_update_bits(afe->regmap, AFE_ADDA_NEWIF_CFG1,
			   ADDA_NEWIF_CFG1_UL_MASK,
			   ADDA_NEWIF_CFG1_UL(mode < 3 ? 1 : 3));

	regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0,
			   UL_SRC_CON0_EN, UL_SRC_CON0_EN);
	regmap_update_bits(afe->regmap, AFE_ADDA_UL_DL_CON0, 0x1, 0x1);

	return 0;
}

static void mt8127_afe_adda_ul_stop(struct mtk_base_afe *afe)
{
	struct mt8127_afe_private *afe_priv = afe->platform_priv;

	regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0,
			   UL_SRC_CON0_EN, 0);

	if (!afe_priv->adda_dl_on)
		regmap_update_bits(afe->regmap, AFE_ADDA_UL_DL_CON0, 0x1, 0);
}

static int mt8127_afe_adda_startup(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		regmap_update_bits(afe->regmap, AUDIO_TOP_CON0,
				   AUD_TCON0_PDN_DAC | AUD_TCON0_PDN_DAC_PREDIS,
				   0);
	else
		regmap_update_bits(afe->regmap, AUDIO_TOP_CON0,
				   AUD_TCON0_PDN_ADC, 0);

	return 0;
}

static void mt8127_afe_adda_shutdown(struct snd_pcm_substream *substream,
				     struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8127_afe_private *afe_priv = afe->platform_priv;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (afe_priv->adda_dl_on) {
			afe_priv->adda_dl_on = false;
			mt8127_afe_adda_dl_stop(afe);
		}
		regmap_update_bits(afe->regmap, AUDIO_TOP_CON0,
				   AUD_TCON0_PDN_DAC | AUD_TCON0_PDN_DAC_PREDIS,
				   AUD_TCON0_PDN_DAC | AUD_TCON0_PDN_DAC_PREDIS);
	} else {
		if (afe_priv->adda_ul_on) {
			afe_priv->adda_ul_on = false;
			mt8127_afe_adda_ul_stop(afe);
		}
		regmap_update_bits(afe->regmap, AUDIO_TOP_CON0,
				   AUD_TCON0_PDN_ADC, AUD_TCON0_PDN_ADC);
	}
}

static int mt8127_afe_adda_prepare(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8127_afe_private *afe_priv = afe->platform_priv;
	unsigned int rate = substream->runtime->rate;
	int ret;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (afe_priv->adda_dl_on)
			mt8127_afe_adda_dl_stop(afe);
		ret = mt8127_afe_adda_dl_start(afe, rate);
		afe_priv->adda_dl_on = !ret;
	} else {
		if (afe_priv->adda_ul_on)
			mt8127_afe_adda_ul_stop(afe);
		ret = mt8127_afe_adda_ul_start(afe, rate);
		afe_priv->adda_ul_on = !ret;
	}

	return ret;
}

static const struct snd_soc_dai_ops mt8127_afe_adda_ops = {
	.startup	= mt8127_afe_adda_startup,
	.shutdown	= mt8127_afe_adda_shutdown,
	.prepare	= mt8127_afe_adda_prepare,
};

#define MT8127_AFE_DL_RATES	SNDRV_PCM_RATE_8000_48000
#define MT8127_AFE_UL_RATES	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | \
				 SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000)

static struct snd_soc_dai_driver mt8127_afe_pcm_dais[] = {
	/* FE DAIs: memory interfaces to the CPU */
	{
		.name = "DL1",
		.id = MT8127_AFE_MEMIF_DL1,
		.playback = {
			.stream_name = "DL1",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MT8127_AFE_DL_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &mtk_afe_fe_ops,
	}, {
		.name = "VUL",
		.id = MT8127_AFE_MEMIF_VUL,
		.capture = {
			.stream_name = "VUL",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MT8127_AFE_UL_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &mtk_afe_fe_ops,
	}, {
	/* BE DAI: the ADDA interface to the PMIC codec */
		.name = "ADDA",
		.id = MT8127_AFE_IO_ADDA,
		.playback = {
			.stream_name = "ADDA Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MT8127_AFE_DL_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.stream_name = "ADDA Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MT8127_AFE_UL_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &mt8127_afe_adda_ops,
	},
};

/*
 * Interconnections: I05/I06 are the DL1 left/right samples, O03/O04 feed the
 * ADDA downlink, I03/I04 are the ADDA uplink left/right samples and O09/O10
 * feed VUL.
 */
static const struct snd_kcontrol_new mt8127_afe_o03_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I05 Switch", AFE_CONN1, 21, 1, 0),
};

static const struct snd_kcontrol_new mt8127_afe_o04_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I06 Switch", AFE_CONN2, 6, 1, 0),
};

static const struct snd_kcontrol_new mt8127_afe_o09_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I03 Switch", AFE_CONN3, 0, 1, 0),
};

static const struct snd_kcontrol_new mt8127_afe_o10_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I04 Switch", AFE_CONN3, 3, 1, 0),
};

static const struct snd_soc_dapm_widget mt8127_afe_pcm_widgets[] = {
	SND_SOC_DAPM_MIXER("I03", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I04", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I05", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I06", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("O03", SND_SOC_NOPM, 0, 0,
			   mt8127_afe_o03_mix, ARRAY_SIZE(mt8127_afe_o03_mix)),
	SND_SOC_DAPM_MIXER("O04", SND_SOC_NOPM, 0, 0,
			   mt8127_afe_o04_mix, ARRAY_SIZE(mt8127_afe_o04_mix)),
	SND_SOC_DAPM_MIXER("O09", SND_SOC_NOPM, 0, 0,
			   mt8127_afe_o09_mix, ARRAY_SIZE(mt8127_afe_o09_mix)),
	SND_SOC_DAPM_MIXER("O10", SND_SOC_NOPM, 0, 0,
			   mt8127_afe_o10_mix, ARRAY_SIZE(mt8127_afe_o10_mix)),
};

static const struct snd_soc_dapm_route mt8127_afe_pcm_routes[] = {
	{ "I05", NULL, "DL1" },
	{ "I06", NULL, "DL1" },
	{ "ADDA Playback", NULL, "O03" },
	{ "ADDA Playback", NULL, "O04" },
	{ "VUL", NULL, "O09" },
	{ "VUL", NULL, "O10" },
	{ "I03", NULL, "ADDA Capture" },
	{ "I04", NULL, "ADDA Capture" },
	{ "O03", "I05 Switch", "I05" },
	{ "O04", "I06 Switch", "I06" },
	{ "O09", "I03 Switch", "I03" },
	{ "O10", "I04 Switch", "I04" },
};

static const struct snd_soc_component_driver mt8127_afe_pcm_dai_component = {
	.name = "mt8127-afe-pcm-dai",
	.dapm_widgets = mt8127_afe_pcm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8127_afe_pcm_widgets),
	.dapm_routes = mt8127_afe_pcm_routes,
	.num_dapm_routes = ARRAY_SIZE(mt8127_afe_pcm_routes),
	.suspend = mtk_afe_suspend,
	.resume = mtk_afe_resume,
};

static const struct mtk_base_memif_data memif_data[MT8127_AFE_MEMIF_NUM] = {
	{
		.name = "DL1",
		.id = MT8127_AFE_MEMIF_DL1,
		.reg_ofs_base = AFE_DL1_BASE,
		.reg_ofs_cur = AFE_DL1_CUR,
		.reg_ofs_end = AFE_DL1_END,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = 0,
		.fs_maskbit = 0xf,
		.mono_reg = AFE_DAC_CON1,
		.mono_shift = 21,
		.hd_reg = -1,
		.hd_align_reg = -1,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 1,
		.msb_reg = -1,
		.agent_disable_reg = -1,
	}, {
		.name = "VUL",
		.id = MT8127_AFE_MEMIF_VUL,
		.reg_ofs_base = AFE_VUL_BASE,
		.reg_ofs_cur = AFE_VUL_CUR,
		.reg_ofs_end = AFE_VUL_END,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = 16,
		.fs_maskbit = 0xf,
		.mono_reg = AFE_DAC_CON1,
		.mono_shift = 27,
		.hd_reg = -1,
		.hd_align_reg = -1,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 3,
		.msb_reg = -1,
		.agent_disable_reg = -1,
	},
};

static const struct mtk_base_irq_data irq_data[MT8127_AFE_IRQ_NUM] = {
	{
		.id = MT8127_AFE_IRQ1,
		.irq_cnt_reg = AFE_IRQ_CNT1,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0x3ffff,
		.irq_en_reg = AFE_IRQ_MCU_CON,
		.irq_en_shift = 0,
		.irq_fs_reg = AFE_IRQ_MCU_CON,
		.irq_fs_shift = 4,
		.irq_fs_maskbit = 0xf,
		.irq_clr_reg = AFE_IRQ_CLR,
		.irq_clr_shift = 0,
	}, {
		.id = MT8127_AFE_IRQ2,
		.irq_cnt_reg = AFE_IRQ_CNT2,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0x3ffff,
		.irq_en_reg = AFE_IRQ_MCU_CON,
		.irq_en_shift = 1,
		.irq_fs_reg = AFE_IRQ_MCU_CON,
		.irq_fs_shift = 8,
		.irq_fs_maskbit = 0xf,
		.irq_clr_reg = AFE_IRQ_CLR,
		.irq_clr_shift = 1,
	},
};

static irqreturn_t mt8127_afe_irq_handler(int irq, void *dev_id)
{
	struct mtk_base_afe *afe = dev_id;
	unsigned int status;
	int i, ret;

	ret = regmap_read(afe->regmap, AFE_IRQ_STATUS, &status);
	if (ret) {
		dev_err(afe->dev, "failed to read the IRQ status: %d\n", ret);
		status = AFE_IRQ_STATUS_BITS;
		goto clear;
	}

	for (i = 0; i < MT8127_AFE_MEMIF_NUM; i++) {
		struct mtk_base_afe_memif *memif = &afe->memif[i];
		struct mtk_base_afe_irq *irq_p;

		if (memif->irq_usage < 0)
			continue;

		irq_p = &afe->irqs[memif->irq_usage];
		if (!(status & BIT(irq_p->irq_data->irq_clr_shift)))
			continue;

		if (memif->substream)
			snd_pcm_period_elapsed(memif->substream);
	}

clear:
	regmap_write(afe->regmap, AFE_IRQ_CLR, status & AFE_IRQ_STATUS_BITS);

	return IRQ_HANDLED;
}

static int mt8127_afe_runtime_suspend(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	struct mt8127_afe_private *afe_priv = afe->platform_priv;

	regmap_update_bits(afe->regmap, AFE_DAC_CON0, 0x1, 0);

	clk_bulk_disable_unprepare(MT8127_CLK_NUM, afe_priv->clks);

	return 0;
}

static int mt8127_afe_runtime_resume(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	struct mt8127_afe_private *afe_priv = afe->platform_priv;
	int ret;

	ret = clk_bulk_prepare_enable(MT8127_CLK_NUM, afe_priv->clks);
	if (ret)
		return ret;

	regmap_update_bits(afe->regmap, AUDIO_TOP_CON0,
			   AUD_TCON0_APB_BUS | AUD_TCON0_APB_W2T |
			   AUD_TCON0_APB_R2T,
			   AUD_TCON0_APB_BUS | AUD_TCON0_APB_W2T |
			   AUD_TCON0_APB_R2T);

	/* 16-bit samples on O03/O04 */
	regmap_update_bits(afe->regmap, AFE_CONN_24BIT, BIT(3) | BIT(4), 0);

	regmap_update_bits(afe->regmap, AFE_IRQ_MCU_EN, AFE_IRQ_STATUS_BITS,
			   AFE_IRQ_STATUS_BITS);

	regmap_update_bits(afe->regmap, AFE_DAC_CON0, 0x1, 0x1);

	return 0;
}

static int mt8127_afe_init_clks(struct mtk_base_afe *afe)
{
	struct mt8127_afe_private *afe_priv = afe->platform_priv;
	int i;

	for (i = 0; i < MT8127_CLK_NUM; i++)
		afe_priv->clks[i].id = mt8127_afe_clk_names[i];

	return devm_clk_bulk_get_optional(afe->dev, MT8127_CLK_NUM,
					  afe_priv->clks);
}

static int mt8127_afe_pcm_dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mt8127_afe_private *afe_priv;
	struct snd_soc_component *comp_pcm;
	struct mtk_base_afe *afe;
	int irq, ret, i;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	afe = devm_kzalloc(dev, sizeof(*afe), GFP_KERNEL);
	if (!afe)
		return -ENOMEM;

	afe_priv = devm_kzalloc(dev, sizeof(*afe_priv), GFP_KERNEL);
	if (!afe_priv)
		return -ENOMEM;

	afe->platform_priv = afe_priv;
	afe->dev = dev;
	mutex_init(&afe->irq_alloc_lock);

	ret = of_reserved_mem_device_init(dev);
	if (ret) {
		dev_dbg(dev, "no reserved memory, preallocating buffers\n");
		afe->preallocate_buffers = true;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	afe->regmap = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(afe->regmap))
		return dev_err_probe(dev, PTR_ERR(afe->regmap),
				     "failed to get the audsys regmap\n");

	ret = mt8127_afe_init_clks(afe);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get the clocks\n");

	afe->memif_size = MT8127_AFE_MEMIF_NUM;
	afe->memif = devm_kcalloc(dev, afe->memif_size, sizeof(*afe->memif),
				  GFP_KERNEL);
	if (!afe->memif)
		return -ENOMEM;

	afe->irqs_size = MT8127_AFE_IRQ_NUM;
	afe->irqs = devm_kcalloc(dev, afe->irqs_size, sizeof(*afe->irqs),
				 GFP_KERNEL);
	if (!afe->irqs)
		return -ENOMEM;

	for (i = 0; i < afe->irqs_size; i++)
		afe->irqs[i].irq_data = &irq_data[i];

	for (i = 0; i < afe->memif_size; i++) {
		afe->memif[i].data = &memif_data[i];
		afe->memif[i].irq_usage = -1;
	}

	/* DL1 owns IRQ1; the capture interfaces take IRQ2 when opened */
	afe->memif[MT8127_AFE_MEMIF_DL1].irq_usage = MT8127_AFE_IRQ1;
	afe->memif[MT8127_AFE_MEMIF_DL1].const_irq = 1;
	afe->irqs[MT8127_AFE_IRQ1].irq_occupyed = true;

	afe->mtk_afe_hardware = &mt8127_afe_hardware;
	afe->memif_fs = mt8127_memif_fs;
	afe->irq_fs = mt8127_irq_fs;
	afe->reg_back_up_list = mt8127_afe_backup_list;
	afe->reg_back_up_list_num = ARRAY_SIZE(mt8127_afe_backup_list);
	afe->runtime_resume = mt8127_afe_runtime_resume;
	afe->runtime_suspend = mt8127_afe_runtime_suspend;

	platform_set_drvdata(pdev, afe);

	pm_runtime_enable(dev);
	if (!pm_runtime_enabled(dev)) {
		ret = mt8127_afe_runtime_resume(dev);
		if (ret)
			goto err_pm_disable;
	}

	ret = devm_request_irq(dev, irq, mt8127_afe_irq_handler, 0,
			       "mt8127-afe", afe);
	if (ret) {
		dev_err(dev, "failed to request the IRQ: %d\n", ret);
		goto err_suspend;
	}

	ret = devm_snd_soc_register_component(dev, &mtk_afe_pcm_platform,
					      NULL, 0);
	if (ret)
		goto err_suspend;

	comp_pcm = devm_kzalloc(dev, sizeof(*comp_pcm), GFP_KERNEL);
	if (!comp_pcm) {
		ret = -ENOMEM;
		goto err_suspend;
	}

	ret = snd_soc_component_initialize(comp_pcm,
					   &mt8127_afe_pcm_dai_component, dev);
	if (ret)
		goto err_suspend;

	/* the regmap belongs to audsys, so the core cannot find it */
	snd_soc_component_init_regmap(comp_pcm, afe->regmap);

#ifdef CONFIG_DEBUG_FS
	comp_pcm->debugfs_prefix = "pcm";
#endif

	ret = snd_soc_add_component(comp_pcm, mt8127_afe_pcm_dais,
				    ARRAY_SIZE(mt8127_afe_pcm_dais));
	if (ret)
		goto err_suspend;

	return 0;

err_suspend:
	if (!pm_runtime_enabled(dev))
		mt8127_afe_runtime_suspend(dev);
err_pm_disable:
	pm_runtime_disable(dev);
	return ret;
}

static void mt8127_afe_pcm_dev_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	snd_soc_unregister_component(dev);

	pm_runtime_disable(dev);
	if (!pm_runtime_status_suspended(dev))
		mt8127_afe_runtime_suspend(dev);
}

static const struct of_device_id mt8127_afe_pcm_dt_match[] = {
	{ .compatible = "mediatek,mt8127-afe-pcm" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt8127_afe_pcm_dt_match);

static const struct dev_pm_ops mt8127_afe_pm_ops = {
	RUNTIME_PM_OPS(mt8127_afe_runtime_suspend,
		       mt8127_afe_runtime_resume, NULL)
};

static struct platform_driver mt8127_afe_pcm_driver = {
	.driver = {
		.name = "mt8127-afe-pcm",
		.of_match_table = mt8127_afe_pcm_dt_match,
		.pm = pm_ptr(&mt8127_afe_pm_ops),
	},
	.probe = mt8127_afe_pcm_dev_probe,
	.remove = mt8127_afe_pcm_dev_remove,
};
module_platform_driver(mt8127_afe_pcm_driver);

MODULE_DESCRIPTION("MediaTek MT8127 ALSA SoC AFE platform driver");
MODULE_AUTHOR("Marc Lainez <marc.lainez@gmail.com>");
MODULE_LICENSE("GPL");
