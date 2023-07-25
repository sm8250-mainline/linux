// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define DSI_NUM_MIN 1

#define mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, cmd, seq...)        \
		do {                                                 \
			mipi_dsi_dcs_write_seq(dsi0, cmd, seq);      \
			mipi_dsi_dcs_write_seq(dsi1, cmd, seq);      \
		} while (0)

struct rm69380_edo_amoled {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi[2];
	const struct panel_desc *desc;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;
	bool prepared;
};

struct panel_desc {
	const struct mipi_dsi_device_info dsi_info;
};

static inline
struct rm69380_edo_amoled *to_rm69380_edo_amoled(struct drm_panel *panel)
{
	return container_of(panel, struct rm69380_edo_amoled, panel);
}

static void rm69380_edo_amoled_reset(struct rm69380_edo_amoled *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(15000, 16000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(30);
}

static int rm69380_edo_amoled_on(struct rm69380_edo_amoled *ctx)
{
	struct mipi_dsi_device *dsi0 = ctx->dsi[0];
	struct mipi_dsi_device *dsi1 = ctx->dsi[1];
	struct device *dev = &dsi0->dev;
	int ret;

	dsi0->mode_flags |= MIPI_DSI_MODE_LPM;
	dsi1->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfe, 0xd4);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x00, 0x80);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfe, 0xd0);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x48, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfe, 0x26);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x75, 0x3f);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x1d, 0x1a);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xfe, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x53, 0x28);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0xc2, 0x08);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x35, 0x00);
	mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, 0x51, 0x07, 0xff);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi0);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi1);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(20);

	ret = mipi_dsi_dcs_set_display_on(dsi0);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	ret = mipi_dsi_dcs_set_display_on(dsi1);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(36);

	return 0;
}

static int rm69380_edo_amoled_off(struct rm69380_edo_amoled *ctx)
{
	struct mipi_dsi_device *dsi0 = ctx->dsi[0];
	struct mipi_dsi_device *dsi1 = ctx->dsi[1];
	struct device *dev = &dsi0->dev;
	int ret;

	dsi0->mode_flags &= ~MIPI_DSI_MODE_LPM;
	dsi1->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi0);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	ret = mipi_dsi_dcs_set_display_off(dsi1);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(35);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi0);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	ret = mipi_dsi_dcs_enter_sleep_mode(dsi1);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(20);

	return 0;
}

static int rm69380_edo_amoled_prepare(struct drm_panel *panel)
{
	struct rm69380_edo_amoled *ctx = to_rm69380_edo_amoled(panel);
	struct device *dev = &ctx->dsi[0]->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_enable(ctx->supply);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulator: %d\n", ret);
		return ret;
	}

	rm69380_edo_amoled_reset(ctx);

	ret = rm69380_edo_amoled_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_disable(ctx->supply);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int rm69380_edo_amoled_unprepare(struct drm_panel *panel)
{
	struct rm69380_edo_amoled *ctx = to_rm69380_edo_amoled(panel);
	struct device *dev = &ctx->dsi[0]->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = rm69380_edo_amoled_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_disable(ctx->supply);

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode rm69380_edo_amoled_mode = {
	.clock = (2560 + 64 + 24 + 76) * (1600 + 20 + 4 + 8) * 60 / 1000,
	.hdisplay = 2560,
	.hsync_start = 2560 + 64,
	.hsync_end = 2560 + 64 + 24,
	.htotal = 2560 + 64 + 24 + 76,
	.vdisplay = 1600,
	.vsync_start = 1600 + 20,
	.vsync_end = 1600 + 20 + 4,
	.vtotal = 1600 + 20 + 4 + 8,
	.width_mm = 248,
	.height_mm = 154,
};

static const struct panel_desc j716f_edo_desc = {
	.dsi_info = {
		.type = "EDO J716F",
		.channel = 0,
		.node = NULL,
	},
};

static int rm69380_edo_amoled_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &rm69380_edo_amoled_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs rm69380_edo_amoled_panel_funcs = {
	.prepare = rm69380_edo_amoled_prepare,
	.unprepare = rm69380_edo_amoled_unprepare,
	.get_modes = rm69380_edo_amoled_get_modes,
};

static int rm69380_edo_amoled_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi0 = bl_get_data(bl);
	struct mipi_dsi_device *dsi1 = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi0->mode_flags &= ~MIPI_DSI_MODE_LPM;
	dsi1->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi0, brightness);
	if (ret < 0)
		return ret;
	ret = mipi_dsi_dcs_set_display_brightness_large(dsi1, brightness);
	if (ret < 0)
		return ret;

	dsi0->mode_flags |= MIPI_DSI_MODE_LPM;
	dsi1->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

