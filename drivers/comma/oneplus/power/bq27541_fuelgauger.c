/*
 * Copyright (C) 2008 Rodolfo Giometti <giometti@linux.it>
 * Copyright (C) 2008 Eurotech S.p.A. <info@eurotech.it>
 * Based on a previous work by Copyright (C) 2008 Texas Instruments, Inc.
 *
 * Copyright (c) 2011, The Linux Foundation. All rights reserved.
 *
 * Copyright (C) 2016, Sultan Alsawaf <sultanxda@gmail.com>
 * Copyright (C) 2018, Comma.ai, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm/unaligned.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/power_supply.h>
#include <linux/slab.h>

/* BQ27541 standard data commands */
#define BQ27541_REG_CNTL		0x00
#define BQ27541_REG_AR			0x02
#define BQ27541_REG_ARTTE		0x04
#define BQ27541_REG_TEMP		0x06
#define BQ27541_REG_VOLT		0x08
#define BQ27541_REG_FLAGS		0x0A
#define BQ27541_REG_NAC			0x0C
#define BQ27541_REG_FAC			0x0e
#define BQ27541_REG_RM			0x10
#define BQ27541_REG_FCC			0x12
#define BQ27541_REG_AI			0x14
#define BQ27541_REG_TTE			0x16
#define BQ27541_REG_TTF			0x18
#define BQ27541_REG_SI			0x1a
#define BQ27541_REG_STTE		0x1c
#define BQ27541_REG_MLI			0x1e
#define BQ27541_REG_MLTTE		0x20
#define BQ27541_REG_AE			0x22
#define BQ27541_REG_AP			0x24
#define BQ27541_REG_TTECP		0x26
#define BQ27541_REG_SOH			0x28
#define BQ27541_REG_CC			0x2a
#define BQ27541_REG_SOC			0x2c
#define BQ27541_REG_NIC			0x2e
#define BQ27541_REG_ICR			0x30
#define BQ27541_REG_LOGIDX		0x32
#define BQ27541_REG_LOGBUF		0x34

#define BQ27541_FLAG_DSC		BIT(0)
#define BQ27541_FLAG_FC			BIT(9)

#define BQ27541_CS_DLOGEN		BIT(15)
#define BQ27541_CS_SS			BIT(13)

/* Control subcommands */
#define BQ27541_SUBCMD_CNTL_STATUS  0x0000
#define BQ27541_SUBCMD_DEVICE_TYPE  0x0001
#define BQ27541_SUBCMD_FW_VER  0x0002
#define BQ27541_SUBCMD_HW_VER  0x0003
#define BQ27541_SUBCMD_DF_CSUM  0x0004
#define BQ27541_SUBCMD_PREV_MACW   0x0007
#define BQ27541_SUBCMD_CHEM_ID   0x0008
#define BQ27541_SUBCMD_BD_OFFSET   0x0009
#define BQ27541_SUBCMD_INT_OFFSET  0x000a
#define BQ27541_SUBCMD_CC_VER   0x000b
#define BQ27541_SUBCMD_OCV  0x000c
#define BQ27541_SUBCMD_BAT_INS   0x000d
#define BQ27541_SUBCMD_BAT_REM   0x000e
#define BQ27541_SUBCMD_SET_HIB   0x0011
#define BQ27541_SUBCMD_CLR_HIB   0x0012
#define BQ27541_SUBCMD_SET_SLP   0x0013
#define BQ27541_SUBCMD_CLR_SLP   0x0014
#define BQ27541_SUBCMD_FCT_RES   0x0015
#define BQ27541_SUBCMD_ENABLE_DLOG  0x0018
#define BQ27541_SUBCMD_DISABLE_DLOG 0x0019
#define BQ27541_SUBCMD_SEALED   0x0020
#define BQ27541_SUBCMD_ENABLE_IT    0x0021
#define BQ27541_SUBCMD_DISABLE_IT   0x0023
#define BQ27541_SUBCMD_CAL_MODE  0x0040
#define BQ27541_SUBCMD_RESET   0x0041
#define ZERO_DEGREES_CELSIUS_IN_TENTH_KELVIN   2731
#define BQ27541_INIT_DELAY   (msecs_to_jiffies(300))

