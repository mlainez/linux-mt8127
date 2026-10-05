// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Marc Lainez <marc.lainez@gmail.com>
 *
 * MediaTek MT8127 APMIXEDSYS: the PLLs that feed the CPU clock mux.
 */

#include <dt-bindings/clock/mt8127-clk.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-pll.h"

#define MT8127_PLL_FMAX		(2000UL * MHZ)
/* Below this VCO frequency the post divider has to be raised. */
#define MT8127_PLL_FMIN		(1000UL * MHZ)

/*
 * Layout shared by the PLLs: CON0 bit 0 enables, the 21 bit PCW is in CON1
 * with the post divider in CON1[26:24], and CON1[31] makes the PLL take a new
 * PCW. The ARMPLL runs the CPUs, so it must never be switched off.
 */
#define PLL(_id, _name, _reg, _pwr_reg, _pcw_reg, _flags) {		\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.pwr_reg = _pwr_reg,					\
		.en_mask = BIT(0),					\
		.flags = _flags,					\
		.fmax = MT8127_PLL_FMAX,				\
		.fmin = MT8127_PLL_FMIN,				\
		.pcwbits = 21,						\
		.pd_reg = _pcw_reg,					\
		.pd_shift = 24,						\
		.pcw_reg = _pcw_reg,					\
		.pcw_shift = 0,						\
		.pcw_chg_reg = _pcw_reg,				\
	}

static const struct mtk_pll_data plls[] = {
	PLL(CLK_APMIXED_ARMPLL, "armpll", 0x200, 0x20c, 0x204, PLL_AO),
	PLL(CLK_APMIXED_MAINPLL, "mainpll", 0x210, 0x21c, 0x214, PLL_AO),
	PLL(CLK_APMIXED_UNIVPLL, "univpll", 0x220, 0x22c, 0x224, PLL_AO),
};

static int clk_mt8127_apmixed_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	int ret;

	clk_data = mtk_alloc_clk_data(CLK_APMIXED_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	ret = mtk_clk_register_plls(&pdev->dev, plls, ARRAY_SIZE(plls),
				    clk_data);
	if (ret)
		goto free_clk_data;

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (ret)
		goto unregister_plls;

	platform_set_drvdata(pdev, clk_data);

	return 0;

unregister_plls:
	mtk_clk_unregister_plls(plls, ARRAY_SIZE(plls), clk_data);
free_clk_data:
	mtk_free_clk_data(clk_data);

	return ret;
}

static void clk_mt8127_apmixed_remove(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);

	of_clk_del_provider(node);
	mtk_clk_unregister_plls(plls, ARRAY_SIZE(plls), clk_data);
	mtk_free_clk_data(clk_data);
}

static const struct of_device_id of_match_clk_mt8127_apmixed[] = {
	{ .compatible = "mediatek,mt8127-apmixedsys" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8127_apmixed);

static struct platform_driver clk_mt8127_apmixed_drv = {
	.probe = clk_mt8127_apmixed_probe,
	.remove = clk_mt8127_apmixed_remove,
	.driver = {
		.name = "clk-mt8127-apmixed",
		.of_match_table = of_match_clk_mt8127_apmixed,
	},
};
module_platform_driver(clk_mt8127_apmixed_drv);

MODULE_DESCRIPTION("MediaTek MT8127 apmixedsys clock driver");
MODULE_LICENSE("GPL");
