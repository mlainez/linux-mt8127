// SPDX-License-Identifier: GPL-2.0-only
/*
 * Panels driven by a Himax HX8282 LVDS timing controller and source driver.
 *
 * The HX8282 needs no register setup: it is configured by straps on the
 * panel, so the driver only sequences the supplies, the reset and the
 * standby inputs. The timings come from the device tree.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_connector.h>
#include <drm/drm_modes.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>

struct hx8282 {
	struct drm_panel panel;
	struct device *dev;

	struct regulator *iovcc;
	struct regulator *vdd;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *standby_gpio;
	struct gpio_desc *enable_gpio;

	struct drm_display_mode mode;
	u32 bus_format;
	u32 bus_flags;
	enum drm_panel_orientation orientation;
};

static inline struct hx8282 *to_hx8282(struct drm_panel *panel)
{
	return container_of(panel, struct hx8282, panel);
}

static int hx8282_prepare(struct drm_panel *panel)
{
	struct hx8282 *ctx = to_hx8282(panel);
	int ret;

	ret = regulator_enable(ctx->iovcc);
	if (ret) {
		dev_err(ctx->dev, "Failed to enable iovcc: %d\n", ret);
		return ret;
	}
	msleep(20);

	gpiod_direction_output(ctx->reset_gpio, 1);
	gpiod_direction_output(ctx->standby_gpio, 1);
	msleep(50);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->standby_gpio, 0);
	msleep(50);

	ret = regulator_enable(ctx->vdd);
	if (ret) {
		dev_err(ctx->dev, "Failed to enable vdd: %d\n", ret);
		goto err_reset;
	}
	usleep_range(5000, 6000);

	gpiod_direction_output(ctx->enable_gpio, 1);
	msleep(50);

	return 0;

err_reset:
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_disable(ctx->iovcc);
	return ret;
}

static int hx8282_unprepare(struct drm_panel *panel)
{
	struct hx8282 *ctx = to_hx8282(panel);

	gpiod_direction_output(ctx->reset_gpio, 1);
	msleep(85);

	regulator_disable(ctx->vdd);
	usleep_range(5000, 6000);
	gpiod_direction_output(ctx->enable_gpio, 0);
	msleep(50);

	regulator_disable(ctx->iovcc);

	return 0;
}

static int hx8282_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct hx8282 *ctx = to_hx8282(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &ctx->mode);
	if (!mode)
		return -ENOMEM;

	mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	connector->display_info.bus_flags = ctx->bus_flags;
	drm_display_info_set_bus_formats(&connector->display_info,
					 &ctx->bus_format, 1);

	return 1;
}

static enum drm_panel_orientation hx8282_get_orientation(struct drm_panel *panel)
{
	return to_hx8282(panel)->orientation;
}

static const struct drm_panel_funcs hx8282_panel_funcs = {
	.prepare = hx8282_prepare,
	.unprepare = hx8282_unprepare,
	.get_modes = hx8282_get_modes,
	.get_orientation = hx8282_get_orientation,
};

static int hx8282_parse_dt(struct hx8282 *ctx)
{
	struct device_node *np = ctx->dev->of_node;
	int ret;

	ret = of_drm_get_panel_orientation(np, &ctx->orientation);
	if (ret)
		return dev_err_probe(ctx->dev, ret, "Invalid rotation\n");

	ret = of_get_drm_panel_display_mode(np, &ctx->mode, &ctx->bus_flags);
	if (ret)
		return dev_err_probe(ctx->dev, ret, "Invalid panel-timing\n");

	ret = drm_of_lvds_get_data_mapping(np);
	if (ret < 0)
		return dev_err_probe(ctx->dev, ret, "Invalid data-mapping\n");
	ctx->bus_format = ret;

	return 0;
}

static int hx8282_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hx8282 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct hx8282, panel,
				   &hx8282_panel_funcs, DRM_MODE_CONNECTOR_LVDS);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->dev = dev;

	ret = hx8282_parse_dt(ctx);
	if (ret)
		return ret;

	ctx->iovcc = devm_regulator_get(dev, "iovcc");
	if (IS_ERR(ctx->iovcc))
		return dev_err_probe(dev, PTR_ERR(ctx->iovcc),
				     "Failed to get iovcc supply\n");

	ctx->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(ctx->vdd))
		return dev_err_probe(dev, PTR_ERR(ctx->vdd),
				     "Failed to get vdd supply\n");

	/*
	 * The bootloader may have left the panel on: keep the lines as they
	 * are until the first prepare, so that a firmware framebuffer stays
	 * visible meanwhile.
	 */
	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset GPIO\n");

	ctx->standby_gpio = devm_gpiod_get(dev, "standby", GPIOD_ASIS);
	if (IS_ERR(ctx->standby_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->standby_gpio),
				     "Failed to get standby GPIO\n");

	ctx->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_ASIS);
	if (IS_ERR(ctx->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->enable_gpio),
				     "Failed to get enable GPIO\n");

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);
	platform_set_drvdata(pdev, ctx);

	return 0;
}

static void hx8282_remove(struct platform_device *pdev)
{
	struct hx8282 *ctx = platform_get_drvdata(pdev);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id hx8282_of_match[] = {
	{ .compatible = "himax,hx8282" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hx8282_of_match);

static struct platform_driver hx8282_driver = {
	.probe = hx8282_probe,
	.remove = hx8282_remove,
	.driver = {
		.name = "panel-himax-hx8282",
		.of_match_table = hx8282_of_match,
	},
};
module_platform_driver(hx8282_driver);

MODULE_AUTHOR("Marc Lainez <marc.lainez@gmail.com>");
MODULE_DESCRIPTION("Himax HX8282 LVDS panel driver");
MODULE_LICENSE("GPL");
