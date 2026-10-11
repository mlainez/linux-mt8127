// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek MT6323 PMIC audio codec driver
 *
 * The codec receives its clock and the downlink samples from the SoC audio
 * front-end over the AUD_CLK/AUD_DAT pins, and sends the uplink samples back
 * the same way. Its registers are reached through the PMIC wrapper: the
 * digital part (ABB_AFE) is at 0x4000, the analog part in AUDTOP_CON0-9.
 *
 * Outputs: the headphone drivers (HPL/HPR). Inputs: three microphone inputs
 * (MIC1-3) feeding a two channel ADC through the left and right PGA muxes.
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/mfd/mt6323/registers.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

/* Digital part of the codec */
#define MT6323_ABB_AFE_CON0		0x4000
#define MT6323_ABB_AFE_CON1		0x4002
#define MT6323_ABB_AFE_CON3		0x4006
#define MT6323_ABB_AFE_CON4		0x4008
#define MT6323_ABB_AFE_CON10		0x4014
#define MT6323_ABB_AFE_CON11		0x4016
#define MT6323_ABB_AFE_NEWIF_CFG0	0x4024
#define MT6323_ABB_AFE_NEWIF_CFG2	0x4028

/* ABB_AFE_CON0 */
#define ABB_AFE_CON0_UL_EN		BIT(1)
#define ABB_AFE_CON0_DL_EN		BIT(0)
/* ABB_AFE_CON1 */
#define ABB_AFE_CON1_UL_48K		BIT(4)
#define ABB_AFE_CON1_DL_RATE_MASK	GENMASK(3, 0)
/* ABB_AFE_CON10 */
#define ABB_AFE_CON10_DC_COMP_EN	BIT(0)
/* ABB_AFE_CON11: the status bits mirror the last trigger toggles */
#define ABB_AFE_CON11_DC_TRIG		BIT(9)
#define ABB_AFE_CON11_TOP_TRIG		BIT(8)
#define ABB_AFE_CON11_DC_STATUS		BIT(1)
#define ABB_AFE_CON11_TOP_STATUS	BIT(0)
/* ABB_AFE_NEWIF_CFG0 */
#define NEWIF_CFG0_DL_RATE(x)		((x) << 12)
#define NEWIF_CFG0_DL_DEFAULT		0x0330
/* ABB_AFE_NEWIF_CFG2 */
#define NEWIF_CFG2_UL_RATE(x)		((x) << 10)
#define NEWIF_CFG2_UL_DEFAULT		0x302f

/* TOP_CKPDN0 */
#define TOP_CKPDN0_CLKSQ_EN_AUD		BIT(0)
/* TOP_CKPDN1 */
#define TOP_CKPDN1_AUD_26M_PDN		BIT(8)

/* AUDTOP_CON0 */
#define AUDTOP_CON0_UL_L_MUX_SHIFT	0
#define AUDTOP_CON0_UL_L_GAIN_SHIFT	4
#define AUDTOP_CON0_UL_L_EN		(BIT(8) | BIT(7))
#define AUDTOP_CON0_UL_L_REF		BIT(11)
#define AUDTOP_CON0_UL_REF_MASK		(GENMASK(14, 13) | GENMASK(11, 7))
#define AUDTOP_CON0_VCM_SHIFT		12
#define AUDTOP_CON0_REF_MASK		GENMASK(15, 13)
#define AUDTOP_CON0_REF_ON		(BIT(14) | BIT(13))
/* AUDTOP_CON1 */
#define AUDTOP_CON1_UL_R_MUX_SHIFT	4
#define AUDTOP_CON1_UL_R_GAIN_SHIFT	8
/* AUDTOP_CON2 */
#define AUDTOP_CON2_UL_R_REF_MASK	GENMASK(7, 0)
#define AUDTOP_CON2_UL_R_REF_ON		0x00f0
#define AUDTOP_CON2_UL_R_REF_OFF	0x00c0
#define AUDTOP_CON2_UL_R_EN		(BIT(1) | BIT(0))
/* AUDTOP_CON3: the "4" input of each PGA mux crosses over to the other side */
#define AUDTOP_CON3_UL_R_CROSS		BIT(9)
#define AUDTOP_CON3_UL_L_CROSS		BIT(8)
/* AUDTOP_CON4 */
#define AUDTOP_CON4_HP_ON		0x007c
/* AUDTOP_CON5 */
#define AUDTOP_CON5_HPL_GAIN_SHIFT	12
#define AUDTOP_CON5_HPR_GAIN_SHIFT	8
#define AUDTOP_CON5_HP_GAIN_MASK	(GENMASK(14, 12) | GENMASK(10, 8))
#define AUDTOP_CON5_HP_BIAS		0x0014
/* AUDTOP_CON6 */
#define AUDTOP_CON6_HP_OFF		0x37e2
#define AUDTOP_CON6_HP_DEPOP		0xf7f2
#define AUDTOP_CON6_HP_ON		0xf5ba
/* AUDTOP_CON8 */
#define AUDTOP_CON8_MICBIAS_SHIFT	3

