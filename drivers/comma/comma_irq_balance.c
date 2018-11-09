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
#include <linux/interrupt.h>

static unsigned int leeco_intense_irqs[] = {
	6,
	13,
	25,
	26,
	33,
	35,
	166,
	167,
	168,
	175,
	238,
	272,
	273,
	274,
	280,
	281,
	371,
	507,
	532,
	822
};

static unsigned int oneplus_intense_irqs[] = {
	6,
	13,
	15,
	16,
	25,
	26,
	33,
	35,
	166,
	167,
	168,
	175,
	238,
	272,
	273,
	274,
	280,
	281,
	371,
	507,
	532,
	825
};

static atomic_t cpu_idx = ATOMIC_INIT(0);

void comma_balance_irq(unsigned int irq)
{
	const unsigned int *intense_irqs;
	const struct cpumask *set;
	size_t i, intense_irq_cnt;

	switch (comma_board_id()) {
	case COMMA_BOARD_LEECO:
		intense_irqs = leeco_intense_irqs;
		intense_irq_cnt = ARRAY_SIZE(leeco_intense_irqs);
		break;
	case COMMA_BOARD_ONEPLUS:
		intense_irqs = oneplus_intense_irqs;
		intense_irq_cnt = ARRAY_SIZE(oneplus_intense_irqs);
		break;
	default:
		return;
	}

	for (i = 0; i < intense_irq_cnt; i++) {
		if (intense_irqs[i] == irq) {
			set = cpumask_of(atomic_inc_return(&cpu_idx) % NR_CPUS);
			irq_set_affinity(irq, set);
			break;
		}
	}
}
