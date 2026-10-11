// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT6323 PMIC AUXADC driver
 *
 * The AUXADC is a 15-bit converter with a 1.8 V reference. A conversion is
 * started by a rising edge on the channel bit of the AP request list; the
 * result and a ready flag then show up in the channel's output register.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/mfd/mt6323/registers.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <linux/iio/iio.h>

#include <dt-bindings/iio/adc/mediatek,mt6323-auxadc.h>

#define MT6323_AUXADC_VREF_MV		1800
#define MT6323_AUXADC_BITS		15

#define MT6323_AUXADC_OUT_MASK		GENMASK(14, 0)
#define MT6323_AUXADC_RDY		BIT(15)

/* AUXADC_CON11 */
#define MT6323_AUXADC_VBUF_EN		BIT(4)
/* AUXADC_CON19 */
#define MT6323_AUXADC_DECI_GDLY		GENMASK(15, 14)
/* AUXADC_ADC19 */
#define MT6323_AUXADC_BUSY		GENMASK(15, 1)
/* AUXADC_CON22 */
#define MT6323_AUXADC_AP_RQST_LIST	GENMASK(8, 0)

/* The converter needs this long after a request before its ready flag is valid. */
#define MT6323_AUXADC_SETTLE_US		300
#define MT6323_AUXADC_POLL_US		1000
#define MT6323_AUXADC_TIMEOUT_US	100000

struct mt6323_auxadc {
	struct device *dev;
	struct regmap *regmap;
	/* Serializes conversions: the request list and VBUF are shared. */
	struct mutex lock;
};

/**
 * struct mt6323_auxadc_chan - per-channel hardware description
 * @rqst_bit: bit in the AP request list that starts a conversion
 * @out_reg:  output register (result and ready flag)
 * @ratio:    input divider in front of the converter
 */
struct mt6323_auxadc_chan {
	u8 rqst_bit;
	u16 out_reg;
	u8 ratio;
};

static const struct mt6323_auxadc_chan mt6323_auxadc_desc[] = {
	[MT6323_AUXADC_BATSNS]		= { 7, MT6323_AUXADC_ADC0, 4 },
	[MT6323_AUXADC_ISENSE]		= { 6, MT6323_AUXADC_ADC1, 4 },
	[MT6323_AUXADC_VCDT]		= { 4, MT6323_AUXADC_ADC2, 1 },
	[MT6323_AUXADC_BATON1]		= { 5, MT6323_AUXADC_ADC3, 1 },
	[MT6323_AUXADC_THR_SENSE1]	= { 3, MT6323_AUXADC_ADC4, 1 },
};

#define MT6323_AUXADC_CHAN(_idx, _name)					\
{									\
	.type = IIO_VOLTAGE,						\
	.indexed = 1,							\
	.channel = MT6323_AUXADC_##_idx,				\
	.scan_index = MT6323_AUXADC_##_idx,				\
	.datasheet_name = _name,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
			      BIT(IIO_CHAN_INFO_SCALE),			\
}

static const struct iio_chan_spec mt6323_auxadc_channels[] = {
	MT6323_AUXADC_CHAN(BATSNS, "batsns"),
	MT6323_AUXADC_CHAN(ISENSE, "isense"),
	MT6323_AUXADC_CHAN(VCDT, "vcdt"),
	MT6323_AUXADC_CHAN(BATON1, "baton1"),
	MT6323_AUXADC_CHAN(THR_SENSE1, "thr_sense1"),
};

/*
 * The decimation group delay is raised for sleep mode. It has to be cleared
 * while no conversion is running before AP requests are served.
 */