// TODO: Check if /sys/class/backlight/.../actual_brightness actually returns
// correct values. If not, remove this function.
static int rm69380_edo_amoled_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops rm69380_edo_amoled_bl_ops = {
	.update_status = rm69380_edo_amoled_bl_update_status,
	.get_brightness = rm69380_edo_amoled_bl_get_brightness,
};

static struct backlight_device *
rm69380_edo_amoled_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 2047,
		.max_brightness = 2047,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &rm69380_edo_amoled_bl_ops, &props);
}

static int rm69380_edo_amoled_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi1;
	struct mipi_dsi_host *dsi1_host;
	struct rm69380_edo_amoled *ctx;
	const struct mipi_dsi_device_info *info;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supply = devm_regulator_get(dev, "vddio");
	if (IS_ERR(ctx->supply))
		return dev_err_probe(dev, PTR_ERR(ctx->supply),
				     "Failed to get vddio regulator\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	info = &ctx->desc->dsi_info;

	dsi1 = of_graph_get_remote_node(dsi->dev.of_node, 1, -1);
	if (!dsi1) {
		dev_err(dev, "cannot get secondary DSI node.\n");
		return -ENODEV;
	}

	dsi1_host = of_find_mipi_dsi_host_by_node(dsi1);
	of_node_put(dsi1);
	if (!dsi1_host)
		return dev_err_probe(dev, -EPROBE_DEFER, "cannot get secondary DSI host\n");

	ctx->dsi[1] = mipi_dsi_device_register_full(dsi1_host, info);
	if (!ctx->dsi[1]) {
		dev_err(dev, "cannot get secondary DSI device\n");
		return -ENODEV;
	}

	ctx->dsi[0] = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dsi[0]->lanes = 4;
	ctx->dsi[0]->format = MIPI_DSI_FMT_RGB888;
	ctx->dsi[0]->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;
	ctx->dsi[1]->lanes = 4;
	ctx->dsi[1]->format = MIPI_DSI_FMT_RGB888;
	ctx->dsi[1]->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ctx->panel.prepare_prev_first = true;

	drm_panel_init(&ctx->panel, dev, &rm69380_edo_amoled_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->panel.backlight = rm69380_edo_amoled_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(ctx->dsi[0]);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}
	ret = mipi_dsi_attach(ctx->dsi[1]);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void rm69380_edo_amoled_remove(struct mipi_dsi_device *dsi)
{
	struct rm69380_edo_amoled *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id rm69380_edo_amoled_of_match[] = {
	{ .compatible = "lenovo,j716f-edo-rm69380" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rm69380_edo_amoled_of_match);

static struct mipi_dsi_driver rm69380_edo_amoled_driver = {
	.probe = rm69380_edo_amoled_probe,
	.remove = rm69380_edo_amoled_remove,
	.driver = {
		.name = "panel-rm69380-edo-amoled",
		.of_match_table = rm69380_edo_amoled_of_match,
	},
};
module_mipi_dsi_driver(rm69380_edo_amoled_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for rm69380 amoled wqxga cmd mode dsi edo panel");
MODULE_LICENSE("GPL");
