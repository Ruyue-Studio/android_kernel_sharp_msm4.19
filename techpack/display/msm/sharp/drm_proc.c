/*
 *
 * Copyright (C) 2018 SHARP CORPORATION
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
/* ------------------------------------------------------------------------- */
/* INCLUDE FILES                                                             */
/* ------------------------------------------------------------------------- */
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/fb.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fb.h>
#include <linux/leds.h>
#include <linux/debugfs.h>
#include "../dsi/dsi_display.h"
#include "drm_diag.h"
#include <video/mipi_display.h>
//#include "drm_mfr.h"
#include "../msm_drv.h"
#include "drm_cmn.h"
#include <drm/drm_sharp.h>
#include <drm/drm_panel.h>
#include "sde/sde_encoder_phys.h"
#include "drm_bias.h"
/* ------------------------------------------------------------------------- */
/* DEFINE                                                                    */
/* ------------------------------------------------------------------------- */
#if defined(CONFIG_ARCH_CHARA)
#define DRM_LCDDR_WRITE_BUF_MAX (255)
#define DRM_LCDDR_READ_BUF_MAX  (255)
#define USES_PANEL_SVEN
#define DRM_BIAS_CALC_SUPPORT
#elif defined(CONFIG_ARCH_RECOA)
#define DRM_LCDDR_WRITE_BUF_MAX (2)
#define DRM_LCDDR_READ_BUF_MAX  (5)
#define USES_PANEL_ELSA
#elif defined(CONFIG_ARCH_BANAGHER)
#define USES_PANEL_RAVEN
#define DRM_FPS_LED_PANEL_SUPPORT
#define DRM_LCDDR_WRITE_BUF_MAX (127)
#define DRM_LCDDR_READ_BUF_MAX  (127)
#else /* CONFIG_ARCH_BANAGHER */
#define USES_PANEL_STEIN
#define DRM_LCDDR_WRITE_BUF_MAX (107)			/* max parameter size of work buffer */
#define DRM_LCDDR_WRITE_BUF_MAX_STEIN    (15)	/* max parameter size of the stein panel */
#define DRM_LCDDR_WRITE_BUF_MAX_SINANJU  (107)	/* max parameter size of the sinanju panel */
#define DRM_LCDDR_READ_BUF_MAX  (107)			/* max parameter size of work buffer */
#define DRM_LCDDR_READ_BUF_MAX_STEIN    (15)	/* max parameter size of the stein panel */
#define DRM_LCDDR_READ_BUF_MAX_SINANJU  (107)	/* max parameter size of the sinanju panel */
#endif /* CONFIG_ARCH_BANAGHER */

#define PROC_BUF_LENGTH            (4096)
#define PROC_BLOCK_SIZE	(PAGE_SIZE - 1024)

#define DRM_PROC_DSI_COLLECT_MAX      (512)
#define DRM_PROC_DSI_PAYLOAD_BUF_LEN  (4 * DRM_PROC_DSI_COLLECT_MAX)


#define DRM_DSI_CTRL_DSI_EN             BIT(0)
#define DRM_DSI_CTRL_VIDEO_MODE_EN      BIT(1)
#define DRM_DSI_CTRL_CMD_MODE_EN        BIT(2)

#define DRM_LEN_ID                   (2)
#define DRM_LEN_PARAM                (4)
#define DRM_PARAM_MAX                (4)

enum {
	DRM_DEBUG_MFR_SET       = 10,
	DRM_DEBUG_FPS_MFR       = 40,
	DRM_DEBUG_FPS_LOW_MODE  = 50,
#ifdef DRM_BIAS_CALC_SUPPORT
	DRM_DEBUG_BIAS_CALC     = 70,
#endif /* DRM_BIAS_CALC_SUPPORT */
	DRM_DEBUG_FLICKER_SET   = 80,
	DRM_DEBUG_FLICKER_GET   = 81,
	DRM_DEBUG_DSI_DCS_WRITE = 90,
	DRM_DEBUG_DSI_DCS_READ  = 91,
	DRM_DEBUG_DSI_GEN_WRITE = 92,
	DRM_DEBUG_DSI_GEN_READ  = 93,
};

#define DRM_LOG_ENABLE

struct drm_dsi_cmd_req {
	char dtype;
	unsigned char addr;
	unsigned char size;
	unsigned char mode;
	unsigned char *data;
};

struct drm_procfs {
	int id;
	int par[4];
};

#ifdef DRM_FPS_LED_PANEL_SUPPORT

