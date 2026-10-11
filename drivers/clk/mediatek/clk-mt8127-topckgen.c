// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Marc Lainez <marc.lainez@gmail.com>
 *
 * MediaTek MT8127 TOPCKGEN: the clock muxes of the buses and of the
 * peripherals, and the fixed dividers of the PLLs that feed them.
 */

#include <dt-bindings/clock/mt8127-clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"

static DEFINE_SPINLOCK(mt8127_top_clk_lock);

/*
 * Clocks generated outside of the clock controllers. The DRAM PLL runs at the
 * rate the preloader programs (266 MHz for the 1066 Mbps DDR3 of the stock
 * firmware), the VENCPLL lives in the DRAM PHY and is not measured; its rate is
 * the one of the MT2701, which has the same multimedia clock tree.
 */
static const struct mtk_fixed_clk top_fixed_clks[] = {
	FIXED_CLK(CLK_TOP_DMPLL, "dmpll_ck", "clk26m", 266 * MHZ),
	FIXED_CLK(CLK_TOP_VENCPLL, "vencpll_ck", "clk26m", 295750000),
	FIXED_CLK(CLK_TOP_DPI, "dpi_ck", "clk26m", 108 * MHZ),
	FIXED_CLK(CLK_TOP_DSI0_LNTC_DSI, "dsi0_lntc_dsi", "clk26m", 143 * MHZ),
};

static const struct mtk_fixed_factor top_divs[] = {
	FACTOR(CLK_TOP_SYSPLL_D2, "syspll_d2", "mainpll", 1, 2),
	FACTOR(CLK_TOP_SYSPLL_D3, "syspll_d3", "mainpll", 1, 3),
	FACTOR(CLK_TOP_SYSPLL_D5, "syspll_d5", "mainpll", 1, 5),
	FACTOR(CLK_TOP_SYSPLL_D7, "syspll_d7", "mainpll", 1, 7),
	FACTOR(CLK_TOP_SYSPLL1_D2, "syspll1_d2", "syspll_d2", 1, 2),
	FACTOR(CLK_TOP_SYSPLL1_D4, "syspll1_d4", "syspll_d2", 1, 4),
	FACTOR(CLK_TOP_SYSPLL1_D8, "syspll1_d8", "syspll_d2", 1, 8),
	FACTOR(CLK_TOP_SYSPLL1_D16, "syspll1_d16", "syspll_d2", 1, 16),
	FACTOR(CLK_TOP_SYSPLL2_D2, "syspll2_d2", "syspll_d3", 1, 2),
	FACTOR(CLK_TOP_SYSPLL2_D4, "syspll2_d4", "syspll_d3", 1, 4),
	FACTOR(CLK_TOP_SYSPLL2_D8, "syspll2_d8", "syspll_d3", 1, 8),
	FACTOR(CLK_TOP_SYSPLL3_D2, "syspll3_d2", "syspll_d5", 1, 2),
	FACTOR(CLK_TOP_SYSPLL3_D4, "syspll3_d4", "syspll_d5", 1, 4),
	FACTOR(CLK_TOP_SYSPLL4_D2, "syspll4_d2", "syspll_d7", 1, 2),
	FACTOR(CLK_TOP_SYSPLL4_D4, "syspll4_d4", "syspll_d7", 1, 4),

	FACTOR(CLK_TOP_UNIVPLL_D2, "univpll_d2", "univpll", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL_D3, "univpll_d3", "univpll", 1, 3),
	FACTOR(CLK_TOP_UNIVPLL_D5, "univpll_d5", "univpll", 1, 5),
	FACTOR(CLK_TOP_UNIVPLL_D26, "univpll_d26", "univpll", 1, 26),
	FACTOR(CLK_TOP_UNIVPLL1_D2, "univpll1_d2", "univpll_d2", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL1_D4, "univpll1_d4", "univpll_d2", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL1_D8, "univpll1_d8", "univpll_d2", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL2_D2, "univpll2_d2", "univpll_d3", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL2_D4, "univpll2_d4", "univpll_d3", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL2_D8, "univpll2_d8", "univpll_d3", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL3_D2, "univpll3_d2", "univpll_d5", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL3_D4, "univpll3_d4", "univpll_d5", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL3_D8, "univpll3_d8", "univpll_d5", 1, 8),

	FACTOR(CLK_TOP_MSDCPLL_D2, "msdcpll_d2", "msdcpll", 1, 2),
	FACTOR(CLK_TOP_MMPLL_D2, "mmpll_d2", "mmpll", 1, 2),
	FACTOR(CLK_TOP_DMPLL_D2, "dmpll_d2", "dmpll_ck", 1, 2),
	FACTOR(CLK_TOP_DMPLL_D4, "dmpll_d4", "dmpll_ck", 1, 4),
	FACTOR(CLK_TOP_DMPLL_X2, "dmpll_x2_ck", "dmpll_ck", 2, 1),
	FACTOR(CLK_TOP_TVDPLL_D2, "tvdpll_d2", "tvdpll", 1, 2),
	FACTOR(CLK_TOP_TVDPLL_D4, "tvdpll_d4", "tvdpll", 1, 4),
	FACTOR(CLK_TOP_LVDSPLL_D2, "lvdspll_d2", "lvdspll", 1, 2),
	FACTOR(CLK_TOP_LVDSPLL_D4, "lvdspll_d4", "lvdspll", 1, 4),
	FACTOR(CLK_TOP_LVDSPLL_D8, "lvdspll_d8", "lvdspll", 1, 8),
	FACTOR(CLK_TOP_MIPIPLL, "mipipll", "dpi_ck", 1, 1),
	FACTOR(CLK_TOP_MIPIPLL_D2, "mipipll_d2", "dpi_ck", 1, 2),
	FACTOR(CLK_TOP_MIPIPLL_D4, "mipipll_d4", "dpi_ck", 1, 4),
	FACTOR(CLK_TOP_HDMIPLL, "hdmipll_ck", "hdmitx_dig_cts", 1, 1),
	FACTOR(CLK_TOP_HDMIPLL_D2, "hdmipll_d2", "hdmitx_dig_cts", 1, 2),
	FACTOR(CLK_TOP_HDMIPLL_D3, "hdmipll_d3", "hdmitx_dig_cts", 1, 3),
	FACTOR(CLK_TOP_AUDPLL_D4, "audpll_d4", "audpll", 1, 4),
	FACTOR(CLK_TOP_AUDPLL_D8, "audpll_d8", "audpll", 1, 8),
	FACTOR(CLK_TOP_AUDPLL_D16, "audpll_d16", "audpll", 1, 16),
	FACTOR(CLK_TOP_AUDPLL_D24, "audpll_d24", "audpll", 1, 24),
	FACTOR(CLK_TOP_32K_INTERNAL, "32k_internal", "clk26m", 1, 793),
	FACTOR(CLK_TOP_32K_EXTERNAL, "32k_external", "rtc32k", 1, 1),
};

