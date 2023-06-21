// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Limited
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/power_supply.h>

#define REG_BATID			0x00 /* This one is very unclear */
 #define BATID_101			0x0101 /* 107kOhm */
 #define BATID_102			0x0102 /* 10kOhm */
#define REG_TEMPERATURE			0x06
#define REG_VOLTAGE			0x08
#define REG_FLAGS			0x0a
 #define MM8013_FLAG_OTC		BIT(15)
 #define MM8013_FLAG_OTD		BIT(14)
 #define MM8013_FLAG_BATHI		BIT(13)
 #define MM8013_FLAG_FC			BIT(9)
 #define MM8013_FLAG_CHG		BIT(8)
 #define MM8013_FLAG_DSG		BIT(0)
#define REG_FULL_CHARGE_CAPACITY	0x0e
#define REG_AVERAGE_CURRENT		0x14
#define REG_AVERAGE_TIME_TO_EMPTY	0x16
#define REG_AVERAGE_TIME_TO_FULL	0x18
#define REG_CYCLE_COUNT			0x2a
#define REG_STATE_OF_CHARGE		0x2c
#define REG_DESIGN_CAPACITY		0x3c
/* TODO: 0x62-0x68 seem to contain 'MM8013C' in a length-prefixed, non-terminated string */

#define DECIKELVIN_TO_DECIDEGC(t)	(t - 2731)

struct mm8013_chip {
	struct i2c_client *client;
};

static int mm8013_write_reg(struct i2c_client *client, u8 reg, u16 value)
{
	int ret;

	ret = i2c_smbus_write_word_data(client, reg, value);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	usleep_range(4000, 5000);
	return ret;
}

static int mm8013_read_reg(struct i2c_client *client, u8 reg)
{
	int ret;

	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	usleep_range(4000, 5000);
	return ret;
}

static int mm8013_checkdevice(struct mm8013_chip *chip)
{
	int battery_id, ret;

	ret = mm8013_write_reg(chip->client, REG_BATID, 0x0008);
	if (ret < 0)
		return ret;

	ret = mm8013_read_reg(chip->client, REG_BATID);
	if (ret < 0)
		return ret;

	if (ret == BATID_102)
		battery_id = 2;
	else if (ret == BATID_101)
		battery_id = 1;
	else
		return -EINVAL;

	dev_dbg(&chip->client->dev, "battery_id: 0x%d\n", battery_id);

	return 0;
}

static enum power_supply_property mm8013_battery_props[] = {
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static int mm8013_get_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	struct mm8013_chip *chip = psy->drv_data;
	struct i2c_client *client = chip->client;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = mm8013_read_reg(client, REG_STATE_OF_CHARGE);
		if (ret < 0)
			return ret;

		val->intval = ret;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		ret = mm8013_read_reg(client, REG_FULL_CHARGE_CAPACITY);
		if (ret < 0)
			return ret;

		val->intval = ret;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = mm8013_read_reg(client, REG_DESIGN_CAPACITY);
		if (ret < 0)
			return ret;

		val->intval = ret;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = mm8013_read_reg(client, REG_AVERAGE_CURRENT);
		if (ret < 0)
			return ret;

		if (ret > S16_MAX)
			val->intval -= (1 << 16);
		else
			val->intval = ret;

		val->intval *= -1000;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		ret = mm8013_read_reg(client, REG_CYCLE_COUNT);
		if (ret < 0)
			return ret;

		val->intval = ret;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		ret = mm8013_read_reg(client, REG_FLAGS);
		if (ret < 0)
			return ret;

		if (ret & MM8013_FLAG_BATHI)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else if (ret & (MM8013_FLAG_OTD | MM8013_FLAG_OTC))
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		ret = mm8013_read_reg(client, REG_FLAGS);
		if (ret < 0)
			return ret;

		if (ret & MM8013_FLAG_DSG)
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		else if (ret & MM8013_FLAG_CHG)
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else if (ret & MM8013_FLAG_FC)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = mm8013_read_reg(client, REG_TEMPERATURE);
		if (ret < 0)
			return ret;

		val->intval = DECIKELVIN_TO_DECIDEGC(ret);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		ret = mm8013_read_reg(client, REG_AVERAGE_TIME_TO_EMPTY);
		if (ret < 0)
			return ret;

		/* The estimation is not yet ready */
		if (ret == U16_MAX)
			return -ENODATA;

		val->intval = ret;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		ret = mm8013_read_reg(client, REG_AVERAGE_TIME_TO_FULL);
		if (ret < 0)
			return ret;

		/* The estimation is not yet ready */
		if (ret == U16_MAX)
			return -ENODATA;

		val->intval = ret;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = mm8013_read_reg(client, REG_VOLTAGE);
		if (ret < 0)
			return ret;

		val->intval = ret;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct power_supply_desc mm8013_desc = {
	.name			= "mm8013",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= mm8013_battery_props,
	.num_properties		= ARRAY_SIZE(mm8013_battery_props),
	.get_property		= mm8013_get_property,
};

static int mm8013_probe(struct i2c_client *client)
{
	struct power_supply_config psy_cfg = {};
	struct device *dev = &client->dev;
	struct power_supply *psy;
	struct mm8013_chip *chip;
	int ret = 0;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return dev_err_probe(dev, -EIO,
				     "I2C_FUNC_SMBUS_WORD_DATA not supported\n");

	chip = devm_kzalloc(dev, sizeof(struct mm8013_chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;

	ret = mm8013_checkdevice(chip);
	if (ret)
		return dev_err_probe(dev, -ENODEV, "MM8013 not found\n");

	psy_cfg.drv_data = chip;
	psy_cfg.of_node = dev->of_node;

	psy = devm_power_supply_register(dev, &mm8013_desc, &psy_cfg);
	if (IS_ERR(psy))
		return PTR_ERR(psy);

	return 0;
}

static const struct i2c_device_id mm8013_id_table[] = {
	{ "mm8013", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, mm8013_id_table);

static struct of_device_id mm8013_match_table[] = {
	{ .compatible = "mitsumi,mm8013" },
	{ },
};

static struct i2c_driver mm8013_i2c_driver = {
	.probe = mm8013_probe,
	.id_table = mm8013_id_table,
	.driver = {
		.name = "mm8013",
		.owner = THIS_MODULE,
		.of_match_table = mm8013_match_table,
	},
};
module_i2c_driver(mm8013_i2c_driver);

MODULE_DESCRIPTION("MM8013 fuel gauge driver");
MODULE_LICENSE("GPL");
