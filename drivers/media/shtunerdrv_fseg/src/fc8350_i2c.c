/*****************************************************************************
	Copyright(c) 2017 FCI Inc. All Rights Reserved

	File name : fc8350_i2c.c

	Description : source of I2C interface

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

	History :
	----------------------------------------------------------------------
*******************************************************************************/
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "fci_types.h"
#include "fc8350_regs.h"
#include "fci_oal.h"
#include "fc8350_spi.h"

#define I2C_M_FCIRD 1
#define I2C_M_FCIWR 0
#define I2C_MAX_SEND_LENGTH 256

struct i2c_ts_driver {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct work_struct work;
};

struct i2c_client *fc8350_i2c;
struct i2c_driver fc8350_i2c_driver;
static DEFINE_MUTEX(fci_i2c_lock);

static int fc8350_i2c_probe(struct i2c_client *i2c_client,
	const struct i2c_device_id *id)
{
	print_log(NULL, "FC8350_I2C Probe\n");
	fc8350_i2c = i2c_client;
	i2c_set_clientdata(i2c_client, NULL);
	return 0;
}

static int fc8350_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id fc8350_id[] = {
#ifdef EVB
	{ "dmb-i2c3", 0 },
#else
	{ "fc8350_i2c", 0 },
#endif
	{ },
};

const static struct of_device_id fc8350_match_table[] = {
	{ .compatible = "isdb,isdb_fc8350",},
	{ },
};

struct i2c_driver fc8350_i2c_driver = {
	.driver = {
#ifdef EVB
			.name = "dmb-i2c3",
#else
			.name = "fc8350_i2c",
#endif
			.owner = THIS_MODULE,
			.of_match_table = fc8350_match_table,
		},
	.probe    = fc8350_i2c_probe,
	.remove   = fc8350_remove,
	.id_table = fc8350_id,
};

static s32 i2c_bulkread(HANDLE handle, u8 chip, u16 addr, u8 *data, u16 length)
{

	struct i2c_msg rmsg[2];
	unsigned char i2c_data[2];

	rmsg[0].addr = chip;
	rmsg[0].flags = I2C_M_FCIWR;
	rmsg[0].len = 2;
	rmsg[0].buf = i2c_data;
	i2c_data[0] = (addr >> 8) & 0xff;
	i2c_data[1] = addr & 0xff;

	rmsg[1].addr = chip;
	rmsg[1].flags = I2C_M_FCIRD;
	rmsg[1].len = length;
	rmsg[1].buf = data;

	return (i2c_transfer(fc8350_i2c->adapter, &rmsg[0], 2) == 2) ? BBM_OK : BBM_NOK;
}

static s32 i2c_bulkwrite(HANDLE handle, u8 chip, u16 addr, u8 *data, u16 length)
{

	struct i2c_msg wmsg;
	unsigned char i2c_data[I2C_MAX_SEND_LENGTH + 2];

	if ((length) > I2C_MAX_SEND_LENGTH)
		return -ENODEV;

	wmsg.addr = chip;
	wmsg.flags = I2C_M_FCIWR;
	wmsg.len = length + 2;
	wmsg.buf = i2c_data;

	i2c_data[0] = (addr >> 8) & 0xff;
	i2c_data[1] = addr & 0xff;
	memcpy(&i2c_data[2], data, length);

	return (i2c_transfer(fc8350_i2c->adapter, &wmsg, 1) == 1) ? BBM_OK : BBM_NOK;
}


s32 fc8350_i2c_init(HANDLE handle, u16 param1, u16 param2)
{
	s32 res;

	fc8350_i2c = kzalloc(sizeof(struct i2c_ts_driver), GFP_KERNEL);

	if (fc8350_i2c == NULL)
		return -ENOMEM;

	res = i2c_add_driver(&fc8350_i2c_driver);

#ifdef BBM_I2C_SPI
	fc8350_spi_init(handle, 0, 0);
#else
	/* ts_initialize(); */
#endif

	return res;
}

s32 fc8350_i2c_byteread(HANDLE handle, DEVICEID devid, u16 addr, u8 *data)
{
	s32 res;

	mutex_lock(&fci_i2c_lock);
	res = i2c_bulkread(handle, (u8) (devid >> 8) & 0xff, addr, data, 1);
	mutex_unlock(&fci_i2c_lock);
	
#ifdef FC8350_SC_DEBUG
	print_log(NULL, "FC8350_I2C byteread addr=%x %x res=%d\n",addr, *data, res);
#endif

	return res;
}

