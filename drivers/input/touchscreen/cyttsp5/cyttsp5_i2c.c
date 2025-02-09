/*
 * cyttsp5_i2c.c
 * Parade TrueTouch(TM) Standard Product V5 I2C Module.
 * For use with Parade touchscreen controllers.
 * Supported parts include:
 * CYTMA5XX
 * CYTMA448
 * CYTMA445A
 * CYTT21XXX
 * CYTT31XXX
 *
 * Copyright (C) 2015 Parade Technologies
 * Copyright (C) 2012-2015 Cypress Semiconductor
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contact Parade Technologies at www.paradetech.com <ttdrivers@paradetech.com>
 *
 */

#include "cyttsp5_regs.h"

#include <linux/i2c.h>
#include <linux/version.h>

#define CY_I2C_DATA_SIZE  (2 * 256)

#define I2C_IO_WRITE	0
#define I2C_IO_READ		1

static int log_communication_enable = 0;
#ifdef	CYTTSP5_DEVELOP_MODE_ENABLE
	module_param(log_communication_enable, int, S_IRUGO | S_IWUSR);
#endif

#define I2C_MEMORY_ALIGNED	1

static void i2c_communication_log(struct device *dev, int io, u8 *buf, int size)
{
	if(log_communication_enable == 1){
		u8 *strbuf = NULL;
		u8 datastrbuf[10];
		int allocSize = 0;
		int i;

		allocSize = 100 + (3 * size);
		strbuf = (u8 *)kzalloc(allocSize, GFP_KERNEL);
		if(strbuf == NULL){
			dev_err(dev, "%s() alloc error. size = %d\n", __func__, allocSize);
			return;
		}

		sprintf(strbuf, "[I2C] %s :", (io == I2C_IO_WRITE ? "W " : " R"));

		for(i = 0; i < size; i++){
			sprintf(datastrbuf, " %02X", buf[i]);
			strcat(strbuf, datastrbuf);
		}

		dev_notice(dev, "[%5d]%s\n", current->pid, strbuf);

		if(strbuf != NULL){
			kfree(strbuf);
		}
	}
}

static int cyttsp5_i2c_read_default(struct device *dev, void *buf, int size)
{
	struct i2c_client *client = to_i2c_client(dev);
	int rc;
#if I2C_MEMORY_ALIGNED
	int alloc_size = 0;
	u8 *alloc_buf;
#endif

	if (!buf || !size || size > CY_I2C_DATA_SIZE)
		return -EINVAL;

#if I2C_MEMORY_ALIGNED
	alloc_size = sizeof(u8) * size;
	alloc_buf = (u8 *)kzalloc(alloc_size, GFP_KERNEL);
	if(alloc_buf == NULL){
		dev_err(dev, "%s alloc error. size = %d\n", __func__, alloc_size);
		return -ENOMEM;
	}

	rc = i2c_master_recv(client, alloc_buf, size);
	memcpy(buf, alloc_buf, size);

	if(alloc_size > 0){
		kfree(alloc_buf);
	}
#else
	rc = i2c_master_recv(client, buf, size);
#endif

	if (rc == size) {
		i2c_communication_log(&client->dev, I2C_IO_READ, buf, size);
	}

	return (rc < 0) ? rc : rc != size ? -EIO : 0;
}

static int cyttsp5_i2c_read_default_nosize(struct device *dev, u8 *buf, u32 max)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msgs[2];
	u8 msg_count = 1;
	int rc;
	u32 size;
#if I2C_MEMORY_ALIGNED
	int alloc_size = 0;
	u8 *alloc_buf;
#endif

	if (!buf)
		return -EINVAL;

#if I2C_MEMORY_ALIGNED
	alloc_size = sizeof(u8) * max;
	alloc_buf = (u8 *)kzalloc(alloc_size, GFP_KERNEL);
	if(alloc_buf == NULL){
		dev_err(dev, "%s alloc error. size = %d\n", __func__, alloc_size);
		return -ENOMEM;
	}
#endif

	msgs[0].addr = client->addr;
	msgs[0].flags = (client->flags & I2C_M_TEN) | I2C_M_RD;
	msgs[0].len = 2;
#if I2C_MEMORY_ALIGNED
	msgs[0].buf = alloc_buf;
#else
	msgs[0].buf = buf;
#endif
	rc = i2c_transfer(client->adapter, msgs, msg_count);
	if (rc < 0 || rc != msg_count) {
		rc = (rc < 0) ? rc : -EIO;
		goto END;
	}
#if I2C_MEMORY_ALIGNED
	memcpy(buf, alloc_buf, msgs[0].len);
#endif
	i2c_communication_log(&client->dev, I2C_IO_READ, buf, msgs[0].len);

	size = get_unaligned_le16(&buf[0]);
	if (!size || size == 2 || size >= CY_PIP_1P7_EMPTY_BUF) {
		/*
		 * Before PIP 1.7, empty buffer is 0x0002;
		 * From PIP 1.7, empty buffer is 0xFFXX
		 */
		rc = 0;
		goto END;
	}

	if (size > max) {
		rc = -EINVAL;
		goto END;
	}

#if I2C_MEMORY_ALIGNED
	rc = i2c_master_recv(client, alloc_buf, size);
	memcpy(buf, alloc_buf, size);
#else
	rc = i2c_master_recv(client, buf, size);