/* PGA mux register values that select the crossed-over input */
#define UL_MUX_CROSS			4

struct mt6323_codec_priv {
	struct device *dev;
	struct regmap *regmap;
	unsigned int dl_rate;
	unsigned int ul_rate;
};

/* DL rate index of ABB_AFE_CON1 and of the new interface */
static const struct {
	unsigned int rate;
	unsigned int con1;
	unsigned int newif;
} mt6323_dl_rates[] = {
	{ 8000, 0, 0 },
	{ 11025, 1, 1 },
	{ 12000, 2, 2 },
	{ 16000, 4, 3 },
	{ 22050, 5, 4 },
	{ 24000, 6, 5 },
	{ 32000, 8, 6 },
	{ 44100, 9, 7 },
	{ 48000, 10, 8 },
};

static int mt6323_dl_rate_index(unsigned int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt6323_dl_rates); i++)
		if (mt6323_dl_rates[i].rate == rate)
			return i;

	return -EINVAL;
}

/* Latch new ABB_AFE_CON0/CON1 settings: toggle the top control trigger. */
static void mt6323_top_ctl_trigger(struct mt6323_codec_priv *priv)
{
	unsigned int val;

	regmap_read(priv->regmap, MT6323_ABB_AFE_CON11, &val);
	regmap_update_bits(priv->regmap, MT6323_ABB_AFE_CON11,
			   ABB_AFE_CON11_TOP_TRIG,
			   (val & ABB_AFE_CON11_TOP_STATUS) ?
			   0 : ABB_AFE_CON11_TOP_TRIG);
}

/* Latch new DC compensation values: toggle the DC trigger. */
static void mt6323_dc_trigger(struct mt6323_codec_priv *priv)
{
	unsigned int val;

	regmap_read(priv->regmap, MT6323_ABB_AFE_CON11, &val);
	regmap_update_bits(priv->regmap, MT6323_ABB_AFE_CON11,
			   ABB_AFE_CON11_DC_TRIG,
			   (val & ABB_AFE_CON11_DC_STATUS) ?
			   0 : ABB_AFE_CON11_DC_TRIG);
}

static int mt6323_clksq_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_write(priv->regmap, MT6323_TOP_CKPDN0_SET,
			     TOP_CKPDN0_CLKSQ_EN_AUD);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_write(priv->regmap, MT6323_TOP_CKPDN0_CLR,
			     TOP_CKPDN0_CLKSQ_EN_AUD);
		break;
	}

	return 0;
}

static int mt6323_aud_clk_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_write(priv->regmap, MT6323_TOP_CKPDN1_CLR,
			     TOP_CKPDN1_AUD_26M_PDN);
		usleep_range(250, 300);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_write(priv->regmap, MT6323_TOP_CKPDN1_SET,
			     TOP_CKPDN1_AUD_26M_PDN);
		break;
	}

	return 0;
}

static int mt6323_dl_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	int i;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		i = mt6323_dl_rate_index(priv->dl_rate);
		if (i < 0)
			return i;

		regmap_write(priv->regmap, MT6323_ABB_AFE_NEWIF_CFG0,
			     NEWIF_CFG0_DL_RATE(mt6323_dl_rates[i].newif) |
			     NEWIF_CFG0_DL_DEFAULT);
		regmap_update_bits(priv->regmap, MT6323_ABB_AFE_CON1,
				   ABB_AFE_CON1_DL_RATE_MASK,
				   mt6323_dl_rates[i].con1);
		mt6323_top_ctl_trigger(priv);
		regmap_update_bits(priv->regmap, MT6323_ABB_AFE_CON0,
				   ABB_AFE_CON0_DL_EN, ABB_AFE_CON0_DL_EN);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(priv->regmap, MT6323_ABB_AFE_CON0,
				   ABB_AFE_CON0_DL_EN, 0);
		mt6323_top_ctl_trigger(priv);
		break;
	}

	return 0;
}