struct shled_tri_led {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

DEFINE_LED_TRIGGER(led_fps_r_trigger);
DEFINE_LED_TRIGGER(led_fps_b_trigger);
DEFINE_LED_TRIGGER(led_fps_g_trigger);

#ifdef USES_PANEL_RAVEN
#define ADDR_PAGE               (0xDE)
#define PAGE_FPS_REG            (0x04)
#define ADDR_FPS_REG            (0xEE)
#define DEFAULT_PAGE_FPS_REG    (0x00)
#define FPS_LED_INTERVAL        (67*1000)
#else
#define ADDR_PAGE               (0xFF)
#define PAGE_FPS_REG            (0x26)
#define ADDR_FPS_REG            (0x6D)
#define DEFAULT_PAGE_FPS_REG    (0x10)
#define FPS_LED_INTERVAL        (1000*1000)
#endif

enum {
	FPS_LED_STATE_NONE,
	FPS_LED_STATE_1HZ,
	FPS_LED_STATE_15HZ,
	FPS_LED_STATE_30HZ,
	FPS_LED_STATE_45HZ,
	FPS_LED_STATE_60HZ,
	FPS_LED_STATE_120HZ,
	MAX_FPS_LED_STATE
};
static const char *drm_fps_led_state_str[MAX_FPS_LED_STATE] = {
	"NONE",
	"1HZ",
	"15HZ",
	"30HZ",
	"45HZ",
	"60HZ",
	"120HZ",
};
static const char drm_fps_led_color[MAX_FPS_LED_STATE][3] = {
	[FPS_LED_STATE_NONE]  = {0, 0, 0},
	[FPS_LED_STATE_1HZ]   = {0, 0, 255},
	[FPS_LED_STATE_15HZ]  = {0, 255, 0},
	[FPS_LED_STATE_30HZ]  = {255, 255, 0},
	[FPS_LED_STATE_45HZ]  = {0, 255, 255},
	[FPS_LED_STATE_60HZ]  = {255, 0, 0},
	[FPS_LED_STATE_120HZ] = {255, 255, 255},
};

static struct {
	bool enable;
	bool suspend;
	bool panel_on;
	int interval;
	int max_frame_rate;
	struct workqueue_struct *workq;
	struct delayed_work work;
} drm_fps_led_ctx = {0};
struct notifier_block drm_notif;
#endif /* DRM_FPS_LED_PANEL_SUPPORT */

#define DRM_DEBUG_CONSOLE(fmt, args...) \
		do { \
			int buflen = 0; \
			int remain = PROC_BUF_LENGTH - proc_buf_pos - 1; \
			if (remain > 0) { \
				buflen = snprintf(&proc_buf[proc_buf_pos], remain, fmt, ## args); \
				proc_buf_pos += (buflen > 0) ? buflen : 0; \
			} \
		} while (0)

#define DRM_PROC_PRINT_CMD_DESC(cmds, size)												\
		do {																				\
			int i;																			\
			char buf[64];																	\
			struct drm_dsi_cmd_desc *cmd;												\
			for (i = 0; i < size; i++) {													\
				cmd = &cmds[i];																\
				hex_dump_to_buffer(cmd->payload, cmd->dlen, 16, 1, buf, sizeof(buf), 0);	\
				pr_debug("%s: dtype=%02x dlen=%d mode=%02x wait=%d data=%s\n", __func__,	\
						(unsigned char)cmd->dtype, cmd->dlen, cmd->mode, cmd->wait, buf);	\
			}																				\
		} while (0)

#define DRM_PROC_PRINT_RX_DATA(data, size)												\
		do {																				\
			char buf[64];																	\
			hex_dump_to_buffer(data, size, 16, 1, buf, sizeof(buf), 0);						\
			pr_debug("%s: rx_data=%s\n", __func__, buf);									\
		} while (0)

static unsigned char proc_buf[PROC_BUF_LENGTH] = {0};
static unsigned int  proc_buf_pos = 0;
//static struct drm_panel_data *drm_panel_ctrl = NULL;
//static int drm_proc_callback_data = 0;
static struct semaphore drm_proc_host_dsi_cmd_sem;
static struct dsi_display *display_ctx = NULL;

static int drm_proc_write(struct file *file, const char *buffer, unsigned long count, void *data);
static int drm_proc_read(char *page, char **start, off_t offset, int count, int *eof, void *data);
static int drm_proc_file_open(struct inode *inode, struct file *file);
static ssize_t drm_proc_file_read(struct file *file, char __user *buf, size_t nbytes, loff_t *ppos);
static ssize_t drm_proc_file_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos);
static void drm_proc_debugfs_create(void);
static int drm_panel_mipi_dsi_cmds_tx(struct drm_dsi_cmd_desc *cmds, int cnt);
static int drm_panel_mipi_dsi_cmds_rx(unsigned char *rbuf, struct drm_dsi_cmd_desc *cmds, unsigned char size);
static void drm_panel_dsi_wlog(struct drm_dsi_cmd_desc *cmds, int cmdslen);
static void drm_panel_dsi_rlog(char addr, char *rbuf, int len);
static int drm_panel_dsi_write_reg(struct drm_dsi_cmd_req *req);
static int drm_panel_dsi_read_reg(struct drm_dsi_cmd_req *req);
#ifdef DRM_FPS_LED_PANEL_SUPPORT
static int drm_fps_led_start(void);

static int drm_fps_led_stop(void);
static void drm_fps_led_work(struct work_struct *work);
#if !defined(CONFIG_SHARP_DRM_HR_VID)
static int drm_fps_set_led_cmd(void);
static int drm_fps_led_read_reg(unsigned char *fps_reg);
#else
static int drm_mfr_get_led_state(int mfr);
#endif
void drm_fps_led_resume(void);
void drm_fps_led_suspend(void);
int drm_notifier_callback(struct notifier_block *self, unsigned long event, void *data);
#endif /* DRM_FPS_LED_PANEL_SUPPORT */
static int drm_proc_mfr(struct dsi_display *display, int value);
#if defined(USES_PANEL_ELSA)
static int drm_proc_mfr_elsa(struct dsi_display *display, int value);
#elif defined(USES_PANEL_SVEN)
static int drm_proc_mfr_sven(struct dsi_display *display, int value);
#elif defined(USES_PANEL_RAVEN)
static int drm_proc_mfr_raven(struct dsi_display *display, int value);
#else /* USES_PANEL_xxx */
static int drm_proc_mfr_rosetta(struct dsi_display *display, int value);
#endif /* USES_PANEL_xxx */

static void drm_proc_get_params(char kbuf[], unsigned long len, struct drm_procfs *drm_pfs);
static bool drm_proc_chk_disp_state(struct dsi_display *display);
static int drm_proc_read_cases(struct drm_procfs *drm_pfs, struct dsi_display *dsi_display);
static bool drm_proc_set_len_params(unsigned char **pparam, const char *buffer, char *buf, unsigned long count);
static int drm_proc_write_cases(unsigned char *param, struct drm_procfs *drm_pfs, struct dsi_display *dsi_display);
static int drm_proc_mfr_case(struct dsi_display *display, struct drm_procfs *drm_pfs);
#ifdef DRM_FPS_LED_PANEL_SUPPORT
static int drm_proc_led_start_stop(struct drm_procfs *drm_pfs);
#endif /* DRM_FPS_LED_PANEL_SUPPORT */
static int drm_proc_set_flicker(struct drm_procfs *drm_pfs);
static int drm_proc_get_flicker(struct drm_procfs *drm_pfs);
#ifdef DRM_BIAS_CALC_SUPPORT
static int drm_proc_bias_calc(unsigned char *param, struct drm_procfs *drm_pfs);
#endif /* DRM_BIAS_CALC_SUPPORT */

static void drm_proc_lock_host_dsi_cmd(void)
{
	down(&drm_proc_host_dsi_cmd_sem);
}

static void drm_proc_unlock_host_dsi_cmd(void)
{
	up(&drm_proc_host_dsi_cmd_sem);
}

static void drm_proc_dsi_to_drm_dsi(const struct drm_dsi_cmd_desc *shdisp_cmds,
				struct dsi_cmd_desc *drm_cmds,
				unsigned char *rx_data, int rx_size)
{
	if (!shdisp_cmds || !drm_cmds) {
		pr_err("%s: Invalid input data\n", __func__);
		return;
	}
	if (shdisp_cmds->dlen > DRM_PROC_DSI_PAYLOAD_BUF_LEN) {
		pr_err("LCDERR: buffer size over %s: shdisp_cmds->dlen=%d\n",
						__func__, shdisp_cmds->dlen);
		return;
	}

	/* read */
	{
		drm_cmds->msg.rx_buf = rx_data;
		drm_cmds->msg.rx_len = rx_size;
	}
	/* write */
	{
		static char payloads[DRM_PROC_DSI_PAYLOAD_BUF_LEN];
		void* tx_buf = payloads;
		memcpy(tx_buf, shdisp_cmds->payload, shdisp_cmds->dlen);
		drm_cmds->msg.tx_buf      = tx_buf;
		drm_cmds->msg.tx_len   = shdisp_cmds->dlen;
	}
	/* msg flag */
	{
		int msg_flags;
		msg_flags = (shdisp_cmds->mode & 0x10) ? MIPI_DSI_MSG_USE_LPM : 0;
		msg_flags |= MIPI_DSI_MSG_UNICAST;
		drm_cmds->msg.flags = msg_flags;
	}
	drm_cmds->msg.type  = shdisp_cmds->dtype;
	drm_cmds->last_command = 1;
	drm_cmds->msg.channel	    = 0;
	drm_cmds->post_wait_ms   = shdisp_cmds->wait ? ((shdisp_cmds->wait+999)/1000) : 0; /* drm_dsi(ms) <- drm_dsi(usec) */
	drm_cmds->msg.ctrl  = 0;	/* 0 = dsi-master */
}

static int drm_proc_kick_cmd(struct dsi_cmd_desc *cmd, bool is_read)
{
	int ret = 0;
	struct dsi_display *display = display_ctx;
	u32 flags = 0;

	pr_debug("%s: in\n", __func__);
	if (!cmd) {
		pr_err("%s: no dsi_cmd_desc\n", __func__);
		return -EINVAL;
	}

	if (!display || !display->panel) {
		pr_err("%s: invalid input\n", __func__);
		return -EINVAL;
	}
	pr_debug("%s: display = %p, panel = %p \n",
					__func__, display, display->panel);

	/* acquire panel_lock to make sure no commands are in progress */
	dsi_panel_acquire_panel_lock(display->panel);

	ret = dsi_display_cmd_engine_ctrl(display, true);
	if (ret) {
		pr_err("%s: failed dsi_display_cmd_engine_enable\n", __func__);
		goto error_disable_panel_lock;
	}

	flags = DSI_CTRL_CMD_FETCH_MEMORY;
	if (is_read) {
		flags |= DSI_CTRL_CMD_READ;
	}
	if (cmd->last_command) {
		cmd->msg.flags |=  MIPI_DSI_MSG_LASTCOMMAND;
		flags |= DSI_CTRL_CMD_LAST_COMMAND;
	}

	ret = dsi_ctrl_cmd_transfer(display->ctrl[0].ctrl, &cmd->msg, &flags);
	if ((!is_read && ret) || (is_read && (ret <= 0))) {
		pr_err("%s: failed dsi_ctrl_cmd_transfer, ret = %d\n",
							__func__, ret);
		goto error_disable_cmd_engine;
	}

	if (cmd->post_wait_ms) {
		msleep(cmd->post_wait_ms);
	}

error_disable_cmd_engine:
	(void)dsi_display_cmd_engine_ctrl(display, false);
error_disable_panel_lock:
	/* release panel_lock */
	dsi_panel_release_panel_lock(display->panel);

	pr_debug("%s: end. ret=%d\n", __func__, ret);
	return 0;
}

static int drm_proc_host_dsi_tx(struct drm_dsi_cmd_desc *drm_cmd, int size)
{
	int ret = 0;
	struct dsi_cmd_desc dsi_cmd;

	pr_debug("%s: in\n", __func__);
	if (!drm_cmd) {
		pr_err("%s: Invalid input data\n", __func__);
		return ret;
	}
	DRM_PROC_PRINT_CMD_DESC(drm_cmd, size);

	drm_proc_lock_host_dsi_cmd();

	memset(&dsi_cmd, 0, sizeof(dsi_cmd) );

	drm_proc_dsi_to_drm_dsi(drm_cmd, &dsi_cmd, NULL, 0);

	ret = drm_proc_kick_cmd(&dsi_cmd, false);
	if (ret) {
		drm_proc_unlock_host_dsi_cmd();
		pr_err("%s: failed drm_proc_kick_cmd, ret=0x%d\n", __func__, ret);
		return ret;
	}

	drm_proc_unlock_host_dsi_cmd();

	pr_debug("%s: out ret=%d\n", __func__, ret);
	return 0;
}

static int drm_proc_host_dsi_rx(struct drm_dsi_cmd_desc *drm_cmd,
					unsigned char *rx_data, int rx_size)
{
	int ret = 0;
	struct dsi_cmd_desc dsi_cmd;

	pr_debug("%s: in rx_data = %s, rx_size = %d\n",
						__func__, rx_data, rx_size);
	DRM_PROC_PRINT_CMD_DESC(drm_cmd, 1);

	drm_proc_lock_host_dsi_cmd();

	memset(&dsi_cmd, 0, sizeof(dsi_cmd) );

	drm_proc_dsi_to_drm_dsi(drm_cmd, &dsi_cmd, rx_data, rx_size);

	ret = drm_proc_kick_cmd(&dsi_cmd, true);
	if (ret) {
		drm_proc_unlock_host_dsi_cmd();
		pr_err("%s: failed drm_proc_kick_cmd, ret=0x%d\n", __func__, ret);
		return ret;
	}

	drm_proc_unlock_host_dsi_cmd();

	DRM_PROC_PRINT_RX_DATA(rx_data, rx_size);
	pr_debug("%s: out\n", __func__);

	return 0;
}

int drm_proc_init(struct dsi_display *display)
{
	if (!display) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}
	display_ctx = display;
	drm_proc_debugfs_create();
	sema_init(&drm_proc_host_dsi_cmd_sem, 1);

#ifdef DRM_FPS_LED_PANEL_SUPPORT
	drm_fps_led_ctx.workq = create_singlethread_workqueue("drm_fps_led_workq");
	INIT_DELAYED_WORK(&drm_fps_led_ctx.work, drm_fps_led_work);
#endif /* DRM_FPS_LED_PANEL_SUPPORT */

