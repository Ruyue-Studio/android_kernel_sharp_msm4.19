/*
 * Synaptics TCM touchscreen driver
 *
 * Copyright (C) 2017 Synaptics Incorporated. All rights reserved.
 *
 * Copyright (C) 2017 Scott Lin <scott.lin@tw.synaptics.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND SYNAPTICS
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL SYNAPTICS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF SYNAPTICS WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION DOES
 * NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES, SYNAPTICS'
 * TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S.
 * DOLLARS.
 */

#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include "synaptics_tcm_core.h"

#define TYPE_B_PROTOCOL

#define TOUCH_REPORT_CONFIG_SIZE 128

#define SYSFS_DIR_NAME "touch"
#define TOUCH_DIAGPOLL_TIME 100

#define USE_DEFAULT_TOUCH_REPORT_CONFIG

#define TOUCH_REPORT_CONFIG_SIZE 128

enum touch_status {
	LIFT = 0,
	FINGER = 1,
	GLOVED_FINGER = 2,
	PALM = 6,
	NOP = -1,
};

enum touch_report_code {
	TOUCH_END = 0,
	TOUCH_FOREACH_ACTIVE_OBJECT,
	TOUCH_FOREACH_OBJECT,
	TOUCH_FOREACH_END,
	TOUCH_PAD_TO_NEXT_BYTE,
	TOUCH_TIMESTAMP,
	TOUCH_OBJECT_N_INDEX,
	TOUCH_OBJECT_N_CLASSIFICATION,
	TOUCH_OBJECT_N_X_POSITION,
	TOUCH_OBJECT_N_Y_POSITION,
	TOUCH_OBJECT_N_Z,
	TOUCH_OBJECT_N_X_WIDTH,
	TOUCH_OBJECT_N_Y_WIDTH,
	TOUCH_OBJECT_N_TX_POSITION_TIXELS,
	TOUCH_OBJECT_N_RX_POSITION_TIXELS,
	TOUCH_0D_BUTTONS_STATE,
	TOUCH_GESTURE_DOUBLE_TAP,
	TOUCH_FRAME_RATE,
	TOUCH_POWER_IM,
	TOUCH_CID_IM,
	TOUCH_RAIL_IM,
	TOUCH_CID_VARIANCE_IM,
	TOUCH_NSM_FREQUENCY,
	TOUCH_NSM_STATE,
	TOUCH_NUM_OF_ACTIVE_OBJECTS,
	TOUCH_NUM_OF_CPU_CYCLES_USED_SINCE_LAST_FRAME,
	TOUCH_TUNING_GAUSSIAN_WIDTHS = 0x80,
	TOUCH_TUNING_SMALL_OBJECT_PARAMS,
	TOUCH_TUNING_0D_BUTTONS_VARIANCE,
};

struct object_data {
	unsigned char status;
	unsigned int x_pos;
	unsigned int y_pos;
	unsigned int x_width;
	unsigned int y_width;
	unsigned int z;
	unsigned int tx_pos;
	unsigned int rx_pos;
};

struct input_params {
	unsigned int max_x;
	unsigned int max_y;
	unsigned int max_objects;
};

struct touch_data {
	struct object_data *object_data;
	unsigned int timestamp;
	unsigned int buttons_state;
	unsigned int gesture_double_tap;
	unsigned int frame_rate;
	unsigned int power_im;
	unsigned int cid_im;
	unsigned int rail_im;
	unsigned int cid_variance_im;
	unsigned int nsm_frequency;
	unsigned int nsm_state;
	unsigned int num_of_active_objects;
	unsigned int num_of_cpu_cycles;
};

struct touch_info{
	unsigned short	x;
	unsigned short	y;
	unsigned char	state;
	unsigned char	wx;
	unsigned char	wy;
	unsigned char	z;
};

struct touch_diag_info{
	u8							tm_mode;
	int							event;
	wait_queue_head_t			wait;
	struct touch_info			*fingers;
};

struct touch_hcd {
	bool irq_wake;
	bool report_touch;
	unsigned char *prev_status;
	unsigned int max_x;
	unsigned int max_y;
	unsigned int max_objects;
	struct mutex report_mutex;
	struct input_dev *input_dev;
	struct touch_data touch_data;
	struct input_params input_params;
	struct syna_tcm_buffer out;
	struct syna_tcm_buffer resp;
	struct syna_tcm_hcd *tcm_hcd;
	struct kobject *sysfs_dir;
	struct touch_info *prev_touch_info;
	unsigned long prev_touch_jiffies;
	struct touch_diag_info diag;
};

DECLARE_COMPLETION(touch_remove_complete);

static struct touch_hcd *touch_hcd;

#ifdef SYNAPTICS_TCM_DEVELOP_MODE_ENABLE
	static int log_event_enable = 0;
	module_param(log_event_enable, int, S_IRUGO | S_IWUSR);
#endif

/**
 * 
 */
#ifdef SYNAPTICS_TCM_DEVELOP_MODE_ENABLE
static ssize_t touch_sysfs_log_event_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	sprintf(buf, "%d\n", log_event_enable);

	return( strlen(buf) );
}
static ssize_t touch_sysfs_log_event_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	log_event_enable = buf[0] - '0';
	return count;
}
#endif

static ssize_t touch_sysfs_poll_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int retval;

	retval = wait_event_interruptible_timeout(touch_hcd->diag.wait,
			touch_hcd->diag.event == 1,
			msecs_to_jiffies(TOUCH_DIAGPOLL_TIME));

	if(0 == retval){
		/* time out */
		return -1;
	}

	return 0;
}