static int mt6323_ul_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	bool rate_48k = priv->ul_rate == 48000;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_write(priv->regmap, MT6323_ABB_AFE_NEWIF_CFG2,
			     NEWIF_CFG2_UL_RATE(rate_48k ? 3 : 1) |
			     NEWIF_CFG2_UL_DEFAULT);
		regmap_update_bits(priv->regmap, MT6323_ABB_AFE_CON1,
				   ABB_AFE_CON1_UL_48K,
				   rate_48k ? ABB_AFE_CON1_UL_48K : 0);
		mt6323_top_ctl_trigger(priv);
		regmap_update_bits(priv->regmap, MT6323_ABB_AFE_CON0,
				   ABB_AFE_CON0_UL_EN, ABB_AFE_CON0_UL_EN);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(priv->regmap, MT6323_ABB_AFE_CON0,
				   ABB_AFE_CON0_UL_EN, 0);
		break;
	}

	return 0;
}

static int mt6323_hp_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int gain;

	regmap_read(priv->regmap, MT6323_AUDTOP_CON5, &gain);
	gain &= AUDTOP_CON5_HP_GAIN_MASK;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* no DC offset calibration: compensate by zero */
		regmap_write(priv->regmap, MT6323_ABB_AFE_CON3, 0);
		regmap_write(priv->regmap, MT6323_ABB_AFE_CON4, 0);
		regmap_update_bits(priv->regmap, MT6323_ABB_AFE_CON10,
				   ABB_AFE_CON10_DC_COMP_EN,
				   ABB_AFE_CON10_DC_COMP_EN);
		mt6323_dc_trigger(priv);

		/* 2.4 V reference, half-voltage buffer for the HP VCM */
		regmap_write(priv->regmap, MT6323_AUDTOP_CON6,
			     AUDTOP_CON6_HP_DEPOP);
		regmap_update_bits(priv->regmap, MT6323_AUDTOP_CON0,
				   AUDTOP_CON0_REF_MASK, AUDTOP_CON0_REF_ON);
		/* start from the lowest gain */
		regmap_write(priv->regmap, MT6323_AUDTOP_CON5,
			     AUDTOP_CON5_HP_BIAS);
		/* audio bias, DACs and HP buffers on */
		regmap_write(priv->regmap, MT6323_AUDTOP_CON4,
			     AUDTOP_CON4_HP_ON);
		usleep_range(10000, 11000);
		/* release the pre-charge, disable the depop mux */
		regmap_write(priv->regmap, MT6323_AUDTOP_CON6,
			     AUDTOP_CON6_HP_ON);
		regmap_write(priv->regmap, MT6323_AUDTOP_CON5,
			     AUDTOP_CON5_HP_BIAS | gain);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(priv->regmap, MT6323_AUDTOP_CON5,
			     AUDTOP_CON5_HP_BIAS);
		regmap_write(priv->regmap, MT6323_AUDTOP_CON6,
			     AUDTOP_CON6_HP_DEPOP);
		regmap_write(priv->regmap, MT6323_AUDTOP_CON4, 0);
		regmap_write(priv->regmap, MT6323_AUDTOP_CON6,
			     AUDTOP_CON6_HP_OFF);
		/* keep the volume setting for the next power up */
		regmap_write(priv->regmap, MT6323_AUDTOP_CON5,
			     AUDTOP_CON5_HP_BIAS | gain);
		break;
	}

	return 0;
}