s32 fc8350_i2c_wordread(HANDLE handle, DEVICEID devid, u16 addr, u16 *data)
{
	s32 res;

	mutex_lock(&fci_i2c_lock);
	res = i2c_bulkread(handle, (u8) (devid >> 8) & 0xff,
			addr, (u8 *) data, 2);
	mutex_unlock(&fci_i2c_lock);

#ifdef FC8350_SC_DEBUG
	print_log(NULL, "FC8350_I2C wordread addr=%x %x,%x res=%d\n",addr, data[0], data[1], res);
#endif

	return res;
}

s32 fc8350_i2c_longread(HANDLE handle, DEVICEID devid, u16 addr, u32 *data)
{
	s32 res;

	mutex_lock(&fci_i2c_lock);
	res = i2c_bulkread(handle, (u8) (devid >> 8) & 0xff,
			addr, (u8 *) data, 4);
	mutex_unlock(&fci_i2c_lock);

#ifdef FC8350_SC_DEBUG
	print_log(NULL, "FC8350_I2C longread addr=%x %x,%x,%x,%x res=%d\n",addr, data[0], data[1], data[2], data[3], res);
#endif

	return res;
}

s32 fc8350_i2c_bulkread(HANDLE handle, DEVICEID devid,
		u16 addr, u8 *data, u16 length)
{
	s32 res;

	mutex_lock(&fci_i2c_lock);
	res = i2c_bulkread(handle, (u8) (devid >> 8) & 0xff,
			addr, data, length);
	mutex_unlock(&fci_i2c_lock);

#ifdef FC8350_SC_DEBUG
	print_log(NULL, "FC8350_I2C buldread addr=%x %x res=%d\n",addr, data[0], res);
#endif

	return res;
}

s32 fc8350_i2c_bytewrite(HANDLE handle, DEVICEID devid, u16 addr, u8 data)
{
	s32 res;

	mutex_lock(&fci_i2c_lock);
	res = i2c_bulkwrite(handle, (u8) (devid >> 8) & 0xff,
			addr, (u8 *)&data, 1);
	mutex_unlock(&fci_i2c_lock);

#ifdef FC8350_SC_DEBUG
	print_log(NULL, "FC8350_I2C bytewrite addr=%x %x res=%d\n",addr, data, res);
#endif
	return res;
}

s32 fc8350_i2c_wordwrite(HANDLE handle, DEVICEID devid, u16 addr, u16 data)
{
	s32 res;

	mutex_lock(&fci_i2c_lock);
	res = i2c_bulkwrite(handle, (u8) (devid >> 8) & 0xff,
			addr, (u8 *)&data, 2);
	mutex_unlock(&fci_i2c_lock);

#ifdef FC8350_SC_DEBUG
	print_log(NULL, "FC8350_I2C wordwrite addr=%x data=%x res=%d\n",addr, data, res);
#endif
	return res;
}

s32 fc8350_i2c_longwrite(HANDLE handle, DEVICEID devid, u16 addr, u32 data)
{
	s32 res;

	mutex_lock(&fci_i2c_lock);
	res = i2c_bulkwrite(handle, (u8) (devid >> 8) & 0xff,
			addr, (u8 *)&data, 4);
	mutex_unlock(&fci_i2c_lock);

#ifdef FC8350_SC_DEBUG
	print_log(NULL, "FC8350_I2C longwrite addr=%x data=%x res=%d\n",addr, data, res);
#endif

	return res;
}

s32 fc8350_i2c_bulkwrite(HANDLE handle, DEVICEID devid,
		u16 addr, u8 *data, u16 length)
{
	s32 res;

	mutex_lock(&fci_i2c_lock);
	res = i2c_bulkwrite(handle, (u8) (devid >> 8) & 0xff,
			addr, data, length);
	mutex_unlock(&fci_i2c_lock);

#ifdef FC8350_SC_DEBUG
	print_log(NULL, "FC8350_I2C bulkwrite addr=%x %x res=%d\n",addr, data[0], res);
#endif

	return res;
}

s32 fc8350_i2c_dataread(HANDLE handle, DEVICEID devid,
		u16 addr, u8 *data, u32 length)
{
	s32 res;

#ifdef BBM_I2C_SPI
	res = fc8350_spi_dataread(handle, devid,
		addr, data, length);
#else
	mutex_lock(&fci_i2c_lock);
	res = i2c_bulkread(handle, (u8) (devid >> 8) & 0xff,
			addr, data, length);
	mutex_unlock(&fci_i2c_lock);
#endif

	return res;
}

s32 fc8350_i2c_deinit(HANDLE handle)
{
	i2c_del_driver(&fc8350_i2c_driver);
#ifdef BBM_I2C_SPI
	fc8350_spi_deinit(handle);
#else
	/* ts_receiver_disable(); */
#endif

	return BBM_OK;
}