static const char * const mm_parents[] = {
	"clk26m",
	"vencpll_ck",
	"syspll1_d2",
	"syspll1_d4",
	"univpll_d5",
	"univpll1_d2",
	"univpll2_d2",
	"dmpll_ck"
};

static const char * const ddrphycfg_parents[] = {
	"clk26m",
	"syspll1_d8"
};

static const char * const mem_parents[] = {
	"clk26m",
	"dmpll_ck"
};

static const char * const axi_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll_d5",
	"syspll1_d4",
	"univpll_d5",
	"univpll2_d2",
	"dmpll_ck",
	"dmpll_d2"
};

static const char * const camtg_parents[] = {
	"clk26m",
	"univpll_d26",
	"univpll2_d2",
	"syspll3_d2",
	"syspll3_d4",
	"msdcpll_d2",
	"mmpll_d2"
};

static const char * const mfg_parents[] = {
	"clk26m",
	"mmpll",
	"dmpll_x2_ck",
	"msdcpll",
	"clk26m",
	"syspll_d3",
	"univpll_d3",
	"univpll1_d2"
};

static const char * const vdec_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll_d5",
	"syspll1_d4",
	"univpll_d5",
	"univpll2_d2",
	"univpll2_d4",
	"msdcpll_d2",
	"mmpll_d2"
};

static const char * const pwm_parents[] = {
	"clk26m",
	"univpll2_d4",
	"univpll3_d2",
	"univpll1_d4"
};

static const char * const msdc30_parents[] = {
	"clk26m",
	"msdcpll_d2",
	"syspll2_d2",
	"syspll1_d4",
	"univpll1_d4",
	"univpll2_d4"
};

static const char * const usb20_parents[] = {
	"clk26m",
	"univpll1_d8",
	"univpll3_d4"
};