	pr_info("[%s] success.\n", __func__);
	return 0;
}

static const struct file_operations drm_proc_fops = {
	.owner			= THIS_MODULE,
	.open			= drm_proc_file_open,
	.write			= drm_proc_file_write,
	.read			= drm_proc_file_read,
	.release		= single_release,
};

static void drm_proc_debugfs_create(void)
{
	struct dentry * root;
	struct dsi_display *display = display_ctx;
	root = debugfs_create_dir("drm", 0);

	if (!display) {
		pr_err("%s: Invalid input data\n", __func__);
	}
	pr_debug("%s: display=%p\n", __func__, display);
	if (!root) {
		pr_err("LCDERR:[%s] failed to create dbgfs root dir\n", __func__);
	}

	if (!debugfs_create_file("proc",
		S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH,
		root, display, &drm_proc_fops)) {
		pr_err("LCDERR:[%s] failed to create dbgfs(drm_proc)\n", __func__);
	}
}

static int drm_proc_file_open(struct inode *inode, struct file *file)
{
	return single_open(file, NULL, inode->i_private);
}

static int drm_proc_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	unsigned long len = count;
	struct drm_procfs drm_pfs;
	struct dsi_display *display;
	char buf[DRM_LEN_PARAM + 1];
	char kbuf[DRM_LEN_ID + DRM_PARAM_MAX * DRM_LEN_PARAM];
	int i;
	int ret = 0;

	unsigned char *param = NULL;

	proc_buf[0] = 0;
	proc_buf_pos = 0;
	DRM_DEBUG_CONSOLE("NG");

	// make len less 1
	len--;

	// check inputs: count, buffer
	if (len < DRM_LEN_ID) {
		return count;
	}
	if (!buffer) {
		return -EFAULT;
	}

	// Check and Set len value: len > 17 then limit to 17
	if (len > (DRM_LEN_ID + DRM_PARAM_MAX * DRM_LEN_PARAM)) {
		len = DRM_LEN_ID + DRM_PARAM_MAX * DRM_LEN_PARAM;
	}
	// copy from user space to kernel space
	if (copy_from_user(kbuf, buffer, len)) {
		return -EFAULT;
	}

	/**
		Get drm_pfs.id
	*/
	memcpy(buf, kbuf, DRM_LEN_ID);
	buf[DRM_LEN_ID] = '\0';
	drm_pfs.id = simple_strtol(buf, NULL, 10);
	if (!isdigit(*buf) || !isdigit(*(buf+1))) {
		pr_err("LCDERR:[%s] invalid func id = %s\n", __func__, buf);
		return count;
	}

	/**
		Acquire data into drm_pfs.par[]
	*/
	// check validity of (after ID length) data in kbuf before writing to drm_pfs.par[]
	for (i = DRM_LEN_ID; i < len; i++) {
		if (!isxdigit(*(kbuf+i))) {
			pr_err("LCDERR:[%s] invalid param pos[%d] (%c)\n",
			__func__,i,  (*(kbuf+i)));
			return count;
		}
	}

	/* Get Parameters */
	drm_proc_get_params(kbuf, len, &drm_pfs);

	/* Get dsi_display */
	display = display_ctx;
	if (!display) {
		pr_err("%s: no dsi_display\n", __func__);
		return count;
	}
	pr_debug("%s: display = %p\n", __func__, display);

	/**
		Execute the processes for each case
	*/
	switch (drm_pfs.id) {
	// read cases
	case DRM_DEBUG_DSI_GEN_READ:
	case DRM_DEBUG_DSI_DCS_READ:
		/* check display's power state */
		if (drm_proc_chk_disp_state(display)) {
			// execute read commands
			ret = drm_proc_read_cases(&drm_pfs, display);
		}
		break;

	// write cases
	case DRM_DEBUG_DSI_DCS_WRITE:
	case DRM_DEBUG_DSI_GEN_WRITE:
		if (len < 8) {
			pr_err("(%d): DSI_WRITE param error\n", drm_pfs.id);
			break;
		}

		// do allocate memory for temporary data arrays
		if (!drm_proc_set_len_params(&param, buffer, buf, count)) {
			if (param)
				kfree(param);
			return -EFAULT;
		}

		/* check display's power state */
		if (drm_proc_chk_disp_state(display)) {
			// execute write commands
			ret = drm_proc_write_cases(param, &drm_pfs, display);
		}
		if (param)
			kfree(param);
		break;

	// MFR case
	case DRM_DEBUG_MFR_SET:
		/* check display's power state */
		if (drm_proc_chk_disp_state(display)) {
			// process for MFR functionality
			ret = drm_proc_mfr_case(display, &drm_pfs);
		}
		break;

	case DRM_DEBUG_FPS_LOW_MODE:
		ret = drm_base_fps_low_mode();
		printk("[DRM] drm_base_fps_low_mode(%d)\n", ret);
		proc_buf[0] = 0;
		proc_buf_pos = 0;
		DRM_DEBUG_CONSOLE("OK, %d", ret);
		break;

	// Panel support
#ifdef DRM_FPS_LED_PANEL_SUPPORT
	case DRM_DEBUG_FPS_MFR:
		/* check display's power state */
		if (drm_proc_chk_disp_state(display)) {
			// process led start / stop
			ret = drm_proc_led_start_stop(&drm_pfs);
		}
		break;
#endif /* DRM_FPS_LED_PANEL_SUPPORT */

	// set flicker
	case DRM_DEBUG_FLICKER_SET:
		/* check display's power state */
		if (drm_proc_chk_disp_state(display)) {
			// process flicker set case
			ret = drm_proc_set_flicker(&drm_pfs);
		}
		break;

	// get flicker
	case DRM_DEBUG_FLICKER_GET:
		/* check display's power state */
		if (drm_proc_chk_disp_state(display)) {
			// process flicker get case
			ret = drm_proc_get_flicker(&drm_pfs);
		}
		break;

#ifdef DRM_BIAS_CALC_SUPPORT
	case DRM_DEBUG_BIAS_CALC:
		// do allocate memory for temporary data arrays
		if (!drm_proc_set_len_params(&param, buffer, buf, count)) {
			if (param)
				kfree(param);
			return -EFAULT;
		}

		// process On-bias Voltage calculation
		ret = drm_proc_bias_calc(param, &drm_pfs);
		break;
#endif /* DRM_BIAS_CALC_SUPPORT */

	default:
		break;
	}

	// print result and free temp memory
	printk("[DRM] result : %d.\n", ret);

	return count;
}

//-----------------------------------------------------------------------
////////////////////////////// sub routines //////////
/**
	this function aquires drm_pfs.par[4]
*/
static void drm_proc_get_params(char kbuf[], unsigned long len, struct drm_procfs *drm_pfs)
{
	// initialize
	int i;
	char buf[DRM_LEN_PARAM + 1];

	for (i = 0; i < 4; i++) {
		drm_pfs->par[i] = 0;
	}

	// Get drm_pfs.par
	for (i = 0; (i + 1) * DRM_LEN_PARAM <= (len - DRM_LEN_ID); i++) {
		// do memory copy for each index, continue upto full length
		memcpy(buf, &(kbuf[DRM_LEN_ID + i * DRM_LEN_PARAM]), DRM_LEN_PARAM);
		buf[DRM_LEN_PARAM] = '\0';
		if (drm_pfs->id == DRM_DEBUG_MFR_SET) {
			drm_pfs->par[i] = simple_strtol(buf, NULL, 10);
		} else {
			drm_pfs->par[i] = simple_strtol(buf, NULL, 16);
		}
	}
	// print the copied par values
	printk("[DRM] drm_proc_write(%d, 0x%04x, 0x%04x, 0x%04x, 0x%04x)\n", drm_pfs->id,
		drm_pfs->par[0], drm_pfs->par[1], drm_pfs->par[2], drm_pfs->par[3]);
}

/**
	Method without using get_sde_encoder_phys_cmd() function
*/
static int drm_proc_chk_disp_active(struct dsi_display *display)
{
	struct drm_device *dev;
	struct drm_encoder *encoder;

	if (!display) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}
	dev = display->drm_dev;
	if (!dev) {
		pr_err("%s: no drm_dev\n", __func__);
		return -EINVAL;
	}
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (!encoder || !encoder->dev || !encoder->dev->dev_private ||
		!encoder->crtc || (encoder->encoder_type != DRM_MODE_ENCODER_DSI)) {
			continue;
		}
		if (!encoder->crtc->state->active) {
			pr_err("%s: display's power state is OFF\n", __func__);
			return false;
		}
		break;
	}

	return	true;
}

