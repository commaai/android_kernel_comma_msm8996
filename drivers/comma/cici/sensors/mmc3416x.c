/*
 * Copyright (c) 2014-2015, Linux Foundation. All rights reserved.
 * Linux Foundation chooses to take subject only to the GPLv2 license
 * terms, and distributes only under these terms.
 * Copyright (C) 2010 MEMSIC, Inc.
 * Copyright (C) 2018, Comma.ai, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/sysctl.h>
#include <linux/regulator/consumer.h>
#include <linux/input.h>
#include <linux/regmap.h>
#include <asm/uaccess.h>

#include <linux/iio/iio.h>

#include "mmc3416x.h"

#define MMC3416X_DELAY_TM_MS	10

#define MMC3416X_DELAY_SET_MS	75
#define MMC3416X_DELAY_RESET_MS	75

#define MMC3416X_RETRY_COUNT	10
#define MMC3416X_DEFAULT_INTERVAL_MS	100
#define MMC3416X_TIMEOUT_SET_MS	15000

#define MMC3416X_PRODUCT_ID	0x06

/* POWER SUPPLY VOLTAGE RANGE */
#define MMC3416X_VDD_MIN_UV	2000000
#define MMC3416X_VDD_MAX_UV	3300000
#define MMC3416X_VIO_MIN_UV	1750000
#define MMC3416X_VIO_MAX_UV	1950000

enum {
	OBVERSE_X_AXIS_FORWARD = 0,
	OBVERSE_X_AXIS_RIGHTWARD,
	OBVERSE_X_AXIS_BACKWARD,
	OBVERSE_X_AXIS_LEFTWARD,
	REVERSE_X_AXIS_FORWARD,
	REVERSE_X_AXIS_RIGHTWARD,
	REVERSE_X_AXIS_BACKWARD,
	REVERSE_X_AXIS_LEFTWARD,
	MMC3416X_DIR_COUNT,
};

static char *mmc3416x_dir[MMC3416X_DIR_COUNT] = {
	[OBVERSE_X_AXIS_FORWARD] = "obverse-x-axis-forward",
	[OBVERSE_X_AXIS_RIGHTWARD] = "obverse-x-axis-rightward",
	[OBVERSE_X_AXIS_BACKWARD] = "obverse-x-axis-backward",
	[OBVERSE_X_AXIS_LEFTWARD] = "obverse-x-axis-leftward",
	[REVERSE_X_AXIS_FORWARD] = "reverse-x-axis-forward",
	[REVERSE_X_AXIS_RIGHTWARD] = "reverse-x-axis-rightward",
	[REVERSE_X_AXIS_BACKWARD] = "reverse-x-axis-backward",
	[REVERSE_X_AXIS_LEFTWARD] = "reverse-x-axis-leftward",
};

static s8 mmc3416x_rotation_matrix[MMC3416X_DIR_COUNT][9] = {
	[OBVERSE_X_AXIS_FORWARD] = {0, -1, 0, 1, 0, 0, 0, 0, 1},
	[OBVERSE_X_AXIS_RIGHTWARD] = {1, 0, 0, 0, 1, 0, 0, 0, 1},
	[OBVERSE_X_AXIS_BACKWARD] = {0, 1, 0, -1, 0, 0, 0, 0, 1},
	[OBVERSE_X_AXIS_LEFTWARD] = {-1, 0, 0, 0, -1, 0, 0, 0, 1},
	[REVERSE_X_AXIS_FORWARD] = {0, 1, 0, 1, 0, 0, 0, 0, -1},
	[REVERSE_X_AXIS_RIGHTWARD] = {1, 0, 0, 0, -1, 0, 0, 0, -1},
	[REVERSE_X_AXIS_BACKWARD] = {0, -1, 0, -1, 0, 0, 0, 0, -1},
	[REVERSE_X_AXIS_LEFTWARD] = {-1, 0, 0, 0, 1, 0, 0, 0, -1},
};

struct mmc3416x_vec {
	int x;
	int y;
	int z;
};

struct mmc3416x_data {
	struct mutex		ecompass_lock;
	struct mutex		ops_lock;

	struct i2c_client	*i2c;
	struct input_dev	*idev;
	struct regulator	*vdd;
	struct regulator	*vio;
	struct regmap		*regmap;

	int			dir;
	int			enable;
	int			power_enabled;
	unsigned long		timeout;
};