static ssize_t touch_sysfs_touch_data_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i;
	static char workbuf[512];

	for (i = 0; i < touch_hcd->max_objects; i++) {
		sprintf(workbuf, "id=%d,state=%d,x=%d,y=%d,wx=%d,wy=%d,z=%d\n",
							i,
							touch_hcd->diag.fingers[i].state,
							touch_hcd->diag.fingers[i].x,
							touch_hcd->diag.fingers[i].y,
							touch_hcd->diag.fingers[i].wx,
							touch_hcd->diag.fingers[i].wy,
							touch_hcd->diag.fingers[i].z);
		strcat(buf, workbuf);
	}

	touch_hcd->diag.event = 0;

	return( strlen(buf) );
}

static struct device_attribute attrs[] = {
#ifdef SYNAPTICS_TCM_DEVELOP_MODE_ENABLE
	__ATTR(log_event_enable, (S_IRUGO | S_IWUSR | S_IWGRP),
			touch_sysfs_log_event_enable_show,
			touch_sysfs_log_event_enable_store),
#endif
	__ATTR(poll, S_IRUGO,
			touch_sysfs_poll_show,
			syna_tcm_store_error),
	__ATTR(touch_data, S_IRUGO,
			touch_sysfs_touch_data_show,
			syna_tcm_store_error),
};

#ifdef SYNAPTICS_TCM_DEVELOP_MODE_ENABLE
static int touch_is_log_event_enable(void){
	if(log_event_enable != 0){
		return 1;
	}
	return 0;
}
#endif


/**
 * touch_free_objects() - Free all touch objects
 *
 * Report finger lift events to the input subsystem for all touch objects.
 */
static void touch_free_objects(void)
{
	unsigned int idx;
#ifdef SYNAPTICS_TCM_DEVELOP_MODE_ENABLE
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
#endif

	if (touch_hcd->input_dev == NULL)
		return;

	mutex_lock(&touch_hcd->report_mutex);

#ifdef TYPE_B_PROTOCOL
	for (idx = 0; idx < touch_hcd->max_objects; idx++) {
		input_mt_slot(touch_hcd->input_dev, idx);
		input_mt_report_slot_state(touch_hcd->input_dev,
				MT_TOOL_FINGER, 0);
	}
#endif
	input_report_key(touch_hcd->input_dev,
			BTN_TOUCH, 0);
	input_report_key(touch_hcd->input_dev,
			BTN_TOOL_FINGER, 0);
#ifndef TYPE_B_PROTOCOL
	input_mt_sync(touch_hcd->input_dev);
#endif
	input_sync(touch_hcd->input_dev);

	for (idx = 0; idx < touch_hcd->max_objects; idx++) {
#ifdef SYNAPTICS_TCM_DEVELOP_MODE_ENABLE
		if(touch_is_log_event_enable()){
			if((touch_hcd->prev_status[idx] != LIFT) && (touch_hcd->prev_status[idx] != PALM)){
				LOG_EVENT(tcm_hcd->pdev->dev.parent,
					"[%s]Notify event[%d] touch=%s(%d), x=%d(%d), y=%d(%d) w=%d(%d,%d)\n",
					(touch_hcd->prev_status[idx] == GLOVED_FINGER ? "glove" : "finger"),
					idx,
					"0",
					touch_hcd->diag.fingers[idx].z,
					touch_hcd->diag.fingers[idx].x,
					touch_hcd->diag.fingers[idx].x,
					touch_hcd->diag.fingers[idx].y,
					touch_hcd->diag.fingers[idx].y,
					(touch_hcd->diag.fingers[idx].wx > touch_hcd->diag.fingers[idx].wy ? touch_hcd->diag.fingers[idx].wx : touch_hcd->diag.fingers[idx].wy),
					touch_hcd->diag.fingers[idx].wx,
					touch_hcd->diag.fingers[idx].wy
				);
			}
		}
#endif

		touch_hcd->prev_status[idx] = LIFT;
		touch_hcd->prev_touch_info[idx].state = LIFT;
	}

	mutex_unlock(&touch_hcd->report_mutex);

	return;
}

/**
 * touch_get_report_data() - Retrieve data from touch report
 *
 * Retrieve data from the touch report based on the bit offset and bit length
 * information from the touch report configuration.
 */
static int touch_get_report_data(unsigned int offset,
		unsigned int bits, unsigned int *data)
{
	unsigned char mask;
	unsigned char byte_data;
	unsigned int output_data;
	unsigned int bit_offset;
	unsigned int byte_offset;
	unsigned int data_bits;
	unsigned int available_bits;
	unsigned int remaining_bits;
	unsigned char *touch_report;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

	if (bits == 0 || bits > 32) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Invalid number of bits\n");
		return -EINVAL;
	}

	if (offset + bits > tcm_hcd->report.buffer.data_length * 8) {
		*data = 0;
		return 0;
	}

	touch_report = tcm_hcd->report.buffer.buf;

	output_data = 0;
	remaining_bits = bits;

	bit_offset = offset % 8;
	byte_offset = offset / 8;

	while (remaining_bits) {
		byte_data = touch_report[byte_offset];
		byte_data >>= bit_offset;

		available_bits = 8 - bit_offset;
		data_bits = MIN(available_bits, remaining_bits);
		mask = 0xff >> (8 - data_bits);

		byte_data &= mask;

		output_data |= byte_data << (bits - remaining_bits);

		bit_offset = 0;
		byte_offset += 1;
		remaining_bits -= data_bits;
	}

	*data = output_data;

	return 0;
}

/**
 * touch_parse_report() - Parse touch report
 *
 * Traverse through the touch report configuration and parse the touch report
 * generated by the device accordingly to retrieve the touch data.
 */