/**
	This funciton checks the display panel state
	return true means paned is active
*/
static bool drm_proc_chk_disp_state(struct dsi_display *display)
{
	// check the panel is powered up
	if (!display->panel->panel_initialized) {
		pr_err("%s: display's power state is OFF\n", __func__);
		return false;
	}
    return drm_proc_chk_disp_active(display);
}

/**
	This function executes read process commands
*/
static int drm_proc_read_cases(struct drm_procfs *drm_pfs, struct dsi_display *dsi_display)
{
	// variables and their initializatioins
	struct drm_dsi_cmd_req dsi_req;
	unsigned char buf[DRM_LCDDR_READ_BUF_MAX + 16];
	int panel_mode = 0;
	int ret = 0, ret2;
	int i;

	// get panel mode from dsi_display
	panel_mode = dsi_display->panel->panel_mode;

	// set memory
	memset(&dsi_req, 0x00, sizeof(dsi_req));
	memset(buf, 0x00, sizeof(buf));
	dsi_req.data = buf;

	// get dsi request type, size, address, mode
	if (drm_pfs->id == DRM_DEBUG_DSI_GEN_READ) {
		dsi_req.dtype = MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM;
	} else {
		dsi_req.dtype = MIPI_DSI_DCS_READ;
	}
	dsi_req.size    = ((drm_pfs->par[0] >> 8) & 0x00FF);
	dsi_req.addr    = ( drm_pfs->par[0]       & 0x00FF);
	dsi_req.mode    = ((drm_pfs->par[1] >> 8) & 0x00FF);

	pr_debug(" Size    : %2d\n", dsi_req.size);
	pr_debug(" Address : %02Xh\n", dsi_req.addr);
	pr_debug(" Mode    : %02Xh\n", dsi_req.mode);

	// stop video mode or set clock ON
	if (panel_mode == DSI_OP_VIDEO_MODE) {
		ret2 = drm_cmn_video_transfer_ctrl(dsi_display, false);
		if (ret2) {
			pr_err("%s: failed stop_video\n", __func__);
			return ret2;
		}
	} else {
		// set clock ON
		dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
				DSI_ALL_CLKS, DSI_CLK_ON);
	}

	// read
	ret = drm_panel_dsi_read_reg(&dsi_req);
	if (ret) {
		pr_err("%s: failed drm_panel_dsi_read_reg\n", __func__);
	}

	// start video mode or set clock OFF
	if (panel_mode == DSI_OP_VIDEO_MODE) {
		ret2 = drm_cmn_video_transfer_ctrl(dsi_display, true);
		if (ret2) {
			pr_err("%s: failed start_video\n", __func__);
			return ret2;
		}
	} else {
		// set clock OFF
		dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
				DSI_ALL_CLKS, DSI_CLK_OFF);
	}

	proc_buf[0] = 0;
	proc_buf_pos = 0;
	DRM_DEBUG_CONSOLE(ret ? "NG" : "OK");
	if (!ret) {
		for (i=0; i != dsi_req.size; ++i) {
			DRM_DEBUG_CONSOLE(",%02X", dsi_req.data[i]);
		}
	}
	return ret;
}

/**
	Check len and set len values and store data into temporary params
*/

static bool drm_proc_set_len_params(unsigned char **pparam, const char *buffer, char *buf, unsigned long count)
{

	unsigned long len;
	char *kbufex;
	unsigned char *param = NULL;
	int paramlen = 0;
	int i;

	// redefine length
	len = count - (DRM_LEN_ID + 1);
	if (len > (1024 * DRM_PARAM_MAX) - (DRM_LEN_ID + 1)) {
	   len = (1024 * DRM_PARAM_MAX) - (DRM_LEN_ID + 1);
	}
	// allocate kernel buffer
	kbufex = kmalloc(len, GFP_KERNEL);
	if (!kbufex) {
		return false;
	}
	// copy from user space to kernel space
	buffer += DRM_LEN_ID;
	if (copy_from_user(kbufex, buffer, len)) {
		kfree(kbufex);
		return false;
	}
	// define paramlength and allocate kernel memory
	paramlen = len / (DRM_LEN_PARAM / 2);
	param = kmalloc(paramlen, GFP_KERNEL);
	if (!param) {
		kfree(kbufex);
		return false;
	}
	/* Set into temp params */
	for (i = 0; i < paramlen; i++) {
		memcpy(buf, &(kbufex[i * (DRM_LEN_PARAM / 2)]), (DRM_LEN_PARAM / 2));
		buf[(DRM_LEN_PARAM / 2)] = '\0';
		param[i] = simple_strtol(buf, NULL, 16);
	}
	kfree(kbufex);
	*pparam = param;

	return true;
}


/**
	This function executes write process commands
*/
static int drm_proc_write_cases(unsigned char *param, struct drm_procfs *drm_pfs, struct dsi_display *dsi_display)
{
	// variables and initializations
	struct drm_dsi_cmd_req dsi_req;
	unsigned char buf[DRM_LCDDR_WRITE_BUF_MAX];
	int panel_mode = 0;
	int ret = 0, ret2;
	int i;
	int panel_buf_max = 0;

	// get panel mode of dsi_display
	panel_mode = dsi_display->panel->panel_mode;

	// do memory set
	memset(&dsi_req, 0x00, sizeof(dsi_req));
	memset(buf, 0x00, sizeof(buf));
	dsi_req.data = buf;

	// set dsi request type, size, address, mode
	if (drm_pfs->id == DRM_DEBUG_DSI_GEN_WRITE) {
		dsi_req.dtype = MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM;
	} else {
		dsi_req.dtype = MIPI_DSI_DCS_SHORT_WRITE;
	}
	dsi_req.size = param[0];
	dsi_req.addr = param[1];
	dsi_req.mode = param[2];

#ifdef CONFIG_ARCH_SARAH
	switch (drm_cmn_get_panel_type()) {
	case DRM_PANEL_STEIN:
		panel_buf_max = DRM_LCDDR_WRITE_BUF_MAX_STEIN;
		break;
	case DRM_PANEL_SINANJU:
		panel_buf_max = DRM_LCDDR_WRITE_BUF_MAX_SINANJU;
		break;
	default:
		pr_err("[%s]panel type is unknown\n", __func__);
		return ret;
	}
#else /* CONFIG_ARCH_SARAH */
	panel_buf_max = DRM_LCDDR_WRITE_BUF_MAX;
#endif /* CONFIG_ARCH_SARAH */

	if (dsi_req.size > panel_buf_max) {
		pr_err("[%s]size over. max=%d\n", __func__, panel_buf_max);
		return ret;
	}
	pr_debug(" Size    : %2d\n", dsi_req.size);
	pr_debug(" Address : %02Xh\n", dsi_req.addr);
	pr_debug(" Mode    : %02Xh\n", dsi_req.mode);

	// print messages for kernel to use
	for (i = 0; i < dsi_req.size; i++) {
		dsi_req.data[i] = param[i + 3];
		if ((i % 8) == 0) {
			printk("[%s]  WData    : ", __func__);
		}
		printk("%02X ", dsi_req.data[i]);
		if ((i % 8) == 7) {
			printk("\n");
		}
	}
	printk("\n");

	// stop video mode or set clock ON
	if (panel_mode == DSI_OP_VIDEO_MODE) {
		ret2 = drm_cmn_video_transfer_ctrl(dsi_display, false);
		if (ret2) {
			pr_err("%s: failed stop_video\n", __func__);
			return ret2;
		}
	} else {
		// set clock ON
		dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
				DSI_ALL_CLKS, DSI_CLK_ON);
	}

	//write
	ret = drm_panel_dsi_write_reg(&dsi_req);
	if (ret) {
		pr_err("%s: failed drm_panel_dsi_write_reg\n", __func__);
	}

	// start video mode or set clock OFF
	if (panel_mode == DSI_OP_VIDEO_MODE) {
		ret2 = drm_cmn_video_transfer_ctrl(dsi_display, true);
		if (ret2) {
			pr_err("%s: failed start_video\n", __func__);
			return ret2;
		}
	} else {
		// set clock OFF
		dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
				DSI_ALL_CLKS, DSI_CLK_OFF);
	}

	proc_buf[0] = 0;
	proc_buf_pos = 0;
	DRM_DEBUG_CONSOLE(ret ? "NG" : "OK");

	return ret;
}


/**
	this function process MFR case
*/
static int drm_proc_mfr_case(struct dsi_display *display, struct drm_procfs *drm_pfs)
{
	int ret;

	ret = drm_proc_mfr(display, drm_pfs->par[0]);
	proc_buf[0] = 0;
	proc_buf_pos = 0;
	DRM_DEBUG_CONSOLE(ret < 0 ? "NG" : "OK");

	return ret;
}

#ifdef DRM_FPS_LED_PANEL_SUPPORT
/**
	This function processes start / stop of led
*/
static int drm_proc_led_start_stop(struct drm_procfs *drm_pfs)
{
	int ret;

	if (drm_pfs->par[0]) {
		ret = drm_fps_led_start();
	} else {
		ret = drm_fps_led_stop();
	}

	return ret;
}
#endif /* DRM_FPS_LED_PANEL_SUPPORT */

