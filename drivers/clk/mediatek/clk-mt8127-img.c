// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Marc Lainez <marc.lainez@gmail.com>
 *
 * MediaTek MT8127 IMGSYS: the clock gates of the camera ISP, the sensor
 * interface and the JPEG encoder.
 */

#include <dt-bindings/clock/mt8127-clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs img_cg_regs = {
	.set_ofs = 0x0004,
	.clr_ofs = 0x0008,
	.sta_ofs = 0x0000,
};

#define GATE_IMG(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &img_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate img_clks[] = {
	GATE_IMG(CLK_IMG_LARB2_SMI, "img_larb2_smi", "mm_sel", 0),
	GATE_IMG(CLK_IMG_CAM_SMI, "img_cam_smi", "mm_sel", 5),
	GATE_IMG(CLK_IMG_CAM_CAM, "img_cam_cam", "mm_sel", 6),
	GATE_IMG(CLK_IMG_SEN_TG, "img_sen_tg", "camtg_sel", 7),
	GATE_IMG(CLK_IMG_SEN_CAM, "img_sen_cam", "camtg_sel", 8),
	GATE_IMG(CLK_IMG_VENC_JPGENC, "img_venc_jpgenc", "mm_sel", 9),
};

static const struct mtk_clk_desc img_desc = {
	.clks = img_clks,
	.num_clks = ARRAY_SIZE(img_clks),
};

static const struct of_device_id of_match_clk_mt8127_img[] = {
	{ .compatible = "mediatek,mt8127-imgsys", .data = &img_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8127_img);

static struct platform_driver clk_mt8127_img_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8127-img",
		.of_match_table = of_match_clk_mt8127_img,
	},
};
module_platform_driver(clk_mt8127_img_drv);

MODULE_DESCRIPTION("MediaTek MT8127 imgsys clock driver");
MODULE_LICENSE("GPL");
