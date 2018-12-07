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

#include <linux/bug.h>
#include <linux/comma_board.h>
#include <linux/kernel.h>
#include <linux/string.h>

static enum comma_id board_id;

enum comma_id comma_board_id(void)
{
	return board_id;
}

static int __init comma_board_init(void)
{
	const char *cmdline = saved_command_line;

	if (strstr(cmdline, "androidboot.baseband=apq"))
		board_id = COMMA_BOARD_CICI;
	else if (strstr(cmdline, "android.letv.product"))
		board_id = COMMA_BOARD_LEECO;
	else if (strstr(cmdline, "androidboot.project_name"))
		board_id = COMMA_BOARD_ONEPLUS;
	else
		WARN(1, "Unknown Comma board!");

	return 0;
}
early_initcall(comma_board_init);