static int mt6323_ul_analog_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* left and right 1.4 V and 2.4 V references */
		regmap_update_bits(priv->regmap, MT6323_AUDTOP_CON0,
				   AUDTOP_CON0_UL_REF_MASK,
				   AUDTOP_CON0_REF_ON | AUDTOP_CON0_UL_L_REF);
		regmap_update_bits(priv->regmap, MT6323_AUDTOP_CON2,
				   AUDTOP_CON2_UL_R_REF_MASK,
				   AUDTOP_CON2_UL_R_REF_ON);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(priv->regmap, MT6323_AUDTOP_CON0,
				   AUDTOP_CON0_UL_L_REF, 0);
		regmap_update_bits(priv->regmap, MT6323_AUDTOP_CON2,
				   AUDTOP_CON2_UL_R_REF_MASK,
				   AUDTOP_CON2_UL_R_REF_OFF);
		regmap_update_bits(priv->regmap, MT6323_AUDTOP_CON3,
				   AUDTOP_CON3_UL_L_CROSS |
				   AUDTOP_CON3_UL_R_CROSS, 0);
		break;
	}

	return 0;
}

static int mt6323_adc_l_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mux;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_read(priv->regmap, MT6323_AUDTOP_CON0, &mux);
		mux = (mux >> AUDTOP_CON0_UL_L_MUX_SHIFT) & 0xf;
		regmap_update_bits(priv->regmap, MT6323_AUDTOP_CON3,
				   AUDTOP_CON3_UL_L_CROSS,
				   mux == UL_MUX_CROSS ?
				   AUDTOP_CON3_UL_L_CROSS : 0);
		regmap_update_bits(priv->regmap, MT6323_AUDTOP_CON0,
				   AUDTOP_CON0_UL_L_EN, AUDTOP_CON0_UL_L_EN);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(priv->regmap, MT6323_AUDTOP_CON0,
				   AUDTOP_CON0_UL_L_EN, 0);
		break;
	}

	return 0;
}

static int mt6323_adc_r_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(cmpnt);
	unsigned int mux;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_read(priv->regmap, MT6323_AUDTOP_CON1, &mux);
		mux = (mux >> AUDTOP_CON1_UL_R_MUX_SHIFT) & 0xf;
		regmap_update_bits(priv->regmap, MT6323_AUDTOP_CON3,
				   AUDTOP_CON3_UL_R_CROSS,
				   mux == UL_MUX_CROSS ?
				   AUDTOP_CON3_UL_R_CROSS : 0);
		regmap_update_bits(priv->regmap, MT6323_AUDTOP_CON2,
				   AUDTOP_CON2_UL_R_EN, AUDTOP_CON2_UL_R_EN);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_update_bits(priv->regmap, MT6323_AUDTOP_CON2,
				   AUDTOP_CON2_UL_R_EN, 0);
		break;
	}

	return 0;
}

/* Left and right PGA input selection; "Open" leaves the PGA unconnected. */
static const char * const mt6323_pga_mux_text[] = {
	"MIC1", "MIC2", "MIC3", "Open",
};

static const unsigned int mt6323_pga_l_mux_values[] = { 0, 1, 4, 3 };
static const unsigned int mt6323_pga_r_mux_values[] = { 4, 1, 0, 2 };

static SOC_VALUE_ENUM_SINGLE_DECL(mt6323_pga_l_mux_enum, MT6323_AUDTOP_CON0,
				  AUDTOP_CON0_UL_L_MUX_SHIFT, 0xf,
				  mt6323_pga_mux_text, mt6323_pga_l_mux_values);
static SOC_VALUE_ENUM_SINGLE_DECL(mt6323_pga_r_mux_enum, MT6323_AUDTOP_CON1,
				  AUDTOP_CON1_UL_R_MUX_SHIFT, 0xf,
				  mt6323_pga_mux_text, mt6323_pga_r_mux_values);

static const struct snd_kcontrol_new mt6323_pga_l_mux =
	SOC_DAPM_ENUM("Left PGA Mux", mt6323_pga_l_mux_enum);
static const struct snd_kcontrol_new mt6323_pga_r_mux =
	SOC_DAPM_ENUM("Right PGA Mux", mt6323_pga_r_mux_enum);

/* -5 dB to +9 dB in 2 dB steps */
static const DECLARE_TLV_DB_SCALE(mt6323_hp_tlv, -500, 200, 0);
/* -6 dB to +24 dB in 6 dB steps */
static const DECLARE_TLV_DB_SCALE(mt6323_pga_tlv, -600, 600, 0);