static int touch_parse_report(void)
{
	int retval;
	bool active_only = false;
	bool num_of_active_objects;
	unsigned char code;
	unsigned int size;
	unsigned int idx;
	unsigned int obj;
	unsigned int next;
	unsigned int data;
	unsigned int bits;
	unsigned int offset;
	unsigned int objects;
	unsigned int active_objects = 0;
	unsigned int report_size;
	unsigned int config_size;
	unsigned char *config_data;
	struct touch_data *touch_data;
	struct object_data *object_data;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	static unsigned int end_of_foreach;

	touch_data = &touch_hcd->touch_data;
	object_data = touch_hcd->touch_data.object_data;

	config_data = tcm_hcd->config.buf;
	config_size = tcm_hcd->config.data_length;

	report_size = tcm_hcd->report.buffer.data_length;

	size = sizeof(*object_data) * touch_hcd->max_objects;
	memset(touch_hcd->touch_data.object_data, 0x00, size);

	num_of_active_objects = false;

	idx = 0;
	offset = 0;
	objects = 0;
	while (idx < config_size) {
		code = config_data[idx++];
		switch (code) {
		case TOUCH_END:
			goto exit;
		case TOUCH_FOREACH_ACTIVE_OBJECT:
			obj = 0;
			next = idx;
			active_only = true;
			break;
		case TOUCH_FOREACH_OBJECT:
			obj = 0;
			next = idx;
			active_only = false;
			break;
		case TOUCH_FOREACH_END:
			end_of_foreach = idx;
			if (active_only) {
				if (num_of_active_objects) {
					objects++;
					if (objects < active_objects)
						idx = next;
				} else if (offset < report_size * 8) {
					idx = next;
				}
			} else {
				obj++;
				if (obj < touch_hcd->max_objects)
					idx = next;
			}
			break;
		case TOUCH_PAD_TO_NEXT_BYTE:
			offset = ceil_div(offset, 8) * 8;
			break;
		case TOUCH_TIMESTAMP:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get timestamp\n");
				return retval;
			}
			touch_data->timestamp = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_INDEX:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &obj);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object index\n");
				return retval;
			}
			offset += bits;
			break;
		case TOUCH_OBJECT_N_CLASSIFICATION:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object classification\n");
				return retval;
			}
			object_data[obj].status = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_X_POSITION:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object x position\n");
				return retval;
			}
			object_data[obj].x_pos = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_Y_POSITION:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object y position\n");
				return retval;
			}
			object_data[obj].y_pos = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_Z:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object z\n");
				return retval;
			}
			object_data[obj].z = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_X_WIDTH:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object x width\n");
				return retval;
			}
			object_data[obj].x_width = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_Y_WIDTH:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object y width\n");
				return retval;
			}
			object_data[obj].y_width = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_TX_POSITION_TIXELS:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object tx position\n");
				return retval;
			}
			object_data[obj].tx_pos = data;
			offset += bits;
			break;
		case TOUCH_OBJECT_N_RX_POSITION_TIXELS:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get object rx position\n");
				return retval;
			}
			object_data[obj].rx_pos = data;
			offset += bits;
			break;
		case TOUCH_0D_BUTTONS_STATE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get 0D buttons state\n");
				return retval;
			}
			touch_data->buttons_state = data;
			offset += bits;
			break;
		case TOUCH_GESTURE_DOUBLE_TAP:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get gesture double tap\n");
				return retval;
			}
			touch_data->gesture_double_tap = data;
			offset += bits;
			break;
		case TOUCH_FRAME_RATE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get frame rate\n");
				return retval;
			}
			touch_data->frame_rate = data;
			offset += bits;
			break;
		case TOUCH_POWER_IM:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get power IM\n");
				return retval;
			}
			touch_data->power_im = data;
			offset += bits;
			break;
		case TOUCH_CID_IM:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get CID IM\n");
				return retval;
			}
			touch_data->cid_im = data;
			offset += bits;
			break;
		case TOUCH_RAIL_IM:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get rail IM\n");
				return retval;
			}
			touch_data->rail_im = data;
			offset += bits;
			break;
		case TOUCH_CID_VARIANCE_IM:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get CID variance IM\n");
				return retval;
			}
			touch_data->cid_variance_im = data;
			offset += bits;
			break;
		case TOUCH_NSM_FREQUENCY:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get NSM frequency\n");
				return retval;
			}
			touch_data->nsm_frequency = data;
			offset += bits;
			break;
		case TOUCH_NSM_STATE:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get NSM state\n");
				return retval;
			}
			touch_data->nsm_state = data;
			offset += bits;
			break;
		case TOUCH_NUM_OF_ACTIVE_OBJECTS:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get number of active objects\n");
				return retval;
			}
			active_objects = data;
			num_of_active_objects = true;
			touch_data->num_of_active_objects = data;
			offset += bits;
			if (touch_data->num_of_active_objects == 0)
				idx = end_of_foreach;
			break;
		case TOUCH_NUM_OF_CPU_CYCLES_USED_SINCE_LAST_FRAME:
			bits = config_data[idx++];
			retval = touch_get_report_data(offset, bits, &data);
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to get number of CPU cycles used since last frame\n");
				return retval;
			}
			touch_data->num_of_cpu_cycles = data;
			offset += bits;
			break;
		case TOUCH_TUNING_GAUSSIAN_WIDTHS:
			bits = config_data[idx++];
			offset += bits;
			break;
		case TOUCH_TUNING_SMALL_OBJECT_PARAMS:
			bits = config_data[idx++];
			offset += bits;
			break;
		case TOUCH_TUNING_0D_BUTTONS_VARIANCE:
			bits = config_data[idx++];
			offset += bits;
			break;
		default:
			bits = config_data[idx++];
			offset += bits;
			break;
		}
	}

exit:
	return 0;
}

/**
 * touch_report() - Report touch events
 *
 * Retrieve data from the touch report generated by the device and report touch
 * events to the input subsystem.
 */