/* Back up and use old data if raw measurement fails */
struct bq27541_old_data {
	int uA;
	int uV;
	int soc;
	int temp;
};

struct bq27541_dev {
	struct device *dev;
	struct i2c_client *client;
	struct delayed_work hw_config;
	struct bq27541_old_data old_data;
	struct power_supply bq_psy;
	struct mutex i2c_read_lock;
};

static int bq27541_read_i2c(struct i2c_client *client, u8 reg, int *rt_value)
{
	struct i2c_msg msg;
	unsigned char data[2];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	data[0] = reg;
	data[1] = 0;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 1;
	msg.buf = data;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0) {
		msg.len = 2;
		msg.flags = I2C_M_RD;

		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret >= 0) {
			*rt_value = get_unaligned_le16(data);
			ret = 0;
		}
	}

	return ret;
}

static int bq27541_read(struct bq27541_dev *di, u8 reg, int *rt_value)
{
	int ret;

	mutex_lock(&di->i2c_read_lock);
	ret = bq27541_read_i2c(di->client, reg, rt_value);
	mutex_unlock(&di->i2c_read_lock);

	return ret;
}

static int bq27541_get_battery_temp(struct bq27541_dev *di)
{
	int ret, temp;

	ret = bq27541_read(di, BQ27541_REG_TEMP, &temp);
	if (ret) {
		dev_err(di->dev, "error reading temperature, ret: %d\n", ret);
		return di->old_data.temp;
	}

	/* Return the battery temperature in tenths of degrees Celsius */
	di->old_data.temp = temp - ZERO_DEGREES_CELSIUS_IN_TENTH_KELVIN;

	return di->old_data.temp;
}

static int bq27541_get_battery_uvolts(struct bq27541_dev *di)
{
	int ret, mV;

	ret = bq27541_read(di, BQ27541_REG_VOLT, &mV);
	if (ret) {
		dev_err(di->dev, "error reading voltage, ret: %d\n", ret);
		return di->old_data.uV;
	}

	di->old_data.uV = mV * 1000;

	return di->old_data.uV;
}

static int bq27541_get_battery_current(struct bq27541_dev *di)
{
	int ret, mA;

	ret = bq27541_read(di, BQ27541_REG_AI, &mA);
	if (ret) {
		dev_err(di->dev, "error reading current, ret: %d\n", ret);
		return di->old_data.uA;
	}

	/* Negative current */
	if (mA & 0x8000)
		mA = -((~(mA - 1)) & 0xFFFF);

	di->old_data.uA = -mA * 1000;

	return di->old_data.uA;
}

static int bq27541_get_battery_soc(struct bq27541_dev *di)
{
	int ret, soc;

	ret = bq27541_read(di, BQ27541_REG_SOC, &soc);
	if (ret) {
		dev_err(di->dev, "error reading SOC, ret: %d\n", ret);
		return di->old_data.soc;
	}

	di->old_data.soc = soc;

	return di->old_data.soc;
}

static void bq27541_i2c_txsubcmd(struct i2c_client *client, u8 reg,
	unsigned short subcmd)
{
	struct i2c_msg msg;
	unsigned char data[3];

	if (!client)
		return;

	data[0] = reg;
	data[1] = subcmd & 0x00FF;
	data[2] = (subcmd & 0xFF00) >> 8;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = data;

	i2c_transfer(client->adapter, &msg, 1);
}

static void bq27541_cntl_cmd(struct bq27541_dev *di, int subcmd)
{
	bq27541_i2c_txsubcmd(di->client, BQ27541_REG_CNTL, subcmd);
}

