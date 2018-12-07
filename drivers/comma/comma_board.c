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
#include <linux/of_fdt.h>

static enum comma_id board_id;

enum comma_id comma_board_id(void)
{
	return board_id;
}

static int __init comma_board_init(void)
{
	const char *board_name;
	unsigned long dt_root;

	dt_root = of_get_flat_dt_root();
	board_name = of_get_flat_dt_prop(dt_root, "comma,board-name", NULL);
	if (!board_name) {
		WARN(1, "Unknown Comma board! Please set comma,board-name");
		return -ENODEV;
	}

	if (!strcmp(board_name, "cici"))
		board_id = COMMA_BOARD_CICI;
	else if (!strcmp(board_name, "leeco"))
		board_id = COMMA_BOARD_LEECO;
	else if (!strcmp(board_name, "oneplus"))
		board_id = COMMA_BOARD_ONEPLUS;

	return 0;
}
early_initcall(comma_board_init);