static int mmc3416x_read_xyz(struct mmc3416x_data *memsic, int index, int *val)
{
	int count = 0;
	unsigned char data[6];
	unsigned int status;
	int rc = 0;
	struct mmc3416x_vec report, vec;
	s8 *tmp;

	mutex_lock(&memsic->ecompass_lock);

	/* mmc3416x need to be set periodly to avoid overflow */
	if (time_after(jiffies, memsic->timeout)) {
		rc = regmap_write(memsic->regmap, MMC3416X_REG_CTRL,
				MMC3416X_CTRL_REFILL);
		if (rc) {
			dev_err(&memsic->i2c->dev, "write reg %d failed at %d.(%d)\n",
					MMC3416X_REG_CTRL, __LINE__, rc);
			goto exit;
		}

		/* Time from refill cap to SET */
		msleep(MMC3416X_DELAY_SET_MS);

		rc = regmap_write(memsic->regmap, MMC3416X_REG_CTRL,
				MMC3416X_CTRL_SET);
		if (rc) {
			dev_err(&memsic->i2c->dev, "write reg %d failed at %d.(%d)\n",
					MMC3416X_REG_CTRL, __LINE__, rc);
			goto exit;
		}

		/* Wait time to complete SET/RESET */
		usleep_range(1000, 1500);
		memsic->timeout = jiffies +
			msecs_to_jiffies(MMC3416X_TIMEOUT_SET_MS);

		dev_dbg(&memsic->i2c->dev, "mmc3416x reset is done\n");

		/* Re-send the TM command */
		rc = regmap_write(memsic->regmap, MMC3416X_REG_CTRL,
				MMC3416X_CTRL_TM);
		if (rc) {
			dev_err(&memsic->i2c->dev, "write reg %d failed at %d.(%d)\n",
					MMC3416X_REG_CTRL, __LINE__, rc);
			goto exit;
		}
	}

	/* Read MD */
	rc = regmap_read(memsic->regmap, MMC3416X_REG_DS, &status);
	if (rc) {
		dev_err(&memsic->i2c->dev, "read reg %d failed at %d.(%d)\n",
				MMC3416X_REG_DS, __LINE__, rc);
		goto exit;

	}

	while ((!(status & 0x01)) && (count < MMC3416X_RETRY_COUNT)) {
		/* Read MD again*/
		rc = regmap_read(memsic->regmap, MMC3416X_REG_DS, &status);
		if (rc) {
			dev_err(&memsic->i2c->dev, "read reg %d failed at %d.(%d)\n",
					MMC3416X_REG_DS, __LINE__, rc);
			goto exit;

		}

		/* Wait more time to get valid data */
		usleep_range(1000, 1500);
		count++;
	}

	if (count >= MMC3416X_RETRY_COUNT) {
		dev_err(&memsic->i2c->dev, "TM not work!!");
		rc = -EFAULT;
		goto exit;
	}

	/* read xyz raw data */
	rc = regmap_bulk_read(memsic->regmap, MMC3416X_REG_DATA, data, 6);
	if (rc) {
		dev_err(&memsic->i2c->dev, "read reg %d failed at %d.(%d)\n",
				MMC3416X_REG_DS, __LINE__, rc);
		goto exit;
	}

	vec.x =   (((u8)data[1]) << 8 | (u8)data[0]) - 32768;
	vec.y =   (((u8)data[3]) << 8 | (u8)data[2]) - 32768;
	vec.z = -((((u8)data[5]) << 8 | (u8)data[4]) - 32768);

	tmp = &mmc3416x_rotation_matrix[memsic->dir][0];
	report.x = tmp[0] * vec.x + tmp[1] * vec.y + tmp[2] * vec.z;
	report.y = tmp[3] * vec.x + tmp[4] * vec.y + tmp[5] * vec.z;
	report.z = tmp[6] * vec.x + tmp[7] * vec.y + tmp[8] * vec.z;

	switch (index) {
	case 0:
		*val = report.x;
		break;
	case 1:
		*val = report.y;
		break;
	case 2:
		*val = report.z;
		break;
	}

	rc = IIO_VAL_INT;

exit:
	/* send TM cmd before read */
	if (regmap_write(memsic->regmap, MMC3416X_REG_CTRL, MMC3416X_CTRL_TM)) {
		dev_warn(&memsic->i2c->dev, "write reg %d failed at %d.(%d)\n",
				MMC3416X_REG_CTRL, __LINE__, rc);
	}

	mutex_unlock(&memsic->ecompass_lock);
	return rc;
}