static int bq27541_chip_config(struct bq27541_dev *di)
{
	int flags, ret;

	bq27541_cntl_cmd(di, BQ27541_SUBCMD_CNTL_STATUS);
	udelay(66);
	ret = bq27541_read(di, BQ27541_REG_CNTL, &flags);
	if (ret) {
		dev_err(di->dev, "error reading register %02x, ret: %d\n",
			 BQ27541_REG_CNTL, ret);
		return ret;
	}
	udelay(66);

	bq27541_cntl_cmd(di, BQ27541_SUBCMD_ENABLE_IT);
	udelay(66);

	if (!(flags & BQ27541_CS_DLOGEN)) {
		bq27541_cntl_cmd(di, BQ27541_SUBCMD_ENABLE_DLOG);
		udelay(66);
	}

	return 0;
}

static enum power_supply_property bq27541_props[] = {
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP
};

static int bq27541_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct bq27541_dev *di = container_of(psy, typeof(*di), bq_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = bq27541_get_battery_soc(di);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = bq27541_get_battery_current(di);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = bq27541_get_battery_uvolts(di);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = bq27541_get_battery_temp(di);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void bq27541_hw_config(struct work_struct *work)
{
	struct bq27541_dev *di =
		container_of(to_delayed_work(work), typeof(*di), hw_config);
	int ret, flags = 0, type = 0, fw_ver = 0;

	ret = bq27541_chip_config(di);
	if (ret) {
		dev_err(di->dev, "Failed to configure device, retrying\n");
		schedule_delayed_work(&di->hw_config, BQ27541_INIT_DELAY);
		return;
	}

	bq27541_cntl_cmd(di, BQ27541_SUBCMD_CNTL_STATUS);
	udelay(66);
	bq27541_read(di, BQ27541_REG_CNTL, &flags);
	bq27541_cntl_cmd(di, BQ27541_SUBCMD_DEVICE_TYPE);
	udelay(66);
	bq27541_read(di, BQ27541_REG_CNTL, &type);
	bq27541_cntl_cmd(di, BQ27541_SUBCMD_FW_VER);
	udelay(66);
	bq27541_read(di, BQ27541_REG_CNTL, &fw_ver);

	dev_info(di->dev, "DEVICE_TYPE is 0x%02X, FIRMWARE_VERSION is 0x%02X\n",
			type, fw_ver);
	dev_info(di->dev, "Completed configuration 0x%02X\n", flags);

	di->bq_psy.name = "bq27541";
	di->bq_psy.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bq_psy.properties = bq27541_props;
	di->bq_psy.num_properties = ARRAY_SIZE(bq27541_props);
	di->bq_psy.get_property = bq27541_get_property;
	ret = power_supply_register(di->dev, &di->bq_psy);
	if (ret < 0)
		pr_err("bq27541 failed to register, ret=%d\n", ret);
}

static int bq27541_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct bq27541_dev *di;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	mutex_init(&di->i2c_read_lock);
	di->dev = &client->dev;
	di->client = client;

	/*
	 * 300ms delay is needed after bq27541 is powered up
	 * and before any successful I2C transaction
	 */
	INIT_DELAYED_WORK(&di->hw_config, bq27541_hw_config);
	schedule_delayed_work(&di->hw_config, BQ27541_INIT_DELAY);

	return 0;
}

static const struct of_device_id bq27541_match[] = {
	{ .compatible = "ti,bq27541-battery" },
	{ }
};

static const struct i2c_device_id bq27541_id[] = {
	{ "bq27541-battery", 1 },
	{ }
};

static struct i2c_driver bq27541_driver = {
	.driver = {
		.name = "bq27541-battery",
		.owner = THIS_MODULE,
		.of_match_table = bq27541_match,
	},
	.probe = bq27541_probe,
	.id_table = bq27541_id
};

static int __init bq27541_init(void)
{
	return i2c_add_driver(&bq27541_driver);
}
device_initcall(bq27541_init);