#endif

	if (rc == size) {
		i2c_communication_log(&client->dev, I2C_IO_READ, buf, size);
	}

	rc = (rc < 0) ? rc : rc != (int)size ? -EIO : 0;

END:
#if I2C_MEMORY_ALIGNED
	if(alloc_size > 0){
		kfree(alloc_buf);
	}
#endif
	return rc;
}

static int cyttsp5_i2c_write_read_specific(struct device *dev, u8 write_len,
		u8 *write_buf, u8 *read_buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msgs[2];
	u8 msg_count = 1;
	int rc;
#if I2C_MEMORY_ALIGNED
	int alloc_size = 0;
	u8 *alloc_buf;
#endif

	if (!write_buf || !write_len)
		return -EINVAL;

#if I2C_MEMORY_ALIGNED
	alloc_size = sizeof(u8) * write_len;
	alloc_buf = (u8 *)kzalloc(alloc_size, GFP_KERNEL);
	if(alloc_buf == NULL){
		dev_err(dev, "%s alloc error. size = %d\n", __func__, alloc_size);
		return -ENOMEM;
	}
	memcpy(alloc_buf, write_buf, write_len);
#endif

	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags & I2C_M_TEN;
	msgs[0].len = write_len;
#if I2C_MEMORY_ALIGNED
	msgs[0].buf = alloc_buf;
#else
	msgs[0].buf = write_buf;
#endif
	rc = i2c_transfer(client->adapter, msgs, msg_count);

	if (rc == msg_count) {
		i2c_communication_log(&client->dev, I2C_IO_WRITE, msgs[0].buf, msgs[0].len);
	}
#if I2C_MEMORY_ALIGNED
	if(alloc_size > 0){
		kfree(alloc_buf);
	}
#endif

	if (rc < 0 || rc != msg_count)
		return (rc < 0) ? rc : -EIO;

	rc = 0;

	if (read_buf)
		rc = cyttsp5_i2c_read_default_nosize(dev, read_buf,
				CY_I2C_DATA_SIZE);

	return rc;
}

static struct cyttsp5_bus_ops cyttsp5_i2c_bus_ops = {
	.bustype = BUS_I2C,
	.read_default = cyttsp5_i2c_read_default,
	.read_default_nosize = cyttsp5_i2c_read_default_nosize,
	.write_read_specific = cyttsp5_i2c_write_read_specific,
};

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
static const struct of_device_id cyttsp5_i2c_of_match[] = {
	{ .compatible = "cy,cyttsp5_i2c_adapter", },
	{ }
};
MODULE_DEVICE_TABLE(of, cyttsp5_i2c_of_match);
#endif

static int cyttsp5_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *i2c_id)
{
	struct device *dev = &client->dev;
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	const struct of_device_id *match;
#endif
	int rc;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "I2C functionality not Supported\n");
		return -EIO;
	}

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	match = of_match_device(of_match_ptr(cyttsp5_i2c_of_match), dev);
	if (match) {
		rc = cyttsp5_devtree_create_and_get_pdata(dev);
		if (rc < 0)
			return rc;
	}
#endif

	rc = cyttsp5_probe(&cyttsp5_i2c_bus_ops, &client->dev, client->irq,
			  CY_I2C_DATA_SIZE);

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	if (rc && match)
		cyttsp5_devtree_clean_pdata(dev);
#endif

	return rc;
}

static int cyttsp5_i2c_remove(struct i2c_client *client)
{
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	struct device *dev = &client->dev;
	const struct of_device_id *match;
#endif
	struct cyttsp5_core_data *cd = i2c_get_clientdata(client);

	cyttsp5_release(cd);

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
	match = of_match_device(of_match_ptr(cyttsp5_i2c_of_match), dev);
	if (match)
		cyttsp5_devtree_clean_pdata(dev);
#endif

	return 0;
}

static const struct i2c_device_id cyttsp5_i2c_id[] = {
	{ CYTTSP5_I2C_NAME, 0, },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cyttsp5_i2c_id);

static struct i2c_driver cyttsp5_i2c_driver = {
	.driver = {
		.name = CYTTSP5_I2C_NAME,
		.owner = THIS_MODULE,
		.pm = &cyttsp5_pm_ops,
#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICETREE_SUPPORT
		.of_match_table = cyttsp5_i2c_of_match,
#endif
	},
	.probe = cyttsp5_i2c_probe,
	.remove = cyttsp5_i2c_remove,
	.id_table = cyttsp5_i2c_id,
};

#if (KERNEL_VERSION(3, 3, 0) <= LINUX_VERSION_CODE)
module_i2c_driver(cyttsp5_i2c_driver);
#else
static int __init cyttsp5_i2c_init(void)
{
	int rc = i2c_add_driver(&cyttsp5_i2c_driver);

	pr_info("%s: Parade TTSP I2C Driver (Built %s) rc=%d\n",
			__func__, CY_DRIVER_VERSION, rc);
	return rc;
}
module_init(cyttsp5_i2c_init);

static void __exit cyttsp5_i2c_exit(void)
{
	i2c_del_driver(&cyttsp5_i2c_driver);
}
module_exit(cyttsp5_i2c_exit);
#endif

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Parade TrueTouch(R) Standard Product I2C driver");
MODULE_AUTHOR("Parade Technologies <ttdrivers@paradetech.com>");