static const struct snd_kcontrol_new mt6323_snd_controls[] = {
	SOC_DOUBLE_TLV("Headphone Playback Volume", MT6323_AUDTOP_CON5,
		       AUDTOP_CON5_HPL_GAIN_SHIFT, AUDTOP_CON5_HPR_GAIN_SHIFT,
		       7, 0, mt6323_hp_tlv),
	SOC_SINGLE_TLV("Left PGA Capture Volume", MT6323_AUDTOP_CON0,
		       AUDTOP_CON0_UL_L_GAIN_SHIFT, 5, 0, mt6323_pga_tlv),
	SOC_SINGLE_TLV("Right PGA Capture Volume", MT6323_AUDTOP_CON1,
		       AUDTOP_CON1_UL_R_GAIN_SHIFT, 5, 0, mt6323_pga_tlv),
};

enum {
	MT6323_SUPPLY_SEQ_CLKSQ = 1,
	MT6323_SUPPLY_SEQ_AUD_CLK,
	MT6323_SUPPLY_SEQ_DIGITAL,
	MT6323_SUPPLY_SEQ_ANALOG,
};

static const struct snd_soc_dapm_widget mt6323_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY_S("CLKSQ", MT6323_SUPPLY_SEQ_CLKSQ, SND_SOC_NOPM,
			      0, 0, mt6323_clksq_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("AUD CK", MT6323_SUPPLY_SEQ_AUD_CLK, SND_SOC_NOPM,
			      0, 0, mt6323_aud_clk_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("DL Digital", MT6323_SUPPLY_SEQ_DIGITAL,
			      SND_SOC_NOPM, 0, 0, mt6323_dl_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("UL Digital", MT6323_SUPPLY_SEQ_DIGITAL,
			      SND_SOC_NOPM, 0, 0, mt6323_ul_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("VCM", MT6323_SUPPLY_SEQ_ANALOG,
			      MT6323_AUDTOP_CON0, AUDTOP_CON0_VCM_SHIFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("UL Analog", MT6323_SUPPLY_SEQ_ANALOG,
			      SND_SOC_NOPM, 0, 0, mt6323_ul_analog_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("MICBIAS", MT6323_AUDTOP_CON8,
			    AUDTOP_CON8_MICBIAS_SHIFT, 0, NULL, 0),

	/* Playback */
	SND_SOC_DAPM_AIF_IN("AIF RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_PGA_E("HP Driver", SND_SOC_NOPM, 0, 0, NULL, 0,
			   mt6323_hp_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),

	/* Capture */
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("MIC3"),
	SND_SOC_DAPM_MUX("Left PGA Mux", SND_SOC_NOPM, 0, 0, &mt6323_pga_l_mux),
	SND_SOC_DAPM_MUX("Right PGA Mux", SND_SOC_NOPM, 0, 0,
			 &mt6323_pga_r_mux),
	SND_SOC_DAPM_ADC_E("Left ADC", NULL, SND_SOC_NOPM, 0, 0,
			   mt6323_adc_l_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("Right ADC", NULL, SND_SOC_NOPM, 0, 0,
			   mt6323_adc_r_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_AIF_OUT("AIF TX", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route mt6323_dapm_routes[] = {
	{ "AUD CK", NULL, "CLKSQ" },
	{ "DL Digital", NULL, "AUD CK" },
	{ "UL Digital", NULL, "AUD CK" },

	/* Playback */
	{ "AIF RX", NULL, "DL Digital" },
	{ "DAC", NULL, "AIF RX" },
	{ "HP Driver", NULL, "DAC" },
	{ "HP Driver", NULL, "VCM" },
	{ "HPL", NULL, "HP Driver" },
	{ "HPR", NULL, "HP Driver" },

	/* Capture */
	{ "Left PGA Mux", "MIC1", "MIC1" },
	{ "Left PGA Mux", "MIC2", "MIC2" },
	{ "Left PGA Mux", "MIC3", "MIC3" },
	{ "Right PGA Mux", "MIC1", "MIC1" },
	{ "Right PGA Mux", "MIC2", "MIC2" },
	{ "Right PGA Mux", "MIC3", "MIC3" },
	{ "Left ADC", NULL, "Left PGA Mux" },
	{ "Right ADC", NULL, "Right PGA Mux" },
	{ "Left ADC", NULL, "UL Analog" },
	{ "Right ADC", NULL, "UL Analog" },
	{ "UL Analog", NULL, "VCM" },
	{ "AIF TX", NULL, "Left ADC" },
	{ "AIF TX", NULL, "Right ADC" },
	{ "AIF TX", NULL, "UL Digital" },
};

static int mt6323_codec_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct mt6323_codec_priv *priv =
		snd_soc_component_get_drvdata(dai->component);
	unsigned int rate = params_rate(params);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (mt6323_dl_rate_index(rate) < 0)
			return -EINVAL;
		priv->dl_rate = rate;
	} else {
		priv->ul_rate = rate;
	}

	return 0;
}

static const struct snd_soc_dai_ops mt6323_codec_dai_ops = {
	.hw_params = mt6323_codec_hw_params,
};

static struct snd_soc_dai_driver mt6323_codec_dai = {
	.name = "mt6323-aif1",
	.playback = {
		.stream_name = "AIF1 Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "AIF1 Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
			 SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &mt6323_codec_dai_ops,
};

static int mt6323_codec_probe(struct snd_soc_component *cmpnt)
{
	struct mt6323_codec_priv *priv = snd_soc_component_get_drvdata(cmpnt);

	snd_soc_component_init_regmap(cmpnt, priv->regmap);

	/* main microphone on both channels, +12 dB PGA gain */
	regmap_update_bits(priv->regmap, MT6323_AUDTOP_CON0,
			   GENMASK(6, 0), (3 << AUDTOP_CON0_UL_L_GAIN_SHIFT) |
			   (0 << AUDTOP_CON0_UL_L_MUX_SHIFT));
	regmap_update_bits(priv->regmap, MT6323_AUDTOP_CON1,
			   GENMASK(10, 4), (3 << AUDTOP_CON1_UL_R_GAIN_SHIFT) |
			   (UL_MUX_CROSS << AUDTOP_CON1_UL_R_MUX_SHIFT));
	/* lowest headphone gain, depop mux of the HP drivers on */
	regmap_write(priv->regmap, MT6323_AUDTOP_CON5, AUDTOP_CON5_HP_BIAS);
	regmap_write(priv->regmap, MT6323_AUDTOP_CON6, 0x37a2);
	regmap_write(priv->regmap, MT6323_AUDTOP_CON6, AUDTOP_CON6_HP_OFF);

	return 0;
}

static void mt6323_codec_remove(struct snd_soc_component *cmpnt)
{
	snd_soc_component_exit_regmap(cmpnt);
}

static const struct snd_soc_component_driver mt6323_codec_driver = {
	.probe = mt6323_codec_probe,
	.remove = mt6323_codec_remove,
	.controls = mt6323_snd_controls,
	.num_controls = ARRAY_SIZE(mt6323_snd_controls),
	.dapm_widgets = mt6323_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt6323_dapm_widgets),
	.dapm_routes = mt6323_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(mt6323_dapm_routes),
	.endianness = 1,
};

static int mt6323_codec_platform_probe(struct platform_device *pdev)
{
	struct mt6397_chip *mt6397 = dev_get_drvdata(pdev->dev.parent);
	struct mt6323_codec_priv *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->regmap = mt6397->regmap;
	if (!priv->regmap)
		return -ENODEV;

	priv->dl_rate = 48000;
	priv->ul_rate = 48000;

	platform_set_drvdata(pdev, priv);

	return devm_snd_soc_register_component(&pdev->dev,
					       &mt6323_codec_driver,
					       &mt6323_codec_dai, 1);
}

static const struct of_device_id mt6323_codec_of_match[] = {
	{ .compatible = "mediatek,mt6323-codec" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt6323_codec_of_match);

static const struct platform_device_id mt6323_codec_id_table[] = {
	{ "mt6323-codec" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, mt6323_codec_id_table);

static struct platform_driver mt6323_codec_platform_driver = {
	.driver = {
		.name = "mt6323-codec",
		.of_match_table = mt6323_codec_of_match,
	},
	.probe = mt6323_codec_platform_probe,
	.id_table = mt6323_codec_id_table,
};
module_platform_driver(mt6323_codec_platform_driver);

MODULE_DESCRIPTION("MediaTek MT6323 audio codec driver");
MODULE_AUTHOR("Marc Lainez <marc.lainez@gmail.com>");
MODULE_LICENSE("GPL");