static void touch_report(void)
{
	int retval;
	unsigned int idx;
	unsigned int x;
	unsigned int y;
	unsigned int z;
	unsigned int temp;
	unsigned int status;
	unsigned int touch_count;
	unsigned int prev_touch_count;
	struct touch_data *touch_data;
	struct touch_info *prev_touch_info;
	struct object_data *object_data;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	const struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;
	int report_previous_event;

	if (!touch_hcd->report_touch)
		return;

	if (touch_hcd->input_dev == NULL)
		return;

	mutex_lock(&touch_hcd->report_mutex);

	retval = touch_parse_report();
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to parse touch report\n");
		goto exit;
	}

	touch_data = &touch_hcd->touch_data;
	object_data = touch_hcd->touch_data.object_data;
	prev_touch_info = touch_hcd->prev_touch_info;

#ifdef WAKEUP_GESTURE
	if (touch_data->gesture_double_tap && is_suspended(tcm_hcd)) {
		input_report_key(touch_hcd->input_dev, KEY_WAKEUP, 1);
		input_sync(touch_hcd->input_dev);
		input_report_key(touch_hcd->input_dev, KEY_WAKEUP, 0);
		input_sync(touch_hcd->input_dev);
	}
#endif

	if (is_suspended(tcm_hcd))
		goto exit;

	touch_count = 0;
	prev_touch_count = 0;
	for (idx = 0; idx < touch_hcd->max_objects; idx++) {
		if(touch_hcd->prev_status[idx] != LIFT){
			prev_touch_count++;
		}
#ifdef SYNAPTICS_TCM_DEVELOP_MODE_ENABLE
		if(touch_is_log_event_enable()){
			if (touch_hcd->prev_status[idx] != LIFT ||
					object_data[idx].status != LIFT) {
				LOG_EVENT(tcm_hcd->pdev->dev.parent,
					"[%s]Touch Info[%d] x=%d, y=%d, wx=%d, wy=%d, z=%d\n",
					(object_data[idx].status == FINGER ? "Finger" :
					(object_data[idx].status == GLOVED_FINGER ? "Glove" :
					(object_data[idx].status == PALM ? "Palm" :
					(object_data[idx].status == LIFT ? "NoTouch" : "Other")))),
					idx,
					object_data[idx].x_pos,
					object_data[idx].y_pos,
					object_data[idx].x_width,
					object_data[idx].y_width,
					object_data[idx].z
				);
			}
		}
#endif
	}

	// report previous event before reporting touch-up event
	if ((jiffies - touch_hcd->prev_touch_jiffies) > msecs_to_jiffies(40)) {
		report_previous_event = 0;

		for (idx = 0; idx < touch_hcd->max_objects; idx++) {
			if ((touch_hcd->prev_status[idx] == LIFT || touch_hcd->prev_status[idx] == PALM) &&
				(object_data[idx].status == LIFT || object_data[idx].status == PALM)){
				status = NOP;
			}
			else{
				status = object_data[idx].status;
			}

			switch (status) {
			case LIFT:
			case PALM:
				LOGI(tcm_hcd->pdev->dev.parent, "report previous event");
				report_previous_event = 1;

#ifdef TYPE_B_PROTOCOL
				input_mt_slot(touch_hcd->input_dev, idx);
				input_mt_report_slot_state(touch_hcd->input_dev,
						MT_TOOL_FINGER, 1);
#endif
				input_report_key(touch_hcd->input_dev,
						BTN_TOUCH, 1);
				input_report_key(touch_hcd->input_dev,
						BTN_TOOL_FINGER, 1);
				input_report_abs(touch_hcd->input_dev,
						ABS_MT_POSITION_X, prev_touch_info[idx].x);
				input_report_abs(touch_hcd->input_dev,
						ABS_MT_POSITION_Y, prev_touch_info[idx].y);
				input_report_abs(touch_hcd->input_dev, ABS_MT_TOUCH_MAJOR,
						(prev_touch_info[idx].wx > prev_touch_info[idx].wy ? prev_touch_info[idx].wx : prev_touch_info[idx].wy));
				input_report_abs(touch_hcd->input_dev, ABS_MT_TOUCH_MINOR,
						(prev_touch_info[idx].wx < prev_touch_info[idx].wy ? prev_touch_info[idx].wx : prev_touch_info[idx].wy));
				input_report_abs(touch_hcd->input_dev, ABS_MT_ORIENTATION,
						(prev_touch_info[idx].wx > prev_touch_info[idx].wy ? 1 : 0));
				input_report_abs(touch_hcd->input_dev, ABS_MT_PRESSURE,
						prev_touch_info[idx].z + 1);
#ifndef TYPE_B_PROTOCOL
				input_mt_sync(touch_hcd->input_dev);
#endif
				break;
			}
		}
		if(report_previous_event == 1){
			input_sync(touch_hcd->input_dev);
			mdelay(8);
		}
	}

	for (idx = 0; idx < touch_hcd->max_objects; idx++) {
		if ((touch_hcd->prev_status[idx] == LIFT || touch_hcd->prev_status[idx] == PALM) &&
			(object_data[idx].status == LIFT || object_data[idx].status == PALM)){
			status = NOP;
		}
		else{
			status = object_data[idx].status;
		}

		x = object_data[idx].x_pos;
		y = object_data[idx].y_pos;
		if (bdata->swap_axes) {
			temp = x;
			x = y;
			y = temp;
		}
		if (bdata->x_flip)
			x = touch_hcd->input_params.max_x - x;
		if (bdata->y_flip)
			y = touch_hcd->input_params.max_y - y;

		z = object_data[idx].z;
		if((object_data[idx].status != LIFT) && (z == 0)){
			z = 1;
		}

		switch (status) {
		case LIFT:
		case PALM:
#ifdef TYPE_B_PROTOCOL
			input_mt_slot(touch_hcd->input_dev, idx);
			input_mt_report_slot_state(touch_hcd->input_dev,
					MT_TOOL_FINGER, 0);
#endif
#ifdef SYNAPTICS_TCM_DEVELOP_MODE_ENABLE
			if(touch_is_log_event_enable()){
				LOG_EVENT(tcm_hcd->pdev->dev.parent,
					"[%s]Notify event[%d] touch=%s(%d), x=%d(%d), y=%d(%d) w=%d(%d,%d)\n",
					"finger",
					idx,
					"0",
					z,
					x,
					x,
					y,
					y,
					(object_data[idx].x_width > object_data[idx].y_width ? object_data[idx].x_width : object_data[idx].y_width),
					object_data[idx].x_width,
					object_data[idx].y_width
				);
			}
#endif
			break;
		case FINGER:
		case GLOVED_FINGER:
#ifdef TYPE_B_PROTOCOL
			input_mt_slot(touch_hcd->input_dev, idx);
			input_mt_report_slot_state(touch_hcd->input_dev,
					MT_TOOL_FINGER, 1);
#endif
			input_report_key(touch_hcd->input_dev,
					BTN_TOUCH, 1);
			input_report_key(touch_hcd->input_dev,
					BTN_TOOL_FINGER, 1);
			input_report_abs(touch_hcd->input_dev,
					ABS_MT_POSITION_X, x);
			input_report_abs(touch_hcd->input_dev,
					ABS_MT_POSITION_Y, y);
			input_report_abs(touch_hcd->input_dev, ABS_MT_TOUCH_MAJOR,
					(object_data[idx].x_width > object_data[idx].y_width ? object_data[idx].x_width : object_data[idx].y_width));
			input_report_abs(touch_hcd->input_dev, ABS_MT_TOUCH_MINOR,
					(object_data[idx].x_width < object_data[idx].y_width ? object_data[idx].x_width : object_data[idx].y_width));
			input_report_abs(touch_hcd->input_dev, ABS_MT_ORIENTATION,
					(object_data[idx].x_width > object_data[idx].y_width ? 1 : 0));
			input_report_abs(touch_hcd->input_dev, ABS_MT_PRESSURE,
					z);
#ifndef TYPE_B_PROTOCOL
			input_mt_sync(touch_hcd->input_dev);
#endif
			LOGD(tcm_hcd->pdev->dev.parent,
					"Finger %d: x = %d, y = %d\n",
					idx, x, y);
			touch_count++;

#ifdef SYNAPTICS_TCM_DEVELOP_MODE_ENABLE
			if(touch_is_log_event_enable()){
				LOG_EVENT(tcm_hcd->pdev->dev.parent,
					"[%s]Notify event[%d] touch=%s(%d), x=%d(%d), y=%d(%d) w=%d(%d,%d)\n",
					(status == GLOVED_FINGER ? "glove" : "finger"),
					idx,
					"100",
					z,
					x,
					x,
					y,
					y,
					(object_data[idx].x_width > object_data[idx].y_width ? object_data[idx].x_width : object_data[idx].y_width),
					object_data[idx].x_width,
					object_data[idx].y_width
				);
			}
#endif
			break;
		default:
			break;
		}

		touch_hcd->prev_status[idx] = object_data[idx].status;

		touch_hcd->diag.fingers[idx].state = object_data[idx].status;
		touch_hcd->diag.fingers[idx].x = x;
		touch_hcd->diag.fingers[idx].y = y;
		touch_hcd->diag.fingers[idx].wx = object_data[idx].x_width;
		touch_hcd->diag.fingers[idx].wy = object_data[idx].y_width;
		touch_hcd->diag.fingers[idx].z = z;

		touch_hcd->prev_touch_info[idx].state = object_data[idx].status;
		touch_hcd->prev_touch_info[idx].x = x;
		touch_hcd->prev_touch_info[idx].y = y;
		touch_hcd->prev_touch_info[idx].wx = object_data[idx].x_width;
		touch_hcd->prev_touch_info[idx].wy = object_data[idx].y_width;
		touch_hcd->prev_touch_info[idx].z = z;
	}

	if (touch_count == 0) {
		input_report_key(touch_hcd->input_dev,
				BTN_TOUCH, 0);
		input_report_key(touch_hcd->input_dev,
				BTN_TOOL_FINGER, 0);
#ifndef TYPE_B_PROTOCOL
		input_mt_sync(touch_hcd->input_dev);
#endif
	}

	if(prev_touch_count > 0 && touch_count == 0){
		touchevent_notifier_call_chain(0, NULL);
	}
	else if(prev_touch_count == 0 && touch_count > 0){
		touchevent_notifier_call_chain(1, NULL);
	}

	input_sync(touch_hcd->input_dev);

	touch_hcd->prev_touch_jiffies = jiffies;

	touch_hcd->diag.event = 1;
	wake_up_interruptible(&touch_hcd->diag.wait);

