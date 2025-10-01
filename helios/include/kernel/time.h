/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "kernel/types.h"

struct clock {
	u64 counter_hz;
	u64 base_counter;
	u64 base_ns;
	u64 count2ns_mul;
	u8 count2ns_shift;

	u64 (*read)(void);
};

void clock_init(u64 (*read)(void), u64 counter_hz);

u64 clock_now_ns(void);
u64 clock_now_ms(void);
