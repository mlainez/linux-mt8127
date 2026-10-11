// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Marc Lainez <marc.lainez@gmail.com>
 *
 * MediaTek MT8127 MFGCFG: the clock gate of the GPU.
 */

#include <dt-bindings/clock/mt8127-clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs mfg_cg_regs = {
	.set_ofs = 0x0004,
	.clr_ofs = 0x0008,
	.sta_ofs = 0x0000,
};

static const struct mtk_gate mfg_clks[] = {
	GATE_MTK(CLK_MFG_G3D, "mfg_g3d", "mfg_sel", &mfg_cg_regs, 0,
		 &mtk_clk_gate_ops_setclr),
};

static const struct mtk_clk_desc mfg_desc = {
	.clks = mfg_clks,
	.num_clks = ARRAY_SIZE(mfg_clks),
};

static const struct of_device_id of_match_clk_mt8127_mfg[] = {
	{ .compatible = "mediatek,mt8127-mfgcfg", .data = &mfg_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8127_mfg);

static struct platform_driver clk_mt8127_mfg_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8127-mfg",
		.of_match_table = of_match_clk_mt8127_mfg,
	},
};
module_platform_driver(clk_mt8127_mfg_drv);

MODULE_DESCRIPTION("MediaTek MT8127 mfgcfg clock driver");
MODULE_LICENSE("GPL");