exit:
	mutex_unlock(&touch_hcd->report_mutex);

	return;
}

/**
 * touch_set_input_params() - Set input parameters
 *
 * Set the input parameters of the input device based on the information
 * retrieved from the application information packet. In addition, set up an
 * array for tracking the status of touch objects.
 */
static int touch_set_input_params(void)
{
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

	input_set_abs_params(touch_hcd->input_dev,
			ABS_MT_POSITION_X, 0, touch_hcd->max_x, 0, 0);
	input_set_abs_params(touch_hcd->input_dev,
			ABS_MT_POSITION_Y, 0, touch_hcd->max_y, 0, 0);

	input_mt_init_slots(touch_hcd->input_dev, touch_hcd->max_objects,
			INPUT_MT_DIRECT);

	input_set_abs_params(touch_hcd->input_dev, ABS_MT_TOUCH_MAJOR, 0, 50, 0, 0);
	input_set_abs_params(touch_hcd->input_dev, ABS_MT_TOUCH_MINOR, 0, 50, 0, 0);
	input_set_abs_params(touch_hcd->input_dev, ABS_MT_ORIENTATION, 0, 1, 0, 0);
	input_set_abs_params(touch_hcd->input_dev, ABS_MT_PRESSURE,    0, 255, 0, 0);

	touch_hcd->input_params.max_x = touch_hcd->max_x;
	touch_hcd->input_params.max_y = touch_hcd->max_y;
	touch_hcd->input_params.max_objects = touch_hcd->max_objects;

	if (touch_hcd->max_objects == 0)
		return 0;

	kfree(touch_hcd->prev_status);
	touch_hcd->prev_status = kzalloc(touch_hcd->max_objects, GFP_KERNEL);
	if (!touch_hcd->prev_status) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to allocate memory for touch_hcd->prev_status\n");
		return -ENOMEM;
	}

	kfree(touch_hcd->diag.fingers);
	touch_hcd->diag.fingers = kzalloc(touch_hcd->max_objects * sizeof(struct touch_info), GFP_KERNEL);
	if (!touch_hcd->diag.fingers) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to allocate memory for touch_hcd->diag.fingers\n");
		return -ENOMEM;
	}

	kfree(touch_hcd->prev_touch_info);
	touch_hcd->prev_touch_info = kzalloc(touch_hcd->max_objects * sizeof(struct touch_info), GFP_KERNEL);
	if (!touch_hcd->prev_touch_info) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to allocate memory for touch_hcd->prev_touch_info\n");
		return -ENOMEM;
	}

	return 0;
}

