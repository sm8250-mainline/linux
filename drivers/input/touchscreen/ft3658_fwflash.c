/*
 *
 * FocalTech fts TouchScreen driver.
 *
 * Copyright (c) 2012-2020, Focaltech Ltd. All rights reserved.
 * Copyright (C) 2021 XiaoMi, Inc.
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

/*****************************************************************************
*
* File Name: focaltech_upgrade_ft5652.c
*
* Author: Focaltech Driver Team
*
* Created: 2019-11-20
*
* Abstract:
*
* Reference:
*
*****************************************************************************/
/*****************************************************************************
* 1.Included header files
*****************************************************************************/
#include "ft3658_flasher.h"
#include <linux/kernel.h>
#define FTS_DELAY_ERASE_PAGE_2K         80
#define FTS_SIZE_PAGE_2K                2048

/************************************************************************
* Name: fts_ft5652_upgrade
* Brief:
* Input:
* Output:
* Return: return 0 if success, otherwise return error code
***********************************************************************/
static int fts_ft5652_upgrade(u8 *buf, u32 len)
{
	int ret = 0;
	u32 start_addr = 0;
	u8 cmd[4] = { 0 };
	u32 delay = 0;
	int ecc_in_host = 0;
	int ecc_in_tp = 0;

	if ((NULL == buf) || (len < FTS_MIN_LEN)) {
		printk(KERN_CRIT "focaltech:buffer/len(%x) is invalid", len);
		return -EINVAL;
	}

	/* enter into upgrade environment */
	ret = fts_fwupg_enter_into_boot();
	if (ret < 0) {
		printk(KERN_CRIT "focaltech:enter into pramboot/bootloader fail,ret=%d", ret);
		goto fw_reset;
	}

	cmd[0] = FTS_CMD_APP_DATA_LEN_INCELL;
	cmd[1] = BYTE_OFF_16(len);
	cmd[2] = BYTE_OFF_8(len);
	cmd[3] = BYTE_OFF_0(len);
	ret = fts_write(cmd, FTS_CMD_DATA_LEN_LEN);
	if (ret < 0) {
		printk(KERN_CRIT "focaltech:data len cmd write fail");
		goto fw_reset;
	}

	cmd[0] = FTS_CMD_FLASH_MODE;
	cmd[1] = FLASH_MODE_UPGRADE_VALUE;
	ret = fts_write(cmd, 2);
	if (ret < 0) {
		printk(KERN_CRIT "focaltech:upgrade mode(09) cmd write fail");
		goto fw_reset;
	}

	delay = FTS_DELAY_ERASE_PAGE_2K * (len / FTS_SIZE_PAGE_2K);
	ret = fts_fwupg_erase(delay);
	if (ret < 0) {
		printk(KERN_CRIT "focaltech:erase cmd write fail");
		goto fw_reset;
	}

	/* write app */
	start_addr = upgrade_func_ft5652.appoff;
	ecc_in_host = fts_flash_write_buf(start_addr, buf, len, 1);
	if (ecc_in_host < 0 ) {
		printk(KERN_CRIT "focaltech:flash write fail");
		goto fw_reset;
	}

	/* ecc */
	ecc_in_tp = fts_fwupg_ecc_cal(start_addr, len);
	if (ecc_in_tp < 0 ) {
		printk(KERN_CRIT "focaltech:ecc read fail");
		goto fw_reset;
	}

	printk(KERN_INFO "ecc in tp:%x, host:%x", ecc_in_tp, ecc_in_host);
	if (ecc_in_tp != ecc_in_host) {
		printk(KERN_CRIT "focaltech:ecc check fail");
		goto fw_reset;
	}

	printk(KERN_INFO "ft3650: Firmware upgrade complete. Rebooting into normal mode.");
	ret = fts_fwupg_reset_in_boot();
	if (ret < 0) {
		printk(KERN_CRIT "ft3650: Reboot to normal mode failed. Chip is in unstable state.");
	}

	msleep(200);
	return 0;

fw_reset:
	printk(KERN_INFO "upgrade fail, reset to normal boot");
	ret = fts_fwupg_reset_in_boot();
	if (ret < 0) {
		printk(KERN_CRIT "focaltech:reset to normal boot fail");
	}
	return -EIO;
}

struct upgrade_func upgrade_func_ft5652 = {
	.ctype = {0x88},
	.fwveroff = 0x010E,
	.fwcfgoff = 0x1F80,
	.appoff = 0x0000,
	.upgspec_version = UPGRADE_SPEC_V_1_0,
	.pramboot_supported = false,
	.hid_supported = true,
	.upgrade = fts_ft5652_upgrade,
};
