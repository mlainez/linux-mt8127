// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Marc Lainez <marc.lainez@gmail.com>
 *
 * MediaTek MT8127 INFRACFG: the CPU clock mux.
 */

#include <dt-bindings/clock/mt8127-clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "clk-cpumux.h"
#include "clk-mtk.h"

static const char * const cpu_parents[] __initconst = {
	"clk26m",
	"armpll",
	"mainpll",
	"univpll"
};

static const struct mtk_composite cpu_muxes[] = {
	MUX(CLK_INFRA_CPU_SEL, "infra_cpu_sel", cpu_parents, 0x00, 2, 2),
};

static int clk_mt8127_infracfg_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data;
	int ret;

	clk_data = mtk_alloc_clk_data(CLK_INFRA_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	ret = mtk_clk_register_cpumuxes(&pdev->dev, node, cpu_muxes,
					ARRAY_SIZE(cpu_muxes), clk_data);
	if (ret)
		goto free_clk_data;

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (ret)
		goto unregister_cpumuxes;

	platform_set_drvdata(pdev, clk_data);

	return 0;

unregister_cpumuxes:
	mtk_clk_unregister_cpumuxes(cpu_muxes, ARRAY_SIZE(cpu_muxes), clk_data);
free_clk_data:
	mtk_free_clk_data(clk_data);

	return ret;
}

static void clk_mt8127_infracfg_remove(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);

	of_clk_del_provider(node);
	mtk_clk_unregister_cpumuxes(cpu_muxes, ARRAY_SIZE(cpu_muxes), clk_data);
	mtk_free_clk_data(clk_data);
}

static const struct of_device_id of_match_clk_mt8127_infracfg[] = {
	{ .compatible = "mediatek,mt8127-infracfg" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8127_infracfg);

static struct platform_driver clk_mt8127_infracfg_drv = {
	.driver = {
		.name = "clk-mt8127-infracfg",
		.of_match_table = of_match_clk_mt8127_infracfg,
	},
	.probe = clk_mt8127_infracfg_probe,
	.remove = clk_mt8127_infracfg_remove,
};
module_platform_driver(clk_mt8127_infracfg_drv);

MODULE_DESCRIPTION("MediaTek MT8127 infracfg clock driver");
MODULE_LICENSE("GPL");