/**
 * touch_get_input_params() - Get input parameters
 *
 * Retrieve the input parameters to register with the input subsystem for
 * the input device from the application information packet. In addition,
 * the touch report configuration is retrieved and stored.
 */
static int touch_get_input_params(void)
{
	int retval;
	unsigned int temp;
	struct syna_tcm_app_info *app_info;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	const struct syna_tcm_board_data *bdata = tcm_hcd->hw_if->bdata;

	app_info = &tcm_hcd->app_info;
	touch_hcd->max_x = le2_to_uint(app_info->max_x);
	touch_hcd->max_y = le2_to_uint(app_info->max_y);
	touch_hcd->max_objects = le2_to_uint(app_info->max_objects);

	if (bdata->swap_axes) {
		temp = touch_hcd->max_x;
		touch_hcd->max_x = touch_hcd->max_y;
		touch_hcd->max_y = temp;
	}

	LOCK_BUFFER(tcm_hcd->config);

	retval = tcm_hcd->write_message(tcm_hcd,
			CMD_GET_TOUCH_REPORT_CONFIG,
			NULL,
			0,
			&tcm_hcd->config.buf,
			&tcm_hcd->config.buf_size,
			&tcm_hcd->config.data_length,
			0);
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to write command %s\n",
				STR(CMD_GET_TOUCH_REPORT_CONFIG));
		UNLOCK_BUFFER(tcm_hcd->config);
		return retval;
	}

	UNLOCK_BUFFER(tcm_hcd->config);

	return 0;
}

/**
 * touch_set_input_dev() - Set up input device
 *
 * Allocate an input device, configure the input device based on the particular
 * input events to be reported, and register the input device with the input
 * subsystem.
 */
static int touch_set_input_dev(void)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

	touch_hcd->input_dev = input_allocate_device();
	if (touch_hcd->input_dev == NULL) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to allocate input device\n");
		return -ENODEV;
	}

	touch_hcd->input_dev->name = TOUCH_INPUT_NAME;
	touch_hcd->input_dev->phys = TOUCH_INPUT_PHYS_PATH;
	touch_hcd->input_dev->id.product = SYNAPTICS_TCM_ID_PRODUCT;
	touch_hcd->input_dev->id.version = SYNAPTICS_TCM_ID_VERSION;
	touch_hcd->input_dev->dev.parent = tcm_hcd->pdev->dev.parent;
	input_set_drvdata(touch_hcd->input_dev, tcm_hcd);

	set_bit(EV_SYN, touch_hcd->input_dev->evbit);
	set_bit(EV_KEY, touch_hcd->input_dev->evbit);
	set_bit(EV_ABS, touch_hcd->input_dev->evbit);
	set_bit(BTN_TOUCH, touch_hcd->input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, touch_hcd->input_dev->keybit);
#ifdef INPUT_PROP_DIRECT
	set_bit(INPUT_PROP_DIRECT, touch_hcd->input_dev->propbit);
#endif

#ifdef WAKEUP_GESTURE
	set_bit(KEY_WAKEUP, touch_hcd->input_dev->keybit);
	input_set_capability(touch_hcd->input_dev, EV_KEY, KEY_WAKEUP);
#endif

	retval = touch_set_input_params();
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to set input parameters\n");
		input_free_device(touch_hcd->input_dev);
		touch_hcd->input_dev = NULL;
		return retval;
	}

	retval = input_register_device(touch_hcd->input_dev);
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to register input device\n");
		input_free_device(touch_hcd->input_dev);
		touch_hcd->input_dev = NULL;
		return retval;
	}

	return 0;
}

/**
 * touch_set_report_config() - Set touch report configuration
 *
 * Send the SET_TOUCH_REPORT_CONFIG command to configure the format and content
 * of the touch report.
 */
static int touch_set_report_config(void)
{
	int retval;
	unsigned int idx;
	unsigned int length;
	struct syna_tcm_app_info *app_info;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

#ifdef USE_DEFAULT_TOUCH_REPORT_CONFIG
	return 0;
#endif

	app_info = &tcm_hcd->app_info;
	length = le2_to_uint(app_info->max_touch_report_config_size);

	if (length < TOUCH_REPORT_CONFIG_SIZE) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Invalid maximum touch report config size\n");
		return -EINVAL;
	}

	LOCK_BUFFER(touch_hcd->out);

	retval = syna_tcm_alloc_mem(tcm_hcd,
			&touch_hcd->out,
			length);
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to allocate memory for touch_hcd->out.buf\n");
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	idx = 0;
#ifdef WAKEUP_GESTURE
	touch_hcd->out.buf[idx++] = TOUCH_GESTURE_DOUBLE_TAP;
	touch_hcd->out.buf[idx++] = 8;