static const char * const spi_parents[] = {
	"clk26m",
	"syspll3_d2",
	"syspll4_d2",
	"univpll2_d4",
	"univpll1_d8"
};

static const char * const uart_parents[] = {
	"clk26m",
	"univpll2_d8"
};

static const char * const aud_intbus_parents[] = {
	"clk26m",
	"syspll1_d4",
	"syspll3_d2",
	"syspll4_d2",
	"univpll3_d2",
	"univpll2_d4"
};

static const char * const audio_parents[] = {
	"clk26m",
	"syspll1_d16"
};

static const char * const dpi1_parents[] = {
	"clk26m",
	"tvdpll",
	"tvdpll_d2",
	"tvdpll_d4"
};

static const char * const dpi0_parents[] = {
	"clk26m",
	"mipipll",
	"mipipll_d2",
	"mipipll_d4",
	"lvdspll",
	"lvdspll_d2",
	"lvdspll_d4",
	"lvdspll_d8"
};

static const char * const scp_parents[] = {
	"clk26m",
	"syspll1_d8",
	"dmpll_d2",
	"dmpll_d4"
};

static const char * const pmicspi_parents[] = {
	"clk26m",
	"syspll1_d8",
	"syspll2_d4",
	"syspll4_d2",
	"syspll3_d4",
	"syspll2_d8",
	"syspll1_d16",
	"univpll3_d4",
	"univpll_d26",
	"dmpll_d2",
	"dmpll_d4"
};

static const char * const dpilvds_parents[] = {
	"clk26m",
	"lvdspll",
	"lvdspll_d2",
	"lvdspll_d4",
	"lvdspll_d8",
	"fpc_ck",
	"clk26m",
	"clk26m"
};

static const char * const apll_parents[] = {
	"clk26m",
	"audpll",
	"audpll_d4",
	"audpll_d8",
	"audpll_d16",
	"audpll_d24",
	"clk26m",
	"clk26m"
};

static const char * const hdmi_parents[] = {
	"clk26m",
	"hdmipll_ck",
	"hdmipll_d2",
	"hdmipll_d3"
};

static const char * const tve_parents[] = {
	"clk26m",
	"mipipll",
	"mipipll_d2",
	"mipipll_d4",
	"clk26m",
	"tvdpll",
	"tvdpll_d2",
	"tvdpll_d4"
};

static const char * const eth_50m_parents[] = {
	"clk26m",
	"syspll3_d4",
	"univpll2_d8",
	"lvdspll_eth",
	"univpll_d26",
	"syspll2_d8",
	"syspll4_d4",
	"univpll3_d8"
};

static const char * const nfi2x_parents[] = {
	"clk26m",
	"syspll2_d2",
	"syspll_d7",
	"univpll3_d2",
	"syspll2_d4",
	"univpll3_d4",
	"syspll4_d4",
	"clk26m"
};

static const char * const rtc_parents[] = {
	"32k_internal",
	"32k_external",
	"clk26m",
	"univpll3_d8"
};

/*
 * CLK_CFG_0..6: four muxes per register, each with a power down bit at the top
 * of its byte. The bus, DRAM, PMIC wrapper, SPM and RTC clocks must keep
 * running.
 */