/**
	This function processes flicker set
*/
static int drm_proc_set_flicker(struct drm_procfs *drm_pfs)
{
	struct dsi_display *pdisp = display_ctx;
	struct drm_flicker_param flicker_req;
	int ret;

	flicker_req.request = drm_pfs->par[0];
	flicker_req.vcom = drm_pfs->par[1];
	ret = drm_diag_set_flicker_param(pdisp, &flicker_req);
	proc_buf[0] = 0;
	proc_buf_pos = 0;
	DRM_DEBUG_CONSOLE(ret ? "NG" : "OK");

	return ret;
}

/**
	This funciton processes flicker get
*/
static int drm_proc_get_flicker(struct drm_procfs *drm_pfs)
{
	struct dsi_display *pdisp = display_ctx;
	struct drm_flicker_param flicker_req;
	int ret;

	flicker_req.request = drm_pfs->par[0];
	flicker_req.vcom = 0;
	ret = drm_diag_get_flicker_param(pdisp, &flicker_req);
	proc_buf[0] = 0;
	proc_buf_pos = 0;
	DRM_DEBUG_CONSOLE(ret ? "NG" : "OK");
	DRM_DEBUG_CONSOLE(",%02X", flicker_req.vcom);

	return ret;
}
//--------------------------------------------------------------------------

static int drm_proc_read(char *page, char **start, off_t offset, int count, int *eof, void *data)
{
	int len = 0;

	len += snprintf(page, count, "%s", proc_buf);
	proc_buf[0] = 0;
	proc_buf_pos = 0;

	return len;
}

static ssize_t drm_proc_file_read(struct file *file, char __user *buf, size_t nbytes, loff_t *ppos)
{
	char    *page;
	ssize_t retval=0;
	int eof=0;
	ssize_t n, count;
	char    *start;
	unsigned long long pos;

	/*
	 * Gaah, please just use "seq_file" instead. The legacy /proc
	 * interfaces cut loff_t down to off_t for reads, and ignore
	 * the offset entirely for writes..
	 */
	pos = *ppos;
	if (pos > MAX_NON_LFS) {
		return 0;
	}
	if (nbytes > (MAX_NON_LFS - pos)) {
		nbytes = MAX_NON_LFS - pos;
	}

	if (!(page = (char*) __get_free_page(GFP_KERNEL))) {
		return -ENOMEM;
	}

	while ((nbytes > 0) && !eof) {
		count = min_t(size_t, PROC_BLOCK_SIZE, nbytes);

		start = NULL;
		n = drm_proc_read(page, &start, *ppos,
				  count, &eof, NULL);

		if (n == 0) {    /* end of file */
			break;
		}
		if (n < 0) {  /* error */
			if (retval == 0)
				retval = n;
			break;
		}

		if (start == NULL) {
			if (n > PAGE_SIZE) {
				printk(KERN_ERR
					"proc_file_read: Apparent buffer overflow!\n");
				n = PAGE_SIZE;
			}
			n -= *ppos;
			if (n <= 0)
				break;
			if (n > count)
				n = count;
			start = page + *ppos;
		} else if (start < page) {
			if (n > PAGE_SIZE) {
				printk(KERN_ERR
					"proc_file_read: Apparent buffer overflow!\n");
				n = PAGE_SIZE;
			}
			if (n > count) {
				/*
				 * Don't reduce n because doing so might
				 * cut off part of a data block.
				 */
				printk(KERN_WARNING
					"proc_file_read: Read count exceeded\n");
			}
		} else /* start >= page */ {
			unsigned long startoff = (unsigned long)(start - page);
			if (n > (PAGE_SIZE - startoff)) {
				printk(KERN_ERR
					"proc_file_read: Apparent buffer overflow!\n");
				n = PAGE_SIZE - startoff;
			}
			if (n > count) {
				n = count;
			}
		}

		n -= copy_to_user(buf, start < page ? page : start, n);
		if (n == 0) {
			if (retval == 0) {
				retval = -EFAULT;
			}
			break;
		}

		*ppos += start < page ? (unsigned long)start : n;
		nbytes -= n;
		buf += n;
		retval += n;
	}
	free_page((unsigned long) page);

	return retval;
}

static ssize_t drm_proc_file_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	ssize_t rv = -EIO;

	if (!file || !file->private_data) {
		pr_err("%s: Invalid input file\n", __func__);
		return rv;
	}
	if (!buffer) {
		pr_err("%s: Invalid input buffer\n", __func__);
		return rv;
	}
	if (!ppos) {
		pr_err("%s: Invalid input ppos\n", __func__);
		return rv;
	}

	rv = drm_proc_write(file, buffer, count, NULL);

	return rv;
}

static int drm_panel_dsi_write_reg(struct drm_dsi_cmd_req *req)
{
	int retry = 5;
	int ret;
	char dtype;
	struct drm_dsi_cmd_desc cmd[1];
	char cmd_buf[DRM_LCDDR_WRITE_BUF_MAX + 1];
	int panel_buf_max = 0;

#ifdef CONFIG_ARCH_SARAH
	switch (drm_cmn_get_panel_type()) {
	case DRM_PANEL_STEIN:
		panel_buf_max = DRM_LCDDR_WRITE_BUF_MAX_STEIN;
		break;
	case DRM_PANEL_SINANJU:
		panel_buf_max = DRM_LCDDR_WRITE_BUF_MAX_SINANJU;
		break;
	default:
		pr_err("[%s]panel type is unknown\n", __func__);
		return ret;
	}
#else /* CONFIG_ARCH_SARAH */
	panel_buf_max = DRM_LCDDR_WRITE_BUF_MAX;
#endif /* CONFIG_ARCH_SARAH */

	if (req->size > panel_buf_max) {
		pr_err("size over, -EINVAL\n");
		return -EINVAL;
	}

	memset(cmd_buf, 0x00, sizeof(cmd_buf));
	cmd_buf[0] = req->addr;
	cmd_buf[1] = 0x00;
	memcpy(&cmd_buf[1], req->data, req->size);

	if (req->dtype != MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM) {
		if (req->size == 0) { /* 0 parameters */
			dtype = MIPI_DSI_DCS_SHORT_WRITE;
		} else if (req->size == 1) { /* 1 parameter */
			dtype = MIPI_DSI_DCS_SHORT_WRITE_PARAM;
		} else { /* Many parameters */
			dtype = MIPI_DSI_DCS_LONG_WRITE;
		}
	} else {
		if (req->size == 0) { /* 0 parameters */
			dtype = MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM;
		} else if (req->size == 1) { /* 1 parameter */
			dtype = MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM;
		} else { /* Many parameters */
			dtype = MIPI_DSI_GENERIC_LONG_WRITE;
		}
	}

	cmd[0].dtype = dtype;
	cmd[0].mode = req->mode;
	cmd[0].wait = 0x00;
	cmd[0].dlen = req->size + 1;
	cmd[0].payload = cmd_buf;

	for (; retry >= 0; retry--) {
		ret = drm_panel_mipi_dsi_cmds_tx(cmd, ARRAY_SIZE(cmd));
		if (ret == 0) {
			break;
		} else {
			pr_warn("drm_panel_mipi_dsi_cmds_tx() failure. ret=%d retry=%d\n", ret, retry);
		}
	}

	if (ret != 0) {
		pr_err("mipi_dsi_cmds_tx error\n");
		return -EIO;
	}

	return 0;
}

static int drm_panel_dsi_read_reg(struct drm_dsi_cmd_req *req)
{
	int retry = 5;

	int ret;
	char dtype;
	struct drm_dsi_cmd_desc cmd[1];
	char cmd_buf[2 + 1];
	int panel_buf_max = 0;

#ifdef CONFIG_ARCH_SARAH
	switch (drm_cmn_get_panel_type()) {
	case DRM_PANEL_STEIN:
		panel_buf_max = DRM_LCDDR_READ_BUF_MAX_STEIN;
		break;
	case DRM_PANEL_SINANJU:
		panel_buf_max = DRM_LCDDR_READ_BUF_MAX_SINANJU;
		break;
	default:
		pr_err("[%s]panel type is unknown\n", __func__);
		return ret;
	}
#else /* CONFIG_ARCH_SARAH */
	panel_buf_max = DRM_LCDDR_READ_BUF_MAX;
#endif /* CONFIG_ARCH_SARAH */

	pr_debug("in address:%02X, buf:%p, size:%d\n", req->addr, req->data, req->size);
	if ((req->size > panel_buf_max) || (req->size == 0)) {
		pr_err("size over, -EINVAL\n");
		return -EINVAL;
	}

	cmd_buf[0] = req->addr;
	cmd_buf[1] = 0x00;

	if (req->dtype != MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM) {
		dtype = MIPI_DSI_DCS_READ;
	} else {
		if (req->size == 1) { /* 1 parameters */
			dtype = MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM;
		} else { /* 2 paramters */
			dtype = MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM;
		}
	}
	cmd[0].dtype    = dtype;
	cmd[0].mode     = req->mode;
	cmd[0].wait     = 0x00;
	cmd[0].dlen     = 1;
	cmd[0].payload  = cmd_buf;

	for (; retry >= 0; retry--) {
		ret = drm_panel_mipi_dsi_cmds_rx((unsigned char *)req->data, cmd, req->size);
		if (ret == 0) {
			break;
		} else {
			pr_warn("drm_panel_mipi_dsi_cmds_rx() failure. ret=%d retry=%d\n", ret, retry);
		}
	}

	if (ret != 0) {
		pr_err("mipi_dsi_cmds_rx error\n");
		return -EIO;
	}

	pr_debug("out 0\n");
	return 0;
}

