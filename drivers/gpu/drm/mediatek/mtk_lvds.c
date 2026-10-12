// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek MT8127 LVDS transmitter
 *
 * The transmitter serializes the parallel output of DPI0 on one LVDS link.
 * Its PLL (LVDS VPLL) runs from the MIPI TX PLL and also generates the DPI0
 * pixel clock, so it has to be running before the DPI is programmed.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/media-bus-format.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_modes.h>

#include "mtk_drm_drv.h"

/* LVDS TX digital block, at offset 0x200 of the LVDS register space */
#define LVDS_FMTCTRL			0x200
#define LVDS_FMT_DATA_FMT		GENMASK(6, 4)
#define LVDS_FMT_DATA_RGB888		0
#define LVDS_FMT_DATA_RGB666		1
#define LVDS_OUT_CTRL			0x218
#define LVDS_OUT_EN			BIT(0)
#define LVDS_CLK_CTRL			0x220
#define LVDS_TX_CK_EN			BIT(0)
#define LVDS_RX_CK_EN			BIT(1)
#define LVDS_TEST_CK_EN			BIT(2)

/* LVDS analog block (serializer, drivers and LVDS VPLL) */
#define LVDSTX_ANA_CTL2			0x04
#define LVDSTX_ANA_TVO			GENMASK(3, 0)
#define LVDSTX_ANA_TVCM			GENMASK(7, 4)
#define LVDSTX_ANA_NSRC			GENMASK(11, 8)
#define LVDSTX_ANA_PSRC			GENMASK(15, 12)
#define LVDSTX_ANA_BIAS_SEL		GENMASK(17, 16)
#define LVDSTX_ANA_LDO_EN		BIT(22)
#define LVDSTX_ANA_BIAS_EN		BIT(23)
#define LVDSTX_ANA_CTL3			0x08
#define LVDSTX_ANA_EXT_EN		GENMASK(9, 5)
#define LVDSTX_ANA_DRV_EN		GENMASK(14, 10)
#define LVDS_VPLL_CTL2			0x18
#define LVDS_VPLL_EN			BIT(7)
#define LVDS_VPLL_CLK_SEL_MIPI		BIT(23)

/* MMSYS_CONFIG register gating the LVDS TX output */
#define MT8127_MMSYS_LVDS_CFG		0x90c
#define MT8127_MMSYS_LVDS_OUT_EN	BIT(16)

/* The MIPI TX PLL runs at 14 times the pixel clock */
#define LVDS_PLL_PIXEL_RATIO		14
#define MIPI_TX_PLL_MIN_HZ		50000000UL
#define MIPI_TX_PLL_MAX_HZ		1250000000UL

/* All four data lanes and the clock lane */
#define LVDS_LANES_MASK			0x1f

struct mtk_lvds {
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct device *dev;
	void __iomem *regs;
	void __iomem *ana;
	struct regmap *mmsys;
	struct clk *pixel_clk;
	struct clk *cts_clk;
	struct clk *pll;
	bool enabled;
};

static inline struct mtk_lvds *bridge_to_lvds(struct drm_bridge *bridge)
{
	return container_of(bridge, struct mtk_lvds, bridge);
}

static void mtk_lvds_update_bits(void __iomem *reg, u32 mask, u32 val)
{
	writel((readl(reg) & ~mask) | val, reg);
}

static u32 mtk_lvds_data_fmt(struct drm_connector *connector)
{
	const struct drm_display_info *info = &connector->display_info;

	if (info->num_bus_formats &&
	    info->bus_formats[0] == MEDIA_BUS_FMT_RGB666_1X7X3_SPWG)
		return LVDS_FMT_DATA_RGB666;

	return LVDS_FMT_DATA_RGB888;
}

static void mtk_lvds_tx_off(struct mtk_lvds *lvds)
{
	writel(0, lvds->ana + LVDSTX_ANA_CTL3);
	usleep_range(5, 10);
	writel(0, lvds->ana + LVDSTX_ANA_CTL2);
	usleep_range(5, 10);
	writel(0, lvds->regs + LVDS_CLK_CTRL);
	writel(0, lvds->regs + LVDS_OUT_CTRL);
	usleep_range(5, 10);
	mtk_lvds_update_bits(lvds->ana + LVDS_VPLL_CTL2, LVDS_VPLL_EN, 0);
}

