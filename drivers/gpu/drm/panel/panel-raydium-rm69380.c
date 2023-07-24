// SPDX-License-Identifier: GPL-2.0-only
/*
 * Raydium RM69380 panel driver
 *
 * Copyright (c) 2023 David Wronek <davidwronek@gmail.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define DSI_NUM_MIN 1

#define mipi_dsi_dual_dcs_write_seq(dsi0, dsi1, cmd, seq...)        \
		do {                                                 \
			mipi_dsi_dcs_write_seq(dsi0, cmd, seq);      \
			mipi_dsi_dcs_write_seq(dsi1, cmd, seq);      \
		} while (0)

struct panel_info {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi[2];
	const struct panel_desc *desc;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[2];

	bool prepared;
};


struct panel_desc {
	unsigned int width_mm;
	unsigned int height_mm;

	unsigned int bpc;
	unsigned int lanes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;

	const struct drm_display_mode *modes;
	unsigned int num_modes;
	const struct mipi_dsi_device_info dsi_info;
	int (*init_sequence)(struct panel_info *pinfo);

	bool is_dual_dsi;
	bool has_dcs_backlight;
};

static inline struct panel_info *to_panel_info(struct drm_panel *panel)
{
	return container_of(panel, struct panel_info, panel);
}

static int j716f_edo_init_sequence(struct panel_info *pinfo)
{
	struct mipi_dsi_device *dsi0 = pinfo->dsi[0];

	pinfo->dsi[0]->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq(dsi0, 0xfe, 0xd4);
	mipi_dsi_dcs_write_seq(dsi0, 0x00, 0x80);
	mipi_dsi_dcs_write_seq(dsi0, 0xfe, 0xd0);
	mipi_dsi_dcs_write_seq(dsi0, 0x48, 0x00);
	mipi_dsi_dcs_write_seq(dsi0, 0xfe, 0x26);
	mipi_dsi_dcs_write_seq(dsi0, 0x75, 0x3f);
	mipi_dsi_dcs_write_seq(dsi0, 0x1d, 0x1a);
	mipi_dsi_dcs_write_seq(dsi0, 0xfe, 0x00);
	mipi_dsi_dcs_write_seq(dsi0, 0x53, 0x28);
	mipi_dsi_dcs_write_seq(dsi0, 0xc2, 0x08);
	mipi_dsi_dcs_write_seq(dsi0, 0x35, 0x00);
	mipi_dsi_dcs_write_seq(dsi0, 0x51, 0x07, 0xff);
	mipi_dsi_dcs_write_seq(dsi0, 0x11, 0x00);
	msleep(20);
	mipi_dsi_dcs_write_seq(dsi0, 0x29, 0x00);
	msleep(36);

	return 0;
}

static const struct drm_display_mode j716f_edo_modes[] = {
	{
		.clock = (1280 + 32 + 12 + 38) * (1600 + 20 + 4 + 8) * 60 / 1000,
		.hdisplay = 1280,
		.hsync_start = 1280 + 32,
		.hsync_end = 1280 + 32 + 12,
		.htotal = 1280 + 32 + 12 + 38,
		.vdisplay = 1600,
		.vsync_start = 1600 + 20,
		.vsync_end = 1600 + 20 + 4,
		.vtotal = 1600 + 20 + 4 + 8,
	},
};

static const struct panel_desc j716f_edo_desc = {
	.modes = j716f_edo_modes,
	.num_modes = ARRAY_SIZE(j716f_edo_modes),
	.dsi_info = {
		.type = "EDO J716F",
		.channel = 0,
		.node = NULL,
	},
	.width_mm = 248,
	.height_mm = 154,
	.bpc = 8,
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.init_sequence = j716f_edo_init_sequence,
	.is_dual_dsi = false,
	.has_dcs_backlight = false,
};

static void rm69380_reset(struct panel_info *pinfo)
{
	gpiod_set_value_cansleep(pinfo->reset_gpio, 0);
	usleep_range(15000, 16000);
	gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(pinfo->reset_gpio, 0);
	msleep(30);
}

static int rm69380_prepare(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int ret;

	if (pinfo->prepared)
		return 0;


	ret = regulator_bulk_enable(ARRAY_SIZE(pinfo->supplies), pinfo->supplies);
	if (ret < 0) {
	    dev_err(panel->dev, "failed to enable regulators: %d\n", ret);
	    return ret;
	}

	rm69380_reset(pinfo);

	ret = pinfo->desc->init_sequence(pinfo);
	if (ret < 0) {
		dev_err(panel->dev, "failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(pinfo->supplies), pinfo->supplies);
		return ret;
	}

	pinfo->prepared = true;

	return 0;
}

static int rm69380_disable(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int i, ret;

	for (i = 0; i < DSI_NUM_MIN + pinfo->desc->is_dual_dsi; i++) {
		ret = mipi_dsi_dcs_set_display_off(pinfo->dsi[i]);
		if (ret < 0)
			dev_err(&pinfo->dsi[i]->dev, "failed to set display off: %d\n", ret);
	}
	msleep(35);

	for (i = 0; i < DSI_NUM_MIN + pinfo->desc->is_dual_dsi; i++) {
		ret = mipi_dsi_dcs_enter_sleep_mode(pinfo->dsi[i]);
		if (ret < 0)
			dev_err(&pinfo->dsi[i]->dev, "failed to enter sleep mode: %d\n", ret);
	}
	msleep(20);

	return 0;
}

static int rm69380_unprepare(struct drm_panel *panel)
{
	struct panel_info *pinfo = to_panel_info(panel);

	if (!pinfo->prepared)
		return 0;

	gpiod_set_value_cansleep(pinfo->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(pinfo->supplies), pinfo->supplies);

	pinfo->prepared = false;
	return 0;
}

static void rm69380_remove(struct mipi_dsi_device *dsi)
{
	struct panel_info *pinfo = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(pinfo->dsi[0]);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI0 host: %d\n", ret);

	if (pinfo->desc->is_dual_dsi) {
		ret = mipi_dsi_detach(pinfo->dsi[1]);
		if (ret < 0)
			dev_err(&pinfo->dsi[1]->dev, "Failed to detach from DSI1 host: %d\n", ret);
		mipi_dsi_device_unregister(pinfo->dsi[1]);
	}

	drm_panel_remove(&pinfo->panel);
}


static int rm69380_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct panel_info *pinfo = to_panel_info(panel);
	int i;

	for (i = 0; i < pinfo->desc->num_modes; i++) {
		const struct drm_display_mode *m = &pinfo->desc->modes[i];
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(connector->dev, m);
		if (!mode) {
			dev_err(panel->dev, "Failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
			return -ENOMEM;
		}

		mode->type = DRM_MODE_TYPE_DRIVER;
		if (i == 0)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.width_mm = pinfo->desc->width_mm;
	connector->display_info.height_mm = pinfo->desc->height_mm;
	connector->display_info.bpc = pinfo->desc->bpc;

	return pinfo->desc->num_modes;
}

static const struct drm_panel_funcs rm69380_panel_funcs = {
	.disable = rm69380_disable,
	.prepare = rm69380_prepare,
	.unprepare = rm69380_unprepare,
	.get_modes = rm69380_get_modes,
};

/*static int rm69380_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int rm69380_bl_get_brightness(struct backlight_device *bl)
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

static const struct backlight_ops rm69380_bl_ops = {
	.update_status = rm69380_bl_update_status,
	.get_brightness = rm69380_bl_get_brightness,
};

static struct backlight_device *rm69380_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 512,
		.max_brightness = 2047,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &rm69380_bl_ops, &props);
}*/

