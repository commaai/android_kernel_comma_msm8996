/*
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

#include <linux/comma_board.h>
#include <linux/kernel.h>
#include <linux/reboot.h>

/*
 * OnePlus needs to be reset when booting from charger mode, otherwise
 * OpenPilot won't start and the device will remain stuck on the spinning
 * Comma logo.
 */
static int __init charger_mode_reset(void)
{
	if (comma_board_id() != COMMA_BOARD_ONEPLUS)
		return 0;

	if (strstr(saved_command_line, "androidboot.mode=charger"))
		machine_restart("normal");

	return 0;
}
device_initcall_sync(charger_mode_reset);