#endif
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_ACTIVE_OBJECT;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_INDEX;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_CLASSIFICATION;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_POSITION;
	touch_hcd->out.buf[idx++] = 12;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_POSITION;
	touch_hcd->out.buf[idx++] = 12;
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_END;
	touch_hcd->out.buf[idx++] = TOUCH_END;

	LOCK_BUFFER(touch_hcd->resp);

	retval = tcm_hcd->write_message(tcm_hcd,
			CMD_SET_TOUCH_REPORT_CONFIG,
			touch_hcd->out.buf,
			length,
			&touch_hcd->resp.buf,
			&touch_hcd->resp.buf_size,
			&touch_hcd->resp.data_length,
			0);
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to write command %s\n",
				STR(CMD_SET_TOUCH_REPORT_CONFIG));
		UNLOCK_BUFFER(touch_hcd->resp);
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	UNLOCK_BUFFER(touch_hcd->resp);
	UNLOCK_BUFFER(touch_hcd->out);

	return 0;
}

/**
 * touch_check_input_params() - Check input parameters
 *
 * Check if any of the input parameters registered with the input subsystem for
 * the input device has changed.
 */
static int touch_check_input_params(void)
{
	unsigned int size;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

	if (touch_hcd->max_x == 0 && touch_hcd->max_y == 0)
		return 0;

	if (touch_hcd->input_params.max_objects != touch_hcd->max_objects) {
		kfree(touch_hcd->touch_data.object_data);
		size = sizeof(*touch_hcd->touch_data.object_data);
		size *= touch_hcd->max_objects;
		touch_hcd->touch_data.object_data = kzalloc(size, GFP_KERNEL);
		if (!touch_hcd->touch_data.object_data) {
			LOGE(tcm_hcd->pdev->dev.parent,
					"Failed to allocate memory for touch_hcd->touch_data.object_data\n");
			return -ENOMEM;
		}
#if 0	// for DIAG
		return 1;
#endif
	}

	if (touch_hcd->input_params.max_x != touch_hcd->max_x)
		return 1;

	if (touch_hcd->input_params.max_y != touch_hcd->max_y)
		return 1;

	return 0;
}

/**
 * touch_set_input_reporting() - Configure touch report and set up new input
 * device if necessary
 *
 * After a device reset event, configure the touch report and set up a new input
 * device if any of the input parameters has changed after the device reset.
 */
static int touch_set_input_reporting(void)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;

	if (tcm_hcd->id_info.mode != MODE_APPLICATION ||
			tcm_hcd->app_status != APP_STATUS_OK) {
//		LOGN(tcm_hcd->pdev->dev.parent,
//				"Application firmware not running\n");
		return 0;
	}

	touch_hcd->report_touch = false;

	touch_free_objects();

	mutex_lock(&touch_hcd->report_mutex);

	retval = touch_set_report_config();
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to set report config\n");
		goto exit;
	}

	retval = touch_get_input_params();
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to get input parameters\n");
		goto exit;
	}

	retval = touch_check_input_params();
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to check input parameters\n");
		goto exit;
	} else if (retval == 0) {
//		LOGN(tcm_hcd->pdev->dev.parent,
//				"Input parameters unchanged\n");
		goto exit;
	}

	if (touch_hcd->input_dev != NULL) {
		input_unregister_device(touch_hcd->input_dev);
		touch_hcd->input_dev = NULL;
	}

	retval = touch_set_input_dev();
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to set up input device\n");
		goto exit;
	}

exit:
	mutex_unlock(&touch_hcd->report_mutex);

	touch_hcd->report_touch = retval < 0 ? false : true;

	return retval;
}

static int touch_set_force_default_input_dev(void)
{
	int retval;
	struct syna_tcm_hcd *tcm_hcd = touch_hcd->tcm_hcd;
	unsigned int size;

	touch_hcd->report_touch = false;

	mutex_lock(&touch_hcd->report_mutex);

	if (touch_hcd->input_dev == NULL) {
		touch_hcd->max_x = TOUCH_DEFAULT_MAX_X;
		touch_hcd->max_y = TOUCH_DEFAULT_MAX_Y;
		touch_hcd->max_objects = TOUCH_DEFAULT_MAX_OBJEXTS;

		size = sizeof(*touch_hcd->touch_data.object_data);
		size *= touch_hcd->max_objects;
		touch_hcd->touch_data.object_data = kzalloc(size, GFP_KERNEL);
		if (!touch_hcd->touch_data.object_data) {
			LOGE(tcm_hcd->pdev->dev.parent,
					"Failed to allocate memory for touch_hcd->touch_data.object_data\n");
		}
		else{
			retval = touch_set_input_dev();
			if (retval < 0) {
				LOGE(tcm_hcd->pdev->dev.parent,
						"Failed to set up input device\n");
			}
		}
	}

	mutex_unlock(&touch_hcd->report_mutex);

	return 0;
}

static int touch_init(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;
	int idx;

	touch_hcd = kzalloc(sizeof(*touch_hcd), GFP_KERNEL);
	if (!touch_hcd) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to allocate memory for touch_hcd\n");
		return -ENOMEM;
	}

	touch_hcd->tcm_hcd = tcm_hcd;

	mutex_init(&touch_hcd->report_mutex);

	INIT_BUFFER(touch_hcd->out, false);
	INIT_BUFFER(touch_hcd->resp, false);

	retval = touch_set_input_reporting();
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to set up input reporting\n");
		goto err_set_input_reporting;
	}

	tcm_hcd->report_touch = touch_report;

	init_waitqueue_head(&touch_hcd->diag.wait);

	touch_hcd->sysfs_dir = kobject_create_and_add(SYSFS_DIR_NAME,
			tcm_hcd->sysfs_dir);
	if (!touch_hcd->sysfs_dir) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to create sysfs directory\n");
		goto err_sysfs_create_dir;
	}

	for (idx = 0; idx < ARRAY_SIZE(attrs); idx++) {
		retval = sysfs_create_file(touch_hcd->sysfs_dir,
				&attrs[idx].attr);
		if (retval < 0) {
			LOGE(tcm_hcd->pdev->dev.parent,
					"Failed to create sysfs file\n");
			goto err_sysfs_create_file;
		}
	}

	return 0;

