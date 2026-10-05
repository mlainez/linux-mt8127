// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Marc Lainez <marc.lainez@gmail.com>
 *
 * MediaTek MT8127 PERICFG: the I2C and AP DMA clock gates.
 */

#include <dt-bindings/clock/mt8127-clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs peri0_cg_regs = {
	.set_ofs = 0x0008,
	.clr_ofs = 0x0010,
	.sta_ofs = 0x0018,
};

#define GATE_PERI0(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &peri0_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

/*
 * The I2C and AP DMA blocks run from the bus clock, which the bootloader sets to
 * MAINPLL / 4 and which the board describes as "axi_sel".
 */
static const struct mtk_gate peri_clks[] = {
	GATE_PERI0(CLK_PERI_I2C0, "i2c0_ck", "axi_sel", 22),
	GATE_PERI0(CLK_PERI_I2C1, "i2c1_ck", "axi_sel", 23),
	GATE_PERI0(CLK_PERI_I2C2, "i2c2_ck", "axi_sel", 24),
	GATE_PERI0(CLK_PERI_I2C3, "i2c3_ck", "axi_sel", 25),
	GATE_PERI0(CLK_PERI_AP_DMA, "ap_dma_ck", "axi_sel", 12),
};

static int clk_mt8127_pericfg_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data;
	int ret;

	clk_data = mtk_alloc_clk_data(CLK_PERI_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	ret = mtk_clk_register_gates(&pdev->dev, node, peri_clks,
				     ARRAY_SIZE(peri_clks), clk_data);
	if (ret)
		goto free_clk_data;

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (ret)
		goto unregister_gates;

	platform_set_drvdata(pdev, clk_data);

	return 0;

unregister_gates:
	mtk_clk_unregister_gates(peri_clks, ARRAY_SIZE(peri_clks), clk_data);
free_clk_data:
	mtk_free_clk_data(clk_data);

	return ret;
}

static void clk_mt8127_pericfg_remove(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);

	of_clk_del_provider(node);
	mtk_clk_unregister_gates(peri_clks, ARRAY_SIZE(peri_clks), clk_data);
	mtk_free_clk_data(clk_data);
}

static const struct of_device_id of_match_clk_mt8127_pericfg[] = {
	{ .compatible = "mediatek,mt8127-pericfg" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8127_pericfg);

static struct platform_driver clk_mt8127_pericfg_drv = {
	.driver = {
		.name = "clk-mt8127-pericfg",
		.of_match_table = of_match_clk_mt8127_pericfg,
	},
	.probe = clk_mt8127_pericfg_probe,
	.remove = clk_mt8127_pericfg_remove,
};
module_platform_driver(clk_mt8127_pericfg_drv);

MODULE_DESCRIPTION("MediaTek MT8127 pericfg clock driver");
MODULE_LICENSE("GPL");