static int drm_panel_mipi_dsi_cmds_tx(struct drm_dsi_cmd_desc *cmds, int cnt)
{
	int ret;
	pr_debug("in cnt=%d\n", cnt);
	drm_panel_dsi_wlog(cmds, cnt);
	ret = drm_proc_host_dsi_tx(cmds, cnt );
	pr_debug("out ret=%d\n", ret);
	return ret;
}

static int drm_panel_mipi_dsi_cmds_rx(unsigned char *rbuf, struct drm_dsi_cmd_desc *cmds, unsigned char size)
{
	int ret;
	pr_debug("in size:%d\n", size);
	drm_panel_dsi_wlog(cmds, 1);
	ret = drm_proc_host_dsi_rx(cmds, rbuf, size);
	drm_panel_dsi_rlog(cmds->payload[0], (char *)rbuf, size);
	pr_debug("out ret:%d\n", ret);
	return ret;
}

static void drm_panel_dsi_wlog(struct drm_dsi_cmd_desc *cmds, int cmdslen)
{
#ifdef DRM_LOG_ENABLE
	char buf[128];
	char *pbuf;
	int cmdcnt;
	int arylen;
	int writelen;

	for (cmdcnt = 0; cmdcnt != cmdslen; cmdcnt++) {
		int i;
		char dtype = cmds[cmdcnt].dtype;
		short payloadlen = cmds[cmdcnt].dlen;
		unsigned char *payload = (unsigned char *)cmds[cmdcnt].payload;

		pbuf = buf;
		arylen = sizeof(buf) / sizeof(*buf);

		writelen = snprintf(pbuf, arylen, "dtype= %02X, ", dtype);
		arylen -= writelen;
		pbuf += writelen;

		writelen = snprintf(pbuf, arylen, "payload= %02X ", payload[0]);
		arylen -= writelen;
		pbuf += writelen;

		for (i = 1; i != payloadlen; ++i) {
			if ((!((i - 1) % 16)) && (i != 1)) {
				int spacecnt = 23;
				*pbuf = '\n';
				*(pbuf+1) = '\0';
				pr_info("%s", buf);

				arylen = sizeof(buf) / sizeof(*buf);
				pbuf = buf;
				memset(pbuf, ' ', spacecnt);
				arylen -= spacecnt;
				pbuf += spacecnt;
			}
			writelen = snprintf(pbuf, arylen, "%02X ", payload[i]);
			arylen -= writelen;
			pbuf += writelen;
		}

		*pbuf = '\n';
		*(pbuf+1) = '\0';
		pr_info("%s", buf);
	}
#endif /* DRM_LOG_ENABLE */
}

static void drm_panel_dsi_rlog(char addr, char *rbuf, int len)
{
#ifdef DRM_LOG_ENABLE
	char buf[128];
	char *pbuf;
	unsigned char *prbuf = (unsigned char *)rbuf;
	int arylen;
	int writelen;
	int i = 0;

	arylen = sizeof(buf) / sizeof(*buf);
	pbuf = buf;

	writelen = snprintf(pbuf, arylen, "addr = %02X, val = ", (unsigned char)addr);
	arylen -= writelen;
	pbuf += writelen;

	for (i = 0; i != len; ++i) {
		if ((!(i % 16)) && (i)) {
			int spacecnt = 17;
			*pbuf = '\n';
			*(pbuf+1) = '\0';
			pr_info("%s", buf);

			arylen = sizeof(buf) / sizeof(*buf);
			pbuf = buf;
			memset(pbuf, ' ', spacecnt);
			arylen -= spacecnt;
			pbuf += spacecnt;
		}
		writelen = snprintf(pbuf, arylen, "%02X ", *prbuf);
		arylen -= writelen;
		pbuf += writelen;
		prbuf++;
	}

	*pbuf = '\n';
	*(pbuf+1) = '\0';
	pr_info("%s", buf);
#endif /* DRM_LOG_ENABLE */
}

#ifdef DRM_FPS_LED_PANEL_SUPPORT
static int drm_led_set_color(struct shled_tri_led* led)
{
	pr_debug("%s: red=%d green=%d blue=%d\n",
			__func__, led->red, led->green, led->blue);
	led_trigger_event(led_fps_r_trigger, led->red);
	led_trigger_event(led_fps_g_trigger, led->green);
	led_trigger_event(led_fps_b_trigger, led->blue);
	return 0;
}

static void drm_fps_led_set_color(int state)
{
	struct shled_tri_led led;

	led.red = drm_fps_led_color[state][0];
	led.green = drm_fps_led_color[state][1];
	led.blue = drm_fps_led_color[state][2];
	drm_led_set_color(&led);

}

static void drm_fps_led_work(struct work_struct *work)
{
	int state = FPS_LED_STATE_NONE;
#if defined(CONFIG_SHARP_DRM_HR_VID)
	unsigned int mfr = 0;
	int vid_output_cnt  = 0;
	static int vid_output_cnt_saved = 0;
#endif
	pr_debug("%s: in\n", __func__);

	if (!drm_fps_led_ctx.enable || drm_fps_led_ctx.suspend) {
		pr_debug("out2 enable=%d suspend=%d\n",
				drm_fps_led_ctx.enable,
				drm_fps_led_ctx.suspend);
		return;
	}

	queue_delayed_work(
			drm_fps_led_ctx.workq,
			&drm_fps_led_ctx.work,
			usecs_to_jiffies(drm_fps_led_ctx.interval));

#if !defined(CONFIG_SHARP_DRM_HR_VID)
	if (drm_fps_led_ctx.panel_on) {
		state = drm_fps_set_led_cmd();
		pr_debug("state=%s\n", drm_fps_led_state_str[state]);
	}
#else

	if (drm_fps_led_ctx.panel_on) {
		vid_output_cnt = drm_mfr_get_mfr();
		mfr = vid_output_cnt  - vid_output_cnt_saved;
		vid_output_cnt_saved = vid_output_cnt ;

		state = drm_mfr_get_led_state(mfr);
		pr_debug("%s: mfr=%d state=%s vid_output_cnt=%d vid_output_cnt_saved=%d\n",
				__func__, mfr, drm_fps_led_state_str[state], vid_output_cnt, vid_output_cnt_saved);
	}

#endif
	drm_fps_led_set_color(state);


	pr_debug("%s: out\n", __func__);
}

static int drm_fps_led_start(void)
{
	struct dsi_display *display = display_ctx;
	struct drm_panel *drm_panel;

	pr_debug("%s: in\n", __func__);

	drm_panel = dsi_display_get_drm_panel(display);

	if (drm_fps_led_ctx.enable) {
		pr_debug("%s: out2 enable=%d\n", __func__, drm_fps_led_ctx.enable);
		return -EINVAL;
	}

	if (!drm_fps_led_ctx.workq) {
		pr_err("%s: workq is NULL.\n", __func__);
		return -EINVAL;
	}


	drm_fps_led_ctx.enable = true;
	drm_fps_led_ctx.interval = FPS_LED_INTERVAL;

	led_trigger_register_simple("led-fps-r-trigger", &led_fps_r_trigger);
	led_trigger_register_simple("led-fps-g-trigger", &led_fps_g_trigger);
	led_trigger_register_simple("led-fps-b-trigger", &led_fps_b_trigger);
	drm_fps_led_set_color(FPS_LED_STATE_NONE);

	drm_fps_led_ctx.panel_on = true;

	drm_fps_led_ctx.max_frame_rate = display->panel->cur_mode->timing.refresh_rate;
	pr_debug("%s: max_frame_rate=%d\n", __func__, drm_fps_led_ctx.max_frame_rate);
	drm_notif.notifier_call = drm_notifier_callback;
	drm_panel_notifier_register(drm_panel,&drm_notif);

	if (drm_fps_led_ctx.panel_on) {
		drm_fps_led_ctx.suspend = false;
		queue_delayed_work(
				drm_fps_led_ctx.workq,
				&drm_fps_led_ctx.work,
				usecs_to_jiffies(drm_fps_led_ctx.interval));
	} else {
		drm_fps_led_ctx.suspend = true;
	}

	pr_debug("%s: out\n", __func__);

	return 0;
}

