// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Marc Lainez <marc.lainez@gmail.com>
 *
 * MediaTek MT8127 AUDSYS: the clock gates of AUDIO_TOP_CON0, at the start of
 * the audio front end registers. A set bit gates the clock.
 */

#include <dt-bindings/clock/mt8127-clk.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs audio_cg_regs = {
	.set_ofs = 0x0000,
	.clr_ofs = 0x0000,
	.sta_ofs = 0x0000,
};

#define GATE_AUDIO(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &audio_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

static const struct mtk_gate audio_clks[] = {
	GATE_AUDIO(CLK_AUD_AFE, "audio_afe", "aud_intbus_sel", 2),
	GATE_AUDIO(CLK_AUD_I2S, "audio_i2s", "audio_sel", 6),
	GATE_AUDIO(CLK_AUD_APLL_TUNER, "audio_apll_tuner", "apll_sel", 19),
	GATE_AUDIO(CLK_AUD_HDMI, "audio_hdmi", "apll_sel", 20),
	GATE_AUDIO(CLK_AUD_SPDF, "audio_spdf", "apll_sel", 21),
	GATE_AUDIO(CLK_AUD_SPDF2, "audio_spdf2", "apll_sel", 22),
};

static const struct mtk_clk_desc audio_desc = {
	.clks = audio_clks,
	.num_clks = ARRAY_SIZE(audio_clks),
};

static int clk_mt8127_aud_probe(struct platform_device *pdev)
{
	struct clk *bus_clk;
	int ret;

	/* The registers are only accessible while the bus clock runs. */
	bus_clk = devm_clk_get_optional_enabled(&pdev->dev, NULL);
	if (IS_ERR(bus_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(bus_clk),
				     "failed to enable the bus clock\n");

	ret = mtk_clk_simple_probe(pdev);
	if (ret)
		return ret;

	ret = devm_of_platform_populate(&pdev->dev);
	if (ret) {
		mtk_clk_simple_remove(pdev);
		return ret;
	}

	return 0;
}

static void clk_mt8127_aud_remove(struct platform_device *pdev)
{
	of_platform_depopulate(&pdev->dev);
	mtk_clk_simple_remove(pdev);
}

static const struct of_device_id of_match_clk_mt8127_aud[] = {
	{ .compatible = "mediatek,mt8127-audsys", .data = &audio_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8127_aud);

static struct platform_driver clk_mt8127_aud_drv = {
	.probe = clk_mt8127_aud_probe,
	.remove = clk_mt8127_aud_remove,
	.driver = {
		.name = "clk-mt8127-aud",
		.of_match_table = of_match_clk_mt8127_aud,
	},
};
module_platform_driver(clk_mt8127_aud_drv);

MODULE_DESCRIPTION("MediaTek MT8127 audsys clock driver");
MODULE_LICENSE("GPL");