static int mtk_lvds_power_on(struct mtk_lvds *lvds, unsigned long pixel_rate)
{
	int ret;

	ret = clk_prepare_enable(lvds->pixel_clk);
	if (ret) {
		dev_err(lvds->dev, "Failed to enable pixel clock: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(lvds->cts_clk);
	if (ret) {
		dev_err(lvds->dev, "Failed to enable CTS clock: %d\n", ret);
		goto err_pixel;
	}

	/*
	 * The bootloader may have left the transmitter running. Stop it before
	 * its PLL is reprogrammed: a transmitter that keeps running across the
	 * PLL change can send a stream the panel does not lock onto.
	 */
	mtk_lvds_tx_off(lvds);

	ret = clk_set_rate(lvds->pll, pixel_rate * LVDS_PLL_PIXEL_RATIO);
	if (ret) {
		dev_err(lvds->dev, "Failed to set PLL rate: %d\n", ret);
		goto err_cts;
	}

	ret = clk_prepare_enable(lvds->pll);
	if (ret) {
		dev_err(lvds->dev, "Failed to enable PLL: %d\n", ret);
		goto err_cts;
	}

	return 0;

err_cts:
	clk_disable_unprepare(lvds->cts_clk);
err_pixel:
	clk_disable_unprepare(lvds->pixel_clk);
	return ret;
}

static void mtk_lvds_power_off(struct mtk_lvds *lvds)
{
	clk_disable_unprepare(lvds->pll);
	clk_disable_unprepare(lvds->cts_clk);
	clk_disable_unprepare(lvds->pixel_clk);
}

static void mtk_lvds_bridge_atomic_pre_enable(struct drm_bridge *bridge,
					      struct drm_atomic_state *state)
{
	struct mtk_lvds *lvds = bridge_to_lvds(bridge);
	const struct drm_crtc_state *crtc_state;
	const struct drm_connector_state *conn_state;
	struct drm_connector *connector;
	u32 val;

	connector = drm_atomic_get_new_connector_for_encoder(state,
							     bridge->encoder);
	if (WARN_ON(!connector))
		return;

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);

	if (mtk_lvds_power_on(lvds, crtc_state->adjusted_mode.clock * 1000UL))
		return;

	/* Let the MIPI TX PLL settle, as the vendor driver does */
	usleep_range(1000, 1500);

	mtk_lvds_update_bits(lvds->ana + LVDS_VPLL_CTL2,
			     LVDS_VPLL_CLK_SEL_MIPI | LVDS_VPLL_EN,
			     LVDS_VPLL_CLK_SEL_MIPI | LVDS_VPLL_EN);
	usleep_range(30, 60);

	writel(FIELD_PREP(LVDSTX_ANA_EXT_EN, LVDS_LANES_MASK) |
	       FIELD_PREP(LVDSTX_ANA_DRV_EN, LVDS_LANES_MASK),
	       lvds->ana + LVDSTX_ANA_CTL3);
	usleep_range(5, 10);

	val = FIELD_PREP(LVDSTX_ANA_TVO, 0x3) |
	      FIELD_PREP(LVDSTX_ANA_TVCM, 0xb) |
	      FIELD_PREP(LVDSTX_ANA_NSRC, 0xf) |
	      FIELD_PREP(LVDSTX_ANA_PSRC, 0x0) |
	      FIELD_PREP(LVDSTX_ANA_BIAS_SEL, 0x1) |
	      LVDSTX_ANA_LDO_EN | LVDSTX_ANA_BIAS_EN;
	writel(val, lvds->ana + LVDSTX_ANA_CTL2);
	usleep_range(5, 10);

	writel(LVDS_TX_CK_EN | LVDS_RX_CK_EN | LVDS_TEST_CK_EN,
	       lvds->regs + LVDS_CLK_CTRL);
	writel(LVDS_OUT_EN, lvds->regs + LVDS_OUT_CTRL);
	usleep_range(5, 10);

	regmap_set_bits(lvds->mmsys, MT8127_MMSYS_LVDS_CFG,
			MT8127_MMSYS_LVDS_OUT_EN);

	mtk_lvds_update_bits(lvds->regs + LVDS_FMTCTRL, LVDS_FMT_DATA_FMT,
			     FIELD_PREP(LVDS_FMT_DATA_FMT,
					mtk_lvds_data_fmt(connector)));

	lvds->enabled = true;
}

static void mtk_lvds_bridge_atomic_post_disable(struct drm_bridge *bridge,
						struct drm_atomic_state *state)
{
	struct mtk_lvds *lvds = bridge_to_lvds(bridge);

	if (!lvds->enabled)
		return;

	mtk_lvds_tx_off(lvds);
	mtk_lvds_power_off(lvds);
	lvds->enabled = false;
}

static enum drm_mode_status
mtk_lvds_bridge_mode_valid(struct drm_bridge *bridge,
			   const struct drm_display_info *info,
			   const struct drm_display_mode *mode)
{
	unsigned long rate = mode->clock * 1000UL * LVDS_PLL_PIXEL_RATIO;

	if (rate < MIPI_TX_PLL_MIN_HZ)
		return MODE_CLOCK_LOW;
	if (rate > MIPI_TX_PLL_MAX_HZ)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static int mtk_lvds_bridge_attach(struct drm_bridge *bridge,
				  struct drm_encoder *encoder,
				  enum drm_bridge_attach_flags flags)
{
	struct mtk_lvds *lvds = bridge_to_lvds(bridge);

	return drm_bridge_attach(encoder, lvds->next_bridge, bridge, flags);
}

static const struct drm_bridge_funcs mtk_lvds_bridge_funcs = {
	.attach = mtk_lvds_bridge_attach,
	.mode_valid = mtk_lvds_bridge_mode_valid,
	.atomic_pre_enable = mtk_lvds_bridge_atomic_pre_enable,
	.atomic_post_disable = mtk_lvds_bridge_atomic_post_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

static int mtk_lvds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_lvds *lvds;

	lvds = devm_drm_bridge_alloc(dev, struct mtk_lvds, bridge,
				     &mtk_lvds_bridge_funcs);
	if (IS_ERR(lvds))
		return PTR_ERR(lvds);

	lvds->dev = dev;

	lvds->regs = devm_platform_ioremap_resource_byname(pdev, "tx");
	if (IS_ERR(lvds->regs))
		return PTR_ERR(lvds->regs);

	lvds->ana = devm_platform_ioremap_resource_byname(pdev, "ana");
	if (IS_ERR(lvds->ana))
		return PTR_ERR(lvds->ana);

	lvds->mmsys = syscon_regmap_lookup_by_phandle(dev->of_node,
						      "mediatek,mmsys");
	if (IS_ERR(lvds->mmsys))
		return dev_err_probe(dev, PTR_ERR(lvds->mmsys),
				     "Failed to get mmsys regmap\n");

	lvds->pixel_clk = devm_clk_get(dev, "pixel");
	if (IS_ERR(lvds->pixel_clk))
		return dev_err_probe(dev, PTR_ERR(lvds->pixel_clk),
				     "Failed to get pixel clock\n");

	lvds->cts_clk = devm_clk_get(dev, "cts");
	if (IS_ERR(lvds->cts_clk))
		return dev_err_probe(dev, PTR_ERR(lvds->cts_clk),
				     "Failed to get CTS clock\n");

	lvds->pll = devm_clk_get(dev, "pll");
	if (IS_ERR(lvds->pll))
		return dev_err_probe(dev, PTR_ERR(lvds->pll),
				     "Failed to get PLL\n");

	lvds->next_bridge = devm_drm_of_get_bridge(dev, dev->of_node, 1, 0);
	if (IS_ERR(lvds->next_bridge))
		return dev_err_probe(dev, PTR_ERR(lvds->next_bridge),
				     "Failed to get next bridge\n");

	lvds->bridge.of_node = dev->of_node;

	return devm_drm_bridge_add(dev, &lvds->bridge);
}

static const struct of_device_id mtk_lvds_of_match[] = {
	{ .compatible = "mediatek,mt8127-lvds" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_lvds_of_match);

struct platform_driver mtk_lvds_driver = {
	.probe = mtk_lvds_probe,
	.driver = {
		.name = "mediatek-lvds",
		.of_match_table = mtk_lvds_of_match,
	},
};