static int drm_fps_led_stop(void)
{
	struct dsi_display *display = display_ctx;
	struct drm_panel *drm_panel;

	pr_debug("%s: in\n", __func__);

	drm_panel = dsi_display_get_drm_panel(display);

	if (!drm_fps_led_ctx.enable) {
		pr_debug("%s:out2 enable=%d\n", __func__, drm_fps_led_ctx.enable);
		return -EINVAL;
	}

	if (!drm_fps_led_ctx.workq) {
		pr_err("%s: workq is NULL.\n", __func__);
		return -EINVAL;
	}

	drm_fps_led_ctx.enable = false;

	drm_panel_notifier_unregister(drm_panel,&drm_notif);

	cancel_delayed_work_sync(&drm_fps_led_ctx.work);

	drm_fps_led_set_color(FPS_LED_STATE_NONE);

	led_trigger_unregister_simple(led_fps_r_trigger);
	led_trigger_unregister_simple(led_fps_g_trigger);
	led_trigger_unregister_simple(led_fps_b_trigger);

	pr_debug("%s: out\n", __func__);

	return 0;
}
#if defined(CONFIG_SHARP_DRM_HR_VID)
static int drm_mfr_get_led_state(int mfr)
{

	if (mfr > 65) {
		return FPS_LED_STATE_120HZ;
	}
	if ((mfr > 45) && (mfr <= 65)) {
		return FPS_LED_STATE_60HZ;
	}
	if ((mfr > 20) && (mfr <= 45)) {
		return FPS_LED_STATE_30HZ;
	}
	if ((mfr > 1) && (mfr <= 20)) {
		return FPS_LED_STATE_15HZ;
	}
	if (mfr == 1) {
		return FPS_LED_STATE_1HZ;
	}
	return FPS_LED_STATE_120HZ;
}
#endif
void drm_fps_led_resume(void)
{
	pr_debug("%s: in\n", __func__);

	if (!drm_fps_led_ctx.enable || !drm_fps_led_ctx.suspend) {
		pr_debug("%s:out2 enable=%d suspend=%d\n", __func__,
				drm_fps_led_ctx.enable,
				drm_fps_led_ctx.suspend);
		return;
	}

	if (!drm_fps_led_ctx.workq) {
		pr_err("%s: workq is NULL.\n", __func__);
		return;
	}

	drm_fps_led_ctx.suspend = false;
	queue_delayed_work(
			drm_fps_led_ctx.workq,
			&drm_fps_led_ctx.work,
			usecs_to_jiffies(drm_fps_led_ctx.interval * 3));

	pr_debug("%s: out\n", __func__);

}
void drm_fps_led_suspend(void)
{
	pr_debug("%s: in\n", __func__);

	if (!drm_fps_led_ctx.enable || drm_fps_led_ctx.suspend) {
		pr_debug("%s:out2 enable=%d suspend=%d\n", __func__,
				drm_fps_led_ctx.enable,
				drm_fps_led_ctx.suspend);
		return;
	}

	if (!drm_fps_led_ctx.workq) {
		pr_err("%s: workq is NULL.\n", __func__);
		return;
	}

	drm_fps_led_ctx.suspend = true;
	cancel_delayed_work_sync(&drm_fps_led_ctx.work);

	drm_fps_led_set_color(FPS_LED_STATE_NONE);

	pr_debug("%s: out\n", __func__);
}
int drm_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	struct drm_panel_notifier *evdata = data;
	int *blank;

	pr_debug("%s: in\n", __func__);

	if (evdata && evdata->data) {
		blank = evdata->data;
		pr_debug("%s:drm request. event=%ld, blank=%d\n",__func__, event, *blank);
		switch (event) {
		case DRM_PANEL_EARLY_EVENT_BLANK:
			if (*blank == DRM_PANEL_BLANK_POWERDOWN) {
				if (drm_fps_led_ctx.panel_on) {
					drm_fps_led_ctx.panel_on = false;
					drm_fps_led_suspend();
				}
			}
		case DRM_PANEL_EVENT_BLANK:
			if (*blank == DRM_PANEL_BLANK_UNBLANK) {
				if (!drm_fps_led_ctx.panel_on) {
					drm_fps_led_ctx.panel_on = true;
					drm_fps_led_resume();
				}
			}
		}
	}
	pr_debug("%s:out. event=%ld", __func__, event);
	return 0;
}
#if !defined(CONFIG_SHARP_DRM_HR_VID)
static int drm_fps_set_led_cmd()
{
	int bit_cnt = 0;
	unsigned char fps_reg = 0;
	int state = 0;
	int ret = 0;

	pr_debug("%s: in\n", __func__);
	ret = drm_fps_led_read_reg(&fps_reg);
	if (ret == 0) {
		bit_cnt = hweight8(fps_reg);
		pr_debug("%s: .max_frame_rate=%d\n", __func__,drm_fps_led_ctx.max_frame_rate);
		if (drm_fps_led_ctx.max_frame_rate == 120) {
			if (bit_cnt >= 8) {
				state = FPS_LED_STATE_120HZ;
			} else if (bit_cnt >= 4) {
				state = FPS_LED_STATE_60HZ;
			} else if (bit_cnt >= 2) {
				state = FPS_LED_STATE_30HZ;
			} else if (bit_cnt >= 1) {
				state = FPS_LED_STATE_15HZ;
			} else {
				state = FPS_LED_STATE_1HZ;
			}
		} else {
			if (bit_cnt >= 8) {
				state = FPS_LED_STATE_60HZ;
			} else if (bit_cnt >= 4) {
				state = FPS_LED_STATE_30HZ;
			} else if (bit_cnt >= 2) {
				state = FPS_LED_STATE_15HZ;
			} else if (bit_cnt >= 1) {
				state = FPS_LED_STATE_1HZ;
			} else {
				state = FPS_LED_STATE_1HZ;
			}
		}
	}
	pr_debug("%s: out state = %d\n", __func__, state);
	return state;
}
static int drm_fps_led_read_reg(unsigned char *fps_reg)
{
	int ret = 0;
#ifdef USES_PANEL_RAVEN
	char read_buf[] = {0x00, 0x00};
#else
	char read_buf[] = {0x00};
#endif
	unsigned char read_cmd_addr[] = {ADDR_FPS_REG};
	struct dsi_display *pdisp = display_ctx;
	read_buf[0]=0x00;

	if (!pdisp) {
		pr_err("%s: dsi_display is NULL.\n", __func__);
		return -EINVAL;
	}
	/* switch_panel_page */
	drm_cmn_panel_dcs_write1(pdisp, ADDR_PAGE, PAGE_FPS_REG);

	/* read fps reg */
	ret = drm_cmn_panel_dcs_read(pdisp,read_cmd_addr[0],sizeof(read_buf),&read_buf[0]);
	if (ret >= 0) {
		*fps_reg = read_buf[sizeof(read_buf)-1];
		pr_debug("%s: fps read succsess. fps=0x%x ret=%d\n", __func__, *fps_reg, ret);
		ret = 0;
	} else {
		pr_err("%s: fps read err. ret=%d\n", __func__, ret);
	}

	/* switch_panel_page */
	drm_cmn_panel_dcs_write1(pdisp, ADDR_PAGE, DEFAULT_PAGE_FPS_REG);

	return ret;
}
#endif
#endif /* DRM_FPS_LED_PANEL_SUPPORT */

static int drm_proc_mfr(struct dsi_display *display, int value)
{
	int ret = 0;

#if defined(USES_PANEL_ELSA)
	ret = drm_proc_mfr_elsa(display, value);
#elif defined(USES_PANEL_SVEN)
	ret = drm_proc_mfr_sven(display, value);
#elif defined(USES_PANEL_RAVEN)
	ret = drm_proc_mfr_raven(display, value);
#else  /* USES_PANEL_XXX */
	ret = drm_proc_mfr_rosetta(display, value);
#endif  /* USES_PANEL_XXX */

	return ret;
}