static int mmc3416x_power_init(struct mmc3416x_data *data)
{
	int rc;

	data->vdd = devm_regulator_get(&data->i2c->dev, "vdd");
	if (IS_ERR(data->vdd)) {
		rc = PTR_ERR(data->vdd);
		dev_err(&data->i2c->dev,
				"Regualtor get failed vdd rc=%d\n", rc);
		return rc;
	}
	if (regulator_count_voltages(data->vdd) > 0) {
		rc = regulator_set_voltage(data->vdd,
				MMC3416X_VDD_MIN_UV, MMC3416X_VDD_MAX_UV);
		if (rc) {
			dev_err(&data->i2c->dev,
					"Regulator set failed vdd rc=%d\n",
					rc);
			goto exit;
		}
	}

	rc = regulator_enable(data->vdd);
	if (rc) {
		dev_err(&data->i2c->dev,
				"Regulator enable vdd failed rc=%d\n", rc);
		goto exit;
	}
	data->vio = devm_regulator_get(&data->i2c->dev, "vio");
	if (IS_ERR(data->vio)) {
		rc = PTR_ERR(data->vio);
		dev_err(&data->i2c->dev,
				"Regulator get failed vio rc=%d\n", rc);
		goto reg_vdd_set;
	}

	if (regulator_count_voltages(data->vio) > 0) {
		rc = regulator_set_voltage(data->vio,
				MMC3416X_VIO_MIN_UV, MMC3416X_VIO_MAX_UV);
		if (rc) {
			dev_err(&data->i2c->dev,
					"Regulator set failed vio rc=%d\n", rc);
			goto reg_vdd_set;
		}
	}
	rc = regulator_enable(data->vio);
	if (rc) {
		dev_err(&data->i2c->dev,
				"Regulator enable vio failed rc=%d\n", rc);
		goto reg_vdd_set;
	}

	 /* The minimum time to operate device after VDD valid is 10 ms. */
	usleep_range(15000, 20000);

	data->power_enabled = true;

	return 0;

reg_vdd_set:
	regulator_disable(data->vdd);
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, MMC3416X_VDD_MAX_UV);
exit:
	return rc;
}

static int mmc3416x_power_deinit(struct mmc3416x_data *data)
{
	if (!IS_ERR_OR_NULL(data->vio)) {
		if (regulator_count_voltages(data->vio) > 0)
			regulator_set_voltage(data->vio, 0,
					MMC3416X_VIO_MAX_UV);

		regulator_disable(data->vio);
	}

	if (!IS_ERR_OR_NULL(data->vdd)) {
		if (regulator_count_voltages(data->vdd) > 0)
			regulator_set_voltage(data->vdd, 0,
					MMC3416X_VDD_MAX_UV);

		regulator_disable(data->vdd);
	}

	data->power_enabled = false;

	return 0;
}

static int mmc3416x_power_set(struct mmc3416x_data *memsic, bool on)
{
	int rc = 0;

	if (!on && memsic->power_enabled) {
		mutex_lock(&memsic->ecompass_lock);

		rc = regulator_disable(memsic->vdd);
		if (rc) {
			dev_err(&memsic->i2c->dev,
				"Regulator vdd disable failed rc=%d\n", rc);
			goto err_vdd_disable;
		}

		rc = regulator_disable(memsic->vio);
		if (rc) {
			dev_err(&memsic->i2c->dev,
				"Regulator vio disable failed rc=%d\n", rc);
			goto err_vio_disable;
		}
		memsic->power_enabled = false;

		mutex_unlock(&memsic->ecompass_lock);
		return rc;
	} else if (on && !memsic->power_enabled) {
		mutex_lock(&memsic->ecompass_lock);

		rc = regulator_enable(memsic->vdd);
		if (rc) {
			dev_err(&memsic->i2c->dev,
				"Regulator vdd enable failed rc=%d\n", rc);
			goto err_vdd_enable;
		}

		rc = regulator_enable(memsic->vio);
		if (rc) {
			dev_err(&memsic->i2c->dev,
				"Regulator vio enable failed rc=%d\n", rc);
			goto err_vio_enable;
		}
		memsic->power_enabled = true;

		mutex_unlock(&memsic->ecompass_lock);

		/* The minimum time to operate after VDD valid is 10 ms */
		usleep_range(15000, 20000);

		return rc;
	} else {
		dev_warn(&memsic->i2c->dev,
				"Power on=%d. enabled=%d\n",
				on, memsic->power_enabled);
		return rc;
	}

err_vio_enable:
	regulator_disable(memsic->vio);
err_vdd_enable:
	mutex_unlock(&memsic->ecompass_lock);
	return rc;

err_vio_disable:
	if (regulator_enable(memsic->vdd))
		dev_warn(&memsic->i2c->dev, "Regulator vdd enable failed\n");
err_vdd_disable:
	mutex_unlock(&memsic->ecompass_lock);
	return rc;
}

static int mmc3416x_check_device(struct mmc3416x_data *memsic)
{
	unsigned int data;
	int rc;

	rc = regmap_read(memsic->regmap, MMC3416X_REG_PRODUCTID_1, &data);
	if (rc) {
		dev_err(&memsic->i2c->dev, "read reg %d failed.(%d)\n",
				MMC3416X_REG_DS, rc);
		return rc;

	}

	if (data != MMC3416X_PRODUCT_ID)
		return -ENODEV;

	return 0;
}