static int mt6323_auxadc_clear_gdly(struct mt6323_auxadc *adc)
{
	unsigned int val;
	int ret;

	ret = regmap_read(adc->regmap, MT6323_AUXADC_CON19, &val);
	if (ret || !FIELD_GET(MT6323_AUXADC_DECI_GDLY, val))
		return ret;

	ret = regmap_read_poll_timeout(adc->regmap, MT6323_AUXADC_ADC19, val,
				       !(val & MT6323_AUXADC_BUSY),
				       MT6323_AUXADC_POLL_US,
				       MT6323_AUXADC_TIMEOUT_US);
	if (ret)
		return ret;

	return regmap_clear_bits(adc->regmap, MT6323_AUXADC_CON19,
				 MT6323_AUXADC_DECI_GDLY);
}

static int mt6323_auxadc_read(struct mt6323_auxadc *adc,
			      const struct mt6323_auxadc_chan *desc, int *out)
{
	unsigned int val;
	int ret;

	guard(mutex)(&adc->lock);

	ret = mt6323_auxadc_clear_gdly(adc);
	if (ret)
		return ret;

	ret = regmap_set_bits(adc->regmap, MT6323_AUXADC_CON11,
			      MT6323_AUXADC_VBUF_EN);
	if (ret)
		return ret;

	ret = regmap_clear_bits(adc->regmap, MT6323_AUXADC_CON22,
				BIT(desc->rqst_bit));
	if (ret)
		return ret;

	ret = regmap_set_bits(adc->regmap, MT6323_AUXADC_CON22,
			      BIT(desc->rqst_bit));
	if (ret)
		return ret;

	fsleep(MT6323_AUXADC_SETTLE_US);

	ret = regmap_read_poll_timeout(adc->regmap, desc->out_reg, val,
				       val & MT6323_AUXADC_RDY,
				       MT6323_AUXADC_POLL_US,
				       MT6323_AUXADC_TIMEOUT_US);
	if (ret) {
		dev_dbg(adc->dev, "conversion timeout on %#x\n", desc->out_reg);
		return ret;
	}

	*out = FIELD_GET(MT6323_AUXADC_OUT_MASK, val);

	return 0;
}

static int mt6323_auxadc_read_raw(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  int *val, int *val2, long mask)
{
	struct mt6323_auxadc *adc = iio_priv(indio_dev);
	const struct mt6323_auxadc_chan *desc = &mt6323_auxadc_desc[chan->channel];
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = mt6323_auxadc_read(adc, desc, val);
		if (ret)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = desc->ratio * MT6323_AUXADC_VREF_MV;
		*val2 = MT6323_AUXADC_BITS;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static int mt6323_auxadc_read_label(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan, char *label)
{
	return sysfs_emit(label, "%s\n", chan->datasheet_name);
}

static const struct iio_info mt6323_auxadc_info = {
	.read_raw = mt6323_auxadc_read_raw,
	.read_label = mt6323_auxadc_read_label,
};

static int mt6323_auxadc_probe(struct platform_device *pdev)
{
	struct mt6397_chip *pmic = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct mt6323_auxadc *adc;
	int ret;

	if (!pmic || !pmic->regmap)
		return dev_err_probe(dev, -ENODEV, "no PMIC regmap\n");

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->dev = dev;
	adc->regmap = pmic->regmap;

	ret = devm_mutex_init(dev, &adc->lock);
	if (ret)
		return ret;

	indio_dev->name = "mt6323-auxadc";
	indio_dev->info = &mt6323_auxadc_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = mt6323_auxadc_channels;
	indio_dev->num_channels = ARRAY_SIZE(mt6323_auxadc_channels);

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id mt6323_auxadc_of_match[] = {
	{ .compatible = "mediatek,mt6323-auxadc" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6323_auxadc_of_match);

static struct platform_driver mt6323_auxadc_driver = {
	.driver = {
		.name = "mt6323-auxadc",
		.of_match_table = mt6323_auxadc_of_match,
	},
	.probe = mt6323_auxadc_probe,
};
module_platform_driver(mt6323_auxadc_driver);

MODULE_DESCRIPTION("MediaTek MT6323 PMIC AUXADC driver");
MODULE_LICENSE("GPL");
