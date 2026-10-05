// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek MT8127 USB2 PHY (MT6589 generation).
 *
 * Replays the power-on/"recover" sequence of the vendor 3.10 kernel
 * (usb_phy_poweron / usb_phy_recover) on top of whatever state the bootloader
 * left behind. The frequency-meter calibration step of the vendor code is not
 * done, so the PHY runs with its default HS current/PLL trim.
 *
 * There is no clock or power driver for this SoC yet, so the USB0 clock gate
 * (PERICFG) and the 48 MHz PHY clock enable (APMIXEDSYS) are written directly.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#define PERI_PDN0_CLR		0x010	/* PERI_GLOBALCON_PDN0 clear */
#define PERI_PDN0_USB0		BIT(10)
#define APMIXED_USB_CLK		0x220
#define APMIXED_USB_CLK_EN	BIT(26)

struct mt8127_usb2_phy {
	void __iomem *phy;
	void __iomem *pericfg;
	void __iomem *apmixed;
	bool powered;
};

static void rmw8(void __iomem *base, u32 off, u8 clr, u8 set)
{
	u8 v = readb(base + off);

	writeb((v & ~clr) | set, base + off);
}

static int mt8127_usb2_phy_init(struct phy *phy)
{
	struct mt8127_usb2_phy *p = phy_get_drvdata(phy);
	u32 v;

	writel(PERI_PDN0_USB0, p->pericfg + PERI_PDN0_CLR);
	v = readl(p->apmixed + APMIXED_USB_CLK);
	writel(v | APMIXED_USB_CLK_EN, p->apmixed + APMIXED_USB_CLK);
	usleep_range(1250, 1500);

	return 0;
}

static int mt8127_usb2_phy_power_on(struct phy *phy)
{
	struct mt8127_usb2_phy *p = phy_get_drvdata(phy);
	void __iomem *b = p->phy;

	if (p->powered)
		return 0;

	rmw8(b, 0x81d, 0x10, 0);
	rmw8(b, 0x86b, 0x04, 0);
	rmw8(b, 0x86e, 0x01, 0);
	rmw8(b, 0x86a, 0x04, 0);
	rmw8(b, 0x868, 0x40, 0);
	rmw8(b, 0x868, 0x80, 0);
	rmw8(b, 0x868, 0x30, 0);
	rmw8(b, 0x868, 0x04, 0);
	rmw8(b, 0x869, 0x3c, 0);
	rmw8(b, 0x86a, 0x10, 0);
	rmw8(b, 0x86a, 0x20, 0);
	rmw8(b, 0x86a, 0x08, 0);
	rmw8(b, 0x86a, 0x02, 0);
	rmw8(b, 0x86a, 0x80, 0);
	rmw8(b, 0x81a, 0x80, 0);
	rmw8(b, 0x81a, 0, 0x10);
	msleep(20);

	rmw8(b, 0x86c, 0x10, 0);
	rmw8(b, 0x86c, 0, 0x2e);
	rmw8(b, 0x86d, 0, 0x3e);

	/* Frequency-meter block off, default trim in 0x815 */
	writeb(0x00, b + 0x815);
	rmw8(b, 0xf03, 0x01, 0);
	rmw8(b, 0xf11, 0x01, 0);

	rmw8(b, 0x805, 0x70, 0);
	writeb(0x40, b + 0x805);
	rmw8(b, 0x800, 0x20, 0);

	rmw8(b, 0x81a, 0, 0x80);
	usleep_range(25, 50);
	rmw8(b, 0x81a, 0x80, 0);

	p->powered = true;
	return 0;
}

static const struct phy_ops mt8127_usb2_phy_ops = {
	.init		= mt8127_usb2_phy_init,
	.power_on	= mt8127_usb2_phy_power_on,
	.owner		= THIS_MODULE,
};

static int mt8127_usb2_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mt8127_usb2_phy *p;
	struct phy_provider *provider;
	struct phy *phy;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->phy = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(p->phy))
		return PTR_ERR(p->phy);
	p->pericfg = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(p->pericfg))
		return PTR_ERR(p->pericfg);
	p->apmixed = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(p->apmixed))
		return PTR_ERR(p->apmixed);

	phy = devm_phy_create(dev, NULL, &mt8127_usb2_phy_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);
	phy_set_drvdata(phy, p);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(provider);
}

static const struct of_device_id mt8127_usb2_phy_of_match[] = {
	{ .compatible = "mediatek,mt8127-usb2-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt8127_usb2_phy_of_match);

static struct platform_driver mt8127_usb2_phy_driver = {
	.probe	= mt8127_usb2_phy_probe,
	.driver	= {
		.name		= "mt8127-usb2-phy",
		.of_match_table	= mt8127_usb2_phy_of_match,
	},
};
module_platform_driver(mt8127_usb2_phy_driver);

MODULE_DESCRIPTION("MediaTek MT8127 USB2 PHY driver");
MODULE_LICENSE("GPL");