static int mmc3416x_parse_dt(struct i2c_client *client,
		struct mmc3416x_data *memsic)
{
	struct device_node *np = client->dev.of_node;
	const char *tmp;
	int rc;
	int i;

	rc = of_property_read_string(np, "memsic,dir", &tmp);

	/* does not have a value or the string is not null-terminated */
	if (rc && (rc != -EINVAL)) {
		dev_err(&client->dev, "Unable to read memsic,dir\n");
		return rc;
	}

	for (i = 0; i < ARRAY_SIZE(mmc3416x_dir); i++) {
		if (strcmp(mmc3416x_dir[i], tmp) == 0)
			break;
	}

	if (i >= ARRAY_SIZE(mmc3416x_dir)) {
		dev_err(&client->dev, "Invalid memsic,dir property");
		return -EINVAL;
	}

	memsic->dir = i;
	return 0;
}

static struct regmap_config mmc3416x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int mmc3416x_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2,
			    long mask)
{
	struct mmc3416x_data *memsic = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return mmc3416x_read_xyz(memsic, chan->address, val);
	}

	return -EINVAL;
}

#define MMC3416X_CHANNEL(axis, index)					\
	{								\
		.type = IIO_MAGN,					\
		.modified = 1,						\
		.channel2 = IIO_MOD_##axis,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.address = index,					\
	}

static const struct iio_chan_spec mmc3416x_channels[] = {
	MMC3416X_CHANNEL(X, 0), MMC3416X_CHANNEL(Y, 1), MMC3416X_CHANNEL(Z, 2),
};

static const struct iio_info mmc3416x_info = {
	.read_raw = &mmc3416x_read_raw,
	.driver_module = THIS_MODULE,
};

static int mmc3416x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int res = 0;
	struct mmc3416x_data *memsic;
	struct iio_dev *indio_dev;

	dev_dbg(&client->dev, "probing mmc3416x\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("mmc3416x i2c functionality check failed.\n");
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*memsic));
	if (!indio_dev)
		return -ENOMEM;

	memsic = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);

	if (client->dev.of_node) {
		res = mmc3416x_parse_dt(client, memsic);
		if (res) {
			dev_err(&client->dev,
				"Unable to parse platform data.(%d)", res);
			return res;
		}
	} else {
		memsic->dir = 0;
	}

	memsic->i2c = client;

	mutex_init(&memsic->ecompass_lock);
	mutex_init(&memsic->ops_lock);

	memsic->regmap = devm_regmap_init_i2c(client, &mmc3416x_regmap_config);
	if (IS_ERR(memsic->regmap)) {
		dev_err(&client->dev, "Init regmap failed.(%ld)",
				PTR_ERR(memsic->regmap));
		return PTR_ERR(memsic->regmap);
	}

	res = mmc3416x_power_init(memsic);
	if (res) {
		dev_err(&client->dev, "Power up mmc3416x failed\n");
		return res;
	}

	res = mmc3416x_check_device(memsic);
	if (res) {
		dev_err(&client->dev, "Check device failed\n");
		goto power_deinit;
	}

	res = mmc3416x_power_set(memsic, true);
	if (res) {
		dev_err(&client->dev, "Power on failed\n");
		goto power_deinit;
	}

	indio_dev->dev.parent = &client->dev;
	indio_dev->channels = mmc3416x_channels;
	indio_dev->num_channels = ARRAY_SIZE(mmc3416x_channels);
	indio_dev->info = &mmc3416x_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = "mmc3416x";
	res = devm_iio_device_register(&client->dev, indio_dev);
	if (res) {
		dev_err(&client->dev, "IIO register failed, %d\n", res);
		goto power_deinit;
	}

	dev_info(&client->dev, "mmc3416x successfully probed\n");

	return 0;

power_deinit:
	mmc3416x_power_deinit(memsic);
	return res;
}

static const struct i2c_device_id mmc3416x_id[] = {
	{ MMC3416X_I2C_NAME, 0 },
	{ }
};

static const struct of_device_id mmc3416x_match_table[] = {
	{ .compatible = "memsic,mmc3416x", },
	{ },
};

static struct i2c_driver mmc3416x_driver = {
	.probe 		= mmc3416x_probe,
	.id_table	= mmc3416x_id,
	.driver 	= {
		.owner	= THIS_MODULE,
		.name	= MMC3416X_I2C_NAME,
		.of_match_table = mmc3416x_match_table,
	},
};
module_i2c_driver(mmc3416x_driver);

MODULE_DESCRIPTION("MEMSIC MMC3416X Magnetic Sensor Driver");
MODULE_LICENSE("GPL");
