/**
 * @file kernel/time.c
 *
 * Copyright (C) 2025  Dylan Parks
 *
 * This file is part of HeliOS
 *
 * HeliOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "kernel/time.h"
#include "kernel/panic.h"

static struct clock tsc = { 0 };

void clock_init(u64 (*read)(void), u64 counter_hz)
{
	if (counter_hz == 0 || !read) {
		panic("Invalid arguments to clock_init");
	}

	tsc.base_counter = read();
	tsc.read = read;

	tsc.counter_hz = counter_hz;
	tsc.count2ns_shift = 32;
	tsc.count2ns_mul =
		(u64)(((unsigned __int128)1000000000ULL << tsc.count2ns_shift) /
		      tsc.counter_hz);
	tsc.base_ns = 0;
}

[[gnu::hot]]
u64 clock_now_ns(void)
{
	u64 t = tsc.read();
	u64 dt = t - tsc.base_counter;
	unsigned __int128 acc = (unsigned __int128)dt * tsc.count2ns_mul;
	u64 ns = tsc.base_ns + (u64)(acc >> tsc.count2ns_shift);
	return ns;
}

u64 clock_now_ms(void)
{
	return clock_now_ns() / 1000000ULL;
}
