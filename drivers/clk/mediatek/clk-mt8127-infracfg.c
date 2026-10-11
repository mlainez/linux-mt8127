// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Marc Lainez <marc.lainez@gmail.com>
 *
 * MediaTek MT8127 INFRACFG: the CPU clock mux and the infrastructure clock
 * gates.
 */

#include <dt-bindings/clock/mt8127-clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "clk-cpumux.h"
#include "clk-gate.h"
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

static const struct mtk_gate_regs infra_cg_regs = {
	.set_ofs = 0x0040,
	.clr_ofs = 0x0044,
	.sta_ofs = 0x0048,
};

#define GATE_ICG(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &infra_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_ICG_FLAGS(_id, _name, _parent, _shift, _flags)		\
	GATE_MTK_FLAGS(_id, _name, _parent, &infra_cg_regs, _shift,	\
		       &mtk_clk_gate_ops_setclr, _flags)

/* The PMIC wrapper must keep running: the CPU supply sits behind it. */
static const struct mtk_gate infra_clks[] = {
	GATE_ICG(CLK_INFRA_SMI, "infra_smi", "mm_sel", 1),
	GATE_ICG(CLK_INFRA_AUDIO, "infra_audio", "aud_intbus_sel", 5),
	GATE_ICG(CLK_INFRA_EFUSE, "infra_efuse", "axi_sel", 6),
	GATE_ICG(CLK_INFRA_M4U, "infra_m4u", "mm_sel", 8),
	GATE_ICG(CLK_INFRA_CONNMCU, "infra_connmcu", "axi_sel", 12),
	GATE_ICG(CLK_INFRA_TRNG, "infra_trng", "axi_sel", 13),
	GATE_ICG(CLK_INFRA_KP, "infra_kp", "axi_sel", 16),
	GATE_ICG(CLK_INFRA_CEC, "infra_cec", "clk26m", 18),
	GATE_ICG(CLK_INFRA_IRRX, "infra_irrx", "axi_sel", 19),
	GATE_ICG_FLAGS(CLK_INFRA_PMICSPI, "infra_pmicspi", "pmicspi_sel", 22,
		       CLK_IS_CRITICAL),
	GATE_ICG_FLAGS(CLK_INFRA_PMICWRAP, "infra_pmicwrap", "axi_sel", 23,
		       CLK_IS_CRITICAL),
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

	ret = mtk_clk_register_gates(&pdev->dev, node, infra_clks,
				     ARRAY_SIZE(infra_clks), clk_data);
	if (ret)
		goto unregister_cpumuxes;

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (ret)
		goto unregister_gates;

	platform_set_drvdata(pdev, clk_data);

	return 0;

unregister_gates:
	mtk_clk_unregister_gates(infra_clks, ARRAY_SIZE(infra_clks), clk_data);
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
	mtk_clk_unregister_gates(infra_clks, ARRAY_SIZE(infra_clks), clk_data);
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