err_sysfs_create_file:
	for (idx--; idx >= 0; idx--)
		sysfs_remove_file(touch_hcd->sysfs_dir, &attrs[idx].attr);

	kobject_put(touch_hcd->sysfs_dir);

err_sysfs_create_dir:
err_set_input_reporting:
	kfree(touch_hcd->prev_touch_info);
	kfree(touch_hcd->diag.fingers);
	kfree(touch_hcd->touch_data.object_data);
	kfree(touch_hcd->prev_status);

	RELEASE_BUFFER(touch_hcd->resp);
	RELEASE_BUFFER(touch_hcd->out);

	kfree(touch_hcd);
	touch_hcd = NULL;

	return retval;
}

static int touch_remove(struct syna_tcm_hcd *tcm_hcd)
{
	int idx;

	if (!touch_hcd)
		goto exit;

	tcm_hcd->report_touch = NULL;

	if (touch_hcd->input_dev != NULL) {
		input_unregister_device(touch_hcd->input_dev);
		touch_hcd->input_dev = NULL;
	}

	for (idx = 0; idx < ARRAY_SIZE(attrs); idx++)
		sysfs_remove_file(touch_hcd->sysfs_dir, &attrs[idx].attr);

	kobject_put(touch_hcd->sysfs_dir);

	kfree(touch_hcd->prev_touch_info);
	kfree(touch_hcd->diag.fingers);
	kfree(touch_hcd->touch_data.object_data);
	kfree(touch_hcd->prev_status);

	RELEASE_BUFFER(touch_hcd->resp);
	RELEASE_BUFFER(touch_hcd->out);

	kfree(touch_hcd);
	touch_hcd = NULL;

exit:
	complete(&touch_remove_complete);

	return 0;
}

static int touch_syncbox(struct syna_tcm_hcd *tcm_hcd)
{
	if (!touch_hcd)
		return 0;

	switch (tcm_hcd->report.id) {
	case REPORT_IDENTIFY:
		touch_free_objects();
		break;
	case REPORT_TOUCH:
		touch_report();
		break;
	default:
		break;
	}

	return 0;
}

static int touch_asyncbox(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;

	if (!touch_hcd)
		return 0;

	switch (tcm_hcd->async_report_id) {
	case REPORT_IDENTIFY:
		if (tcm_hcd->id_info.mode != MODE_APPLICATION)
			break;
		retval = tcm_hcd->identify(tcm_hcd, false);
		if (retval < 0) {
			LOGE(tcm_hcd->pdev->dev.parent,
					"Failed to do identification\n");
			return retval;
		}
		retval = touch_set_input_reporting();
		if (retval < 0) {
			LOGE(tcm_hcd->pdev->dev.parent,
					"Failed to set up input reporting\n");
			return retval;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int touch_reset(struct syna_tcm_hcd *tcm_hcd)
{
	int retval;

	if (!touch_hcd) {
		retval = touch_init(tcm_hcd);
		return retval;
	}

	if (tcm_hcd->id_info.mode == MODE_APPLICATION) {
		retval = touch_set_input_reporting();
		if (retval < 0) {
			LOGE(tcm_hcd->pdev->dev.parent,
					"Failed to set up input reporting\n");
			return retval;
		}
	}
	else if(touch_hcd->input_dev == NULL){
		touch_set_force_default_input_dev();
	}

	return 0;
}

static int touch_suspend(struct syna_tcm_hcd *tcm_hcd)
{
#ifdef WAKEUP_GESTURE
	int retval;
#endif

	if (!touch_hcd)
		return 0;

	touch_free_objects();

	touchevent_notifier_call_chain(0, NULL);

#ifdef WAKEUP_GESTURE
	if (!touch_hcd->irq_wake) {
		enable_irq_wake(tcm_hcd->irq);
		touch_hcd->irq_wake = true;
	}

	retval = tcm_hcd->set_dynamic_config(tcm_hcd,
			DC_IN_WAKEUP_GESTURE_MODE,
			1);
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to enable wakeup gesture mode\n");
		return retval;
	}
#endif

	return 0;
}

static int touch_resume(struct syna_tcm_hcd *tcm_hcd)
{
#ifdef WAKEUP_GESTURE
	int retval;
#endif

	if (!touch_hcd)
		return 0;

#ifdef WAKEUP_GESTURE
	if (touch_hcd->irq_wake) {
		disable_irq_wake(tcm_hcd->irq);
		touch_hcd->irq_wake = false;
	}

	retval = tcm_hcd->set_dynamic_config(tcm_hcd,
			DC_IN_WAKEUP_GESTURE_MODE,
			0);
	if (retval < 0) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to disable wakeup gesture mode\n");
		return retval;
	}
#endif

	return 0;
}

static struct syna_tcm_module_cb touch_module = {
	.type = TCM_TOUCH,
	.init = touch_init,
	.remove = touch_remove,
	.syncbox = touch_syncbox,
	.asyncbox = touch_asyncbox,
	.reset = touch_reset,
	.suspend = touch_suspend,
	.resume = touch_resume,
};

int touch_module_init(void)
{
	return syna_tcm_add_module(&touch_module, true);
}

void touch_module_exit(void)
{
	syna_tcm_add_module(&touch_module, false);

	wait_for_completion(&touch_remove_complete);

	return;
}