static const struct mtk_composite top_muxes[] = {
	/* CLK_CFG_0 */
	MUX_GATE_FLAGS(CLK_TOP_AXI_SEL, "axi_sel", axi_parents,
		       0x0040, 0, 3, 7, CLK_IS_CRITICAL),
	MUX_GATE_FLAGS(CLK_TOP_MEM_SEL, "mem_sel", mem_parents,
		       0x0040, 8, 1, 15, CLK_IS_CRITICAL),
	MUX_GATE_FLAGS(CLK_TOP_DDRPHYCFG_SEL, "ddrphycfg_sel", ddrphycfg_parents,
		       0x0040, 16, 1, 23, CLK_IS_CRITICAL),
	MUX_GATE(CLK_TOP_MM_SEL, "mm_sel", mm_parents, 0x0040, 24, 3, 31),
	/* CLK_CFG_1 */
	MUX_GATE(CLK_TOP_PWM_SEL, "pwm_sel", pwm_parents, 0x0050, 0, 2, 7),
	MUX_GATE(CLK_TOP_VDEC_SEL, "vdec_sel", vdec_parents, 0x0050, 8, 4, 15),
	MUX_GATE(CLK_TOP_MFG_SEL, "mfg_sel", mfg_parents, 0x0050, 16, 3, 23),
	MUX_GATE(CLK_TOP_CAMTG_SEL, "camtg_sel", camtg_parents,
		 0x0050, 24, 3, 31),
	/* CLK_CFG_2 */
	MUX_GATE(CLK_TOP_UART_SEL, "uart_sel", uart_parents, 0x0060, 0, 1, 7),
	MUX_GATE(CLK_TOP_SPI_SEL, "spi_sel", spi_parents, 0x0060, 8, 3, 15),
	MUX_GATE(CLK_TOP_USB20_SEL, "usb20_sel", usb20_parents,
		 0x0060, 16, 2, 23),
	MUX_GATE(CLK_TOP_MSDC30_0_SEL, "msdc30_0_sel", msdc30_parents,
		 0x0060, 24, 3, 31),
	/* CLK_CFG_3 */
	MUX_GATE(CLK_TOP_MSDC30_1_SEL, "msdc30_1_sel", msdc30_parents,
		 0x0070, 0, 3, 7),
	MUX_GATE(CLK_TOP_MSDC30_2_SEL, "msdc30_2_sel", msdc30_parents,
		 0x0070, 8, 3, 15),
	MUX_GATE(CLK_TOP_AUDIO_SEL, "audio_sel", audio_parents,
		 0x0070, 16, 1, 23),
	MUX_GATE(CLK_TOP_AUD_INTBUS_SEL, "aud_intbus_sel", aud_intbus_parents,
		 0x0070, 24, 3, 31),
	/* CLK_CFG_4 */
	MUX_GATE_FLAGS(CLK_TOP_PMICSPI_SEL, "pmicspi_sel", pmicspi_parents,
		       0x0080, 0, 4, 7, CLK_IS_CRITICAL),
	MUX_GATE_FLAGS(CLK_TOP_SCP_SEL, "scp_sel", scp_parents,
		       0x0080, 8, 2, 15, CLK_IS_CRITICAL),
	MUX_GATE(CLK_TOP_DPI0_SEL, "dpi0_sel", dpi0_parents, 0x0080, 16, 3, 23),
	MUX_GATE(CLK_TOP_DPI1_SEL, "dpi1_sel", dpi1_parents, 0x0080, 24, 2, 31),
	/* CLK_CFG_5 */
	MUX_GATE(CLK_TOP_TVE_SEL, "tve_sel", tve_parents, 0x0090, 0, 3, 7),
	MUX_GATE(CLK_TOP_HDMI_SEL, "hdmi_sel", hdmi_parents, 0x0090, 8, 2, 15),
	MUX_GATE(CLK_TOP_APLL_SEL, "apll_sel", apll_parents, 0x0090, 16, 3, 23),
	MUX_GATE(CLK_TOP_DPILVDS_SEL, "dpilvds_sel", dpilvds_parents,
		 0x0090, 24, 3, 31),
	/* CLK_CFG_6 */
	MUX_GATE_FLAGS(CLK_TOP_RTC_SEL, "rtc_sel", rtc_parents,
		       0x00a0, 0, 2, 7, CLK_IS_CRITICAL),
	MUX_GATE(CLK_TOP_NFI2X_SEL, "nfi2x_sel", nfi2x_parents,
		 0x00a0, 8, 3, 15),
	MUX_GATE(CLK_TOP_ETH_50M_SEL, "eth_50m_sel", eth_50m_parents,
		 0x00a0, 16, 3, 23),
};

static const struct mtk_clk_desc topck_desc = {
	.fixed_clks = top_fixed_clks,
	.num_fixed_clks = ARRAY_SIZE(top_fixed_clks),
	.factor_clks = top_divs,
	.num_factor_clks = ARRAY_SIZE(top_divs),
	.composite_clks = top_muxes,
	.num_composite_clks = ARRAY_SIZE(top_muxes),
	.clk_lock = &mt8127_top_clk_lock,
};

static const struct of_device_id of_match_clk_mt8127_topckgen[] = {
	{ .compatible = "mediatek,mt8127-topckgen", .data = &topck_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8127_topckgen);

static struct platform_driver clk_mt8127_topckgen_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8127-topckgen",
		.of_match_table = of_match_clk_mt8127_topckgen,
	},
};
module_platform_driver(clk_mt8127_topckgen_drv);

MODULE_DESCRIPTION("MediaTek MT8127 topckgen clock driver");
MODULE_LICENSE("GPL");