static int rm69380_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *dsi1;
	struct mipi_dsi_host *dsi1_host;
	struct panel_info *pinfo;
	const struct mipi_dsi_device_info *info;
	int i, ret;

	pinfo = devm_kzalloc(dev, sizeof(*pinfo), GFP_KERNEL);
	if (!pinfo)
		return -ENOMEM;

	pinfo->supplies[0].supply = "vddio";
	pinfo->supplies[1].supply = "avdd";

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(pinfo->supplies),
    	              pinfo->supplies);
	if (ret < 0)
    	return dev_err_probe(dev, ret, "failed to get regulators\n");

	pinfo->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(pinfo->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(pinfo->reset_gpio), "failed to get reset gpio\n");

	pinfo->desc = of_device_get_match_data(dev);
	if (!pinfo->desc)
		return -ENODEV;

	pinfo->panel.prepare_prev_first = true;

	/* If the panel is dual dsi, register DSI1 */
	if (pinfo->desc->is_dual_dsi) {
		info = &pinfo->desc->dsi_info;

		dsi1 = of_graph_get_remote_node(dsi->dev.of_node, 1, -1);
		if (!dsi1) {
			dev_err(dev, "Cannot get secondary DSI node.\n");
			return -ENODEV;
		}

		dsi1_host = of_find_mipi_dsi_host_by_node(dsi1);
		of_node_put(dsi1);
		if (!dsi1_host)
			return dev_err_probe(dev, -EPROBE_DEFER, "Cannot get secondary DSI host\n");

		pinfo->dsi[1] = mipi_dsi_device_register_full(dsi1_host, info);
		if (!pinfo->dsi[1]) {
			dev_err(dev, "Cannot get secondary DSI device\n");
			return -ENODEV;
		}
	}

	pinfo->dsi[0] = dsi;
	mipi_dsi_set_drvdata(dsi, pinfo);
	drm_panel_init(&pinfo->panel, dev, &rm69380_panel_funcs, DRM_MODE_CONNECTOR_DSI);

	if (pinfo->desc->has_dcs_backlight) {
		//pinfo->panel.backlight = rm69380_create_backlight(dsi);
		if (IS_ERR(pinfo->panel.backlight))
			return dev_err_probe(dev, PTR_ERR(pinfo->panel.backlight),
					     "Failed to create backlight\n");
	} else {
		ret = drm_panel_of_backlight(&pinfo->panel);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to get backlight\n");
	}

	drm_panel_add(&pinfo->panel);

	for (i = 0; i < DSI_NUM_MIN + pinfo->desc->is_dual_dsi; i++) {
		pinfo->dsi[i]->lanes = pinfo->desc->lanes;
		pinfo->dsi[i]->format = pinfo->desc->format;
		pinfo->dsi[i]->mode_flags = pinfo->desc->mode_flags;

		ret = mipi_dsi_attach(pinfo->dsi[i]);
		if (ret < 0)
			return dev_err_probe(dev, ret, "Cannot attach to DSI%d host.\n", i);
	}

	return 0;
}

static const struct of_device_id rm69380_of_match[] = {
	{
		.compatible = "lenovo,j716f-edo-rm69380",
		.data = &j716f_edo_desc,
	},
	{},
};
MODULE_DEVICE_TABLE(of, rm69380_of_match);

static struct mipi_dsi_driver rm69380_driver = {
	.probe = rm69380_probe,
	.remove = rm69380_remove,
	.driver = {
		.name = "panel-raydium-rm69380",
		.of_match_table = rm69380_of_match,
	},
};
module_mipi_dsi_driver(rm69380_driver);

MODULE_AUTHOR("David Wronek <davidwronek@gmail.com>");
MODULE_DESCRIPTION("DRM driver for Raydium rm69380 based MIPI DSI panels");
MODULE_LICENSE("GPL");