#if defined(USES_PANEL_ELSA)
static int drm_proc_mfr_elsa(struct dsi_display *display, int value)
{
	int ret = 0;
	int cmd_cnt;
	struct dsi_cmd_desc *cmds;
	static unsigned char mfr_proc_cmd_addr_value[][2] = {
		{0xFE, 0xD2},
		{0x33, 0x97},
		{0xFE, 0xD0},
		{0x96, 0x01},
		{0xFB, 0xAA},
		{0xFE, 0x40},
		{0x6B, 0x31},
		{0x77, 0x41},
		{0xFE, 0x60},
		{0x09, 0xC0},
		{0x12, 0xC0},
		{0xC6, 0x06},
		{0xC9, 0xF3},
		{0xFE, 0xD0},
		{0x51, 0xB5},
		{0x52, 0x0E},
		{0xFB, 0xAA},
		{0xFE, 0xD2},
		{0x33, 0x9F},
		{0xFE, 0xD0},
		{0x96, 0x02},
		{0xFB, 0xAA},
		{0xFE, 0x00},
	};
	static struct dsi_cmd_desc mfr_cmd[] = {
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[ 0], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[ 1], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[ 2], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[ 3], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[ 4], 0, 0}, 1, 1},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[ 5], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[ 6], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[ 7], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[ 8], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[ 9], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[10], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[11], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[12], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[13], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[14], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[15], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[16], 0, 0}, 1, 18},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[17], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[18], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[19], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[20], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[21], 0, 0}, 1, 1},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[22], 0, 0}, 1, 0},
	};

	switch(value) {
	case 120:
		mfr_proc_cmd_addr_value[ 7][1] = 0x00;
		mfr_proc_cmd_addr_value[ 9][1] = 0xC8;
		mfr_proc_cmd_addr_value[10][1] = 0xC8;
		mfr_proc_cmd_addr_value[11][1] = 0x00;
		break;
	case 60:
		mfr_proc_cmd_addr_value[ 7][1] = 0x41;
		mfr_proc_cmd_addr_value[ 9][1] = 0xC0;
		mfr_proc_cmd_addr_value[10][1] = 0xC0;
		mfr_proc_cmd_addr_value[11][1] = 0x06;
		break;
	default:
		pr_err("%s:error.not support mfr=%d\n", __func__, value);
		return -EPERM;
	}

	cmd_cnt = ARRAY_SIZE(mfr_cmd);
	cmds = mfr_cmd;

	ret = drm_cmn_panel_cmds_transfer(display, cmds, cmd_cnt);
	if (ret) {
		pr_err("%s:failed to set cmds, ret=%d\n", __func__, ret);
	}

	pr_debug("%s:end mfr=%d ret=%d", __func__, value, ret);
	return ret;
}
#elif defined(USES_PANEL_SVEN)
static int drm_proc_mfr_sven(struct dsi_display *display, int value)
{
	int ret = 0;
	int cmd_cnt;
	struct dsi_cmd_desc *cmds;
	static unsigned char mfr_proc_cmd_addr_value[][2] = {
		{0xFE, 0x84},
		{0xE0, 0x00},
		{0xFE, 0x00},
	};
	static struct dsi_cmd_desc mfr_cmd[] = {
		{{0, MIPI_DSI_GENERIC_LONG_WRITE, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[0], 0, 0}, 0, 0},
		{{0, MIPI_DSI_GENERIC_LONG_WRITE, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[1], 0, 0}, 0, 0},
		{{0, MIPI_DSI_GENERIC_LONG_WRITE, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[2], 0, 0}, 1, 0},
	};

	switch(value) {
	case 60:
		mfr_proc_cmd_addr_value[1][1] = 0x00;
		break;
	case 30:
		mfr_proc_cmd_addr_value[1][1] = 0x44;
		break;
	default:
		pr_err("%s:error.not support mfr=%d\n", __func__, value);
		return -EPERM;
	}

	cmd_cnt = ARRAY_SIZE(mfr_cmd);
	cmds = mfr_cmd;

	ret = drm_cmn_panel_cmds_transfer(display, cmds, cmd_cnt);
	if (ret) {
		pr_err("%s:failed to set cmds, ret=%d\n", __func__, ret);
	}

	pr_debug("%s:end mfr=%d ret=%d", __func__, value, ret);
	return ret;
}
#elif defined(USES_PANEL_RAVEN)
static int drm_proc_mfr_raven(struct dsi_display *display, int value)
{
	int ret = 0;
	int cmd_cnt;
	struct dsi_cmd_desc *cmds;
	static unsigned char mfr;
	static unsigned char rfdet;
	static unsigned char mfr_proc_cmd_addr_value[][26] = {
		{0xDE, 0x04},
		{0xB0, 0x00},
		{0xEB, 0x00,
		       0x79,
		       0x00,
		       0x79,
		       0x00,
		       0x03,
		       0x08,
		       0x00,
		       0x00,
		       0x00,
		       0x00,
		       0x00,
		       0x90,
		       0x00,
		       0x02,
		       0x0B,
		       0x00,
		       0x38,
		       0x00,
		       0x00,
		       0x00,
		       0x00,
		       0x00,
		       0x00,
		       0x00},
		{0xDE, 0x00}
	};
	static struct dsi_cmd_desc mfr_cmd[] = {
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[0], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[1], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, MIPI_DSI_MSG_USE_LPM,
			0, 0, 26, mfr_proc_cmd_addr_value[2], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_SHORT_WRITE_PARAM, MIPI_DSI_MSG_USE_LPM,
			0, 0,  2, mfr_proc_cmd_addr_value[3], 0, 0}, 1, 0},
	};

	switch(value) {
	case 120:
		mfr = 0x00;
		rfdet = 0x90;
		break;
	case 60:
		mfr = 0x01;
		rfdet = 0x90;
		break;
	case 30:
		mfr = 0x03;
		rfdet = 0x90;
		break;
	case 1:
		mfr = 0x77;
		rfdet = 0xB7;
		break;
	default:
		pr_err("%s:error.not support mfr=%d\n", __func__, value);
		return -EPERM;
	}

	mfr_proc_cmd_addr_value[2][6] = mfr;
	mfr_proc_cmd_addr_value[2][13] = rfdet;

	cmd_cnt = ARRAY_SIZE(mfr_cmd);
	cmds = mfr_cmd;

	ret = drm_cmn_panel_cmds_transfer(display, cmds, cmd_cnt);
	if (ret) {
		pr_err("%s:failed to set cmds, ret=%d\n", __func__, ret);
	}

	pr_debug("%s:end mfr=%d ret=%d", __func__, value, ret);
	return ret;
}
#else  /* USES_PANEL_XXX */
static int drm_proc_mfr_rosetta(struct dsi_display *display, int value)
{
	int ret = 0;
	int cmd_cnt;
	struct dsi_cmd_desc *cmds;
	static unsigned char mfr[] = {0x62, 0x00};
	static unsigned char refdet[] = {0x66, 0x20};
	static unsigned char mfr_proc_cmd_addr_value[][2] = {
		{0xFF, 0x26},
		{0x25, 0x78},
		{0x27, 0x78},
		{0x28, 0x25},
		{0x36, 0x08},
		{0x60, 0x00},
		{0x61, 0x00},
		{0x65, 0x9F},
		{0x41, 0x00},
		{0x42, 0x00},
		{0x43, 0x00},
		{0x44, 0x00},
		{0x45, 0x00},
		{0x46, 0x00},
		{0x47, 0x00},
		{0x48, 0x00},
		{0x49, 0x00},
		{0x4A, 0x00},
		{0x4B, 0x00},
		{0x4C, 0x00},
		{0x4D, 0x00},
		{0x4E, 0x00},
		{0x4F, 0x00},
		{0x50, 0x00},
		{0x51, 0x00},
		{0x40, 0x00},
		{0x31, 0x00},
		{0x32, 0x00},
		{0x33, 0x00},
		{0x34, 0x00},
		{0x35, 0x00},
		{0x30, 0x00},
		{0xFF, 0x10},
		{0xBC, 0x0A},
	};
	static struct dsi_cmd_desc mfr_cmd[] = {
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[ 0], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[ 1], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[ 2], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[ 3], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[ 4], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[ 5], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[ 6], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr                        , 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[ 7], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, refdet                     , 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[ 8], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[ 9], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[10], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[11], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[12], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[13], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[14], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[15], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[16], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[17], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[18], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[19], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[20], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[21], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[22], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[23], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[24], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[25], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[26], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[27], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[28], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[29], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[30], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[31], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[32], 0, 0}, 0, 0},
		{{0, MIPI_DSI_DCS_LONG_WRITE, 0, 0, 0, 2, mfr_proc_cmd_addr_value[33], 0, 0}, 1, 0},
	};

	switch(value) {
	case 120:
		mfr[1] = 0x00;
		refdet[1] = 0x20;
		break;
	case 60:
		mfr[1] = 0x01;
		refdet[1] = 0x21;
		break;
	case 40:
		mfr[1] = 0x02;
		refdet[1] = 0x22;
		break;
	case 30:
		mfr[1] = 0x03;
		refdet[1] = 0x23;
		break;
	case 20:
		mfr[1] = 0x05;
		refdet[1] = 0x25;
		break;
	case 15:
		mfr[1] = 0x07;
		refdet[1] = 0x37;
		break;
	case 10:
		mfr[1] = 0x0B;
		refdet[1] = 0x37;
		break;
	case 1:
		mfr[1] = 0x77;
		refdet[1] = 0x37;
		break;
	default:
		pr_err("%s:error.not support mfr=%d\n", __func__, value);
		return -EPERM;
	}

	cmd_cnt = ARRAY_SIZE(mfr_cmd);
	cmds = mfr_cmd;

	ret = drm_cmn_panel_cmds_transfer(display, cmds, cmd_cnt);
	if (ret) {
		pr_err("%s:failed to set cmds, ret=%d\n", __func__, ret);
	}

	pr_debug("%s:end mfr=%d ret=%d", __func__, value, ret);
	return ret;
}
#endif  /* USES_PANEL_XXX */

#ifdef DRM_BIAS_CALC_SUPPORT
/**
	This function processes On-bias Voltage calculation
*/
static int drm_proc_bias_calc(unsigned char *param, struct drm_procfs *drm_pfs)
{
	int ret = 0;
	struct drm_bias_calc_param bias_param;

	memset(&bias_param, 0, sizeof(bias_param));

	bias_param.x1      = (s16)(param[0]<<8)+param[1];
	bias_param.x2      = (s16)(param[2]<<8)+param[3];
	bias_param.x3      = (s16)(param[4]<<8)+param[5];
	bias_param.vob_otp = (s16)(param[6]<<8)+param[7];
	bias_param.gradation = (s16)(param[8]<<8)+param[9];

	ret = drm_bias_bias_calc_proc(&bias_param);

	return ret;
}
#endif /* DRM_BIAS_CALC_SUPPORT */
